"""Shared helpers for Dolphin-format texture packs.

A replacement is named after the texture it replaces:

    tex1_{w}x{h}[_m]_{texhash}[_{tluthash}|_$]_{fmt}[_mip{n}|_arb].{png,dds}

`w`x`h` and `fmt` describe the GameCube texture, the hashes are XXH64 of its
texels (and palette). The image inside may be any multiple of `w`x`h`. The
identity used for matching is everything except the mip suffix and extension,
which is what aurora's loader keys on as well (extern/aurora/lib/texture).
"""
import os
import re
import struct
from dataclasses import dataclass

NAME_RE = re.compile(
    r"^tex1_(?P<w>\d+)x(?P<h>\d+)(?P<m>_m)?_(?P<tex>[0-9a-f]{16}|\$)"
    r"(?:_(?P<tlut>[0-9a-f]{16}|\$))?_(?P<fmt>\d+)"
    r"(?:_(?P<mip>mip\d+|arb))?\.(?P<ext>png|dds)$",
    re.IGNORECASE,
)

# Pre-2015 Dolphin names (GALE01_<crc32>_<fmt>.png). The hash is a different
# function over different bytes, so these cannot be renamed to tex1_ without a
# dump that records both names for the same texture.
LEGACY_RE = re.compile(r"^GALE01_[0-9a-f]{8}_\d+\.(png|dds)$", re.IGNORECASE)

FORMAT_NAMES = {
    0: "I4", 1: "I8", 2: "IA4", 3: "IA8", 4: "RGB565", 5: "RGB5A3",
    6: "RGBA8", 8: "C4", 9: "C8", 10: "C14X2", 14: "CMPR",
}


@dataclass(frozen=True)
class TexName:
    width: int
    height: int
    has_mips: bool
    tex_hash: str
    tlut_hash: str | None
    fmt: int
    mip: str | None
    ext: str

    @property
    def key(self) -> str:
        """Identity of the replaced texture (no mip level, no extension)."""
        parts = [f"tex1_{self.width}x{self.height}"]
        if self.has_mips:
            parts.append("m")
        parts.append(self.tex_hash)
        if self.tlut_hash is not None:
            parts.append(self.tlut_hash)
        parts.append(str(self.fmt))
        return "_".join(parts)

    @property
    def is_wildcard(self) -> bool:
        return "$" in (self.tex_hash, self.tlut_hash)


def parse_name(filename: str) -> TexName | None:
    m = NAME_RE.match(filename)
    if not m:
        return None
    return TexName(
        width=int(m["w"]),
        height=int(m["h"]),
        has_mips=m["m"] is not None,
        tex_hash=m["tex"].lower(),
        tlut_hash=m["tlut"].lower() if m["tlut"] else None,
        fmt=int(m["fmt"]),
        mip=m["mip"].lower() if m["mip"] else None,
        ext=m["ext"].lower(),
    )


def image_size(path: str) -> tuple[int, int] | None:
    """Pixel size from the PNG IHDR or DDS header, without decoding."""
    with open(path, "rb") as f:
        head = f.read(32)
    if head[:8] == b"\x89PNG\r\n\x1a\n" and head[12:16] == b"IHDR":
        w, h = struct.unpack(">II", head[16:24])
        return w, h
    if head[:4] == b"DDS " and len(head) >= 20:
        h, w = struct.unpack("<II", head[12:20])
        return w, h
    return None


def dds_fourcc(path: str) -> str | None:
    """Compression of a DDS file: its FourCC, or the DX10 DXGI format number."""
    with open(path, "rb") as f:
        head = f.read(132)
    if len(head) < 132 or head[:4] != b"DDS ":
        return None
    fourcc = head[84:88]
    if fourcc == b"DX10":
        with open(path, "rb") as f:
            f.seek(128)
            (dxgi,) = struct.unpack("<I", f.read(4))
        return f"DXGI{dxgi}"
    return fourcc.decode("latin-1").strip("\0") or "uncompressed"


def walk_pack(root: str):
    """Yield (path, TexName or None, legacy: bool) for every image in a pack."""
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if not d.startswith((".", "__MACOSX"))]
        for fn in filenames:
            if fn.startswith("._"):
                continue
            if not fn.lower().endswith((".png", ".dds")):
                continue
            path = os.path.join(dirpath, fn)
            name = parse_name(fn)
            yield path, name, name is None and bool(LEGACY_RE.match(fn))
