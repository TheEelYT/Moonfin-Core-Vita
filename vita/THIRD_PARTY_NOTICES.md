# Native Vita dependencies and attribution

The new native implementation is GPL-3.0-or-later (see `LICENSE`). It implements Moonfin Core's server-facing behavior for Vita; it does not include a Flutter engine or rename another complete client.

- **Moonfin Core**, Moonfin contributors: upstream project and behavior reference, unchanged app icon and Roboto assets. Source: https://github.com/Moonfin-Client/Moonfin-Core. Upstream files keep their original licenses; the repository root contains the upstream GPLv2 license.
- **Borealis**, natinusala, p-sam, XITRIX, xfangfang and contributors: Apache-2.0, pinned submodule `f347bfd419ae28d466ca7b0957ca73b3c401e957`. Source and bundled dependency notices are in `vendor/borealis` (https://github.com/dragonflylee/borealis).
- **Switchfin**, dragonflylee and xfangfang: GXM framebuffer integration in `src/player.cpp` adapted from `app/src/view/mpv_core.cpp` and its header at Switchfin commit `1cb439ac51d0a228061b95e2a40e4ce9a01cce1e`; Apache-2.0, copy in `licenses/Switchfin-Apache-2.0.txt`. Source: https://github.com/dragonflylee/switchfin. Vita mpv/FFmpeg package recipes and GXM patches: `scripts/vita` in that repository.
- **mpv 0.36.0**, mpv contributors, and its Vita/GXM patches: GPL-2.0-or-later/LGPL-2.1-or-later as identified by the source files/build. Source: https://github.com/mpv-player/mpv/tree/v0.36.0 and https://github.com/dragonflylee/switchfin/tree/dev/scripts/vita/mpv. The dependency installer uses Switchfin's `0.36.0-3` Vita package.
- **FFmpeg n6.0**, FFmpeg contributors, plus Vita hardware decoding patches: source/recipe at https://github.com/dragonflylee/switchfin/tree/dev/scripts/vita/ffmpeg and https://github.com/FFmpeg/FFmpeg/tree/n6.0. Individual source files retain their LGPL/GPL licenses.
- **VitaSDK** and its package contributors: toolchain/system support and pinned port libraries (libass, FreeType, HarfBuzz, FriBidi, SDL2 headers, libarchive, zlib, libpng, zstd and bzip2). Sources/recipes: https://github.com/vitasdk/packages and https://github.com/vitasdk/vitasdk-autobuild. Package versions and hashes: `dependencies.lock.json`.
- **curl/libcurl**: curl license, https://github.com/curl/curl/tree/curl-8_16_0. **Mbed TLS**: Apache-2.0, https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-3.6.7. Their Vita package recipes are in Switchfin's `scripts/vita`.
- **Mozilla CA bundle**, redistributed by curl: `resources/cacert.pem`, obtained from https://curl.se/ca/cacert.pem; certificate data carries the notices in that file.
- **Roboto**, Google/Christian Robertson: Apache-2.0; font and license sourced from Moonfin Core's `assets/fonts`.

The VPK embeds this notice, the new native GPL license, the upstream license, Roboto's license and the Apache license used by Borealis/Switchfin. Complete dependency source remains available from the linked repositories and pinned recipes. This is a community Vita port, with no claim of official Moonfin or Sony endorsement.
