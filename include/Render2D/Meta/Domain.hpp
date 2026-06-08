#pragma once

#include "Render2D/Meta/Dim.hpp"
#include "Render2D/Meta/Provider.hpp"

#include <type_traits>

namespace Render2D {

template<class Provider, class Dim>
inline constexpr bool kSupportedDomain =
    kSupportedProvider<std::remove_cvref_t<Provider>> &&
    kSupportedDim<std::remove_cvref_t<Dim>>;

template<class Provider, class Dim>
concept SupportedRenderDomain = kSupportedDomain<Provider, Dim>;

template<class Provider, class Dim>
consteval void requireSupportedDomain()
{
    static_assert(kSupportedProvider<std::remove_cvref_t<Provider>>,
        "Render2D currently supports only VulkanNativeProvider.");
    static_assert(kSupportedDim<std::remove_cvref_t<Dim>>,
        "Render2D currently supports only Dim2.");
}

} // namespace Render2D
