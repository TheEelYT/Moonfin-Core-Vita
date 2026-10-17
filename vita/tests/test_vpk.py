"""Prevent VitaShell installation failures caused by incompatible LiveArea PNGs."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

VITA = Path(__file__).parents[1]
SPEC = importlib.util.spec_from_file_location("check_vpk", VITA / "scripts/check-vpk.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


class LiveAreaPngTest(unittest.TestCase):
    def setUp(self):
        self.icon = (VITA / "sce_sys/icon0.png").read_bytes()

    def test_shipped_assets_have_installable_encoding(self):
        for name, dimensions in checker.LIVEAREA_IMAGES.items():
            with self.subTest(name=name):
                checker.check_livearea_png((VITA / name).read_bytes(), name, dimensions)

    def test_truecolor_and_rgba_are_rejected(self):
        for color in (2, 6):
            with self.subTest(color=color):
                header = struct.pack(">IIBBBBB", 128, 128, 8, color, 0, 0, 0)
                data = self.icon[:8] + chunk(b"IHDR", header) + self.icon[33:]
                with self.assertRaisesRegex(AssertionError, "8-bit indexed PNG"):
                    checker.check_livearea_png(data, "icon0.png", (128, 128))

    def test_palette_alpha_is_rejected(self):
        data = self.icon[:33] + chunk(b"tRNS", b"\0") + self.icon[33:]
        with self.assertRaisesRegex(AssertionError, "Unexpected LiveArea alpha"):
            checker.check_livearea_png(data, "icon0.png", (128, 128))

    def test_wrong_dimensions_are_rejected(self):
        with self.assertRaisesRegex(AssertionError, "Wrong LiveArea dimensions"):
            checker.check_livearea_png(self.icon, "bg.png", (840, 500))


if __name__ == "__main__":
    unittest.main()
