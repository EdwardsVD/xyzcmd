// game.cpp - loop utama: muat aset, bangun dunia bertahap, fisika mobil, kamera,
// kontrol sentuh analog, gambar terrain + jalan + objek (instancing + LOD).
#include "core.h"
#include "config.h"
#include "render.h"
#include "world.h"
#include "car.h"
#include "gltf.h"

#ifndef __ANDROID__
// hanya dibutuhkan untuk kompilasi host (g++) agar snprintf terdeklarasi;
// pada target Android (__ANDROID__) deklarasi sudah ada lewat header NDK transitif.
#include <cstdio>
#endif

#include <android/asset_manager.h>
#include <android/native_activity.h>


// ------------------------------------------------------------------ akses aset
struct AssetReader {
    AAssetManager* mgr;
};
extern AAssetManager* g_assetMgr;   // diisi platform.cpp

static void* readAsset(const char* name, size_t* outLen) {
    if (!g_assetMgr) return nullptr;
    AAsset* a = AAssetManager_open(g_assetMgr, name, 1 /*AASSET_MODE_BUFFER*/);
    if (!a) { LOGE("aset tidak ada: %s", name); return nullptr; }
    size_t n = (size_t)AAsset_getLength(a);
    void* data = malloc(n + 1);
    if (data) {
        int rd = AAsset_read(a, data, n);
        if (rd < 0) rd = 0;
        ((char*)data)[rd] = 0;
        *outLen = (size_t)rd;
    }
    AAsset_close(a);
    return data;
}

// ------------------------------------------------------------------ state global
struct Game {
    World world;
    Car car;
    u32 roadTex[3] = {0, 0, 0};
    Mesh roadMeshTrotoar[3];             // trotoar + kerb (model terpisah)
    u32 roadTexTrotoar[3] = {0, 0, 0};
    Mesh roadMesh[3];
    int roadTris[3] = {0, 0, 0};
    Model treeModel[4];      // oak, spruce, sakura, palm
    Model treeLod[4];
    Model desaModel[12];
    Model desaModelLod[12];
    Model kotaModel[12];
    Model kotaModelLod[12];
    Model carModel;
    Mesh instMeshes[56];
    bool loaded = false, ready = false;
    v3 carPos;
    f32 carYaw = 0;
    double time = 0;
    int frameCount = 0;
    f32 fps = 0, fpsAccum = 0;
    int fpsFrames = 0;
    // input
    struct Touch { int id = -1; f32 x = 0, y = 0; f32 amp = 0; int btn = -1; };
    Touch touches[8];
    int nTouches = 0;
    CarInput input;
    f32 analogSetir = 0, analogGas = 0, analogRem = 0;
    f32 resetTimer = 0;
    // statistik
    int drawnInstances = 0, drawnChunks = 0;
    f32 streamingX = 0, streamingZ = 0;
    float instBuf[6000 * 5];
    float uploadMs = 0;
};
static Game g;

// ------------------------------------------------------------------ tombol UI
// posisi dalam piksel (diisi tiap frame)
struct Button {
    f32 cx, cy, r;
    int id;           // 0 kiri, 1 kanan, 2 rem, 3 gas, 4 reset
    f32 amp;          // tekanan 0..1 (untuk analog)
    bool down;
};
static Button g_btn[5];
static f32 g_uiScale = 1.f;

static void layoutButtons(int w, int h) {
    f32 base = vmin((f32)w, (f32)h) * 0.5f;
    f32 r = vmax(base * 0.19f, Cfg::tombolMinPx * 0.5f);
    f32 m = r * 1.45f;
    g_btn[0] = {m + r * 0.2f, h - m, r, 0, 0, false};                    // kiri
    g_btn[1] = {m + r * 2.6f, h - m, r, 1, 0, false};                    // kanan
    g_btn[2] = {w - m - r * 2.6f, h - m, r, 2, 0, false};                // rem
    g_btn[3] = {w - m - r * 0.2f, h - m, r, 3, 0, false};                // gas
    f32 rr = vmax(r * 0.52f, Cfg::tombolMinPx * 0.5f);
    g_btn[4] = {w - rr * 1.8f, rr * 1.8f, rr, 4, 0, false};              // reset
}

// unggah mesh (pos3/nrm3/uv2, indeks u32) langsung dari .glb tanpa cgltf (hemat RAM saat buka)
static bool uploadModelFast(const char* path, Model* m, u32* texOut) {
    size_t len = 0;
    void* data = readAsset(path, &len);
    if (!data) return false;
    bool ok = modelLoadPacked(data, len, m);
    free(data);
    if (!ok) { LOGW("gagal muat %s", path); return false; }
    if (texOut) *texOut = m->texId;
    return true;
}

// ------------------------------------------------------------------ aset
static bool loadModelByName(const char* path, Model* m, bool* missing) {
    size_t len = 0;
    void* data = readAsset(path, &len);
    if (!data) {
        if (missing) *missing = true;
        return false;
    }
    bool ok = modelLoadPacked(data, len, m);
    free(data);
    if (!ok) LOGW("model gagal: %s", path);
    return ok;
}

static void loadAllAssets() {
    size_t len = 0;
    // pulau
    void* isl = readAsset("generated/island.bin", &len);
    size_t roadsLen = 0;
    char* roads = (char*)readAsset("generated/roads.json", &roadsLen);
    size_t propsLen = 0;
    void* props = readAsset("generated/props.bin", &propsLen);
    if (isl && roads && props) {
        g.world.loadAssets((const u8*)isl, len, roads, (int)roadsLen, (const u8*)props, propsLen);
    } else {
        LOGE("aset inti tidak lengkap: island=%p roads=%p props=%p", isl, roads, props);
    }
    free(isl);
    free(roads);
    free(props);

    // jalan (3 jenis) -> Mesh + tekstur, Model dibuang
    const char* roadNames[3] = {"models/roads_desa.glb", "models/roads_kota.glb",
                                "models/roads_tanah.glb"};
    for (int k = 0; k < 3; k++) {
        Model tmp;
        if (uploadModelFast(roadNames[k], &tmp, &g.roadTex[k])) {
            g.roadMesh[k].vao = tmp.vao;
            g.roadMesh[k].vbo = tmp.vbo;
            g.roadMesh[k].ibo = tmp.ibo;
            g.roadMesh[k].count = tmp.indexCount;
            g.roadTris[k] = tmp.indexCount / 3;
            if (g.roadTex[k] == 0) g.roadTex[k] = tmp.texId;
            // tekstur dipindah ke g.roadTex; jangan dihapus oleh modelFree
            tmp.texId = 0;
            modelFree(&tmp);
            LOGI("jalan %s: %d tri, tekstur=%u", roadNames[k], g.roadTris[k], g.roadTex[k]);
        }
        // trotoar (desa & kota) - model terpisah, satu draw call
        if (k == 2) continue;
        char tn[64];
        snprintf(tn, sizeof(tn), "models/roads_%s_trotoar.glb", k == 0 ? "desa" :
                 (k == 1 ? "kota" : "tanah"));
        Model tt;
        if (uploadModelFast(tn, &tt, &g.roadTexTrotoar[k])) {
            g.roadMeshTrotoar[k].vao = tt.vao;
            g.roadMeshTrotoar[k].vbo = tt.vbo;
            g.roadMeshTrotoar[k].ibo = tt.ibo;
            g.roadMeshTrotoar[k].count = tt.indexCount;
            if (g.roadTexTrotoar[k] == 0) g.roadTexTrotoar[k] = tt.texId;
            tt.texId = 0;
            modelFree(&tt);
            LOGI("trotoar %s: %d tri, tekstur=%u", tn, g.roadMeshTrotoar[k].count / 3,
                 g.roadTexTrotoar[k]);
        }
    }

    // pohon: 4 jenis, LOD1 untuk jarak jauh. Model _lod1 disimpan utuh (mesh kecil)
    const char* treeN[4] = {"oak", "spruce", "sakura", "palm"};
    for (int k = 0; k < 4; k++) {
        char p[96], q[96];
        snprintf(p, sizeof(p), "models/trees/%s.glb", treeN[k]);
        snprintf(q, sizeof(q), "models/trees/%s_lod1.glb", treeN[k]);
        loadModelByName(p, &g.treeModel[k], nullptr);
        loadModelByName(q, &g.treeLod[k], nullptr);
    }
    // rumah desa & gedung kota: 24 model (12+12) + lod
    const char* sets[4] = {"A", "B", "C", "D"};
    for (int si = 0; si < 4; si++)
        for (int k = 0; k < 3; k++) {
            char p[96], q[96];
            int slot = si * 3 + k;
            snprintf(p, sizeof(p), "models/desa/desa_%s_%d.glb", sets[si], k + 1);
            snprintf(q, sizeof(q), "models/desa/desa_%s_%d_lod1.glb", sets[si], k + 1);
            loadModelByName(p, &g.desaModel[slot], nullptr);
            loadModelByName(q, &g.desaModelLod[slot], nullptr);
            snprintf(p, sizeof(p), "models/kota/kota_%s_%d.glb", sets[si], k + 1);
            snprintf(q, sizeof(q), "models/kota/kota_%s_%d_lod1.glb", sets[si], k + 1);
            loadModelByName(p, &g.kotaModel[slot], nullptr);
            loadModelByName(q, &g.kotaModelLod[slot], nullptr);
        }
    // mobil
    loadModelByName("models/mobil.glb", &g.carModel, nullptr);
    LOGI("aset dimuat. pohon0=%d kota18=%d mobil=%d jalan=%d/%d/%d", (int)g.treeModel[0].ok,
         (int)g.kotaModel[2].ok, (int)g.carModel.ok, g.roadTris[0], g.roadTris[1], g.roadTris[2]);
}

// ------------------------------------------------------------------ API platform
void gameInit(ANativeActivity* act, AAssetManager* mgr) {
    LOGI("gameInit");
    g_assetMgr = mgr;
    if (!g_ren.init()) {
        LOGE("renderer gagal init");
        return;
    }
    loadAllAssets();
    g.loaded = true;
    // posisi awal dari car_spawn.json (dibuat tools/pack_assets.py)
    g.carPos = v3(-1306.f, g.world.isl.height(-1306.f, 1380.f) + 1.2f, 1380.f);
    g.carYaw = 1.614f;
    size_t slen = 0;
    char* sp = (char*)readAsset("generated/car_spawn.json", &slen);
    if (sp) {
        const char* px = strstr(sp, "\"x\"");
        if (px) {
            g.carPos.x = strtof(strchr(px, ':') + 1, nullptr);
            const char* pz = strstr(sp, "\"z\"");
            const char* py = strstr(sp, "\"yaw\"");
            if (pz) g.carPos.z = strtof(strchr(pz, ':') + 1, nullptr);
            if (py) g.carYaw = strtof(strchr(py, ':') + 1, nullptr);
            g.carPos.y = g.world.isl.height(g.carPos.x, g.carPos.z) + 1.0f;
        }
        free(sp);
    }
    g.car.reset(g.carPos, g.carYaw);
    g.nTouches = 0;
    for (int i = 0; i < 5; i++) { g_btn[i].amp = 0; g_btn[i].down = false; }
    g_ren.setFog(Cfg::kabutMulai, Cfg::kabutAkhir, v3(0.70f, 0.80f, 0.90f));
    layoutButtons(g_ren.width, g_ren.height);
    g.world.buildTerrainNear(g.carPos.x, g.carPos.z, 12, 5);
    LOGI("mulai di (%.0f, %.0f)", g.carPos.x, g.carPos.z);
}

void gameResize(int w, int h) {
    g_ren.setSize(w, h);
    layoutButtons(w, h);
    if (w > h) g_uiScale = h / 720.f;
    else g_uiScale = w / 720.f;
}

void gameTouchCancelAll() {
    for (int i = 0; i < g.nTouches; i++) {
        g.touches[i].id = -1;
        g.touches[i].btn = -1;
        g.touches[i].amp = 0;
    }
    g.nTouches = 0;
    for (int i = 0; i < 5; i++) { g_btn[i].amp = 0; g_btn[i].down = false; }
}

static int hitButton(f32 x, f32 y) {
    for (int i = 0; i < 5; i++) {
        f32 dx = x - g_btn[i].cx, dy = y - g_btn[i].cy;
        f32 r = g_btn[i].r * (i == 4 ? 1.15f : 1.22f);   // area sentuh lebih besar
        if (dx * dx + dy * dy <= r * r) return i;
    }
    return -1;
}

void gameInput(int action, int pointerId, float x, float y) {
    if (action == 0) {  // DOWN
        int b = hitButton(x, y);
        if (b < 0) return;
        if (g.nTouches >= 8) return;
        Game::Touch& t = g.touches[g.nTouches++];
        t.id = pointerId;
        t.x = x;
        t.y = y;
        t.btn = b;
        t.amp = 0;
        g_btn[b].down = true;
    } else if (action == 1) {  // UP
        for (int i = 0; i < g.nTouches; i++) {
            if (g.touches[i].id == pointerId) {
                int b = g.touches[i].btn;
                if (b >= 0) { g_btn[b].down = false; g_btn[b].amp = 0; }
                g.touches[i] = g.touches[--g.nTouches];
                break;
            }
        }
    } else {  // MOVE
        for (int i = 0; i < g.nTouches; i++) {
            if (g.touches[i].id == pointerId) {
                g.touches[i].x = x;
                g.touches[i].y = y;
                break;
            }
        }
    }
    // hitung amplitudo analog: makin jauh dari pusat (atau makin dalam) makin besar
    for (int i = 0; i < 5; i++) g_btn[i].amp = 0;
    for (int i = 0; i < g.nTouches; i++) {
        Game::Touch& t = g.touches[i];
        if (t.btn < 0 || t.btn > 3) continue;
        Button& b = g_btn[t.btn];
        f32 dx = t.x - b.cx, dy = t.y - b.cy;
        f32 dist = sqrtf(dx * dx + dy * dy);
        f32 a = vclamp(dist / (b.r * 0.95f), 0.f, 1.f);
        if (a > b.amp) b.amp = a;
    }
}

// ------------------------------------------------------------------ bantuan render
static mat4 carTransform(const CarState& s) {
    mat4 body = mat4::translate(v3(s.pos.x, s.pos.y - Cfg::tinggiGantung + 0.06f, s.pos.z)) *
                mat4::rotY(s.yaw) * mat4::rotX(s.pitch * 0.35f);
    mat4 rz;
    f32 c = cosf(s.roll), sn = sinf(s.roll);
    rz.m[0] = c; rz.m[1] = sn; rz.m[4] = -sn; rz.m[5] = c;
    return body * rz;
}

// ------------------------------------------------------------------ frame
void gameFrame(double dtD) {
    if (!g.loaded) return;
    g.time += dtD;
    f32 dt = (f32)vclamp(dtD, 0.0, 0.05);
    g.frameCount++;
    g.fpsAccum += dt;
    g.fpsFrames++;
    if (g.fpsAccum > 0.5f) {
        g.fps = g.fpsFrames / g.fpsAccum;
        g.fpsAccum = 0;
        g.fpsFrames = 0;
    }

    int W = g_ren.width, H = g_ren.height;
    if (W <= 0 || H <= 0) return;

    // ---------------- input analog
    g.analogSetir = g_btn[1].amp - g_btn[0].amp;
    g.analogGas = g_btn[3].amp;
    g.analogRem = g_btn[2].amp;
    g.input.setir = g.analogSetir;
    g.input.gas = g.analogGas;
    g.input.rem = g.analogRem;
    if (g_btn[4].down) {
        if (g.resetTimer <= 0) {
            f32 ox, oz, heading;
            if (g.world.roads.nearestOnRoad(g.car.s.pos.x, g.car.s.pos.z, &ox, &oz, &heading)) {
                g.car.reset(v3(ox, g.world.isl.height(ox, oz) + 1.0f, oz), heading);
            } else {
                g.car.reset(v3(-1306, g.world.isl.height(-1306, 1380) + 1.f, 1380), 0);
            }
            g.resetTimer = 0.8f;
        }
    }
    if (g.resetTimer > 0) g.resetTimer -= dt;

    // ---------------- fisika
    f32 grip = g.world.roads.gripAt(g.car.s.pos.x, g.car.s.pos.z, g.world.isl);
    if (g.car.s.diLaut) {
        f32 ox, oz, heading;
        if (g.world.roads.nearestOnRoad(g.car.s.pos.x, g.car.s.pos.z, &ox, &oz, &heading))
            g.car.reset(v3(ox, g.world.isl.height(ox, oz) + 1.f, oz), heading);
        else
            g.car.reset(v3(-1306, g.world.isl.height(-1306, 1380) + 1.f, 1380), 0);
    }
    g.car.step(dt, g.input, g.world.isl, grip);

    // ---------------- kamera di belakang mobil
    CarState& cs = g.car.s;
    v3 fwd = v3(sinf(cs.yaw), 0, cosf(cs.yaw));
    f32 speed = vabs(cs.kecepatanMaju);
    f32 dist = Cfg::kamJarak + vclamp(speed * 0.035f, 0.f, 2.2f);
    f32 tinggi = Cfg::kamTinggi + vclamp(speed * 0.01f, 0.f, 0.7f);
    v3 camTarget = cs.pos - fwd * dist + v3(0, tinggi, 0);
    // jangan masuk ke dalam tanah
    f32 gh = g.world.isl.height(camTarget.x, camTarget.z) + 0.7f;
    if (camTarget.y < gh) camTarget.y = gh;
    static v3 camPos;
    if (camPos.len2() < 1.f) camPos = camTarget;
    f32 k = vclamp(dt * Cfg::kamLag, 0.f, 1.f);
    camPos = vlerp(camPos, camTarget, k);
    f32 gh2 = g.world.isl.height(camPos.x, camPos.z) + 0.55f;
    if (camPos.y < gh2) camPos.y = gh2;
    v3 look = cs.pos + v3(0, Cfg::kamLookTinggi, 0) + fwd * 2.2f;
    f32 fov = vlerp(Cfg::kamFov, Cfg::kamFovCepat, vclamp(speed / 45.f, 0.f, 1.f));
    mat4 proj = mat4::perspective(fov * DEG2RAD, (f32)W / (f32)H, Cfg::kamNear, Cfg::kamFar);
    mat4 view = mat4::lookAt(camPos, look, v3(0, 1, 0));
    mat4 vp = proj * view;
    Frustum fr;
    fr.fromMatrix(vp);

    // ---------------- render
    pglViewport(0, 0, W, H);
    pglClearColor(0.72f, 0.82f, 0.92f, 1.f);
    pglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g_ren.beginFrame(vp, camPos);

    // bangun terrain bertahap (2 chunk per frame untuk hindari lag)
    g.world.buildTerrainNear(cs.pos.x, cs.pos.z, 3, 5);
    g.world.drawTerrain(fr, camPos);

    // jalan: badan jalan + trotoar (2 draw call per jenis)
    {
        mat4 I;
        for (int k = 0; k < 3; k++) {
            if (g.roadMesh[k].count)
                g_ren.drawModel(g.roadMesh[k], g.roadTex[k], v4(1, 1, 1, 1), I, true, false,
                                false);
            if (g.roadMeshTrotoar[k].count)
                g_ren.drawModel(g.roadMeshTrotoar[k], g.roadTexTrotoar[k],
                                v4(1, 1, 1, 1), I, true, false, false);
        }
    }

    // objek (pohon/rumah/gedung): instancing per chunk + LOD per model
    int chunks[512];
    int nc = g.world.props.gather(cs.pos.x, cs.pos.z, Cfg::jarakPandang, chunks, 512);
    g.drawnChunks = nc;
    g.drawnInstances = 0;
    // batch[tipe][0=dekat,1=jauh]
    static float* batches = nullptr;
    static int* bcount = nullptr;
    if (!batches) {
        batches = (float*)malloc(sizeof(float) * 5 * 56 * 1400);
        bcount = (int*)malloc(sizeof(int) * 56);
    }
    for (int i = 0; i < 56; i++) bcount[i] = 0;
    const int CAP = 1400;
    f32 lod2 = Cfg::lodJarak * Cfg::lodJarak;
    f32 vp2 = Cfg::jarakPandang * Cfg::jarakPandang;
    for (int ci = 0; ci < nc; ci++) {
        int ch = chunks[ci];
        int st = g.world.props.chunkStart[ch];
        int cnt = g.world.props.chunkCountArr[ch];
        for (int i = 0; i < cnt; i++) {
            const Inst& in = g.world.props.items[st + i];
            f32 dx = in.x - cs.pos.x, dz = in.z - cs.pos.z;
            f32 d2 = dx * dx + dz * dz;
            if (d2 > vp2) continue;
            if (g.drawnInstances >= Cfg::maksInstance) break;
            // culling frustum: buang objek di belakang kamera / di luar layar
            f32 rr = (in.type < 4 ? 9.f : 22.f) * in.scale;
            if (!fr.sphereIn(v3(in.x, in.y + rr * 0.5f, in.z), rr)) continue;
            int t = in.type;
            if (t >= 28) continue;
            int far = d2 > lod2 ? 1 : 0;
            int bi = t * 2 + far;
            int n = bcount[bi];
            if (n >= CAP) continue;
            float* b = batches + ((size_t)bi * CAP + n) * 5;
            b[0] = in.x; b[1] = in.y; b[2] = in.z; b[3] = in.yaw; b[4] = in.scale;
            bcount[bi] = n + 1;
            g.drawnInstances++;
        }
    }
    mat4 I;
    for (int t = 0; t < 28; t++) {
        for (int far = 0; far < 2; far++) {
            int bi = t * 2 + far;
            int n = bcount[bi];
            if (n == 0) continue;
            Model* src;
            if (t < 4) src = far ? &g.treeLod[t] : &g.treeModel[t];
            else if (t < 16) src = far ? &g.desaModelLod[t - 4] : &g.desaModel[t - 4];
            else src = far ? &g.kotaModelLod[t - 16] : &g.kotaModel[t - 16];
            if (!src || !src->ok) continue;
            Mesh& m = g.instMeshes[bi];
            if (!m.instVbo) {
                m.vao = src->vao;
                m.vbo = src->vbo;
                m.ibo = src->ibo;
                m.count = src->indexCount;
                pglGenBuffers(1, &m.instVbo);
            }
            pglBindBuffer(GL_ARRAY_BUFFER, m.instVbo);
            pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptrT)((size_t)n * 20), batches + (size_t)bi * CAP * 5,
                          GL_DYNAMIC_DRAW);
            pglBindVertexArray(m.vao);
            pglEnableVertexAttribArray(3);
            pglVertexAttribPointer(3, 4, GL_FLOAT, 0, 20, (const void*)0);
            pglVertexAttribDivisor(3, 1);
            pglEnableVertexAttribArray(4);
            pglVertexAttribPointer(4, 1, GL_FLOAT, 0, 20, (const void*)(16));
            pglVertexAttribDivisor(4, 1);
            u32 tex = src->texId;
            bool ds = (t < 4);   // pohon: daun dua sisi
            g_ren.drawInstanced(m, tex, v4(1, 1, 1, 1), I, tex != 0, n, ds);
            pglDisableVertexAttribArray(3);
            pglDisableVertexAttribArray(4);
            pglVertexAttribDivisor(3, 0);
            pglVertexAttribDivisor(4, 0);
        }
    }
    g.streamingX = cs.pos.x;
    g.streamingZ = cs.pos.z;

    // mobil: badan + roda yang berputar & berbelok (sub-mesh ber-pivot dari pack_assets)
    if (g.carModel.ok) {
        mat4 body = carTransform(cs);
        Mesh cm;
        cm.vao = g.carModel.vao;
        cm.vbo = g.carModel.vbo;
        cm.ibo = g.carModel.ibo;
        u32 carTex = g.carModel.texId;
        for (int i = 0; i < g.carModel.subs.size; i++) {
            const SubMesh& sm = g.carModel.subs[i];
            mat4 m = body;
            bool isWheel = false;
            for (int c = 0; sm.name[c] && c < 22; c++) {
                if ((sm.name[c] == 'W' || sm.name[c] == 'w') &&
                    (sm.name[c + 1] == 'h' || sm.name[c + 1] == 'H')) {
                    isWheel = true;
                    break;
                }
            }
            if (isWheel) {
                // indeks roda: FL=0 FR=1 RL=2 RR=3 (dari posisi pivot)
                int wi = (sm.pivot.z >= 0) ? ((sm.pivot.x >= 0) ? 0 : 1)
                                           : ((sm.pivot.x >= 0) ? 2 : 3);
                f32 steer = (wi <= 1) ? cs.wheels[0].sudutSetir : 0.f;
                f32 spin = cs.wheels[wi].sudutPutar;
                m = body * mat4::translate(sm.pivot) * mat4::rotY(steer) * mat4::rotX(-spin);
            }
            cm.count = sm.indexCount;
            g_ren.drawModelF(sm.indexOffset, cm, carTex, sm.baseColor, m, carTex != 0, true);
        }
    }

    // ---------------- UI tombol analog
    g_ren.uiInit();
    f32 rr = g_btn[0].r;
    for (int i = 0; i < 4; i++) {
        const Button& b = g_btn[i];
        v4 base(1, 1, 1, Cfg::tombolAlpha * (b.down ? 1.6f : 1.f));
        g_ren.uiCircle(b.cx, b.cy, b.r, base, 26);
        g_ren.uiCircle(b.cx, b.cy, b.r * 0.86f, v4(0.05f, 0.08f, 0.12f, 0.28f), 26);
        // indikator tekanan analog
        f32 a = b.amp;
        if (a > 0.01f) g_ren.uiCircle(b.cx, b.cy, b.r * 0.3f + b.r * 0.5f * a,
                                      v4(1.f, 0.85f, 0.25f, 0.45f), 20);
    }
    g_ren.uiCircle(g_btn[4].cx, g_btn[4].cy, g_btn[4].r, v4(1, 0.35f, 0.3f, 0.35f), 18);
    g_ren.uiFlush();
}

void gameShutdown() {
    LOGI("gameShutdown");
    for (int k = 0; k < 3; k++) { g.roadMesh[k].freeMesh(); g.roadMeshTrotoar[k].freeMesh(); }
    for (int i = 0; i < g.world.terrainChunks.size; i++) g.world.terrainChunks[i].mesh.freeMesh();
    for (int i = 0; i < 4; i++) { modelFree(&g.treeModel[i]); modelFree(&g.treeLod[i]); }
    for (int i = 0; i < 12; i++) {
        modelFree(&g.desaModel[i]);
        modelFree(&g.kotaModel[i]);
    }
    modelFree(&g.carModel);
    g.loaded = false;
}

// lepaskan semua objek GL (dipanggil dari thread render sebelum konteks dibuang)
void gameTeardownGL() {
    LOGI("gameTeardownGL");
    if (!g.loaded) return;
    for (int k = 0; k < 3; k++) {
        g.roadMesh[k].freeMesh();
        g.roadMeshTrotoar[k].freeMesh();
        if (g.roadTex[k]) { pglDeleteTextures(1, &g.roadTex[k]); g.roadTex[k] = 0; }
        if (g.roadTexTrotoar[k]) { pglDeleteTextures(1, &g.roadTexTrotoar[k]);
                                   g.roadTexTrotoar[k] = 0; }
    }
    for (int i = 0; i < g.world.terrainChunks.size; i++) g.world.terrainChunks[i].mesh.freeMesh();
    for (int i = 0; i < 4; i++) { modelFree(&g.treeModel[i]); modelFree(&g.treeLod[i]); }
    for (int i = 0; i < 12; i++) {
        modelFree(&g.desaModel[i]); modelFree(&g.desaModelLod[i]);
        modelFree(&g.kotaModel[i]); modelFree(&g.kotaModelLod[i]);
    }
    modelFree(&g.carModel);
    for (int i = 0; i < 56; i++)
        if (g.instMeshes[i].instVbo) pglDeleteBuffers(1, &g.instMeshes[i].instVbo);
    g.loaded = false;
}

// aset manager global (didefinisikan di sini, diisi platform.cpp)
AAssetManager* g_assetMgr = nullptr;
