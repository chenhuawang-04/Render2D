# Render2D

Render2D is a C++23, **header-only `INTERFACE` library** (`Render2D::Render2D`) implementing a
component-first, Vulkan-native 2D renderer. There is no application or shared/static-library output —
the library is validated entirely through CTest executables under `tests/` and benchmark executables
under `bench/`. It is designed to be merged into a host engine later.

> **Scope:** Render2D is a 2D *rendering module*, **not a game engine.** Windowing/app loop, input,
> audio, the production ECS/scene graph, the asset pipeline, animation, physics, gameplay/scripting,
> particles/tilemaps/UI, editor tooling, and networking are **intentionally out of scope** — they are
> the host engine's responsibility, filled in at merge. See
> [Scope and non-goals](docs/ARCHITECTURE.md#scope-and-non-goals). Measure completeness against "a
> complete 2D *renderer*", not "a complete 2D *engine*".

For architecture, see [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and the file-by-file
[`docs/PROJECT_INDEX.md`](docs/PROJECT_INDEX.md). For the working conventions and invariants, see
[`CLAUDE.md`](CLAUDE.md) and [`AGENTS.md`](AGENTS.md).

## Dependencies

Render2D builds against four host-engine source trees and the Vulkan SDK. Each engine tree is resolved
in three tiers, **downloading only as a last resort**: (1) reuse it if the enclosing CMake project
already defines its target; (2) `add_subdirectory` a local checkout if one is present; (3) otherwise
fetch it from git. Two of the four repos are private (`MemoryCenter`, `Vector`), so a fetch on a clean
or CI machine needs git credentials with read access to them.

To build against local checkouts (no download), set **one** umbrella path and the four locations are
derived from it:

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

If neither an existing target nor a local tree is found, that dependency is **fetched from git**. The
repository and ref are overridable per dependency, so CI can pin an exact commit instead of a branch:

| Dependency              | Fetch repo (default)                            | Repo / ref override variables                         |
| ----------------------- | ----------------------------------------------- | ----------------------------------------------------- |
| MemoryCenterNew         | `chenhuawang-04/MelosyneMemoryCenter` (private) | `RENDER2D_MEMORY_CENTER_GIT_REPOSITORY` / `…_GIT_TAG` |
| fast_math (`MMath`)     | `chenhuawang-04/Melosyne-Math`                  | `RENDER2D_FAST_MATH_GIT_REPOSITORY` / `…_GIT_TAG`     |
| Vector_New (`McVector`) | `chenhuawang-04/Vector` (private)               | `RENDER2D_VECTOR_NEW_GIT_REPOSITORY` / `…_GIT_TAG`    |
| ThreadCenter            | `chenhuawang-04/Melosyne_ThreadCenter`          | `RENDER2D_THREAD_CENTER_GIT_REPOSITORY` / `…_GIT_TAG` |

`RENDER2D_ENGINE_DEPS_ROOT` only seeds the per-dependency local defaults at first configure; to re-point
an existing build directory, reconfigure a fresh build or set the individual variables.

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

The Perf preset also registers an automated **performance-regression gate**
(`render2d.perf_gate_*` CTest cases). Each asserts the pipeline's deterministic,
machine-independent work counts (visible / draws / glyph draws / batches) and a
generous summed-stage-time catastrophe budget (`--max-total-avg-ms`), so it catches
algorithmic regressions and O(n²)-class slowdowns without flaking on CI noise. It
rides the `Test (Perf)` CI step automatically. See
`docs/architecture/BENCHMARK_BASELINE.md` and
`docs/adr/2026-06-14-stage21-perf-regression-gate.md` for what it does and does not
prove (it is not a micro-regression detector — those still use the manual baseline).

## Constraint scans

`scripts/scan_constraints.sh` enforces the non-negotiable source invariants — no `std::vector` (use
`Render2D::McVector<T>`), no direct Vulkan memory API (route through `VulkanMemoryCenterAllocator`), and
no Render2D-local math structs (use the `MMath` aliases). It scans only Render2D-owned source
(`include/`, `src/`, `tests/`, `bench/`) and never the vendored `third_party/` trees. It needs no build
or GPU, runs on Linux and on Windows under Git-bash, and exits nonzero printing `file:line` for any
violation:

```bash
bash scripts/scan_constraints.sh
```

## Continuous integration

CI is a single workflow, `.github/workflows/ci.yml`, in two tiers:

- **Portable checks** (`portable-checks`, hosted `ubuntu-latest`) run on every push to `main`/`master`
  and every pull request. They need no engine dependencies, Vulkan SDK, GPU, or build — the constraint
  scan, a merge-conflict-marker scan, and a `CMakePresets.json` validity check. This is the always-on
  safety net.
- **Full build** (`full-build`, hosted `ubuntu-latest`) runs configure → build → `ctest` (Debug + Perf) →
  clang-tidy → constraint scan → bench smoke. It installs clang + Ninja and the Vulkan loader/headers,
  then lets CMake fetch the four engine deps from git. It is **manual** (`workflow_dispatch`) to control
  cost. No GPU is present, so the `vulkan_*` smokes skip — the build is green but GPU paths are not
  exercised here (see [No GPU required](#no-gpu-required)).

To enable the full tier: the two private deps (`MemoryCenter`, `Vector`) are fetched over HTTPS, so add a
repository secret **`ENGINE_DEPS_TOKEN`** — a personal access token with read access to them
(*Settings → Secrets and variables → Actions*). Then start the **CI** workflow via *Run workflow*; leave
`engine_deps_root` empty to fetch, or set it to a pre-staged local root to skip fetching. The font
submodules are public and are checked out by the workflow.

## No GPU required

Every `render2d.vulkan_*` smoke test creates an instance/device and **returns 0 (pass) if it cannot**
(the `main()` guard in `tests/vulkan_*_test.cpp`). The tests render offscreen and verify by readback —
no window or swapchain. A green `ctest` run therefore confirms the contract/state logic and that the
GPU paths did not crash where a device was present; it does not by itself prove the GPU paths executed.

The `render2d.vulkan_validation_layer_smoke` test additionally runs an offscreen workload under the
Vulkan validation layer and fails on any validation error; it skips (still passing) when the validation
layer is not installed.
