#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

inline constexpr U32 kTextDirectionLtr = 0U;
inline constexpr U32 kTextDirectionRtl = 1U;

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
struct TextState {
    U32 source_id;
    U32 font_id;
    U32 utf8_buffer_id;
    U32 utf8_offset;
    U32 utf8_size;
    U32 color_rgba8;
    float pixel_size;
    U32 layer;
    U32 flags;
    U32 glyph_first;
    U32 glyph_count;
};

template<class Provider, class Dim>
struct TextDirtyRange {
    U32 source_text_index;
    U32 previous_glyph_first;
    U32 previous_glyph_count;
    U32 new_glyph_first;
    U32 new_glyph_count;
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
    U32 generation;
    U32 flags;
};

template<class Provider, class Dim>
struct FontAtlasRef {
    U32 font_id;
    U32 atlas_id;
    U32 generation;
    U32 texture_id;
    U32 texture_generation;
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
    float width;
    float height;
    U32 color_rgba8;
    U32 sort_key;
    U32 layer;
    U32 flags;
};

template<class Provider, class Dim>
struct Codepoint {
    U32 text_index;
    U32 codepoint;
    U32 byte_offset;
    U32 flags;
};

template<class Provider, class Dim>
struct ShapingRun {
    U32 text_index;
    U32 codepoint_first;
    U32 codepoint_count;
    U32 font_id;
    U32 script;
    U32 direction;
    U32 bidi_level;
    U32 flags;
};

template<class Provider, class Dim>
struct ShapedGlyph {
    U32 run_index;
    U32 glyph_id;
    U32 cluster;
    float x_advance;
    float y_advance;
    float x_offset;
    float y_offset;
    U32 flags;
};

template<class Provider, class Dim>
struct GlyphAtlasEntry {
    U32 font_id;
    U32 generation;
    U32 glyph_id;
    float pixel_size;
    U32 atlas_id;
    U32 atlas_generation;
    Aabb2 atlas_rect;
    float bitmap_width;
    float bitmap_height;
    float bearing_x;
    float bearing_y;
    U32 flags;
};

template<class Provider, class Dim>
struct FontMetrics {
    U32 font_id;
    U32 generation;
    float pixel_size;
    float ascent;
    float descent;
    float line_height;
    U32 flags;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, Text<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<Text<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, TextState<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<TextState<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, TextDirtyRange<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<TextDirtyRange<Provider, Dim>>;
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

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, Codepoint<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<Codepoint<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, ShapingRun<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<ShapingRun<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, ShapedGlyph<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<ShapedGlyph<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, GlyphAtlasEntry<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<GlyphAtlasEntry<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, FontMetrics<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<FontMetrics<Provider, Dim>>;
};

} // namespace Render2D
