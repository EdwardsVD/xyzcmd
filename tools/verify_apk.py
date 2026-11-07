#!/usr/bin/env python3
"""verify_apk.py - periksa APK seperti parser Android (libziparchive) membacanya.

Fokus: menangkap penyebab "ada masalah saat mengurai paket"
(PackageParser / INSTALL_PARSE_FAILED_UNEXPECTED_EXCEPTION), yaitu:
  1. struktur zip rusak / central directory tidak konsisten
  2. AndroidManifest.xml tidak ada, terkompresi, atau bukan AXML valid
  3. chunk AXML tidak konsisten (ukuran header salah -> parser kehilangan sinkron)
  4. atribut wajib tidak ada (package, versionCode, activity, meta-data lib_name)
  5. native lib tidak ada / tidak cocok ABI
  6. local header vs central directory tidak sama (offset, crc, ukuran)

Jalankan: python3 tools/verify_apk.py output/nusa-rush-s3-debug.apk
"""
import struct
import sys
import zipfile

RES = {
    0x01010000: "theme", 0x01010001: "label", 0x01010002: "icon",
    0x01010003: "name", 0x01010006: "permission", 0x0101000c: "hasCode",
    0x0101000f: "debuggable", 0x01010010: "exported",
    0x0101001e: "screenOrientation", 0x0101001f: "configChanges",
    0x01010024: "value", 0x0101020c: "minSdkVersion", 0x0101021b: "versionCode",
    0x0101021c: "versionName", 0x01010270: "targetSdkVersion",
    0x01010271: "maxSdkVersion", 0x01010280: "allowBackup",
    0x01010281: "glEsVersion", 0x0101028e: "required", 0x010102d3: "hardwareAccelerated",
    0x010104ea: "extractNativeLibs", 0x010104ec: "usesCleartextTraffic",
    0x010104f7: "resizeableActivity",
}

problems = []


def bad(msg):
    problems.append(msg)
    print("  [GAGAL]", msg)


def ok(msg):
    print("  [ok]", msg)


# ---------------------------------------------------------------- 1. struktur zip
def check_zip(path):
    print("\n[1] struktur zip")
    raw = open(path, "rb").read()
    if raw[:4] != b"PK\x03\x04":
        bad("berkas tidak dimulai dengan local file header")
        return raw, None
    eocd = raw.rfind(b"PK\x05\x06")
    if eocd < 0:
        bad("EOCD tidak ditemukan")
        return raw, None
    if eocd + 22 > len(raw):
        bad("EOCD terpotong")
    n, = struct.unpack_from("<H", raw, eocd + 10)
    cd_size, cd_off = struct.unpack_from("<II", raw, eocd + 12)
    comment_len, = struct.unpack_from("<H", raw, eocd + 20)
    if eocd + 22 + comment_len != len(raw):
        bad("panjang EOCD/komentar tidak cocok dengan ukuran berkas")
    if raw[cd_off:cd_off + 4] != b"PK\x01\x02":
        bad(f"offset central directory ({cd_off}) tidak menunjuk 'PK\\x01\\x02'")
    if cd_off + cd_size != eocd:
        bad(f"cd_off+cd_size ({cd_off + cd_size}) != posisi EOCD ({eocd})")
    else:
        ok(f"central directory utuh di {cd_off} ({cd_size} byte)")
    zf = zipfile.ZipFile(path)
    names = zf.namelist()
    if len(names) != n:
        bad(f"jumlah entri EOCD ({n}) != jumlah entri CD ({len(names)})")
    # bandingkan local header dengan central directory
    mism = 0
    for i in zf.infolist():
        off = i.header_offset
        if raw[off:off + 4] != b"PK\x03\x04":
            bad(f"{i.filename}: local header tidak ada di offset {off}")
            mism += 1
            continue
        (_ver, flags, method, _t, _d, crc, csz, usz, nlen, xlen) = struct.unpack_from(
            "<HHHHHIIIHH", raw, off + 4)
        lname = raw[off + 30:off + 30 + nlen].decode("utf-8", "replace")
        if lname != i.filename:
            bad(f"nama local ({lname}) != CD ({i.filename})")
            mism += 1
        if (crc, csz, usz) != (i.CRC, i.compress_size, i.file_size):
            bad(f"{i.filename}: crc/ukuran local != CD")
            mism += 1
        if method != i.compress_type:
            bad(f"{i.filename}: metode kompresi local != CD")
            mism += 1
    if mism == 0:
        ok(f"{len(names)} entri: local header cocok dengan central directory")
    if zf.testzip() is not None:
        bad("ada entri yang CRC-nya tidak cocok")
    else:
        ok("semua entri lolos uji CRC")
    return raw, zf


# ------------------------------------------------------------------- 2. manifest
def _read_pool_ret(b, off):
    """kembalikan (string list, ukuran chunk pool)"""
    _strings, size = read_pool(b, off)
    return _strings, size


def read_pool(b, off):
    _t, _h, size = struct.unpack_from("<HHI", b, off)
    count, _styc, flags, str_off, _slen = struct.unpack_from("<IIIII", b, off + 8)
    # tabel offset ada tepat setelah header chunk; stringsStart menunjuk data string
    table = off + _h
    base = off + str_off
    utf8 = bool(flags & 0x100)
    out = []
    for i in range(count):
        rel, = struct.unpack_from("<I", b, table + 4 * i)
        p = base + rel
        if utf8:
            n = b[p]
            p += 1
            if n & 0x80:
                n = ((n & 0x7F) << 8) | b[p]
                p += 1
            out.append(b[p:p + n].decode("utf-8", "replace"))
        else:
            n, = struct.unpack_from("<H", b, p)
            out.append(b[p + 2:p + 2 + 2 * n].decode("utf-16-le", "replace"))
    return out, size


def check_manifest(zf):
    print("\n[2] AndroidManifest.xml (AXML)")
    if "AndroidManifest.xml" not in zf.namelist():
        bad("AndroidManifest.xml tidak ada di APK")
        return None
    info = zf.getinfo("AndroidManifest.xml")
    if info.compress_type != 8:
        bad("AndroidManifest.xml harus dikompresi (deflate)")
    import zipfile as _zip
    with _zip.ZipFile(zf.filename) as _z:
        b = _z.read("AndroidManifest.xml")
    t, hsz, size = struct.unpack_from("<HHI", b, 0)
    if t != 3 or hsz != 8:
        bad(f"header AXML salah (type={t:#x} hdr={hsz})")
        return None
    if size != len(b):
        bad(f"ukuran AXML di header ({size}) != berkas ({len(b)})")
    else:
        ok(f"header AXML benar ({len(b)} byte)")
    strs, psize = _read_pool_ret(b, 8)
    off = 8 + psize
    t2, h2, s2 = struct.unpack_from("<HHI", b, off)
    if t2 != 0x0180:
        bad(f"resource map tidak ditemukan (type={t2:#x})")
        return None
    resmap = [struct.unpack_from("<I", b, off + h2 + 4 * i)[0]
              for i in range((s2 - h2) // 4)]
    ok(f"string pool {len(strs)} string, resource map {len(resmap)} ID")
    off += s2
    if off + 8 > len(b):
        bad(f"tidak ada elemen setelah resource map (offset {off})")

    root = None
    stack = []
    attrs_out = {}
    nelem = 0
    while off < len(b):
        t, h, sz = struct.unpack_from("<HHI", b, off)
        if sz <= 0 or off + sz > len(b):
            bad(f"chunk AXML @{off}: ukuran tidak masuk akal ({sz})")
            return None
        if t == 0x0102:
            _line, _c, _ns, name_i, a_start, a_size, a_count = \
                struct.unpack_from("<IIIIHHH", b, off + 8)
            if a_size != 20:
                bad(f"elemen '{strs[name_i]}': attributeStart/Size salah ({a_size})")
            # size = header(16) + attributeStart + count*attributeSize
            if sz != h + a_start + a_count * a_size:
                bad(f"elemen '{strs[name_i]}': size {sz} != {h}+{a_start}+{a_count}*{a_size}")
            if a_start != 20:
                bad(f"elemen '{strs[name_i]}': attributeStart {a_start} != 20")
            nelem += 1
            attrs = {}
            for k in range(a_count):
                # atribut mulai di chunk + header(16) + attributeStart
                a = off + h + a_start + k * a_size
                ns_i, name_a, raw_i = struct.unpack_from("<III", b, a)
                _ts, _res0, vtype, vdata = struct.unpack_from("<HBBI", b, a + 12)
                # atribut ber-namespace memakai resource id (resmap diindeks
                # dengan indeks string pool); atribut tanpa namespace (package,
                # versionCode, versionName) memakai namanya apa adanya.
                if ns_i != 0xFFFFFFFF:
                    rid = resmap[name_a] if name_a < len(resmap) else 0
                    key = "android:" + RES.get(rid, strs[name_a])
                else:
                    key = strs[name_a]
                # nilai string: pakai data string (raw). Tipe 0x03 berarti
                # nilainya referensi resource, jadi jangan ambil dari string pool.
                if raw_i != 0xFFFFFFFF and raw_i < len(strs):
                    val = strs[raw_i]
                elif vtype == 0x03:
                    val = "resid:%#x" % vdata
                elif vtype == 0x10:
                    val = str(struct.unpack("<i", struct.pack("<I", vdata))[0])
                elif vtype == 0x12:
                    val = str(bool(vdata))
                else:
                    val = str(vdata)
                attrs[key] = val
            name = strs[name_i]
            if root is None:
                root = (name, attrs)
            else:
                attrs_out.setdefault(name, []).append(attrs)
            stack.append(name)
        elif t == 0x0103:
            _line, _c, ns_i, name_i = struct.unpack_from("<IIII", b, off + 8)
            if sz != 24 or h != 16:
                bad(f"END_ELEMENT ukuran salah ({h}/{sz}), seharusnya 16/24")
            if ns_i != 0xFFFFFFFF:
                bad("END_ELEMENT punya namespace padahal tidak seharusnya")
            if not stack or stack[-1] != strs[name_i]:
                bad(f"END_ELEMENT '{strs[name_i]}' tidak cocok dengan elemen terbuka "
                    f"({stack[-1] if stack else '-'})")
                return None
            stack.pop()
        off += sz
    if stack:
        bad(f"masih ada elemen tidak tertutup: {stack}")
    ok(f"{nelem} elemen XML terurai tanpa kehilangan sinkron")

    # ---- atribut wajib untuk bisa dipasang
    if not root:
        bad("root element tidak ada")
        return None
    name, attrs = root
    if name != "manifest":
        bad(f"root element '{name}' bukan manifest")
    if "package" not in attrs:
        bad("atribut package tidak ada")
    else:
        ok(f"package = {attrs['package']}")
    for a in ("versionCode", "versionName"):
        v = attrs.get(a)
        if v is None:
            bad(f"{a} tidak ada")
        else:
            ok(f"{a} = {v}")
    if "uses-sdk" not in attrs_out:
        bad("uses-sdk tidak ada")
    else:
        sdk = attrs_out["uses-sdk"][0]
        ok(f"minSdk={sdk.get('android:minSdkVersion')} "
           f"targetSdk={sdk.get('android:targetSdkVersion')}")
    acts = attrs_out.get("activity", [])
    if not acts:
        bad("tidak ada <activity>")
    else:
        a0 = acts[0]
        if "android:name" not in a0:
            bad("activity tanpa android:name")
        elif "android:exported" not in a0:
            bad("activity tanpa android:exported (wajib untuk targetSdk >= 31)")
        else:
            ok(f"activity {a0['android:name']} exported={a0['android:exported']}")
        if "intent-filter" not in attrs_out:
            bad("intent-filter LAUNCHER tidak ada")
    md = attrs_out.get("meta-data", [])
    libname = [m for m in md if m.get("android:name") == "android.app.lib_name"]
    if not libname:
        bad("meta-data android.app.lib_name tidak ada (NativeActivity butuh ini)")
    else:
        ok(f"android.app.lib_name = {libname[0].get('android:value')}")
    return attrs


# --------------------------------------------------------------------- 3. native
def check_native(zf, libname):
    print("\n[3] pustaka native")
    abis = sorted({n.split("/")[1] for n in zf.namelist() if n.startswith("lib/")})
    if not abis:
        bad("tidak ada lib/ di APK")
        return
    ok("ABI: " + ", ".join(abis))
    for abi in abis:
        p = f"lib/{abi}/lib{libname}.so"
        if p not in zf.namelist():
            bad(f"{p} tidak ada")
            continue
        info = zf.getinfo(p)
        raw = open(zf.filename, "rb").read()
        _nl, _xl = struct.unpack_from("<HH", raw, info.header_offset + 26)
        data_off = info.header_offset + 30 + _nl + _xl
        if info.compress_type != 0:
            print("  [catatan] .so dikompresi; extractNativeLibs harus true")
        if data_off % 16384:
            print(f"  [catatan] {p} tidak selaras 16 KB (data @{data_off}) "
                  f"-> bisa gagal di Android 15+")
        head = raw[data_off:data_off + 20]
        if head[:4] != b"\x7fELF":
            bad(f"{p}: bukan ELF")
            continue
        cls = {1: "32-bit", 2: "64-bit"}.get(head[4], "?")
        mach = struct.unpack_from("<H", head, 18)[0]
        mach_name = {0x28: "ARM", 0xB7: "AArch64"}.get(mach, hex(mach))
        ok(f"{p}: ELF {cls} {mach_name}, {info.file_size} byte")
        if abi == "arm64-v8a" and mach != 0xB7:
            bad(f"{p}: ABI arm64-v8a tetapi mesin {mach_name}")
        if abi == "armeabi-v7a" and mach != 0x28:
            bad(f"{p}: ABI armeabi-v7a tetapi mesin {mach_name}")


def main(path):
    print(f"== periksa {path}")
    raw, zf = check_zip(path)

    class _Z:
        def __init__(self, p):
            self._z = zipfile.ZipFile(p)
            self.filename = p
            self.namelist = self._z.namelist
            self.getinfo = self._z.getinfo
            self.read = self._z.read

    z = _Z(path)
    attrs = check_manifest(z)
    libname = "nusa"
    if attrs:
        libname = attrs.get("android:lib_name", libname)
    if zf:
        check_native(z, "nusa")
        print("\n[4] isi wajib")
        for need in ("AndroidManifest.xml", "assets/generated/island.bin",
                     "assets/generated/props.bin", "assets/generated/roads.json"):
            if need in zf.namelist():
                ok(f"{need} ada")
            else:
                bad(f"{need} tidak ada")

    print("\n" + "=" * 60)
    if problems:
        print(f"HASIL: {len(problems)} MASALAH -> APK kemungkinan besar gagal dipasang")
        for p in problems:
            print("  -", p)
        return 1
    print("HASIL: SEMUA CEK LULUS (struktur zip, AXML, atribut wajib, native lib)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "output/nusa-rush-s3-debug.apk"))
