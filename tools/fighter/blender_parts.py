# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: low-poly modelling primitives for a custom fighter, built straight
onto a base fighter's skeleton (blender_import_base.py's "Skeleton").

A character's own build script (kept with its art, outside the repo) loads
this and builds its parts from these shapes, placed at the skeleton's bind
positions (jp) and weighted to its bones by name ("J23"):

    exec(open(".../tools/fighter/blender_parts.py").read())
    m = material("xx_body", "/path/tex/body.png")
    p = Part("Xx_Body", m)
    ellipsoid(p, jp(23) + Vector((0, 1.5, 0)), (1.7, 1.6, 1.6), [("J23", 1.0)])
    limb(p, jp(8), jp(9), 0.36, 0.32, "J08", None)
    p.finish()

Everything is in game space (Y up, +Z the fighter's front, its left at +X),
as children of the Skeleton object, so blender_export_fighter.py exports it
as it is. Each Part becomes one object with one material; re-running a
build replaces objects of the same name (and every object marked
"hsd_part" can be cleared with clear_parts()).

  material(name, png, double=False)   image-textured material
  Part(name, mat): vert(co, [(bone, w)...]), face(verts, uvs), finish()
  ring_grid(part, rows, weights_of, uv_of, closed=True)   quads between rings
  tube(part, centres, radii, weights_of, seg)   rings along a curve
  ellipsoid(part, centre, radii, weights, seg, rings, rot=None)
  limb(part, a, b, ra, rb, bone_a, bone_b)   tapered open tube a -> b
  jp(i)                                bind position of joint i (Vector)
"""
import math

import bmesh
import bpy
from mathutils import Matrix, Vector

skel = bpy.data.objects["Skeleton"]
J = {b.name: Vector([list(b["hsd_world"])[k] for k in (3, 7, 11)]) for b in skel.data.bones}


def jp(i):
    """Bind-pose position of joint i (depth-first index, as the bone names)."""
    return J[f"J{i:02d}"].copy()


def clear_parts():
    for ob in [o for o in bpy.data.objects if o.get("hsd_part")]:
        bpy.data.objects.remove(ob)


# ------------------------------------------------------------- materials ----

def material(name, image, double=False):
    """A material with one image texture (a PNG path), marked for
    blender_export_fighter.py: "hsd_texture" and "hsd_double_sided"."""
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        if n.type == "TEX_IMAGE":
            nt.nodes.remove(n)
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(image, check_existing=True)
    bsdf = nt.nodes.get("Principled BSDF")
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.8
    m["hsd_texture"] = image
    m["hsd_double_sided"] = double
    m.use_backface_culling = not double
    return m


# ---------------------------------------------------------------- parts ----
# A part is built into a shared bmesh per material: positions, UVs and one
# or two bone weights per vertex.

class Part:
    def __init__(self, name, mat):
        self.name, self.mat = name, mat
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UV")
        self.dl = self.bm.verts.layers.deform.new()
        self.groups = []

    def group(self, bone):
        if bone not in self.groups:
            self.groups.append(bone)
        return self.groups.index(bone)

    def vert(self, co, weights):
        v = self.bm.verts.new(co)
        for bone, w in weights:
            v[self.dl][self.group(bone)] = w
        return v

    def face(self, verts, uvs):
        try:
            f = self.bm.faces.new(verts)
        except ValueError:
            return None
        for loop, uv in zip(f.loops, uvs):
            loop[self.uv].uv = uv
        return f

    def finish(self):
        me = bpy.data.meshes.new(self.name)
        self.bm.normal_update()
        self.bm.to_mesh(me)
        self.bm.free()
        old = bpy.data.objects.get(self.name)
        if old is not None:
            bpy.data.objects.remove(old)
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.scene.collection.objects.link(ob)
        ob.parent = skel
        for g in self.groups:
            ob.vertex_groups.new(name=g)
        me.materials.append(self.mat)
        for p in me.polygons:
            p.use_smooth = True
        mod = ob.modifiers.new("Armature", "ARMATURE")
        mod.object = skel
        ob["hsd_part"] = True
        return ob


def ring_grid(part, rows, weights_of, uv_of, closed=True):
    """Quads between successive rings (each a list of positions, same
    length): closed around, or with closed=False an open strip (a cape open
    at the front). weights_of(r, i, pos), uv_of(r, i) -> (u, v)."""
    vs = [[part.vert(p, weights_of(r, i, p)) for i, p in enumerate(ring)] for r, ring in enumerate(rows)]
    n = len(rows[0])
    for r in range(len(rows) - 1):
        for i in range(n if closed else n - 1):
            j = (i + 1) % n
            uj = uv_of(r, i + 1)
            part.face([vs[r][i], vs[r + 1][i], vs[r + 1][j], vs[r][j]],
                      [uv_of(r, i), uv_of(r + 1, i), (uj[0], uv_of(r + 1, i + 1)[1]), uj])
    return vs


def ellipsoid(part, centre, radii, bone_w, seg=12, rings=8, uv_cyl=False, rot=None):
    """A UV ellipsoid; uv: u around (0.5 at +Z), v pole to pole."""
    rows = []
    for r in range(1, rings):
        phi = math.pi * r / rings
        ring = []
        for i in range(seg):
            th = 2 * math.pi * i / seg
            d = Vector((math.sin(phi) * math.sin(th) * radii[0], math.cos(phi) * radii[1],
                        math.sin(phi) * math.cos(th) * radii[2]))
            if rot is not None:
                d = rot @ d
            ring.append(centre + d)
        rows.append(ring)
    # u runs 0.5 .. 1.5 around (0.5 at +Z, the front); textures repeat.
    w = bone_w if callable(bone_w) else (lambda r, i, p: bone_w)
    vs = ring_grid(part, rows, w, lambda r, i: (i / seg + 0.5, 1 - (r + 1) / rings))
    # caps
    top = part.vert(centre + (rot @ Vector((0, radii[1], 0)) if rot else Vector((0, radii[1], 0))),
                    w(0, 0, centre))
    bot = part.vert(centre + (rot @ Vector((0, -radii[1], 0)) if rot else Vector((0, -radii[1], 0))),
                    w(len(rows) - 1, 0, centre))
    for i in range(seg):
        j = (i + 1) % seg
        part.face([top, vs[0][i], vs[0][j]], [((i + 0.5) / seg + 0.5, 1.0), (i / seg + 0.5, 1 - 1 / rings), ((i + 1) / seg + 0.5, 1 - 1 / rings)])
        part.face([bot, vs[-1][j], vs[-1][i]], [((i + 0.5) / seg + 0.5, 0.0), ((i + 1) / seg + 0.5, 1 / rings), (i / seg + 0.5, 1 / rings)])


def basis_along(a, b):
    """Rotation taking +Y onto the direction a->b."""
    d = (b - a).normalized()
    return Vector((0, 1, 0)).rotation_difference(d).to_matrix()


def limb(part, a, b, ra, rb, wa, wb, seg=8):
    """A tapered tube from a (radius ra) to b (rb), open ends (ball joints
    cover them). Weights blend from wa at a to wb at b over the last 20%."""
    rot = basis_along(a, b)
    length = (b - a).length
    rows = []
    for t in (0.0, 0.5, 1.0):
        r = ra + (rb - ra) * t
        rows.append([a + rot @ Vector((math.sin(2 * math.pi * i / seg) * r, t * length,
                                       math.cos(2 * math.pi * i / seg) * r)) for i in range(seg)])

    def w(rr, i, p):
        return [(wa, 1.0)] if rr < 2 or wb is None else [(wa, 0.5), (wb, 0.5)]

    ring_grid(part, rows, w, lambda r, i: (i / seg, r / 2))


def tube(part, centres, radii, weights_of, seg=12, uv_v=(0.0, 1.0), cap=True):
    """Rings of radius radii[k] around a curve through centres[k], each ring
    square to the curve (a bent hat tip, a curled ribbon), closed with a
    point at the end when cap. weights_of(k, i, pos) as ring_grid's; u runs
    around from 0.5 at the ring's +Z side, v from uv_v[0] to uv_v[1]."""
    n = len(centres)
    rows = []
    for k, c in enumerate(centres):
        d = centres[min(k + 1, n - 1)] - centres[max(k - 1, 0)]
        rot = Vector((0, 1, 0)).rotation_difference(d.normalized()).to_matrix()
        rows.append([c + rot @ Vector((math.sin(2 * math.pi * i / seg) * radii[k], 0,
                                       math.cos(2 * math.pi * i / seg) * radii[k])) for i in range(seg)])
    v_of = lambda k: uv_v[0] + (uv_v[1] - uv_v[0]) * k / max(n - 1, 1)  # noqa: E731
    vs = ring_grid(part, rows, weights_of, lambda k, i: (i / seg + 0.5, v_of(k)))
    if cap:
        tip = part.vert(centres[-1] + (centres[-1] - centres[-2]).normalized() * radii[-1] * 0.5,
                        weights_of(n - 1, 0, centres[-1]))
        for i in range(seg):
            part.face([vs[-1][i], vs[-1][(i + 1) % seg], tip],
                      [(i / seg + 0.5, v_of(n - 1)), ((i + 1) / seg + 0.5, v_of(n - 1)),
                       ((i + 0.5) / seg + 0.5, uv_v[1])])
    return vs

