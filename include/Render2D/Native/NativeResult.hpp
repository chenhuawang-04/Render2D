#pragma once

#include "Render2D/Native/NativeTypes.hpp"

#include <type_traits>

namespace Render2D {

struct NativeResult {
    NativeStatusCode code;
    NativeObjectKind object_kind;
    NativeId object_id;
    NativeGeneration generation;
};

struct NativeCapacityResult {
    NativeStatusCode code;
    U32 requested_count;
    U32 available_count;
};

inline constexpr NativeResult kNativeOk{
    .code = NativeStatusCode::Ok,
    .object_kind = NativeObjectKind::Unknown,
    .object_id = {.value = 0U},
    .generation = {.value = 0U},
};

static_assert(std::is_trivial_v<NativeResult>);
static_assert(std::is_standard_layout_v<NativeResult>);
static_assert(std::is_trivially_copyable_v<NativeResult>);
static_assert(std::is_aggregate_v<NativeResult>);

static_assert(std::is_trivial_v<NativeCapacityResult>);
static_assert(std::is_standard_layout_v<NativeCapacityResult>);
static_assert(std::is_trivially_copyable_v<NativeCapacityResult>);
static_assert(std::is_aggregate_v<NativeCapacityResult>);

} // namespace Render2D
