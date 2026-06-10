#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct Sprite {
    U32 source_id;
    U32 texture_id;
    U32 texture_generation;
    U32 texture_region_id;
    U32 texture_region_generation;
    U32 material_id;
    U32 material_generation;
    U32 color_rgba8;
    U32 layer;
    U32 flags;
};

template<class Provider, class Dim>
struct SpriteVertex {
    float position_x;
    float position_y;
    float uv_x;
    float uv_y;
};

template<class Provider, class Dim>
struct SpriteInstance {
    float transform_m00;
    float transform_m01;
    float transform_m02;
    float transform_m10;
    float transform_m11;
    float transform_m12;
    float uv_min_x;
    float uv_min_y;
    float uv_max_x;
    float uv_max_y;
    U32 source_index;
    U32 source_id;
    U32 texture_id;
    U32 texture_generation;
    U32 material_id;
    U32 material_generation;
    U32 color_rgba8;
    U32 sort_key;
    U32 layer;
    U32 flags;
};

template<class Provider, class Dim>
struct SpriteDrawPacket {
    U32 batch_index;
    U32 draw_first;
    U32 draw_count;
    U32 instance_first;
    U32 instance_count;
    U32 vertex_first;
    U32 vertex_count;
    U32 index_first;
    U32 index_count;
    U32 material_id;
    U32 material_generation;
    U32 texture_id;
    U32 texture_generation;
    U32 pipeline_id;
    U32 pipeline_generation;
    U32 descriptor_id;
    U32 descriptor_generation;
    U32 descriptor_first;
    U32 descriptor_count;
    U32 flags;
};

template<class Provider, class Dim>
struct SpriteMaterialBinding {
    U32 material_id;
    U32 material_generation;
    U32 pipeline_id;
    U32 pipeline_generation;
    U32 flags;
};

template<class Provider, class Dim>
struct SpriteTextureBinding {
    U32 texture_id;
    U32 texture_generation;
    U32 descriptor_id;
    U32 descriptor_generation;
    U32 descriptor_first;
    U32 descriptor_count;
    U32 flags;
};

struct TextureAtlasBuildConfig {
    U32 atlas_width;
    U32 atlas_height;
    U32 atlas_id;
    U32 atlas_generation;
    U32 texture_id;
    U32 texture_generation;
    U32 padding;
    U32 flags;
};

template<class Provider, class Dim>
struct TextureAtlasItem {
    U32 region_id;
    U32 generation;
    U32 width;
    U32 height;
    U32 padding;
    U32 flags;
};

template<class Provider, class Dim>
struct TextureAtlasRegion {
    U32 region_id;
    U32 generation;
    U32 atlas_id;
    U32 atlas_generation;
    U32 texture_id;
    U32 texture_generation;
    U32 x;
    U32 y;
    U32 width;
    U32 height;
    float uv_min_x;
    float uv_min_y;
    float uv_max_x;
    float uv_max_y;
    U32 flags;
};

template<class Provider, class Dim>
struct SpriteInstanceUploadCommand {
    U32 instance_first;
    U32 instance_count;
    U32 destination_buffer_id;
    U32 destination_generation;
    U64 destination_offset;
    U32 frame_index;
    U32 flags;
};

template<class Provider, class Dim>
struct MaterialRef {
    U32 id;
    U32 generation;
    U32 flags;
};

template<class Provider, class Dim>
struct TextureRef {
    U32 id;
    U32 generation;
    U32 flags;
};

template<class Provider, class Dim>
struct RenderLayer {
    U32 value;
};

template<class Provider, class Dim>
struct VisibilityMask {
    U32 mask;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, Sprite<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<Sprite<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, SpriteVertex<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<SpriteVertex<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, SpriteInstance<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<SpriteInstance<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, SpriteDrawPacket<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<SpriteDrawPacket<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, SpriteMaterialBinding<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<SpriteMaterialBinding<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, SpriteTextureBinding<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<SpriteTextureBinding<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, TextureAtlasItem<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<TextureAtlasItem<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, TextureAtlasRegion<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<TextureAtlasRegion<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, SpriteInstanceUploadCommand<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<SpriteInstanceUploadCommand<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, MaterialRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<MaterialRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, TextureRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<TextureRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, RenderLayer<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<RenderLayer<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, VisibilityMask<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<VisibilityMask<Provider, Dim>>;
};

} // namespace Render2D
