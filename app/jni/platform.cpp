// platform.cpp - jembatan ke Android: dlopen libEGL/libGLESv3/libandroid/liblog,
// thread render sendiri, thread input sentuh, NativeActivity lifecycle, aset, waktu & log.
//
// Semua panggilan EGL/GL dilakukan di thread render (tidak boleh lintas thread).
#include "core.h"
#include "gl_api.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>

// ------------------------------------------------------------------- pointer EGL
EGLDisplayT(*peglGetDisplay)(void*);
EGLintT(*peglInitialize)(EGLDisplayT, EGLintT*, EGLintT*);
EGLintT(*peglChooseConfig)(EGLDisplayT, const EGLintT*, EGLConfigT*, EGLintT, EGLintT*);
EGLSurfaceT(*peglCreateWindowSurface)(EGLDisplayT, EGLConfigT, void*, const EGLintT*);
EGLContextT(*peglCreateContext)(EGLDisplayT, EGLConfigT, EGLContextT, const EGLintT*);
EGLintT(*peglMakeCurrent)(EGLDisplayT, EGLSurfaceT, EGLSurfaceT, EGLContextT);
EGLintT(*peglSwapBuffers)(EGLDisplayT, EGLSurfaceT);
EGLintT(*peglQuerySurface)(EGLDisplayT, EGLSurfaceT, EGLintT, EGLintT*);
EGLintT(*peglGetError)(void);
EGLintT(*peglTerminate)(EGLDisplayT);
EGLintT(*peglSwapInterval)(EGLDisplayT, EGLintT);
EGLintT(*peglBindAPI)(uint32_t);
EGLintT(*peglDestroySurface)(EGLDisplayT, EGLSurfaceT);
EGLintT(*peglDestroyContext)(EGLDisplayT, EGLContextT);

extern "C" {
void (*pglEnable)(GLenumT); void (*pglDisable)(GLenumT); void (*pglClear)(uint32_t);
void (*pglClearColor)(float, float, float, float); void (*pglViewport)(int, int, int, int);
void (*pglDepthFunc)(GLenumT); void (*pglDepthMask)(GLbooleanT); void (*pglCullFace)(GLenumT);
void (*pglFrontFace)(GLenumT); void (*pglBlendFunc)(GLenumT, GLenumT); void (*pglScissor)(int, int, int, int);
void (*pglGetIntegerv)(GLenumT, int*); const char* (*pglGetString)(GLenumT);
void (*pglPixelStorei)(GLenumT, int); void (*pglPolygonOffset)(float, float);
void (*pglGenBuffers)(int, GLuintT*); void (*pglBindBuffer)(GLenumT, GLuintT);
void (*pglBufferData)(GLenumT, GLsizeiptrT, const void*, GLenumT);
void (*pglBufferSubData)(GLenumT, GLsizeiptrT, GLsizeiptrT, const void*);
void (*pglDeleteBuffers)(int, const GLuintT*); void (*pglGenVertexArrays)(int, GLuintT*);
void (*pglBindVertexArray)(GLuintT); void (*pglDeleteVertexArrays)(int, const GLuintT*);
void (*pglEnableVertexAttribArray)(GLuintT); void (*pglDisableVertexAttribArray)(GLuintT);
void (*pglVertexAttribPointer)(GLuintT, int, GLenumT, GLbooleanT, int, const void*);
void (*pglVertexAttribDivisor)(GLuintT, GLuintT);
GLuintT (*pglCreateShader)(GLenumT);
void (*pglShaderSource)(GLuintT, int, const GLcharT* const*, const int*);
void (*pglCompileShader)(GLuintT); void (*pglGetShaderiv)(GLuintT, GLenumT, int*);
void (*pglGetShaderInfoLog)(GLuintT, int, int*, GLcharT*);
GLuintT (*pglCreateProgram)(void); void (*pglAttachShader)(GLuintT, GLuintT);
void (*pglLinkProgram)(GLuintT); void (*pglGetProgramiv)(GLuintT, GLenumT, int*);
void (*pglGetProgramInfoLog)(GLuintT, int, int*, GLcharT*); void (*pglUseProgram)(GLuintT);
void (*pglDeleteShader)(GLuintT); void (*pglDeleteProgram)(GLuintT);
int (*pglGetUniformLocation)(GLuintT, const GLcharT*); int (*pglGetAttribLocation)(GLuintT, const GLcharT*);
void (*pglUniform1i)(int, int); void (*pglUniform1f)(int, float);
void (*pglUniform2f)(int, float, float); void (*pglUniform3f)(int, float, float, float);
void (*pglUniform4f)(int, float, float, float, float);
void (*pglUniformMatrix4fv)(int, int, GLbooleanT, const float*);
void (*pglGenTextures)(int, GLuintT*); void (*pglBindTexture)(GLenumT, GLuintT);
void (*pglTexImage2D)(GLenumT, int, int, int, int, int, GLenumT, GLenumT, const void*);
void (*pglTexParameteri)(GLenumT, GLenumT, int); void (*pglDeleteTextures)(int, const GLuintT*);
void (*pglGenerateMipmap)(GLenumT); void (*pglActiveTexture)(GLenumT);
void (*pglDrawElements)(GLenumT, int, GLenumT, const void*);
void (*pglDrawArrays)(GLenumT, int, int);
void (*pglDrawElementsInstanced)(GLenumT, int, GLenumT, const void*, int);
void (*pglDrawArraysInstanced)(GLenumT, int, int, int);
}

static void* sym(void* lib, const char* name, bool critical = true) {
    void* p = lib ? dlsym(lib, name) : nullptr;
    if (!p && critical) LOGE("simbol hilang: %s", name);
    return p;
}

bool glLoadAll() {
    void* egl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    if (!egl) { LOGE("dlopen libEGL gagal"); return false; }
    void* gl = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
    if (!gl) gl = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
    if (!gl) { LOGE("dlopen libGLESv3 gagal"); return false; }
    *(void**)&peglGetDisplay = sym(egl, "eglGetDisplay");
    *(void**)&peglInitialize = sym(egl, "eglInitialize");
    *(void**)&peglChooseConfig = sym(egl, "eglChooseConfig");
    *(void**)&peglCreateWindowSurface = sym(egl, "eglCreateWindowSurface");
    *(void**)&peglCreateContext = sym(egl, "eglCreateContext");
    *(void**)&peglMakeCurrent = sym(egl, "eglMakeCurrent");
    *(void**)&peglSwapBuffers = sym(egl, "eglSwapBuffers");
    *(void**)&peglQuerySurface = sym(egl, "eglQuerySurface");
    *(void**)&peglGetError = sym(egl, "eglGetError");
    *(void**)&peglTerminate = sym(egl, "eglTerminate");
    *(void**)&peglSwapInterval = sym(egl, "eglSwapInterval", false);
    *(void**)&peglBindAPI = sym(egl, "eglBindAPI", false);
    *(void**)&peglDestroySurface = sym(egl, "eglDestroySurface", false);
    *(void**)&peglDestroyContext = sym(egl, "eglDestroyContext", false);

    static const char* names[] = {
        "glEnable", "glDisable", "glClear", "glClearColor", "glViewport", "glDepthFunc",
        "glDepthMask", "glCullFace", "glFrontFace", "glBlendFunc", "glScissor",
        "glGetIntegerv", "glGetString", "glPixelStorei", "glPolygonOffset",
        "glGenBuffers", "glBindBuffer", "glBufferData", "glBufferSubData", "glDeleteBuffers",
        "glGenVertexArrays", "glBindVertexArray", "glDeleteVertexArrays",
        "glEnableVertexAttribArray", "glDisableVertexAttribArray", "glVertexAttribPointer",
        "glVertexAttribDivisor", "glCreateShader", "glShaderSource", "glCompileShader",
        "glGetShaderiv", "glGetShaderInfoLog", "glCreateProgram", "glAttachShader",
        "glLinkProgram", "glGetProgramiv", "glGetProgramInfoLog", "glUseProgram",
        "glDeleteShader", "glDeleteProgram", "glGetUniformLocation", "glGetAttribLocation",
        "glUniform1i", "glUniform1f", "glUniform2f", "glUniform3f", "glUniform4f",
        "glUniformMatrix4fv", "glGenTextures", "glBindTexture", "glTexImage2D",
        "glTexParameteri", "glDeleteTextures", "glGenerateMipmap", "glActiveTexture",
        "glDrawElements", "glDrawArrays", "glDrawElementsInstanced", "glDrawArraysInstanced"};
    static void** dst[] = {
        (void**)&pglEnable, (void**)&pglDisable, (void**)&pglClear, (void**)&pglClearColor,
        (void**)&pglViewport, (void**)&pglDepthFunc, (void**)&pglDepthMask, (void**)&pglCullFace,
        (void**)&pglFrontFace, (void**)&pglBlendFunc, (void**)&pglScissor, (void**)&pglGetIntegerv,
        (void**)&pglGetString, (void**)&pglPixelStorei, (void**)&pglPolygonOffset,
        (void**)&pglGenBuffers, (void**)&pglBindBuffer, (void**)&pglBufferData,
        (void**)&pglBufferSubData, (void**)&pglDeleteBuffers, (void**)&pglGenVertexArrays,
        (void**)&pglBindVertexArray, (void**)&pglDeleteVertexArrays,
        (void**)&pglEnableVertexAttribArray, (void**)&pglDisableVertexAttribArray,
        (void**)&pglVertexAttribPointer, (void**)&pglVertexAttribDivisor,
        (void**)&pglCreateShader, (void**)&pglShaderSource, (void**)&pglCompileShader,
        (void**)&pglGetShaderiv, (void**)&pglGetShaderInfoLog, (void**)&pglCreateProgram,
        (void**)&pglAttachShader, (void**)&pglLinkProgram, (void**)&pglGetProgramiv,
        (void**)&pglGetProgramInfoLog, (void**)&pglUseProgram, (void**)&pglDeleteShader,
        (void**)&pglDeleteProgram, (void**)&pglGetUniformLocation, (void**)&pglGetAttribLocation,
        (void**)&pglUniform1i, (void**)&pglUniform1f, (void**)&pglUniform2f, (void**)&pglUniform3f,
        (void**)&pglUniform4f, (void**)&pglUniformMatrix4fv, (void**)&pglGenTextures,
        (void**)&pglBindTexture, (void**)&pglTexImage2D, (void**)&pglTexParameteri,
        (void**)&pglDeleteTextures, (void**)&pglGenerateMipmap, (void**)&pglActiveTexture,
        (void**)&pglDrawElements, (void**)&pglDrawArrays, (void**)&pglDrawElementsInstanced,
        (void**)&pglDrawArraysInstanced};
    int n = (int)(sizeof(names) / sizeof(names[0]));
    for (int i = 0; i < n; i++) {
        void* p = dlsym(gl, names[i]);
        if (!p) p = dlsym(egl, names[i]);
        if (!p) LOGE("GL simbol hilang: %s", names[i]);
        *dst[i] = p;
    }
    LOGI("GLES dimuat: %s | %s", pglGetString ? pglGetString(GL_VENDOR) : "?",
         pglGetString ? pglGetString(GL_RENDERER) : "?");
    return true;
}

// ------------------------------------------------------------------- log & waktu
void logmsg(int prio, const char* fmt, ...) {
    typedef int (*logfn)(int, const char*, const char*, ...);
    static logfn fn = nullptr;
    if (!fn) {
        void* l = dlopen("liblog.so", RTLD_NOW);
        if (l) *(void**)&fn = dlsym(l, "__android_log_print");
        if (!fn) return;
    }
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fn(prio, LOG_TAG, "%s", buf);
}

double nowSeconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ------------------------------------------------------------------- API game
void gameInit(ANativeActivity* act, AAssetManager* mgr);
void gameResize(int w, int h);
void gameFrame(double dt);
void gameShutdown();
void gameInput(int action, int pointerId, float x, float y);
void gameTouchCancelAll();
void gameTeardownGL();   // game.cpp: hapus VAO/VBO/tekstur sebelum konteks hilang

// ------------------------------------------------------------------- state mesin
struct Engine {
    ANativeActivity* activity = nullptr;
    AAssetManager* assets = nullptr;
    ANativeWindow* window = nullptr;      // dilindungi g_lock
    EGLDisplayT display = nullptr;
    EGLSurfaceT surface = nullptr;
    EGLContextT context = nullptr;
    EGLConfigT config = nullptr;
    int width = 0, height = 0;
    volatile bool running = false;        // thread render aktif
    volatile bool paused = false;
    volatile bool quit = false;
    volatile bool surfaceDirty = false;
    bool started = false;
    double lastTime = 0;
};
static Engine g_eng;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_renderThread;

static bool engineInitDisplay() {
    g_eng.display = peglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!g_eng.display || g_eng.display == EGL_NO_DISPLAY) { LOGE("eglGetDisplay gagal"); return false; }
    EGLintT maj = 0, mn = 0;
    if (!peglInitialize(g_eng.display, &maj, &mn)) { LOGE("eglInitialize gagal"); return false; }
    LOGI("EGL %d.%d", maj, mn);
    if (peglBindAPI) peglBindAPI(EGL_OPENGL_ES_API);
    const EGLintT cfgAttr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLintT num = 0;
    if (!peglChooseConfig(g_eng.display, cfgAttr, &g_eng.config, 1, &num) || num < 1) {
        const EGLintT cfg2[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
                                EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE};
        if (!peglChooseConfig(g_eng.display, cfg2, &g_eng.config, 1, &num) || num < 1) {
            LOGE("eglChooseConfig gagal");
            return false;
        }
    }
    return true;
}

static bool engineCreateContext(ANativeWindow* win) {
    if (!win) return false;
    g_eng.surface = peglCreateWindowSurface(g_eng.display, g_eng.config, win, nullptr);
    if (!g_eng.surface || g_eng.surface == EGL_NO_SURFACE) { LOGE("windowSurface gagal"); return false; }
    const EGLintT ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_eng.context = peglCreateContext(g_eng.display, g_eng.config, EGL_NO_CONTEXT, ctxAttr);
    if (!g_eng.context || g_eng.context == EGL_NO_CONTEXT) {
        const EGLintT ctx2[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        g_eng.context = peglCreateContext(g_eng.display, g_eng.config, EGL_NO_CONTEXT, ctx2);
    }
    if (!g_eng.context || g_eng.context == EGL_NO_CONTEXT) { LOGE("context gagal"); return false; }
    if (!peglMakeCurrent(g_eng.display, g_eng.surface, g_eng.surface, g_eng.context)) {
        LOGE("makeCurrent gagal");
        return false;
    }
    if (peglSwapInterval) peglSwapInterval(g_eng.display, 1);
    EGLintT w = 0, h = 0;
    peglQuerySurface(g_eng.display, g_eng.surface, EGL_WIDTH, &w);
    peglQuerySurface(g_eng.display, g_eng.surface, EGL_HEIGHT, &h);
    g_eng.width = w; g_eng.height = h;
    LOGI("permukaan %dx%d", w, h);
    return true;
}

static void engineTeardownGL() {
    peglMakeCurrent(g_eng.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_eng.surface && peglDestroySurface) peglDestroySurface(g_eng.display, g_eng.surface);
    if (g_eng.context && peglDestroyContext) peglDestroyContext(g_eng.display, g_eng.context);
    g_eng.surface = EGL_NO_SURFACE;
    g_eng.context = EGL_NO_CONTEXT;
}

// ------------------------------------------------------------------- thread render
static void* renderThread(void* arg) {
    LOGI("thread render mulai");
    bool inited = false;
    while (!g_eng.quit) {
        // tunggu jendela siap
        ANativeWindow* win = nullptr;
        pthread_mutex_lock(&g_lock);
        win = g_eng.window;
        pthread_mutex_unlock(&g_lock);
        if (!win) { usleep(20000); continue; }
        if (!engineCreateContext(win)) {
            usleep(50000);
            continue;
        }
        // semua panggilan GL (termasuk gameInit) HARUS di thread ini
        if (!inited) {
            gameResize(g_eng.width, g_eng.height);
            gameInit(g_eng.activity, g_eng.assets);
            inited = true;
        } else {
            gameResize(g_eng.width, g_eng.height);
        }
        g_eng.lastTime = nowSeconds();
        g_eng.running = true;
        while (!g_eng.quit) {
            ANativeWindow* w2 = nullptr;
            pthread_mutex_lock(&g_lock);
            w2 = g_eng.window;
            pthread_mutex_unlock(&g_lock);
            if (!w2) break;              // jendela hilang -> buat konteks baru
            if (g_eng.paused) {          // jangan render, tapi konteks tetap hidup
                usleep(30000);
                g_eng.lastTime = nowSeconds();
                continue;
            }
            if (g_eng.surfaceDirty) {   // jendela berubah ukuran
                g_eng.surfaceDirty = false;
                EGLintT ww = 0, hh = 0;
                peglQuerySurface(g_eng.display, g_eng.surface, EGL_WIDTH, &ww);
                peglQuerySurface(g_eng.display, g_eng.surface, EGL_HEIGHT, &hh);
                if (ww > 0 && hh > 0 && (ww != g_eng.width || hh != g_eng.height)) {
                    g_eng.width = ww; g_eng.height = hh;
                    gameResize(ww, hh);
                }
            }
            double t = nowSeconds();
            float dt = (float)(t - g_eng.lastTime);
            g_eng.lastTime = t;
            if (dt > 0.1f) dt = 0.1f;
            if (dt < 0.0f) dt = 0.0f;
            gameFrame(dt);
            if (!peglSwapBuffers(g_eng.display, g_eng.surface)) {
                LOGW("swapBuffers gagal (jendela kemungkinan hilang)");
                break;
            }
        }
        g_eng.running = false;
        if (inited) { gameTeardownGL(); inited = false; }
        engineTeardownGL();
        if (!g_eng.quit) usleep(50000);
    }
    LOGI("thread render selesai");
    return nullptr;
}

// ------------------------------------------------------------------- input
static AInputQueue* g_queue = nullptr;

static int32_t onInputEvent(ANativeActivity* act, AInputEvent* ev) {
    int type = AInputEvent_getType(ev);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int action = AMotionEvent_getAction(ev);
        int masked = action & 0xFF;
        int pidx = (action & 0xFF00) >> 8;
        int count = (int)AMotionEvent_getPointerCount(ev);
        switch (masked) {
            case 0:  // DOWN
            case 5:  // POINTER_DOWN
                gameInput(0, (int)AMotionEvent_getPointerId(ev, pidx),
                          AMotionEvent_getX(ev, pidx), AMotionEvent_getY(ev, pidx));
                break;
            case 1:  // UP
            case 6:  // POINTER_UP
                gameInput(1, (int)AMotionEvent_getPointerId(ev, pidx),
                          AMotionEvent_getX(ev, pidx), AMotionEvent_getY(ev, pidx));
                break;
            case 2:  // MOVE (laporkan semua jari)
                for (int i = 0; i < count; i++)
                    gameInput(2, (int)AMotionEvent_getPointerId(ev, i),
                              AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
                break;
            case 3:  // CANCEL
                gameTouchCancelAll();
                break;
            default:
                break;
        }
        return 1;
    }
    return 0;
}

extern "C" void* inputThread(void* arg) {
    AInputQueue* queue = (AInputQueue*)arg;
    typedef int (*loopfn)(int, int*, int*, void**);
    typedef int32_t (*getfn)(AInputQueue*, AInputEvent**);
    typedef int32_t (*prefn)(AInputQueue*, AInputEvent*);
    typedef void (*finishfn)(AInputQueue*, AInputEvent*, int);
    static loopfn pollOnce = nullptr;
    static getfn getEv = nullptr;
    static prefn pre = nullptr;
    static finishfn finish = nullptr;
    if (!pollOnce) {
        void* l = dlopen("libandroid.so", RTLD_NOW);
        *(void**)&pollOnce = dlsym(l, "ALooper_pollOnce");
        *(void**)&getEv = dlsym(l, "AInputQueue_getEvent");
        *(void**)&pre = dlsym(l, "AInputQueue_preDispatchEvent");
        *(void**)&finish = dlsym(l, "AInputQueue_finishEvent");
    }
    if (!getEv || !finish) { LOGE("input queue tidak tersedia"); return nullptr; }
    while (queue && !g_eng.quit) {
        int events = 0, outFd = 0;
        void* outData = nullptr;
        int ident = pollOnce ? pollOnce(-1, &outFd, &events, &outData) : -1;
        if (ident == -1) { usleep(5000); continue; }
        AInputEvent* ev = nullptr;
        if (getEv(queue, &ev) != 0 || !ev) continue;
        if (pre && pre(queue, ev)) { finish(queue, ev, 1); continue; }
        finish(queue, ev, onInputEvent(nullptr, ev));
    }
    return nullptr;
}

static void onInputQueueCreated(ANativeActivity* a, AInputQueue* q) {
    g_queue = q;
    pthread_t th;
    if (pthread_create(&th, nullptr, inputThread, q) == 0) {
        pthread_detach(th);
        LOGI("thread input dibuat");
    }
}
static void onInputQueueDestroyed(ANativeActivity* a, AInputQueue* q) { g_queue = nullptr; }

// ------------------------------------------------------------------- callbacks
static void onNativeWindowCreated(ANativeActivity* a, ANativeWindow* w) {
    pthread_mutex_lock(&g_lock);
    g_eng.window = w;
    pthread_mutex_unlock(&g_lock);
    g_eng.surfaceDirty = true;
    g_eng.paused = false;
    g_eng.lastTime = nowSeconds();
    LOGI("jendela dibuat (%p), aset=%p", (void*)w, (void*)g_eng.assets);
    (void)a;
}
static void onNativeWindowResized(ANativeActivity* a, ANativeWindow* w) {
    // EGL tidak boleh disentuh dari thread UI; ukuran dibaca ulang di thread render
    pthread_mutex_lock(&g_lock);
    g_eng.window = w;
    g_eng.surfaceDirty = true;
    pthread_mutex_unlock(&g_lock);
    (void)a;
}
static void onNativeWindowRedraw(ANativeActivity* a, ANativeWindow* w) {}
static void onNativeWindowDestroyed(ANativeActivity* a, ANativeWindow* w) {
    pthread_mutex_lock(&g_lock);
    g_eng.window = nullptr;
    pthread_mutex_unlock(&g_lock);
    LOGI("jendela dihancurkan");
}
static void onPause(ANativeActivity* a) { g_eng.paused = true; }
static void onResume(ANativeActivity* a) {
    g_eng.paused = false;
    g_eng.lastTime = nowSeconds();
    gameTouchCancelAll();
}
static void onDestroy(ANativeActivity* a) {
    g_eng.quit = true;
    g_eng.paused = true;
    if (g_renderThread) pthread_join(g_renderThread, nullptr);
}
static void* onSaveState(ANativeActivity* a, size_t* len) { *len = 0; return nullptr; }

extern "C" __attribute__((visibility("default"))) void ANativeActivity_onCreate(
    ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    LOGI("Nusa Rush dibuat (NativeActivity)");
    g_eng.activity = activity;
    activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
    activity->callbacks->onNativeWindowResized = onNativeWindowResized;
    activity->callbacks->onNativeWindowRedrawNeeded = onNativeWindowRedraw;
    activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
    activity->callbacks->onInputQueueCreated = onInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = onInputQueueDestroyed;
    activity->callbacks->onPause = onPause;
    activity->callbacks->onResume = onResume;
    activity->callbacks->onDestroy = onDestroy;
    activity->callbacks->onSaveInstanceState = onSaveState;

    // asset manager
    JNIEnv* env = nullptr;
    if (activity->vm && activity->vm->AttachCurrentThread((void**)&env, nullptr) == 0) {
        typedef void* (*amfn)(void*, void*);
        static amfn fromJava = nullptr;
        if (!fromJava) {
            void* libandroid = dlopen("libandroid.so", RTLD_NOW);
            if (libandroid) *(void**)&fromJava = dlsym(libandroid, "AAssetManager_fromJava");
        }
        if (fromJava && activity->assetManager)
            g_eng.assets = (AAssetManager*)fromJava((void*)env, (void*)activity->assetManager);
        activity->vm->DetachCurrentThread();
    }
    LOGI("asset manager: %p", g_eng.assets);
    // environment
    if (!getenv("NUSA_NOLOG")) {
        // pembatas log agar mudah dicari di logcat
        LOGI("=================================");
        LOGI(" NUSA RUSH - sesi 3 (arm64-v8a)");
        LOGI("=================================");
    }
    if (!glLoadAll()) { LOGE("GLES gagal dimuat"); return; }
    if (!engineInitDisplay()) return;
    g_eng.started = true;
    if (pthread_create(&g_renderThread, nullptr, renderThread, nullptr) != 0)
        LOGE("thread render gagal dibuat");
}
