#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""See a custom fighter in the game: CPU matches screenshotted into one sheet.

    python3 tools/fighter/preview.py DISC PACKS_DIR ID OUT_DIR
        [--costumes 0,1] [--stages 32,31,3] [--vs 8] [--frames 900]
        [--game build/macos/melee]

For each costume x stage, boots a VS match (MELEE_BOOT_SCENE=vs) of the
fighter (MELEE_DEBUG_VS_CHARS=<ID>:<costume>, a level 9 CPU) against a CPU
of character --vs (CKind number; 8 is Mario), takes screenshots through the
match (AURORA_SCREENSHOT_FRAMES), and writes OUT_DIR/sheet.jpg with every
shot labelled. This is the in-match look; the menus (character select,
HUD, results) are checked as the make-fighter skill's checklist says.

Stages are StKind numbers: 32 Final Destination, 31 Battlefield, 3 Pokemon
Stadium, 8 Yoshi's Story, 24 Big Blue (StKind, src/melee/gr/forward.h). Exit status 1 when a run
crashes or the fighter pack did not load.
"""
import argparse
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def to_png(tga, png, size):
    if sys.platform == "darwin" and shutil.which("sips"):
        subprocess.run(["sips", "-s", "format", "png", "-Z", str(size), tga, "--out", png],
                       stdout=subprocess.DEVNULL, check=True)
    else:
        from PIL import Image
        im = Image.open(tga)
        im.thumbnail((size, size))
        im.save(png)
    os.remove(tga)


def sheet(shots, out):
    from PIL import Image, ImageDraw
    w, h, cols = 480, 270, 4
    rows = (len(shots) + cols - 1) // cols
    s = Image.new("RGB", (w * cols, h * max(rows, 1)))
    for i, (label, path) in enumerate(shots):
        im = Image.open(path).convert("RGB").resize((w, h))
        ImageDraw.Draw(im).text((6, 6), label, fill="yellow")
        s.paste(im, ((i % cols) * w, (i // cols) * h))
    s.save(out, quality=85)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc")
    ap.add_argument("packs", help="the folder holding <ID>/fighter.cfg")
    ap.add_argument("id")
    ap.add_argument("out")
    ap.add_argument("--costumes", default="0")
    ap.add_argument("--stages", default="32,31,3")
    ap.add_argument("--vs", default="8")
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--game", default=os.path.join(REPO, "build", "macos", "melee"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    shots, failed = [], False
    frames = [a.frames * k // 4 for k in (1, 2, 3, 4)]
    for costume in a.costumes.split(","):
        for stage in a.stages.split(","):
            name = f"c{costume}-st{stage}"
            work = os.path.join(a.out, name)
            shutil.rmtree(work, ignore_errors=True)
            os.makedirs(os.path.join(work, "user"))
            env = dict(os.environ, MELEE_USER_DIR=os.path.join(work, "user"),
                       MELEE_FIGHTERS_DIR=os.path.abspath(a.packs), MELEE_BOOT_SCENE="vs",
                       MELEE_DEBUG_VS_CHARS=f"{a.id}:{costume},{a.vs}:0",
                       MELEE_DEBUG_VS_STAGE=stage,
                       AURORA_SCREENSHOT_FRAMES=",".join(map(str, frames)),
                       AURORA_SCREENSHOT_DIR=work,
                       MELEE_EXIT_AFTER_FRAMES=str(frames[-1] + 20),
                       MELEE_LOG_FILE=os.path.join(work, "log"))
            with open(os.path.join(work, "out"), "w") as out:
                try:
                    rc = subprocess.run([a.game, "--no-card", a.disc], env=env, stdout=out,
                                        stderr=subprocess.STDOUT, timeout=frames[-1] / 20 + 120).returncode
                except subprocess.TimeoutExpired:
                    rc = "timeout"
            log = open(os.path.join(work, "log"), errors="replace").read() \
                if os.path.exists(os.path.join(work, "log")) else ""
            loaded = f"fighters: {a.id} is " in log
            print(f"{name}: exit {rc}{'' if loaded else ', pack NOT loaded'}")
            failed |= rc != 0 or not loaded
            for f in frames:
                tga = os.path.join(work, f"frame_{f}.tga")
                if os.path.exists(tga):
                    png = os.path.join(work, f"frame_{f}.png")
                    to_png(tga, png, 1280)
                    shots.append((f"{a.id} costume {costume}, stage {stage}, frame {f}", png))
    if shots:
        sheet(shots, os.path.join(a.out, "sheet.jpg"))
        print(f"{len(shots)} shots -> {os.path.join(a.out, 'sheet.jpg')}")
    return 1 if failed or not shots else 0


if __name__ == "__main__":
    sys.exit(main())
