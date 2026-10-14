# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: pose the "Skeleton" exactly as the game had it, from a pose dump
(the game's MELEE_DUMP_POSE=path:frame knob, src/melee/ft/ftcustom.c: every
joint's world matrix on that frame). Shows how the model really deforms in
the base's animations: a collar the head bends into, a cape an arm goes
through.

    POSE_JSON = "/tmp/idle.json"; exec(open(".../blender_pose.py").read())

Joint i of the dump is bone "J<i>" (the fighter's parts are its joints in
tree order). Positions are relative to joint 0 (the fighter's place and
facing on the stage are left out).
"""
import json

import bpy
from mathutils import Matrix

POSE_JSON = globals()["POSE_JSON"]


def _m(v):
    return Matrix((v[0:4], v[4:8], v[8:12], (0.0, 0.0, 0.0, 1.0)))


def pose_from_dump(path):
    skel = bpy.data.objects["Skeleton"]
    joints = [_m(v) for v in json.load(open(path))["joints"]]
    root = joints[0].inverted()
    bones = sorted(skel.data.bones, key=lambda b: len(b.parent_recursive))
    for b in bones:
        i = int(b.name[1:])
        if i >= len(joints):
            continue
        bind = _m(list(b["hsd_world"]))
        pb = skel.pose.bones[b.name]
        pb.matrix = root @ joints[i] @ bind.inverted() @ b.matrix_local
        bpy.context.view_layer.update()


pose_from_dump(POSE_JSON)
