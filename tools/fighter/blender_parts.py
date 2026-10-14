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
  Part(name, mat or [mats]): vert(co, [(bone, w)...]), face(verts, uvs, mat=k),
      orient(inside_of), finish(subsurf=, solidify=, inner_mat=, smooth=)
  join(name, objects)                  merge finished parts into one export mesh
  decimate(ob, ratio)                  fewer triangles, same look (Melee budgets)
  loft(part, rings, weights_of, ...)   quads between free-standing ellipses
  spiral(part, centre, axis, r0, r1, turns, width, weights)   a ribbon curl
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
    """One mesh object. mat is a material or a list of them (face(...,
    mat=k) picks one); export needs a single material, which
    blender_bake.py makes by baking every material into one image."""

    def __init__(self, name, mat):
        self.name = name
        self.mats = list(mat) if isinstance(mat, (list, tuple)) else [mat]
        self.mat = self.mats[0]
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UV")
        self.dl = self.bm.verts.layers.deform.new()
        self.groups = []
        self.oriented = False

    def group(self, bone):
        if bone not in self.groups:
            self.groups.append(bone)
        return self.groups.index(bone)

    def vert(self, co, weights):
        v = self.bm.verts.new(co)
        for bone, w in weights:
            v[self.dl][self.group(bone)] = w
        return v

    def face(self, verts, uvs, mat=0):
        try:
            f = self.bm.faces.new(verts)
        except ValueError:
            return None
        f.material_index = mat
        for loop, uv in zip(f.loops, uvs):
            loop[self.uv].uv = uv
        return f

    def orient(self, inside_of):
        """Turn every face to point away from inside_of(face centre) (a
        point: the axis of a cape, the centre of a head)."""
        for f in self.bm.faces:
            c = f.calc_center_median()
            f.normal_update()
            if f.normal.dot(c - inside_of(c)) < 0:
                f.normal_flip()
        self.oriented = True

    def finish(self, subsurf=0, solidify=0.0, inner_mat=None, smooth=True):
        """Make the object. subsurf: Catmull-Clark levels applied (weights
        and UVs follow). solidify: give a sheet this thickness, the inner
        side in material inner_mat (a cape's lining)."""
        me = bpy.data.meshes.new(self.name)
        if not self.oriented:
            # Every face outward: the game draws front faces only, so a face
            # wound inward is a hole (Blender's renders show both sides).
            bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
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
        for m in self.mats:
            me.materials.append(m)
        if inner_mat is not None and inner_mat not in self.mats:
            me.materials.append(inner_mat)
            self.mats.append(inner_mat)
        if subsurf or solidify:
            if solidify:
                mod = ob.modifiers.new("Solidify", "SOLIDIFY")
                mod.thickness = solidify
                mod.offset = -1.0
                mod.use_even_offset = True
                if inner_mat is not None:
                    mod.material_offset = self.mats.index(inner_mat)
                    mod.material_offset_rim = self.mats.index(inner_mat)
            if subsurf:
                mod = ob.modifiers.new("Subsurf", "SUBSURF")
                mod.levels = mod.render_levels = subsurf
                mod.uv_smooth = "PRESERVE_BOUNDARIES"
            dg = bpy.context.evaluated_depsgraph_get()
            new = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), preserve_all_data_layers=True,
                                                  depsgraph=dg)
            ob.modifiers.clear()
            ob.data = new
            bpy.data.meshes.remove(me)
            me = new
        for p in me.polygons:
            p.use_smooth = smooth
        mod = ob.modifiers.new("Armature", "ARMATURE")
        mod.object = skel
        ob["hsd_part"] = True
        return ob


def join(name, objs):
    """Join finished parts into one object (one export mesh) named name."""
    objs = [o for o in objs if o is not None]
    keep = objs[0]
    with bpy.context.temp_override(active_object=keep, selected_editable_objects=objs,
                                   selected_objects=objs, object=keep):
        bpy.ops.object.join()
    keep.name = keep.data.name = name
    return keep


def decimate(ob, ratio):
    """Reduce a finished part to about ratio of its triangles (collapse:
    keeps the silhouette, weights and UVs). Smooth subdivided shapes keep
    their look down to ~0.35."""
    mod = ob.modifiers.new("Decimate", "DECIMATE")
    mod.ratio = ratio
    mod.use_collapse_triangulate = True
    ob.modifiers.move(len(ob.modifiers) - 1, 0)  # before the Armature
    dg = bpy.context.evaluated_depsgraph_get()
    arm = [m for m in ob.modifiers if m.type == "ARMATURE"]
    for m in arm:
        m.show_viewport = False
    dg.update()
    new = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
    for m in arm:
        m.show_viewport = True
    ob.modifiers.remove(mod)
    old = ob.data
    ob.data = new
    new.name = old.name
    bpy.data.meshes.remove(old)
    bm = bmesh.new()
    bm.from_mesh(new)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(new)
    bm.free()
    return ob


def loft(part, rings, weights_of, closed=True, cap_start=False, cap_end=False, mat=0, v_range=(0.0, 1.0),
         open_arc=None, seg=16):
    """Quads between rings, each ring given as (centre, normal, rx, rz,
    turn): an ellipse in the plane square to normal, rx along its side axis,
    rz along its front axis (+Z flattened into the plane; turn degrees spins
    the ellipse about normal). n points per ring = len of a shared angle
    list; u = 0.5 at the front. open_arc=(a0, a1) degrees keeps only that
    arc (a cape open at the front: (40, 320)). Returns the vertex rows."""
    if open_arc:
        angles = [math.radians(open_arc[0] + (open_arc[1] - open_arc[0]) * i / (seg - 1)) for i in range(seg)]
    else:
        angles = [2 * math.pi * i / seg for i in range(seg)]
    rows = []
    for (c, nrm, rx, rz, *rest) in rings:
        turn = rest[0] if rest else 0.0
        jag = rest[1] if len(rest) > 1 else None
        nrm = Vector(nrm).normalized()
        front = Vector((0, 0, 1))
        front = (front - nrm * front.dot(nrm))
        if front.length < 1e-4:
            front = Vector((0, 1, 0)) - nrm * nrm.y
        front.normalize()
        side = nrm.cross(front).normalized()
        if turn:
            q = Matrix.Rotation(math.radians(turn), 3, nrm)
            front, side = q @ front, q @ side
        ring = []
        for i, a in enumerate(angles):
            p = Vector(c) + side * (math.sin(a) * rx) + front * (math.cos(a) * rz)
            if jag is not None:
                p = p + Vector(jag(i, a))
            ring.append(p)
        rows.append(ring)
    n = len(angles)
    span = (open_arc[1] - open_arc[0]) / 360 if open_arc else 1.0
    m = len(rows)
    v_of = lambda r: v_range[0] + (v_range[1] - v_range[0]) * r / max(m - 1, 1)  # noqa: E731
    u_of = lambda i: (0.5 + (angles[i] if i < n else 2 * math.pi) / (2 * math.pi)) if not open_arc else (0.5 - span / 2 + span * i / (n - 1))  # noqa: E731
    vs = [[part.vert(p, weights_of(r, i, p)) for i, p in enumerate(ring)] for r, ring in enumerate(rows)]
    for r in range(m - 1):
        for i in range(n if not open_arc else n - 1):
            j = (i + 1) % n
            part.face([vs[r][i], vs[r][j], vs[r + 1][j], vs[r + 1][i]],
                      [(u_of(i), v_of(r)), (u_of(i + 1), v_of(r)), (u_of(i + 1), v_of(r + 1)), (u_of(i), v_of(r + 1))],
                      mat)
    for at, idx in ((cap_start, 0), (cap_end, m - 1)):
        if not at or open_arc:
            continue
        c = sum(rows[idx], Vector()) / n
        if idx == m - 1 and m > 1:
            c = c + (c - sum(rows[idx - 1], Vector()) / n).normalized() * min(rings[idx][2], rings[idx][3]) * 0.6
        elif m > 1:
            c = c - (sum(rows[1], Vector()) / n - c).normalized() * min(rings[0][2], rings[0][3]) * 0.6
        cv = part.vert(c, weights_of(idx, 0, c))
        for i in range(n):
            j = (i + 1) % n
            part.face([vs[idx][i], vs[idx][j], cv], [(u_of(i), v_of(idx)), (u_of(i + 1), v_of(idx)),
                                                     ((u_of(i) + u_of(i + 1)) / 2, v_of(idx))], mat)
    return vs


def spiral(part, centre, axis, r0, r1, turns, width, weights, seg_per_turn=20, mat=0):
    """A rolled ribbon (a scroll curl): a strip winding about axis from
    radius r0 in to r1 over turns, width across. Double-sided material."""
    axis = Vector(axis).normalized()
    a = axis.orthogonal().normalized()
    b = axis.cross(a)
    n = max(2, int(seg_per_turn * turns))
    rows = []
    for k in range(n + 1):
        t = k / n
        ang = 2 * math.pi * turns * t
        r = r0 + (r1 - r0) * t
        p = Vector(centre) + (a * math.cos(ang) + b * math.sin(ang)) * r
        rows.append((p - axis * width / 2, p + axis * width / 2, t))
    vs = [(part.vert(p, weights), part.vert(q, weights), t) for p, q, t in rows]
    for (p0, q0, t0), (p1, q1, t1) in zip(vs, vs[1:]):
        part.face([p0, p1, q1, q0], [(t0, 0), (t1, 0), (t1, 1), (t0, 1)], mat)


def ring_grid(part, rows, weights_of, uv_of, closed=True, mat=0):
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
                      [uv_of(r, i), uv_of(r + 1, i), (uj[0], uv_of(r + 1, i + 1)[1]), uj], mat)
    return vs


def ellipsoid(part, centre, radii, bone_w, seg=12, rings=8, uv_cyl=False, rot=None, mat=0):
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
    vs = ring_grid(part, rows, w, lambda r, i: (i / seg + 0.5, 1 - (r + 1) / rings), mat=mat)
    # caps
    top = part.vert(centre + (rot @ Vector((0, radii[1], 0)) if rot else Vector((0, radii[1], 0))),
                    w(0, 0, centre))
    bot = part.vert(centre + (rot @ Vector((0, -radii[1], 0)) if rot else Vector((0, -radii[1], 0))),
                    w(len(rows) - 1, 0, centre))
    for i in range(seg):
        j = (i + 1) % seg
        part.face([top, vs[0][i], vs[0][j]], [((i + 0.5) / seg + 0.5, 1.0), (i / seg + 0.5, 1 - 1 / rings), ((i + 1) / seg + 0.5, 1 - 1 / rings)], mat)
        part.face([bot, vs[-1][j], vs[-1][i]], [((i + 0.5) / seg + 0.5, 0.0), ((i + 1) / seg + 0.5, 1 / rings), (i / seg + 0.5, 1 / rings)], mat)


def basis_along(a, b):
    """Rotation taking +Y onto the direction a->b."""
    d = (b - a).normalized()
    return Vector((0, 1, 0)).rotation_difference(d).to_matrix()


def limb(part, a, b, ra, rb, wa, wb, seg=8, mat=0):
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

    ring_grid(part, rows, w, lambda r, i: (i / seg, r / 2), mat=mat)


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

