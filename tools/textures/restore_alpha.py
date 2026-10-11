#!/usr/bin/env python3
"""Give back transparency that a community replacement dropped.

    .venv/bin/python tools/textures/restore_alpha.py MERGED ORIGINALS [--dry-run]

MERGED is a merged pack (merge.py output, tex1_*.png); ORIGINALS holds the
same keys decoded from the disc (extract.py DISC ORIGINALS). Some community
textures were saved without their alpha: HD Remix's item foods are opaque on
black, so a Shy Guy's carried food shows a black box in game. For every key
whose original is transparent somewhere but whose replacement is (almost)
not, the original's alpha is upscaled to the replacement's size and applied:
the HD art is kept, the cut-out comes back. The replacement file is
rewritten as a new file (merge.py hard-links into the source packs, which
must stay untouched). Prints what it changed.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image


def upscale_alpha(alpha, size):
    """Smooth, not blurry: Lanczos, then a contrast curve that keeps the
    edge about one source texel wide at the original's 0/255 levels."""
    big = Image.fromarray(alpha).resize(size, Image.LANCZOS)
    a = np.asarray(big).astype(np.float32) / 255.0
    a = np.clip((a - 0.5) * 2.5 + 0.5, 0.0, 1.0)
    return (a * 255.0 + 0.5).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("merged")
    ap.add_argument("originals")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    changed = 0
    for name in sorted(os.listdir(args.merged)):
        if not (name.startswith("tex1_") and name.endswith(".png")) or "_mip" in name:
            continue
        orig = os.path.join(args.originals, name)
        if not os.path.exists(orig):
            continue
        oa = np.asarray(Image.open(orig).convert("RGBA"))[..., 3]
        transparent = float((oa < 16).mean())
        if transparent < 0.02:
            continue
        path = os.path.join(args.merged, name)
        with Image.open(path) as im:
            hd = np.array(im.convert("RGBA"))
        if float((hd[..., 3] < 16).mean()) >= 0.5 * transparent:
            continue
        hd[..., 3] = upscale_alpha(oa, (hd.shape[1], hd.shape[0]))
        changed += 1
        print(f"{name}: original {transparent:.0%} transparent, replacement was opaque")
        if not args.dry_run:
            os.remove(path)  # break the hard link before writing
            Image.fromarray(hd).save(path)
    print(f"{changed} replacement(s) given back their transparency")
    return 0


if __name__ == "__main__":
    sys.exit(main())
