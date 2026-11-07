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
