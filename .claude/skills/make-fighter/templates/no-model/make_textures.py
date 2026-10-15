#!/usr/bin/env python3
"""<NAME>'s painted overlays (copy to ~/Melee4K-dev/fighters/<ID>/ and edit).

RGBA images, transparent where a part's own procedural colour shows (the
build script's paint(..., overlay=...)); they are baked with the rest
(tools/fighter/blender_bake.py), so costumes need none of their own: colours
change through the build's PALETTE. Paint here what is drawn, not modelled:
face details, seams, emblems, irises.

UVs (blender_parts.py loft): u = 0.5 at the part's front, growing toward the
fighter's left (the viewer's right); image row 0 is v = 1 (a head's crown
when its rings run crown to chin with v_range=(1, 0); an eye's front pole).

    REPO/.venv/bin/python make_textures.py OUT_DIR
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

N = 2048


def blank(n=N):
    return Image.new("RGBA", (n, n), (0, 0, 0, 0))


def face(out):
    """The head: a mouth and brows at the front (eyes are modelled)."""
    img = blank()
    d = ImageDraw.Draw(img)
    d.line([(0.47 * N, 0.74 * N), (0.53 * N, 0.74 * N)], fill=(90, 40, 40, 255), width=12)
    for side in (-1, 1):
        x = (0.5 + side * 0.08) * N
        d.line([(x - 0.04 * N, 0.4 * N), (x + 0.04 * N, 0.39 * N)], fill=(60, 40, 30, 255), width=14)
    img.filter(ImageFilter.GaussianBlur(2)).save(os.path.join(out, "face.png"))


def iris(out):
    """An eye ball's front pole (top rows): iris, then pupil."""
    n = 1024
    img = blank(n)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, n, 0.25 * n], fill=(60, 110, 200, 255))
    d.rectangle([0, 0, n, 0.11 * n], fill=(20, 20, 30, 255))
    img.filter(ImageFilter.GaussianBlur(3)).save(os.path.join(out, "iris.png"))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    face(out)
    iris(out)
    print("overlays ->", out)


if __name__ == "__main__":
    main()
