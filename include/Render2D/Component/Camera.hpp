#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct Camera {
    U32 source_id;
    float position_x;
    float position_y;
    float rotation_radians;
    float viewport_width;
    float viewport_height;
    float near_z;
    float far_z;
    U32 layer_mask;
    U32 flags;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, Camera<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<Camera<Provider, Dim>>;
};

} // namespace Render2D
