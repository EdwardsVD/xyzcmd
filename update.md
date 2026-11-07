# UPDATE — rencana besar Nusa Rush (riset game pembanding)

Status: **perbaikan install selesai** (lihat bagian 0). Rencana di bawah ini belum
dikerjakan — menunggu perintah. Semua angka "target" adalah usulan saya, bukan janji.

---

## 0. Yang sudah diperbaiki (APK gagal install di Android 10)

Ada **tiga** penyebab, dua di antaranya fatal:

| # | Masalah | Akibat di HP | Perbaikan |
|---|---|---|---|
| 1 | Digest tanda tangan v2 dihitung per bagian utuh | HP menolak dengan `INSTALL_PARSE_FAILED_NO_CERTIFICATES` / "Paket tampaknya rusak" — **semua APK > 1 MB gagal** | `tools/apkbuild.py` sekarang memakai skema asli apksig: potongan 1 MB digabung dalam **satu** daftar `sha256(0x5a‖u32(n)‖semua digest potongan)`. Dibuktikan cocok pada 5 APK asli (hello-world, app-prod-debug, tvleanback, dll). |
| 2 | Hanya ABI `arm64-v8a` | HP Android 10 32-bit (Snapdragon 4xx/6xx lama, RAM 2–3 GB) menolak: `INSTALL_FAILED_NO_MATCHING_ABIS` | APK sekarang berisi **dua** ABI: `arm64-v8a` + `armeabi-v7a` (keduanya tidak dikompresi & selaras 16 KB). |
| 3 | `versionCode` 1 dan nama versi tidak naik | Kalau versi lama sudah terpasang, update ditolak (`INSTALL_FAILED_VERSION_DOWNGRADE` / signature mismatch) | `versionCode 3`, `versionName 1.0.3`. **Cabut dulu aplikasi lama** sebelum memasang yang baru. |

Hasil verifikasi terakhir:
- `digest isi APK cocok: True`, `tanda tangan RSA: VALID`, `public key cocok: True`
- zip sehat (70 entri), `zip.testzip() == None`
- `native-code: arm64-v8a armeabi-v7a`, APK 22,78 MB
- `host_test` LULUS, `game_sim` (ASan) OK

Catatan jujur: saya tidak bisa memasang APK ke HP dari sandbox, jadi bukti di atas
adalah verifikasi skema terhadap APK asli + validasi struktur, bukan uji di perangkat.
Kalau masih gagal, kirim teks error persis dari HP (mis. "There was a problem parsing
the package") supaya saya bisa pastikan penyebab berikutnya.

---

## 1. Ringkasan game pembanding yang saya pelajari

### Forza Horizon 5 — acuan "dunia terbuka yang hidup" ([1](https://lubbil.com/article/forza-horizon-5-map-size-location-full-map-and-more), [2](https://www.thegamer.com/forza-horizon-5-map-features-highlights/), [3](https://shapes.inc/fandom/forza-horizon-5/accolades-and-progression))
- Peta ±**107 km²**, **11 biome** (kanopi, pesisir tropis, sawah, perbukitan kering,
  hutan, gurun hidup, pesisir batu, gurun pasir, rawa, kota, gunung api).
- Progresi lewat **Accolade** (bukan kampanye linear): poin dari balapan + aksi
  (speed trap, speed zone, danger sign, drift zone, trailblazer).
- **Skill chain**: drift/near-miss/jump dirangkai, dikali multiplier, lalu "dibank".
  `[5](https://mashable.com/article/forza-horizon-5-review)`
- **PR Stunt**: 31 speed trap, 22 speed zone, 20 danger sign, 20 drift zone,
  10 trailblazer (103 total, bintang 1–3).
- **Barn find** (14 mobil tersembunyi), **bonus board** (250 papan bisa dihancurkan),
  rumah pemain, fast travel, cuaca dinamis + 4 musim yang mengubah traksi.
- Festival/outpost sebagai "pintu" disiplin balap (Road, Dirt, Cross Country, Street).

### Need for Speed: Most Wanted (2012) — acuan "kota padat + polisi" ([4](https://www.empathygame.org/need-for-speed-most-wanted-2012/), [5](https://www.beamng.com/threads/fairhaven-usa-a-need-for-speed-most-wanted-2012-port.96401/))
- Peta **Fairhaven ±72 km²**: grid downtown, jalan layang, terowongan, dermaga,
  kawasan industri, taman, bandara, pantai, highway lingkar **I-92**.
- Mobil tidak dibeli tapi **ditemukan** di *Jack Spot* — eksplorasi jadi reward.
- **Polisi** jadi inti: heat level, spike strip, roadblock, bengkel untuk mengecat
  ulang & memperbaiki, pit untuk mengganti ban.
- Event: **Sprint** (A→B), **Circuit** (2–3 lap), **Speed Run** (rata-rata kecepatan).
- Koleksi: **billboard**, **smash gate**, **speed camera**, **shortcut**.
- Balapan bebas rute: hanya titik awal & akhir, pemain pilih jalur sendiri.

### Burnout Paradise — acuan "kebebasan & koleksi" ([1](https://tvtropes.org/pmwiki/pmwiki.php/VideoGame/BurnoutParadise), [4](https://www.pcmag.com/reviews/burnout-paradise-remastered-for-pc))
- Setiap persimpangan punya **event** (Race, Road Rage, Marked Man, Stunt Run,
  Burning Route) — tidak perlu menu.
- **Road Rules**: waktu terbaik & skor "Showtime" per ruas jalan.
- **Smash gate** (kuning), **billboard** (merah), **super jump** (biru) dengan
  penanda warna; menyelesaikan semua koleksi membuka mobil khusus.
- **Drive-through**: bengkel (perbaikan), pompa bensin (isi boost), cat, junkyard.
- Boost diisi oleh gaya menyetir (drift, near-miss, lawan yang ditabrak).

### Asphalt 9: Legends / Asphalt Legends — acuan "mobile arcade" ([1](https://www.stuff4android.com/2023/05/asphalt-9-legends/), [3](https://www.bananatic.com/games/asphalt-9-legends-405/asphalt-9-legends-a-professional-guide-23785))
- **Nitro** tiga tingkat: nitro biasa, *perfect nitro* (tekan saat zona biru),
  *shockwave* (tekan ganda) — manajemen nitro jadi inti skill.
- Drift mengisi nitro; **360° spin** (tekan ganda rem) untuk menabrak lawan.
- TouchDrive (setir otomatis) vs manual; kontrol harus ramah layar sentuh.
- Struktur: Career (bab/musim) + event harian + multiplayer + klub.

### Daftar game balap Android teratas 2026 (untuk konteks pasar) ([1](https://www.androidhire.com/best-racing-games-for-android))
Asphalt Legends, **CarX Street** (open-world drift), GRID Autosport (sim),
Rush Rally 3, CarX Drift Racing 3, CSR 2 (drag), NFS No Limits, Racing Master,
Riptide GP (jet ski), Beach Buggy Racing 2 (kart), Assoluto Racing, Apex Racing.

Pola yang berulang di semua game di atas, dan paling menentukan rasa "game besar":
1. **Peta yang bisa dikenali** (landmark, distrik, lingkar utama).
2. **Aktivitas di mana-mana** (event di persimpangan, PR stunt, koleksi).
3. **Umpan balik kecepatan** (nitro, kamera, kabut kecepatan, suara).
4. **Progresi** (koin/XP → mobil/perk → target baru).
5. **Sesuatu untuk dikejar setelah cerita habis** (rekor per ruas jalan, koleksi).

---

## 2. Rencana Nusa Rush (usulan, bertahap)

Prinsip: **ambil sebagian kecil** dari tiap game di atas — bukan meniru skalanya.
Peta kita sekarang ±90 km² (9,6 × 9,4 km) yang sudah sebanding FH5/MW, jadi
**fokus ke isi & rasa**, bukan menambah luas.

### Tahap A — Rasa mengemudi (paling terasa duluan)
- **Nitro**: bar terisi dari drift, near-miss, dan lompatan; 3 tingkat
  (normal / perfect / shockwave) seperti Asphalt 9. Tombol khusus di layar.
- **Drift skor**: deteksi slip angle, rantai skill + multiplier + "bank", seperti
  skill chain Forza. Tampil di HUD.
- **Kamera & efek kecepatan**: FOV dinamis (sudah ada), tambah garis kecepatan,
  getaran halus, motion blur murah (2 sampel), suara mesin berlapis.
- **Handling**: kurva torsi realistis, rem tangan untuk drift, perbaikan fisika
  saat tanah/aspal (sudah ada grip 0,98 vs 0,55).

### Tahap B — Aktivitas di peta (peta terasa hidup)
- **PR Stunt**: 12 speed trap, 8 drift zone, 8 danger sign (ramp) — bintang 1–3,
  tersebar di desa/kota/pantai/gunung. Datanya JSON, mudah ditambah.
- **Koleksi**: 40 billboard bisa dihancurkan, 30 smash gate, 12 super jump,
  dengan penanda warna seperti Burnout.
- **Event di jalan**: titik event di pinggir jalan (bukan menu): Sprint, Circuit,
  Speed Run, Eliminasi. AI lawan sederhana (waypoint + kecepatan target).
- **Barn find**: 6 mobil tersembunyi di lokasi sulit (gua, puncak, dermaga).

### Tahap C — Kota & landmark (peta mudah diingat)
- **Distrik kota**: downtown padat, kawasan industri, dermaga, pantai, bandara
  kecil, taman — memakai aset gedung yang ada + trotoar yang sudah jadi.
- **Lingkar utama** seperti I-92: jalan lebar keliling pulau untuk balapan cepat.
- **Terowongan & jalan layang** pendek (2–3) sebagai shortcut.
- **Landmark**: menara, jembatan besar, stadion, mercusuar — penanda navigasi.

### Tahap D — Progresi & UI
- **HUD**: speedometer, bar nitro, minimap bulat dengan ikon aktivitas, panah rute.
- **Progresi**: koin dari event/aksi → buka 6–10 mobil + upgrade (mesin, ban, nitro).
- **Menu ringan** tanpa pustaka UI: peta besar, garasi, daftar event.
- **Simpan progres**: file JSON di penyimpanan internal.

### Tahap E — Polisi (opsional, terasa "NFS")
- Heat level 1–5, pengejar AI yang mengikuti jalan, spike strip, roadblock.
- Bengkel drive-through untuk mengecat/memperbaiki (mengakhiri pengejaran).

### Tahap F — Polish teknis
- Streaming chunk lebih halus, LOD 3 tingkat, lampu malam (matahari terbenam).
- Suara: mesin, ban, angin, tabrakan (sintesis sederhana, tanpa file besar).
- Opsi grafis (kualitas tekstur, kepadatan pohon) untuk HP kelas bawah.

---

## 3. Urutan kerja yang saya sarankan

1. **A** (nitro + drift skor + HUD) — dampak terbesar, risiko kecil.
2. **B** (PR stunt + koleksi) — memanfaatkan peta yang sudah ada.
3. **D** (minimap + progresi + simpan) — membuat semuanya terasa terhubung.
4. **C** (distrik & landmark) — butuh kerja aset paling banyak.
5. **E**, lalu **F**.

Setiap tahap akan ditutup dengan: `host_test` + `game_sim` ASan lulus, `verify_scene`
lulus, APK baru (`versionCode` naik), verifikasi tanda tangan, lalu unggah ke Drive.

---

## 4. Pertanyaan yang menunggu jawabanmu

1. Mulai dari tahap mana? (saran saya: A)
2. Mau tetap **tanpa musuh AI** dulu (fokus eksplorasi + time trial), atau langsung
   tambah lawan?
3. Polisi (tahap E) perlu atau tidak? Ini yang paling menambah beban kerja.
4. Kontrol: tetap tombol analog seperti sekarang, atau tambah **TouchDrive**
   (setir otomatis, tinggal gas/rem)?
5. Bahasa UI: Indonesia, Inggris, atau keduanya?
