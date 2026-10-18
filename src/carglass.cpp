// Cars: their glass told from their paint, and broken. SHAR draws each car's glass as a piece of its
// own with a small see-through texture (the minivan's is a light blue gradient at alpha 145 all
// over; most cars' "Windsheild.bmp" is alpha 97), so:
//  - While the game draws the world, each draw that belongs to an active car is kept as one of that
//    car's pieces (its buffers, texture and where it was drawn). The body sits on the car's position;
//    the doors, hood and trunk are parts of their own, drawn from their hinges but turned the car's
//    way (a car has up to 32 pieces: the Krusty truck's windshield is its 31st). A piece whose
//    texture is see-through all over is glass - door windows too, five triangles each.
//  - A bullet into a car is traced against those pieces' triangles (read once per piece from the
//    game's own vertex and index buffers, in the car's frame): the first one along the shot is what
//    it hit - glass or paint - and exactly where, as drawn (the car's collision box is rougher).
//  - A bullet in the glass leaves a crack; the third one, or a rocket going off next to the car,
//    breaks every window: that car's glass pieces are left out of the drawing from then on (that car
//    only - another of the same model shares the buffers, not the draw) and fly off as shards, with
//    the game's own breaking-glass sound.
//  - (drive-by M5) One side window can go alone: the glass triangles inside its box in the car's frame
//    (facing out of that side, between its sill and top, its rear and front). A door window that is a
//    piece of its own (the Canyonero's) is left out; one that is part of a bigger glass piece (the
//    minivan's: every window is one piece) has that piece drawn from an index list without them. Shots
//    go through the hole from then on. Both stay broken until the car is repaired (its hit points go
//    up), taken back or moved by the game, or wrecked - when the car's record goes.
#include "render.h"
#include "math3d.h"
#include "hit.h"

static const int kShatterHits = 3;   // bullets in a car's glass before it all goes

struct Piece {
    uintptr_t vb, ib, tex;
    UINT stride, base, mi, nv, si, pc;
    D3DPRIMITIVETYPE type;
    int glass;   // 1 glass, 0 not, -1 not looked at yet
    bool alphaTest;   // (the blob shadow under a car is see-through too, but cut out with alpha testing)
    Mat4 model;       // where it was drawn last (a door's own place)
    DWORD vs;         // (diagnostic) the vertex shader it was drawn with
    int palette;      // (diagnostic) bone palette uploads before its draw
};
static const int kPieces = 48;
// (M5) a window broken out of one of the car's glass pieces
struct Cut {
    int piece;
    uint8_t* gone;                 // one per triangle of the piece's mesh: 1 broken
    int ngone, ntri;
    IDirect3DIndexBuffer8* ib;     // the rest of the piece as a list, made when it's next drawn (none while it's all gone)
};
static const int kCuts = 6;
struct Car {
    uint32_t veh;
    Mat4 model;        // the car as last drawn
    int drawnFrame;
    Piece pieces[kPieces];
    int npieces;
    int glassHits;
    bool shattered;
    Vec3 lastPos;
    Cut cuts[kCuts];
    int ncuts;
    float hp;          // Vehicle+0x4A0: up again is a repair
    int type;          // Vehicle+0xA4 (a traffic car he takes becomes his: its hit points are set anew, not a repair)
};
static Car g_cars[32];
static int g_ncars;
static int g_frame;   // the scene's frame
int g_glassLog;       // debug "glasslog <n>"

// ------------------------------------------------------------------ the active cars, once a frame
static struct { uint32_t v; Vec3 p, ax[3]; } g_active[30];
static int g_nactive, g_activeFrame = -1;
// (M5) and the characters: one sitting in a car is drawn on his own position, turned the car's way - not a
// piece of it (his skin's triangles, read as they are in the buffer, are him standing arms out: one arm
// would stick out of a window as paint a shot could hit)
static float g_charPos[64][3];
static int g_nchars;

static void FreeCuts(Car& c)
{
    for (int i = 0; i < c.ncuts; i++) {
        free(c.cuts[i].gone);
        if (c.cuts[i].ib) c.cuts[i].ib->Release();
    }
    c.ncuts = 0;
}

static const Cut* CutOf(const Car& c, int piece)
{
    for (int i = 0; i < c.ncuts; i++)
        if (c.cuts[i].piece == piece) return &c.cuts[i];
    return nullptr;
}

static void RefreshActive(int frame)
{
    g_activeFrame = frame;
    uint32_t ptrs[30];
    float pos[30][3];
    g_nactive = Game_ListVehiclePtrs(ptrs, pos, 30);
    g_nchars = 0;
    if (g_nactive) {   // the player (CharacterManager+192, the first), then everyone else shown
        uint32_t cm = Game_CharacterManager(), pl = 0;
        SIZE_T got = 0;
        if (cm && ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)(cm + 192), &pl, 4, &got) && pl &&
            ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)(pl + 0x64), g_charPos[0], 12, &got))
            g_nchars = 1;
        g_nchars += Game_ListCharacters(g_charPos + g_nchars, 64 - g_nchars);
    }
    for (int i = 0; i < g_nactive; i++) {
        g_active[i].v = ptrs[i];
        g_active[i].p = V3(pos[i][0], pos[i][1], pos[i][2]);
        const float* m = (const float*)(uintptr_t)(ptrs[i] + 184);   // its transform: rows 0-2 its axes
        for (int r = 0; r < 3; r++) g_active[i].ax[r] = Norm(V3(m[r * 4], m[r * 4 + 1], m[r * 4 + 2]));
    }
    // cars that left (wrecked into a husk, taken back by the traffic) or were reused somewhere else
    for (int i = 0; i < g_ncars;) {
        Car& c = g_cars[i];
        int k = -1;
        for (int j = 0; j < g_nactive && k < 0; j++)
            if (g_active[j].v == c.veh) k = j;
        if (k < 0 || Len(g_active[k].p - c.lastPos) > 12.0f) {
            if (g_glassLog > 0 && (c.ncuts || c.shattered)) Log("glass: %08X %s: its glass is whole again", c.veh, k < 0 ? "gone" : "moved");
            FreeCuts(c);
            g_cars[i] = g_cars[--g_ncars];
            continue;
        }
        c.lastPos = g_active[k].p;
        float hp = *(const float*)(uintptr_t)(c.veh + 0x4A0);
        int type = *(const int*)(uintptr_t)(c.veh + 0xA4);
        if (type != c.type) c.type = type;
        else if (hp > c.hp + 0.001f && (c.ncuts || c.shattered || c.glassHits)) {   // repaired: new glass
            if (g_glassLog > 0) Log("glass: %08X repaired (%.2f -> %.2f): its glass is whole again", c.veh, c.hp, hp);
            FreeCuts(c);
            c.shattered = false;
            c.glassHits = 0;
            Decal_RemoveOnVehicle(c.veh, DECAL_GLASS);
        }
        c.hp = hp;
        i++;
    }
}

static Car* CarFor(uint32_t veh, bool create)
{
    for (int i = 0; i < g_ncars; i++)
        if (g_cars[i].veh == veh) return &g_cars[i];
    if (!create || g_ncars >= 32) return nullptr;
    Car& c = g_cars[g_ncars++];
    memset(&c, 0, sizeof(c));
    c.veh = veh;
    c.drawnFrame = -100;
    for (int j = 0; j < g_nactive; j++)
        if (g_active[j].v == veh) c.lastPos = g_active[j].p;
    c.hp = *(const float*)(uintptr_t)(veh + 0x4A0);
    c.type = *(const int*)(uintptr_t)(veh + 0xA4);
    return &c;
}

// ------------------------------------------------------------------ glass textures
static struct { uintptr_t tex; bool glass; } g_texKind[64];
static int g_ntexKind;

// The windows' texture: see-through all over (every texel's alpha 30-250: every car's windshield
// texture is one shade, 77-190) - or see-through in one shade with clear holes (the Scorpion's).
// Not with a solid texel: the traffic cars' body textures are 223-255 (their alpha isn't used).
static bool IsGlassTexture(uintptr_t texp)
{
    for (int i = 0; i < g_ntexKind; i++)
        if (g_texKind[i].tex == texp) return g_texKind[i].glass;
    bool glass = false;
    IDirect3DBaseTexture8* bt = (IDirect3DBaseTexture8*)texp;
    if (bt && bt->GetType() == D3DRTYPE_TEXTURE) {
        IDirect3DTexture8* t = (IDirect3DTexture8*)bt;
        D3DSURFACE_DESC sd;
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(t->GetLevelDesc(0, &sd)) && sd.Format == D3DFMT_A8R8G8B8 && sd.Pool != D3DPOOL_DEFAULT &&
            sd.Width <= 512 && sd.Height <= 512 && SUCCEEDED(t->LockRect(0, &lr, nullptr, D3DLOCK_READONLY))) {
            int lo = 255, hi = 0, clear = 0, shade = -1;
            bool oneShade = true;
            for (UINT y = 0; y < sd.Height; y++) {
                const BYTE* row = (const BYTE*)lr.pBits + y * lr.Pitch;
                for (UINT x = 0; x < sd.Width; x++) {
                    int a = row[x * 4 + 3];
                    if (a < 30) {
                        clear++;
                        continue;
                    }
                    if (a < lo) lo = a;
                    if (a > hi) hi = a;
                    if (shade < 0) shade = a;
                    else if (abs(a - shade) > 2) oneShade = false;
                }
            }
            t->UnlockRect(0);
            UINT n = sd.Width * sd.Height;
            glass = shade >= 0 && hi <= 250 && (clear == 0 || (oneShade && clear * 10 <= n * 7));
        }
    }
    if (g_ntexKind < 64) g_texKind[g_ntexKind++] = {texp, glass};
    return glass;
}

// ------------------------------------------------------------------ while the game draws
struct Mesh;
static const Mesh* MeshOf(const Piece& p);
static IDirect3DIndexBuffer8* CutIB(IDirect3DDevice8* d, Cut& c, const Mesh* m, UINT* n);

int CarGlass_OnDraw(int frame, IDirect3DDevice8* d, const float* world16, const float* view16, uintptr_t vb, UINT stride,
                    uintptr_t ib, UINT base, D3DPRIMITIVETYPE type, UINT mi, UINT nv, UINT si, UINT pc,
                    IDirect3DIndexBuffer8** cutIB, UINT* cutN, DWORD vs, int paletteUploads)
{
    g_frame = frame;
    static Mat4 s_viewInv;
    if (frame != g_activeFrame) {
        RefreshActive(frame);
        Mat4 v;
        memcpy(v.m, view16, 64);
        s_viewInv = RigidInverse(v);   // (once a frame: the view is the frame's)
    }
    if (!g_nactive || !vb || !ib || !stride) return 0;
    // the model's position first (the WORLD the game sets is model x view): most draws aren't cars
    const float* wm = world16;
    const float* vi = s_viewInv.m;
    Vec3 o = V3(wm[12] * vi[0] + wm[13] * vi[4] + wm[14] * vi[8] + vi[12], wm[12] * vi[1] + wm[13] * vi[5] + wm[14] * vi[9] + vi[13],
                wm[12] * vi[2] + wm[13] * vi[6] + wm[14] * vi[10] + vi[14]);
    // the car it is part of: drawn on its position, or near it (a door, the hood) and turned its way
    uint32_t veh = 0;
    bool body = false;
    float bestD = 4.0f;
    Mat4 m;
    bool haveM = false;
    for (int i = 0; i < g_nactive; i++) {
        Vec3 dd = g_active[i].p - o;
        float d = Len(dd);
        if (d >= bestD) continue;
        if (d < 0.05f) {
            veh = g_active[i].v, body = true;
            break;
        }
        if (!haveM) {
            Mat4 w;
            memcpy(w.m, world16, 64);
            m = Mul(w, s_viewInv);
            haveM = true;
        }
        bool turned = true;
        for (int r = 0; r < 3 && turned; r++) turned = Dot(Norm(Row(m, r)), g_active[i].ax[r]) > 0.995f;
        if (turned) veh = g_active[i].v, bestD = d;
    }
    if (!veh || paletteUploads > 0) return 0;   // (a skinned mesh: someone's body)
    for (int i = 0; i < g_nchars; i++) {
        float dx = g_charPos[i][0] - o.x, dy = g_charPos[i][1] - o.y, dz = g_charPos[i][2] - o.z;
        if (dx * dx + dy * dy + dz * dz < 0.05f * 0.05f) return 0;   // someone in it
    }
    Car* car = CarFor(veh, true);
    if (!car) return 0;
    if (!haveM) {
        Mat4 w;
        memcpy(w.m, world16, 64);
        m = Mul(w, s_viewInv);
    }
    if (body) car->model = m;
    car->drawnFrame = frame;
    Piece* p = nullptr;
    for (int i = 0; i < car->npieces && !p; i++) {
        Piece& q = car->pieces[i];
        if (q.vb == vb && q.ib == ib && q.si == si && q.pc == pc && q.base == base) p = &q;
    }
    if (!p) {
        if (car->npieces >= kPieces) return 0;
        p = &car->pieces[car->npieces++];
        IDirect3DBaseTexture8* tex = nullptr;
        d->GetTexture(0, &tex);
        DWORD at = 0;
        d->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
        *p = {vb, ib, (uintptr_t)tex, stride, base, mi, nv, si, pc, type, -1, at != 0, m};
        if (tex) tex->Release();   // (kept as a name: the car's model holds it)
    }
    p->model = m;
    p->vs = vs, p->palette = paletteUploads;
    // windows: a see-through texture (some cars draw theirs cut out - the glass truck's - so that's no sign)
    if (p->glass < 0) p->glass = p->tex && IsGlassTexture(p->tex) ? 1 : 0;
    if (p->glass != 1) return 0;
    if (car->shattered) return 1;   // its windows are broken: not drawn
    Cut* c = (Cut*)CutOf(*car, (int)(p - car->pieces));
    if (!c) return 0;
    if (c->ngone >= c->ntri) return 1;   // the piece was that window alone
    *cutIB = CutIB(d, *c, MeshOf(*p), cutN);
    return *cutIB ? 2 : 0;   // (no list made: drawn whole rather than not at all)
}

// ------------------------------------------------------------------ the pieces' triangles
struct Mesh {
    uintptr_t vb, ib;
    UINT stride, base, si, pc, vbSize, ibSize;
    Vec3* tri;   // 3 corners each, in the car's frame
    int ntri;
    UINT* idx;   // (M5) their indices as the index buffer has them, 3 each, turned the way they're drawn (a strip's odd ones swapped)
    UINT isz;    // 2 or 4 bytes an index
};
static const int kMeshes = 256;   // (M5: 64 filled up in a level's busy street, and the cars after had no shape)
static Mesh g_mesh[kMeshes];
static int g_nmesh;

static void FreeMeshes()
{
    for (int i = 0; i < g_nmesh; i++) free(g_mesh[i].tri), free(g_mesh[i].idx);
    g_nmesh = 0;
}

static const Mesh* MeshOf(const Piece& p)
{
    IDirect3DVertexBuffer8* vb = (IDirect3DVertexBuffer8*)p.vb;
    IDirect3DIndexBuffer8* ib = (IDirect3DIndexBuffer8*)p.ib;
    D3DVERTEXBUFFER_DESC vd;
    D3DINDEXBUFFER_DESC id;
    if (FAILED(vb->GetDesc(&vd)) || FAILED(ib->GetDesc(&id))) return nullptr;
    int slot = g_nmesh;
    for (int i = 0; i < g_nmesh; i++) {
        Mesh& m = g_mesh[i];
        if (m.vb == p.vb && m.ib == p.ib && m.stride == p.stride && m.base == p.base && m.si == p.si && m.pc == p.pc) {
            if (m.vbSize == vd.Size && m.ibSize == id.Size) return &m;
            slot = i;   // the buffers were reused for another model: read again
            break;
        }
    }
    if (slot >= kMeshes) {
        static bool s_said;
        if (!s_said) Log("glass: %d car meshes kept: no room for more this level", kMeshes), s_said = true;
        return nullptr;
    }
    if (vd.Pool == D3DPOOL_DEFAULT || id.Pool == D3DPOOL_DEFAULT || p.pc == 0 || p.pc > 8000 ||
        (p.type != D3DPT_TRIANGLELIST && p.type != D3DPT_TRIANGLESTRIP) || p.stride < 12)
        return nullptr;
    UINT isz = id.Format == D3DFMT_INDEX32 ? 4 : 2, nidx = p.type == D3DPT_TRIANGLELIST ? p.pc * 3 : p.pc + 2;
    if ((p.si + nidx) * isz > id.Size) return nullptr;
    BYTE *vdata = nullptr, *idata = nullptr;
    if (FAILED(vb->Lock(0, 0, &vdata, D3DLOCK_READONLY))) return nullptr;
    if (FAILED(ib->Lock(0, 0, &idata, D3DLOCK_READONLY))) {
        vb->Unlock();
        return nullptr;
    }
    UINT nvert = vd.Size / p.stride;
    Vec3* tri = (Vec3*)malloc(sizeof(Vec3) * 3 * p.pc);
    UINT* tidx = (UINT*)malloc(sizeof(UINT) * 3 * p.pc);
    int n = 0;
    bool sane = tri != nullptr && tidx != nullptr;
    for (UINT k = 0; sane && k < p.pc; k++) {
        UINT a = p.type == D3DPT_TRIANGLELIST ? 3 * k : k;
        UINT ix[3];
        for (int j = 0; j < 3; j++) {
            UINT at = p.si + a + j;
            ix[j] = (isz == 4 ? ((const uint32_t*)idata)[at] : ((const uint16_t*)idata)[at]) + p.base;
        }
        if (ix[0] >= nvert || ix[1] >= nvert || ix[2] >= nvert) {
            sane = false;
            break;
        }
        if (ix[0] == ix[1] || ix[1] == ix[2] || ix[0] == ix[2]) continue;   // (a strip's joins)
        bool odd = p.type == D3DPT_TRIANGLESTRIP && (k & 1);
        for (int j = 0; j < 3; j++) {
            Vec3 v = *(const Vec3*)(vdata + ix[j] * p.stride);   // (the position comes first in every vertex)
            if (!(fabsf(v.x) < 30.0f && fabsf(v.y) < 30.0f && fabsf(v.z) < 30.0f)) sane = false;
            tri[3 * n + j] = v;
            tidx[3 * n + (odd && j < 2 ? 1 - j : j)] = ix[j] - p.base;
        }
        n++;
    }
    ib->Unlock();
    vb->Unlock();
    if (!sane || !n) {
        free(tri);
        free(tidx);
        return nullptr;
    }
    Mesh& m = g_mesh[slot];
    if (slot < g_nmesh) free(m.tri), free(m.idx);
    else g_nmesh++;
    m = {p.vb, p.ib, p.stride, p.base, p.si, p.pc, vd.Size, id.Size, tri, n, tidx, isz};
    if (g_glassLog > 0) Log("glass: piece %08X/%08X: %d triangles%s", (unsigned)p.vb, (unsigned)p.ib, n, p.glass == 1 ? " (glass)" : "");
    return &m;
}

// Möller-Trumbore, both faces
static bool RayTri(Vec3 o, Vec3 d, const Vec3* t, float* dist, Vec3* n)
{
    Vec3 e1 = t[1] - t[0], e2 = t[2] - t[0], pv = Cross(d, e2);
    float det = Dot(e1, pv);
    if (fabsf(det) < 1e-9f) return false;
    float inv = 1.0f / det;
    Vec3 tv = o - t[0];
    float u = Dot(tv, pv) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    Vec3 qv = Cross(tv, e1);
    float v = Dot(d, qv) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    *dist = Dot(e2, qv) * inv;
    *n = Cross(e1, e2);
    return true;
}

bool CarGlass_Raycast(uint32_t veh, Vec3 from, Vec3 dir, float maxT, CarHit* out)
{
    Car* car = CarFor(veh, false);
    if (!car || car->drawnFrame < g_frame - 2) return false;   // (not drawn just now)
    float best = maxT;
    bool glass = false, any = false;
    Vec3 bestN = {0, 1, 0};
    const Piece* bestP = nullptr;
    for (int i = 0; i < car->npieces; i++) {
        const Piece& p = car->pieces[i];
        if (car->shattered && p.glass == 1) continue;   // (the windows are open)
        const Cut* cut = p.glass == 1 ? CutOf(*car, i) : nullptr;   // (one window out of it)
        const Mesh* m = MeshOf(p);
        if (!m) continue;
        Mat4 inv = FullInverse(p.model);   // (the piece's own frame: the ray's t stays in metres, the frame is rigid)
        Vec3 o = XformPoint(from, inv), d = XformDir(dir, inv);
        for (int k = 0; k < m->ntri; k++) {
            if (cut && k < cut->ntri && cut->gone[k]) continue;
            float t;
            Vec3 n;
            if (RayTri(o, d, m->tri + 3 * k, &t, &n) && t > 0.02f && t < best) {
                best = t;
                bestN = n;
                bestP = &p;
                glass = p.glass == 1;
                any = true;
            }
        }
    }
    if (!any) return false;
    out->pos = from + dir * best;
    Vec3 n = Norm(XformDir(bestN, bestP->model));
    if (Dot(n, dir) > 0.0f) n = n * -1.0f;   // facing the shot
    out->normal = n;
    out->glass = glass;
    if (g_glassLog > 0) {
        g_glassLog--;
        Log("glass: shot into %08X hits its %s at %.2f m", veh, glass ? "glass" : "paint", best);
    }
    return true;
}

// ------------------------------------------------------------------ breaking it
struct Shard { Vec3 p, v, axis; float t, life, size, ang, spin, ground; };
static Shard g_shards[120];
static int g_nshards;
static uint32_t g_rng = 0x9E3779B9u;

static float Rnd()
{
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)((g_rng >> 8) & 0xFFFF) / 65535.0f;
}

static void PlayAt(int snd, Vec3 p, float vol, float pitch)
{
    float d = 15.0f;
    float view[16], proj[16];
    if (Game_GetCamera(view, proj)) {
        Mat4 v;
        memcpy(v.m, view, 64);
        d = Len(p - Row(RigidInverse(v), 3));
    }
    Sound_Play(snd, Clamp01f(1.15f - d / 60.0f) * vol, pitch);
}

void CarGlass_Shatter(uint32_t veh, Vec3 from)
{
    Car* car = CarFor(veh, false);
    if (!car || car->shattered) return;
    int glassPieces = 0;
    for (int i = 0; i < car->npieces; i++) glassPieces += car->pieces[i].glass == 1;
    if (!glassPieces) return;   // (no glass seen on it: a convertible with its top down, a husk...)
    car->shattered = true;
    Decal_RemoveOnVehicle(veh, DECAL_GLASS);   // the cracks go with the glass
    Vec3 centre = Row(car->model, 3);
    float gy;
    if (!Trace_GroundY(centre + V3(0, 1, 0), &gy)) gy = centre.y - 0.6f;
    int made = 0;
    for (int tries = 0; tries < 400 && made < 70 && g_nshards < 120; tries++) {
        const Piece& p = car->pieces[(int)(Rnd() * car->npieces) % car->npieces];
        if (p.glass != 1) continue;
        const Mesh* m = MeshOf(p);
        if (!m || !m->ntri) continue;
        const Vec3* t = m->tri + 3 * ((int)(Rnd() * m->ntri) % m->ntri);
        float a = Rnd(), b = Rnd();
        if (a + b > 1.0f) a = 1.0f - a, b = 1.0f - b;
        Vec3 local = t[0] + (t[1] - t[0]) * a + (t[2] - t[0]) * b;
        Vec3 pos = XformPoint(local, p.model);
        Vec3 out = Norm(pos - centre);
        Vec3 away = Norm(pos - from);
        Shard& s = g_shards[g_nshards++];
        s.p = pos;
        s.v = out * (0.8f + Rnd() * 1.8f) + away * (0.6f + Rnd() * 1.6f) + V3(Rnd() - 0.5f, 0.6f + Rnd() * 1.4f, Rnd() - 0.5f);
        s.axis = Norm(V3(Rnd() - 0.5f, Rnd() - 0.5f, Rnd() - 0.5f) + V3(0, 0.01f, 0));
        s.t = 0;
        s.life = 1.4f + Rnd() * 1.0f;
        s.size = 0.025f + Rnd() * 0.045f;
        s.ang = Rnd() * 6.2831853f;
        s.spin = (Rnd() - 0.5f) * 24.0f;
        s.ground = gy + 0.01f;
        made++;
    }
    PlayAt(Rnd() < 0.5f ? SND_GLASS1 : SND_GLASS2, centre, 1.0f, 0.95f + Rnd() * 0.1f);
    if (g_glassLog > 0) Log("glass: %08X's windows broken (%d shards)", veh, made);
}

bool CarGlass_Knows(uint32_t veh)
{
    Car* car = CarFor(veh, false);
    if (!car || car->drawnFrame < g_frame - 2 || !car->npieces) return false;
    for (int i = 0; i < car->npieces; i++)
        if (!MeshOf(car->pieces[i])) return false;
    return true;
}

// (M5) one window's glass: car glass breaks into crumbs, most of them thrown out of the window by the blow (from
// inside, at `from`), a few bigger pieces; they leave with the car's own speed and fall, bounce and skitter on the
// road. The sound is the game's own window smash (the Krusty Glass shop's: lrg_glass_09 / 11, pitch 0.9-1.1).
static void WindowShards(Car& car, const Vec3 (*tri)[3], int ntri, Vec3 out, Vec3 from)
{
    if (!ntri) return;
    float area[24], total = 0.0f;
    Vec3 mid = {0, 0, 0};
    for (int i = 0; i < ntri; i++) {
        area[i] = Len(Cross(tri[i][1] - tri[i][0], tri[i][2] - tri[i][0])) * 0.5f;
        total += area[i];
        mid = mid + (tri[i][0] + tri[i][1] + tri[i][2]) * (area[i] / 3.0f);
    }
    if (total <= 1e-6f) return;
    mid = mid * (1.0f / total);
    Vec3 vel = {0, 0, 0};   // the car's (SimState +0x50)
    uint32_t sim = 0;
    SIZE_T got = 0;
    if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)(car.veh + 1192), &sim, 4, &got) && sim)
        ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)(sim + 0x50), &vel, 12, &got);
    if (!(Len(vel) < 60.0f)) vel = V3(0, 0, 0);
    float gy;
    if (!Trace_GroundY(mid + V3(0, 0.5f, 0), &gy)) gy = mid.y - 1.3f;
    int want = 34 + (int)(total * 40.0f), made = 0;   // (a door window is about 0.3 m2)
    for (int k = 0; k < want && g_nshards < 120; k++) {
        float r = Rnd() * total;
        int i = 0;
        while (i < ntri - 1 && r > area[i]) r -= area[i++];
        float a = Rnd(), b = Rnd();
        if (a + b > 1.0f) a = 1.0f - a, b = 1.0f - b;
        Vec3 pos = tri[i][0] + (tri[i][1] - tri[i][0]) * a + (tri[i][2] - tri[i][0]) * b;
        Vec3 away = Norm(pos - from);
        bool big = k % 7 == 0;
        float push = Clamp01f(1.2f - Len(pos - from) * 1.5f);   // (hardest where it was hit)
        Shard& s = g_shards[g_nshards++];
        s.p = pos + out * 0.01f;
        s.v = vel * (0.92f + Rnd() * 0.08f) + out * (0.5f + Rnd() * 1.3f + push * 1.6f) + away * (0.3f + Rnd() * 0.8f) +
              V3((Rnd() - 0.5f) * 0.6f, Rnd() * 0.9f - 0.2f, (Rnd() - 0.5f) * 0.6f);
        s.axis = Norm(V3(Rnd() - 0.5f, Rnd() - 0.5f, Rnd() - 0.5f) + V3(0, 0.01f, 0));
        s.t = 0;
        s.life = 1.6f + Rnd() * 1.2f;
        s.size = big ? 0.035f + Rnd() * 0.03f : 0.012f + Rnd() * 0.02f;
        s.ang = Rnd() * 6.2831853f;
        s.spin = (Rnd() - 0.5f) * 30.0f;
        s.ground = gy + 0.01f;
        made++;
    }
    PlayAt(Rnd() < 0.5f ? SND_GLASS1 : SND_GLASS2, mid, 0.96f, 0.9f + Rnd() * 0.2f);
    if (g_glassLog > 0) Log("glass: %08X's window: %d shards, %.2f m2, the car at %.1f m/s", car.veh, made, total, Len(vel));
}

// (diagnostic, driveby-m2) a car's glass: -1 not drawn yet / none seen, 0 intact, 1 broken
int CarGlass_State(uint32_t veh)
{
    Car* car = CarFor(veh, false);
    if (!car) return -1;
    if (car->shattered) return 1;
    for (int i = 0; i < car->npieces; i++)
        if (car->pieces[i].glass == 1) return 0;
    return -1;
}

// ------------------------------------------------------------------ (M5) one side window
// The car's glass triangles inside that window's box: facing out of its side, their middles between its sill
// and top, rear and front, well out from the car's middle. f(piece, mesh, triangle) for each; how many.
template <class F> static int ForWindow(Car& car, const CarWindow& w, F f)
{
    Mat4 ci = FullInverse(car.model);
    int n = 0;
    for (int i = 0; i < car.npieces; i++) {
        const Piece& p = car.pieces[i];
        if (p.glass != 1) continue;
        const Mesh* m = MeshOf(p);
        if (!m) continue;
        Mat4 rel = Mul(p.model, ci);   // the piece in the car's frame (a door's, from its hinge)
        for (int k = 0; k < m->ntri; k++) {
            Vec3 c0 = XformPoint(m->tri[3 * k], rel), c1 = XformPoint(m->tri[3 * k + 1], rel), c2 = XformPoint(m->tri[3 * k + 2], rel);
            Vec3 nn = Cross(c1 - c0, c2 - c0), mid = (c0 + c1 + c2) * (1.0f / 3.0f);
            float l = Len(nn);
            if (l < 1e-8f || fabsf(nn.x) < 0.55f * l || mid.x * (float)w.side < 0.3f || mid.y < w.y0 || mid.y > w.y1 || mid.z < w.z0 ||
                mid.z > w.z1)
                continue;
            f(i, m, k);
            n++;
        }
    }
    return n;
}

// the piece without its broken triangles, as a list in an index buffer of the car's own
static IDirect3DIndexBuffer8* CutIB(IDirect3DDevice8* d, Cut& c, const Mesh* m, UINT* n)
{
    UINT keep = (UINT)(c.ntri - c.ngone);
    *n = keep;
    if (c.ib || !m || m->ntri != c.ntri || !keep) return c.ib;
    IDirect3DIndexBuffer8* ib = nullptr;
    BYTE* w = nullptr;
    if (FAILED(d->CreateIndexBuffer(keep * 3 * m->isz, D3DUSAGE_WRITEONLY, m->isz == 4 ? D3DFMT_INDEX32 : D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib)))
        return nullptr;
    if (FAILED(ib->Lock(0, 0, &w, 0))) {
        ib->Release();
        return nullptr;
    }
    UINT o = 0;
    for (int k = 0; k < m->ntri; k++) {
        if (c.gone[k]) continue;
        for (int j = 0; j < 3; j++, o++)
            if (m->isz == 4) ((uint32_t*)w)[o] = m->idx[3 * k + j];
            else ((uint16_t*)w)[o] = (uint16_t)m->idx[3 * k + j];
    }
    ib->Unlock();
    c.ib = ib;
    if (g_glassLog > 0) Log("glass: piece %d drawn from a list of %u of its %d triangles", c.piece, keep, m->ntri);
    return ib;
}

int CarGlass_WindowState(uint32_t veh, const CarWindow& w)
{
    Car* car = CarFor(veh, false);
    if (!car || car->drawnFrame < g_frame - 30) return -2;
    int intact = 0, gone = 0;
    ForWindow(*car, w, [&](int i, const Mesh*, int k) {
        const Cut* c = CutOf(*car, i);
        if (car->shattered || (c && k < c->ntri && c->gone[k])) gone++;
        else intact++;
    });
    return intact ? 0 : gone ? 1 : -1;
}

bool CarGlass_BreakWindow(uint32_t veh, const CarWindow& w, Vec3 fromLocal)
{
    Car* car = CarFor(veh, false);
    if (!car || car->shattered || car->drawnFrame < g_frame - 30) return false;
    int n = 0;
    static Vec3 s_tri[24][3];   // the window's triangles in the world, for its shards
    int ntri = 0;
    ForWindow(*car, w, [&](int i, const Mesh* m, int k) {
        if (ntri < 24) {
            const Piece& p = car->pieces[i];
            for (int j = 0; j < 3; j++) s_tri[ntri][j] = XformPoint(m->tri[3 * k + j], p.model);
            ntri++;
        }
        Cut* c = (Cut*)CutOf(*car, i);
        if (!c) {
            uint8_t* gone = (uint8_t*)calloc(m->ntri, 1);
            if (!gone || car->ncuts >= kCuts) {
                free(gone);
                return;
            }
            c = &car->cuts[car->ncuts++];
            *c = {i, gone, 0, m->ntri, nullptr};
        }
        if (k >= c->ntri || c->gone[k]) return;
        c->gone[k] = 1;
        c->ngone++;
        if (c->ib) c->ib->Release(), c->ib = nullptr;   // (made again without it)
        n++;
    });
    if (!n) return false;
    Vec3 from = XformPoint(fromLocal, car->model);   // (the car as drawn, as its triangles are: at speed the caller's may be a frame on)
    WindowShards(*car, s_tri, ntri, Norm(Row(car->model, 0)) * (float)w.side, from);
    // the cracks in it go with it
    float s = (float)w.side;
    Decal_RemoveOnVehicleIn(veh, DECAL_GLASS, V3(s > 0 ? 0.3f : -9.0f, w.y0, w.z0), V3(s > 0 ? 9.0f : -0.3f, w.y1, w.z1));
    if (g_glassLog > 0)
        Log("glass: %08X's %s window broken: %d triangles (from %.2f,%.2f,%.2f in the car's frame)", veh, w.side > 0 ? "right" : "left", n, fromLocal.x,
            fromLocal.y, fromLocal.z);
    return true;
}

// (drive-by M7) the car as drawn along a line given in its own frame (from inside it: at speed the caller's frame of
// the car is a frame on from the one its triangles were drawn in); the hit in that frame too
bool CarGlass_RaycastLocal(uint32_t veh, Vec3 fromL, Vec3 dirL, float maxT, CarHit* out)
{
    Car* car = CarFor(veh, false);
    if (!car || car->drawnFrame < g_frame - 2) return false;
    Vec3 from = XformPoint(fromL, car->model);
    dirL = Norm(dirL);
    if (!CarGlass_Raycast(veh, from, Norm(XformDir(dirL, car->model)), maxT, out)) return false;
    out->pos = fromL + dirL * Len(out->pos - from);   // (the caller's frame: as far along its line)
    out->normal = Norm(XformDir(out->normal, FullInverse(car->model)));
    return true;
}

// (drive-by M7) a shot from inside his own car out through its glass: the pane it goes through breaks - the glass
// triangles round where the line meets it that lie in its plane (facing within 37 degrees of it, 15 cm of its plane,
// 1.6 m of the hit): the windshield, a back window, the far door's window. Glass that is one piece with the others
// (the minivan's) loses only that pane, as a door window does. Paint first along the line: nothing breaks (false).
// The line in the car's own frame (as RaycastLocal); *where too
bool CarGlass_BreakPane(uint32_t veh, Vec3 fromL, Vec3 dirL, float maxT, Vec3* where)
{
    Car* car = CarFor(veh, false);
    if (!car || car->shattered || car->drawnFrame < g_frame - 30) return false;
    Vec3 from = XformPoint(fromL, car->model), dir = Norm(XformDir(dirL, car->model));
    float best = maxT;
    int bp = -1, bk = -1;
    for (int i = 0; i < car->npieces; i++) {   // the first triangle along the line, as CarGlass_Raycast has it
        const Piece& p = car->pieces[i];
        const Cut* cut = p.glass == 1 ? CutOf(*car, i) : nullptr;
        const Mesh* m = MeshOf(p);
        if (!m) continue;
        Mat4 inv = FullInverse(p.model);
        Vec3 o = XformPoint(from, inv), d = XformDir(dir, inv);
        for (int k = 0; k < m->ntri; k++) {
            if (cut && k < cut->ntri && cut->gone[k]) continue;
            float t;
            Vec3 n;
            if (RayTri(o, d, m->tri + 3 * k, &t, &n) && t > 0.0f && t < best) best = t, bp = i, bk = k;
        }
    }
    if (bp < 0 || car->pieces[bp].glass != 1) return false;
    Mat4 ci = FullInverse(car->model);
    auto TriCar = [&](int i, const Mesh* m, int k, Vec3* c) {   // a triangle in the car's frame: its corners, its unit normal
        Mat4 rel = Mul(car->pieces[i].model, ci);
        for (int j = 0; j < 3; j++) c[j] = XformPoint(m->tri[3 * k + j], rel);
        Vec3 nn = Cross(c[1] - c[0], c[2] - c[0]);
        float l = Len(nn);
        return l > 1e-8f ? nn * (1.0f / l) : V3(0, 0, 0);
    };
    Vec3 hc[3];
    Vec3 hn = TriCar(bp, MeshOf(car->pieces[bp]), bk, hc);
    Vec3 hit = XformPoint(from + dir * best, ci);
    static Vec3 s_tri[24][3];
    int n = 0, ntri = 0;
    Vec3 lo = hit, hi = hit;
    for (int i = 0; i < car->npieces; i++) {
        const Piece& p = car->pieces[i];
        if (p.glass != 1) continue;
        const Mesh* m = MeshOf(p);
        if (!m) continue;
        for (int k = 0; k < m->ntri; k++) {
            Vec3 c[3];
            Vec3 tn = TriCar(i, m, k, c), mid = (c[0] + c[1] + c[2]) * (1.0f / 3.0f);
            if (fabsf(Dot(tn, hn)) < 0.80f || fabsf(Dot(mid - hit, hn)) > 0.15f || Len(mid - hit) > 1.6f) continue;
            Cut* cut = (Cut*)CutOf(*car, i);
            if (!cut) {
                uint8_t* gone = (uint8_t*)calloc(m->ntri, 1);
                if (!gone || car->ncuts >= kCuts) {
                    free(gone);
                    continue;
                }
                cut = &car->cuts[car->ncuts++];
                *cut = {i, gone, 0, m->ntri, nullptr};
            }
            if (k >= cut->ntri || cut->gone[k]) continue;
            cut->gone[k] = 1;
            cut->ngone++;
            if (cut->ib) cut->ib->Release(), cut->ib = nullptr;
            if (ntri < 24) {
                for (int j = 0; j < 3; j++) s_tri[ntri][j] = XformPoint(m->tri[3 * k + j], p.model);
                ntri++;
            }
            for (int j = 0; j < 3; j++)
                lo = V3(fminf(lo.x, c[j].x), fminf(lo.y, c[j].y), fminf(lo.z, c[j].z)), hi = V3(fmaxf(hi.x, c[j].x), fmaxf(hi.y, c[j].y), fmaxf(hi.z, c[j].z));
            n++;
        }
    }
    if (!n) return false;
    WindowShards(*car, s_tri, ntri, Norm(dir), from);
    Decal_RemoveOnVehicleIn(veh, DECAL_GLASS, lo - V3(0.1f, 0.1f, 0.1f), hi + V3(0.1f, 0.1f, 0.1f));   // (its cracks go with it)
    if (where) *where = hit;
    if (g_glassLog > 0)
        Log("glass: %08X's pane broken from inside: %d triangles round %.2f,%.2f,%.2f (car frame), facing %.2f %.2f %.2f", veh, n, hit.x, hit.y, hit.z, hn.x,
            hn.y, hn.z);
    return true;
}

void CarGlass_GlassHit(uint32_t veh, Vec3 pos, Vec3 n, Vec3 dir)
{
    Car* car = CarFor(veh, false);
    if (!car || car->shattered) return;
    if (++car->glassHits >= kShatterHits) {
        CarGlass_Shatter(veh, pos - dir * 2.0f);
        return;
    }
    Decal_AddOnVehicle(veh, pos, n, 0.5f, DECAL_GLASS, true);
    PlayAt(SND_GLASS1, pos, 0.35f, 1.55f + Rnd() * 0.2f);   // a crack, not the crash
}

void CarGlass_Update(float dt)
{
    for (int i = 0; i < g_nshards;) {
        Shard& s = g_shards[i];
        s.t += dt;
        s.v.y -= 9.8f * dt;
        s.p = s.p + s.v * dt;
        s.ang += s.spin * dt;
        if (s.p.y < s.ground && s.v.y < 0.0f) {   // bounce, skitter, settle
            s.p.y = s.ground;
            s.v = s.v * 0.35f;
            s.v.y = -s.v.y * 0.4f;
            s.spin *= 0.5f;
        }
        if (s.t > s.life)
            g_shards[i] = g_shards[--g_nshards];
        else
            i++;
    }
}

bool CarGlass_Any() { return g_nshards > 0; }

void CarGlass_Draw(Renderer& r)
{
    float view[16], proj[16];
    const Sprite* white = r.Find("white");
    if (!g_nshards || !white || !Game_GetCamera(view, proj)) return;
    Mat4 v, pj;
    memcpy(v.m, view, 64);
    memcpy(pj.m, proj, 64);
    Mat4 vp = Mul(v, pj);
    r.SpriteDepth(true);
    for (int i = 0; i < g_nshards; i++) {
        const Shard& s = g_shards[i];
        // a small tumbling triangle-ish quad of glass, catching the light now and then
        Mat4 rot = AxisAngle(s.axis, s.ang);
        Vec3 a = XformDir(V3(s.size, 0, 0), rot), b = XformDir(V3(0, s.size * 0.7f, 0), rot);
        const Vec3 c[4] = {s.p - a - b, s.p + a * 0.3f - b, s.p + a + b * 0.4f, s.p - a * 0.2f + b};
        float q[4][4];
        bool ok = true;
        for (int k = 0; k < 4 && ok; k++) {
            const float* m = vp.m;
            float x = c[k].x * m[0] + c[k].y * m[4] + c[k].z * m[8] + m[12];
            float y = c[k].x * m[1] + c[k].y * m[5] + c[k].z * m[9] + m[13];
            float z = c[k].x * m[2] + c[k].y * m[6] + c[k].z * m[10] + m[14];
            float w = c[k].x * m[3] + c[k].y * m[7] + c[k].z * m[11] + m[15];
            if (w < 0.05f) ok = false;
            else q[k][0] = (x / w * 0.5f + 0.5f) * r.W, q[k][1] = (-y / w * 0.5f + 0.5f) * r.H, q[k][2] = z / w, q[k][3] = 1.0f / w;
        }
        if (!ok) continue;
        float fade = Clamp01f((s.life - s.t) / 0.3f), glint = 0.55f + 0.45f * fabsf(sinf(s.ang * 1.7f));
        r.SpriteQuad(white, q, Rgba(0.80f * glint + 0.2f, 0.92f * glint + 0.08f, 1.0f, 0.75f * fade));
    }
    r.SpriteDepth(false);
}

void CarGlass_Clear()
{
    for (int i = 0; i < g_ncars; i++) FreeCuts(g_cars[i]);
    g_ncars = 0;
    g_nshards = 0;
    g_ntexKind = 0;
    FreeMeshes();
}

void CarGlass_DebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "glasspoint")) {   // "glasspoint x,y,z": a point on the glass of the car nearest that point, facing it
        Vec3 from = {};
        sscanf(arg, "%f,%f,%f", &from.x, &from.y, &from.z);
        float best = 1e9f;
        Vec3 bp = {};
        uint32_t bv = 0;
        for (int i = 0; i < g_ncars; i++) {
            const Car& c = g_cars[i];
            for (int k = 0; k < c.npieces; k++) {
                if (c.pieces[k].glass != 1) continue;
                const Mesh* m = MeshOf(c.pieces[k]);
                for (int t = 0; m && t < m->ntri; t++) {
                    const Vec3* tr = m->tri + 3 * t;
                    Vec3 w = XformPoint((tr[0] + tr[1] + tr[2]) * (1.0f / 3.0f), c.pieces[k].model);
                    Vec3 n = Norm(XformDir(Cross(tr[1] - tr[0], tr[2] - tr[0]), c.pieces[k].model));
                    Vec3 to = Norm(from - w);
                    float d = Len(from - w) * (1.5f - fabsf(Dot(n, to)));   // near, and facing it
                    if (d < best) best = d, bp = w, bv = c.veh;
                }
            }
        }
        if (bv) Log("glass: point on %08X's glass at %.2f,%.2f,%.2f", bv, bp.x, bp.y, bp.z);
        else Log("glass: no car with glass seen");
        return;
    }
    if (!strcmp(cmd, "glasslog")) g_glassLog = arg[0] ? atoi(arg) : 5;
    else if (!strcmp(cmd, "glasscars"))
        for (int i = 0; i < g_ncars; i++) {
            const Car& c = g_cars[i];
            int gl = 0;
            for (int k = 0; k < c.npieces; k++) gl += c.pieces[k].glass == 1;
            char name[24] = "?";
            const char* np = *(const char* const*)(uintptr_t)(c.veh + 248);   // (Vehicle+248: its name)
            if (np) {
                strncpy(name, np, 23);
                name[23] = 0;
            }
            Log("glass: car %08X '%s' drawn frame %d (now %d), %d pieces (%d glass), %d glass hits%s, hp %.2f type %d (meshes kept %d/%d)", c.veh, name,
                c.drawnFrame, g_frame, c.npieces, gl, c.glassHits, c.shattered ? ", broken" : "", c.hp, c.type, g_nmesh, kMeshes);
            for (int k = 0; k < c.npieces; k++) {
                const Piece& p = c.pieces[k];
                const Cut* cut = CutOf(c, k);
                // where it is in the car's frame (its triangles' box), what drew it
                Vec3 lo = V3(99, 99, 99), hi = V3(-99, -99, -99);
                const Mesh* m = MeshOf(p);
                Mat4 rel = Mul(p.model, FullInverse(c.model));
                for (int t = 0; m && t < m->ntri * 3; t++) {
                    Vec3 v = XformPoint(m->tri[t], rel);
                    lo = V3(fminf(lo.x, v.x), fminf(lo.y, v.y), fminf(lo.z, v.z)), hi = V3(fmaxf(hi.x, v.x), fmaxf(hi.y, v.y), fmaxf(hi.z, v.z));
                }
                Vec3 at = XformPoint(V3(0, 0, 0), rel);
                Log("glass:   piece %d: %u triangles (type %d), texture %08X, alpha test %d -> %s%s | vs %08lX palette %d stride %u | at %+.2f %+.2f %+.2f, "
                    "x %+.2f..%+.2f y %+.2f..%+.2f z %+.2f..%+.2f", k, p.pc, (int)p.type, (unsigned)p.tex, p.alphaTest,
                    p.glass == 1 ? "GLASS" : p.glass == 0 ? "paint" : "?", cut ? (cut->ngone >= cut->ntri ? ", all broken out" : ", a window broken out") : "",
                    p.vs, p.palette, p.stride, at.x, at.y, at.z, lo.x, hi.x, lo.y, hi.y, lo.z, hi.z);
                if (cut && cut->ngone < cut->ntri) Log("glass:     %d of its %d triangles gone", cut->ngone, cut->ntri);
            }
        }
}
