#!/usr/bin/env python3
"""Lay out the community and AI texture layers from the choices page.

    python3 tools/textures/make_choices.py OUT --plan PAGE/plan.json
        [--choices melee4k-texture-choices.json] --base MERGED_DIR
        [--base-ai DIR ...] [--ai DIR ...]

PAGE/plan.json (choices_page.py) lists every texture that has a community
version, with its versions and the page's starting choice; --choices is the
file the page saves, and textures it does not name keep that starting
choice. OUT gets two merged packs, hard-linked like merge.py's:

  OUT/community  the chosen version of every planned texture, plus whatever
                 the current merged pack (--base, merge.py output with its
                 MANIFEST.json) holds that the plan does not cover (stock
                 icons, which the Stocks setting chooses). Textures --base
                 took from an AI source (--base-ai) are left to OUT/ai.
  OUT/ai         AI upscales (--ai, highest priority first) of textures with
                 no community version, the AI fill.

A texture chosen as "Original" is in neither, so the game draws the disc's
own. OUT/report.json counts what came from where. Run restore_alpha.py on
OUT/community and encode.py on both, then install them as
textures/packs/10-community and textures/packs/20-ai-fill.
"""
import argparse
import collections
import glob
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texpack import walk_pack  # noqa: E402


def place(src, dst):
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--plan", required=True)
    ap.add_argument("--choices")
    ap.add_argument("--base", required=True, help="the current merged pack (merge.py output)")
    ap.add_argument("--base-ai", action="append", default=[],
                    help="a source directory of --base that is AI output")
    ap.add_argument("--ai", action="append", default=[], help="AI upscale directories")
    args = ap.parse_args()

    if os.path.exists(args.out):
        sys.exit(f"{args.out} exists")
    plan = json.load(open(args.plan))
    choices = json.load(open(args.choices))["choices"] if args.choices else {}
    unknown = [k for k in choices if k not in plan]
    community, excluded, came = {}, set(), collections.Counter()
    for key, entry in plan.items():
        want = choices.get(key, entry["default"])
        option = next((o for o in entry["options"] if o["id"] == want), None)
        if option is None:
            option = next(o for o in entry["options"] if o["id"] == entry["default"])
            came["unknown choice, kept the default"] += 1
        if option["file"] is None:
            excluded.add(key)
            came["Original"] += 1
            continue
        community[key] = option["file"]
        came[option["id"]] += 1

    base = json.load(open(os.path.join(args.base, "MANIFEST.json")))["textures"]
    base_ai = {os.path.abspath(d) for d in args.base_ai}
    kept = 0
    for key, entry in base.items():
        if key in community or key in excluded or os.path.abspath(entry["source"]) in base_ai:
            continue
        community[key] = os.path.join(args.base, os.path.basename(entry["file"]))
        kept += 1
    came["current pack, not on the page"] = kept

    ai = {}
    for root in args.ai:
        for path, name, _ in walk_pack(root):
            if name is None or name.mip is not None:
                continue
            if name.key in community or name.key in excluded or name.key in ai:
                continue
            ai[name.key] = path

    for layer, chosen in (("community", community), ("ai", ai)):
        out = os.path.join(args.out, layer)
        os.makedirs(out)
        for key, src in chosen.items():
            ext = os.path.splitext(src)[1]
            place(src, os.path.join(out, key + ext))
            # A base texture's mip sidecars travel with it (merge.py layout).
            for mip in glob.glob(os.path.join(os.path.dirname(src), key + "_mip*")):
                place(mip, os.path.join(out, os.path.basename(mip)))
    report = {"community": len(community), "ai": len(ai), "original": len(excluded),
              "by_choice": dict(came.most_common()), "choices_not_in_plan": len(unknown)}
    with open(os.path.join(args.out, "report.json"), "w") as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    sys.exit(main())
