# Setup Guide — Sonic Generations recomp (WASM / WebGPU)

This repo contains the **browser (wasm) port harness** for the Sonic
Generations recompilation project: the Emscripten build pipeline, the SDK
patch that adds a **WebGPU GPU backend** (with a real end-to-end guest draw
path) and an in-tree **SPIR-V → WGSL shader translator**, the browser shell,
a standalone WebGPU validation demo, and CI. Follow this guide to set it up
from a fresh clone.

---

## 0. What is in this repo

```
./                              (repository root)
├── README.md                     Project overview + status
├── SETUP.md                      This guide
├── LICENSE                       BSD 3-Clause (port code; see notes)
├── .gitignore
├── docs/
│   └── …-feasibility-report.pdf  Full codebase analysis + port feasibility report
├── analysis/
│   ├── codebase-analysis.md      What the game port + SDK actually are (architecture)
│   └── port-plan.md              Phased port plan with current status
├── emscripten/
│   ├── build.sh                  One-command wasm build (see §3)
│   ├── CMakeLists.txt            Link config: pthreads + JSPI + Dawn/WebGPU port
│   └── rexglue-sdk-v0.10.0-emscripten.patch   The SDK patch (31 files, incl. WebGPU backend)
├── ci/
│   └── github-actions-wasm.yml   CI build workflow (ubuntu-latest, ~2–4 h)
├── scripts/
│   ├── wgsl_test.cpp             Native test for the SPIR-V → WGSL translator
│   └── webgpu-compile-check.sh   Per-file emcc compile check for the backend
└── web/
    ├── index.html                Browser shell: game loader, input, HUD, saves
    ├── serve.py                  Local server with the COOP/COEP headers pthreads needs
    └── webgpu-demo.html          Standalone WebGPU demo (no build needed)
```

**Verified status (be realistic about what works today):**

| Piece | Status |
| --- | --- |
| SDK patch applies to a fresh `rexglue-sdk v0.10.0` clone | ✅ verified (`git apply --check`) |
| WebGPU backend (9 C++ files) compiles under emcc 7 + Dawn port | ✅ verified (`scripts/webgpu-compile-check.sh`, all 9 OK) |
| Guest draw path (constants, textures, render targets, pipelines, present) | ✅ implemented end-to-end with documented v1 limitations (see the file header of `command_processor.cpp` inside the patch) |
| SPIR-V → WGSL translator output | ✅ verified natively (`scripts/wgsl_test.cpp` → valid WGSL) |
| WebGPU demo in a real browser | ✅ loads, zero JS errors; render loop needs a GPU |
| Full wasm link (`build.sh` end-to-end) | ⚠️ wired up, not yet green on CI — expect to fix a few remaining link-level gaps on first runs (this is what the CI workflow is for) |
| Playing the game | ⚠️ depends on the above |

---

## 1. Prerequisites

- **git**, **cmake ≥ 3.20**, **ninja** (or make), **python 3.8+**
- **Emscripten 7.x** — `build.sh` clones emsdk automatically (into
  `../emsdk` relative to the repo, overridable via `EMSDK_DIR`). Tested with
  `emcc 7.0.0 (777e6e5db)`. Emscripten 7 is required: the WebGPU headers come
  from the new `--use-port=emdawnwebgpu` (Dawn) port, which replaced the old
  `-sUSE_WEBGPU`.
- **~15 GB free disk** and ideally 8+ CPU cores (the full build compiles
  ~11.7 M lines of generated recompiled PPC code; on 16 cores ≈ 2–4 h).
- **A WebGPU-capable browser** to run anything: Chrome/Edge 113+, Safari 26+,
  or Firefox with WebGPU enabled.

## 2. Quick start — WebGPU demo (no build, 1 minute)

```bash
cd web
python3 -m http.server 8090
# open http://localhost:8090/webgpu-demo.html
```

You should see the validation page exercise surface configuration, a
`queue.writeTexture` upload, a render pass and a compute pass. If it says
`FAIL requestAdapter returned null` your machine/browser lacks a GPU adapter —
the page still proves the toolchain-side parts.

## 3. Full build (local)

`build.sh` is idempotent: it reuses whatever is already cloned/patched.

```bash
# 0) You also need the game recomp project (the generated code source):
git clone --depth 1 \
  https://github.com/Player124413/Sonic-Generations-recomp-android-and-pc-edition.git \
  ../Sonic-Generations-recomp-android-and-pc-edition

# 1) Build (clones emsdk + rexglue-sdk v0.10.0 + submodules, applies the
#    patch, stages generated code, configures CMake, links the wasm):
bash emscripten/build.sh --jobs $(nproc)
```

Environment overrides if you keep things elsewhere:

| Variable | Default | Purpose |
| --- | --- | --- |
| `GAME_DIR` | `../Sonic-Generations-recomp-android-and-pc-edition` | generated-code source |
| `REXGLUE_SDK_DIR` | `../rexglue-sdk` | SDK clone location |
| `EMSDK_DIR` | `../emsdk` | emsdk location |

What it produces: `emscripten/build/web/sonicgenerations.js` +
`sonicgenerations.wasm` (JSPI + pthreads + WebGPU backend).

If the patch was already applied to a clone from before this repo's patch
changed, delete the stamp file and reset the SDK clone first:
`rm ../rexglue-sdk/.emscripten-patched && git -C ../rexglue-sdk reset --hard` (or
just delete the SDK dir; build.sh re-clones it).

## 4. Run it in the browser

pthreads needs `SharedArrayBuffer`, which browsers only expose under
COOP/COEP headers — **use `serve.py`, not `python -m http.server`**:

```bash
cp emscripten/build/web/sonicgenerations.js web/
cp emscripten/build/web/sonicgenerations.wasm web/
cd web && python3 serve.py 8080
# open http://localhost:8080
```

The shell loads the runtime, shows FPS/heap HUD, and takes the game image
through the in-page loader (stored in IndexedDB). **You must provide your own
legally-obtained copy of the game** — this project contains no game assets.

Save data persists through IDFS → IndexedDB; use the shell's export button
for a portable `.sav`.

## 5. Clone + CI

1. This repository is the project root — these files are already committed at
   the top level, so a plain clone is all you need:

```bash
git clone git@github.com:mightyhammerheadtree/sonicgen.git
cd sonicgen
```

2. CI: `.github/workflows/wasm-build.yml` (same file as
   `ci/github-actions-wasm.yml`) triggers on pushes touching `emscripten/**`
   or `ci/**`, plus manual `workflow_dispatch`. It checks out the game repo +
   this harness, caches emsdk and the patched SDK, runs `build.sh` (with one
   automatic retry), and uploads `sonicgenerations.js/.wasm` as an artifact.

3. Runner notes: `ubuntu-latest` works; the build is long, so the job has a
   300-minute timeout and caches `emsdk/` + `rexglue-sdk/` (minus `build/`).
   If you fork the game repo, update the `repository:` line in the workflow.

## 6. Verifying your toolchain / backend changes

```bash
# per-file compile check of the WebGPU backend (fast, ~1 min):
bash scripts/webgpu-compile-check.sh     # expect: OK on all 9 files

# native WGSL translator test (g++ only, no emscripten needed):
g++ -std=c++20 -I <path-to-rexglue-sdk>/src/graphics/webgpu \
    scripts/wgsl_test.cpp <path-to-rexglue-sdk>/src/graphics/webgpu/wgsl_translator.cpp \
    -o /tmp/wgsl_test && /tmp/wgsl_test
```

## 7. Troubleshooting

- **`emcc: error: ... --use-port=emdawnwebgpu` unknown** → you are on
  Emscripten ≤ 3.x/4.x. Install 7.x (`./emsdk install latest && ./emsdk
  activate latest`).
- **`emdawnwebgpu requires C++`** → link with `em++`/CMake (the provided
  CMakeLists already does; this bites only hand-rolled `emcc` link lines).
- **Patch fails with `git apply`** → the SDK clone is not at `v0.10.0` or has
  local modifications. Re-clone at the tag and re-run build.sh.
- **Blank page / console shows `SharedArrayBuffer` error** → you served
  without COOP/COEP; use `web/serve.py`.
- **`navigator.gpu` unavailable** → Chrome/Edge: needs 113+ (or
  `chrome://flags#enable-unsafe-webgpu` on older builds); Firefox:
  `dom.webgpu.enabled`; headless Linux VMs usually have no GPU adapter at all.
- **Link errors mentioning `wgpu*` symbols** → the WebGPU port flag must be
  present at **link time too** (it is, via `CMakeLists.txt`; re-check if you
  modified link options).
- **Build OOM on small runners** → drop `--jobs`, or add swap; the generated
  translation units are the heavy part.

## 8. License notes

Port code in this repo (the patch's new files, scripts, web shell) is BSD
3-Clause — see `LICENSE`. The patched `rexglue-sdk` remains under its own
license (BSD-licensed Xenia-derived code, per its file headers); the game
recompilation and the game itself have their own terms — you are responsible
for using legally obtained content.
