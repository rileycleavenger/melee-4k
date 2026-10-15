# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: give an unrigged model a skeleton, for blender_retarget.py.

The model's joints are given by hand (read them off the model: its parts'
centres and ends, e.g. the ball joints of an animatronic or a doll), as
model-space points in the importer's space (Blender: Z up, -Y front, +X
the model's left). Each bone runs from its joint to its first child's (a
leaf to TAILS[name], else a short way up). Each vertex is weighted wholly
to the nearest bone: per loose part (RIGID, for models built of separate
rigid pieces: every piece moves as one) or per vertex.

    SRC = "model.glb"; OUT = "rigged.glb"
    JOINTS = [                  # name, parent, (x, y, z); parents first
        ("Hips", None, (0, 0, 0.33)),
        ("Spine", "Hips", (0, 0, 0.40)), ...
    ]
    TAILS = {"Head": (0, 0, 0.77)}
    RIGID = True
    SMOOTH = False              # True: Blender's automatic (heat) weights, a
                                # smooth skin for one-piece organic models;
                                # RIGID / nearest-bone only where it finds none
    PIN = {r"^Hat": "Head"}     # optional: parts whose material matches the
                                # regex go to that bone whatever is nearest
    exec(open(".../blender_autorig.py").read())

The result (meshes skinned to "Armature", textures kept) is written as
glTF to OUT; blender_retarget.py maps its bones like any rigged model's.
"""
import re

import bmesh
import bpy
from mathutils import Vector
from mathutils.geometry import intersect_point_line

SRC = globals()["SRC"]
OUT = globals()["OUT"]
JOINTS = globals()["JOINTS"]
TAILS = globals().get("TAILS", {})
RIGID = globals().get("RIGID", True)
SMOOTH = globals().get("SMOOTH", False)
PIN = globals().get("PIN", {})


def _seg_dist(p, a, b):
    t = intersect_point_line(p, a, b)[1]
    t = min(1.0, max(0.0, t))
    return (p - (a + (b - a) * t)).length


def autorig():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=SRC)
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    heads = {n: Vector(p) for n, _, p in JOINTS}
    children = {}
    for n, parent, _ in JOINTS:
        if parent:
            children.setdefault(parent, []).append(n)
    tails = {}
    for n, _, _ in JOINTS:
        if n in TAILS:
            tails[n] = Vector(TAILS[n])
        elif n in children:
            tails[n] = heads[children[n][0]]
        else:
            tails[n] = heads[n] + Vector((0, 0, 0.02))
        if (tails[n] - heads[n]).length < 1e-4:
            tails[n] = heads[n] + Vector((0, 0, 0.02))

    arm_data = bpy.data.armatures.new("Armature")
    arm = bpy.data.objects.new("Armature", arm_data)
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    for n, parent, _ in JOINTS:
        b = arm_data.edit_bones.new(n)
        b.head, b.tail = heads[n], tails[n]
        if parent:
            b.parent = arm_data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")

    names = [n for n, _, _ in JOINTS]
    pins = [(re.compile(p), b) for p, b in PIN.items()]

    def nearest(p):
        return min(names, key=lambda n: _seg_dist(p, heads[n], tails[n]))

    for ob in meshes:
        mw = ob.matrix_world.copy()
        ob.parent = None
        ob.data.transform(mw)
        ob.matrix_basis.identity()
        ob.parent = arm
        groups = {n: ob.vertex_groups.new(name=n) for n in names}
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        bm.verts.ensure_lookup_table()
        mat_of = {}
        for f in bm.faces:
            for v in f.verts:
                mat_of[v.index] = f.material_index
        assign = {}
        if RIGID:
            seen = set()
            for v in bm.verts:
                if v.index in seen:
                    continue
                comp, stack = [], [v]
                seen.add(v.index)
                while stack:
                    x = stack.pop()
                    comp.append(x.index)
                    for e in x.link_edges:
                        y = e.other_vert(x)
                        if y.index not in seen:
                            seen.add(y.index)
                            stack.append(y)
                c = sum((bm.verts[i].co for i in comp), Vector()) / len(comp)
                bone = nearest(c)
                mats = ob.data.materials
                mi = mat_of.get(comp[0], 0)
                mname = mats[mi].name if mats and mi < len(mats) and mats[mi] else ""
                for rx, b in pins:
                    if rx.search(mname):
                        bone = b
                for i in comp:
                    assign[i] = bone
        else:
            for v in bm.verts:
                assign[v.index] = nearest(v.co)
        bm.free()
        if SMOOTH:
            # Heat weights; a vertex they leave bare keeps its nearest bone.
            for g in list(ob.vertex_groups):
                ob.vertex_groups.remove(g)
            bpy.ops.object.select_all(action="DESELECT")
            ob.select_set(True)
            arm.select_set(True)
            bpy.context.view_layer.objects.active = arm
            bpy.ops.object.parent_set(type="ARMATURE_AUTO")
            ob.modifiers.clear()
            groups = {n: ob.vertex_groups.get(n) or ob.vertex_groups.new(name=n) for n in names}
            bare = [v.index for v in ob.data.vertices if not any(g.weight > 1e-4 for g in v.groups)]
            assign = {i: assign[i] for i in bare}
            print(f"{ob.name}: heat weights, {len(bare)} vertices on their nearest bone")
        by_bone = {}
        for i, b in assign.items():
            by_bone.setdefault(b, []).append(i)
        for b, idx in by_bone.items():
            groups[b].add(idx, 1.0, "REPLACE")
        m = ob.modifiers.new("Armature", "ARMATURE")
        m.object = arm
        print(f"{ob.name}: " + ", ".join(f"{b} {len(i)}" for b, i in sorted(by_bone.items())))
    bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB")
    print(f"rigged {len(meshes)} meshes on {len(names)} bones -> {OUT}")


autorig()
