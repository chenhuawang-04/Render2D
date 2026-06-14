# Repository Guidelines

## Project Structure & Module Organization

Render2D is a C++23, CMake-based rendering library currently exposed as the `Render2D::Render2D` interface target. Public headers live under `include/Render2D/`, grouped by concern: `Component/`, `Core/`, `Memory/`, `Meta/`, `Native/`, `Storage/`, and `System/`. The umbrella include is `include/Render2D/Render2D.hpp`. Tests are in `tests/`; benchmark smoke targets are in `bench/`. Architecture notes and ADRs belong in `docs/architecture/` and `docs/adr/`. Generated build output stays in `build/` and is ignored.

## Build, Test, and Development Commands

- `cmake --preset clang-ninja-debug` configures the default Debug build with Ninja, Clang, tests enabled, and Vulkan required.
- `cmake --build --preset clang-ninja-debug` builds the library consumers and enabled test targets.
- `ctest --preset clang-ninja-debug` runs the CTest suite with failure output.
- To enable benchmarks, configure with `-DRENDER2D_BUILD_BENCHMARKS=ON`, then build `render2d_bench_smoke`.

The build expects a Vulkan SDK and four local engine dependency source trees (`MemoryCenterNew`, `fast_math`, `Vector_New`, `ThreadCenter`), which are not vendored or fetched automatically. Point the build at a local checkout with the umbrella `-DRENDER2D_ENGINE_DEPS_ROOT=<root>` (defaults to `E:/Project/MelosyneTest`), or override any tree individually (`RENDER2D_MEMORY_CENTER_SOURCE_DIR`, `RENDER2D_FAST_MATH_SOURCE_DIR`, `RENDER2D_VECTOR_NEW_INCLUDE_DIR`, `RENDER2D_THREAD_CENTER_SOURCE_DIR`). See `README.md` > Dependencies for the expected layout.

## Coding Style & Naming Conventions

Use modern C++23 with no compiler extensions. Keep headers under the `Render2D` namespace and prefer small, POD-friendly types for component-facing APIs. Use `Render2D::McVector<T>` for Render2D-owned dynamic arrays; do not introduce `std::vector` in project source. Follow existing formatting: 4-space indentation in CMake continuation blocks, braces on a new line for C++ functions, PascalCase for public types/concepts such as `StrictPodComponent`, and lower_snake_case for target names such as `render2d_compile_smoke`. Strict warnings are enabled (`/W4 /WX` or `-Wall -Wextra -Wpedantic -Werror`), so fix warnings rather than suppressing them.

## Testing Guidelines

Tests use CTest and executable smoke targets. Add new tests in `tests/`, register them with `add_test`, and name CTest cases with the `render2d.<feature>` pattern. Prefer compile-time `static_assert` coverage for concepts, traits, and POD constraints, plus minimal runtime checks where integration with Vulkan or dependencies is required.

## Commit & Pull Request Guidelines

This checkout has no local Git history, so no project-specific commit pattern can be inferred. Use concise imperative commits, optionally scoped, for example `core: add range type aliases`. Pull requests should include a short purpose statement, affected headers or docs, linked issue if any, and the exact build/test commands run. Include screenshots only for visual rendering changes.

## Agent-Specific Instructions

Do not edit generated files under `build/`. Preserve the Vulkan-native, component-first architecture documented in `docs/architecture/`, and add or update an ADR when changing provider, storage, API, or dependency contracts.
