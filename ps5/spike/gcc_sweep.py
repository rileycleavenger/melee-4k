#!/usr/bin/env python3
"""Compile every melee_game translation unit for the PS5 with x86_64-elf-gcc.

    python3 ps5/spike/gcc_sweep.py [--build build/macos] [--jobs N]

Replays the game's compile commands from an existing host build
(compile_commands.json), retargeted the way the PS5 build will route them:
x86_64-elf-gcc, the ps5-payload-sdk libc headers, the SDK predefines and the
console ABI flags (see ps5/spike/build.sh). Host-only flags and include
directories are dropped. Objects go to build/ps5-sweep; failures are grouped
by their first error so the porting work can be sized.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SDK = os.environ.get("PS5_PAYLOAD_SDK", os.path.join(ROOT, ".deps/ps5-payload-sdk-v0.42/ps5-payload-sdk"))
GCC = os.environ.get("GCC", "x86_64-elf-gcc")
OUT = os.path.join(ROOT, "build/ps5-sweep")

HOST_PREFIXES = ("/opt/homebrew", "/opt/miniconda3", "/usr/local")
DROP_FLAGS = {"-fcolor-diagnostics", "-Wno-unknown-warning-option", "-Werror=format-security",
              "-Wno-unknown-attributes"}
DROP_WITH_VALUE = {"-arch", "-isysroot", "-o", "-c", "-MF", "-MT", "-MQ"}


def ps5_flags():
    gcc_include = subprocess.check_output([GCC, "-print-file-name=include"], text=True).strip()
    return [
        "-fPIC", "-fno-plt", "-fno-stack-protector", "-fshort-wchar",
        "-nostdinc", "-isystem", f"{SDK}/target/include", "-isystem", gcc_include,
        "-D__FreeBSD__=9", "-D__PROSPERO__=1",
        # GCC emits native TLS but the console build uses emulated TLS; this
        # makes the FreeBSD ctype headers call libc instead of reading a
        # _Thread_local (runetype.h).
        "-D__NO_TLS",
        "-fexec-charset=CP932", "-Wno-scalar-storage-order",
    ]


def retarget(command, source, obj):
    args = shlex.split(command)[1:]
    out = []
    skip = False
    for i, a in enumerate(args):
        if skip:
            skip = False
            continue
        if a in DROP_WITH_VALUE:
            skip = True
            continue
        if a in DROP_FLAGS or a in ("-MD", "-MMD") or a == source:
            continue
        if a in ("-I", "-isystem") and i + 1 < len(args) and args[i + 1].startswith(HOST_PREFIXES):
            skip = True
            continue
        if a.startswith(("-I", "-isystem")) and a.lstrip("-Isystem").startswith(HOST_PREFIXES):
            continue
        out.append(a)
    return [GCC] + ps5_flags() + out + ["-c", source, "-o", obj]


def compile_one(entry):
    source = entry["file"]
    rel = os.path.relpath(source, ROOT)
    obj = os.path.join(OUT, rel + ".o")
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    cmd = retarget(entry["command"], source, obj)
    p = subprocess.run(cmd, cwd=entry["directory"], capture_output=True, text=True)
    return rel, p.returncode, p.stderr


def first_error(stderr):
    for line in stderr.splitlines():
        m = re.search(r"(?:fatal )?error: (.*)", line)
        if m:
            return re.sub(r"'[^']*'", "'X'", m.group(1))[:140]
    return stderr.strip().splitlines()[-1][:140] if stderr.strip() else "?"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", default=os.path.join(ROOT, "build/macos"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    args = ap.parse_args()

    with open(os.path.join(args.build, "compile_commands.json")) as f:
        entries = [e for e in json.load(f) if "melee_game.dir" in e["command"]]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(compile_one, entries))

    failed = [(rel, err) for rel, rc, err in results if rc != 0]
    groups = collections.defaultdict(list)
    for rel, err in failed:
        groups[first_error(err)].append(rel)
    print(f"{len(entries) - len(failed)}/{len(entries)} compiled for PS5")
    for msg, files in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        print(f"{len(files):>4}  {msg}")
        for rel in files[:3]:
            print(f"        {rel}")
    if failed:
        with open(os.path.join(OUT, "failures.log"), "w") as f:
            for rel, err in failed:
                f.write(f"==== {rel}\n{err}\n")
        print(f"full output: {os.path.relpath(OUT, ROOT)}/failures.log")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
