# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: export the new fighter's meshes for build_dat.py (mesh.json).

Exports every mesh object parented to the "Skeleton" armature that is not a
reference mesh (name starting "Ref_"), in the rest pose, in game space (the
Skeleton object's local space). Per mesh: triangles with position, normal,
UV (GX convention: t = 1 - v; each triangle moved into the 0-1 tile) and weights from vertex groups named after
bones ("J07"). Per material: the image texture (custom property
"hsd_texture", else the first Image Texture node's file) and whether it is
double sided (property "hsd_double_sided").

    OUT = "/path/mesh.json"; exec(open(".../blender_export_fighter.py").read())

Checks: every vertex weighted (at most 4 bones are kept by build_dat.py),
every mesh has one material with a texture and a UV map.
"""
import json
import math
import re

import bpy

OUT = globals().get("OUT", "/tmp/mesh.json")
skel = bpy.data.objects["Skeleton"]


def texture_of(mat):
    if mat.get("hsd_texture"):
        return bpy.path.abspath(mat["hsd_texture"])
    if mat.use_nodes:
        for n in mat.node_tree.nodes:
            if n.type == "TEX_IMAGE" and n.image is not None:
                return bpy.path.abspath(n.image.filepath)
    raise SystemExit(f"material {mat.name} has no image texture")


def export():
    materials, mat_index, meshes, problems = [], {}, [], []
    for ob in sorted(bpy.data.objects, key=lambda o: o.name):
        if ob.type != "MESH" or ob.parent != skel or ob.name.startswith("Ref_"):
            continue
        me = ob.data
        if not me.materials or me.materials[0] is None:
            problems.append(f"{ob.name}: no material")
            continue
        if me.uv_layers.active is None:
            problems.append(f"{ob.name}: no UV map")
            continue
        mat = me.materials[0]
        if mat.name not in mat_index:
            mat_index[mat.name] = len(materials)
            materials.append({"name": mat.name, "texture": texture_of(mat),
                              "double_sided": bool(mat.get("hsd_double_sided", False))})
        groups = {g.index: g.name for g in ob.vertex_groups}
        me.calc_loop_triangles()
        uv = me.uv_layers.active.data
        normals = me.corner_normals
        tris = []
        unweighted = 0
        for t in me.loop_triangles:
            tri = []
            for li, vi in zip(t.loops, t.vertices):
                v = me.vertices[vi]
                w = [[int(re.sub(r"\D", "", groups[g.group])), g.weight] for g in v.groups
                     if g.group in groups and re.fullmatch(r"J\d+", groups[g.group]) and g.weight > 0]
                if not w:
                    unweighted += 1
                n = normals[li].vector
                u = uv[li].uv
                tri.append({"p": list(v.co), "n": [n.x, n.y, n.z], "uv": [u.x, 1.0 - u.y], "w": w})
            # Into the 0-1 tile: models often keep UVs in another (v 1..2),
            # which Blender repeats and the game's clamped textures do not.
            for k in (0, 1):
                shift = math.floor(min(c["uv"][k] for c in tri) + 1e-4)
                for c in tri:
                    c["uv"][k] -= shift
            tris.append(tri)
        if unweighted:
            problems.append(f"{ob.name}: {unweighted} corners with no bone weight")
        meshes.append({"name": ob.name, "material": mat_index[mat.name], "tris": tris})
    if problems:
        raise SystemExit("; ".join(problems))
    json.dump({"materials": materials, "meshes": meshes}, open(OUT, "w"))
    n = sum(len(m["tris"]) for m in meshes)
    print(f"exported {len(meshes)} meshes, {n} triangles, {len(materials)} materials -> {OUT}")


export()
