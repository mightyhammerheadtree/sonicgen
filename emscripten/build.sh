#!/bin/bash
# build.sh -- WASM/browser build of the Sonic Generations recomp port.
#
# Pipeline:
#   1. Emscripten toolchain (emsdk latest)
#   2. rexglue-sdk v0.10.0 + submodules + emscripten patch (this dir)
#   3. Stage generated recompiled code + apply the wasm overload rewrite
#   4. CMake configure (SDK as subproject, WebGPU backend, pthreads+JSPI)
#   5. Build -> sonicgenerations.{js,wasm} into web/
#
# Usage:  bash emscripten/build.sh [--jobs N]
# Output: build/web/  ->  copy sonicgenerations.js + sonicgenerations.wasm
#         next to web/index.html and serve with web/serve.py.
set -euo pipefail

PRJ="$(cd "$(dirname "$0")/.." && pwd)"
GAME="${GAME_DIR:-$PRJ/../Sonic-Generations-recomp-android-and-pc-edition}"
SDK_ROOT="${REXGLUE_SDK_DIR:-$PRJ/../rexglue-sdk}"
SDK_VER="v0.10.0"
EMSDK_DIR="${EMSDK_DIR:-$PRJ/../emsdk}"
BUILD_DIR="$PRJ/build/web"
JOBS="$(nproc)"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --jobs) JOBS="$2"; shift 2 ;;
    *) shift ;;
  esac
done
STAMP="$SDK_ROOT/.emscripten-patched"

echo "=== [1/5] Emscripten toolchain ==="
if [ ! -x "$EMSDK_DIR/emsdk" ]; then
  git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
fi
export EMSDK_QUIET=1
# A fresh emsdk clone has no toolchain installed: emsdk_env.sh only puts emcc
# on PATH once install+activate have run, so bootstrap first when needed.
if [ ! -x "$EMSDK_DIR/upstream/emscripten/emcc" ]; then
  echo "[build] emsdk: installing + activating latest toolchain (first run only)"
  "$EMSDK_DIR/emsdk" install latest
  "$EMSDK_DIR/emsdk" activate latest
fi
# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh"
if ! command -v emcc >/dev/null 2>&1; then
  echo "ERROR: emcc is not on PATH after sourcing $EMSDK_DIR/emsdk_env.sh" >&2
  echo "       emsdk state: $(ls "$EMSDK_DIR" 2>/dev/null | tr '\n' ' ')" >&2
  exit 1
fi
emcc --version | head -1

echo "=== [2/5] rexglue-sdk $SDK_VER + emscripten patch ==="
if [ ! -d "$SDK_ROOT/src" ]; then
  git clone --depth 1 --branch "$SDK_VER" https://github.com/rexglue/rexglue-sdk.git "$SDK_ROOT"
fi
# Submodules required to compile core + generated code (GPU/video giants are
# pulled only when their backends are enabled; WebGPU needs none of them).
for m in fmt spdlog tomlplusplus simde xxHash utfcpp stb imgui o1heap \
         disruptorplus inja cli11 aes_128 tiny-aes-c crypto pe disasm \
         vulkan-headers spirv-headers glslang renderdoc catch2; do
  git -C "$SDK_ROOT" submodule update --init --depth 1 "thirdparty/$m"
done
if [ ! -f "$STAMP" ]; then
  echo "[build] applying emscripten patch"
  git -C "$SDK_ROOT" apply --whitespace=nowarn \
      "$PRJ/emscripten/rexglue-sdk-v0.10.0-emscripten.patch"
  touch "$STAMP"
fi

echo "=== [3/5] Stage generated code + wasm overload rewrite ==="
GEN_SRC="$GAME/port/generated/default"
GEN_DST="$BUILD_DIR/generated/default"
if [ ! -f "$GAME/port/src/main.cpp" ] || [ ! -d "$GEN_SRC" ]; then
  echo "ERROR: generated game code not found at $GAME" >&2
  echo "       Expected the Sonic-Generations-recomp checkout with:" >&2
  echo "         $GEN_SRC/*.cpp" >&2
  echo "         $GAME/port/src/main.cpp" >&2
  echo "       Point GAME_DIR at your checkout, e.g.:" >&2
  echo "         GAME_DIR=\$GITHUB_WORKSPACE/game bash harness/emscripten/build.sh" >&2
  exit 1
fi
mkdir -p "$GEN_DST" "$BUILD_DIR/app"
cp "$GEN_SRC"/*.cpp "$GEN_SRC"/*.h "$GEN_DST/"
cp "$GAME/port/src/main.cpp" "$GAME/port/src/sonicgenerations_app.h" "$BUILD_DIR/app/"
cp "$GAME/port/"sonicgenerations_*.toml "$BUILD_DIR/" 2>/dev/null || true
# On wasm+SIMD128 simde maps __m128/__m128i to the same v128_t; the
# movemask_epi8 call sites emitted by the vector builder are rewritten to the
# explicit setFromMaskInt variant added by the patch (see context.h).
grep -rl 'setFromMask(simde_mm_load_si128((simde__m128i\*)' "$GEN_DST" | while read -r f; do
  sed -i 's/setFromMask(simde_mm_load_si128((simde__m128i\*)/setFromMaskInt(simde_mm_load_si128((simde__m128i*)/g' "$f"
done
echo "[build] setFromMaskInt rewrite applied to $(grep -rl setFromMaskInt "$GEN_DST" | wc -l) files"

echo "=== [4/5] CMake configure ==="
cmake -B "$BUILD_DIR" -S "$PRJ/emscripten" \
  -DCMAKE_TOOLCHAIN_FILE="$EMSDK_DIR/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DREXGLUE_SDK_DIR="$SDK_ROOT" \
  -DREXGLUE_APP_SRC_DIR="$BUILD_DIR" \
  -DREXGLUE_USE_WEBGPU=ON \
  -DREXGLUE_ENABLE_TRACY=OFF \
  -DREXGLUE_ENABLE_PERF_COUNTERS=OFF \
  -DREXGLUE_BUILD_TESTS=OFF \
  -DREX_WASM_DAWN_PORT=ON

echo "=== [5/5] Build ==="
cmake --build "$BUILD_DIR" -j "$JOBS"
echo "[build] done -> $BUILD_DIR/sonicgenerations.{js,wasm}"
