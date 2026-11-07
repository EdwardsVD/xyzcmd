#!/bin/bash
# build_apk.sh - bangun APK Nusa Rush tanpa Android SDK/Gradle.
#
# Kenapa tidak Gradle? Di sandbox ini dl.google.com (SDK/NDK resmi) & services.gradle.org
# diblokir. Jadi:
#   * kompiler : zig (pip install ziglang) -> target aarch64-linux-android (ELF64)
#   * header   : sysroot bionic+EGL+GLES3 dari mirror AOSP (tools/make_sysroot.sh)
#   * paket APK: tools/apkbuild.py (AndroidManifest biner + zip + signature v2)
# Kalau kamu punya Android Studio, project Gradle ada di app/ (build.gradle) untuk dipakai normal.
set -e
cd "$(dirname "$0")"
ROOT=$(pwd)
SESI=${SESI:-3}
ZIG=${ZIG:-$ROOT/.venv/lib/python3.11/site-packages/ziglang/zig}
PY=${PY:-$ROOT/.venv/bin/python}
OUT=output
BUILD=build
LIB=nusa
AVD=arm64-v8a
AVD32=armeabi-v7a

echo "== [1/5] sysroot Android"
[ -f sysroot/include/android/native_activity.h ] || ./tools/make_sysroot.sh

echo "== [2/5] aset runtime (pack_assets.py)"
if [ ! -f app/assets/generated/island.bin ] || [ "${REPACK:-0}" = "1" ]; then
  # rantai aset lengkap: pulau -> jalan -> objek -> perbaikan tata letak -> paket runtime
  [ -f assets/generated/island.bin ] || $PY tools/extract_island.py assets/pulau/pulau_P2.glb assets/generated | tail -2
  $PY tools/build_world.py | tail -3          # meratakan terrain (pad bangunan, hotel, taman)
  $PY tools/build_roads.py | tail -4          # SESUDAH pad: jalan mengikuti terrain final
  $PY tools/build_world.py | tail -3          # sebar ulang objek di terrain yang sudah final
  $PY tools/fix_scene.py | tail -6
  $PY tools/pack_assets.py | tail -3
fi

echo "== [3/5] kompilasi C++17 -> $AVD"
mkdir -p $BUILD $OUT
CFLAGS="-target aarch64-linux-android -O2 -fPIC -fno-exceptions -fno-rtti -std=c++17 \
 -ffunction-sections -fdata-sections -Wall -Wno-unused-function -Wno-unused-variable \
 -I sysroot/include -I app/jni"
SRCS="platform.cpp gltf.cpp render.cpp world.cpp car.cpp game.cpp"
OBJS=""
for s in $SRCS; do
  o=$BUILD/${s%.cpp}.o
  echo "   compile $s"
  $ZIG c++ $CFLAGS -c app/jni/$s -o $o
  OBJS="$OBJS $o"
done


# ---- ABI 32-bit (armeabi-v7a): HP lama / Android 10 32-bit tetap bisa memasang
AVD32_OK=0
# -include arm32_fix.h: sysroot berlayout arm64 memakai __uint128_t (tak ada di ARM32).
CFLAGS32="-target arm-linux-android -mfloat-abi=softfp -include app/jni/arm32_fix.h -O2 -fPIC -fno-exceptions -fno-rtti -std=c++17 \
 -ffunction-sections -fdata-sections -Wall -Wno-unused-function -Wno-unused-variable \
 -I sysroot/include -I app/jni"
OBJS32=""
for s in $SRCS; do
  o=$BUILD/arm/${s%.cpp}.o
  mkdir -p $BUILD/arm
  if $ZIG c++ $CFLAGS32 -c app/jni/$s -o $o 2>$BUILD/arm/err.txt; then
    OBJS32="$OBJS32 $o"
  else
    echo "   (armv7 dilewati: $s gagal dikompilasi)"; tail -3 $BUILD/arm/err.txt
    AVD32_OK=1; break
  fi
done

echo "== [3b/5] stub pustaka bionic (sekali saja)"
if [ ! -f sysroot/lib/libc.so ]; then
  mkdir -p sysroot/lib $BUILD/stub
  cat > $BUILD/stub/stub.c <<'EOS'
EOS
  # stub kosong: hanya untuk memuaskan linker; simbol asli dipakai dari HP saat runtime
  printf 'void __nusa_stub(void){}\n' > $BUILD/stub/stub.c
  for L in c m dl log android; do
    $ZIG cc -target aarch64-linux-gnu -shared -fPIC -nostdlib \
      -Wl,-soname,lib$L.so $BUILD/stub/stub.c -o sysroot/lib/lib$L.so
  done
fi

# stub pustaka bionic 32-bit (hanya untuk memuaskan linker; simbol asli dari HP)
if [ "$AVD32_OK" = "0" ] && [ ! -f sysroot/lib32/libc.so ]; then
  mkdir -p sysroot/lib32
  printf 'void __nusa_stub32(void){}\n' > $BUILD/stub/stub32.c
  for L in c m dl log android; do
    $ZIG cc -target arm-linux-android -mfloat-abi=softfp -shared -fPIC -nostdlib \
      -Wl,-soname,lib$L.so $BUILD/stub/stub32.c -o sysroot/lib32/lib$L.so
  done
fi

echo "== [4/5] link lib$LIB.so"
$ZIG c++ -target aarch64-linux-android -shared -fPIC -nostdlib -O2 \
  -Wl,-soname,lib$LIB.so -Wl,--gc-sections -Wl,-z,max-page-size=16384 \
  -o $BUILD/lib$LIB.so $OBJS \
  -Wl,--no-as-needed -Wl,--allow-shlib-undefined -Wl,-z,max-page-size=16384 \
  -L sysroot/lib -lm -ldl -llog -landroid
# catatan: libc tidak di-link (-lc ditolak zig untuk target android di sandbox ini);
# libc bionic selalu ada di proses Android, jadi simbol malloc/printf dst. terselesaikan runtime.
# (rpath-link tidak didukung zig untuk android; stub .so dibuat di langkah 3b)
ls -la $BUILD/lib$LIB.so
readelf -h $BUILD/lib$LIB.so | egrep "Class|Machine" | sed 's/^/   /'
readelf -d $BUILD/lib$LIB.so | grep NEEDED | sed 's/^/   /'
readelf --dyn-syms -W $BUILD/lib$LIB.so | grep -c "ANativeActivity_onCreate" | sed 's/^/   ekspor onCreate: /'


if [ "$AVD32_OK" = "0" ]; then
  echo "== [4/5b] link lib$LIB.so (armeabi-v7a)"
  if $ZIG c++ -target arm-linux-android -mfloat-abi=softfp -shared -fPIC -nostdlib -O2 \
      -Wl,-soname,lib$LIB.so -Wl,--gc-sections \
      -o $BUILD/lib$LIB-arm.so $OBJS32 \
      -Wl,--no-as-needed -Wl,--allow-shlib-undefined \
      -L sysroot/lib32 -lm -ldl -llog -landroid 2>$BUILD/arm/link.txt; then
    readelf -h $BUILD/lib$LIB-arm.so | egrep "Class|Machine" | sed 's/^/   /'
  else
    echo "   (link armv7 gagal, APK hanya arm64)"; tail -5 $BUILD/arm/link.txt; AVD32_OK=1
  fi
fi

echo "== [4b/5] kunci tanda tangan"
[ -f keystore/debug.key.pem ] || ./tools/make_keystore.sh

echo "== [5/5] paket APK"
APK=$OUT/nusa-rush-s${SESI}-debug.apk
LIBARG="--lib $BUILD/lib$LIB.so"
[ -f $BUILD/lib$LIB-arm.so ] && LIBARG="$LIBARG --lib32 $BUILD/lib$LIB-arm.so"
$PY tools/apkbuild.py build --out "$APK" $LIBARG --assets app/assets \
  --version-name "1.0.3" --version-code 3 --min-sdk 24 --target-sdk 34
$PY tools/apkbuild.py badging "$APK" 2>/dev/null | tail -6
echo
echo "SELESAI: $APK"
