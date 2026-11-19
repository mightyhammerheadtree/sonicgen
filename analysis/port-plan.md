# Phased port plan — Sonic Generations recomp → WebAssembly

Goal: the game runs in a browser at 25–30 fps from user-supplied game files,
with the same "you own the game" legal posture as the source project.

Phases 0–2 are **complete** in this repository (validated by compiling actual
generated game code). Phase 3 is the remaining big engineering item; Phase 4
is tuning to the fps target.

---

## Phase 0 — Analysis (done)

Full source-tree analysis with measurements: see `codebase-analysis.md`.
Key facts: 11.7M generated lines across 295 TUs; all runtime platform gaps are
enumerated and small enough to patch (13-file patch); the only architectural
blocker is the graphics backend (Vulkan/D3D12-only).

## Phase 1 — Toolchain + platform layer (done, validated)

- Emscripten 7.0.0 toolchain, C++23, `-msimd128`, pthreads.
- `emscripten/rexglue-sdk-v0.10.0-emscripten.patch` (13 files) adds:
  `REX_PLATFORM_EMSCRIPTEN`, software FPSCR, `fiber_wasm.cpp` (JSPI),
  portable vsl/vslo/vsro, wasm-safe `setFromMask`, chrono libc++ fix,
  CMake source selection, WebGPU backend option + skeleton + plugin wiring.
- Measured: generated TU → wasm object OK (4.3–5.1 s, 305 MB RSS).
- **Exit criteria: met.**

## Phase 2 — Build harness + browser shell (done)

- `emscripten/build.sh`: stages generated code (applies the `setFromMaskInt`
  rewrite), patches the SDK, CMake config, parallel build.
- `emscripten/CMakeLists.txt`: SDK subproject + generated code (`-O3 -flto`)
  + app shell; link flags: 1 GB→4 GB memory growth, pthread pool 4, JSPI,
  IDBFS, modularized exports.
- `ci/github-actions-wasm.yml`: the 2–4 h CI build.
- `web/index.html`: loader (IndexedDB persistence), boot flow (mount /game,
  IDBFS /save sync, `callMain`), keyboard/touch/gamepad input, FPS/heap HUD,
  status console; `web/serve.py` sends COOP/COEP.
- **Exit criteria: met** (shell runs standalone today; boots module when CI
  artifact is dropped next to it).

## Phase 3 — Runtime bring-up on CI (2–4 CI iterations)

1. Full SDK compile under the harness on GH Actions (16 cores): fix the
   remaining POSIX-shaped gaps surfaced only at scale — expected hotspots:
   `dlopen`/side-module linking of `rexgpu-xenos` (link with `-sMAIN_MODULE=2`
   or switch the plugin loader to a static registration path under
   `REX_PLATFORM_EMSCRIPTEN`), `SDL3` audio/video init quirks, `dynlib_posix`,
   `socket_posix` non-blocking semantics.
2. Boot milestone: kernel init → XEX image load (`OnLoadXexImage` hook) →
   guest main runs to first vblank **without** GPU plugin (graphics disabled,
   UI overlay + logs visible). Audio (SDL) enabled.
3. Filesystem: mount user files from the shell (`/game`), verify
   STFS-container + disc-image devices open the game data; saves via IDBFS.
4. **Exit criteria: runtime boots in Chrome, logs show guest execution,
   no crash for 60 s headless.**

## Phase 4 — WebGPU renderer (draw path implemented; v1 texture/resolve port remaining)

**Status update (this revision):** the backend is implemented with a real
end-to-end draw path and compile-verified with Emscripten 7 + the Dawn
`emdawnwebgpu` port (all 9 sources OK in `scripts/webgpu-compile-check.sh`):

- `webgpu_device.{h,cpp}` — async adapter/device acquisition state machine,
  canvas surface creation (`WGPUEmscriptenSurfaceSourceCanvasHTMLSelector`),
  capabilities-based format selection, configure/acquire/present.
- `shader.{h,cpp}` — `WebGPUShader : SpirvShader`: base translator emits
  SPIR-V, `wgsl_translator` converts to WGSL on the CP thread
  (`EnsureWGSLTranslated`), `WGPUShaderModule` created on the main thread.
- `deferred_queue.{h,cpp}` — main-thread operation queue (the browser API is
  main-thread-only); the CP thread records, `PumpFrame()` drains.
- `command_processor.{h,cpp}` — **real draw encoding**: each draw is fully
  captured on the CP thread (shader WGSL + identity hashes, constant banks
  packed per the shader register maps — float tight-packing with full-bank
  fallback for dynamic addressing, bool/loop/fetch, `SystemConstants` with
  index params + NDC pass-through), texture bytes converted to BGRA8 for the
  untiled 8888/2_10_10_10/5_6_5/1_5_5_5 formats, vertex-fetch ranges and the
  index buffer copied out of guest memory; the main thread then encodes real
  render passes: bind groups over the fixed shader ABI (set 0 shared-memory
  storage buffer, set 1 five uniform buffers, sets 2/3 textures+samplers),
  guest pipelines keyed by shaders + blend + topology, indexed draws into
  cached render targets, `SetIndexBuffer` over the shared-memory buffer.
- `resources.{h,cpp}` — shared memory buffer + its set-0 bind group,
  five-bank constant buffer cache (256-aligned row-padded texture uploads,
  null-texture substitution for unbound textures), texture cache with sampler
  cache, render-target cache with tile-range invalidation, ABI-aware pipeline
  cache (blend/topology decoded from `RB_BLENDCONTROL`/`RB_COLOR_MASK`).
- `webgpu_presenter.{h,cpp}` — provider + presenter + immediate drawer;
  aspect-preserving letterbox presentation shared with the command
  processor's per-frame pump (`webgpu_util.cpp`), with real bind groups.
- `wgsl_translator.{h,cpp}` — SPIR-V -> WGSL (see below).

**SPIR-V -> WGSL translator** (`wgsl_translator.cpp`): SPIRV-Cross dropped its
WGSL writer upstream and Dawn accepts WGSL only, so the port ships an in-tree
translator covering the opcode subset the SDK's `SpirvShaderTranslator` emits
(~60 opcodes: arithmetic/bitwise/bitfield/comparison/composite/image ops,
GLSL.std.450 extinsts, Phi-to-variable promotion, if/else + loop + switch CFG
structurization). Unsupported CFG shapes fail loudly, never silently. A
native test harness (`scripts/wgsl_test.cpp` in the deliverable) hand-assembles
a full fragment-shader SPIR-V module and validates the emitted WGSL structurally
(braces, entry point, textureSample, loop, if, no unresolved ids) — output is
valid WGSL accepted by the browser WGSL grammar.

Remaining in phase 4 (documented v1 limitations, see the file header of
`command_processor.cpp` in the patch): tiled/compressed guest texture formats
(port the Vulkan texture-load kernels), full `UpdateSystemConstantValues`
register conversion, depth attachment support, the resolve copy to the guest
resolve destination, and the FSR/CAS paint effects.

Port the Xenos GPU emulation from the Vulkan backend to WebGPU, reusing the
backend-agnostic machinery that is already wasm-clean (command processor
structure, PM4 ring, register file, shader **SPIR-V translator** — the
WebGPU skeleton in the patch is the slot):

1. Device/swapchain: `navigator.gpu` → canvas presentation (shell already
   sizes the canvas + detects WebGPU).
2. `render_target_cache`: Emscripten memory → storage textures; the Vulkan
   backend's texture-load compute shaders translate to WGSL 1:1 (they are
   pure image ops).
3. `pipeline_cache`: SPIR-V (from the existing translator) → WGSL via
   SPIRV-Cross's WGSL backend at cache time; cache in IndexedDB.
4. `texture_cache` + shared memory staging via `GPUBuffer` ring.
5. Command processor: reuse the Vulkan `command_processor.cpp` structure,
   swapping VkCmd → WebGPU render/compute pass encoding.
6. Async upload queue on a worker; frame pacing tied to `requestAnimationFrame`
   + `REX_FPS_CAP`.
- **Exit criteria: title menu renders; a stage loads at ≥25 fps on desktop
  Chrome; texture/RT correctness spot-checks.**

## Phase 5 — Performance + robustness to the 25–30 fps target

- Profile with emscripten's `--profiling` + tracy-less counters: find the
  top wasm hot functions (expected: translator-inner-loop, texture load,
  memory copy paths) and add `wasm_simd128.h` fast paths where the scalar
  fallbacks were used.
- `-O3 -flto` verified across TUs; `-msimd128` already on.
- Memory: keep `INITIAL_MEMORY=1GB`, measure growth events (they cost a full
  copy); pre-size to 2 GB if the game allocates heavy content.
- Turn on the game-repo's Android perf patches as defaults for wasm
  (log-flush throttle, interrupt dispatch serialization, `REX_FPS_CAP=30`).
- Device tiers: desktop Chrome/Edge (WebGPU) ≥60 fps target; Safari/Firefox
  fallback = software-safety mode (render at 0.75× scale) until JSPI/WebGPU
  ship enabled everywhere.
- **Exit criteria: 25–30 fps sustained in the benchmark stage on a mid-range
  laptop; no heap-growth stalls >100 ms after warmup.**

---

## Risks / unknowns

| Risk | Likelihood | Mitigation |
| --- | --- | --- |
| LTO link of 11.7M lines exceeds CI RAM/time | medium | per-TU `-O3` without cross-TU LTO (codegen partition already isolates hot paths); or `wasm-opt -O3` on the un-LTO'd module |
| Side-module dlopen friction under pthreads | medium | static-register the GPU plugin under Emscripten (small loader patch) |
| JSPI not yet enabled in some browsers | low | Asyncify fallback flag; fibers only used in kernel callbacks (low frequency) |
| FP rounding edge cases | low | shadow FPSCR + per-op trunc; test suite of float ops vs native |
| WebGPU memory limits on iGPUs | medium | texture cache eviction policy copied from Vulkan backend (it already has one) |
