# Nusa Rush — game balap jelajah pulau (sesi 3)

Game balap *open world* satu pulau: jalan desa dan kota dengan trotoar, hutan rapat,
gunung, pantai, dan satu mobil sport. Dibangun sebagai **aplikasi Android native
arm64-v8a** tanpa mesin game dan tanpa Android Studio.

```
app/jni/        kode C++17 (native activity, GLES 3.0, fisika, dunia)
tools/          pipeline aset + pembuat APK + penanda tangan v2 (Python)
assets/         sumber .glb (Drive) dan aset hasil pipeline (assets/generated)
app/assets/     aset runtime yang dipaket ke dalam APK
output/         APK jadi + render pratinjau
build_apk.sh    build satu perintah: aset -> C++ -> .so -> APK bertanda tangan
```

## Hasil

| Item | Nilai |
|---|---|
| Package | `com.arenalab.nusarush` (NativeActivity, minSdk 24, targetSdk 34) |
| ABI | `arm64-v8a` + `armeabi-v7a` (HP 32-bit & 64-bit) |
| APK | `output/nusa-rush-s3-debug.apk` — 22,8 MB, 67 aset, tanda tangan v2, versionCode 3 |
| Dunia | pulau 9,6 km × 9,4 km, tinggi −35…240 m, 7 biome |
| Jalan | desa 14,6 km, kota 27,0 km, tanah 6,2 km (total 47,8 km) |
| Objek | 162.447 instance (pohon, rumah desa, gedung kota) dalam 659 chunk |
| Pohon | 76.278 oak, 74.346 spruce, 9.825 sakura, 897 palem |

## Build

```bash
./tools/make_sysroot.sh        # sekali: header Android/EGL/GLES3
./build_apk.sh                 # aset (bila perlu) + kompilasi + APK + tanda tangan
REPACK=1 ./build_apk.sh        # paksa bangun ulang seluruh aset
./.venv/bin/python tools/verify_apk_sign.py output/nusa-rush-s3-debug.apk
```

Butuh Python 3 + venv (`.venv`: numpy, Pillow, cryptography, androguard, ziglang)
dan `zig` 0.13 sebagai cross-compiler C++17 → `aarch64-linux-android`.

> Catatan lingkungan: `dl.google.com`, maven, gradle, dan `download.blender.org`
> diblokir di sandbox ini, jadi SDK/NDK/Blender resmi tidak bisa dipakai. Lihat
> `DECISIONS.md` D1–D2 untuk penggantinya (sysroot dari mirror AOSP, zip/APK
> ditulis sendiri oleh `tools/apkbuild.py`).

## Uji

```bash
g++ -std=c++17 -O1 -include stdio.h -I tools/hostinc -I app/jni -I . \
    tools/host_test.cpp app/jni/world.cpp app/jni/car.cpp app/jni/gltf.cpp \
    app/jni/render.cpp -o build/host_test && ./build/host_test app/assets

./.venv/bin/python tools/verify_scene.py     # tata letak dunia
.venv/bin/python tools/preview_3d.py         # render pratinjau ke output/preview/
```

Status terakhir: `host_test` **SEMUA UJI LULUS**, `game_sim` (ASan, 400 frame)
**SELESAI OK** dengan 64 draw call/frame, `verify_scene` **SEMUA CEK VISUAL LULUS**,
`verify_apk_sign` **digest cocok + tanda tangan RSA valid**.

## Kontrol

- Tombol kiri bawah: belok kiri / kanan (analog, makin jauh dari pusat makin tajam).
- Tombol kanan bawah: gas dan rem (analog).
- Tombol merah kanan atas: reset ke jalan terdekat.

## Berkas di Google Drive

Folder **game SESI-3**:
<https://drive.google.com/drive/folders/1WpllqpdioX9vQaJmUA-TX1o7dOkhSScL>

- `nusa-rush-s3.apk` — APK siap pasang
- `nusa-rush-s3-source.zip` — seluruh kode & tools
- `nusa-rush-s3-assets.zip` — `assets/generated` + `app/assets`

## Kalau gagal install

Lihat `BUGS.md` B1–B2 (dua bug fatal yang sudah diperbaiki: digest v2 dan ABI
32-bit). Sebelum memasang versi baru, **cabut dulu** aplikasi lama karena
tanda tangannya berganti. Rencana pengembangan berikutnya ada di `update.md`.

## Isi penting

- `PROGRESS.md` — status tiap milestone, termasuk bug fatal yang ditemukan & diperbaiki.
- `DECISIONS.md` — semua keputusan teknis beserta alasannya (D1–D10).
- `tools/apkbuild.py` — penulis AndroidManifest biner, zip selaras, dan **skema
  tanda tangan v2** (ID blok `0x7109871a`; digest EOCD memakai offset awal blok,
  EOCD di disk memakai offset CD baru).
- `tools/pack_assets.py` — pemaket GLB: 1024 px, JPEG q92 4:4:4 + unsharp mask,
  buffer `[verts][idx][tex0][tex1]`, metadata `nusa.prims`.
- `tools/build_roads.py` — jalan prosedural + **trotoar 3D** (permukaan, kerb 0,18 m).
- `tools/build_world.py` — sebaran objek per biome, perataan terrain, jarak presisi ke jalan.
