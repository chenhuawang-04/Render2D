#include <Render2D/Render2D.hpp>

#include "support/FullScreenTriangleShaders.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using UploadRingSlice = R2D::UploadRingSlice<Provider, Dim>;

static_assert(!R2D::SupportedRenderComponent<Provider, Dim, R2D::VulkanDynamicRenderEncoder<Provider, Dim>>);
static_assert(R2D::StrictPodComponent<R2D::VulkanDynamicRenderEncoderConfig>);

void testOffscreenMagentaSprite(const Render2DTest::VulkanSmokeContext& context_)
{
    constexpr R2D::U32 kWidth = 4U;
    constexpr R2D::U32 kHeight = 4U;
    constexpr R2D::U64 kReadbackByteCount = static_cast<R2D::U64>(kWidth) * kHeight * 4U;

    R2D::VulkanResourceRuntime<Provider, Dim> resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    auto capacity = resource_runtime.reserveImages(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    capacity = resource_runtime.reserveBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    ImageRef color_target{};
    result = resource_runtime.createImageRef(
        kWidth,
        kHeight,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        color_target);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef readback_buffer{};
    result = resource_runtime.createBufferRef(
        kReadbackByteCount,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        R2D::NativeMemoryDomain::Readback,
        readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanPipelineRuntime<Provider, Dim> pipeline_runtime;
    result = pipeline_runtime.initialize({
        .device = context_.device,
        .pipeline_cache_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = pipeline_runtime.reservePipelines(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    result = pipeline_runtime.createShaderModule(Render2DTest::kFullScreenTriangleVertSpv, vertex_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    result = pipeline_runtime.createShaderModule(Render2DTest::kMagentaFragSpv, fragment_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);

    PipelineRef pipeline_ref{};
    result = pipeline_runtime.createGraphicsPipelineRef(
        {
            .vertex_shader = vertex_shader,
            .fragment_shader = fragment_shader,
            .descriptor_set_layouts = nullptr,
            .descriptor_set_layout_count = 0U,
            .color_format = VK_FORMAT_R8G8B8A8_UNORM,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .cull_mode = VK_CULL_MODE_NONE,
            .front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .polygon_mode = VK_POLYGON_MODE_FILL,
            .sample_count = VK_SAMPLE_COUNT_1_BIT,
            .flags = 0U,
        },
        pipeline_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanUploadRingRuntime<Provider, Dim> upload_ring_runtime;
    result = upload_ring_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
        .byte_capacity = 256U,
        .frame_count = 2U,
        .usage_flags = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = upload_ring_runtime.beginFrame(0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    UploadRingSlice indirect_slice{};
    result = upload_ring_runtime.allocateSlice(0U, sizeof(VkDrawIndirectCommand), 4U, indirect_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    const VkDrawIndirectCommand indirect_command{
        .vertexCount = 3U,
        .instanceCount = 1U,
        .firstVertex = 0U,
        .firstInstance = 0U,
    };
    result = upload_ring_runtime.writeSlice(indirect_slice, &indirect_command, sizeof(indirect_command), 0U);
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
        {.first = 0U, .count = 1U},
        {.first = 0U, .count = 0U},
        0U,
        command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = command_runtime.beginCommandBuffer(command_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    constexpr std::array<BatchCommand, 1U> kBatches{{
        {
            .draw_first = 0U,
            .draw_count = 1U,
            .material_id = 0U,
            .texture_id = 0U,
            .pipeline_id = 0U,
            .descriptor_id = 0U,
            .sort_key = 0U,
            .flags = 0U,
        },
    }};
    result = R2D::VulkanDynamicRenderEncoder<Provider, Dim>::recordIndirect(
        command_ref,
        color_target,
        pipeline_ref,
        indirect_slice,
        kBatches,
        command_runtime,
        resource_runtime,
        pipeline_runtime,
        upload_ring_runtime,
        {
            .width = kWidth,
            .height = kHeight,
            .clear_color_rgba8 = 0x000000FFU,
            .draw_vertex_count = 3U,
            .draw_instance_count = 1U,
            .flags = 0U,
        },
        1U,
        sizeof(VkDrawIndirectCommand));
    assert(result.code == R2D::NativeStatusCode::Ok);

    VkCommandBuffer native_command_buffer = VK_NULL_HANDLE;
    result = command_runtime.resolveNativeCommandBuffer(command_ref, native_command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        color_target,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyImageToBuffer(native_command_buffer, color_target, readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordBufferBarrier(
        native_command_buffer,
        readback_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_HOST_READ_BIT);
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
        .wait_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = submit_runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
    result = submit_runtime.submit(command_refs, frame_sync, command_runtime, sync_runtime, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.waitFence(frame_sync, 1'000'000'000ULL);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = upload_ring_runtime.completeFrame(0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<R2D::U8, static_cast<R2D::Usize>(kReadbackByteCount)> pixels{};
    result = resource_runtime.readBuffer(readback_buffer, pixels.data(), kReadbackByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(pixels[0U] == 0xFFU);
    assert(pixels[1U] == 0x00U);
    assert(pixels[2U] == 0xFFU);
    assert(pixels[3U] == 0xFFU);

    result = command_runtime.releaseCommandBufferRef(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.releaseFrameSync(frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.releasePipelineRef(pipeline_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.destroyShaderModule(fragment_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.destroyShaderModule(vertex_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseImageRef(color_target);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

int main()
{
    try {
        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }
        if (!context.supports_dynamic_rendering) {
            Render2DTest::destroyVulkanSmokeContext(context);
            return 0;
        }

        testOffscreenMagentaSprite(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
