#!/usr/bin/env python3
"""Merge Dolphin-format texture packs into one directory by priority.

    python3 tools/textures/merge.py OUT_DIR SRC [SRC ...] [--copy]

Sources are directories in priority order, highest first: the first source
that has a texture key wins it, along with that source's mip sidecars for the
key. Pass a pack's option folders (a stock-icon style, say) before the pack
itself so they override it, the way the pack's README says to copy them over.

Only loadable `tex1_*` names are taken; legacy GALE01_<crc> names, material
maps, `sd_tex1_*` references and renamed copies are skipped and counted.
Files are hard-linked (same volume) unless --copy, so a merge costs no space.
OUT_DIR must not exist. OUT_DIR/MANIFEST.json records every key's source.
"""
import argparse
import collections
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texpack import walk_pack  # noqa: E402


def collect(src):
    """key -> {"base": path, "mips": {suffix: path}} for one source."""
    found = {}
    dupes = skipped = 0
    for path, name, _legacy in sorted(walk_pack(src)):
        if name is None:
            skipped += 1
            continue
        entry = found.setdefault(name.key, {"base": None, "mips": {}})
        if name.mip is None:
            if entry["base"] is not None:
                dupes += 1
                continue
            entry["base"] = path
        else:
            entry["mips"].setdefault(name.mip, path)
    found = {k: v for k, v in found.items() if v["base"] is not None}
    return found, dupes, skipped


def place(src, dst, copy):
    if copy:
        shutil.copy2(src, dst)
        return
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("sources", nargs="+")
    ap.add_argument("--copy", action="store_true", help="copy instead of hard-linking")
    args = ap.parse_args()

    if os.path.exists(args.out):
        sys.exit(f"{args.out} exists; pick a new directory")
    os.makedirs(args.out)

    manifest = {}
    won = collections.Counter()
    for src in args.sources:
        found, dupes, skipped = collect(src)
        for key, entry in found.items():
            if key in manifest:
                continue
            base = entry["base"]
            ext = os.path.splitext(base)[1].lower()
            place(base, os.path.join(args.out, key + ext), args.copy)
            for mip, path in entry["mips"].items():
                place(path, os.path.join(args.out, f"{key}_{mip}{os.path.splitext(path)[1].lower()}"),
                      args.copy)
            manifest[key] = {"source": src, "file": os.path.relpath(base, src)}
            won[src] += 1
        print(f"{won[src]:>6} won  {len(found):>6} offered  {dupes:>4} in-source dupes  "
              f"{skipped:>4} skipped  {src}")

    with open(os.path.join(args.out, "MANIFEST.json"), "w") as f:
        json.dump({"sources": args.sources, "textures": manifest}, f, indent=1, sort_keys=True)
    print(f"{len(manifest)} textures -> {args.out}")


if __name__ == "__main__":
    main()
