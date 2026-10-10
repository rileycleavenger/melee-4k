#!/usr/bin/env python3
"""Extract every texture on a Melee disc under the name the game looks it up by.

    .venv/bin/python tools/textures/extract.py DISC OUT_DIR [--keys-only] [--compare DIR ...]

DISC is a raw .iso/.gcm or a .ciso of GALE01 rev 2. Every HSD archive on it
(.dat/.usd) is scanned for texture descriptors through its relocation table:

  HSD_ImageDesc  (image_ptr, width, height, format, ...)       0x18 bytes
  HSD_TlutDesc   (lut, format, name, n_entries)                0x10 bytes
  HSD_TObjDesc   imagedesc at +0x4C, tlutdesc at +0x50: which palette an
                 image is drawn with
  HSD_TexAnim    imagetbl/tluttbl arrays: animated textures (eyes, faces),
                 every image with every palette of the animation

A descriptor is accepted when its pointer slot is relocated and its fields
are plausible (sizes, a GX format, data inside the archive). Each texture is
named exactly as aurora keys replacements (gfx/texture_replacement.cpp):
width, height, format, XXH64 of the base level, and for palette formats
XXH64 of the palette range the texels actually reference. Names are written
as tex1_*.png (RGBA, decoded here) unless --keys-only.

--compare DIR prints how many tex1_ names in DIR (runtime dumps, packs) the
extraction found, which is the coverage check for this scanner.
"""
import argparse
import os
import struct
import sys

import numpy as np
import xxhash

# GX texture formats: (tile width, tile height, bytes per tile)
FORMATS = {
    0: (8, 8, 32),    # I4
    1: (8, 4, 32),    # I8
    2: (8, 4, 32),    # IA4
    3: (4, 4, 32),    # IA8
    4: (4, 4, 32),    # RGB565
    5: (4, 4, 32),    # RGB5A3
    6: (4, 4, 64),    # RGBA8
    8: (8, 8, 32),    # C4
    9: (8, 4, 32),    # C8
    10: (4, 4, 32),   # C14X2
    14: (8, 8, 32),   # CMPR
}
PALETTE = {8, 9, 10}


def base_size(w, h, fmt):
    tw, th, tb = FORMATS[fmt]
    return ((w + tw - 1) // tw) * ((h + th - 1) // th) * tb


# ---------------------------------------------------------------- disc ----

class Disc:
    def __init__(self, path):
        self.f = open(path, "rb")
        head = self.f.read(8)
        self.ciso = head[:4] == b"CISO"
        if self.ciso:
            self.block = struct.unpack("<I", head[4:8])[0]
            present = self.f.read(0x8000 - 8)
            self.blocks = {}
            n = 0
            for i, p in enumerate(present):
                if p:
                    self.blocks[i] = n
                    n += 1
        fst_off, fst_size = struct.unpack(">II", self.read(0x424, 8))
        self.fst = self.read(fst_off, fst_size)

    def read(self, off, size):
        if not self.ciso:
            self.f.seek(off)
            return self.f.read(size)
        out = bytearray()
        while size > 0:
            b, inner = divmod(off, self.block)
            n = min(size, self.block - inner)
            if b in self.blocks:
                self.f.seek(0x8000 + self.blocks[b] * self.block + inner)
                out += self.f.read(n)
            else:
                out += bytes(n)
            off += n
            size -= n
        return bytes(out)

    def files(self):
        """(path, offset, size) for every file in the FST."""
        count = struct.unpack(">I", self.fst[8:12])[0]
        names = count * 12
        out = []

        def name(i):
            off = struct.unpack(">I", self.fst[i * 12:i * 12 + 4])[0] & 0xFFFFFF
            end = self.fst.index(b"\0", names + off)
            return self.fst[names + off:end].decode("shift_jis", "replace")

        def walk(start, end, prefix):
            i = start
            while i < end:
                flags_name, a, b = struct.unpack(">III", self.fst[i * 12:i * 12 + 12])
                if flags_name >> 24:
                    walk(i + 1, b, prefix + name(i) + "/")
                    i = b
                else:
                    out.append((prefix + name(i), a, b))
                    i += 1

        walk(1, count, "")
        return out


# ------------------------------------------------------------- archive ----

def scan_archive(data):
    """Texture descriptors in one HSD archive: [(image, tlut or None)]."""
    if len(data) < 0x20:
        return []
    file_size, data_size, nb_reloc = struct.unpack(">III", data[:12])
    if file_size != len(data) or data_size + 0x20 > len(data):
        return []
    body = data[0x20:0x20 + data_size]
    rel_off = 0x20 + data_size
    if rel_off + nb_reloc * 4 > len(data):
        return []
    slots = {}
    for i in range(nb_reloc):
        s = struct.unpack(">I", data[rel_off + i * 4:rel_off + i * 4 + 4])[0]
        if s + 4 <= data_size:
            slots[s] = struct.unpack(">I", body[s:s + 4])[0]

    def u32(o):
        return struct.unpack(">I", body[o:o + 4])[0] if o + 4 <= data_size else None

    def image_at(o):
        """An HSD_ImageDesc at offset o, or None."""
        if o not in slots or o + 0x18 > data_size:
            return None
        ptr = slots[o]
        w, h, fmt, mip = struct.unpack(">HHII", body[o + 4:o + 16])
        if fmt not in FORMATS or not (1 <= w <= 1024 and 1 <= h <= 1024) or mip > 1:
            return None
        if ptr % 32 or ptr + base_size(w, h, fmt) > data_size:
            return None
        return (o, ptr, w, h, fmt)

    def tlut_at(o):
        if o not in slots or o + 0x10 > data_size:
            return None
        ptr = slots[o]
        fmt = u32(o + 4)
        n = struct.unpack(">H", body[o + 12:o + 14])[0]
        if fmt not in (0, 1, 2) or not (1 <= n <= 16384) or ptr + n * 2 > data_size:
            return None
        return (o, ptr, fmt, n)

    images = {}
    tluts = {}
    for target in set(slots.values()):
        im = image_at(target)
        if im:
            images[target] = im
        tl = tlut_at(target)
        if tl:
            tluts[target] = tl

    pairs = set()
    paired_images = set()
    # TObjDesc: a relocated slot at +0x4C pointing at an image.
    for slot, target in slots.items():
        if target in images and slot >= 0x4C and (slot - 0x4C) % 4 == 0:
            tobj = slot - 0x4C
            tlut = slots.get(tobj + 0x50)
            if images[target][4] in PALETTE and tlut in tluts:
                pairs.add((target, tlut))
                paired_images.add(target)
    # TexAnim: imagetbl at +0xC, tluttbl at +0x10, counts at +0x14/+0x16.
    for slot, table in slots.items():
        if slot < 0xC or (slot - 0xC) % 4:
            continue
        anim = slot - 0xC
        if anim + 0x18 > data_size:
            continue
        n_img, n_tlut = struct.unpack(">HH", body[anim + 0x14:anim + 0x18])
        if not (1 <= n_img <= 512):
            continue
        imgs = [slots.get(table + 4 * i) for i in range(n_img)]
        if not all(i in images for i in imgs):
            continue
        tl_table = slots.get(anim + 0x10)
        tls = []
        if tl_table is not None and 1 <= n_tlut <= 512:
            tls = [slots.get(tl_table + 4 * i) for i in range(n_tlut)]
            tls = [t for t in tls if t in tluts]
        for i in imgs:
            if images[i][4] in PALETTE:
                for t in tls:
                    pairs.add((i, t))
                    paired_images.add(i)

    out = []
    for target, im in images.items():
        if im[4] not in PALETTE:
            out.append((im, None))
    for i, t in pairs:
        out.append((images[i], tluts[t]))
    out += particle_textures(data, body, slots)
    unpaired = [images[i] for i in images if images[i][4] in PALETTE and i not in paired_images]
    # Palette images reached through a game-specific table (stage animations
    # such as Icicle Mountain's swap images under a TObj's palette): try them
    # against every palette in the archive. referenced_tlut rejects palettes
    # too short for the image; keys that never occur only cost disk space.
    if len(unpaired) * len(tluts) <= 4096:
        out += [(im, tl) for im in unpaired for tl in tluts.values()]
    return out, body, unpaired


def public_symbols(data):
    """{name: body offset} of an HSD archive's public (root) symbols."""
    _, data_size, nb_reloc, nb_public, nb_extern = struct.unpack(">IIIII", data[:20])
    root = 0x20 + data_size + nb_reloc * 4
    strings = root + (nb_public + nb_extern) * 8
    out = {}
    for i in range(nb_public):
        off, name = struct.unpack(">II", data[root + i * 8:root + i * 8 + 8])
        end = data.find(b"\0", strings + name)
        out[data[strings + name:end].decode("latin-1")] = off
    return out


def particle_textures(data, body, slots):
    """Particle texture banks (HSD_PSTexGroup, psstructs.h): the stage's
    "map_texg" and the tex_bank of an Ef*Data.dat "eff*DataTable". Their
    pointers are offsets from the bank itself (psInitDataBankLocate), not
    archive relocations."""
    banks = []
    try:
        for name, off in public_symbols(data).items():
            if name.endswith("texg"):
                banks.append(off)
            elif name.endswith("DataTable") and off + 4 in slots:
                banks.append(slots[off + 4])
    except (struct.error, UnicodeDecodeError):
        return []
    size = len(body)

    def u32(o):
        return struct.unpack(">I", body[o:o + 4])[0] if 0 <= o and o + 4 <= size else None

    out = []
    for bank in banks:
        n_groups = u32(bank)
        if n_groups is None or n_groups > 4096:
            continue
        for g in range(n_groups):
            rel = u32(bank + 4 + 4 * g)
            if not rel:
                continue
            tg = bank + rel
            if tg + 0x18 > size:
                continue
            num, fmt, tlutfmt, w, h = struct.unpack(">IIIII", body[tg:tg + 20])
            palnum, palflag = struct.unpack(">HH", body[tg + 20:tg + 24])
            if fmt not in FORMATS or not (1 <= w <= 1024 and 1 <= h <= 1024) or num > 4096:
                continue
            table = [u32(tg + 0x18 + 4 * i) for i in range(num)]
            imgs = []
            for rel in table:
                if not rel or bank + rel + base_size(w, h, fmt) > size:
                    continue
                imgs.append((tg, bank + rel, w, h, fmt))
            if fmt not in PALETTE:
                out += [(im, None) for im in imgs]
                continue
            n_pal = 1 if palflag & 1 else (palnum or num)
            entries = 16 if fmt == 8 else 256
            pals = []
            for i in range(n_pal):
                rel = u32(tg + 0x18 + 4 * (num + i))
                if rel and bank + rel + entries * 2 <= size:
                    pals.append((tg, bank + rel, tlutfmt, entries))
            # The particle picks the palette (palNum, poseNum or 0), so
            # every image can meet every palette of its group.
            out += [(im, tl) for im in imgs for tl in pals]
    return out


# ---------------------------------------------------------------- keys ----

def referenced_tlut(tex, fmt, lut):
    if fmt == 8:
        idx = np.concatenate([tex >> 4, tex & 0xF])
    elif fmt == 9:
        idx = tex
    else:
        idx = (tex.view(">u2") & 0x3FFF)
    lo, hi = int(idx.min()), int(idx.max())
    if 2 * (hi + 1) > len(lut):
        return None
    return xxhash.xxh64(lut[2 * lo:2 * (hi + 1)], seed=0).intdigest()


def texture_key(body, im, tl):
    _, ptr, w, h, fmt = im
    size = base_size(w, h, fmt)
    tex = body[ptr:ptr + size]
    th = xxhash.xxh64(tex, seed=0).intdigest()
    if fmt not in PALETTE:
        return f"tex1_{w}x{h}_{th:016x}_{fmt}"
    _, lptr, _, n = tl
    lut = body[lptr:lptr + n * 2]
    lh = referenced_tlut(np.frombuffer(tex, np.uint8), fmt, lut)
    if lh is None:
        return None
    return f"tex1_{w}x{h}_{th:016x}_{lh:016x}_{fmt}"


# -------------------------------------------------------------- decode ----

def _expand(v, bits):
    v = v.astype(np.uint32)
    if bits == 3:  # aurora's ExpandTo8<3>
        return ((v << 5) | (v << 2) | (v >> 1)).astype(np.uint8)
    return ((v << (8 - bits)) | (v >> (2 * bits - 8))).astype(np.uint8)


def _from565(c):
    c = c.astype(np.uint32)
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return np.stack([_expand(r, 5), _expand(g, 6), _expand(b, 5), np.full_like(r, 255, np.uint8)], -1)


def _from5a3(c):
    c = c.astype(np.uint32)
    opaque = (c & 0x8000) != 0
    r = np.where(opaque, _expand((c >> 10) & 31, 5), _expand((c >> 8) & 15, 4))
    g = np.where(opaque, _expand((c >> 5) & 31, 5), _expand((c >> 4) & 15, 4))
    b = np.where(opaque, _expand(c & 31, 5), _expand(c & 15, 4))
    a = np.where(opaque, 255, _expand((c >> 12) & 7, 3))
    return np.stack([r, g, b, a], -1).astype(np.uint8)


def _from_ia8(c):
    c = c.astype(np.uint32)
    i, a = (c & 0xFF).astype(np.uint8), (c >> 8).astype(np.uint8)
    return np.stack([i, i, i, a], -1)


def _untile(tiles, w, h, tw, th):
    """tiles: (ntiles, th, tw, 4) in tile order -> (h, w, 4)."""
    nx, ny = (w + tw - 1) // tw, (h + th - 1) // th
    img = tiles.reshape(ny, nx, th, tw, 4).transpose(0, 2, 1, 3, 4).reshape(ny * th, nx * tw, 4)
    return img[:h, :w]


def decode(body, im, tl):
    _, ptr, w, h, fmt = im
    tw, th, tb = FORMATS[fmt]
    nx, ny = (w + tw - 1) // tw, (h + th - 1) // th
    raw = np.frombuffer(body[ptr:ptr + nx * ny * tb], np.uint8)
    if fmt in (0, 8):  # 4 bpp
        v = np.stack([raw >> 4, raw & 15], -1).reshape(-1)
    elif fmt in (1, 2, 9):  # 8 bpp
        v = raw
    else:
        v = raw.view(">u2").astype(np.uint32) if fmt != 6 else None
    if fmt == 0:
        i = _expand(v, 4)
        px = np.stack([i, i, i, i], -1)
    elif fmt == 1:
        px = np.stack([v, v, v, v], -1)
    elif fmt == 2:
        i, a = _expand(v & 15, 4), _expand(v >> 4, 4)
        px = np.stack([i, i, i, a], -1)
    elif fmt == 3:
        px = _from_ia8(v)
    elif fmt == 4:
        px = _from565(v)
    elif fmt == 5:
        px = _from5a3(v)
    elif fmt == 6:
        t = raw.reshape(-1, 2, 16, 2)  # per tile: AR plane, GB plane
        a, r = t[:, 0, :, 0], t[:, 0, :, 1]
        g, b = t[:, 1, :, 0], t[:, 1, :, 1]
        px = np.stack([r, g, b, a], -1).reshape(-1, 4)
    elif fmt in (8, 9, 10):
        _, lptr, lfmt, n = tl
        lut = np.frombuffer(body[lptr:lptr + n * 2], ">u2").astype(np.uint32)
        pal = {0: _from_ia8, 1: _from565, 2: _from5a3}[lfmt](lut)
        idx = (v & 0x3FFF) if fmt == 10 else v
        idx = np.minimum(idx, len(pal) - 1)
        px = pal[idx]
    elif fmt == 14:
        return _decode_cmpr(raw, w, h)
    return _untile(px.reshape(-1, th, tw, 4), w, h, tw, th)


def _decode_cmpr(raw, w, h):
    nx, ny = (w + 7) // 8, (h + 7) // 8
    blocks = raw.reshape(ny, nx, 2, 2, 8)
    out = np.zeros((ny * 8, nx * 8, 4), np.uint8)
    for by in range(ny):
        for bx in range(nx):
            for sy in range(2):
                for sx in range(2):
                    b = blocks[by, bx, sy, sx]
                    c0, c1 = struct.unpack(">HH", b[:4].tobytes())
                    bits = struct.unpack(">I", b[4:8].tobytes())[0]
                    p = _from565(np.array([c0, c1], np.uint32)).astype(np.int32)
                    # GX blends at 3/8 and 5/8 (aurora's S3TCBlend), and its
                    # transparent fourth color keeps the midpoint's RGB.
                    if c0 > c1:
                        p2 = (5 * p[0] + 3 * p[1]) >> 3
                        p3 = (3 * p[0] + 5 * p[1]) >> 3
                    else:
                        p2 = (p[0] + p[1]) >> 1
                        p3 = p2
                    pal = np.array([p[0], p[1], p2, p3], np.uint8)
                    pal[:, 3] = [255, 255, 255, 255 if c0 > c1 else 0]
                    idx = [(bits >> (30 - 2 * k)) & 3 for k in range(16)]
                    y0, x0 = by * 8 + sy * 4, bx * 8 + sx * 4
                    out[y0:y0 + 4, x0:x0 + 4] = pal[idx].reshape(4, 4, 4)
    return out[:h, :w]


# ---------------------------------------------------------------- main ----

def check_decode(key, rgba, dds, worst):
    """Largest per-channel difference from Aurora's dump of the same key."""
    from PIL import Image
    ref = np.asarray(Image.open(dds).convert("RGBA"))
    fmt = int(key.rsplit("_", 1)[1], 16)
    if ref.shape != rgba.shape:
        diff = 999
    else:
        diff = int(np.abs(ref.astype(int) - rgba.astype(int)).max())
    d, _, n = worst.get(fmt, (-1, None, 0))
    worst[fmt] = (diff, key, n + 1) if diff > d else (d, _, n + 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc")
    ap.add_argument("out")
    ap.add_argument("--keys-only", action="store_true")
    ap.add_argument("--compare", nargs="*", default=[])
    ap.add_argument("--validate", help="runtime dump dir (MELEE_TEXTURE_DUMP): decode every "
                    "texture it holds and compare the pixels")
    args = ap.parse_args()
    validate = {}
    if args.validate:
        for n in os.listdir(args.validate):
            if n.startswith("tex1_") and n.endswith(".dds") and "_mip" not in n:
                validate[n[:-4]] = os.path.join(args.validate, n)
    worst = {}

    disc = Disc(args.disc)
    os.makedirs(args.out, exist_ok=True)
    keys = {}
    unpaired_total = 0
    files = [f for f in disc.files() if f[0].lower().endswith((".dat", ".usd"))]
    for path, off, size in files:
        data = disc.read(off, size)
        scanned = scan_archive(data)
        if not scanned:
            continue
        found, body, unpaired = scanned
        unpaired_total += len(unpaired)
        for im, tl in found:
            key = texture_key(body, im, tl)
            if key is None or key in keys:
                continue
            keys[key] = path
            if key in validate:
                check_decode(key, decode(body, im, tl), validate[key], worst)
            if not args.keys_only:
                from PIL import Image
                Image.fromarray(decode(body, im, tl), "RGBA").save(os.path.join(args.out, key + ".png"))
    with open(os.path.join(args.out, "KEYS.txt"), "w") as f:
        for k in sorted(keys):
            f.write(f"{k}\t{keys[k]}\n")
    print(f"{len(files)} archives, {len(keys)} textures, {unpaired_total} palette images "
          f"with no palette found")

    for fmt, (diff, key, n) in sorted(worst.items()):
        print(f"decode fmt {fmt}: {n} checked, max channel error {diff} ({key})")

    for d in args.compare:
        ref = set()
        for root, _, names in os.walk(d):
            for n in names:
                if n.startswith("tex1_") and n.endswith((".png", ".dds")) and "_mip" not in n:
                    ref.add(n.rsplit(".", 1)[0])
        hit = ref & set(keys)
        print(f"{d}: {len(hit)}/{len(ref)} found ({100 * len(hit) / max(1, len(ref)):.1f}%)")


if __name__ == "__main__":
    sys.exit(main())
