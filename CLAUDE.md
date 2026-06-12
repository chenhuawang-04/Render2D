# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Render2D is a C++23, **header-only `INTERFACE` library** (`Render2D::Render2D`) implementing a component-first, Vulkan-native 2D renderer. There is no application or shared/static lib output — the library is validated entirely through CTest executables under `tests/` and benchmark executables under `bench/`. It is designed to be merged into a host engine later; see `docs/ProjectMergeTODO.md` for the integration contracts that constrain current design.

## Build, test, run

The project uses CMake presets (Ninja + Clang). Two presets:

- `clang-ninja-debug` — Debug, tests ON, benchmarks OFF, Vulkan required → builds into `build/`
- `clang-ninja-perf` — RelWithDebInfo, tests ON, **benchmarks ON**, Vulkan required → builds into `build_perf/`

```bash
cmake --preset clang-ninja-debug          # configure
cmake --build --preset clang-ninja-debug  # build all test targets
ctest --preset clang-ninja-debug          # run full suite (outputOnFailure is on)
ctest --preset clang-ninja-debug -R render2d.bounds_system   # run ONE test
```

CTest cases are named `render2d.<feature>` (e.g. `render2d.cpu_system_pipeline`, `render2d.vulkan_resource_runtime`). Use `-R` with that pattern to run a single test.

Benchmarks (Perf preset builds them automatically):

```bash
cmake --preset clang-ninja-perf && cmake --build --preset clang-ninja-perf
./build_perf/bench/render2d_null_cpu_bench.exe --scenario mixed --sprites 10000 --texts 2048 --frames 8 --warmup 2
# Or the standard suites: scripts/run_null_cpu_benchmarks.ps1, scripts/run_threaded_cpu_benchmarks.ps1
```

### External dependencies (build will not configure without them)

`CMakeLists.txt` hardcodes absolute paths to four source trees that are **not in this repo** and fails fast (FATAL_ERROR) if missing. Defaults assume a sibling `E:/Project/MelosyneTest/` checkout:

- `RENDER2D_MEMORY_CENTER_SOURCE_DIR` → MemoryCenterNew
- `RENDER2D_FAST_MATH_SOURCE_DIR` → fast_math (provides `MMath`)
- `RENDER2D_VECTOR_NEW_INCLUDE_DIR` → Vector_New (provides `McVector`)
- `RENDER2D_THREAD_CENTER_SOURCE_DIR` → ThreadCenter
- Plus the Vulkan SDK (`find_package(Vulkan REQUIRED)`)

Override these cache variables if your layout differs. Set `-DRENDER2D_REQUIRE_VULKAN=OFF` to configure without a Vulkan SDK.

### Vulkan tests pass without a GPU

Every `render2d.vulkan_*` smoke test creates an instance/device and **returns 0 (pass) if it cannot** (see the `main()` guard in `tests/vulkan_*_test.cpp`). A green `ctest` run therefore does **not** prove the Vulkan paths actually executed — only that contract/state logic passed and the GPU paths didn't crash where a device was present. These tests render offscreen and verify by readback (no window/swapchain).

## Architecture

Three layers, strictly separated. Read `docs/ARCHITECTURE.md` for the full pipeline diagram and the runtime-class inventory; `docs/PROJECT_INDEX.md` is a maintained file-by-file index.

**1. Components (`include/Render2D/Component/`, `Native/NativeComponents.hpp`)** — All render data is an ECS component and every component is **Strict POD**: `is_trivial && is_standard_layout && is_trivially_copyable && is_aggregate` (the `StrictPodComponent` concept in `Component/StrictPod.hpp`). No `std::string`/`std::vector`/`std::span` members, no constructors/destructors/virtuals, no RAII. Components own no resources. Every supported type is registered in `ComponentTraits.hpp` and gated by the `SupportedRenderComponent` concept. `tests/negative_non_pod_component.cpp` is a compile-failure test enforcing this.

**2. Systems (`include/Render2D/System/`)** — Stateless template types. They take inputs as `std::span<const T>`, write outputs as `std::span<T>`, return `SystemResult`, **do not allocate on the hot path, do not touch ECS storage, and do not call Vulkan.** This keeps them reusable when the host engine's ECS replaces the test storage. Stream sizes are `U32`-bounded; systems reject larger inputs with `SystemStatusCode::InvalidInput`.

**3. Native runtimes (`include/Render2D/Native/`)** — Own Vulkan/backend object lifetimes. ECS never holds a Vulkan handle; it holds POD references of the form `id + generation` (e.g. `BufferRef::buffer_id + ::generation`). Runtime tables validate the generation and **reject stale references after slot reuse**. `Native*` classes are CPU-only skeletons; `Vulkan*` classes attach real handles behind the same refs.

Data flow (sprite path): `Transform/Sprite/Camera → Transform → Bounds → Culling → CommandBuild → DrawCommand → SpriteInstance → Batch`. Native-bound side streams (`UploadCommand`, `DescriptorSlice`) are coalesced/compacted, then `CommandBuffer → Encode → Submit`. A parallel text path runs `TextDirty → GlyphRunBuild → GlyphInstanceBuild → GlyphBatch → DrawCommand`.

### Non-negotiable invariants (these have ADRs; preserve them)

- **Memory:** Render2D-owned dynamic arrays use `Render2D::McVector<T>` (alias of Vector_New `mc_vector`, routed through MemoryCenter). **Never introduce `std::vector`** in src, tests, or bench. All Vulkan buffer/image memory goes through `VulkanMemoryCenterAllocator` — never call `vkAllocateMemory`/`vkFreeMemory`/`vkMapMemory`/`vkBindBufferMemory`/etc. directly.
- **Math:** `Render2D::Vec2`/`Mat3`/`Aabb2` are aliases of `MMath` (fast_math) POD types; math is done via fast_math free functions. There are no Render2D-local vector/matrix/AABB structs. Construct/access AABBs via `makeAabb2` / `aabb2Min` / `aabb2Max` — fast_math stores max as negatives internally, so touching fields directly is wrong.
- **Provider/Dim:** compile-time tags gate the domain; the only valid one is `VulkanNativeProvider + Dim2`.
- **ThreadCenter is runtime-only infrastructure.** It must not appear in ECS components or in the umbrella `Render2D.hpp`. Code needing it (e.g. `System/ThreadedCpuPipeline.hpp`) links the internal `render2d_thread_runtime_support` target instead. Single-threaded systems remain the deterministic correctness reference; multi-threaded paths must merge deterministically.
- **The ECS under `tests/support/` is test-only scaffolding**, not production architecture — the host engine's ECS replaces it. Systems must consume `std::span`, never the test storage type.

## Conventions

Naming (enforced by `.clang-tidy` `readability-identifier-naming`, warnings-as-errors):

- Types / namespaces / enums / enum values / template params: `UpperCamelCase`
- Functions / methods: `lowerCamelCase`
- Variables / all members / globals / locals: `lower_snake_case`
- **Function parameters: `lower_snake_case_` with a trailing underscore** (e.g. `out_instance_`)
- Constants / `constexpr` / static constants: `k` + `UpperCamelCase` (e.g. `kStrictPodComponentValue`)
- CMake target names: `lower_snake_case` (e.g. `render2d_compile_smoke`); CTest names: `render2d.<feature>`

C++23, no compiler extensions. Strict warnings are on everywhere (`/W4 /WX` MSVC, `-Wall -Wextra -Wpedantic -Werror` Clang) — fix warnings, don't suppress. Open braces on a new line for C++ functions. Prefer compile-time `static_assert` coverage for concepts/traits/POD constraints, with minimal runtime checks where Vulkan/dependency integration requires it.

Commits: `scope: imperative summary` (e.g. `sprite: add texture atlas uv regions`, `native: add stage 11 vulkan swapchain runtime`).

## Working in this repo

- Work is organized into numbered **Stages** (currently through Stage 16). `Plan.md` is the long-term plan; `docs/PROJECT_INDEX.md` and `docs/ARCHITECTURE.md` are living docs kept in sync with the code.
- When changing a **provider, storage, API, or dependency contract**, add or update an ADR in `docs/adr/` and the relevant note in `docs/architecture/` — this is an explicit repo rule, not optional.
- Do not edit generated output under `build/` or `build_perf/`.
- `third_party/freetype/`, `third_party/harfbuzz/`, and `third_party/sheenbidi/` are vendored source trees for the **Stage 19** font/text runtime. They build as static libraries behind the `RENDER2D_BUILD_FONT_RUNTIME` option (default ON) and are exposed only through the internal `render2d_font_runtime_support` target — the same isolation as `render2d_thread_runtime_support`. **Never include FreeType/HarfBuzz/SheenBidi headers from the public umbrella `Render2D.hpp`**; font-runtime headers live under `include/Render2D/Font/` and require linking `render2d_font_runtime_support`. The Stage 9 glyph systems and the Stage 19C pure systems (`include/Render2D/System/TextShapingSystem.hpp`) stay deterministic and dependency-free.
