#!/bin/bash
# Builds the browser version of the patcher with Emscripten:
#
#   ./build-web.sh <outdir>     -> <outdir>/zelda3ds-patcher.js + .wasm
#
# Needs em++ on PATH (https://emscripten.org, e.g. `source emsdk_env.sh`).
# The website (docs/) loads these two files; CI builds them when it deploys
# the site, so they are not stored in the repository.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${1:?usage: build-web.sh <outdir>}"
mkdir -p "$OUT"
em++ -std=c++14 -O2 -fexceptions -Isrc -Isrc/extract \
    src/web.cpp src/patch_core.cpp src/extract/RomExtract.cpp \
    -o "$OUT/zelda3ds-patcher.js" \
    -sMODULARIZE=1 -sEXPORT_NAME=createZeldaPatcher \
    -sENVIRONMENT=web,worker,node \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_FUNCTIONS=_malloc,_free,_zp_patch,_zp_error,_zp_file_count,_zp_cia,_zp_cia_len,_zp_3dsx,_zp_3dsx_len \
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString
echo "Built $OUT/zelda3ds-patcher.js + .wasm"
