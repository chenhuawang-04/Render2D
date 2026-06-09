#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct FrameIndex {
    U32 value;
};

template<class Provider, class Dim>
struct FrameArenaState {
    U64 capacity_bytes;
    U64 used_bytes;
    U32 generation;
    U32 flags;
};

template<class Provider, class Dim>
struct UploadRingSlice {
    U64 offset;
    U64 byte_count;
    U32 ring_id;
    U32 frame_index;
    U32 generation;
};

template<class Provider, class Dim>
struct DescriptorSlice {
    U32 descriptor_set_id;
    U32 first;
    U32 count;
    U32 generation;
};

template<class Provider, class Dim>
struct FenceState {
    U32 fence_id;
    U32 frame_index;
    U32 signaled;
    U32 flags;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, FrameIndex<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<FrameIndex<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, FrameArenaState<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<FrameArenaState<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, UploadRingSlice<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<UploadRingSlice<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, DescriptorSlice<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<DescriptorSlice<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, FenceState<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<FenceState<Provider, Dim>>;
};

} // namespace Render2D
