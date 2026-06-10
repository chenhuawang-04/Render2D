#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct VisibleItem {
    U32 source_index;
    U32 layer;
    U32 sort_key;
    U32 flags;
};

template<class Provider, class Dim>
struct SortedItem {
    U32 visible_index;
    U32 sort_key;
};

template<class Provider, class Dim>
struct DrawCommand {
    U32 source_index;
    U32 material_id;
    U32 material_generation;
    U32 texture_id;
    U32 texture_generation;
    U32 vertex_first;
    U32 vertex_count;
    U32 index_first;
    U32 index_count;
    U32 instance_first;
    U32 instance_count;
    U32 sort_key;
    U32 layer;
    U32 flags;
};

template<class Provider, class Dim>
struct CommandBuffer {
    U32 frame_index;
    U32 draw_first;
    U32 draw_count;
    U32 batch_first;
    U32 batch_count;
    U32 upload_first;
    U32 upload_count;
    U32 native_submit_first;
    U32 native_submit_count;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, VisibleItem<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<VisibleItem<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, SortedItem<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<SortedItem<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, DrawCommand<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<DrawCommand<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, CommandBuffer<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<CommandBuffer<Provider, Dim>>;
};

} // namespace Render2D
