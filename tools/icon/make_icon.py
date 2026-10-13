#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Melee 4K's app icon: a game disc, red and black, with a halftone where
the two meet and the disc's centre ring. Original vector art, no logos or
text. Every variant is drawn from code, so the icon can be rebuilt exactly.

    python3 tools/icon/make_icon.py --variant diagonal --out DIR
        writes DIR/icon.svg and DIR/icon-1024.png
    python3 tools/icon/make_icon.py --variant diagonal --install
        also writes platforms/macos/melee.icns and platforms/windows/melee.ico

Variants: diagonal, vertical, wave, burst. Needs rsvg-convert (librsvg) and,
for --install, iconutil (macOS) and Pillow.
"""
import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZE = 1024
CX = CY = SIZE / 2
R_DISC = 468      # the disc
R_HUB = 150       # the clear ring around the hole
R_HOLE = 64       # the hole
RED_HI, RED_LO = "#e8303a", "#a3121c"
BLACK_HI, BLACK_LO = "#2b2b2f", "#0c0c0e"
DOT_STEP = 26     # halftone grid pitch
BAND = 170        # how far the halftone reaches either side of the split


def side(variant, x, y):
    """Signed distance-like value from the red/black boundary: > 0 is red."""
    dx, dy = x - CX, y - CY
    if variant == "diagonal":
        return -(dx + dy) / math.sqrt(2)  # top left red
    if variant == "vertical":
        return -dx  # left red
    if variant == "wave":
        return -(dy - 70 * math.sin(dx / R_DISC * math.pi * 1.2))  # top red
    if variant == "burst":
        return 300 - math.hypot(dx, dy)  # red middle, black rim
    raise ValueError(variant)


def boundary_path(variant):
    """The red region as an SVG path, clipped later to the disc."""
    if variant == "diagonal":
        return f"M0 0 L{SIZE} 0 L0 {SIZE} Z"
    if variant == "vertical":
        return f"M0 0 L{CX} 0 L{CX} {SIZE} L0 {SIZE} Z"
    if variant == "wave":
        pts = []
        for i in range(0, 101):
            x = SIZE * i / 100
            y = CY + 70 * math.sin((x - CX) / R_DISC * math.pi * 1.2)
            pts.append(f"{x:.1f} {y:.1f}")
        return "M0 0 L" + " L".join(pts) + f" L{SIZE} 0 Z"
    if variant == "burst":
        return f"M{CX - 300} {CY} a300 300 0 1 0 600 0 a300 300 0 1 0 -600 0 Z"
    raise ValueError(variant)


def halftone(variant):
    """Dots across the boundary: black dots on the red side and red dots on
    the black, each shrinking away from it."""
    out = []
    step = DOT_STEP
    # A grid turned 45 degrees, the way printed halftone is.
    n = int(SIZE / step) + 4
    for i in range(-n, n):
        for j in range(-n, n):
            u, v = i * step, j * step
            x = CX + (u - v) / math.sqrt(2)
            y = CY + (u + v) / math.sqrt(2)
            r = math.hypot(x - CX, y - CY)
            if r > R_DISC - 10 or r < R_HUB + 12:
                continue
            d = side(variant, x, y)
            if abs(d) > BAND:
                continue
            t = 1 - abs(d) / BAND          # 1 at the boundary, 0 at the band's edge
            radius = step * 0.5 * (0.18 + 0.82 * t ** 1.4)
            color = BLACK_LO if d > 0 else RED_LO
            out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius:.2f}" fill="{color}"/>')
    return "\n    ".join(out)


def svg(variant):
    rim = R_DISC
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{SIZE}" height="{SIZE}" viewBox="0 0 {SIZE} {SIZE}">
  <defs>
    <radialGradient id="red" cx="35%" cy="30%" r="80%">
      <stop offset="0" stop-color="{RED_HI}"/><stop offset="1" stop-color="{RED_LO}"/>
    </radialGradient>
    <radialGradient id="black" cx="60%" cy="70%" r="80%">
      <stop offset="0" stop-color="{BLACK_HI}"/><stop offset="1" stop-color="{BLACK_LO}"/>
    </radialGradient>
    <linearGradient id="gloss" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#ffffff" stop-opacity="0.38"/>
      <stop offset="0.45" stop-color="#ffffff" stop-opacity="0.06"/>
      <stop offset="0.55" stop-color="#ffffff" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="hub" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#f4f4f6"/><stop offset="0.5" stop-color="#b9bcc4"/>
      <stop offset="1" stop-color="#eceef2"/>
    </linearGradient>
    <clipPath id="disc"><circle cx="{CX}" cy="{CY}" r="{rim}"/></clipPath>
    <mask id="hole">
      <rect width="{SIZE}" height="{SIZE}" fill="#fff"/>
      <circle cx="{CX}" cy="{CY}" r="{R_HOLE}" fill="#000"/>
    </mask>
    <filter id="shadow" x="-10%" y="-10%" width="120%" height="125%">
      <feDropShadow dx="0" dy="14" stdDeviation="16" flood-color="#000" flood-opacity="0.35"/>
    </filter>
  </defs>
  <g mask="url(#hole)" filter="url(#shadow)">
    <g clip-path="url(#disc)">
      <rect width="{SIZE}" height="{SIZE}" fill="url(#black)"/>
      <path d="{boundary_path(variant)}" fill="url(#red)"/>
      <g>
    {halftone(variant)}
      </g>
      <circle cx="{CX}" cy="{CY}" r="{rim}" fill="url(#gloss)"/>
    </g>
    <circle cx="{CX}" cy="{CY}" r="{rim - 4}" fill="none" stroke="#ffffff" stroke-opacity="0.22" stroke-width="8"/>
    <circle cx="{CX}" cy="{CY}" r="{R_HUB}" fill="url(#hub)"/>
    <circle cx="{CX}" cy="{CY}" r="{R_HUB - 22}" fill="none" stroke="#8d9099" stroke-width="3"/>
    <circle cx="{CX}" cy="{CY}" r="{R_HOLE + 18}" fill="#d9dbe0" stroke="#7d8089" stroke-width="3"/>
  </g>
</svg>
'''


def render(svg_path, png_path, size):
    subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), svg_path, "-o", png_path],
                   check=True)


def install(svg_path):
    from PIL import Image
    work = tempfile.mkdtemp(prefix="melee-icon-")
    try:
        iconset = os.path.join(work, "melee.iconset")
        os.makedirs(iconset)
        for base in (16, 32, 128, 256, 512):
            render(svg_path, os.path.join(iconset, f"icon_{base}x{base}.png"), base)
            render(svg_path, os.path.join(iconset, f"icon_{base}x{base}@2x.png"), base * 2)
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o",
                        os.path.join(ROOT, "platforms", "macos", "melee.icns")], check=True)
        big = os.path.join(work, "256.png")
        render(svg_path, big, 256)
        Image.open(big).save(os.path.join(ROOT, "platforms", "windows", "melee.ico"),
                             sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                                    (128, 128), (256, 256)])
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--variant", default="diagonal",
                    choices=["diagonal", "vertical", "wave", "burst"])
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "icon"))
    ap.add_argument("--install", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    svg_path = os.path.join(a.out, f"icon-{a.variant}.svg")
    with open(svg_path, "w") as f:
        f.write(svg(a.variant))
    render(svg_path, os.path.join(a.out, f"icon-{a.variant}-1024.png"), SIZE)
    if a.install:
        install(svg_path)
    print(svg_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
