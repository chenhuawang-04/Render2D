#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct BatchCommand {
    U32 draw_first;
    U32 draw_count;
    U32 material_id;
    U32 material_generation;
    U32 texture_id;
    U32 texture_generation;
    U32 pipeline_id;
    U32 pipeline_generation;
    U32 descriptor_id;
    U32 descriptor_generation;
    U32 sort_key;
    U32 flags;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, BatchCommand<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<BatchCommand<Provider, Dim>>;
};

} // namespace Render2D
