// Scene hooks on the game's Direct3D 8 device: remembers the camera the game used for
// the 3D world (view + projection) and, for debugging, can log one frame of draw
// calls ("probe" debug command).
#include "common.h"
#include "hit.h"
#include <stdarg.h>

typedef HRESULT(WINAPI* PFN_SetTransform)(IDirect3DDevice8*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
typedef HRESULT(WINAPI* PFN_SetVS)(IDirect3DDevice8*, DWORD);
typedef HRESULT(WINAPI* PFN_SetVSConst)(IDirect3DDevice8*, DWORD, const void*, DWORD);
typedef HRESULT(WINAPI* PFN_SetStream)(IDirect3DDevice8*, UINT, IDirect3DVertexBuffer8*, UINT);
typedef HRESULT(WINAPI* PFN_SetIndices)(IDirect3DDevice8*, IDirect3DIndexBuffer8*, UINT);
typedef HRESULT(WINAPI* PFN_DrawIP)(IDirect3DDevice8*, D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT);
typedef HRESULT(WINAPI* PFN_DrawP)(IDirect3DDevice8*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(WINAPI* PFN_Scene)(IDirect3DDevice8*);
typedef HRESULT(WINAPI* PFN_SetRT)(IDirect3DDevice8*, IDirect3DSurface8*, IDirect3DSurface8*);
typedef HRESULT(WINAPI* PFN_Clear)(IDirect3DDevice8*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);

static PFN_SetTransform o_SetTransform;
static PFN_SetVS o_SetVS;
static PFN_SetVSConst o_SetVSConst;
static PFN_SetStream o_SetStream;
static PFN_SetIndices o_SetIndices;
static PFN_DrawIP o_DrawIP;
static PFN_DrawP o_DrawP;
static PFN_Scene o_BeginScene, o_EndScene;
static PFN_SetRT o_SetRT;
static PFN_Clear o_Clear;

static bool g_ownScene;   // our overlay's BeginScene, not the game's
static bool g_inGameScene;
static int g_sceneDraws;  // draws since the game's BeginScene
static int g_worldDoneFrame = -1;
static int g_frameId, g_preFrame = -1;
void Wheel_OnWorldDrawn(IDirect3DDevice8* dev);
static int g_probe;          // frames left to log
static bool g_hudHide;       // (lab) debug "hud 0": the game's HUD not drawn - clean close-ups for the footage (the radar sat on his mouth)
static int g_events;
static DWORD g_curVS;
static int g_draws, g_drawsVS, g_vsConsts;
static int g_drawsMark;      // draws counted at the last scene/target/clear event (probe)

// scene, render target and clear events are always logged during a probe, with the
// number of draws since the previous one
static void ProbeEvent(const char* fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Log("  [%d draws] %s", g_draws - g_drawsMark, buf);
    g_drawsMark = g_draws;
}

static D3DMATRIX g_view, g_proj, g_world;
static bool g_haveCamera;
static bool g_wantView;      // next WORLD matrix after the world projection is the camera view
static bool g_frameCamera;   // captured this frame (only the first world pass counts)
static float g_aspect = 16.0f / 9.0f;

static bool IsPerspective(const D3DMATRIX* m) { return fabsf(m->_34 - 1.0f) < 1e-3f && fabsf(m->_44) < 1e-3f; }

void Scene_SetAspect(float aspect) { g_aspect = aspect; }

static void LogMatrix(const char* what, const D3DMATRIX* m)
{
    Log("  %s [%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]", what,
        m->_11, m->_12, m->_13, m->_14, m->_21, m->_22, m->_23, m->_24, m->_31, m->_32, m->_33, m->_34, m->_41,
        m->_42, m->_43, m->_44);
}

static D3DMATRIX g_lastWorld;   // as last set
static bool g_haveLastWorld;
bool Sky_PlaneDraw(UINT nv, UINT prims, int drawIndex, const float* world, float* out);

static HRESULT WINAPI h_SetTransform(IDirect3DDevice8* d, D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m)
{
    if (m && s == D3DTS_WORLD) g_lastWorld = *m, g_haveLastWorld = true;
    if (m) {
        // Pure3D never sets VIEW: WORLD carries model*view. The world pass starts with a
        // perspective projection matching the window's aspect, and the first WORLD set
        // after it is the bare camera view (static scenery has an identity model matrix).
        if (s == D3DTS_PROJECTION && IsPerspective(m) && !g_frameCamera && m->_11 > 0 &&
            fabsf((m->_22 / m->_11) - g_aspect) < 0.05f * g_aspect) {
            g_proj = *m;
            g_wantView = true;
        } else if (s == D3DTS_WORLD) {
            g_world = *m;
            if (g_wantView && (fabsf(m->_41) + fabsf(m->_42) + fabsf(m->_43)) > 1e-3f) {
                g_view = *m;
                g_wantView = false;
                g_frameCamera = true;
                g_haveCamera = true;
            }
        }
        if (g_probe && g_events++ < 900) {
            char w[32];
            snprintf(w, sizeof(w), "SetTransform(%d)", (int)s);
            LogMatrix(w, m);
        }
    }
    return o_SetTransform(d, s, m);
}

static HRESULT WINAPI h_SetVS(IDirect3DDevice8* d, DWORD h)
{
    g_curVS = h;
    if (g_probe && g_events++ < 900) Log("  SetVertexShader(0x%lX)", h);
    return o_SetVS(d, h);
}

bool Gun_SkinActive();
struct EyeSplit {
    IDirect3DIndexBuffer8 *left, *right;
    UINT nl, nr;
    IDirect3DBaseTexture8* leftTex;
    IDirect3DBaseTexture8* rightTex;
};
bool Gun_EyeSplit(IDirect3DDevice8* d, D3DPRIMITIVETYPE type, UINT mi, UINT nv, UINT si, UINT pc, uintptr_t vb, UINT stride, uintptr_t ib,
                  UINT base, EyeSplit* out);
void Gun_SkinRefresh(const float* firstBone);
int Gun_SkinResolve(const float (*bones)[12], const bool* set, int nslots, uint32_t key, float (*out)[12], bool* rep);

// Skinned meshes upload their bone palette (c17 on, 3 registers a bone, one bone per call
// in this game) and then draw. The uploads are collected here and the player's bones are
// swapped for our pose just before the draw, when the whole piece is known.
static float g_bones[32][12];
static bool g_boneSet[32];
static int g_boneMax;   // highest slot uploaded since the last draw, + 1
static uintptr_t g_vb0, g_ib;
static UINT g_baseVertex, g_stride0;

static HRESULT WINAPI h_SetStream(IDirect3DDevice8* d, UINT stream, IDirect3DVertexBuffer8* vb, UINT stride)
{
    if (stream == 0) g_vb0 = (uintptr_t)vb, g_stride0 = stride;
    return o_SetStream(d, stream, vb, stride);
}

static HRESULT WINAPI h_SetIndices(IDirect3DDevice8* d, IDirect3DIndexBuffer8* ib, UINT base)
{
    g_ib = (uintptr_t)ib;
    g_baseVertex = base;
    return o_SetIndices(d, ib, base);
}

extern int g_skinHeadPiece;   // (gun.cpp) set by Gun_SkinResolve when this draw is his eye piece
static void FixBones(IDirect3DDevice8* d, uint32_t a, uint32_t b, uint32_t c, uint32_t e)
{
    g_skinHeadPiece = 0;   // (only a skinned draw of his resolved just now can be his eyes)
    if (!g_boneMax) return;
    int first = 0;
    while (first < g_boneMax && !g_boneSet[first]) first++;
    if (first < g_boneMax && Gun_SkinActive()) {
        Gun_SkinRefresh(g_bones[first]);
        // the piece: same buffers and range = same palette layout
        uint32_t key = 2166136261u;
        const uint32_t parts[7] = {(uint32_t)g_vb0, (uint32_t)g_ib, g_baseVertex, a, b, c, e};
        for (uint32_t x : parts) key = (key ^ x) * 16777619u;
        static float out[32][12];
        bool rep[32];
        if (Gun_SkinResolve(g_bones, g_boneSet, g_boneMax, key, out, rep) > 0)
            for (int s = 0; s < g_boneMax;) {
                if (!rep[s]) {
                    s++;
                    continue;
                }
                int t = s;
                while (t < g_boneMax && rep[t]) t++;
                o_SetVSConst(d, 17 + 3 * s, out[s], 3 * (t - s));
                s = t;
            }
    }
    // someone knocked down far off (react.cpp): the game keeps him on the ground there, so his
    // flight is drawn here, his bones raised by its height (their translations are in view space)
    if (first < g_boneMax && g_haveCamera) {
        float h = React_LiftNear(&g_view._11, V3(g_bones[first][3], g_bones[first][7], g_bones[first][11]));
        if (h > 0.0f) {
            float up[3] = {g_view._21 * h, g_view._22 * h, g_view._23 * h};   // world up, in view space
            for (int b = first; b < g_boneMax; b++) {
                if (!g_boneSet[b]) continue;
                float m[12];
                memcpy(m, g_bones[b], 48);
                m[3] += up[0], m[7] += up[1], m[11] += up[2];
                o_SetVSConst(d, 17 + 3 * b, m, 3);
            }
        }
    }
    memset(g_boneSet, 0, sizeof(g_boneSet));
    g_boneMax = 0;
}

static int g_paletteUploads;   // (M5) bone palette uploads (c17 on, 3 registers a bone) since the last draw: a skinned mesh's
static HRESULT WINAPI h_SetVSConst(IDirect3DDevice8* d, DWORD reg, const void* data, DWORD n)
{
    if (reg >= 17 && n >= 3 && n % 3 == 0 && (reg - 17) % 3 == 0) g_paletteUploads++;
    if (reg >= 17 && data && n >= 3 && n % 3 == 0 && n <= 96 && (reg - 17) % 3 == 0 && (Gun_SkinActive() || React_Lifting())) {
        for (DWORD b = 0, s = (reg - 17) / 3; b < n / 3 && s < 32; b++, s++) {
            memcpy(g_bones[s], (const float*)data + b * 12, 48);
            g_boneSet[s] = true;
            if ((int)s + 1 > g_boneMax) g_boneMax = (int)s + 1;
        }
    }
    if (g_probe) {
        g_vsConsts++;
        if (g_events++ < 900) {
            const float* f = (const float*)data;
            Log("  SetVertexShaderConstant(c%lu, %lu) %.3f %.3f %.3f %.3f", reg, n, f[0], f[1], f[2], f[3]);
        }
    }
    return o_SetVSConst(d, reg, data, n);
}

// debug "texdump <hex address> <file>": a texture's top level as a 32-bit BMP with its alpha (WeaponWheel\)
void Scene_TexDump(const char* arg)
{
    unsigned addr = 0;
    char file[96] = "tex.bmp";
    sscanf(arg, "%x,%95s", &addr, file);
    IDirect3DTexture8* t = (IDirect3DTexture8*)(uintptr_t)addr;
    D3DSURFACE_DESC sd;
    D3DLOCKED_RECT lr;
    if (!t || t->GetType() != D3DRTYPE_TEXTURE || FAILED(t->GetLevelDesc(0, &sd))) {
        Log("texdump: %08X isn't a texture", addr);
        return;
    }
    if (sd.Format != D3DFMT_A8R8G8B8 && sd.Format != D3DFMT_X8R8G8B8) {
        Log("texdump: %08X is %ux%u format %d (only 32-bit ones)", addr, sd.Width, sd.Height, (int)sd.Format);
        return;
    }
    if (FAILED(t->LockRect(0, &lr, nullptr, D3DLOCK_READONLY))) {
        Log("texdump: %08X can't be locked (pool %d)", addr, (int)sd.Pool);
        return;
    }
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls%hs", g_modDir, file);
    FILE* f = _wfopen(path, L"wb");
    if (f) {
        BITMAPFILEHEADER fh = {};
        BITMAPINFOHEADER ih = {};
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(ih);
        fh.bfSize = fh.bfOffBits + sd.Width * sd.Height * 4;
        ih.biSize = sizeof(ih);
        ih.biWidth = (LONG)sd.Width;
        ih.biHeight = -(LONG)sd.Height;
        ih.biPlanes = 1;
        ih.biBitCount = 32;
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&ih, sizeof(ih), 1, f);
        for (UINT y = 0; y < sd.Height; y++) fwrite((const BYTE*)lr.pBits + y * lr.Pitch, 4, sd.Width, f);
        fclose(f);
    }
    t->UnlockRect(0);
    Log("texdump: %08X %ux%u format %d pool %d -> %s", addr, sd.Width, sd.Height, (int)sd.Format, (int)sd.Pool, file);
}

// debug "carprobe <frames>": every world draw whose model sits on a vehicle - its counts, blending,
// depth writes and texture (to find a car's glass)
static int g_carProbe;
void Scene_CarProbe(int frames) { g_carProbe = frames; }

static void CarProbeDraw(IDirect3DDevice8* d, const char* kind, UINT nv, UINT pc, UINT si)
{
    if (!g_haveCamera || !g_haveLastWorld || !g_inGameScene || g_ownScene) return;
    // model = world (model * view) * view^-1; the view is rigid
    const D3DMATRIX& v = g_view;
    D3DMATRIX inv = {};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) inv.m[i][j] = v.m[j][i];
    for (int j = 0; j < 3; j++) inv.m[3][j] = -(v.m[3][0] * inv.m[0][j] + v.m[3][1] * inv.m[1][j] + v.m[3][2] * inv.m[2][j]);
    inv.m[3][3] = 1;
    float o[3];
    for (int j = 0; j < 3; j++)
        o[j] = g_lastWorld.m[3][0] * inv.m[0][j] + g_lastWorld.m[3][1] * inv.m[1][j] + g_lastWorld.m[3][2] * inv.m[2][j] + inv.m[3][j];
    float pos[30][3];
    char names[30][24];
    int n = Game_ListVehicles(pos, names, 30);
    for (int i = 0; i < n; i++) {
        float dx = pos[i][0] - o[0], dy = pos[i][1] - o[1], dz = pos[i][2] - o[2];
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist > 3.0f) continue;
        DWORD ab = 0, src = 0, dst = 0, zw = 0, at = 0, cull = 0;
        d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
        d->GetRenderState(D3DRS_SRCBLEND, &src);
        d->GetRenderState(D3DRS_DESTBLEND, &dst);
        d->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
        d->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
        d->GetRenderState(D3DRS_CULLMODE, &cull);
        IDirect3DBaseTexture8* tex = nullptr;
        d->GetTexture(0, &tex);
        UINT tw = 0, th = 0;
        D3DFORMAT tf = D3DFMT_UNKNOWN;
        if (tex && tex->GetType() == D3DRTYPE_TEXTURE) {
            D3DSURFACE_DESC sd;
            if (SUCCEEDED(((IDirect3DTexture8*)tex)->GetLevelDesc(0, &sd))) tw = sd.Width, th = sd.Height, tf = sd.Format;
        }
        if (tex) tex->Release();
        Log("carprobe: %s #%d '%s' %.2f m: %u verts %u prims start %u | blend %lu (%lu,%lu) zwrite %lu alphatest %lu cull %lu | tex %p %ux%u fmt %d | vs 0x%lX vb %08X ib %08X",
            kind, g_sceneDraws, names[i], dist, nv, pc, si, ab, src, dst, zw, at, cull, (void*)tex, tw, th, (int)tf, g_curVS,
            (unsigned)g_vb0, (unsigned)g_ib);
        break;
    }
}

static HRESULT WINAPI h_DrawIP(IDirect3DDevice8* d, D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT si, UINT pc)
{
    if (g_hudHide && !g_ownScene && g_worldDoneFrame == g_frameId) return D3D_OK;   // (lab: the HUD left out - see h_DrawP)
    g_sceneDraws++;
    int palette = g_paletteUploads;
    g_paletteUploads = 0;
    if (g_carProbe > 0) CarProbeDraw(d, "DIP", nv, pc, si);
    // a car's pieces (carglass.cpp): which are its windows; a car whose windows are broken goes without,
    // a glass piece with one window broken out of it is drawn without that window's triangles
    if (g_inGameScene && !g_ownScene && g_frameCamera && g_haveLastWorld) {
        IDirect3DIndexBuffer8* cutIB = nullptr;
        UINT cutN = 0;
        int g = CarGlass_OnDraw(g_frameId, d, &g_lastWorld._11, &g_view._11, g_vb0, g_stride0, g_ib, g_baseVertex, t, mi, nv, si, pc, &cutIB, &cutN,
                                g_curVS, palette);
        if (g == 1) return D3D_OK;
        if (g == 2) {
            o_SetIndices(d, cutIB, g_baseVertex);
            HRESULT hr = o_DrawIP(d, D3DPT_TRIANGLELIST, mi, nv, 0, cutN);
            o_SetIndices(d, (IDirect3DIndexBuffer8*)g_ib, g_baseVertex);   // (the game's, as it set them)
            return hr;
        }
    }
    // a flat tire (tires.cpp): drawn squashed against the road
    D3DMATRIX squashed;
    if (g_inGameScene && !g_ownScene && g_frameCamera && g_haveLastWorld && Tires_OnDraw(g_frameId, &g_lastWorld._11, &g_view._11, &squashed._11)) {
        D3DMATRIX keep = g_lastWorld;
        o_SetTransform(d, D3DTS_WORLD, &squashed);
        HRESULT hr = o_DrawIP(d, t, mi, nv, si, pc);
        o_SetTransform(d, D3DTS_WORLD, &keep);
        return hr;
    }
    // the Duff plane in the sky (sky.cpp): drawn further along its circle when it's been shot
    D3DMATRIX moved;
    if (g_haveLastWorld && Sky_PlaneDraw(nv, pc, g_sceneDraws, &g_lastWorld._11, &moved._11)) {
        D3DMATRIX keep = g_lastWorld;
        o_SetTransform(d, D3DTS_WORLD, &moved);
        HRESULT hr = o_DrawIP(d, t, mi, nv, si, pc);
        o_SetTransform(d, D3DTS_WORLD, &keep);
        return hr;
    }
    FixBones(d, mi, nv, si, pc);
    {   // (face.inc) his eyes, while he winks: the right one as the game draws it, the left one closed
        EyeSplit es;
        if (g_inGameScene && !g_ownScene && Gun_EyeSplit(d, t, mi, nv, si, pc, g_vb0, g_stride0, g_ib, g_baseVertex, &es)) {
            IDirect3DBaseTexture8* keep = nullptr;
            d->GetTexture(0, &keep);
            if (es.rightTex) d->SetTexture(0, es.rightTex);
            o_SetIndices(d, es.right, g_baseVertex);
            HRESULT hr = o_DrawIP(d, D3DPT_TRIANGLELIST, mi, nv, 0, es.nr);
            d->SetTexture(0, es.leftTex);
            o_SetIndices(d, es.left, g_baseVertex);
            o_DrawIP(d, D3DPT_TRIANGLELIST, mi, nv, 0, es.nl);
            d->SetTexture(0, keep);
            if (keep) keep->Release();
            o_SetIndices(d, (IDirect3DIndexBuffer8*)g_ib, g_baseVertex);   // (the game's, as it set them)
            return hr;
        }
    }
    if (g_probe) {
        g_draws++;
        if (g_curVS > 0xFFFF) g_drawsVS++;
        if (g_events++ < 900) Log("  DrawIndexedPrimitive(type %d, %u verts, %u prims) vs=0x%lX", t, nv, pc, g_curVS);
    }
    return o_DrawIP(d, t, mi, nv, si, pc);
}

static HRESULT WINAPI h_DrawP(IDirect3DDevice8* d, D3DPRIMITIVETYPE t, UINT sv, UINT pc)
{
    // (lab: the HUD - what the game draws once its world is done (its depth cleared for the HUD) - left out)
    if (g_hudHide && !g_ownScene && g_worldDoneFrame == g_frameId) return D3D_OK;
    g_sceneDraws++;
    g_paletteUploads = 0;
    if (g_carProbe > 0) CarProbeDraw(d, "DP", pc * 3, pc, sv);
    FixBones(d, 0xFFFFFFFFu, sv, pc, 0);
    {   // (face.inc) his eyes - drawn without indices - while he winks: as two indexed halves, the left one closed
        EyeSplit es;
        UINT nverts = t == D3DPT_TRIANGLELIST ? pc * 3 : pc + 2;
        if (g_inGameScene && !g_ownScene && Gun_EyeSplit(d, t, sv, nverts, sv, pc, g_vb0, g_stride0, 0, 0, &es)) {
            IDirect3DBaseTexture8* keep = nullptr;
            d->GetTexture(0, &keep);
            if (es.rightTex) d->SetTexture(0, es.rightTex);
            o_SetIndices(d, es.right, 0);
            HRESULT hr = o_DrawIP(d, D3DPT_TRIANGLELIST, sv, nverts, 0, es.nr);
            d->SetTexture(0, es.leftTex);
            o_SetIndices(d, es.left, 0);
            o_DrawIP(d, D3DPT_TRIANGLELIST, sv, nverts, 0, es.nl);
            d->SetTexture(0, keep);
            if (keep) keep->Release();
            o_SetIndices(d, (IDirect3DIndexBuffer8*)g_ib, g_baseVertex);   // (the game's, as it set them)
            return hr;
        }
    }
    if (g_probe) {
        g_draws++;
        if (g_events++ < 900) Log("  DrawPrimitive(type %d, %u prims) vs=0x%lX", t, pc, g_curVS);
    }
    return o_DrawP(d, t, sv, pc);
}

void Gun_PreRender();
void Gun_PreRenderCamera();

void Scene_OwnBegin(bool own) { g_ownScene = own; }

static HRESULT WINAPI h_BeginScene(IDirect3DDevice8* d)
{
    if (g_probe) ProbeEvent("BeginScene");
    // the game opens several scenes per frame; pose/camera changes go in once, before the first
    if (!g_ownScene) {
        g_inGameScene = true;
        g_sceneDraws = 0;
        if (g_preFrame != g_frameId) {   // once per frame: aiming camera, then the arm/fist pose
            g_preFrame = g_frameId;
            Debug_ApplyPokes('b');
            Game_CameraFrameStart();
            Gun_PreRenderCamera();
            Gun_PreRender();
        }
    }
    return o_BeginScene(d);
}

static HRESULT WINAPI h_EndScene(IDirect3DDevice8* d)
{
    if (g_probe) ProbeEvent("EndScene");
    // the world is drawn: the game's own camera back before its update reads it (game.cpp)
    if (g_inGameScene && !g_ownScene) Game_RestoreCamera();
    g_inGameScene = false;
    return o_EndScene(d);
}

static HRESULT WINAPI h_SetRT(IDirect3DDevice8* d, IDirect3DSurface8* rt, IDirect3DSurface8* ds)
{
    if (g_probe) ProbeEvent("SetRenderTarget(%p, depth %p)", rt, ds);
    return o_SetRT(d, rt, ds);
}

static HRESULT WINAPI h_Clear(IDirect3DDevice8* d, DWORD n, const D3DRECT* r, DWORD flags, D3DCOLOR c, float z, DWORD s)
{
    if (g_probe) ProbeEvent("Clear(flags 0x%lX color %08lX z %.2f)", flags, c, z);
    // The game draws the 3D world, clears depth (only) and draws the HUD on top. Just
    // before that clear the world's depth is still there: draw the gun into the world.
    if (g_inGameScene && !g_ownScene && g_sceneDraws > 0 && (flags & D3DCLEAR_ZBUFFER) && !(flags & D3DCLEAR_TARGET) &&
        g_worldDoneFrame != g_frameId) {
        g_worldDoneFrame = g_frameId;
        Wheel_OnWorldDrawn(d);
    }
    return o_Clear(d, n, r, flags, c, z, s);
}

template <class T> static void Hook(IDirect3DDevice8* dev, int idx, T hook, T* orig)
{
    void* p = PatchVTable(dev, idx, (void*)hook);
    if (p && p != (void*)hook) *orig = (T)p;
}

void Scene_HookDevice(IDirect3DDevice8* dev)
{
    Hook(dev, 31, h_SetRT, &o_SetRT);
    Hook(dev, 34, h_BeginScene, &o_BeginScene);
    Hook(dev, 35, h_EndScene, &o_EndScene);
    Hook(dev, 36, h_Clear, &o_Clear);
    Hook(dev, 37, h_SetTransform, &o_SetTransform);
    Hook(dev, 70, h_DrawP, &o_DrawP);
    Hook(dev, 71, h_DrawIP, &o_DrawIP);
    Hook(dev, 76, h_SetVS, &o_SetVS);
    Hook(dev, 79, h_SetVSConst, &o_SetVSConst);
    Hook(dev, 83, h_SetStream, &o_SetStream);
    Hook(dev, 85, h_SetIndices, &o_SetIndices);
}

// Called once per presented frame.
void Scene_EndFrame()
{
    g_frameId++;
    if (g_carProbe > 0) g_carProbe--;
    g_frameCamera = false;
    g_wantView = false;
    if (g_probe) {
        Log("probe frame end: %d draws (%d with vertex shaders), %d VS constant uploads", g_draws, g_drawsVS, g_vsConsts);
        if (g_haveCamera) {
            LogMatrix("last view", &g_view);
            LogMatrix("last perspective proj", &g_proj);
        }
        g_probe--;
        g_events = 0;
        g_draws = g_drawsVS = g_vsConsts = g_drawsMark = 0;
    }
}

void Scene_HudHide(bool hide)
{
    g_hudHide = hide;
    Log("hud: the game's HUD %s", hide ? "hidden (lab footage)" : "drawn");
}

void Scene_Probe(int frames)
{
    g_probe = frames;
    g_events = 0;
    g_draws = g_drawsVS = g_vsConsts = g_drawsMark = 0;
    Log("probe: logging %d frame(s) of draw calls", frames);
}

bool Scene_GetCamera(D3DMATRIX* view, D3DMATRIX* proj)
{
    if (!g_haveCamera) return false;
    *view = g_view;
    *proj = g_proj;
    return true;
}
