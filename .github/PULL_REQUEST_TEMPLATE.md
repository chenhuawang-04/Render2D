<!-- Render2D: a windowless, header-only 2D rendering backend validated via CTest. -->

## Summary

What this change does, and why.

## Scope check

- [ ] Stays within the renderer's scope (no windowing/input/audio/production-ECS/
      asset-pipeline/gameplay — see docs/ARCHITECTURE.md → "Scope and non-goals").
- [ ] Preserves the non-negotiable invariants: Strict POD components, `std::span`
      system boundaries, `McVector`/MemoryCenter memory (no `std::vector`, no direct
      `vk*Memory` calls), `id + generation` refs, `VulkanNativeProvider + Dim2`.
- [ ] Public umbrella `Render2D.hpp` stays free of SDL/FreeType/HarfBuzz/SheenBidi/
      RenderDoc/stb includes.
- [ ] Added/updated an ADR under `docs/adr/` if a provider/storage/API/dependency
      contract changed, and kept `docs/PROJECT_INDEX.md` / `docs/ARCHITECTURE.md` in
      sync.

## Verification

- [ ] `cmake --build --preset clang-ninja-debug` && `ctest --preset clang-ninja-debug`
- [ ] `cmake --build --preset clang-ninja-perf` && `ctest --preset clang-ninja-perf`
- [ ] `bash scripts/scan_constraints.sh` (3/3)
- [ ] clang-tidy clean on changed TUs; `git diff --check` clean

```
paste the relevant ctest summary
```
