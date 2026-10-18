"""
Builds the weapon-wheel sprite atlas from The Simpsons: Hit & Run's own art.

Everything is drawn in the game's UI style: black outlines, the gold "tube" border
(cream highlight on top, gold underneath) used by the menus and level bar, translucent
navy panels like the help-text box, and the game's Boulder / Swiss 721 fonts and
gold digit images.

Output (written to the folder given on the command line):
    atlas.png   RGBA, straight alpha
    atlas.txt   sprite + glyph table read by the DLL
    preview.png a mock-up of the open wheel (for design checks only)

Units: all layout numbers are "reference pixels" for a 1920x1080 screen.
Shapes are rendered at SHAPE_SCALE atlas pixels per reference pixel and
minified (mip-mapped) at runtime, text and game images are stored 1:1.
"""
import io, math, os, struct, sys

import numpy as np
from PIL import Image, ImageFilter

GAME = os.environ.get("SHAR_GAME") or sys.exit("Set SHAR_GAME to your game folder (the one with Simpsons.exe).")
IMAGES = os.path.join(GAME, "art", "frontend", "scrooby2", "resource", "images")
FONTS = os.path.join(GAME, "art", "frontend", "scrooby2", "resource", "fonts")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)

SHAPE_SCALE = 1.5

# ---- layout (reference pixels) - mirrored in wheel.cpp ------------------------
R_OUT = 330.0        # outer radius of the slot ring
R_IN = 150.0         # inner radius of the slot ring
HUB_R = 128.0        # hub circle radius
SEG_GAP = 9.0        # gap between neighbouring slots
SEG_CORNER = 15.0    # slot corner rounding
OUTLINE = 3.0        # black outline
TUBE = 5.5           # gold tube
INNER = 2.0          # black line between tube and fill
NSEG = 8

# ---- palette (sampled from the game's UI pngs) -------------------------------
CREAM = np.array([255, 253, 210]) / 255.0
WHITE = np.array([255, 255, 252]) / 255.0
GOLD_LIGHT = np.array([236, 200, 123]) / 255.0
GOLD = np.array([224, 165, 38]) / 255.0
GOLD_DARK = np.array([150, 95, 0]) / 255.0
NAVY = np.array([3, 15, 75]) / 255.0
INDIGO_DARK = np.array([22, 15, 74]) / 255.0
INDIGO = np.array([40, 23, 141]) / 255.0
BLACK = np.zeros(3)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def cov(d):
    """Anti-aliased coverage of the region d < 0 (d in atlas pixels)."""
    return np.clip(0.5 - d, 0.0, 1.0)


class Canvas:
    """Premultiplied RGBA float canvas."""

    def __init__(self, w, h):
        self.c = np.zeros((h, w, 3))
        self.a = np.zeros((h, w))

    def paint(self, alpha, color):
        alpha = np.clip(alpha, 0, 1)
        color = np.broadcast_to(color, self.c.shape)
        self.c = color * alpha[..., None] + self.c * (1 - alpha[..., None])
        self.a = alpha + self.a * (1 - alpha)

    def image(self):
        a = np.clip(self.a, 0, 1)
        rgb = np.where(a[..., None] > 1e-6, self.c / np.maximum(a[..., None], 1e-6), 0)
        arr = np.dstack([np.clip(rgb, 0, 1), a])
        return Image.fromarray((arr * 255 + 0.5).astype(np.uint8), "RGBA")


def tube_color(t, ny):
    """Gold tube cross-section. t: 0 at the outer edge .. 1 at the inner edge.
    ny: y component of the outward normal (-1 = facing up). Top-lit like the
    game's frames: the upward-facing side of the tube gets the cream highlight."""
    # how much this part of the tube faces the light (up)
    lit = np.clip(-ny, -1, 1)
    # position of the highlight band inside the tube moves with orientation
    centre = 0.35 - 0.2 * lit
    band = np.exp(-((t - centre) / 0.22) ** 2)
    base = GOLD + (GOLD_LIGHT - GOLD) * smooth(-0.6, 0.8, lit)[..., None]
    hi = CREAM + (WHITE - CREAM) * band[..., None]
    k = (band * (0.55 + 0.45 * smooth(-1.0, 0.6, lit)))[..., None]
    col = base * (1 - k) + hi * k
    # darker lip on the shadowed side
    shade = (smooth(0.75, 1.0, t) * smooth(0.0, 1.0, -lit) * 0.35)[..., None]
    return col * (1 - shade) + GOLD_DARK * shade


def framed(cv, d, nx, ny, fill_fn, s, outline=OUTLINE, tube=TUBE, inner=INNER, shadow=True):
    """Paint a SHAR-style framed shape from its signed distance field."""
    O, T, I = outline * s, tube * s, inner * s
    if shadow:
        # soft drop shadow (down-right), drawn first
        sh = Image.fromarray((cov(d - 1.0 * s) * 255).astype(np.uint8), "L")
        sh = sh.filter(ImageFilter.GaussianBlur(5 * s))
        sh = np.asarray(sh, dtype=np.float64) / 255.0
        dx, dy = int(round(3 * s)), int(round(5 * s))
        sh = np.roll(np.roll(sh, dy, axis=0), dx, axis=1)
        cv.paint(sh * 0.55, BLACK)
    cv.paint(cov(d), BLACK)
    t = np.clip((-d - O) / T, 0, 1)
    cv.paint(cov(d + O), tube_color(t, ny))
    cv.paint(cov(d + O + T), BLACK)
    inside = -(d + O + T + I)  # distance inside the fill (atlas px)
    rgb, alpha = fill_fn(inside)
    cv.paint(cov(-inside) * alpha, rgb)


# ---------------------------------------------------------------------------
# signed distance fields
# ---------------------------------------------------------------------------

def seg_sdf(px, py, centre_angle, s, nseg=NSEG):
    """Rounded annular sector (one of nseg round the ring). px,py relative to the wheel centre (atlas px)."""
    r = np.hypot(px, py)
    phi = np.arctan2(py, px) - centre_angle
    phi = (phi + np.pi) % (2 * np.pi) - np.pi
    half = np.pi / nseg
    r0, r1 = R_IN * s, R_OUT * s
    cr = SEG_CORNER * s
    d_r = np.abs(r - (r0 + r1) / 2) - (r1 - r0) / 2
    d_a = r * np.sin(np.clip(np.abs(phi) - half, -np.pi / 2, np.pi / 2)) + SEG_GAP * s / 2
    qx, qy = d_r + cr, d_a + cr
    d = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - cr
    return d, r, phi


def sdf_normal(d):
    gy, gx = np.gradient(d)
    n = np.hypot(gx, gy) + 1e-9
    return gx / n, gy / n


def capsule_sdf(px, py, w, h):
    """Capsule centred at 0 with total width w and height h."""
    r = h / 2
    hx = max(w / 2 - r, 0)
    qx = np.maximum(np.abs(px) - hx, 0)
    return np.hypot(qx, py) - r


# ---------------------------------------------------------------------------
# sprite builders
# ---------------------------------------------------------------------------
sprites = []   # (name, PIL image, anchor_x, anchor_y, scale)


def add(name, im, ax, ay, scale):
    sprites.append((name, im, ax, ay, scale))


def greyed(im, amount=0.92, dark=0.85):
    """Desaturated, slightly darker copy of a straight-alpha RGBA image."""
    a = np.asarray(im).astype(np.float64) / 255.0
    rgb, al = a[..., :3], a[..., 3:]
    lum = (rgb @ np.array([0.299, 0.587, 0.114]))[..., None]
    g = np.clip((rgb + (lum - rgb) * amount) * dark, 0, 1)
    return Image.fromarray((np.dstack([g, al]) * 255 + 0.5).astype(np.uint8), "RGBA")


def build_segments():
    s = SHAPE_SCALE
    margin = 26 * s
    for k in range(5):  # 0=top, 1=top-right ... 4=bottom; 5..7 are mirrors of 3..1
        ca = -np.pi / 2 + k * 2 * np.pi / NSEG
        # bounding box of the sector
        angs = np.linspace(ca - np.pi / NSEG, ca + np.pi / NSEG, 64)
        xs = np.concatenate([np.cos(angs) * R_OUT, np.cos(angs) * R_IN]) * s
        ys = np.concatenate([np.sin(angs) * R_OUT, np.sin(angs) * R_IN]) * s
        x0, x1 = xs.min() - margin, xs.max() + margin
        y0, y1 = ys.min() - margin, ys.max() + margin
        w, h = int(math.ceil(x1 - x0)), int(math.ceil(y1 - y0))
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
        px, py = xx + x0 + 0.5, yy + y0 + 0.5
        d, r, phi = seg_sdf(px, py, ca, s)
        nx, ny = sdf_normal(d)
        u = np.clip((r - R_IN * s) / ((R_OUT - R_IN) * s), 0, 1)  # 0 inner .. 1 outer
        vy = py / (R_OUT * s)  # -1 top of wheel .. +1 bottom

        def fill_normal(inside):
            # translucent navy panel, a bit lighter towards the rim, glossy top
            col = NAVY * (1 - u[..., None] * 0.2) + np.array([0.10, 0.16, 0.42]) * (u[..., None] * 0.55)
            gloss = smooth(9 * s, 0, inside) * 0.0
            # top-lit sheen: brighter on the upper side of the wheel
            sheen = (0.10 * smooth(0.6, -1.0, vy))[..., None]
            col = col + (np.array([0.45, 0.55, 0.95]) - col) * sheen
            # inner shadow just inside the frame
            ish = (0.35 * smooth(7 * s, 0, inside))[..., None]
            col = col * (1 - ish)
            alpha = 0.62 + 0.14 * u + gloss
            return col, alpha

        # vertical position inside this slot's own shape (0 = top .. 1 = bottom)
        inside_mask = d < 0
        ys_in = py[inside_mask]
        sy0, sy1 = ys_in.min(), ys_in.max()
        vloc = (py - sy0) / max(sy1 - sy0, 1)

        def fill_selected(inside):
            # the game's level-bar look: cream upper half, gold lower half,
            # a bright line where they meet, always lit from the top
            split = 0.47
            mix = smooth(0.035, -0.035, vloc - split)[..., None]
            col = GOLD * (1 - mix) + CREAM * mix
            line = np.exp(-((vloc - split + 0.03) / 0.022) ** 2)[..., None] * 0.55
            col = col + (WHITE - col) * line
            # slightly deeper gold at the very bottom
            low = smooth(0.75, 1.0, vloc)[..., None] * 0.18
            col = col * (1 - low) + GOLD_DARK * low
            ish = (0.30 * smooth(6 * s, 0, inside))[..., None]
            col = col * (1 - ish) + GOLD_DARK * ish
            return col, np.ones_like(u) * 0.97

        for state, fn in (("n", fill_normal), ("s", fill_selected)):
            cv = Canvas(w, h)
            framed(cv, d, nx, ny, fn, s)
            im = cv.image()
            add(f"seg{k}{state}", im, -x0, -y0, s)
            # greyed-out twin for slots that are not in the game yet ("l" normal,
            # "k" highlighted); half resolution is plenty for a dimmed panel
            gk = 0.66
            gim = greyed(im, dark=0.80 if state == "n" else 0.86).resize(
                (int(round(w / s * gk)), int(round(h / s * gk))), Image.LANCZOS)
            add(f"seg{k}{'l' if state == 'n' else 'k'}", gim, -x0 / s * gk, -y0 / s * gk, gk)

        # glow halo for the selected slot (low res is fine, it is blurred)
        gs = 0.5
        gw, gh = int(math.ceil(w / s * gs)), int(math.ceil(h / s * gs))
        m = Image.fromarray((cov(d) * 255).astype(np.uint8), "L").resize((gw, gh), Image.LANCZOS)
        m = m.filter(ImageFilter.GaussianBlur(9 * gs))
        a = np.asarray(m, dtype=np.float64) / 255.0
        a = np.clip(a * 1.6, 0, 1)
        rgb = np.dstack([np.full_like(a, 1.0), np.full_like(a, 0.88), np.full_like(a, 0.45)])
        add(f"glow{k}", Image.fromarray((np.dstack([rgb, a]) * 255).astype(np.uint8), "RGBA"), -x0 / s * gs, -y0 / s * gs, gs)


# (v1.0) The wheel as it ships: four weapons round the Weapons page (a quarter each: fists up, the handgun left, the RPG down,
# the shotgun right) and the two items either side of the Items page (a half each: the donut left, the beer right) - the 8-slot
# ring had five "?" slots on Weapons and six on Items. Top-lit like the rest, so each way up is its own sprite; the left ones are
# the right ones mirrored. Drawn at RING_SCALE (they're bigger than the old 45-degree slots and the atlas has 2048 rows)
RING_SCALE = 1.15
RINGS = [("q", 4, [(0, -np.pi / 2), (1, 0.0), (2, np.pi / 2)]),   # quarters: up, right, down (left = right mirrored)
         ("h", 2, [(0, 0.0)])]                                    # halves: right (left = mirrored)


def build_rings():
    s = RING_SCALE
    margin = 26 * s
    for prefix, nseg, ways in RINGS:
        for k, ca in ways:
            half = np.pi / nseg
            angs = np.linspace(ca - half, ca + half, 128)
            xs = np.concatenate([np.cos(angs) * R_OUT, np.cos(angs) * R_IN]) * s
            ys = np.concatenate([np.sin(angs) * R_OUT, np.sin(angs) * R_IN]) * s
            x0, x1 = xs.min() - margin, xs.max() + margin
            y0, y1 = ys.min() - margin, ys.max() + margin
            w, h = int(math.ceil(x1 - x0)), int(math.ceil(y1 - y0))
            yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
            px, py = xx + x0 + 0.5, yy + y0 + 0.5
            d, r, phi = seg_sdf(px, py, ca, s, nseg)
            nx, ny = sdf_normal(d)
            u = np.clip((r - R_IN * s) / ((R_OUT - R_IN) * s), 0, 1)  # 0 inner .. 1 outer
            vy = py / (R_OUT * s)  # -1 top of wheel .. +1 bottom

            def fill_normal(inside):
                col = NAVY * (1 - u[..., None] * 0.2) + np.array([0.10, 0.16, 0.42]) * (u[..., None] * 0.55)
                sheen = (0.10 * smooth(0.6, -1.0, vy))[..., None]
                col = col + (np.array([0.45, 0.55, 0.95]) - col) * sheen
                ish = (0.35 * smooth(7 * s, 0, inside))[..., None]
                col = col * (1 - ish)
                return col, 0.62 + 0.14 * u

            # (selected: the game's level-bar look - cream over gold, a bright line between, lit from the top - over the
            # slot's own height; a half's is tall, so its line sits a little below the middle where its label is)
            inside_mask = d < 0
            ys_in = py[inside_mask]
            sy0, sy1 = ys_in.min(), ys_in.max()
            vloc = (py - sy0) / max(sy1 - sy0, 1)
            split = 0.47 if nseg > 2 else 0.52

            def fill_selected(inside):
                mix = smooth(0.035, -0.035, vloc - split)[..., None]
                col = GOLD * (1 - mix) + CREAM * mix
                line = np.exp(-((vloc - split + 0.03) / 0.022) ** 2)[..., None] * 0.55
                col = col + (WHITE - col) * line
                low = smooth(0.75, 1.0, vloc)[..., None] * 0.18
                col = col * (1 - low) + GOLD_DARK * low
                ish = (0.30 * smooth(6 * s, 0, inside))[..., None]
                col = col * (1 - ish) + GOLD_DARK * ish
                return col, np.ones_like(u) * 0.97

            for state, fn in (("n", fill_normal), ("s", fill_selected)):
                cv = Canvas(w, h)
                framed(cv, d, nx, ny, fn, s)
                add(f"{prefix}{k}{state}", cv.image(), -x0, -y0, s)
            gs = 0.5
            gw, gh = int(math.ceil(w / s * gs)), int(math.ceil(h / s * gs))
            m = Image.fromarray((cov(d) * 255).astype(np.uint8), "L").resize((gw, gh), Image.LANCZOS)
            m = m.filter(ImageFilter.GaussianBlur(9 * gs))
            a = np.clip(np.asarray(m, dtype=np.float64) / 255.0 * 1.6, 0, 1)
            rgb = np.dstack([np.full_like(a, 1.0), np.full_like(a, 0.88), np.full_like(a, 0.45)])
            add(f"{prefix}glow{k}", Image.fromarray((np.dstack([rgb, a]) * 255).astype(np.uint8), "RGBA"), -x0 / s * gs, -y0 / s * gs, gs)


def build_hub():
    s = SHAPE_SCALE
    margin = 24 * s
    R = HUB_R * s
    size = int(math.ceil(2 * (R + margin)))
    c = size / 2
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float64)
    px, py = xx + 0.5 - c, yy + 0.5 - c
    rr = np.hypot(px, py)
    d = rr - R
    nx, ny = sdf_normal(d)

    def fill(inside):
        # glossy indigo sphere like the game's map background
        rn = np.clip(rr / R, 0, 1)
        col = INDIGO * (1 - rn[..., None]) + INDIGO_DARK * rn[..., None]
        col = col * (0.85 + 0.15 * (1 - rn[..., None]))
        # specular cap at the top
        hx, hy = px / R, (py + 0.52 * R) / R
        spec = np.exp(-((hx / 0.62) ** 2 + (hy / 0.30) ** 2) * 2.2) * 0.30
        col = col + (np.array([0.75, 0.80, 1.0]) - col) * spec[..., None]
        ish = (0.40 * smooth(9 * s, 0, inside))[..., None]
        col = col * (1 - ish)
        return col, np.ones_like(rr) * 0.93

    cv = Canvas(size, size)
    framed(cv, d, nx, ny, fill, s, tube=6.0)
    add("hub", cv.image(), c, c, s)


def build_capsules():
    s = SHAPE_SCALE
    specs = [
        # name, height, fill
        ("tabon", 50.0, "gold"),
        ("taboff", 50.0, "navy"),
        ("bar", 46.0, "navy"),
        ("badge", 30.0, "black"),
    ]
    for name, H, kind in specs:
        Wc = H + 2 * 18  # cap piece covers half circle + margin; middle is 1px stretched
        margin = 14 * s
        h = int(math.ceil(H * s + 2 * margin))
        wfull = int(math.ceil((H * 3) * s + 2 * margin))
        yy, xx = np.mgrid[0:h, 0:wfull].astype(np.float64)
        px, py = xx + 0.5 - wfull / 2, yy + 0.5 - h / 2
        d = capsule_sdf(px, py, H * 3 * s, H * s)
        nx, ny = sdf_normal(d)
        vy = py / (H * s / 2)

        if kind == "gold":
            def fill(inside, vy=vy):
                mix = smooth(0.08, -0.08, vy)[..., None]
                col = GOLD * (1 - mix) + CREAM * mix
                ish = (0.30 * smooth(5 * s, 0, inside))[..., None]
                return col * (1 - ish) + GOLD_DARK * ish, np.ones_like(vy)
            o, t, i = OUTLINE, 4.5, 1.5
        elif kind == "navy":
            def fill(inside, vy=vy):
                col = NAVY + (np.array([0.16, 0.22, 0.55]) - NAVY) * smooth(0.9, -0.9, vy)[..., None] * 0.6
                return col, np.ones_like(vy) * 0.72
            o, t, i = 2.5, 3.0, 1.0
        else:
            def fill(inside, vy=vy):
                col = np.array([0.05, 0.05, 0.08]) + np.array([0.18, 0.18, 0.24]) * smooth(0.9, -0.9, vy)[..., None]
                return col, np.ones_like(vy) * 0.95
            o, t, i = 2.0, 2.5, 1.0
        cv = Canvas(wfull, h)
        framed(cv, d, nx, ny, fill, s, outline=o, tube=t, inner=i, shadow=(kind != "black"))
        im = cv.image()
        # caps carry 8px of the straight section; the join with the middle is placed
        # 4px into it (the anchor), so filtering at the seam only sees straight pixels
        c = int(math.ceil(wfull / 2 - H * 1.5 * s + H * s / 2))   # where the straight part starts
        mid = wfull // 2
        left = im.crop((0, 0, c + 8, h))
        right = im.crop((wfull - c - 8, 0, wfull, h))
        # wide strip from the straight part; the DLL samples only its centre column
        middle = im.crop((mid - 8, 0, mid + 8, h))
        add(f"{name}_l", left, c + 4, h / 2, s)
        add(f"{name}_m", middle, 8, h / 2, s)
        add(f"{name}_r", right, 4, h / 2, s)


def build_misc():
    # white square for solid fills
    add("white", Image.new("RGBA", (8, 8), (255, 255, 255, 255)), 4, 4, 1.0)
    # vignette: black, transparent centre, dark corners (stretched to the screen)
    n = 256
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    ux, uy = (xx + 0.5) / n * 2 - 1, (yy + 0.5) / n * 2 - 1
    rr = np.hypot(ux * 1.0, uy * 1.15)
    a = smooth(0.35, 1.35, rr) ** 1.2
    add("vignette", Image.fromarray((np.dstack([np.zeros((n, n, 3)), a]) * 255).astype(np.uint8), "RGBA"), n / 2, n / 2, 1.0)
    # soft round blob
    n = 128
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    rr = np.hypot(xx + 0.5 - n / 2, yy + 0.5 - n / 2) / (n / 2)
    a = np.exp(-(rr ** 2) * 4.0) * (rr < 1)
    add("blob", Image.fromarray((np.dstack([np.ones((n, n, 3)), a]) * 255).astype(np.uint8), "RGBA"), n / 2, n / 2, 1.0)
    # cartoon sparkle (4-point star with a black outline, like the game's collectibles)
    s = 2.0
    n = int(48 * s)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    px, py = (xx + 0.5 - n / 2) / s, (yy + 0.5 - n / 2) / s
    ang = np.arctan2(py, px)
    rad = np.hypot(px, py)
    star_r = 6 + 15 * np.abs(np.cos(2 * ang)) ** 6
    d = (rad - star_r) * s
    cv = Canvas(n, n)
    cv.paint(cov(d - 2.2 * s), BLACK)
    cv.paint(cov(d), np.broadcast_to(WHITE, (n, n, 3)) * 0 + (CREAM * 0.5 + WHITE * 0.5))
    cv.paint(cov(d + 4 * s), WHITE)
    add("sparkle", cv.image(), n / 2, n / 2, s)


def build_game_images():
    """Copy pieces of the game's own HUD art into the atlas."""
    for i in range(10):
        im = Image.open(os.path.join(IMAGES, "letters", f"{i}.png")).convert("RGBA")
        add(f"digit{i}", im, im.width / 2, im.height / 2, 1.0)
    for nm in ("arrow_l", "arrow_r"):
        im = Image.open(os.path.join(IMAGES, "levelbar", f"{nm}.png")).convert("RGBA")
        add(nm, im, im.width / 2, im.height / 2, 1.0)
    # the game's yellow question mark marks slots that are still to come
    im = Image.open(os.path.join(IMAGES, "letters", "yquestion.png")).convert("RGBA")
    add("question", im, im.width / 2, im.height / 2, 1.0)


# ---------------------------------------------------------------------------
# icons and effects, drawn with polygons at 4x and outlined like the game's art
# ---------------------------------------------------------------------------
from PIL import ImageDraw, ImageChops

SS = 4   # supersampling


class Layered:
    """Paint flat-colour shapes at SS x size, then add a black outline around the
    union and shrink. Shapes are given in output pixels."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def _p(self, pts):
        return [(x * SS, y * SS) for x, y in pts]

    def poly(self, pts, col):
        self.d.polygon(self._p(pts), fill=col)

    def ellipse(self, box, col):
        x0, y0, x1, y1 = box
        self.d.ellipse([x0 * SS, y0 * SS, x1 * SS, y1 * SS], fill=col)

    def rrect(self, box, r, col):
        x0, y0, x1, y1 = box
        self.d.rounded_rectangle([x0 * SS, y0 * SS, x1 * SS, y1 * SS], radius=r * SS, fill=col)

    def line(self, pts, col, width):
        self.d.line(self._p(pts), fill=col, width=int(width * SS), joint="curve")

    def finish(self, outline=3.0, shadow=True):
        a = self.img.split()[3]
        rad = int(round(outline * SS))
        dil = a.filter(ImageFilter.MaxFilter(rad * 2 + 1)) if rad > 0 else a
        out = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
        if shadow:
            blur = dil.filter(ImageFilter.GaussianBlur(3 * SS)).point(lambda v: int(v * 0.5))
            sh = Image.new("L", blur.size, 0)
            sh.paste(blur.crop((0, 0, blur.width - 2 * SS, blur.height - 4 * SS)), (2 * SS, 4 * SS))
            out.paste((0, 0, 0, 255), (0, 0), sh)
        black = Image.new("RGBA", self.img.size, (0, 0, 0, 255))
        out = Image.alpha_composite(out, Image.composite(black, Image.new("RGBA", self.img.size, (0, 0, 0, 0)), dil))
        out = Image.alpha_composite(out, self.img)
        return out.resize((self.w, self.h), Image.LANCZOS)


def shade_regions(img, top_col, frac_top):
    """Lighten the top part of every opaque pixel (cartoon two-tone shading)."""
    arr = np.asarray(img, dtype=np.float64)
    h = arr.shape[0]
    yy = np.linspace(0, 1, h)[:, None]
    return arr, yy


def build_icons():
    s = 2  # icons stored at 2x reference size
    YEL, YEL_D, YEL_L = (254, 217, 15, 255), (225, 168, 0, 255), (255, 243, 160, 255)
    # ---- Homer's fist (the unarmed option): knuckles to the right, wrist bottom-left
    W, H = 70 * s, 64 * s
    L = Layered(W, H)
    k = s
    L.rrect((6 * k, 34 * k, 26 * k, 60 * k), 7 * k, YEL_D)                 # wrist
    L.rrect((14 * k, 10 * k, 56 * k, 50 * k), 15 * k, YEL)                  # palm / back of hand
    for i, y in enumerate((12, 23, 34)):                                    # three curled fingers
        L.rrect((40 * k, y * k, 64 * k, (y + 12) * k), 6 * k, YEL)
    L.rrect((20 * k, 6 * k, 52 * k, 20 * k), 7 * k, YEL)                    # thumb across the top
    im = L.finish(outline=2.4)
    # two-tone shading + finger creases
    arr = np.asarray(im, dtype=np.float64)
    ys = np.linspace(0, 1, arr.shape[0])[:, None]
    yellowish = (arr[..., 0] > 200) & (arr[..., 1] > 150) & (arr[..., 2] < 120)
    lower = (ys > 0.62) & yellowish
    arr[lower, :3] = arr[lower, :3] * 0.88
    upper = (ys < 0.3) & yellowish
    arr[upper, :3] = np.minimum(255, arr[upper, :3] * 1.04 + 8)
    im = Image.fromarray(arr.astype(np.uint8), "RGBA")
    d = ImageDraw.Draw(im)
    for y in (23, 34):
        d.line([(44 * k, y * k), (60 * k, y * k)], fill=(120, 80, 0, 255), width=max(1, k))
    add("icon_hand", im, W / 2, H / 2, s)

    # ---- handgun, side view, barrel to the right
    W, H = 96 * s, 64 * s
    L = Layered(W, H)
    GM, GM_L, GM_D = (58, 62, 72, 255), (110, 118, 132, 255), (34, 36, 42, 255)
    WOOD, WOOD_L = (112, 60, 28, 255), (150, 88, 44, 255)
    L.poly([(16 * k, 34 * k), (34 * k, 34 * k), (28 * k, 60 * k), (8 * k, 60 * k)], WOOD)   # grip
    L.rrect((10 * k, 13 * k, 86 * k, 31 * k), 4 * k, GM)                                    # slide
    L.rrect((14 * k, 28 * k, 70 * k, 38 * k), 3 * k, GM_D)                                  # frame
    L.rrect((84 * k, 17 * k, 90 * k, 27 * k), 2 * k, GM_D)                                  # muzzle
    L.rrect((76 * k, 9 * k, 81 * k, 14 * k), 1 * k, GM_D)                                   # front sight
    L.rrect((14 * k, 9 * k, 22 * k, 14 * k), 1 * k, GM_D)                                   # rear sight
    L.poly([(8 * k, 14 * k), (13 * k, 12 * k), (14 * k, 22 * k), (9 * k, 22 * k)], GM_D)   # hammer
    L.line([(34 * k, 37 * k), (36 * k, 47 * k), (52 * k, 47 * k), (54 * k, 37 * k)], GM_D, 2.6 * k)  # trigger guard
    L.poly([(40 * k, 37 * k), (44 * k, 37 * k), (42 * k, 45 * k), (39 * k, 44 * k)], GM_D)  # trigger
    im = L.finish(outline=2.2)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([12 * k, 15 * k, 84 * k, 18 * k], radius=1 * k, fill=GM_L)          # slide highlight
    for x in range(16, 30, 3):                                                               # serrations
        d.line([(x * k, 20 * k), (x * k, 28 * k)], fill=GM_D, width=max(1, k // 2))
    d.polygon([(19 * k, 38 * k), (30 * k, 38 * k), (26 * k, 56 * k), (14 * k, 56 * k)], fill=WOOD_L)  # wood panel
    for i in range(4):
        d.line([(18 * k + i * 3 * k, 42 * k), (16 * k + i * 3 * k, 52 * k)], fill=WOOD, width=max(1, k // 2))
    add("icon_gun", im, W / 2, H / 2, s)

    # ---- rocket launcher (RPG), side view, muzzle to the right: a chunky army-green tube with
    # a flared back end, yellow bands, two grips, a sight, and a fat red rocket in the front
    W, H = 96 * s, 64 * s
    L = Layered(W, H)
    GR, GR_L, GR_D = (104, 128, 50, 255), (150, 178, 82, 255), (64, 82, 30, 255)
    RED, RED_L, RED_D = (222, 52, 30, 255), (255, 128, 86, 255), (150, 26, 16, 255)
    GRIP, METAL = (62, 46, 34, 255), (58, 62, 72, 255)
    L.poly([(3 * k, 15 * k), (13 * k, 21 * k), (13 * k, 39 * k), (3 * k, 45 * k)], GR_D)       # flared back end
    L.rrect((10 * k, 21 * k, 74 * k, 39 * k), 5 * k, GR)                                     # tube
    L.rrect((70 * k, 19 * k, 77 * k, 41 * k), 2 * k, GR_D)                                   # muzzle ring
    L.poly([(76 * k, 22 * k), (84 * k, 19 * k), (93 * k, 26 * k), (95 * k, 30 * k), (93 * k, 34 * k),
            (84 * k, 41 * k), (76 * k, 38 * k)], RED)                                        # rocket nose
    L.rrect((36 * k, 37 * k, 44 * k, 55 * k), 3 * k, GRIP)                                   # trigger grip
    L.rrect((55 * k, 37 * k, 62 * k, 50 * k), 3 * k, GRIP)                                   # front grip
    L.rrect((44 * k, 13 * k, 56 * k, 22 * k), 2 * k, METAL)                                  # sight
    L.line([(44 * k, 39 * k), (45 * k, 46 * k), (50 * k, 46 * k)], METAL, 2.4 * k)            # trigger guard
    im = L.finish(outline=2.2)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([13 * k, 23 * k, 72 * k, 27 * k], radius=2 * k, fill=GR_L)           # tube highlight
    d.rectangle([13 * k, 35 * k, 72 * k, 37 * k], fill=GR_D)                                 # tube shade
    for x0 in (22, 62):                                                                       # yellow bands
        d.rectangle([x0 * k, 21 * k, (x0 + 4) * k, 39 * k], fill=YEL)
        d.rectangle([x0 * k, 21 * k, (x0 + 4) * k, 24 * k], fill=YEL_L)
    d.polygon([(79 * k, 23 * k), (85 * k, 21 * k), (91 * k, 26 * k), (86 * k, 27 * k)], fill=RED_L)   # nose shine
    d.line([(76 * k, 38 * k), (84 * k, 40 * k)], fill=RED_D, width=max(1, k))
    d.rectangle([52 * k, 15 * k, 55 * k, 19 * k], fill=(150, 210, 255, 255))                  # sight glass
    add("icon_rpg", im, W / 2, H / 2, s)

    # ---- pump-action shotgun, side view, muzzle up to the right: a chunky wooden stock with a dark recoil
    # pad, a gunmetal receiver with its ejection port, the barrel over the magazine tube, the ridged
    # wooden pump, a bead sight, the trigger and guard. Drawn long and level, then turned and fitted.
    W, H = 96 * s, 64 * s
    L = Layered(132 * s, 60 * s)
    k = s
    WOOD, WOOD_L, WOOD_D = (156, 84, 34, 255), (204, 130, 66, 255), (100, 50, 20, 255)
    GM, GM_L, GM_D = (62, 66, 78, 255), (128, 136, 152, 255), (30, 32, 40, 255)
    PAD = (38, 32, 30, 255)
    L.poly([(6 * k, 18 * k), (38 * k, 23 * k), (46 * k, 24 * k), (46 * k, 37 * k), (32 * k, 40 * k), (8 * k, 47 * k)], WOOD)  # stock
    L.poly([(2 * k, 17 * k), (8 * k, 18 * k), (10 * k, 47 * k), (4 * k, 49 * k)], PAD)                                       # recoil pad
    L.poly([(38 * k, 33 * k), (48 * k, 32 * k), (45 * k, 49 * k), (36 * k, 48 * k)], WOOD)                                  # grip wrist
    L.rrect((44 * k, 20 * k, 74 * k, 38 * k), 3 * k, GM)                                                                    # receiver
    L.rrect((70 * k, 22 * k, 128 * k, 30 * k), 3 * k, GM)                                                                   # barrel
    L.rrect((70 * k, 30 * k, 118 * k, 36 * k), 3 * k, GM_D)                                                                 # magazine tube
    L.rrect((82 * k, 27 * k, 108 * k, 41 * k), 4 * k, WOOD)                                                                 # pump
    L.rrect((124 * k, 18 * k, 128 * k, 22 * k), 1 * k, GM_L)                                                                # bead sight
    L.line([(51 * k, 38 * k), (52 * k, 46 * k), (64 * k, 46 * k), (65 * k, 38 * k)], GM_D, 3.0 * k)                         # trigger guard
    L.poly([(56 * k, 38 * k), (60 * k, 38 * k), (58 * k, 45 * k), (55 * k, 44 * k)], GM_D)                                  # trigger
    im = L.finish(outline=2.4)
    d = ImageDraw.Draw(im)
    d.polygon([(11 * k, 22 * k), (38 * k, 26 * k), (44 * k, 27 * k), (44 * k, 30 * k), (11 * k, 26 * k)], fill=WOOD_L)      # stock shine
    d.line([(12 * k, 44 * k), (34 * k, 38 * k)], fill=WOOD_D, width=max(1, k))                                              # stock shade
    d.rectangle([47 * k, 22 * k, 72 * k, 24 * k], fill=GM_L)                                                                # receiver highlight
    d.rectangle([52 * k, 26 * k, 64 * k, 31 * k], fill=GM_D)                                                                # ejection port
    d.rectangle([73 * k, 23 * k, 126 * k, 24 * k], fill=GM_L)                                                               # barrel highlight
    d.rectangle([85 * k, 29 * k, 105 * k, 31 * k], fill=WOOD_L)                                                             # pump shine
    for x in range(87, 106, 4):                                                                                              # pump grooves
        d.line([(x * k, 33 * k), (x * k, 39 * k)], fill=WOOD_D, width=max(1, k))
    im = im.rotate(24, resample=Image.BICUBIC, expand=True)
    im = im.crop(im.getbbox())
    f = min((W - 2) / im.width, (H - 2) / im.height)
    im = im.resize((max(1, int(im.width * f)), max(1, int(im.height * f))), Image.LANCZOS)
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    canvas.paste(im, ((W - im.width) // 2, (H - im.height) // 2), im)
    add("icon_shotgun", canvas, W / 2, H / 2, s)

    # ---- the pink sprinkled donut (item 1), seen from a little above: golden dough, the pale fried band round its middle, a
    # thick pink icing with drips down its side, chunky sprinkles, a shine - the hole cut out, outlined like the rest
    W, H = 96 * s, 64 * s
    L = Layered(W, H)
    k = s
    cx, cy, rx, ry = 48 * k, 30 * k, 40 * k, 25 * k   # (v1.0: was cy 34, ry 26 - the underside reached row 130 of 128, cut off)
    DOUGH, DOUGH_D, BAND = (206, 132, 54, 255), (160, 94, 34, 255), (244, 208, 140, 255)
    PINK, PINK_L, PINK_D = (255, 112, 172, 255), (255, 170, 208, 255), (214, 72, 132, 255)
    L.ellipse((cx - rx, cy - ry + 5 * k, cx + rx, cy + ry + 5 * k), DOUGH_D)                     # the dough's underside
    L.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), DOUGH)                                       # the dough
    im = L.finish(outline=2.4)
    d = ImageDraw.Draw(im)
    # the pale fried band round the side
    d.arc([cx - rx + 2 * k, cy - ry + 3 * k, cx + rx - 2 * k, cy + ry + 3 * k], 15, 165, fill=BAND, width=int(3 * k))
    # the icing: a wavy-edged blob over the top, dripping down the front
    import math as _m
    pts = []
    for i in range(72):
        a = 2 * _m.pi * i / 72
        wob = 1.0 + 0.045 * _m.sin(5 * a + 0.8) + 0.03 * _m.sin(11 * a)
        drip = (2.6 * k * max(0.0, _m.sin(a)) * (0.5 + 0.5 * _m.sin(7 * a + 1.1))) if _m.sin(a) > 0 else 0.0
        pts.append((cx + (rx - 3 * k) * wob * _m.cos(a), cy - 2 * k + (ry - 4 * k) * wob * _m.sin(a) + drip))
    d.polygon(pts, fill=PINK)
    d.line(pts + [pts[0]], fill=PINK_D, width=max(1, k))
    # a shine on the icing (upper left)
    d.ellipse([cx - 30 * k, cy - 20 * k, cx - 12 * k, cy - 13 * k], fill=PINK_L)
    # the hole: a dark-rimmed opening showing the dough inside, then cut through
    hx, hy = 13 * k, 8 * k
    d.ellipse([cx - hx - 2 * k, cy - hy - 3 * k, cx + hx + 2 * k, cy + hy + 1 * k], fill=PINK_D)
    d.ellipse([cx - hx, cy - hy - 1 * k, cx + hx, cy + hy + 1 * k], fill=DOUGH_D)
    arr = np.asarray(im).copy()
    yy, xx = np.mgrid[0:H, 0:W]
    hole = ((xx - cx) / (hx * 0.92)) ** 2 + ((yy - (cy + 2 * k)) / (hy * 0.78)) ** 2 < 1.0
    ring = ((xx - cx) / (hx * 0.92 + 2.2 * k)) ** 2 + ((yy - (cy + 2 * k)) / (hy * 0.78 + 2.2 * k)) ** 2 < 1.0
    arr[ring & ~hole] = (0, 0, 0, 255)
    arr[hole] = (0, 0, 0, 0)
    im = Image.fromarray(arr, "RGBA")
    d = ImageDraw.Draw(im)
    # sprinkles: chunky little rods in the show's colours, scattered over the icing
    rs = np.random.RandomState(7)
    SPR = [(232, 40, 44, 255), (255, 222, 40, 255), (44, 120, 240, 255), (60, 196, 76, 255), (255, 252, 240, 255), (255, 140, 30, 255)]
    placed = 0
    for ring, n in ((0.52, 9), (0.80, 14)):   # (two rings, evenly round with a little jitter: clumps read as dirt)
        for i in range(n):
            a = 2 * _m.pi * (i + rs.uniform(-0.3, 0.3)) / n + ring
            rr = ring + rs.uniform(-0.06, 0.06)
            px, py = cx + rr * (rx - 6 * k) * _m.cos(a), cy - 2 * k + rr * (ry - 6 * k) * _m.sin(a)
            ang = rs.uniform(0, _m.pi)
            L2 = 3.4 * k
            x0, y0, x1, y1 = px - L2 * _m.cos(ang), py - L2 * 0.7 * _m.sin(ang), px + L2 * _m.cos(ang), py + L2 * 0.7 * _m.sin(ang)
            d.line([(x0, y0), (x1, y1)], fill=SPR[placed % len(SPR)], width=int(2.2 * k))
            placed += 1
    add("icon_donut", im, W / 2, H / 2, s)

    # ---- a can of beer (item 2), standing a little tilted: red, a white band with a red swoosh, the silver lid and its tab
    sb = 1.25   # (drawn at 1.25 px a reference px, not 2: the atlas is full - at 2 it opened a new row past 2048)
    W, H = int(40 * sb), int(64 * sb)
    L = Layered(W, H)
    k = sb * 0.75
    cx, top, bot, r = 20 * sb, 6 * k, 80 * k, 18 * k
    RED, RED_D, WHITE, SILV, SILV_D = (206, 20, 26, 255), (140, 10, 16, 255), (248, 246, 236, 255), (214, 216, 224, 255), (150, 152, 162, 255)
    L.poly([(cx - r, top + 6 * k), (cx + r, top + 6 * k), (cx + r, bot - 5 * k), (cx - r, bot - 5 * k)], RED)   # the body
    L.ellipse((cx - r, bot - 11 * k, cx + r, bot + 1 * k), SILV_D)                                     # the bottom
    L.ellipse((cx - r * 0.86, top, cx + r * 0.86, top + 12 * k), SILV)                                # the lid
    im = L.finish(outline=2.4)
    d = ImageDraw.Draw(im)
    # the neck's taper and the rim
    d.polygon([(cx - r, top + 8 * k), (cx - r * 0.86, top + 6 * k), (cx + r * 0.86, top + 6 * k), (cx + r, top + 8 * k),
               (cx + r, top + 12 * k), (cx - r, top + 12 * k)], fill=SILV)
    d.ellipse([cx - r * 0.86, top, cx + r * 0.86, top + 12 * k], fill=SILV, outline=SILV_D, width=max(1, int(k)))
    # the opening and the tab on the lid
    d.ellipse([cx + 2 * k, top + 3 * k, cx + 10 * k, top + 7 * k], fill=SILV_D)
    d.rounded_rectangle([cx - 9 * k, top + 4 * k, cx + 1 * k, top + 8 * k], radius=int(2 * k), fill=(236, 238, 244, 255), outline=SILV_D)
    # the white band and the swoosh across it
    by0, by1 = top + 32 * k, top + 54 * k
    d.rectangle([cx - r + 1, by0, cx + r - 1, by1], fill=WHITE)
    import math as _m
    sw = [(cx - r + 1 + i, (by0 + by1) / 2 + 5 * k * _m.sin((i / (2 * r)) * 2 * _m.pi + 0.6)) for i in range(0, int(2 * r) - 1)]
    d.line(sw, fill=RED, width=max(1, int(4 * k)))
    # the bottom's silver lip, the shading down the side, a shine
    d.rectangle([cx - r + 1, bot - 9 * k, cx + r - 1, bot - 5 * k], fill=SILV_D)
    d.rectangle([cx + r * 0.55, top + 13 * k, cx + r - 1, bot - 10 * k], fill=None)
    for x in range(int(cx + r * 0.45), int(cx + r - 1)):   # (the shadowed side)
        for (y0, y1, c) in ((top + 13 * k, by0, RED_D), (by1, bot - 10 * k, RED_D)):
            d.line([(x, y0), (x, y1)], fill=c)
    d.rectangle([cx - r * 0.62, top + 15 * k, cx - r * 0.42, bot - 13 * k], fill=(255, 255, 255, 110))
    add("icon_beer", im, W / 2, H / 2, sb)


def build_fx():
    s = 2
    # ---- muzzle flash: cartoon starburst, white core, yellow, orange rim
    n = 72 * s
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    px, py = (xx + 0.5 - n / 2) / s, (yy + 0.5 - n / 2) / s
    ang = np.arctan2(py, px)
    rad = np.hypot(px, py)
    rng = np.random.RandomState(7)
    spikes = 9
    phase = rng.rand() * 6.28
    jag = 0.62 + 0.38 * np.abs(np.cos(spikes * ang / 2 + phase)) ** 2.2
    jag *= 1 + 0.12 * np.sin(3 * ang + 1.3)
    R = 33.0 * jag
    d = (rad - R) * s
    cv = Canvas(n, n)
    cv.paint(cov(d - 1.6 * s), np.array([0.70, 0.22, 0.0]))
    cv.paint(cov(d), np.array([1.0, 0.55, 0.05]))
    cv.paint(cov(d + 5 * s), np.array([1.0, 0.86, 0.15]))
    cv.paint(cov((rad - R * 0.45) * s), np.array([1.0, 0.98, 0.75]))
    cv.paint(cov((rad - 7.0) * s), np.array([1.0, 1.0, 1.0]))
    add("flash", cv.image(), n / 2, n / 2, s)

    # ---- smoke puff: three soft overlapping blobs with a light grey rim
    n = 64 * s
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    px, py = (xx + 0.5 - n / 2) / s, (yy + 0.5 - n / 2) / s
    d = np.minimum.reduce([np.hypot(px - cx, py - cy) - r for cx, cy, r in ((-9, 4, 13), (8, 3, 14), (0, -8, 14))])
    cv = Canvas(n, n)
    shade = np.clip(0.5 + py / 40, 0, 1)[..., None]
    base = np.array([0.95, 0.95, 0.97]) * (1 - shade * 0.25) + np.array([0.72, 0.72, 0.78]) * shade * 0.25
    cv.paint(cov(d * s) * 0.55, np.array([0.55, 0.56, 0.62]))
    cv.paint(cov((d + 1.8) * s) * 0.9, base)
    add("smoke", cv.image(), n / 2, n / 2, s)

    # ---- bullet hole (a decal laid on walls): a dark ragged core, a scorched halo, thin
    # radial cracks and a few chips. Drawn at the hole's real size in the world.
    n = 48 * s
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    px, py = (xx + 0.5 - n / 2) / s, (yy + 0.5 - n / 2) / s
    ang = np.arctan2(py, px)
    rad = np.hypot(px, py)
    rng = np.random.RandomState(11)
    wob = sum(rng.rand() * 0.12 * np.sin(k * ang + rng.rand() * 6.28) for k in (3, 5, 7, 11))
    cv = Canvas(n, n)
    halo = np.clip(1 - rad / (22.0 * (1 + wob)), 0, 1) ** 1.3
    cv.paint(halo * 0.75, np.array([0.14, 0.12, 0.10]))
    cracks = np.zeros_like(rad)
    for _ in range(6):
        a0 = rng.rand() * 6.28 + 0.15 * np.sin(rad * 0.7)
        length = 13 + rng.rand() * 8
        da = np.abs((ang - a0 + np.pi) % (2 * np.pi) - np.pi)
        taper = np.clip(1 - rad / length, 0, 1)
        width = 0.15 + 0.75 * taper
        cracks = np.maximum(cracks, cov((da * rad - width) * s) * smooth(0.0, 0.25, taper) * (rad > 3))
    cv.paint(cracks * 0.8, np.array([0.08, 0.07, 0.06]))
    for _ in range(6):
        a0, r0 = rng.rand() * 6.28, 12 + rng.rand() * 5
        cx, cy = np.cos(a0) * r0, np.sin(a0) * r0
        cv.paint(cov((np.hypot(px - cx, py - cy) - (0.3 + rng.rand() * 0.5)) * s) * 0.7, np.array([0.12, 0.10, 0.09]))
    core = rad - 10.5 * (1 + 0.4 * wob)
    cv.paint(cov((core - 1.2) * s) * 0.9, np.array([0.30, 0.27, 0.24]))   # chipped rim
    cv.paint(cov(core * s), np.array([0.03, 0.03, 0.03]))
    add("bullethole", cv.image(), n / 2, n / 2, s)

    # ---- tracer streak (horizontal), white core with a warm glow
    w, h = 96, 20
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    u = (xx + 0.5) / w
    v = np.abs((yy + 0.5) / h - 0.5) * 2
    core = np.exp(-(v / 0.22) ** 2) * smooth(0.0, 0.25, u) * smooth(1.0, 0.85, u)
    glow = np.exp(-(v / 0.6) ** 2) * smooth(0.0, 0.5, u) * smooth(1.0, 0.8, u) * 0.6
    a = np.clip(core + glow, 0, 1)
    rgb = np.dstack([np.ones_like(a), 0.85 + 0.15 * core, 0.45 + 0.55 * core])
    add("tracer", Image.fromarray((np.dstack([rgb, a]) * 255).astype(np.uint8), "RGBA"), w / 2, h / 2, 1.0)

    # ---- crosshair: gold tube ring with ticks and a centre dot, black outlines
    s = SHAPE_SCALE
    n = int(96 * s)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    px, py = xx + 0.5 - n / 2, yy + 0.5 - n / 2
    rr = np.hypot(px, py)
    ring = np.abs(rr - 22 * s) - 4.2 * s
    ticks = []
    for ax, ay in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        cxp, cyp = ax * 33 * s, ay * 33 * s
        hw, hh = (7 * s, 2.6 * s) if ax else (2.6 * s, 7 * s)
        qx, qy = np.abs(px - cxp) - hw, np.abs(py - cyp) - hh
        ticks.append(np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - 1.5 * s)
    dot = rr - 3.6 * s
    shape = np.minimum.reduce([ring] + ticks + [dot])
    cv = Canvas(n, n)
    cv.paint(cov(shape - 2.4 * s), BLACK)
    nx, ny = sdf_normal(shape)
    t = np.clip((-shape) / (4.2 * s), 0, 1)
    gold = tube_color(t, ny)
    cv.paint(cov(shape), gold)
    cv.paint(cov(dot), WHITE)
    add("crosshair", cv.image(), n / 2, n / 2, s)


# ---------------------------------------------------------------------------
# fonts (Pure3D texture fonts)
# ---------------------------------------------------------------------------
glyph_rows = []   # text lines for atlas.txt
font_rows = []


def p3dstr(buf, off):
    n = buf[off]
    return buf[off + 1:off + 1 + n].split(b"\0")[0].decode("latin-1"), off + 1 + n


def chunks(buf, off, end):
    while off + 12 <= end:
        cid, dsz, tsz = struct.unpack_from("<III", buf, off)
        yield cid, off, dsz, tsz
        off += tsz


def load_p3d_font(fn):
    data = open(os.path.join(FONTS, fn), "rb").read()
    for cid, off, dsz, tsz in chunks(data, 12, len(data)):
        if cid != 0x22000:
            continue
        p = off + 12 + 4
        name, p = p3dstr(data, p)
        shader, p = p3dstr(data, p)
        size, width, height, baseline = struct.unpack_from("<4f", data, p)
        pages, glyphs = [], []
        for ccid, coff, cdsz, ctsz in chunks(data, off + dsz, off + tsz):
            if ccid == 0x19000:
                for icid, ioff, idsz, itsz in chunks(data, coff + cdsz, coff + ctsz):
                    for dcid, doff, ddsz, dtsz in chunks(data, ioff + idsz, ioff + itsz):
                        if dcid == 0x19002:
                            blob = data[doff + 12:doff + dtsz]
                            pages.append(Image.open(io.BytesIO(blob[blob.find(b"\x89PNG"):])).convert("RGBA"))
            elif ccid == 0x22001:
                n = struct.unpack_from("<I", data, coff + 12)[0]
                q = coff + 16
                for _ in range(n):
                    glyphs.append(struct.unpack_from("<I8fi", data, q))
                    q += 40
        return dict(name=name, size=size, height=height, baseline=baseline, pages=pages, glyphs=glyphs)
    raise RuntimeError("no font in " + fn)


def build_font(key, fn, outline, grad):
    f = load_p3d_font(fn)
    H = f["height"]
    font_rows.append(f"F {key} {H:.2f} {f['baseline']:.2f}")
    pad = int(math.ceil(outline)) + 2
    for tex, blx, bly, trx, try_, lb, rb, w, adv, code in f["glyphs"]:
        if code < 32 or code > 126:
            continue
        page = f["pages"][tex]
        pw, ph = page.size
        x0, x1 = int(round(blx * pw)), int(round(trx * pw))
        y0, y1 = int(round((1 - try_) * ph)), int(round((1 - bly) * ph))
        if x1 <= x0 or y1 <= y0 or code == 32:
            glyph_rows.append((key, code, None, 0, 0, adv))
            continue
        cell = page.crop((x0, y0, x1, y1))
        a = np.asarray(cell, dtype=np.float64)[..., 3] / 255.0
        h, w_ = a.shape
        A = np.zeros((h + 2 * pad, w_ + 2 * pad))
        A[pad:pad + h, pad:pad + w_] = a
        # outline = dilated alpha (disc kernel, anti-aliased via max of shifted copies)
        O = np.zeros_like(A)
        rad = outline
        steps = int(math.ceil(rad)) * 2 + 1
        for dy in np.linspace(-rad, rad, steps * 2):
            for dx in np.linspace(-rad, rad, steps * 2):
                if dx * dx + dy * dy > rad * rad:
                    continue
                sh = np.roll(np.roll(A, int(round(dy)), axis=0), int(round(dx)), axis=1)
                O = np.maximum(O, sh)
        cv = Canvas(A.shape[1], A.shape[0])
        cv.paint(O, BLACK)
        # vertical luminance gradient inside the fill (tinted at runtime)
        yy = np.linspace(0, 1, A.shape[0])[:, None] * np.ones((1, A.shape[1]))
        lum = grad[0] + (grad[1] - grad[0]) * yy
        cv.paint(A, np.dstack([lum, lum, lum]))
        im = cv.image()
        # Every glyph cell is a full line tall (row 0 = top of the line) and the
        # font's baseline is measured from that top. Pen sits on the baseline.
        glyph_rows.append((key, code, im, lb - pad, -f["baseline"] - pad, adv))


# ---------------------------------------------------------------------------
# packing + output
# ---------------------------------------------------------------------------

def pack(items, W, pad=4):
    """Shelf packer with transparent padding so mip levels don't bleed between
    neighbouring sprites. items: list of (key, image). Returns positions."""
    order = sorted(range(len(items)), key=lambda i: -items[i][1].height)
    x = y = 0
    shelf = 0
    pos = {}
    for i in order:
        im = items[i][1]
        w, h = im.width + 2 * pad, im.height + 2 * pad
        w = (w + 3) & ~3
        if x + w > W:
            x = 0
            y += shelf
            shelf = 0
        pos[i] = (x + pad, y + pad)
        x += w
        shelf = max(shelf, (h + 3) & ~3)
    return pos, y + shelf


def main():
    build_rings()   # (v1.0: the 4 + 2 rings; build_segments' 8-slot ring is no longer used)
    build_hub()
    build_capsules()
    build_misc()
    build_game_images()
    build_icons()
    build_fx()
    build_font("B", "font0_24.p3d", outline=3.2, grad=(1.0, 0.80))   # Boulder 48px
    build_font("W", "font2_12.p3d", outline=2.2, grad=(1.0, 0.88))   # Swiss 721 italic 24px

    items = [(s[0], s[1]) for s in sprites]
    gl = [g for g in glyph_rows if g[2] is not None]
    items += [(f"g{g[0]}{g[1]}", g[2]) for g in gl]
    W = 2048
    pos, H = pack(items, W)
    if H > 2048:   # (the shotgun filled 2048 x 2048: the atlas grows to 2048 x 4096 for the items - the mod reads its size from atlas.txt)
        print("atlas: %d px tall" % H)
    Hp = 1
    while Hp < H:
        Hp *= 2
    atlas = Image.new("RGBA", (W, Hp), (0, 0, 0, 0))
    for i, (key, im) in enumerate(items):
        atlas.paste(im, pos[i])
    atlas.save(os.path.join(OUT, "atlas.png"), optimize=True)

    lines = [f"ATLAS {W} {Hp}"]
    for i, (name, im, ax, ay, sc) in enumerate(sprites):
        x, y = pos[i]
        lines.append(f"S {name} {x} {y} {im.width} {im.height} {ax:.2f} {ay:.2f} {sc:.3f}")
    lines += font_rows
    gi = len(sprites)
    for g in glyph_rows:
        key, code, im, xoff, yoff, adv = g
        if im is None:
            lines.append(f"G {key} {code} 0 0 0 0 0 0 {adv:.2f}")
        else:
            x, y = pos[gi]
            gi += 1
            lines.append(f"G {key} {code} {x} {y} {im.width} {im.height} {xoff:.2f} {yoff:.2f} {adv:.2f}")
    open(os.path.join(OUT, "atlas.txt"), "w").write("\n".join(lines) + "\n")
    print(f"atlas {W}x{Hp} (used {H}), {len(sprites)} sprites, {len(gl)} glyphs")


if __name__ == "__main__":
    main()
