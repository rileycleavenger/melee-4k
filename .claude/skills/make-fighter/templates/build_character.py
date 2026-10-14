# <NAME> on <BASE>'s skeleton, in Blender (copy to
# ~/Melee4K-dev/fighters/<ID>/build_<name>.py and edit). Run inside Blender
# after tools/fighter/blender_import_base.py made "Skeleton":
#
#   REPO = ".../melee-4k"; TEX = ".../<ID>/tex"; exec(open(".../build_<name>.py").read())
#
# Game space: Y up, +Z the fighter's front, its left at +X. List the base's
# joints first (tools.md §4) and build every part at them; weight each part
# to the bones it should follow. Material names are what make_pack.sh's
# costume overrides refer to.
import math  # noqa: F401

from mathutils import Vector

TEX = globals().get("TEX")
exec(open(f"{globals()['REPO']}/tools/fighter/blender_parts.py").read())
_material = material


def material(name, image, double=False):  # noqa: F811
    return _material(name, f"{TEX}/{image}", double)


def build():
    clear_parts()
    m_head = material("xx_head", "head.png")
    m_cloth = material("xx_cloth", "cloth.png")
    m_accent = material("xx_accent", "accent.png", double=True)

    head_c = jp(23) + Vector((0, 1.5, 0.2))      # Mario: J23 is the neck/head joint
    head = Part("Xx_Head", m_head)
    ellipsoid(head, head_c, (1.6, 1.5, 1.5), [("J23", 1.0)], seg=16, rings=10)
    head.finish()

    body = Part("Xx_Body", m_cloth)
    ellipsoid(body, (jp(4) + jp(22)) * 0.5, (1.3, 1.7, 1.0),
              lambda r, i, p: [("J04", 1.0)] if p.y < jp(5).y else [("J22", 1.0)], seg=12, rings=8)
    for up, el, wr in ((8, 9, 10), (31, 32, 33)):            # arms (Mario's joints)
        limb(body, jp(up), jp(el), 0.4, 0.36, f"J{up:02d}", None)
        limb(body, jp(el), jp(wr), 0.36, 0.32, f"J{el:02d}", None)
    for hj, kn, an in ((48, 49, 51), (54, 55, 57)):          # legs
        limb(body, jp(hj), jp(kn), 0.5, 0.45, f"J{hj:02d}", None)
        limb(body, jp(kn), jp(an), 0.45, 0.4, f"J{kn:02d}", None)
    body.finish()

    # A hat or horn: a tube whose first centres rise straight (level rings),
    # then bend.
    hat = Part("Xx_Hat", m_accent)
    top = head_c + Vector((0, 1.2, 0))
    tube(hat, [top, top + Vector((0, 0.6, -0.2)), top + Vector((0, 1.0, -0.8))], [1.0, 0.6, 0.2],
         lambda k, i, p: [("J23", 1.0)], seg=12)
    hat.finish()


build()
print("built", [o.name for o in bpy.data.objects if o.get("hsd_part")])
