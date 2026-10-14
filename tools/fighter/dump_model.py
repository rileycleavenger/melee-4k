#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print a fighter costume's model structure: joints, DObjs, materials,
textures, PObj types and vertex formats.

    python3 tools/fighter/dump_model.py DISC PlMrNr.dat [--joint SYMBOL]
    python3 tools/fighter/dump_model.py path/to/PlGnNr.dat
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "textures"))
import hsd  # noqa: E402

ATTR = {9: "POS", 10: "NRM", 11: "CLR0", 12: "CLR1", 13: "TEX0", 14: "TEX1", 0: "PNMTXIDX",
        25: "NBT"}


def load(args):
    if args.file is None:
        return open(args.disc, "rb").read()
    from extract import Disc
    d = Disc(args.disc)
    for path, off, size in d.files():
        if path.lower() == args.file.lower():
            return d.read(off, size)
    raise SystemExit(f"{args.file} not on the disc")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("disc")
    ap.add_argument("file", nargs="?")
    ap.add_argument("--joint")
    ap.add_argument("--out", help="also save the raw file here")
    args = ap.parse_args()
    data = load(args)
    if args.out:
        open(args.out, "wb").write(data)
    a = hsd.Archive(data)
    print("symbols:", a.symbols)
    sym = args.joint or next(s for s in a.symbols if s.endswith("_Share_joint") and "matanim" not in s)
    joints = hsd.read_joint_tree(a, a.symbols[sym])
    tri = 0
    for j in joints:
        line = (f"{'  ' * j['depth']}J{j['index']:02d} flags={j['flags']:08x} "
                f"pos=({j['pos'][0]:.2f},{j['pos'][1]:.2f},{j['pos'][2]:.2f}) "
                f"ibm={'y' if j['mtx'] is not None else 'n'}")
        print(line)
        for di, d in enumerate(j["dobjs"]):
            m = d["mobj"]
            texs = []
            for t in (m["tex"] if m else []):
                tt = hsd.read_tobj(a, t)
                texs.append(f"{tt.get('w')}x{tt.get('h')}f{tt.get('fmt')}")
            print(f"{'  ' * j['depth']}  D{di} rm={m['rendermode']:08x} tex={texs}")
            for p in d["pobjs"]:
                attrs = ",".join(f"{ATTR.get(v['attr'], v['attr'])}:{v['type']}/{v['comp']}"
                                 for v in p["verts"])
                env = len(p.get("envelopes", []))
                print(f"{'  ' * j['depth']}    P {p['kind']} flags={p['flags']:04x} "
                      f"dl={p['n_display'] * 32}B env={env} [{attrs}]")
    print(f"{len(joints)} joints, {sum(len(j['dobjs']) for j in joints)} dobjs")


if __name__ == "__main__":
    main()
