#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write a custom fighter's menu images (Pl<id>Ui.dat).

    .venv/bin/python tools/fighter/build_ui.py --icon icon.png \\
        --csp csp0.png csp1.png ... --stock stock0.png stock1.png ... --out pack/PlGeUi.dat

An HSD archive of image descriptors (HSD_ImageDesc, GX RGBA8), public
symbols ftUiIcon (character select grid icon), ftUiCsp<n> (the portrait in
a player's door, one per costume) and ftUiStock<n> (stock icon, one per
costume). The game loads it with lbArchive_LoadSymbols into the menu's own
heap (src/melee/mn/mncharsel.c, src/melee/if/ifstock.c) and points the
corresponding texture objects at these images.

Sizes are twice the disc's, the shapes the game draws them at: icon 128x112
(64x56), portrait 272x376 (136x188), stock 64x64 (24x24 drawn).

With --name, the icon is set on a Melee-style plate (striped background,
the name on a dark bar; --plain-icon keeps it as given), and also ftUiBanner (the results screen's winner banner, 512x56)
and ftUiName (a results panel's name plate, 160x128): white letters whose
alpha is their coverage, which the game tints as it does its own
intensity-only name textures.
"""
import argparse
import struct
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hsd  # noqa: E402

GX_TF_RGBA8 = 6
SIZES = {"icon": (128, 112), "csp": (272, 376), "stock": (64, 64), "banner": (512, 56),
         "name": (160, 128)}
FONTS = ["/System/Library/Fonts/Supplemental/Arial Black.ttf",
         "/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf",
         "C:/Windows/Fonts/ariblk.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]


def text_image(text, size, font_path, italic, fill=False):
    """White text on transparent, centred: as large as fits, or with `fill`
    stretched to fill the image (the game's own name plates are)."""
    from PIL import Image, ImageDraw, ImageFont
    w, h = size
    big = Image.new("L", (w * 2, h * 2), 0)
    d = ImageDraw.Draw(big)
    pt = h * 2
    while pt > 4:
        font = ImageFont.truetype(font_path, pt)
        box = d.textbbox((0, 0), text, font=font)
        if box[2] - box[0] <= w * 2 * 0.94 and box[3] - box[1] <= h * 2 * 0.86:
            break
        pt -= 2
    x = (w * 2 - (box[2] - box[0])) // 2 - box[0]
    y = (h * 2 - (box[3] - box[1])) // 2 - box[1]
    d.text((x, y), text, font=font, fill=255)
    if italic:
        big = big.transform(big.size, Image.AFFINE, (1, 0.22, -0.11 * h * 2, 0, 1, 0),
                            Image.BICUBIC)
    if fill:
        box = big.getbbox()
        if box is not None:
            text = big.crop(box)
            big = Image.new("L", big.size, 0)
            tw, th = int(big.size[0] * 0.92), int(big.size[1] * 0.8)
            big.paste(text.resize((tw, th), Image.LANCZOS),
                      ((big.size[0] - tw) // 2, (big.size[1] - th) // 2))
    a = big.resize(size, Image.LANCZOS)
    out = Image.new("RGBA", size, (255, 255, 255, 0))
    out.putalpha(a)
    return out


def melee_icon(render, name, font_path, size=SIZES["icon"]):
    """A character select icon in Melee's style: the render over a slate
    striped plate, the name in light letters on a dark bar along the
    bottom, inside a light rounded frame (all part of Melee's own icon
    textures)."""
    from PIL import Image, ImageDraw
    w, h = size
    plate = Image.new("RGBA", size)
    d = ImageDraw.Draw(plate)
    for y in range(h):
        t = y / (h - 1)
        c = [int(a + (b - a) * t) for a, b in zip((66, 82, 108), (30, 40, 56))]
        if y % 4 >= 2:  # Melee's faint horizontal lines
            c = [int(v * 0.86) for v in c]
        d.line([(0, y), (w, y)], fill=tuple(c) + (255,))
    r = render.convert("RGBA")
    box = r.getbbox()
    if box is not None:
        r = r.crop(box)
    scale = w * 1.05 / r.width
    r = r.resize((max(1, int(r.width * scale)), max(1, int(r.height * scale))), Image.LANCZOS)
    plate.alpha_composite(r, ((w - r.width) // 2, 0))  # head at the top, body cut by the bar
    m = max(2, w // 40)  # frame width
    bar = int(h * 0.22)
    d.rectangle([m, h - bar - m, w - m, h - m], fill=(16, 4, 4, 255))
    label = text_image(name.upper(), (int(w * 0.9), int(bar * 0.9)), font_path, False)
    label = Image.composite(Image.new("RGBA", label.size, (200, 196, 196, 255)),
                            Image.new("RGBA", label.size, (0, 0, 0, 0)), label.getchannel("A"))
    plate.alpha_composite(label, ((w - label.width) // 2, h - bar - m + (bar - label.height) // 2))
    # The frame: a light rounded border, transparent outside its corners.
    radius = w // 12
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], radius, fill=255)
    ImageDraw.Draw(plate).rounded_rectangle([0, 0, w - 1, h - 1], radius, outline=(200, 200, 204, 255),
                                            width=m)
    ImageDraw.Draw(plate).rounded_rectangle([m, m, w - 1 - m, h - 1 - m], max(1, radius - m),
                                            outline=(60, 60, 66, 255), width=1)
    plate.putalpha(Image.composite(plate.getchannel("A"), mask, mask))
    return plate


def encode_rgba8(img):
    """GX RGBA8: 4x4 tiles, each 32 bytes of A,R pairs then 32 of G,B."""
    w, h = img.size
    px = img.load()
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            ar, gb = bytearray(), bytearray()
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    r, g, b, a = px[min(x, w - 1), min(y, h - 1)]
                    ar += bytes((a, r))
                    gb += bytes((g, b))
            out += ar + gb
    return bytes(out)


def image_desc(w, img, size):
    from PIL import Image
    img = img.convert("RGBA")
    if img.size != size:
        img = img.resize(size, Image.LANCZOS)
    data = w.blob(encode_rgba8(img), 32)
    d = w.alloc(0x18)
    w.ptr(d, data)
    w.put(d + 4, "HHIIff", size[0], size[1], GX_TF_RGBA8, 0, 0.0, 0.0)
    return d


def main():
    from PIL import Image
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--icon", required=True)
    ap.add_argument("--csp", nargs="+", required=True)
    ap.add_argument("--stock", nargs="+", required=True)
    ap.add_argument("--name", help="the fighter's name, for the results screen")
    ap.add_argument("--font", help="TrueType font for --name (default: Arial Black or similar)")
    ap.add_argument("--plain-icon", action="store_true",
                    help="use --icon as it is (by default, with --name, it is set on a Melee-style "
                         "plate with the name)")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    w = hsd.Writer()
    font = a.font or next((f for f in FONTS if os.path.exists(f)), None)
    icon = Image.open(a.icon)
    if a.name and not a.plain_icon:
        if font is None:
            raise SystemExit("no font for --name; pass --font")
        icon = melee_icon(icon, a.name, font)
    pubs = {"ftUiIcon": image_desc(w, icon, SIZES["icon"])}
    for i, p in enumerate(a.csp):
        pubs[f"ftUiCsp{i}"] = image_desc(w, Image.open(p), SIZES["csp"])
    for i, p in enumerate(a.stock):
        pubs[f"ftUiStock{i}"] = image_desc(w, Image.open(p), SIZES["stock"])
    if a.name:
        if font is None:
            raise SystemExit("no font for --name; pass --font")
        pubs["ftUiBanner"] = image_desc(w, text_image(a.name.upper(), SIZES["banner"], font, True),
                                        SIZES["banner"])
        pubs["ftUiName"] = image_desc(w, text_image(a.name.upper(), SIZES["name"], font, False, True),
                                      SIZES["name"])
    data = w.finish(pubs)
    open(a.out, "wb").write(data)
    print(f"{a.out}: {len(pubs)} images, {len(data)} bytes")


if __name__ == "__main__":
    main()
