#!/usr/bin/env python3
"""Side-by-side review page for upscaled textures.

    .venv/bin/python tools/textures/review.py ORIG_DIR UP_DIR KEYS.txt OUT_DIR [--per-group 120]

ORIG_DIR holds the original textures (extract.py PNGs), UP_DIR the upscaled
ones with the same names, KEYS.txt (from extract.py) maps each key to the
disc file it came from. OUT_DIR/index.html groups them by game area (stages,
fighters, trophies, menus and HUD, effects and items, cutscenes), largest
first, original on the left drawn at the upscaled size with no smoothing.
Thumbnails are written beside it, so the page works offline and is never
published: the textures are game artwork.
"""
import argparse
import html
import os
import sys
from collections import defaultdict

from PIL import Image

GROUPS = [
    ("Stages", ("Gr",)),
    ("Fighters", ("Pl",)),
    ("Trophies", ("Ty",)),
    ("Menus and HUD", ("Mn", "Gm", "If", "Sd", "Nt", "Lb")),
    ("Effects and items", ("Ef", "It")),
    ("Cutscenes", ("Vi", "Mv", "Op")),
]
THUMB = 192


def group_of(path):
    name = os.path.basename(path)
    for title, prefixes in GROUPS:
        if name.startswith(prefixes):
            return title
    return "Other"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("orig")
    ap.add_argument("up")
    ap.add_argument("keys")
    ap.add_argument("out")
    ap.add_argument("--per-group", type=int, default=120)
    args = ap.parse_args()

    source = {}
    for line in open(args.keys):
        key, path = line.rstrip("\n").split("\t")
        source[key] = path
    groups = defaultdict(list)
    for name in os.listdir(args.up):
        key = name.rsplit(".", 1)[0]
        orig = os.path.join(args.orig, key + ".png")
        if not name.endswith(".png") or not os.path.exists(orig):
            continue
        w, h = (int(v) for v in key.split("_")[1].split("x"))
        groups[group_of(source.get(key, ""))].append((w * h, key))

    thumbs = os.path.join(args.out, "img")
    os.makedirs(thumbs, exist_ok=True)
    parts = ["<!doctype html><meta charset=utf-8><title>Texture review</title><style>"
             "body{background:#1b1b22;color:#ddd;font:13px system-ui;margin:16px}"
             "h2{margin-top:28px}.g{display:flex;flex-wrap:wrap;gap:12px}"
             ".t{background:#262630;padding:6px;border-radius:6px}"
             ".t img{width:%dpx;height:auto;background:repeating-conic-gradient(#555 0 25%%,#333 0 50%%)"
             " 0 0/16px 16px}.o{image-rendering:pixelated}.k{font:11px monospace;max-width:%dpx;"
             "overflow-wrap:anywhere;color:#999}</style><h1>Texture review</h1>"
             "<p>Left: original (pixels). Right: upscaled. Largest textures first.</p>"
             % (THUMB, 2 * THUMB + 6)]
    order = [g for g, _ in GROUPS] + ["Other"]
    total = 0
    for title in order:
        items = sorted(groups.get(title, []), reverse=True)[:args.per_group]
        if not items:
            continue
        parts.append(f"<h2>{html.escape(title)} ({len(groups[title])})</h2><div class=g>")
        for _, key in items:
            for side, folder in (("o", args.orig), ("u", args.up)):
                im = Image.open(os.path.join(folder, key + ".png")).convert("RGBA")
                im.thumbnail((THUMB * 2, THUMB * 2),
                             Image.NEAREST if side == "o" else Image.LANCZOS)
                im.save(os.path.join(thumbs, f"{key}.{side}.png"))
            parts.append(f'<div class=t><img class=o src="img/{key}.o.png"> '
                         f'<img src="img/{key}.u.png"><div class=k>{html.escape(key)}<br>'
                         f'{html.escape(source.get(key, "?"))}</div></div>')
            total += 1
        parts.append("</div>")
    with open(os.path.join(args.out, "index.html"), "w") as f:
        f.write("".join(parts))
    print(f"{total} textures -> {os.path.join(args.out, 'index.html')}")


if __name__ == "__main__":
    sys.exit(main())
