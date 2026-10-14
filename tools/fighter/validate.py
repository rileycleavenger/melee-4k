#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a custom fighter pack before it goes in the game.

    .venv/bin/python tools/fighter/validate.py PACK_DIR [--base-dir BASE] [--disc DISC]

PACK_DIR is fighters/<id>/ (fighter.cfg, Pl<id><costume>.dat, Pl<id>Ui.dat,
textures/). BASE is export_base.py's folder for the pack's base fighter: with
it the skeleton is compared joint by joint. With --disc the id is checked
against the disc's own Pl*.dat names.

Errors (exit 1) are what the game would refuse or draw wrong; warnings are
budget and style (Melee's own fighters are 1.5-4k triangles, 128-256 px
textures; HD replacements up to 2048).
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hsd  # noqa: E402
import model  # noqa: E402

REPO = os.path.dirname(os.path.dirname(HERE))
ATTRS = ("weight", "walk", "dash", "jump", "gravity", "fall", "air")
KEYS = {"id", "name", "base", "symbol", "costumes", "teams", "moves", "voice"} | set(ATTRS)
# Melee's own fighters draw 800-6000 triangles; an HD custom fighter can
# afford more (smooth shapes), up to about 20000 with eight on screen.
TRI_WARN = (800, 20000)
MAX_ENVELOPES = 10
GX_TF_RGBA8 = 6
UI_SIZES = {"ftUiIcon": (64, 56), "ftUiCsp": (136, 188), "ftUiStock": (24, 24),
            "ftUiBanner": (512, 56), "ftUiName": (160, 128)}


class Report:
    def __init__(self):
        self.errors, self.warnings = [], []

    def error(self, msg):
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)


def read_cfg(path, r):
    """fighter.cfg as the game reads it (src/pc/fighter_pack.c)."""
    cfg = {"costumes": []}
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        key, _, rest = line.partition(" ")
        rest = rest.strip()
        if key not in KEYS:
            r.warn(f"fighter.cfg:{n}: unknown key {key!r} (ignored by the game)")
            continue
        if key in ("costumes", "teams"):
            cfg[key] = rest.split()
        else:
            cfg[key] = rest.strip('"')
    for k in ("id", "base", "symbol"):
        if k not in cfg:
            r.error(f"fighter.cfg: no {k}")
    for k in ("id", "base"):
        if k in cfg and not re.fullmatch(r"[A-Z][a-z]", cfg[k]):
            r.error(f"fighter.cfg: {k} {cfg[k]!r} is not two letters like 'Mr'")
    if not cfg["costumes"]:
        r.error("fighter.cfg: no costumes")
    if len(cfg["costumes"]) > 8:
        r.error("fighter.cfg: more than 8 costumes")
    for c in cfg["costumes"]:
        if not re.fullmatch(r"[A-Z][a-z]", c):
            r.error(f"fighter.cfg: costume {c!r} is not two letters like 'Nr'")
    for t in cfg.get("teams", []):
        if t not in cfg["costumes"]:
            r.warn(f"fighter.cfg: team costume {t!r} is not one of the costumes")
    if len(cfg.get("name", "")) > 31:
        r.error("fighter.cfg: name longer than 31 characters")
    for k in ATTRS:
        if k in cfg:
            try:
                v = float(cfg[k])
            except ValueError:
                r.error(f"fighter.cfg: {k} {cfg[k]!r} is not a number")
                continue
            if not 0.1 < v < 10:
                r.error(f"fighter.cfg: {k} {v} outside 0.1..10 (the game uses 1)")
            elif not 0.5 <= v <= 2:
                r.warn(f"fighter.cfg: {k} {v} is a big change from the base")
    if "moves" in cfg:
        known = re.findall(r'^\s*"([^"]+)",\s*$',
                           "\n".join(open(p).read() for p in moves_sources()), re.M)
        if known and cfg["moves"] not in known:
            r.error(f"fighter.cfg: moves {cfg['moves']!r} is not compiled in (have: {known})")
    return cfg


def moves_sources():
    """The C files that define a FtCustomMoves (src/melee/ft/ftcustom.h)."""
    import glob
    return [p for p in glob.glob(os.path.join(REPO, "src", "melee", "ft", "kinds", "*", "*.c"))
            if "const FtCustomMoves " in open(p, errors="replace").read()]


def costume_symbol(cfg, costume):
    """The joint symbol stem the game looks up (src/melee/ft/ftcustom.c):
    the cfg's symbol, then the costume suffix unless it is Nr, as on the
    disc (PlyMario5K_Share_joint, PlyMario5KRe_Share_joint)."""
    return cfg.get("symbol", "") + ("" if costume == "Nr" else costume)


def check_costume(path, cfg, costume, base_skel, r):
    name = os.path.basename(path)
    try:
        a = hsd.Archive(open(path, "rb").read())
    except Exception as e:  # noqa: BLE001
        r.error(f"{name}: not an HSD archive ({e})")
        return
    stem = costume_symbol(cfg, costume)
    sym = f"{stem}_Share_joint"
    if sym not in a.symbols:
        r.error(f"{name}: no symbol {sym} (build_dat.py --symbol {stem})")
        return
    if f"{stem}_Share_matanim_joint" not in a.symbols:
        r.warn(f"{name}: no {stem}_Share_matanim_joint (the eyes will not blink)")
    skel = model.read_skeleton(a, sym)
    if base_skel is not None:
        if len(skel) != len(base_skel):
            r.error(f"{name}: {len(skel)} joints, the base has {len(base_skel)}")
        else:
            for j, b in zip(skel, base_skel):
                if j["parent"] != b["parent"]:
                    r.error(f"{name}: J{j['index']:02d} parent differs from the base's")
                    break
                if b["ibm"] is not None and j["ibm"] is not None and max(
                        abs(x - y) for rx, ry in zip(j["ibm"], b["ibm"]) for x, y in zip(rx, ry)) > 1e-4:
                    r.error(f"{name}: J{j['index']:02d} bind pose differs from the base's "
                            "(animations and hitboxes would not fit)")
                    break
    joints = hsd.read_joint_tree(a, a.symbols[sym])
    tris, textures = 0, set()
    for j in joints:
        for d in j["dobjs"]:
            if d["mobj"]:
                for t in d["mobj"]["tex"]:
                    tt = hsd.read_tobj(a, t)
                    textures.add((tt.get("w"), tt.get("h")))
            for p in d["pobjs"]:
                if len(p.get("envelopes", [])) > MAX_ENVELOPES:
                    r.error(f"{name}: a PObj with {len(p['envelopes'])} envelopes (max {MAX_ENVELOPES})")
    try:
        meshes = model.read_meshes(a, skel, sym)
    except Exception as e:  # noqa: BLE001
        r.error(f"{name}: meshes do not decode ({e})")
        return
    seen = set()
    for m in meshes:
        key = (m["dobj"], m["pobj"]) if "dobj" in m and "pobj" in m else id(m)
        if key in seen:
            continue
        seen.add(key)
        for tri in m["tris"]:
            for v in tri:
                w = sum(x for _, x in v["weights"]) if v["weights"] else 1.0
                if abs(w - 1.0) > 0.01:
                    r.error(f"{name}: a vertex's weights sum to {w:.3f}")
                    break
        tris += len(m["tris"])
    # The base's low-detail slots repeat the meshes: count what one table shows.
    shown = tris // 2 if tris else 0
    if not TRI_WARN[0] <= shown <= TRI_WARN[1]:
        r.warn(f"{name}: ~{shown} triangles drawn, outside {TRI_WARN[0]}-{TRI_WARN[1]}")
    for w, h in textures:
        if w and h and (w > 512 or h > 512):
            r.warn(f"{name}: a {w}x{h} texture in the file; keep it low-res and put HD in textures/")
    return shown


def check_ui(path, cfg, r):
    name = os.path.basename(path)
    if not os.path.exists(path):
        r.error(f"{name}: missing (tools/fighter/build_ui.py); the menus would show the base")
        return
    a = hsd.Archive(open(path, "rb").read())
    want = ["ftUiIcon"] + [f"ftUiCsp{i}" for i in range(len(cfg["costumes"]))] + \
        [f"ftUiStock{i}" for i in range(len(cfg["costumes"]))]
    for s in want:
        if s not in a.symbols:
            r.error(f"{name}: no {s}")
    for s in ("ftUiBanner", "ftUiName"):
        if s not in a.symbols:
            r.warn(f"{name}: no {s} (build_ui.py --name): the results screen shows the base's name")
    for s, off in a.symbols.items():
        size = UI_SIZES.get(re.sub(r"\d+$", "", s))
        if size is None:
            continue
        w, h, fmt = struct.unpack(">HHI", a.bytes(off + 4, 8))
        if (w, h) != size or fmt != GX_TF_RGBA8:
            r.error(f"{name}: {s} is {w}x{h} format {fmt}, the game expects {size[0]}x{size[1]} RGBA8")


def check_textures(d, r):
    if not os.path.isdir(d):
        r.warn("no textures/ (no HD textures: the low-res ones in the .dat are drawn)")
        return
    try:
        from PIL import Image
    except ImportError:
        Image = None
    for f in sorted(os.listdir(d)):
        if not f.endswith(".png"):
            continue
        m = re.fullmatch(r"tex1_(\d+)x(\d+)_[0-9a-f]{16}_\d+\.png", f)
        if m is None:
            r.error(f"textures/{f}: not named tex1_WxH_hash_fmt.png (build_dat.py --textures names them)")
            continue
        if Image is not None:
            w, h = Image.open(os.path.join(d, f)).size
            lw, lh = int(m.group(1)), int(m.group(2))
            if w * lh != h * lw:
                r.error(f"textures/{f}: {w}x{h} is not the low-res {lw}x{lh}'s shape")
            if max(w, h) > 2048:
                r.warn(f"textures/{f}: {w}x{h}, larger than 2048")


def disc_names(disc):
    sys.path.insert(0, os.path.join(HERE, "..", "textures"))
    from extract import Disc  # noqa: E402
    return [os.path.basename(path) for path, _, _ in Disc(disc).files()]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("pack")
    ap.add_argument("--base-dir", help="export_base.py's folder for the base fighter")
    ap.add_argument("--disc", help="check the id against the disc's files")
    a = ap.parse_args()
    r = Report()
    cfg_path = os.path.join(a.pack, "fighter.cfg")
    if not os.path.exists(cfg_path):
        print(f"error: {cfg_path} missing")
        return 1
    cfg = read_cfg(cfg_path, r)
    pid = cfg.get("id", "??")
    if os.path.basename(os.path.normpath(a.pack)) != pid:
        r.warn(f"the folder is not named after the id ({pid})")
    base_skel = None
    if a.base_dir:
        bname = f"Pl{cfg.get('base')}Nr.dat"
        bpath = os.path.join(a.base_dir, bname)
        if os.path.exists(bpath):
            ba = hsd.Archive(open(bpath, "rb").read())
            bsym = next(s for s in ba.symbols if s.endswith("_Share_joint") and "matanim" not in s)
            base_skel = model.read_skeleton(ba, bsym)
        else:
            r.error(f"--base-dir has no {bname}")
    if a.disc:
        try:
            names = disc_names(a.disc)
            if any(n.lower().startswith(f"pl{pid.lower()}") for n in names):
                r.error(f"id {pid}: the disc already has Pl{pid}*.dat files; pick another id")
        except Exception as e:  # noqa: BLE001
            r.warn(f"could not read the disc's file names ({e})")
    counts = {}
    for c in cfg["costumes"]:
        p = os.path.join(a.pack, f"Pl{pid}{c}.dat")
        if not os.path.exists(p):
            r.error(f"Pl{pid}{c}.dat missing (costume {c})")
            continue
        counts[c] = check_costume(p, cfg, c, base_skel, r)
    check_ui(os.path.join(a.pack, f"Pl{pid}Ui.dat"), cfg, r)
    check_textures(os.path.join(a.pack, "textures"), r)

    print(f"{pid} \"{cfg.get('name', pid)}\" on {cfg.get('base')}: {len(cfg['costumes'])} costume(s) "
          + ", ".join(f"{c} ~{n} tris" for c, n in counts.items() if n is not None)
          + (f", moves {cfg['moves']}" if "moves" in cfg else ""))
    for w in r.warnings:
        print(f"warning: {w}")
    for e in r.errors:
        print(f"error: {e}")
    print("OK" if not r.errors else f"{len(r.errors)} error(s)")
    return 1 if r.errors else 0


if __name__ == "__main__":
    sys.exit(main())
