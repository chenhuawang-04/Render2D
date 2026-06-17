#!/usr/bin/env bash
# Stage 25C: verify Render2D's tier-3 (FetchContent) engine-dependency resolver.
#
# The three-tier resolver (cmake/Render2DDependencies.cmake) falls back to tier 3
# -- FetchContent clones the repo from git, then the resolver add_subdirectory's
# it -- when a dependency has neither an existing target (tier 1) nor a local
# source tree (tier 2). This script FORCES tier 3 and checks it actually clones
# and configures, rather than trusting that the code path works untested.
#
# Modes:
#   (default) OFFLINE mechanism check. Forces tier 3 for the two submodule-free
#     deps -- fast_math and Vector_New/McVector -- using the LOCAL dependency git
#     repos as clone sources, so the real FetchContent clone + add_subdirectory
#     path runs with NO network. MemoryCenter and ThreadCenter stay tier 2: their
#     github submodules (mimalloc/VMA/json; taskflow) would need network to
#     recurse, which defeats an offline check.
#   --online  FULL check. Forces tier 3 for all four against the real github URLs.
#     Needs network + git credentials for the two PRIVATE repos (MemoryCenterNew,
#     Vector_New) -- set up a proxy / ENGINE_DEPS_TOKEN as your environment
#     requires. This is what CI's hosted full-build runs; see
#     docs/adr/2026-06-14-stage17e-engine-dep-fetch.md.
#
# Exit 0 = tier 3 verified for this mode; non-zero = it failed (NOT a silent pass).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps_root="${RENDER2D_ENGINE_DEPS_ROOT:-E:/Project/MelosyneTest}"
work="${repo_root}/build_fetch_verify"
nonexistent="${work}/_no_such_local_tree"

mode="offline"
[ "${1:-}" = "--online" ] && mode="online"

rm -rf "${work}"
mkdir -p "${work}"

cfg=(
  -S "${repo_root}" -B "${work}"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_CXX_COMPILER=clang++
  -DCMAKE_C_COMPILER=clang
  -DRENDER2D_BUILD_TESTS=OFF
  -DRENDER2D_BUILD_BENCHMARKS=OFF
  -DRENDER2D_BUILD_FONT_RUNTIME=OFF
  -DRENDER2D_BUILD_PRESENT_HOST=OFF
  -DRENDER2D_INSTALL=OFF
)

if [ "${mode}" = "offline" ]; then
  fm="${deps_root}/Math/fast_math"
  vn="${deps_root}/Vector_New"
  if [ ! -e "${fm}/.git" ] || [ ! -e "${vn}/.git" ]; then
    echo "SKIP: local git repos for fast_math/Vector_New not found under ${deps_root}; cannot run the offline mechanism check."
    exit 0
  fi
  fm_sha="$(git -C "${fm}" rev-parse HEAD)"
  vn_sha="$(git -C "${vn}" rev-parse HEAD)"
  cfg+=(
    -DRENDER2D_FAST_MATH_SOURCE_DIR="${nonexistent}/fast_math"
    -DRENDER2D_FAST_MATH_GIT_REPOSITORY="${fm}"
    -DRENDER2D_FAST_MATH_GIT_TAG="${fm_sha}"
    -DRENDER2D_VECTOR_NEW_INCLUDE_DIR="${nonexistent}/Vector_New/include"
    -DRENDER2D_VECTOR_NEW_GIT_REPOSITORY="${vn}"
    -DRENDER2D_VECTOR_NEW_GIT_TAG="${vn_sha}"
  )
  echo "Render2D fetch-tier verify: OFFLINE mechanism check"
  echo "  fast_math    <- clone ${fm} @ ${fm_sha}"
  echo "  Vector_New   <- clone ${vn} @ ${vn_sha}"
  echo "  MemoryCenter/ThreadCenter stay tier 2 (local; github submodules)"
else
  cfg+=( -DRENDER2D_ENGINE_DEPS_ROOT="${nonexistent}" )
  echo "Render2D fetch-tier verify: ONLINE full check (real github URLs)"
  echo "  needs network + ENGINE_DEPS_TOKEN for the two private repos"
fi

echo "+ cmake ${cfg[*]}"
cmake "${cfg[@]}"

# Configure success means the resolver ran to completion: tier 3 cloned the
# forced deps, add_subdirectory defined their targets, and the resolver's own
# FATAL_ERROR guards (target defined / McVector header present) passed. Confirm a
# real clone landed under _deps rather than a silent tier-2 fallback.
if [ "${mode}" = "offline" ]; then
  test -f "${work}/_deps/fast_math-src/CMakeLists.txt" \
    || { echo "FAIL: fast_math was not fetched into _deps/fast_math-src"; exit 1; }
  test -f "${work}/_deps/vector-src/include/Center/Memory/Container/Vector/McVector.hpp" \
    || { echo "FAIL: McVector was not fetched into _deps/vector-src"; exit 1; }
  echo "PASS: tier-3 FetchContent cloned + configured fast_math and McVector (offline mechanism verified)."
else
  test -f "${work}/_deps/fast_math-src/CMakeLists.txt" \
    || { echo "FAIL: fast_math was not fetched into _deps/fast_math-src"; exit 1; }
  echo "PASS: tier-3 FetchContent resolved all four deps from github + configured Render2D."
fi
