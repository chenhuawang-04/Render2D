#pragma once

#include <type_traits>

namespace Render2D {

struct VulkanNativeProvider {
};

template<class Provider>
inline constexpr bool kSupportedProvider = false;

template<>
inline constexpr bool kSupportedProvider<VulkanNativeProvider> = true;

template<class Provider>
concept SupportedProviderType =
    kSupportedProvider<std::remove_cvref_t<Provider>>;

} // namespace Render2D
