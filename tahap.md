# TAHAP.md — Pengembangan Nusa Rush (langkah demi langkah)

Dokumen ini mengatur urutan pekerjaan agar tidak terlewat, terutama ketika
chat terputus. Setiap tahap harus **selesai & diverifikasi** sebelum melanjut
ke tahap berikutnya. Tandai `[x]` saat selesai.

---

## Tahap 0 — Perbaikan instalasi APK (SELESAI)

- [x] Masalah: "ada masalah saat mengurai paket" kadang muncul, kadang tidak.
- [x] Penyebab: APK hanya ditandatangani skema v2. Beberapa perangkat
  membutuhkan tanda tangan v1 (JAR). Ditambahkan v1 (MANIFEST.MF, CERT.SF,
  CERT.RSA) bersama v2 di `tools/apkbuild.py`.
- [x] Build ulang APK: `output/Nusa rush V0.0.2_3.apk` (22,8 MB, v1+v2 signed,
  arm64-v8a + armeabi-v7a, versionName 0.0.2_3, versionCode 4).
- [x] Verifikasi: `tools/verify_apk.py` semua lulus, androguard
  `signed v1: True`, `signed v2: True`. minSdk 24 → kompatibel Android 8+.
- [x] Upload ke Drive folder `game SESI-3` (ganti file lama):
  - `Nusa rush V0.0.2_3.apk` (id `1ZOMGrru-cSed8XX2JSlcWF5Wq7DCUPWS`)
  - `nusa-rush-source-v0.0.2_3.zip` (id `1LDx7C_NVZTIxZHrPrlRTgvOzB3m0_iy9`)
- [x] Salinan disimpan di workspace `output/`.

---

## Tahap 1 — Dokumentasi: tahap.md + update.md (SELESAI)

- [x] `tahap.md`: daftar langkah demi langkah (berkas ini).
- [x] `update.md`: tambahkan fitur **lobby** (lihat mobil) dan **pengaturan
  shader** (grafik anime & realistik, sederhana).
- [x] Commit & push ke branch `arena/84395d94-xyzcmd` (v0.0.2_3).

---

## Tahap 2 — Fitur lobby (lihat mobil)

Tujuan: layar lobby sebelum balap, pemain bisa melihat model mobil 3D,
putar-putar, dan lihat spesifikasi singkat.

- [ ] Buat scene lobby sederhana (background + kamera orbit).
- [ ] Muat model mobil (GLB) ke layar lobby dengan instancing.
- [ ] Kontrol sentuh: geser untuk memutar model mobil.
- [ ] Tombol "Mulai Balap" untuk melanjut ke game.
- [ ] Daftar mobil tersedia (scroll atau tombol kiri/kanan).

---

## Tahap 3 — Pengaturan shader (grafik anime & realistik)

Tujuan: pemain bisa menyesuaikan tampilan visual melalui menu pengaturan.

- [ ] Buat menu pengaturan grafis (sederhana: tombol pilihan).
- [ ] Opsi shader **Realistik**: pencahayaan standar, bayangan, refleksi.
- [ ] Opsi shader **Anime**: warna rata (cel-shaded), outline tebal,
  cahaya sederhana.
- [ ] Simpan pilihan ke file JSON di penyimpanan internal.
- [ ] Terapkan shader saat game dimulai (dan saat kembali dari menu pengaturan).

---

## Tahap 4 — Konten gameplay (berdasarkan update.md Tahap A–B)

- [ ] Nitro: bar terisi dari drift, near-miss, lompatan (3 tingkat).
- [ ] Drift skor: slip angle, rantai skill, multiplier, "bank".
- [ ] PR Stunt: speed trap, drift zone, danger sign (ramp).
- [ ] Koleksi: billboard, smash gate, super jump.

---

## Tahap 5 — Progresi & UI

- [ ] HUD: speedometer, bar nitro, minimap.
- [ ] Progresi: koin → buka mobil + upgrade.
- [ ] Simpan progres: file JSON di penyimpanan internal.

---

## Tahap 6 — Polisi & fitur lanjutan (opsional)

- [ ] Heat level 1–5, pengejar AI.
- [ ] Bengkel drive-through.

---

## Catatan

- Setiap tahap diakhiri dengan: `host_test` + `game_sim` ASan lulus,
  APK baru (`versionCode` naik), verifikasi tanda tangan, unggah ke Drive.
- Jangan gunakan APK Unity / APKPure / alternatif lain — build kita sendiri
  dengan `zig` + `apkbuild.py` karena `dl.google.com` diblokir di sandbox.
