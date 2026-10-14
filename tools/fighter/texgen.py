# SPDX-License-Identifier: GPL-3.0-or-later
"""Procedural texture helpers for custom fighters (PIL + numpy): tileable
noise, wood grain, woven fabric, and flat painted colour, at HD size. A
character's make_textures.py (.claude/skills/make-fighter/templates/) builds
its images from these and paints faces and emblems on top with ImageDraw.

    sys.path.insert(0, f"{REPO}/tools/fighter"); from texgen import *
    wood(base=(204, 150, 96), dark=(160, 104, 58), rings=24).save("body.png")
    fabric((44, 92, 214)).save("cloth.png")
    flat((230, 40, 40), grain=0.04).save("metal.png")

UV conventions of tools/fighter/blender_parts.py: u = 0.5 is the part's
front (+Z); image y = (1 - v) * N, so the top of the image is v = 1.
"""
import math

import numpy as np
from PIL import Image

N = 2048
rng = np.random.default_rng(7)


def noise(n, scale, octaves=4):
    """Tileable value noise in [0,1]."""
    out = np.zeros((n, n))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cells = max(2, int(scale * (2 ** o)))
        grid = rng.random((cells, cells))
        img = Image.fromarray((grid * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC)
        # make tileable by wrapping the grid
        g = np.asarray(img, dtype=float) / 255.0
        out += amp * g
        total += amp
        amp *= 0.5
    return out / total


def wood(base=(196, 140, 86), dark=(150, 96, 52), rings=34.0, n=N):
    y, x = np.mgrid[0:n, 0:n] / n
    warp = noise(n, 3) * 0.08 + noise(n, 12, 2) * 0.015
    grain = np.sin((x + warp) * rings * 2 * math.pi) * 0.5 + 0.5
    fine = noise(n, 64, 2)
    t = np.clip(grain ** 3 * 0.75 + fine * 0.25, 0, 1)
    b = np.array(base, float)
    d = np.array(dark, float)
    rgb = b[None, None, :] * (1 - t[..., None]) + d[None, None, :] * t[..., None]
    shade = 0.92 + noise(n, 6, 3) * 0.16
    rgb = np.clip(rgb * shade[..., None], 0, 255)
    return Image.fromarray(rgb.astype(np.uint8), "RGB")


def fabric(color, n=N, weave=220):
    y, x = np.mgrid[0:n, 0:n]
    w = (np.sin(x * 2 * math.pi / n * weave) * np.sin(y * 2 * math.pi / n * weave)) * 0.035
    v = noise(n, 8, 3) * 0.12 - 0.06
    c = np.array(color, float)
    rgb = np.clip(c[None, None, :] * (1 + w + v)[..., None], 0, 255)
    return Image.fromarray(rgb.astype(np.uint8), "RGB")


def flat(color, n=N, grain=0.05):
    """A painted colour with a little low-frequency variation (plastic,
    painted metal, skin)."""
    v = noise(n, 6, 3) * grain * 2 - grain
    c = np.array(color, float)
    rgb = np.clip(c[None, None, :] * (1 + v)[..., None], 0, 255)
    return Image.fromarray(rgb.astype(np.uint8), "RGB")

