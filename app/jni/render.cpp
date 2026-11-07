// render.cpp - shader GLES3, mesh, dan UI 2D untuk tombol sentuh.
#include "render.h"

Renderer g_ren;

static const char* VS_TERR =
    "uniform mat4 uMVP;\n"
    "attribute vec3 aPos; attribute vec3 aNrm; attribute vec3 aCol;\n"
    "varying vec3 vN; varying vec3 vC; varying vec3 vW;\n"
    "void main(){ vN=aNrm; vC=aCol; vW=aPos; gl_Position=uMVP*vec4(aPos,1.0); }\n";

static const char* FS_TERR =
    "precision mediump float;\n"
    "uniform vec3 uLightDir; uniform vec3 uFogColor; uniform vec2 uFogDist; uniform vec3 uCamPos;\n"
    "varying vec3 vN; varying vec3 vC; varying vec3 vW;\n"
    "void main(){\n"
    "  vec3 n = normalize(vN);\n"
    "  float d = max(dot(n, uLightDir), 0.0);\n"
    "  vec3 col = vC * (0.42 + 0.72*d);\n"
    "  float dist = length(vW - uCamPos);\n"
    "  float f = clamp((dist - uFogDist.x) / (uFogDist.y - uFogDist.x), 0.0, 1.0);\n"
    "  gl_FragColor = vec4(mix(col, uFogColor, f), 1.0);\n"
    "}\n";

static const char* VS_OBJ =
    "uniform mat4 uMVP; uniform mat4 uModel;\n"
    "attribute vec3 aPos; attribute vec3 aNrm; attribute vec2 aUV;\n"
    "varying vec3 vN; varying vec2 vUV; varying vec3 vW;\n"
    "void main(){\n"
    "  vec4 wp = uModel * vec4(aPos,1.0);\n"
    "  vW = wp.xyz;\n"
    "  vN = mat3(uModel[0].xyz, uModel[1].xyz, uModel[2].xyz) * aNrm;\n"
    "  vUV = aUV;\n"
    "  gl_Position = uMVP * wp;\n"
    "}\n";

static const char* FS_OBJ =
    "precision mediump float;\n"
    "uniform sampler2D uTex; uniform vec4 uBaseColor; uniform int uUseTex;\n"
    "uniform vec3 uLightDir; uniform vec3 uFogColor; uniform vec2 uFogDist; uniform vec3 uCamPos;\n"
    "varying vec3 vN; varying vec2 vUV; varying vec3 vW;\n"
    "void main(){\n"
    "  vec4 base = uBaseColor;\n"
    "  if (uUseTex == 1) base *= texture2D(uTex, vUV);\n"
    "  vec3 n = normalize(vN);\n"
    "  float d = max(dot(n, uLightDir), 0.0);\n"
    "  vec3 col = base.rgb * (0.50 + 0.62*d);\n"
    "  float dist = length(vW - uCamPos);\n"
    "  float f = clamp((dist - uFogDist.x) / (uFogDist.y - uFogDist.x), 0.0, 1.0);\n"
    "  gl_FragColor = vec4(mix(col, uFogColor, f), base.a);\n"
    "}\n";

static const char* VS_INST =
    "uniform mat4 uMVP; uniform mat4 uModel;\n"
    "attribute vec3 aPos; attribute vec3 aNrm; attribute vec2 aUV;\n"
    "attribute vec4 iPos; attribute float iYaw; attribute float iScale;\n"
    "varying vec3 vN; varying vec2 vUV; varying vec3 vW; varying vec3 vLocal;\n"
    "void main(){\n"
    "  float c = cos(iYaw), s = sin(iYaw);\n"
    "  vec3 p = vec3(aPos.x * c - aPos.z * s, aPos.y, aPos.x * s + aPos.z * c) * iScale;\n"
    "  vec3 n = vec3(aNrm.x * c - aNrm.z * s, aNrm.y, aNrm.x * s + aNrm.z * c);\n"
    "  vec4 wp = uModel * vec4(p + iPos.xyz, 1.0);\n"
    "  vW = wp.xyz; vN = n; vUV = aUV; vLocal = aPos;\n"
    "  gl_Position = uMVP * wp;\n"
    "}\n";

static const char* FS_INST =
    "precision mediump float;\n"
    "uniform sampler2D uTex; uniform vec4 uBaseColor; uniform int uUseTex;\n"
    "uniform vec3 uLightDir; uniform vec3 uFogColor; uniform vec2 uFogDist; uniform vec3 uCamPos;\n"
    "uniform float uWaver;\n"
    "varying vec3 vN; varying vec2 vUV; varying vec3 vW; varying vec3 vLocal;\n"
    "void main(){\n"
    "  vec4 base = uBaseColor;\n"
    "  if (uUseTex == 1) base *= texture2D(uTex, vUV);\n"
    "  if (base.a < 0.35) discard;\n"
    "  vec3 n = normalize(vN);\n"
    "  float d = max(dot(n, uLightDir), 0.0);\n"
    "  vec3 col = base.rgb * (0.52 + 0.58*d);\n"
    "  float dist = length(vW - uCamPos);\n"
    "  float f = clamp((dist - uFogDist.x) / (uFogDist.y - uFogDist.x), 0.0, 1.0);\n"
    "  gl_FragColor = vec4(mix(col, uFogColor, f), 1.0);\n"
    "}\n";

static const char* VS_UI =
    "uniform vec2 uScreen;\n"
    "attribute vec2 aPos; attribute vec4 aCol; attribute float aUVx;\n"
    "varying vec4 vCol; varying vec2 vUV;\n"
    "void main(){ vCol=aCol; vUV=vec2(aUVx,0.0);\n"
    "  vec2 p = aPos / uScreen * 2.0 - 1.0; gl_Position=vec4(p.x, -p.y, 0.0, 1.0); }\n";

static const char* FS_UI =
    "precision mediump float;\n"
    "varying vec4 vCol; varying vec2 vUV;\n"
    "void main(){ gl_FragColor = vCol; }\n";

static u32 compile(GLenumT type, const char* src) {
    u32 sh = pglCreateShader(type);
    pglShaderSource(sh, 1, &src, nullptr);
    pglCompileShader(sh);
    int ok = 0;
    pglGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        int n = 0;
        pglGetShaderInfoLog(sh, sizeof(log), &n, log);
        LOGE("compile shader gagal: %s", log);
        return 0;
    }
    return sh;
}

bool Shader::build(const char* vs, const char* fs) {
    u32 v = compile(GL_VERTEX_SHADER, vs);
    u32 f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return false;
    prog = pglCreateProgram();
    pglAttachShader(prog, v);
    pglAttachShader(prog, f);
    pglLinkProgram(prog);
    int ok = 0;
    pglGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        int n = 0;
        pglGetProgramInfoLog(prog, sizeof(log), &n, log);
        LOGE("link program gagal: %s", log);
        return false;
    }
    pglDeleteShader(v);
    pglDeleteShader(f);
    uMVP = pglGetUniformLocation(prog, "uMVP");
    uModel = pglGetUniformLocation(prog, "uModel");
    uLightDir = pglGetUniformLocation(prog, "uLightDir");
    uFogColor = pglGetUniformLocation(prog, "uFogColor");
    uFogDist = pglGetUniformLocation(prog, "uFogDist");
    uBaseColor = pglGetUniformLocation(prog, "uBaseColor");
    uUseTex = pglGetUniformLocation(prog, "uUseTex");
    uTex = pglGetUniformLocation(prog, "uTex");
    uCamPos = pglGetUniformLocation(prog, "uCamPos");
    uVertexColor = pglGetUniformLocation(prog, "uVertexColor");
    aPos = pglGetAttribLocation(prog, "aPos");
    aNrm = pglGetAttribLocation(prog, "aNrm");
    aUV = pglGetAttribLocation(prog, "aUV");
    aCol = pglGetAttribLocation(prog, "aCol");
    iPos = pglGetAttribLocation(prog, "iPos");
    iRot = pglGetAttribLocation(prog, "iYaw");
    return true;
}

void Mesh::make(const void* verts, int vcount, int vstride, const u32* idx, int icount,
                bool withColor, bool withUV, bool withInstance) {
    stride = vstride;
    count = icount;
    pglGenVertexArrays(1, &vao);
    pglBindVertexArray(vao);
    pglGenBuffers(1, &vbo);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo);
    pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptrT)((size_t)vcount * vstride), verts, GL_STATIC_DRAW);
    if (icount > 0 && idx) {
        pglGenBuffers(1, &ibo);
        pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        pglBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptrT)((size_t)icount * 4), idx, GL_STATIC_DRAW);
    }
    if (withColor) {
        // layout terrain: pos3 nrm3 col3 (36 byte)
        pglEnableVertexAttribArray(0);
        pglVertexAttribPointer(0, 3, GL_FLOAT, 0, vstride, (const void*)0);
        pglEnableVertexAttribArray(1);
        pglVertexAttribPointer(1, 3, GL_FLOAT, 0, vstride, (const void*)(12));
        pglEnableVertexAttribArray(2);
        pglVertexAttribPointer(2, 3, GL_FLOAT, 0, vstride, (const void*)(24));
    } else {
        // layout objek: pos3 nrm3 uv2 (32 byte)
        pglEnableVertexAttribArray(0);
        pglVertexAttribPointer(0, 3, GL_FLOAT, 0, vstride, (const void*)0);
        pglEnableVertexAttribArray(1);
        pglVertexAttribPointer(1, 3, GL_FLOAT, 0, vstride, (const void*)(12));
        if (withUV) {
            pglEnableVertexAttribArray(2);
            pglVertexAttribPointer(2, 2, GL_FLOAT, 0, vstride, (const void*)(24));
        }
    }
    if (withInstance) {
        // buffer instan: x,y,z,yaw,scale (5 float)
        pglGenBuffers(1, &instVbo);
        pglBindBuffer(GL_ARRAY_BUFFER, instVbo);
        pglEnableVertexAttribArray(3);
        pglVertexAttribPointer(3, 4, GL_FLOAT, 0, 20, (const void*)0);
        pglVertexAttribDivisor(3, 1);
        pglEnableVertexAttribArray(4);
        pglVertexAttribPointer(4, 1, GL_FLOAT, 0, 20, (const void*)(16));
        pglVertexAttribDivisor(4, 1);
    }
    pglBindVertexArray(0);
}

void Mesh::updateInstances(const void* data, int bytes) {
    if (!instVbo) return;
    pglBindBuffer(GL_ARRAY_BUFFER, instVbo);
    pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptrT)bytes, data, GL_DYNAMIC_DRAW);
}

void Mesh::freeMesh() {
    if (vao) pglDeleteVertexArrays(1, &vao);
    if (vbo) pglDeleteBuffers(1, &vbo);
    if (ibo) pglDeleteBuffers(1, &ibo);
    if (instVbo) pglDeleteBuffers(1, &instVbo);
    vao = vbo = ibo = instVbo = 0;
    count = 0;
}

bool Renderer::init() {
    if (!terr.build(VS_TERR, FS_TERR)) return false;
    if (!obj.build(VS_OBJ, FS_OBJ)) return false;
    if (!this->inst.build(VS_INST, FS_INST)) return false;  // shader instancing terpisah
    if (!ui.build(VS_UI, FS_UI)) return false;
    ui.uLightDir = pglGetUniformLocation(ui.prog, "uScreen");
    u8 white[4] = {255, 255, 255, 255};
    pglGenTextures(1, &whiteTex);
    pglBindTexture(GL_TEXTURE_2D, whiteTex);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    pglEnable(GL_DEPTH_TEST);
    pglDepthFunc(GL_LEQUAL);
    pglCullFace(GL_BACK);
    pglFrontFace(GL_CCW);
    uiInit();
    return true;
}

static void setCommon(Shader& s, const mat4& vp, const v3& cam, const v3& lightDir,
                      const v3& fog, f32 f0, f32 f1) {
    pglUseProgram(s.prog);
    if (s.uMVP >= 0) pglUniformMatrix4fv(s.uMVP, 1, 0, vp.m);
    if (s.uLightDir >= 0) pglUniform3f(s.uLightDir, lightDir.x, lightDir.y, lightDir.z);
    if (s.uFogColor >= 0) pglUniform3f(s.uFogColor, fog.x, fog.y, fog.z);
    if (s.uFogDist >= 0) pglUniform2f(s.uFogDist, f0, f1);
    if (s.uCamPos >= 0) pglUniform3f(s.uCamPos, cam.x, cam.y, cam.z);
}

void Renderer::drawModel(const Mesh& m, const u32 tex, const v4& color, const mat4& model,
                         bool useTex, bool vertColor, bool doubleSided) {
    Shader& s = vertColor ? terr : obj;
    setCommon(s, curVP, curCam, lightDir, fogColor, fogStart, fogEnd);
    if (s.uModel >= 0) pglUniformMatrix4fv(s.uModel, 1, 0, model.m);
    if (s.uBaseColor >= 0) pglUniform4f(s.uBaseColor, color.x, color.y, color.z, color.w);
    if (s.uUseTex >= 0) pglUniform1i(s.uUseTex, useTex ? 1 : 0);
    if (useTex && s.uTex >= 0) {
        pglActiveTexture(GL_TEXTURE0);
        pglBindTexture(GL_TEXTURE_2D, tex ? tex : whiteTex);
        pglUniform1i(s.uTex, 0);
    }
    if (doubleSided) pglDisable(GL_CULL_FACE);
    pglBindVertexArray(m.vao);
    if (m.count > 0) pglDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, (const void*)0);
    pglBindVertexArray(0);
    if (doubleSided) pglEnable(GL_CULL_FACE);
}

void Renderer::drawModelF(int indexOffset, const Mesh& m, const u32 tex, const v4& color,
                          const mat4& model, bool useTex, bool doubleSided) {
    Shader& s = obj;
    setCommon(s, curVP, curCam, lightDir, fogColor, fogStart, fogEnd);
    if (s.uModel >= 0) pglUniformMatrix4fv(s.uModel, 1, 0, model.m);
    if (s.uBaseColor >= 0) pglUniform4f(s.uBaseColor, color.x, color.y, color.z, color.w);
    if (s.uUseTex >= 0) pglUniform1i(s.uUseTex, useTex ? 1 : 0);
    if (useTex && s.uTex >= 0) {
        pglActiveTexture(GL_TEXTURE0);
        pglBindTexture(GL_TEXTURE_2D, tex ? tex : whiteTex);
        pglUniform1i(s.uTex, 0);
    }
    if (doubleSided) pglDisable(GL_CULL_FACE);
    pglBindVertexArray(m.vao);
    pglDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, (const void*)(size_t)(indexOffset * 4));
    pglBindVertexArray(0);
    if (doubleSided) pglEnable(GL_CULL_FACE);
}

void Renderer::drawInstanced(const Mesh& m, const u32 tex, const v4& color, const mat4& model,
                             bool useTex, int instances, bool doubleSided) {
    if (instances <= 0) return;
    Shader& s = inst;
    setCommon(s, curVP, curCam, lightDir, fogColor, fogStart, fogEnd);
    if (s.uModel >= 0) pglUniformMatrix4fv(s.uModel, 1, 0, model.m);
    if (s.uBaseColor >= 0) pglUniform4f(s.uBaseColor, color.x, color.y, color.z, color.w);
    if (s.uUseTex >= 0) pglUniform1i(s.uUseTex, useTex ? 1 : 0);
    if (useTex && s.uTex >= 0) {
        pglActiveTexture(GL_TEXTURE0);
        pglBindTexture(GL_TEXTURE_2D, tex ? tex : whiteTex);
        pglUniform1i(s.uTex, 0);
    }
    if (doubleSided) pglDisable(GL_CULL_FACE);
    pglBindVertexArray(m.vao);
    pglDrawElementsInstanced(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, (const void*)0, instances);
    pglBindVertexArray(0);
    if (doubleSided) pglEnable(GL_CULL_FACE);
}

// -------------------------------------------------------------------- UI 2D
struct UIVertex {
    f32 x, y;
    f32 r, g, b, a;
    f32 uvx;
};
static Array<UIVertex> g_uiv;

void Renderer::uiInit() {
    g_uiv.reserve(4096);
}

void Renderer::uiRect(f32 x, f32 y, f32 w, f32 h, const v4& c) {
    UIVertex v;
    v.r = c.x; v.g = c.y; v.b = c.z; v.a = c.w; v.uvx = 0;
    v.x = x; v.y = y; g_uiv.push(v);
    v.x = x + w; v.y = y; g_uiv.push(v);
    v.x = x + w; v.y = y + h; g_uiv.push(v);
    v.x = x; v.y = y + h; g_uiv.push(v);
}

void Renderer::uiCircle(f32 cx, f32 cy, f32 r, const v4& c, int seg) {
    UIVertex v;
    v.r = c.x; v.g = c.y; v.b = c.z; v.a = c.w; v.uvx = 0;
    for (int i = 0; i < seg; i++) {
        f32 a0 = (f32)i / seg * 2 * PI, a1 = (f32)(i + 1) / seg * 2 * PI;
        UIVertex c0 = v, c1 = v, c2 = v;
        c0.x = cx; c0.y = cy;
        c1.x = cx + cosf(a0) * r; c1.y = cy + sinf(a0) * r;
        c2.x = cx + cosf(a1) * r; c2.y = cy + sinf(a1) * r;
        g_uiv.push(c0); g_uiv.push(c1); g_uiv.push(c2);
    }
}

void Renderer::uiFlush() {
    if (g_uiv.size == 0) return;
    pglUseProgram(ui.prog);
    int uScreen = pglGetUniformLocation(ui.prog, "uScreen");
    pglUniform2f(uScreen, (f32)width, (f32)height);
    pglDisable(GL_DEPTH_TEST);
    pglEnable(GL_BLEND);
    pglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    pglDisable(GL_CULL_FACE);
    static u32 vao = 0, vbo = 0;
    if (!vao) {
        pglGenVertexArrays(1, &vao);
        pglGenBuffers(1, &vbo);
    }
    pglBindVertexArray(vao);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo);
    pglBufferData(GL_ARRAY_BUFFER, (GLsizeiptrT)((size_t)g_uiv.size * sizeof(UIVertex)),
                  g_uiv.data, GL_DYNAMIC_DRAW);
    pglEnableVertexAttribArray(0);
    pglVertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(UIVertex), (const void*)0);
    pglEnableVertexAttribArray(1);
    pglVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(UIVertex), (const void*)(8));
    pglEnableVertexAttribArray(2);
    pglVertexAttribPointer(2, 1, GL_FLOAT, 0, sizeof(UIVertex), (const void*)(24));
    pglDrawArrays(GL_TRIANGLES, 0, g_uiv.size);
    pglBindVertexArray(0);
    pglDisable(GL_BLEND);
    pglEnable(GL_DEPTH_TEST);
    pglEnable(GL_CULL_FACE);
    g_uiv.clear();
}
