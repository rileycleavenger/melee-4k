#!/usr/bin/env python3
"""<NAME>'s textures (copy to ~/Melee4K-dev/fighters/<ID>/ and edit).

One PNG per material, 2048 px for big parts (the game gets a 128 px copy in
the .dat and this file as its HD replacement). Faces, emblems and seams are
painted here, not modelled. UVs (blender_parts.py): u = 0.5 at the front,
image y = (1 - v) * N.

Costumes: COSTUMES maps each costume suffix to the colours it changes;
every image a costume changes is written as <image>_<suffix>.png (the
default costume, first, without a suffix), and make_pack.sh points the
materials at them.

    REPO/.venv/bin/python make_textures.py OUT_DIR
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter  # noqa: F401

REPO = os.environ.get("REPO", os.path.expanduser("~/Desktop/GitRepos/melee-4k"))
sys.path.insert(0, os.path.join(REPO, "tools", "fighter"))
from texgen import N, fabric, flat, wood  # noqa: E402,F401

# suffix: (main colour, accent colour). First is the default.
COSTUMES = {
    "Nr": ((44, 92, 214), (242, 190, 46)),
    "Re": ((204, 44, 40), (242, 190, 46)),
    "Gr": ((44, 150, 64), (242, 190, 46)),
}


def face(out):
    """The head: base material plus painted eyes and mouth at the front."""
    img = flat((230, 190, 160)).convert("RGBA")
    d = ImageDraw.Draw(img)
    for side in (-1, 1):  # eyes either side of u = 0.5, a little above the middle
        cx, cy, r = int((0.5 + side * 0.08) * N), int(0.47 * N), int(0.06 * N)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(250, 250, 250, 255), outline=(40, 30, 30, 255), width=10)
        d.ellipse([cx - r // 2, cy - r // 2, cx + r // 2, cy + r // 2], fill=(40, 30, 30, 255))
    d.line([(int(0.47 * N), int(0.6 * N)), (int(0.53 * N), int(0.6 * N))], fill=(90, 40, 40, 255), width=12)
    img.save(os.path.join(out, "head.png"))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    face(out)
    for name, (main_c, accent) in COSTUMES.items():
        sfx = "" if name == "Nr" else "_" + name.lower()
        fabric(main_c).save(os.path.join(out, f"cloth{sfx}.png"))
        flat(accent).save(os.path.join(out, f"accent{sfx}.png"))
    print("textures ->", out)


if __name__ == "__main__":
    main()
