#!/usr/bin/env python3
"""Play every stage and every costume to find textures no pack replaces.

    python3 tools/textures/sweep.py DISC PACK_DIR WORK_DIR [--frames 900] [--game build/macos/melee]

Runs the game once per group of four costumes (CPU level 9, so they fight and
spawn effects), cycling through the VS stages, with PACK_DIR loaded and
MELEE_TEXTURE_DUMP=1. Aurora writes each texture that has no replacement once
to WORK_DIR/cache/texture_dumps, so after the sweep that directory is exactly
the uncovered set. WORK_DIR/sweep.json records, per run, the setup, exit
status and which dumps it produced. Rerunning skips runs already recorded.

Menus, 1P modes and cutscenes are not reached by VS boots; play those by hand
with the same MELEE_TEXTURE_DIR / MELEE_TEXTURE_DUMP / MELEE_CACHE_DIR to add
their misses to the same directory.
"""
import argparse
import json
import os
import subprocess
import sys
import time

# CharacterKind order (src/melee/ft/forward.h) and costume counts
# (src/melee/ft/ftdata.c CostumeListsForeachCharacter).
COSTUMES = [
    ("Captain Falcon", 6), ("Donkey Kong", 5), ("Fox", 4), ("Game & Watch", 4),
    ("Kirby", 6), ("Bowser", 4), ("Link", 5), ("Luigi", 4), ("Mario", 5),
    ("Marth", 5), ("Mewtwo", 4), ("Ness", 4), ("Peach", 5), ("Pikachu", 4),
    ("Ice Climbers", 4), ("Jigglypuff", 5), ("Samus", 5), ("Yoshi", 6),
    ("Zelda", 5), ("Sheik", 5), ("Falco", 4), ("Young Link", 5),
    ("Dr. Mario", 5), ("Roy", 5), ("Pichu", 4), ("Ganondorf", 5),
]
# StKind 0x02..0x1F (src/melee/gr/forward.h); 0 means the debug default, Final Destination.
# 0x15 (Akaneia) has no stage data and crashes in Ground_801C0754; 0x1A (Icetop)
# never finishes loading. Their slots fall back to Final Destination.
UNPLAYABLE = {0x15, 0x1A}
STAGES = [0] + [0 if s in UNPLAYABLE else s for s in range(0x02, 0x20)]


def runs():
    slots = [(ck, color) for ck, (_, n) in enumerate(COSTUMES) for color in range(n)]
    groups = [slots[i:i + 4] for i in range(0, len(slots), 4)]
    total = max(len(groups), len(STAGES))
    for i in range(total):
        yield STAGES[i % len(STAGES)], groups[i % len(groups)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc")
    ap.add_argument("pack")
    ap.add_argument("work")
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--game", default="build/macos/melee")
    ap.add_argument("--timeout", type=int, default=180)
    args = ap.parse_args()

    os.makedirs(args.work, exist_ok=True)
    cache = os.path.join(args.work, "cache")
    dumps = os.path.join(cache, "texture_dumps")
    logs = os.path.join(args.work, "logs")
    os.makedirs(logs, exist_ok=True)
    state_path = os.path.join(args.work, "sweep.json")
    state = json.load(open(state_path)) if os.path.exists(state_path) else {"runs": {}}

    plan = list(runs())
    for n, (stage, group) in enumerate(plan):
        key = f"{stage}:" + ",".join(f"{c}:{k}" for c, k in group)
        if key in state["runs"]:
            continue
        before = set(os.listdir(dumps)) if os.path.isdir(dumps) else set()
        env = dict(os.environ,
                   MELEE_BOOT_SCENE="vs",
                   MELEE_DEBUG_VS_CHARS=",".join(f"{c}:{k}" for c, k in group),
                   MELEE_EXIT_AFTER_FRAMES=str(args.frames),
                   MELEE_TEXTURE_DIR=os.path.abspath(args.pack),
                   MELEE_TEXTURE_DUMP="1",
                   MELEE_CACHE_DIR=os.path.abspath(cache),
                   MELEE_LOG_FILE=os.path.join(logs, f"run{n:02d}.log"))
        if stage:
            env["MELEE_DEBUG_VS_STAGE"] = str(stage)
        else:
            env.pop("MELEE_DEBUG_VS_STAGE", None)
        start = time.time()
        try:
            rc = subprocess.run([args.game, args.disc], env=env, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, timeout=args.timeout).returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
        after = set(os.listdir(dumps)) if os.path.isdir(dumps) else set()
        new = sorted(after - before)
        who = ", ".join(f"{COSTUMES[c][0]} #{k}" for c, k in group)
        state["runs"][key] = {"stage": stage, "fighters": who, "exit": rc,
                              "new_dumps": new, "seconds": round(time.time() - start)}
        with open(state_path, "w") as f:
            json.dump(state, f, indent=1)
        print(f"[{n + 1}/{len(plan)}] stage {stage:#04x} exit {rc} +{len(new)} dumps  {who}", flush=True)

    total = len(os.listdir(dumps)) if os.path.isdir(dumps) else 0
    bad = [r for r in state["runs"].values() if r["exit"] != 0]
    print(f"done: {total} uncovered textures in {dumps}; {len(bad)} run(s) did not exit cleanly")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
