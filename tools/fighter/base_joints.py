#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A base fighter's skeleton as an indented tree, for mapping a model's
bones onto it (blender_retarget.py) and posing it (build_moves.py).

    .venv/bin/python tools/fighter/base_joints.py BASE_DIR

Per joint: index (J<n>, the bone and hitbox-bone number), its bind-pose
position in game space (Y up, +Z front, +X the fighter's left), whether it
deforms the mesh (ibm: has an inverse bind matrix; joints without one are
helpers: never weight to them), and a guessed role from where its chain
goes: L/R arm (shoulder > elbow > hand > fingers), L/R leg (thigh > knee >
foot > toe), spine, head, root/body (TransN = root motion, XRotN/YRotN =
whole-body flips/turns on most fighters). Check the guesses against a
render: the guesses follow positions, not names.
"""
import json
import os
import sys


def main():
    base = sys.argv[1]
    sk = json.load(open(os.path.join(base, "skeleton.json")))["joints"]
    pos = {j["index"]: (j["world"][0][3], j["world"][1][3], j["world"][2][3]) for j in sk}
    kids = {j["index"]: [] for j in sk}
    for j in sk:
        if j["parent"] >= 0:
            kids[j["parent"]].append(j["index"])
    height = max(p[1] for p in pos.values()) or 1.0

    def sub(i):
        out = [i]
        for c in kids[i]:
            out += sub(c)
        return out

    role = {}
    for j in sk:
        i = j["index"]
        x, y, _ = pos[i]
        side = "L" if x > 0.3 else "R" if x < -0.3 else ""
        if not side:
            continue
        # A chain heading down from below the waist is a leg; out to the
        # side at the chest an arm.
        below = [pos[k][1] for k in sub(i)]
        if y < height * 0.55 and min(below) < height * 0.2:
            role[i] = f"{side} leg"
        elif y > height * 0.45:
            role[i] = f"{side} arm"
    tops = sorted((p[1], i) for i, p in pos.items() if i not in role)
    if tops:
        role.setdefault(tops[-1][1], "head top")

    def walk(i, depth):
        j = next(j for j in sk if j["index"] == i)
        x, y, z = pos[i]
        tag = role.get(i, "")
        print(f"{'  ' * depth}J{i:02d}  ({x:6.2f} {y:6.2f} {z:6.2f})  {'ibm' if j['ibm'] else '---'}  {tag}")
        for c in kids[i]:
            walk(c, depth + 1)
    for j in sk:
        if j["parent"] < 0:
            walk(j["index"], 0)


if __name__ == "__main__":
    main()
