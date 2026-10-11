#!/usr/bin/env python3
"""Interactive page for choosing between community versions of each texture.

    .venv/bin/python tools/textures/choices_page.py OUT_DIR --keys KEYS.txt --originals DIR
        --source "Label=DIR" [--source ...] [--current MANIFEST.json] [--alt "Label=DIR"]
        [--prefer Label] [--prefer-css Label]

Every `--source` is a Dolphin-format pack, highest priority first; a pack's
option folders can be given as their own source ("Melee HD (optional)=..."). A
texture is offered on the page when two or more sources hold visibly different
versions of it (versions that look the same are shown once, with every pack
that has them). `--alt` adds a set that is only offered, never chosen by
default: re-upscales of textures the AI version of which was rejected, say.

OUT_DIR/index.html groups textures by game area and disc file. Characters are
chosen per costume, with a per-texture override; everything else per
texture. Choices start at the version in the current build (`--current`, the
merged pack's MANIFEST.json) or else the first source in priority order,
`--prefer-css` for the character select screen. The page saves choices in the
browser as they are made and downloads them as melee4k-texture-choices.json,
which make_choices.py turns into a pack.

The page links each version's full-size file by absolute path and is meant to
be opened locally, never published: the textures are game artwork.
"""
import argparse
import collections
import hashlib
import json
import os
import sys
from multiprocessing import Pool

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texpack import walk_pack  # noqa: E402

THUMB = 176
CHAR_THUMB = 112
SAME = 1.5  # mean per-channel difference (of 255) below which two versions look the same
SIG = 128  # side of the image the difference is measured on

FIGHTERS = {
    "Ca": "Captain Falcon", "Cl": "Young Link", "Dk": "Donkey Kong", "Dr": "Dr. Mario",
    "Fc": "Falco", "Fe": "Roy", "Fx": "Fox", "Gn": "Ganondorf", "Gw": "Mr. Game & Watch",
    "Kb": "Kirby", "Kp": "Bowser", "Lg": "Luigi", "Lk": "Link", "Mr": "Mario", "Ms": "Marth",
    "Mt": "Mewtwo", "Nn": "Nana", "Ns": "Ness", "Pc": "Pichu", "Pe": "Peach", "Pk": "Pikachu",
    "Pp": "Popo", "Pr": "Jigglypuff", "Sk": "Sheik", "Ss": "Samus", "Ys": "Yoshi", "Zd": "Zelda",
    "Gk": "Giga Bowser", "Mh": "Master Hand", "Ch": "Crazy Hand", "Sb": "Sandbag",
}
COSTUMES = {
    "Nr": "Default", "Re": "Red", "Bu": "Blue", "Gr": "Green", "Bk": "Black", "Wh": "White",
    "Ye": "Yellow", "Pi": "Pink", "Or": "Orange", "La": "Lavender", "Aq": "Aqua", "Gy": "Gray",
}
SECTIONS = [
    ("flagged", "Rejected AI textures"),
    ("css", "Character select"),
    ("fighters", "Characters (one choice per costume)"),
    ("stages", "Stages"),
    ("menus", "Menus and HUD"),
    ("effects", "Effects and items"),
    ("other", "Everything else"),
]


def section_of(disc_file, flagged):
    if flagged:
        return "flagged"
    if disc_file.startswith(("MnSlChr", "MnSlMap")):
        return "css"
    if disc_file.startswith("Pl"):
        return "fighters"
    if disc_file.startswith("Gr"):
        return "stages"
    if disc_file.startswith(("Mn", "Gm", "If", "Sd", "Nt", "Lb")):
        return "menus"
    if disc_file.startswith(("Ef", "It", "Ir")):
        return "effects"
    return "other"


def fighter_title(disc_file):
    stem = disc_file.split(".")[0]
    who = FIGHTERS.get(stem[2:4], stem[2:4])
    costume = COSTUMES.get(stem[4:6], stem[4:6] or "Common")
    return f"{who} · {costume}"


def parse_source(spec):
    label, _, path = spec.partition("=")
    if not path or not os.path.isdir(path):
        sys.exit(f"bad --source {spec!r}: expected Label=DIR")
    return label, path


def signature(path):
    im = Image.open(path).convert("RGBA")
    a = np.asarray(im.resize((SIG, SIG), Image.BILINEAR), dtype=np.float32)
    a[..., :3] *= a[..., 3:4] / 255.0
    return im.size, a


def distinct(item):
    """Collapse one key's versions into those that look different."""
    key, versions = item
    groups = []  # [signature, [(label, path, size)]]
    for label, path, alt in versions:
        try:
            size, sig = signature(path)
        except Exception:  # noqa: BLE001 - an unreadable file is not offered
            continue
        for g in groups:
            # Offered-only versions are alternatives to compare, so they
            # merge only with a practically identical version.
            limit = SAME if not alt and not g[2] else 0.3
            if np.abs(g[0] - sig).mean() < limit:
                g[1].append((label, path, size))
                break
        else:
            groups.append([sig, [(label, path, size)], alt])
    return key, [g[1] for g in groups]


def thumb(job):
    src, dst, size = job
    if os.path.exists(dst):
        return
    im = Image.open(src).convert("RGBA")
    im.thumbnail((size, size), Image.LANCZOS if im.width > size else Image.NEAREST)
    if im.width < size and im.height < size:
        f = max(1, min(size // im.width, size // im.height))
        im = im.resize((im.width * f, im.height * f), Image.NEAREST)
    im.save(dst, "WEBP", quality=88, method=4)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--keys", required=True, help="extract.py KEYS.txt (key <TAB> disc file)")
    ap.add_argument("--originals", required=True, help="extract.py PNGs, shown for reference")
    ap.add_argument("--source", action="append", default=[], help="Label=DIR, priority order")
    ap.add_argument("--alt", action="append", default=[], help="Label=DIR, offered only")
    ap.add_argument("--current", help="MANIFEST.json of the pack in the current build")
    ap.add_argument("--current-label", action="append", default=[],
                    help="DIR=Label: name a --current source directory as a page label")
    ap.add_argument("--prefer-css", help="default label for the character select screen")
    ap.add_argument("--flagged", help="file of keys whose AI version was rejected")
    ap.add_argument("--skip-keys", action="append", default=[],
                    help="DIR: leave the textures in this pack off the page (stock icons, "
                         "which the Stocks setting chooses)")
    args = ap.parse_args()

    sources = [parse_source(s) for s in args.source]
    alts = [parse_source(s) for s in args.alt]
    disc_file = {}
    with open(args.keys) as f:
        for line in f:
            k, _, df = line.rstrip("\n").partition("\t")
            disc_file.setdefault(k, df)
    flagged = set(open(args.flagged).read().split()) if args.flagged else set()

    versions = collections.defaultdict(list)
    for label, root in sources + alts:
        alt = (label, root) in alts
        for path, name, _ in walk_pack(root):
            if name is not None and name.mip is None and "Stock Icons" not in path:
                versions[name.key].append((label, path, alt))
    skip = set()
    for root in args.skip_keys:
        skip.update(name.key for _, name, _ in walk_pack(root) if name is not None)
    versions = {k: v for k, v in versions.items() if k in disc_file and k not in skip}
    with Pool() as pool:
        grouped = dict(pool.map(distinct, versions.items(), chunksize=16))
    alt_labels = {label for label, _ in alts}
    offered = {k: g for k, g in grouped.items()
               if k in flagged or len([x for x in g if x[0][0] not in alt_labels]) > 1}

    current = {}
    if args.current:
        names = dict(s.split("=", 1) for s in args.current_label)
        for k, entry in json.load(open(args.current))["textures"].items():
            current[k] = names.get(entry["source"])
    order = [label for label, _ in sources]

    os.makedirs(os.path.join(args.out, "t"), exist_ok=True)
    jobs = []
    data = {"sections": [], "textures": {}}
    # Every texture with a community version, offered or not: what
    # make_choices.py builds the pack from.
    plan = {}
    for key, groups in grouped.items():
        options = []
        for g in groups:
            labels = sorted({label for label, _, _ in g},
                            key=lambda lb: order.index(lb) if lb in order else len(order))
            label, path, size = next(x for x in g if x[0] == labels[0])
            options.append({"id": labels[0], "also": labels[1:], "file": path, "size": size})
        if key in flagged:
            options.insert(0, {"id": "Original", "also": [], "file": None, "size": None})
        if all(o["id"] in alt_labels or o["id"] == "Original" for o in options) and \
                key not in flagged:
            continue  # only offered-only versions: nothing to build from
        sec = section_of(disc_file[key], key in flagged)
        ids = [o["id"] for o in options]
        want = (args.prefer_css if sec == "css" else None) or current.get(key)
        default = next((i for i in [want] + order if i in ids), ids[0])
        if key in flagged and default in alt_labels:
            default = "Original"
        plan[key] = {"options": options, "default": default}
    with open(os.path.join(args.out, "plan.json"), "w") as f:
        json.dump(plan, f, separators=(",", ":"))
    by_section = collections.defaultdict(lambda: collections.defaultdict(list))
    for key in sorted(offered, key=lambda k: (disc_file[k], k)):
        if key not in plan:
            continue
        df = disc_file[key]
        sec = section_of(df, key in flagged)
        options = []
        for o in plan[key]["options"]:
            tid = "o_" + key
            if o["file"] is not None:
                tid = hashlib.sha1(o["file"].encode()).hexdigest()[:16]
                jobs.append((o["file"], os.path.join(args.out, "t", tid + ".webp"),
                             CHAR_THUMB if sec == "fighters" else THUMB))
            options.append(dict(o, t=tid))
        default = plan[key]["default"]
        orig = os.path.join(args.originals, key + ".png")
        if os.path.exists(orig):
            jobs.append((orig, os.path.join(args.out, "t", "o_" + key + ".webp"),
                         CHAR_THUMB if sec == "fighters" else THUMB))
        data["textures"][key] = {"options": options, "default": default, "file": df,
                                 "orig": os.path.exists(orig)}
        group = fighter_title(df) if sec == "fighters" else df.split(".")[0]
        by_section[sec][group].append(key)
    for sec, title in SECTIONS:
        if sec in by_section:
            data["sections"].append({"id": sec, "title": title, "per_group": sec == "fighters",
                                     "groups": [{"name": n, "keys": ks}
                                                for n, ks in by_section[sec].items()]})
    with Pool() as pool:
        pool.map(thumb, jobs, chunksize=16)

    template = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                 "choices_page.html")).read()
    page = template.replace("/*DATA*/null", json.dumps(data, separators=(",", ":")))
    with open(os.path.join(args.out, "index.html"), "w") as f:
        f.write(page)
    n = collections.Counter(len(t["options"]) for t in data["textures"].values())
    print(f"{len(data['textures'])} textures with a choice ({dict(sorted(n.items()))} by "
          f"option count) -> {os.path.join(args.out, 'index.html')}")
    print("by section:", {s["id"]: sum(len(g["keys"]) for g in s["groups"])
                          for s in data["sections"]})


if __name__ == "__main__":
    sys.exit(main())
