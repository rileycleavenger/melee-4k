# SPDX-License-Identifier: GPL-3.0-or-later
"""HSD archives (.dat), the format of every model and data file on the disc.

An archive is a 0x20-byte header, a big-endian data body, a relocation
table (body offsets of every pointer slot), public symbols (name -> body
offset) and extern symbols. Pointers inside the body are body offsets; the
game adds the load address to each slot listed in the relocation table
(sysdolphin/baselib/archive.c).

Reading: `Archive(bytes)` gives `u32/f32/ptr/...` accessors over the body,
`symbols`, and `is_ptr(off)` (whether a slot is relocated).

Writing: `Writer` lays out structures, records pointer slots, and emits a
complete archive with `finish({symbol: offset})`.

The struct walkers (`read_joint_tree`) follow the layouts in
src/sysdolphin/baselib/{jobj,dobj,pobj,mobj,tobj}.h.
"""
import struct

HEADER = 0x20


class Archive:
    def __init__(self, data):
        (self.file_size, data_size, nb_reloc, nb_public,
         nb_extern) = struct.unpack(">IIIII", data[:20])
        if self.file_size != len(data):
            raise ValueError(f"archive size {self.file_size} != file size {len(data)}")
        self.body = data[HEADER:HEADER + data_size]
        rel = HEADER + data_size
        self.reloc = set(struct.unpack(f">{nb_reloc}I", data[rel:rel + nb_reloc * 4]))
        root = rel + nb_reloc * 4
        strings = root + (nb_public + nb_extern) * 8
        self.symbols = {}
        self.externs = {}
        for i in range(nb_public + nb_extern):
            off, name = struct.unpack(">II", data[root + i * 8:root + i * 8 + 8])
            end = data.find(b"\0", strings + name)
            sym = data[strings + name:end].decode("latin-1")
            (self.symbols if i < nb_public else self.externs)[sym] = off

    def u8(self, o):
        return self.body[o]

    def u16(self, o):
        return struct.unpack(">H", self.body[o:o + 2])[0]

    def u32(self, o):
        return struct.unpack(">I", self.body[o:o + 4])[0]

    def f32(self, o):
        return struct.unpack(">f", self.body[o:o + 4])[0]

    def ptr(self, o):
        """The body offset a pointer slot holds, or None when the slot is
        not relocated (a NULL pointer)."""
        return self.u32(o) if o in self.reloc else None

    def cstr(self, o):
        return self.body[o:self.body.index(b"\0", o)].decode("latin-1")

    def bytes(self, o, n):
        return self.body[o:o + n]


class Writer:
    """Lay out an archive body. `alloc` reserves aligned space and returns
    its offset; `put_*` write into it; `ptr(slot, target)` stores a pointer
    and records the slot for relocation (target None stays NULL)."""

    def __init__(self):
        self.body = bytearray()
        self.reloc = []

    def alloc(self, size, align=4):
        while len(self.body) % align:
            self.body.append(0)
        off = len(self.body)
        self.body += bytes(size)
        return off

    def blob(self, data, align=4):
        off = self.alloc(len(data), align)
        self.body[off:off + len(data)] = data
        return off

    def put(self, off, fmt, *values):
        struct.pack_into(">" + fmt, self.body, off, *values)

    def ptr(self, slot, target):
        if target is None:
            self.put(slot, "I", 0)
            return
        self.put(slot, "I", target)
        self.reloc.append(slot)

    def cstr(self, s):
        return self.blob(s.encode("latin-1") + b"\0", 1)

    def finish(self, publics, externs=None):
        while len(self.body) % 4:
            self.body.append(0)
        externs = externs or {}
        names = list(publics) + list(externs)
        strtab = bytearray()
        name_off = {}
        for n in names:
            name_off[n] = len(strtab)
            strtab += n.encode("latin-1") + b"\0"
        reloc = sorted(set(self.reloc))
        out = bytearray(HEADER)
        out += self.body
        out += struct.pack(f">{len(reloc)}I", *reloc)
        for n in publics:
            out += struct.pack(">II", publics[n], name_off[n])
        for n in externs:
            out += struct.pack(">II", externs[n], name_off[n])
        out += strtab
        struct.pack_into(">IIIII", out, 0, len(out), len(self.body), len(reloc), len(publics),
                         len(externs))
        return bytes(out)


# --------------------------------------------------------------- walkers ----

def read_list(a, off, next_at):
    out = []
    while off is not None:
        out.append(off)
        off = a.ptr(off + next_at)
    return out


def read_vtx_descs(a, off):
    """HSD_VtxDescList entries until GX_VA_NULL (attr 0xFF)."""
    out = []
    while True:
        attr, attr_type, comp_cnt, comp_type = struct.unpack(">IIII", a.bytes(off, 16))
        frac, stride = a.u8(off + 16), a.u16(off + 18)
        if attr == 0xFF:
            break
        out.append({"attr": attr, "type": attr_type, "cnt": comp_cnt, "comp": comp_type,
                    "frac": frac, "stride": stride, "data": a.ptr(off + 20), "at": off})
        off += 0x18
    return out


def read_pobj(a, off):
    flags, n_display = a.u16(off + 12), a.u16(off + 14)
    p = {"at": off, "flags": flags, "n_display": n_display, "display": a.ptr(off + 16),
         "verts": read_vtx_descs(a, a.ptr(off + 8)) if a.ptr(off + 8) is not None else [],
         "u": a.ptr(off + 20)}
    kind = flags & 0x3000  # POBJ_SKIN 0, SHAPEANIM 0x1000, ENVELOPE 0x2000
    p["kind"] = {0: "skin", 0x1000: "shape", 0x2000: "envelope"}.get(kind, hex(kind))
    if p["kind"] == "envelope" and p["u"] is not None:
        envs = []
        o = p["u"]
        while a.ptr(o) is not None:
            lst = []
            e = a.ptr(o)
            while a.ptr(e) is not None:
                lst.append((a.ptr(e), a.f32(e + 4)))
                e += 8
            envs.append(lst)
            o += 4
        p["envelopes"] = envs
    return p


def read_tobj(a, off):
    t = {"at": off, "id": a.u32(off + 8), "src": a.u32(off + 0xC), "flags": a.u32(off + 0x40),
         "image": a.ptr(off + 0x4C), "tlut": a.ptr(off + 0x50)}
    if t["image"] is not None:
        im = t["image"]
        t["w"], t["h"], t["fmt"] = a.u16(im + 4), a.u16(im + 6), a.u32(im + 8)
    t["next"] = a.ptr(off + 0x4)
    return t


def read_mobj(a, off):
    m = {"at": off, "rendermode": a.u32(off + 4), "tex": [], "mat": a.ptr(off + 12),
         "pe": a.ptr(off + 20)}
    t = a.ptr(off + 8)
    while t is not None:
        m["tex"].append(t)
        t = a.ptr(t + 4)
    return m


def read_joint_tree(a, off, depth=0, out=None):
    """Depth-first list of joints: dicts with index, depth, flags, TRS,
    inverse bind matrix offset, and the DObj -> (MObj, [PObj]) list."""
    if out is None:
        out = []
    while off is not None:
        j = {"index": len(out), "at": off, "depth": depth, "flags": a.u32(off + 4),
             "rot": struct.unpack(">3f", a.bytes(off + 0x14, 12)),
             "scale": struct.unpack(">3f", a.bytes(off + 0x20, 12)),
             "pos": struct.unpack(">3f", a.bytes(off + 0x2C, 12)),
             "mtx": a.ptr(off + 0x38), "robj": a.ptr(off + 0x3C), "dobjs": []}
        # JOBJ_SPLINE / JOBJ_PTCL reuse the union; only plain joints own DObjs.
        if not (j["flags"] & (0x4000 | 0x20)):
            for d in read_list(a, a.ptr(off + 0x10), 4):
                mo = a.ptr(d + 8)
                pobjs = [read_pobj(a, p) for p in read_list(a, a.ptr(d + 12), 4)]
                j["dobjs"].append({"at": d, "mobj": read_mobj(a, mo) if mo is not None else None,
                                   "pobjs": pobjs})
        out.append(j)
        read_joint_tree(a, a.ptr(off + 8), depth + 1, out)
        off = a.ptr(off + 0xC)
    return out
