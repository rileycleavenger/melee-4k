#!/usr/bin/env python3
"""Screenshot every mode's screens in 16:9 and 4:3, and list uncovered textures.

    python3 tools/screen_sweep.py DISC WORK [--texture-dir DIR] [--aspects wide,43]
                                  [--only REGEX] [--game build/macos/melee]

Each boot scene (MELEE_BOOT_SCENE, gmboot.c) runs in a throwaway settings
folder (MELEE_USER_DIR), so the player's launcher.cfg is never touched, with
smooth motion off so the capture frame is a game frame. WORK/<aspect>/<scene>.png
is the screen, WORK/<aspect>.png a contact sheet of all of them. With
--texture-dir, textures drawn that no pack replaced are dumped
(MELEE_TEXTURE_DUMP) and listed in WORK/uncovered.txt.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

SCENES = ["title", "menu", "vs-css", "vs-sss", "vs", "classic", "adventure", "allstar",
          "training", "event", "target", "homerun", "multiman10", "tournament", "trophies",
          "lottery", "collection", "camera", "special-stamina"]
CAPTURE_FRAME = 480


def run(args, scene, aspect, work, dumps):
    user = tempfile.mkdtemp(prefix="sweep-user-")
    try:
        with open(os.path.join(user, "launcher.cfg"), "w") as f:
            f.write(f"widescreen {1 if aspect == 'wide' else 0}\n")
        shots = os.path.join(work, aspect, scene + "-tga")
        os.makedirs(shots, exist_ok=True)
        env = dict(os.environ, MELEE_USER_DIR=user, MELEE_BOOT_SCENE=scene, MELEE_UNLOCK_ALL="1",
                   MELEE_FRAME_INTERP="0", AURORA_SCREENSHOT_FRAMES=str(CAPTURE_FRAME),
                   AURORA_SCREENSHOT_DIR=shots, MELEE_EXIT_AFTER_FRAMES=str(CAPTURE_FRAME + 20))
        if args.texture_dir:
            env["MELEE_TEXTURE_DIR"] = args.texture_dir
            env["MELEE_TEXTURE_DUMP"] = "1"
            env["MELEE_CACHE_DIR"] = dumps
        with open(os.path.join(work, aspect, scene + ".log"), "w") as log:
            subprocess.run([args.game, args.disc], env=env, stdout=log, stderr=subprocess.STDOUT,
                           timeout=180)
        tga = os.path.join(shots, f"frame_{CAPTURE_FRAME}.tga")
        if not os.path.exists(tga):
            return None
        from PIL import Image
        out = os.path.join(work, aspect, scene + ".png")
        im = Image.open(tga).convert("RGB")
        im.thumbnail((960, 960))
        im.save(out)
        shutil.rmtree(shots, ignore_errors=True)
        return out
    finally:
        shutil.rmtree(user, ignore_errors=True)


def sheet(paths, out):
    from PIL import Image, ImageDraw
    ims = [(os.path.basename(p)[:-4], Image.open(p)) for p in paths]
    w = 480
    for _, im in ims:
        im.thumbnail((w, w))
    h = max(im.height for _, im in ims) + 18
    cols = 3
    s = Image.new("RGB", (cols * (w + 8), ((len(ims) + cols - 1) // cols) * h), (40, 40, 48))
    d = ImageDraw.Draw(s)
    for i, (name, im) in enumerate(ims):
        x, y = (i % cols) * (w + 8), (i // cols) * h
        s.paste(im, (x, y + 16))
        d.text((x + 2, y + 2), name, fill=(255, 255, 255))
    s.save(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc")
    ap.add_argument("work")
    ap.add_argument("--texture-dir")
    ap.add_argument("--aspects", default="wide,43")
    ap.add_argument("--only")
    ap.add_argument("--game", default="build/macos/melee")
    args = ap.parse_args()
    args.game = os.path.abspath(args.game)
    dumps = os.path.join(os.path.abspath(args.work), "cache")
    for aspect in args.aspects.split(","):
        os.makedirs(os.path.join(args.work, aspect), exist_ok=True)
        done = []
        for scene in SCENES:
            if args.only and not re.search(args.only, scene):
                continue
            png = run(args, scene, aspect, args.work, dumps)
            print(f"{aspect}/{scene}: {'ok' if png else 'NO CAPTURE'}", flush=True)
            if png:
                done.append(png)
        if done:
            sheet(done, os.path.join(args.work, aspect + ".png"))
    dump_dir = os.path.join(dumps, "texture_dumps")
    if args.texture_dir and os.path.isdir(dump_dir):
        names = sorted(n[:-4] for n in os.listdir(dump_dir) if n.endswith(".dds") and "_mip" not in n)
        with open(os.path.join(args.work, "uncovered.txt"), "w") as f:
            f.write("\n".join(names) + ("\n" if names else ""))
        print(f"{len(names)} textures drawn with no replacement -> {args.work}/uncovered.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
