#pragma once

#include <type_traits>

namespace Render2D {

template<class T>
concept StrictPodComponent =
    std::is_trivial_v<std::remove_cvref_t<T>> &&
    std::is_standard_layout_v<std::remove_cvref_t<T>> &&
    std::is_trivially_copyable_v<std::remove_cvref_t<T>> &&
    std::is_aggregate_v<std::remove_cvref_t<T>>;

template<class T>
inline constexpr bool kStrictPodComponentValue = StrictPodComponent<T>;

template<class T>
consteval void requireStrictPodComponent()
{
    using Type = std::remove_cvref_t<T>;
    static_assert(std::is_trivial_v<Type>,
        "Render2D component must be trivial.");
    static_assert(std::is_standard_layout_v<Type>,
        "Render2D component must have standard layout.");
    static_assert(std::is_trivially_copyable_v<Type>,
        "Render2D component must be trivially copyable.");
    static_assert(std::is_aggregate_v<Type>,
        "Render2D component must be an aggregate.");
}

} // namespace Render2D
