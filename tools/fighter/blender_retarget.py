# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: put a rigged model (a glTF with its own skeleton) on the base
fighter's skeleton.

Each of the model's bones is mapped to one of the base's joints. The model
is moved, bone by bone, so its joints land on the base's (a bone's segment,
its head to a chosen child's head, is turned and stretched onto the base's
segment), and its skin weights are summed onto the base's bones. The result
is one mesh per material of the model, parented to "Skeleton" in game space,
ready for blender_export_fighter.py. Run blender_import_base.py first.

    SRC = "model.glb"           # rigged model (convert .dae/.fbx with assimp:
                                # `assimp export in.dae out.glb -f glb2`)
    SRC_MESH_ROT_X = 0          # degrees: turn the meshes (not the bones) about X
                                # if the converter left them out of line with
                                # the bones (assimp's .dae: 90)
    SCALE = 11.0                # model units -> game units, before fitting
    DROP = [r"Hand[12]$", ...]  # bones (regex) whose faces are removed (spare
                                # parts the model keeps out of sight)
    MAP = {                     # model bone -> base joint
        "Hips": "J04",
        "LeftArm": ("J08", "LeftForeArm"),   # segment to that child bone
        ...
    }
    WEIGHT_AS = {"Head": "J23"} # weight a bone's skin to another joint than
                                # it is placed at
    FOLLOW = {"Spine1": "Hips"} # move with another bone (one whose parent is
                                # not a mapped bone, e.g. a spine under the root)
    KEEP_Y = ["LeftFoot"]       # place at the joint's x/z but the model's own
                                # height (feet that must stay on the ground)
    OFFSET = {"Head": (0, 0.3, 0)}  # game units added to a bone's target
    WEIGHT_STEP = 0.1; MAX_BONES = 3   # quantise weights (few envelopes)
    NAME = "Fx"                 # object/material name prefix
    exec(open("./blender_retarget.py").read()); OBS = retarget()

Then `save_textures(OBS, dir)` writes every material's image as a PNG
(embedded ones too) and sets it as the material's texture for the
exporter; `bake_ao(OBS, dir)` bakes ambient occlusion into those images'
UV space for a model without an AO map (melee_texture.py);
`melee_mesh(OBS, 0.6)` decimates the meshes for the Melee-ified costume.

Bones not in MAP follow their nearest mapped ancestor (rigidly) and weight
to its joint. Game space is Y up, +Z front; the model is taken as glTF
(Blender's importer: Z up, -Y front). The model's left (+X) must be the
base's left (+X).
"""
import re

import bpy
import bmesh
from mathutils import Matrix, Vector

SRC = globals()["SRC"]
SRC_MESH_ROT_X = globals().get("SRC_MESH_ROT_X", 0.0)
SCALE = globals().get("SCALE", 1.0)
DROP = globals().get("DROP", [])
MAP = globals()["MAP"]
WEIGHT_AS = globals().get("WEIGHT_AS", {})
FOLLOW = globals().get("FOLLOW", {})
KEEP_Y = globals().get("KEEP_Y", [])
OFFSET = globals().get("OFFSET", {})
WEIGHT_STEP = globals().get("WEIGHT_STEP", 0.1)
MAX_BONES = globals().get("MAX_BONES", 3)
NAME = globals().get("NAME", "Fx")

# Blender (Z up, -Y front) -> game (Y up, +Z front).
TO_GAME = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))


def _segment(a0, a1, b0, b1):
    """Matrix taking point a0 to b0 and the direction a0->a1 onto b0->b1,
    stretched along it to b's length; across it unchanged."""
    da, db = a1 - a0, b1 - b0
    if da.length < 1e-6 or db.length < 1e-6:
        return Matrix.Translation(b0 - a0)
    d = da.normalized()
    stretch = Matrix.Identity(3) + (db.length / da.length - 1.0) * Matrix(
        [[d[i] * d[j] for j in range(3)] for i in range(3)])
    rot = d.rotation_difference(db.normalized()).to_matrix()
    m = (rot @ stretch).to_4x4()
    return Matrix.Translation(b0) @ m @ Matrix.Translation(-a0)


def retarget():
    skel = bpy.data.objects["Skeleton"]
    joint = {b.name: b.head_local.copy() for b in skel.data.bones}
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=SRC)
    new = [o for o in bpy.data.objects if o not in before]
    arm = next(o for o in new if o.type == "ARMATURE")
    meshes = [o for o in new if o.type == "MESH" and any(m.type == "ARMATURE" for m in o.modifiers)]

    def game(p):
        return (TO_GAME @ p) * SCALE

    # Bone names without the importer's prefix ("Armature_Hips" -> "Hips").
    def short(n):
        for k in list(MAP) + list(WEIGHT_AS) + list(FOLLOW):
            if n == k or n.endswith("_" + k):
                return k
        return n

    heads = {short(b.name): game(arm.matrix_world @ b.head_local) for b in arm.data.bones}
    parent = {short(b.name): short(b.parent.name) if b.parent else None for b in arm.data.bones}

    def target(name):
        j = MAP[name] if isinstance(MAP[name], str) else MAP[name][0]
        t = joint[j].copy()
        if name in KEEP_Y:
            t.y = heads[name].y
        return t + Vector(OFFSET.get(name, (0, 0, 0)))

    mats, wbone = {}, {}
    for name in heads:
        n = FOLLOW.get(name, name)
        while n is not None and n not in MAP:
            n = FOLLOW.get(parent[n], parent[n])
        if n is None:
            continue
        if n not in mats:
            spec = MAP[n]
            if isinstance(spec, str) or spec[1] is None:
                mats[n] = Matrix.Translation(target(n) - heads[n])
            else:
                mats[n] = _segment(heads[n], heads[spec[1]], target(n), target(spec[1]))
        mats[name] = mats[n]
        wbone[name] = WEIGHT_AS.get(name, WEIGHT_AS.get(n, MAP[n] if isinstance(MAP[n], str) else MAP[n][0]))

    drop = [re.compile(p) for p in DROP]
    rot = Matrix.Rotation(SRC_MESH_ROT_X * 3.141592653589793 / 180.0, 4, "X")
    out = {}
    for ob in meshes:
        groups = {g.index: short(g.name) for g in ob.vertex_groups}
        me = ob.data.copy()
        me.transform(rot)
        # Vertex group names live on the mesh: the base's joints are added
        # to the copy's list (through a stand-in object) before weighting.
        tmp = bpy.data.objects.new("_retarget", me)
        jindex = {b.name: (tmp.vertex_groups.get(b.name) or tmp.vertex_groups.new(name=b.name)).index
                  for b in skel.data.bones}
        bm = bmesh.new()
        bm.from_mesh(me)
        deform = bm.verts.layers.deform.verify()
        dead, unmapped = [], 0
        for v in bm.verts:
            ws = {groups[g]: w for g, w in v[deform].items() if g in groups and w > 0}
            dropped = sum(w for g, w in ws.items() if any(r.search(g) for r in drop))
            ws = {g: w for g, w in ws.items() if g in mats or any(r.search(g) for r in drop)}
            total = sum(ws.values())
            if total <= 0:
                unmapped += 1
            if total <= 0 or sum(w for g, w in ws.items() if any(r.search(g) for r in drop)) > 0.5 * total:
                dead.append(v)
                continue
            p = game(ob.matrix_world @ v.co)
            q = Vector((0, 0, 0))
            bw = {}
            total -= dropped
            for g, w in ws.items():
                if g not in mats or any(r.search(g) for r in drop):
                    continue
                q += (mats[g] @ p) * (w / total)
                bw[wbone[g]] = bw.get(wbone[g], 0.0) + w / total
            v.co = q
            # Quantised: the heaviest MAX_BONES, in WEIGHT_STEPs, summing to 1.
            top = sorted(bw.items(), key=lambda kv: -kv[1])[:MAX_BONES]
            s = sum(w for _, w in top)
            q = [(j, round(w / s / WEIGHT_STEP) * WEIGHT_STEP) for j, w in top]
            q = [(j, w) for j, w in q if w > 0] or [top[0]]
            fix = 1.0 - sum(w for _, w in q)
            q[0] = (q[0][0], q[0][1] + fix)
            v[deform].clear()
            for j, w in q:
                v[deform][jindex[j]] = w
        if unmapped:
            print(f"{ob.name}: {unmapped} vertices on no mapped bone, removed")
        bmesh.ops.delete(bm, geom=dead, context="VERTS")
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4 * SCALE)
        # Each face's UVs into the 0-1 tile (models often use another;
        # the game clamps, and bakes only see 0-1).
        import math
        uvl = bm.loops.layers.uv.active
        if uvl is not None:
            for f in bm.faces:
                for k in (0, 1):
                    lo = math.floor(min(l[uvl].uv[k] for l in f.loops) + 1e-4)
                    if lo:
                        for l in f.loops:
                            l[uvl].uv[k] -= lo
        bm.to_mesh(me)
        bm.free()
        bpy.data.objects.remove(tmp)
        for i, mat in enumerate(me.materials):
            name = f"{NAME}_{mat.name}" if len(meshes) > 1 or len(me.materials) > 1 else f"{NAME}_Body"
            part = me.copy()
            keep = [p.index for p in part.polygons if p.material_index != i]
            pb = bmesh.new()
            pb.from_mesh(part)
            pb.faces.ensure_lookup_table()
            bmesh.ops.delete(pb, geom=[pb.faces[k] for k in keep], context="FACES")
            pb.to_mesh(part)
            pb.free()
            if not part.polygons:
                continue
            part.materials.clear()
            part.materials.append(mat)
            for p in part.polygons:
                p.use_smooth = True
            o = bpy.data.objects.new(name, part)
            bpy.context.scene.collection.objects.link(o)
            o.parent = skel
            o.matrix_parent_inverse = Matrix()
            o.matrix_world = skel.matrix_world.copy()
            m = o.modifiers.new("Armature", "ARMATURE")
            m.object = skel
            out[name] = o
    # Only the base's joints remain as groups.
    for o in out.values():
        for g in list(o.vertex_groups):
            if not re.fullmatch(r"J\d+", g.name):
                o.vertex_groups.remove(g)
    for o in new:
        bpy.data.objects.remove(o)
    n = sum(len(o.data.polygons) for o in out.values())
    print(f"retargeted {len(out)} meshes, {n} faces onto {skel.name}")
    return out



def save_textures(obs, out_dir):
    """Each mesh's material image as a PNG in out_dir (glTF models often
    embed theirs), set as the material's hsd_texture. Returns {material
    name: png path}. Materials sharing an image share the file."""
    import os
    os.makedirs(out_dir, exist_ok=True)
    out, done = {}, {}
    for o in obs.values():
        for m in o.data.materials:
            img = next((n.image for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image), None) \
                if m.use_nodes else None
            if img is None:
                raise SystemExit(f"material {m.name} has no image texture")
            if img.name not in done:
                path = os.path.join(out_dir, bpy.path.clean_name(os.path.splitext(img.name)[0]) + ".png")
                if img.filepath and os.path.exists(bpy.path.abspath(img.filepath)) and \
                        bpy.path.abspath(img.filepath).lower().endswith(".png"):
                    import shutil
                    shutil.copyfile(bpy.path.abspath(img.filepath), path)
                else:
                    img.filepath_raw = path
                    img.file_format = "PNG"
                    img.save()
                done[img.name] = path
            m["hsd_texture"] = done[img.name]
            out[m.name] = done[img.name]
    return out


def bake_ao(obs, out_dir, size=1024, samples=64):
    """Ambient occlusion baked into each material image's UV space, for a
    model that has no AO map (melee_texture.py --ao). One PNG per image
    (out_dir/<image>_ao.png), in the bind pose. Returns {image path: ao
    path} for the images save_textures wrote."""
    import os
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    groups = {}
    for o in obs.values():
        for m in o.data.materials:
            groups.setdefault(m["hsd_texture"], set()).add(o)
    out = {}
    for tex, objs in groups.items():
        img = bpy.data.images.new(os.path.basename(tex) + "_ao", size, size)
        nodes = []
        for o in objs:
            for m in o.data.materials:
                n = m.node_tree.nodes.new("ShaderNodeTexImage")
                n.image = img
                m.node_tree.nodes.active = n
                nodes.append((m, n))
        bpy.ops.object.select_all(action="DESELECT")
        for o in objs:
            o.select_set(True)
            o.hide_render = False
        bpy.context.view_layer.objects.active = next(iter(objs))
        bpy.ops.object.bake(type="AO", margin=8)
        path = os.path.join(out_dir, os.path.basename(tex).rsplit(".", 1)[0] + "_ao.png")
        img.filepath_raw = path
        img.file_format = "PNG"
        img.save()
        for m, n in nodes:
            m.node_tree.nodes.remove(n)
        out[tex] = path
    return out


def melee_mesh(obs, ratio=0.6, step=0.1, max_bones=3):
    """The Melee-ified costume's mesh: each mesh decimated to `ratio` of
    its triangles (Melee's 1-4k budget for a ~5-15k model), UV seams kept,
    weights quantised again (decimation blends them; few distinct
    envelopes keep the game's skinning right). In place."""
    for o in obs.values():
        bpy.context.view_layer.objects.active = o
        d = o.modifiers.new("Decimate", "DECIMATE")
        d.ratio = ratio
        o.modifiers.move(len(o.modifiers) - 1, 0)
        bpy.ops.object.modifier_apply(modifier="Decimate")
        for v in o.data.vertices:
            ws = sorted(((g.group, g.weight) for g in v.groups if g.weight > 0), key=lambda t: -t[1])[:max_bones]
            if not ws:
                continue
            s = sum(w for _, w in ws)
            q = [(gi, round(w / s / step) * step) for gi, w in ws]
            q = [t for t in q if t[1] > 0] or [ws[0]]
            q[0] = (q[0][0], q[0][1] + 1.0 - sum(w for _, w in q))
            for g in list(v.groups):
                o.vertex_groups[g.group].remove([v.index])
            for gi, w in q:
                o.vertex_groups[gi].add([v.index], w, "REPLACE")
    print(f"melee mesh: {sum(len(o.data.polygons) for o in obs.values())} faces")
