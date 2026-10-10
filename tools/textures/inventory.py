#!/usr/bin/env python3
"""Summarise Dolphin-format texture packs and how they overlap.

    python3 tools/textures/inventory.py PACK_DIR [PACK_DIR ...] [--json out.json]

Per pack: replaceable textures (unique keys), files, legacy-named files that
cannot load, wildcard names, image formats, the upscale factor distribution,
and keys whose file is not a whole multiple of the GameCube size. Then a
pairwise overlap matrix, so merge priorities are a decision rather than a
guess. Nothing is modified.
"""
import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texpack import FORMAT_NAMES, dds_fourcc, image_size, walk_pack  # noqa: E402


def scan(root):
    keys = {}
    files = legacy = other = wildcard = odd_scale = 0
    exts = collections.Counter()
    scales = collections.Counter()
    fmts = collections.Counter()
    dds = collections.Counter()
    for path, name, is_legacy in walk_pack(root):
        if name is None:
            if is_legacy:
                legacy += 1
            else:
                other += 1
            continue
        files += 1
        exts[name.ext] += 1
        if name.is_wildcard:
            wildcard += 1
        if name.ext == "dds":
            dds[dds_fourcc(path) or "?"] += 1
        if name.mip is not None:
            continue
        size = image_size(path)
        if size is None:
            continue
        sx, sy = size[0] / name.width, size[1] / name.height
        if sx != sy or sx != int(sx):
            odd_scale += 1
            scales["non-integer"] += 1
        else:
            scales[f"{int(sx)}x"] += 1
        keys[name.key] = path
        fmts[FORMAT_NAMES.get(name.fmt, str(name.fmt))] += 1
    return {
        "keys": keys,
        "files": files,
        "legacy": legacy,
        "other": other,
        "wildcard": wildcard,
        "odd_scale": odd_scale,
        "exts": dict(exts),
        "scales": dict(scales.most_common()),
        "formats": dict(fmts.most_common()),
        "dds": dict(dds),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("packs", nargs="+")
    ap.add_argument("--json", help="write the per-pack key lists here")
    args = ap.parse_args()

    results = {}
    for root in args.packs:
        label = os.path.basename(os.path.normpath(root))
        results[label] = scan(root)

    for label, r in results.items():
        print(f"== {label}")
        print(f"   textures {len(r['keys'])}  files {r['files']}  "
              f"legacy-named {r['legacy']}  unrecognised {r['other']}  "
              f"wildcard {r['wildcard']}")
        print(f"   ext {r['exts']}  dds {r['dds'] or '-'}")
        print(f"   scale {r['scales']}")
        print(f"   gc formats {r['formats']}")

    labels = list(results)
    if len(labels) > 1:
        print("\noverlap (shared texture keys)")
        width = max(len(l) for l in labels)
        for a in labels:
            row = [len(results[a]["keys"].keys() & results[b]["keys"].keys())
                   if a != b else len(results[a]["keys"]) for b in labels]
            print(f"   {a:<{width}}  " + " ".join(f"{n:>6}" for n in row))
        union = set().union(*(r["keys"] for r in results.values()))
        print(f"\nunion of all packs: {len(union)} textures")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({l: sorted(r["keys"]) for l, r in results.items()}, f)


if __name__ == "__main__":
    main()
