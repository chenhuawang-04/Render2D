#pragma once

#include "Render2D/Core/Types.hpp"

#include <type_traits>

namespace Render2D {

enum class SystemStatusCode : U8 {
    Ok = 0,
    InvalidInput = 1,
    InsufficientCapacity = 2,
    UnsupportedDomain = 3,
};

struct SystemResult {
    SystemStatusCode code;
    U32 read_count;
    U32 write_count;
};

inline constexpr Usize kMaxSystemResultCount = static_cast<Usize>(0xFFFFFFFFU);

[[nodiscard]] constexpr bool isSystemResultCountRepresentable(Usize count_) noexcept
{
    return count_ <= kMaxSystemResultCount;
}

static_assert(std::is_trivial_v<SystemResult>);
static_assert(std::is_standard_layout_v<SystemResult>);
static_assert(std::is_trivially_copyable_v<SystemResult>);
static_assert(std::is_aggregate_v<SystemResult>);

} // namespace Render2D
