---
name: Bug report
about: A reproducible defect in Render2D's renderer contracts, systems, or runtimes
title: "[bug] "
labels: bug
---

<!-- Render2D is a windowless 2D *rendering backend*, validated via CTest. Please
     keep reports about the renderer itself (components/systems/native runtimes,
     build/packaging, docs) — host-engine concerns (windowing, input, production
     ECS, asset pipeline, gameplay) are intentionally out of scope. -->

## What happened

A clear description of the bug and what you expected instead.

## Reproduction

- Preset: `clang-ninja-debug` / `clang-ninja-perf` / `clang-ninja-host-min` / other
- Failing test (if any): `ctest --preset clang-ninja-debug -R render2d.<feature>`
- Minimal steps or a diff/snippet that triggers it.

## Environment

- OS / compiler / CMake version:
- Vulkan SDK + GPU/driver (or "no GPU — running with the skip guard"):
- How dependencies were resolved: host target reuse / local checkout / git fetch

## Output

```
paste the relevant ctest --output-on-failure / build / clang-tidy output
```
