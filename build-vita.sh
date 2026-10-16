#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${VITASDK:?Set VITASDK to your VitaSDK installation first}"
export PATH="$VITASDK/bin:$PATH"
git submodule update --init --recursive vita/vendor/borealis
cmake -S vita -B build-vita -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build-vita --parallel "${MOONFIN_BUILD_JOBS:-4}"
printf '\nBuilt: %s/build-vita/MoonfinVita.vpk\n' "$PWD"
