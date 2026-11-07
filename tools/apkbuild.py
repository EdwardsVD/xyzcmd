#!/usr/bin/env python3
"""apkbuild.py - pembuat APK tanpa Android SDK (AXML manifest, zip, signature v2).

Dipakai build_apk.sh:
    python tools/apkbuild.py build --out output/nusa-rush-s3-debug.apk \
        --lib build/libnusa.so --assets assets_pack

    python tools/apkbuild.py verify output/nusa-rush-s3-debug.apk

Kenapa manual? Di sandbox ini dl.google.com (SDK/NDK resmi) diblokir, jadi tidak ada
aapt2/apksigner/zipalign. Semua langkah (manifest biner, zip ter-align, skema tanda
tangan APK v2) diimplementasikan di sini dengan pustaka standar + `cryptography`.
"""
import io
import os
import struct
import sys
import zipfile
import hashlib
import subprocess

# ---------------------------------------------------------------- AXML (manifest)

ANDROID_NS = "http://schemas.android.com/apk/res/android"

RES_IDS = {
    # android.R.attr - nilai ID stabil di semua versi Android
    "theme": 0x01010000, "label": 0x01010001, "icon": 0x01010002,
    "name": 0x01010003, "permission": 0x01010006, "debuggable": 0x0101000f,
    "screenOrientation": 0x0101001e, "configChanges": 0x0101001f,
    "minSdkVersion": 0x0101020c, "versionCode": 0x0101021b, "versionName": 0x0101021c,
    "targetSdkVersion": 0x01010270, "maxSdkVersion": 0x01010271,
    "glEsVersion": 0x01010281, "hardwareAccelerated": 0x010102d3,
    "resizeableActivity": 0x010104f7, "exported": 0x01010010,
    "allowBackup": 0x01010280, "value": 0x01010024, "required": 0x0101028e,
    "hasCode": 0x0101000c, "usesCleartextTraffic": 0x010104ec,
    "extractNativeLibs": 0x010104ea,
}

TYPE_REF, TYPE_STRING, TYPE_INT_DEC, TYPE_INT_HEX, TYPE_BOOL = 0x01, 0x03, 0x10, 0x11, 0x12


def _s(v):
    return ("s", v)


def _i(v):
    return ("i", v)


def _b(v):
    return ("b", bool(v))


class AXML:
    def __init__(self):
        self.strings = []
        self._smap = {}

    def sidx(self, s):
        if s not in self._smap:
            self._smap[s] = len(self.strings)
            self.strings.append(s)
        return self._smap[s]


def _pool(strings, utf8=False):
    """RES_STRING_POOL_TYPE.

    Tata letak: header(28) | offsets[count] | string data | padding.
    BUG yang diperbaiki: versi sebelumnya menulis offsets dan data pada buffer
    terpisah tetapi `stringsStart` (28) tidak memperhitungkan tabel offsets,
    sehingga parser Android membaca tabel offset sebagai isi string. Akibatnya
    nama tag/atribut jadi sampah -> PackageParser melempar error dan HP
    menampilkan "ada masalah saat mengurai paket".
    """
    data = bytearray()
    offsets = []
    for s in strings:
        offsets.append(len(data))
        if utf8:
            enc = s.encode("utf-8")
            # panjang karakter & panjang byte; >0x7f pakai 2 field
            if len(enc) > 0x7F or len(s) > 0x7F:
                data += struct.pack("<BBH", (len(s) >> 8) | 0x80, len(s) & 0xFF, len(enc))
            else:
                data += struct.pack("<BB", len(s), len(enc))
            data += enc + b"\x00"
        else:
            data += struct.pack("<H", len(s))
            data += s.encode("utf-16-le") + b"\x00\x00"
    while len(data) % 4:
        data += b"\x00"
    hdr_size = 28
    table = b"".join(struct.pack("<I", o) for o in offsets)
    strings_start = hdr_size + len(table)
    size = strings_start + len(data)
    return struct.pack("<HHIIIIII", 0x0001, hdr_size, size, len(strings), 0,
                       0x100 if utf8 else 0, strings_start, 0) + table + bytes(data)


def build_axml(root):
    """root = elemen tree: ('element', name, [(ns, attrname, value)]) ..."""
    ax = AXML()
    # aapt menaruh nama atribut framework di awal string pool dengan urutan = urutan
    # resource map (Android memakai name.index sebagai indeks resource map).
    for k in sorted(RES_IDS, key=lambda k: RES_IDS[k]):
        ax.sidx(k)
    def walk(e):
        for a in e["attrs"]:
            if a[0]:
                ax.sidx(a[0])
            ax.sidx(a[1])
            if a[2][0] == "s":
                ax.sidx(a[2][1])
        ax.sidx(e["name"])
        for c in e["children"]:
            walk(c)
    walk(root)
    # string index 0..: namespace prefix "android" dll
    ax.sidx("android")

    ns_decls = [("android", ANDROID_NS)]
    for p, u in ns_decls:
        ax.sidx(p)
        ax.sidx(u)

    # resource map: hanya atribut ber-namespace yang punya resource id
    res_map = {}
    for k, v in RES_IDS.items():
        res_map[k] = v

    body = bytearray()

    def emit_element(e):
        ext = bytearray()
        # atribut
        for (ns, name, val) in e["attrs"]:
            ns_idx = ax._smap[ns] if ns else 0xFFFFFFFF
            name_idx = ax._smap[name]
            raw = 0xFFFFFFFF
            if val[0] == "s":
                raw = ax._smap[val[1]]
                typed = struct.pack("<HBBI", 8, 0, TYPE_STRING, ax._smap[val[1]])
            elif val[0] == "i":
                typed = struct.pack("<HBBI", 8, 0, TYPE_INT_DEC, val[1] & 0xFFFFFFFF)
            elif val[0] == "b":
                typed = struct.pack("<HBBI", 8, 0, TYPE_BOOL, 0xFFFFFFFF if val[1] else 0)
            else:
                raise ValueError(val)
            ext += struct.pack("<III", ns_idx, name_idx, raw) + typed
        start = struct.pack("<IIHHHHHH", 0xFFFFFFFF, ax._smap[e["name"]], 0x0014, 0x0014,
                            len(e["attrs"]), 0, 0, 0)
        body.extend(struct.pack("<HHIII", 0x0102, 0x0010, 0x0010 + 0x0014 + len(ext), 1, 0xFFFFFFFF))
        body.extend(start + ext)
        for c in e["children"]:
            emit_element(c)
        # END_ELEMENT: header(16) + ns(4) + name(4) = 24 byte.
        # Versi lama hanya menulis `name` tanpa `ns` -> parser Android kehilangan
        # sinkronisasi dan menampilkan "ada masalah saat mengurai paket".
        body.extend(struct.pack("<HHIII", 0x0103, 0x0010, 0x0018, 1, 0xFFFFFFFF))
        body.extend(struct.pack("<II", 0xFFFFFFFF, ax._smap[e["name"]]))

    # namespace start
    body.extend(struct.pack("<HHIII", 0x0100, 0x10, 0x18, 1, 0xFFFFFFFF))
    body.extend(struct.pack("<II", ax._smap["android"], ax._smap[ANDROID_NS]))
    emit_element(root)
    body.extend(struct.pack("<HHIII", 0x0101, 0x10, 0x18, 1, 0xFFFFFFFF))
    body.extend(struct.pack("<II", ax._smap["android"], ax._smap[ANDROID_NS]))

    rm = b"".join(struct.pack("<I", RES_IDS[k]) for k in sorted(RES_IDS, key=lambda k: RES_IDS[k]))
    rm_chunk = struct.pack("<HHI", 0x0180, 8, 8 + len(rm)) + rm

    strings_chunk = _pool(ax.strings)
    size = 8 + len(strings_chunk) + len(rm_chunk) + len(body)
    return struct.pack("<HHI", 0x0003, 8, size) + strings_chunk + rm_chunk + bytes(body)


def manifest_tree(pkg, version_name, version_code, min_sdk, target_sdk, lib_name,
                  activity_name, app_label):
    A = ANDROID_NS

    def el(name, attrs=(), children=()):
        return {"name": name, "attrs": list(attrs), "children": list(children)}

    native = el("activity", [
        (A, "name", _s(activity_name)),
        (A, "exported", _b(True)),
        (A, "label", _s(app_label)),
        (A, "screenOrientation", _i(0)),      # 0 = landscape
        (A, "configChanges", _i(0x0FFF)),      # hindari restart saat rotasi/fokus
        (A, "theme", _i(0x01030007)),          # @android:style/Theme.Black.NoTitleBar.Fullscreen
    ], [
        el("intent-filter", [], [
            el("action", [(A, "name", _s("android.intent.action.MAIN"))]),
            el("category", [(A, "name", _s("android.intent.category.LAUNCHER"))]),
        ]),
        el("meta-data", [(A, "name", _s("android.app.lib_name")),
                         (A, "value", _s(lib_name))]),
    ])
    app = el("application", [
        (A, "label", _s(app_label)),
        (A, "debuggable", _b(True)),
        (A, "hardwareAccelerated", _b(True)),
        (A, "allowBackup", _b(False)),
        # Android 15/16: lib dibaca langsung dari APK (butuh .so tersimpan + selaras 16 KB)
        (A, "extractNativeLibs", _b(False)),
    ], [native])
    return el("manifest", [
        ("", "package", _s(pkg)),
        ("", "versionCode", _i(version_code)),
        ("", "versionName", _s(version_name)),
    ], [
        el("uses-sdk", [(A, "minSdkVersion", _i(min_sdk)), (A, "targetSdkVersion", _i(target_sdk))]),
        el("uses-feature", [(A, "glEsVersion", _i(0x00030000)), ("", "required", _b(True))]),
        el("uses-feature", [(A, "name", _s("android.hardware.touchscreen")), ("", "required", _b(True))]),
        app,
    ])


# ------------------------------------------------------------------------ zip/APK

def _align(data, align, offset_of_entry):
    """padding extra field agar data mulai di kelipatan `align`."""
    local_off = offset_of_entry
    # header local: 30 + len(name) + len(extra)
    hdr = 30 + 0
    pad = (align - ((local_off + hdr) % align)) % align
    return pad


class ZipWriter:
    def __init__(self):
        self.buf = bytearray()
        self.entries = []
        self.entry_data = {}   # name -> uncompressed data (untuk tanda tangan v1)

    def add(self, name, data, compress=True, align=4):
        name_b = name.encode("utf-8")
        if compress:
            comp = zipfile.ZIP_DEFLATED
            c = __import__("zlib").compressobj(9, __import__("zlib").DEFLATED, -15)
            payload = c.compress(data) + c.flush()
            method = 8
        else:
            payload = data
            method = 0
        crc = __import__("zlib").crc32(data) & 0xFFFFFFFF
        off = len(self.buf)
        extra = b""
        if method == 0 and align > 1:
            base = off + 30 + len(name_b)
            pad = (align - (base % align)) % align
            if pad:
                if pad < 4:
                    pad += align
                extra = struct.pack("<HH", 0xD935, pad - 4) + b"\x00" * (pad - 4)
        # time/date zip dibuat tetap (0x0021/0x0000) supaya build bisa direproduksi
        self.buf += struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0x0800, method, 0x0021, 0x0000,
                                crc, len(payload), len(data), len(name_b), len(extra))
        self.buf += name_b + extra + payload
        self.entries.append(dict(name=name_b, method=method, crc=crc, csize=len(payload),
                                 usize=len(data), off=off, flags=0x0800,
                                 time=0x0021, date=0x0000, extra=extra))
        self.entry_data[name] = data   # simpan uncompressed untuk digest v1
        return len(payload)

    def v1_signature_files(self, key_path, cert_path):
        """Beri tanda v1 (JAR) pada semua entri non-META-INF, kembalikan berkas META-INF."""
        non_meta = [(n, d) for n, d in self.entry_data.items()
                    if not n.startswith("META-INF/")]
        return sign_v1(non_meta, key_path, cert_path)

    def finish(self):
        cd_off = len(self.buf)
        for e in self.entries:
            self.buf += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, e["flags"],
                                    e["method"], e["time"], e["date"], e["crc"], e["csize"],
                                    e["usize"], len(e["name"]), 0, 0, 0, 0, 0, e["off"])
            self.buf += e["name"]
        cd_size = len(self.buf) - cd_off
        self.buf += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(self.entries), len(self.entries),
                                cd_size, cd_off, 0)
        return bytes(self.buf)


# ------------------------------------- ASN.1 DER helpers (untuk tanda tangan v1 / JAR)

def _der_len(n):
    if n < 0x80:
        return bytes([n])
    out = bytearray()
    while n > 0:
        out.insert(0, n & 0xFF)
        n >>= 8
    return bytes([0x80 | len(out)]) + bytes(out)

def _der_int(n):
    if n == 0:
        return b"\x02\x01\x00"
    b = n.to_bytes((n.bit_length() + 7) // 8, "big")
    if b[0] & 0x80:
        b = b"\x00" + b
    return b"\x02" + _der_len(len(b)) + b

def _der_octet(data):
    return b"\x04" + _der_len(len(data)) + data

def _der_oid_subid(n):
    """Encode satu sub-identifier OID dalam base-128."""
    if n == 0:
        return bytes([0])
    out = bytearray()
    while n > 0:
        out.insert(0, n & 0x7F)
        n >>= 7
    # set high bit pada semua byte kecuali yang terakhir
    for i in range(len(out) - 1):
        out[i] |= 0x80
    return bytes(out)

def _der_oid(dotted):
    parts = [int(x) for x in dotted.split(".")]
    first = parts[0] * 40 + parts[1]
    result = bytes([first]) + b"".join(_der_oid_subid(p) for p in parts[2:])
    return b"\x06" + _der_len(len(result)) + result

def _der_seq(content):
    return b"\x30" + _der_len(len(content)) + content

def _der_set(content):
    return b"\x31" + _der_len(len(content)) + content

def _der_context(tag, content):
    return bytes([0xA0 | tag]) + _der_len(len(content)) + content

def _der_utf8(s):
    b = s.encode("utf-8")
    return b"\x0c" + _der_len(len(b)) + b

def _der_printable(s):
    b = s.encode("ascii")
    return b"\x13" + _der_len(len(b)) + b

def _encode_name(name):
    """x509.Name -> DER (Sequence of Set of AttributeTypeAndValue).

    Setiap NameAttribute dianggap satu RDN (set dengan satu atribut) —
    bentuk standar untuk sertifikat APK debug.
    """
    rdns = []
    for attr in name:  # tiap item adalah NameAttribute
        oid = _der_oid(attr.oid.dotted_string)
        val = attr.value
        if isinstance(val, str):
            if attr.oid.dotted_string == "2.5.4.6":  # countryName
                v = _der_printable(val)
            else:
                v = _der_utf8(val)
        else:
            v = _der_octet(bytes(val))
        rdns.append(_der_set(_der_seq(oid + v)))
    return _der_seq(b"".join(rdns))


def sign_v1(zip_entries, key_path, cert_path):
    """Tanda tangan APK skema v1 (JAR).

    zip_entries: list of (name, uncompressed_data) — HANYA entri non-META-INF,
                 karena MANIFEST.MF/CERT.SF/CERT.RSA tidak mencantumkan dirinya sendiri.
    Returns list of (name, data) untuk META-INF files yang akan ditambahkan ke zip.
    """
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding, utils
    from cryptography import x509
    import base64

    key = serialization.load_pem_private_key(open(key_path, "rb").read(), password=None)
    cert = x509.load_der_x509_certificate(open(cert_path, "rb").read())
    cert_der = cert.public_bytes(serialization.Encoding.DER)

    # --- MANIFEST.MF: daftar semua entri + digest SHA-256 (uncompressed)
    mf = bytearray()
    mf += b"Manifest-Version: 1.0\n"
    mf += b"Created-By: 1.0 (Android)\n"
    mf += b"\n"
    entry_digests = {}
    for name, data in zip_entries:
        d = hashlib.sha256(data).digest()
        entry_digests[name] = d
        mf += f"Name: {name}\n".encode("utf-8")
        mf += f"SHA-256: {base64.b64encode(d).decode('ascii')}\n".encode("ascii")
        mf += b"\n"
    manifest_bytes = bytes(mf)
    manifest_digest_b64 = base64.b64encode(hashlib.sha256(manifest_bytes).digest()).decode("ascii")

    # --- CERT.SF: digest manifest + daftar entri
    sf = bytearray()
    sf += b"Signature-Version: 1.0\n"
    sf += b"Created-By: 1.0 (Android)\n"
    sf += f"SHA-256-Digest-Manifest: {manifest_digest_b64}\n".encode("ascii")
    sf += b"\n"
    for name, data in zip_entries:
        d = entry_digests[name]
        sf += f"Name: {name}\n".encode("utf-8")
        sf += f"SHA-256: {base64.b64encode(d).decode('ascii')}\n".encode("ascii")
        sf += b"\n"
    sf_bytes = bytes(sf)

    # --- Tanda tangan CERT.SF -> PKCS#7 SignedData (CERT.RSA)
    sf_digest = hashlib.sha256(sf_bytes).digest()
    signature = key.sign(sf_digest, padding.PKCS1v15(), utils.Prehashed(hashes.SHA256()))

    # signedAttrs: SET of { messageDigest, <sf_digest> }
    msg_digest_attr = _der_seq(
        _der_oid("1.2.840.113549.1.9.4") + _der_set(_der_octet(sf_digest))
    )
    signed_attrs = _der_set(msg_digest_attr)
    sig_over_attrs = key.sign(hashlib.sha256(signed_attrs).digest(),
                              padding.PKCS1v15(), utils.Prehashed(hashes.SHA256()))

    # IssuerAndSerialNumber dari sertifikat
    issuer_der = _encode_name(cert.issuer)
    iasn = _der_seq(issuer_der + _der_int(cert.serial_number))

    # SignerInfo
    signer_info = _der_seq(
        _der_int(1) +                      # version (dengan signedAttrs)
        iasn +
        _der_seq(_der_oid("2.16.840.1.101.3.4.2.1") + b"\x05\x00") +  # digestAlgorithm SHA-256
        _der_context(0, signed_attrs) +   # signedAttrs [0] EXPLICIT
        _der_seq(_der_oid("1.2.840.113549.1.1.11") + b"\x05\x00") +   # signatureAlgorithm sha256WithRSA
        _der_octet(sig_over_attrs)        # signature
    )

    # EncapsulatedContentInfo: eContentType=data, eContent=CERT.SF
    encap = _der_seq(
        _der_oid("1.2.840.113549.1.7.1") + _der_context(0, _der_octet(sf_bytes))
    )

    # SignedData: version 1 (sertifikat ada), digestAlgorithms, encapContentInfo,
    # certificates, signerInfos
    signed_data = _der_seq(
        _der_int(1) +
        _der_set(_der_seq(_der_oid("2.16.840.1.101.3.4.2.1") + b"\x05\x00")) +
        encap +
        _der_context(0, _der_set(cert_der)) +
        _der_set(signer_info)
    )

    # ContentInfo: contentType=signedData, content=SignedData
    cert_rsa = _der_seq(
        _der_oid("1.2.840.113549.1.7.2") + _der_context(0, signed_data)
    )

    return [
        ("META-INF/MANIFEST.MF", manifest_bytes),
        ("META-INF/CERT.SF", sf_bytes),
        ("META-INF/CERT.RSA", cert_rsa),
    ]


# ------------------------------------------------------- APK Signature Scheme v2

def _lp(b):
    return struct.pack("<I", len(b)) + b


V2_ID = struct.pack("<I", 0x7109871A)   # id blok tanda tangan v2


_CHUNK = 1024 * 1024      # ukuran potongan digest skema v2 (apksig)


def _sig_algo_for(key_size):
    return 0x0103  # RSASSA-PKCS1-v1_5 with SHA-256


def sign_v2(apk_bytes, key_path, cert_path, extra_certs=()):
    """Buat blok APK Signing Block (skema v2).

    Tata letak akhir APK:  [isi] [blok tanda tangan] [central dir] [EOCD]
    Dua hal yang mudah tertukar (sudah diuji ke APK contoh apksig):
    - DIGEST memakai EOCD dengan field "offset CD" = AWAL BLOK (nilai sebelum sisip);
    - EOCD yang DITULIS ke disk memakai field "offset CD" = posisi CD baru
      (= awal blok + panjang blok), supaya zip tetap terbaca alat standar.
    Tiga bagian yang di-digest: isi, CD saja, lalu EOCD termodifikasi.
    """
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding
    from cryptography import x509

    key = serialization.load_pem_private_key(open(key_path, "rb").read(), password=None)
    cert = x509.load_der_x509_certificate(open(cert_path, "rb").read())
    cert_der = cert.public_bytes(serialization.Encoding.DER)
    pub = key.public_key().public_bytes(serialization.Encoding.DER,
                                        serialization.PublicFormat.SubjectPublicKeyInfo)
    algo = _sig_algo_for(key.key_size)

    eocd_off = apk_bytes.rfind(b"PK\x05\x06")
    cd_off = struct.unpack("<I", apk_bytes[eocd_off + 16:eocd_off + 20])[0]

    def make_signed_data(digest):
        digests_field = _lp(struct.pack("<I", 1) + struct.pack("<II", algo, len(digest)) + digest)
        certs_field = _lp(_lp(cert_der))
        return digests_field + certs_field + struct.pack("<I", 0)   # 0 atribut tambahan

    def make_block(signed_data):
        signature = key.sign(signed_data, padding.PKCS1v15(), hashes.SHA256())
        signer = (_lp(signed_data)
                  + _lp(struct.pack("<I", 1) + struct.pack("<II", algo, len(signature)) + signature)
                  + _lp(pub))
        signers = struct.pack("<I", 1) + _lp(signer)          # 1 penanda tangan
        pair = struct.pack("<Q", 4 + len(signers)) + V2_ID + signers
        size = len(pair) + 24
        magic = b"APK Sig Block 42"
        block = struct.pack("<Q", size) + pair + struct.pack("<Q", size) + magic
        assert len(block) == size + 8
        return block

    # 1) bangun blok percobaan (digest masih nol) -> panjang final diketahui
    probe = make_block(make_signed_data(b"\x00" * 32))
    block_start = cd_off                       # blok disisipkan tepat di sini

    # 2) digest: isi | central dir | EOCD dengan field offset = awal blok
    #    (persis seperti apksig: nilai EOCD SEBELUM blok disisipkan).
    #    EOCD yang ditulis ke disk nanti memakai offset CD yang sudah digeser.
    #
    #    PENTING (ini yang membuat APK besar gagal dipasang):
    #    apksig memecah SEMUA bagian menjadi potongan 1 MB lalu menggabungkannya
    #    jadi SATU daftar, baru di-hash:
    #        digest = sha256(0x5a || u32(jumlah potongan) || semua digest potongan)
    #        digest potongan = sha256(0xa5 || u32(len) || isi potongan)
    #    Versi lama menghitung per bagian utuh lalu membungkus tiap bagian dengan
    #    0x5a sendiri -> salah untuk semua APK > 1 MB (Android menolak tanda tangan).
    eocd = bytearray(apk_bytes[eocd_off:])
    eocd[16:20] = struct.pack("<I", block_start)
    sections = [apk_bytes[0:block_start], apk_bytes[cd_off:eocd_off], bytes(eocd)]
    chunk_digests = []
    for s in sections:
        if not s:
            continue
        for i in range(0, len(s), _CHUNK):
            c = s[i:i + _CHUNK]
            chunk_digests.append(hashlib.sha256(b"\xa5" + struct.pack("<I", len(c)) + c).digest())
    digest = hashlib.sha256(b"\x5a" + struct.pack("<I", len(chunk_digests))
                            + b"".join(chunk_digests)).digest()

    block = make_block(make_signed_data(digest))
    assert len(block) == len(probe), "panjang blok tidak stabil"
    return block, digest


def insert_v2(apk_bytes, key_path, cert_path):
    eocd_off = apk_bytes.rfind(b"PK\x05\x06")
    cd_off = struct.unpack("<I", apk_bytes[eocd_off + 16:eocd_off + 20])[0]
    block, _digest = sign_v2(apk_bytes, key_path, cert_path)
    cd_new = cd_off + len(block)
    eocd = bytearray(apk_bytes[eocd_off:])
    eocd[16:20] = struct.pack("<I", cd_new)    # menunjuk awal CD yang baru
    return apk_bytes[:cd_off] + block + apk_bytes[cd_off:eocd_off] + bytes(eocd)


# ------------------------------------------------------------------------- build

APP_PKG = "com.arenalab.nusarush"
ACTIVITY = "android.app.NativeActivity"
LIB = "nusa"
LABEL = "Nusa Rush"


def build(out, lib_path, assets_dir, version_name, version_code, min_sdk, target_sdk, key, cert,
          lib32_path=None):
    root = manifest_tree(APP_PKG, version_name, version_code, min_sdk, target_sdk, LIB,
                         ACTIVITY, LABEL)
    man = build_axml(root)
    zw = ZipWriter()
    zw.add("AndroidManifest.xml", man, compress=True)
    # .so TIDAK dikompresi & data-nya selaras 16384 (syarat Android 15+ untuk
    # extractNativeLibs=false; sekaligus aman untuk semua versi lama).
    zw.add("lib/arm64-v8a/lib%s.so" % LIB, open(lib_path, "rb").read(), compress=False,
           align=16384)
    # ABI 32-bit opsional (HP lama / Android 10 32-bit-only tidak bisa memasang APK arm64)
    if lib32_path and os.path.exists(lib32_path):
        zw.add("lib/armeabi-v7a/lib%s.so" % LIB, open(lib32_path, "rb").read(),
               compress=False, align=16384)
    n_assets = 0
    for base, _, files in os.walk(assets_dir):
        for f in sorted(files):
            p = os.path.join(base, f)
            rel = os.path.relpath(p, assets_dir).replace(os.sep, "/")
            # aset besar disimpan tanpa kompresi + ter-align (baca mmap langsung, install cepat)
            store = os.path.getsize(p) > 64 * 1024
            zw.add("assets/" + rel, open(p, "rb").read(), compress=not store,
                   align=4096 if store else 4)
            n_assets += 1
    # Tanda tangan v1 (JAR): tambahkan META-INF/MANIFEST.MF, CERT.SF, CERT.RSA
    # SEBELUM v2, agar v2 mencakup seluruh isi APK (termasuk META-INF).
    if key and cert:
        for vname, vdata in zw.v1_signature_files(key, cert):
            zw.add(vname, vdata, compress=False, align=4)
    raw = zw.finish()
    if key and cert:
        raw = insert_v2(raw, key, cert)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, "wb").write(raw)
    print("APK ditulis: %s (%.2f MB, %d aset)" % (out, len(raw) / 1e6, n_assets))
    return out


def verify(path):
    from androguard.core.apk import APK
    a = APK(path)
    print("package      :", a.get_package())
    print("versionName  :", a.get_androidversion_name(), "code:", a.get_androidversion_code())
    print("minSdk       :", a.get_min_sdk_version(), "targetSdk:", a.get_target_sdk_version())
    print("activities   :", a.get_activities())
    print("signed v1    :", a.is_signed_v1())
    print("signed v2    :", a.is_signed_v2())
    try:
        print("cert sha256  :", a.get_certificates_der_v2()[0] and
              hashlib.sha256(a.get_certificates_der_v2()[0]).hexdigest()[:32])
    except Exception as e:
        print("cert         :", e)
    print("native libs  :", [x for x in a.get_files() if x.startswith("lib/")])
    apk_size = os.path.getsize(path)
    print("size MB      : %.2f" % (apk_size / 1e6))
    assets = [x for x in a.get_files() if x.startswith("assets/")]
    print("asset count  :", len(assets))
    return a


def badging(path):
    """Pengganti `aapt dump badging` (aapt2 tidak tersedia di sandbox)."""
    a = verify(path)
    print("application-label:", LABEL)
    print("launchable-activity: name='%s'" % ACTIVITY)
    try:
        import zipfile
        names = zipfile.ZipFile(path).namelist()
        abis = sorted({n.split("/")[1] for n in names if n.startswith("lib/")})
        if abis:
            print("native-code:", " ".join(abis))
    except Exception:
        print("native-code: arm64-v8a")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "build":
        import argparse
        p = argparse.ArgumentParser()
        p.add_argument("--out", required=True)
        p.add_argument("--lib", required=True)
        p.add_argument("--lib32", default=None)
        p.add_argument("--assets", required=True)
        p.add_argument("--version-name", default="1.0")
        p.add_argument("--version-code", type=int, default=1)
        p.add_argument("--min-sdk", type=int, default=24)
        p.add_argument("--target-sdk", type=int, default=34)
        p.add_argument("--key", default="keystore/debug.key.pem")
        p.add_argument("--cert", default="keystore/debug.cert.der")
        p.add_argument("buildcmd", nargs="?")
        a = p.parse_args()
        build(a.out, a.lib, a.assets, a.version_name, a.version_code, a.min_sdk, a.target_sdk,
              a.key if os.path.exists(a.key) else None, a.cert if os.path.exists(a.cert) else None,
              a.lib32)
    elif cmd == "verify":
        verify(sys.argv[2])
    elif cmd == "badging":
        badging(sys.argv[2])
    else:
        print("perintah: build | verify | badging")
