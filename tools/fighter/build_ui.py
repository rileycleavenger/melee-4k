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

In the file each image has the disc's size (icon 64x56, portrait 136x188,
stock 24x24); with --textures each also gets an HD replacement there (icon
320x280, portrait 544x752, stock 144x144, about Melee HD's), which the game
swaps in like the costume files' textures.

With --name, the icon is set on a Melee HD-style plate (the face cut off
by a light frame, the name on a black bar) and each portrait gets Melee's
drop shadow (--plain-icon keeps both as given), and also ftUiBanner (the results screen's winner banner, 512x56)
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
# In the file: the disc's sizes. HD: the replacement each gets in the pack's
# textures folder (--textures), as Melee HD's own menu textures have.
SIZES = {"icon": (64, 56), "csp": (136, 188), "stock": (24, 24), "banner": (512, 56),
         "name": (160, 128)}
HD = {"icon": (320, 280), "csp": (544, 752), "stock": (144, 144), "banner": (1024, 112),
      "name": (320, 256)}
FONTS_ICON = ["/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf", "C:/Windows/Fonts/ARLRDBD.TTF"]
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


def melee_icon(render, name, font_path, size=HD["icon"]):
    """A character select icon in the style of Melee HD's: the face, big
    and cut off by the frame, on a transparent ground (the game draws its
    striped backdrop behind), a light grey rounded frame with a soft inner
    shadow, and the name in widely spaced light capitals on a black bar
    across the bottom quarter. Measured from the HD pack's icons (320x288)."""
    from PIL import Image, ImageDraw, ImageFilter
    w, h = size
    k = w / 320.0
    fw = max(2, round(10 * k))  # frame width
    radius = round(22 * k)
    inner = Image.new("L", size, 0)
    ImageDraw.Draw(inner).rounded_rectangle([fw, fw, w - 1 - fw, h - 1 - fw], max(1, radius - fw), fill=255)
    plate = Image.new("RGBA", size, (0, 0, 0, 0))
    r = render.convert("RGBA")
    box = r.getbbox()
    if box is not None:
        r = r.crop(box)
    # The face fills the window above the bar: as wide as the icon, its
    # bottom (chin, collar) just behind the bar, its top cut by the frame.
    bar0 = round(h * 0.735)
    scale = max(w * 1.08 / r.width, (bar0 + h * 0.1) / r.height)
    r = r.resize((max(1, int(r.width * scale)), max(1, int(r.height * scale))), Image.LANCZOS)
    plate.alpha_composite(r, ((w - r.width) // 2 + round(6 * k), bar0 + round(h * 0.1) - r.height))
    plate.putalpha(Image.composite(plate.getchannel("A"), Image.new("L", size, 0), inner))
    # Inner shadow along the frame.
    shade = Image.new("L", size, 0)
    ImageDraw.Draw(shade).rounded_rectangle([fw, fw, w - 1 - fw, h - 1 - fw], max(1, radius - fw),
                                            outline=170, width=round(6 * k))
    shade = Image.composite(shade.filter(ImageFilter.GaussianBlur(2 * k)), Image.new("L", size, 0), inner)
    plate.alpha_composite(Image.merge("RGBA", (Image.new("L", size, 36),) * 3 + (shade,)))
    # The name bar and the name.
    d = ImageDraw.Draw(plate)
    bar = Image.new("RGBA", size, (0, 0, 0, 0))
    ImageDraw.Draw(bar).rectangle([0, bar0, w, h], fill=(24, 4, 4, 255))
    bar.putalpha(Image.composite(bar.getchannel("A"), Image.new("L", size, 0), inner))
    plate.alpha_composite(bar)
    # Bold: the font's weight plus a stroke, so the name reads at the
    # icon's small size like the HD pack's.
    label = tracked_text(name.upper(), font_path, round(h * 0.19), round(w * 0.05), bold=max(1, round(h * 0.012)))
    if label.width > w - 2 * fw - round(16 * k):
        label = label.resize((w - 2 * fw - round(16 * k), label.height), Image.LANCZOS)
    tint = Image.new("RGBA", label.size, (236, 236, 236, 255))
    tint.putalpha(label)
    plate.alpha_composite(tint, ((w - label.width) // 2, bar0 + (h - fw - bar0 - label.height) // 2))
    # The frame.
    frame = Image.new("L", size, 0)
    ImageDraw.Draw(frame).rounded_rectangle([0, 0, w - 1, h - 1], radius, fill=255)
    frame = Image.composite(Image.new("L", size, 0), frame, inner)
    plate.alpha_composite(Image.merge("RGBA", (Image.new("L", size, 197),) * 3 + (frame,)))
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius, outline=(224, 224, 224, 255), width=max(1, round(2 * k)))
    return plate


def tracked_text(text, font_path, px, tracking, bold=0):
    """Coverage mask of text at px high, letters spaced by tracking, each
    glyph thickened by a bold-pixel stroke."""
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype(font_path, px)
    glyphs = []
    for ch in text:
        box = font.getbbox(ch, stroke_width=bold)
        g = Image.new("L", (max(1, box[2] - box[0]), px + px // 3 + 2 * bold), 0)
        ImageDraw.Draw(g).text((-box[0], 0), ch, font=font, fill=255, stroke_width=bold, stroke_fill=255)
        glyphs.append(g if ch != " " else Image.new("L", (px // 3, px + px // 3), 0))
    out = Image.new("L", (sum(g.width for g in glyphs) + tracking * (len(glyphs) - 1), px + px // 3 + 2 * bold), 0)
    x = 0
    for g in glyphs:
        out.paste(g, (x, 0))
        x += g.width + tracking
    return out.crop(out.getbbox() or (0, 0, 1, 1))


def portrait(render, size=HD["csp"]):
    """The door portrait: the render with Melee's soft drop shadow."""
    from PIL import Image, ImageFilter
    r = render.convert("RGBA")
    if r.size != size:
        r = r.resize(size, Image.LANCZOS)
    k = size[0] / 544.0
    shadow = Image.new("RGBA", size, (0, 0, 0, 0))
    a = r.getchannel("A").point(lambda v: v * 0.55)
    shadow.paste(Image.new("RGBA", size, (8, 6, 20, 255)), (round(14 * k), round(10 * k)),
                 a.filter(ImageFilter.GaussianBlur(6 * k)))
    shadow.alpha_composite(r)
    return shadow


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


def image_desc(w, img, size, hd=None, textures=None):
    """An RGBA8 image of size in the file; with textures, img at hd size is
    also saved there as its HD replacement (named by the small image's key,
    tools/fighter/build_dat.py's texture_key)."""
    from PIL import Image
    big = img.convert("RGBA")
    img = big
    if img.size != size:
        img = img.resize(size, Image.LANCZOS)
    raw = encode_rgba8(img)
    if textures and hd:
        from build_dat import texture_key
        os.makedirs(textures, exist_ok=True)
        if big.size == size:
            # Drawn at the game's size (pixel art, like the stock icons):
            # the HD copy keeps its pixels square and sharp.
            big = big.resize(hd, Image.NEAREST)
        elif big.size != hd:
            big = big.resize(hd, Image.LANCZOS)
        big.save(os.path.join(textures, texture_key(raw, size[0], size[1], GX_TF_RGBA8) + ".png"))
    data = w.blob(raw, 32)
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
    ap.add_argument("--textures", help="the pack's textures folder: HD replacements go there")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    w = hsd.Writer()
    font = a.font or next((f for f in FONTS if os.path.exists(f)), None)
    icon_font = a.font or next((f for f in FONTS_ICON + FONTS if os.path.exists(f)), None)
    icon = Image.open(a.icon)
    if a.name and not a.plain_icon:
        if icon_font is None:
            raise SystemExit("no font for --name; pass --font")
        icon = melee_icon(icon, a.name, icon_font)
    tx = a.textures
    pubs = {"ftUiIcon": image_desc(w, icon, SIZES["icon"], HD["icon"], tx)}
    for i, p in enumerate(a.csp):
        img = Image.open(p) if a.plain_icon else portrait(Image.open(p))
        pubs[f"ftUiCsp{i}"] = image_desc(w, img, SIZES["csp"], HD["csp"], tx)
    for i, p in enumerate(a.stock):
        pubs[f"ftUiStock{i}"] = image_desc(w, Image.open(p), SIZES["stock"], HD["stock"], tx)
    if a.name:
        if font is None:
            raise SystemExit("no font for --name; pass --font")
        pubs["ftUiBanner"] = image_desc(w, text_image(a.name.upper(), HD["banner"], font, True),
                                        SIZES["banner"], HD["banner"], tx)
        pubs["ftUiName"] = image_desc(w, text_image(a.name.upper(), HD["name"], font, False, True),
                                      SIZES["name"], HD["name"], tx)
    data = w.finish(pubs)
    open(a.out, "wb").write(data)
    print(f"{a.out}: {len(pubs)} images, {len(data)} bytes")


if __name__ == "__main__":
    main()
