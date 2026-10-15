#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Play a custom fighter's moves from a script, screenshotting and catching
crashes.

    python3 tools/fighter/play.py DISC PACKS_DIR ID OUT_DIR SCRIPT
        [--costume 0] [--stage 32] [--boot 8] [--every 6] [--game build/macos/melee]

Boots a VS match with the fighter as player 1 (a human driven through
MELEE_KEY_FIFO) against an idle Mario, waits --boot seconds, then plays
SCRIPT: comma-separated steps "Keys hold_ms" (keys joined by +, SDL names:
Z=B, X=A, V=Y jump, arrows = control stick, I/J/K/L = C-stick, Return =
start) or "wait ms". Examples: "Up+Z 100, wait 1500" (up B),
"Right+X 60, wait 800" (forward tilt; hold the arrow longer for a smash),
"L 80" (forward smash on the C-stick).

Screenshots every --every frames from the first step to the end go to
OUT_DIR/sheet.jpg (four across). Exit status 0 only if the game ran to the
end: a crash prints the game's exit code and the tail of its log.
"""
import argparse
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from preview import to_png  # noqa: E402


def parse(script):
    steps = []
    for part in script.split(","):
        part = part.strip()
        if not part:
            continue
        keys, ms = part.rsplit(None, 1)
        steps.append((None if keys == "wait" else keys, int(ms)))
    return steps


def drive(fifo, boot, steps):
    with open(fifo, "w") as f:
        time.sleep(boot)
        for keys, ms in steps:
            if keys:
                f.write(f"{keys} {ms}\n")
                f.flush()
            time.sleep((ms + (100 if keys else 0)) / 1000)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc")
    ap.add_argument("packs")
    ap.add_argument("id")
    ap.add_argument("out")
    ap.add_argument("script")
    ap.add_argument("--costume", default="0")
    ap.add_argument("--stage", default="32")
    ap.add_argument("--boot", type=float, default=8.0)
    ap.add_argument("--every", type=int, default=6)
    ap.add_argument("--foe", default="8:0h", help="the opponent (MELEE_DEBUG_VS_CHARS form): an idle Mario; "
                    "\"8:0\" a level 9 CPU Mario that comes to fight (attacks land wherever you stand)")
    ap.add_argument("--game", default=os.path.join(REPO, "build", "macos", "melee"))
    a = ap.parse_args()
    steps = parse(a.script)
    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(os.path.join(a.out, "user"))
    fifo = os.path.join(a.out, "keys")
    os.mkfifo(fifo)
    length = sum(ms + (100 if k else 0) for k, ms in steps) / 1000 + 1.5
    total = int((a.boot + length) * 60)
    frames = list(range(int(a.boot * 60), total, a.every))
    env = dict(os.environ, MELEE_USER_DIR=os.path.join(a.out, "user"),
               MELEE_FIGHTERS_DIR=os.path.abspath(a.packs), MELEE_BOOT_SCENE="vs",
               MELEE_DEBUG_VS_CHARS=f"{a.id}:{a.costume}h,{a.foe}", MELEE_DEBUG_VS_STAGE=a.stage,
               MELEE_KEY_FIFO=fifo, AURORA_SCREENSHOT_FRAMES=",".join(map(str, frames)),
               AURORA_SCREENSHOT_DIR=a.out, MELEE_EXIT_AFTER_FRAMES=str(total + 10),
               MELEE_LOG_FILE=os.path.join(a.out, "log"))
    threading.Thread(target=drive, args=(fifo, a.boot, steps), daemon=True).start()
    with open(os.path.join(a.out, "out"), "w") as out:
        rc = subprocess.run([a.game, "--no-card", a.disc], env=env, stdout=out, stderr=subprocess.STDOUT,
                            timeout=total / 20 + 120).returncode
    shots = []
    for f in frames:
        tga = os.path.join(a.out, f"frame_{f}.tga")
        if os.path.exists(tga):
            png = os.path.join(a.out, f"frame_{f}.png")
            to_png(tga, png, 960)
            shots.append(png)
    if shots:
        from PIL import Image
        w, h, cols = 480, 270, 4
        s = Image.new("RGB", (w * cols, h * ((len(shots) + cols - 1) // cols)))
        for i, p in enumerate(shots):
            s.paste(Image.open(p).convert("RGB").resize((w, h)), ((i % cols) * w, (i // cols) * h))
        s.save(os.path.join(a.out, "sheet.jpg"), quality=85)
    print(f"exit {rc}; {len(shots)}/{len(frames)} shots -> {os.path.join(a.out, 'sheet.jpg')}")
    if rc != 0:
        for name in ("out", "log"):
            p = os.path.join(a.out, name)
            if os.path.exists(p):
                print(f"--- {name} (tail)")
                print("".join(open(p, errors="replace").readlines()[-25:]))
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
