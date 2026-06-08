#pragma once

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

struct Aabb2 {
    float min_x;
    float min_y;
    float max_x;
    float max_y;
};

struct Affine2X3 {
    float m00;
    float m01;
    float m02;
    float m10;
    float m11;
    float m12;
};

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

static_assert(std::is_trivial_v<Affine2X3>);
static_assert(std::is_standard_layout_v<Affine2X3>);
static_assert(std::is_trivially_copyable_v<Affine2X3>);
static_assert(std::is_aggregate_v<Affine2X3>);

} // namespace Render2D
