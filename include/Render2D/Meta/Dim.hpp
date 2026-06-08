#pragma once

#include <type_traits>

namespace Render2D {

struct Dim2 {
};

template<class Dim>
inline constexpr bool kSupportedDim = false;

template<>
inline constexpr bool kSupportedDim<Dim2> = true;

template<class Dim>
concept SupportedDimType =
    kSupportedDim<std::remove_cvref_t<Dim>>;

} // namespace Render2D
