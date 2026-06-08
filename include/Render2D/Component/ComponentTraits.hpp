#pragma once

#include "Render2D/Component/ComponentFwd.hpp"
#include "Render2D/Component/StrictPod.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <type_traits>

namespace Render2D {

template<class Provider, class Dim, class Component>
struct ComponentTraits {
    static constexpr bool kSupported = false;
};

template<class Provider, class Dim, class Component>
inline constexpr bool kSupportedComponent =
    ComponentTraits<Provider, Dim, std::remove_cvref_t<Component>>::kSupported;

template<class Provider, class Dim, class Component>
concept SupportedRenderComponent =
    SupportedRenderDomain<Provider, Dim> &&
    StrictPodComponent<std::remove_cvref_t<Component>> &&
    kSupportedComponent<Provider, Dim, Component>;

template<class Provider, class Dim, class Component>
consteval void requireSupportedRenderComponent()
{
    static_assert(SupportedRenderDomain<Provider, Dim>,
        "Render2D component requires a supported Provider/Dim domain.");
    static_assert(StrictPodComponent<std::remove_cvref_t<Component>>,
        "Render2D component must satisfy StrictPodComponent.");
    static_assert(kSupportedComponent<Provider, Dim, Component>,
        "Unsupported Render2D component for this Provider/Dim domain.");
}

} // namespace Render2D
