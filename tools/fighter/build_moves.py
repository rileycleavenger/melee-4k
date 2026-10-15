#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a custom fighter's own attacks: new animations and action scripts
for its base's motions, as the pack's motions.bin (src/pc/fighter_pack.h,
read by ftCustom_Motions in src/melee/ft/ftcustom.c).

    .venv/bin/python tools/fighter/build_moves.py SPEC.py --base-dir BASE \\
        --out PACK/motions.bin [--preview DIR] [--only Attack11,...]

SPEC.py (the character's, outside the repo) defines `moves(ctx)` returning
a list of `Move`s. `ctx` gives the base's skeleton and poses:

    ctx.pose("Wait1", 0)     a Poser (anim.py) at a base animation's frame:
                             aim/turn/move from there, model space
                             (Y up, +Z front, +X the fighter's left)
    ctx.script()             an action script (anim.Script: hitbox, wait,
                             until, clear, iasa, end)
    ctx.rescript(motion, [{...}, ...])
                             the base's script for the motion, timing and
                             other commands kept, its hitboxes changed one
                             by one (fields: bone, damage, size, offset,
                             angle, kbg, bkb, wkb, element; None removes
                             one), the base's effects (0x28) and model
                             swaps (0xA0) left out
    Move(motion, frames, keys=[(frame, Poser)], script=Script, flags=None,
         translate=[joints with authored positions], also=[more motions],
         root=False)

`root=True` keeps the base animation's root motion: its TransN (joint 1)
position track, time-scaled to the move's length, sampled every frame.
Motions whose row flags have 0x80000000 move the fighter by it (up B
recoveries, dash attacks, ledge attacks): without it they go nowhere.

`motion` is the base's motion name ("Attack11" for PlyMario5K_Share_ACTION_
Attack11_figatree); `also` gives other motions the same move replaces
(the angled forward tilts and smashes). Every joint any of the base's
animations moves gets a track, so nothing of the previous animation
lingers.

--preview writes DIR/<motion>/f<frame>.json poses (every 2 frames) in the
game's pose-dump format, for blender_pose.py / the render scripts.
"""
import argparse
import importlib.util
import json
import os
import struct
import sys
from dataclasses import dataclass, field

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anim  # noqa: E402
import hsd  # noqa: E402
import model  # noqa: E402


@dataclass
class Move:
    motion: str
    frames: int
    keys: list
    script: object = None
    flags: int = None
    translate: list = field(default_factory=list)
    also: list = field(default_factory=list)
    root: bool = False


class Ctx:
    def __init__(self, base_dir, base):
        nr = hsd.Archive(open(os.path.join(base_dir, f"Pl{base}Nr.dat"), "rb").read())
        sym = next(s for s in nr.symbols if s.endswith("_Share_joint") and "matanim" not in s)
        self.joints = hsd.read_joint_tree(nr, nr.symbols[sym])
        self.skeleton = model.read_skeleton(nr, sym)
        self._pl_bytes = open(os.path.join(base_dir, f"Pl{base}.dat"), "rb").read()
        self.rows, _ = anim.motion_table(self._pl_bytes)
        self.aj = open(os.path.join(base_dir, f"Pl{base}AJ.dat"), "rb").read()
        self._cache = {}
        # Every (joint, track type) any of the base's animations uses.
        self.channels = set()
        seen = set()
        for r in self.rows:
            if r[1] and r[3] and r[2] not in seen:
                seen.add(r[2])
                try:
                    _, tr = anim.read_figatree(self.aj[r[2]:r[2] + r[3]])
                except ValueError:
                    continue  # rows into another file (the victory poses)
                self.channels |= {(j, k) for j, t in tr.items() for k in t}

    # The same motion under another name on some bases.
    ALIASES = {"Wait1": ["Wait"], "Wait": ["Wait1"], "Appeal": ["AppealR"], "AppealR": ["Appeal"],
               "AttackS4S": ["AttackS4"], "AttackS4": ["AttackS4S"],
               "SquatWait": ["SquatWait1"], "SquatWait1": ["SquatWait"]}

    def row(self, motion):
        for name in [motion] + self.ALIASES.get(motion, []):
            r = next((r for r in self.rows if r[1] and r[1].endswith(f"_ACTION_{name}_figatree")), None)
            if r is not None:
                return r
        raise StopIteration(motion)

    def tracks(self, motion):
        if motion not in self._cache:
            r = self.row(motion)
            self._cache[motion] = anim.read_figatree(self.aj[r[2]:r[2] + r[3]])
        return self._cache[motion]

    def locals(self, motion, frame):
        _, tr = self.tracks(motion)
        out = []
        for j in self.joints:
            rot, sc, pos = list(j["rot"]), list(j["scale"]), list(j["pos"])
            for k, keys in tr.get(j["index"], {}).items():
                v = anim.sample(keys, frame)
                if 1 <= k <= 3:
                    rot[k - 1] = v
                elif 5 <= k <= 7:
                    pos[k - 5] = v
                elif 8 <= k <= 10:
                    sc[k - 8] = v
            out.append([rot, sc, pos])
        return out

    def pose(self, motion="Wait1", frame=0):
        return anim.Poser(self.skeleton, self.locals(motion, frame))

    def script(self):
        return anim.Script()

    def rescript(self, motion, hits, drop=(0x28, 0xA0)):
        """The base's script for `motion`, its hitboxes changed (anim.rescript)."""
        if not hasattr(self, "_pl"):
            self._pl = anim.motion_table(self._pl_bytes)[1]
        return anim.rescript(anim.read_script(self._pl, self.row(motion)[4]), hits, drop)


def tracks_for(ctx, move):
    keys = [(f, p.locals) for f, p in move.keys]
    n = len(ctx.skeleton)
    out = {}
    for j in range(n):
        tr = {}
        prev = None
        rot = []
        for f, L in keys:
            r = L[j][0] if prev is None else anim.unwrap(prev, L[j][0])
            prev = r
            rot.append((f, r))
        for k, name in enumerate(("rx", "ry", "rz")):
            if (j, k + 1) in ctx.channels or any(abs(r[k] - rot[0][1][k]) > 1e-6 for _, r in rot):
                tr[name] = [(f, r[k]) for f, r in rot]
        for k, name in enumerate(("tx", "ty", "tz")):
            if (j, k + 5) in ctx.channels or j in move.translate:
                tr[name] = [(f, L[j][2][k]) for f, L in keys]
        for k, name in enumerate(("sx", "sy", "sz")):
            if (j, k + 8) in ctx.channels:
                tr[name] = [(f, L[j][1][k]) for f, L in keys]
        if tr:
            out[j] = tr
    if move.root:
        base_frames, base = ctx.tracks(move.motion)
        k = base_frames / move.frames
        for t, name in ((5, "tx"), (6, "ty"), (7, "tz")):
            keys = base.get(1, {}).get(t)
            if keys:
                out.setdefault(1, {})[name] = [(f, anim.sample(keys, f * k)) for f in range(move.frames + 1)]
    return out


def write_bin(entries):
    """entries: [(name, archive bytes, script bytes or None, flags or None)]."""
    head = 12 + 24 * len(entries)
    blob = bytearray()
    table = []

    def put(data, align):
        while (head + len(blob)) % align:
            blob.append(0)
        off = head + len(blob)
        blob.extend(data)
        return off

    for name, arc, scr, flags in entries:
        n = put(name.encode() + b"\0", 1)
        a = put(arc, 32)
        s = put(scr, 4) if scr else 0
        table.append(struct.pack(">6I", n, a, len(arc), s, len(scr) if scr else 0,
                                 0xFFFFFFFF if flags is None else flags))
    return b"M4MV" + struct.pack(">II", 1, len(entries)) + b"".join(table) + bytes(blob)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("spec")
    ap.add_argument("--base-dir", required=True)
    ap.add_argument("--base", default="Mr")
    ap.add_argument("--out", required=True)
    ap.add_argument("--preview")
    ap.add_argument("--only")
    a = ap.parse_args()
    ctx = Ctx(a.base_dir, a.base)
    # The spec's own folder too: its helper modules (a body's posing kit).
    sys.path.insert(0, os.path.dirname(os.path.abspath(a.spec)))
    spec = importlib.util.spec_from_file_location("spec", a.spec)
    mod = importlib.util.module_from_spec(spec)
    mod.Move = Move
    spec.loader.exec_module(mod)
    moves = mod.moves(ctx)
    only = set(a.only.split(",")) if a.only else None
    entries = []
    done = set()
    for m in moves:
        if only and m.motion not in only:
            continue
        tracks = tracks_for(ctx, m)
        scr = m.script.bytes() if m.script is not None else None
        for motion in [m.motion] + list(m.also):
            sym = ctx.row(motion)[1]
            # The row's own name (an alias resolved): the game matches it exactly.
            name = sym.split("_ACTION_")[1][:-len("_figatree")]
            if name in done:
                continue
            done.add(name)
            arc = anim.write_figatree(sym, m.frames, tracks, len(ctx.skeleton))
            if len(arc) > 0x8000:
                raise SystemExit(f"{motion}: animation {len(arc)} bytes, over the game's 0x8000")
            entries.append((name, arc, scr, m.flags))
        print(f"{m.motion}{'+' + '+'.join(m.also) if m.also else ''}: {m.frames} frames, "
              f"{len(m.keys)} keys, {len(arc)} bytes, script {len(scr) if scr else 0} bytes")
        if a.preview:
            d = os.path.join(a.preview, m.motion)
            os.makedirs(d, exist_ok=True)
            frames, decoded = anim.read_figatree(arc)
            for f in range(0, m.frames + 1, 2):
                L = []
                for j in ctx.joints:
                    rot, sc, pos = list(j["rot"]), list(j["scale"]), list(j["pos"])
                    for k, keys in decoded.get(j["index"], {}).items():
                        v = anim.sample(keys, f)
                        if 1 <= k <= 3:
                            rot[k - 1] = v
                        elif 5 <= k <= 7:
                            pos[k - 5] = v
                        elif 8 <= k <= 10:
                            sc[k - 8] = v
                    L.append([rot, sc, pos])
                json.dump(anim.dump_pose(anim.pose(ctx.skeleton, L)),
                          open(os.path.join(d, f"f{f:03d}.json"), "w"))
    data = write_bin(entries)
    open(a.out, "wb").write(data)
    print(f"{len(entries)} motions, {len(data)} bytes -> {a.out}")


if __name__ == "__main__":
    main()
