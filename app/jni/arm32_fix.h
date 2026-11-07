// arm32_fix.h - hanya untuk build armeabi-v7a (32-bit).
// sysroot kita diambil dari layout arm64 sehingga asm/sigcontext.h ikut memakai
// tipe __uint128_t yang tidak ada di ARM32. Header ini menggantinya sebelum
// header sistem dibaca (dipasang lewat -include), supaya build 32-bit bisa jalan
// tanpa mengubah sysroot bersama.
#pragma once
#if defined(__arm__) && !defined(__aarch64__)
#define __uint128_t unsigned long long
#endif
