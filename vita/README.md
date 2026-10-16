# Moonfin Vita

A native PlayStation Vita client in the Moonfin Core fork. The Vita target is C++17, Borealis and Vita mpv/GXM; it does not run Flutter or require a second proxy application on your PC. Jellyfin or Emby provides any required transcoding.

**Current version: 0.1.0 native preview. This is not yet full Moonfin feature parity.** See [PORT_STATUS.md](PORT_STATUS.md) for the implementation and validation record.

## Install and connect

1. Install `MoonfinVita.vpk` through VitaShell on a homebrew-enabled Vita.
2. Launch **Moonfin Vita**, choose **Add Jellyfin or Emby server**, and enter your server URL. Include any reverse-proxy base path, for example `https://media.example.net/jellyfin`.
3. Enter your server username and password, or use Jellyfin Quick Connect. Local addresses such as `http://192.168.1.10:8096` work on the same network.
4. Browse a library and play a movie or episode. Your server account must allow transcoding for incompatible formats and server-rendered subtitles.

The VPK uses title ID `MFVITA001`. Settings and access tokens are stored at `ux0:data/moonfin/settings.json`; passwords are not stored. Remove an account from Settings to delete its token. HTTPS certificate verification is enabled; this preview does not offer an ignore-certificate-errors toggle. Use the final URL directly if your reverse proxy redirects the API URL.

## Controls

| Control | Browsing | Playback |
| --- | --- | --- |
| D-pad / sticks | Navigate | Left/right: seek 10 seconds; up/down: volume |
| Cross | Select | Pause/resume |
| Circle | Back | Stop and return |
| Square | — | Audio, subtitle and quality menus |
| Triangle | — | Skip the active intro/credits segment, if the server provides it |
| L / R | — | Seek 60 seconds |
| Start | — | Show controls |
| Front touch | Select / scroll | Pause/resume |

Cross/Circle follow the Vita's system confirm-button preference. The initial video profile is H.264 8-bit, at most 960×544/30 fps, stereo AAC/MP3 and 4 Mbps. A 720p/6 Mbps preset is available for testing. HEVC, AV1, HDR, multichannel audio, unsupported containers and selected subtitles are handled through server conversion. Subtitles are burned in by the server, including ASS and bitmap subtitles. Track and quality changes reopen the stream at its current position.

## Build

Linux/WSL with VitaSDK, CMake, Ninja, pkg-config, Python 3 and Git:

```bash
git clone --recurse-submodules https://github.com/TheEelYT/Moonfin-Core-Vita.git
cd Moonfin-Core-Vita
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
python3 vita/scripts/install-dependencies.py
bash build-vita.sh
python3 vita/scripts/check-vpk.py build-vita/MoonfinVita.vpk
```

The dependency installer checks SHA-256 hashes from `dependencies.lock.json` and installs a complete, pinned set in one transaction. This avoids the incomplete 2026.08 package snapshot encountered during development. It changes packages in the specified VitaSDK prefix; use a dedicated SDK prefix/container if that SDK serves other projects. The native workflow uses `vitasdk/vitasdk:2026.08`. No proprietary shader compiler package is required; this build uses GXM with precompiled shaders.

The **Vita native client** GitHub Actions workflow builds both the VPK and the portable protocol tests. Its artifact includes the VPK and debug VELF.

Host verification:

```bash
cmake -S vita -B build-vita-tests -G Ninja -DMOONFIN_HOST_TESTS=ON
cmake --build build-vita-tests
ctest --test-dir build-vita-tests --output-on-failure
python3 vita/tests/http_integration.py build-vita-tests/moonfin-tests
```

## Source and licensing

The native implementation is in `vita/src`, its protocol model in `vita/include/moonfin`, and the pinned UI dependency in `vita/vendor/borealis`. Moonfin's existing Flutter platforms remain available in the rest of the fork. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LICENSE](LICENSE). The new native code is GPL-3.0-or-later; upstream code, artwork, fonts and dependencies retain their respective licenses.
