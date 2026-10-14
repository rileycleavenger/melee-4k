# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: import a base fighter exported by export_base.py.

Builds an armature "Skeleton" whose bones are named J00..Jnn after the
model's joints (depth-first order, the order every fighter table uses), in
the bind pose, and one mesh per DObj ("Ref_D07" ...) with vertex groups
named after the bones, skinned by an Armature modifier. The reference
meshes are for scale and shape only; the new character is modelled next to
them and weighted to the same bones.

Run inside Blender (blender_mcp.py code ...), with DIR set first:

    DIR = "/Users/you/Melee4K-dev/fighters/base/Mr"; exec(open(".../blender_import_base.py").read())
"""
import json
import os

import bpy
from mathutils import Matrix, Vector

DIR = globals().get("DIR") or os.environ.get("FIGHTER_BASE_DIR")
SKIP_REF = globals().get("SKIP_REF", False)


def mat4(m):
    return Matrix([m[0], m[1], m[2], [0.0, 0.0, 0.0, 1.0]])


def build(dir_):
    skel = json.load(open(os.path.join(dir_, "skeleton.json")))
    joints = skel["joints"]
    # HSD is Y-up; Blender is Z-up. Rotate the whole fighter +90 deg about X
    # on the armature object, so bone data stays in game space (exports are
    # exact inverses).
    arm = bpy.data.armatures.new("Skeleton")
    obj = bpy.data.objects.new("Skeleton", arm)
    bpy.context.scene.collection.objects.link(obj)
    obj.rotation_euler = (1.5707963267948966, 0.0, 0.0)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bones = []
    for j in joints:
        b = arm.edit_bones.new(f"J{j['index']:02d}")
        w = mat4(j["world"])
        head = w.to_translation()
        b.head = head
        b.tail = head + (w.to_3x3() @ Vector((0.0, 0.0, 0.0)) + Vector((0.0, 0.6, 0.0)))
        b["hsd_world"] = [v for row in j["world"] for v in row]
        b["hsd_has_ibm"] = j["ibm"] is not None
        bones.append(b)
    for j, b in zip(joints, bones):
        if j["parent"] >= 0:
            b.parent = bones[j["parent"]]
    bpy.ops.object.mode_set(mode="OBJECT")
    obj["hsd_base"] = skel["base"]
    obj["hsd_joint_symbol"] = skel["joint_symbol"]
    if SKIP_REF:
        return obj
    meshes = json.load(open(os.path.join(dir_, "meshes.json")))
    by_dobj = {}
    for m in meshes:
        by_dobj.setdefault(m["dobj"], []).extend(m["tris"])
    for d, tris in sorted(by_dobj.items()):
        verts, faces, groups = [], [], []
        for t in tris:
            base = len(verts)
            for v in t:
                verts.append(v["pos"])
                groups.append(v["weights"])
            faces.append((base, base + 1, base + 2))
        me = bpy.data.meshes.new(f"Ref_D{d:02d}")
        me.from_pydata(verts, [], faces)
        me.update()
        mo = bpy.data.objects.new(f"Ref_D{d:02d}", me)
        bpy.context.scene.collection.objects.link(mo)
        mo.parent = obj
        vgs = {}
        for vi, ws in enumerate(groups):
            for ji, w in ws:
                name = f"J{ji:02d}"
                if name not in vgs:
                    vgs[name] = mo.vertex_groups.new(name=name)
                vgs[name].add([vi], w, "REPLACE")
        mod = mo.modifiers.new("Armature", "ARMATURE")
        mod.object = obj
        mo["hsd_reference"] = True
    return obj


build(DIR)
