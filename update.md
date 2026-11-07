# UPDATE — Nusa Rush (status build & rilis)

Dokumen ini mencakup keadaan proyek saat ini, apa yang sudah selesai & diverifikasi,
bug yang ditemukan/diperbaiki, proses build, dan rencana kerja selanjutnya. Fokus
pada apa yang **nyata & teruji**, bukan daftar keinginan ("baplan").

---

## 0. Status sekarang — APK bisa dipasang (sesi 3, selesai)

Masalah utama ("ada masalah saat mengurai paket" / `INSTALL_PARSE_FAILED_NO_CERTIFICATES`
di Android 10) sudah diperbaiki dan **terverifikasi byte-level** terhadap 5 APK asli
(hello-world, app-prod-debug, multiple_locale_appname_test, tvleanback, dll).

Berkas akhir (workspace):
- `output/Nusa rush V0.0.1_3.apk` — 22,78 MB, 70 entri, 2 ABI
- `output/nusa-rush-s3-debug.apk` — versi build yang sama (cadangan)

Spesifikasi APK akhir:
| Item | Nilai |
|---|---|
| Package | `com.arenalab.nusarush` |
| versionName | `0.0.1_3` |
| versionCode | `3` |
| minSdk / targetSdk | `24` / `34` |
| Activity | `android.app.NativeActivity` (exported) |
| Native lib | `libnusa.so` (arm64-v8a ELF64 + armeabi-v7a ELF32) |
| Tanda tangan | APK Signature Scheme v2 **valid** (RSA 2048), v3 tidak dipakai |
| Ukuran | ~22,78 MB, 67 aset, tidak dikompresi & selaras 16 KB |
| Tekstur GLB | ≥1024 px, q92, 4:4:4, mipmap terbatas 1/4 resolusi, anisotropic 8× |
| Trotoar | desa & kota (GLB terpisah per jenis jalan) |
| Hutan | banyak pohon (instancing + LOD) |

Hasil verifikasi terakhir (`tools/verify_apk.py` + `androguard`):
- zip sehat, central directory di akhir, semua CRC entri lolos
- AXML sinkron: 44 string, 23 resource-map ID, 10 elemen, END_ELEMENT `size=24`
- package, versionName (`0.0.1_3`), versionCode (`3`), minSdk 24, targetSdk 34
- `android.app.lib_name = nusa`, activity exported
- ELF arm64-v8a + armeabi-v7a valid, `ANativeActivity_onCreate` terekspor
- Aset wajib ada: `AndroidManifest.xml`, `assets/generated/island.bin`,
  `assets/generated/props.bin`, `assets/generated/roads.json`
- Tanda tangan v2 valid (cocok dengan skema asli apksig, potongan 1 MB)

**Catatan jujur:** Saya tidak bisa memasang APK ke HP fisik dari sandbox, jadi bukti
adalah verifikasi skema terhadap APK asli + validasi struktur, bukan uji langsung di
perangkat. Jika masih gagal saat dipasang, kirim teks error persis dari HP.

---

## 1. Bug yang ditemukan & diperbaiki (lihat `BUGS.md` detail)

Bug fatal install (B1–B3):
- **B1** Digest tanda tangan v2 dihitung per bagian utuh (salah untuk APK > 1 MB)
  → diperbaiki ke skema asli apksig (potongan 1 MB digabung dalam satu daftar digest).
- **B2** Hanya ABI arm64-v8a → HP 32-bit menolak. Sekarang ada **armeabi-v7a** juga.
- **B3** `versionCode` tidak naik → update ditolak. Sekarang `versionCode 3`.

Bug aset/render (B4–B9): terrain menutupi jalan, jalan tenggelam, objek menempel jalan,
tekstur GLB buram, draw call jalan meledak, salah ABI pada badging, versionCode.
Semua sudah selesai (lihat `BUGS.md`).

Bug baru ditemukan dalam debug gameplay sesi ini:
- **B10** Pemakaian pointer `i32` pada offset tidak ter-align di `world.cpp`
  (`Props::load` memuat `props.bin`). Bisa crash (SIGBUS) pada HP ARM. Diperbaiki
  dengan `memcpy` per elemen (portable). Teruji via gameplay host test (ASan+UBSan,
  tanpa crash).
- Debug gameplay **tidak menemukan** bug tombol "nyangkut" (`gameTouchCancelAll`
  berfungsi), tidak ada penurunan fisika aneh dalam simulasi host.

---

## 2. Proses build (tanpa Android SDK / NDK resmi)

`dl.google.com` diblokir di sandbox, jadi SDK/NDK resmi tidak dapat diunduh. Build
diganti dengan toolchain sendiri:

- **Kompilasi C++17:** `zig` (lewat `ziglang` PyPI) → target
  `aarch64-linux-android` (ELF64) dan `arm-linux-android` (ELF32, `-mfloat-abi=softfp`).
- **Header/sysroot:** bionic + EGL + GLES3 dari mirror AOSP (`tools/make_sysroot.sh`).
- **Stub pustaka 32-bit:** `sysroot/lib32/*.so` dibuat otomatis oleh `build_apk.sh`
  (simbol asli berasal dari HP saat runtime).
- **Paket APK + tanda tangan v2:** `tools/apkbuild.py build` (AndroidManifest biner,
  zip, digest v2). CLI: `--out --lib --lib32 --assets --version-name --version-code
  --min-sdk --target-sdk`.
- **Kunci tanda tangan:** `keystore/debug.key.pem` + `debug.cert.der` (RSA 2048,
  dibuat oleh `tools/make_keystore.sh`; bukan kunci produksi).
- **JDK:** via `jdk4py` PyPI (Temurin 25.0.2, hanya `java`/`keytool`, tanpa
  `javac`/`jarsigner` — tanda tangan v1 buatan sendiri di Python belum dibuat).

Cara build ulang:
```
cd /home/user/xyzcmd
./build_apk.sh
```
Output: `output/nusa-rush-s3-debug.apk`. Salinan akhir:
`output/Nusa rush V0.0.1_3.apk`.

Verifikasi:
```
./.venv/bin/python tools/verify_apk.py output/nusa-rush-s3-debug.apk
```

---

## 3. Isi APK & keputusan teknis

- **Native:** 2 ABI agar HP 32-bit lama (Android 10, Snapdragon 4xx/6xx, RAM 2–3 GB)
  tetap bisa memasang. `libnusa.so` diekspor `ANativeActivity_onCreate`.
- **Tekstur:** ≥1024 px (user menilai tekstur GLB "rada burem"); q92, 4:4:4,
  unsharp mask, `GL_TEXTURE_MAX_LEVEL` terbatas 1/4 resolusi, anisotropic 8×.
- **Trotoar:** khusus jalan **desa dan kota** (GLB terpisah per jenis jalan, 2 draw call
  per jenis). Di hutan ada banyak pohon (instancing + LOD).
- **Aset runtime:** `island.bin`, `props.bin`, `roads.json` dipaket ke `app/assets`.
- **Versi:** `versionName 0.0.1_3`, `versionCode 3` (harus ≥ versi terpasang agar update
  diterima; cabut aplikasi lama sebelum pasang baru).

---

## 4. Rencana kerja selanjutnya (konkrit, bertahap)

Prioritas diturunkan dari dampak nyata dan risiko rendah:

1. **Tanda tangan v1 (JAR)** — saat ini hanya v2. Beberapa perangkat lama memerlukan
   v1; JDK resmi tidak punya `jarsigner`, jadi harus buat sendiri di Python.
2. **Uji di HP nyata** — memasang APK dan mencatat error persis jika gagal, untuk
   memastikan tidak ada masalah instalasi tersembunyi.
3. **Perbaikan tepat waktu** — menangani laporan crash saat pause/resume dan
   `surfaceDirty` (area yang masih layak dipantau di `platform.cpp`).
4. **Konten gameplay** — nitro, drift skor, PR stunt, koleksi (berdasarkan rencana
   Tahap A–B di versi sebelumnya), prioritas sesuai dampak.

---

## 5. Pertanyaan terbuka (menunggu keputusan)

1. Mulai dari tahap pekerjaan mana di bagian 4? (saran: tanda tangan v1 dulu)
2. Mau tetap tanpa musuh AI (eksplorasi + time trial), atau langsung tambah lawan?
3. Polisi/heat level perlu atau tidak? Ini paling menambah beban kerja.
4. Kontrol: tetap tombol analog, atau tambah TouchDrive (setir otomatis)?
5. Bahasa UI: Indonesia, Inggris, atau keduanya?

---

_Sumber: verifikasi `tools/verify_apk.py`, `androguard` 4.1.4, `cryptography` 50.0.2,
`ziglang` cross-compile, ASan/UBSan gameplay host test. File penting & source sudah
diunggah ke Google Drive (file baru saja, file lama dibuang) dan disalin di workspace._
