// core.h - dasar-dasar Nusa Rush: mat, vec, array, util. Tanpa STL/exception (freestanding).
#pragma once
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

typedef int8_t i8; typedef uint8_t u8; typedef int16_t i16; typedef uint16_t u16;
typedef int32_t i32; typedef uint32_t u32; typedef int64_t i64; typedef uint64_t u64;
typedef float f32; typedef double f64;

#define PI 3.14159265358979f
#define DEG2RAD (PI / 180.0f)
#define RAD2DEG (180.0f / PI)

template <class T> inline T vmin(T a, T b) { return a < b ? a : b; }
template <class T> inline T vmax(T a, T b) { return a > b ? a : b; }
template <class T> inline T vclamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> inline T vlerp(T a, T b, f32 t) { return a + (b - a) * t; }
template <class T> inline T vabs(T v) { return v < 0 ? -v : v; }
inline f32 signf(f32 v) { return v < 0 ? -1.f : 1.f; }

// ------------------------------------------------------------------ vektor
struct v2 {
    f32 x, y;
    v2() {} v2(f32 a, f32 b) : x(a), y(b) {}
    v2 operator+(const v2& o) const { return v2(x + o.x, y + o.y); }
    v2 operator-(const v2& o) const { return v2(x - o.x, y - o.y); }
    v2 operator*(f32 s) const { return v2(x * s, y * s); }
    f32 len() const { return sqrtf(x * x + y * y); }
};
struct v3 {
    f32 x, y, z;
    v3() {} v3(f32 a) : x(a), y(a), z(a) {} v3(f32 a, f32 b, f32 c) : x(a), y(b), z(c) {}
    v3 operator+(const v3& o) const { return v3(x + o.x, y + o.y, z + o.z); }
    v3 operator-(const v3& o) const { return v3(x - o.x, y - o.y, z - o.z); }
    v3 operator-() const { return v3(-x, -y, -z); }
    v3 operator*(f32 s) const { return v3(x * s, y * s, z * s); }
    v3 operator/(f32 s) const { return v3(x / s, y / s, z / s); }
    v3& operator+=(const v3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    v3& operator-=(const v3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    v3& operator*=(f32 s) { x *= s; y *= s; z *= s; return *this; }
    f32 len() const { return sqrtf(x * x + y * y + z * z); }
    f32 len2() const { return x * x + y * y + z * z; }
};
inline f32 dot(const v3& a, const v3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline v3 cross(const v3& a, const v3& b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline v3 normalize(const v3& a) { f32 l = a.len(); return l > 1e-8f ? a / l : v3(0, 0, 0); }
inline v3 vlerp(const v3& a, const v3& b, f32 t) { return a + (b - a) * t; }
struct v4 {
    f32 x, y, z, w;
    v4() {} v4(f32 a, f32 b, f32 c, f32 d) : x(a), y(b), z(c), w(d) {}
    v4(const v3& v, f32 d) : x(v.x), y(v.y), z(v.z), w(d) {}
};

// ------------------------------------------------------------------ matriks (column-major)
struct mat4 {
    f32 m[16];
    mat4() { identity(); }
    void identity() { memset(m, 0, sizeof(m)); m[0] = m[5] = m[10] = m[15] = 1; }
    static mat4 translate(const v3& t) { mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
    static mat4 scale(const v3& s) { mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
    static mat4 rotY(f32 a) {
        mat4 r; f32 c = cosf(a), s = sinf(a);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r;
    }
    static mat4 rotX(f32 a) {
        mat4 r; f32 c = cosf(a), s = sinf(a);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r;
    }
    mat4 operator*(const mat4& o) const {
        mat4 r;
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++) {
                f32 s = 0;
                for (int k = 0; k < 4; k++) s += m[k * 4 + row] * o.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
    v3 mulPoint(const v3& p) const {
        return v3(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                  m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                  m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
    }
    static mat4 perspective(f32 fovy, f32 aspect, f32 zn, f32 zf) {
        mat4 r; memset(r.m, 0, sizeof(r.m));
        f32 f = 1.0f / tanf(fovy * 0.5f);
        r.m[0] = f / aspect; r.m[5] = f;
        r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1.0f;
        r.m[14] = (2.0f * zf * zn) / (zn - zf);
        return r;
    }
    static mat4 lookAt(const v3& eye, const v3& center, const v3& up) {
        v3 f = normalize(center - eye);
        v3 s = normalize(cross(f, up));
        v3 u = cross(s, f);
        mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
        return r;
    }
    mat4 T() const {
        mat4 r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) r.m[i * 4 + j] = m[j * 4 + i];
        return r;
    }
};

// frustum: 6 bidang (n.xyz, d) dari matriks view-projection
struct Frustum {
    v4 p[6];
    void fromMatrix(const mat4& M) {
        // baris matriks (row-major view): m[col*4+row]
        f32 r0[4], r1[4], r2[4], r3[4];
        for (int i = 0; i < 4; i++) {
            r0[i] = M.m[i * 4 + 0]; r1[i] = M.m[i * 4 + 1];
            r2[i] = M.m[i * 4 + 2]; r3[i] = M.m[i * 4 + 3];
        }
        p[0] = v4(r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]);  // kiri
        p[1] = v4(r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]);  // kanan
        p[2] = v4(r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]);  // bawah
        p[3] = v4(r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]);  // atas
        p[4] = v4(r3[0] + r2[0], r3[1] + r2[1], r3[2] + r2[2], r3[3] + r2[3]);  // near
        p[5] = v4(r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]);  // far
        for (int i = 0; i < 6; i++) {
            f32 l = sqrtf(p[i].x * p[i].x + p[i].y * p[i].y + p[i].z * p[i].z);
            if (l > 1e-9f) { p[i].x /= l; p[i].y /= l; p[i].z /= l; p[i].w /= l; }
        }
    }
    bool sphereIn(const v3& c, f32 r) const {
        for (int i = 0; i < 6; i++) {
            if (p[i].x * c.x + p[i].y * c.y + p[i].z * c.z + p[i].w < -r) return false;
        }
        return true;
    }
};

// ------------------------------------------------------------------ array dinamis
template <class T> struct Array {
    T* data = nullptr;
    int size = 0;
    int cap = 0;
    void reserve(int n) {
        if (n <= cap) return;
        int nc = cap ? cap : 8;
        while (nc < n) nc *= 2;
        T* nd = (T*)realloc(data, (size_t)nc * sizeof(T));
        if (!nd) return;
        data = nd; cap = nc;
    }
    void push(const T& v) { reserve(size + 1); if (size < cap) data[size++] = v; }
    T& operator[](int i) { return data[i]; }
    const T& operator[](int i) const { return data[i]; }
    void clear() { size = 0; }
    void freeMem() { if (data) ::free(data); data = nullptr; size = cap = 0; }
};

struct Str {
    char* s = nullptr;
    int len = 0;
    void set(const char* a, int n) {
        if (s) ::free(s);
        s = (char*)malloc((size_t)n + 1); memcpy(s, a, n); s[n] = 0; len = n;
    }
};

// ------------------------------------------------------------------ random
struct Rng {
    u32 state = 0x12345678u;
    u32 next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    f32 f() { return (next() & 0xFFFFFF) / (f32)0x1000000; }
    f32 range(f32 a, f32 b) { return a + (b - a) * f(); }
};

// ------------------------------------------------------------------ hash peta sederhana
inline u32 hash2(i32 x, i32 y) {
    u32 h = (u32)(x * 374761393) + (u32)(y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
// noise nilai 2D halus (untuk detail visual kecil-kecilan)
f32 valueNoise2(f32 x, f32 y);

// ------------------------------------------------------------------ log & waktu (platform.cpp)
#define LOG_TAG "NusaRush"
void logmsg(int prio, const char* fmt, ...);
#define LOGI(...) logmsg(4, __VA_ARGS__)   // ANDROID_LOG_INFO
#define LOGW(...) logmsg(5, __VA_ARGS__)   // WARN
#define LOGE(...) logmsg(6, __VA_ARGS__)   // ERROR
double nowSeconds();
