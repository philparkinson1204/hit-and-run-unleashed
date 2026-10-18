// Minimal row-vector 3D math matching Direct3D / Pure3D conventions
// (points are rows: p' = p * M, translation in the 4th row).
#pragma once
#include <math.h>
#include <string.h>

struct Vec3 {
    float x, y, z;
};

struct Mat4 {
    float m[16];   // row major, m[12..14] = translation
    float& at(int r, int c) { return m[r * 4 + c]; }
    float at(int r, int c) const { return m[r * 4 + c]; }
};

static inline Vec3 V3(float x, float y, float z) { return {x, y, z}; }
static inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static inline float Len(Vec3 a) { return sqrtf(Dot(a, a)); }
static inline Vec3 Norm(Vec3 a)
{
    float l = Len(a);
    return l > 1e-8f ? a * (1.0f / l) : V3(0, 0, 0);
}
static inline Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

static inline Mat4 Identity()
{
    Mat4 r = {};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static inline Mat4 Mul(const Mat4& a, const Mat4& b)
{
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i * 4 + j] = a.m[i * 4 + 0] * b.m[0 * 4 + j] + a.m[i * 4 + 1] * b.m[1 * 4 + j] +
                             a.m[i * 4 + 2] * b.m[2 * 4 + j] + a.m[i * 4 + 3] * b.m[3 * 4 + j];
    return r;
}

static inline Vec3 XformPoint(Vec3 p, const Mat4& m)
{
    return {p.x * m.m[0] + p.y * m.m[4] + p.z * m.m[8] + m.m[12], p.x * m.m[1] + p.y * m.m[5] + p.z * m.m[9] + m.m[13],
            p.x * m.m[2] + p.y * m.m[6] + p.z * m.m[10] + m.m[14]};
}

static inline Vec3 XformDir(Vec3 p, const Mat4& m)
{
    return {p.x * m.m[0] + p.y * m.m[4] + p.z * m.m[8], p.x * m.m[1] + p.y * m.m[5] + p.z * m.m[9],
            p.x * m.m[2] + p.y * m.m[6] + p.z * m.m[10]};
}

static inline Vec3 Row(const Mat4& m, int r) { return {m.m[r * 4], m.m[r * 4 + 1], m.m[r * 4 + 2]}; }
static inline void SetRow(Mat4& m, int r, Vec3 v, float w)
{
    m.m[r * 4] = v.x;
    m.m[r * 4 + 1] = v.y;
    m.m[r * 4 + 2] = v.z;
    m.m[r * 4 + 3] = w;
}

static inline Mat4 Translation(Vec3 t)
{
    Mat4 r = Identity();
    SetRow(r, 3, t, 1);
    return r;
}

// rotation about an arbitrary unit axis (row-vector convention)
static inline Mat4 AxisAngle(Vec3 a, float ang)
{
    float c = cosf(ang), s = sinf(ang), t = 1 - c;
    Mat4 r = Identity();
    r.at(0, 0) = t * a.x * a.x + c;
    r.at(0, 1) = t * a.x * a.y + s * a.z;
    r.at(0, 2) = t * a.x * a.z - s * a.y;
    r.at(1, 0) = t * a.x * a.y - s * a.z;
    r.at(1, 1) = t * a.y * a.y + c;
    r.at(1, 2) = t * a.y * a.z + s * a.x;
    r.at(2, 0) = t * a.x * a.z + s * a.y;
    r.at(2, 1) = t * a.y * a.z - s * a.x;
    r.at(2, 2) = t * a.z * a.z + c;
    return r;
}

// inverse of a rigid transform (rotation + translation)
static inline Mat4 RigidInverse(const Mat4& m)
{
    Mat4 r = Identity();
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.at(i, j) = m.at(j, i);
    Vec3 t = Row(m, 3);
    Vec3 it = {-(t.x * r.at(0, 0) + t.y * r.at(1, 0) + t.z * r.at(2, 0)),
               -(t.x * r.at(0, 1) + t.y * r.at(1, 1) + t.z * r.at(2, 1)),
               -(t.x * r.at(0, 2) + t.y * r.at(1, 2) + t.z * r.at(2, 2))};
    SetRow(r, 3, it, 1);
    return r;
}

// project a world point with view*proj; returns false if behind the camera
static inline bool Project(Vec3 p, const Mat4& viewProj, float w, float h, float* sx, float* sy, float* depth)
{
    float x = p.x * viewProj.m[0] + p.y * viewProj.m[4] + p.z * viewProj.m[8] + viewProj.m[12];
    float y = p.x * viewProj.m[1] + p.y * viewProj.m[5] + p.z * viewProj.m[9] + viewProj.m[13];
    float z = p.x * viewProj.m[2] + p.y * viewProj.m[6] + p.z * viewProj.m[10] + viewProj.m[14];
    float ww = p.x * viewProj.m[3] + p.y * viewProj.m[7] + p.z * viewProj.m[11] + viewProj.m[15];
    if (ww < 1e-4f) return false;
    *sx = (x / ww * 0.5f + 0.5f) * w;
    *sy = (-y / ww * 0.5f + 0.5f) * h;
    if (depth) *depth = z / ww;
    return true;
}

// general 4x4 inverse (row-vector convention irrelevant: inverse is the inverse)
static inline Mat4 FullInverse(const Mat4& a)
{
    const float* m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    Mat4 r;
    float id = fabsf(det) > 1e-12f ? 1.0f / det : 0.0f;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * id;
    return r;
}
