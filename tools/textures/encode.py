#!/usr/bin/env python3
"""Re-encode a texture pack as BC7 DDS with full mip chains.

    .venv/bin/python tools/textures/encode.py IN_DIR OUT_DIR [--max-scale 6] [--max-dim 4096]

Needs Pillow, numpy and etcpak (`python3 -m venv .venv && .venv/bin/pip
install Pillow numpy etcpak`).

Why: PNG replacements load as a single level, so a 3x-16x texture on a floor
seen edge-on is minified with no mips and shimmers into noise. Uncompressed
the merged community pack is ~6 GB of texture memory; BC7 with mips is ~1.5.

Per texture: the target size is the source size capped at --max-scale times
the GameCube size and --max-dim on the long side, rounded to a multiple of 4
(block-compressed levels must be block aligned), coarser for big textures
so the mip aurora streams in first as a placeholder is aligned too. Mips are built down to
1x1 from premultiplied alpha, so transparent texels do not darken the edges
around them. The colour under (near-)zero alpha is kept from a straight-alpha
resize instead of the premultiplied one, which would make it black: Melee
draws some textures with an alpha test or ignores their alpha (the character
select slot wipe animates an alpha threshold over art stored under alpha 0),
so that colour is visible. Every level is BC7-encoded (levels under 4x4 are padded by
edge replication). Mip sidecars in the input are ignored; the chain is
regenerated. OUT_DIR must not exist.
"""
import argparse
import json
import multiprocessing
import os
import shutil
import struct
import sys
import time

import etcpak
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texpack import parse_name  # noqa: E402

DXGI_BC7_UNORM = 98
THUMB_DIM = 64  # gx::texture::ReplacementThumbnailDim


def target_size(w, h, gc_w, gc_h, max_scale, max_dim):
    scale = min(w / gc_w, h / gc_h, max_scale)
    tw, th = gc_w * scale, gc_h * scale
    if max(tw, th) > max_dim:
        f = max_dim / max(tw, th)
        tw, th = tw * f, th * f
    # Round so the chain stays block aligned down to the first level that fits
    # THUMB_DIM: aurora streams that level as a placeholder texture of its own.
    k = 0
    while max(tw, th) / (1 << k) > THUMB_DIM:
        k += 1
    align = 4 << k
    tw = max(align, int(round(tw / align)) * align)
    th = max(align, int(round(th / align)) * align)
    return tw, th


ALPHA_FADE = 16.0  # below this alpha, colour comes from the straight resize


def premultiply(rgba):
    a = rgba[..., 3:4].astype(np.float32) / 255.0
    out = rgba.astype(np.float32)
    out[..., :3] *= a
    return out


def unpremultiply(f):
    a = f[..., 3:4] / 255.0
    rgb = np.where(a > 0, f[..., :3] / np.maximum(a, 1e-6), 0)
    out = np.concatenate([rgb, f[..., 3:4]], axis=-1)
    return np.clip(np.rint(out), 0, 255).astype(np.uint8)


def resize_premultiplied(premul, w, h):
    """Lanczos-resize a float32 RGBA array (premultiplied or not) channel by channel."""
    chans = [np.asarray(Image.fromarray(premul[..., c], mode="F").resize((w, h), Image.LANCZOS))
             for c in range(4)]
    return np.clip(np.stack(chans, axis=-1), 0, 255 * 1.0)


def merge_levels(premul, straight):
    """RGBA8 from the premultiplied level, with the straight level's colour
    faded in where alpha nears zero (where unpremultiplying loses it)."""
    out = unpremultiply(premul).astype(np.float32)
    weight = np.clip(premul[..., 3:4] / ALPHA_FADE, 0, 1)
    out[..., :3] = out[..., :3] * weight + straight[..., :3] * (1 - weight)
    return np.clip(np.rint(out), 0, 255).astype(np.uint8)


def bc7_level(rgba):
    h, w = rgba.shape[:2]
    pw, ph = (w + 3) & ~3, (h + 3) & ~3
    if (pw, ph) != (w, h):
        rgba = np.pad(rgba, ((0, ph - h), (0, pw - w), (0, 0)), mode="edge")
    return etcpak.compress_bc7(np.ascontiguousarray(rgba).tobytes(), pw, ph)


def dds_header(w, h, mips, base_size):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000  # caps height width pixfmt mipcount linearsize
    caps = 0x1000 | 0x8 | 0x400000                          # texture complex mipmap
    pixfmt = struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    header = struct.pack("<7I", 124, flags, h, w, base_size, 0, mips) + b"\0" * 44 + pixfmt
    header += struct.pack("<5I", caps, 0, 0, 0, 0)
    dx10 = struct.pack("<5I", DXGI_BC7_UNORM, 3, 0, 1, 0)
    return b"DDS " + header + dx10


def encode_one(job):
    src, dst, gc_w, gc_h, max_scale, max_dim = job
    with Image.open(src) as im:
        rgba = np.asarray(im.convert("RGBA"))
    h, w = rgba.shape[:2]
    tw, th = target_size(w, h, gc_w, gc_h, max_scale, max_dim)
    premul = premultiply(rgba)
    straight = rgba.astype(np.float32)
    if (tw, th) != (w, h):
        premul = resize_premultiplied(premul, tw, th)
        straight = resize_premultiplied(straight, tw, th)
    blobs = []
    lw, lh = tw, th
    while True:
        blobs.append(bc7_level(rgba if (lw, lh) == (w, h) else merge_levels(premul, straight)))
        if lw == 1 and lh == 1:
            break
        lw, lh = max(1, lw // 2), max(1, lh // 2)
        premul = resize_premultiplied(premul, lw, lh)
        straight = resize_premultiplied(straight, lw, lh)
    with open(dst, "wb") as f:
        f.write(dds_header(tw, th, len(blobs), len(blobs[0])))
        for b in blobs:
            f.write(b)
    return src, (w, h), (tw, th), os.path.getsize(dst)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--max-scale", type=float, default=6)
    ap.add_argument("--max-dim", type=int, default=4096)
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--limit", type=int, default=0, help="encode only the first N (for testing)")
    args = ap.parse_args()

    if os.path.exists(args.out):
        sys.exit(f"{args.out} exists; pick a new directory")
    os.makedirs(args.out)

    jobs = []
    for fn in sorted(os.listdir(args.inp)):
        name = parse_name(fn)
        if name is None or name.mip is not None:
            continue
        jobs.append((os.path.join(args.inp, fn), os.path.join(args.out, name.key + ".dds"),
                     name.width, name.height, args.max_scale, args.max_dim))
    if args.limit:
        jobs = jobs[:args.limit]

    manifest = os.path.join(args.inp, "MANIFEST.json")
    if os.path.exists(manifest):
        shutil.copy2(manifest, os.path.join(args.out, "MANIFEST.json"))

    start = time.time()
    in_px = out_px = out_bytes = shrunk = failed = 0
    with multiprocessing.Pool(args.jobs) as pool:
        for i, res in enumerate(pool.imap_unordered(_safe_encode, jobs, chunksize=4), 1):
            if isinstance(res, str):
                failed += 1
                print(res, file=sys.stderr)
                continue
            _src, (w, h), (tw, th), size = res
            in_px += w * h
            out_px += tw * th
            out_bytes += size
            shrunk += (tw, th) != (w, h)
            if i % 250 == 0:
                print(f"  {i}/{len(jobs)}  {time.time() - start:.0f}s", flush=True)

    print(f"{len(jobs) - failed} encoded, {failed} failed, {shrunk} resized, "
          f"base pixels {in_px / 1e6:.0f}M -> {out_px / 1e6:.0f}M, "
          f"{out_bytes / 1e9:.2f} GB on disk, {time.time() - start:.0f}s")


def _safe_encode(job):
    try:
        return encode_one(job)
    except Exception as e:  # one bad file must not stop a 5000-file run
        return f"FAILED {job[0]}: {e}"


if __name__ == "__main__":
    main()
