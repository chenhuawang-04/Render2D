#include <Render2D/Render2D.hpp>

#include <type_traits>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;

template<class Component>
consteval void requireNativeComponent()
{
    static_assert(R2D::StrictPodComponent<Component>);
    static_assert(R2D::SupportedRenderComponent<Provider, Dim, Component>);
    static_assert(std::is_trivial_v<Component>);
    static_assert(std::is_standard_layout_v<Component>);
    static_assert(std::is_trivially_copyable_v<Component>);
    static_assert(std::is_aggregate_v<Component>);
}

int main()
{
    requireNativeComponent<R2D::DeviceHandle<Provider, Dim>>();
    requireNativeComponent<R2D::QueueHandle<Provider, Dim>>();
    requireNativeComponent<R2D::SwapchainState<Provider, Dim>>();
    requireNativeComponent<R2D::FrameSync<Provider, Dim>>();
    requireNativeComponent<R2D::NativeCommandBufferRef<Provider, Dim>>();
    requireNativeComponent<R2D::PipelineRef<Provider, Dim>>();
    requireNativeComponent<R2D::ImageRef<Provider, Dim>>();
    requireNativeComponent<R2D::BufferRef<Provider, Dim>>();
    requireNativeComponent<R2D::UploadSlice<Provider, Dim>>();

    static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::DescriptorSlice<Provider, Dim>>);
    static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::UploadRingSlice<Provider, Dim>>);
    static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FenceState<Provider, Dim>>);

    static_assert(!R2D::SupportedRenderComponent<int, Dim, R2D::BufferRef<int, Dim>>);
    static_assert(!R2D::SupportedRenderComponent<Provider, int, R2D::BufferRef<Provider, int>>);

    constexpr R2D::BufferRef<Provider, Dim> kBuffer{
        .handle = 0x1234U,
        .byte_size = 4096U,
        .buffer_id = 1U,
        .generation = 2U,
        .usage_flags = 3U,
        .memory_domain = 4U,
    };
    static_assert(kBuffer.byte_size == 4096U);

    return 0;
}
