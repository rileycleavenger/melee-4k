# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: render a custom fighter's menu images for build_ui.py.

Renders the fighter's meshes (children of "Skeleton", not "Ref_") in a
relaxed pose, on a transparent background, with Cycles:

  csp<n>.png    character select portrait (544x752): three-quarter view,
                in CSP_POSE (default POSE), the upper body
  icon.png      character select grid icon source (640x560): the face
  stock<n>.png  stock icon (144x144): the head, front

<n> is the costume: COSTUMES lists each costume's material image overrides
({material name: image path}); the first is the default costume.

    OUT = "/path/ui"; COSTUMES = [{}, {"ge_cape_out": ".../cape_red.png"}]
    POSE = {"J08": (0, 0, -70), "J31": (0, 0, 70)}   # bone: XYZ degrees
    CSP_POSE = {...}                                  # optional: the portrait's pose
    exec(open(".../blender_render_ui.py").read())
"""
import math
import os

import bpy
from mathutils import Euler, Vector

OUT = globals().get("OUT", "/tmp/ui")
COSTUMES = globals().get("COSTUMES", [{}])
POSE = globals().get("POSE", {})
CSP_POSE = globals().get("CSP_POSE", POSE)
HEAD = globals().get("HEAD", 0.42)  # the head's share of the fighter's height
SAMPLES = globals().get("SAMPLES", 32)

os.makedirs(OUT, exist_ok=True)
scene = bpy.context.scene
skel = bpy.data.objects["Skeleton"]
parts = [o for o in bpy.data.objects
         if o.type == "MESH" and o.parent == skel and not o.name.startswith("Ref_")]
for o in scene.objects:
    if o.type == "MESH":
        o.hide_render = o not in parts

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


def bounds_above(objs, zmin):
    """Bounds of the parts' points above height zmin (the head, for the
    icon and stock icon, without the arms' width)."""
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in objs:
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        pts += [p for p in (o.matrix_world @ v.co for v in me.vertices) if p.z >= zmin]
        ev.to_mesh_clear()
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), zmin))
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


def pose(spec):
    for pb in skel.pose.bones:
        pb.rotation_mode = "XYZ"
        pb.rotation_euler = Euler((0, 0, 0))
    for name, deg in spec.items():
        pb = skel.pose.bones.get(name)
        if pb is not None:
            pb.rotation_euler = Euler([math.radians(d) for d in deg])
    bpy.context.view_layer.update()


lights()
for n, over in enumerate(COSTUMES):
    set_costume(over)
    # Portrait: the character select door's picture, Melee-HD sized
    # (544x752 for the disc's 136x188): big, in CSP_POSE, the upper body
    # like the disc's portraits.
    pose(CSP_POSE)
    lo, hi = bounds(parts)
    height = hi.z - lo.z
    shoot(os.path.join(OUT, f"csp{n}.png"), 544, 752, (0.62, -1.0, 0.16),
          Vector((lo.x, lo.y, lo.z + height * 0.5)), hi, 0.86)
    pose(POSE)
    lo, hi = bounds(parts)
    height = hi.z - lo.z
    # The head: the top HEAD of the fighter's height (HEAD = 0.42 default).
    hlo, hhi = bounds_above(parts, hi.z - height * HEAD)
    shoot(os.path.join(OUT, f"stock{n}.png"), 144, 144, (0.0, -1.0, 0.05), hlo, hhi, 1.0)
    if n == 0:
        # Icon: the face, close (build_ui.py fits it into the grid icon).
        shoot(os.path.join(OUT, "icon.png"), 640, 560, (0.45, -1.0, 0.08), hlo, hhi, 0.95)
set_costume({})
pose({})
print("rendered", len(COSTUMES), "costume(s) ->", OUT)
