#!/usr/bin/env bash
# Code coverage for Render2D: how much of include/Render2D/ the CTest suite
# exercises. Render2D is a header-only library, so coverage is measured on the test
# TUs that include the headers, using Clang source-based coverage
# (-fprofile-instr-generate -fcoverage-mapping, applied to the Render2D-owned test
# targets by RENDER2D_ENABLE_COVERAGE). No external service and no token: it emits
# a local text summary + an HTML report under build_coverage/coverage/.
#
# On a GPU box the vulkan_*/present_* tests run their real paths, so those lines
# count too; on a headless box they graceful-skip and show as uncovered (honest).
# Clang only (uses llvm-profdata / llvm-cov). Works on Linux and on Windows under
# Git-bash. Override the LLVM tools with LLVM_PROFDATA / LLVM_COV if needed.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

build_dir="build_coverage"
profraw_dir="$build_dir/profraw"
report_dir="$build_dir/coverage"
profdata="$report_dir/render2d.profdata"

profdata_tool="${LLVM_PROFDATA:-llvm-profdata}"
cov_tool="${LLVM_COV:-llvm-cov}"
# Extra cmake configure args (e.g. CI passes -DCMAKE_CXX_COMPILER=clang++-22 plus
# the engine-deps root / FETCHCONTENT_BASE_DIR). Empty by default.
read -r -a cmake_extra <<< "${R2D_CMAKE_EXTRA:-}" || true

# Native (forward-slash) path form for path-matching args, so llvm-cov's source
# filter matches clang's recorded absolute header paths. cygpath -m on Windows
# (Git-bash), identity on Linux.
to_native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi
}

echo "=== configure + build (coverage, Debug) ==="
cmake --preset clang-ninja-debug -B "$build_dir" -DRENDER2D_ENABLE_COVERAGE=ON "${cmake_extra[@]}"
cmake --build "$build_dir"

echo "=== run ctest (writing raw profiles) ==="
rm -rf "$profraw_dir" "$report_dir"
mkdir -p "$profraw_dir" "$report_dir"
# %p (pid) + %m (binary signature) keeps one distinct profile per test process.
LLVM_PROFILE_FILE="$(to_native "$repo_root/$profraw_dir")/%p-%m.profraw" \
    ctest --test-dir "$build_dir" --output-on-failure

echo "=== collect instrumented test binaries ==="
shopt -s nullglob
test_bins=()
for f in "$build_dir"/tests/render2d_*; do
    case "$f" in
        *.exe) test_bins+=("$f") ;;                              # Windows executable
        *.pdb | *.ilk | *.lib | *.exp | *.manifest) ;;          # Windows build artifacts -> skip
        *) if [ -f "$f" ] && [ -x "$f" ]; then test_bins+=("$f"); fi ;;  # Linux executable
    esac
done
if [ "${#test_bins[@]}" -eq 0 ]; then
    echo "error: no instrumented test binaries found under $build_dir/tests" >&2
    exit 1
fi
echo "  ${#test_bins[@]} binaries"

echo "=== merge raw profiles ==="
profraw_files=("$profraw_dir"/*.profraw)
if [ "${#profraw_files[@]}" -eq 0 ]; then
    echo "error: no .profraw files produced under $profraw_dir" >&2
    exit 1
fi
"$profdata_tool" merge -sparse "${profraw_files[@]}" -o "$profdata"

# llvm-cov takes the first binary positionally and the rest as -object; pass native
# paths and disable Git-bash's argument path mangling so they survive verbatim.
bin0="$(to_native "${test_bins[0]}")"
object_args=()
for ((i = 1; i < ${#test_bins[@]}; i++)); do
    object_args+=(-object "$(to_native "${test_bins[$i]}")")
done
native_include="$(to_native "$repo_root/include/Render2D")"
native_profdata="$(to_native "$profdata")"
native_html="$(to_native "$report_dir/html")"
# Restrict the report to Render2D's own headers (the library under test); the
# positional source filter excludes the test code, third_party, engine deps, and
# system headers. ignore-regex is belt-and-suspenders for any vendored path.
ignore_regex='(third_party|_deps|tests)'

echo "=== coverage summary (include/Render2D) ==="
MSYS2_ARG_CONV_EXCL='*' "$cov_tool" report "$bin0" "${object_args[@]}" \
    -instr-profile="$native_profdata" -ignore-filename-regex="$ignore_regex" \
    "$native_include" | tee "$report_dir/summary.txt"

echo "=== HTML report ==="
MSYS2_ARG_CONV_EXCL='*' "$cov_tool" show "$bin0" "${object_args[@]}" \
    -instr-profile="$native_profdata" -ignore-filename-regex="$ignore_regex" \
    -format=html -output-dir="$native_html" "$native_include"

echo
echo "Coverage summary: $report_dir/summary.txt"
echo "HTML report:      $report_dir/html/index.html"
