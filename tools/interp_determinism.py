#!/usr/bin/env python3
"""Prove that high-refresh presentation never changes the simulation.

    python3 tools/interp_determinism.py DISC WORK_DIR [--frames 2400] [--seeds 1,2,3]
                                        [--stages 0,3,24] [--game build/macos/melee]

For each seed x stage, a CPU-only VS match (no human input, so input pacing
cannot differ between runs) goes through the offline record/replay harness
(MELEE_NET_RECORD / MELEE_NET_REPLAY, src/pc/net_snapshot.c) with the wide
per-frame checksum (MELEE_NET_CHECKSUM_WIDE=1: fighters, hitboxes, items):

  1. record      smooth motion off, the reference run
  2. canon       replay of 1, smooth motion off, re-recorded; this re-recording
                 is the reference (as in tools/net_determinism.py: the record
                 leg's own file can differ in pacing)
  3. off         replay of 2, smooth motion off     must not diverge
  4. on          replay of 2, smooth motion forced  must not diverge
  5. poisoned    replay of a copy of 2 with one checksum flipped
                                                   must diverge (the check works)

Exit status is 0 only when every combination passes.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

RECORD_MAGIC_SIZE = 8           # "MRC5" + u32 seed
FRAME_RECORD = 4 * 16 + 4 + 4 + 4  # PADStatus[4] (16 B on PC), ck, seed, scene
REPLAY_SLACK = 120


def run(args, env_extra, log):
    # A replay reports "no divergence" when its recording runs out; give it
    # time to get there before the exit frame (both landing on the same
    # frame let the exit win and read as a failure). Not for the canon leg,
    # which records as it replays and would only lengthen its recording.
    checking = "MELEE_NET_REPLAY" in env_extra and "MELEE_NET_RECORD" not in env_extra
    frames = args.frames + (REPLAY_SLACK if checking else 0)
    env = dict(os.environ, MELEE_BOOT_SCENE="vs", MELEE_DEBUG_VS="cpu4",
               MELEE_NET_CHECKSUM_WIDE="1", MELEE_EXIT_AFTER_FRAMES=str(frames),
               MELEE_LOG_FILE=log)
    env.update(env_extra)
    with open(log + ".out", "w") as out:
        rc = subprocess.run([args.game, args.disc], env=env, stdout=out, stderr=subprocess.STDOUT,
                            timeout=frames / 30 + 120).returncode
    text = open(log, errors="replace").read() if os.path.exists(log) else ""
    diverged = "REPLAY DIVERGED" in text
    finished = "no divergence" in text
    return rc, diverged, finished, text


def poison(src, dst):
    shutil.copyfile(src, dst)
    size = os.path.getsize(dst)
    frames = (size - RECORD_MAGIC_SIZE) // FRAME_RECORD
    if frames < 10:
        return False
    target = RECORD_MAGIC_SIZE + (frames // 2) * FRAME_RECORD + 4 * 16  # that frame's checksum
    with open(dst, "r+b") as f:
        f.seek(target)
        (ck,) = struct.unpack("<I", f.read(4))
        f.seek(target)
        f.write(struct.pack("<I", ck ^ 0x1))
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc")
    ap.add_argument("work")
    ap.add_argument("--frames", type=int, default=2400)
    ap.add_argument("--seeds", default="1,2,3")
    ap.add_argument("--stages", default="0,3,24")
    ap.add_argument("--game", default="build/macos/melee")
    args = ap.parse_args()
    os.makedirs(args.work, exist_ok=True)

    failures = 0
    for seed in args.seeds.split(","):
        for stage in args.stages.split(","):
            tag = f"seed{seed}-stage{stage}"
            base = {"MELEE_SEED": seed}
            if stage != "0":
                base["MELEE_DEBUG_VS_STAGE"] = stage
            p = lambda name: os.path.join(args.work, f"{tag}-{name}")
            rec, canon, bad = p("record.mrc"), p("canon.mrc"), p("poisoned.mrc")

            run(args, dict(base, MELEE_FRAME_INTERP="0", MELEE_NET_RECORD=rec), p("record.log"))
            run(args, dict(base, MELEE_FRAME_INTERP="0", MELEE_NET_REPLAY=rec, MELEE_NET_RECORD=canon),
                p("canon.log"))
            if not os.path.exists(canon) or os.path.getsize(canon) <= RECORD_MAGIC_SIZE:
                print(f"{tag}: FAIL no reference recording")
                failures += 1
                continue

            results = {}
            for leg, interp in (("off", "0"), ("on", "1")):
                rc, div, fin, _ = run(args, dict(base, MELEE_FRAME_INTERP=interp,
                                                 MELEE_NET_REPLAY=canon), p(f"{leg}.log"))
                results[leg] = rc == 0 and fin and not div
            if poison(canon, bad):
                rc, div, _, _ = run(args, dict(base, MELEE_FRAME_INTERP="0", MELEE_NET_REPLAY=bad),
                                    p("poisoned.log"))
                results["poison-caught"] = div
            else:
                results["poison-caught"] = False

            ok = all(results.values())
            failures += not ok
            print(f"{tag}: {'PASS' if ok else 'FAIL'} " +
                  " ".join(f"{k}={'ok' if v else 'BAD'}" for k, v in results.items()), flush=True)
    print("all passed" if failures == 0 else f"{failures} combination(s) failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
