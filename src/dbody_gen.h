// The drunk body's extreme skin points (kDbPoints, dbody_points.h), built from the player's own game files when the drunk
// body first loads: Homer's skin (art\chars\homer_m.p3d) and his get-up and balance clips (homer_a.p3d). Nothing of the
// game ships with the mod: v1.0's first release candidate carried this table in the DLL; this rebuilds the same table,
// value for value, from the files the player's game already has. Platform-neutral, as dbody.h.
//
// The rule (what the table always was):
//   - a joint carries the skin vertices it holds at least half of (its heaviest weight slot - the first slot on a tie);
//   - for every pose - standing (bind), hom_get_up frames 0-30, hom_balence frames 2-16 - and each of 98 directions (the
//     nonzero integer vectors in [-2, 2]^3, one per direction, in counting order), the carried vertex reaching furthest
//     (the first on a tie) is kept, unless it is the same point (to 0.1 mm), or within 1.2 cm, at bind, of one kept;
//   - a point is stored at bind in its joint's frame, with its second-heaviest joint (the first slot on a tie), that
//     joint's share of the two and the point in that joint's frame - rounded to 4 and 3 decimals, as the table was printed.
// Worked in double, as the generator the table came from was; the result is checked against that table by a fingerprint
// (Fingerprint below; drunkbody.inc logs whether it matches).
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DBGEN_MARK
#define DBGEN_MARK(what)   // (tests/dbody_gen_test.cpp times the steps)
#endif
namespace dbgen {

// ---- a 4x4 matrix in double, row vectors (p' = p M), as the game's
struct M4 { double m[4][4]; };
static inline M4 Mul(const M4& a, const M4& b)
{
    M4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0.0;
            for (int k = 0; k < 4; k++) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}
static inline bool Inv(const M4& a, M4* out)   // Gauss-Jordan, partial pivoting
{
    double w[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++) w[i][j] = j < 4 ? a.m[i][j] : (j - 4 == i ? 1.0 : 0.0);
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++)
            if (fabs(w[r][c]) > fabs(w[p][c])) p = r;
        if (!(fabs(w[p][c]) > 1e-12)) return false;
        if (p != c)
            for (int j = 0; j < 8; j++) { double t = w[c][j]; w[c][j] = w[p][j]; w[p][j] = t; }
        double d = w[c][c];
        for (int j = 0; j < 8; j++) w[c][j] /= d;
        for (int r = 0; r < 4; r++) {
            if (r == c) continue;
            double f = w[r][c];
            if (f != 0.0)
                for (int j = 0; j < 8; j++) w[r][j] -= f * w[c][j];
        }
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) out->m[i][j] = w[i][j + 4];
    return true;
}
static inline void Xform(const double p[4], const M4& M, double out[3])   // the first three of [p] M
{
    for (int j = 0; j < 3; j++) {
        double s = 0.0;
        for (int i = 0; i < 4; i++) s += p[i] * M.m[i][j];
        out[j] = s;
    }
}

// ---- quaternions (x, y, z, w), as the clips hold them
struct Qd { double v[4]; };
static inline Qd QNorm(Qd q)
{
    double n = sqrt(q.v[0] * q.v[0] + q.v[1] * q.v[1] + q.v[2] * q.v[2] + q.v[3] * q.v[3]);
    if (n < 1e-12) return Qd{{0, 0, 0, 1}};
    for (double& x : q.v) x /= n;
    return q;
}
static inline M4 QRowT(const Qd& q, const double t[3])   // its row-vector matrix, translated by t
{
    double x = q.v[0], y = q.v[1], z = q.v[2], w = q.v[3];
    M4 M = {{{1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y), 0},
             {2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x), 0},
             {2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y), 0},
             {t[0], t[1], t[2], 1}}};
    return M;
}
static inline Qd RowQ(const M4& M)   // a matrix's rotation as a quaternion (its rows normalised first)
{
    double r[3][3];
    for (int i = 0; i < 3; i++) {
        double n = sqrt(M.m[i][0] * M.m[i][0] + M.m[i][1] * M.m[i][1] + M.m[i][2] * M.m[i][2]);
        for (int j = 0; j < 3; j++) r[i][j] = M.m[i][j] / n;
    }
    double c00 = r[0][0], c01 = r[1][0], c02 = r[2][0], c10 = r[0][1], c11 = r[1][1], c12 = r[2][1], c20 = r[0][2], c21 = r[1][2],
           c22 = r[2][2];
    double tr = c00 + c11 + c22, s;
    Qd q;
    if (tr > 0) s = sqrt(tr + 1) * 2, q = Qd{{(c21 - c12) / s, (c02 - c20) / s, (c10 - c01) / s, 0.25 * s}};
    else if (c00 > c11 && c00 > c22) s = sqrt(1 + c00 - c11 - c22) * 2, q = Qd{{0.25 * s, (c01 + c10) / s, (c02 + c20) / s, (c21 - c12) / s}};
    else if (c11 > c22) s = sqrt(1 + c11 - c00 - c22) * 2, q = Qd{{(c01 + c10) / s, 0.25 * s, (c12 + c21) / s, (c02 - c20) / s}};
    else s = sqrt(1 + c22 - c00 - c11) * 2, q = Qd{{(c02 + c20) / s, (c12 + c21) / s, 0.25 * s, (c10 - c01) / s}};
    return QNorm(q);
}
static inline Qd Slerp(const Qd& a, Qd b, double t)
{
    double d = a.v[0] * b.v[0] + a.v[1] * b.v[1] + a.v[2] * b.v[2] + a.v[3] * b.v[3];
    if (d < 0) {
        for (double& x : b.v) x = -x;
        d = -d;
    }
    Qd r;
    if (d > 0.9995) {
        for (int i = 0; i < 4; i++) r.v[i] = a.v[i] + (b.v[i] - a.v[i]) * t;
        return QNorm(r);
    }
    double th = acos(d), s = sin(th), sa = sin((1 - t) * th), sb = sin(t * th);
    for (int i = 0; i < 4; i++) r.v[i] = a.v[i] * sa / s + b.v[i] * sb / s;
    return r;
}

// ---- reading the game's Pure3D files (every read checked against the buffer: a damaged file is turned down, never read past)
struct Rd {
    const uint8_t* b;
    uint64_t n;
    bool ok = true;
    bool Has(uint64_t o, uint64_t len) { if (o + len > n) ok = false; return ok; }
    uint32_t U32(uint64_t o) { uint32_t v = 0; if (Has(o, 4)) memcpy(&v, b + o, 4); return v; }
    uint16_t U16(uint64_t o) { uint16_t v = 0; if (Has(o, 2)) memcpy(&v, b + o, 2); return v; }
    int16_t S16(uint64_t o) { int16_t v = 0; if (Has(o, 2)) memcpy(&v, b + o, 2); return v; }
    float F32(uint64_t o) { float v = 0; if (Has(o, 4)) memcpy(&v, b + o, 4); return v; }
    uint8_t U8(uint64_t o) { return Has(o, 1) ? b[o] : 0; }
    // a length-prefixed string: its text (to the first NUL, at most 63 characters) and the offset after it
    uint64_t Str(uint64_t o, char* out, int cap)
    {
        int len = U8(o);
        out[0] = 0;
        if (!Has(o + 1, len)) return o + 1;
        int k = 0;
        while (k < len && k < cap - 1 && b[o + 1 + k]) out[k] = (char)b[o + 1 + k], k++;
        out[k] = 0;
        return o + 1 + len;
    }
};
struct Chunk { uint32_t id; uint64_t at, data, total; };
// the chunks in [off, end), one at a time: a size that doesn't fit ends the list
struct Chunks {
    Rd& R;
    uint64_t off, end;
    bool Next(Chunk* c)
    {
        if (off + 12 > end || !R.Has(off, 12)) return false;
        uint32_t id = R.U32(off), dsz = R.U32(off + 4), tsz = R.U32(off + 8);
        if (tsz < 12 || dsz < 12 || off + tsz > end) return false;
        *c = Chunk{id, off, dsz, tsz};
        off += tsz;
        return true;
    }
};

enum { kMaxJ = 40, kNumJ = 33, kMaxV = 16384, kMaxKeys = 4096, kMaxTracks = 64 };

// ---- Homer's skin: per vertex its bind position, four joints and four weights (as bodymesh.inc reads it)
struct Mesh {
    int nj = 0;                   // skeleton joints
    char name[kMaxJ][32];         // (their names: the head is found by name - Marge's skeleton has 36 joints)
    int parent[kMaxJ];
    M4 rest[kMaxJ];               // each joint's rest matrix (relative to its parent)
    int nv = 0;
    float* pos = nullptr;         // [nv][3]
    int* jnt = nullptr;           // [nv][4]
    float* wt = nullptr;          // [nv][4]
};
static void MeshFree(Mesh* m) { free(m->pos), free(m->jnt), free(m->wt); m->pos = nullptr, m->jnt = nullptr, m->wt = nullptr; }

static bool MeshWalk(Rd& R, Mesh* m, uint64_t off, uint64_t end, bool inSkin, int depth)
{
    if (depth > 16) return true;
    Chunks it{R, off, end};
    Chunk k;
    while (R.ok && it.Next(&k)) {
        if (k.id == 0x4501) {   // a skeleton joint: name, parent, rest matrix
            char name[64];
            uint64_t p = R.Str(k.at + 12, name, 64);
            if (m->nj >= kMaxJ) return false;
            int j = m->nj++;
            strncpy(m->name[j], name, 31), m->name[j][31] = 0;
            m->parent[j] = (int)R.U32(p);
            for (int e = 0; e < 16; e++) m->rest[j].m[e / 4][e % 4] = (double)R.F32(p + 24 + 4 * e);
        } else if (k.id == 0x10001) {   // the skin
            if (!MeshWalk(R, m, k.at + k.data, k.at + k.total, true, depth + 1)) return false;
        } else if (k.id == 0x10002 && inSkin) {   // a primitive group: positions, joint palette, weights
            uint32_t cnt[5] = {}; uint64_t at[5] = {}; bool have[5] = {};
            const uint32_t ids[5] = {0x10005, 0x1000B, 0x1000D, 0x1000A, 0x1000C};   // positions, matrix lists, palette, indices, weights
            Chunks si{R, k.at + k.data, k.at + k.total};
            Chunk s;
            while (si.Next(&s))
                for (int t = 0; t < 5; t++)
                    if (s.id == ids[t]) cnt[t] = R.U32(s.at + 12), at[t] = s.at + 16, have[t] = true;
            if (!have[0] || !have[1] || !have[2] || !have[3]) continue;
            uint32_t npos = cnt[0], npal = cnt[2];
            if (cnt[1] != npos) continue;
            if (npos > kMaxV || m->nv + (int)npos > kMaxV || npal > 4096) return false;
            if (!R.Has(at[0], 12ull * npos) || !R.Has(at[1], 4ull * npos) || !R.Has(at[2], 4ull * npal)) return false;
            bool w3 = have[4] && cnt[4] == npos && R.Has(at[4], 12ull * npos);
            for (uint32_t v = 0; v < npos; v++) {
                int o = m->nv++;
                for (int e = 0; e < 3; e++) m->pos[3 * o + e] = R.F32(at[0] + 12ull * v + 4 * e);
                float a = w3 ? R.F32(at[4] + 12ull * v) : 1.0f, b = w3 ? R.F32(at[4] + 12ull * v + 4) : 0.0f,
                      c2 = w3 ? R.F32(at[4] + 12ull * v + 8) : 0.0f;
                float w[4] = {a, b, c2, 1.0f - a - b - c2};
                for (int e = 0; e < 4; e++) {
                    uint8_t pi = R.U8(at[1] + 4ull * v + (3 - e));
                    uint32_t x = pi < npal ? R.U32(at[2] + 4ull * pi) : 0;
                    m->jnt[4 * o + e] = x < kMaxJ ? (int)x : 0;
                    m->wt[4 * o + e] = w[e];
                }
            }
        } else if (!MeshWalk(R, m, k.at + k.data, k.at + k.total, inSkin, depth + 1)) return false;
    }
    return R.ok;
}

// ---- a clip: per joint, its rotation keys (and the root joints' translation keys), sampled at a frame
struct Track {
    int j;
    int nr = 0, nt = 0;
    bool ri = true, ti = true;   // interpolated between keys
    uint16_t* rf = nullptr;      // rotation key frames
    Qd* rq = nullptr;
    uint16_t* tf = nullptr;      // translation key frames
    double (*tv)[3] = nullptr;
};
struct Clip { char name[64]; int nt = 0; Track t[kMaxTracks]; };
static void ClipFree(Clip* c)
{
    for (int i = 0; i < c->nt; i++) free(c->t[i].rf), free(c->t[i].rq), free(c->t[i].tf), free(c->t[i].tv);
    c->nt = 0;
}

static bool ReadChannel(Rd& R, Track* t, const Chunk& k)
{
    const uint32_t VEC1 = 0x121102, VEC2 = 0x121103, VEC3 = 0x121104, QUAT = 0x121105, CQUAT = 0x121111, INTERP = 0x121110;
    uint64_t q = k.at + 16;
    if (!R.Has(q, 4)) return false;
    char param[4];
    memcpy(param, R.b + q, 4);
    q += 4;
    int index = 0;
    double cst[3] = {0, 0, 0};
    if (k.id == VEC1 || k.id == VEC2) {
        index = R.U16(q), q += 2;
        for (int e = 0; e < 3; e++) cst[e] = R.F32(q + 4 * e);
        q += 12;
    }
    uint32_t n = R.U32(q);
    q += 4;
    if (!R.ok) return false;
    if (n == 0 || n > kMaxKeys) return true;
    if (!R.Has(q, 2ull * n)) return false;
    uint64_t fr = q;
    q += 2ull * n;
    bool interp = true;
    Chunks ci{R, k.at + k.data, k.at + k.total};
    Chunk c;
    while (ci.Next(&c))
        if (c.id == INTERP) interp = R.U32(c.at + 16) != 0;
    if (k.id == QUAT || k.id == CQUAT) {
        if (memcmp(param, "ROT", 3)) return true;
        if (!R.Has(q, (k.id == QUAT ? 16ull : 8ull) * n)) return false;
        uint16_t* f = (uint16_t*)malloc(2 * n);
        Qd* v = (Qd*)malloc(sizeof(Qd) * n);
        if (!f || !v) return free(f), free(v), false;
        for (uint32_t i = 0; i < n; i++) {
            f[i] = R.U16(fr + 2ull * i);
            double w, x, y, z;
            if (k.id == QUAT) w = R.F32(q + 16ull * i), x = R.F32(q + 16ull * i + 4), y = R.F32(q + 16ull * i + 8), z = R.F32(q + 16ull * i + 12);
            else w = R.S16(q + 8ull * i) / 32767.0, x = R.S16(q + 8ull * i + 2) / 32767.0, y = R.S16(q + 8ull * i + 4) / 32767.0,
                 z = R.S16(q + 8ull * i + 6) / 32767.0;
            v[i] = QNorm(Qd{{x, y, z, w}});
        }
        free(t->rf), free(t->rq);
        t->rf = f, t->rq = v, t->nr = (int)n, t->ri = interp;
        return true;
    }
    if ((k.id == VEC1 || k.id == VEC2 || k.id == VEC3) && !memcmp(param, "TRAN", 4)) {
        int dim = k.id == VEC1 ? 1 : k.id == VEC2 ? 2 : 3;
        if (!R.Has(q, 4ull * dim * n) || index < 0 || index > 2) return false;
        uint16_t* f = (uint16_t*)malloc(2 * n);
        double (*v)[3] = (double (*)[3])malloc(sizeof(double) * 3 * n);
        if (!f || !v) return free(f), free(v), false;
        for (uint32_t i = 0; i < n; i++) {
            f[i] = R.U16(fr + 2ull * i);
            double s[3];
            for (int e = 0; e < dim; e++) s[e] = R.F32(q + 4ull * dim * i + 4 * e);
            double* o = v[i];
            o[0] = cst[0], o[1] = cst[1], o[2] = cst[2];
            if (dim == 3) o[0] = s[0], o[1] = s[1], o[2] = s[2];
            else if (dim == 1) o[index] = s[0];
            else {
                int d0 = index == 0 ? 1 : 0, d1 = index == 2 ? 1 : 2;
                o[d0] = s[0], o[d1] = s[1];
            }
        }
        free(t->tf), free(t->tv);
        t->tf = f, t->tv = v, t->nt = (int)n, t->ti = interp;
    }
    return true;
}

static bool ReadClip(Rd& R, const Chunk& k, const char* const* names, Clip* out)
{
    ClipFree(out);
    uint64_t p = k.at + 16;
    R.Str(p, out->name, 64);
    Chunks gi{R, k.at + k.data, k.at + k.total};
    Chunk g;
    while (gi.Next(&g)) {
        if (g.id != 0x121002) continue;   // the group list
        Chunks ei{R, g.at + g.data, g.at + g.total};
        Chunk c;
        while (ei.Next(&c)) {
            if (c.id != 0x121001) continue;   // a joint's group
            char gname[64];
            R.Str(c.at + 16, gname, 64);
            int j = -1;
            for (int x = 0; x < kNumJ; x++)
                if (!strcmp(gname, names[x])) j = x;
            if (j < 0) continue;
            if (out->nt >= kMaxTracks) return false;
            Track* t = &out->t[out->nt++];
            *t = Track();
            t->j = j;
            Chunks hi{R, c.at + c.data, c.at + c.total};
            Chunk h;
            while (hi.Next(&h))
                if (!ReadChannel(R, t, h)) return false;
        }
    }
    return R.ok;
}

// the clips named, from an animation file (the last of a name wins, as a dictionary of them would have it)
static bool FindClips(Rd& R, uint64_t off, uint64_t end, int depth, const char* const* want, Clip* out, int nwant,
                      const char* const* names)
{
    if (depth >= 8) return true;
    Chunks it{R, off, end};
    Chunk c;
    while (R.ok && it.Next(&c)) {
        if (c.id == 0x121000) {   // a clip
            char name[64];
            R.Str(c.at + 16, name, 64);
            for (int w = 0; w < nwant; w++)
                if (!strcmp(name, want[w]) && !ReadClip(R, c, names, &out[w])) return false;
        } else if (!FindClips(R, c.at + c.data, c.at + c.total, depth + 1, want, out, nwant, names)) return false;
    }
    return R.ok;
}

template <class V>
static V SampleKeys(const uint16_t* fr, const V* vals, int n, bool interp, double f, V (*mix)(const V&, const V&, double))
{
    if (n == 1 || f <= fr[0]) return vals[0];
    if (f >= fr[n - 1]) return vals[n - 1];
    int i = 0;
    while (i + 1 < n && fr[i + 1] <= f) i++;
    if (!interp) return vals[i];
    double u = (f - fr[i]) / (double)(fr[i + 1] - fr[i]);
    return mix(vals[i], vals[i + 1], u);
}
static Qd MixQ(const Qd& a, const Qd& b, double u) { return Slerp(a, b, u); }
struct V3d { double v[3]; };
static V3d MixV(const V3d& a, const V3d& b, double u)
{
    V3d r;
    for (int e = 0; e < 3; e++) r.v[e] = a.v[e] + (b.v[e] - a.v[e]) * u;
    return r;
}

// ---- the rig: each joint's rest (relative to its parent), its translation and rest rotation
struct Rig { int n; int parent[kMaxJ]; M4 restL[kMaxJ]; double t[kMaxJ][3]; Qd bq[kMaxJ]; };

// the world matrices of a clip at a frame (the clip's root at its place in its own model space)
static bool ClipWorld(const Rig& G, const Clip& c, double f, M4* W)
{
    Qd lq[kMaxJ];
    double lt[kMaxJ][3];
    for (int j = 0; j < G.n; j++) lq[j] = G.bq[j], lt[j][0] = G.t[j][0], lt[j][1] = G.t[j][1], lt[j][2] = G.t[j][2];
    for (int i = 0; i < c.nt; i++) {
        const Track& t = c.t[i];
        if (t.j >= G.n) continue;
        if (t.nr) lq[t.j] = SampleKeys<Qd>(t.rf, t.rq, t.nr, t.ri, f, MixQ);
        if (t.nt && t.j <= 3) {
            V3d v = SampleKeys<V3d>(t.tf, (const V3d*)t.tv, t.nt, t.ti, f, MixV);
            lt[t.j][0] = v.v[0], lt[t.j][1] = v.v[1], lt[t.j][2] = v.v[2];
        }
    }
    M4 W0 = QRowT(lq[0], lt[0]);
    M4 W2 = Mul(QRowT(lq[2], lt[2]), W0);
    M4 W3 = Mul(QRowT(lq[3], lt[3]), W2);
    M4 i3, i2;
    if (!Inv(G.restL[3], &i3) || !Inv(G.restL[2], &i2)) return false;
    W[3] = QRowT(RowQ(W3), W3.m[3]);
    W[2] = Mul(i3, W[3]);
    W[0] = Mul(i2, W[2]);
    W[1] = Mul(G.restL[1], W[0]);
    for (int j = 4; j < G.n; j++) W[j] = Mul(QRowT(lq[j], G.t[j]), W[G.parent[j]]);
    return true;
}

// ---- the table
struct Out { int j; float p[3]; int j2; float w2; float p2[3]; };

static float Printed(double x, const char* fmt)   // the value as printed to the table's decimals, read back as a float
{
    char s[64];
    snprintf(s, sizeof(s), fmt, x);
    return strtof(s, nullptr);
}

// 64-bit FNV-1a over the entries (j, p, j2, w2, p2 - the bytes of each field, in order): a table's fingerprint
static uint64_t Fingerprint(const Out* t, int n)
{
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; i++) {
        uint8_t b[36];
        memcpy(b, &t[i].j, 4), memcpy(b + 4, t[i].p, 12), memcpy(b + 16, &t[i].j2, 4), memcpy(b + 20, &t[i].w2, 4), memcpy(b + 24, t[i].p2, 12);
        for (uint8_t x : b) h = (h ^ x) * 1099511628211ull;
    }
    return h;
}

// Builds the table from homer_m.p3d (mesh) and homer_a.p3d (anim) in memory. On failure: false, and why. names: the 33
// joint names (dbody.h's), the clips' groups matched by them.
static bool BuildPoints(const uint8_t* meshB, size_t meshN, const uint8_t* animB, size_t animN, const char* const* names,
                        Out* out, int cap, int* nOut, char* why, size_t whyN)
{
    *nOut = 0;
    bool ok = false;
    Mesh m;
    Clip* clips = (Clip*)calloc(2, sizeof(Clip));
    double* Pb = nullptr;    // [nv][3] each vertex at bind, in the joint's frame being built (per joint)
    double* V = nullptr;     // [poses][nv][3] skinned vertices
    int* car = nullptr;
    int* kept = nullptr;
    int* dom = nullptr;      // [nv] each vertex's heaviest joint, and that joint's weight
    double* domW = nullptr;
    double* Pj = nullptr;    // [nv][3] the carried vertices in the joint's frame, this pose
    double* Pr = nullptr;    // [nv][3] Pb rounded to 0.1 mm (the same point, as the table prints it)
    uint8_t* seen = nullptr; // [nv] judged already for this joint (kept, or close to one kept: the answer can't change)
    const int nPose = 1 + 31 + 15;
    M4* Wp = (M4*)malloc(sizeof(M4) * nPose * kMaxJ);   // each pose's world matrices
    m.pos = (float*)malloc(sizeof(float) * 3 * kMaxV), m.jnt = (int*)malloc(sizeof(int) * 4 * kMaxV), m.wt = (float*)malloc(sizeof(float) * 4 * kMaxV);
#define DBGEN_FAIL(...) do { snprintf(why, whyN, __VA_ARGS__); goto done; } while (0)
    {
        if (!clips || !Wp || !m.pos || !m.jnt || !m.wt) DBGEN_FAIL("out of memory");
        Rd RM{meshB, meshN};
        if (meshN < 12 || !MeshWalk(RM, &m, 12, meshN, false, 0) || !RM.ok) DBGEN_FAIL("homer_m.p3d didn't read");
        if (m.nj != kNumJ) DBGEN_FAIL("homer_m.p3d: %d joints, not %d", m.nj, kNumJ);
        if (m.nv < 100) DBGEN_FAIL("homer_m.p3d: only %d skin vertices", m.nv);
        for (int j = 0; j < m.nj; j++)
            if (m.parent[j] < 0 || m.parent[j] > j) DBGEN_FAIL("homer_m.p3d: joint %d's parent %d", j, m.parent[j]);
        for (int i = 0; i < m.nv * 3; i++)
            if (!isfinite(m.pos[i]) || fabsf(m.pos[i]) > 10.0f) DBGEN_FAIL("homer_m.p3d: a skin vertex out of range");
        DBGEN_MARK("mesh read");
        // bind: each joint in the model (its rest times its parent's)
        M4 BW[kMaxJ], IB[kMaxJ];
        for (int j = 0; j < m.nj; j++) BW[j] = (j == 0 || m.parent[j] == j) ? m.rest[j] : Mul(m.rest[j], BW[m.parent[j]]);
        for (int j = 0; j < m.nj; j++)
            if (!Inv(BW[j], &IB[j])) DBGEN_FAIL("homer_m.p3d: joint %d's bind doesn't invert", j);
        Rig G;
        G.n = m.nj;
        for (int j = 0; j < m.nj; j++) {
            int p = m.parent[j];
            G.parent[j] = p;
            if (j == 0 || p == j) G.restL[j] = BW[j];
            else {
                M4 ip;
                if (!Inv(BW[p], &ip)) DBGEN_FAIL("homer_m.p3d: joint %d doesn't invert", p);
                G.restL[j] = Mul(BW[j], ip);
            }
            for (int e = 0; e < 3; e++) G.t[j][e] = G.restL[j].m[3][e];
            G.bq[j] = RowQ(G.restL[j]);
        }
        // the clips
        Rd RA{animB, animN};
        const char* want[2] = {"hom_get_up", "hom_balence"};
        if (animN < 12 || !FindClips(RA, 12, animN, 0, want, clips, 2, names)) DBGEN_FAIL("homer_a.p3d didn't read");
        if (!clips[0].nt || !clips[1].nt) DBGEN_FAIL("homer_a.p3d: no hom_get_up or hom_balence");
        DBGEN_MARK("clips read");
        // the poses: bind, then hom_get_up 0..30, hom_balence 2..16
        int np = 0;
        for (int f = 0; f <= 30; f++)
            if (!ClipWorld(G, clips[0], f, Wp + kMaxJ * (1 + np++))) DBGEN_FAIL("hom_get_up frame %d", f);
        for (int f = 2; f <= 16; f++)
            if (!ClipWorld(G, clips[1], f, Wp + kMaxJ * (1 + np++))) DBGEN_FAIL("hom_balence frame %d", f);
        DBGEN_MARK("poses");
        // each posed vertex: its four joints' (inverse bind x pose), weighted
        const int nv = m.nv;
        V = (double*)malloc(sizeof(double) * 3 * (size_t)nv * nPose);
        Pb = (double*)malloc(sizeof(double) * 3 * (size_t)nv);
        car = (int*)malloc(sizeof(int) * nv);
        kept = (int*)malloc(sizeof(int) * nv);
        dom = (int*)malloc(sizeof(int) * nv);
        domW = (double*)malloc(sizeof(double) * nv);
        Pj = (double*)malloc(sizeof(double) * 3 * (size_t)nv);
        Pr = (double*)malloc(sizeof(double) * 3 * (size_t)nv);
        seen = (uint8_t*)malloc(nv);
        if (!V || !Pb || !car || !kept || !dom || !domW || !Pj || !Pr || !seen) DBGEN_FAIL("out of memory");
        for (int ps = 1; ps < nPose; ps++) {
            M4 S[kMaxJ];
            for (int j = 0; j < m.nj; j++) S[j] = Mul(IB[j], Wp[kMaxJ * ps + j]);
            for (int i = 0; i < nv; i++) {
                double h[4] = {m.pos[3 * i], m.pos[3 * i + 1], m.pos[3 * i + 2], 1.0}, acc[3] = {0, 0, 0};
                for (int k = 0; k < 4; k++) {
                    int jj = m.jnt[4 * i + k];
                    if (jj >= m.nj) jj = 0;
                    double t[3];
                    Xform(h, S[jj], t);
                    double w = m.wt[4 * i + k];
                    for (int e = 0; e < 3; e++) acc[e] += w * t[e];
                }
                for (int e = 0; e < 3; e++) V[((size_t)ps * nv + i) * 3 + e] = acc[e];
            }
        }
        DBGEN_MARK("skinned");
        // per vertex: its weight per joint (slot order), its heaviest joint (the first slot on a tie)
        for (int i = 0; i < nv; i++) {
            int a = 0;
            for (int k = 1; k < 4; k++)
                if (m.wt[4 * i + k] > m.wt[4 * i + a]) a = k;
            dom[i] = m.jnt[4 * i + a];
            double s = 0.0;
            for (int k = 0; k < 4; k++)
                if (m.jnt[4 * i + k] == dom[i]) s += (double)m.wt[4 * i + k];
            domW[i] = s;
        }
        // the 98 directions
        double D[124][3];
        int nd = 0;
        for (int x = -2; x <= 2; x++)
            for (int y = -2; y <= 2; y++)
                for (int z = -2; z <= 2; z++) {
                    if (!x && !y && !z) continue;
                    double l = sqrt((double)(x * x + y * y + z * z)), u[3] = {x / l, y / l, z / l};
                    bool dup = false;
                    for (int e = 0; e < nd && !dup; e++) dup = u[0] * D[e][0] + u[1] * D[e][1] + u[2] * D[e][2] > 1 - 1e-9;
                    if (!dup) D[nd][0] = u[0], D[nd][1] = u[1], D[nd][2] = u[2], nd++;
                }
        if (nd != 98) DBGEN_FAIL("%d directions", nd);
        double Dx[124], Dy[124], Dz[124];
        for (int d = 0; d < nd; d++) Dx[d] = D[d][0], Dy[d] = D[d][1], Dz[d] = D[d][2];
        const double R = 0.012;
        int n = 0;
        for (int j = 4; j < kNumJ; j++) {
            int nc = 0;
            for (int i = 0; i < nv; i++)
                if (dom[i] == j && domW[i] >= 0.5 - 1e-6) car[nc++] = i;
            if (!nc) continue;
            for (int i = 0; i < nv; i++) {
                double h[4] = {m.pos[3 * i], m.pos[3 * i + 1], m.pos[3 * i + 2], 1.0};
                Xform(h, IB[j], Pb + 3 * i);
                for (int e = 0; e < 3; e++) Pr[3 * i + e] = rint(Pb[3 * i + e] * 1e4) / 1e4;
            }
            int nk = 0;
            memset(seen, 0, nv);
            for (int ps = 0; ps < nPose; ps++) {
                M4 iw;
                if (ps && !Inv(Wp[kMaxJ * ps + j], &iw)) DBGEN_FAIL("pose %d: joint %d doesn't invert", ps, j);
                for (int c = 0; c < nc; c++) {   // (each carried vertex in the joint's frame this pose: at bind, Pb)
                    int i = car[c];
                    if (!ps) Pj[3 * c] = Pb[3 * i], Pj[3 * c + 1] = Pb[3 * i + 1], Pj[3 * c + 2] = Pb[3 * i + 2];
                    else {
                        const double* v = V + ((size_t)ps * nv + i) * 3;
                        double h[4] = {v[0], v[1], v[2], 1.0};
                        Xform(h, iw, Pj + 3 * c);
                    }
                }
                // per direction, the carried vertex reaching furthest (the first on a tie): all directions at once, a vertex at a time
                int bi[124];
                double bv[124];
                for (int d = 0; d < nd; d++) bi[d] = -1, bv[d] = 0;
                for (int c = 0; c < nc; c++) {
                    double x = Pj[3 * c], y = Pj[3 * c + 1], z = Pj[3 * c + 2];
                    for (int d = 0; d < nd; d++) {
                        double s = x * Dx[d] + y * Dy[d] + z * Dz[d];
                        if (bi[d] < 0 || s > bv[d]) bi[d] = c, bv[d] = s;
                    }
                }
                for (int d = 0; d < nd; d++) {
                    int best = car[bi[d]];
                    if (seen[best]) continue;
                    seen[best] = 1;
                    const double* pk = Pb + 3 * best;
                    const double* rk = Pr + 3 * best;
                    bool skip = false;   // (the same point as one kept, to 0.1 mm; or within R of one)
                    for (int e = 0; e < nk && !skip; e++) {
                        const double* re = Pr + 3 * kept[e];
                        const double* pe = Pb + 3 * kept[e];
                        double dx = pe[0] - pk[0], dy = pe[1] - pk[1], dz = pe[2] - pk[2], d2 = dx * dx + dy * dy + dz * dz;
                        skip = (rk[0] == re[0] && rk[1] == re[1] && rk[2] == re[2]) || d2 < R * R * 0.998 ||
                               (d2 < R * R * 1.002 && sqrt(d2) < R);   // (sqrt only right at the edge: the same answer as sqrt(d2) < R)
                    }
                    if (!skip) kept[nk++] = best;
                }
            }
            for (int e = 0; e < nk; e++) {
                int k = kept[e];
                // its joints in slot order, weights summed; the heaviest other than j (the first on a tie)
                int sj[4], ns = 0;
                double sw[4];
                for (int a = 0; a < 4; a++) {
                    int x = m.jnt[4 * k + a], s = 0;
                    while (s < ns && sj[s] != x) s++;
                    if (s == ns) sj[ns] = x, sw[ns] = 0.0, ns++;
                    sw[s] += (double)m.wt[4 * k + a];
                }
                int j2 = j, b = -1;
                double w2 = 0.0, wj = 0.0;
                for (int s = 0; s < ns; s++)
                    if (sj[s] == j) wj = sw[s];
                for (int s = 0; s < ns; s++)
                    if (sw[s] > 0 && sj[s] != j && (b < 0 || sw[s] > sw[b])) b = s;
                if (b >= 0) j2 = sj[b], w2 = sw[b] / (wj + sw[b]);
                if (j2 >= m.nj) DBGEN_FAIL("vertex %d: joint %d", k, j2);
                if (n >= cap) DBGEN_FAIL("more than %d points", cap);
                double h[4] = {m.pos[3 * k], m.pos[3 * k + 1], m.pos[3 * k + 2], 1.0}, p2[3];
                Xform(h, IB[j2], p2);
                Out& o = out[n++];
                o.j = j, o.j2 = j2, o.w2 = Printed(w2, "%.3f");
                for (int c = 0; c < 3; c++) o.p[c] = Printed(Pb[3 * k + c], "%.4f"), o.p2[c] = Printed(p2[c], "%.4f");
            }
        }
        DBGEN_MARK("selected");
        // a last look: every point a real place on a man-sized body
        for (int i = 0; i < n; i++)
            for (int c = 0; c < 3; c++)
                if (!isfinite(out[i].p[c]) || !isfinite(out[i].p2[c]) || fabsf(out[i].p[c]) > 2.0f || fabsf(out[i].p2[c]) > 2.0f ||
                    !(out[i].w2 >= 0.0f && out[i].w2 <= 1.0f))
                    DBGEN_FAIL("point %d out of range", i);
        if (n < 50) DBGEN_FAIL("only %d points", n);
        *nOut = n;
        ok = true;
    }
done:
#undef DBGEN_FAIL
    if (clips) ClipFree(&clips[0]), ClipFree(&clips[1]);
    free(clips), free(Wp), free(V), free(Pb), free(car), free(kept), free(dom), free(domW), free(Pj), free(Pr), free(seen);
    MeshFree(&m);
    return ok;
}

// (v1.0 RC3) A model's jaw points (charshape.inc's CharShape.jawVert): its vertices by number, at bind in its Head joint's
// frame, rounded to 4 decimals as the table printed them - the shotgun's chin rests on the gun by them. The numbers are trusted
// only in a model of the expected size whose Head joint is there; otherwise false, and why (no chin points for that model).
static bool JawPoints(const uint8_t* meshB, size_t meshN, const int* verts, int n, int expectVerts, float (*out)[3], char* why, size_t whyN)
{
    bool ok = false;
    Mesh m;
    m.pos = (float*)malloc(sizeof(float) * 3 * kMaxV), m.jnt = (int*)malloc(sizeof(int) * 4 * kMaxV), m.wt = (float*)malloc(sizeof(float) * 4 * kMaxV);
    do {
        if (!m.pos || !m.jnt || !m.wt) { snprintf(why, whyN, "out of memory"); break; }
        Rd R{meshB, meshN};
        if (meshN < 12 || !MeshWalk(R, &m, 12, meshN, false, 0) || !R.ok) { snprintf(why, whyN, "the model didn't read"); break; }
        if (m.nv != expectVerts) { snprintf(why, whyN, "%d vertices, not %d - another model?", m.nv, expectVerts); break; }
        int head = -1;
        for (int j = 0; j < m.nj; j++)
            if (!strcmp(m.name[j], "Head")) head = j;
        if (head < 0) { snprintf(why, whyN, "no Head joint"); break; }
        bool bad = false;
        for (int j = 0; j < m.nj && !bad; j++) bad = m.parent[j] < 0 || m.parent[j] > j;
        if (bad) { snprintf(why, whyN, "a joint's parent out of order"); break; }
        M4 BW[kMaxJ], IH;
        for (int j = 0; j < m.nj; j++) BW[j] = (j == 0 || m.parent[j] == j) ? m.rest[j] : Mul(m.rest[j], BW[m.parent[j]]);
        if (!Inv(BW[head], &IH)) { snprintf(why, whyN, "the Head's bind doesn't invert"); break; }
        int i = 0;
        for (; i < n; i++) {
            int v = verts[i];
            if (v < 0 || v >= m.nv) break;
            double h[4] = {m.pos[3 * v], m.pos[3 * v + 1], m.pos[3 * v + 2], 1.0}, p[3];
            Xform(h, IH, p);
            for (int c = 0; c < 3; c++) out[i][c] = Printed(p[c], "%.4f");
            if (!isfinite(out[i][0]) || !isfinite(out[i][1]) || !isfinite(out[i][2]) || fabsf(out[i][0]) > 1.0f || fabsf(out[i][1]) > 1.0f ||
                fabsf(out[i][2]) > 1.0f) break;
        }
        if (i < n) { snprintf(why, whyN, "vertex %d out of range", verts[i]); break; }
        ok = true;
    } while (0);
    MeshFree(&m);
    return ok;
}

// the jaw points' fingerprint (64-bit FNV-1a over each model's count and points, in kCharShapes' order)
static uint64_t JawFingerprint(const float (*pts)[8][3], const int* n, int models)
{
    uint64_t h = 1469598103934665603ull;
    for (int c = 0; c < models; c++) {
        uint8_t b[4];
        memcpy(b, &n[c], 4);
        for (uint8_t x : b) h = (h ^ x) * 1099511628211ull;
        for (int i = 0; i < n[c]; i++) {
            uint8_t q[12];
            memcpy(q, pts[c][i], 12);
            for (uint8_t x : q) h = (h ^ x) * 1099511628211ull;
        }
    }
    return h;
}

}   // namespace dbgen
