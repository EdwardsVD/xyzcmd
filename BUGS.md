# BUGS & PERBAIKAN — Nusa Rush sesi 3

Catatan bug nyata yang ditemukan, penyebabnya, dan cara memperbaikinya. Diurutkan
dari yang paling merusak.

## B1 — APK tidak bisa dipasang (fatal) — SELESAI
**Gejala:** APK 20 MB gagal di HP Android 10 ("Paket tampaknya rusak" /
`INSTALL_PARSE_FAILED_NO_CERTIFICATES`).

**Penyebab:** digest APK Signature Scheme v2 salah. Saya menghitung
`sha256(0xa5‖len‖isi)` untuk **tiap bagian utuh** (isi, central directory, EOCD)
lalu membungkus tiap bagian dengan `0x5a` sendiri. Apksig sebenarnya memecah
bagian-bagian itu menjadi potongan **1 MB** dan menggabungkan semuanya menjadi
**satu** daftar:

```
digest  = sha256(0x5a ‖ u32(jumlah_potongan) ‖ semua digest_potongan)
potongan= sha256(0xa5 ‖ u32(panjang) ‖ isi_potongan)
```

Kesalahan ini **tidak terlihat** pada APK kecil (< 1 MB, hanya 1 potongan per
bagian) — itulah sebabnya verifikasi saya dulu "lulus" tetapi tetap ditolak HP.

**Bukti:** setelah diperbaiki, verifier mencocokkan digest pada 5 APK asli:
`hello-world.apk`, `app-prod-debug.apk`, `multiple_locale_appname_test.apk`,
`com.android.example.text.styling.apk`, `com.example.android.tvleanback.apk`.
Sebelum perbaikan, kelimanya **gagal**.

**Sumber acuan:** `ApkSigningBlockUtils.computeOneMbChunkContentDigests`
(apksig AOSP, `CONTENT_DIGESTED_CHUNK_MAX_SIZE_BYTES = 1024*1024`).

## B2 — HP 32-bit tidak bisa memasang (fatal) — SELESAI
**Gejala:** `INSTALL_FAILED_NO_MATCHING_ABIS` pada HP Android 10 32-bit.
Hanya ada `lib/arm64-v8a/libnusa.so`.

**Perbaikan:** APK sekarang berisi `armeabi-v7a` juga.
Kendala yang harus diatasi untuk build 32-bit:
- zig menolak target `armv7a-linux-androideabi`; dipakai `arm-linux-android`
  dengan `-mfloat-abi=softfp` (ABI v7).
- `sysroot/include/asm/sigcontext.h` (layout arm64) memakai `__uint128_t` yang
  tidak ada di ARM32 → `app/jni/arm32_fix.h` dipasang lewat `-include`.
- Linker butuh stub pustaka 32-bit → `sysroot/lib32/*.so` dibuat otomatis
  oleh `build_apk.sh`.
Hasil: `readelf` menunjukkan `ELF32 / ARM`, `ANativeActivity_onCreate` terekspor,
kedua `.so` tidak dikompresi dan selaras 16384 byte.

## B3 — Terrain menutupi jalan — SELESAI
Terrain dibangun dari 100 chunk dan **semua** digambar lebih dulu (termasuk yang
ada di luar jarak kabut), menulis depth di depan jalan/objek yang lebih jauh.
Diperbaiki: chunk di luar `kabutAkhir + 240 m` dilewati. Draw call turun dari 76
menjadi 64 per frame.

## B4 — Jalan tenggelam ke dalam terrain — SELESAI
Urutan pipeline lama membuat jalan **sebelum** terrain diratakan di bawah
bangunan, sehingga 17–29% verteks jalan tertimbun. Urutan baru:
`build_world` (ratakan) → `build_roads` (jalan mengikuti terrain final) →
`build_world` (sebar objek) → `fix_scene` → `pack_assets`.
`verify_scene` sekarang: **0 verteks tenggelam**.

## B5 — Pohon & bangunan menempel jalan — SELESAI
Larangan berbasis grid 37,5 m terlalu kasar (723 objek di tepi jalan). Diganti
peta jarak presisi (`road_d`) ke tepi jalan; objek vektor dipindah/dibuang oleh
`fix_scene`. Sekarang: **0 objek di atas jalan**.

## B6 — Tekstur GLB buram — SELESAI
Empat penyebab sekaligus: (a) pemaket mengecilkan tekstur, (b) loader membatasi
tekstur, (c) mipmap sampai 1×1 px membuat permukaan miring kabur, (d) JPEG 4:2:0.
Perbaikan: 1024 px q92 4:4:4 + unsharp mask, `GL_TEXTURE_MAX_LEVEL` dibatasi
1/4 resolusi, anisotropic 8×.

## B7 — Draw call jalan meledak (1174/frame) — SELESAI
Menggambar jalan per sub-mesh berarti ratusan tarikan per frame. Trotoar
dipisahkan menjadi GLB sendiri (`roads_{desa,kota}_trotoar.glb`) sehingga cukup
2 draw call per jenis jalan.

## B8 — `versionCode` tidak pernah naik — SELESAI
Versi lama tidak bisa ditimpa. Sekarang `versionCode 3` / `versionName 1.0.3`.

## B9 — Salah ABI pada `badging` — SELESAI
`tools/apkbuild.py badging` selalu mencetak `arm64-v8a`. Sekarang membaca isi zip.
