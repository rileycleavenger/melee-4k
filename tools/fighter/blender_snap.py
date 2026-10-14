# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: render a quick check image of the fighter (workbench, solid
colours) from the front, side or back, framed on visible meshes.

    OUT = "/tmp/x.png"; VIEW = "front"; HIDE = ["Ref_D37", ...]; exec(open(".../blender_snap.py").read())

VIEW: front, side, back, threequarter. HIDE: object names left out.
"""
import math

import bpy
from mathutils import Vector

OUT = globals().get("OUT", "/tmp/fighter.png")
VIEW = globals().get("VIEW", "front")
HIDE = set(globals().get("HIDE", []))
SIZE = globals().get("SIZE", 768)

scene = bpy.context.scene
objs = [o for o in scene.objects if o.type == "MESH"]
for o in objs:
    o.hide_render = o.name in HIDE
vis = [o for o in objs if not o.hide_render]
dg = bpy.context.evaluated_depsgraph_get()
pts = []
for o in vis:
    ev = o.evaluated_get(dg)
    me = ev.to_mesh()
    pts += [o.matrix_world @ v.co for v in me.vertices]
    ev.to_mesh_clear()
lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
centre, size = (lo + hi) / 2, max(hi - lo)
cam = scene.objects.get("SnapCam")
if cam is None:
    cam = bpy.data.objects.new("SnapCam", bpy.data.cameras.new("SnapCam"))
    scene.collection.objects.link(cam)
cam.data.type = "ORTHO"
cam.data.ortho_scale = size * 1.15
d = {"front": (0, -1, 0), "back": (0, 1, 0), "side": (1, 0, 0), "threequarter": (0.7, -0.7, 0.25)}[VIEW]
cam.location = centre + Vector(d).normalized() * size * 3
cam.rotation_euler = (centre - cam.location).to_track_quat("-Z", "Y").to_euler()
scene.camera = cam
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = globals().get("COLOR", "MATERIAL")
scene.render.resolution_x = scene.render.resolution_y = SIZE
scene.render.film_transparent = False
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("wrote", OUT, "size", round(size, 2))
