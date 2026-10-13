#!/usr/bin/env python3
"""Stability soak: play every stage, character and mode, and report failures.

    python3 tools/soak.py DISC WORK_DIR [--only REGEX] [--frames 1800] [--game build/macos/melee]
                                        [--texture-dir DIR] [--repeat N]

Each case boots straight into a mode (MELEE_BOOT_SCENE) with the checks that
turn silent corruption into a log line or an abort:

  MELEE_NET_RENDER_AUDIT=1  simulation state must not change while drawing
  MELEE_HEAP_CHECK=1        canaries on every heap allocation
  MELEE_CAM_BONE=1          names the fighter when a camera point goes bad
  MELEE_UNLOCK_ALL=1        every character, stage and trophy is reachable

Cases:
  vs/*        the debug VS match, four level-9 CPUs, every costume of every
              character spread over every playable stage
  classic, training
              their boot hooks seed the pick themselves
  1p/*        Adventure, All-Star, Target Test, Home-Run, Multi-Man: boot to the
              mode's character select and pick through MELEE_KEY_FIFO (the
              hand starts at the mouse pointer, so the script pushes it to a corner
              first; stick time picks the portrait)
  menu/*      menu-only modes (main menu, trophies, tournament, ...)

A case fails on a non-zero exit, a timeout, a failure line in its output
(panic, assertion, heap stomp, render audit, bad camera point), or when it
never reached its gameplay scene (the "[scene] kind" line, gmscene.c), so a
pick script that stalls on a menu is not counted as a pass. On macOS the
newest crash report written during the case is attached (top stack frames).
WORK_DIR/soak.json holds the results, so an interrupted soak resumes; the
summary goes to WORK_DIR/summary.md.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "textures"))
from sweep import COSTUMES, STAGES  # noqa: E402

FAIL_PATTERNS = re.compile(
    r"PANIC|assertion .* failed|heap check|HEAP CHECK|canary|render audit|"
    r"\[cam-bone\]|bad pos3d|REPLAY DIVERGED|Segmentation|Abort",
    re.IGNORECASE,
)

# 1P character select: stick time (ms) to reach a portrait from the hand's
# starting point, then A, then Start until the mode begins.
ONE_P_MODES = ["adventure", "allstar", "target", "homerun", "multiman10", "multiman100",
               "multiman3min", "multiman15min", "endless", "cruel"]
MENU_MODES = ["menu", "trophies", "lottery", "collection", "tournament", "camera", "vs-css",
              "special-stamina", "event"]


GS_VS, GS_TRAINING = 0x02, 0x04


def cases(frames):
    out = []
    slots = [(ck, color) for ck, (_, n) in enumerate(COSTUMES) for color in range(n)]
    groups = [slots[i:i + 4] for i in range(0, len(slots), 4)]
    for i in range(max(len(groups), len(STAGES))):
        stage, group = STAGES[i % len(STAGES)], groups[i % len(groups)]
        env = {"MELEE_BOOT_SCENE": "vs",
               "MELEE_DEBUG_VS_CHARS": ",".join(f"{c}:{k}" for c, k in group)}
        if stage:
            env["MELEE_DEBUG_VS_STAGE"] = str(stage)
        who = "+".join(f"{COSTUMES[c][0].replace(' ', '')}{k}" for c, k in group)
        out.append({"name": f"vs/stage{stage:02x}-{who}", "env": env, "frames": frames,
                    "expect": GS_VS})
    out.append({"name": "classic", "env": {"MELEE_BOOT_SCENE": "classic"}, "frames": frames * 2,
                "expect": GS_VS})
    out.append({"name": "training", "env": {"MELEE_BOOT_SCENE": "training"}, "frames": frames,
                "expect": GS_TRAINING})
    for i, mode in enumerate(ONE_P_MODES):
        # Vary the portrait between modes: 7 columns, ~55 ms of stick each,
        # along the top row. The hand starts wherever the mouse pointer is,
        # so first push it into the top-left corner, then down onto the row.
        right = 120 + (i * 3 % 7) * 55
        script = ["Up+Left 900", "Down 110", f"Right {right}", "X 150"] + ["Return 150"] * 4
        out.append({"name": f"1p/{mode}", "env": {"MELEE_BOOT_SCENE": mode},
                    "frames": frames * 2, "fifo": script, "fifo_delay": 9.0, "expect": GS_VS})
    for mode in MENU_MODES:
        out.append({"name": f"menu/{mode}", "env": {"MELEE_BOOT_SCENE": mode}, "frames": 900})
    return out


def newest_crash_report(since):
    reports = glob.glob(os.path.expanduser("~/Library/Logs/DiagnosticReports/melee-*.ips"))
    reports = [r for r in reports if os.path.getmtime(r) >= since]
    return max(reports, key=os.path.getmtime) if reports else None


def crash_stack(path, depth=8):
    try:
        with open(path) as f:
            header, body = f.read().split("\n", 1)
        d = json.loads(body)
        th = d["threads"][d.get("faultingThread", 0)]
        imgs = d["usedImages"]
        frames = [f"{imgs[f['imageIndex']].get('name')} {f.get('symbol')}+{f.get('symbolLocation')}"
                  for f in th["frames"][:depth]]
        exc = d.get("exception", {})
        return [f"{exc.get('type')} {exc.get('signal')} {exc.get('subtype', '')}".strip()] + frames
    except Exception as e:  # a report we cannot parse is still worth naming
        return [f"(unparsed crash report {path}: {e})"]


def feed_fifo(path, script, delay, stop):
    time.sleep(delay)
    for line in script:
        if stop.is_set():
            return
        for _ in range(200):
            try:
                fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
                break
            except OSError:
                time.sleep(0.05)
        else:
            return
        os.write(fd, (line + "\n").encode())
        os.close(fd)
        time.sleep(0.9)


def run_case(case, args, logs):
    log_path = os.path.join(logs, case["name"].replace("/", "_") + ".log")
    env = dict(os.environ,
               MELEE_NET_RENDER_AUDIT="1", MELEE_HEAP_CHECK="1", MELEE_CAM_BONE="1",
               MELEE_UNLOCK_ALL="1", MELEE_FPS="1",
               MELEE_EXIT_AFTER_FRAMES=str(case["frames"]))
    env.update(case["env"])
    if args.texture_dir:
        env["MELEE_TEXTURE_DIR"] = args.texture_dir
    stop = threading.Event()
    feeder = None
    fifo_dir = None
    if case.get("fifo"):
        fifo_dir = tempfile.mkdtemp()
        fifo = os.path.join(fifo_dir, "keys")
        os.mkfifo(fifo)
        env["MELEE_KEY_FIFO"] = fifo
        feeder = threading.Thread(target=feed_fifo, args=(fifo, case["fifo"], case["fifo_delay"], stop),
                                  daemon=True)
        feeder.start()
    start = time.time()
    with open(log_path, "w") as log:
        try:
            rc = subprocess.run([args.game, args.disc], env=env, stdout=log, stderr=subprocess.STDOUT,
                                timeout=args.timeout).returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
    stop.set()
    if fifo_dir:
        try:
            os.unlink(os.path.join(fifo_dir, "keys"))
            os.rmdir(fifo_dir)
        except OSError:
            pass
    with open(log_path, errors="replace") as f:
        text = f.read()
    hits = sorted({m.group(0) for m in FAIL_PATTERNS.finditer(text)})
    scenes = [int(k, 16) for k in re.findall(r"\[scene\] kind 0x([0-9a-f]+)", text)]
    if case.get("expect") is not None and case["expect"] not in scenes:
        hits.append(f"never reached scene 0x{case['expect']:02x} (saw {sorted(set(scenes))})")
    ticks = [float(x) for x in re.findall(r"fps [0-9.]+ ticks ([0-9.]+)", text)[3:]]
    result = {"exit": rc, "seconds": round(time.time() - start), "failures": hits,
              "ticks_per_s": round(sum(ticks) / len(ticks), 2) if ticks else None,
              "scenes": scenes,
              "log": os.path.relpath(log_path, args.work)}
    if rc != 0 and sys.platform == "darwin":
        # ReportCrash writes the report a few seconds after the process dies.
        report = None
        for _ in range(20):
            report = newest_crash_report(start)
            if report:
                break
            time.sleep(0.5)
        if report:
            result["crash"] = crash_stack(report)
    result["ok"] = rc == 0 and not hits
    return result


def write_summary(state, path):
    runs = state["runs"]
    bad = {k: v for k, v in runs.items() if not v["ok"]}
    lines = [f"# Soak summary: {len(runs) - len(bad)}/{len(runs)} passed", ""]
    for name, r in sorted(bad.items()):
        lines.append(f"## {name}: exit {r['exit']}")
        lines += [f"- {h}" for h in r["failures"]]
        lines += [f"    {c}" for c in r.get("crash", [])]
        lines.append(f"- log: {r['log']}")
        lines.append("")
    slow = [(k, v["ticks_per_s"]) for k, v in runs.items()
            if v.get("ticks_per_s") is not None and v["ticks_per_s"] < 59.5]
    if slow:
        lines.append("## Below 59.5 ticks/s")
        lines += [f"- {k}: {t}" for k, t in sorted(slow)]
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc")
    ap.add_argument("work")
    ap.add_argument("--only", help="regex over case names")
    ap.add_argument("--frames", type=int, default=1800)
    ap.add_argument("--game", default="build/macos/melee")
    ap.add_argument("--texture-dir")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--repeat", type=int, default=1, help="run each case N times (rare crashes)")
    args = ap.parse_args()

    os.makedirs(args.work, exist_ok=True)
    logs = os.path.join(args.work, "logs")
    os.makedirs(logs, exist_ok=True)
    state_path = os.path.join(args.work, "soak.json")
    state = json.load(open(state_path)) if os.path.exists(state_path) else {"runs": {}}

    todo = [c for c in cases(args.frames) if not args.only or re.search(args.only, c["name"])]
    for rep in range(args.repeat):
        for n, case in enumerate(todo):
            key = case["name"] + (f"#{rep}" if args.repeat > 1 else "")
            if key in state["runs"]:
                continue
            r = run_case(dict(case, name=key), args, logs)
            state["runs"][key] = r
            with open(state_path, "w") as f:
                json.dump(state, f, indent=1)
            status = "ok" if r["ok"] else f"FAIL exit={r['exit']} {r['failures']}"
            print(f"[{n + 1}/{len(todo)}] {key}: {status} ({r['seconds']}s, {r['ticks_per_s']} ticks/s)",
                  flush=True)
    write_summary(state, os.path.join(args.work, "summary.md"))
    bad = [k for k, v in state["runs"].items() if not v["ok"]]
    print(f"{len(state['runs']) - len(bad)}/{len(state['runs'])} passed; see {args.work}/summary.md")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
