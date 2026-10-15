# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: any model file to glTF (model.glb) for blender_retarget.py, with
its armature, skinned meshes and textures.

    blender -b [MODEL.blend] --python blender_to_glb.py -- IN OUT.glb [--frame N]

IN: .fbx, .obj, .glb/.gltf, .dae (Blender 5 has no COLLADA: convert with
assimp first), or .blend (pass the .blend as Blender's file, and IN the
same path). Textures the model references are packed into the glb.
Prints the armature, meshes, materials and bones, like make_pack.sh
--inspect. --frame: the pose at that animation frame is the rest pose
(models that ship in an animation, not a T/A-pose).
"""
import os
import sys

import bpy

args = sys.argv[sys.argv.index("--") + 1:]
src, out = args[0], args[1]
frame = int(args[args.index("--frame") + 1]) if "--frame" in args else None
ext = os.path.splitext(src)[1].lower()
if ext != ".blend":
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=src, automatic_bone_orientation=True)
    elif ext == ".obj":
        bpy.ops.wm.obj_import(filepath=src)
    elif ext in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=src)
    else:
        raise SystemExit(f"{src}: convert {ext} to .glb or .fbx first (assimp export IN out.glb -f glb2)")
# Textures by file name next to the model or in a textures/ folder beside it.
roots = [os.path.dirname(src), os.path.join(os.path.dirname(src), "..", "textures"),
         os.path.join(os.path.dirname(src), "textures")]
for img in bpy.data.images:
    if img.packed_file is None and img.filepath and not os.path.exists(bpy.path.abspath(img.filepath)):
        name = os.path.basename(bpy.path.abspath(img.filepath).replace("\\", "/"))
        for r in roots:
            p = os.path.join(r, name)
            if os.path.exists(p):
                img.filepath = p
                break
if frame is not None:
    bpy.context.scene.frame_set(frame)
arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
for a in arms:
    print("ARMATURE", a.name, len(a.data.bones), "bones")
for m in meshes:
    mods = [md.object.name for md in m.modifiers if md.type == "ARMATURE" and md.object]
    print("MESH", m.name, len(m.data.polygons), "faces", "materials", [x.name for x in m.data.materials if x],
          "armature", mods or (m.parent.name if m.parent else "-"), "hidden" if m.hide_get() or m.hide_render else "")
for img in bpy.data.images:
    print("IMAGE", img.name, img.filepath, "packed" if img.packed_file else "",
          "missing" if img.source == "FILE" and not img.packed_file and not os.path.exists(bpy.path.abspath(img.filepath)) else "")
for o in bpy.data.objects:
    o.hide_set(False)
    o.hide_viewport = False
bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_skins=True, export_animations=False,
                          export_apply=False, export_yup=True, use_visible=False)
print("glb ->", out)
