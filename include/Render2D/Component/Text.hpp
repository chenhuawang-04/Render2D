#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct Text {
    U32 source_id;
    U32 font_id;
    U32 utf8_buffer_id;
    U32 utf8_offset;
    U32 utf8_size;
    U32 color_rgba8;
    float pixel_size;
    U32 layer;
    U32 flags;
};

template<class Provider, class Dim>
struct FontRef {
    U32 id;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, Text<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<Text<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, FontRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<FontRef<Provider, Dim>>;
};

} // namespace Render2D
