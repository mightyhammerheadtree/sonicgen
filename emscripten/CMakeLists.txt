# Browser (wasm) build of the Sonic Generations recomp port.
#
# Consumes rexglue-sdk as a subproject (core, kernel, filesystem, system,
# ui, input, audio, codegen + rexgpu-xenos plugin with the WebGPU backend)
# and adds the recompiled game code + port app shell.
#
# Configure via emscripten/build.sh (it stages the generated code and applies
# the wasm overload rewrite first).

cmake_minimum_required(VERSION 3.24)
project(sonicgenerations_wasm CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(REXGLUE_SDK_DIR "${REXGLUE_SDK_DIR}" CACHE PATH "rexglue-sdk root")
set(REXGLUE_APP_SRC_DIR "${REXGLUE_APP_SRC_DIR}" CACHE PATH "staged generated code root")

# ---- SDK subproject -------------------------------------------------------
# Tracy/perf counters off for the web build; WebGPU backend on (option set by
# build.sh; defaults resolve inside the SDK for EMSCRIPTEN).
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${REXGLUE_SDK_DIR}" rexglue-sdk-build EXCLUDE_FROM_ALL)

# ---- Recompiled game code --------------------------------------------------
set(GEN "${REXGLUE_APP_SRC_DIR}/generated/default")
file(GLOB REXGEN_SOURCES "${GEN}/*.cpp")
add_library(rexgen OBJECT ${REXGEN_SOURCES})
target_include_directories(rexgen PRIVATE
    "${GEN}"
    "${REXGLUE_SDK_DIR}/include"
)
# The generated code is hot-path bound: -O3 + LTO. SIMD128 is on project-wide
# (simde maps its SSE calls onto wasm intrinsics).
target_compile_options(rexgen PRIVATE -O3 -flto -fno-rtti -w)

# ---- App shell -------------------------------------------------------------
add_executable(sonicgenerations
    "${REXGLUE_APP_SRC_DIR}/app/main.cpp"
)
target_include_directories(sonicgenerations PRIVATE
    "${REXGLUE_APP_SRC_DIR}"
    "${REXGLUE_SDK_DIR}/include"
)
target_link_libraries(sonicgenerations PRIVATE
    rexgen
    rex::core
    rex::filesystem
    rex::ui
    rex::input
    rex::audio
    rex::system
    rex::kernel
    rex::codegen
    rexglue
)

# ---- Emscripten link flags -------------------------------------------------
# Memory: guest image + heap live in the wasm32 linear memory (matches PPC 32-bit
# pointers). 1 GB initial, up to 4 GB growth; ALLOW_MEMORY_GROWTH is required.
# Threads: kernel vblank/timer/GPU threads -> pthread pool (needs COOP/COEP;
# web/serve.py sets the headers).
# JSPI: fibers (kernel host callbacks) switch via stack switching; Asyncify is
# the fallback (-sJSPI=0 -sASYNCIFY=1) if the target browser lacks JSPI.
# Filesystem: FORCE_FILESYSTEM + IDBFS for save persistence; the shell mounts
# the user-provided game files under /game before boot.
target_link_options(sonicgenerations PRIVATE
    -O3 -flto
    -msimd128
    -sWASM=1 -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=1GB -sMAXIMUM_MEMORY=4GB
    -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=4
    -sJSPI=1
    -sSTACK_SIZE=8MB
    -sFORCE_FILESYSTEM=1 -lidbfs.js
    -sMODULARIZE=1 -sEXPORT_NAME=SonicGenerationsRecomp -sEXPORT_ES6=0
    -sENVIRONMENT=web,worker
    -sALLOW_TABLE_GROWTH=1
    -sDYNAMIC_EXECUTION=0
    -fwasm-exceptions
    -sEXIT_RUNTIME=0
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,HEAPU8,addFunction
    -sEXPORTED_FUNCTIONS=_main
    "SHELL:--use-port=emdawnwebgpu"
)
target_compile_options(sonicgenerations PRIVATE -O3 -flto -fno-rtti -w --use-port=emdawnwebgpu)

# The GPU plugin consumes WebGPU headers (Dawn port) as well.
target_compile_options(rexgpu-xenos PRIVATE --use-port=emdawnwebgpu)

# The GPU plugin stays a side module (dlopen'd at runtime like on native);
# the runtime falls back to graceful "no GPU plugin" if the load fails.
set_target_properties(rexgpu-xenos PROPERTIES
    SUFFIX ".wasm"
)
