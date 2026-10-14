#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write a custom fighter's costume file (PlXxNr.dat) over a base fighter's.

    .venv/bin/python tools/fighter/build_dat.py --base-dir BASE --mesh mesh.json \\
        --symbol PlyGeno5K --out pack/PlGnNr.dat [--textures pack/textures] [--lowres 128]

BASE is export_base.py's folder (the base fighter's PlXxNr.dat and PlXx.dat).
mesh.json is blender_export_fighter.py's output: materials (PNG textures) and
meshes, each triangle's vertices with bind-space position, normal, UV (GX
convention: v grows downward) and bone weights by joint index.

How the file is made. The base costume is copied byte for byte -- its
skeleton (bind pose and inverse-bind matrices, so every animation and hitbox
of the base fighter fits), and its material-animation tree (eye blinks) --
and the new meshes are appended. The root joint's DObj list is replaced by a
new one with exactly as many DObjs as the base had, because the base
fighter's data file (PlXx.dat) shows and hides DObjs by index: the new
meshes go in the slots it shows normally (its visibility table 0) and again
in its low-detail slots (table 1); the eye slots keep the base's eye
material, whose texture animation the fighter code requires; every other
slot gets an empty triangle. The base's own meshes stay in the file,
unreferenced.

Textures: each material's PNG goes in the file at --lowres (RGB565, or
RGB5A3 with alpha) and, at full size, into --textures as a replacement named
by that low-res texture's key (tex1_WxH_hash_fmt.png), which the game's
texture pack loader swaps in (src/pc/textures.cpp).
"""
import argparse
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hsd  # noqa: E402
import model  # noqa: E402

GX_TF_RGB565, GX_TF_RGB5A3 = 4, 5
MAX_ENVELOPES = 10  # per PObj (pobj.c: MtxIdx < 10)
MAX_INFLUENCES = 4


# ----------------------------------------------------------- base fighter ----

def base_tables(base_data):
    """(visible slots, low-detail slots, eye slots) from PlXx.dat."""
    a = hsd.Archive(base_data)
    ft = next(off for s, off in a.symbols.items() if s.startswith("ftData"))
    x8 = a.ptr(ft + 8)
    model_num, vis = a.u32(x8), a.ptr(x8 + 4)
    tables = []
    for t in range(4):
        lk = a.ptr(vis + t * 4)
        slots = []
        if lk is not None:
            for m in range(model_num):
                n, arr = a.u32(lk + m * 8), a.ptr(lk + m * 8 + 4)
                for j in range(n):
                    c, idx = a.u32(arr + j * 8), a.ptr(arr + j * 8 + 4)
                    slots += [a.u8(idx + k) for k in range(c)] if idx is not None else []
        tables.append(slots)
    eyes = []
    cnt, p = a.u32(x8 + 8), a.ptr(x8 + 12)
    if p is not None and a.ptr(p) is not None:
        q = a.ptr(p)
        eyes = [a.u16(q + 2 * k) for k in range(cnt)]
    return tables[0], tables[1], eyes


# -------------------------------------------------------------- textures ----

def load_png(path):
    from PIL import Image
    return Image.open(path).convert("RGBA")


def encode_16bit(img, fmt):
    """GX RGB565 / RGB5A3, 4x4 tiles, big-endian."""
    w, h = img.size
    px = img.load()
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    r, g, b, a = px[min(x, w - 1), min(y, h - 1)]
                    if fmt == GX_TF_RGB565:
                        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                    elif a >= 0xE0:
                        v = 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
                    else:
                        v = ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
                    out += struct.pack(">H", v)
    return bytes(out)


def texture_key(data, w, h, fmt):
    import xxhash
    return f"tex1_{w}x{h}_{xxhash.xxh64(data, seed=0).intdigest():016x}_{fmt}"


# ----------------------------------------------------------------- meshes ----

def envelope_of(weights):
    """Canonical envelope: up to MAX_INFLUENCES joints, weights normalised and
    rounded so near-identical vertices share one."""
    ws = sorted(((int(j), float(w)) for j, w in weights if w > 1e-4), key=lambda x: -x[1])
    ws = ws[:MAX_INFLUENCES]
    total = sum(w for _, w in ws) or 1.0
    ws = [(j, round(w / total, 3)) for j, w in ws]
    if len(ws) == 1:
        ws = [(ws[0][0], 1.0)]
    else:
        drift = 1.0 - sum(w for _, w in ws)
        ws[0] = (ws[0][0], round(ws[0][1] + drift, 4))
    return tuple(sorted(ws))


def split_pobjs(tris):
    """Group triangles so each group uses at most MAX_ENVELOPES envelopes."""
    groups = []
    cur, cur_env = [], []
    for t in tris:
        envs = [envelope_of(v["w"]) for v in t]
        new = [e for e in dict.fromkeys(envs) if e not in cur_env]
        if len(cur_env) + len(new) > MAX_ENVELOPES:
            groups.append((cur, cur_env))
            cur, cur_env = [], []
            new = list(dict.fromkeys(envs))
        cur_env += new
        cur.append((t, envs))
    if cur:
        groups.append((cur, cur_env))
    return groups


class Builder:
    def __init__(self, template, joint_sym):
        self.t = hsd.Archive(template)
        self.joint_sym = joint_sym
        self.skel = model.read_skeleton(self.t, joint_sym)
        self.joints = hsd.read_joint_tree(self.t, self.t.symbols[joint_sym])
        self.w = hsd.Writer()
        self.w.body += self.t.body
        self.w.reloc += sorted(self.t.reloc)
        # The template's own vertex descriptor for PNMTXIDX, copied verbatim.
        d0 = self.joints[0]["dobjs"][0]["pobjs"][0]
        self.vd_pnmtx = self.t.bytes(self.t.ptr(d0["at"] + 8), 0x18)
        self.template_mobj = self.joints[0]["dobjs"][0]["mobj"]
        self.empty_pobj = None

    def joint_at(self, index):
        return self.joints[index]["at"]

    def vtx_descs(self, pos, nrm, uv):
        w = self.w
        out = w.alloc(0x18 * 5)
        w.body[out:out + 0x18] = self.vd_pnmtx
        for i, (attr, cnt, stride, data) in enumerate(((9, 1, 12, pos), (10, 0, 12, nrm),
                                                       (13, 1, 8, uv))):
            o = out + 0x18 * (i + 1)
            w.put(o, "IIIIBxH", attr, 3, cnt, 4, 0, stride)  # INDEX16, F32
            w.ptr(o + 20, data)
        w.put(out + 0x18 * 4, "IIIIBxH", 0xFF, 2, 0, 4, 0, 0)
        return out

    def envelopes(self, envs):
        """NULL-terminated array of pointers to NULL-terminated
        HSD_EnvelopeDesc lists."""
        w = self.w
        lists = []
        for env in envs:
            o = w.alloc(8 * (len(env) + 1))
            for k, (j, weight) in enumerate(env):
                w.ptr(o + 8 * k, self.joint_at(j))
                w.put(o + 8 * k + 4, "f", weight)
            lists.append(o)
        arr = w.alloc(4 * (len(lists) + 1))
        for k, o in enumerate(lists):
            w.ptr(arr + 4 * k, o)
        return arr

    def pobj(self, group, envs, double_sided):
        """One PObjDesc from (triangle, envelopes) pairs, F32 indexed arrays."""
        w = self.w
        pos, nrm, uv, keys = [], [], [], {}
        dl = bytearray()
        verts = []
        for t, tenvs in group:
            # mesh.json triangles wind counter-clockwise about their normal
            # (Blender); GX's front faces are clockwise, as every disc model's.
            for v, env in zip((t[0], t[2], t[1]), (tenvs[0], tenvs[2], tenvs[1])):
                p, n = v["p"], v["n"]
                if len(env) == 1:  # single joint: stored in its local space
                    ibm = self.skel[env[0][0]]["ibm"]
                    if ibm is None:
                        raise SystemExit(f"weight on joint {env[0][0]}, which has no bind matrix")
                    p = model.apply(ibm, p)
                    n = model.apply(ibm, n, 0.0)
                ln = math.sqrt(sum(c * c for c in n)) or 1.0
                n = [c / ln for c in n]
                key = (tuple(round(c, 5) for c in p), tuple(round(c, 4) for c in n),
                       tuple(round(c, 5) for c in v["uv"]))
                pi = keys.setdefault(("p", key[0]), len(pos))
                if pi == len(pos):
                    pos.append(p)
                ni = keys.setdefault(("n", key[1]), len(nrm))
                if ni == len(nrm):
                    nrm.append(n)
                ti = keys.setdefault(("t", key[2]), len(uv))
                if ti == len(uv):
                    uv.append(v["uv"])
                verts.append((envs.index(env) * 3, pi, ni, ti))
        for start in range(0, len(verts), 0xFFFF // 3 * 3):
            chunk = verts[start:start + 0xFFFF // 3 * 3]
            dl += struct.pack(">BH", 0x90, len(chunk))
            for m, pi, ni, ti in chunk:
                dl += struct.pack(">BHHH", m, pi, ni, ti)
        while len(dl) % 32:
            dl.append(0)
        pos_off = w.blob(b"".join(struct.pack(">3f", *p) for p in pos), 32)
        nrm_off = w.blob(b"".join(struct.pack(">3f", *n) for n in nrm), 32)
        uv_off = w.blob(b"".join(struct.pack(">2f", *t) for t in uv), 32)
        dl_off = w.blob(bytes(dl), 32)
        vd = self.vtx_descs(pos_off, nrm_off, uv_off)
        env_off = self.envelopes(envs)
        p = w.alloc(0x18)
        w.ptr(p + 8, vd)
        w.put(p + 12, "HH", 0x2001 if double_sided else 0xA001, len(dl) // 32)
        w.ptr(p + 16, dl_off)
        w.ptr(p + 20, env_off)
        return p

    def pobj_chain(self, tris, double_sided):
        first = prev = None
        for group, envs in split_pobjs(tris):
            p = self.pobj(group, envs, double_sided)
            if prev is None:
                first = p
            else:
                self.w.ptr(prev + 4, p)
            prev = p
        return first

    def empty(self):
        """A single zero-area triangle on the first skinned joint."""
        if self.empty_pobj is None:
            j = next(s["index"] for s in self.skel if s["ibm"] is not None)
            v = {"p": model.apply(self.skel[j]["world"], [0, 0, 0]), "n": [0, 1, 0], "uv": [0, 0],
                 "w": [[j, 1.0]]}
            self.empty_pobj = self.pobj_chain([[v, v, v]], False)
        return self.empty_pobj

    def material(self, mat, lowres, textures_dir):
        """MObjDesc for a textured material, copying the template's first
        material's render mode, colours and texture settings."""
        w, t = self.w, self.t
        img = load_png(mat["texture"])
        has_alpha = img.getextrema()[3][0] < 255
        fmt = GX_TF_RGB5A3 if has_alpha else GX_TF_RGB565
        side = lowres
        small = img.resize((side, side))
        data = encode_16bit(small, fmt)
        key = texture_key(data, side, side, fmt)
        if textures_dir:
            os.makedirs(textures_dir, exist_ok=True)
            img.save(os.path.join(textures_dir, key + ".png"))
        image = w.blob(data, 32)
        imd = w.alloc(0x18)
        w.ptr(imd, image)
        w.put(imd + 4, "HHIIff", side, side, fmt, 0, 0.0, 0.0)
        tm = self.template_mobj
        tobj = w.blob(t.bytes(tm["tex"][0], 0x5C))
        for slot in (0x0, 0x50, 0x54, 0x58):  # name, tlut, lod, tev
            w.ptr(tobj + slot, t.ptr(tm["tex"][0] + slot))
        w.ptr(tobj + 0x4, None)  # one texture per material
        w.ptr(tobj + 0x4C, imd)
        mobj = w.blob(t.bytes(tm["at"], 0x18))
        w.ptr(mobj + 0, None)
        w.ptr(mobj + 8, tobj)
        w.ptr(mobj + 12, tm["mat"])
        w.ptr(mobj + 16, None)
        rm = t.u32(tm["at"] + 4)
        if has_alpha:
            rm |= 0x40000000  # RENDER_XLU
        w.put(mobj + 4, "I", rm)
        pe = t.ptr(tm["at"] + 20)
        w.ptr(mobj + 20, pe)
        return mobj, key

    def build(self, mesh, vis_slots, low_slots, eye_slots, lowres, textures_dir):
        w = self.w
        old = self.joints[0]["dobjs"]
        n_slots = len(old)
        materials = [self.material(m, lowres, textures_dir) for m in mesh["materials"]]
        parts = []
        for m in mesh["meshes"]:
            mat = mesh["materials"][m["material"]]
            parts.append((materials[m["material"]][0],
                          self.pobj_chain(m["tris"], mat.get("double_sided", False))))
        free_vis = [s for s in vis_slots if s not in eye_slots]
        free_low = [s for s in low_slots if s not in eye_slots]
        if len(parts) > len(free_vis):
            raise SystemExit(f"{len(parts)} meshes, but the base shows only {len(free_vis)} slots")
        slot_part = {}
        for i, part in enumerate(parts):
            slot_part[free_vis[i]] = part
            if i < len(free_low):
                slot_part[free_low[i]] = part
        descs = []
        for s in range(n_slots):
            d = w.alloc(0x10)
            if s in slot_part:
                mobj, pobj = slot_part[s]
            elif s in eye_slots:
                # The base's eye material: the fighter code requires its
                # texture animation (ftanim.c, "can't find fighter texture anim").
                mobj, pobj = old[s]["mobj"]["at"], self.empty()
            else:
                # The base's first material: one texture on TEX0, which the
                # empty triangle carries (some base materials need TEX1 or
                # texture-matrix indices it does not have).
                mobj, pobj = self.template_mobj["at"], self.empty()
            w.ptr(d + 8, mobj)
            w.ptr(d + 12, pobj)
            descs.append(d)
        for a_, b_ in zip(descs, descs[1:]):
            w.ptr(a_ + 4, b_)
        w.ptr(self.joints[0]["at"] + 0x10, descs[0])
        return [k for _, k in materials], len(parts)

    def finish(self, symbol_prefix):
        pubs = {}
        for s, off in self.t.symbols.items():
            name = s
            if s == self.joint_sym:
                name = f"{symbol_prefix}_Share_joint"
            elif s.endswith("_Share_matanim_joint"):
                name = f"{symbol_prefix}_Share_matanim_joint"
            pubs[name] = off
        return self.w.finish(pubs, self.t.externs)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--base-dir", required=True)
    ap.add_argument("--mesh", required=True)
    ap.add_argument("--symbol", required=True, help="e.g. PlyGeno5K (for PlyGeno5K_Share_joint)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--textures")
    ap.add_argument("--lowres", type=int, default=128)
    a = ap.parse_args()
    skel = json.load(open(os.path.join(a.base_dir, "skeleton.json")))
    code = skel["base"]
    template = open(os.path.join(a.base_dir, f"Pl{code}Nr.dat"), "rb").read()
    vis, low, eyes = base_tables(open(os.path.join(a.base_dir, f"Pl{code}.dat"), "rb").read())
    b = Builder(template, skel["joint_symbol"])
    keys, n = b.build(json.load(open(a.mesh)), vis, low, eyes, a.lowres, a.textures)
    data = b.finish(a.symbol)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, "wb").write(data)
    print(f"{a.out}: {n} meshes, {len(data)} bytes; textures {keys}")


if __name__ == "__main__":
    main()
