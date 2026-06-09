#include <Render2D/Render2D.hpp>

#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<ResourceRuntime>);
static_assert(R2D::StrictPodComponent<R2D::VulkanResourceRuntimeConfig>);
static_assert(R2D::StrictPodComponent<BufferRef>);
static_assert(R2D::StrictPodComponent<ImageRef>);

void testInvalidConfig()
{
    ResourceRuntime runtime;
    const auto result = runtime.initialize({
        .physical_device = VK_NULL_HANDLE,
        .device = VK_NULL_HANDLE,
    });
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(!runtime.isInitialized());
}

void testBufferAndImageLifecycle(const Render2DTest::VulkanSmokeContext& context_)
{
    ResourceRuntime resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.isInitialized());

    auto capacity = resource_runtime.reserveBuffers(3U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.bufferCapacity() >= 3U);
    capacity = resource_runtime.reserveImages(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.imageCapacity() >= 1U);

    constexpr R2D::U64 kByteCount = 16U;
    const std::array<R2D::U8, static_cast<R2D::Usize>(kByteCount)> source_bytes{
        0x01U,
        0x23U,
        0x45U,
        0x67U,
        0x89U,
        0xABU,
        0xCDU,
        0xEFU,
        0x10U,
        0x32U,
        0x54U,
        0x76U,
        0x98U,
        0xBAU,
        0xDCU,
        0xFEU,
    };

    BufferRef upload_buffer{};
    result = resource_runtime.createBufferRef(
        kByteCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        R2D::NativeMemoryDomain::Upload,
        upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(upload_buffer.buffer_id == 0U);
    assert(upload_buffer.generation == 1U);
    assert(resource_runtime.bufferCount() == 1U);

    result = resource_runtime.writeBuffer(upload_buffer, source_bytes.data(), kByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<R2D::U8, static_cast<R2D::Usize>(kByteCount)> local_readback{};
    result = resource_runtime.readBuffer(upload_buffer, local_readback.data(), kByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(local_readback == source_bytes);

    BufferRef device_buffer{};
    result = resource_runtime.createBufferRef(
        kByteCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        R2D::NativeMemoryDomain::DeviceLocal,
        device_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef readback_buffer{};
    result = resource_runtime.createBufferRef(
        kByteCount,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        R2D::NativeMemoryDomain::Readback,
        readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanCommandRuntime<Provider, Dim> command_runtime;
    result = command_runtime.initialize({
        .device = context_.device,
        .queue_family_index = context_.queue_family_index,
        .command_pool_flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = command_runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    NativeCommandBufferRef command_ref{};
    result = command_runtime.allocateCommandBufferRef(
        0U,
        {.first = 0U, .count = 0U},
        {.first = 0U, .count = 0U},
        0U,
        command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = command_runtime.beginCommandBuffer(command_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = command_runtime.resolveNativeCommandBuffer(command_ref, command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(command_buffer != VK_NULL_HANDLE);

    result = resource_runtime.recordCopyBuffer(command_buffer, upload_buffer, device_buffer, kByteCount);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordBufferBarrier(
        command_buffer,
        device_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyBuffer(command_buffer, device_buffer, readback_buffer, kByteCount);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = command_runtime.endCommandBuffer(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSyncRuntime<Provider, Dim> sync_runtime;
    result = sync_runtime.initialize({
        .device = context_.device,
        .fence_create_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = sync_runtime.reserveFrameSyncs(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    result = sync_runtime.createFrameSync(0U, 0U, frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSubmitRuntime<Provider, Dim> submit_runtime;
    result = submit_runtime.initialize({
        .queue = context_.queue,
        .wait_stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = submit_runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
    result = submit_runtime.submit(command_refs, frame_sync, command_runtime, sync_runtime, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.waitFence(frame_sync, 1'000'000'000ULL);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<R2D::U8, static_cast<R2D::Usize>(kByteCount)> copied_bytes{};
    result = resource_runtime.readBuffer(readback_buffer, copied_bytes.data(), kByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(copied_bytes == source_bytes);

    ImageRef color_image{};
    result = resource_runtime.createImageRef(
        4U,
        4U,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        color_image);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(color_image.image_id == 0U);
    assert(resource_runtime.imageCount() == 1U);

    VkImage native_image = VK_NULL_HANDLE;
    VkImageView native_view = VK_NULL_HANDLE;
    result = resource_runtime.resolveNativeImage(color_image, native_image, native_view);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(native_image != VK_NULL_HANDLE);
    assert(native_view != VK_NULL_HANDLE);

    result = resource_runtime.releaseImageRef(color_image);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.imageCount() == 0U);
    result = resource_runtime.resolveNativeImage(color_image, native_image, native_view);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    result = command_runtime.releaseCommandBufferRef(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.releaseFrameSync(frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = resource_runtime.releaseBufferRef(upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(device_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.bufferCount() == 0U);

    VkBuffer stale_native_buffer = VK_NULL_HANDLE;
    result = resource_runtime.resolveNativeBuffer(upload_buffer, stale_native_buffer);
    assert(result.code == R2D::NativeStatusCode::StaleReference);
    assert(stale_native_buffer == VK_NULL_HANDLE);
}

int main()
{
    try {
        testInvalidConfig();

        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testBufferAndImageLifecycle(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
