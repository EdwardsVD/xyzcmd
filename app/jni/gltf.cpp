// gltf.cpp - loader model runtime (cgltf + stb_image).
//
// Aset .glb dari Drive punya tekstur lewat data URI dan node tanpa transform: pipeline
// build (tools/pack_assets.py) sudah menulis ulang .glb dengan indeks "dipanggang"
// (vertex unik, urut per sub-mesh) sehingga runtime hanya membaca POSITION/NORMAL/
// TEXCOORD_0 + indices apa adanya. Di sini semua tetap didukung:
//   * tekstur lewat bufferView (GLB) DAN data URI base64
//   * node transform sederhana (translation/rotation/scale/children)
#define CGLTF_IMPLEMENTATION
#include "../../third_party/cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "../../third_party/stb_image.h"

#include "gltf.h"

// ------------------------------------------------------------ base64 (data URI)
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static u8* base64decode(const char* s, int n, int* outLen) {
    u8* out = (u8*)malloc((size_t)n + 4);
    if (!out) return nullptr;
    int o = 0, acc = 0, bits = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '=') break;
        int v = b64val(c);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (u8)((acc >> bits) & 0xFF);
        }
    }
    *outLen = o;
    return out;
}

// ------------------------------------------------------------ tekstur
static u32 uploadTexture(const u8* enc, int len, int texMax) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pix = stbi_load_from_memory(enc, len, &w, &h, &comp, 4);
    if (!pix) {
        LOGW("tekstur gagal decode (%d byte) -> warna putih", len);
        return 0;
    }
    if (texMax > 0 && (w > texMax || h > texMax)) {
        int nw = w, nh = h;
        while (nw > texMax) nw /= 2;
        while (nh > texMax) nh /= 2;
        stbi_uc* np = (stbi_uc*)malloc((size_t)nw * nh * 4);
        if (np) {
            for (int y = 0; y < nh; y++) {
                int sy = y * h / nh;
                for (int x = 0; x < nw; x++) {
                    int sx = x * w / nw;
                    memcpy(np + ((size_t)y * nw + x) * 4, pix + ((size_t)sy * w + sx) * 4, 4);
                }
            }
            stbi_image_free(pix);
            pix = np;
            w = nw;
            h = nh;
        }
    }
    u32 tex = 0;
    pglGenTextures(1, &tex);
    pglBindTexture(GL_TEXTURE_2D, tex);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // mip hanya sampai 1/4 res: mip terkecil membuat permukaan miring (jalan/jembatan)
    // terlihat buram, sementara pop aliasing praktis tidak terlihat di layar HP.
    {
        int lv = 0;
        while ((w >> (lv + 2)) > 0 && (h >> (lv + 2)) > 0 && lv < 8) lv++;
        pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, lv);
    }
    pglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix);
    pglGenerateMipmap(GL_TEXTURE_2D);
    // anisotropic filtering: tekstur aspal & beton dilihat sangat miring saat menyetir
    if (pglTexParameterf) {
        static float aniso = -1.f;
        if (aniso < 0.f) {
            aniso = 1.f;
            if (pglGetFloatv) {
                float mx = 1.f;
                pglGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &mx);
                if (mx >= 2.f) aniso = mx > 8.f ? 8.f : mx;
            }
        }
        if (aniso > 1.f) pglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    }
    stbi_image_free(pix);
    return tex;
}

// ambil byte PNG dari image glTF: bufferView (GLB) atau data URI base64
static u8* imageBytes(cgltf_image* img, int* len, bool* mustFree) {
    *mustFree = false;
    *len = 0;
    if (!img) return nullptr;
    if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
        const u8* base = (const u8*)img->buffer_view->buffer->data;
        *len = (int)img->buffer_view->size;
        return (u8*)(base + img->buffer_view->offset);
    }
    if (img->uri && strncmp(img->uri, "data:", 5) == 0) {
        const char* comma = strchr(img->uri, ',');
        if (!comma) return nullptr;
        bool isB64 = false;
        int hdrLen = (int)(comma - img->uri);
        for (int i = 0; i + 8 < hdrLen; i++) {
            if (img->uri[i] == ';' && strncmp(img->uri + i, ";base64,", 8) == 0) isB64 = true;
        }
        if (!isB64) {
            *len = (int)strlen(comma + 1);
            return (u8*)comma + 1;
        }
        int decLen = 0;
        u8* dec = base64decode(comma + 1, (int)strlen(comma + 1), &decLen);
        if (dec) {
            *len = decLen;
            *mustFree = true;
        }
        return dec;
    }
    return nullptr;
}

// ------------------------------------------------------------ akumulasi primitif
struct PrimInfo {
    cgltf_primitive* prim;
    int vertexStart;     // indeks vertex pertama di buffer besar
    int vertexCount;
    int indexStart;
    int indexCount;
    SubMesh sm;
    mat4 xform;
};
struct Vtx {
    f32 px, py, pz, nx, ny, nz, u, v;
};

bool modelLoadGLB(const void* data, size_t size, Model* out, int texMax) {
    memset(out, 0, sizeof(Model));
    out->subs.reserve(8);
    out->textures.reserve(4);
    cgltf_options opt;
    memset(&opt, 0, sizeof(opt));
    opt.type = cgltf_file_type_glb;
    cgltf_data* d = nullptr;
    cgltf_result r = cgltf_parse(&opt, data, size, &d);
    if (r != cgltf_result_success) {
        memset(&opt, 0, sizeof(opt));
        r = cgltf_parse(&opt, data, size, &d);
        if (r != cgltf_result_success) {
            LOGE("cgltf_parse gagal (%d)", (int)r);
            return false;
        }
    }
    memset(&opt, 0, sizeof(opt));
    opt.type = cgltf_file_type_glb;
    cgltf_load_buffers(&opt, d, nullptr);

    // hitung ukuran total
    cgltf_size nverts = 0, nidx = 0;
    for (cgltf_size mi = 0; mi < d->meshes_count; mi++)
        for (cgltf_size pi = 0; pi < d->meshes[mi].primitives_count; pi++) {
            cgltf_primitive* p = &d->meshes[mi].primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;
            for (cgltf_size ai = 0; ai < p->attributes_count; ai++)
                if (p->attributes[ai].type == cgltf_attribute_type_position) {
                    nverts += p->attributes[ai].data->count;
                    nidx += p->indices ? p->indices->count : p->attributes[ai].data->count;
                }
        }
    if (nverts == 0 || nidx == 0) {
        LOGE("model tanpa segitiga");
        cgltf_free(d);
        return false;
    }
    Vtx* verts = (Vtx*)malloc((size_t)nverts * sizeof(Vtx));
    u32* idx = (u32*)malloc((size_t)nidx * sizeof(u32));
    PrimInfo* prims = (PrimInfo*)malloc((size_t)(nverts ? 1 : 1) * 0 + sizeof(PrimInfo) * 64);
    int primCap = 64;
    if (!verts || !idx || !prims) {
        free(verts); free(idx); free(prims); cgltf_free(d);
        return false;
    }
    int vo = 0, io = 0, np = 0;
    v3 bmin(1e30f, 1e30f, 1e30f), bmax(-1e30f, -1e30f, -1e30f);

    // material -> tekstur (cache per material)
    int matCount = (int)d->materials_count;
    u32* matTex = (u32*)malloc(sizeof(u32) * (matCount ? matCount : 1));
    for (int i = 0; i < matCount; i++) matTex[i] = 0;

    for (cgltf_size mi = 0; mi < d->meshes_count; mi++) {
        for (cgltf_size pi = 0; pi < d->meshes[mi].primitives_count; pi++) {
            cgltf_primitive* p = &d->meshes[mi].primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;
            cgltf_accessor* apos = nullptr, *anrm = nullptr, *auv = nullptr;
            for (cgltf_size ai = 0; ai < p->attributes_count; ai++) {
                cgltf_attribute* a = &p->attributes[ai];
                if (a->type == cgltf_attribute_type_position) apos = a->data;
                else if (a->type == cgltf_attribute_type_normal) anrm = a->data;
                else if (a->type == cgltf_attribute_type_texcoord && a->index == 0) auv = a->data;
            }
            if (!apos) continue;
            if (np >= primCap) {
                primCap *= 2;
                PrimInfo* np2 = (PrimInfo*)realloc(prims, sizeof(PrimInfo) * primCap);
                if (!np2) break;
                prims = np2;
            }
            PrimInfo& info = prims[np];
            info.prim = p;
            info.vertexStart = vo;
            info.vertexCount = (int)apos->count;
            f32 pos[3], nrm[3], uv[2];
            for (cgltf_size vi = 0; vi < apos->count; vi++) {
                pos[0] = 0; pos[1] = 0; pos[2] = 0;
                nrm[0] = 0; nrm[1] = 1; nrm[2] = 0;
                uv[0] = 0; uv[1] = 0;
                cgltf_accessor_read_float(apos, vi, pos, 3);
                if (anrm) cgltf_accessor_read_float(anrm, vi, nrm, 3);
                if (auv) cgltf_accessor_read_float(auv, vi, uv, 2);
                Vtx& v = verts[vo++];
                v.px = pos[0]; v.py = pos[1]; v.pz = pos[2];
                v.nx = nrm[0]; v.ny = nrm[1]; v.nz = nrm[2];
                v.u = uv[0]; v.v = uv[1];
                if (v.px < bmin.x) bmin.x = v.px;
                if (v.py < bmin.y) bmin.y = v.py;
                if (v.pz < bmin.z) bmin.z = v.pz;
                if (v.px > bmax.x) bmax.x = v.px;
                if (v.py > bmax.y) bmax.y = v.py;
                if (v.pz > bmax.z) bmax.z = v.pz;
            }
            int istart = io;
            if (p->indices) {
                for (cgltf_size ii = 0; ii < p->indices->count; ii++)
                    idx[io++] = (u32)(cgltf_accessor_read_index(p->indices, ii) + info.vertexStart);
            } else {
                for (int ii = 0; ii < info.vertexCount; ii++) idx[io++] = (u32)(ii + info.vertexStart);
            }
            info.indexStart = istart;
            info.indexCount = io - istart;
            memset(&info.sm, 0, sizeof(SubMesh));
            info.sm.indexOffset = istart;
            info.sm.indexCount = info.indexCount;
            info.sm.baseColor = v4(1, 1, 1, 1);
            info.sm.texId = 0;
            if (p->material) {
                int mIdx = (int)(p->material - d->materials);
                if (matTex[mIdx] == 0 && p->material->has_pbr_metallic_roughness &&
                    p->material->pbr_metallic_roughness.base_color_texture.texture) {
                    cgltf_image* img =
                        p->material->pbr_metallic_roughness.base_color_texture.texture->image;
                    int len = 0;
                    bool mf = false;
                    u8* bytes = imageBytes(img, &len, &mf);
                    if (bytes && len > 0) matTex[mIdx] = uploadTexture(bytes, len, texMax);
                    if (mf && bytes) free(bytes);
                }
                info.sm.texId = matTex[mIdx];
                f32* c = p->material->pbr_metallic_roughness.base_color_factor;
                info.sm.baseColor = v4(c[0], c[1], c[2], c[3]);
                info.sm.emissive = p->material->emissive_factor[0] + p->material->emissive_factor[1] +
                                   p->material->emissive_factor[2];
            }
            info.xform = mat4();  // aset sudah dipanggang di build time
            out->subs.push(info.sm);
            np++;
        }
    }
    cgltf_free(d);
    free(prims);
    free(matTex);

    // unggah ke GPU (satu VBO/IBO)
    pglGenVertexArrays(1, &out->vao);
    pglBindVertexArray(out->vao);
    pglGenBuffers(1, &out->vbo);
    pglBindBuffer(GL_ARRAY_BUFFER, out->vbo);
    pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptrT)((size_t)vo * sizeof(Vtx)), verts, GL_STATIC_DRAW);
    pglGenBuffers(1, &out->ibo);
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ibo);
    pglBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptrT)((size_t)io * sizeof(u32)), idx, GL_STATIC_DRAW);
    pglEnableVertexAttribArray(0);
    pglVertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(Vtx), (const void*)0);
    pglEnableVertexAttribArray(1);
    pglVertexAttribPointer(1, 3, GL_FLOAT, 0, sizeof(Vtx), (const void*)(12));
    pglEnableVertexAttribArray(2);
    pglVertexAttribPointer(2, 2, GL_FLOAT, 0, sizeof(Vtx), (const void*)(24));
    pglBindVertexArray(0);
    free(verts);
    free(idx);
    out->indexCount = io;
    out->bmin = bmin;
    out->bmax = bmax;
    out->ok = true;
    return true;
}

void modelFree(Model* m) {
    if (m->texId2) { pglDeleteTextures(1, &m->texId2); m->texId2 = 0; }
    if (!m) return;
    if (m->vao) pglDeleteVertexArrays(1, &m->vao);
    if (m->vbo) pglDeleteBuffers(1, &m->vbo);
    if (m->ibo) pglDeleteBuffers(1, &m->ibo);
    if (m->texId) {
        u32 t = m->texId;
        pglDeleteTextures(1, &t);
        m->texId = 0;
    }
    m->subs.freeMem();
    m->vao = m->vbo = m->ibo = 0;
    m->ok = false;
}

// ------------------------------------------------------------------ loader paket cepat
// Format yang ditulis tools/pack_assets.py:
//   BIN = [vertex 32 byte (pos3,nrm3,uv2)][indeks u32][JPEG/PNG tekstur]
//   bufferViews: 0 = verteks, 1 = indeks, 2 = tekstur (opsional)
// Loader ini tidak memakai cgltf: cukup parsing ringan + salin langsung ke VBO.
static int findU32After(const char* p, const char* key, int defVal) {
    const char* q = strstr(p, key);
    if (!q) return defVal;
    q += strlen(key);
    while (*q && (*q == ' ' || *q == ':' || *q == '\t')) q++;
    return (int)strtol(q, nullptr, 10);
}

static const char* skipComma(const char* p) {
    while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ':') p++;
    return p;
}

bool modelLoadPacked(const void* data, size_t size, Model* out) {
    out->subs.reserve(1);
    const u8* d = (const u8*)data;
    if (size < 20 || memcmp(d, "glTF", 4) != 0) return false;
    u32 jlen = *(const u32*)(d + 12);
    const char* js = (const char*)(d + 20);
    if (20 + (size_t)jlen + 8 > size) return false;
    const u8* bin = d + 20 + jlen + 8;
    size_t binLen = *(const u32*)(d + 20 + jlen);
    if ((size_t)(bin - d) + binLen > size) binLen = size - (size_t)(bin - d);

    const char* bvs = strstr(js, "\"bufferViews\"");
    if (!bvs) return false;
    int vOff = findU32After(bvs, "\"byteOffset\"", 0);
    int vLen = findU32After(bvs, "\"byteLength\"", 0);
    const char* afterFirst = strstr(bvs, "target");  // penanda akhir view pertama
    int iLen = 0, tOff = 0, tLen = 0;
    if (afterFirst) {
        // view indeks: byteOffset / byteLength berikutnya
        const char* p2 = strstr(afterFirst, "byteOffset");
        iLen = findU32After(afterFirst, "\"byteLength\"", 0);
        if (p2) {
            const char* p3 = strstr(p2, "byteLength");
            if (p3) tLen = findU32After(p3, "\"byteLength\"", 0);
            const char* p4 = strstr(p3 ? p3 : p2, "\"byteOffset\"");
            if (p4) tOff = findU32After(p4, "\"byteOffset\"", 0);
        }
    }
    // offset indeks = vLen (disusun rapat), verifikasi lewat byteOffset view 2
    int iOff = vLen;
    {
        const char* p = bvs;
        // cari byteOffset kedua & ketiga setelah view pertama
        p = strstr(p, "\"byteOffset\"");
        if (p) {
            p = strstr(p + 1, "\"byteOffset\"");
            if (p) iOff = findU32After(p, "\"byteOffset\"", iOff);
            p = strstr(p + 1, "\"byteOffset\"");
            if (p) tOff = findU32After(p, "\"byteOffset\"", 0);
        }
    }
    int nv = vLen / 32, ni = iLen / 4;
    if (nv <= 0 || ni <= 0) { LOGE("paket model rusak (nv=%d ni=%d)", nv, ni); return false; }
    if ((size_t)(iOff + iLen) > binLen) { LOGE("paket model: indeks di luar buffer"); return false; }
    if (tOff > 0 && (size_t)(tOff + tLen) > binLen) tLen = (int)binLen - tOff;

    pglGenVertexArrays(1, &out->vao);
    pglBindVertexArray(out->vao);
    pglGenBuffers(1, &out->vbo);
    pglBindBuffer(GL_ARRAY_BUFFER, out->vbo);
    pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptrT)vLen, bin + vOff, GL_STATIC_DRAW);
    pglGenBuffers(1, &out->ibo);
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ibo);
    pglBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptrT)iLen, bin + iOff, GL_STATIC_DRAW);
    pglEnableVertexAttribArray(0);
    pglVertexAttribPointer(0, 3, GL_FLOAT, 0, 32, (const void*)0);
    pglEnableVertexAttribArray(1);
    pglVertexAttribPointer(1, 3, GL_FLOAT, 0, 32, (const void*)12);
    pglEnableVertexAttribArray(2);
    pglVertexAttribPointer(2, 2, GL_FLOAT, 0, 32, (const void*)24);
    pglBindVertexArray(0);

    if (tLen > 0) out->texId = uploadTexture(bin + tOff, tLen, 0);
    // tekstur kedua (trotoar): bufferView ke-4 kalau ada
    {
        const char* p = strstr(bvs, "\"byteOffset\"");
        for (int k = 1; k < 4 && p; k++) p = strstr(p + 1, "\"byteOffset\"");
        if (p) {
            int t2off = findU32After(p, "\"byteOffset\"", 0);
            const char* q = strstr(p, "\"byteLength\"");
            int t2len = q ? findU32After(q, "\"byteLength\"", 0) : 0;
            if (t2len > 0 && (size_t)(t2off + t2len) <= binLen)
                out->texId2 = uploadTexture(bin + t2off, t2len, 0);
        }
    }
    out->indexCount = ni;
    // sub-mesh (termasuk pivot roda) dari metadata "nusa"

    const char* nb = strstr(js, "\"nusa\"");
    if (nb && strstr(nb, "\"prims\"")) {
        const char* q = strchr(strstr(nb, "\"prims\""), '[');  // '[[' pembuka
        if (q) q++;                                            // lewati '[' luar
        while (q && *q == '[') {
            q++;  // lewati '[' sub-mesh
            SubMesh sm;
            sm.texId = out->texId;
            sm.baseColor = v4(1, 1, 1, 1);
            while (*q == ' ') q++;
            sm.indexOffset = (int)strtol(q, (char**)&q, 10);
            while (*q == ',' || *q == ' ') q++;
            sm.indexCount = (int)strtol(q, (char**)&q, 10);
            while (*q == ',' || *q == ' ') q++;
            if (*q == '"') {
                q++;
                int n = 0;
                while (*q && *q != '"' && n < 23) sm.name[n++] = *q++;
                sm.name[n] = 0;
                if (*q == '"') q++;
            }
            while (*q == ',' || *q == ' ') q++;
            if (*q == '[') {  // pivot roda
                q++;
                f32 px = strtof(q, (char**)&q);
                while (*q == ',' || *q == ' ') q++;
                f32 py = strtof(q, (char**)&q);
                while (*q == ',' || *q == ' ') q++;
                f32 pz = strtof(q, (char**)&q);
                while (*q && *q != ']') q++;
                if (*q == ']') q++;
                sm.pivot = v3(px, py, pz);
            } else {
                while (*q && *q != ']') q++;  // "null"
            }
            int matId = 0;
            if (*q == ',') {                 // elemen ke-5: indeks material
                q++;
                while (*q == ' ') q++;
                matId = (int)strtol(q, (char**)&q, 10);
            }
            while (*q && *q != ']') q++;      // tutup sub-mesh
            if (*q == ']') q++;
            while (*q == ',' || *q == ' ') q++;
            if (matId >= 1 && out->texId2) sm.texId = out->texId2;
            out->subs.push(sm);
        }
    }
    if (out->subs.size == 0) {
        SubMesh sm;
        sm.indexOffset = 0;
        sm.indexCount = ni;
        sm.texId = out->texId;
        sm.baseColor = v4(1, 1, 1, 1);
        out->subs.push(sm);
    }
    out->primCount = out->subs.size;
    // bounding box dari verteks (untuk LOD/culling)
    v3 bmin(1e30f, 1e30f, 1e30f), bmax(-1e30f, -1e30f, -1e30f);
    const f32* vp = (const f32*)(bin + vOff);
    for (int i = 0; i < nv; i++) {
        v3 p(vp[i * 8], vp[i * 8 + 1], vp[i * 8 + 2]);
        if (p.x < bmin.x) bmin.x = p.x;
        if (p.y < bmin.y) bmin.y = p.y;
        if (p.z < bmin.z) bmin.z = p.z;
        if (p.x > bmax.x) bmax.x = p.x;
        if (p.y > bmax.y) bmax.y = p.y;
        if (p.z > bmax.z) bmax.z = p.z;
    }
    out->bmin = bmin;
    out->bmax = bmax;
    out->ok = true;
    return true;
}
