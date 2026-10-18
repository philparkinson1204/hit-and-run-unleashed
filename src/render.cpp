#include "render.h"

#define FVF_2D (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

static inline DWORD PM(Color c)   // premultiplied ARGB
{
    float a = c.a < 0 ? 0 : (c.a > 1 ? 1 : c.a);
    auto ch = [&](float v) { v = v < 0 ? 0 : (v > 1 ? 1 : v); return (DWORD)(v * a * 255.0f + 0.5f); };
    return ((DWORD)(a * 255.0f + 0.5f) << 24) | (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

// ------------------------------------------------------------------ atlas
bool Renderer::LoadAtlas()
{
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%lsatlas.txt", g_modDir);
    FILE* f = _wfopen(path, L"r");
    if (!f) {
        Log("atlas.txt missing");
        return false;
    }
    memset(fonts, 0, sizeof(fonts));
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char kind = line[0];
        if (kind == 'S' && nsprites < 96) {
            Sprite s;
            char name[64];
            if (sscanf(line + 2, "%63s %d %d %d %d %f %f %f", name, &s.x, &s.y, &s.w, &s.h, &s.ax, &s.ay, &s.sc) == 8) {
                strncpy(names[nsprites], name, 23);
                names[nsprites][23] = 0;
                sprites[nsprites++] = s;
            }
        } else if (kind == 'F') {
            char key;
            float h, b;
            if (sscanf(line + 2, "%c %f %f", &key, &h, &b) == 3) {
                Font& ft = fonts[key == 'B' ? 0 : 1];
                ft.height = h;
                ft.base = b;
            }
        } else if (kind == 'G') {
            char key;
            int code;
            Glyph g;
            if (sscanf(line + 2, "%c %d %d %d %d %d %f %f %f", &key, &code, &g.x, &g.y, &g.w, &g.h, &g.xo, &g.yo, &g.adv) == 9 &&
                code > 0 && code < 128) {
                g.ok = true;
                fonts[key == 'B' ? 0 : 1].g[code] = g;
            }
        }
    }
    fclose(f);

    swprintf(path, MAX_PATH, L"%lsatlas.png", g_modDir);
    uint32_t* px = nullptr;
    int w = 0, h = 0;
    if (!Png_LoadFile(path, &px, &w, &h)) {
        Log("atlas.png could not be loaded");
        return false;
    }
    // the game's own art (digits, arrows, the fonts' letters) from the player's copy of the game
    swprintf(path, MAX_PATH, L"%lsatlas.txt", g_modDir);
    GameArt_Fill(path, px, w, h);
    if (g_cfg.debug) {   // for tools: the atlas as the mod uses it (width, height, then ARGB rows)
        swprintf(path, MAX_PATH, L"%lsatlas_filled.raw", g_modDir);
        if (FILE* d = _wfopen(path, L"wb")) {
            fwrite(&w, 4, 1, d);
            fwrite(&h, 4, 1, d);
            fwrite(px, 4, (size_t)w * h, d);
            fclose(d);
        }
    }
    // premultiply
    for (int i = 0; i < w * h; i++) {
        uint32_t c = px[i];
        uint32_t a = c >> 24;
        uint32_t r = ((c >> 16) & 255) * a / 255, g = ((c >> 8) & 255) * a / 255, b = (c & 255) * a / 255;
        px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    HRESULT hr = dev->CreateTexture(w, h, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &atlas);
    if (FAILED(hr)) {
        Log("CreateTexture(atlas %dx%d) failed 0x%08lX", w, h, hr);
        free(px);
        return false;
    }
    // level 0 + box-filtered mip chain
    int lw = w, lh = h;
    uint32_t* cur = px;
    DWORD levels = atlas->GetLevelCount();
    for (DWORD lv = 0; lv < levels; lv++) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(atlas->LockRect(lv, &lr, nullptr, 0))) {
            for (int y = 0; y < lh; y++) memcpy((BYTE*)lr.pBits + (size_t)y * lr.Pitch, cur + (size_t)y * lw, lw * 4);
            atlas->UnlockRect(lv);
        }
        if (lv + 1 >= levels) break;
        int nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
        uint32_t* nxt = (uint32_t*)malloc((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                int x0 = x * 2, y0 = y * 2, x1 = x0 + (lw > 1), y1 = y0 + (lh > 1);
                uint32_t p[4] = {cur[y0 * lw + x0], cur[y0 * lw + x1], cur[y1 * lw + x0], cur[y1 * lw + x1]};
                uint32_t out = 0;
                for (int s = 0; s < 32; s += 8) {
                    uint32_t sum = ((p[0] >> s) & 255) + ((p[1] >> s) & 255) + ((p[2] >> s) & 255) + ((p[3] >> s) & 255);
                    out |= ((sum + 2) / 4) << s;
                }
                nxt[y * nw + x] = out;
            }
        if (cur != px) free(cur);
        cur = nxt;
        lw = nw;
        lh = nh;
    }
    if (cur != px) free(cur);
    free(px);
    aw = w;
    ah = h;
    Log("Atlas loaded: %dx%d, %d sprites, %lu mip levels", w, h, nsprites, levels);
    return true;
}

const Sprite* Renderer::Find(const char* name) const
{
    for (int i = 0; i < nsprites; i++)
        if (strcmp(names[i], name) == 0) return &sprites[i];
    return nullptr;
}

bool Renderer::Load(IDirect3DDevice8* d)
{
    dev = d;
    loaded = LoadAtlas();
    return loaded;
}

// the blur's four targets only (a failure mid-frame must not take the state block with it: End()
// would then skip putting the game's render state back)
void Renderer::ReleaseBlur()
{
    auto rel = [](IDirect3DTexture8*& t) { if (t) { t->Release(); t = nullptr; } };
    rel(scene);
    rel(half);
    rel(qa);
    rel(qb);
    blurResult = nullptr;
}

void Renderer::OnLost()
{
    ReleaseBlur();
    if (probeRT) probeRT->Release();
    if (probeSys) probeSys->Release();
    probeRT = probeSys = nullptr;
    if (scanRT) scanRT->Release();
    if (scanSys) scanSys->Release();
    scanRT = scanSys = nullptr;
    scanPending = false;
    if (patch) patch->Release();
    patch = nullptr;
    patchOk = patchFailed = false;
    probePending = false;
    if (stateBlock && dev) {
        dev->DeleteStateBlock(stateBlock);
        stateBlock = 0;
    }
    targetsFailed = false;
    probeOff = false;   // a lost device or a new size: try the depth probe again (the checks run again)
}

void Renderer::Release()
{
    OnLost();
    if (atlas) {
        atlas->Release();
        atlas = nullptr;
    }
    loaded = false;
}

// ------------------------------------------------------------------ state
bool Renderer::BeginInScene()
{
    if (!dev || !atlas) return false;
    IDirect3DSurface8* rt = nullptr;
    if (FAILED(dev->GetRenderTarget(&rt)) || !rt) return false;
    inGameScene = true;
    bool ok = Begin(rt);
    rt->Release();
    if (!ok) inGameScene = false;
    return ok;
}

bool Renderer::Begin(IDirect3DSurface8* target)
{
    if (!dev || !atlas || !target) return false;
    // (no state block, no drawing: without it End() couldn't give the game back its render states)
    if (!stateBlock && FAILED(dev->CreateStateBlock(D3DSBT_ALL, &stateBlock))) stateBlock = 0;
    if (!stateBlock) return false;
    backbuffer = target;
    backbuffer->AddRef();
    D3DSURFACE_DESC d;
    backbuffer->GetDesc(&d);
    W = (int)d.Width;
    H = (int)d.Height;
    if (scene && (W != tw || H != th)) OnLost();   // window was resized

    if (!stateBlock && FAILED(dev->CreateStateBlock(D3DSBT_ALL, &stateBlock))) stateBlock = 0;   // (OnLost let it go)
    if (!stateBlock) {
        backbuffer->Release();
        backbuffer = nullptr;
        return false;
    }
    dev->CaptureStateBlock(stateBlock);
    dev->GetRenderTarget(&savedRT);
    if (FAILED(dev->GetDepthStencilSurface(&savedDS))) savedDS = nullptr;
    dev->GetViewport(&savedVP);
    dev->GetStreamSource(0, &savedVB, &savedStride);
    dev->GetIndices(&savedIB, &savedBase);

    if (!inGameScene) {
        Scene_OwnBegin(true);
        dev->BeginScene();
        Scene_OwnBegin(false);
    }
    dev->SetRenderTarget(backbuffer, nullptr);
    D3DVIEWPORT8 vp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    SetCommonState();
    nv = 0;
    curTex = nullptr;
    ClearGroup();
    return true;
}

void Renderer::SetCommonState()
{
    dev->SetPixelShader(0);
    dev->SetVertexShader(FVF_2D);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_DITHERENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPING, TRUE);
    dev->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_WRAP0, 0);
    dev->SetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    curBlend = BLEND_PREMUL;

    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetTextureStageState(0, D3DTSS_MIPMAPLODBIAS, 0);
    dev->SetTextureStageState(0, D3DTSS_MAXMIPLEVEL, 0);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetTexture(1, nullptr);
}

void Renderer::End()
{
    Flush();
    spriteDepthOn = false;
    spriteZ = 0.0f;
    spriteRhw = 1.0f;
    if (!inGameScene) dev->EndScene();
    inGameScene = false;
    dev->SetRenderTarget(savedRT, savedDS);
    dev->SetViewport(&savedVP);
    dev->SetStreamSource(0, savedVB, savedStride);
    dev->SetIndices(savedIB, savedBase);
    if (stateBlock) dev->ApplyStateBlock(stateBlock);
    auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    rel(savedRT);
    rel(savedDS);
    rel(savedVB);
    rel(savedIB);
    rel(backbuffer);
}

// ------------------------------------------------------------------ batching
void Renderer::Use(IDirect3DBaseTexture8* tex, BlendMode blend)
{
    if (tex == curTex && blend == curBlend) return;
    Flush();
    if (tex != curTex) {
        dev->SetTexture(0, tex);
        curTex = tex;
    }
    if (blend != curBlend) {
        switch (blend) {
        case BLEND_OPAQUE:
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            break;
        case BLEND_PREMUL:
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            break;
        case BLEND_ADD:
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
            break;
        case BLEND_AVG:
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            break;
        }
        curBlend = blend;
    }
}

void Renderer::Flush()
{
    if (nv) {
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, nv / 3, verts, sizeof(V));
        nv = 0;
    }
}

void Renderer::Quad(const float* xs, const float* ys, float u0, float v0, float u1, float v1, const DWORD* c)
{
    if (nv + 6 > (int)(sizeof(verts) / sizeof(verts[0]))) Flush();
    // corners: 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left
    const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int j = idx[i];
        V& v = verts[nv++];
        v.x = gox + (xs[j] - gox) * gsx - 0.5f;
        v.y = goy + (ys[j] - goy) * gsy - 0.5f;
        v.z = spriteZ;
        v.rhw = spriteRhw;
        v.c = c[j];
        v.u = us[j];
        v.v = vs[j];
    }
}

void Renderer::Sprite2(const Sprite* s, float x, float y, float scale, Color c, bool mirror, float rot,
                       BlendMode blend, float sx, float sy)
{
    if (!s || c.a <= 0.002f) return;
    Use(atlas, blend);
    float f = k * scale / s->sc;
    float fx = f * sx, fy = f * sy;
    float ax = mirror ? (s->w - s->ax) : s->ax;
    float l = -ax * fx, r = (s->w - ax) * fx, t = -s->ay * fy, b = (s->h - s->ay) * fy;
    float cx[4] = {l, r, r, l}, cy[4] = {t, t, b, b};
    float xs[4], ys[4];
    float cs = cosf(rot), sn = sinf(rot);
    for (int i = 0; i < 4; i++) {
        xs[i] = x + cx[i] * cs - cy[i] * sn;
        ys[i] = y + cx[i] * sn + cy[i] * cs;
    }
    float u0 = (float)s->x / aw, u1 = (float)(s->x + s->w) / aw;
    float v0 = (float)s->y / ah, v1 = (float)(s->y + s->h) / ah;
    if (mirror) {
        float t2 = u0;
        u0 = u1;
        u1 = t2;
    }
    DWORD col = PM(c);
    DWORD cc[4] = {col, col, col, col};
    Quad(xs, ys, u0, v0, u1, v1, cc);
}

void Renderer::SpriteQuad(const Sprite* s, const float (*p)[4], Color c, BlendMode blend)
{
    if (!s || c.a <= 0.002f) return;
    Use(atlas, blend);
    if (nv + 6 > (int)(sizeof(verts) / sizeof(verts[0]))) Flush();
    float u0 = (float)s->x / aw, u1 = (float)(s->x + s->w) / aw;
    float v0 = (float)s->y / ah, v1 = (float)(s->y + s->h) / ah;
    const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    DWORD col = PM(c);
    for (int i = 0; i < 6; i++) {
        int j = idx[i];
        V& v = verts[nv++];
        v.x = p[j][0] - 0.5f;
        v.y = p[j][1] - 0.5f;
        v.z = p[j][2];
        v.rhw = p[j][3];
        v.c = col;
        v.u = us[j];
        v.v = vs[j];
    }
}

void Renderer::SpriteDepth(bool on, float z, float rhw)
{
    on = on && inGameScene && savedDS;
    if (on != spriteDepthOn) {
        Flush();
        dev->SetRenderTarget(backbuffer, on ? savedDS : nullptr);
        D3DVIEWPORT8 vp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
        dev->SetViewport(&vp);
        dev->SetRenderState(D3DRS_ZENABLE, on ? D3DZB_TRUE : D3DZB_FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        spriteDepthOn = on;
    }
    spriteZ = on ? z : 0.0f;
    spriteRhw = on ? rhw : 1.0f;
}

void Renderer::Rect(float x0, float y0, float x1, float y1, Color c)
{
    const Sprite* w = Find("white");
    if (!w) return;
    Use(atlas, BLEND_PREMUL);
    float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    float u = (w->x + w->w * 0.5f) / aw, v = (w->y + w->h * 0.5f) / ah;
    DWORD col = PM(c);
    DWORD cc[4] = {col, col, col, col};
    Quad(xs, ys, u, v, u, v, cc);
}

void Renderer::Capsule(const char* base, float cx, float cy, float width, float scale, Color c)
{
    char n[32];
    snprintf(n, sizeof(n), "%s_l", base);
    const Sprite* L = Find(n);
    snprintf(n, sizeof(n), "%s_m", base);
    const Sprite* M = Find(n);
    snprintf(n, sizeof(n), "%s_r", base);
    const Sprite* R = Find(n);
    if (!L || !M || !R) return;
    float f = k * scale / L->sc;
    float capw = L->ax * f;
    float inner = width - 2 * capw;
    if (inner < 1) inner = 1;
    float x0 = cx - inner / 2, x1 = cx + inner / 2;
    float y0 = cy - M->ay * f, y1 = cy + (M->h - M->ay) * f;
    float v0 = (float)M->y / ah, v1 = (float)(M->y + M->h) / ah;
    Use(atlas, BLEND_PREMUL);
    DWORD col = PM(c);
    DWORD cc[4] = {col, col, col, col};
    // left cap up to its anchor (the join sits inside the straight part)
    {
        float xs[4] = {x0 - L->ax * f, x0, x0, x0 - L->ax * f};
        float ys[4] = {cy - L->ay * f, cy - L->ay * f, cy + (L->h - L->ay) * f, cy + (L->h - L->ay) * f};
        Quad(xs, ys, (float)L->x / aw, (float)L->y / ah, (L->x + L->ax) / aw, (float)(L->y + L->h) / ah, cc);
    }
    // right cap from its anchor
    {
        float xe = x1 + (R->w - R->ax) * f;
        float xs[4] = {x1, xe, xe, x1};
        float ys[4] = {cy - R->ay * f, cy - R->ay * f, cy + (R->h - R->ay) * f, cy + (R->h - R->ay) * f};
        Quad(xs, ys, (R->x + R->ax) / aw, (float)R->y / ah, (float)(R->x + R->w) / aw, (float)(R->y + R->h) / ah, cc);
    }
    // straight middle: stretch the centre column of the strip between the caps
    float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    float u = (M->x + M->w * 0.5f) / aw;
    Quad(xs, ys, u, v0, u, v1, cc);
}

float Renderer::TextWidth(char font, const char* txt, float scale) const
{
    const Font& ft = fonts[font == 'B' ? 0 : 1];
    float w = 0;
    for (const char* p = txt; *p; p++) {
        int ch = (unsigned char)*p;
        if (ch < 128 && ft.g[ch].ok) w += ft.g[ch].adv;
    }
    return w * scale * k;
}

void Renderer::Text(char font, const char* txt, float x, float baseline, float scale, Color top, Color bottom,
                    TextAlign align, float shadow)
{
    const Font& ft = fonts[font == 'B' ? 0 : 1];
    float w = TextWidth(font, txt, scale);
    float pen0 = align == ALIGN_CENTER ? x - w / 2 : (align == ALIGN_RIGHT ? x - w : x);
    Use(atlas, BLEND_PREMUL);
    for (int pass = shadow > 0 ? 0 : 1; pass < 2; pass++) {
        float ox = pass == 0 ? 2.0f * k * scale : 0, oy = pass == 0 ? 3.0f * k * scale : 0;
        DWORD ct = pass == 0 ? PM(Rgba(0, 0, 0, shadow * top.a)) : PM(top);
        DWORD cb = pass == 0 ? PM(Rgba(0, 0, 0, shadow * bottom.a)) : PM(bottom);
        DWORD cc[4] = {ct, ct, cb, cb};
        float pen = pen0;
        for (const char* p = txt; *p; p++) {
            int ch = (unsigned char)*p;
            if (ch >= 128 || !ft.g[ch].ok) continue;
            const Glyph& g = ft.g[ch];
            if (g.w > 0) {
                float x0 = pen + g.xo * scale * k + ox, y0 = baseline + g.yo * scale * k + oy;
                float x1 = x0 + g.w * scale * k, y1 = y0 + g.h * scale * k;
                float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
                Quad(xs, ys, (float)g.x / aw, (float)g.y / ah, (float)(g.x + g.w) / aw, (float)(g.y + g.h) / ah, cc);
            }
            pen += g.adv * scale * k;
        }
    }
}

// ------------------------------------------------------------------ blur
bool Renderer::EnsureTargets()
{
    if (scene) return true;
    if (targetsFailed) return false;
    D3DSURFACE_DESC d;
    backbuffer->GetDesc(&d);
    if (d.MultiSampleType != D3DMULTISAMPLE_NONE) {
        Log("Back buffer is multisampled, blur disabled");
        targetsFailed = true;
        return false;
    }
    qw = (W + 3) / 4;
    qh = (H + 3) / 4;
    HRESULT h1 = dev->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &scene);
    HRESULT h2 = dev->CreateTexture((W + 1) / 2, (H + 1) / 2, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &half);
    HRESULT h3 = dev->CreateTexture(qw, qh, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &qa);
    HRESULT h4 = dev->CreateTexture(qw, qh, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &qb);
    if (FAILED(h1) || FAILED(h2) || FAILED(h3) || FAILED(h4)) {
        Log("Blur targets failed (fmt %d): %08lX %08lX %08lX %08lX", d.Format, h1, h2, h3, h4);
        ReleaseBlur();
        targetsFailed = true;
        return false;
    }
    tw = W;
    th = H;
    Log("Blur targets created %dx%d fmt %d", W, H, d.Format);
    return true;
}

void Renderer::Pass(IDirect3DTexture8* dst, int dw, int dh, IDirect3DTexture8* src, int sw, int sh,
                    const float (*taps)[2], int ntaps)
{
    Flush();
    IDirect3DSurface8* surf = nullptr;
    dst->GetSurfaceLevel(0, &surf);
    dev->SetRenderTarget(surf, nullptr);
    surf->Release();
    D3DVIEWPORT8 vp = {0, 0, (DWORD)dw, (DWORD)dh, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    for (int i = 0; i < ntaps; i++) {
        // running average: tap i is blended in with weight 1/(i+1)
        Use(src, i == 0 ? BLEND_OPAQUE : BLEND_AVG);
        float a = 1.0f / (float)(i + 1);
        DWORD c = ((DWORD)(a * 255.0f + 0.5f) << 24) | 0xFFFFFF;
        DWORD cc[4] = {c, c, c, c};
        float du = taps[i][0] / sw, dv = taps[i][1] / sh;
        float xs[4] = {0, (float)dw, (float)dw, 0}, ys[4] = {0, 0, (float)dh, (float)dh};
        Quad(xs, ys, du, dv, 1 + du, 1 + dv, cc);
        Flush();
    }
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
}

bool Renderer::CaptureBlur(float strength)
{
    blurResult = nullptr;
    if (strength <= 0.0f || !EnsureTargets()) return false;
    IDirect3DSurface8* s = nullptr;
    scene->GetSurfaceLevel(0, &s);
    HRESULT hr = dev->CopyRects(backbuffer, nullptr, 0, s, nullptr);
    s->Release();
    if (FAILED(hr)) {
        if (!copyFailedLogged) Log("CopyRects(backbuffer) failed 0x%08lX, blur disabled", hr);
        copyFailedLogged = true;
        ReleaseBlur();
        targetsFailed = true;   // (after: OnLost cleared it, so the "blur disabled" never held)
        return false;
    }
    ClearGroup();
    static const float one[1][2] = {{0, 0}};
    Pass(half, (W + 1) / 2, (H + 1) / 2, scene, W, H, one, 1);
    Pass(qa, qw, qh, half, (W + 1) / 2, (H + 1) / 2, one, 1);
    int passes = (int)(strength * 3.0f + 0.5f);
    if (passes < 1) passes = 1;
    if (passes > 6) passes = 6;
    IDirect3DTexture8 *a = qa, *b = qb;
    for (int p = 0; p < passes; p++) {
        float o = 0.5f + (float)p;
        const float taps[4][2] = {{-o, -o}, {o, -o}, {-o, o}, {o, o}};
        Pass(b, qw, qh, a, qw, qh, taps, 4);
        IDirect3DTexture8* t = a;
        a = b;
        b = t;
    }
    blurResult = a;
    dev->SetRenderTarget(backbuffer, nullptr);
    D3DVIEWPORT8 vp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    return true;
}

void Renderer::DrawBackdrop(float fade, float dim, bool blurred)
{
    ClearGroup();
    float xs[4] = {0, (float)W, (float)W, 0}, ys[4] = {0, 0, (float)H, (float)H};
    if (blurred && blurResult) {
        // blurred + dimmed copy of the frame, cross-faded in
        Use(blurResult, BLEND_AVG);
        DWORD d = (DWORD)(dim * 255.0f + 0.5f);
        DWORD c = ((DWORD)(fade * 255.0f + 0.5f) << 24) | (d << 16) | (d << 8) | d;
        DWORD cc[4] = {c, c, c, c};
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
        Quad(xs, ys, 0, 0, 1, 1, cc);   // (the passes map the whole frame onto the whole quarter-size target)
        Flush();
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
    } else {
        Rect(0, 0, (float)W, (float)H, Rgba(0, 0, 0, fade * (1.0f - dim)));
    }
    // navy tint like the game's help boxes, then a vignette
    Rect(0, 0, (float)W, (float)H, Rgba(3 / 255.0f, 15 / 255.0f, 75 / 255.0f, 0.20f * fade));
    const Sprite* v = Find("vignette");
    if (v) {
        Use(atlas, BLEND_PREMUL);
        DWORD col = PM(Rgba(1, 1, 1, 0.85f * fade));
        DWORD cc[4] = {col, col, col, col};
        Quad(xs, ys, (v->x + 0.5f) / aw, (v->y + 0.5f) / ah, (v->x + v->w - 0.5f) / aw, (v->y + v->h - 0.5f) / ah, cc);
    }
}

// the frame copied, halved (soft) and drawn back over itself shifted, see-through (the blur's targets; nothing if they failed)
void Renderer::DrawGhost(float alpha, float dx, float dy)
{
    if (alpha <= 0.002f || !EnsureTargets()) return;
    IDirect3DSurface8* s = nullptr;
    scene->GetSurfaceLevel(0, &s);
    HRESULT hr = dev->CopyRects(backbuffer, nullptr, 0, s, nullptr);
    s->Release();
    if (FAILED(hr)) return;   // (the blur's capture logs it and turns the targets off)
    ClearGroup();
    static const float one[1][2] = {{0, 0}};
    Pass(half, (W + 1) / 2, (H + 1) / 2, scene, W, H, one, 1);
    dev->SetRenderTarget(backbuffer, nullptr);
    D3DVIEWPORT8 vp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    Use(half, BLEND_AVG);
    DWORD c = ((DWORD)(fminf(alpha, 1.0f) * 255.0f + 0.5f) << 24) | 0xFFFFFF;
    DWORD cc[4] = {c, c, c, c};
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    float xs[4] = {dx, W + dx, W + dx, dx}, ys[4] = {dy, dy, H + dy, H + dy};
    Quad(xs, ys, 0, 0, 1, 1, cc);
    Flush();
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
}

// ------------------------------------------------------------------ depth probe
static float ProbeDepth(int i) { return powf(150.0f, (i + 0.5f) / Renderer::kProbeN); }   // 1 .. 150 m

bool Renderer::ProbeDraw(const float (*px)[2], int k, float p10, float p14, const float (*range)[2])
{
    if (probeOff || !inGameScene || !savedDS || k < 1 || k > kProbeMax) return false;
    D3DSURFACE_DESC dd;
    if (FAILED(savedDS->GetDesc(&dd))) return false;
    static bool s_logged;
    if (!s_logged) {
        s_logged = true;
        Log("Depth buffer: format %d, %ux%u, multisample %d", dd.Format, dd.Width, dd.Height, dd.MultiSampleType);
    }
    if (dd.MultiSampleType != D3DMULTISAMPLE_NONE) {
        probeOff = true;
        Log("Depth probe off: anti-aliased depth buffer");
        return false;
    }
    if (probeRT) {
        D3DSURFACE_DESC rd;
        probeRT->GetDesc(&rd);
        if (rd.Width != dd.Width || rd.Height != dd.Height) {
            probeRT->Release();
            probeRT = nullptr;
        }
    }
    if (!probeRT && FAILED(dev->CreateRenderTarget(dd.Width, dd.Height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, FALSE, &probeRT))) {
        probeRT = nullptr;
        probeOff = true;
        Log("Depth probe off: no render target");
        return false;
    }
    if (!probeSys && FAILED(dev->CreateImageSurface(kProbeMax, 1, D3DFMT_A8R8G8B8, &probeSys))) {
        probeSys = nullptr;
        probeOff = true;
        Log("Depth probe off: no readback surface");
        return false;
    }
    Flush();
    if (FAILED(dev->SetRenderTarget(probeRT, savedDS))) {   // our colour, the game's depth
        probeOff = true;   // (or the probe's clear and quads went onto the picture: a dark dot under the crosshair)
        Log("Depth probe off: its target doesn't go with the game's depth buffer");
        return false;
    }
    D3DVIEWPORT8 vp = {0, 0, dd.Width, dd.Height, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    D3DRECT rc[kProbeMax];
    for (int i = 0; i < k; i++) {
        LONG x = (LONG)px[i][0], y = (LONG)px[i][1];
        rc[i] = {x, y, x + 1, y + 1};
        probeRects[i] = {x, y, x + 1, y + 1};
    }
    dev->Clear(k, rc, D3DCLEAR_TARGET, 0, 1.0f, 0);   // colour only: the depth stays the game's
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    struct PV {
        float x, y, z, rhw;
        DWORD c;
    };
    static PV v[8 * kProbeN * 6];   // eight pixels a draw
    int n = 0;
    static const int tri[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < k; i++) {
        bool lin = range && range[i][1] > range[i][0] && range[i][0] > 0.01f;
        probeRange[i][0] = lin ? range[i][0] : 0.0f;
        probeRange[i][1] = lin ? range[i][1] : 0.0f;
        for (int j = 0; j < kProbeN; j++) {
            float d = lin ? range[i][0] + (range[i][1] - range[i][0]) * (j + 0.5f) / kProbeN : ProbeDepth(j);
            float z = p10 + p14 / d;
            float x0 = (float)rc[i].x1 - 0.5f, y0 = (float)rc[i].y1 - 0.5f;
            PV q[4] = {{x0, y0, z, 1 / d, 0x00010000}, {x0 + 1, y0, z, 1 / d, 0x00010000},
                       {x0 + 1, y0 + 1, z, 1 / d, 0x00010000}, {x0, y0 + 1, z, 1 / d, 0x00010000}};
            for (int m = 0; m < 6; m++) v[n++] = q[tri[m]];
        }
        if ((i & 7) == 7 || i == k - 1) {
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, n / 3, v, sizeof(PV));
            n = 0;
        }
    }
    probeK = k;
    probePending = true;
    // back to ordinary drawing on the frame
    dev->SetRenderTarget(backbuffer, nullptr);
    D3DVIEWPORT8 bvp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    dev->SetViewport(&bvp);
    SetCommonState();
    curTex = nullptr;
    return true;
}

bool Renderer::ProbeRead(float* dist, int k)
{
    if (!probePending || !probeRT || !probeSys) return false;
    probePending = false;
    if (k > probeK) k = probeK;
    POINT pts[kProbeMax];
    for (int i = 0; i < k; i++) pts[i] = {i, 0};
    if (FAILED(dev->CopyRects(probeRT, probeRects, k, probeSys, pts))) return false;
    D3DLOCKED_RECT lr;
    if (FAILED(probeSys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    for (int i = 0; i < k; i++) {
        int c = (((const DWORD*)lr.pBits)[i] >> 16) & 255;
        float n0 = probeRange[i][0], n1 = probeRange[i][1];
        if (n1 > n0)   // fine range: the surface lies between the last quad in front and the next
            dist[i] = c >= kProbeN ? -1.0f : c == 0 ? -2.0f : n0 + (n1 - n0) * (float)c / kProbeN;
        else
            dist[i] = c >= kProbeN ? -1.0f : c == 0 ? ProbeDepth(0) * 0.8f : sqrtf(ProbeDepth(c - 1) * ProbeDepth(c));
    }
    probeSys->UnlockRect();
    return true;
}

// ------------------------------------------------------------------ (lab) the depth probe, many pixels
bool Renderer::ScanDraw(const float (*px)[2], int k, float p10, float p14, const float (*range)[2])
{
    if (scanOff || !inGameScene || !savedDS || k < 1 || k > kScanMax) return false;
    D3DSURFACE_DESC dd;
    if (FAILED(savedDS->GetDesc(&dd)) || dd.MultiSampleType != D3DMULTISAMPLE_NONE) return false;
    if (scanRT) {
        D3DSURFACE_DESC rd;
        scanRT->GetDesc(&rd);
        if (rd.Width != dd.Width || rd.Height != dd.Height) {
            scanRT->Release();
            scanRT = nullptr;
        }
    }
    if (!scanRT && FAILED(dev->CreateRenderTarget(dd.Width, dd.Height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, FALSE, &scanRT))) {
        scanRT = nullptr;
        scanOff = true;
        Log("lab scan off: no render target");
        return false;
    }
    if (!scanSys && FAILED(dev->CreateImageSurface(kScanMax, 1, D3DFMT_A8R8G8B8, &scanSys))) {
        scanSys = nullptr;
        scanOff = true;
        Log("lab scan off: no readback surface");
        return false;
    }
    Flush();
    if (FAILED(dev->SetRenderTarget(scanRT, savedDS))) {
        scanOff = true;
        Log("lab scan off: its target doesn't go with the game's depth buffer");
        return false;
    }
    D3DVIEWPORT8 vp = {0, 0, dd.Width, dd.Height, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    static D3DRECT rc[kScanMax];
    for (int i = 0; i < k; i++) {
        LONG x = (LONG)px[i][0], y = (LONG)px[i][1];
        rc[i] = {x, y, x + 1, y + 1};
        scanRects[i] = {x, y, x + 1, y + 1};
    }
    dev->Clear(k, rc, D3DCLEAR_TARGET, 0, 1.0f, 0);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    struct PV {
        float x, y, z, rhw;
        DWORD c;
    };
    static PV v[8 * kProbeN * 6];   // eight pixels a draw
    static const int tri[6] = {0, 1, 2, 0, 2, 3};
    int n = 0;
    for (int i = 0; i < k; i++) {
        bool lin = range && range[i][1] > range[i][0] && range[i][0] > 0.01f;
        scanRange[i][0] = lin ? range[i][0] : 0.0f;
        scanRange[i][1] = lin ? range[i][1] : 0.0f;
        for (int j = 0; j < kProbeN; j++) {
            float d = lin ? range[i][0] + (range[i][1] - range[i][0]) * (j + 0.5f) / kProbeN : ProbeDepth(j);
            float z = p10 + p14 / d;
            float x0 = (float)rc[i].x1 - 0.5f, y0 = (float)rc[i].y1 - 0.5f;
            PV q[4] = {{x0, y0, z, 1 / d, 0x00010000}, {x0 + 1, y0, z, 1 / d, 0x00010000},
                       {x0 + 1, y0 + 1, z, 1 / d, 0x00010000}, {x0, y0 + 1, z, 1 / d, 0x00010000}};
            for (int m = 0; m < 6; m++) v[n++] = q[tri[m]];
        }
        if ((i & 7) == 7 || i == k - 1) {
            dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, n / 3, v, sizeof(PV));
            n = 0;
        }
    }
    scanK = k;
    scanPending = true;
    dev->SetRenderTarget(backbuffer, nullptr);
    D3DVIEWPORT8 bvp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    dev->SetViewport(&bvp);
    SetCommonState();
    curTex = nullptr;
    return true;
}

bool Renderer::ScanRead(float* dist, int k)
{
    if (!scanPending || !scanRT || !scanSys) return false;
    scanPending = false;
    if (k > scanK) k = scanK;
    static POINT pts[kScanMax];
    for (int i = 0; i < k; i++) pts[i] = {i, 0};
    if (FAILED(dev->CopyRects(scanRT, scanRects, k, scanSys, pts))) return false;
    D3DLOCKED_RECT lr;
    if (FAILED(scanSys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    for (int i = 0; i < k; i++) {
        int c = (((const DWORD*)lr.pBits)[i] >> 16) & 255;
        float n0 = scanRange[i][0], n1 = scanRange[i][1];
        if (n1 > n0)
            dist[i] = c >= kProbeN ? -1.0f : c == 0 ? -2.0f : n0 + (n1 - n0) * (float)c / kProbeN;
        else
            dist[i] = c >= kProbeN ? -1.0f : c == 0 ? ProbeDepth(0) * 0.8f : sqrtf(ProbeDepth(c - 1) * ProbeDepth(c));
    }
    scanSys->UnlockRect();
    return true;
}

// ------------------------------------------------------------------ a kept patch of the picture
bool Renderer::KeepPatch(int x, int y, int w, int h)
{
    patchOk = false;
    if (patchFailed || !dev) return false;
    IDirect3DSurface8* rt = nullptr;
    if (FAILED(dev->GetRenderTarget(&rt)) || !rt) return false;
    D3DSURFACE_DESC d;
    rt->GetDesc(&d);
    if (x < 0) w += x, x = 0;
    if (y < 0) h += y, y = 0;
    if (x + w > (int)d.Width) w = (int)d.Width - x;
    if (y + h > (int)d.Height) h = (int)d.Height - y;
    if (w <= 0 || h <= 0 || d.MultiSampleType != D3DMULTISAMPLE_NONE) {
        rt->Release();
        return false;
    }
    if (patch && (patchTW < w || patchTH < h || patchFmt != d.Format)) {
        patch->Release();
        patch = nullptr;
    }
    if (!patch) {
        int tw = 64, th = 64;
        while (tw < w) tw *= 2;
        while (th < h) th *= 2;
        if (FAILED(dev->CreateTexture(tw, th, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &patch))) {
            patch = nullptr;
            patchFailed = true;
            Log("KeepPatch: no %dx%d texture (format %d): the radar won't fade", tw, th, d.Format);
            rt->Release();
            return false;
        }
        patchTW = tw, patchTH = th, patchFmt = d.Format;
    }
    IDirect3DSurface8* s = nullptr;
    patch->GetSurfaceLevel(0, &s);
    RECT src = {x, y, x + w, y + h};
    POINT dst = {0, 0};
    HRESULT hr = dev->CopyRects(rt, &src, 1, s, &dst);
    s->Release();
    rt->Release();
    if (FAILED(hr)) {
        static bool logged;
        if (!logged) Log("KeepPatch: CopyRects failed 0x%08lX", hr);
        logged = true;
        return false;
    }
    patchX = x, patchY = y, patchW = w, patchH = h, patchOk = true;
    return true;
}

void Renderer::DrawPatchDisc(float cx, float cy, float r, float feather, float alpha)
{
    if (!patchOk || !patch || alpha <= 0.002f) return;
    Use(patch, BLEND_AVG);
    if (nv + 48 * 9 > (int)(sizeof(verts) / sizeof(verts[0]))) Flush();
    DWORD cin = ((DWORD)(fminf(alpha, 1.0f) * 255.0f + 0.5f) << 24) | 0xFFFFFF, cout = 0x00FFFFFF;
    auto put = [&](float x, float y, DWORD c) {
        // (only inside the kept piece)
        x = fminf(fmaxf(x, (float)patchX), (float)(patchX + patchW));
        y = fminf(fmaxf(y, (float)patchY), (float)(patchY + patchH));
        V& v = verts[nv++];
        v.x = x - 0.5f, v.y = y - 0.5f, v.z = 0.0f, v.rhw = 1.0f, v.c = c;
        v.u = (x - patchX) / (float)patchTW, v.v = (y - patchY) / (float)patchTH;
    };
    const int N = 48;
    for (int i = 0; i < N; i++) {
        float a0 = 6.2831853f * i / N, a1 = 6.2831853f * (i + 1) / N;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        put(cx, cy, cin), put(cx + c0 * r, cy + s0 * r, cin), put(cx + c1 * r, cy + s1 * r, cin);
        float R = r + feather;
        put(cx + c0 * r, cy + s0 * r, cin), put(cx + c0 * R, cy + s0 * R, cout), put(cx + c1 * R, cy + s1 * R, cout);
        put(cx + c0 * r, cy + s0 * r, cin), put(cx + c1 * R, cy + s1 * R, cout), put(cx + c1 * r, cy + s1 * r, cin);
    }
    Flush();
}

// ------------------------------------------------------------------ hint bar
// button badge: short names sit in a round pill, longer ones get room past the caps
float Renderer::BadgeWidth(const char* label) const
{
    float tw = TextWidth('W', label, 0.84f);
    float w = tw + 30.0f * k + fmaxf(0.0f, tw - 30.0f * k) * 0.6f;
    // (three-letter keyboard keys, "TAB", "RMB", spilled over the rounded ends; no pad label has three letters)
    if (strlen(label) == 3) w = fmaxf(w, tw + 58.0f * k);
    return w;
}

void Renderer::HintBar(float cx, float cy, const HintPart* parts, int n, float alpha)
{
    const float badgeGap = 6.0f * k, textGap = 10.0f * k, partGap = 40.0f * k;
    float total = 0;
    for (int i = 0; i < n; i++) {
        for (int b = 0; b < 2 && parts[i].buttons[b]; b++)
            total += BadgeWidth(parts[i].buttons[b]) + (b ? badgeGap : 0);
        total += textGap + TextWidth('W', parts[i].text, 0.86f);
    }
    total += partGap * (n - 1);
    Capsule("bar", cx, cy, total + 70.0f * k, 1.0f, Rgba(1, 1, 1, alpha));
    float x = cx - total / 2;
    for (int i = 0; i < n; i++) {
        for (int b = 0; b < 2 && parts[i].buttons[b]; b++) {
            if (b) x += badgeGap;
            float bw = BadgeWidth(parts[i].buttons[b]);
            Capsule("badge", x + bw / 2, cy, bw, 1.18f, Rgba(1, 1, 1, alpha));
            Text('W', parts[i].buttons[b], x + bw / 2, cy + 9.0f * k, 0.84f, Rgba(1, 0.97f, 0.8f, alpha),
                 Rgba(1, 0.86f, 0.45f, alpha), ALIGN_CENTER, 0.0f);
            x += bw;
        }
        x += textGap;
        Text('W', parts[i].text, x, cy + 9.0f * k, 0.86f, Rgba(1, 1, 1, alpha), Rgba(0.88f, 0.92f, 1, alpha), ALIGN_LEFT,
             0.6f);
        x += TextWidth('W', parts[i].text, 0.86f) + partGap;
    }
}

// ------------------------------------------------------------------ 3D meshes
void Renderer::Begin3D(const float* view, const float* proj, bool depthTest)
{
    Flush();
    ClearGroup();
    dev->SetRenderTarget(backbuffer, depthTest ? savedDS : nullptr);
    D3DVIEWPORT8 vp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    D3DMATRIX id = {};
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    dev->SetTransform(D3DTS_VIEW, (const D3DMATRIX*)view);
    dev->SetTransform(D3DTS_PROJECTION, (const D3DMATRIX*)proj);
    dev->SetTransform(D3DTS_WORLD, &id);
    dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    dev->SetRenderState(D3DRS_ZENABLE, depthTest && savedDS ? D3DZB_TRUE : D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, depthTest && savedDS ? TRUE : FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    curTex = nullptr;
    curBlend = BLEND_OPAQUE;
}

void Renderer::Mesh(const MeshV* v, int nv, const uint16_t* idx, int ni, const float* world)
{
    dev->SetTransform(D3DTS_WORLD, (const D3DMATRIX*)world);
    dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nv, ni / 3, idx, D3DFMT_INDEX16, v, sizeof(MeshV));
}

void Renderer::Clip3D(const float* plane4)
{
    if (plane4) dev->SetClipPlane(0, plane4);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, plane4 ? D3DCLIPPLANE0 : 0);
}

void Renderer::Clip3Dn(const float (*planes)[4], int n)
{
    DWORD bits = 0;
    for (int i = 0; i < n && i < 3; i++) {
        dev->SetClipPlane(i, planes[i]);
        bits |= 1u << i;
    }
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, bits);
}

void Renderer::End3D()
{
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    D3DMATRIX id = {};
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    dev->SetTransform(D3DTS_WORLD, &id);
    dev->SetRenderTarget(backbuffer, nullptr);
    D3DVIEWPORT8 vp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    SetCommonState();
    dev->SetTexture(0, nullptr);
    curTex = nullptr;
}

// ------------------------------------------------------------------ recording (debug "rec")
// the frame at half size (each 2 x 2 block averaged) or every pixel, top row first, appended to f as BGR
bool Renderer::RecordFrame(IDirect3DSurface8* bb, FILE* f, int* ow, int* oh, bool full)
{
    if (!bb || !f) return false;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    if (d.Format != D3DFMT_X8R8G8B8 && d.Format != D3DFMT_A8R8G8B8) return false;
    static IDirect3DSurface8* img;
    static UINT iw, ih;
    if (img && (iw != d.Width || ih != d.Height)) img->Release(), img = nullptr;
    if (!img) {
        if (FAILED(dev->CreateImageSurface(d.Width, d.Height, d.Format, &img))) return false;
        iw = d.Width, ih = d.Height;
    }
    bool ok = false;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(dev->CopyRects(bb, nullptr, 0, img, nullptr)) && SUCCEEDED(img->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        int w = full ? d.Width : d.Width / 2, h = full ? d.Height : d.Height / 2;
        static BYTE* row;
        static int rowCap;
        if (rowCap < w * 3) free(row), row = (BYTE*)malloc(w * 3), rowCap = w * 3;
        for (int y = 0; full && y < h; y++) {
            const BYTE* a = (const BYTE*)lr.pBits + (size_t)y * lr.Pitch;
            for (int x = 0; x < w; x++)
                for (int c = 0; c < 3; c++) row[x * 3 + c] = a[x * 4 + c];
            fwrite(row, 1, w * 3, f);
        }
        for (int y = 0; !full && y < h; y++) {
            const BYTE* a = (const BYTE*)lr.pBits + (size_t)(2 * y) * lr.Pitch;
            const BYTE* b = a + lr.Pitch;
            for (int x = 0; x < w; x++)
                for (int c = 0; c < 3; c++)
                    row[x * 3 + c] = (BYTE)((a[x * 8 + c] + a[x * 8 + 4 + c] + b[x * 8 + c] + b[x * 8 + 4 + c] + 2) >> 2);
            fwrite(row, 1, w * 3, f);
        }
        img->UnlockRect();
        *ow = w, *oh = h;
        ok = true;
    }
    return ok;
}

// ------------------------------------------------------------------ screenshot (debug)
bool Renderer::Screenshot(IDirect3DSurface8* bb, const wchar_t* path)
{
    if (!bb) return false;
    bb->AddRef();
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    IDirect3DSurface8* img = nullptr;
    bool ok = false;
    if (SUCCEEDED(dev->CreateImageSurface(d.Width, d.Height, d.Format, &img))) {
        HRESULT hr = dev->CopyRects(bb, nullptr, 0, img, nullptr);
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(hr) && SUCCEEDED(img->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            FILE* f = _wfopen(path, L"wb");
            if (f && (d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8R8G8B8)) {
                int w = d.Width, h = d.Height, rowb = w * 3, pad = (4 - rowb % 4) % 4;
                BITMAPFILEHEADER fh = {};
                BITMAPINFOHEADER ih = {};
                fh.bfType = 0x4D42;
                fh.bfOffBits = sizeof(fh) + sizeof(ih);
                fh.bfSize = fh.bfOffBits + (rowb + pad) * h;
                ih.biSize = sizeof(ih);
                ih.biWidth = w;
                ih.biHeight = h;
                ih.biPlanes = 1;
                ih.biBitCount = 24;
                fwrite(&fh, sizeof(fh), 1, f);
                fwrite(&ih, sizeof(ih), 1, f);
                BYTE* row = (BYTE*)malloc(rowb + pad);
                memset(row, 0, rowb + pad);
                for (int y = h - 1; y >= 0; y--) {
                    const BYTE* src = (const BYTE*)lr.pBits + (size_t)y * lr.Pitch;
                    for (int x = 0; x < w; x++) {
                        row[x * 3 + 0] = src[x * 4 + 0];
                        row[x * 3 + 1] = src[x * 4 + 1];
                        row[x * 3 + 2] = src[x * 4 + 2];
                    }
                    fwrite(row, 1, rowb + pad, f);
                }
                free(row);
                ok = true;
            } else {
                Log("Screenshot: unsupported back buffer format %d", d.Format);
            }
            if (f) fclose(f);
            img->UnlockRect();
        } else {
            Log("Screenshot copy failed 0x%08lX", hr);
        }
        img->Release();
    }
    bb->Release();
    return ok;
}
