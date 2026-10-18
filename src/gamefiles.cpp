// The game's own art and sounds, read from the player's copy of the game at start-up, so the mod
// ships none of the game's files (assets/strip_game_art.py leaves their places in the atlas blank):
//   - sounds: 16-bit PCM straight out of soundfx.rcf (layout in assets/rcf.py)
//   - sprites: the HUD's PNGs (the gold digits, the arrows, the question mark), copied as they are
//   - fonts: the letters of two .p3d fonts, given build_assets.py's outline and gradient, computed
//     the same way (numpy's float64 maths, its rounding) so they come out as the atlas was built
// PNGs are decoded with GDI+ after their colour-space chunks (gAMA, cHRM, sRGB, iCCP) are dropped:
// GDI+ colour-corrects by them and PIL, which built the atlas, doesn't.
#include "common.h"
#include <objidl.h>
#include <float.h>

// ------------------------------------------------------------------ files
static bool GameDir(wchar_t* out)
{
    if (!GetModuleFileNameW(nullptr, out, MAX_PATH)) return false;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    return true;
}

static FILE* OpenGameFile(const char* rel)
{
    wchar_t path[MAX_PATH * 2];
    if (!GameDir(path)) return nullptr;
    size_t n = wcslen(path);
    MultiByteToWideChar(CP_ACP, 0, rel, -1, path + n, (int)(MAX_PATH * 2 - n));
    return _wfopen(path, L"rb");
}

static BYTE* ReadAll(FILE* f, DWORD* size)
{
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64 * 1024 * 1024) return nullptr;
    BYTE* b = (BYTE*)malloc(n);
    if (b && fread(b, 1, n, f) != (size_t)n) {
        free(b);
        b = nullptr;
    }
    *size = (DWORD)n;
    return b;
}

// ------------------------------------------------------------------ PNG (GDI+)
struct GdipStartupInput { UINT32 version; void* cb; BOOL noThread; BOOL noCodecs; };
struct GdipRect { INT x, y, w, h; };
struct GdipBitmapData { UINT w, h; INT stride; INT format; void* scan0; UINT_PTR reserved; };

static struct {
    HMODULE dll;
    ULONG_PTR token;
    int(WINAPI* fromStream)(IStream*, void**);
    int(WINAPI* getW)(void*, UINT*);
    int(WINAPI* getH)(void*, UINT*);
    int(WINAPI* lock)(void*, const GdipRect*, UINT, INT, GdipBitmapData*);
    int(WINAPI* unlock)(void*, GdipBitmapData*);
    int(WINAPI* dispose)(void*);
    IStream*(WINAPI* memStream)(const BYTE*, UINT);
} P;

void Png_Begin()
{
    if (P.token) return;
    if (!P.dll) {
        P.dll = LoadLibraryA("gdiplus.dll");
        HMODULE sh = LoadLibraryA("shlwapi.dll");
        if (!P.dll || !sh) return;
        P.fromStream = (decltype(P.fromStream))GetProcAddress(P.dll, "GdipCreateBitmapFromStream");
        P.getW = (decltype(P.getW))GetProcAddress(P.dll, "GdipGetImageWidth");
        P.getH = (decltype(P.getH))GetProcAddress(P.dll, "GdipGetImageHeight");
        P.lock = (decltype(P.lock))GetProcAddress(P.dll, "GdipBitmapLockBits");
        P.unlock = (decltype(P.unlock))GetProcAddress(P.dll, "GdipBitmapUnlockBits");
        P.dispose = (decltype(P.dispose))GetProcAddress(P.dll, "GdipDisposeImage");
        P.memStream = (decltype(P.memStream))GetProcAddress(sh, "SHCreateMemStream");
        if (!P.memStream) P.memStream = (decltype(P.memStream))GetProcAddress(sh, (LPCSTR)12);
    }
    typedef int(WINAPI * Startup)(ULONG_PTR*, const GdipStartupInput*, void*);
    Startup startup = P.dll ? (Startup)GetProcAddress(P.dll, "GdiplusStartup") : nullptr;
    GdipStartupInput in = {1, nullptr, FALSE, FALSE};
    if (!startup || !P.fromStream || !P.lock || !P.memStream || startup(&P.token, &in, nullptr) != 0) P.token = 0;
}

void Png_End()
{
    if (!P.token) return;
    typedef void(WINAPI * Shutdown)(ULONG_PTR);
    Shutdown shutdown = (Shutdown)GetProcAddress(P.dll, "GdiplusShutdown");
    if (shutdown) shutdown(P.token);
    P.token = 0;
}

// straight-alpha ARGB pixels (malloc'd), between Png_Begin and Png_End
bool Png_Decode(const BYTE* data, size_t n, uint32_t** pixels, int* w, int* h)
{
    static const BYTE sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    if (!P.token || n < 24 || memcmp(data, sig, 8) || memcmp(data + 12, "IHDR", 4)) return false;
    auto be32 = [&](size_t o) { return (DWORD)data[o] << 24 | (DWORD)data[o + 1] << 16 | (DWORD)data[o + 2] << 8 | data[o + 3]; };
    if (be32(16) == 0 || be32(20) == 0 || be32(16) > 4096 || be32(20) > 4096) return false;   // (the game's are 256 at most)
    // copy without the colour-space chunks
    BYTE* clean = (BYTE*)malloc(n);
    if (!clean) return false;
    memcpy(clean, data, 8);
    size_t o = 8, c = 8;
    while (o + 12 <= n) {
        DWORD len = (DWORD)data[o] << 24 | (DWORD)data[o + 1] << 16 | (DWORD)data[o + 2] << 8 | data[o + 3];
        if (len > n - o - 12) break;
        const BYTE* type = data + o + 4;
        bool drop = !memcmp(type, "gAMA", 4) || !memcmp(type, "cHRM", 4) || !memcmp(type, "sRGB", 4) || !memcmp(type, "iCCP", 4);
        if (!drop) {
            memcpy(clean + c, data + o, len + 12);
            c += len + 12;
        }
        o += len + 12;
        if (!memcmp(type, "IEND", 4)) break;
    }
    bool ok = false;
    IStream* st = P.memStream(clean, (UINT)c);
    free(clean);
    void* bmp = nullptr;
    if (st && P.fromStream(st, &bmp) == 0 && bmp) {
        UINT bw = 0, bh = 0;
        P.getW(bmp, &bw);
        P.getH(bmp, &bh);
        GdipRect r = {0, 0, (INT)bw, (INT)bh};
        GdipBitmapData bd;
        if (bw && bh && P.lock(bmp, &r, 1 /*read*/, 0x26200A /*32bppARGB*/, &bd) == 0) {
            *pixels = (uint32_t*)malloc((size_t)bw * bh * 4);
            if (*pixels) {
                for (UINT y = 0; y < bh; y++)
                    memcpy(*pixels + (size_t)y * bw, (BYTE*)bd.scan0 + (size_t)y * bd.stride, bw * 4);
                *w = (int)bw;
                *h = (int)bh;
                ok = true;
            }
            P.unlock(bmp, &bd);
        }
        P.dispose(bmp);
    }
    if (st) st->Release();
    return ok;
}

bool Png_LoadFile(const wchar_t* path, uint32_t** pixels, int* w, int* h)
{
    FILE* f = _wfopen(path, L"rb");
    if (!f) return false;
    DWORD n = 0;
    BYTE* b = ReadAll(f, &n);
    fclose(f);
    if (!b) return false;
    Png_Begin();
    bool ok = Png_Decode(b, n, pixels, w, h);
    Png_End();
    free(b);
    return ok;
}

// ------------------------------------------------------------------ sounds (soundfx.rcf)
// Each entry: a 2 KB header ("RSD4", "PCM ", channels, bits, sample rate: 1, 16, 24000 for all of
// ours), then the PCM. Index: the entry count at 0x800, the names table's offset at 0x804, then
// (hash, offset, size) records from 0x810; the names 8 bytes into their table (u32 length, the name
// with its NUL, a u32), in the order of the records sorted by offset.
struct RcfRec { DWORD hash, off, size; };
static int CmpOff(const void* a, const void* b)
{
    DWORD x = ((const RcfRec*)a)->off, y = ((const RcfRec*)b)->off;
    return x < y ? -1 : x > y;
}

int GameFile_Sounds(const char* const* names, int count, BYTE** wavs, DWORD* sizes)
{
    for (int i = 0; i < count; i++) wavs[i] = nullptr, sizes[i] = 0;
    FILE* f = OpenGameFile("soundfx.rcf");
    if (!f) {
        Log("Game sounds: soundfx.rcf not found in the game folder");
        return 0;
    }
    const DWORD kHead = 0x100000;
    BYTE* head = (BYTE*)calloc(kHead, 1);
    size_t got = head ? fread(head, 1, kHead, f) : 0;
    int found = 0;
    DWORD n = got >= 0x810 ? *(DWORD*)(head + 0x800) : 0;
    size_t p = got >= 0x810 ? (size_t)*(DWORD*)(head + 0x804) + 8 : 0;
    RcfRec* recs = n && n < 100000 && 0x810 + (size_t)n * 12 <= got ? (RcfRec*)malloc(n * sizeof(RcfRec)) : nullptr;
    if (recs) {
        memcpy(recs, head + 0x810, n * sizeof(RcfRec));
        qsort(recs, n, sizeof(RcfRec), CmpOff);
        for (DWORD i = 0; i < n && p >= 0x810 && p + 4 <= got; i++) {
            DWORD len = *(DWORD*)(head + p);
            if (len > 512 || p + 4 + len + 4 > got) break;
            const char* nm = (const char*)head + p + 4;   // (NUL-terminated within len)
            for (int k = 0; k < count; k++) {
                if (wavs[k] || _strnicmp(nm, names[k], len) || strlen(names[k]) != strnlen(nm, len)) continue;
                DWORD size = recs[i].size;
                if (size <= 2048 || size > 16 * 1024 * 1024 || recs[i].off > 0x7FFFFFFF) continue;
                DWORD pcm = size - 2048, hdr[5];
                BYTE* w = (BYTE*)malloc(44 + pcm);
                if (!w || fseek(f, (long)recs[i].off, SEEK_SET) || fread(hdr, 1, 20, f) != 20 ||
                    memcmp(hdr, "RSD4PCM ", 8) || (hdr[2] != 1 && hdr[2] != 2) || hdr[3] != 16 || hdr[4] < 8000 ||
                    hdr[4] > 48000 || fseek(f, (long)recs[i].off + 2048, SEEK_SET) || fread(w + 44, 1, pcm, f) != pcm) {
                    free(w);   // (not one of the game's plain PCM sounds: a damaged or rebuilt archive)
                    continue;
                }
                const DWORD rate = hdr[4], ch = hdr[2];
                memcpy(w, "RIFF", 4);
                *(DWORD*)(w + 4) = 36 + pcm;
                memcpy(w + 8, "WAVEfmt ", 8);
                *(DWORD*)(w + 16) = 16;
                *(WORD*)(w + 20) = 1;   // PCM
                *(WORD*)(w + 22) = (WORD)ch;
                *(DWORD*)(w + 24) = rate;
                *(DWORD*)(w + 28) = rate * 2 * ch;
                *(WORD*)(w + 32) = (WORD)(2 * ch);
                *(WORD*)(w + 34) = 16;
                memcpy(w + 36, "data", 4);
                *(DWORD*)(w + 40) = pcm;
                wavs[k] = w;
                sizes[k] = 44 + pcm;
                found++;
            }
            p += 4 + len + 4;
        }
    }
    free(recs);
    free(head);
    fclose(f);
    if (found < count) Log("Game sounds: %d of %d found in soundfx.rcf", found, count);
    return found;
}

// ------------------------------------------------------------------ art
static inline uint32_t RdU(const BYTE* b, size_t o) { return *(const uint32_t*)(b + o); }
static inline float RdF(const BYTE* b, size_t o) { return *(const float*)(b + o); }

struct P3dGlyph { uint32_t tex; float blx, bly, trx, try_, lb, rb, w, adv; int code; };
struct P3dFont {
    uint32_t* page[8];
    int pw[8], ph[8], npages;
    P3dGlyph* glyphs;
    int nglyphs;
};

static void FreeFont(P3dFont& f)
{
    for (int i = 0; i < f.npages; i++) free(f.page[i]);
    free(f.glyphs);
    memset(&f, 0, sizeof(f));
}

// chunk header: id, data size, total size (data + children)
static bool Chunk(const BYTE* b, size_t off, size_t end, uint32_t* id, size_t* dsz, size_t* tsz)
{
    if (off + 12 > end) return false;
    *id = RdU(b, off);
    *dsz = RdU(b, off + 4);
    *tsz = RdU(b, off + 8);
    return *tsz >= 12 && *dsz >= 12 && *dsz <= *tsz && *tsz <= end - off;   // (not off + tsz: it wraps)
}

static bool LoadFont(const char* rel, P3dFont* out)
{
    memset(out, 0, sizeof(*out));
    FILE* f = OpenGameFile(rel);
    if (!f) return false;
    DWORD n = 0;
    BYTE* d = ReadAll(f, &n);
    fclose(f);
    if (!d) return false;
    uint32_t id;
    size_t dsz, tsz;
    for (size_t off = 12; Chunk(d, off, n, &id, &dsz, &tsz); off += tsz) {
        if (id != 0x22000) continue;
        for (size_t co = off + dsz, cid_end = off + tsz; ; ) {
            uint32_t cid;
            size_t cd, ct;
            if (!Chunk(d, co, cid_end, &cid, &cd, &ct)) break;
            if (cid == 0x19000) {   // texture > image > image data (a PNG)
                uint32_t iid;
                size_t id_, it;
                for (size_t io = co + cd; Chunk(d, io, co + ct, &iid, &id_, &it); io += it) {
                    uint32_t did;
                    size_t dd, dt;
                    for (size_t dof = io + id_; Chunk(d, dof, io + it, &did, &dd, &dt); dof += dt) {
                        if (did != 0x19002 || out->npages >= 8) continue;
                        const BYTE* blob = d + dof + 12;
                        size_t bn = dt - 12;
                        int pi = out->npages++;   // (a page that won't decode keeps its place, empty)
                        for (size_t k = 0; k + 4 <= bn; k++)
                            if (blob[k] == 0x89 && !memcmp(blob + k + 1, "PNG", 3)) {
                                if (!Png_Decode(blob + k, bn - k, &out->page[pi], &out->pw[pi], &out->ph[pi]))
                                    out->page[pi] = nullptr;
                                break;
                            }
                    }
                }
            } else if (cid == 0x22001 && co + 16 <= n) {   // the glyph list
                uint32_t cnt = RdU(d, co + 12);
                if (cnt < 4096 && 16 + (size_t)cnt * 40 <= ct && !out->glyphs &&
                    (out->glyphs = (P3dGlyph*)malloc((cnt ? cnt : 1) * sizeof(P3dGlyph)))) {
                    for (uint32_t i = 0; i < cnt; i++) {
                        size_t q = co + 16 + (size_t)i * 40;
                        P3dGlyph& g = out->glyphs[i];
                        g.tex = RdU(d, q);
                        g.blx = RdF(d, q + 4), g.bly = RdF(d, q + 8), g.trx = RdF(d, q + 12), g.try_ = RdF(d, q + 16);
                        g.lb = RdF(d, q + 20), g.rb = RdF(d, q + 24), g.w = RdF(d, q + 28), g.adv = RdF(d, q + 32);
                        g.code = (int)RdU(d, q + 36);
                    }
                    out->nglyphs = (int)cnt;
                }
            }
            co += ct;
        }
        break;
    }
    free(d);
    if (out->npages > 0 && out->nglyphs > 0) return true;
    FreeFont(*out);
    return false;
}

// numpy.linspace(start, stop, num)[i]
static double Linspace(double start, double stop, int num, int i)
{
    if (num == 1) return start;
    if (i == num - 1) return stop;
    double step = (stop - start) / (num - 1);
    return (double)i * step + start;
}

// One letter as build_assets.py build_font() draws it: the cell's alpha, padded; a black outline
// (the alpha spread by a disc of the outline's radius); the fill on top with a top-to-bottom gradient.
static bool DrawGlyph(const P3dFont& f, const P3dGlyph& g, double outline, double g0, double g1, uint32_t* atlas, int aw,
                      int ah, int ax, int ay, int expectW, int expectH)
{
    if (g.tex >= (uint32_t)f.npages || !f.page[g.tex]) return false;
    const uint32_t* page = f.page[g.tex];
    int pw = f.pw[g.tex], ph = f.ph[g.tex];
    int x0 = (int)nearbyint((double)g.blx * pw), x1 = (int)nearbyint((double)g.trx * pw);
    int y0 = (int)nearbyint((1.0 - (double)g.try_) * ph), y1 = (int)nearbyint((1.0 - (double)g.bly) * ph);
    if (x1 <= x0 || y1 <= y0) return false;
    int pad = (int)ceil(outline) + 2;
    int cw = x1 - x0, ch = y1 - y0, W = cw + 2 * pad, H = ch + 2 * pad;
    if (W != expectW || H != expectH || ax < 0 || ay < 0 || ax + W > aw || ay + H > ah) return false;
    double* A = (double*)calloc((size_t)W * H, sizeof(double));
    double* O = (double*)calloc((size_t)W * H, sizeof(double));
    if (!A || !O) {
        free(A);
        free(O);
        return false;
    }
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            int px = x0 + x, py = y0 + y;
            uint32_t c = (px >= 0 && py >= 0 && px < pw && py < ph) ? page[(size_t)py * pw + px] : 0;
            A[(size_t)(y + pad) * W + x + pad] = (double)(c >> 24) / 255.0;
        }
    // the offsets numpy.roll shifts by: a grid of steps*2 samples a side, those within the disc
    int steps = (int)ceil(outline) * 2 + 1, num = steps * 2;
    for (int iy = 0; iy < num; iy++) {
        double dy = Linspace(-outline, outline, num, iy);
        for (int ix = 0; ix < num; ix++) {
            double dx = Linspace(-outline, outline, num, ix);
            if (dx * dx + dy * dy > outline * outline) continue;
            int sy = (int)nearbyint(dy), sx = (int)nearbyint(dx);
            for (int y = 0; y < H; y++) {
                int yy = ((y - sy) % H + H) % H;
                for (int x = 0; x < W; x++) {
                    int xx = ((x - sx) % W + W) % W;
                    double v = A[(size_t)yy * W + xx];
                    double& o = O[(size_t)y * W + x];
                    if (v > o) o = v;
                }
            }
        }
    }
    for (int y = 0; y < H; y++) {
        double lum = g0 + (g1 - g0) * Linspace(0.0, 1.0, H, y);
        for (int x = 0; x < W; x++) {
            double a1 = A[(size_t)y * W + x], o = O[(size_t)y * W + x];
            double c = lum * a1;   // premultiplied: black outline (0), then the fill over it
            double a = a1 + o * (1.0 - a1);
            if (a > 1) a = 1;
            double rgb = a > 1e-6 ? c / (a > 1e-6 ? a : 1e-6) : 0.0;
            if (rgb < 0) rgb = 0;
            if (rgb > 1) rgb = 1;
            uint32_t v = (uint32_t)(rgb * 255.0 + 0.5), al = (uint32_t)(a * 255.0 + 0.5);
            atlas[(size_t)(ay + y) * aw + ax + x] = al << 24 | v << 16 | v << 8 | v;
        }
    }
    free(A);
    free(O);
    return true;
}

// Fills the atlas's blank game pieces, as listed in atlas.txt (I and T lines). Returns false if
// something the mod needs couldn't be read (logged).
bool GameArt_Fill(const wchar_t* atlasTxt, uint32_t* px, int aw, int ah)
{
    FILE* f = _wfopen(atlasTxt, L"r");
    if (!f) return false;
    struct Rect { char name[32]; int x, y, w, h; };
    static Rect sprites[128];
    static struct { char key; int code, x, y, w, h; } glyphs[512];
    static struct { char name[32]; char file[200]; } images[32];
    static struct { char key; char file[200]; double outline, g0, g1; } fonts[4];
    int ns = 0, ng = 0, ni = 0, nf = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S' && ns < 128) {
            Rect& r = sprites[ns];
            if (sscanf(line + 2, "%31s %d %d %d %d", r.name, &r.x, &r.y, &r.w, &r.h) == 5) ns++;
        } else if (line[0] == 'G' && ng < 512) {
            auto& g = glyphs[ng];
            if (sscanf(line + 2, "%c %d %d %d %d %d", &g.key, &g.code, &g.x, &g.y, &g.w, &g.h) == 6) ng++;
        } else if (line[0] == 'I' && ni < 32) {
            if (sscanf(line + 2, "%31s %199s", images[ni].name, images[ni].file) == 2) ni++;
        } else if (line[0] == 'T' && nf < 4) {
            auto& t = fonts[nf];
            if (sscanf(line + 2, "%c %199s %lf %lf %lf", &t.key, t.file, &t.outline, &t.g0, &t.g1) == 5) nf++;
        }
    }
    fclose(f);
    if (!ni && !nf) return true;   // an atlas with the art already in it

    // double precision and round-to-nearest for the letters' maths: Direct3D leaves the x87 unit in
    // single precision, which shifted some edge pixels against numpy
    unsigned int fpu = _controlfp(0, 0);
    _controlfp(_PC_53 | _RC_NEAR, _MCW_PC | _MCW_RC);
    Png_Begin();
    int imagesOk = 0, letters = 0, lettersWant = 0;
    bool ok = true;
    for (int i = 0; i < ni; i++) {
        const Rect* r = nullptr;
        for (int s = 0; s < ns; s++)
            if (!strcmp(sprites[s].name, images[i].name)) r = &sprites[s];
        FILE* pf = OpenGameFile(images[i].file);
        DWORD n = 0;
        BYTE* b = pf ? ReadAll(pf, &n) : nullptr;
        if (pf) fclose(pf);
        uint32_t* im = nullptr;
        int w = 0, h = 0;
        if (r && b && Png_Decode(b, n, &im, &w, &h) && w == r->w && h == r->h && r->x >= 0 && r->y >= 0 &&
            r->x + w <= aw && r->y + h <= ah) {
            for (int y = 0; y < h; y++) memcpy(px + (size_t)(r->y + y) * aw + r->x, im + (size_t)y * w, w * 4);
            imagesOk++;
        } else {
            Log("Game art: %s (%s) %s", images[i].name, images[i].file, !b ? "not found" : "doesn't fit");
            ok = false;
        }
        free(im);
        free(b);
    }
    for (int t = 0; t < nf; t++) {
        P3dFont font;
        if (!LoadFont(fonts[t].file, &font)) {
            Log("Game art: font %s not found or unreadable - its text will be missing", fonts[t].file);
            ok = false;
            continue;
        }
        // each letter the atlas has room for (G lines), from the font's glyph with that character code
        // (by code, not position: a release's font with more letters still lines up)
        int bad = 0;
        for (int gi = 0; gi < ng; gi++) {
            auto& row = glyphs[gi];
            if (row.key != fonts[t].key || row.w <= 0) continue;   // another font's, a space or an empty cell
            lettersWant++;
            const P3dGlyph* g = nullptr;
            for (int k = 0; k < font.nglyphs && !g; k++)
                if (font.glyphs[k].code == row.code) g = &font.glyphs[k];
            if (g && DrawGlyph(font, *g, fonts[t].outline, fonts[t].g0, fonts[t].g1, px, aw, ah, row.x, row.y, row.w, row.h))
                letters++;
            else
                bad++;
        }
        if (bad) {
            Log("Game art: font %s doesn't match the atlas (%d letters) - is it a modded font?", fonts[t].file, bad);
            ok = false;
        }
        FreeFont(font);
    }
    Png_End();
    _controlfp(fpu, _MCW_PC | _MCW_RC);
    Log("Game art: %d of %d pictures and %d of %d letters from your copy of the game", imagesOk, ni, letters, lettersWant);
    return ok;
}
