#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The product's names agree everywhere product.env cannot reach.

product.env names the product (Melee 4K, or a fork's own game): CMake,
the packaging scripts, the Windows workflow and Setup read it. install.sh
is fetched on its own and repeats the values, and the release README
points at the installer; this checks both against product.env.

    python3 tools/test_product.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_env(path):
    values = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r'^([A-Z_]+)="?([^"]*)"?$', line.strip())
            if m:
                values[m.group(1)] = m.group(2)
    return values


def main():
    product = read_env(os.path.join(ROOT, "product.env"))
    install = read_env(os.path.join(ROOT, "install.sh"))
    errors = []
    for key in ("NAME", "FILE", "SLUG", "REPO"):
        if install.get(key) != product.get("PRODUCT_" + key):
            errors.append(f"install.sh {key}={install.get(key)!r}, product.env "
                          f"PRODUCT_{key}={product.get('PRODUCT_' + key)!r}")
    with open(os.path.join(ROOT, "platforms", "release", "README.txt"), encoding="utf-8") as f:
        readme = f.read()
    for url in re.findall(r"https://raw\.githubusercontent\.com/([^/]+/[^/]+)/", readme):
        if url != product["PRODUCT_REPO"]:
            errors.append(f"platforms/release/README.txt installs from {url}, "
                          f"product.env PRODUCT_REPO={product['PRODUCT_REPO']}")
    if product.get("PRODUCT_FIGHTER_PACKS") not in ("0", "1"):
        errors.append("product.env PRODUCT_FIGHTER_PACKS must be 0 or 1")
    for e in errors:
        print(e)
    print("product: " + ("FAIL" if errors else "ok"))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
