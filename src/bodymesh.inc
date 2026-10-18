#include <algorithm>
// His real body, for the poses to fit against (Weapons Lab). The mesh is read from the game's own files when his skeleton is
// first seen (art\chars\<name>_m.p3d: the skin's positions, joints, weights and triangles - read, nothing of it ships with the
// mod), and skinned on the CPU with the pose we build, the same linear blend of up to four joints the game draws him with. The
// shotgun then asks it where his skin is: the recoil pad seated on his chest (not floating, not sunk), the gun kept out of his belly
// and chest, his chin on the comb. (Before this the poses knew his shape only as radial profiles round the spine, which missed his
// round chest: the stock went 3 cm into it - the pose QA, tools/lab/qa.) Included in gun.cpp after face.inc.
static const int kBodyMaxV = 2048, kBodyMaxT = 3072;
static struct {
    char name[16];
    int nv, nt;
    bool ok, tried;
    uint32_t skel;
    Vec3 p[kBodyMaxV];           // bind (model space)
    uint8_t j[kBodyMaxV][4];     // joints
    float w[kBodyMaxV][4];       // weights
    uint8_t dom[kBodyMaxV];      // the heaviest joint
    bool eye[kBodyMaxV];         // eyeballs (spheres inside the head: not his skin)
    uint16_t t[kBodyMaxT][3];
    uint8_t tdom[kBodyMaxT];     // a triangle's joint (its first corner's heaviest)
    float sign;                  // +1: the triangles wind so their normals point out of him
    float armR[2];               // his upper arm's and forearm's thickness: skin from the bone's line (median, m)
    Vec3 v[kBodyMaxV];           // posed (BodySkin)
    Vec3 n[kBodyMaxT];           // posed face normals (unit, outward)
    Vec3 tc[kBodyMaxT];          // posed triangle centres
    float tr[kBodyMaxT];         // ... and radii (the nearest-skin search skips what can't be nearer)
    int builtAt;                 // the build (g_buildFrame) it was last skinned for
    // clusters of triangles (his mesh's pieces - shirt, sleeves, arms, head ... - the big ones split), contiguous in t[]: the
    // inside test sums each one's solid angle, exactly near the point, as a whole (its area's dipole) far from it
    int nc;
    int c0[256], cn[256];
    uint64_t cmask[256];         // the joints its triangles belong to
    Vec3 cc[256], cA[256];       // posed: centre, summed area vector (outward)
    float cr[256];               // posed: radius round the centre
} g_body;

// body parts by joint, as bits (BodyDist's mask)
static const uint64_t kBodyTorso = (1ull << 4) | (1ull << 13) | (1ull << 14) | (1ull << 15);
static const uint64_t kBodyHead = (1ull << 16) | (1ull << 17) | (1ull << 18);
static const uint64_t kBodyArmR = (1ull << 26) | (1ull << 27), kBodyArmL = (1ull << 19) | (1ull << 20);
static const uint64_t kBodyHandR = 0x1Full << 28, kBodyHandL = 0x1Full << 21;
static const uint64_t kBodyLegs = 0x1FEEull & ~(1ull << 4);   // (hips and legs: 1-12 but the pelvis)

static uint32_t P3dU32(const uint8_t* b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

// the skin chunks' primitive groups -> g_body (positions, joints by the group's palette, weights, triangles)
static void BodyReadGroups(const uint8_t* b, size_t off, size_t end, bool inSkin)
{
    while (off + 12 <= end) {
        uint32_t id = P3dU32(b + off), dsz = P3dU32(b + off + 4), tsz = P3dU32(b + off + 8);
        if (tsz < 12 || dsz < 12 || off + tsz > end) return;
        if (id == 0x10001) {   // a skin: its primitive groups follow its data
            BodyReadGroups(b, off + dsz, off + tsz, true);
        } else if (id == 0x10002 && inSkin) {
            size_t q = off + 16;
            int sl = b[q];
            char shader[64] = {0};
            memcpy(shader, b + q + 1, sl < 63 ? sl : 63);
            q += 1 + sl;
            uint32_t prim = P3dU32(b + q);
            const float* pos = nullptr;
            const float* wts = nullptr;
            const uint8_t* mats = nullptr;
            const uint8_t* pal = nullptr;
            const uint8_t* idx = nullptr;
            uint32_t npos = 0, nw = 0, nm = 0, npal = 0, nidx = 0;
            for (size_t o2 = off + dsz; o2 + 12 <= off + tsz;) {
                uint32_t c2 = P3dU32(b + o2), d2 = P3dU32(b + o2 + 8 - 4), t2 = P3dU32(b + o2 + 8);
                (void)d2;
                if (t2 < 12 || o2 + t2 > off + tsz) break;
                uint32_t n = P3dU32(b + o2 + 12);
                const uint8_t* data = b + o2 + 16;
                if (c2 == 0x10005) pos = (const float*)data, npos = n;
                else if (c2 == 0x1000C) wts = (const float*)data, nw = n;
                else if (c2 == 0x1000B) mats = data, nm = n;
                else if (c2 == 0x1000D) pal = data, npal = n;
                else if (c2 == 0x1000A) idx = data, nidx = n;
                o2 += t2;
            }
            if (!pos || !mats || !pal || !idx || nm != npos || g_body.nv + (int)npos > kBodyMaxV) {
                off += tsz;
                continue;
            }
            int base = g_body.nv;
            bool eyeball = strstr(shader, "eyeball") != nullptr;
            for (uint32_t i = 0; i < npos; i++) {
                int v = base + i;
                g_body.p[v] = V3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
                float w3[3] = {1, 0, 0};
                if (wts && nw == npos) w3[0] = wts[i * 3], w3[1] = wts[i * 3 + 1], w3[2] = wts[i * 3 + 2];
                float ww[4] = {w3[0], w3[1], w3[2], 1.0f - w3[0] - w3[1] - w3[2]};
                int best = 0;
                for (int k = 0; k < 4; k++) {
                    int pi = mats[i * 4 + 3 - k];   // (weight k goes with the matrix list's byte 3 - k)
                    uint32_t jj = pi < (int)npal ? P3dU32(pal + pi * 4) : 0;
                    g_body.j[v][k] = (uint8_t)(jj < 40 ? jj : 0);
                    g_body.w[v][k] = ww[k];
                    if (ww[k] > ww[best]) best = k;
                }
                g_body.dom[v] = g_body.j[v][best];
                g_body.eye[v] = eyeball;
            }
            g_body.nv += npos;
            const uint8_t* ix = idx;
            auto Add = [&](uint32_t a, uint32_t c, uint32_t d) {
                if (a == c || c == d || a == d || a >= npos || c >= npos || d >= npos || g_body.nt >= kBodyMaxT) return;
                uint16_t* t = g_body.t[g_body.nt];
                t[0] = (uint16_t)(base + a), t[1] = (uint16_t)(base + c), t[2] = (uint16_t)(base + d);
                g_body.tdom[g_body.nt++] = g_body.dom[base + a];
            };
            if (prim == 0)
                for (uint32_t i = 0; i + 2 < nidx; i += 3) Add(P3dU32(ix + i * 4), P3dU32(ix + i * 4 + 4), P3dU32(ix + i * 4 + 8));
            else if (prim == 1)
                for (uint32_t i = 0; i + 2 < nidx; i++) {
                    uint32_t a = P3dU32(ix + i * 4), c = P3dU32(ix + i * 4 + 4), d = P3dU32(ix + i * 4 + 8);
                    if (i & 1) Add(c, a, d);
                    else Add(a, c, d);
                }
        } else {
            BodyReadGroups(b, off + dsz, off + tsz, inSkin);
        }
        off += tsz;
    }
}

// his mesh for the skeleton in use (once per new skeleton); false if it couldn't be read
static bool BodyLoad()
{
    if (g_body.skel == g_invBindSkel && g_body.tried) return g_body.ok;
    const CharShape* c = CharShapeNow();
    memset(&g_body.nv, 0, sizeof(int) * 2);
    g_body.ok = false, g_body.tried = true, g_body.skel = g_invBindSkel, g_body.builtAt = -1;
    if (!c) return false;
    strncpy(g_body.name, c->name, 15);
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls..\\art\\chars\\%hs_m.p3d", g_modDir, c->name);
    FILE* f = _wfopen(path, L"rb");
    if (!f) {
        Log("Body: no %s_m.p3d (the poses fit his measured profiles instead)", c->name);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* b = (uint8_t*)malloc(sz);
    bool read = b && fread(b, 1, sz, f) == (size_t)sz;
    fclose(f);
    g_body.nv = g_body.nt = 0;
    if (read && sz > 12) BodyReadGroups(b, 12, sz, false);
    free(b);
    // the pieces: triangles that share vertices (union-find), the eyeballs left out; big pieces split in halves along their longest
    // side until each has at most 48 triangles; the triangles reordered piece by piece
    {
        static int par[kBodyMaxV], piece[kBodyMaxT], order[kBodyMaxT];
        for (int i = 0; i < g_body.nv; i++) par[i] = i;
        auto Find = [&](int x) { while (par[x] != x) x = par[x] = par[par[x]]; return x; };
        for (int i = 0; i < g_body.nt; i++)
            for (int e = 0; e < 3; e++) {
                int a = Find(g_body.t[i][e]), c = Find(g_body.t[i][(e + 1) % 3]);
                if (a != c) par[a] = c;
            }
        int n = 0;
        for (int i = 0; i < g_body.nt; i++)
            if (!g_body.eye[g_body.t[i][0]]) piece[i] = Find(g_body.t[i][0]), order[n++] = i;
        std::sort(order, order + n, [&](int x, int y) { return piece[x] < piece[y]; });
        // split recursively: [lo, hi) of order[] is one cluster or gets cut at the median along its longest extent
        g_body.nc = 0;
        struct Rng { int lo, hi; } stack[512];
        int sp = 0;
        for (int i = 0; i < n;) {
            int j = i;
            while (j < n && piece[order[j]] == piece[order[i]]) j++;
            stack[sp++] = {i, j};
            i = j;
        }
        static uint16_t tt[kBodyMaxT][3];
        static uint8_t td[kBodyMaxT];
        int out = 0;
        while (sp > 0 && g_body.nc < 256) {
            Rng r = stack[--sp];
            if (r.hi - r.lo > 48 && sp < 500) {
                Vec3 lo = {1e9f, 1e9f, 1e9f}, hi = {-1e9f, -1e9f, -1e9f};
                auto Cen = [&](int ti) { return (g_body.p[g_body.t[ti][0]] + g_body.p[g_body.t[ti][1]] + g_body.p[g_body.t[ti][2]]) * (1.0f / 3); };
                for (int q = r.lo; q < r.hi; q++) {
                    Vec3 c = Cen(order[q]);
                    lo = V3(fminf(lo.x, c.x), fminf(lo.y, c.y), fminf(lo.z, c.z)), hi = V3(fmaxf(hi.x, c.x), fmaxf(hi.y, c.y), fmaxf(hi.z, c.z));
                }
                Vec3 ext = hi - lo;
                int ax = ext.x > ext.y && ext.x > ext.z ? 0 : ext.y > ext.z ? 1 : 2;
                std::sort(order + r.lo, order + r.hi, [&](int x, int y) {
                    Vec3 a = Cen(x), b2 = Cen(y);
                    return (ax == 0 ? a.x : ax == 1 ? a.y : a.z) < (ax == 0 ? b2.x : ax == 1 ? b2.y : b2.z);
                });
                int mid = (r.lo + r.hi) / 2;
                stack[sp++] = {r.lo, mid};
                stack[sp++] = {mid, r.hi};
                continue;
            }
            int ci = g_body.nc++;
            g_body.c0[ci] = out, g_body.cn[ci] = r.hi - r.lo, g_body.cmask[ci] = 0;
            for (int q = r.lo; q < r.hi; q++) {
                int ti = order[q];
                memcpy(tt[out], g_body.t[ti], sizeof(tt[0]));
                td[out] = g_body.tdom[ti];
                g_body.cmask[ci] |= 1ull << g_body.tdom[ti];
                out++;
            }
        }
        // the eyeballs after the clusters (in no cluster: not his skin for the inside test)
        for (int i = 0; i < g_body.nt; i++)
            if (g_body.eye[g_body.t[i][0]]) memcpy(tt[out], g_body.t[i], sizeof(tt[0])), td[out++] = g_body.tdom[i];
        memcpy(g_body.t, tt, sizeof(tt[0]) * out);
        memcpy(g_body.tdom, td, out);
        g_body.nt = out;
    }
    // which way the triangles wind: out of him if the face normals point away from his middle, mostly
    Vec3 mid = {0, 0, 0};
    for (int i = 0; i < g_body.nv; i++) mid = mid + g_body.p[i];
    mid = mid * (1.0f / (g_body.nv > 0 ? g_body.nv : 1));
    float vote = 0;
    for (int i = 0; i < g_body.nt; i++) {
        Vec3 a = g_body.p[g_body.t[i][0]], bb = g_body.p[g_body.t[i][1]], cc = g_body.p[g_body.t[i][2]];
        vote += Dot(Cross(bb - a, cc - a), (a + bb + cc) * (1.0f / 3) - mid) > 0.0f ? 1.0f : -1.0f;
    }
    g_body.sign = vote >= 0.0f ? 1.0f : -1.0f;
    // the arms' thickness: each right-arm vertex's distance from its bone's line in the bind pose (the median, both bones)
    for (int bone = 0; bone < 2; bone++) {
        int j0 = 26 + bone, j1 = 27 + bone;
        Vec3 a0 = Row(FullInverse(g_invBind[j0]), 3), a1 = Row(FullInverse(g_invBind[j1]), 3), ax = Norm(a1 - a0);
        static float r[kBodyMaxV];
        int m = 0;
        for (int i = 0; i < g_body.nv; i++)
            if (g_body.dom[i] == j0) {
                Vec3 q = g_body.p[i] - a0;
                float t = Dot(q, ax);
                if (t > 0.0f && t < Len(a1 - a0)) r[m++] = Len(q - ax * t);
            }
        std::sort(r, r + m);
        g_body.armR[bone] = m ? r[m / 2] : 0.05f;
    }
    g_body.ok = g_body.nv > 300 && g_body.nt > 300;
    Log("Body: %s_m.p3d: %d vertices, %d triangles in %d pieces, arm %.1f / forearm %.1f cm thick%s", c->name, g_body.nv, g_body.nt,
        g_body.nc, g_body.armR[0] * 100, g_body.armR[1] * 100, g_body.ok ? "" : " - not used");
    return g_body.ok;
}

// ---- a grid over his posed triangles (each in the cells its bounding box covers), for the nearest-skin queries
static const float kBodyCell = 0.06f;
static const int kBodyHash = 2048, kBodyMaxRefs = 24000;
static int g_bodyHead[kBodyHash], g_bodyNext[kBodyMaxRefs], g_bodyTri[kBodyMaxRefs], g_bodyRefs;
static inline int BodyHash(int x, int y, int z) { return (int)(((unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)z * 83492791u) % kBodyHash); }

// skinned with the pose W (world joint matrices) and the game's inverse binds
static void BodySkinBody(const Mat4* W, int n);
static void BodySkin(const Mat4* W, int n)
{
    if (!g_body.ok) return;
    double t0 = ProfNow();
    BodySkinBody(W, n);
    PerfAdd(5, ProfNow() - t0);
}
static void BodySkinBody(const Mat4* W, int n)
{
    static Mat4 M[40];
    for (int j = 0; j < n && j < 40; j++) M[j] = Mul(g_invBind[j], W[j]);
    for (int i = 0; i < g_body.nv; i++) {
        Vec3 acc = {0, 0, 0};
        for (int k = 0; k < 4; k++) {
            float w = g_body.w[i][k];
            if (w == 0.0f) continue;
            int jj = g_body.j[i][k] < n ? g_body.j[i][k] : 0;
            acc = acc + XformPoint(g_body.p[i], M[jj]) * w;
        }
        g_body.v[i] = acc;
    }
    for (int i = 0; i < kBodyHash; i++) g_bodyHead[i] = -1;
    g_bodyRefs = 0;
    for (int i = 0; i < g_body.nt; i++) {
        Vec3 a = g_body.v[g_body.t[i][0]], b = g_body.v[g_body.t[i][1]], c = g_body.v[g_body.t[i][2]];
        Vec3 nn = Cross(b - a, c - a) * g_body.sign;
        float L = Len(nn);
        g_body.n[i] = L > 1e-12f ? nn * (1.0f / L) : V3(0, 1, 0);
        Vec3 tcen = (a + b + c) * (1.0f / 3);
        g_body.tc[i] = tcen;
        g_body.tr[i] = fmaxf(Len(a - tcen), fmaxf(Len(b - tcen), Len(c - tcen)));
        if (g_body.eye[g_body.t[i][0]]) continue;
        int x0 = (int)floorf(fminf(a.x, fminf(b.x, c.x)) / kBodyCell), x1 = (int)floorf(fmaxf(a.x, fmaxf(b.x, c.x)) / kBodyCell);
        int y0 = (int)floorf(fminf(a.y, fminf(b.y, c.y)) / kBodyCell), y1 = (int)floorf(fmaxf(a.y, fmaxf(b.y, c.y)) / kBodyCell);
        int z0 = (int)floorf(fminf(a.z, fminf(b.z, c.z)) / kBodyCell), z1 = (int)floorf(fmaxf(a.z, fmaxf(b.z, c.z)) / kBodyCell);
        for (int x = x0; x <= x1; x++)
            for (int y = y0; y <= y1; y++)
                for (int z = z0; z <= z1; z++) {
                    if (g_bodyRefs >= kBodyMaxRefs) continue;
                    int h = BodyHash(x, y, z);
                    g_bodyTri[g_bodyRefs] = i, g_bodyNext[g_bodyRefs] = g_bodyHead[h], g_bodyHead[h] = g_bodyRefs++;
                }
    }
    for (int ci = 0; ci < g_body.nc; ci++) {   // each piece's centre, area vector and radius as posed
        Vec3 c = {0, 0, 0}, A = {0, 0, 0};
        int n = 0;
        for (int q = g_body.c0[ci]; q < g_body.c0[ci] + g_body.cn[ci]; q++) {
            Vec3 a = g_body.v[g_body.t[q][0]], b = g_body.v[g_body.t[q][1]], cc = g_body.v[g_body.t[q][2]];
            c = c + a + b + cc, n += 3;
            A = A + Cross(b - a, cc - a) * (0.5f * g_body.sign);
        }
        c = c * (1.0f / (n ? n : 1));
        float r = 0;
        for (int q = g_body.c0[ci]; q < g_body.c0[ci] + g_body.cn[ci]; q++)
            for (int e = 0; e < 3; e++) r = fmaxf(r, Len(g_body.v[g_body.t[q][e]] - c));
        g_body.cc[ci] = c, g_body.cA[ci] = A, g_body.cr[ci] = r;
    }
    g_body.builtAt = g_buildFrame;
}

// the generalized winding number of his skin (the pieces with any joint in `mask`) round q: ~1 inside him, ~0 outside. Robust where
// his pieces overlap and leave seams (shirt over belly, sleeve over arm: no single face's side can say) - the QA's test.
static float BodyWinding(Vec3 q, uint64_t mask)
{
    float w = 0.0f;
    for (int ci = 0; ci < g_body.nc; ci++) {
        if (!(g_body.cmask[ci] & mask)) continue;
        Vec3 r = g_body.cc[ci] - q;
        float d = Len(r);
        if (d > 1.8f * g_body.cr[ci]) {   // far: the piece as one small patch (its area's dipole)
            w += Dot(g_body.cA[ci], r) / (d * d * d);
            continue;
        }
        for (int t = g_body.c0[ci]; t < g_body.c0[ci] + g_body.cn[ci]; t++) {
            Vec3 a = g_body.v[g_body.t[t][0]] - q, b = g_body.v[g_body.t[t][1]] - q, c = g_body.v[g_body.t[t][2]] - q;
            float la = Len(a), lb = Len(b), lc = Len(c);
            float num = Dot(a, Cross(b, c)) * g_body.sign;
            float den = la * lb * lc + Dot(a, b) * lc + Dot(a, c) * lb + Dot(b, c) * la;
            w += 2.0f * atan2f(num, den);
        }
    }
    return fabsf(w) / (4.0f * PI);
}

// the closest point of triangle abc to p (Ericson); *onFace: inside the face (not an edge or corner)
static Vec3 BodyClosest(Vec3 p, Vec3 a, Vec3 b, Vec3 c, bool* onFace)
{
    Vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
    *onFace = false;
    if (d1 <= 0 && d2 <= 0) return a;
    Vec3 bp = p - b;
    float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    Vec3 cp = p - c;
    float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float den = 1.0f / (va + vb + vc);
    *onFace = true;
    return a + ab * (vb * den) + ac * (vc * den);
}

// signed distance from p to his skin (the parts in `mask`), negative inside him; *out: the outward direction there (the face's
// normal, or from the skin to p off an edge). Looks `reach` metres round p; FLT_MAX when no skin is that near.
// a point's side of his skin as last worked out (a point can only change sides by passing through his skin: further than 1.5 cm
// from it, the last answer holds for 0.1 s)
struct BodySide {
    int side;    // -1 inside, +1 outside, 0 not known
    double at;   // when it was worked out (s)
    Vec3 p;      // where (a point must pass through his skin to change sides: it can't, nearer that place than its distance from the skin)
    float d;     // its distance from his skin then
};
static double BodyNow()
{
    LARGE_INTEGER q, f;
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return (double)q.QuadPart / (double)f.QuadPart;
}
// The nearest skin among the parts in `mask`, and inside or out of those parts: inside him (the winding number over all of him - his
// pieces only close up together: the shirt alone is open at the neck, the waist and the arm holes, where a masked test flipped from
// frame to frame) and not inside the parts left out.
// (maskOnly: inside means inside him and inside the parts in the mask, whatever else is there - the donut's: the right arm it leaves
// out is the game's, not drawn, hanging at his side where the donut comes up past his belly - it sits in his belly: "outside" before)
static int g_bodyLastT = -1;   // (debug: the nearest triangle of the last maskOnly query)
static float BodyDist(Vec3 p, uint64_t mask, float reach, Vec3* out = nullptr, BodySide* cache = nullptr, bool maskOnly = false)
{
    if (!g_body.ok || g_body.builtAt < 0) return 3.4e38f;
    // the nearest of his triangles: piece by piece, skipping any piece (then triangle) whose bounding sphere can't hold a nearer point
    float best = reach;
    int bestT = -1;
    Vec3 bestQ = p;
    bool bestFace = false;
    for (int ci = 0; ci < g_body.nc; ci++) {
        if (!(g_body.cmask[ci] & mask) || Len(g_body.cc[ci] - p) - g_body.cr[ci] > best) continue;
        for (int i = g_body.c0[ci]; i < g_body.c0[ci] + g_body.cn[ci]; i++) {
            if (!(mask & (1ull << g_body.tdom[i])) || Len(g_body.tc[i] - p) - g_body.tr[i] > best) continue;
            bool face;
            Vec3 q = BodyClosest(p, g_body.v[g_body.t[i][0]], g_body.v[g_body.t[i][1]], g_body.v[g_body.t[i][2]], &face);
            float d = Len(p - q);
            if (d < best - 1e-6f || (d < best + 1e-6f && face && !bestFace)) best = d, bestT = i, bestQ = q, bestFace = face;
        }
    }
    if (bestT < 0) return 3.4e38f;
    // inside or out by the nearest face's normal (off an edge or a corner, the faces through that nearest point vote)
    Vec3 dir = p - bestQ;
    float s = Dot(dir, g_body.n[bestT]);
    if (!bestFace && best > 1e-5f) {
        float vote = 0;
        for (int ci = 0; ci < g_body.nc; ci++) {
            if (!(g_body.cmask[ci] & mask) || Len(g_body.cc[ci] - bestQ) - g_body.cr[ci] > 1e-3f) continue;
            for (int i = g_body.c0[ci]; i < g_body.c0[ci] + g_body.cn[ci]; i++) {
                if (!(mask & (1ull << g_body.tdom[i])) || Len(g_body.tc[i] - bestQ) - g_body.tr[i] > 1e-3f) continue;
                bool face;
                Vec3 q = BodyClosest(p, g_body.v[g_body.t[i][0]], g_body.v[g_body.t[i][1]], g_body.v[g_body.t[i][2]], &face);
                if (Len(q - bestQ) < 1e-4f) vote += Dot(dir, g_body.n[i]) > 0.0f ? 1.0f : -1.0f;
            }
        }
        s = vote;
    }
    // (maskOnly: always the winding numbers - inside his whole body, closed, and inside the parts in the mask - at any distance: the
    // masked parts alone are open (Apu's jacket), and the nearest face's normal, an inner face, called points well clear of him
    // inside; the donut was pushed 20-29 cm a different way each frame)
    if (maskOnly) {
        // (cheap first: the nearest masked face's side; only a point it calls inside is confirmed by the whole body's winding number -
        // the two winding numbers for every point cost 55 ms a frame with the donut by his face)
        bool in = s < 0.0f && BodyWinding(p, ~0ull) > 0.5f;
        if (out) *out = best > 1e-5f ? dir * ((in ? -1.0f : 1.0f) / best) : g_body.n[bestT];
        g_bodyLastT = bestT;
        return in ? -best : best;
    }
    // (inside or out near his skin: the winding number decides, see above; a caller's cache keeps the answer while it can't change)
    if (best < 0.06f) {
        double now = cache ? BodyNow() : 0.0;
        // (kept only while the point stays nearer the place it was worked out than that place was to his skin: a gun pushed out last frame
        // starts this frame back where it was, inside - the push came and went on alternate frames)
        if (cache && cache->side && best > 0.015f && now - cache->at < 0.1 && Len(p - cache->p) < cache->d - 0.005f) s = (float)cache->side;
        else {
            // inside the parts that count: inside him (all his pieces, closed) and not inside the parts that don't (the arm that holds
            // the gun lies on his chest: a stock point in his chest by that arm was "nearest the arm" and let through)
            bool in = BodyWinding(p, ~0ull) > 0.5f && (mask == ~0ull || BodyWinding(p, ~mask) < 0.5f);
            s = in ? -1.0f : 1.0f;
            if (cache) cache->side = (int)s, cache->at = now, cache->p = p, cache->d = best;
        }
    }
    if (out) *out = best > 1e-5f ? dir * ((s >= 0 ? 1.0f : -1.0f) / best) : g_body.n[bestT];
    return s >= 0 ? best : -best;
}
