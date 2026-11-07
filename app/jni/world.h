// world.h - data pulau (heightmap/biome/warna), jaringan jalan (roads.json),
// sebaran objek (props.bin), dan fungsi sampling tinggi/grip.
#pragma once
#include "core.h"
#include "render.h"

struct Island {
    int N = 0;
    f32 x0 = 0, x1 = 0, z0 = 0, z1 = 0;
    f32* H = nullptr;      // tinggi (m)
    u8* color = nullptr;   // warna sRGB terrain (N*N*3)
    u8* biome = nullptr;   // indeks biome (N*N)
    bool ok = false;
    f32 dx() const { return (x1 - x0) / (N - 1); }
    f32 dz() const { return (z1 - z0) / (N - 1); }
    bool load(const u8* data, size_t len);
    f32 height(f32 x, f32 z) const;
    f32 normalY(f32 x, f32 z) const;          // cos kemiringan (1 = datar)
    f32 slopeDeg(f32 x, f32 z) const;
    int biomeAt(f32 x, f32 z) const;
    void colorAt(f32 x, f32 z, f32* rgb) const;
};

// satu segmen jalan untuk grip & spawn
struct RoadSeg {
    f32 ax, az, bx, bz;
    f32 lebar;
    u8 jenis;      // 0 desa,1 kota,2 tanah
};

struct Roads {
    Array<RoadSeg> segs;
    f32* gridDist = nullptr;      // jarak kasar ke jalan (grid 100 m) untuk deteksi cepat
    int gn = 0;
    f32 gx0 = 0, gz0 = 0, gstep = 100.f;
    bool loadJson(const char* text, int len);
    // grip 0..1 berdasarkan posisi (aspal/tanah/rumput/pasir)
    f32 gripAt(f32 x, f32 z, const Island& isl) const;
    bool nearestOnRoad(f32 x, f32 z, f32* ox, f32* oz, f32* heading) const;
};

// instance objek dari props.bin
struct Inst {
    f32 x, y, z, yaw, scale;
    u16 type;
};
struct Props {
    Inst* items = nullptr;
    int count = 0;
    int chunkCount = 0;
    i32* chunkId = nullptr;     // id chunk per kelompok
    i32* chunkStart = nullptr;
    i32* chunkCountArr = nullptr;
    int* cellOf = nullptr;      // grid cepat: indeks chunk per sel 200 m
    int cellN = 0;
    f32 cellX0 = 0, cellZ0 = 0;
    bool load(const u8* data, size_t len);
    // ambil chunk yang berjarak < radius dari posisi
    int gather(f32 x, f32 z, f32 radius, int* outChunks, int maxOut) const;
};

// terrain mesh per potongan (dibangun bertahap di runtime)
struct TerrainMesh {
    Mesh mesh;
    int ci = 0, cj = 0;         // posisi chunk (kolom, baris) di grid terrain
    bool built = false;
};

struct World {
    Island isl;
    Roads roads;
    Props props;
    Array<TerrainMesh> terrainChunks;
    int tcN = 0;                 // jumlah chunk per sisi (mis. 10 -> 10x10)
    f32 tcSize = 0;              // ukuran chunk (m)
    int texHantu = 0;
    bool loadAssets(const u8* islandBin, size_t islandLen, const char* roadsJson, int roadsLen,
                    const u8* propsBin, size_t propsLen);
    // bangun maksimal n chunk terrain dekat (x,z); return jumlah dibangun
    int buildTerrainNear(f32 x, f32 z, int maxBuild, int radiusChunk);
    TerrainMesh* chunkAt(int ci, int cj);
    void drawTerrain(const Frustum& fr, const v3& cam);
};
