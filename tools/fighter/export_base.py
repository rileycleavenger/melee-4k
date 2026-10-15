#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export a base fighter's skeleton and costume meshes from your disc, for
Blender (blender_import_base.py) and as the template build_dat.py writes a
new fighter over. Nothing it writes goes in the repo.

    .venv/bin/python tools/fighter/export_base.py DISC Mr OUT_DIR

OUT_DIR gets PlXx.dat, PlXxNr.dat (the template), PlXxAJ.dat (its
animations, for build_moves.py), skeleton.json and
meshes.json (bind-space triangles with UVs and bone weights per DObj).
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "textures"))
import hsd  # noqa: E402
import model  # noqa: E402


def disc_file(disc, name):
    for path, off, size in disc.files():
        if path.lower() == name.lower():
            return disc.read(off, size)
    raise SystemExit(f"{name} is not on the disc")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("disc")
    ap.add_argument("base", help="two-letter fighter code, e.g. Mr")
    ap.add_argument("out")
    args = ap.parse_args()
    from extract import Disc
    disc = Disc(args.disc)
    os.makedirs(args.out, exist_ok=True)
    nr = disc_file(disc, f"Pl{args.base}Nr.dat")
    open(os.path.join(args.out, f"Pl{args.base}Nr.dat"), "wb").write(nr)
    open(os.path.join(args.out, f"Pl{args.base}.dat"), "wb").write(disc_file(disc, f"Pl{args.base}.dat"))
    open(os.path.join(args.out, f"Pl{args.base}AJ.dat"), "wb").write(disc_file(disc, f"Pl{args.base}AJ.dat"))
    a = hsd.Archive(nr)
    sym = next(s for s in a.symbols if s.endswith("_Share_joint") and "matanim" not in s)
    skel = model.read_skeleton(a, sym)
    meshes = model.read_meshes(a, skel, sym)
    json.dump({"base": args.base, "joint_symbol": sym,
               "joints": [{k: j[k] for k in ("index", "parent", "flags", "world", "ibm")} for j in skel]},
              open(os.path.join(args.out, "skeleton.json"), "w"), indent=0)
    json.dump([{k: m[k] for k in ("dobj", "pobj", "tris")} for m in meshes],
              open(os.path.join(args.out, "meshes.json"), "w"))
    tris = sum(len(m["tris"]) for m in meshes)
    print(f"{len(skel)} joints, {len(meshes)} pobjs, {tris} triangles -> {args.out}")


if __name__ == "__main__":
    main()
