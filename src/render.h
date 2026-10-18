// Direct3D 8 overlay renderer: sprite atlas, game-font text, background blur.
#pragma once
#include "common.h"

struct Sprite {
    int x, y, w, h;   // atlas rectangle
    float ax, ay;     // anchor inside the rectangle (atlas px)
    float sc;         // atlas px per reference (1080p) px
};

struct Glyph {
    int x, y, w, h;
    float xo, yo, adv;   // reference px, relative to the pen on the baseline
    bool ok;
};

struct Font {
    float height, base;
    Glyph g[128];
};

struct Color {
    float r, g, b, a;
};

static inline Color Rgba(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }

enum BlendMode { BLEND_OPAQUE, BLEND_PREMUL, BLEND_ADD, BLEND_AVG };
enum TextAlign { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

class Renderer {
public:
    bool Load(IDirect3DDevice8* dev);      // atlas texture (managed pool, survives Reset)
    void Release();                        // everything
    void OnLost();                         // default-pool targets
    bool Begin(IDirect3DSurface8* target); // the back buffer being presented
    bool BeginInScene();                   // inside the game's scene, on its target + depth
    void End();

    // background
    bool CaptureBlur(float strength);
    void DrawBackdrop(float fade, float dim, bool blurred);
    // drunk: the frame laid back over itself, softened, see-through and shifted (dx, dy px) - double vision
    void DrawGhost(float alpha, float dx, float dy);

    // 2D drawing in screen pixels; sizes are in reference px and multiplied by k
    const Sprite* Find(const char* name) const;
    void Sprite2(const Sprite* s, float x, float y, float scale, Color c, bool mirror = false,
                 float rot = 0.0f, BlendMode blend = BLEND_PREMUL, float sx = 1.0f, float sy = 1.0f);
    void Capsule(const char* base, float cx, float cy, float width, float scale, Color c);
    float TextWidth(char font, const char* txt, float scale) const;
    void Text(char font, const char* txt, float x, float baseline, float scale, Color top, Color bottom,
              TextAlign align, float shadow);
    void Rect(float x0, float y0, float x1, float y1, Color c);

    // inside the game's scene only: sprites are depth-tested against the 3D world at this
    // depth (z/w and 1/w of their projected position); off = flat 2D again
    void SpriteDepth(bool on, float z = 0.0f, float rhw = 1.0f);
    // a sprite on four projected corners (x, y in screen px, z/w, 1/w each; top-left, top-right,
    // bottom-right, bottom-left): lies on a plane in the 3D world, depth-tested per pixel. Needs
    // SpriteDepth(true) first.
    void SpriteQuad(const Sprite* s, const float (*corner)[4], Color c, BlendMode blend = BLEND_PREMUL);
    bool InGameScene() const { return inGameScene; }

    // Depth probe, for "what is under the crosshair": inside the game's scene (the world's
    // depth buffer intact), ProbeDraw tests 1-pixel quads at kProbeN distances per pixel
    // against it, counting the ones in front into a render target of ours. ProbeRead (at
    // Present) reads the counts back: distance in metres, or -1 when nothing within range.
    // z(d) = p10 + p14 / d is the projection's depth for distance d (view depth).
    // range[i] = {near, far}: that pixel tests evenly spaced depths between the two instead
    // of 1-150 m (for a fine reading near a known depth); ProbeRead then gives -2 when the
    // surface is nearer than `near`. Up to kProbeMax pixels.
    static const int kProbeN = 128;
    static const int kProbeMax = 48;
    bool ProbeDraw(const float (*px)[2], int k, float p10, float p14, const float (*range)[2] = nullptr);
    bool ProbeRead(float* dist, int k);
    // (lab) the same test for many pixels at once - a scan of the picture (lab.cpp): up to kScanMax
    // pixels a frame into a target of its own, read back at Present. range as above (nullptr = 1-150 m)
    static const int kScanMax = 256;
    bool ScanDraw(const float (*px)[2], int k, float p10, float p14, const float (*range)[2]);
    bool ScanRead(float* dist, int k);

    // optional group transform (used for the tab flip): p' = o + (p - o) * s
    void SetGroup(float ox, float oy, float sx, float sy) { gox = ox; goy = oy; gsx = sx; gsy = sy; }
    void ClearGroup() { gsx = gsy = 1.0f; gox = goy = 0.0f; }

    // a piece of the 3D picture kept from before the HUD (inside the game's scene, after its world) and drawn back over
    // the HUD as a soft-edged disc (the radar faded out while aiming from a car)
    bool KeepPatch(int x, int y, int w, int h);
    void DrawPatchDisc(float cx, float cy, float r, float feather, float alpha);

    bool Screenshot(IDirect3DSurface8* target, const wchar_t* path);
    bool RecordFrame(IDirect3DSurface8* target, FILE* f, int* w, int* h, bool full);   // (debug "rec") half size (or full), BGR rows appended

    // a capsule bar of button badges and labels, centred on (cx, cy)
    struct HintPart { const char* buttons[2]; const char* text; };
    void HintBar(float cx, float cy, const HintPart* parts, int n, float alpha);
    float BadgeWidth(const char* label) const;

    // 3D: draw coloured triangle meshes into the game's scene (game camera + depth)
    struct MeshV { float x, y, z; DWORD c; };
    void Begin3D(const float* view, const float* proj, bool depthTest);
    void Mesh(const MeshV* v, int nv, const uint16_t* idx, int ni, const float* world);
    // user clip planes for the next meshes (world space: a x + b y + c z + d >= 0 is kept; n = 0: off), up to 3
    void Clip3D(const float* plane4);
    void Clip3Dn(const float (*planes)[4], int n);
    void End3D();

    int W = 0, H = 0;      // back buffer size
    float k = 1.0f;        // reference px -> screen px
    bool loaded = false;

private:
    struct V { float x, y, z, rhw; DWORD c; float u, v; };
    void Flush();
    void Use(IDirect3DBaseTexture8* tex, BlendMode blend);
    void Quad(const float* xs, const float* ys, float u0, float v0, float u1, float v1, const DWORD* c);
    void SetCommonState();
    bool EnsureTargets();
    void ReleaseBlur();   // the blur targets only
    void Pass(IDirect3DTexture8* dst, int dw, int dh, IDirect3DTexture8* src, int sw, int sh,
              const float (*taps)[2], int ntaps);
    bool LoadAtlas();

    IDirect3DDevice8* dev = nullptr;
    bool inGameScene = false;
    bool spriteDepthOn = false;
    float spriteZ = 0.0f, spriteRhw = 1.0f;
    IDirect3DTexture8* atlas = nullptr;
    int aw = 1, ah = 1;
    Sprite sprites[96];
    char names[96][24];
    int nsprites = 0;
    Font fonts[2];   // 'B' Boulder, 'W' Swiss 721

    IDirect3DTexture8 *scene = nullptr, *half = nullptr, *qa = nullptr, *qb = nullptr;
    int qw = 0, qh = 0;
    int tw = 0, th = 0;   // size the blur targets were made for
    IDirect3DTexture8* blurResult = nullptr;
    IDirect3DSurface8 *probeRT = nullptr, *probeSys = nullptr;
    IDirect3DSurface8 *scanRT = nullptr, *scanSys = nullptr;   // (lab) the scan's
    RECT scanRects[kScanMax];
    float scanRange[kScanMax][2];
    int scanK = 0;
    bool scanPending = false, scanOff = false;
    IDirect3DTexture8* patch = nullptr;
    int patchTW = 0, patchTH = 0, patchX = 0, patchY = 0, patchW = 0, patchH = 0;
    D3DFORMAT patchFmt = D3DFMT_UNKNOWN;
    bool patchOk = false, patchFailed = false;
    RECT probeRects[kProbeMax];
    float probeRange[kProbeMax][2];
    int probeK = 0;
    bool probePending = false, probeOff = false;
    bool targetsFailed = false;
    bool copyFailedLogged = false;

    DWORD stateBlock = 0;
    IDirect3DSurface8 *savedRT = nullptr, *savedDS = nullptr;
    D3DVIEWPORT8 savedVP;
    IDirect3DVertexBuffer8* savedVB = nullptr;
    UINT savedStride = 0;
    IDirect3DIndexBuffer8* savedIB = nullptr;
    UINT savedBase = 0;
    IDirect3DSurface8* backbuffer = nullptr;

    V verts[6 * 1024];
    int nv = 0;
    IDirect3DBaseTexture8* curTex = nullptr;
    BlendMode curBlend = BLEND_PREMUL;

    float gox = 0, goy = 0, gsx = 1, gsy = 1;
};
