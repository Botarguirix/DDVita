// Minimal 3D math for the renderer. World space is Unity's (left-handed: x right, y up, z forward);
// the view matrix converts it to OpenGL's right-handed eye space.
#pragma once
#include <cmath>

namespace dd::gfx {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float k) const { return {x * k, y * k, z * k}; }
    Vec3& operator+=(Vec3 b) { x += b.x; y += b.y; z += b.z; return *this; }
};
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(Vec3 v) { return std::sqrt(Dot(v, v)); }
inline Vec3 Normalize(Vec3 v) { float l = Length(v); return l > 0 ? v * (1.0f / l) : v; }
inline Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// Column-major 4x4 (OpenGL layout).
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static Mat4 Perspective(float fovYDeg, float aspect, float zNear, float zFar) {
        Mat4 r;
        float f = 1.0f / std::tan(fovYDeg * 3.14159265f / 360.0f);
        for (float& v : r.m) v = 0;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1;
        r.m[14] = 2 * zFar * zNear / (zNear - zFar);
        return r;
    }
    static Mat4 Ortho(float l, float r_, float b, float t, float n, float f) {
        Mat4 r;
        r.m[0] = 2 / (r_ - l);
        r.m[5] = 2 / (t - b);
        r.m[10] = -2 / (f - n);
        r.m[12] = -(r_ + l) / (r_ - l);
        r.m[13] = -(t + b) / (t - b);
        r.m[14] = -(f + n) / (f - n);
        return r;
    }
    // Unity-space camera basis -> GL eye space (rows: right, up, -forward).
    static Mat4 View(Vec3 eye, Vec3 right, Vec3 up, Vec3 forward) {
        Mat4 r;
        r.m[0] = right.x; r.m[4] = right.y; r.m[8] = right.z;
        r.m[1] = up.x; r.m[5] = up.y; r.m[9] = up.z;
        r.m[2] = -forward.x; r.m[6] = -forward.y; r.m[10] = -forward.z;
        r.m[12] = -Dot(right, eye);
        r.m[13] = -Dot(up, eye);
        r.m[14] = Dot(forward, eye);
        return r;
    }
};

} // namespace dd::gfx
