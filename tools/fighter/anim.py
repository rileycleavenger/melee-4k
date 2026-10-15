# SPDX-License-Identifier: GPL-3.0-or-later
"""Fighter animations (figatrees) and action scripts, read and written.

A fighter's motions are rows of its data file's motion table (PlXx.dat,
ftData +0xC, 0x18 bytes each, src/melee/ft/types.h Fighter_WaitAnimData):
the animation's symbol, its offset and size in PlXxAJ.dat, the action
script (hitboxes, timers, effects) and flags. Each animation in PlXxAJ.dat
is a small archive of its own whose one symbol is a FigaTree
(src/melee/lb/lbanim.h): the length in frames, a track count per joint (in
joint order, -1 ends) and the tracks.

A track animates one value of a joint (`TYPES`: rotation x/y/z, radians;
translation x/y/z) as HSD key data (src/sysdolphin/baselib/fobj.c): packs
of keys sharing an interpolation (constant, linear, Hermite spline with
slopes), each key its value (little-endian, float or fixed point) then a
wait: the frames to the next key. A key's interpolation is the one used
from it to the next; a slope-only key (SLP) sets the outgoing slope of the
key before it and has no wait.

    tracks = read_figatree(archive_bytes)      # {joint: {type: [Key]}}, frames
    sample(keys, frame)                        # a track's value, as the game
    data = write_figatree(symbol, frames, {joint: {type: [(frame, value)]}})

Action scripts are big-endian words; the opcode is the top 6 bits of the
first (src/melee/ft/ftaction.c). `Script` writes them (`hitbox`, `wait`,
`until`, `clear`, `iasa`, ...); `read_script` decodes a base's.
"""
import math
import struct
from dataclasses import dataclass

import hsd

ROTX, ROTY, ROTZ, TRAX, TRAY, TRAZ, SCAX, SCAY, SCAZ = 1, 2, 3, 5, 6, 7, 8, 9, 10
TYPES = {"rx": ROTX, "ry": ROTY, "rz": ROTZ, "tx": TRAX, "ty": TRAY, "tz": TRAZ,
         "sx": SCAX, "sy": SCAY, "sz": SCAZ}
CON, LIN, SPL0, SPL, SLP, KEY = 1, 2, 3, 4, 5, 6
FRAC_FLOAT, FRAC_S16, FRAC_U16, FRAC_S8, FRAC_U8 = 0, 0x20, 0x40, 0x60, 0x80


@dataclass
class Key:
    op: int
    value: float
    slope: float      # incoming slope (SPL)
    out: float        # outgoing slope
    wait: int         # frames to the next key


# ------------------------------------------------------------- figatrees ----

def _num(data, pos, frac):
    if frac & 0xE0 == FRAC_FLOAT:
        return struct.unpack("<f", data[pos:pos + 4])[0], pos + 4
    denom = 1 << (frac & 0x1F)
    kind = frac & 0xE0
    if kind == FRAC_S8:
        return struct.unpack("<b", data[pos:pos + 1])[0] / denom, pos + 1
    if kind == FRAC_U8:
        return data[pos] / denom, pos + 1
    if kind == FRAC_S16:
        return struct.unpack("<h", data[pos:pos + 2])[0] / denom, pos + 2
    if kind == FRAC_U16:
        return struct.unpack("<H", data[pos:pos + 2])[0] / denom, pos + 2
    return 0.0, pos


def decode_keys(data, frac_value, frac_slope):
    keys, pos, op, left, slope = [], 0, 0, 0, 0.0
    value = 0.0
    while pos < len(data):
        if left == 0:
            d = data[pos]
            op = d & 0xF
            left = ((d >> 4) & 7) + 1
            pos += 1
            shift = 3
            while d & 0x80:
                d = data[pos]
                pos += 1
                left += (d & 0x7F) << shift
                shift += 7
        left -= 1
        if op == SLP:
            slope, pos = _num(data, pos, frac_slope)
            if keys:
                keys[-1].out = slope
            continue
        value, pos = _num(data, pos, frac_value)
        slope = 0.0
        if op == SPL:
            slope, pos = _num(data, pos, frac_slope)
        wait, shift = 0, 0
        while pos < len(data):
            d = data[pos]
            pos += 1
            wait |= (d & 0x7F) << shift
            shift += 7
            if not d & 0x80:
                break
        keys.append(Key(op, value, slope, slope, wait))
    return keys


def sample(keys, frame):
    """A track's value at `frame`, as HSD's FObj player computes it."""
    if not keys:
        return 0.0
    t = 0.0
    for k, nxt in zip(keys, keys[1:]):
        if frame < t + k.wait:
            u = frame - t
            if k.op in (CON, KEY):
                return k.value
            if k.op == LIN:
                return k.value + (nxt.value - k.value) * u / k.wait
            s = u / k.wait
            d0 = k.out if k.op in (SPL, SLP) else 0.0
            d1 = nxt.slope
            return (k.value * (2 * s ** 3 - 3 * s ** 2 + 1) + nxt.value * (-2 * s ** 3 + 3 * s ** 2)
                    + d0 * k.wait * (s ** 3 - 2 * s ** 2 + s) + d1 * k.wait * (s ** 3 - s ** 2))
        t += k.wait
    return keys[-1].value


def read_figatree(data):
    """(frames, {joint index: {track type: [Key]}}) of an animation archive."""
    a = hsd.Archive(data)
    sym = next(s for s in a.symbols if s.endswith("figatree"))
    tree = a.symbols[sym]
    frames = a.f32(tree + 8)
    nodes, tracks = a.ptr(tree + 12), a.ptr(tree + 16)
    out, j, t = {}, 0, 0
    while True:
        n = struct.unpack(">b", a.bytes(nodes + j, 1))[0]
        if n == -1:
            break
        for _ in range(n):
            o = tracks + t * 0xC
            length, _start, kind, fv, fs = struct.unpack(">HHBBB", a.bytes(o, 7))
            ad = a.ptr(o + 8)
            out.setdefault(j, {})[kind] = decode_keys(a.bytes(ad, length), fv, fs)
            t += 1
        j += 1
    return frames, out


def _wait(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _pack_head(op, count):
    n = count - 1
    b = op | ((n & 7) << 4)
    rest = n >> 3
    out = bytearray([b | (0x80 if rest else 0)])
    while rest:
        out.append((rest & 0x7F) | (0x80 if rest >> 7 else 0))
        rest >>= 7
    return bytes(out)


def encode_track(points):
    """Key data for [(frame, value)] (frames ascending, the first at 0):
    two constant keys if the value never changes (the player interpolates
    from the second key on: a lone key leaves it with none, and the value
    undefined), else Hermite keys with Catmull-Rom slopes (smooth through
    every point)."""
    if all(abs(v - points[0][1]) < 1e-6 for _, v in points):
        v = struct.pack("<f", points[0][1])
        return _pack_head(CON, 2) + v + _wait(1) + v + _wait(0)
    out = bytearray(_pack_head(SPL, len(points)))
    for i, (f, v) in enumerate(points):
        if 0 < i < len(points) - 1:
            (f0, v0), (f1, v1) = points[i - 1], points[i + 1]
            slope = (v1 - v0) / (f1 - f0)
        else:
            slope = 0.0
        wait = points[i + 1][0] - f if i + 1 < len(points) else 0
        out += struct.pack("<ff", v, slope) + _wait(int(round(wait)))
    return bytes(out)


def write_figatree(symbol, frames, joints, njoints):
    """An animation archive: `joints` {joint: {type name or id: [(frame,
    value)]}}; joints left out get no tracks (they keep the pose the
    previous animation left: give every moving joint its tracks)."""
    w = hsd.Writer()
    tree = w.alloc(0x14)
    counts, entries = [], []
    for j in range(njoints):
        tr = joints.get(j, {})
        kinds = sorted((TYPES.get(k, k), pts) for k, pts in tr.items())
        counts.append(len(kinds))
        entries += [(kind, encode_track(pts)) for kind, pts in kinds]
    nodes = w.blob(bytes((c & 0xFF) for c in counts) + b"\xff", 4)
    tracks = w.alloc(0xC * max(1, len(entries)))
    for i, (kind, data) in enumerate(entries):
        ad = w.blob(data, 4)
        w.put(tracks + i * 0xC, "HHBBBB", len(data), 0, kind, FRAC_FLOAT, FRAC_FLOAT, 0)
        w.ptr(tracks + i * 0xC + 8, ad)
    w.put(tree, "IIf", 1, 0, float(frames))
    w.ptr(tree + 12, nodes)
    w.ptr(tree + 16, tracks)
    return w.finish({symbol: tree})


# ----------------------------------------------------- the motion table ----

def motion_table(pl_dat):
    """[(index, symbol or None, aj offset, aj size, script offset or None,
    flags)] of a base fighter's PlXx.dat, and its archive."""
    a = hsd.Archive(pl_dat)
    ft = next(o for s, o in a.symbols.items() if s.startswith("ftData"))
    tbl = a.ptr(ft + 0xC)
    rows, i = [], 0
    while tbl + (i + 1) * 0x18 <= len(a.body):
        o = tbl + i * 0x18
        name, scr = a.ptr(o), a.ptr(o + 0xC)
        if name is None and scr is None:
            break
        rows.append((i, a.cstr(name) if name is not None else None, a.u32(o + 4), a.u32(o + 8), scr,
                     a.u32(o + 0x10)))
        i += 1
    return rows, a


# ------------------------------------------------------------ the poses ----

def trs(rot, scale, pos):
    """HSD joint matrix T * Rz * Ry * Rx * S as 3x4 rows."""
    cx, sx = math.cos(rot[0]), math.sin(rot[0])
    cy, sy = math.cos(rot[1]), math.sin(rot[1])
    cz, sz = math.cos(rot[2]), math.sin(rot[2])
    r = [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz],
         [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz],
         [-sy, sx * cy, cx * cy]]
    return [[r[i][j] * scale[j] for j in range(3)] + [pos[i]] for i in range(3)]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0.0) for j in range(4)]
            for i in range(3)]


def euler_of(m):
    """XYZ Euler angles (HSD's Rz*Ry*Rx) of a rotation's 3x3 rows."""
    sy = -m[2][0]
    sy = max(-1.0, min(1.0, sy))
    y = math.asin(sy)
    if abs(sy) < 0.99999:
        x = math.atan2(m[2][1], m[2][2])
        z = math.atan2(m[1][0], m[0][0])
    else:
        x = math.atan2(-m[1][2], m[1][1])
        z = 0.0
    return [x, y, z]


def pose(skeleton, local):
    """World matrices (3x4 rows) of every joint from local SRT lists
    `local[j] = (rot, scale, pos)`."""
    world = []
    for j in skeleton:
        m = trs(*local[j["index"]])
        world.append(mul(world[j["parent"]], m) if j["parent"] >= 0 else m)
    return world


# --------------------------------------------------------- action scripts ----

# Words per command, by opcode >> 2 (ftaction.c ftAction_803C0870 from 10 up;
# 0-9 are the script interpreter's own: end, timers, loops, goto, return,
# subroutine).
_SIZES = [1, 1, 1, 1, 1, 2, 1, 2, 1, 1] + [5, 5, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                          1, 1, 1, 3, 1, 1, 1, 7, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                          1, 1, 3, 3, 2, 1, 4]


def _bits(fields):
    """A big-endian word from [(width, value)], the first field highest."""
    w, used = 0, 0
    for width, v in fields:
        w = (w << width) | (int(v) & ((1 << width) - 1))
        used += width
    return w << (32 - used)


def read_script(a, off):
    """[(opcode, [words])] of a script in archive `a` at body offset `off`."""
    out = []
    while off is not None:
        w = a.u32(off)
        op = w >> 26
        n = _SIZES[op] if op < len(_SIZES) else 1
        out.append((op << 2, [a.u32(off + 4 * k) for k in range(n)]))
        if op == 0:
            break
        off += 4 * n
    return out


def parse_hitbox(w):
    """A hitbox command's (5 words) fields."""
    def s16(v):
        return v - 0x10000 if v & 0x8000 else v
    el = (w[4] >> 18) & 0x1F
    return dict(id=(w[0] >> 23) & 7, group=(w[0] >> 20) & 7, bone=(w[0] >> 11) & 0xFF,
                damage=w[0] & 0x3FF, size=(w[1] >> 16) / 256,
                offset=(s16(w[2] & 0xFFFF) / 256, s16(w[2] >> 16) / 256, s16(w[1] & 0xFFFF) / 256),
                angle=w[3] >> 23, kbg=(w[3] >> 14) & 0x1FF, wkb=(w[3] >> 5) & 0x1FF, flags=w[3] & 0x1F,
                bkb=w[4] >> 23, element=next((k for k, v in ELEMENT.items() if v == el), el),
                shield=((w[4] >> 10) & 0xFF) - (0x100 if (w[4] >> 10) & 0x80 else 0),
                sfx=((w[4] >> 7) & 7, (w[4] >> 2) & 0x1F), ground=(w[4] >> 1) & 1, air=w[4] & 1)


def hitbox_words(h):
    ox, oy, oz = (int(round(v * 256)) for v in h["offset"])
    el = ELEMENT[h["element"]] if isinstance(h["element"], str) else h["element"]
    return [
        _bits([(6, 0x2C >> 2), (3, h["id"]), (3, h.get("group", 0)), (1, 0), (8, h["bone"]), (1, 0),
               (10, h["damage"])]),
        _bits([(16, int(round(h["size"] * 256))), (16, oz)]),
        _bits([(16, oy), (16, ox)]),
        _bits([(9, h["angle"]), (9, h["kbg"]), (9, h.get("wkb", 0)), (5, h.get("flags", 3))]),
        _bits([(9, h["bkb"]), (5, el), (8, h.get("shield", 0)), (3, h["sfx"][0]), (5, h["sfx"][1]),
               (1, h.get("ground", 1)), (1, h.get("air", 1))]),
    ]


def rescript(commands, hits, drop=(0x28, 0xA0)):
    """A base's script ([(opcode, words)], read_script) with its timing and
    other commands kept, its hitboxes changed: hits[i] (a dict of fields to
    change, or None to remove) for the i-th hitbox command; commands with
    opcodes in `drop` (the base's effects and model swaps) left out."""
    out, i = Script(), 0
    for op, w in commands:
        if op == 0x2C:
            h = hits[min(i, len(hits) - 1)] if hits else {}
            i += 1
            if h is None:
                continue
            f = parse_hitbox(w)
            f.update(h)
            out.words += hitbox_words(f)
        elif op not in drop:
            out.words += w
    return out


ELEMENT = {"normal": 0, "fire": 1, "electric": 2, "slash": 3, "coin": 4, "ice": 5, "nap": 6, "sleep": 7,
           "catch": 8, "grounded": 9, "cape": 10, "inert": 11, "disable": 12, "darkness": 13, "screw": 14}


class Script:
    """An action script, command by command (ftaction.c, HSDRaw's
    command_fighter.yml for the field names)."""

    def __init__(self):
        self.words = []

    def wait(self, n):
        """Wait n frames (synchronous timer)."""
        self.words.append(_bits([(6, 0x04 >> 2), (26, n)]))
        return self

    def until(self, frame):
        """Wait until the animation's frame (asynchronous timer)."""
        self.words.append(_bits([(6, 0x08 >> 2), (26, frame)]))
        return self

    def hitbox(self, id, bone, damage, size, offset=(0, 0, 0), angle=361, kbg=100, bkb=0, wkb=0,
               element="normal", shield=0, sfx=(1, 0), group=0, ground=True, air=True):
        """Hitbox `id` (0-3) on `bone` (joint index), size and offset in
        game units, angle 361 = Sakurai angle. On joint 0 (TopN) the
        offset's x is forward and y up."""
        self.words += hitbox_words(dict(id=id, group=group, bone=bone, damage=damage, size=size,
                                        offset=offset, angle=angle, kbg=kbg, wkb=wkb, bkb=bkb,
                                        element=element, shield=shield, sfx=sfx, ground=ground, air=air))
        return self

    def remove(self, id):
        """Remove hitbox `id`."""
        self.words.append(_bits([(6, 0x3C >> 2), (26, 0)]) | (id & 3))
        return self

    def clear(self):
        """Remove every hitbox."""
        self.words.append(0x40000000)
        return self

    def iasa(self):
        """Interruptible from here."""
        self.words.append(0x5C000000)
        return self

    def raw(self, *words):
        self.words += list(words)
        return self

    def end(self):
        self.words.append(0)
        return self

    def bytes(self):
        return struct.pack(f">{len(self.words)}I", *self.words)


# ------------------------------------------------------- authoring poses ----

def euler_matrix(rot):
    return [row[:3] for row in trs(rot, (1, 1, 1), (0, 0, 0))]


def _mul3(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def turn(rot, delta_deg, space="local"):
    """Euler angles `rot` turned by `delta_deg` (XYZ degrees, applied X then
    Y then Z) about the joint's own axes ("local") or its parent's
    ("parent")."""
    d = euler_matrix([math.radians(v) for v in delta_deg])
    m = euler_matrix(rot)
    return euler_of(_mul3(m, d) if space == "local" else _mul3(d, m))


def unwrap(prev, angles):
    """`angles` moved by whole turns to lie nearest `prev` (smooth keys)."""
    out = []
    for p, a in zip(prev, angles):
        while a - p > math.pi:
            a -= 2 * math.pi
        while a - p < -math.pi:
            a += 2 * math.pi
        out.append(a)
    return out


def dump_pose(world):
    """A pose in the game's MELEE_DUMP_POSE JSON shape (blender_pose.py)."""
    return {"motion": -1, "frame": 0, "joints": [sum((list(r) for r in m), []) for m in world]}


def _norm(v):
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / n for x in v]


def _axis_angle(axis, ang):
    x, y, z = _norm(axis)
    c, s, t = math.cos(ang), math.sin(ang), 1 - math.cos(ang)
    return [[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
            [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
            [t * x * z - s * y, t * y * z + s * x, t * z * z + c]]


def _rot_of(m):
    """3x3 rotation of a 3x4 matrix, scale removed."""
    cols = [_norm([m[i][j] for i in range(3)]) for j in range(3)]
    return [[cols[j][i] for j in range(3)] for i in range(3)]


def _t(m):
    return [[m[j][i] for j in range(3)] for i in range(3)]


class Poser:
    """Poses built in the model's space (Y up, +Z front, +X the fighter's
    left) on a base fighter's skeleton, for authoring animations:

        p = Poser(skeleton, base_locals)    # base_locals[j] = [rot, scale, pos]
        p.aim(8, 9, (0, 0, 1))              # upper arm (joint 8 -> 9) forward
        p.turn(3, (0, 1, 0), 90)            # whole body a quarter turn left
        p.move(1, (0, 2, 0))                # joint 1 up 2 units
        p.locals                            # the pose, for write_figatree

    Each operation works on the pose as it is, so order matters (parents
    first)."""

    def __init__(self, skeleton, base):
        self.skel = skeleton
        self.locals = [[list(r), list(s), list(p)] for r, s, p in base]

    def world(self):
        return pose(self.skel, self.locals)

    def _set_world_rot(self, j, R, W):
        par = self.skel[j]["parent"]
        P = _rot_of(W[par]) if par >= 0 else [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        self.locals[j][0] = euler_of(_mul3(_t(P), R))

    def turn(self, j, axis, deg):
        """Turn joint j (and everything under it) by deg about a model-space
        axis through the joint."""
        W = self.world()
        self._set_world_rot(j, _mul3(_axis_angle(axis, math.radians(deg)), _rot_of(W[j])), W)
        return self

    def aim(self, j, child, direction, blend=1.0):
        """Turn joint j so the bone to `child` points along `direction`
        (model space); blend < 1 goes part of the way."""
        W = self.world()
        a = [W[child][i][3] - W[j][i][3] for i in range(3)]
        d, t = _norm(a), _norm(direction)
        axis = [d[1] * t[2] - d[2] * t[1], d[2] * t[0] - d[0] * t[2], d[0] * t[1] - d[1] * t[0]]
        dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(d, t))))
        if math.sqrt(sum(x * x for x in axis)) < 1e-6:
            if dot > 0:
                return self
            axis = [1, 0, 0] if abs(d[0]) < 0.9 else [0, 1, 0]
        ang = math.acos(dot) * blend
        self._set_world_rot(j, _mul3(_axis_angle(axis, ang), _rot_of(W[j])), W)
        return self

    def spin(self, j, axis, deg):
        """Add deg to joint j's own Euler angle `axis` (0 x, 1 y, 2 z):
        whole turns for the body joints (XRotN, YRotN), which a matrix
        cannot hold."""
        self.locals[j][0][axis] += math.radians(deg)
        return self

    def stretch(self, j, child, factor):
        """Scale joint j along its bone (towards `child`) by factor: a limb
        longer for a pose. Its children move out with it."""
        pos = self.locals[child][2]
        k = max(range(3), key=lambda i: abs(pos[i]))
        self.locals[j][1][k] *= factor
        return self

    def move(self, j, offset):
        """Move joint j by a model-space offset (its parent's axes turned
        back: the joint's own position track)."""
        W = self.world()
        par = self.skel[j]["parent"]
        P = _rot_of(W[par]) if par >= 0 else [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        local = [sum(P[k][i] * offset[k] for k in range(3)) for i in range(3)]
        sc = [math.sqrt(sum(W[par][r][c] ** 2 for r in range(3))) for c in range(3)] if par >= 0 else [1, 1, 1]
        self.locals[j][2] = [p + v / s for p, v, s in zip(self.locals[j][2], local, sc)]
        return self


def animate(keys, njoints, translate=()):
    """Tracks for write_figatree from key poses [(frame, locals)]: every
    joint's rotation (angles unwrapped between keys), and the position of
    the joints in `translate`."""
    tracks = {}
    for j in range(njoints):
        prev, rx, ry, rz = None, [], [], []
        for f, L in keys:
            r = L[j][0] if prev is None else unwrap(prev, L[j][0])
            prev = r
            rx.append((f, r[0]))
            ry.append((f, r[1]))
            rz.append((f, r[2]))
        tracks[j] = {"rx": rx, "ry": ry, "rz": rz}
        if j in translate:
            for k, name in enumerate(("tx", "ty", "tz")):
                tracks[j][name] = [(f, L[j][2][k]) for f, L in keys]
    return tracks
