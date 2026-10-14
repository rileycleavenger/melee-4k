# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender: dress an image-to-3D mesh (AssetFurnace / Hunyuan3D) as a fighter,
using a primitives model of the same character as its proxy.

The generated mesh has the likeness but no skeleton, parts or materials; the
proxy (blender_parts.py parts, painted with blender_bake.py) has all of them
but only roughly the shape. This moves everything onto the generated mesh:

1. Import the mesh and reduce it to AI_TRIS triangles.
2. Pose the skeleton like the generated mesh (POSE; image-to-3D gives
   whatever pose the picture had, usually arms down) and fit the mesh onto
   the posed proxy: uniform scale to the proxy's height, feet on the floor,
   centred.
3. Each face takes the part and material of the nearest proxy triangle that
   faces the same way (a cape's lining is the inside of the cape); each
   vertex takes bone weights, the "UV" map (overlays: faces, emblems) and
   the wood grain from the nearest point of its part's proxy.
4. Undo the pose (inverse skinning), so the mesh sits in the bind pose.

PROXY_PARTS (["Xx_Collar"]): parts kept whole from the proxy, the mesh's
faces there dropped: thin shells the generated surface makes ragged.
DROP_FACES (lambda part, centre: ...): more of the mesh's faces to drop
(fitted, posed coordinates). FORCE_PART (lambda cls, part, centre: part
or None): move faces to another part by colour class and place (the
face's wood above the shoulders is the head's, whatever is nearest).

LIMB_BONES: limbs hidden or fused in the picture (arms under a cape, hands
touching the body) cannot be un-posed cleanly; faces skinned mostly to
these bones are dropped from the mesh and the proxy's are kept instead.

The new parts replace the proxies under the same names, so bake_all() and
blender_export_fighter.py run unchanged afterwards.

    AI_MESH = "/path/shape.obj"    # .obj or .glb, Y up, facing +Z (Hunyuan's)
    PROXY = OBS                    # {name: proxy part}, as built
    POSE = {"J08": (0, 0, -70), "J31": (0, 0, 70)}   # the mesh's pose
    AI_TRIS = 9000                 # triangle budget for the whole model
    MIRROR_X = False               # True if the picture was mirrored
    BIAS = {"Xx_Cape": 0.5}        # parts that win near-ties (a cape over arms)
    LIMB_BONES = ["J08", ...]      # arms: from the proxy, not the mesh
    exec(open(".../blender_fit_ai.py").read()); OBS = fit_ai()

Check the result with blender_snap.py (and a part-coloured view: every face
should belong to the part it looks like) before baking.
"""
import math

import bmesh
import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector
from mathutils.bvhtree import BVHTree


def _import(path):
    before = set(bpy.data.objects)
    if path.lower().endswith(".obj"):
        bpy.ops.wm.obj_import(filepath=path)
    else:
        bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    if len(new) > 1:
        with bpy.context.temp_override(active_object=new[0], selected_editable_objects=new,
                                       selected_objects=new, object=new[0]):
            bpy.ops.object.join()
    ob = new[0]
    # The importers turn Y up into Blender's Z up; game space is the
    # skeleton's own (Y up, +Z front), Blender's world turned back -90 deg
    # about X.
    ob.data.transform(Matrix.Rotation(-math.pi / 2, 4, "X") @ ob.matrix_world)
    ob.matrix_world = Matrix.Identity(4)
    return ob


def _reduce(ob, tris):
    n = sum(len(p.vertices) - 2 for p in ob.data.polygons)
    if n > tris:
        mod = ob.modifiers.new("Decimate", "DECIMATE")
        mod.ratio = tris / n
        mod.use_collapse_triangulate = True
        dg = bpy.context.evaluated_depsgraph_get()
        new = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), depsgraph=dg)
        ob.modifiers.clear()
        old, ob.data = ob.data, new
        bpy.data.meshes.remove(old)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(ob.data)
    bm.free()


def _pose(skel, pose):
    for pb in skel.pose.bones:
        pb.rotation_mode = "XYZ"
        pb.rotation_euler = Euler((0, 0, 0))
    for name, deg in pose.items():
        skel.pose.bones[name].rotation_euler = Euler([math.radians(d) for d in deg])
    bpy.context.view_layer.update()


class _Proxy:
    """One proxy part, posed: triangles, their normals and materials, and
    per-corner weights, UVs and grain."""

    def __init__(self, ob):
        dg = bpy.context.evaluated_depsgraph_get()
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        me.calc_loop_triangles()
        self.co = [v.co.copy() for v in me.vertices]
        self.tris = [tuple(t.vertices) for t in me.loop_triangles]
        self.loops = [tuple(t.loops) for t in me.loop_triangles]
        self.mat = [t.material_index for t in me.loop_triangles]
        self.normal = [t.normal.copy() for t in me.loop_triangles]
        names = {g.index: g.name for g in ob.vertex_groups}
        self.weights = [{names[g.group]: g.weight for g in v.groups if g.weight > 0} for v in me.vertices]
        uv = me.uv_layers.get("UV")
        self.uv = [tuple(d.uv) for d in uv.data] if uv else None
        g = me.attributes.get("grain_rot")
        self.grain = [tuple(d.vector) for d in g.data] if g else None
        ev.to_mesh_clear()
        self.bvh = BVHTree.FromPolygons(self.co, self.tris)

    def nearest(self, p, n=None):
        """(triangle, distance) nearest p; with normal n, the nearest whose
        normal points the same way (within a little more distance)."""
        loc, _, i, d = self.bvh.find_nearest(p)
        if i is None or n is None or self.normal[i].dot(n) > 0.2:
            return i, d
        best = None
        for loc2, nrm, j, d2 in self.bvh.find_nearest_range(p, d * 1.6 + 0.15):
            if self.normal[j].dot(n) > 0.2 and (best is None or d2 < best[1]):
                best = (j, d2)
        return best if best else (i, d)

    def bary(self, i, p):
        a, b, c = (self.co[k] for k in self.tris[i])
        v0, v1, v2 = b - a, c - a, p - a
        d00, d01, d11 = v0.dot(v0), v0.dot(v1), v1.dot(v1)
        d20, d21 = v2.dot(v0), v2.dot(v1)
        den = d00 * d11 - d01 * d01 or 1e-12
        v = (d11 * d20 - d01 * d21) / den
        w = (d00 * d21 - d01 * d20) / den
        u = 1 - v - w
        bc = np.clip([u, v, w], 0, 1)
        return bc / (bc.sum() or 1)


def _skin_matrix(skel, weights):
    m = Matrix(((0, 0, 0, 0),) * 4)
    for name, w in weights.items():
        pb = skel.pose.bones[name]
        m += (pb.matrix @ pb.bone.matrix_local.inverted()) * w
    return m


def _limb_faces(proxy, limb):
    """A copy of the proxy part keeping only its faces skinned mostly to
    limb bones (bind pose), or None."""
    names = {g.index: g.name for g in proxy.vertex_groups}
    lw = [sum(g.weight for g in v.groups if names[g.group] in limb) for v in proxy.data.vertices]
    mesh = proxy.data.copy()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    drop = [f for f in bm.faces if sum(lw[v.index] for v in f.verts) / len(f.verts) < 0.5]
    bmesh.ops.delete(bm, geom=drop, context="FACES")
    if not bm.faces:
        bm.free()
        bpy.data.meshes.remove(mesh)
        return None
    bm.to_mesh(mesh)
    bm.free()
    ob = bpy.data.objects.new(proxy.name + "_limb", mesh)
    bpy.context.scene.collection.objects.link(ob)
    ob.parent = proxy.parent
    for g in proxy.vertex_groups:
        ob.vertex_groups.new(name=g.name)
    return ob


def _search_pose(skel, proxies, pts, faces):
    """POSE_SEARCH {bone: [(x, y, z) degrees, ...]}: for each bone in turn,
    the rotation that puts the proxy vertices it moves closest to the
    generated surface (an arm inside the picture's arm)."""
    bvh = BVHTree.FromPolygons(pts, faces)
    pose = dict(globals().get("POSE", {}))
    for bone, choices in globals()["POSE_SEARCH"].items():
        moved = {bone} | {b.name for b in skel.data.bones[bone].children_recursive}
        best = None
        for rot in choices:
            pose[bone] = rot
            _pose(skel, pose)
            dg = bpy.context.evaluated_depsgraph_get()
            dist = []
            for ob in proxies.values():
                names = {g.index: g.name for g in ob.vertex_groups}
                ev = ob.evaluated_get(dg)
                m = ev.to_mesh()
                for v, v0 in zip(m.vertices, ob.data.vertices):
                    if sum(g.weight for g in v0.groups if names[g.group] in moved) > 0.5:
                        dist.append(bvh.find_nearest(v.co)[3])
                ev.to_mesh_clear()
            score = float(np.mean(dist)) if dist else 0.0
            if best is None or score < best[0]:
                best = (score, rot)
        pose[bone] = best[1]
        print("fit_ai: pose", bone, best[1], "distance %.3f" % best[0])
    _pose(skel, pose)


def _load_image(path, mirror=False):
    """RGBA floats (row 0 = bottom, sRGB as stored) and the alpha's box."""
    img = bpy.data.images.load(path, check_existing=True)
    w, h = img.size
    a = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    if mirror:
        a = a[:, ::-1]
    ys, xs = np.nonzero(a[:, :, 3] > 0.5)
    return a, (xs.min(), xs.max(), ys.min(), ys.max())


def _rot(yaw, pitch):
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    return rx @ ry


def _find_camera(allp, faces, img, box, from_back):
    """The view the picture was taken from: the yaw and pitch (orthographic)
    whose silhouette of the mesh best covers the picture's alpha. Returns
    (rotation, projected min, max, direction to the camera)."""
    res = 96
    alpha = img[:, :, 3] > 0.5
    x0, x1, y0, y1 = box
    ys = np.clip(np.linspace(y0, y1, res).astype(int), 0, img.shape[0] - 1)
    xs = np.clip(np.linspace(x0, x1, res).astype(int), 0, img.shape[1] - 1)
    target = alpha[np.ix_(ys, xs)]
    tri = np.array([f[:3] for f in faces])
    # Points over the surface: vertices and points inside each triangle.
    w = np.array([[1, 0, 0], [0, 1, 0], [0, 0, 1], [1 / 3] * 3, [0.6, 0.2, 0.2], [0.2, 0.6, 0.2], [0.2, 0.2, 0.6]])
    surf = np.einsum("kj,tjd->tkd", w, allp[tri]).reshape(-1, 3)
    best = None
    for yaw in range(-45, 46, 3):
        for pitch in range(-24, 25, 3):
            R = _rot(yaw + (180 if from_back else 0), pitch)
            q = surf @ R.T
            lo, hi = q.min(0), q.max(0)
            u = ((q[:, 0] - lo[0]) / (hi[0] - lo[0]) * (res - 1)).round().astype(int)
            v = ((q[:, 1] - lo[1]) / (hi[1] - lo[1]) * (res - 1)).round().astype(int)
            sil = np.zeros((res, res), bool)
            sil[v, u] = True
            iou = (sil & target).sum() / max(1, (sil | target).sum())
            if best is None or iou > best[0]:
                best = (iou, yaw, pitch, R, lo, hi)
    iou, yaw, pitch, R, lo, hi = best
    print("fit_ai: %s picture seen from yaw %d pitch %d (silhouette %.2f)" % (
        "back" if from_back else "front", yaw, pitch, iou))
    return R, lo, hi, tuple(R.T @ np.array([0, 0, 1.0]))


def _classify_colour(px, pts, faces, adj, part_of, mat_of, bias):
    """CLASSES: {class: {"rgb": [(r, g, b), ...], "parts": {part: material},
    "near": [parts]}}: a class may colour faces near the "near" parts
    (default: its own; a ribbon sewn on a hat is near the hat).
    Each face's colour is read where it projects in FRONT_IMAGE (the picture
    the mesh was made from; seen from +Z) or, facing away, BACK_IMAGE
    (BACK_MIRROR: flip it); faces neither sees take their neighbours'.
    Only classes with a part within CLASS_MARGIN of the face's nearest part
    compete; the nearest colour wins and the nearest of its parts takes
    the face. DECAL_IMAGE (FRONT_IMAGE's size) is laid over front-facing
    faces of DECAL_CLASSES through the "UV" map, a projection from the
    front: eyes, carved lines, emblems."""
    classes = globals()["CLASSES"]
    margin = globals().get("CLASS_MARGIN", 1.2)
    front, fbox = _load_image(globals()["FRONT_IMAGE"])
    back = None
    if globals().get("BACK_IMAGE"):
        back, bbox = _load_image(globals()["BACK_IMAGE"], globals().get("BACK_MIRROR", False))
    allp = np.array([v[:] for v in pts])
    bvh = BVHTree.FromPolygons(pts, faces)
    cams = {False: _find_camera(allp, faces, front, fbox, False)}
    if back is not None:
        cams[True] = _find_camera(allp, faces, back, bbox, True)

    def project(p, box, from_back):
        R, lo, hi, _ = cams[from_back]
        q = R @ np.array(p[:])
        return (box[0] + (q[0] - lo[0]) / (hi[0] - lo[0]) * (box[1] - box[0]),
                box[2] + (q[1] - lo[1]) / (hi[1] - lo[1]) * (box[3] - box[2]))

    def sample(img, u, v):
        h, w = img.shape[:2]
        x, y = int(round(u)), int(round(v))
        patch = img[max(0, y - 2):min(h, y + 3), max(0, x - 2):min(w, x + 3)]
        patch = patch[patch[:, :, 3] > 0.5]
        if len(patch) == 0:
            return None
        return np.median(patch[:, :3], 0) * 255

    def front_uv(p):
        u, v = project(p, fbox, False)
        return (u / front.shape[1], v / front.shape[0])

    cls_of, near_of = [], []
    for f in faces:
        c = sum((pts[k] for k in f), Vector()) / len(f)
        n = (pts[f[1]] - pts[f[0]]).cross(pts[f[2]] - pts[f[0]])
        n = n.normalized() if n.length > 0 else Vector((0, 0, 1))
        dist = {}
        for name, p in px.items():
            i, d = p.nearest(c, n)
            if i is not None:
                dist[name] = d - bias.get(name, 0.0)
        near_of.append(dist)
        rgb = None
        for from_back in cams:
            look = Vector(cams[from_back][3])  # towards the camera
            if n.dot(look) > 0.05 and bvh.ray_cast(c + look * 1e-3, look)[0] is None:
                rgb = sample(back if from_back else front, *project(c, bbox if from_back else fbox, from_back))
                break
        if rgb is None:
            cls_of.append(None)
            continue
        dmin = min(dist.values())
        best = None
        for cname, cl in classes.items():
            if not any(pn in dist and dist[pn] <= dmin + margin for pn in cl.get("near", cl["parts"])):
                continue
            e = min(float(np.linalg.norm(rgb - np.array(c3))) for c3 in cl["rgb"])
            if best is None or e < best[1]:
                best = (cname, e)
        cls_of.append(best[0] if best else None)
    # Faces no picture sees, and lone faces, follow their neighbours.
    for it in range(40):
        changed = False
        new = list(cls_of)
        for fi, nb in enumerate(adj):
            votes = {}
            for b in nb:
                if cls_of[b] is not None:
                    votes[cls_of[b]] = votes.get(cls_of[b], 0) + 1
            if not votes:
                continue
            top = max(votes, key=votes.get)
            if cls_of[fi] is None or (cls_of[fi] not in votes and it < 2):
                new[fi] = top
                changed = True
        cls_of = new
        if not changed:
            break
    # Parts meet at creases (a hat's hem, a ribbon sewn on): each region
    # bounded by edges sharper than the first angle in SHARP takes its
    # majority class; a region with no clear majority is split again at the
    # next, finer angle.
    fn = []
    for f in faces:
        n = (pts[f[1]] - pts[f[0]]).cross(pts[f[2]] - pts[f[0]])
        fn.append(n.normalized() if n.length > 0 else Vector((0, 0, 1)))
    angles = globals().get("SHARP", [35.0, 20.0, 12.0, 7.0])
    nreg = 0

    def settle(members, level):
        nonlocal nreg
        inside = set(members)
        cos = math.cos(math.radians(angles[level]))
        region = {}
        for start in members:
            if start in region:
                continue
            stack, part = [start], [start]
            region[start] = True
            while stack:
                fi = stack.pop()
                for b in adj[fi]:
                    if b in inside and b not in region and fn[fi].dot(fn[b]) > cos:
                        region[b] = True
                        stack.append(b)
                        part.append(b)
            votes = {}
            for fi in part:
                if cls_of[fi] is not None:
                    votes[cls_of[fi]] = votes.get(cls_of[fi], 0) + 1
            if not votes:
                continue
            top = max(votes, key=votes.get)
            if votes[top] >= 0.8 * sum(votes.values()) or level + 1 >= len(angles) or len(part) < 4:
                for fi in part:
                    cls_of[fi] = top
                nreg += 1
            else:
                settle(part, level + 1)

    import sys
    sys.setrecursionlimit(10000)
    settle(list(range(len(faces))), 0)
    # Specks: same-class patches under MIN_PATCH faces take the class
    # around them.
    min_patch = globals().get("MIN_PATCH", 30)
    for _ in range(3):
        seen = [False] * len(faces)
        for start in range(len(faces)):
            if seen[start]:
                continue
            c0 = cls_of[start]
            stack, patch, border = [start], [start], {}
            seen[start] = True
            while stack:
                fi = stack.pop()
                for b in adj[fi]:
                    if cls_of[b] == c0:
                        if not seen[b]:
                            seen[b] = True
                            stack.append(b)
                            patch.append(b)
                    elif cls_of[b] is not None:
                        border[cls_of[b]] = border.get(cls_of[b], 0) + 1
            if len(patch) < min_patch and border:
                top = max(border, key=border.get)
                for fi in patch:
                    cls_of[fi] = top
    print("fit_ai:", nreg, "regions")
    decal_classes = set(globals().get("DECAL_CLASSES", []))
    min_nz = globals().get("DECAL_MIN_NZ", 0.3)
    parts, mats, decal = [], [], []
    for fi, f in enumerate(faces):
        cname = cls_of[fi]
        if cname is None:
            parts.append(part_of[fi])
            mats.append(mat_of[fi])
            decal.append(False)
            continue
        cl = classes[cname]
        dist = near_of[fi]
        pn = min((p for p in cl["parts"] if p in dist), key=lambda p: dist[p])
        force = globals().get("FORCE_PART")  # (class, part, centre) -> part
        if force:
            c = sum((pts[k] for k in f), Vector()) / len(f)
            pn = force(cname, pn, c) or pn
        parts.append(pn)
        mats.append(bpy.data.materials[cl["parts"].get(pn, next(iter(cl["parts"].values())))])
        n = (pts[f[1]] - pts[f[0]]).cross(pts[f[2]] - pts[f[0]])
        decal.append(bool(globals().get("DECAL_IMAGE")) and cname in decal_classes and n.length > 0
                     and n.normalized().dot(Vector(cams[False][3])) > min_nz)
    counts = {}
    for c in cls_of:
        counts[c] = counts.get(c, 0) + 1
    print("fit_ai: classes", counts)
    return parts, mats, decal, (front_uv if globals().get("DECAL_IMAGE") else None)


def fit_ai():
    skel = bpy.data.objects["Skeleton"]
    proxies = dict(globals()["PROXY"])
    bias = globals().get("BIAS", {})
    limb = set(globals().get("LIMB_BONES", []))
    ai = _import(globals()["AI_MESH"])
    _reduce(ai, globals().get("AI_TRIS", 9000))
    me = ai.data
    co = np.array([v.co[:] for v in me.vertices])
    if globals().get("MIRROR_X", False):
        co[:, 0] *= -1

    _pose(skel, globals().get("POSE", {}))
    px = {name: _Proxy(ob) for name, ob in proxies.items()}
    allp = np.array([c[:] for p in px.values() for c in p.co])
    lo, hi = allp.min(0), allp.max(0)
    alo, ahi = co.min(0), co.max(0)
    s = (hi[1] - lo[1]) / (ahi[1] - alo[1])
    co = (co - [(alo[0] + ahi[0]) / 2, alo[1], (alo[2] + ahi[2]) / 2]) * s
    co += [(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2]
    pts = [Vector(c) for c in co]
    if globals().get("POSE_SEARCH"):
        _search_pose(skel, proxies, pts, [tuple(p.vertices) for p in me.polygons])
        px = {name: _Proxy(ob) for name, ob in proxies.items()}

    if globals().get("FIT_DEBUG"):
        # Stop here: the fitted mesh beside the posed proxy, to look at.
        ai.data.vertices.foreach_set("co", [x for v in pts for x in v])
        ai.parent = skel
        ai.matrix_parent_inverse = Matrix.Identity(4)
        ai.matrix_world = skel.matrix_world.copy()
        return proxies
    # Faces -> part and material (by face centre and normal).
    faces = [tuple(p.vertices) for p in me.polygons]
    part_of, mat_of = [], []
    for f in faces:
        c = sum((pts[k] for k in f), Vector()) / len(f)
        n = (pts[f[1]] - pts[f[0]]).cross(pts[f[2]] - pts[f[0]])
        n = n.normalized() if n.length > 0 else None
        best = None
        for name, p in px.items():
            i, d = p.nearest(c, n)
            d -= bias.get(name, 0.0)
            if i is not None and (best is None or d < best[2]):
                best = (name, i, d)
        part_of.append(best[0])
        mat_of.append((best[0], best[1]))
    # Small islands (a few faces of one part inside another) follow their
    # neighbours.
    adj = [[] for _ in faces]
    by_edge = {}
    for fi, f in enumerate(faces):
        for a, b in zip(f, f[1:] + f[:1]):
            by_edge.setdefault((min(a, b), max(a, b)), []).append(fi)
    for fl in by_edge.values():
        for a in fl:
            adj[a] += [b for b in fl if b != a]
    for _ in range(3):
        new = list(part_of)
        for fi, nb in enumerate(adj):
            votes = {}
            for b in nb:
                votes[part_of[b]] = votes.get(part_of[b], 0) + 1
            if votes and part_of[fi] not in votes:
                new[fi] = max(votes, key=votes.get)
        part_of = new
    mat_of = [px[pn].mat[i] if pn == mn else px[pn].mat[px[pn].nearest(
        sum((pts[k] for k in f), Vector()) / len(f))[0]] for f, pn, (mn, i) in zip(faces, part_of, mat_of)]
    mat_of = [proxies[pn].data.materials[m] for pn, m in zip(part_of, mat_of)]
    decal_of = [False] * len(faces)
    front_uv = None
    if globals().get("CLASSES"):
        part_of, mat_of, decal_of, front_uv = _classify_colour(px, pts, faces, adj, part_of, mat_of, bias)

    # Limb faces go (the proxy's limbs replace them), then whatever they
    # leave floating (bits of a hand that touched the body).
    alive = [True] * len(faces)
    if limb:
        lco, ltri = [], []
        for p in px.values():
            lw = [sum(x for g, x in w.items() if g in limb) for w in p.weights]
            for t in p.tris:
                if sum(lw[k] for k in t) / 3 > 0.5:
                    ltri.append(tuple(range(len(lco), len(lco) + 3)))
                    lco += [p.co[k] for k in t]
        limb_bvh = BVHTree.FromPolygons(lco, ltri) if ltri else None
        radius = globals().get("LIMB_RADIUS", 0.0)
        for fi, (f, pn) in enumerate(zip(faces, part_of)):
            if limb_bvh and radius > 0 and pn not in bias:
                c = sum((pts[k] for k in f), Vector()) / len(f)
                if limb_bvh.find_nearest(c)[3] < radius:
                    alive[fi] = False
                    continue
            p = px[pn]
            c = sum((pts[k] for k in f), Vector()) / len(f)
            i, _ = p.nearest(c)
            bc = p.bary(i, c)
            lw = sum(b * sum(x for g, x in p.weights[vk].items() if g in limb) for b, vk in zip(bc, p.tris[i]))
            alive[fi] = lw < 0.5
    # PROXY_PARTS: parts the proxy keeps whole (a thin collar the
    # generated surface makes ragged); the mesh's faces there go.
    whole = set(globals().get("PROXY_PARTS", []))
    drop = globals().get("DROP_FACES")  # (part, centre) -> True: drop it
    for fi, pn in enumerate(part_of):
        if pn in whole or (drop and drop(pn, sum((pts[k] for k in faces[fi]), Vector()) / len(faces[fi]))):
            alive[fi] = False
    seen = [False] * len(faces)
    min_island = globals().get("MIN_ISLAND", 0.01) * len(faces)
    for start in range(len(faces)):
        if seen[start] or not alive[start]:
            continue
        comp, stack = [], [start]
        seen[start] = True
        while stack:
            fi = stack.pop()
            comp.append(fi)
            for b in adj[fi]:
                if not seen[b] and alive[b]:
                    seen[b] = True
                    stack.append(b)
        if len(comp) < min_island:
            for fi in comp:
                alive[fi] = False
    faces = [f for f, a in zip(faces, alive) if a]
    part_of = [x for x, a in zip(part_of, alive) if a]
    mat_of = [x for x, a in zip(mat_of, alive) if a]
    decal_of = [x for x, a in zip(decal_of, alive) if a]
    decals = {}

    # One set of weights per vertex, whichever parts its faces went to (a
    # vertex on a part's border is in both: different weights would tear
    # the seam open), smoothed over the surface (SMOOTH_WEIGHTS passes).
    vparts = {}
    for f, pn in zip(faces, part_of):
        for k in f:
            vparts.setdefault(k, {})
            vparts[k][pn] = vparts[k].get(pn, 0) + 1
    vweights = {}
    for k, prt in vparts.items():
        w = {}
        tot = sum(prt.values())
        for pn, cnt in prt.items():
            p = px[pn]
            i, _ = p.nearest(pts[k])
            bc = p.bary(i, pts[k])
            for b, vk in zip(bc, p.tris[i]):
                for g, x in p.weights[vk].items():
                    if g not in limb:  # the mesh's limbs are the proxy's
                        w[g] = w.get(g, 0) + b * x * cnt / tot
        if not w:
            w = {max(p.weights[p.tris[i][0]], key=p.weights[p.tris[i][0]].get): 1.0}
        vweights[k] = w
    # PART_BLEND {part: {bone: share}}: that part follows a bone partly (a
    # collar that the base's head bends into follows the head).
    for k, prt in vparts.items():
        for pn, cnt in prt.items():
            for g, a in globals().get("PART_BLEND", {}).get(pn, {}).items():
                a *= cnt / sum(prt.values())
                w = {b: x * (1 - a) for b, x in vweights[k].items()}
                w[g] = w.get(g, 0) + a
                vweights[k] = w
    # REGION_BLEND [(parts, test(point), bone, share)]: where test holds
    # (fitted, posed coordinates), those parts' vertices follow the bone;
    # test may return a number (0-1) for a ramp.
    for parts_, test, g, a in globals().get("REGION_BLEND", []):
        for k, prt in vparts.items():
            t = test(pts[k]) if any(pn in parts_ for pn in prt) else 0
            if t:
                t = a * float(t)  # True, or a share (a ramp)
                w = {b: x * (1 - t) for b, x in vweights[k].items()}
                w[g] = w.get(g, 0) + t
                vweights[k] = w
    vadj = {}
    for f in faces:
        for a in f:
            vadj.setdefault(a, set()).update(b for b in f if b != a)
    for _ in range(globals().get("SMOOTH_WEIGHTS", 3)):
        new = {}
        for k, w in vweights.items():
            acc = dict((g, x * 0.5) for g, x in w.items())
            nb = vadj.get(k, ())
            for b in nb:
                for g, x in vweights[b].items():
                    acc[g] = acc.get(g, 0) + x * 0.5 / len(nb)
            new[k] = acc
        vweights = new
    # Few distinct envelopes (the game draws at most 10 per mesh piece, and
    # thousands of slightly different ones break skinning): WEIGHT_STEP
    # steps, at most 3 bones.
    step = globals().get("WEIGHT_STEP", 0.1)
    for k, w in vweights.items():
        w = dict(sorted(w.items(), key=lambda t: -t[1])[:3])
        tot = sum(w.values()) or 1
        q = {g: round(x / tot / step) * step for g, x in w.items()}
        q = {g: x for g, x in q.items() if x > 0} or {max(w, key=w.get): 1.0}
        tot = sum(q.values())
        vweights[k] = {g: x / tot for g, x in q.items()}

    out = {}
    for name, proxy in proxies.items():
        p = px[name]
        fs = []
        for f, pn, m, dc in zip(faces, part_of, mat_of, decal_of):
            if pn != name:
                continue
            if dc:
                if m.name not in decals:
                    decals[m.name] = paint_with_overlay(m, globals()["DECAL_IMAGE"])
                m = decals[m.name]
            fs.append((f, m))
        if name in whole:
            print("fit_ai:", name, "is the proxy's")
            out[name] = proxy
            continue
        if not fs:
            # Nothing of the mesh is this part: the proxy's limbs stay (the
            # mesh has none), anything else would only poke through it.
            keep = _limb_faces(proxy, limb) if limb else None
            if keep is not None:
                bpy.data.objects.remove(keep)
                print("fit_ai: no faces for", name, "(kept the proxy's limbs)")
                out[name] = proxy
            else:
                print("fit_ai: no faces for", name, "(dropped)")
                bpy.data.objects.remove(proxy)
            continue
        used = sorted({k for f, _ in fs for k in f})
        idx = {k: i for i, k in enumerate(used)}
        # Per vertex: the shared weights; UV and grain from the nearest
        # point of this part's proxy.
        weights, uvs, grains = [], [], []
        for k in used:
            i, _ = p.nearest(pts[k])
            bc = p.bary(i, pts[k])
            weights.append(vweights[k])
            if front_uv is not None:
                uvs.append(front_uv(pts[k]))
            else:
                uvs.append(tuple(sum(b * np.array(p.uv[lk]) for b, lk in zip(bc, p.loops[i]))) if p.uv else None)
            grains.append(p.grain[p.tris[i][int(np.argmax(bc))]] if p.grain else None)
        if limb:
            # Faces skinned mostly to limb bones come from the proxy.
            lw = [sum(x for g, x in w.items() if g in limb) for w in weights]
            fs = [(f, m) for f, m in fs if sum(lw[idx[k]] for k in f) / len(f) < 0.5]
            keep = sorted({k for f, _ in fs for k in f})
            weights = [weights[idx[k]] for k in keep]
            uvs = [uvs[idx[k]] for k in keep]
            grains = [grains[idx[k]] for k in keep]
            used, idx = keep, {k: i for i, k in enumerate(keep)}
        # Inverse skinning: posed -> bind pose.
        bind = []
        for k, w in zip(used, weights):
            m = _skin_matrix(skel, w)
            bind.append((m.inverted_safe() @ pts[k])[:])

        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(bind, [], [tuple(idx[k] for k in f) for f, _ in fs])
        mats = []
        for _, m in fs:
            if m not in mats:
                mats.append(m)
        for mat in mats:
            mesh.materials.append(mat)
        for poly, (_, m) in zip(mesh.polygons, fs):
            poly.material_index = mats.index(m)
            poly.use_smooth = True
        if p.uv or front_uv is not None:
            uvl = mesh.uv_layers.new(name="UV")
            for loop in mesh.loops:
                uvl.data[loop.index].uv = uvs[loop.vertex_index]
        if p.grain:
            a = mesh.attributes.new("grain_rot", "FLOAT_VECTOR", "POINT")
            a.data.foreach_set("vector", [x for g in grains for x in g])
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(mesh)
        bm.free()
        old_name = proxy.name
        limb_part = None
        if limb:
            limb_part = _limb_faces(proxy, limb)
        bpy.data.objects.remove(proxy)
        ob = bpy.data.objects.new(old_name, mesh)
        bpy.context.scene.collection.objects.link(ob)
        ob.parent = skel
        for i, w in enumerate(weights):
            for g, x in w.items():
                vg = ob.vertex_groups.get(g) or ob.vertex_groups.new(name=g)
                vg.add([i], x, "REPLACE")
        mod = ob.modifiers.new("Armature", "ARMATURE")
        mod.object = skel
        ob["hsd_part"] = True
        if limb_part is not None:
            objs = [ob, limb_part]
            with bpy.context.temp_override(active_object=ob, selected_editable_objects=objs,
                                           selected_objects=objs, object=ob):
                bpy.ops.object.join()
        out[old_name] = ob
    bpy.data.objects.remove(ai)
    _pose(skel, {})
    print("fit_ai:", {n: len(o.data.polygons) for n, o in out.items()})
    return out
