# Codebase analysis — Sonic Generations recomp (Android/PC edition)

Measured on the actual clone of `Player124413/Sonic-Generations-recomp-android-and-pc-edition`
(+ its `rexglue-sdk` v0.10.0 dependency), November 2026.

## 1. What the repo is

A **static recompilation** of Sonic Generations (Xbox 360) produced by **rexauto**
using **ReXGlue v0.10.0** — a Xenia-derived Xbox 360 runtime that turns PowerPC
code ahead-of-time into portable C++ (approach of XenonRecomp / UnleashedRecomp,
kernel layer from Xenia). 321 MB checkout, 97% C++.

```
port/                       the recompiled game
  src/main.cpp                5 lines: REX_DEFINE_APP(sonicgenerations, SonicgenerationsApp::Create)
  src/sonicgenerations_app.h  thin rex::ReXApp subclass (hooks: OnPreSetup, OnLoadXexImage, ...)
  generated/default/          597 files, 315.6 MB, 11,745,062 lines of C++
                             295 .cpp (~1.05 MB each, median) + register.cpp 5.2 MB + init.cpp 3.1 MB
                             + 298 .h func declarations + codegen.partition.json (2.1 MB)
  sonicgenerations_manifest.toml       entrypoint (setjmp/longjmp addrs), game_root, includes
  sonicgenerations_functions.toml      ~700 function-boundary overrides (auto-healed)
  sonicgenerations_forced_landings.toml jump-table landings recovered as in-function blocks
android/                    Gradle launcher (SDL3 Java glue, patches, adrenotools)
android_sdk.sh              fetches rexglue-sdk v0.10.0 + applies 9 patch files
```

The generated code contains the **executable's code only** (plus the memory image
skeleton in `init.cpp`: `PPCImageConfig` with code/image base and ~104k
`PPCFuncMappings` entries). The game's assets are read at runtime from the
user's game files via the SDK's `filesystem` layer (STFS container + GDFX disc
image + host-path devices) — that is why no ISO ships in the repo.

## 2. The runtime dependency: rexglue-sdk v0.10.0

332 .cpp files; layer sizes (KB of source):

| Layer | Size | Notes for wasm |
| --- | --- | --- |
| graphics | 16,460 | Vulkan + D3D12 Xenos emulation; **neither exists in a browser** — WebGPU backend required (skeleton provided in patch) |
| ui | 1,736 | SDL3 + ImGui overlay; SDL3 supports Emscripten natively |
| kernel | 1,144 | xboxkrnl HLE (Xenia-derived) — portable C++, compiles to wasm |
| codegen | 784 | PPC helpers; simde-based, needs wasm fallbacks (patched) |
| system | 540 | function dispatcher, threads — compiles (validated) |
| core | 440 | memory/clock/threading/fibers — **fibers use ucontext: no ucontext in Emscripten** → `emscripten/fiber.h` backend (patched) |
| rexglue | 128 | app framework |
| audio | 100 | SDL backend + nop |
| input | 108 | SDL/xinput/mnk |
| filesystem | 132 | STFS/GDFX/host devices — mounts user files; compiles |

Third-party: SDL3 (Emscripten target supported), FFmpeg (video — 6 MB+),
glslang/spirv-tools (shader translation), fmt/spdlog/tomlplusplus/simde/xxHash/
imgui/o1heap/inja, Vulkan headers/loader, VMA, dxbc, dxc, tracy.

Threading: only 6 files create threads (vblank, GPU interrupt, timer DPCs,
audio) — maps cleanly to Emscripten `-sUSE_PTHREADS` with a pool of 4.

## 3. Build model

- PC: CMake / Android: Gradle + CMake, both compile `port/generated` + SDK
  against `sdk/rexglue-sdk` (fetched by `android_sdk.sh`).
- The game binary (default.xex) is consumed **at build time** by rexauto; the
  generated code embeds it. The ISO/data files are needed at **runtime** only
  for assets and content mounts.
- GPU plugin (`rexgpu-xenos`) is built as a **shared library loaded at runtime**
  (`rex_gpu_create`, ABI versioned); consumers never link it. Backend selection
  inside the plugin: d3d12 / vulkan (+ webgpu added by the patch).

## 4. WASM port feasibility (measured, not guessed)

Validated with Emscripten 7.0.0 (clang 20), flags `-O2 -msimd128 -pthread -std=c++23`:

| Translation unit | Result | Wall | Peak RSS | Object |
| --- | --- | --- | --- | --- |
| SDK `core/memory.cpp` (patched platform layer) | OK | 1.0 s | 129 MB | 16 KB |
| SDK `system/function_dispatcher.cpp` | OK | 2.5 s | 264 MB | 44 KB |
| **generated `recomp.41.cpp`** (~1.05 MB, game code) | **OK** | 4.3 s (-O1) / 5.1 s (-O2) | 305 MB | 433/417 KB |

Extrapolation to the full build (295 TUs, single-core serial):

- 293 × ~5.2 s ≈ 25 min + register.cpp/init.cpp (superlinear: ~1.5–3 min each) ≈ 30–35 min
- SDK + SDL3 + fmt/spdlog ≈ 20–40 min; final LTO link of a 4 GB-memory module is
  the dominant unknown: budget 1–2 h
- **Total ≈ 2–5 h on a 16-core GH runner**; 2 cores/4 GB (this sandbox) can do
  the compile phase (~18 min wall for generated code) but not comfortably the
  full SDK + link in one sitting.

## 5. Port blockers found by actually compiling (and fixed in the patch)

1. `platform.h` — no Emscripten OS case → `#error`. **Fixed**: `REX_PLATFORM_EMSCRIPTEN` (+ Linux semantics).
2. `fpscr.h` — FPSCR intrinsics are x86 (MXCSR via simde) / ARM64 (FPCR asm) only. **Fixed**: software shadow; wasm MVP has fixed round-to-nearest, so rounding-sensitive guest ops rely on explicit trunc sequences the recompiler already emits for fctiw/fctiwz.
3. `fiber.h`/`fiber_posix.cpp` — ucontext (makecontext/swapcontext) does not exist in Emscripten. **Fixed**: `fiber_wasm.cpp` on `emscripten/fiber.h` (JSPI preferred, Asyncify fallback); exact API mirrored (ConvertCurrentThread/Create/SwitchTo/Destroy).
4. `ppc/intrinsics.h` — vsl/vslo/vsro vector shifts are x86/ARM64 only. **Fixed**: portable scalar/`memcpy` fallbacks (the ARM64 body was already portable; made it the generic path).
5. `ppc/context.h` — on wasm+SIMD128 simde maps `__m128`/`__m128i` to the same `v128_t`, so the two `setFromMask` overloads collide. **Fixed**: float-semantics `setFromMask` + new `setFromMaskInt`; the build script mechanically rewrites the `movemask_epi8` call sites in generated code (the codegen builder emits exactly two distinguishable patterns).
6. `chrono.h` — `std::clock_time_conversion` missing from Emscripten libc++ (same as Apple). **Fixed**: extended the existing Apple guard.
7. `core/CMakeLists.txt` — posix/win source split has no Emscripten case. **Fixed**: EMSCRIPTEN branch (posix sources − signal-based fastmem paths + `fiber_wasm.cpp`).

Known functional gaps after the patch (documented, not hidden):

- Guest memory: wasm32 linear memory replaces the 4 GB host VA fastmem trick —
  guest accesses run through the linear mapping (this is *why* the port is
  viable: PPC 32-bit pointers == wasm32 pointers; but no SIGSEGV-based
  fastmem, so memory.cpp's exception_handler/seh paths are excluded).
- FP rounding: software shadow (see #2). Fine for round-nearest game math;
  exotic modes are per-op correct via recompiler trunc sequences.
- Audio: SDL3 audio backend compiles for emscripten; needs runtime validation.
- GPU: WebGPU backend is a **skeleton** (boots, reports "not implemented").

## 6. Performance engineering notes for the web build (Phase 4)

- `-O3 -flto -msimd128` on generated code (measured: -O2 433 KB → -O1 437 KB;
  LTO across 11.7M lines is heavy but CI-sized).
- `REX_FPS_CAP` env (game-repo patch) → vblank-pacing cap; the browser shell
  exposes it as a setting.
- Log flush throttling (game-repo Android perf patch) applies verbatim —
  synchronous per-line flushes are even worse over Emscripten's stdout bridge.
- Pthread pool 4 (vblank + GPU + timer + audio) with COOP/COEP headers.
- JSPI for fibers; Asyncify fallback costs ~2x code size.
- Memory: 1 GB initial → 4 GB max (`ALLOW_MEMORY_GROWTH`), STACK_SIZE 8 MB for
  fiber host stacks.
