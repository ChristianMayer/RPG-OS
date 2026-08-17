#!/usr/bin/env bash
# Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
# SPDX-License-Identifier: Apache-2.0

# Builds the universal RPG-OS engine to WebAssembly for the npm package
# @mundus-mirabilis/rpg-os. The output lands in wasm/package/dist/ and is
# shipped inside the npm package.
#
# Requires the Emscripten SDK on PATH (any recent release; the bundled clang
# must provide libc++ >= 18 so <expected> and std::ranges are available):
#
#   git clone https://github.com/emscripten-core/emsdk.git
#   cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   source emsdk_env.sh
#
# then run:  wasm/build.sh
#
# The ruleset JSON files are NOT compiled into the binary — the host reads
# them (Node fs, browser fetch) and passes them as strings to
# rpg_os_engine_load_json. The shipped package additionally carries the
# rulesets under rulesets/ as plain data for convenience.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${SCRIPT_DIR}/package/dist"

mkdir -p "${OUT_DIR}"

if ! command -v em++ >/dev/null 2>&1; then
  echo "error: em++ not found — activate the Emscripten SDK first (see header)" >&2
  exit 1
fi

# em++ (not emcc): the binding is C++ and needs the C++ standard library
# linked (emcc would leave operator new/delete undefined).
em++ "${SCRIPT_DIR}/bindings.cpp" \
  -std=c++23 -O3 \
  -I "${ROOT_DIR}/include" \
  -I "${ROOT_DIR}/include/rpg_os/third_party" \
  -fexceptions -sDISABLE_EXCEPTION_CATCHING=0 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sMODULARIZE=1 -sEXPORT_ES6=1 \
  -sENVIRONMENT=web,worker,node \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,stringToUTF8,UTF8ToString,lengthBytesUTF8,HEAPU8,HEAP32 \
  -sEXPORTED_FUNCTIONS=_malloc,_free \
  -sINCOMING_MODULE_JS_API=onRuntimeInitialized,locateFile \
  -o "${OUT_DIR}/rpg-os-universal.js"

# Ship the project's rulesets as plain JSON package data. They are read at
# runtime (Node fs / browser fetch) — never compiled into the WASM.
mkdir -p "${SCRIPT_DIR}/package/rulesets"
for ruleset in dnd5e_srd.json tde5e_core.json brp_ugc.json; do
  cp "${ROOT_DIR}/rulesets/${ruleset}" "${SCRIPT_DIR}/package/rulesets/"
done

# Ship the licence alongside the package.
cp "${ROOT_DIR}/LICENSE" "${SCRIPT_DIR}/package/LICENSE"

echo "built ${OUT_DIR}/rpg-os-universal.js (+ .wasm)"
echo "packaged rulesets + LICENSE under ${SCRIPT_DIR}/package/"
