#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct LocalBounds {
    U32 source_id;
    Aabb2 bounds;
};

template<class Provider, class Dim>
struct WorldBounds {
    U32 source_id;
    Aabb2 bounds;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, LocalBounds<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<LocalBounds<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, WorldBounds<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<WorldBounds<Provider, Dim>>;
};

} // namespace Render2D
