#!/usr/bin/env bash
#
# Build the Byte interpreter as WebAssembly for the browser playground.
#
#   ./web/build.sh
#
# Requires the Emscripten SDK (https://emscripten.org):
#
#   git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
#   ~/emsdk/emsdk install latest
#   ~/emsdk/emsdk activate latest
#   source ~/emsdk/emsdk_env.sh
#
# Output: web/public/wasm/byte.mjs + byte.wasm, loaded by the Astro worker.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

EMCC="${EMCC:-emcc}"

if ! command -v "$EMCC" >/dev/null 2>&1; then
  cat >&2 <<'EOF'
error: emcc not found on PATH.

Install the Emscripten SDK and source its environment, then retry:

  git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
  ~/emsdk/emsdk install latest
  ~/emsdk/emsdk activate latest
  source ~/emsdk/emsdk_env.sh
EOF
  exit 1
fi

mkdir -p web/public/wasm

# src/*/*.c pulls in the scanner, compiler, VM, GC and helpers, and skips
# src/main.c (the native CLI/REPL entry point) which byte_web.c replaces.
# shellcheck disable=SC2206
sources=(src/*/*.c web/byte_web.c)

"$EMCC" "${sources[@]}" \
  -I src -I src/compiler -I src/core -I src/debug -I src/utils \
  -std=c11 \
  -Wall -Wextra \
  -O2 \
  -o web/public/wasm/byte.mjs \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sENVIRONMENT=web,worker \
  -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=33554432 \
  -sSTACK_SIZE=1048576 \
  -sFILESYSTEM=0 \
  -sEXIT_RUNTIME=0 \
  -sEXPORTED_FUNCTIONS=_byte_init,_byte_run,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8

echo
ls -lh web/public/wasm/byte.mjs web/public/wasm/byte.wasm
echo
echo "Built. Start the playground:"
echo "  cd web && npm install && npm run dev"
