#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct DeviceHandle {
    U64 handle;
    U32 device_id;
    U32 generation;
};

template<class Provider, class Dim>
struct QueueHandle {
    U64 handle;
    U32 queue_id;
    U32 generation;
    U32 queue_family_index;
    U32 queue_index;
    U32 flags;
};

template<class Provider, class Dim>
struct SwapchainState {
    U64 handle;
    U32 swapchain_id;
    U32 image_first;
    U32 image_count;
    U32 width;
    U32 height;
    U32 format;
    U32 generation;
    U32 flags;
};

template<class Provider, class Dim>
struct SwapchainImageRef {
    U64 image_handle;
    U64 image_view_handle;
    U32 swapchain_id;
    U32 image_index;
    U32 width;
    U32 height;
    U32 format;
    U32 generation;
    U32 flags;
};

template<class Provider, class Dim>
struct AcquiredImage {
    U32 swapchain_id;
    U32 image_index;
    U32 frame_index;
    U32 sync_id;
    U32 sync_generation;
    U32 generation;
    U32 flags;
};

template<class Provider, class Dim>
struct PresentCommand {
    U32 swapchain_id;
    U32 image_index;
    U32 wait_sync_id;
    U32 wait_sync_generation;
    U32 frame_index;
    U32 generation;
    U32 flags;
};

template<class Provider, class Dim>
struct FrameSync {
    U32 frame_index;
    U32 image_available_semaphore_id;
    U32 render_finished_semaphore_id;
    U32 in_flight_fence_id;
    U32 flags;
    U32 sync_id;
    U32 generation;
};

template<class Provider, class Dim>
struct DeferredDestroyCommand {
    U64 handle;
    U64 aux_handle;
    U32 object_kind;
    U32 object_id;
    U32 generation;
    U32 retire_frame_index;
    U32 flags;
    U32 reserved;
};

template<class Provider, class Dim>
struct NativeCommandBufferRef {
    U32 command_buffer_id;
    U32 generation;
    U32 frame_index;
    U32 batch_first;
    U32 batch_count;
    U32 upload_first;
    U32 upload_count;
    U32 flags;
};

template<class Provider, class Dim>
struct PipelineRef {
    U64 handle;
    U64 layout_handle;
    U32 pipeline_id;
    U32 generation;
    U32 flags;
};

template<class Provider, class Dim>
struct ImageRef {
    U64 image_handle;
    U64 image_view_handle;
    U32 image_id;
    U32 width;
    U32 height;
    U32 format;
    U32 generation;
    U32 usage_flags;
};

template<class Provider, class Dim>
struct BufferRef {
    U64 handle;
    U64 byte_size;
    U32 buffer_id;
    U32 generation;
    U32 usage_flags;
    U32 memory_domain;
};

template<class Provider, class Dim>
struct UploadSlice {
    U64 source_offset;
    U64 destination_offset;
    U64 byte_count;
    U32 upload_id;
    U32 resource_id;
    U32 frame_index;
    U32 flags;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, DeviceHandle<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<DeviceHandle<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, QueueHandle<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<QueueHandle<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, SwapchainState<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<SwapchainState<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, SwapchainImageRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<SwapchainImageRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, AcquiredImage<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<AcquiredImage<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, PresentCommand<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<PresentCommand<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, FrameSync<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<FrameSync<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, DeferredDestroyCommand<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<DeferredDestroyCommand<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, NativeCommandBufferRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<NativeCommandBufferRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, PipelineRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<PipelineRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, ImageRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<ImageRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, BufferRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<BufferRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, UploadSlice<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<UploadSlice<Provider, Dim>>;
};

} // namespace Render2D
