"""Leaves blank the pieces of the atlas that are The Simpsons: Hit & Run's own art, so the mod ships
none of the game's files. The mod fills them in at start-up from the player's own copy of the game
(src/gamefiles.cpp), following the lines added to atlas.txt:
    I <sprite> <file>                          a sprite copied as it is from one of the game's PNGs
    T <font> <file> <outline> <top> <bottom>   a font's letters, cut from the game's .p3d font and
                                               given build_assets.py's outline and fill gradient
(<file> is relative to the game folder.)

    python strip_game_art.py <full_dir> <out_dir>
<full_dir> holds the atlas.png / atlas.txt from build_assets.py (game art included)."""
import os, sys

from PIL import Image

IMG = r"art\frontend\scrooby2\resource\images"
FNT = r"art\frontend\scrooby2\resource\fonts"
GAME_IMAGES = dict([(f"digit{i}", rf"{IMG}\letters\{i}.png") for i in range(10)] +
                   [("arrow_l", rf"{IMG}\levelbar\arrow_l.png"), ("arrow_r", rf"{IMG}\levelbar\arrow_r.png"),
                    ("question", rf"{IMG}\letters\yquestion.png")])
# (font, file, outline, gradient top, gradient bottom): as build_assets.py main() builds them
GAME_FONTS = [("B", rf"{FNT}\font0_24.p3d", 3.2, 1.0, 0.80), ("W", rf"{FNT}\font2_12.p3d", 2.2, 1.0, 0.88)]


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    lines = [l.rstrip("\n") for l in open(os.path.join(src, "atlas.txt"))]
    lines = [l for l in lines if not l.startswith(("I ", "T "))]
    atlas = Image.open(os.path.join(src, "atlas.png")).convert("RGBA")
    blank = lambda x, y, w, h: atlas.paste((0, 0, 0, 0), (x, y, x + w, y + h))
    done = set()
    for l in lines:
        p = l.split()
        if p[0] == "S" and p[1] in GAME_IMAGES:
            blank(*map(int, p[2:6]))
            done.add(p[1])
        elif p[0] == "G" and int(p[5]) > 0:   # (G key code x y w h ...)
            blank(*map(int, p[3:7]))
    missing = set(GAME_IMAGES) - done
    if missing:
        sys.exit("not in atlas.txt: " + " ".join(sorted(missing)))
    lines += [f"I {name} {path}" for name, path in GAME_IMAGES.items()]
    lines += [f"T {key} {path} {o!r} {g0!r} {g1!r}" for key, path, o, g0, g1 in GAME_FONTS]
    atlas.save(os.path.join(out, "atlas.png"), optimize=True)
    open(os.path.join(out, "atlas.txt"), "w").write("\n".join(lines) + "\n")
    print(f"blanked {len(done)} sprites and every glyph; {len(GAME_IMAGES)} I + {len(GAME_FONTS)} T lines")


if __name__ == "__main__":
    main()
