# Render2D

Render2D is a C++23, **header-only `INTERFACE` library** (`Render2D::Render2D`) implementing a
component-first, Vulkan-native 2D renderer. There is no application or shared/static-library output —
the library is validated entirely through CTest executables under `tests/` and benchmark executables
under `bench/`. It is designed to be merged into a host engine later.

For architecture, see [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and the file-by-file
[`docs/PROJECT_INDEX.md`](docs/PROJECT_INDEX.md). For the working conventions and invariants, see
[`CLAUDE.md`](CLAUDE.md) and [`AGENTS.md`](AGENTS.md).

## Dependencies

Render2D builds against four host-engine source trees and the Vulkan SDK. The four engine trees are
**not vendored and not fetched automatically**: one (`Vector_New`) has no git remote and the others
live in private repositories, so the build cannot pull them on a clean or CI machine. Point the build
at a local checkout instead.

Set **one** umbrella path and the four locations are derived from it:

```bash
cmake --preset clang-ninja-debug -DRENDER2D_ENGINE_DEPS_ROOT=/path/to/MelosyneTest
```

`RENDER2D_ENGINE_DEPS_ROOT` defaults to `E:/Project/MelosyneTest`. Each tree may also be overridden
individually (an individual override takes precedence over the umbrella default):

| Dependency             | Default location (under the root)         | Individual override               | Marker file                                       |
| ---------------------- | ----------------------------------------- | --------------------------------- | ------------------------------------------------- |
| MemoryCenterNew        | `<root>/MemoryCenterNew`                  | `RENDER2D_MEMORY_CENTER_SOURCE_DIR` | `CMakeLists.txt`                                |
| fast_math (`MMath`)    | `<root>/Math/fast_math`                   | `RENDER2D_FAST_MATH_SOURCE_DIR`     | `CMakeLists.txt`                                |
| Vector_New (`McVector`)| `<root>/Vector_New/include`               | `RENDER2D_VECTOR_NEW_INCLUDE_DIR`   | `Center/Memory/Container/Vector/McVector.hpp`   |
| ThreadCenter           | `<root>/ThreadCenter`                     | `RENDER2D_THREAD_CENTER_SOURCE_DIR` | `CMakeLists.txt`                                |

If a tree is missing, configure fails fast with the expected marker path, the variable that was used,
and the fix. `RENDER2D_ENGINE_DEPS_ROOT` only seeds the per-dependency defaults at first configure; to
re-point an existing build directory, either reconfigure a fresh build or set the individual variables.

### Vulkan SDK

`find_package(Vulkan REQUIRED)` must succeed. Configure with `-DRENDER2D_REQUIRE_VULKAN=OFF` to build
the non-Vulkan paths without an SDK (the Vulkan smoke tests then skip — see
[No GPU required](#no-gpu-required)).

### Font/text submodules

The Stage 19 font/text runtime uses three git submodules (`third_party/freetype`,
`third_party/harfbuzz`, `third_party/sheenbidi`). After cloning:

```bash
git submodule update --init --recursive
```

They build as static libraries behind `RENDER2D_BUILD_FONT_RUNTIME` (default `ON`); set it `OFF` to
skip the font runtime entirely.

## Build & test

The project uses CMake presets (Ninja + Clang):

| Preset               | Build type     | Tests | Benchmarks | Build dir     |
| -------------------- | -------------- | ----- | ---------- | ------------- |
| `clang-ninja-debug`  | Debug          | ON    | OFF        | `build/`      |
| `clang-ninja-perf`   | RelWithDebInfo | ON    | **ON**     | `build_perf/` |

```bash
cmake --preset clang-ninja-debug                 # configure
cmake --build --preset clang-ninja-debug         # build all test targets
ctest --preset clang-ninja-debug                 # run the full suite (output-on-failure is on)
ctest --preset clang-ninja-debug -R render2d.bounds_system   # run ONE test
```

CTest cases are named `render2d.<feature>`; use `-R render2d.<feature>` to run a single test.

### Benchmarks

The perf preset builds the benchmark executables automatically:

```bash
cmake --preset clang-ninja-perf && cmake --build --preset clang-ninja-perf
./build_perf/bench/render2d_null_cpu_bench.exe --scenario mixed --sprites 10000 --texts 2048 --frames 8 --warmup 2
# Standard suites: scripts/run_null_cpu_benchmarks.ps1, scripts/run_threaded_cpu_benchmarks.ps1
```

## No GPU required

Every `render2d.vulkan_*` smoke test creates an instance/device and **returns 0 (pass) if it cannot**
(the `main()` guard in `tests/vulkan_*_test.cpp`). The tests render offscreen and verify by readback —
no window or swapchain. A green `ctest` run therefore confirms the contract/state logic and that the
GPU paths did not crash where a device was present; it does not by itself prove the GPU paths executed.
