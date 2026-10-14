#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Close-ups of a custom fighter in a match, through the pause camera.

    python3 tools/fighter/closeup.py DISC PACKS_DIR ID OUT_DIR [--costume 0]
        [--stage 32] [--boot 8] [--game build/macos/melee]

Boots a VS match (MELEE_BOOT_SCENE=vs) with the fighter as player 1, a
human with no input (MELEE_DEBUG_VS_CHARS=<ID>:<costume>h, so it stands in
its idle pose) against an idle Mario, pauses it after --boot seconds (Return
through MELEE_KEY_FIFO, a named pipe: the pause camera zooms in on player
1), then zooms in (Y: the V key) and turns the camera with the control stick
(arrow keys),
screenshotting every 12 frames. Writes OUT_DIR/closeups.jpg: the paused
frames, in order (front, zoomed in, turned right, turned left, from above). The
fighter's in-game model, shading and textures at close range; preview.py
shows it moving at match distance.
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

# (keys, hold ms, then wait ms): after the pause, turn and zoom the camera.
# The pause camera: the control stick (arrow keys) turns it, Y (V) zooms in.
CAMERA = [("Return", 150, 1200), ("V", 1500, 1200), ("Right", 600, 1200), ("Right", 600, 1200),
          ("Left", 1800, 1200), ("Left", 600, 1200), ("Up", 500, 1200)]


def drive(fifo, boot, log):
    with open(fifo, "w") as f:
        time.sleep(boot)
        for keys, hold, wait in CAMERA:
            f.write(f"{keys} {hold}\n")
            f.flush()
            log.append((time.time(), keys))
            time.sleep((hold + 100 + wait) / 1000)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc")
    ap.add_argument("packs")
    ap.add_argument("id")
    ap.add_argument("out")
    ap.add_argument("--costume", default="0")
    ap.add_argument("--stage", default="32")
    ap.add_argument("--boot", type=float, default=8.0)
    ap.add_argument("--game", default=os.path.join(REPO, "build", "macos", "melee"))
    a = ap.parse_args()
    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(os.path.join(a.out, "user"))
    fifo = os.path.join(a.out, "keys")
    os.mkfifo(fifo)
    total = int((a.boot + sum(h + 100 + w for _, h, w in CAMERA) / 1000 + 1) * 60)
    start = int(a.boot * 60)
    frames = list(range(start, total, 12))
    env = dict(os.environ, MELEE_USER_DIR=os.path.join(a.out, "user"),
               MELEE_FIGHTERS_DIR=os.path.abspath(a.packs), MELEE_BOOT_SCENE="vs",
               MELEE_DEBUG_VS_CHARS=f"{a.id}:{a.costume}h,8:0h", MELEE_DEBUG_VS_STAGE=a.stage,
               MELEE_KEY_FIFO=fifo, AURORA_SCREENSHOT_FRAMES=",".join(map(str, frames)),
               AURORA_SCREENSHOT_DIR=a.out, MELEE_EXIT_AFTER_FRAMES=str(total + 10),
               MELEE_LOG_FILE=os.path.join(a.out, "log"))
    log = []
    t = threading.Thread(target=drive, args=(fifo, a.boot, log), daemon=True)
    t.start()
    with open(os.path.join(a.out, "out"), "w") as out:
        rc = subprocess.run([a.game, "--no-card", a.disc], env=env, stdout=out, stderr=subprocess.STDOUT,
                            timeout=total / 20 + 120).returncode
    shots = []
    for f in frames:
        tga = os.path.join(a.out, f"frame_{f}.tga")
        if os.path.exists(tga):
            png = os.path.join(a.out, f"frame_{f}.png")
            to_png(tga, png, 1600)
            shots.append(png)
    if shots:
        from PIL import Image
        step = max(1, len(shots) // 12)
        pick = shots[::step][:12]
        w, h, cols = 640, 360, 4
        s = Image.new("RGB", (w * cols, h * ((len(pick) + cols - 1) // cols)))
        for i, p in enumerate(pick):
            s.paste(Image.open(p).convert("RGB").resize((w, h)), ((i % cols) * w, (i // cols) * h))
        s.save(os.path.join(a.out, "closeups.jpg"), quality=88)
    print(f"exit {rc}; {len(shots)} shots -> {os.path.join(a.out, 'closeups.jpg')}")
    return 0 if rc == 0 and shots else 1


if __name__ == "__main__":
    sys.exit(main())
