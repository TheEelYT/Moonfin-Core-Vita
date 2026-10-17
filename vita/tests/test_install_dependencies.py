"""Regression coverage for SDK images containing the conflicting SDL variant."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).parents[1] / "scripts/install-dependencies.py"
SPEC = importlib.util.spec_from_file_location("install_dependencies", SCRIPT)
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


class InstallDependenciesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.sdk = Path(self.temp.name)
        self.payload = b"verified package bytes"
        self.lock = {"packages": [
            {"file": name, "url": "https://example.invalid/" + name,
             "sha256": hashlib.sha256(self.payload).hexdigest()}
            for name in ["sdl2-2.32.8-1-vita.pkg.tar.xz", "mpv-0.36.0-3-vita.pkg.tar.xz"]
        ]}

    def download(self, url, target):
        Path(target).write_bytes(self.payload)

    def run_installer(self):
        with patch.object(installer.urllib.request, "urlretrieve", side_effect=self.download) as download:
            with patch.object(installer.subprocess, "run") as pacman:
                installer.install_dependencies(self.sdk, self.lock)
                return download, pacman

    def test_existing_sdl_provider_is_preserved(self):
        header = self.sdk / "arm-vita-eabi/include/SDL2/SDL.h"
        header.parent.mkdir(parents=True)
        header.write_text("headers from sdl2_vitagl")
        download, pacman = self.run_installer()
        self.assertEqual(download.call_count, 1)
        args = pacman.call_args.args[0]
        self.assertEqual(args[1:4], ["pacman", "--noconfirm", "-U"])
        self.assertEqual([Path(p).name for p in args[4:]], ["mpv-0.36.0-3-vita.pkg.tar.xz"])
        self.assertEqual(header.read_text(), "headers from sdl2_vitagl")

    def test_clean_sdk_installs_pinned_sdl_headers(self):
        download, pacman = self.run_installer()
        self.assertEqual(download.call_count, 2)
        self.assertEqual(len(pacman.call_args.args[0][4:]), 2)

    def test_bad_hash_never_installs_packages(self):
        self.lock["packages"][0]["sha256"] = "0" * 64
        with patch.object(installer.urllib.request, "urlretrieve", side_effect=self.download):
            with patch.object(installer.subprocess, "run") as pacman:
                with self.assertRaisesRegex(RuntimeError, "Package hash mismatch"):
                    installer.install_dependencies(self.sdk, self.lock)
                pacman.assert_not_called()


if __name__ == "__main__":
    unittest.main()
