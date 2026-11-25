// FFmpeg config dispatch -- WASM BUILD OVERRIDE.
//
// The rexglue FFmpeg submodule vendors per-platform config headers
// (config_windows_*.h, config_linux_*.h, ...) but ships none for Emscripten.
// harness/emscripten/build.sh copies THIS file over the submodule's
// thirdparty/FFmpeg/config.h on every run (idempotent, survives SDK cache
// restores), so the wasm build gets config_emscripten.h while every other
// platform keeps its original branch.
#if defined(__EMSCRIPTEN__)
#include "config_emscripten.h"
#elif defined(WIN32) || defined(_WIN32)
#if defined(__aarch64__) || defined(_M_ARM64)
#include "config_windows_aarch64.h"
#else
#include "config_windows_x86_64.h"
#endif
#elif defined(__APPLE__) && defined(__MACH__)
#if defined(__aarch64__) || defined(__arm64__)
#include "config_macos_aarch64.h"
#else
#include "config_macos_x86_64.h"
#endif
#elif defined(__gnu_linux__)
#if defined(__aarch64__)
#include "config_linux_aarch64.h"
#else
#include "config_linux_x86_64.h"
#endif
#elif defined(__ANDROID__)
#if defined(__aarch64__)
#include "config_android_aarch64.h"
#elif defined(__amd64__) || defined(__x86_64__)
#include "config_android_x86_64.h"
#else
#error "android arch unsupported"
#endif
#else
#error "no config"
#endif
