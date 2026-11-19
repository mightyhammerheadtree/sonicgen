# Sonic Generations recomp — WebAssembly / Browser Edition

A WASM build target for [Player124413/Sonic-Generations-recomp-android-and-pc-edition](https://github.com/Player124413/Sonic-Generations-recomp-android-and-pc-edition)
(a ReXGlue static recompilation of Sonic Generations, Xbox 360), so the game runs
in the browser from user-supplied game files. **No game assets ship here** —
same legal posture as the original repo and every other recomp project.

## Status: WebGPU backend implemented; full-game runtime pending CI bring-up

What is in this repo now (see `analysis/` for the full evidence):

| Result | Evidence |
| --- | --- |
| Generated recompiled game code compiles to wasm | `recomp.41.cpp` (1 MB, ~10k instructions) → 433 KB `.o` at `-O2/-msimd128 -pthread`, ~5 s, 305 MB peak RSS |
| SDK core compiles with the emscripten patch | `memory.cpp`, `function_dispatcher.cpp` → OK |
| Full-build budget | 295 TUs ≈ 25–35 min single-core + SDK + link ≈ **2–5 h on a 16-core CI runner** |
| Platform layer | `emscripten/rexglue-sdk-v0.10.0-emscripten.patch` (13 files): platform detection, software FPSCR, JSPI fibers, portable SIMD, WebGPU backend skeleton |
| Browser shell | `web/index.html` + `web/serve.py` — file loader (IndexedDB), input (keyboard/touch/gamepad), FPS/heap HUD, IDBFS save sync, COOP/COEP server |
| **WebGPU backend (C++)** | 9 sources in the SDK patch, all compile clean with emcc + emdawnwebgpu. Device state machine + canvas surface; shader path = SpirvShaderTranslator -> SPIR-V -> WGSL -> `WGPUShaderModule`; **real draw encode path**: per-draw capture on the CP thread (WGSL translation, packed constant banks per the shader register maps, texture bytes converted to BGRA8, index/vertex fetch ranges), main-thread encoding into render passes with bind groups over the fixed shader ABI (shared-memory storage buffer, five uniform buffers, texture sets 2/3), guest pipelines keyed by shaders+blend+topology, indexed draws into cached render targets; deferred main-thread queue; presenter with aspect-preserving letterbox quad (shared with the per-frame pump) |
| **SPIR-V -> WGSL translator** | In-tree `wgsl_translator.cpp`: parses the opcode subset the SDK translator emits, structurizes CFG (if/else, loops with Phi promotion), maps GLSL.std.450; native test produces valid WGSL (`scripts/wgsl_test.cpp`) |
| **Browser demo** | `web/webgpu-demo.html` — proves surface config, `queue.writeTexture`, render pass and compute pass work in a real browser; verified to load with zero JS errors (GPU adapter requires real hardware) |

What is NOT done yet (honest list — see the v1 limitation notes in
`src/graphics/webgpu/command_processor.cpp` of the patch):

- **Renderer maturity**: the draw path is real end-to-end, but with v1
  simplifications: untiled textures in the 8888/2_10_10_10/5_6_5/1_5_5_5
  formats (tiled/compressed bind the null texture until the Vulkan texture
  loader is ported), one color render target, no depth attachment, resolve is
  recorded-not-copied, and the system constant struct is zero-filled except
  index params + NDC pass-through (full port =
  `VulkanCommandProcessor::UpdateSystemConstantValues`).
- Full SDK link under Emscripten (SDL3 + kernel + UI): mechanically enabled by
  the harness, needs CI iterations to fix the remaining POSIX-API gaps
  (`dlopen` of the GPU plugin side module, audio path, etc.).

## Layout

```
emscripten/
  build.sh                                one-command build (stages + patches + CMake)
  CMakeLists.txt                          app target: SDK subproject + generated code + shell
  rexglue-sdk-v0.10.0-emscripten.patch    the WASM platform layer (13 files)
web/
  index.html                              browser shell (self-contained)
  serve.py                                localhost server with COOP/COEP headers
ci/
  github-actions-wasm.yml                 CI build of the full runtime (~2-4 h)
analysis/
  codebase-analysis.md                    full source-tree analysis with numbers
  port-plan.md                            phased plan: toolchain -> runtime -> renderer -> optimize
```

## Build

```bash
# sandbox/CI: needs ~8 GB disk, 16 GB RAM comfortable (4 GB works, slower)
git clone https://github.com/Player124413/Sonic-Generations-recomp-android-and-pc-edition.git game
git clone https://github.com/rexglue/rexglue-sdk.git rexglue-sdk   # optional; build.sh clones it
bash emscripten/build.sh --jobs $(nproc)
```

Or push `ci/github-actions-wasm.yml` into a repo and let Actions build it.

## Run

```bash
cd web && python3 serve.py 8080     # COOP/COEP headers are required for pthreads
# open http://localhost:8080, drop your game files, press "Boot runtime"
```

## Performance targets (25–30 fps in-browser)

The recompiled code is wasm-friendly by construction (32-bit guest pointers
= wasm32 linear memory, SIMD-mapped vector ops, no JIT). Budgeted headroom:
guest code ~4–8 ms/frame on desktop-class hardware; the risk is the renderer,
which must be kept to the Vulkan backend's amortized cost model (frame-local
command processing + cached pipelines + texture-on-demand). The Android perf
patches in the game repo (`REX_FPS_CAP`, log-flush throttling, interrupt
dispatch serialization) apply to the web build unchanged and are the first
knobs to turn in Phase 4.

## License / legality

Port harness, patch and shell: MIT. rexglue-sdk: BSD 3-Clause. Game code and
assets remain the property of Sega/ Sonic Team; you must own the game and
supply your own files.
