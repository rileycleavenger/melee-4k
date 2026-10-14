#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the custom fighter tools (tools/fighter/): the HSD archive
writer/reader round trip every .dat they write depends on, and the pack
validator's rules. Standard library only; no disc.

    python3 tools/test_fighter_tools.py
"""
import os
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "fighter"))
import hsd  # noqa: E402
import validate  # noqa: E402


class ArchiveRoundTrip(unittest.TestCase):
    def test_pointers_symbols_strings(self):
        w = hsd.Writer()
        a = w.alloc(12)
        b = w.alloc(8, 32)
        s = w.cstr("hello")
        w.ptr(a, b)          # a -> b
        w.ptr(a + 4, None)   # NULL
        w.ptr(a + 8, s)      # a string
        w.put(b, "If", 0xDEADBEEF, 1.5)
        data = w.finish({"top": a, "other": b}, {"ext_sym": 0})
        r = hsd.Archive(data)
        self.assertEqual(r.file_size, len(data))
        self.assertEqual(r.symbols, {"top": a, "other": b})
        self.assertEqual(r.externs, {"ext_sym": 0})
        self.assertEqual(b % 32, 0)
        self.assertEqual(r.ptr(a), b)
        self.assertIsNone(r.ptr(a + 4))
        self.assertEqual(r.cstr(r.ptr(a + 8)), "hello")
        self.assertEqual(r.u32(b), 0xDEADBEEF)
        self.assertEqual(r.f32(b + 4), 1.5)
        # The header the game reads (sysdolphin/baselib/archive.c).
        size, data_size, nb_reloc, nb_pub, nb_ext = struct.unpack(">IIIII", data[:20])
        self.assertEqual((size, nb_reloc, nb_pub, nb_ext), (len(data), 2, 2, 1))
        self.assertEqual(data_size % 4, 0)

    def test_size_mismatch_rejected(self):
        data = hsd.Writer().finish({})
        with self.assertRaises(ValueError):
            hsd.Archive(data + b"\0\0\0\0")


def png(path, w, h):
    """A grey RGB PNG, without PIL."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    raw = b"".join(b"\0" + b"\x80" * (w * 3) for _ in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def read_cfg(text):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "fighter.cfg")
        with open(p, "w") as f:
            f.write(text)
        r = validate.Report()
        cfg = validate.read_cfg(p, r)
        return cfg, r


class Validator(unittest.TestCase):
    def test_good_cfg(self):
        cfg, r = read_cfg('id Ge\nname "GENO"  # comment\nbase Mr\nsymbol PlyGeno5K\n'
                          'costumes Nr Re Gr\nteams Re Nr Gr\nmoves Geno\nweight 0.94\n')
        self.assertEqual(r.errors, [])
        self.assertEqual(cfg["costumes"], ["Nr", "Re", "Gr"])
        self.assertEqual(cfg["name"], "GENO")

    def test_bad_cfg(self):
        cfg, r = read_cfg("id GEN\nbase Mr\ncostumes Nr re\nmoves Nope\nweight 20\nfoo 1\n")
        text = "\n".join(r.errors)
        self.assertIn("no symbol", text)
        self.assertIn("id 'GEN'", text)
        self.assertIn("costume 're'", text)
        self.assertIn("moves 'Nope'", text)
        self.assertIn("weight 20.0", text)
        self.assertTrue(any("unknown key 'foo'" in w for w in r.warnings))

    def test_team_not_a_costume(self):
        _, r = read_cfg("id Ge\nbase Mr\nsymbol S\ncostumes Nr\nteams Re Nr Gr\n")
        self.assertTrue(any("team costume 'Re'" in w for w in r.warnings))

    def test_costume_symbol_rule(self):
        # As the disc and src/melee/ft/ftcustom.c: no suffix for Nr only.
        cfg = {"symbol": "PlyGeno5K"}
        self.assertEqual(validate.costume_symbol(cfg, "Nr"), "PlyGeno5K")
        self.assertEqual(validate.costume_symbol(cfg, "Re"), "PlyGeno5KRe")

    def test_moves_are_found_in_the_source(self):
        names = [os.path.basename(p) for p in validate.moves_sources()]
        self.assertIn("ftgeno.c", names)

    def test_texture_names(self):
        with tempfile.TemporaryDirectory() as d:
            png(os.path.join(d, "tex1_128x128_0123456789abcdef_4.png"), 256, 256)  # HD, same shape
            png(os.path.join(d, "wood.png"), 8, 8)                                 # not a texture key
            r = validate.Report()
            validate.check_textures(d, r)
            self.assertEqual(len(r.errors), 1)
            self.assertIn("wood.png", r.errors[0])


if __name__ == "__main__":
    unittest.main()
