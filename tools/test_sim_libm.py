#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The simulation must not call a platform libm function that rounds.

Online play needs every machine to compute the same bits. IEEE makes
sqrt, floor, fmod and the like exact everywhere, but sin, log, fma and
the rest are each libm's own approximation: mingw-w64's msvcrt fmaf was one
ULP off the Mac's on a floor normal, and the v1.1.0-rc2 Mac-PC game
desynced on it. So the game library and aurora_mtx (collision maths) take
every such function from src/pc/libm (pc_trig.h routes them) or define it
themselves (lbtrigf.c, lb_00CE.c). This scans their object files for any
other math symbol they leave to the platform.

    python3 tools/test_sim_libm.py --build build/macos
    python3 tools/test_sim_libm.py --build build-win --nm x86_64-w64-mingw32-nm

Run it on an x86-64 build too: AArch64 compiles fmaf to an instruction, so
only there does a platform fmaf show up as a call.
"""
import argparse
import os
import re
import subprocess
import sys

# Functions that round: never from the platform. (Exact ones -- sqrt,
# floor, ceil, trunc, round, fmod, fabs, fmin/fmax, copysign, ldexp,
# frexp, modf -- are fine anywhere.)
ROUNDING = ["sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
            "asinh", "acosh", "atanh", "exp", "exp2", "expm1", "log", "log2", "log10", "log1p",
            "pow", "cbrt", "hypot", "fma", "erf", "erfc", "lgamma", "tgamma"]
NAMES = {f + s for f in ROUNDING for s in ("", "f", "l")} | {"_hypot", "_hypotf"}

# Objects outside the simulation: they draw or play sound only.
PRESENTATION = {"interp.c", "audio.c", "stb_vorbis.c", "axdriver.c"}


def objects(build):
    for sub in ("CMakeFiles/melee_game.dir", "extern/aurora/CMakeFiles/aurora_mtx.dir"):
        root = os.path.join(build, sub)
        for d, _, files in os.walk(root):
            for f in files:
                if f.endswith((".o", ".obj")):
                    yield os.path.join(d, f)


def symbols(nm, path, undefined):
    args = [nm, "-u", path] if undefined else [nm, "-g", "--defined-only", path]
    out = subprocess.run(args, capture_output=True, text=True).stdout
    names = set()
    for line in out.splitlines():
        name = line.split()[-1] if line.split() else ""
        names.add(re.sub(r"^_(?=[a-z])", "", name) if path.endswith(".o") and
                  sys.platform == "darwin" else name)
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", required=True)
    ap.add_argument("--nm", default="nm")
    a = ap.parse_args()
    objs = list(objects(a.build))
    if not objs:
        print(f"FAIL: no object files under {a.build}")
        return 1
    defined = set()
    for o in objs:
        defined |= symbols(a.nm, o, False)
    bad = []
    for o in objs:
        src = os.path.basename(o).rsplit(".", 1)[0]
        if src in PRESENTATION:
            continue
        for name in sorted(symbols(a.nm, o, True) & NAMES - defined):
            bad.append(f"{src}: {name}")
    for b in bad:
        print(f"FAIL: {b} comes from the platform libm (route it in src/pc/libm/pc_trig.h)")
    if bad:
        return 1
    print(f"test_sim_libm: ok ({len(objs)} objects)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
