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
    CSP_POSE = {...}                                  # optional: the portrait's pose;
                                                      # or a game pose dump (MELEE_DUMP_POSE)
    exec(open(".../blender_render_ui.py").read())

CHECK_POSES (game pose dumps) adds check_<pose>.png sheets; CHECK_ONLY = True
renders only those.
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
ICON_JOINTS = globals().get("ICON_JOINTS", [])  # the head's joints ("J60"): icon and stock framed on
                                                # the vertices mostly weighted to them (else HEAD)
ICON_POSE = globals().get("ICON_POSE", POSE)    # the icon's and stock's pose (a pose dump: arms down)
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


def bounds_joints(objs, names, share=0.5):
    """Bounds of the posed vertices weighted at least `share` to the joints."""
    dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o in objs:
        idx = {o.vertex_groups[n].index for n in names if n in o.vertex_groups}
        if not idx:
            continue
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        for v, ve in zip(o.data.vertices, me.vertices):
            if sum(g.weight for g in v.groups if g.group in idx) >= share:
                pts.append(o.matrix_world @ ve.co)
        ev.to_mesh_clear()
    if not pts:
        return None
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
    """spec: {bone: XYZ degrees}, or a game pose dump's path (needs REPO)."""
    for pb in skel.pose.bones:
        pb.matrix_basis.identity()
        pb.rotation_mode = "XYZ"
        pb.rotation_euler = Euler((0, 0, 0))
    if isinstance(spec, str):
        bpy.context.view_layer.update()
        exec(open(os.path.join(REPO, "tools", "fighter", "blender_pose.py")).read(), {"POSE_JSON": spec})
        return
    for name, deg in spec.items():
        pb = skel.pose.bones.get(name)
        if pb is not None:
            pb.rotation_euler = Euler([math.radians(d) for d in deg])
    bpy.context.view_layer.update()


lights()
# CHECK_ONLY: only the CHECK_POSES sheets, no menu images.
for n, over in enumerate([] if globals().get("CHECK_ONLY") else COSTUMES):
    set_costume(over)
    # Portrait: the character select door's picture, Melee-HD sized
    # (544x752 for the disc's 136x188): big, in CSP_POSE, the upper body
    # like the disc's portraits.
    pose(CSP_POSE)
    lo, hi = bounds(parts)
    height = hi.z - lo.z
    # A wide upper body would leave sky above the head: the box keeps the
    # portrait's shape, hung from the top of the head.
    ulo, uhi = bounds_above(parts, lo.z + height * 0.5)
    across = math.hypot(uhi.x - ulo.x, uhi.y - ulo.y) * 0.86  # a little cut off at the sides
    tall = across * 752 / 544
    top = hi.z + tall * 0.03
    if tall > height * 1.06:
        # The whole fighter fits: centred, not hung from the top.
        top = (lo.z + hi.z) / 2 + tall / 2
    shoot(os.path.join(OUT, f"csp{n}.png"), 544, 752, (0.62, -1.0, 0.16),
          Vector((ulo.x, ulo.y, top - tall)), Vector((uhi.x, uhi.y, top)), 1.0)
    pose(ICON_POSE)
    lo, hi = bounds(parts)
    height = hi.z - lo.z
    # The head: its joints' vertices (ICON_JOINTS), else the top HEAD of
    # the fighter's height (0.42 default).
    hb = bounds_joints(parts, ICON_JOINTS) if ICON_JOINTS else None
    hlo, hhi = hb if hb else bounds_above(parts, hi.z - height * HEAD)
    shoot(os.path.join(OUT, f"stock{n}.png"), 144, 144, (0.0, -1.0, 0.05), hlo, hhi, 1.0)
    if n == 0:
        # Icon: the face, close (build_ui.py fits it into the grid icon).
        shoot(os.path.join(OUT, "icon.png"), 640, 560, (0.45, -1.0, 0.08), hlo, hhi, 0.95)
# CHECK_POSES (["/path/idle.json", ...]: game pose dumps, the game's
# MELEE_DUMP_POSE knob; needs REPO): the first costume in each pose, front, three-
# quarter and side, to OUT/check_<pose>.png: how it deforms in the game.
set_costume(COSTUMES[0] if COSTUMES else {})
for path in globals().get("CHECK_POSES", []):
    pose(path)
    lo, hi = bounds(parts)
    shots = []
    for k, d in enumerate(((0.0, -1.0, 0.12), (0.7, -1.0, 0.12), (1.0, 0.0, 0.12))):
        shot = os.path.join(OUT, f"_check{k}.png")
        shoot(shot, 400, 520, d, lo, hi, 1.05)
        shots.append(shot)
    from_shots = [bpy.data.images.load(s_) for s_ in shots]
    sheet = bpy.data.images.new("check", 1200, 520, alpha=True)
    px = [0.0] * (1200 * 520 * 4)
    for k, im in enumerate(from_shots):
        src = list(im.pixels)
        for y in range(520):
            row = src[y * 400 * 4:(y + 1) * 400 * 4]
            px[(y * 1200 + k * 400) * 4:(y * 1200 + k * 400 + 400) * 4] = row
        bpy.data.images.remove(im)
        os.remove(shots[k])
    sheet.pixels = px
    sheet.filepath_raw = os.path.join(OUT, "check_" + os.path.splitext(os.path.basename(path))[0] + ".png")
    sheet.file_format = "PNG"
    sheet.save()
set_costume({})
pose({})
print("rendered", len(COSTUMES), "costume(s) ->", OUT)
