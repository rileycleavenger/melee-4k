# SPDX-License-Identifier: GPL-3.0-or-later
"""Fighter models in HSD archives, as plain data: the skeleton in its bind
pose and every mesh as triangles with bind-space positions, normals, UVs and
bone weights. Both ways: `read_*` decode a costume (the base fighter's, or
one we wrote, to check it), and build_dat.py writes one back.

How the game skins (sysdolphin/baselib/pobj.c, SetupEnvelopeModelMtx): each
vertex picks one of the PObj's envelopes (PNMTXIDX / 3). An envelope that is
a single joint at weight 1.0 is drawn with that joint's world matrix, so its
vertices are stored in the joint's *local* space; any other envelope blends
world x inverse-bind for each joint, so its vertices are in bind (model)
space. Every position here is converted to bind space.
"""
import struct

import hsd

GX_VA_PNMTXIDX, GX_VA_TEX0MTXIDX, GX_VA_POS, GX_VA_NRM, GX_VA_TEX0, GX_VA_TEX1 = 0, 1, 9, 10, 13, 14
GX_DIRECT, GX_INDEX8, GX_INDEX16 = 1, 2, 3
COMP = {0: ("B", 1), 1: ("b", 1), 2: ("H", 2), 3: ("h", 2), 4: ("f", 4)}
NCOMP = {GX_VA_POS: {0: 2, 1: 3}, GX_VA_NRM: {0: 3}, GX_VA_TEX0: {0: 1, 1: 2},
         GX_VA_TEX1: {0: 1, 1: 2}}


# ------------------------------------------------------------- matrices ----

def mat_mul(a, b):
    """3x4 affine matrices as [[r0],[r1],[r2]] rows of 4."""
    out = []
    for r in range(3):
        row = []
        for c in range(4):
            v = sum(a[r][k] * b[k][c] for k in range(3))
            if c == 3:
                v += a[r][3]
            row.append(v)
        out.append(row)
    return out


def mat_inv(m):
    a, b, c = m[0][:3], m[1][:3], m[2][:3]
    det = (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) +
           a[2] * (b[0] * c[1] - b[1] * c[0]))
    inv = [[(b[1] * c[2] - b[2] * c[1]) / det, (a[2] * c[1] - a[1] * c[2]) / det,
            (a[1] * b[2] - a[2] * b[1]) / det],
           [(b[2] * c[0] - b[0] * c[2]) / det, (a[0] * c[2] - a[2] * c[0]) / det,
            (a[2] * b[0] - a[0] * b[2]) / det],
           [(b[0] * c[1] - b[1] * c[0]) / det, (a[1] * c[0] - a[0] * c[1]) / det,
            (a[0] * b[1] - a[1] * b[0]) / det]]
    t = [m[0][3], m[1][3], m[2][3]]
    return [inv[r] + [-sum(inv[r][k] * t[k] for k in range(3))] for r in range(3)]


def apply(m, v, w=1.0):
    return [m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2] + m[r][3] * w for r in range(3)]


def trs(rot, scale, pos):
    """HSD joint matrix: T * Rz * Ry * Rx * S (jobj.c HSD_MtxSRT)."""
    import math
    cx, sx = math.cos(rot[0]), math.sin(rot[0])
    cy, sy = math.cos(rot[1]), math.sin(rot[1])
    cz, sz = math.cos(rot[2]), math.sin(rot[2])
    r = [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz],
         [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz],
         [-sy, sx * cy, cx * cy]]
    return [[r[i][j] * scale[j] for j in range(3)] + [pos[i]] for i in range(3)]


# -------------------------------------------------------------- skeleton ----

def read_skeleton(a, joint_sym):
    """[{index, parent, flags, at, local, world, ibm}] in depth-first order.
    `world` is the bind pose: the inverse of the joint's inverse-bind matrix
    when it has one, else the parent's world x its local TRS."""
    joints = hsd.read_joint_tree(a, a.symbols[joint_sym])
    by_at = {j["at"]: j for j in joints}
    parent = {}
    for j in joints:
        c = a.ptr(j["at"] + 8)
        while c is not None:
            parent[c] = j["at"]
            c = a.ptr(c + 0xC)
    out = []
    for j in joints:
        p = parent.get(j["at"])
        local = trs(j["rot"], j["scale"], j["pos"])
        ibm = None
        if j["mtx"] is not None:
            vals = struct.unpack(">12f", a.bytes(j["mtx"], 48))
            ibm = [list(vals[0:4]), list(vals[4:8]), list(vals[8:12])]
        pw = out[by_at[p]["index"]]["world"] if p is not None else [[1, 0, 0, 0], [0, 1, 0, 0],
                                                                      [0, 0, 1, 0]]
        world = mat_inv(ibm) if ibm is not None else mat_mul(pw, local)
        out.append({"index": j["index"], "parent": by_at[p]["index"] if p is not None else -1,
                    "flags": j["flags"], "at": j["at"], "local": local, "world": world,
                    "ibm": ibm})
    return out


# ---------------------------------------------------------------- meshes ----

def _read_attr(a, v, idx):
    fmt, size = COMP[v["comp"]]
    n = NCOMP.get(v["attr"], {}).get(v["cnt"], 3)
    off = v["data"] + idx * v["stride"]
    vals = struct.unpack(">" + fmt * n, a.bytes(off, size * n))
    scale = 1.0 if v["comp"] == 4 else float(1 << v["frac"])
    return [x / scale for x in vals]


def decode_display(a, pobj):
    """Triangles from a PObj's display list: [(v0, v1, v2)], each vertex a
    dict of the raw attribute values ({attr: index or value})."""
    verts = pobj["verts"]
    dl = a.bytes(pobj["display"], pobj["n_display"] * 32)
    tris = []
    o = 0
    while o < len(dl):
        op = dl[o] & 0xF8
        if op == 0:
            break
        count = struct.unpack(">H", dl[o + 1:o + 3])[0]
        o += 3
        vs = []
        for _ in range(count):
            vtx = {}
            for v in verts:
                if v["type"] == GX_DIRECT:
                    if v["attr"] in (GX_VA_PNMTXIDX, GX_VA_TEX0MTXIDX):
                        vtx[v["attr"]] = dl[o]
                        o += 1
                    elif v["attr"] in (11, 12):
                        # Vertex colours (GX_VA_CLR0/1) in the display list:
                        # RGB565, RGB8, RGBX8, RGBA4, RGBA6, RGBA8. Skipped.
                        o += (2, 3, 4, 2, 3, 4)[v["comp"]]
                    else:
                        raise ValueError(f"direct attribute {v['attr']} not handled")
                elif v["type"] == GX_INDEX8:
                    vtx[v["attr"]] = dl[o]
                    o += 1
                else:
                    vtx[v["attr"]] = struct.unpack(">H", dl[o:o + 2])[0]
                    o += 2
            vs.append(vtx)
        if op == 0x90:
            tris += [(vs[i], vs[i + 1], vs[i + 2]) for i in range(0, len(vs) - 2, 3)]
        elif op == 0x98:
            for i in range(len(vs) - 2):
                tris.append((vs[i], vs[i + 1], vs[i + 2]) if i % 2 == 0 else
                            (vs[i + 1], vs[i], vs[i + 2]))
        elif op == 0xA0:
            tris += [(vs[0], vs[i], vs[i + 1]) for i in range(1, len(vs) - 1)]
        elif op == 0x80:
            for i in range(0, len(vs) - 3, 4):
                tris += [(vs[i], vs[i + 1], vs[i + 2]), (vs[i], vs[i + 2], vs[i + 3])]
        else:
            raise ValueError(f"display list opcode {op:#x}")
    return tris


def read_meshes(a, skel, joint_sym):
    """[{dobj, pobj, texture (tobj offset or None), tris: [[{pos, nrm, uv,
    weights: [(joint index, w)]}] * 3]}], positions and normals in bind
    space."""
    joints = hsd.read_joint_tree(a, a.symbols[joint_sym])
    by_at = {j["at"]: j["index"] for j in joints}
    out = []
    dobj_index = 0
    for j in joints:
        for d in j["dobjs"]:
            tex = d["mobj"]["tex"][0] if d["mobj"] and d["mobj"]["tex"] else None
            for pi, p in enumerate(d["pobjs"]):
                envs = [[(by_at[jt], w) for jt, w in e] for e in p.get("envelopes", [])]
                attrs = {v["attr"]: v for v in p["verts"]}
                tris = []
                for tri in decode_display(a, p):
                    tv = []
                    for vtx in tri:
                        weights = envs[vtx.get(GX_VA_PNMTXIDX, 0) // 3] if envs else [(j["index"], 1.0)]
                        pos = _read_attr(a, attrs[GX_VA_POS], vtx[GX_VA_POS])
                        nrm = _read_attr(a, attrs[GX_VA_NRM], vtx[GX_VA_NRM]) if GX_VA_NRM in vtx else [0, 1, 0]
                        uv = _read_attr(a, attrs[GX_VA_TEX0], vtx[GX_VA_TEX0]) if GX_VA_TEX0 in vtx else [0, 0]
                        if len(weights) == 1 and weights[0][1] >= 1.0 - 1e-6:
                            w = skel[weights[0][0]]["world"]
                            pos = apply(w, pos)
                            nrm = apply(w, nrm, 0.0)
                        tv.append({"pos": pos, "nrm": nrm, "uv": uv, "weights": weights})
                    tris.append(tv)
                out.append({"dobj": dobj_index, "pobj": pi, "pobj_at": p["at"], "texture": tex, "tris": tris})
            dobj_index += 1
    return out
