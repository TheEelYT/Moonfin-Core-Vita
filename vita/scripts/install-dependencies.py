#!/usr/bin/env python3
"""Download hash-pinned Vita packages, then install in one dependency transaction."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.request

sdk = Path(os.environ["VITASDK"])
lock = json.loads((Path(__file__).parents[1] / "dependencies.lock.json").read_text())
with tempfile.TemporaryDirectory(prefix="moonfin-vita-deps-") as temp:
    packages = []
    for package in lock["packages"]:
        target = Path(temp) / package["file"]
        print("Fetching", package["file"], flush=True)
        urllib.request.urlretrieve(package["url"], target)
        actual = hashlib.sha256(target.read_bytes()).hexdigest()
        if actual != package["sha256"]:
            raise RuntimeError("Package hash mismatch: " + package["file"])
        packages.append(str(target))
    subprocess.run([str(sdk / "bin" / "vdpm"), "pacman", "--noconfirm", "-U", *packages], check=True)
