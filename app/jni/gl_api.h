// gl_api.h - semua fungsi EGL/GLES3 dimuat runtime lewat dlopen/dlsym (tanpa lib di link time)
#pragma once
#include "core.h"

typedef void* EGLCtx;
typedef uint32_t GLenumT;
typedef int32_t GLintT;
typedef uint32_t GLuintT;
typedef float GLfloatT;
typedef char GLcharT;
typedef intptr_t GLsizeiptrT;
typedef int GLsizeiT;
typedef uint8_t GLbooleanT;
typedef void (*GLProc)(void);

// ---------------------------------------------------------------- EGL
typedef void* EGLDisplayT;
typedef void* EGLSurfaceT;
typedef void* EGLContextT;
typedef void* EGLConfigT;
typedef int32_t EGLintT;
typedef void* EGLNativeWindowTypeT;

#define EGL_DEFAULT_DISPLAY ((void*)0)
#define EGL_NO_CONTEXT ((void*)0)
#define EGL_NO_SURFACE ((void*)0)
#define EGL_NO_DISPLAY ((void*)0)
#define EGL_SUCCESS 0x3000
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 4
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 4
#define EGL_OPENGL_ES3_BIT 0x40
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_NONE 0x3038
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_SWAP_BEHAVIOR 0x3093
#define EGL_BUFFER_PRESERVED 0x3094

extern "C" {
extern EGLDisplayT(*peglGetDisplay)(void*);
extern EGLintT(*peglInitialize)(EGLDisplayT, EGLintT*, EGLintT*);
extern EGLintT(*peglChooseConfig)(EGLDisplayT, const EGLintT*, EGLConfigT*, EGLintT, EGLintT*);
extern EGLSurfaceT(*peglCreateWindowSurface)(EGLDisplayT, EGLConfigT, void*, const EGLintT*);
extern EGLContextT(*peglCreateContext)(EGLDisplayT, EGLConfigT, EGLContextT, const EGLintT*);
extern EGLintT(*peglMakeCurrent)(EGLDisplayT, EGLSurfaceT, EGLSurfaceT, EGLContextT);
extern EGLintT(*peglSwapBuffers)(EGLDisplayT, EGLSurfaceT);
extern EGLintT(*peglQuerySurface)(EGLDisplayT, EGLSurfaceT, EGLintT, EGLintT*);
extern EGLintT(*peglGetError)(void);
extern EGLintT(*peglTerminate)(EGLDisplayT);
extern EGLintT(*peglSwapInterval)(EGLDisplayT, EGLintT);
extern EGLintT(*peglBindAPI)(uint32_t);
}

// ---------------------------------------------------------------- GLES
#define GL_VERSION 0x1F02
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_EXTENSIONS 0x1F03
#define GL_MAX_TEXTURE_SIZE 0x0D33

#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_ALWAYS 0x0207
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_ZERO 0
#define GL_BACK 0x0405
#define GL_FRONT 0x0404
#define GL_CCW 0x0901
#define GL_CW 0x0900
#define GL_TRIANGLES 4
#define GL_TRIANGLE_STRIP 5
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_RGBA8 0x8058
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_VIEWPORT 0x0BA2

extern "C" {
extern void (*pglEnable)(GLenumT);
extern void (*pglDisable)(GLenumT);
extern void (*pglClear)(uint32_t);
extern void (*pglClearColor)(float, float, float, float);
extern void (*pglViewport)(int, int, int, int);
extern void (*pglDepthFunc)(GLenumT);
extern void (*pglDepthMask)(GLbooleanT);
extern void (*pglCullFace)(GLenumT);
extern void (*pglFrontFace)(GLenumT);
extern void (*pglBlendFunc)(GLenumT, GLenumT);
extern void (*pglScissor)(int, int, int, int);
extern void (*pglGetIntegerv)(GLenumT, int*);
extern const char* (*pglGetString)(GLenumT);
extern void (*pglPixelStorei)(GLenumT, int);
extern void (*pglPolygonOffset)(float, float);

extern void (*pglGenBuffers)(int, GLuintT*);
extern void (*pglBindBuffer)(GLenumT, GLuintT);
extern void (*pglBufferData)(GLenumT, GLsizeiptrT, const void*, GLenumT);
extern void (*pglBufferSubData)(GLenumT, GLsizeiptrT, GLsizeiptrT, const void*);
extern void (*pglDeleteBuffers)(int, const GLuintT*);
extern void (*pglGenVertexArrays)(int, GLuintT*);
extern void (*pglBindVertexArray)(GLuintT);
extern void (*pglDeleteVertexArrays)(int, const GLuintT*);
extern void (*pglEnableVertexAttribArray)(GLuintT);
extern void (*pglDisableVertexAttribArray)(GLuintT);
extern void (*pglVertexAttribPointer)(GLuintT, int, GLenumT, GLbooleanT, int, const void*);
extern void (*pglVertexAttribDivisor)(GLuintT, GLuintT);

extern GLuintT (*pglCreateShader)(GLenumT);
extern void (*pglShaderSource)(GLuintT, int, const GLcharT* const*, const int*);
extern void (*pglCompileShader)(GLuintT);
extern void (*pglGetShaderiv)(GLuintT, GLenumT, int*);
extern void (*pglGetShaderInfoLog)(GLuintT, int, int*, GLcharT*);
extern GLuintT (*pglCreateProgram)(void);
extern void (*pglAttachShader)(GLuintT, GLuintT);
extern void (*pglLinkProgram)(GLuintT);
extern void (*pglGetProgramiv)(GLuintT, GLenumT, int*);
extern void (*pglGetProgramInfoLog)(GLuintT, int, int*, GLcharT*);
extern void (*pglUseProgram)(GLuintT);
extern void (*pglDeleteShader)(GLuintT);
extern void (*pglDeleteProgram)(GLuintT);
extern int (*pglGetUniformLocation)(GLuintT, const GLcharT*);
extern int (*pglGetAttribLocation)(GLuintT, const GLcharT*);
extern void (*pglUniform1i)(int, int);
extern void (*pglUniform1f)(int, float);
extern void (*pglUniform2f)(int, float, float);
extern void (*pglUniform3f)(int, float, float, float);
extern void (*pglUniform4f)(int, float, float, float, float);
extern void (*pglUniformMatrix4fv)(int, int, GLbooleanT, const float*);

extern void (*pglGenTextures)(int, GLuintT*);
extern void (*pglBindTexture)(GLenumT, GLuintT);
extern void (*pglTexImage2D)(GLenumT, int, int, int, int, int, GLenumT, GLenumT, const void*);
extern void (*pglTexParameteri)(GLenumT, GLenumT, int);
extern void (*pglDeleteTextures)(int, const GLuintT*);
extern void (*pglGenerateMipmap)(GLenumT);
extern void (*pglActiveTexture)(GLenumT);

extern void (*pglDrawElements)(GLenumT, int, GLenumT, const void*);
extern void (*pglDrawArrays)(GLenumT, int, int);
extern void (*pglDrawElementsInstanced)(GLenumT, int, GLenumT, const void*, int);
extern void (*pglDrawArraysInstanced)(GLenumT, int, int, int);
}

// memuat semua simbol; return true bila GLES3 & EGL inti tersedia
bool glLoadAll();
