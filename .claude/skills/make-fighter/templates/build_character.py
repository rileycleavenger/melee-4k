# <NAME> on <BASE>'s skeleton, in Blender (copy to
# ~/Melee4K-dev/fighters/<ID>/build_<name>.py and edit). Run inside Blender
# after tools/fighter/blender_import_base.py made "Skeleton":
#
#   REPO = ".../melee-4k"; TEX = ".../<ID>/tex"; exec(open(".../build_<name>.py").read())
#
# Game space: Y up, +Z the fighter's front, its left at +X. List the base's
# joints first (tools.md §4) and build every part at them; weight each part
# to the bones it should follow. Parts are coarse cages subdivided smooth
# (finish(subsurf=1)), painted with procedural materials (paint(): colour
# from PALETTE, which costumes change), joined into export meshes by how they
# move and shade, and decimated to the budget. make_pack.sh bakes them
# (tools.md §5b). The joint numbers below are Mario's.
import math

from mathutils import Vector

REPO = globals()["REPO"]
TEX = globals()["TEX"]
exec(open(f"{REPO}/tools/fighter/blender_parts.py").read())
exec(open(f"{REPO}/tools/fighter/blender_bake.py").read())

# sRGB colours by role; costumes override keys (make_pack.sh COSTUMES).
PALETTE = {
    "skin": (236, 190, 150),
    "cloth": (40, 100, 220),
    "accent": (250, 196, 44),
    "eye": (246, 244, 236),
}

V = Vector
UP = V((0, 1, 0))


def W(bone):
    return lambda *a: [(bone, 1.0)]


def sphere_rings(c, rx, ry, rz, n=10, phi0=0.0, phi1=180.0, up=UP):
    """Rings of an ellipsoid about axis `up`, pole to pole, for loft."""
    up = up.normalized()
    rings = []
    for k in range(n):
        phi = math.radians(phi0 + (phi1 - phi0) * k / (n - 1))
        s = max(math.sin(phi), 0.02)
        rings.append((c + up * (math.cos(phi) * ry), up, rx * s, rz * s))
    return rings


def ball(part, c, r, bone, seg=12, mat=0):
    loft(part, sphere_rings(V(c), r, r, r, 8, 8, 172), W(bone), cap_start=True, cap_end=True, seg=seg, mat=mat)


def capsule(part, a, b, ra, rb, bone, seg=10, ends=True):
    """A rounded tube a -> b (a limb segment, a finger)."""
    a, b = V(a), V(b)
    d = (b - a).normalized()
    rings = [(a - d * ra * 0.55, d, ra * 0.55, ra * 0.55)] if ends else []
    rings += [(a, d, ra, ra), (a.lerp(b, 0.5), d, (ra + rb) / 2, (ra + rb) / 2), (b, d, rb, rb)]
    if ends:
        rings.append((b + d * rb * 0.55, d, rb * 0.55, rb * 0.55))
    loft(part, rings, W(bone), cap_start=ends, cap_end=ends, seg=seg)


M = {
    "skin": paint("xx_p_skin", "skin", "flat"),
    "face": paint("xx_p_face", "skin", "flat", overlay=f"{TEX}/face.png"),
    "cloth": paint("xx_p_cloth", "cloth", "fabric"),
    "lining": paint("xx_p_lining", "accent", "fabric"),
    "eye": paint("xx_p_eye", "eye", "flat", overlay=f"{TEX}/iris.png"),
}


def build():
    clear_parts()
    for o in [o for o in bpy.data.objects if o.name.startswith("Xx_") or o.name in ("Cube", "Light", "Camera")]:
        bpy.data.objects.remove(o)
    out = {}

    # Head: rings from the crown down (v 1 -> 0), u = 0.5 at the front, so
    # face.png's centre column is the face.
    hc, hr = jp(23) + V((0, 1.4, 0.2)), (1.6, 1.5, 1.55)
    head = Part("Xx_HeadBall", [M["face"]])
    loft(head, sphere_rings(hc, *hr, n=14, phi0=6, phi1=174), W("J23"), cap_start=True, cap_end=True,
         seg=24, v_range=(1.0, 0.0))
    out["Xx_Head"] = head.finish(subsurf=1)

    # Eyes: balls looking forward (the iris at their front pole: iris.png's
    # top rows); no occlusion baked on them (make_pack.sh AO_FLOORS).
    eyes = Part("Xx_EyeBalls", [M["eye"]])
    for side in (-1, 1):
        d = V((side * 0.4, 0.1, 0.9)).normalized()
        c = hc + V((d.x * hr[0], d.y * hr[1], d.z * hr[2])) * 0.9
        loft(eyes, sphere_rings(c, 0.45, 0.45, 0.45, 10, 4, 176, up=d), W("J23"), cap_start=True,
             cap_end=True, seg=16, v_range=(1.0, 0.0))
    out["Xx_Eyes"] = eyes.finish(subsurf=1)

    # Torso and legs; arms and their balls in meshes of their own (they
    # shade nothing else: AO_NO_CAST).
    body, arms, joints = [], [], []
    torso = Part("Xx_Torso", [M["cloth"]])
    loft(torso, sphere_rings(V((0, 6.6, 0.05)), 1.4, 1.9, 1.15, 10, 8, 172),
         lambda r, i, p: [("J05", 1.0)] if p.y > 6.2 else [("J04", 1.0)], cap_start=True, cap_end=True, seg=18)
    body.append(torso.finish(subsurf=1))
    for up, el, wr in ((8, 9, 10), (31, 32, 33)):
        arm = Part(f"Xx_Arm{up}", [M["skin"]])
        capsule(arm, jp(up), jp(el), 0.42, 0.4, f"J{up:02d}", ends=False)
        capsule(arm, jp(el), jp(wr), 0.4, 0.38, f"J{el:02d}", ends=False)
        a = arm.finish(subsurf=1)
        grain(a, "x")  # T-pose arms lie along x (wood patterns)
        arms.append(a)
        jb = Part(f"Xx_ArmJoints{up}", [M["skin"]])
        for j, r in ((up, 0.5), (el, 0.45), (wr, 0.42)):
            ball(jb, jp(j), r, f"J{j:02d}")
        joints.append(jb.finish(subsurf=1))
    for hj, kn, an in ((48, 49, 51), (54, 55, 57)):
        leg = Part(f"Xx_Leg{hj}", [M["cloth"]])
        capsule(leg, jp(hj), jp(kn), 0.48, 0.44, f"J{hj:02d}", ends=False)
        capsule(leg, jp(kn), jp(an), 0.44, 0.4, f"J{kn:02d}")
        body.append(leg.finish(subsurf=1))
    out["Xx_Body"] = join("Xx_Body", body)
    out["Xx_Arms"] = join("Xx_Arms", arms)
    out["Xx_Joints"] = join("Xx_Joints", joints)

    # A cape: open at the front, a jagged hem (jag), the lining in the
    # accent colour on the inside (solidify's inner side).
    cape = Part("Xx_CapeSheet", [M["cloth"]])
    loft(cape, [(V((0, 8.9, -0.1)), UP, 1.2, 1.05),
                (V((0, 8.5, -0.15)), UP, 2.2, 1.55),
                (V((0, 7.6, -0.2)), UP, 3.0, 2.0),
                (V((0, 6.7, -0.25)), UP, 3.3, 2.2, 0.0, lambda i, a: (0, -0.6 if i % 2 else 0.0, 0))],
         lambda r, i, p: [("J22", 0.4), ("J05", 0.6)] if r == 0 else [("J05", 1.0)], open_arc=(38, 322), seg=23)
    cape.orient(lambda c: V((0, c.y + 0.6, -0.2)))
    out["Xx_Cape"] = cape.finish(solidify=0.07, inner_mat=M["lining"])

    for name, ob in out.items():
        ob.name = ob.data.name = name
    # To the budget (~20k triangles at most; the "exported" line counts them).
    for name, r in (("Xx_Body", 0.4), ("Xx_Arms", 0.4), ("Xx_Joints", 0.4), ("Xx_Head", 0.5)):
        decimate(out[name], r)
    return out


OBS = build()
print("built", sorted(OBS), sum(len(o.data.polygons) for o in OBS.values()), "faces")
