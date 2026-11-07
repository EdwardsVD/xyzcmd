// gltf.h - loader GLB/glTF (cgltf + stb_image). Mendukung tekstur lewat bufferView
// DAN data URI (sesuai fakta aset). Hasil: model siap render (satu VBO/IBO, sub-mesh per material).
#pragma once
#include "core.h"
#include "gl_api.h"

struct SubMesh {
    int indexOffset = 0;
    int indexCount = 0;
    v3 pivot = v3(0, 0, 0);   // titik putar sub-mesh (roda mobil) relatif as roda
    char name[24] = {0};
    int texId = 0;          // handle GL
    v4 baseColor = v4(1, 1, 1, 1);
    f32 emissive = 0.f;
    bool doubleSided = false;
};

struct Model {
    u32 vao = 0, vbo = 0, ibo = 0;
    int indexCount = 0;
    Array<SubMesh> subs;
    v3 bmin = v3(0, 0, 0), bmax = v3(0, 0, 0);
    Array<u32> textures;      // untuk dibersihkan
    int primCount = 0;
    u32 texId = 0;
    u32 texId2 = 0;      // tekstur material kedua (trotoar jalan)
    bool ok = false;
    float height() const { return bmax.y - bmin.y; }
};

// muat model dari data GLB di memori. texMax = ukuran maksimum tekstur (mis. 512)
bool modelLoadGLB(const void* data, size_t size, Model* out, int texMax = 512);
// loader cepat untuk format hasil tools/pack_assets.py (tanpa cgltf)
bool modelLoadPacked(const void* data, size_t size, Model* out);
void modelFree(Model* m);
