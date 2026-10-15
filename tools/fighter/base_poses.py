#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A base fighter's poses from its own animations, in the game's pose-dump
format (MELEE_DUMP_POSE), without running the game: for checking a fitted
model in the base's real poses (blender_pose.py, make_pack.sh --check).

    .venv/bin/python tools/fighter/base_poses.py BASE_DIR OUT_DIR [--base Mr]
        [MOTION:FRAME ...]

Default poses: idle, run, jab, forward smash, up smash, jump, neutral air,
crouch, fall. Each to OUT_DIR/<name>.json. Joints the game does not animate
stay in their bind pose here (the game leaves some at zero: see SKILL.md).
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anim  # noqa: E402
import build_moves  # noqa: E402

DEFAULT = ["Wait1:0:idle", "Run:6:run", "Attack11:4:jab", "AttackS4S:14:fsmash", "AttackHi4:12:usmash",
           "JumpF:8:jump", "AttackAirN:8:nair", "SquatWait:10:squat", "Fall:4:fall", "AttackLw4:10:dsmash"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("base_dir")
    ap.add_argument("out")
    ap.add_argument("poses", nargs="*")
    ap.add_argument("--base", default="Mr")
    a = ap.parse_args()
    ctx = build_moves.Ctx(a.base_dir, a.base)
    os.makedirs(a.out, exist_ok=True)
    for spec in a.poses or DEFAULT:
        parts = spec.split(":")
        motion, frame = parts[0], float(parts[1])
        name = parts[2] if len(parts) > 2 else motion
        try:
            local = ctx.locals(motion, frame)
        except StopIteration:
            print(f"{motion}: not a motion of {a.base}")
            continue
        json.dump(anim.dump_pose(anim.pose(ctx.skeleton, local)), open(os.path.join(a.out, name + ".json"), "w"))
        print(f"{name}: {motion} frame {frame:g}")


if __name__ == "__main__":
    main()
