#!/bin/bash
# webgpu-compile-check.sh -- compile-verify the WebGPU backend sources with
# Emscripten (emdawnwebgpu port), using the same flags as the wasm build.
set -uo pipefail
export EMSDK_QUIET=1
source /home/z/my-project/work/emsdk/emsdk_env.sh

SDK=/home/z/my-project/work/rexglue-sdk
WGPU=/home/z/my-project/work/rexglue-sdk/src/graphics/webgpu
OUT=/home/z/my-project/work/webgpu-build-check
mkdir -p "$OUT"

CXXFLAGS="-O1 -std=c++23 -pthread -msimd128 -fno-rtti -w"
INC="-I$SDK/include \
 -I$SDK/thirdparty/fmt/include \
 -I$SDK/thirdparty/spdlog/include \
 -I$SDK/thirdparty/tomlplusplus/include \
 -I$SDK/thirdparty/simde \
 -I$SDK/thirdparty/xxHash \
 -I$SDK/thirdparty/utfcpp/source \
 -I$SDK/thirdparty/stb \
 -I$SDK/thirdparty/imgui \
 -I$SDK/thirdparty/o1heap/include \
 -I$SDK/thirdparty/inja/include \
 -I$SDK/thirdparty/cli11/include \
 -I$SDK/thirdparty/vulkan-headers/include \
 -I$SDK/thirdparty/glslang \
 -I$SDK/thirdparty/renderdoc \
 -I$SDK/thirdparty/spirv-headers/include \
 -I$SDK/thirdparty/disruptorplus/include \
 -I$SDK/thirdparty/aes_128 \
 -I$SDK/thirdparty/tiny-aes-c \
 -I$SDK/thirdparty/crypto \
 -I$SDK/thirdparty/pe \
 -I$SDK/thirdparty/disasm"

FILES="wgsl_translator.cpp webgpu_util.cpp webgpu_device.cpp shader.cpp deferred_queue.cpp resources.cpp command_processor.cpp webgpu_presenter.cpp graphics_system.cpp"

fail=0
for f in $FILES; do
  src="$WGPU/$f"
  [ -f "$src" ] || { echo "MISSING $f"; fail=1; continue; }
  if em++ $CXXFLAGS $INC --use-port=emdawnwebgpu -c "$src" -o "$OUT/${f%.cpp}.o" 2>"$OUT/${f%.cpp}.log"; then
    echo "OK   $f"
  else
    echo "FAIL $f  ->  $(grep -m2 'error:' "$OUT/${f%.cpp}.log" | head -2 | tr '\n' ' ')"
    fail=1
  fi
done
exit $fail
