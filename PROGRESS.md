# PROGRESS — Nusa Rush (sesi 3)

Status: berjalan.

## M1 Lingkungan — SELESAI (dengan pengganti)
- JDK/SDK/NDK/Blender resmi TIDAK tersedia (host diblokir, lihat DECISIONS D1).
- Toolchain pengganti terpasang & teruji: zig 0.13 (C++17 → aarch64-android),
  sysroot bionic+EGL+GLES3 hasil `tools/make_sysroot.sh` (1105 header),
  APK builder + signer v2 `tools/apkbuild.py` (teruji: manifest valid, v2 signature),
  androguard untuk verifikasi APK, numpy/Pillow untuk pipeline aset.

## Aset
- Semua aset dari Drive terunduh & diverifikasi (magic "glTF", jumlah segitiga, tinggi):
  12 rumah desa, 12 gedung kota, 4 jenis pohon (+_b, +_lod1), pulau P2 (+lod1), lamborghini.glb.
  `mobil_textured.glb` tidak ada di Drive → dipakai `lamborghini.glb` (lihat ASSETS_MISSING.md).

## M2 — Dunia & aset (SELESAI)
- Akar "tekstur buram" ditemukan & diperbaiki secara berlapis:
  `pack_assets.py` mengecilkan tekstur, loader `gltf.cpp` membatasi mip terlalu
  kecil (`GL_LINEAR_MIPMAP_LINEAR` sampai 1 px), dan tidak ada anisotropic filter.
  Sekarang: tekstur 1024 px (pohon 512, LOD 256), JPEG q92 4:4:4 + unsharp mask,
  mip dibatasi 1/4 resolusi, anisotropic 8x bila GPU mendukung.
- Trotoar prosedural (bukan Blender, D1): permukaan + dinding kerb 0,18 m,
  3 pita per sisi. Desa 1,2–1,5 m, kota 2,0 m, tanah tanpa trotoar.
  Dipaket sebagai model terpisah `roads_{desa,kota}_trotoar.glb` supaya cukup
  2 draw call per jenis jalan (sebelumnya 76 draw call karena sub-mesh).
- Hutan: 120 → 48 m²/pohon (2,5x), total 162.447 instance / 659 chunk.

## M3 — Validasi (SELESAI)
- `tools/verify_scene.py`: SEMUA CEK VISUAL LULUS → 0 verteks jalan tenggelam,
  0 objek di laut, 0 objek di atas jalan, 0 pohon di lereng > 26°, 0 objek melayang.
- `tools/host_test`: SEMUA UJI LULUS (0 masalah). `tools/game_sim` (ASan, 400 frame):
  SELESAI OK, 76 draw call/frame.

## M4 — APK & tanda tangan (SELESAI)
- **Bug fatal diperbaiki**: ID blok skema v2 salah (`0x1B93A55A` → `0x7109871A`),
  dan field offset CD di EOCD salah sehingga zip tidak terbaca alat standar.
  Aturan final (diuji ke APK contoh apksig): digest memakai EOCD dengan
  offset = awal blok; EOCD yang ditulis ke disk memakai offset CD yang baru.
- `tools/verify_apk_sign.py` diuji silang: lulus untuk APK kita **dan**
  `golden-aligned-v2-out.apk` (buatan Google) → skema terverifikasi benar.
- APK `output/nusa-rush-s3-debug.apk`: 20,99 MB, 69 entri zip, 67 aset,
  `lib/arm64-v8a/libnusa.so` (2,0 MB), extractNativeLibs=false,
  data `.so` selaras 16384 byte (syarat Android 15+), minSdk 24 / targetSdk 34.
