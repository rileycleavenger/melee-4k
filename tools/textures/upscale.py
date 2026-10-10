#!/usr/bin/env python3
"""AI-upscale texture dumps into a Dolphin-format replacement pack.

    .venv/bin/python tools/textures/upscale.py DUMP_DIR OUT_DIR [--esrgan .deps/realesrgan]

DUMP_DIR holds aurora's MELEE_TEXTURE_DUMP output (tex1_*.dds, RGBA8). OUT_DIR
gets tex1_*.png at 4x, ready to merge (lowest priority) and encode.

Per texture:
  - colours are bled into fully transparent texels, so the model never sees the
    arbitrary RGB under alpha 0 and edges do not pick up dark halos;
  - the image is padded by wrapping before upscaling and cropped after, so
    repeating textures stay seamless (GameCube stage and model textures mostly
    repeat; for clamped UI art the padding is simply discarded);
  - RGB goes through Real-ESRGAN; alpha goes through it separately as a grey
    image, so cut-out edges stay sharp instead of being Lanczos-soft.

Model by GameCube format: palette and 4-bit intensity formats (C4/C8/C14X2,
I4/IA4) are mostly flat-shaded UI, font and sprite art and use the anime
model; CMPR, direct colour and 8-bit intensity formats (I8/IA8: smoke, cloud
and light gradients, which the anime model turns into sparkles) use the
general model. --model forces one for everything.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texpack import parse_name  # noqa: E402

SCALE = 4
FLAT_FORMATS = {0, 2, 8, 9, 10}  # I4 IA4 C4 C8 C14X2
MODELS = {"flat": "realesrgan-x4plus-anime", "painted": "realesrgan-x4plus"}


def hidden_art(rgba):
    """True when the colour under alpha 0 is a picture rather than filler.
    Melee draws some textures with an alpha test or ignores their alpha (the
    character select slot wipe animates an alpha threshold over art stored
    under alpha 0); that colour is seen, so it must be upscaled, not bled over.
    Filler is one flat colour, or near-black with compression noise (the
    cut-outs: plants, fences, trees), and is bled so edges stay free of dark
    halos; so are an icon's transparent corners, too small to count."""
    hidden = rgba[..., 3] == 0
    if hidden.mean() < 0.25:
        return False
    rgb = rgba[..., :3][hidden].astype(np.float32)
    return float(rgb.std(axis=0).max()) > 8.0 and float(rgb.mean()) > 32.0


def bleed(rgba, iterations=16):
    """Fill RGB under alpha 0 from opaque neighbours (repeated 3x3 dilation),
    unless that colour is art in its own right (hidden_art)."""
    rgb = rgba[..., :3].astype(np.float32)
    known = rgba[..., 3] > 0
    if known.all() or not known.any() or hidden_art(rgba):
        return rgba
    for _ in range(iterations):
        if known.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                k = np.roll(np.roll(known, dy, 0), dx, 1)
                acc += np.roll(np.roll(rgb, dy, 0), dx, 1) * k[..., None]
                cnt += k
        grow = ~known & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow, None]
        known = known | grow
    out = rgba.copy()
    out[..., :3] = np.clip(np.rint(rgb), 0, 255).astype(np.uint8)
    return out


def pad_amount(w, h):
    return max(2, min(16, max(w, h) // 4))


def run_esrgan(esrgan_dir, model, src, dst):
    exe = os.path.join(esrgan_dir, "realesrgan-ncnn-vulkan")
    os.makedirs(dst, exist_ok=True)  # folder mode fails if the output folder is missing
    subprocess.run([exe, "-i", src, "-o", dst, "-n", model, "-s", str(SCALE), "-f", "png",
                    "-m", os.path.join(esrgan_dir, "models")],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dumps")
    ap.add_argument("out")
    ap.add_argument("--esrgan", default=".deps/realesrgan")
    ap.add_argument("--model", choices=sorted(MODELS), help="use one model for every texture")
    args = ap.parse_args()

    if os.path.exists(args.out):
        sys.exit(f"{args.out} exists; pick a new directory")
    os.makedirs(args.out)

    items = []
    for fn in sorted(os.listdir(args.dumps)):
        name = parse_name(fn)
        if name is None or name.mip is not None:
            continue
        kind = args.model or ("flat" if name.fmt in FLAT_FORMATS else "painted")
        items.append((fn, name, kind))

    with tempfile.TemporaryDirectory() as tmp:
        stage = {k: (os.path.join(tmp, f"{k}-rgb"), os.path.join(tmp, f"{k}-a"))
                 for k in MODELS}
        for rgb_dir, a_dir in stage.values():
            os.makedirs(rgb_dir)
            os.makedirs(a_dir)

        meta = {}
        for fn, name, kind in items:
            with Image.open(os.path.join(args.dumps, fn)) as im:
                rgba = np.asarray(im.convert("RGBA"))
            h, w = rgba.shape[:2]
            p = pad_amount(w, h)
            padded = np.pad(bleed(rgba), ((p, p), (p, p), (0, 0)), mode="wrap")
            rgb_dir, a_dir = stage[kind]
            Image.fromarray(padded[..., :3]).save(os.path.join(rgb_dir, name.key + ".png"))
            has_alpha = bool((rgba[..., 3] < 255).any())
            if has_alpha:
                a = padded[..., 3]
                Image.fromarray(np.stack([a, a, a], -1)).save(os.path.join(a_dir, name.key + ".png"))
            meta[name.key] = (kind, p, w, h, has_alpha)

        for kind, (rgb_dir, a_dir) in stage.items():
            for src in (rgb_dir, a_dir):
                if os.listdir(src):
                    run_esrgan(args.esrgan, MODELS[kind], src, src + "-up")
                    print(f"{MODELS[kind]}: {len(os.listdir(src))} images from {os.path.basename(src)}")

        for key, (kind, p, w, h, has_alpha) in meta.items():
            rgb_dir, a_dir = stage[kind]
            box = (p * SCALE, p * SCALE, (p + w) * SCALE, (p + h) * SCALE)
            with Image.open(os.path.join(rgb_dir + "-up", key + ".png")) as im:
                rgb = im.convert("RGB").crop(box)
            if has_alpha:
                with Image.open(os.path.join(a_dir + "-up", key + ".png")) as im:
                    alpha = im.convert("L").crop(box)
            else:
                alpha = Image.new("L", rgb.size, 255)
            out = rgb.copy()
            out.putalpha(alpha)
            out.save(os.path.join(args.out, key + ".png"))

    print(f"{len(meta)} textures upscaled {SCALE}x -> {args.out}")


if __name__ == "__main__":
    main()
