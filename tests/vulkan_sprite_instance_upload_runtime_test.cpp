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
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpriteInstanceUploadCommand = R2D::SpriteInstanceUploadCommand<Provider, Dim>;
using SpriteUploadRuntime = R2D::VulkanSpriteInstanceUploadRuntime<Provider, Dim>;
using UploadRingRuntime = R2D::VulkanUploadRingRuntime<Provider, Dim>;
using UploadRingSlice = R2D::UploadRingSlice<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;

static_assert(!R2D::SupportedRenderComponent<Provider, Dim, SpriteUploadRuntime>);

constexpr SpriteInstance makeInstance(R2D::U32 source_index_) noexcept
{
    return {
        .transform_m00 = 1.0F + static_cast<float>(source_index_),
        .transform_m01 = 0.0F,
        .transform_m02 = 2.0F + static_cast<float>(source_index_),
        .transform_m10 = 0.0F,
        .transform_m11 = 1.0F,
        .transform_m12 = 3.0F + static_cast<float>(source_index_),
        .uv_min_x = 0.0F,
        .uv_min_y = 0.0F,
        .uv_max_x = 1.0F,
        .uv_max_y = 1.0F,
        .source_index = source_index_,
        .source_id = source_index_ + 10U,
        .texture_id = source_index_ + 20U,
        .material_id = source_index_ + 30U,
        .color_rgba8 = 0xFF000000U | source_index_,
        .sort_key = source_index_ + 40U,
        .layer = source_index_ + 1U,
        .flags = source_index_,
    };
}

void testSpriteInstanceUpload(const Render2DTest::VulkanSmokeContext& context_)
{
    constexpr std::array<SpriteInstance, 3U> kInstances{{
        makeInstance(0U),
        makeInstance(1U),
        makeInstance(2U),
    }};
    constexpr R2D::U64 kDestinationOffset = 64U;
    constexpr R2D::U64 kUploadByteCount = 2U * sizeof(SpriteInstance);
    constexpr R2D::U64 kDestinationByteCount = kDestinationOffset + kUploadByteCount;

    UploadRingRuntime upload_ring_runtime;
    auto result = upload_ring_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
        .byte_capacity = 512U,
        .frame_count = 1U,
        .usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);

    ResourceRuntime resource_runtime;
    result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    auto capacity = resource_runtime.reserveBuffers(2U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    BufferRef device_buffer{};
    result = resource_runtime.createBufferRef(
        kDestinationByteCount,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        R2D::NativeMemoryDomain::DeviceLocal,
        device_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef readback_buffer{};
    result = resource_runtime.createBufferRef(
        kDestinationByteCount,
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

    result = upload_ring_runtime.beginFrame(0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = command_runtime.beginCommandBuffer(command_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = command_runtime.resolveNativeCommandBuffer(command_ref, command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    const SpriteInstanceUploadCommand upload_command{
        .instance_first = 1U,
        .instance_count = 2U,
        .destination_buffer_id = device_buffer.buffer_id,
        .destination_generation = device_buffer.generation,
        .destination_offset = kDestinationOffset,
        .frame_index = 0U,
        .flags = 0U,
    };
    UploadRingSlice upload_slice{};
    result = SpriteUploadRuntime::recordUpload(
        upload_command,
        kInstances,
        device_buffer,
        upload_ring_runtime,
        resource_runtime,
        command_buffer,
        upload_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(upload_slice.byte_count == kUploadByteCount);

    result = resource_runtime.recordBufferBarrier(
        command_buffer,
        device_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyBuffer(command_buffer, device_buffer, readback_buffer, kDestinationByteCount);
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

    std::array<SpriteInstance, 2U> copied_instances{};
    result = resource_runtime.readBuffer(
        readback_buffer,
        copied_instances.data(),
        kUploadByteCount,
        kDestinationOffset);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(copied_instances[0U].source_index == kInstances[1U].source_index);
    assert(copied_instances[0U].source_id == kInstances[1U].source_id);
    assert(copied_instances[0U].texture_id == kInstances[1U].texture_id);
    assert(copied_instances[0U].transform_m02 == kInstances[1U].transform_m02);
    assert(copied_instances[1U].source_index == kInstances[2U].source_index);
    assert(copied_instances[1U].source_id == kInstances[2U].source_id);
    assert(copied_instances[1U].color_rgba8 == kInstances[2U].color_rgba8);
    assert(copied_instances[1U].transform_m12 == kInstances[2U].transform_m12);

    result = upload_ring_runtime.completeFrame(0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = command_runtime.releaseCommandBufferRef(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.releaseFrameSync(frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(device_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

int main()
{
    try {
        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testSpriteInstanceUpload(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
