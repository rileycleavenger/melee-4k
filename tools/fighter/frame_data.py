#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A base fighter's frame data, from its own files: per motion the
animation's length and the action script's events (hitboxes with bone,
damage, size, angle, knockback; timers; IASA; auto-cancel; smash charge;
throw frames). The timing a custom fighter's new attacks are built on
(build_moves.py).

    .venv/bin/python tools/fighter/frame_data.py BASE_DIR [--base Mr] [MOTION ...]

BASE_DIR is export_base.py's folder (PlXx.dat, PlXxAJ.dat). Without
MOTION names it prints every attack, the taunt, throws and specials.
Opcodes: 2c hitbox, 40 clear hitboxes, 5c IASA (interruptible), 4c
auto-cancel on/off, e0 smash charge frame, 28 effect, 44 sound, a0 model
swap, 88 throw, 74/d0/ac base-specific (keep them).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anim  # noqa: E402

DEFAULT = ("Attack11 Attack12 Attack13 Attack100 AttackDash AttackS3Hi AttackS3S AttackS3Lw AttackHi3 AttackLw3 "
           "AttackS4Hi AttackS4S AttackS4Lw AttackHi4 AttackLw4 AttackAirN AttackAirF AttackAirB AttackAirHi "
           "AttackAirLw Appeal CatchAttack ThrowF ThrowB ThrowHi ThrowLw DownAttackU DownAttackD "
           "CliffAttackSlow CliffAttackQuick SpecialN SpecialAirN SpecialS SpecialSAir SpecialHi SpecialAirHi "
           "SpecialLw SpecialAirLw").split()


def events(a, off):
    t, out = 0, []
    for op, w in anim.read_script(a, off):
        if op == 0x04:
            t += w[0] & 0x3FFFFFF
        elif op == 0x08:
            t = w[0] & 0x3FFFFFF
        elif op == 0x2C:
            h = anim.parse_hitbox(w)
            out.append(f"f{t}:hit{h['id']}[bone {h['bone']} {h['damage']}% size {h['size']:.1f} "
                       f"angle {h['angle']} kbg {h['kbg']} bkb {h['bkb']} wkb {h['wkb']} {h['element']}]")
        elif op == 0x40:
            out.append(f"f{t}:clear")
        elif op == 0x5C:
            out.append(f"f{t}:IASA")
        elif op == 0x4C:
            out.append(f"f{t}:autocancel{w[0] & 1}")
        elif op == 0xE0:
            out.append(f"f{t}:charge")
        elif op:
            out.append(f"f{t}:{op:02x}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("base_dir")
    ap.add_argument("motions", nargs="*")
    ap.add_argument("--base", default="Mr")
    a = ap.parse_args()
    rows, arc = anim.motion_table(open(os.path.join(a.base_dir, f"Pl{a.base}.dat"), "rb").read())
    aj = open(os.path.join(a.base_dir, f"Pl{a.base}AJ.dat"), "rb").read()
    names = {}
    for r in rows:
        if r[1] and "_ACTION_" in r[1]:
            names.setdefault(r[1].split("_ACTION_")[1][:-len("_figatree")], r)
    for m in a.motions or DEFAULT:
        r = names.get(m)
        if r is None:
            if a.motions:
                print(f"{m}: not a motion of {a.base}")
            continue
        try:
            frames = int(anim.read_figatree(aj[r[2]:r[2] + r[3]])[0])
        except ValueError:
            frames = "?"
        print(f"{m} ({frames} frames): " + " ".join(events(arc, r[4]) if r[4] is not None else []))


if __name__ == "__main__":
    main()
