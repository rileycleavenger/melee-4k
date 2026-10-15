#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A provided model's texture, Melee-styled: Melee bakes its shading into
its textures, so the "Melee-ified" costume gets the model's ambient
occlusion and surface relief painted into its colour map, a little richer
in colour and contrast.

    .venv/bin/python tools/fighter/melee_texture.py ALBEDO.png OUT.png
        [--ao AO.png] [--normal NORMAL.png] [--strength 1.0]

ALBEDO is the model's colour map. AO: its ambient-occlusion map (from the
model's files, or baked: blender_retarget.py bake_ao). NORMAL: its
tangent-space normal map (relief, lit from up and front). All three share
the model's UVs; they are resized to ALBEDO's size. Alpha is kept.
"""
import argparse

import numpy as np
from PIL import Image, ImageEnhance


def melee(albedo, ao=None, normal=None, strength=1.0):
    rgba = albedo.convert("RGBA")
    size = rgba.size
    col = np.asarray(rgba, dtype=np.float32)[..., :3] / 255.0
    alpha = rgba.getchannel("A")
    shade = np.ones(col.shape[:2] + (1,), np.float32)
    if ao is not None:
        a = np.asarray(ao.convert("L").resize(size, Image.BILINEAR), np.float32)[..., None] / 255.0
        shade *= 1.0 - strength * 0.65 * (1.0 - a)
    if normal is not None:
        n = np.asarray(normal.convert("RGB").resize(size, Image.BILINEAR), np.float32) / 255.0 * 2.0 - 1.0
        light = np.array([0.0, 0.45, 0.89], np.float32)
        relief = np.clip((n * light).sum(-1, keepdims=True) / light[2], 0.6, 1.25)
        shade *= 1.0 - strength * 0.2 * (1.0 - relief)
    img = Image.fromarray((np.clip(col * shade, 0, 1) * 255).astype(np.uint8))
    img = ImageEnhance.Color(img).enhance(1.0 + 0.18 * strength)
    img = ImageEnhance.Contrast(img).enhance(1.0 + 0.06 * strength)
    img = ImageEnhance.Brightness(img).enhance(1.0 + 0.08 * strength)
    out = img.convert("RGBA")
    out.putalpha(alpha)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("albedo")
    ap.add_argument("out")
    ap.add_argument("--ao")
    ap.add_argument("--normal")
    ap.add_argument("--strength", type=float, default=1.0)
    a = ap.parse_args()
    img = melee(Image.open(a.albedo), Image.open(a.ao) if a.ao else None,
                Image.open(a.normal) if a.normal else None, a.strength)
    if Image.open(a.albedo).mode in ("RGB", "L", "P") and img.getchannel("A").getextrema() == (255, 255):
        img = img.convert("RGB")
    img.save(a.out)
    print("melee texture ->", a.out)


if __name__ == "__main__":
    main()
