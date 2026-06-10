#include <Render2D/Render2D.hpp>

#include "support/SpriteShaders.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <span>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using DescriptorRuntime = R2D::VulkanDescriptorRuntime<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;
using SamplerRef = R2D::SamplerRef<Provider, Dim>;
using SamplerRuntime = R2D::VulkanSamplerRuntime<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpritePipelineRuntime = R2D::VulkanSpritePipelineRuntime<Provider, Dim>;
using SpriteRenderEncoder = R2D::VulkanSpriteRenderEncoder<Provider, Dim>;
using SpriteRenderEncoderConfig = R2D::VulkanSpriteRenderEncoderConfig;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;

static_assert(R2D::SupportedRenderComponent<Provider, Dim, SamplerRef>);
static_assert(!R2D::SupportedRenderComponent<Provider, Dim, SamplerRuntime>);

constexpr SpriteVertex makeVertex(float position_x_, float position_y_, float uv_x_, float uv_y_) noexcept
{
    return {
        .position_x = position_x_,
        .position_y = position_y_,
        .uv_x = uv_x_,
        .uv_y = uv_y_,
    };
}

constexpr SpriteInstance makeInstance() noexcept
{
    return {
        .transform_m00 = 1.0F,
        .transform_m01 = 0.0F,
        .transform_m02 = 0.0F,
        .transform_m10 = 0.0F,
        .transform_m11 = 1.0F,
        .transform_m12 = 0.0F,
        .uv_min_x = 0.0F,
        .uv_min_y = 0.0F,
        .uv_max_x = 1.0F,
        .uv_max_y = 1.0F,
        .source_index = 0U,
        .source_id = 1U,
        .texture_id = 1U,
        .material_id = 1U,
        .color_rgba8 = 0xFFFFFFFFU,
        .sort_key = 0U,
        .layer = 0U,
        .flags = 0U,
    };
}

void testTexturedSpriteDraw(const Render2DTest::VulkanSmokeContext& context_)
{
    constexpr R2D::U32 kWidth = 4U;
    constexpr R2D::U32 kHeight = 4U;
    constexpr R2D::U32 kTextureWidth = 1U;
    constexpr R2D::U32 kTextureHeight = 1U;
    constexpr R2D::U64 kReadbackByteCount = static_cast<R2D::U64>(kWidth) * kHeight * 4U;
    constexpr R2D::U64 kTextureByteCount = static_cast<R2D::U64>(kTextureWidth) * kTextureHeight * 4U;
    constexpr std::array<R2D::U8, static_cast<R2D::Usize>(kTextureByteCount)> kTexturePixels{
        0x00U,
        0xFFU,
        0x00U,
        0xFFU,
    };
    constexpr std::array<SpriteVertex, 6U> kVertices{{
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, -1.0F, 1.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, 1.0F, 0.0F, 1.0F),
    }};
    constexpr std::array<SpriteInstance, 1U> kInstances{{makeInstance()}};
    constexpr auto kVertexByteCount = static_cast<R2D::U64>(sizeof(kVertices));
    constexpr auto kInstanceByteCount = static_cast<R2D::U64>(sizeof(kInstances));

    R2D::VulkanResourceRuntime<Provider, Dim> resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    auto capacity = resource_runtime.reserveImages(2U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    capacity = resource_runtime.reserveBuffers(4U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    ImageRef color_target{};
    result = resource_runtime.createImageRef(
        kWidth,
        kHeight,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        color_target);
    assert(result.code == R2D::NativeStatusCode::Ok);

    ImageRef texture_image{};
    result = resource_runtime.createImageRef(
        kTextureWidth,
        kTextureHeight,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        texture_image);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef texture_upload_buffer{};
    result = resource_runtime.createBufferRef(
        kTextureByteCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        R2D::NativeMemoryDomain::Upload,
        texture_upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(texture_upload_buffer, kTexturePixels.data(), kTextureByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef vertex_buffer{};
    result = resource_runtime.createBufferRef(
        kVertexByteCount,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        R2D::NativeMemoryDomain::Upload,
        vertex_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(vertex_buffer, kVertices.data(), kVertexByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef instance_buffer{};
    result = resource_runtime.createBufferRef(
        kInstanceByteCount,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        R2D::NativeMemoryDomain::Upload,
        instance_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(instance_buffer, kInstances.data(), kInstanceByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef readback_buffer{};
    result = resource_runtime.createBufferRef(
        kReadbackByteCount,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        R2D::NativeMemoryDomain::Readback,
        readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    DescriptorRuntime descriptor_runtime;
    result = descriptor_runtime.initialize(
        SpritePipelineRuntime::makeDescriptorRuntimeConfig(
            context_.device,
            1U,
            R2D::kVulkanSpriteTextureDescriptorCount,
            0U,
            0U));
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = descriptor_runtime.reserveDescriptorSets(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    DescriptorSlice descriptor_slice{};
    result = descriptor_runtime.allocateDescriptorSlice(0U, 1U, descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);

    SamplerRuntime sampler_runtime;
    result = sampler_runtime.initialize({
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = sampler_runtime.reserveSamplers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    SamplerRef sampler_ref{};
    result = sampler_runtime.createSamplerRef(
        {
            .mag_filter = VK_FILTER_NEAREST,
            .min_filter = VK_FILTER_NEAREST,
            .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .flags = 0U,
        },
        sampler_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    VkSampler native_sampler = VK_NULL_HANDLE;
    result = sampler_runtime.resolveNativeSampler(sampler_ref, native_sampler);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = descriptor_runtime.updateCombinedImageSampler(
        descriptor_slice,
        0U,
        texture_image,
        resource_runtime,
        native_sampler,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    assert(result.code == R2D::NativeStatusCode::Ok);

    PipelineRuntime pipeline_runtime;
    result = pipeline_runtime.initialize({
        .device = context_.device,
        .pipeline_cache_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = pipeline_runtime.reservePipelines(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    result = pipeline_runtime.createShaderModule(Render2DTest::kSpriteVertSpv, vertex_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    result = pipeline_runtime.createShaderModule(Render2DTest::kSpriteTexturedFragSpv, fragment_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);

    PipelineRef pipeline_ref{};
    result = SpritePipelineRuntime::createPipelineRef(
        pipeline_runtime,
        {
            .vertex_shader = vertex_shader,
            .fragment_shader = fragment_shader,
            .descriptor_set_layout = descriptor_runtime.nativeDescriptorSetLayout(),
            .color_format = VK_FORMAT_R8G8B8A8_UNORM,
            .flags = 0U,
        },
        pipeline_ref);
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

    VkCommandBuffer native_command_buffer = VK_NULL_HANDLE;
    result = command_runtime.resolveNativeCommandBuffer(command_ref, native_command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyBufferToImage(native_command_buffer, readback_buffer, texture_image);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(result.object_kind == R2D::NativeObjectKind::Image);
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        texture_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyBufferToImage(native_command_buffer, texture_upload_buffer, texture_image);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        texture_image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    const std::array<DescriptorSlice, 1U> descriptor_slices{descriptor_slice};
    constexpr SpriteRenderEncoderConfig kRenderConfig{
        .vertex_buffer_offset = 0U,
        .instance_buffer_offset = 0U,
        .width = kWidth,
        .height = kHeight,
        .clear_color_rgba8 = 0x000000FFU,
        .vertex_count = static_cast<R2D::U32>(kVertices.size()),
        .instance_count = static_cast<R2D::U32>(kInstances.size()),
        .first_vertex = 0U,
        .first_instance = 0U,
        .flags = 0U,
    };
    result = SpriteRenderEncoder::record(
        command_ref,
        color_target,
        pipeline_ref,
        vertex_buffer,
        instance_buffer,
        descriptor_slices,
        command_runtime,
        resource_runtime,
        pipeline_runtime,
        descriptor_runtime,
        kRenderConfig);
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

    std::array<R2D::U8, static_cast<R2D::Usize>(kReadbackByteCount)> pixels{};
    result = resource_runtime.readBuffer(readback_buffer, pixels.data(), kReadbackByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    for (R2D::Usize byte_offset = 0U; byte_offset < pixels.size(); byte_offset += 4U) {
        assert(pixels[byte_offset] == 0x00U);
        assert(pixels[byte_offset + 1U] == 0xFFU);
        assert(pixels[byte_offset + 2U] == 0x00U);
        assert(pixels[byte_offset + 3U] == 0xFFU);
    }

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
    result = descriptor_runtime.releaseDescriptorSlice(descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sampler_runtime.releaseSamplerRef(sampler_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sampler_runtime.resolveNativeSampler(sampler_ref, native_sampler);
    assert(result.code == R2D::NativeStatusCode::StaleReference);
    assert(native_sampler == VK_NULL_HANDLE);
    result = resource_runtime.releaseBufferRef(readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(instance_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(vertex_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(texture_upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseImageRef(texture_image);
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

        testTexturedSpriteDraw(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
