#pragma once

#include <fast_math/aabb2.h>
#include <fast_math/common.h>
#include <fast_math/mat3.h>
#include <fast_math/trig.h>
#include <fast_math/vec2.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Render2D {

using U8 = std::uint8_t;
using U16 = std::uint16_t;
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using I8 = std::int8_t;
using I16 = std::int16_t;
using I32 = std::int32_t;
using I64 = std::int64_t;
using Usize = std::size_t;

struct RangeU32 {
    U32 first;
    U32 count;
};

struct RangeU64 {
    U64 first;
    U64 count;
};

using Vec2 = MMath::Vec2;
using Mat3 = MMath::Mat3;
using Aabb2 = MMath::Aabb2;

[[nodiscard]] inline Vec2 makeVec2(float x_, float y_) noexcept
{
    return {.x = x_, .y = y_};
}

[[nodiscard]] inline Aabb2 makeAabb2(
    float min_x_,
    float min_y_,
    float max_x_,
    float max_y_) noexcept
{
    return MMath::aabb2FromMinMax(makeVec2(min_x_, min_y_), makeVec2(max_x_, max_y_));
}

[[nodiscard]] inline Aabb2 makeAabb2FromCenterExtents(
    Vec2 center_,
    Vec2 extents_) noexcept
{
    return MMath::aabb2FromCenterExtents(center_, extents_);
}

[[nodiscard]] inline Vec2 aabb2Min(Aabb2 bounds_) noexcept
{
    return MMath::aabb2Min(bounds_);
}

[[nodiscard]] inline Vec2 aabb2Max(Aabb2 bounds_) noexcept
{
    return MMath::aabb2Max(bounds_);
}

[[nodiscard]] inline bool aabb2Intersects(
    const Aabb2& left_,
    const Aabb2& right_) noexcept
{
    return MMath::aabb2Intersects(left_, right_);
}

[[nodiscard]] inline bool aabb2NearEqual(
    const Aabb2& left_,
    const Aabb2& right_,
    float epsilon_) noexcept
{
    return MMath::aabb2NearEqual(left_, right_, epsilon_);
}

static_assert(std::is_trivial_v<RangeU32>);
static_assert(std::is_standard_layout_v<RangeU32>);
static_assert(std::is_trivially_copyable_v<RangeU32>);
static_assert(std::is_aggregate_v<RangeU32>);

static_assert(std::is_trivial_v<RangeU64>);
static_assert(std::is_standard_layout_v<RangeU64>);
static_assert(std::is_trivially_copyable_v<RangeU64>);
static_assert(std::is_aggregate_v<RangeU64>);

static_assert(std::is_trivial_v<Aabb2>);
static_assert(std::is_standard_layout_v<Aabb2>);
static_assert(std::is_trivially_copyable_v<Aabb2>);
static_assert(std::is_aggregate_v<Aabb2>);

static_assert(std::is_trivial_v<Vec2>);
static_assert(std::is_standard_layout_v<Vec2>);
static_assert(std::is_trivially_copyable_v<Vec2>);
static_assert(std::is_aggregate_v<Vec2>);

static_assert(std::is_trivial_v<Mat3>);
static_assert(std::is_standard_layout_v<Mat3>);
static_assert(std::is_trivially_copyable_v<Mat3>);
static_assert(std::is_aggregate_v<Mat3>);

} // namespace Render2D
