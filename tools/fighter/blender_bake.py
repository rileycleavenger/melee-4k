# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: paint a fighter with procedural materials, then bake each export
mesh into one HD texture per costume, with ambient occlusion for depth.

Two halves, both run inside Blender after blender_parts.py:

1. While modelling, give parts paint materials (instead of material()):

       wood = paint("xx_wood", "wood", pattern="wood")            # palette key
       hat = paint("xx_hat", "cloth", pattern="fabric", overlay="/p/emblem.png")
       Part("Xx_Hat", [hat])

   pattern: "wood" (grain bands along the object's grain axis, see below),
   "fabric" (soft noise), "flat", "gloss" (flat; for eyes). overlay: a PNG
   with alpha painted over the colour through the part's own "UV" map (faces,
   emblems, seams; UV conventions of blender_parts.py). The colour comes from
   PALETTE[key] (sRGB 0-255); costumes change palette entries.
   Wood grain runs along y (up) unless grain(ob, "x") (or "z") was called on
   the finished part; parts joined later keep their own grain.

2. When the model is done:

       PALETTE = {"wood": (226, 168, 108), "cloth": (40, 95, 215), ...}
       COSTUMES = {"Nr": {}, "Re": {"cloth": (205, 40, 40)}, ...}   # first = default
       BAKE = {"Xx_Body": 2048, "Xx_Hat": 1024, ...}      # export mesh: texture size
       DOUBLE = ["Xx_Cape"]                               # double-sided meshes
       OUT = "/path/tex"; exec(open(".../blender_bake.py").read()); bake_all()

   Per mesh it adds a "Bake" UV map (smart projection: its own atlas), bakes
   colour (emission) per costume and ambient occlusion once (Cycles, with
   every part present, so a hat darkens the head under it), and writes
   OUT/<mesh>_<costume>.png = colour x occlusion. Then each mesh gets one image
   material named after it in lower case (the Nr texture), ready for
   blender_export_fighter.py, and OUT/bake.json lists
   {material: {costume: png}} for build_dat.py's --texture and
   blender_render_ui.py's COSTUMES. A mesh is baked again for a costume only
   if its materials use a palette key the costume changes.

AO_NO_CAST (["Xx_Arms"]): meshes that shade no other mesh. Limbs move far
from their bind pose; their occlusion baked onto the body or a cape would
show as dark blotches wherever they are not. Keep arms a separate mesh.
POSE ({"J08": (0, 0, -70), ...}, bone: XYZ degrees) poses the skeleton
for the bake (default: the bind pose).
AO strength: AO_FLOOR (0.38): the darkest a fully occluded texel gets.
AO_FLOORS ({"Xx_Eyes": 1.0}) sets it per mesh; 1.0 bakes no occlusion
(eyes set into sockets would go grey). AO_DISTANCE (1.6): how far occlusion reaches, in model units.
"""
import json
import math
import os

import bpy
import numpy as np

PALETTE = globals().get("PALETTE", {})
AO_FLOOR = globals().get("AO_FLOOR", 0.38)
AO_DISTANCE = globals().get("AO_DISTANCE", 1.6)
AO_SAMPLES = globals().get("AO_SAMPLES", 96)
MARGIN = globals().get("MARGIN", 16)


def _lin(c):
    """sRGB 0-255 -> linear 0-1 (Blender colour sockets are linear)."""
    out = []
    for v in c[:3]:
        v = v / 255.0
        out.append(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4)
    return out + [1.0]


def paint(name, key, pattern="flat", overlay=None, dark=0.72, scale=1.0):
    """A procedural material whose colour is PALETTE[key] (node "pal:key")."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(emit.outputs[0], out.inputs["Surface"])
    rgb = nt.nodes.new("ShaderNodeRGB")
    rgb.name = rgb.label = f"pal:{key}"
    rgb.outputs[0].default_value = _lin(PALETTE.get(key, (200, 200, 200)))
    col = rgb.outputs[0]
    if pattern in ("wood", "fabric"):
        coord = nt.nodes.new("ShaderNodeTexCoord")
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        darker = nt.nodes.new("ShaderNodeMix")  # base x dark
        darker.data_type = "RGBA"
        darker.blend_type = "MULTIPLY"
        darker.inputs[0].default_value = 1.0
        nt.links.new(col, darker.inputs[6])
        darker.inputs[7].default_value = (dark, dark * 0.93, dark * 0.86, 1.0)
        if pattern == "wood":
            # Grain: bands across the grain axis, warped by noise; the
            # axis comes from the object ("grain" attribute -> mapping).
            attr = nt.nodes.new("ShaderNodeAttribute")
            attr.attribute_type = "GEOMETRY"
            attr.attribute_name = "grain_rot"
            mapn = nt.nodes.new("ShaderNodeMapping")
            nt.links.new(coord.outputs["Object"], mapn.inputs["Vector"])
            nt.links.new(attr.outputs["Vector"], mapn.inputs["Rotation"])
            wave = nt.nodes.new("ShaderNodeTexWave")
            wave.wave_type = "BANDS"
            wave.bands_direction = "X"
            wave.wave_profile = "SIN"
            wave.inputs["Scale"].default_value = 1.1 * scale
            wave.inputs["Distortion"].default_value = 5.0
            wave.inputs["Detail"].default_value = 3.0
            wave.inputs["Detail Scale"].default_value = 0.6
            nt.links.new(mapn.outputs["Vector"], wave.inputs["Vector"])
            ramp = nt.nodes.new("ShaderNodeValToRGB")
            ramp.color_ramp.elements[0].position = 0.45
            ramp.color_ramp.elements[1].position = 1.0
            ramp.color_ramp.elements[0].color = (0, 0, 0, 1)
            ramp.color_ramp.elements[1].color = (0.55, 0.55, 0.55, 1)
            nt.links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
            fac = ramp.outputs["Color"]
        else:
            noise = nt.nodes.new("ShaderNodeTexNoise")
            noise.inputs["Scale"].default_value = 9.0 * scale
            noise.inputs["Detail"].default_value = 6.0
            nt.links.new(coord.outputs["Object"], noise.inputs["Vector"])
            ramp = nt.nodes.new("ShaderNodeValToRGB")
            ramp.color_ramp.elements[0].position = 0.3
            ramp.color_ramp.elements[1].position = 0.75
            ramp.color_ramp.elements[0].color = (0.22, 0.22, 0.22, 1)
            ramp.color_ramp.elements[1].color = (0, 0, 0, 1)
            nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
            fac = ramp.outputs["Color"]
        nt.links.new(fac, mix.inputs[0])
        nt.links.new(col, mix.inputs[6])
        nt.links.new(darker.outputs[2], mix.inputs[7])
        col = mix.outputs[2]
    if overlay:
        uv = nt.nodes.new("ShaderNodeUVMap")
        uv.uv_map = "UV"
        img = nt.nodes.new("ShaderNodeTexImage")
        img.image = bpy.data.images.load(overlay, check_existing=True)
        nt.links.new(uv.outputs[0], img.inputs[0])
        over = nt.nodes.new("ShaderNodeMix")
        over.data_type = "RGBA"
        nt.links.new(img.outputs["Alpha"], over.inputs[0])
        nt.links.new(col, over.inputs[6])
        nt.links.new(img.outputs["Color"], over.inputs[7])
        col = over.outputs[2]
    nt.links.new(col, emit.inputs["Color"])
    m["hsd_paint"] = True
    m["hsd_paint_args"] = json.dumps([key, pattern, dark, scale])
    return m


def paint_with_overlay(m, overlay, uv_map="UV"):
    """A copy of paint material m with an overlay image on top (alpha;
    nothing outside the image: CLIP)."""
    key, pattern, dark, scale = json.loads(m["hsd_paint_args"])
    v = paint(m.name + "_ov", key, pattern, overlay=overlay, dark=dark, scale=scale)
    for n in v.node_tree.nodes:
        if n.type == "TEX_IMAGE":
            n.extension = "CLIP"
        if n.type == "UVMAP":
            n.uv_map = uv_map
    return v


def set_palette(pal):
    for m in bpy.data.materials:
        if not m.get("hsd_paint"):
            continue
        for n in m.node_tree.nodes:
            if n.name.startswith("pal:") and n.name[4:] in pal:
                n.outputs[0].default_value = _lin(pal[n.name[4:]])


def _keys_of(ob):
    keys = set()
    for m in ob.data.materials:
        if m and m.get("hsd_paint"):
            keys |= {n.name[4:] for n in m.node_tree.nodes if n.name.startswith("pal:")}
    return keys


GRAIN_ROT = {"x": (0.0, 0.0, math.pi / 2), "y": (0.0, 0.0, 0.0), "z": (math.pi / 2, 0.0, 0.0)}


def grain(ob, axis="y"):
    """Wood grain along axis for this part (a per-vertex attribute, kept
    through join())."""
    a = ob.data.attributes.get("grain_rot") or ob.data.attributes.new("grain_rot", "FLOAT_VECTOR", "POINT")
    rot = GRAIN_ROT[axis]
    a.data.foreach_set("vector", list(rot) * len(ob.data.vertices))


def _grain(ob):
    if "grain_rot" not in ob.data.attributes:
        grain(ob, "y")


def _select_only(ob):
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob


def _unwrap(ob):
    me = ob.data
    if "Bake" not in me.uv_layers:
        me.uv_layers.new(name="Bake")
    me.uv_layers.active = me.uv_layers["Bake"]
    _select_only(ob)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.012, area_weight=0.0,
                             scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    me.uv_layers["Bake"].active_render = True


def _target(ob, image):
    for m in ob.data.materials:
        nt = m.node_tree
        n = nt.nodes.get("bake_target") or nt.nodes.new("ShaderNodeTexImage")
        n.name = "bake_target"
        n.image = image
        for o in nt.nodes:
            o.select = False
        n.select = True
        nt.nodes.active = n


def _bake(ob, kind, size, samples):
    img = bpy.data.images.new(f"{ob.name}_{kind}", size, size, alpha=False, float_buffer=True)
    if kind == "AO":
        img.colorspace_settings.name = "Non-Color"
    _target(ob, img)
    _select_only(ob)
    bpy.context.scene.cycles.samples = samples
    bpy.ops.object.bake(type=kind, margin=MARGIN, use_clear=True)
    px = np.empty(size * size * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(size, size, 4)


def bake_all():
    from_ = globals()
    costumes = from_.get("COSTUMES", {"Nr": {}})
    sizes = from_["BAKE"]
    double = set(from_.get("DOUBLE", []))
    out = from_["OUT"]
    os.makedirs(out, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.render.bake.margin = MARGIN
    if scene.world is None:
        scene.world = bpy.data.worlds.new("World")
    scene.world.light_settings.distance = AO_DISTANCE
    for o in scene.objects:
        if o.type == "MESH":
            o.hide_render = o.name.startswith("Ref_")
    obs = [bpy.data.objects[n] for n in sizes]
    for ob in obs:
        _grain(ob)
        _unwrap(ob)
    # Bake in a relaxed pose (POSE, as blender_render_ui.py's), so arms
    # held out in the bind pose do not shadow the body and cape.
    skel = bpy.data.objects["Skeleton"]
    from mathutils import Euler
    for name, deg in from_.get("POSE", {}).items():
        pb = skel.pose.bones.get(name)
        if pb is not None:
            pb.rotation_mode = "XYZ"
            pb.rotation_euler = Euler([math.radians(d) for d in deg])
    bpy.context.view_layer.update()
    names = list(costumes)
    result = {}
    for ob in obs:
        size = sizes[ob.name]
        floor = from_.get("AO_FLOORS", {}).get(ob.name, AO_FLOOR)
        if floor >= 1.0:
            shade = 1.0
        else:
            # Moving parts (arms) cast no occlusion on the rest: hidden
            # while anything else bakes.
            hidden = [bpy.data.objects[n] for n in from_.get("AO_NO_CAST", []) if n != ob.name]
            for o in hidden:
                o.hide_render = True
            ao = _bake(ob, "AO", size, AO_SAMPLES)[..., :1]
            for o in hidden:
                o.hide_render = False
            shade = floor + (1.0 - floor) * np.clip(ao, 0, 1) ** 0.8
        keys = _keys_of(ob)
        mat = ob.name.lower()
        result[mat] = {}
        for c in names:
            pal = dict(PALETTE)
            pal.update(costumes[c])
            if c != names[0] and not keys & set(costumes[c]):
                result[mat][c] = result[mat][names[0]]
                continue
            set_palette(pal)
            col = _bake(ob, "EMIT", size, 1)
            # Emission bakes to a linear float image; to sRGB, shade there.
            rgb = np.where(col[..., :3] <= 0.0031308, col[..., :3] * 12.92,
                           1.055 * np.power(np.clip(col[..., :3], 0, None), 1 / 2.4) - 0.055)
            rgb = np.clip(rgb * shade, 0, 1)
            arr = np.concatenate([rgb, np.ones_like(rgb[..., :1])], axis=-1)
            path = os.path.join(out, f"{ob.name}_{c}.png")
            # Blender stores byte images as sRGB already: write the encoded
            # values through a Non-Color image so they are not converted twice.
            img = bpy.data.images.new(os.path.basename(path), size, size, alpha=False)
            img.colorspace_settings.name = "Non-Color"
            img.pixels.foreach_set(arr.astype(np.float32).ravel())
            img.filepath_raw = path
            img.file_format = "PNG"
            img.save()
            bpy.data.images.remove(img)
            result[mat][c] = path
        set_palette(PALETTE)
        print(f"baked {ob.name} {size}px")
    # Swap in the image materials (export and menu renders use these).
    for ob in obs:
        mat = ob.name.lower()
        m = bpy.data.materials.get(mat) or bpy.data.materials.new(mat)
        m.use_nodes = True
        nt = m.node_tree
        nt.nodes.clear()
        out_n = nt.nodes.new("ShaderNodeOutputMaterial")
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        bsdf.inputs["Roughness"].default_value = 0.75
        uvn = nt.nodes.new("ShaderNodeUVMap")
        uvn.uv_map = "Bake"
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(result[mat][names[0]], check_existing=True)
        nt.links.new(uvn.outputs[0], tex.inputs[0])
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        nt.links.new(bsdf.outputs[0], out_n.inputs["Surface"])
        m["hsd_texture"] = result[mat][names[0]]
        m["hsd_double_sided"] = ob.name in double
        m.use_backface_culling = ob.name not in double
        ob.data.materials.clear()
        ob.data.materials.append(m)
        for p in ob.data.polygons:
            p.material_index = 0
        # Export reads the active UV map: the atlas.
        ob.data.uv_layers.active = ob.data.uv_layers["Bake"]
    json.dump(result, open(os.path.join(out, "bake.json"), "w"), indent=1)
    print("baked", len(obs), "meshes x", len(names), "costumes ->", out)
