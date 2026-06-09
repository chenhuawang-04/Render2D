#include <Render2D/Render2D.hpp>

#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using UploadRingSlice = R2D::UploadRingSlice<Provider, Dim>;
using UploadRingRuntime = R2D::VulkanUploadRingRuntime<Provider, Dim>;

static_assert(!R2D::SupportedRenderComponent<Provider, Dim, UploadRingRuntime>);
static_assert(R2D::StrictPodComponent<R2D::VulkanUploadRingRuntimeConfig>);
static_assert(R2D::StrictPodComponent<UploadRingSlice>);

void testInvalidConfig()
{
    UploadRingRuntime runtime;
    auto result = runtime.initialize({
        .physical_device = VK_NULL_HANDLE,
        .device = VK_NULL_HANDLE,
        .byte_capacity = 0U,
        .frame_count = 0U,
        .usage_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(!runtime.isInitialized());
}

void testUploadRingLifecycle(const Render2DTest::VulkanSmokeContext& context_)
{
    UploadRingRuntime runtime;
    auto result = runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
        .byte_capacity = 256U,
        .frame_count = 2U,
        .usage_flags = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.isInitialized());
    assert(runtime.nativeBuffer() != VK_NULL_HANDLE);
    assert(runtime.capacityBytes() == 256U);
    assert(runtime.frameSegmentBytes() == 128U);
    assert(runtime.framesInFlight() == 2U);

    result = runtime.beginFrame(0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    UploadRingSlice first_slice{};
    result = runtime.allocateSlice(0U, sizeof(VkDrawIndirectCommand), 4U, first_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(first_slice.ring_id == 0U);
    assert(first_slice.frame_index == 0U);
    assert(first_slice.generation == 1U);
    assert(first_slice.offset == 0U);

    const VkDrawIndirectCommand draw_command{
        .vertexCount = 3U,
        .instanceCount = 1U,
        .firstVertex = 0U,
        .firstInstance = 0U,
    };
    result = runtime.writeSlice(first_slice, &draw_command, sizeof(draw_command), 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    VkBuffer native_buffer = VK_NULL_HANDLE;
    R2D::U64 native_offset = 0U;
    result = runtime.resolveNativeBuffer(first_slice, native_buffer, native_offset);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(native_buffer != VK_NULL_HANDLE);
    assert(native_offset == first_slice.offset);

    result = runtime.beginFrame(2U);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);

    result = runtime.completeFrame(0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.resolveNativeBuffer(first_slice, native_buffer, native_offset);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    result = runtime.beginFrame(2U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    UploadRingSlice reused_slice{};
    result = runtime.allocateSlice(2U, sizeof(VkDrawIndirectCommand), 4U, reused_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_slice.ring_id == first_slice.ring_id);
    assert(reused_slice.generation == first_slice.generation + 1U);

    result = runtime.completeFrame(2U);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

int main()
{
    try {
        testInvalidConfig();

        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testUploadRingLifecycle(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
