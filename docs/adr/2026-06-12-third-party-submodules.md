# ADR: Third-Party Font Dependencies as Git Submodules

Date: 2026-06-12

## Status

Accepted. FreeType, HarfBuzz, and SheenBidi are now git submodules under
`third_party/`, pinned to release tags, replacing the previously in-tree
(committed) FreeType source and the in-tree HarfBuzz/SheenBidi source added
during Stage 19.

## Context

Stage 19 introduced three font-runtime dependencies (FreeType, HarfBuzz,
SheenBidi). The Stage 9 work had copied the full FreeType source tree into the
repository and committed it; the Stage 19 work added HarfBuzz and SheenBidi the
same way. Committing the full upstream trees was heavy and obscured provenance:
the HarfBuzz tree alone carried ~88 MB of `test/` plus `perf/`, `docs/`, and
`util/` that the build never uses, and the committed FreeType source was ~350k
lines with no clear pin to an upstream version. Changing how a dependency is
acquired is a dependency-contract change, which this repository requires to be
recorded in an ADR (CLAUDE.md red line #9).

## Decision

- Acquire all three font dependencies as **git submodules** under `third_party/`
  instead of vendoring their source into the repository:
  - `third_party/freetype` → tag `VER-2-14-3`
  - `third_party/harfbuzz` → tag `14.2.1`
  - `third_party/sheenbidi` → tag `v3.0.0`
- Pin to the **exact upstream versions Stage 19 was written and verified against**
  (the same FreeType 2.14.3 / HarfBuzz 14.2.1 / SheenBidi 3.0.0 the vendored
  trees carried), so the submodule conversion is a byte-for-byte equivalent build,
  not a version bump. SheenBidi `v3.0.0` is a deliberate pin: its API
  (`SBLineGetRunsPtr`, `SBScriptLocator`) is what `BidiItemizeRuntime` targets,
  and the v3 → main line may diverge.
- Record `shallow = true` for each submodule in `.gitmodules` so first-time
  clones (and CI) fetch only the pinned snapshot, not full upstream history.
- The CMake wiring is unchanged: each submodule is still `add_subdirectory`-ed and
  built as a static library behind `RENDER2D_BUILD_FONT_RUNTIME` (default ON),
  exposed only through the internal `render2d_font_runtime_support` target, with
  include dirs promoted to SYSTEM. Nothing about the umbrella isolation or the
  pure-vs-runtime split changes.

## Consequences

- Cloning the repository now requires `git submodule update --init --recursive`
  before a font-runtime build will configure; without it the three
  `third_party/<lib>` directories are empty and `add_subdirectory` fails. The
  `RENDER2D_BUILD_FONT_RUNTIME=OFF` pure-sprite build is unaffected.
- The repository no longer carries the upstream source or its unused `test/`,
  `perf/`, `docs/`, `util/` directories; dependency versions are now explicit,
  pinned commits visible in `git submodule status`.
- Upgrading a font dependency is now an explicit submodule bump (checkout the new
  tag, `git add` the gitlink, re-run the Debug + Perf gates) rather than a source
  re-copy. The pinned tags must move in lockstep with any API usage change in
  `include/Render2D/Font/`.
- The four engine source dependencies (`MemoryCenterNew`, `fast_math`,
  `Vector_New`, `ThreadCenter`) remain hardcoded sibling paths in `CMakeLists.txt`
  and are **not** addressed here; decoupling them (FetchContent or submodule, with
  `RENDER2D_*_SOURCE_DIR` overrides as fallback) is Stage 17A.
- The Debug (`ctest` 46/46) and Perf (`ctest` 55/55) gates pass against the
  submodule trees, confirming equivalence with the prior vendored build.
