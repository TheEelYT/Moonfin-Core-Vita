#!/usr/bin/env python3
"""Download hash-pinned Vita packages, then install in one dependency transaction."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.request

def install_dependencies(sdk, lock):
    # Borealis includes SDL event types in its Vita input header, but the GXM
    # target does not link SDL. Both sdl2 and sdl2_vitagl supply these headers;
    # installing one over the other fails pacman's conflict check.
    sdl_headers_exist = (sdk / "arm-vita-eabi/include/SDL2/SDL.h").is_file()
    with tempfile.TemporaryDirectory(prefix="moonfin-vita-deps-") as temp:
        packages = []
        for package in lock["packages"]:
            if package["file"].startswith("sdl2-") and sdl_headers_exist:
                print("Using installed SDL2 headers (GXM does not link SDL2)", flush=True)
                continue
            target = Path(temp) / package["file"]
            print("Fetching", package["file"], flush=True)
            urllib.request.urlretrieve(package["url"], target)
            actual = hashlib.sha256(target.read_bytes()).hexdigest()
            if actual != package["sha256"]:
                raise RuntimeError("Package hash mismatch: " + package["file"])
            packages.append(str(target))
        if packages:
            subprocess.run(
                [str(sdk / "bin" / "vdpm"), "pacman", "--noconfirm", "-U", *packages],
                check=True,
            )


if __name__ == "__main__":
    sdk = Path(os.environ["VITASDK"])
    lock = json.loads((Path(__file__).parents[1] / "dependencies.lock.json").read_text())
    install_dependencies(sdk, lock)
