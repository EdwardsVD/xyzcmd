// game_sim.cpp - jalankan game.cpp asli di PC tanpa GPU/Android.
// Tujuan: menangkap crash (segfault) di gameInit/gameFrame/gameInput, memeriksa
// perilaku multi-touch, dan memastikan tidak ada akses memori liar.
// Aset dibaca dari app/assets lewat stub AAssetManager.
#include "core.h"
#include "gl_api.h"
#include "render.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/types.h>
#include <android/asset_manager.h>
#include <android/native_activity.h>

// ------------------------------------------------------------------ stub GL
static void nop() {}
#define NOP1(T) static void n1_##T(T) {}
static void n1i(int) {}
static void n1u(GLuintT) {}
static void n1e(GLenumT) {}
static void n1b(GLbooleanT) {}
static void n1f(float) {}
static void n4i(int, int, int, int) {}
static void n2e(GLenumT, GLenumT) {}
static void n2u(GLuintT, GLuintT) {}
static void n2fe(float, float) {}
static void n2ef(GLenumT, GLuintT) {}
static void nef(GLenumT, GLuintT) {}
static void n1ef(GLenumT, int) {}
static void ni32(int, int) {}
static void nf3(float, float, float) {}
static void nf4(float, float, float, float) {}
static void nopf(int, float, float, float, float) {}
static void nu4(GLuintT, GLuintT, GLuintT, GLuintT) {}
static void nbuf(GLenumT, GLsizeiptrT, const void*, GLenumT) {}
static void nbufsub(GLenumT, GLsizeiptrT, GLsizeiptrT, const void*) {}
static void nvap(GLuintT, int, GLenumT, GLbooleanT, int, const void*) {}
static void nsrc(GLuintT, int, const GLcharT* const*, const int*) {}
static void nlog(GLuintT, int, int*, GLcharT*) {}
static void nlogp(GLuintT, int, int*, GLcharT*) {}
static void ngiv(GLuintT, GLenumT, int* o) { *o = 1; }
static void ngiv1(GLenumT, int* o) { *o = 1; }
static void ntex2d(GLenumT, int, int, int, int, int, GLenumT, GLenumT, const void*) {}
static void ntexp(GLenumT, GLenumT, int) {}
static void ndelbuf(int, const GLuintT*) {}
static void ndeltex(int, const GLuintT*) {}
static void ndelva(int, const GLuintT*) {}
static int g_draws = 0, g_drawInst = 0, g_instCount = 0;
static void ndrawe(GLenumT, int n, GLenumT, const void*) { g_draws++; }
static void ndrawa(GLenumT, int, int) {}
static void ndrawei(GLenumT, int n, GLenumT, const void*, int c) { g_draws++; g_instCount += c; }
static void ndrawai(GLenumT, int, int, int) {}
static void nmatrix(int, int, GLbooleanT, const float*) {}
static void ngen(int n, GLuintT* o) {
    static GLuintT c = 1;
    for (int i = 0; i < n; i++) o[i] = c++;
}
static GLuintT cshader(GLenumT) { return 1; }
static GLuintT cprog() { return 1; }
static int gloc(GLuintT, const GLcharT*) { return 0; }
static const char* glstr(GLenumT) { return "sim"; }

extern "C" {
void (*pglEnable)(GLenumT) = n1e;
void (*pglDisable)(GLenumT) = n1e;
void (*pglClear)(uint32_t) = (void (*)(uint32_t))nop;
void (*pglClearColor)(float, float, float, float) = nf4;
void (*pglViewport)(int, int, int, int) = n4i;
void (*pglDepthFunc)(GLenumT) = n1e;
void (*pglDepthMask)(GLbooleanT) = n1b;
void (*pglCullFace)(GLenumT) = n1e;
void (*pglFrontFace)(GLenumT) = n1e;
void (*pglBlendFunc)(GLenumT, GLenumT) = n2e;
void (*pglScissor)(int, int, int, int) = n4i;
void (*pglGetIntegerv)(GLenumT, int*) = ngiv1;
const char* (*pglGetString)(GLenumT) = glstr;
void (*pglPixelStorei)(GLenumT, int) = n1ef;
void (*pglPolygonOffset)(float, float) = n2fe;
void (*pglGenBuffers)(int, GLuintT*) = ngen;
void (*pglBindBuffer)(GLenumT, GLuintT) = nef;
void (*pglBufferData)(GLenumT, GLsizeiptrT, const void*, GLenumT) = nbuf;
void (*pglBufferSubData)(GLenumT, GLsizeiptrT, GLsizeiptrT, const void*) = nbufsub;
void (*pglDeleteBuffers)(int, const GLuintT*) = ndelbuf;
void (*pglGenVertexArrays)(int, GLuintT*) = ngen;
void (*pglBindVertexArray)(GLuintT) = n1u;
void (*pglDeleteVertexArrays)(int, const GLuintT*) = ndelva;
void (*pglEnableVertexAttribArray)(GLuintT) = n1u;
void (*pglDisableVertexAttribArray)(GLuintT) = n1u;
void (*pglVertexAttribPointer)(GLuintT, int, GLenumT, GLbooleanT, int, const void*) = nvap;
void (*pglVertexAttribDivisor)(GLuintT, GLuintT) = n2u;
GLuintT (*pglCreateShader)(GLenumT) = cshader;
void (*pglShaderSource)(GLuintT, int, const GLcharT* const*, const int*) = nsrc;
void (*pglCompileShader)(GLuintT) = n1u;
void (*pglGetShaderiv)(GLuintT, GLenumT, int*) = ngiv;
void (*pglGetShaderInfoLog)(GLuintT, int, int*, GLcharT*) = nlog;
GLuintT (*pglCreateProgram)(void) = cprog;
void (*pglAttachShader)(GLuintT, GLuintT) = n2u;
void (*pglLinkProgram)(GLuintT) = n1u;
void (*pglGetProgramiv)(GLuintT, GLenumT, int*) = ngiv;
void (*pglGetProgramInfoLog)(GLuintT, int, int*, GLcharT*) = nlogp;
void (*pglUseProgram)(GLuintT) = n1u;
void (*pglDeleteShader)(GLuintT) = n1u;
void (*pglDeleteProgram)(GLuintT) = n1u;
int (*pglGetUniformLocation)(GLuintT, const GLcharT*) = gloc;
int (*pglGetAttribLocation)(GLuintT, const GLcharT*) = gloc;
void (*pglUniform1i)(int, int) = ni32;
void nopf1(int, float) {}
void (*pglUniform1f)(int, float) = nopf1;
void (*pglUniform2f)(int, float, float) = (void (*)(int, float, float))nf3;
void (*pglUniform3f)(int, float, float, float) = (void (*)(int, float, float, float))nf4;
void (*pglUniform4f)(int, float, float, float, float) = (void (*)(int, float, float, float, float))nopf;
void (*pglUniformMatrix4fv)(int, int, GLbooleanT, const float*) = nmatrix;
void (*pglGenTextures)(int, GLuintT*) = ngen;
void (*pglBindTexture)(GLenumT, GLuintT) = nef;
void (*pglTexImage2D)(GLenumT, int, int, int, int, int, GLenumT, GLenumT, const void*) = ntex2d;
void (*pglTexParameteri)(GLenumT, GLenumT, int) = ntexp;
void (*pglDeleteTextures)(int, const GLuintT*) = ndeltex;
void (*pglGenerateMipmap)(GLenumT) = n1e;
void (*pglActiveTexture)(GLenumT) = n1e;
static void ngf(GLenumT p, float* v) { (void)p; if (v) *v = 1.0f; }
void (*pglGetFloatv)(GLenumT, float*) = ngf;
static void ngtpf(GLenumT a, GLenumT b, float c) { (void)a; (void)b; (void)c; }
void (*pglTexParameterf)(GLenumT, GLenumT, float) = ngtpf;
void (*pglDrawElements)(GLenumT, int, GLenumT, const void*) = ndrawe;
void (*pglDrawArrays)(GLenumT, int, int) = ndrawa;
void (*pglDrawElementsInstanced)(GLenumT, int, GLenumT, const void*, int) = ndrawei;
void (*pglDrawArraysInstanced)(GLenumT, int, int, int) = ndrawai;
}

// ------------------------------------------------------------------ log & waktu
void logmsg(int prio, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char b[2048];
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    printf("[%d] %s\n", prio, b);
}
double nowSeconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ------------------------------------------------------------------ API game
void gameInit(ANativeActivity* act, AAssetManager* mgr);
void gameResize(int w, int h);
void gameFrame(double dt);
void gameInput(int action, int pointerId, float x, float y);
void gameTouchCancelAll();
void gameTeardownGL();

// ------------------------------------------------------------------ stub aset
struct AAsset { unsigned char* data; size_t len; size_t pos; };
struct AAssetManager { const char* base; };
static AAssetManager g_mgr;

extern "C" {
AAsset* AAssetManager_open(AAssetManager* mgr, const char* name, int) {
    if (!mgr) return nullptr;
    char p[1024];
    snprintf(p, sizeof(p), "%s/%s", mgr->base, name);
    FILE* f = fopen(p, "rb");
    if (!f) { printf("  aset hilang: %s\n", name); return nullptr; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    AAsset* a = (AAsset*)malloc(sizeof(AAsset));
    a->data = (unsigned char*)malloc((size_t)n);
    a->len = (size_t)n;
    a->pos = 0;
    if (fread(a->data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(a->data); free(a); return nullptr; }
    fclose(f);
    return a;
}
int AAsset_read(AAsset* a, void* buf, size_t count) {
    if (!a) return -1;
    size_t n = a->len - a->pos;
    if (count < n) n = count;
    memcpy(buf, a->data + a->pos, n);
    a->pos += n;
    return (int)n;
}
off_t AAsset_getLength(AAsset* a) { return a ? (off_t)a->len : 0; }
off_t AAsset_getRemainingLength(AAsset* a) { return a ? (off_t)(a->len - a->pos) : 0; }
void AAsset_close(AAsset* a) { if (a) { free(a->data); free(a); } }
const void* AAsset_getBuffer(AAsset* a) { return a ? a->data : nullptr; }
int AAsset_openFileDescriptor(AAsset*, off_t*, off_t*) { return -1; }
int AAsset_isAllocated(AAsset*) { return 1; }
int64_t AAsset_seek(AAsset* a, int64_t off, int whence) {
    if (!a) return -1;
    int64_t np = off;
    if (whence == 1) np = (int64_t)a->pos + off;
    else if (whence == 2) np = (int64_t)a->len + off;
    if (np < 0) np = 0;
    if (np > (int64_t)a->len) np = (int64_t)a->len;
    a->pos = (size_t)np;
    return (int64_t)a->pos;
}
AAssetDir* AAssetManager_openDir(AAssetManager*, const char*) { return nullptr; }
void AAssetDir_close(AAssetDir*) {}
const char* AAssetDir_getNextFileName(AAssetDir*) { return nullptr; }
void AAssetDir_rewind(AAssetDir*) {}
}

int main(int argc, char** argv) {
    const char* base = argc > 1 ? argv[1] : "app/assets";
    printf("=== UJI GAMEPLAY NUSA RUSH (host, tanpa GPU) ===\n");
    g_mgr.base = base;

    // ---- U1: init -> jalan -> teardown -> init lagi (setelah konteks GL hilang)
    for (int run = 1; run <= 2; run++) {
        printf("\n[U1] siklus init/frame/teardown #%d\n", run);
        gameResize(1280, 720);
        gameInit(nullptr, &g_mgr);
        gameInput(0, 1, 90, 640);      // kiri
        gameInput(0, 2, 1100, 640);    // gas
        for (int f = 0; f < 240; f++) gameFrame(1.0 / 60.0);
        gameTouchCancelAll();
        for (int f = 0; f < 10; f++) gameFrame(1.0 / 60.0);
        gameTeardownGL();
        printf("  selesai, draw=%d inst=%d\n", g_draws, g_instCount);
    }

    // ---- U2: tombol tidak boleh "nyangkut" setelah cancel-all
    printf("\n[U2] tombol nyangkut setelah batal semua\n");
    gameResize(1280, 720);
    gameInit(nullptr, &g_mgr);
    gameInput(0, 7, 1100, 640);   // gas ditekan
    for (int f = 0; f < 120; f++) gameFrame(1.0 / 60.0);
    gameTouchCancelAll();         // mis. aplikasi di-pause
    for (int f = 0; f < 120; f++) gameFrame(1.0 / 60.0);
    printf("  (cek manual: mobil tidak boleh terus menambah kecepatan)\n");

    // ---- U3: dt ekstrem tidak boleh bikin fisika meledak
    printf("\n[U3] dt ekstrem\n");
    gameInput(0, 8, 1100, 640);
    for (int f = 0; f < 30; f++) gameFrame(0.25);
    gameFrame(0.0);
    gameFrame(-0.5);              // dt negatif
    gameFrame(1e9);              // dt sangat besar
    printf("  lolos tanpa crash\n");

    gameTeardownGL();
    printf("\nSELESAI OK\n");
    return 0;
}
