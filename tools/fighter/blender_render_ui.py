# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: render a custom fighter's menu images for build_ui.py.

Renders the fighter's meshes (children of "Skeleton", not "Ref_") in a
relaxed pose, on a transparent background, with Cycles:

  csp<n>.png    character select portrait, three-quarter view, full body
  icon.png      character select grid icon: head and shoulders
  stock<n>.png  stock icon: the head, front

<n> is the costume: COSTUMES lists each costume's material image overrides
({material name: image path}); the first is the default costume.

    OUT = "/path/ui"; COSTUMES = [{}, {"ge_cape_out": ".../cape_red.png"}]
    POSE = {"J08": (0, 0, -70), "J31": (0, 0, 70)}   # bone: XYZ degrees
    exec(open(".../blender_render_ui.py").read())
"""
import math
import os

import bpy
from mathutils import Euler, Vector

OUT = globals().get("OUT", "/tmp/ui")
COSTUMES = globals().get("COSTUMES", [{}])
POSE = globals().get("POSE", {})
SAMPLES = globals().get("SAMPLES", 32)

os.makedirs(OUT, exist_ok=True)
scene = bpy.context.scene
skel = bpy.data.objects["Skeleton"]
parts = [o for o in bpy.data.objects
         if o.type == "MESH" and o.parent == skel and not o.name.startswith("Ref_")]
for o in scene.objects:
    if o.type == "MESH":
        o.hide_render = o not in parts

# Pose: rotations on top of the bind pose, in each bone's own space.
for pb in skel.pose.bones:
    pb.rotation_mode = "XYZ"
    pb.rotation_euler = Euler((0, 0, 0))
for name, deg in POSE.items():
    pb = skel.pose.bones.get(name)
    if pb is not None:
        pb.rotation_euler = Euler([math.radians(d) for d in deg])
bpy.context.view_layer.update()


def bounds(objs):
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in objs:
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        pts += [o.matrix_world @ v.co for v in me.vertices]
        ev.to_mesh_clear()
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    return lo, hi


def camera(name):
    cam = scene.objects.get(name)
    if cam is None:
        cam = bpy.data.objects.new(name, bpy.data.cameras.new(name))
        scene.collection.objects.link(cam)
    cam.data.type = "ORTHO"
    return cam


def lights():
    if scene.objects.get("UiKey") is None:
        for name, loc, power in (("UiKey", (6, -10, 14), 2500), ("UiFill", (-8, -6, 8), 900),
                                 ("UiRim", (0, 12, 12), 1500)):
            l = bpy.data.objects.new(name, bpy.data.lights.new(name, "AREA"))
            l.data.energy = power
            l.data.size = 6
            l.location = loc
            l.rotation_euler = (Vector((0, 0, 6)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
            scene.collection.objects.link(l)


def shoot(path, w, h, direction, frame_lo, frame_hi, margin=1.08):
    """Orthographic shot of the box [frame_lo, frame_hi] from `direction`."""
    cam = camera("UiCam")
    centre = (frame_lo + frame_hi) / 2
    size = frame_hi - frame_lo
    d = Vector(direction).normalized()
    cam.location = centre + d * 40
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    span_h = max(size.z, 1e-3)
    span_w = max(math.hypot(size.x, size.y) if abs(d.z) < 0.5 else size.x, 1e-3)
    cam.data.ortho_scale = max(span_h * w / h, span_w) * margin if w >= h else max(span_h, span_w * h / w) * margin
    scene.camera = cam
    scene.render.engine = "CYCLES"
    scene.cycles.samples = SAMPLES
    scene.cycles.device = "GPU" if bpy.context.preferences.addons.get("cycles") else "CPU"
    scene.render.film_transparent = True
    scene.render.resolution_x, scene.render.resolution_y = w, h
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def set_costume(over):
    for o in parts:
        for m in o.data.materials:
            img_node = next((n for n in m.node_tree.nodes if n.type == "TEX_IMAGE"), None)
            if img_node is None:
                continue
            if "ui_default" not in m:
                m["ui_default"] = img_node.image.filepath
            path = over.get(m.name, m["ui_default"])
            img_node.image = bpy.data.images.load(bpy.path.abspath(path), check_existing=True)


lights()
lo, hi = bounds(parts)
height = hi.z - lo.z
head_lo = Vector((lo.x, lo.y, hi.z - height * 0.36))
for n, over in enumerate(COSTUMES):
    set_costume(over)
    shoot(os.path.join(OUT, f"csp{n}.png"), 272, 376, (0.55, -1.0, 0.18), lo, hi)
    shoot(os.path.join(OUT, f"stock{n}.png"), 64, 64, (0.0, -1.0, 0.05), head_lo, hi, 1.02)
    if n == 0:
        shoot(os.path.join(OUT, "icon.png"), 128, 112, (0.35, -1.0, 0.1),
              Vector((lo.x, lo.y, hi.z - height * 0.6)), hi, 0.95)
set_costume({})
print("rendered", len(COSTUMES), "costume(s) ->", OUT)
