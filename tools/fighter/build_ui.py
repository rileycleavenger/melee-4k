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
"""
import argparse
import struct
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hsd  # noqa: E402

GX_TF_RGBA8 = 6
SIZES = {"icon": (128, 112), "csp": (272, 376), "stock": (64, 64)}


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
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    w = hsd.Writer()
    pubs = {"ftUiIcon": image_desc(w, Image.open(a.icon), SIZES["icon"])}
    for i, p in enumerate(a.csp):
        pubs[f"ftUiCsp{i}"] = image_desc(w, Image.open(p), SIZES["csp"])
    for i, p in enumerate(a.stock):
        pubs[f"ftUiStock{i}"] = image_desc(w, Image.open(p), SIZES["stock"])
    data = w.finish(pubs)
    open(a.out, "wb").write(data)
    print(f"{a.out}: {len(pubs)} images, {len(data)} bytes")


if __name__ == "__main__":
    main()
