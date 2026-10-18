// His head and body in numbers (charshape.inc, measured from the player models by the Weapons Lab's research), and a
// shut eye. Included in gun.cpp (it uses the skeleton cache, the mesh builders and the lighting).
//
// The game's own blink can't shut one eye: both eyes are one mesh with one texture, and a blink swaps that texture
// (eyeball.bmp.0 -> .3 and back, Blinker 0x464010). So a wink is drawn: two lids - the upper one coming down, the lower
// one up - on the left eye's dome, a little bigger than the eyeball, in the skin colour of the game's own closed-eye
// texture, lit like the gun, on his head joint. Drawn depth-tested in the 3D world, his face hides them exactly where it
// hides the eye. Where they meet, a dark lash line.
#include "charshape.inc"

static float g_sgWink;         // his left eye shut (0..1): the shotgun's wink, eased (SgUpdate)
static float g_dnBliss;        // both his eyes shut (0..1): the donut's bliss as he chews (donut.inc)
static float g_drunkLids;      // both eyelids drooping (0..1): drunk (drunk.inc)
static float g_sgWinkTarget;   // ... towards this (the shotgun's pose: late in the raise, when he aims)
static const CharShape* g_shape;
static uint32_t g_shapeSkel;
// which model he is: the head joint's bind pitch (and height) tells the five apart (9.9 / 36 / 28.4 / 31.5 / 18.3 degrees)
static const CharShape* CharShapeNow()
{
    if (!g_invBindSkel) return nullptr;
    if (g_invBindSkel == g_shapeSkel) return g_shape;
    g_shapeSkel = g_invBindSkel;
    Mat4 rest = FullInverse(g_invBind[17]);   // the head joint's bind world (model space): row 0 = (0, -sin p, -cos p)
    float pitch = atan2f(-rest.m[1], -rest.m[2]) * 57.29578f, y = rest.m[13];
    g_shape = nullptr;
    float best = 6.0f;
    for (const CharShape& c : kCharShapes) {
        float e = fabsf(pitch - c.headPitch) + 20.0f * fabsf(y - c.headY);
        if (e < best) best = e, g_shape = &c;
    }
    Log("Face: head pitch %.1f deg, height %.3f m -> %s", pitch, y, g_shape ? g_shape->name : "unknown model (no wink)");
    return g_shape;
}

// his eye in the head joint's frame (left: the table's; right: mirrored)
static Vec3 EyeLocal(const CharShape* c, bool left)
{
    return V3(c->eye[0], c->eye[1], left ? c->eye[2] : -c->eye[2]);
}

// how far his body reaches in front of Spine_2 at a height above it (metres; table steps of 0.10 m from -0.40)
static float FrontAt(const CharShape* c, float h)
{
    if (!c) return 0.30f;
    float f = (h + 0.40f) / 0.10f;
    int i = (int)floorf(f);
    if (i < 0) return c->front[0];
    if (i >= 7) return c->front[7];
    float t = f - i;
    return c->front[i] * (1 - t) + c->front[i + 1] * t;
}
static float FrontMax(const CharShape* c, float h0, float h1)
{
    float m = 0.0f;
    for (float h = h0; h <= h1 + 1e-4f; h += 0.05f) m = fmaxf(m, FrontAt(c, h));
    return m;
}

// ------------------------------------------------------------------ the lid
// a cap of a sphere round the eye's dome axis, in the head joint's frame (built once per model)
static GV g_lidv[400];
static uint16_t g_lidi[720];
static MeshBuf g_lidMesh = {g_lidv, g_lidi, 0, 0, 400, 720};
static const CharShape* g_lidFor;
static void BuildLid(const CharShape* c)
{
    if (g_lidFor == c && g_lidMesh.nv) return;
    g_lidFor = c;
    g_lidMesh.nv = g_lidMesh.ni = 0;
    g_mb = &g_lidMesh;
    Vec3 C = EyeLocal(c, true), A = Norm(V3(c->dome[0], c->dome[1], c->dome[2]));
    float R = c->eyeR * 1.035f, cap = fminf(c->capDeg + 8.0f, 150.0f) * PI / 180.0f;
    Vec3 u = Norm(Cross(A, fabsf(A.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0))), v = Cross(A, u);
    const int rings = 9, segs = 18;
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < segs; j++) {
            float t0 = cap * i / rings, t1 = cap * (i + 1) / rings, p0 = 2 * PI * j / segs, p1 = 2 * PI * (j + 1) / segs;
            auto dirAt = [&](float t, float p) { return A * cosf(t) + (u * cosf(p) + v * sinf(p)) * sinf(t); };
            Vec3 d[4] = {dirAt(t0, p0), dirAt(t1, p0), dirAt(t1, p1), dirAt(t0, p1)};
            Vec3 q[4];
            for (int k = 0; k < 4; k++) {
                q[k] = C + d[k] * R;
                if (q[k].z > -0.002f) q[k].z = -0.002f;   // (not past his nose: the eye's dome is cut at the head's mid-plane)
            }
            AddQuadN(q, d, V3(c->skin[0], c->skin[1], c->skin[2]), 0.05f, 0);
        }
}

// The wink: `shut` 0 open .. 1 shut. head = his posed head joint (world).
static void WinkDraw(Renderer& r, const Mat4& head, float shut, Vec3 viewDir)
{
    const CharShape* c = CharShapeNow();
    if (!c || shut < 0.01f) return;
    BuildLid(c);
    Vec3 C = XformPoint(EyeLocal(c, true), head);
    Vec3 up = Norm(XformDir(V3(0, -1, 0), head));      // (the head joint's -Y is up his face)
    Vec3 left = Norm(XformDir(V3(0, 0, -1), head));
    float R = c->eyeR * 1.035f;
    float meet = -0.10f * R;                            // where the lids meet: a little below the middle
    float hUp = R * 1.05f - (R * 1.05f - meet) * shut;  // the upper lid's edge: from over the top down to there
    float hLo = -R * 1.05f + (meet + R * 1.05f) * Clamp01(shut * 1.2f - 0.2f) * 0.999f;   // the lower lid's: up a little
    float planes[3][4];
    // upper lid: what is above its edge; and only on his left of the head's mid-plane (not over his nose)
    planes[0][0] = up.x, planes[0][1] = up.y, planes[0][2] = up.z, planes[0][3] = -Dot(up, C + up * hUp);
    planes[1][0] = left.x, planes[1][1] = left.y, planes[1][2] = left.z, planes[1][3] = -Dot(left, Row(head, 3));
    r.Clip3Dn(planes, 2);
    DrawLitMesh(r, g_lidMesh, head, viewDir);
    // lower lid: what is below its edge
    planes[0][0] = -up.x, planes[0][1] = -up.y, planes[0][2] = -up.z, planes[0][3] = Dot(up, C + up * hLo);
    r.Clip3Dn(planes, 2);
    DrawLitMesh(r, g_lidMesh, head, viewDir);
    // the lash line where they meet (a thin band of the cap, a touch bigger, dark brown-black)
    if (shut > 0.55f) {
        float w = c->eyeR * 0.055f * Clamp01((shut - 0.55f) / 0.3f);
        float edge = hUp;
        float bp[3][4] = {{up.x, up.y, up.z, -Dot(up, C + up * (edge - w))},
                          {-up.x, -up.y, -up.z, Dot(up, C + up * (edge + w * 0.6f))},
                          {left.x, left.y, left.z, -Dot(left, Row(head, 3))}};
        r.Clip3Dn(bp, 3);
        static GV lash[400];
        static MeshBuf lm = {lash, g_lidi, 0, 0, 400, 720};
        lm.nv = g_lidMesh.nv;
        lm.ni = g_lidMesh.ni;
        Vec3 Cl = EyeLocal(c, true);
        for (int i = 0; i < g_lidMesh.nv; i++) {
            lash[i] = g_lidv[i];
            lash[i].p = Cl + (g_lidv[i].p - Cl) * 1.012f;
            lash[i].col = V3(0.10f, 0.06f, 0.03f);
        }
        DrawLitMesh(r, lm, head, viewDir);
    }
    r.Clip3Dn(nullptr, 0);
}

// ------------------------------------------------------------------ the eyes themselves (a wink that holds from every side)
// The drawn lids (WinkDraw) cover the eye from the front, but from his side the eyeball's own edge still showed white. So
// the left eye is closed on the eyeball itself: his eyes are one small piece of his mesh, skinned to the head joint alone
// (a draw with one bone slot: g_skinHeadPiece), with one texture (the research: eyeball.bmp.0, the pupil a dot in its
// middle; the game blinks by swapping it for .1 .2 .3, the last plain skin). Its triangles are split by the head's
// mid-plane (bind pose x > 0 is his left) into two index lists; the right eye is drawn as the game set it (it goes on
// blinking), the left one with a closed-eye texture of ours - the eyeball's own texture with the lid down over it in his
// skin colour and a lash line - so it is a closed eye from every side, lit and shaped as the game draws it.
struct EyeTex {
    uintptr_t ptr;          // the game's texture
    int w, h;
    uint32_t* rgba;         // decoded (A8R8G8B8)
    float white, dark;      // share of near-white texels / dark ones in the middle (an eyeball: lots of white, a dark pupil)
    bool eyeball;
};
static EyeTex g_eyeTex[8];
static int g_neyeTex;
static struct {
    uintptr_t vb, ib;
    UINT stride, base, si, pc, mi, nv;
    D3DPRIMITIVETYPE type;
    IDirect3DIndexBuffer8 *left, *right;
    UINT nl, nr;
    bool bad;
} g_eyePiece;
static IDirect3DTexture8* g_eyeShut[4];   // the left eye closing: 1/3, 2/3, nearly, fully shut
static uintptr_t g_eyeShutFrom;           // made from this texture, in this character's colours
static const CharShape* g_eyeShutFor;
static float g_eyeShutWhite;              // ... the most open of his eyeball textures seen (its share of white)
static int g_eyeLog, g_eyeDump;
static int g_eyeSplitAt = -1;   // the frame (g_buildFrame) his eyes were last drawn split: the overlay lids aren't needed then
static float g_winkForce = -1.0f;   // debug "wink <0..1>": hold his left eye that shut (-1 = off)
static uintptr_t g_eyeDumped[16];
static int g_neyeDumped;

static uint32_t Rgb565(uint16_t c, uint32_t a = 255)
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (a << 24) | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}

// a texture's top level as A8R8G8B8 (32- and 16-bit formats, DXT1/3/5); malloc'd, nullptr if it can't be read
static uint32_t* ReadTexture(IDirect3DTexture8* t, int* w, int* h)
{
    D3DSURFACE_DESC sd;
    D3DLOCKED_RECT lr;
    if (FAILED(t->GetLevelDesc(0, &sd)) || sd.Pool == D3DPOOL_DEFAULT || sd.Width > 256 || sd.Height > 256) return nullptr;
    if (FAILED(t->LockRect(0, &lr, nullptr, D3DLOCK_READONLY))) return nullptr;
    int W = (int)sd.Width, H = (int)sd.Height;
    uint32_t* out = (uint32_t*)malloc(sizeof(uint32_t) * W * H);
    bool ok = out != nullptr;
    const BYTE* p = (const BYTE*)lr.pBits;
    switch (ok ? sd.Format : D3DFMT_UNKNOWN) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint32_t c = ((const uint32_t*)(p + y * lr.Pitch))[x];
                out[y * W + x] = sd.Format == D3DFMT_X8R8G8B8 ? (c | 0xFF000000u) : c;
            }
        break;
    case D3DFMT_R5G6B5:
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) out[y * W + x] = Rgb565(((const uint16_t*)(p + y * lr.Pitch))[x]);
        break;
    case D3DFMT_A1R5G5B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A4R4G4B4:
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint16_t c = ((const uint16_t*)(p + y * lr.Pitch))[x];
                uint32_t r, g, b, a;
                if (sd.Format == D3DFMT_A4R4G4B4) a = ((c >> 12) & 15) * 17, r = ((c >> 8) & 15) * 17, g = ((c >> 4) & 15) * 17, b = (c & 15) * 17;
                else a = sd.Format == D3DFMT_A1R5G5B5 ? ((c >> 15) ? 255 : 0) : 255, r = ((c >> 10) & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31,
                     b = (c & 31) * 255 / 31;
                out[y * W + x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        break;
    case D3DFMT_DXT1:
    case D3DFMT_DXT3:
    case D3DFMT_DXT5: {
        int bsz = sd.Format == D3DFMT_DXT1 ? 8 : 16;
        for (int by = 0; by < (H + 3) / 4; by++)
            for (int bx = 0; bx < (W + 3) / 4; bx++) {
                const BYTE* b = p + by * lr.Pitch + bx * bsz;
                uint32_t alpha[16];
                for (int i = 0; i < 16; i++) alpha[i] = 255;
                if (sd.Format == D3DFMT_DXT3)
                    for (int i = 0; i < 16; i++) alpha[i] = ((b[i / 2] >> ((i & 1) * 4)) & 15) * 17;
                if (sd.Format == D3DFMT_DXT5) {
                    uint32_t a0 = b[0], a1 = b[1], av[8] = {a0, a1};
                    for (int i = 2; i < 8; i++)
                        av[i] = a0 > a1 ? ((8 - i) * a0 + (i - 1) * a1) / 7 : i < 6 ? ((6 - i) * a0 + (i - 1) * a1) / 5 : (i == 6 ? 0 : 255);
                    uint64_t bits = 0;
                    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
                    for (int i = 0; i < 16; i++) alpha[i] = av[(bits >> (3 * i)) & 7];
                }
                const BYTE* c = b + (bsz - 8);
                uint16_t c0 = c[0] | (c[1] << 8), c1 = c[2] | (c[3] << 8);
                uint32_t col[4] = {Rgb565(c0), Rgb565(c1), 0, 0};
                auto mix = [](uint32_t a, uint32_t b2, int wa, int wb, int d) {
                    uint32_t r = (((a >> 16) & 255) * wa + ((b2 >> 16) & 255) * wb) / d, g = (((a >> 8) & 255) * wa + ((b2 >> 8) & 255) * wb) / d,
                             bl = ((a & 255) * wa + (b2 & 255) * wb) / d;
                    return 0xFF000000u | (r << 16) | (g << 8) | bl;
                };
                if (c0 > c1 || sd.Format != D3DFMT_DXT1) col[2] = mix(col[0], col[1], 2, 1, 3), col[3] = mix(col[0], col[1], 1, 2, 3);
                else col[2] = mix(col[0], col[1], 1, 1, 2), col[3] = 0;
                uint32_t idx = c[4] | (c[5] << 8) | (c[6] << 16) | ((uint32_t)c[7] << 24);
                for (int i = 0; i < 16; i++) {
                    int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if (x >= W || y >= H) continue;
                    uint32_t k = col[(idx >> (2 * i)) & 3];
                    out[y * W + x] = (k & 0x00FFFFFFu) | ((sd.Format == D3DFMT_DXT1 ? (k >> 24) : alpha[i]) << 24);
                }
            }
        break;
    }
    default:
        ok = false;
    }
    t->UnlockRect(0);
    if (!ok) {
        free(out);
        return nullptr;
    }
    *w = W;
    *h = H;
    return out;
}

static EyeTex* EyeTexOf(uintptr_t ptr)
{
    for (int i = 0; i < g_neyeTex; i++)
        if (g_eyeTex[i].ptr == ptr) return &g_eyeTex[i];
    if (g_neyeTex >= 8) return nullptr;
    EyeTex& e = g_eyeTex[g_neyeTex++];
    e = {};
    e.ptr = ptr;
    IDirect3DBaseTexture8* bt = (IDirect3DBaseTexture8*)ptr;
    if (bt && bt->GetType() == D3DRTYPE_TEXTURE) e.rgba = ReadTexture((IDirect3DTexture8*)bt, &e.w, &e.h);
    if (e.rgba) {
        int white = 0, dark = 0, mid = 0;
        for (int y = 0; y < e.h; y++)
            for (int x = 0; x < e.w; x++) {
                uint32_t c = e.rgba[y * e.w + x];
                int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                if (r > 215 && g > 215 && b > 215) white++;
                if (abs(x - e.w / 2) <= e.w / 10 && abs(y - e.h / 2) <= e.h / 10) {
                    mid++;
                    if (r + g + b < 150) dark++;
                }
            }
        e.white = (float)white / (e.w * e.h);
        e.dark = mid ? (float)dark / mid : 0.0f;
        e.eyeball = e.white > 0.35f && e.dark > 0.05f;
    }
    Log("Eyes: texture %08X %dx%d: %.0f%% white, %.0f%% dark in the middle -> %s", (unsigned)ptr, e.w, e.h, e.white * 100.0f,
        e.dark * 100.0f, e.eyeball ? "an eyeball (open)" : e.rgba ? "not an open eyeball" : "unreadable");
    return &e;
}

// our closed-eye textures from the game's open one, drawn the way the game's own blink frames are (research/eyes: eyeball.bmp.1
// has skin-coloured lids over the top and bottom quarter, .2 leaves an 8-of-64 px slit round the pupil, .3 is plain skin with no
// lash line - in SHAR a shut eye is a skin-coloured bulge): straight lids closing from the top and the bottom to just below the
// middle. g_eyeLash (debug "eyelash <share of the height>") adds a thin lash line where they meet; 0 = the game's look.
static float g_eyeLash = 0.035f;   // (a thin line reads as a deliberate wink; 0 = the plain lid of the game's own blink)
static void MakeShutTextures(IDirect3DDevice8* d, const EyeTex& e, const CharShape* c)
{
    for (auto& t : g_eyeShut)
        if (t) t->Release(), t = nullptr;
    uint32_t skin = 0xFF000000u | ((uint32_t)(c->skin[0] * 255.0f) << 16) | ((uint32_t)(c->skin[1] * 255.0f) << 8) |
                    (uint32_t)(c->skin[2] * 255.0f);
    const uint32_t lash = 0xFF1A1006u;
    // the open slit's half height for each frame, as a share of the texture (.1 = 32 px of 64, .2 = 8 px, nearly, shut)
    static const float kSlit[4] = {0.25f, 0.0625f, 0.025f, 0.0f};
    const float mid = 0.5f + 0.5f / 64.0f;   // (the pupil is rows 29-34 of 64: the lids meet on its centre)
    for (int k = 0; k < 4; k++) {
        IDirect3DTexture8* t = nullptr;
        if (FAILED(d->CreateTexture(e.w, e.h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t))) return;
        D3DLOCKED_RECT lr;
        if (FAILED(t->LockRect(0, &lr, nullptr, 0))) {
            t->Release();
            return;
        }
        float half = kSlit[k];
        for (int y = 0; y < e.h; y++) {
            uint32_t* row = (uint32_t*)((BYTE*)lr.pBits + y * lr.Pitch);
            float v = (y + 0.5f) / e.h;
            for (int x = 0; x < e.w; x++) {
                uint32_t col = e.rgba[y * e.w + x];
                if (fabsf(v - mid) >= half) col = skin;
                if (k == 3 && g_eyeLash > 0.0f && fabsf(v - mid) < g_eyeLash * 0.5f) col = lash;
                row[x] = (col & 0x00FFFFFFu) | (e.rgba[y * e.w + x] & 0xFF000000u);
            }
        }
        t->UnlockRect(0);
        g_eyeShut[k] = t;
    }
    g_eyeShutFrom = e.ptr;
    g_eyeShutFor = c;
    g_eyeShutWhite = e.white;
    Log("Eyes: closed-eye textures made from %08X (%dx%d) in %s's colours (lash %.3f)", (unsigned)e.ptr, e.w, e.h, c->name, g_eyeLash);
}

// split the eye piece into his left and right eyes: two index lists (once per piece)
static bool EyeLists(IDirect3DDevice8* d, uintptr_t vbp, UINT stride, uintptr_t ibp, UINT base, D3DPRIMITIVETYPE type, UINT mi, UINT nv,
                     UINT si, UINT pc)
{
    auto& E = g_eyePiece;
    if (E.vb == vbp && E.ib == ibp && E.stride == stride && E.base == base && E.si == si && E.pc == pc && E.type == type) return !E.bad;
    if (E.left) E.left->Release();
    if (E.right) E.right->Release();
    E = {};
    E.vb = vbp, E.ib = ibp, E.stride = stride, E.base = base, E.si = si, E.pc = pc, E.type = type, E.mi = mi, E.nv = nv;
    E.bad = true;
    // (ibp 0: drawn without indices - DrawPrimitive - the triangles are the vertices from si on, and our lists index the
    // vertex buffer itself)
    IDirect3DVertexBuffer8* vb = (IDirect3DVertexBuffer8*)vbp;
    IDirect3DIndexBuffer8* ib = (IDirect3DIndexBuffer8*)ibp;
    D3DVERTEXBUFFER_DESC vd;
    D3DINDEXBUFFER_DESC id = {};
    if (!vb || FAILED(vb->GetDesc(&vd)) || vd.Pool == D3DPOOL_DEFAULT || (ib && (FAILED(ib->GetDesc(&id)) || id.Pool == D3DPOOL_DEFAULT)) ||
        (type != D3DPT_TRIANGLELIST && type != D3DPT_TRIANGLESTRIP) || stride < 12 || pc == 0 || pc > 4000)
        return false;
    UINT isz = ib && id.Format == D3DFMT_INDEX32 ? 4 : 2, nidx = type == D3DPT_TRIANGLELIST ? pc * 3 : pc + 2;
    if (ib && (si + nidx) * isz > id.Size) return false;
    if (!ib && si + nidx > 65535) return false;
    BYTE *vdata = nullptr, *idata = nullptr;
    if (FAILED(vb->Lock(0, 0, &vdata, D3DLOCK_READONLY))) return false;
    if (ib && FAILED(ib->Lock(0, 0, &idata, D3DLOCK_READONLY))) {
        vb->Unlock();
        return false;
    }
    UINT nvert = vd.Size / stride;
    static uint32_t L[3 * 4000], R[3 * 4000];
    UINT nl = 0, nr = 0;
    bool sane = true;
    for (UINT k = 0; k < pc && sane; k++) {
        UINT a = type == D3DPT_TRIANGLELIST ? 3 * k : k, ix[3];
        for (int j = 0; j < 3; j++)
            ix[j] = !ib ? si + a + j : (isz == 4 ? ((const uint32_t*)idata)[si + a + j] : ((const uint16_t*)idata)[si + a + j]);
        if (ix[0] + base >= nvert || ix[1] + base >= nvert || ix[2] + base >= nvert) {
            sane = false;
            break;
        }
        if (ix[0] == ix[1] || ix[1] == ix[2] || ix[0] == ix[2]) continue;
        bool odd = type == D3DPT_TRIANGLESTRIP && (k & 1);
        float cx = 0;
        for (int j = 0; j < 3; j++) cx += ((const Vec3*)(vdata + (ix[j] + base) * stride))->x;
        uint32_t* dst = cx > 0.0f ? L + 3 * nl++ : R + 3 * nr++;   // bind pose x > 0: his left
        for (int j = 0; j < 3; j++) dst[odd && j < 2 ? 1 - j : j] = ix[j];
    }
    if (ib) ib->Unlock();
    vb->Unlock();
    if (!sane || !nl || !nr) {
        Log("Eyes: the eye piece couldn't be split (%u left, %u right triangles)", nl, nr);
        return false;
    }
    auto make = [&](const uint32_t* src, UINT n, IDirect3DIndexBuffer8** out) {
        BYTE* w = nullptr;
        if (FAILED(d->CreateIndexBuffer(n * 3 * isz, D3DUSAGE_WRITEONLY, isz == 4 ? D3DFMT_INDEX32 : D3DFMT_INDEX16, D3DPOOL_MANAGED, out)))
            return false;
        if (FAILED((*out)->Lock(0, 0, &w, 0))) return false;
        for (UINT i = 0; i < 3 * n; i++)
            if (isz == 4) ((uint32_t*)w)[i] = src[i];
            else ((uint16_t*)w)[i] = (uint16_t)src[i];
        (*out)->Unlock();
        return true;
    };
    if (!make(L, nl, &E.left) || !make(R, nr, &E.right)) return false;
    E.nl = nl, E.nr = nr, E.bad = false;
    Log("Eyes: his eyes are piece %08X/%08X: %u + %u triangles (left / right)", (unsigned)vbp, (unsigned)ibp, nl, nr);
    return true;
}

// scene.cpp, at a draw of his: his eyes are drawn as two halves (the left one closed by `shut`). False: draw as usual.
struct EyeSplit {
    IDirect3DIndexBuffer8 *left, *right;
    UINT nl, nr;
    IDirect3DBaseTexture8* leftTex;   // nullptr: the game's own
    IDirect3DBaseTexture8* rightTex;  // nullptr: the game's own (both shut: the donut's "mmm")
};
bool Gun_EyeSplit(IDirect3DDevice8* d, D3DPRIMITIVETYPE type, UINT mi, UINT nv, UINT si, UINT pc, uintptr_t vb, UINT stride, uintptr_t ib,
                  UINT base, EyeSplit* out)
{
    if (!g_skinHeadPiece) return false;
    IDirect3DBaseTexture8* tex = nullptr;
    d->GetTexture(0, &tex);
    if (tex) tex->Release();   // (GetTexture adds a reference)
    if (g_eyeDump > 0 && tex) {   // debug "eyedump <seconds>": every texture his eye piece is drawn with, as BMPs (the blink's frames)
        bool seen = false;
        for (int i = 0; i < g_neyeDumped; i++) seen |= g_eyeDumped[i] == (uintptr_t)tex;
        if (!seen && g_neyeDumped < 16) {
            g_eyeDumped[g_neyeDumped++] = (uintptr_t)tex;
            EyeTex* e = EyeTexOf((uintptr_t)tex);
            if (e && e->rgba) {
                wchar_t path[MAX_PATH];
                swprintf(path, MAX_PATH, L"%lseyetex_%d.bmp", g_modDir, g_neyeDumped);
                FILE* f = _wfopen(path, L"wb");
                if (f) {
                    BITMAPFILEHEADER fh = {};
                    BITMAPINFOHEADER ih = {};
                    fh.bfType = 0x4D42, fh.bfOffBits = sizeof(fh) + sizeof(ih), fh.bfSize = fh.bfOffBits + e->w * e->h * 4;
                    ih.biSize = sizeof(ih), ih.biWidth = e->w, ih.biHeight = -e->h, ih.biPlanes = 1, ih.biBitCount = 32;
                    fwrite(&fh, sizeof(fh), 1, f), fwrite(&ih, sizeof(ih), 1, f), fwrite(e->rgba, 4, e->w * e->h, f), fclose(f);
                }
                Log("eyedump: texture %d = %08X (piece %08X, %u triangles)", g_neyeDumped, (unsigned)(uintptr_t)tex, (unsigned)g_skinHeadPiece, pc);
            }
        }
    }
    float both = fmaxf((G.held == WPN_DONUT || G.held == WPN_BEER) ? g_dnBliss : 0.0f, g_drunkLids);
    float shut = fmaxf(g_winkForce >= 0.0f ? g_winkForce : g_sgWink, both);
    if (!tex || (shut < 0.02f && G.held != WPN_SHOTGUN && G.held != WPN_DONUT && G.held != WPN_BEER)) return false;
    const CharShape* c = CharShapeNow();
    if (!c) return false;
    // the texture must read as an eyeball (white with a dark middle) - or be one of the game's blink frames for this piece
    EyeTex* e = EyeTexOf((uintptr_t)tex);
    if (!e || !e->rgba) return false;
    // (made once per character, from the most open eyeball texture seen: his blink swaps the open texture for a half-shut one
    // that also reads as an eyeball, and remaking ours at every blink cost four textures and baked half a lid into them. Made as
    // soon as the shotgun is out, not at the first wink: if the game happened to be mid-blink then, there was nothing to make them
    // from and the overlay lids stood in for a frame - the pose QA's "wink without the eyeball shut")
    if (e->eyeball && (g_eyeShutFor != c || !g_eyeShut[3] || e->white > g_eyeShutWhite + 0.05f)) MakeShutTextures(d, *e, c);
    if (shut < 0.02f || !g_eyeShut[3]) return false;
    if (!EyeLists(d, vb, stride, ib, base, type, mi, nv, si, pc)) return false;
    auto frame = [](float s) { return s < 0.30f ? 0 : s < 0.60f ? 1 : s < 0.90f ? 2 : 3; };
    int k = frame(shut);
    out->left = g_eyePiece.left, out->right = g_eyePiece.right, out->nl = g_eyePiece.nl, out->nr = g_eyePiece.nr;
    out->leftTex = g_eyeShut[k];
    out->rightTex = both >= 0.02f ? g_eyeShut[frame(both)] : nullptr;
    g_eyeSplitAt = g_buildFrame;
    if (g_eyeLog > 0) {
        g_eyeLog--;
        Log("eyelog: shut %.2f -> frame %d, piece %08X, %u/%u triangles", shut, k, (unsigned)g_skinHeadPiece, out->nl, out->nr);
    }
    return true;
}
void Gun_EyeReset()   // the device is being reset: our textures and lists go (they are rebuilt)
{
    for (auto& t : g_eyeShut)
        if (t) t->Release(), t = nullptr;
    g_eyeShutFrom = 0;
    g_eyeShutFor = nullptr;
    g_eyeShutWhite = 0.0f;
    if (g_eyePiece.left) g_eyePiece.left->Release();
    if (g_eyePiece.right) g_eyePiece.right->Release();
    g_eyePiece = {};
    for (int i = 0; i < g_neyeTex; i++) free(g_eyeTex[i].rgba);
    g_neyeTex = 0;
}

static bool FaceDebugCmd(const char* cmd, const char* arg)
{
    if (!strcmp(cmd, "eyedump")) {   // "eyedump 1": dump every texture his eye piece is drawn with (WeaponWheel\eyetex_N.bmp); 0 = off
        g_eyeDump = atoi(arg);
        g_neyeDumped = 0;
    } else if (!strcmp(cmd, "eyelog")) g_eyeLog = arg[0] ? atoi(arg) : 1;
    else if (!strcmp(cmd, "eyelash")) {   // "eyelash <share of the height>": a lash line on the shut eye (0 = none, the game's look)
        g_eyeLash = (float)atof(arg);
        g_eyeShutFor = nullptr;   // (remade at the next draw)
        Log("eyelash: %.3f", g_eyeLash);
    } else if (!strcmp(cmd, "wink")) {   // "wink <0..1>": his left eye held that shut (-1 = as aiming makes it)
        g_winkForce = arg[0] ? (float)atof(arg) : -1.0f;
        Log("wink: %.2f", g_winkForce);
    } else return false;
    return true;
}
