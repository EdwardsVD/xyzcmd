// render.h - program shader, buffer mesh, dan gambar UI 2D.
#pragma once
#include "core.h"
#include "gl_api.h"

struct Shader {
    u32 prog = 0;
    int uMVP = -1, uModel = -1, uLightDir = -1, uFogColor = -1, uFogDist = -1;
    int uBaseColor = -1, uUseTex = -1, uTex = -1, uCamPos = -1, uVertexColor = -1;
    int aPos = -1, aNrm = -1, aUV = -1, aCol = -1, iPos = -1, iRot = -1;
    bool build(const char* vs, const char* fs);
};

struct Mesh {
    u32 vao = 0, vbo = 0, ibo = 0, instVbo = 0;
    int count = 0;
    int stride = 0;
    void make(const void* verts, int vcount, int vstride, const u32* idx, int icount,
              bool withColor, bool withUV, bool withInstance);
    void updateInstances(const void* data, int bytes);
    void freeMesh();
};

struct Renderer {
    Shader terr, obj, ui, inst;
    u32 whiteTex = 0;
    int width = 0, height = 0;
    mat4 curVP;
    v3 curCam;
    v3 lightDir = v3(0.42f, 0.78f, 0.32f);
    v3 fogColor = v3(0.62f, 0.74f, 0.86f);
    f32 fogStart = 200.f, fogEnd = 430.f;
    bool init();
    void setSize(int w, int h) { width = w; height = h; }
    void beginFrame(const mat4& vp, const v3& camPos) { curVP = vp; curCam = camPos; }
    void drawModel(const Mesh& m, const u32 tex, const v4& color, const mat4& model,
                   bool useTex, bool vertColor, bool doubleSided);
    void drawInstanced(const Mesh& m, const u32 tex, const v4& color, const mat4& model,
                       bool useTex, int instances, bool doubleSided);
    // gambar satu sub-mesh (offset indeks) dari model
    void drawModelF(int indexOffset, const Mesh& m, const u32 tex, const v4& color,
                    const mat4& model, bool useTex, bool doubleSided);
    void uiInit();
    void uiRect(f32 x, f32 y, f32 w, f32 h, const v4& c);
    void uiCircle(f32 cx, f32 cy, f32 r, const v4& c, int seg = 20);
    void uiFlush();
    void setFog(f32 start, f32 end, const v3& col) { fogStart = start; fogEnd = end; fogColor = col; }
};
extern Renderer g_ren;
