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
    SRC_ARMATURE = r"Body"      # the model's armature (regex on its name) when
                                # it has several; the first otherwise
    SRC_ROT_Z = 0               # degrees: turn the whole model about the up
                                # axis first (180 for one facing +Y: its toes
                                # point +Y, its left is -X)
    SCALE = 11.0                # model units -> game units, before fitting
    DROP = [r"Hand[12]$", ...]  # bones (regex) whose faces are removed (spare
                                # parts the model keeps out of sight)
    DROP_MESHES = [r"^Hands$"]  # mesh objects (regex) left out
    REWEIGHT = [(r"^Robe", {"J07": ("J04", 0.7)})]
                                # per mesh (regex): move that share of a
                                # joint's weight to another (a long robe or
                                # skirt mostly on the hips, not the thighs)
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
    SRC_POSE = [("CapeL1", (0, 1, 0), 50)]  # bend the model's own bones first,
                                # in order, about a Blender-world axis through
                                # each bone's head (degrees): a cape draped
                                # down the sides, a tail curled. Only bones
                                # that follow others (not in MAP)
    exec(open("./blender_retarget.py").read()); OBS = retarget()

Then `save_textures(OBS, dir)` writes every material's image as a PNG
(embedded ones too) and sets it as the material's texture for the
exporter; `add_prop(OBS, file, joint, name, size, ...)` adds a rigid mesh from
another file on one joint (a vehicle form, summoned creatures).

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
SRC_ROT_Z = globals().get("SRC_ROT_Z", 0.0)
SRC_ARMATURE = globals().get("SRC_ARMATURE")
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
DROP_MESHES = globals().get("DROP_MESHES", [])
REWEIGHT = globals().get("REWEIGHT", [])
SRC_POSE = globals().get("SRC_POSE", [])

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
    arm = next(o for o in new if o.type == "ARMATURE"
               and (SRC_ARMATURE is None or re.search(SRC_ARMATURE, o.name)))
    meshes = [o for o in new if o.type == "MESH" and any(m.type == "ARMATURE" for m in o.modifiers)
              and not any(re.search(p, o.name) for p in DROP_MESHES)]
    # One mesh: parts sharing a material (eyes, teeth, collar as separate
    # objects) become one output object per material, not one each.
    # Parts with no material (teeth, spikes left plain): one "Plain"
    # material, coloured by save_textures' colors ({"Plain": (r, g, b)}).
    for o in meshes:
        if not o.data.materials:
            o.data.materials.append(bpy.data.materials.get("Plain") or bpy.data.materials.new("Plain"))
    if len(meshes) > 1:
        names = [o.name for o in new]
        with bpy.context.temp_override(active_object=meshes[0], selected_editable_objects=meshes):
            bpy.ops.object.join()
        new = [bpy.data.objects[n] for n in names if n in bpy.data.objects]
        meshes = meshes[:1]

    face = Matrix.Rotation(SRC_ROT_Z * 3.141592653589793 / 180.0, 4, "Z")

    # The model's own bones bent first (SRC_POSE): the meshes are taken as
    # that pose deforms them.
    for name, axis, deg in SRC_POSE:
        pb = next(b for b in arm.pose.bones if b.name == name or b.name.endswith("_" + name))
        head = arm.matrix_world @ pb.head
        rw = (Matrix.Translation(head) @ Matrix.Rotation(deg * 3.141592653589793 / 180.0, 4, Vector(axis))
              @ Matrix.Translation(-head))
        pb.matrix = arm.matrix_world.inverted() @ rw @ arm.matrix_world @ pb.matrix
        bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get() if SRC_POSE else None

    def game(p):
        return (TO_GAME @ face @ p) * SCALE

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
        reweight = [(p, m) for p, m in REWEIGHT if re.search(p, ob.name)]
        me = (bpy.data.meshes.new_from_object(ob.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
              if dg else ob.data.copy())
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
            for pat, moves in reweight:
                for j, (to, share) in moves.items():
                    if j in bw:
                        w = bw[j] * share
                        bw[j] -= w
                        bw[to] = bw.get(to, 0.0) + w
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



def save_textures(obs, out_dir, search=(), colors=None, assign=None):
    """Each mesh's material image as a PNG in out_dir (glTF models often
    embed theirs), set as the material's hsd_texture. Returns {material
    name: png path}. Materials sharing an image share the file.

    A material without an image (models whose converter lost the links):
    an image file in one of the `search` folders named like the material
    (or its base colour texture's name); else colors[material] = (r, g, b)
    fills a small flat texture (a texture the model's files lack).
    assign[material] = image path: that image, whatever the material has."""
    import os
    import shutil
    os.makedirs(out_dir, exist_ok=True)
    out, done = {}, {}
    colors = colors or {}
    for o in obs.values():
        for m in o.data.materials:
            img = None
            if assign and m.name in assign:
                img = bpy.data.images.load(assign[m.name], check_existing=True)
                m.use_nodes = True
                n = next((n for n in m.node_tree.nodes if n.type == "TEX_IMAGE"), None) or \
                    m.node_tree.nodes.new("ShaderNodeTexImage")
                n.image = img
                bsdf = next((b for b in m.node_tree.nodes if b.type == "BSDF_PRINCIPLED"), None)
                if bsdf is not None and not bsdf.inputs["Base Color"].is_linked:
                    m.node_tree.links.new(n.outputs["Color"], bsdf.inputs["Base Color"])
            elif m.use_nodes:
                img = next((n.image for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image), None)
            if img is None or (not img.packed_file and not os.path.exists(bpy.path.abspath(img.filepath))
                                and not img.has_data):
                stem = os.path.splitext(m.name)[0].rsplit(".0", 1)[0]
                found = None
                for d in search:
                    for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
                        if os.path.splitext(f)[0].lower() in (stem.lower(), m.name.lower()):
                            found = os.path.join(d, f)
                            break
                    if found:
                        break
                if found:
                    img = bpy.data.images.load(found, check_existing=True)
                elif m.name in colors or stem in colors:
                    c = colors.get(m.name, colors.get(stem))
                    img = bpy.data.images.new(stem + "_flat", 64, 64)
                    img.pixels = [c[0] / 255, c[1] / 255, c[2] / 255, 1.0] * (64 * 64)
                else:
                    raise SystemExit(f"material {m.name} has no image texture (search=, colors=)")
                m.use_nodes = True
                n = next((n for n in m.node_tree.nodes if n.type == "TEX_IMAGE"), None)
                if n is None:
                    n = m.node_tree.nodes.new("ShaderNodeTexImage")
                bsdf = next((b for b in m.node_tree.nodes if b.type == "BSDF_PRINCIPLED"), None)
                if bsdf is not None and not bsdf.inputs["Base Color"].is_linked:
                    m.node_tree.links.new(n.outputs["Color"], bsdf.inputs["Base Color"])
                n.image = img
            if img.name not in done:
                path = os.path.join(out_dir, bpy.path.clean_name(os.path.splitext(img.name)[0]) + ".png")
                src = bpy.path.abspath(img.filepath) if img.filepath else ""
                if src and os.path.exists(src) and src.lower().endswith(".png") and not img.is_dirty:
                    shutil.copyfile(src, path)
                else:
                    img.filepath_raw = path
                    img.file_format = "PNG"
                    img.save()
                done[img.name] = path
            m["hsd_texture"] = done[img.name]
            out[m.name] = done[img.name]
            # Cut-out textures (hair cards, lashes): their alpha is the
            # material's, for the renders as in the game.
            if img.channels == 4 and m.use_nodes:
                n = next((n for n in m.node_tree.nodes if n.type == "TEX_IMAGE" and n.image == img), None)
                bsdf = next((b for b in m.node_tree.nodes if b.type == "BSDF_PRINCIPLED"), None)
                if n is not None and bsdf is not None and not bsdf.inputs["Alpha"].is_linked:
                    px = list(img.pixels[3::4][::97])
                    if px and sum(1 for v in px if v < 0.5) > 0.01 * len(px):
                        m.node_tree.links.new(n.outputs["Alpha"], bsdf.inputs["Alpha"])
    return out


def add_prop(obs, src, joint, name, size, at=(0.0, 0.0, 0.0), rot=(0.0, 0.0, 0.0), texture=None, pick=None):
    """Add a rigid mesh from another file (.obj, .glb/.gltf, .fbx) on one
    base joint: a vehicle form, a summoned creature, a prop. It is turned
    by `rot` (degrees about game X, Y, Z: game space is Y up, +Z the
    fighter's front), scaled so its largest side is `size` game units, and
    placed with its footprint centred on at's x/z and its bottom at at's y.
    Its one material is "<NAME>_<name>" (texture: that image). pick: a
    regex on the file's object names, the ones taken (one of several
    characters in a file). Shown where
    that joint is: a joint the base never animates, kept at ~0 scale
    (build_dat --hide-joints) or in its own model-part state (--form)."""
    import math
    import os
    skel = bpy.data.objects["Skeleton"]
    before = set(bpy.data.objects)
    ext = os.path.splitext(src)[1].lower()
    if ext == ".obj":
        bpy.ops.wm.obj_import(filepath=src)
    elif ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=src)
    else:
        bpy.ops.import_scene.gltf(filepath=src)
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == "MESH" and (pick is None or re.search(pick, o.name))]
    if not meshes:
        raise SystemExit(f"add_prop {name}: no mesh in {src}" + (f" matching {pick!r}" if pick else ""))
    for o in meshes:
        for m in list(o.modifiers):
            o.modifiers.remove(m)
        o.data.transform(o.matrix_world)
        o.matrix_world = Matrix()
        o.parent = None
    if len(meshes) > 1:
        with bpy.context.temp_override(active_object=meshes[0], selected_editable_objects=meshes):
            bpy.ops.object.join()
    o = meshes[0]
    for x in new:
        if x != o and x.name in bpy.data.objects:
            bpy.data.objects.remove(x)
    me = o.data
    me.transform(TO_GAME)
    r = [math.radians(d) for d in rot]
    from mathutils import Euler
    me.transform(Euler(r, "XYZ").to_matrix().to_4x4())
    lo = [min(v.co[k] for v in me.vertices) for k in range(3)]
    hi = [max(v.co[k] for v in me.vertices) for k in range(3)]
    k = size / max(h - l for h, l in zip(hi, lo))
    me.transform(Matrix.Scale(k, 4))
    lo, hi = [x * k for x in lo], [x * k for x in hi]
    me.transform(Matrix.Translation(Vector((at[0] - (lo[0] + hi[0]) / 2, at[1] - lo[1], at[2] - (lo[2] + hi[2]) / 2))))
    o.name = me.name = f"{NAME}_{name}"
    mat = bpy.data.materials.new(f"{NAME}_{name}")
    old = me.materials[0] if me.materials else None
    img = None
    if texture:
        img = bpy.data.images.load(texture, check_existing=True)
    elif old is not None and old.use_nodes:
        img = next((n.image for n in old.node_tree.nodes if n.type == "TEX_IMAGE" and n.image), None)
    mat.use_nodes = True
    if img is not None:
        n = mat.node_tree.nodes.new("ShaderNodeTexImage")
        n.image = img
        bsdf = next(b for b in mat.node_tree.nodes if b.type == "BSDF_PRINCIPLED")
        mat.node_tree.links.new(n.outputs["Color"], bsdf.inputs["Base Color"])
    me.materials.clear()
    me.materials.append(mat)
    for p in me.polygons:
        p.use_smooth = True
    for g in list(o.vertex_groups):
        o.vertex_groups.remove(g)
    o.vertex_groups.new(name=joint).add([v.index for v in me.vertices], 1.0, "REPLACE")
    o.parent = skel
    o.matrix_parent_inverse = Matrix()
    o.matrix_world = skel.matrix_world.copy()
    m = o.modifiers.new("Armature", "ARMATURE")
    m.object = skel
    obs[o.name] = o
    print(f"prop {o.name}: {len(me.polygons)} faces on {joint}, "
          f"{tuple(round(h - l, 1) for h, l in zip(hi, lo))} game units")
    return o


def use_textures(obs, paths):
    """Give each material the image paths[material name] (its texture node
    created and connected to the base colour if missing) and set it as the
    exporter's hsd_texture: the costume's textures for renders and export."""
    for o in obs.values():
        for m in o.data.materials:
            path = paths.get(m.name)
            if path is None:
                continue
            m["hsd_texture"] = path
            m.use_nodes = True
            n = next((n for n in m.node_tree.nodes if n.type == "TEX_IMAGE"), None)
            if n is None:
                n = m.node_tree.nodes.new("ShaderNodeTexImage")
            n.image = bpy.data.images.load(path, check_existing=True)
            bsdf = next((b for b in m.node_tree.nodes if b.type == "BSDF_PRINCIPLED"), None)
            if bsdf is not None:
                for l in list(bsdf.inputs["Base Color"].links) + list(bsdf.inputs["Alpha"].links):
                    m.node_tree.links.remove(l)
                m.node_tree.links.new(n.outputs["Color"], bsdf.inputs["Base Color"])
                # Opaque, as the game draws it (converted materials often
                # carry a transparency that hides the model in renders) -
                # unless the image itself is a cut-out (hair cards), which
                # the game keeps too.
                bsdf.inputs["Alpha"].default_value = 1.0
                img = n.image
                if img.channels == 4:
                    px = list(img.pixels[3::4][::97])
                    if px and sum(1 for v in px if v < 0.5) > 0.01 * len(px):
                        m.node_tree.links.new(n.outputs["Alpha"], bsdf.inputs["Alpha"])
                for name in ("Emission Strength",):
                    if name in bsdf.inputs:
                        for l in list(bsdf.inputs[name].links):
                            m.node_tree.links.remove(l)
                        bsdf.inputs[name].default_value = 0.0
                if "Emission Color" in bsdf.inputs:
                    for l in list(bsdf.inputs["Emission Color"].links):
                        m.node_tree.links.remove(l)
