#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct Transform {
    U32 source_id;
    float position_x;
    float position_y;
    float rotation_radians;
    float scale_x;
    float scale_y;
};

template<class Provider, class Dim>
struct WorldTransform {
    U32 source_id;
    Affine2X3 affine;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, Transform<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<Transform<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, WorldTransform<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<WorldTransform<Provider, Dim>>;
};

} // namespace Render2D
