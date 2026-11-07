#!/bin/bash
# Susun sysroot Android minimal (header bionic + android + EGL/GLES) dari repo GitHub publik.
# Dipakai build_apk.sh. Hasil: sysroot/include (+ 3 stub lib). Catatan: SDK/NDK resmi tidak bisa
# diunduh di sandbox ini (host dl.google.com diblokir), jadi header diambil dari mirror AOSP.
set -e
R=/home/user/xyzcmd
S=$R/sysroot
W=/tmp/sysroot_src; mkdir -p $W
cd $W
if [ ! -d bionic/libc/include ]; then
  curl -sfL https://codeload.github.com/aosp-mirror/platform_bionic/tar.gz/refs/tags/android-12.0.0_r1 -o bionic.tgz
  mkdir -p bionic; tar xzf bionic.tgz -C bionic --strip-components=1 platform_bionic-android-12.0.0_r1/libc/include platform_bionic-android-12.0.0_r1/libc/kernel
fi
if [ ! -d eglreg/api ]; then curl -sfL https://codeload.github.com/KhronosGroup/EGL-Registry/tar.gz/refs/heads/main -o egl.tgz; mkdir -p eglreg; tar xzf egl.tgz -C eglreg --strip-components=1; fi
if [ ! -d glreg/api ]; then curl -sfL https://codeload.github.com/KhronosGroup/OpenGL-Registry/tar.gz/refs/heads/main -o gl.tgz; mkdir -p glreg; tar xzf gl.tgz -C glreg --strip-components=1 OpenGL-Registry-main/api; fi
if [ ! -d android_ndk_inc ]; then
  mkdir -p android_ndk_inc
  gh api "repos/riscv-android-src/toolchain-prebuilts-ndk-r20/git/trees/master?recursive=1" --jq '.tree[].path' | grep "^sysroot/usr/include/android/.*\.h$" > /tmp/andlist.txt
  python3 - <<'PY'
import subprocess, base64, os
for l in open('/tmp/andlist.txt'):
    p=l.strip()
    if not p: continue
    out='/tmp/sysroot_src/android_ndk_inc/'+p[len('sysroot/usr/include/'):]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    r=subprocess.run(['gh','api',f'repos/riscv-android-src/toolchain-prebuilts-ndk-r20/contents/{p}','--jq','.content'],capture_output=True)
    if r.returncode==0: open(out,'wb').write(base64.b64decode(r.stdout.replace(b'\n',b'')))
PY
fi
rm -rf $S/include; mkdir -p $S/include
cp -r bionic/libc/include/. $S/include/
cp -rn android_ndk_inc/android $S/include/ 2>/dev/null || true   # header android/* dari NDK (lebih lengkap)
mkdir -p $S/include/linux && cp -r bionic/libc/kernel/uapi/linux/. $S/include/linux/ 2>/dev/null || true
cp -r bionic/libc/kernel/uapi/asm-generic $S/include/ 2>/dev/null || true
mkdir -p $S/include/aarch64-linux-android/asm
cp -r bionic/libc/kernel/android/uapi/linux/. $S/include/linux/ 2>/dev/null || true
cp -r bionic/libc/kernel/uapi/asm-arm64/asm/. $S/include/aarch64-linux-android/asm/ 2>/dev/null || true
ln -sfn $S/include/aarch64-linux-android/asm $S/include/asm; ln -sfn $S/include/asm-generic $S/include/linux/asm-generic 2>/dev/null || true
mkdir -p $S/include/EGL $S/include/GLES3 $S/include/GLES2 $S/include/KHR
cp eglreg/api/EGL/egl.h eglreg/api/EGL/eglext.h eglreg/api/EGL/eglplatform.h $S/include/EGL/
cp glreg/api/GLES3/gl3.h glreg/api/GLES3/gl3platform.h $S/include/GLES3/
cp glreg/api/GLES2/gl2platform.h $S/include/GLES2/
cp eglreg/api/KHR/khrplatform.h $S/include/KHR/
cp $R/third_party/jni.h $S/include/jni.h; cp $R/third_party/jni_md.h $S/include/jni_md.h
echo "sysroot siap: $(find $S/include -name '*.h'|wc -l) header"
