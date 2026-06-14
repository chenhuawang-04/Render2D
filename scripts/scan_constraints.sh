#!/usr/bin/env bash
#
# scan_constraints.sh -- Render2D architecture constraint scanner (Stage 17D).
#
# Fails (nonzero exit) if any Render2D-owned source under include/, src/,
# tests/, or bench/ violates a non-negotiable invariant:
#
#   1. std::vector              -> use Render2D::McVector<T>
#   2. direct Vulkan memory API -> route through VulkanMemoryCenterAllocator
#   3. local math structs       -> use the MMath aliases (Vec2/Mat3/Aabb2)
#
# The vendored third_party/ trees (FreeType/HarfBuzz/SheenBidi) and the build
# directories are NEVER scanned -- the invariants apply only to Render2D's own
# source. Codifies the manual std::vector / Vulkan-memory / old-math scans that
# were run by hand from Stage 10K onward.
#
# Portable: POSIX grep only (GNU grep features --include and -E). Runs on Linux
# CI runners and on Windows under Git-bash. Invoke as:
#     bash scripts/scan_constraints.sh

set -u

# Resolve the repo root from this script's own location so it runs from any CWD.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
cd "${repo_root}" || exit 2

# Only Render2D-owned source roots that exist. third_party/ stays excluded.
roots=()
for candidate_root in include src tests bench; do
    [ -d "${candidate_root}" ] && roots+=("${candidate_root}")
done
if [ "${#roots[@]}" -eq 0 ]; then
    echo "scan_constraints: no source roots found under ${repo_root}" >&2
    exit 2
fi

includes=(--include='*.hpp' --include='*.cpp' --include='*.h' --include='*.inl' --include='*.c')

violations=0

# scan <label> <extended-regex> <fix-hint>
scan() {
    local label="$1"
    local pattern="$2"
    local hint="$3"
    local matches
    # grep exit 0 => a match exists => an invariant was violated.
    matches="$(grep -rnE "${includes[@]}" -e "${pattern}" "${roots[@]}" 2>/dev/null)"
    if [ -n "${matches}" ]; then
        local count
        count="$(printf '%s\n' "${matches}" | wc -l | tr -cd '0-9')"
        echo "FAIL  ${label}  (${count} match(es))"
        echo "      fix: ${hint}"
        printf '%s\n' "${matches}" | sed 's/^/        /'
        violations=$((violations + count))
    else
        echo "ok    ${label}"
    fi
}

echo "Render2D constraint scan over: ${roots[*]}"
echo

scan "no std::vector" \
    'std::vector' \
    'use Render2D::McVector<T>'

scan "no direct Vulkan memory API" \
    'vk(Allocate|Free)Memory|vk(Map|Unmap)Memory|vkBind(Buffer|Image)Memory' \
    'route allocations through VulkanMemoryCenterAllocator'

scan "no Render2D-local math structs" \
    '(struct|class|union)[[:space:]]+(Vec2|Vec3|Vec4|Mat3|Mat4|Aabb2)\b|\bAffine2X3\b' \
    'use Render2D::Vec2/Mat3/Aabb2 (= MMath) with makeAabb2/aabb2Min/aabb2Max'

echo
if [ "${violations}" -gt 0 ]; then
    echo "constraint scan FAILED: ${violations} violation(s)"
    exit 1
fi
echo "constraint scan passed"
exit 0
