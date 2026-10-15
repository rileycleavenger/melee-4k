#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A custom fighter's stock icon from a render of its head: the head alone,
bright and clean, cut out with a thick outline in a dark shade of its own
colours, at the HD stock icon's 144x144 (build_ui.py writes the game's
24x24 from it and keeps this as its HD replacement).

    .venv/bin/python tools/fighter/stock_icon.py HEAD.png OUT.png
        [--outline 7] [--saturation 1.2] [--contrast 1.08] [--brightness 220]
        [--preview big.png]

HEAD.png: blender_render_ui.py's stock<n>.png (the head alone, three-quarter
on, transparent background).
"""
import argparse

from PIL import Image, ImageEnhance, ImageFilter

SIZE = 144


def outline_colour(rgb, a, radius):
    """Each pixel: the head's colour nearby (blurred, weighted by coverage),
    darkened - the outline is a shade of what it borders."""
    pm = Image.composite(rgb, Image.new("RGB", rgb.size), a).filter(ImageFilter.GaussianBlur(radius))
    w = a.filter(ImageFilter.GaussianBlur(radius))
    out = Image.new("RGB", rgb.size)
    pp, wp, op = pm.load(), w.load(), out.load()
    for y in range(rgb.height):
        for x in range(rgb.width):
            s = 0.16 * 255.0 / max(wp[x, y], 1)
            op[x, y] = tuple(min(255, int(v * s)) for v in pp[x, y])
    return out


def make(src, outline=7, saturation=1.2, contrast=1.08, brightness=220):
    img = Image.open(src).convert("RGBA")
    box = img.getchannel("A").point(lambda v: 255 if v > 24 else 0).getbbox()
    if box is None:
        raise SystemExit(f"{src}: empty")
    img = img.crop(box)
    # The head as big as the outline allows, centred.
    inner = SIZE - 2 * outline - 2
    k = inner / max(img.size)
    img = img.resize((max(1, round(img.width * k)), max(1, round(img.height * k))), Image.LANCZOS)
    head = Image.new("RGBA", (SIZE, SIZE))
    head.alpha_composite(img, ((SIZE - img.width) // 2, (SIZE - img.height) // 2))
    a = head.getchannel("A")
    rgb = head.convert("RGB")
    # Bright like Melee's: the head's highlights (its 90th percentile) lifted
    # to `brightness`, so a dark head is lit without blowing out a pale face.
    lum = sorted(l for l, al in zip(rgb.convert("L").get_flattened_data(), a.get_flattened_data()) if al > 128)
    hi = lum[int(len(lum) * 0.9)] if lum else 255
    if hi < brightness:
        rgb = ImageEnhance.Brightness(rgb).enhance(min(1.8, brightness / max(hi, 1)))
    rgb = ImageEnhance.Contrast(ImageEnhance.Color(rgb).enhance(saturation)).enhance(contrast)
    rgb = rgb.filter(ImageFilter.UnsharpMask(radius=1.2, percent=60, threshold=2))
    # The outline: the cut-out grown by `outline`, a dark shade of the
    # colours it borders; the head on top.
    grown = a.point(lambda v: 255 if v > 40 else 0).filter(ImageFilter.MaxFilter(2 * outline + 1)) \
        .filter(ImageFilter.GaussianBlur(0.6))
    out = Image.new("RGBA", (SIZE, SIZE))
    out.paste(outline_colour(rgb, a, outline * 1.5), (0, 0), grown)
    out.paste(rgb, (0, 0), a)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("head")
    ap.add_argument("out")
    ap.add_argument("--outline", type=int, default=7)
    ap.add_argument("--saturation", type=float, default=1.2)
    ap.add_argument("--contrast", type=float, default=1.08)
    ap.add_argument("--brightness", type=float, default=220)
    ap.add_argument("--preview", help="also write it on a dark ground, the game's 24x24 beside it")
    a = ap.parse_args()
    icon = make(a.head, a.outline, a.saturation, a.contrast, a.brightness)
    icon.save(a.out)
    if a.preview:
        bg = Image.new("RGBA", (SIZE * 2 + 8, SIZE), (40, 40, 60, 255))
        bg.alpha_composite(icon)
        bg.alpha_composite(icon.resize((24, 24), Image.LANCZOS).resize((SIZE, SIZE), Image.NEAREST), (SIZE + 8, 0))
        bg.save(a.preview)


if __name__ == "__main__":
    main()
