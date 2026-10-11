#!/usr/bin/env python3
"""Package an installed-layout textures folder for Setup to download.

    python3 tools/setup/make_pack.py TEXTURES_DIR OUT_DIR --version 1.0.0
                                     --url-base https://github.com/OWNER/REPO/releases/download/TAG/
                                     [--part-size-mb 1900]

TEXTURES_DIR is laid out the way the game reads it (packs/<NN-name>/,
stocks/<style>/). OUT_DIR gets melee4k-textures-<version>-partN.zip, each
under GitHub's 2 GB release-asset limit, plus manifest.json listing every
part's URL, size and SHA-256 (tools/setup/setup.py checks all three). Files
are stored, not deflated: BC7 DDS barely compresses and storing keeps
unpacking fast.
"""
import argparse
import hashlib
import json
import os
import sys
import zipfile


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("textures")
    ap.add_argument("out")
    ap.add_argument("--version", required=True)
    ap.add_argument("--url-base", required=True)
    ap.add_argument("--part-size-mb", type=int, default=1900)
    args = ap.parse_args()

    files = []
    for dirpath, _, names in os.walk(args.textures, followlinks=True):
        for n in sorted(names):
            if n.startswith(".") or n == "MANIFEST.json":
                continue
            full = os.path.join(dirpath, n)
            files.append((os.path.relpath(full, args.textures).replace(os.sep, "/"), full))
    files.sort()
    os.makedirs(args.out, exist_ok=True)
    limit = args.part_size_mb << 20
    parts, current, size = [], None, 0
    for rel, full in files:
        fsize = os.path.getsize(full)
        if current is None or size + fsize > limit:
            if current:
                current.close()
            name = f"melee4k-textures-{args.version}-part{len(parts) + 1}.zip"
            parts.append(name)
            current = zipfile.ZipFile(os.path.join(args.out, name), "w", zipfile.ZIP_STORED)
            size = 0
        current.write(full, rel)
        size += fsize
    if current:
        current.close()

    manifest = {"version": args.version, "parts": []}
    for name in parts:
        path = os.path.join(args.out, name)
        manifest["parts"].append({"name": name, "url": args.url_base + name,
                                  "size": os.path.getsize(path), "sha256": sha256(path)})
    with open(os.path.join(args.out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    total = sum(p["size"] for p in manifest["parts"]) >> 20
    print(f"{len(files)} files in {len(parts)} part(s), {total} MB -> {args.out}")


if __name__ == "__main__":
    sys.exit(main())
