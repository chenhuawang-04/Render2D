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
struct Utf8Slice {
    U32 buffer_id;
    U32 byte_offset;
    U32 byte_count;
};

template<class Provider, class Dim>
struct FontRef {
    U32 id;
};

template<class Provider, class Dim>
struct FontAtlasRef {
    U32 font_id;
    U32 atlas_id;
    U32 generation;
    U32 texture_id;
    U32 flags;
};

template<class Provider, class Dim>
struct GlyphRun {
    U32 source_text_index;
    U32 glyph_first;
    U32 glyph_count;
    U32 atlas_id;
    U32 atlas_generation;
    U32 flags;
};

template<class Provider, class Dim>
struct GlyphInstance {
    U32 glyph_run_index;
    U32 glyph_id;
    Aabb2 atlas_rect;
    float position_x;
    float position_y;
    U32 color_rgba8;
    U32 sort_key;
    U32 layer;
    U32 flags;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, Text<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<Text<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, Utf8Slice<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<Utf8Slice<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, FontRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<FontRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, FontAtlasRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<FontAtlasRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, GlyphRun<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<GlyphRun<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, GlyphInstance<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<GlyphInstance<Provider, Dim>>;
};

} // namespace Render2D
