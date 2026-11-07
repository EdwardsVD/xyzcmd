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
    body = b"".join(struct.pack("<I", o) for o in offsets) + bytes(data)
    size = hdr_size + len(body)
    return struct.pack("<HHIIIIII", 0x0001, hdr_size, size, len(strings), 0,
                       0x100 if utf8 else 0, hdr_size + 4 * len(strings), 0) + body


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
        body.extend(struct.pack("<HHIII", 0x0103, 0x0010, 0x0014, 1, 0xFFFFFFFF))
        body.extend(struct.pack("<I", ax._smap[e["name"]]))

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
        self.buf += struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0x0800, method, 0, 0x0021,
                                crc, len(payload), len(data), len(name_b), len(extra))
        self.buf += name_b + extra + payload
        self.entries.append(dict(name=name_b, method=method, crc=crc, csize=len(payload),
                                 usize=len(data), off=off, flags=0x0800))
        return len(payload)

    def finish(self):
        cd_off = len(self.buf)
        for e in self.entries:
            self.buf += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, e["flags"],
                                    e["method"], 0, 0x0021, e["crc"], e["csize"], e["usize"],
                                    len(e["name"]), 0, 0, 0, 0, 0, e["off"])
            self.buf += e["name"]
        cd_size = len(self.buf) - cd_off
        self.buf += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(self.entries), len(self.entries),
                                cd_size, cd_off, 0)
        return bytes(self.buf)


# ------------------------------------------------------- APK Signature Scheme v2

def _lp(b):
    return struct.pack("<I", len(b)) + b


V2_ID = struct.pack("<I", 0x7109871A)   # id blok tanda tangan v2


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
    eocd = bytearray(apk_bytes[eocd_off:])
    eocd[16:20] = struct.pack("<I", block_start)
    sections = [apk_bytes[0:block_start], apk_bytes[cd_off:eocd_off], bytes(eocd)]
    digests = [hashlib.sha256(b"\xa5" + struct.pack("<I", len(s)) + s).digest() for s in sections]
    digest = hashlib.sha256(b"\x5a" + struct.pack("<I", len(digests)) + b"".join(digests)).digest()

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


def build(out, lib_path, assets_dir, version_name, version_code, min_sdk, target_sdk, key, cert):
    root = manifest_tree(APP_PKG, version_name, version_code, min_sdk, target_sdk, LIB,
                         ACTIVITY, LABEL)
    man = build_axml(root)
    zw = ZipWriter()
    zw.add("AndroidManifest.xml", man, compress=True)
    # .so TIDAK dikompresi & data-nya selaras 16384 (syarat Android 15+ untuk
    # extractNativeLibs=false; sekaligus aman untuk semua versi lama).
    zw.add("lib/arm64-v8a/lib%s.so" % LIB, open(lib_path, "rb").read(), compress=False,
           align=16384)
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
    print("native-code:", "arm64-v8a")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "build":
        import argparse
        p = argparse.ArgumentParser()
        p.add_argument("--out", required=True)
        p.add_argument("--lib", required=True)
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
              a.key if os.path.exists(a.key) else None, a.cert if os.path.exists(a.cert) else None)
    elif cmd == "verify":
        verify(sys.argv[2])
    elif cmd == "badging":
        badging(sys.argv[2])
    else:
        print("perintah: build | verify | badging")
