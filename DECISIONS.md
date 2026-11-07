# DECISIONS — Nusa Rush (sesi 3)

Catatan keputusan yang saya ambil sendiri (sesuai aturan: jangan bertanya balik).

## D1 — Lingkungan build (M1)
- Sandbox hanya mengizinkan host: github.com, codeload.github.com, api.github.com,
  registry.npmjs.org, pypi.org/files.pythonhosted.org.
- **dl.google.com diblokir** → Android SDK resmi, cmdline-tools, build-tools, aapt2/apksigner,
  dan **NDK** tidak bisa diunduh. **download.blender.org juga diblokir** → Blender 4.2 LTS
  tidak bisa dipasang. Java (JDK) juga tidak ada & tidak bisa diunduh (api.adoptium.net diblokir).
- Pengganti yang dipakai (semua legal, open source):
  - **Kompiler**: `zig` 0.13 dari PyPI (`pip install ziglang`) dipakai sebagai cross-compiler
    C/C++ → target `aarch64-linux-android` (ELF64 AArch64). Tetap C++17 murni tanpa engine.
  - **Header Android (bionic + android/* + EGL + GLES3 + jni)**: diambil dari mirror AOSP di
    GitHub (`aosp-mirror/platform_bionic`, `KhronosGroup/EGL-Registry`, `OpenGL-Registry`,
    header `android/*` dari mirror NDK r20). Disusun oleh `tools/make_sysroot.sh`.
  - **APK**: dibuat tanpa aapt2/apksigner oleh `tools/apkbuild.py` (AndroidManifest biner/AXML,
    zip stream-aligned, signature scheme v2 dengan `cryptography`). Verifikasi pakai `androguard`.
  - Semua ketiadaan ini dicatat di `ASSETS_MISSING.md` + `PROGRESS.md` (bagian kendala).

## D2 — Blender tidak ada → tool Python
`build_roads.py`, `build_props` dan ekstraksi pulau dikerjakan skrip Python murni
(numpy/Pillow) yang membaca vertex pulau dan menulis GLB sendiri (`tools/glbwrite.py`).
Alasan: Blender tak bisa diunduh (D1) dan juga berat untuk RAM 3 GB.

## D3 — Data pulau
Heightmap & warna biome diambil langsung dari vertex `pulau_P2.glb` (grid 256×256) dan
dikemas ke `assets/generated/island.bin` (tinggi f32 + warna sRGB u8 + indeks biome u8).
Warna biome diklasifikasi jarak-terdekat ke swatch asli generator pulau
(pantai, kota, desa, hutan, gunung, danau). Hasil validasi: 72,7% laut, kota 20,5%,
desa 21,9%, hutan 23,5%, gunung 23,1%, pantai 8,6% (pantai lebih kecil dari target
generator karena tepi blur; dipakai apa adanya). Render atas: `output/preview/island_top.png`.

## D4 — Jalan
Jaringan jalan dibangun procedural di `tools/build_roads.py`: lingkar + cabang, snap tinggi
terrain (offset +0,05 m), kemiringan maksimum 12% (koridor pegunungan 10%), tidak ada jalan
di laut (h ≥ 0,5 m), dibagi per segmen 200 m, tekstur prosedural 512 px tertanam di GLB.
Output: `roads_desa.glb`, `roads_kota.glb`, `roads_tanah.glb`, `roads.json` (centerline+grip).

## D5 — Renderer
OpenGL ES 3.0, tanpa libc++ (freestanding: tanpa STL/exception/RTTI; container kecil sendiri),
terrain di-generate di runtime dari `island.bin`, instancing untuk pohon/rumah/gedung,
LOD `_lod1` untuk jarak > 120 m, streaming per chunk 400 m, batas ~6000 instance.

## D6 — Kontrol
NativeActivity + input sentuh langsung (multi-touch), tombol analog di kiri/kanan bawah
digambar dengan shader 2D, 44 dp minimum, semi-transparan.
