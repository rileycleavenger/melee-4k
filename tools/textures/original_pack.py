#!/usr/bin/env python3
"""Build a pack of AI-upscaled original textures that outranks the community packs.

    .venv/bin/python tools/textures/original_pack.py DISC PACKS_DIR [--preset css-characters]
                                                    [--esrgan .deps/realesrgan]

Some original art is preferred over the community replacements: the
character select icons and the portraits shown while hovering a character
keep their original look, only sharper. A preset names those textures by key
and source file; they are decoded from the disc (extract.py), upscaled 4x
(upscale.py) and BC7-encoded (encode.py) into PACKS_DIR/<NN-name>, whose low
number gives it priority over the community pack (src/pc/textures.cpp).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

PRESETS = {
    # MnSlChr: the 64x56 grid icons and the 136x188 hover portraits, every
    # costume (C8, format 9). MnSlMap's stage icons are the same size and are
    # left to the community pack.
    "css-characters": ("05-css-original",
                       r"^tex1_(136x188|64x56)_[^\t]*_9\tMnSlChr\.(dat|usd)$"),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc")
    ap.add_argument("packs")
    ap.add_argument("--preset", choices=sorted(PRESETS), default="css-characters")
    ap.add_argument("--esrgan", default=".deps/realesrgan")
    args = ap.parse_args()

    name, match = PRESETS[args.preset]
    here = os.path.dirname(os.path.abspath(__file__))
    py = sys.executable
    out = os.path.join(args.packs, name)
    with tempfile.TemporaryDirectory() as tmp:
        src, up = os.path.join(tmp, "src"), os.path.join(tmp, "up")
        subprocess.run([py, os.path.join(here, "extract.py"), args.disc, src, "--match", match],
                       check=True)
        os.remove(os.path.join(src, "KEYS.txt"))
        subprocess.run([py, os.path.join(here, "upscale.py"), src, up, "--esrgan", args.esrgan],
                       check=True)
        staged = os.path.join(tmp, "enc")
        subprocess.run([py, os.path.join(here, "encode.py"), up, staged], check=True)
        if os.path.exists(out):
            shutil.rmtree(out)
        shutil.move(staged, out)
    print(f"{args.preset}: {len(os.listdir(out))} textures -> {out}")


if __name__ == "__main__":
    sys.exit(main())
