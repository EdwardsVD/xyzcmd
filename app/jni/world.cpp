// world.cpp - pemuatan pulau, jalan, objek; terrain chunk dibangun bertahap.
#include "world.h"
#include <stdlib.h>
#include "config.h"

// ---------------------------------------------------------------- Island
bool Island::load(const u8* data, size_t len) {
    if (len < 36) return false;
    if (memcmp(data, "NISA", 4) != 0) { LOGE("island.bin: magic salah"); return false; }
    u32 ver = *(const u32*)(data + 4);
    N = (int)(*(const u32*)(data + 8));
    const f32* f = (const f32*)(data + 12);
    x0 = f[0]; x1 = f[1]; z0 = f[2]; z1 = f[3];
    size_t need = 36 + (size_t)N * N * 4 + (size_t)N * N * 3 + (size_t)N * N;
    if (len < need) { LOGE("island.bin kurang (%zu < %zu)", len, need); return false; }
    H = (f32*)malloc((size_t)N * N * 4);
    color = (u8*)malloc((size_t)N * N * 3);
    biome = (u8*)malloc((size_t)N * N);
    if (!H || !color || !biome) return false;
    memcpy(H, data + 36, (size_t)N * N * 4);
    memcpy(color, data + 36 + (size_t)N * N * 4, (size_t)N * N * 3);
    memcpy(biome, data + 36 + (size_t)N * N * 4 + (size_t)N * N * 3, (size_t)N * N);
    ok = true;
    LOGI("pulau %d x %d, x[%.0f..%.0f] z[%.0f..%.0f] (v%d)", N, N, x0, x1, z0, z1, ver);
    return true;
}

f32 Island::height(f32 x, f32 z) const {
    f32 fx = (x - x0) / dx(), fz = (z - z0) / dz();
    fx = vclamp(fx, 0.f, (f32)(N - 2));
    fz = vclamp(fz, 0.f, (f32)(N - 2));
    int i = (int)fx, j = (int)fz;
    f32 tx = fx - i, tz = fz - j;
    const f32* r0 = H + (size_t)j * N + i;
    const f32* r1 = r0 + N;
    f32 h00 = r0[0], h10 = r0[1], h01 = r1[0], h11 = r1[1];
    return (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
}

int Island::biomeAt(f32 x, f32 z) const {
    int i = (int)vclamp((x - x0) / dx(), 0.f, (f32)(N - 1));
    int j = (int)vclamp((z - z0) / dz(), 0.f, (f32)(N - 1));
    return biome[(size_t)j * N + i];
}

void Island::colorAt(f32 x, f32 z, f32* rgb) const {
    int i = (int)vclamp((x - x0) / dx(), 0.f, (f32)(N - 1));
    int j = (int)vclamp((z - z0) / dz(), 0.f, (f32)(N - 1));
    const u8* c = color + ((size_t)j * N + i) * 3;
    rgb[0] = c[0] / 255.f; rgb[1] = c[1] / 255.f; rgb[2] = c[2] / 255.f;
}

f32 Island::normalY(f32 x, f32 z) const {
    f32 e = dx();
    f32 hl = height(x - e, z), hr = height(x + e, z);
    f32 hd = height(x, z - e), hu = height(x, z + e);
    v3 n = normalize(v3(hl - hr, 2 * e, hd - hu));
    return n.y;
}

f32 Island::slopeDeg(f32 x, f32 z) const { return acosf(vclamp(normalY(x, z), -1.f, 1.f)) * RAD2DEG; }

// ---------------------------------------------------------------- Roads (JSON mini)
static const char* skipWs(const char* p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',' || *p == ':') p++;
    return p;
}
static const char* readNum(const char* p, f32* out) {
    p = skipWs(p);
    char* end = nullptr;
    *out = strtof(p, &end);
    return end ? end : p;
}

bool Roads::loadJson(const char* text, int len) {
    segs.reserve(4096);
    // cari bagian "jalan":[ { "jenis":"desa","lebar":6.0,"titik":[[x,z,h],...] } ... ]
    const char* p = strstr(text, "\"jalan\"");
    if (!p) { LOGE("roads.json tanpa \"jalan\""); return false; }
    int jenis = 0;
    f32 lebar = 6.f;
    while (*p) {
        if (strncmp(p, "\"jenis\"", 7) == 0) {
            const char* q = strstr(p, "\"desa\"");
            const char* k = strstr(p, "\"kota\"");
            const char* t = strstr(p, "\"tanah\"");
            if (k && (!q || k < q) && (!t || k < t)) jenis = 1;
            else if (t && (!q || t < q)) jenis = 2;
            else jenis = 0;
            p += 7;
            continue;
        }
        if (strncmp(p, "\"lebar\"", 7) == 0) {
            p = readNum(p + 7, &lebar);
            continue;
        }
        if (strncmp(p, "\"titik\"", 7) == 0) {
            p += 7;
            p = strchr(p, '[');
            if (!p) break;
            f32 px = 0, pz = 0, ph = 0;
            bool first = true;
            p++;
            while (*p) {
                p = skipWs(p);
                if (*p == ']') { p++; break; }
                if (*p != '[') { p++; continue; }
                f32 x, z, h;
                p = readNum(p + 1, &x);
                p = readNum(p, &z);
                p = readNum(p, &h);
                while (*p && *p != ']') p++;
                if (*p == ']') p++;
                if (!first) {
                    RoadSeg s;
                    s.ax = px; s.az = pz; s.bx = x; s.bz = z;
                    s.lebar = lebar; s.jenis = (u8)jenis;
                    segs.push(s);
                } else {
                    first = false;
                }
                px = x; pz = z; ph = h;
                (void)ph;
            }
            continue;
        }
        p++;
    }
    LOGI("roads.json: %d segmen", segs.size);
    return segs.size > 0;
}

static inline f32 distSegSq(f32 px, f32 pz, f32 ax, f32 az, f32 bx, f32 bz, f32* tOut) {
    f32 vx = bx - ax, vz = bz - az;
    f32 L2 = vx * vx + vz * vz;
    f32 t = L2 > 1e-6f ? ((px - ax) * vx + (pz - az) * vz) / L2 : 0.f;
    t = vclamp(t, 0.f, 1.f);
    f32 dx = px - (ax + t * vx), dz = pz - (az + t * vz);
    if (tOut) *tOut = t;
    return dx * dx + dz * dz;
}

f32 Roads::gripAt(f32 x, f32 z, const Island& isl) const {
    f32 best = 1e30f;
    int bi = -1;
    // cari 2 segmen terdekat (cepat: batasi dengan kotak 40 m)
    for (int i = 0; i < segs.size; i++) {
        const RoadSeg& s = segs[i];
        if (x < s.ax - 40 && x < s.bx - 40) continue;
        if (x > s.ax + 40 && x > s.bx + 40) continue;
        if (z < s.az - 40 && z < s.bz - 40) continue;
        if (z > s.az + 40 && z > s.bz + 40) continue;
        f32 d2 = distSegSq(x, z, s.ax, s.az, s.bx, s.bz, nullptr);
        if (d2 < best) { best = d2; bi = i; }
    }
    f32 d = sqrtf(best);
    if (bi >= 0 && d < segs[bi].lebar * 0.5f + 0.6f) {
        const RoadSeg& s = segs[bi];
        // aspal kota 1.0, aspal desa 0.95, tanah 0.62
        return s.jenis == 1 ? 0.98f : (s.jenis == 0 ? 0.94f : 0.62f);
    }
    int b = isl.biomeAt(x, z);
    if (b == 1) return 0.30f;   // pasir
    if (b == 4 || b == 5) return 0.45f;   // hutan / gunung
    if (b == 2) return 0.80f;   // kota (aspal/taman)
    return 0.55f;               // rumput
}

bool Roads::nearestOnRoad(f32 x, f32 z, f32* ox, f32* oz, f32* heading) const {
    f32 best = 1e30f;
    int bi = -1;
    f32 bt = 0;
    for (int i = 0; i < segs.size; i++) {
        const RoadSeg& s = segs[i];
        f32 t;
        f32 d2 = distSegSq(x, z, s.ax, s.az, s.bx, s.bz, &t);
        if (d2 < best) { best = d2; bi = i; bt = t; }
    }
    if (bi < 0) return false;
    const RoadSeg& s = segs[bi];
    *ox = s.ax + (s.bx - s.ax) * bt;
    *oz = s.az + (s.bz - s.az) * bt;
    *heading = atan2f(s.bx - s.ax, s.bz - s.az);
    return true;
}

// ---------------------------------------------------------------- Props
bool Props::load(const u8* data, size_t len) {
    if (len < 16 || memcmp(data, "NPRP", 4) != 0) { LOGE("props.bin: magic salah"); return false; }
    count = (int)(*(const u32*)(data + 8));
    chunkCount = (int)(*(const u32*)(data + 12));
    size_t need = 16 + (size_t)count * 20 + (size_t)count * 2 + (size_t)chunkCount * 12;
    if (len < need) { LOGE("props.bin kurang (%zu < %zu)", len, need); return false; }
    items = (Inst*)malloc((size_t)count * sizeof(Inst));
    chunkId = (i32*)malloc((size_t)chunkCount * 4);
    chunkStart = (i32*)malloc((size_t)chunkCount * 4);
    chunkCountArr = (i32*)malloc((size_t)chunkCount * 4);
    if (!items || !chunkId || !chunkStart || !chunkCountArr) return false;
    const f32* f = (const f32*)(data + 16);
    const u16* ty = (const u16*)(data + 16 + (size_t)count * 20);
    for (int i = 0; i < count; i++) {
        items[i].x = f[i * 5 + 0];
        items[i].y = f[i * 5 + 1];
        items[i].z = f[i * 5 + 2];
        items[i].yaw = f[i * 5 + 3];
        items[i].scale = f[i * 5 + 4];
        items[i].type = ty[i];
    }
    // catatan: data + 16 + count*22 tidak selalu 4-byte ter-align
    // (saat count ganjil), jadi baca lewat memcpy agar portabel (ARM mengharuskan
    // pemakaian i32* ter-align, tidak boleh cast langsung dari offset aneh).
    const u8* base = data + 16 + (size_t)count * 22;
    for (int i = 0; i < chunkCount; i++) {
        i32 v;
        memcpy(&v, base + (size_t)i * 4, 4);               chunkId[i] = v;
        memcpy(&v, base + (size_t)(chunkCount + i) * 4, 4); chunkStart[i] = v;
        memcpy(&v, base + (size_t)(2 * chunkCount + i) * 4, 4); chunkCountArr[i] = v;
    }
    // grid cepat 200 m -> rentang chunk
    cellN = 400;
    cellX0 = -8000.f;
    cellZ0 = -8000.f;
    f32 step = 40.f;  // sel grid 200m -> id = iz*1000+ix, ix = (x - x0)/200
    (void)step;
    LOGI("props: %d instance, %d chunk", count, chunkCount);
    return true;
}

int Props::gather(f32 x, f32 z, f32 radius, int* outChunks, int maxOut) const {
    // chunk id = iz*1000 + ix dengan ukuran 200 m, asal (x0,z0) pulau (-4800,-4700)
    const f32 CH = 200.f;
    const f32 X0 = -4800.f, Z0 = -4700.f;
    int ix0 = (int)floorf((x - radius - X0) / CH), ix1 = (int)floorf((x + radius - X0) / CH);
    int iz0 = (int)floorf((z - radius - Z0) / CH), iz1 = (int)floorf((z + radius - Z0) / CH);
    int n = 0;
    for (int iz = iz0; iz <= iz1 && n < maxOut; iz++) {
        for (int ix = ix0; ix <= ix1 && n < maxOut; ix++) {
            int id = iz * 1000 + ix;
            for (int k = 0; k < chunkCount; k++) {
                if (chunkId[k] == id) {
                    outChunks[n++] = k;
                    break;
                }
            }
        }
    }
    return n;
}

// ---------------------------------------------------------------- World
bool World::loadAssets(const u8* islandBin, size_t islandLen, const char* roadsJson,
                       int roadsLen, const u8* propsBin, size_t propsLen) {
    if (!isl.load(islandBin, islandLen)) return false;
    if (!roads.loadJson(roadsJson, roadsLen)) LOGW("roads.json gagal, grip pakai biome saja");
    if (!props.load(propsBin, propsLen)) LOGW("props.bin gagal, tanpa objek");
    tcN = 10;
    tcSize = (isl.x1 - isl.x0) / tcN;
    terrainChunks.reserve(tcN * tcN);
    for (int j = 0; j < tcN; j++)
        for (int i = 0; i < tcN; i++) {
            TerrainMesh tm;
            tm.ci = i;
            tm.cj = j;
            terrainChunks.push(tm);
        }
    return true;
}

TerrainMesh* World::chunkAt(int ci, int cj) {
    if (ci < 0 || cj < 0 || ci >= tcN || cj >= tcN) return nullptr;
    int idx = cj * tcN + ci;
    if (idx < 0 || idx >= terrainChunks.size) return nullptr;
    return &terrainChunks[idx];
}

int World::buildTerrainNear(f32 x, f32 z, int maxBuild, int radiusChunk) {
    int ci = (int)((x - isl.x0) / tcSize), cj = (int)((z - isl.z0) / tcSize);
    int built = 0;
    // urut jarak chunk terdekat dulu
    for (int ring = 0; ring <= radiusChunk && built < maxBuild; ring++) {
        for (int dj = -ring; dj <= ring && built < maxBuild; dj++) {
            for (int di = -ring; di <= ring && built < maxBuild; di++) {
                if (ring > 0 && vabs(di) != ring && vabs(dj) != ring) continue;
                TerrainMesh* tm = chunkAt(ci + di, cj + dj);
                if (!tm || tm->built) continue;
                // bangun mesh grid
                int per = 16;                       // segmen per sisi chunk
                int nv = (per + 1) * (per + 1);
                struct TV { f32 x, y, z, nx, ny, nz, r, g, b; };
                TV* v = (TV*)malloc(sizeof(TV) * nv);
                u32* idx = (u32*)malloc(sizeof(u32) * per * per * 6);
                if (!v || !idx) { free(v); free(idx); return built; }
                f32 sx = tcSize / per;
                f32 bx = isl.x0 + tm->ci * tcSize, bz = isl.z0 + tm->cj * tcSize;
                int k = 0;
                for (int jj = 0; jj <= per; jj++) {
                    for (int ii = 0; ii <= per; ii++) {
                        f32 px = bx + ii * sx, pz = bz + jj * sx;
                        f32 h = isl.height(px, pz);
                        f32 e = isl.dx();
                        f32 hl = isl.height(px - e, pz), hr = isl.height(px + e, pz);
                        f32 hd = isl.height(px, pz - e), hu = isl.height(px, pz + e);
                        v3 n = normalize(v3(hl - hr, 2 * e, hd - hu));
                        f32 c[3];
                        isl.colorAt(px, pz, c);
                        TV& t = v[k++];
                        t.x = px; t.y = h; t.z = pz;
                        t.nx = n.x; t.ny = n.y; t.nz = n.z;
                        t.r = c[0]; t.g = c[1]; t.b = c[2];
                    }
                }
                int t = 0;
                for (int jj = 0; jj < per; jj++)
                    for (int ii = 0; ii < per; ii++) {
                        u32 a = jj * (per + 1) + ii, b = a + 1, c = a + per + 1, d = c + 1;
                        idx[t++] = a; idx[t++] = c; idx[t++] = b;
                        idx[t++] = b; idx[t++] = c; idx[t++] = d;
                    }
                tm->mesh.make(v, nv, sizeof(TV), idx, t, true, false, false);
                tm->built = true;
                free(v);
                free(idx);
                built++;
            }
        }
    }
    return built;
}

void World::drawTerrain(const Frustum& fr, const v3& cam) {
    // potong terrain di luar jarak kabut: tanpa ini seluruh pulau (10x10 chunk)
    // tetap digambar dan menutupi jalan/objek karena menulis depth lebih dulu.
    const f32 cut2 = (Cfg::kabutAkhir + 240.f) * (Cfg::kabutAkhir + 240.f);
    for (int i = 0; i < terrainChunks.size; i++) {
        TerrainMesh& tm = terrainChunks[i];
        if (!tm.built) continue;
        f32 ox = isl.x0 + (tm.ci + 0.5f) * tcSize - cam.x;
        f32 oz = isl.z0 + (tm.cj + 0.5f) * tcSize - cam.z;
        if (ox * ox + oz * oz > cut2) continue;
        f32 cx = isl.x0 + (tm.ci + 0.5f) * tcSize, cz = isl.z0 + (tm.cj + 0.5f) * tcSize;
        f32 h = isl.height(cx, cz);
        f32 r = tcSize * 0.75f;
        if (!fr.sphereIn(v3(cx, h, cz), r)) continue;
        mat4 I;
        g_ren.drawModel(tm.mesh, 0, v4(1, 1, 1, 1), I, false, true, false);
    }
    (void)cam;
}
