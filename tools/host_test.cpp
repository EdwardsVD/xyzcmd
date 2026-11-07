// host_test.cpp - uji logika di PC (x86-64), tanpa GPU/Android.
// Menguji kode asli: world.cpp (island.bin, roads.json, props.bin, terrain chunk),
// car.cpp (fisika), gltf.cpp (loader paket glb). Semua panggilan GL diganti stub.
#include "core.h"
#include "gl_api.h"
#include "gltf.h"
#include "world.h"
#include "car.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


// ---------------- stub GL (semuanya no-op)
#define STUB(ret, name, args) extern "C" ret (*p##name) args = nullptr;
static void nop() {}
static void nop1(GLuintT) {}
static void nop2(GLuintT, GLuintT) {}
static void genBuf(int, GLuintT* o) { static GLuintT c = 1; o[0] = c++; }
static void genTex(int n, GLuintT* o) { static GLuintT c = 9000; for (int i = 0; i < n; i++) o[i] = c++; }
static GLuintT createShader(GLenumT) { return 1; }
static GLuintT createProgram() { return 1; }
static void getiv(GLuintT, GLenumT, int* o) { *o = 1; }
static void getlog(GLuintT, int, int*, GLcharT* l) { l[0] = 0; }
static int getLoc(GLuintT, const GLcharT*) { return 0; }
static const char* getStr(GLenumT) { return "host-stub"; }
static void uniformNoop() {}

extern "C" {
void fp_pglEnable(GLenumT) {}
void (*pglEnable)(GLenumT) = fp_pglEnable;
void fp_pglDisable(GLenumT) {}
void (*pglDisable)(GLenumT) = fp_pglDisable;
void (*pglClear)(uint32_t) = (void (*)(uint32_t))nop1;
void fClearColor(float,float,float,float){}
void (*pglClearColor)(float, float, float, float) = fClearColor;
void fViewport(int,int,int,int){}
void (*pglViewport)(int, int, int, int) = fViewport;
void fp_pglDepthFunc(GLenumT) {}
void (*pglDepthFunc)(GLenumT) = fp_pglDepthFunc;
void (*pglDepthMask)(GLbooleanT) = (void (*)(GLbooleanT))nop1;
void fp_pglCullFace(GLenumT) {}
void (*pglCullFace)(GLenumT) = fp_pglCullFace;
void fp_pglFrontFace(GLenumT) {}
void (*pglFrontFace)(GLenumT) = fp_pglFrontFace;
void (*pglUniformMatrix2fv)(int, int, GLbooleanT, const float*) = nullptr;
void (*pglUniformMatrix3fv)(int, int, GLbooleanT, const float*) = nullptr;
void (*pglBlendFunc)(GLenumT, GLenumT) = (void (*)(GLenumT, GLenumT))nop2;
void fScissor(int,int,int,int){}
void (*pglScissor)(int, int, int, int) = fScissor;
void fGetIV(GLenumT,int*o){*o=1;}
void (*pglGetIntegerv)(GLenumT, int*) = fGetIV;
const char* (*pglGetString)(GLenumT) = getStr;
void fPixel(GLenumT,int){}
void (*pglPixelStorei)(GLenumT, int) = fPixel;
void fPoly(float,float){}
void (*pglPolygonOffset)(float, float) = fPoly;
void (*pglGenBuffers)(int, GLuintT*) = genBuf;
void f2i2(GLuintT, GLuintT) {}
void f2e2(GLenumT, GLenumT) {}
void f4(GLuintT, GLenumT, int*, GLcharT*) {}
void fuifa(int, int, int, float, float, float, float) {}
void (*pglBindBuffer)(GLenumT, GLuintT) = (void (*)(GLenumT, GLuintT))nop2;
void fBufData(GLenumT,GLsizeiptrT,const void*,GLenumT){}
void (*pglBufferData)(GLenumT, GLsizeiptrT, const void*, GLenumT) = fBufData;
void fBufSub(GLenumT,GLsizeiptrT,GLsizeiptrT,const void*){}
void (*pglBufferSubData)(GLenumT, GLsizeiptrT, GLsizeiptrT, const void*) = fBufSub;
void fDelBuf(int,const GLuintT*){}
void (*pglDeleteBuffers)(int, const GLuintT*) = fDelBuf;
void (*pglGenVertexArrays)(int, GLuintT*) = genBuf;
void fu_pglBindVertexArray(GLuintT) {}
void (*pglBindVertexArray)(GLuintT) = fu_pglBindVertexArray;
void fDelVA(int,const GLuintT*){}
void (*pglDeleteVertexArrays)(int, const GLuintT*) = fDelVA;
void fu_pglEnableVertexAttribArray(GLuintT) {}
void (*pglEnableVertexAttribArray)(GLuintT) = fu_pglEnableVertexAttribArray;
void fu_pglDisableVertexAttribArray(GLuintT) {}
void (*pglDisableVertexAttribArray)(GLuintT) = fu_pglDisableVertexAttribArray;
void fVAP(GLuintT,int,GLenumT,GLbooleanT,int,const void*){}
void (*pglVertexAttribPointer)(GLuintT, int, GLenumT, GLbooleanT, int, const void*) = fVAP;
void (*pglVertexAttribDivisor)(GLuintT, GLuintT) = (void (*)(GLuintT, GLuintT))nop2;
GLuintT (*pglCreateShader)(GLenumT) = createShader;
void fSS(GLuintT,int,const GLcharT* const*,const int*){}
void (*pglShaderSource)(GLuintT, int, const GLcharT* const*, const int*) = fSS;
void fu_pglCompileShader(GLuintT) {}
void (*pglCompileShader)(GLuintT) = fu_pglCompileShader;
void (*pglGetShaderiv)(GLuintT, GLenumT, int*) = getiv;
void (*pglGetShaderInfoLog)(GLuintT, int, int*, GLcharT*) = getlog;
GLuintT (*pglCreateProgram)(void) = createProgram;
void (*pglAttachShader)(GLuintT, GLuintT) = (void (*)(GLuintT, GLuintT))nop2;
void fu_pglLinkProgram(GLuintT) {}
void (*pglLinkProgram)(GLuintT) = fu_pglLinkProgram;
void (*pglGetProgramiv)(GLuintT, GLenumT, int*) = getiv;
void (*pglGetProgramInfoLog)(GLuintT, int, int*, GLcharT*) = getlog;
void fu_pglUseProgram(GLuintT) {}
void (*pglUseProgram)(GLuintT) = fu_pglUseProgram;
void fu_pglDeleteShader(GLuintT) {}
void (*pglDeleteShader)(GLuintT) = fu_pglDeleteShader;
void fu_pglDeleteProgram(GLuintT) {}
void (*pglDeleteProgram)(GLuintT) = fu_pglDeleteProgram;
int (*pglGetUniformLocation)(GLuintT, const GLcharT*) = getLoc;
int (*pglGetAttribLocation)(GLuintT, const GLcharT*) = getLoc;
void fU1i(int,int){}
void (*pglUniform1i)(int, int) = fU1i;
void fU1f(int,float){}
void (*pglUniform1f)(int, float) = fU1f;
void fU2f(int,float,float){}
void (*pglUniform2f)(int, float, float) = fU2f;
void fU3f(int,float,float,float){}
void (*pglUniform3f)(int, float, float, float) = fU3f;
void fU4f(int,float,float,float,float){}
void (*pglUniform4f)(int, float, float, float, float) = fU4f;
void fUM4(int,int,GLbooleanT,const float*){}
void (*pglUniformMatrix4fv)(int, int, GLbooleanT, const float*) = fUM4;
void (*pglGenTextures)(int, GLuintT*) = genTex;
void (*pglBindTexture)(GLenumT, GLuintT) = (void (*)(GLenumT, GLuintT))nop2;
void fTexImg(GLenumT,int,int,int,int,int,GLenumT,GLenumT,const void*){}
void (*pglTexImage2D)(GLenumT, int, int, int, int, int, GLenumT, GLenumT, const void*) = fTexImg;
void fTexPar(GLenumT,GLenumT,int){}
void (*pglTexParameteri)(GLenumT, GLenumT, int) = fTexPar;
void fDelTex(int,const GLuintT*){}
void (*pglDeleteTextures)(int, const GLuintT*) = fDelTex;
void fp_pglGenerateMipmap(GLenumT) {}
void (*pglGenerateMipmap)(GLenumT) = fp_pglGenerateMipmap;
void fp_pglActiveTexture(GLenumT) {}
void (*pglActiveTexture)(GLenumT) = fp_pglActiveTexture;
void fDrawE(GLenumT,int,GLenumT,const void*){}
void (*pglDrawElements)(GLenumT, int, GLenumT, const void*) = fDrawE;
void fDrawA(GLenumT,int,int){}
void (*pglDrawArrays)(GLenumT, int, int) = fDrawA;
void fDrawEI(GLenumT,int,GLenumT,const void*,int){}
void (*pglDrawElementsInstanced)(GLenumT, int, GLenumT, const void*, int) = fDrawEI;
void fDrawAI(GLenumT,int,int,int){}
void (*pglDrawArraysInstanced)(GLenumT, int, int, int) = fDrawAI;
}

void logmsg(int prio, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char b[1024];
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    printf("[%d] %s\n", prio, b);
}
double nowSeconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int g_fail = 0;
#define CHECK(cond, ...)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            printf("  GAGAL: ");                                   \
            printf(__VA_ARGS__);                                    \
            printf("\n");                                           \
            g_fail++;                                               \
        }                                                           \
    } while (0)

static u8* slurp(const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("  tidak bisa buka %s\n", path); return nullptr; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* d = (u8*)malloc(n + 1);
    if (fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return nullptr; }
    fclose(f);
    d[n] = 0;
    *len = (size_t)n;
    return d;
}

int main(int argc, char** argv) {
    const char* base = argc > 1 ? argv[1] : "app/assets";
    char p[512];
    printf("=== UJI HOST NUSA RUSH (x86-64) ===\n");

    // ---------------- 1. island.bin
    size_t ilen = 0;
    snprintf(p, sizeof(p), "%s/generated/island.bin", base);
    u8* isl = slurp(p, &ilen);
    CHECK(isl != nullptr, "island.bin tidak terbaca");
    if (!isl) return 1;
    Island island;
    CHECK(island.load(isl, ilen), "island.load gagal");
    printf("  pulau: %dx%d, x[%.0f..%.0f] z[%.0f..%.0f]\n", island.N, island.N, island.x0,
           island.x1, island.z0, island.z1);
    f32 hmax = -1e9, hmin = 1e9;
    for (int j = 0; j < island.N; j += 4)
        for (int i = 0; i < island.N; i += 4) {
            f32 h = island.H[j * island.N + i];
            if (h > hmax) hmax = h;
            if (h < hmin) hmin = h;
        }
    printf("  tinggi %.1f .. %.1f m\n", hmin, hmax);
    CHECK(hmax > 200 && hmax < 260, "tinggi maksimum pulau aneh: %.1f", hmax);
    CHECK(hmin < -20, "laut dalam hilang: %.1f", hmin);

    // ---------------- 2. roads.json (parser manual)
    size_t rlen = 0;
    snprintf(p, sizeof(p), "%s/generated/roads.json", base);
    char* roadsTxt = (char*)slurp(p, &rlen);
    CHECK(roadsTxt != nullptr, "roads.json tidak terbaca");
    Roads roads;
    roads.loadJson(roadsTxt, (int)rlen);
    printf("  jalan: %d segmen\n", roads.segs.size);
    CHECK(roads.segs.size > 2500, "segmen jalan terlalu sedikit: %d", roads.segs.size);
    for (int i = 0; i < roads.segs.size; i++) {
        const RoadSeg& s = roads.segs[i];
        CHECK(s.lebar > 3.5f && s.lebar < 15.f, "lebar jalan aneh %.2f", s.lebar);
        CHECK(s.jenis <= 2, "jenis jalan aneh %d", s.jenis);
        if (i > 50) break;
    }
    // tidak ada jalan di laut
    int diLaut = 0;
    for (int i = 0; i < roads.segs.size; i++) {
        const RoadSeg& s = roads.segs[i];
        if (island.height(s.ax, s.az) < 0.4f || island.height(s.bx, s.bz) < 0.4f) diLaut++;
    }
    printf("  segmen di laut (<0.4 m): %d\n", diLaut);
    CHECK(diLaut == 0, "ada %d segmen jalan di laut", diLaut);

    // ---------------- 3. props.bin
    size_t plen = 0;
    snprintf(p, sizeof(p), "%s/generated/props.bin", base);
    u8* propsBin = slurp(p, &plen);
    Props props;
    CHECK(props.load(propsBin, plen), "props.load gagal");
    printf("  instance: %d, chunk: %d\n", props.count, props.chunkCount);
    CHECK(props.count > 50000, "instance terlalu sedikit");
    int jenis[32] = {0};
    for (int i = 0; i < props.count; i++) {
        if (props.items[i].type < 32) jenis[props.items[i].type]++;
    }
    printf("  pohon: %d/%d/%d/%d  rumah desa: %d  gedung kota: %d/%d/%d\n", jenis[0], jenis[1],
           jenis[2], jenis[3], jenis[4] + jenis[5] + jenis[6], jenis[16], jenis[17], jenis[18]);
    CHECK(jenis[0] > 1000 && jenis[16] > 50, "komposisi objek aneh");

    // chunk gather
    int ch[512];
    int nc = props.gather(-1338.f, 1381.f, 340.f, ch, 512);
    int nInst = 0;
    for (int i = 0; i < nc; i++) nInst += props.chunkCountArr[ch[i]];
    printf("  chunk dalam radius 340 m: %d (instance %d)\n", nc, nInst);
    CHECK(nc > 0, "tidak ada chunk di sekitar spawn");
    CHECK(nInst < 150000, "kepdatan objek terlalu tinggi untuk HP");

    // ---------------- 4. terrain chunk
    World world;
    CHECK(world.loadAssets(isl, ilen, roadsTxt, (int)rlen, propsBin, plen), "world.loadAssets gagal");
    double t0 = nowSeconds();
    int built = world.buildTerrainNear(-1338.f, 1381.f, 400, 5);
    double dtb = nowSeconds() - t0;
    printf("  chunk terrain: %d dibangun dalam %.1f ms (%.2f ms/chunk)\n", built, dtb * 1000,
           dtb * 1000 / (built ? built : 1));
    CHECK(built >= 64, "jumlah chunk terrain terlalu sedikit: %d", built);
    CHECK(world.terrainChunks.size == 100, "grid terrain bukan 100 chunk: %d", world.terrainChunks.size);
    CHECK(dtb < 3.0, "pembangunan terrain terlalu lambat: %.0f ms", dtb * 1000);

    // ---------------- 5. grip di jalan vs luar
    f32 gripJalan = 0, gripOut = 0;
    {
        const RoadSeg& s = roads.segs[roads.segs.size / 2];
        f32 cx = (s.ax + s.bx) * 0.5f, cz = (s.az + s.bz) * 0.5f;
        gripJalan = roads.gripAt(cx, cz, island);
        gripOut = roads.gripAt(island.x0 + 200.f, island.z0 + 3800.f, island);
        printf("  grip di jalan=%.2f, di luar jalan=%.2f (biome %d)\n", gripJalan, gripOut,
               island.biomeAt(island.x0 + 200.f, island.z0 + 3800.f));
    }
    CHECK(gripJalan > gripOut, "grip jalan (%.2f) harus > luar jalan (%.2f)", gripJalan, gripOut);

    // ---------------- 6. fisika mobil
    Car car;
    f32 spawnX = -1338.f, spawnZ = 1381.f;
    car.reset(v3(spawnX, island.height(spawnX, spawnZ) + 1.0f, spawnZ), 1.614f);
    CarInput in;
    f32 dt = 1.f / 60.f;
    f32 maxSpeed = 0, minYabove = 1e9;
    int nanCount = 0, diLautCount = 0;
    // 8 detik gas penuh
    for (int i = 0; i < 480; i++) {
        in.gas = 1.f;
        in.setir = 0;
        f32 grip = roads.gripAt(car.s.pos.x, car.s.pos.z, island);
        car.step(dt, in, island, grip);
        f32 gh = island.height(car.s.pos.x, car.s.pos.z);
        f32 gap = car.s.pos.y - gh;
        if (gap < minYabove) minYabove = gap;
        if (vabs(car.s.kecepatanMaju) > maxSpeed) maxSpeed = vabs(car.s.kecepatanMaju);
        if (isnan(car.s.pos.x) || isnan(car.s.pos.y) || isnan(car.s.vel.x)) nanCount++;
        if (car.s.diLaut) diLautCount++;
    }
    printf("  gas 8 dtk: kecepatan %.1f km/jam, tinggi min di atas tanah %.2f m\n",
           maxSpeed * 3.6f, minYabove);
    CHECK(maxSpeed * 3.6f > 60.f && maxSpeed * 3.6f < 260.f, "kecepatan tidak wajar: %.0f km/jam",
          maxSpeed * 3.6f);
    CHECK(nanCount == 0, "fisika menghasilkan NaN (%d kali)", nanCount);
    CHECK(minYabove > -0.6f, "mobil menembus tanah (%.2f m)", minYabove);

    // menikung
    f32 yaw0 = car.s.yaw;
    for (int i = 0; i < 120; i++) {
        in.gas = 0.6f;
        in.setir = 1.f;
        car.step(dt, in, island, 0.9f);
    }
    f32 dYaw = vabs(car.s.yaw - yaw0);
    printf("  setir kanan 2 dtk: yaw berubah %.1f derajat\n", dYaw * RAD2DEG);
    CHECK(dYaw * RAD2DEG > 10.f, "mobil tidak berbelok");

    // rem sampai berhenti lalu mundur
    for (int i = 0; i < 400; i++) {
        in.setir = 0;
        in.gas = 1.f;
        in.rem = 1.f;
        car.step(dt, in, island, 0.9f);
    }
    printf("  rem+gas: kecepatan %.2f m/s (negatif = mundur)\n", car.s.kecepatanMaju);
    CHECK(car.s.kecepatanMaju < 0.5f, "rem tidak menghentikan mobil");

    // setir kembali ke tengah saat input dilepas
    in.gas = 0;
    in.rem = 0;
    in.setir = 0;
    for (int i = 0; i < 90; i++) car.step(dt, in, island, 0.9f);
    printf("  setir setelah dilepas: %.3f\n", car.s.setir);
    CHECK(vabs(car.s.setir) < 0.05f, "setir tidak kembali ke tengah");

    // ---------------- 7. loader paket glb
    printf("  uji loader glb paket:\n");
    const char* models[] = {"models/mobil.glb", "models/trees/oak.glb",
                            "models/kota/kota_B_2.glb", "models/desa/desa_A_1.glb",
                            "models/roads_kota.glb", "models/trees/palm_lod1.glb"};
    for (int i = 0; i < 6; i++) {
        snprintf(p, sizeof(p), "%s/%s", base, models[i]);
        size_t mlen = 0;
        u8* md = slurp(p, &mlen);
        CHECK(md != nullptr, "%s tidak terbaca", models[i]);
        if (!md) continue;
        Model m;
        bool ok = modelLoadPacked(md, mlen, &m);
        CHECK(ok, "%s gagal diparse", models[i]);
        if (ok) {
            printf("    %-28s sub=%d tri=%d tinggi=%.1f tex=%s\n", models[i], m.subs.size,
                   m.indexCount / 3, m.bmax.y - m.bmin.y, m.texId ? "ada" : "TIDAK ADA");
            CHECK(m.subs.size >= 1, "%s tanpa sub-mesh", models[i]);
            CHECK(m.indexCount % 3 == 0, "%s indeks bukan kelipatan 3", models[i]);
            CHECK(m.texId != 0, "%s tekstur tidak termuat", models[i]);
            for (int s = 0; s < m.subs.size; s++) {
                CHECK(m.subs[s].indexOffset + m.subs[s].indexCount <= m.indexCount,
                      "%s sub-mesh %d di luar indeks", models[i], s);
            }
        }
        free(md);
    }
    // mobil harus punya 4 roda dengan pivot
    {
        snprintf(p, sizeof(p), "%s/models/mobil.glb", base);
        size_t mlen = 0;
        u8* md = slurp(p, &mlen);
        Model m;
        if (md && modelLoadPacked(md, mlen, &m)) {
            int roda = 0;
            for (int s = 0; s < m.subs.size; s++)
                if (strstr(m.subs[s].name, "Wheel") || strstr(m.subs[s].name, "wheel")) roda++;
            printf("    mobil: %d sub-mesh roda, total %d sub-mesh\n", roda, m.subs.size);
            CHECK(roda >= 4, "roda mobil tidak terdeteksi");
        }
        free(md);
    }

    printf("\n%s (%d masalah)\n", g_fail ? "ADA MASALAH" : "SEMUA UJI LULUS", g_fail);
    return g_fail ? 2 : 0;
}
