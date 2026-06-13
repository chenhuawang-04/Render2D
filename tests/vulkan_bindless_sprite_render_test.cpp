#include <Render2D/Render2D.hpp>

#include "support/SpriteShaders.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <cstring>
#include <span>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BindlessTable = R2D::VulkanBindlessTextureTable<Provider, Dim>;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using DescriptorRuntime = R2D::VulkanDescriptorRuntime<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;
using SamplerRef = R2D::SamplerRef<Provider, Dim>;
using SamplerRuntime = R2D::VulkanSamplerRuntime<Provider, Dim>;
using SpriteDrawPacket = R2D::SpriteDrawPacket<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpritePipelineRuntime = R2D::VulkanSpritePipelineRuntime<Provider, Dim>;
using SpriteRenderEncoder = R2D::VulkanSpriteRenderEncoder<Provider, Dim>;
using SpriteRenderEncoderConfig = R2D::VulkanSpriteRenderEncoderConfig;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;

// Eight 1x1 textures, two samplers, one screen carved into eight vertical bands.
// kWidth must be a whole multiple of kTextureCount so each band is an integer
// pixel span; the per-instance affine maps the unit quad onto band i.
constexpr R2D::U32 kTextureCount = 8U;
constexpr R2D::U32 kSamplerCount = 2U;
constexpr R2D::U32 kWidth = 16U;
constexpr R2D::U32 kHeight = 4U;
constexpr R2D::U32 kBandWidth = kWidth / kTextureCount;
constexpr R2D::U64 kReadbackByteCount = static_cast<R2D::U64>(kWidth) * kHeight * 4U;
constexpr R2D::U64 kSingleTextureByteCount = 4U;
constexpr R2D::U64 kTextureByteCount = kSingleTextureByteCount * kTextureCount;

// One distinct, exactly round-trippable color per texture (UNORM stays byte-exact).
constexpr std::array<R2D::U8, static_cast<R2D::Usize>(kTextureByteCount)> kTextureColors{{
    0xFFU, 0x00U, 0x00U, 0xFFU, // 0 red
    0x00U, 0xFFU, 0x00U, 0xFFU, // 1 green
    0x00U, 0x00U, 0xFFU, 0xFFU, // 2 blue
    0xFFU, 0xFFU, 0x00U, 0xFFU, // 3 yellow
    0xFFU, 0x00U, 0xFFU, 0xFFU, // 4 magenta
    0x00U, 0xFFU, 0xFFU, 0xFFU, // 5 cyan
    0xFFU, 0xFFU, 0xFFU, 0xFFU, // 6 white
    0x80U, 0x80U, 0x80U, 0xFFU, // 7 gray
}};

constexpr SpriteVertex makeVertex(float position_x_, float position_y_, float uv_x_, float uv_y_) noexcept
{
    return {
        .position_x = position_x_,
        .position_y = position_y_,
        .uv_x = uv_x_,
        .uv_y = uv_y_,
    };
}

// Band i: scale the unit quad to 1/kTextureCount of NDC width and translate it to
// the center of the i-th band. texture_id selects the bindless image; sampler_index
// alternates so both sampler-array entries are genuinely exercised.
SpriteInstance makeBandInstance(R2D::U32 band_, R2D::U32 texture_id_, R2D::U32 sampler_index_) noexcept
{
    const float scale_x = 1.0F / static_cast<float>(kTextureCount);
    const float translate_x = -1.0F + scale_x + static_cast<float>(band_) * (2.0F * scale_x);
    return {
        .transform_m00 = scale_x,
        .transform_m01 = 0.0F,
        .transform_m02 = translate_x,
        .transform_m10 = 0.0F,
        .transform_m11 = 1.0F,
        .transform_m12 = 0.0F,
        .uv_min_x = 0.0F,
        .uv_min_y = 0.0F,
        .uv_max_x = 1.0F,
        .uv_max_y = 1.0F,
        .source_index = band_,
        .source_id = band_ + 1U,
        .texture_id = texture_id_,
        .texture_generation = 0U,
        .material_id = 1U,
        .material_generation = 0U,
        .color_rgba8 = 0xFFFFFFFFU,
        .sort_key = 0U,
        .layer = 0U,
        .flags = 0U,
        .sampler_index = sampler_index_,
    };
}

SamplerRef createSampler(SamplerRuntime& sampler_runtime_, VkSamplerAddressMode address_mode_) noexcept
{
    SamplerRef sampler_ref{};
    const auto result = sampler_runtime_.createSamplerRef(
        {
            .mag_filter = static_cast<R2D::U32>(VK_FILTER_NEAREST),
            .min_filter = static_cast<R2D::U32>(VK_FILTER_NEAREST),
            .mipmap_mode = static_cast<R2D::U32>(VK_SAMPLER_MIPMAP_MODE_NEAREST),
            .address_mode_u = static_cast<R2D::U32>(address_mode_),
            .address_mode_v = static_cast<R2D::U32>(address_mode_),
            .address_mode_w = static_cast<R2D::U32>(address_mode_),
            .flags = 0U,
        },
        sampler_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    return sampler_ref;
}

void uploadTexture(
    ResourceRuntime& resource_runtime_,
    VkCommandBuffer command_buffer_,
    const BufferRef& upload_buffer_,
    const ImageRef& texture_,
    R2D::U64 buffer_offset_) noexcept
{
    auto result = resource_runtime_.transitionImageLayout(
        command_buffer_,
        texture_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime_.recordCopyBufferToImage(command_buffer_, upload_buffer_, texture_, buffer_offset_);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime_.transitionImageLayout(
        command_buffer_,
        texture_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

void copyTargetToReadback(
    ResourceRuntime& resource_runtime_,
    VkCommandBuffer command_buffer_,
    const ImageRef& color_target_,
    const BufferRef& readback_buffer_) noexcept
{
    auto result = resource_runtime_.transitionImageLayout(
        command_buffer_,
        color_target_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime_.recordCopyImageToBuffer(command_buffer_, color_target_, readback_buffer_);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime_.recordBufferBarrier(
        command_buffer_,
        readback_buffer_,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_HOST_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

// Renders the same eight-band scene twice in one command buffer: once through the
// bindless single-set path (recordBindless, per-instance texture/sampler selectors)
// and once through the combined-image-sampler fallback (recordPackets, one
// descriptor per texture). The two readbacks must be byte-for-byte identical, and
// each band must show its texture's color (so a both-wrong match cannot pass).
void testBindlessVsFallbackEquivalence(const Render2DTest::VulkanSmokeContext& context_)
{
    const R2D::VulkanBindlessCapability capability =
        R2D::queryVulkanBindlessCapability(context_.physical_device);
    if (capability.supported != R2D::kVulkanBindlessSupported) {
        return;
    }
    assert(context_.supports_bindless);

    constexpr std::array<SpriteVertex, 6U> kVertices{{
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, -1.0F, 1.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, 1.0F, 0.0F, 1.0F),
    }};
    std::array<SpriteInstance, kTextureCount> instances{};
    for (R2D::U32 band = 0U; band < kTextureCount; ++band) {
        instances[band] = makeBandInstance(band, band, band % kSamplerCount);
    }
    constexpr auto kVertexByteCount = static_cast<R2D::U64>(sizeof(kVertices));
    const auto instance_byte_count = static_cast<R2D::U64>(sizeof(instances));

    ResourceRuntime resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.reserveImages(kTextureCount + 2U).code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.reserveBuffers(5U).code == R2D::NativeStatusCode::Ok);

    ImageRef bindless_target{};
    result = resource_runtime.createImageRef(
        kWidth,
        kHeight,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        bindless_target);
    assert(result.code == R2D::NativeStatusCode::Ok);
    ImageRef fallback_target{};
    result = resource_runtime.createImageRef(
        kWidth,
        kHeight,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        fallback_target);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<ImageRef, kTextureCount> textures{};
    for (R2D::U32 index = 0U; index < kTextureCount; ++index) {
        result = resource_runtime.createImageRef(
            1U,
            1U,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            textures[index]);
        assert(result.code == R2D::NativeStatusCode::Ok);
    }

    BufferRef texture_upload_buffer{};
    result = resource_runtime.createBufferRef(
        kTextureByteCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        R2D::NativeMemoryDomain::Upload,
        texture_upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(texture_upload_buffer, kTextureColors.data(), kTextureByteCount, 0U);
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
        instance_byte_count,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        R2D::NativeMemoryDomain::Upload,
        instance_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(instance_buffer, instances.data(), instance_byte_count, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef bindless_readback{};
    result = resource_runtime.createBufferRef(
        kReadbackByteCount,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        R2D::NativeMemoryDomain::Readback,
        bindless_readback);
    assert(result.code == R2D::NativeStatusCode::Ok);
    BufferRef fallback_readback{};
    result = resource_runtime.createBufferRef(
        kReadbackByteCount,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        R2D::NativeMemoryDomain::Readback,
        fallback_readback);
    assert(result.code == R2D::NativeStatusCode::Ok);

    // Two samplers: identical filtering (so 1x1 sampling is byte-identical) but a
    // distinct address mode, so the table holds two genuinely different objects.
    SamplerRuntime sampler_runtime;
    result = sampler_runtime.initialize({.device = context_.device});
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(sampler_runtime.reserveSamplers(kSamplerCount).code == R2D::NativeStatusCode::Ok);
    const SamplerRef sampler_clamp = createSampler(sampler_runtime, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    const SamplerRef sampler_repeat = createSampler(sampler_runtime, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    VkSampler native_sampler_clamp = VK_NULL_HANDLE;
    VkSampler native_sampler_repeat = VK_NULL_HANDLE;
    assert(sampler_runtime.resolveNativeSampler(sampler_clamp, native_sampler_clamp).code == R2D::NativeStatusCode::Ok);
    assert(sampler_runtime.resolveNativeSampler(sampler_repeat, native_sampler_repeat).code == R2D::NativeStatusCode::Ok);

    // Bindless table: one set with all eight images resident (identity-indexed by
    // texture_id) and both samplers. The image views exist at creation, so residency
    // can be set before the pixel upload; only the draw-time layout must match.
    const R2D::U32 texture_capacity =
        capability.max_descriptor_set_sampled_images < 64U ? capability.max_descriptor_set_sampled_images : 64U;
    assert(texture_capacity >= kTextureCount);
    BindlessTable bindless_table;
    result = bindless_table.initialize(
        {
            .device = context_.device,
            .texture_capacity = texture_capacity,
            .sampler_capacity = kSamplerCount,
            .sampled_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .debug_fill_view = VK_NULL_HANDLE,
        },
        capability);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(bindless_table.setSampler(0U, native_sampler_clamp).code == R2D::NativeStatusCode::Ok);
    assert(bindless_table.setSampler(1U, native_sampler_repeat).code == R2D::NativeStatusCode::Ok);
    for (R2D::U32 index = 0U; index < kTextureCount; ++index) {
        result = bindless_table.setResident(index, textures[index].generation, textures[index], resource_runtime);
        assert(result.code == R2D::NativeStatusCode::Ok);
    }

    // Fallback: one combined-image-sampler descriptor set per texture.
    DescriptorRuntime descriptor_runtime;
    result = descriptor_runtime.initialize(
        SpritePipelineRuntime::makeDescriptorRuntimeConfig(
            context_.device,
            kTextureCount,
            R2D::kVulkanSpriteTextureDescriptorCount,
            0U,
            0U));
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(descriptor_runtime.reserveDescriptorSets(kTextureCount).code == R2D::NativeStatusCode::Ok);
    std::array<DescriptorSlice, kTextureCount> fallback_slices{};
    for (R2D::U32 index = 0U; index < kTextureCount; ++index) {
        result = descriptor_runtime.allocateDescriptorSlice(0U, 1U, fallback_slices[index]);
        assert(result.code == R2D::NativeStatusCode::Ok);
        result = descriptor_runtime.updateCombinedImageSampler(
            fallback_slices[index],
            0U,
            textures[index],
            resource_runtime,
            native_sampler_clamp,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        assert(result.code == R2D::NativeStatusCode::Ok);
    }

    PipelineRuntime pipeline_runtime;
    result = pipeline_runtime.initialize({
        .device = context_.device,
        .pipeline_cache_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(pipeline_runtime.reservePipelines(2U).code == R2D::NativeStatusCode::Ok);

    VkShaderModule bindless_vert = VK_NULL_HANDLE;
    VkShaderModule bindless_frag = VK_NULL_HANDLE;
    VkShaderModule fallback_vert = VK_NULL_HANDLE;
    VkShaderModule fallback_frag = VK_NULL_HANDLE;
    assert(pipeline_runtime.createShaderModule(Render2DTest::kSpriteBindlessVertSpv, bindless_vert).code == R2D::NativeStatusCode::Ok);
    assert(pipeline_runtime.createShaderModule(Render2DTest::kSpriteBindlessFragSpv, bindless_frag).code == R2D::NativeStatusCode::Ok);
    assert(pipeline_runtime.createShaderModule(Render2DTest::kSpriteVertSpv, fallback_vert).code == R2D::NativeStatusCode::Ok);
    assert(pipeline_runtime.createShaderModule(Render2DTest::kSpriteTexturedFragSpv, fallback_frag).code == R2D::NativeStatusCode::Ok);

    PipelineRef bindless_pipeline{};
    result = SpritePipelineRuntime::createBindlessPipelineRef(
        pipeline_runtime,
        {
            .vertex_shader = bindless_vert,
            .fragment_shader = bindless_frag,
            .descriptor_set_layout = bindless_table.nativeDescriptorSetLayout(),
            .color_format = VK_FORMAT_R8G8B8A8_UNORM,
            .flags = 0U,
        },
        bindless_pipeline);
    assert(result.code == R2D::NativeStatusCode::Ok);
    PipelineRef fallback_pipeline{};
    result = SpritePipelineRuntime::createPipelineRef(
        pipeline_runtime,
        {
            .vertex_shader = fallback_vert,
            .fragment_shader = fallback_frag,
            .descriptor_set_layout = descriptor_runtime.nativeDescriptorSetLayout(),
            .color_format = VK_FORMAT_R8G8B8A8_UNORM,
            .flags = 0U,
        },
        fallback_pipeline);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanCommandRuntime<Provider, Dim> command_runtime;
    result = command_runtime.initialize({
        .device = context_.device,
        .queue_family_index = context_.queue_family_index,
        .command_pool_flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(command_runtime.reserveCommandBuffers(1U).code == R2D::NativeStatusCode::Ok);
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

    for (R2D::U32 index = 0U; index < kTextureCount; ++index) {
        uploadTexture(
            resource_runtime,
            native_command_buffer,
            texture_upload_buffer,
            textures[index],
            static_cast<R2D::U64>(index) * kSingleTextureByteCount);
    }

    const SpriteRenderEncoderConfig render_config{
        .vertex_buffer_offset = 0U,
        .instance_buffer_offset = 0U,
        .width = kWidth,
        .height = kHeight,
        .clear_color_rgba8 = 0x000000FFU,
        .vertex_count = static_cast<R2D::U32>(kVertices.size()),
        .instance_count = kTextureCount,
        .first_vertex = 0U,
        .first_instance = 0U,
        .flags = 0U,
    };

    // Bindless: one packet covers all bands; the single set is bound once.
    const std::array<SpriteDrawPacket, 1U> bindless_packets{{
        {
            .batch_index = 0U,
            .draw_first = 0U,
            .draw_count = kTextureCount,
            .instance_first = 0U,
            .instance_count = kTextureCount,
            .vertex_first = 0U,
            .vertex_count = static_cast<R2D::U32>(kVertices.size()),
            .index_first = 0U,
            .index_count = 0U,
            .material_id = 1U,
            .material_generation = 1U,
            .texture_id = 0U,         // ignored by the bindless path
            .texture_generation = 0U, // ignored by the bindless path
            .pipeline_id = bindless_pipeline.pipeline_id,
            .pipeline_generation = bindless_pipeline.generation,
            .descriptor_id = 0U,         // ignored by the bindless path
            .descriptor_generation = 0U, // ignored by the bindless path
            .descriptor_first = 0U,
            .descriptor_count = 0U,
            .flags = 0U,
        },
    }};
    result = SpriteRenderEncoder::recordBindless(
        command_ref,
        bindless_target,
        vertex_buffer,
        instance_buffer,
        bindless_packets,
        command_runtime,
        resource_runtime,
        pipeline_runtime,
        bindless_table,
        render_config);
    assert(result.code == R2D::NativeStatusCode::Ok);
    copyTargetToReadback(resource_runtime, native_command_buffer, bindless_target, bindless_readback);

    // Fallback: one packet per band, each rebinding its own combined-image-sampler.
    std::array<SpriteDrawPacket, kTextureCount> fallback_packets{};
    for (R2D::U32 index = 0U; index < kTextureCount; ++index) {
        fallback_packets[index] = SpriteDrawPacket{
            .batch_index = index,
            .draw_first = index,
            .draw_count = 1U,
            .instance_first = index,
            .instance_count = 1U,
            .vertex_first = 0U,
            .vertex_count = static_cast<R2D::U32>(kVertices.size()),
            .index_first = 0U,
            .index_count = 0U,
            .material_id = 1U,
            .material_generation = 1U,
            .texture_id = index,
            .texture_generation = textures[index].generation,
            .pipeline_id = fallback_pipeline.pipeline_id,
            .pipeline_generation = fallback_pipeline.generation,
            .descriptor_id = fallback_slices[index].descriptor_set_id,
            .descriptor_generation = fallback_slices[index].generation,
            .descriptor_first = fallback_slices[index].first,
            .descriptor_count = fallback_slices[index].count,
            .flags = 0U,
        };
    }
    result = SpriteRenderEncoder::recordPackets(
        command_ref,
        fallback_target,
        vertex_buffer,
        instance_buffer,
        fallback_packets,
        command_runtime,
        resource_runtime,
        pipeline_runtime,
        descriptor_runtime,
        render_config);
    assert(result.code == R2D::NativeStatusCode::Ok);
    copyTargetToReadback(resource_runtime, native_command_buffer, fallback_target, fallback_readback);

    result = command_runtime.endCommandBuffer(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSyncRuntime<Provider, Dim> sync_runtime;
    result = sync_runtime.initialize({
        .device = context_.device,
        .fence_create_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(sync_runtime.reserveFrameSyncs(1U).code == R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    result = sync_runtime.createFrameSync(0U, 0U, frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSubmitRuntime<Provider, Dim> submit_runtime;
    result = submit_runtime.initialize({
        .queue = context_.queue,
        .wait_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(submit_runtime.reserveCommandBuffers(1U).code == R2D::NativeStatusCode::Ok);
    const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
    result = submit_runtime.submit(command_refs, frame_sync, command_runtime, sync_runtime, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.waitFence(frame_sync, 1'000'000'000ULL);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<R2D::U8, static_cast<R2D::Usize>(kReadbackByteCount)> bindless_pixels{};
    std::array<R2D::U8, static_cast<R2D::Usize>(kReadbackByteCount)> fallback_pixels{};
    result = resource_runtime.readBuffer(bindless_readback, bindless_pixels.data(), kReadbackByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.readBuffer(fallback_readback, fallback_pixels.data(), kReadbackByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    // Core claim: the bindless single-set render is byte-for-byte identical to the
    // per-texture combined-image-sampler fallback.
    assert(std::memcmp(bindless_pixels.data(), fallback_pixels.data(), bindless_pixels.size()) == 0);

    // And each band shows its own texture's color, so a mutual-failure match cannot
    // sneak through the equivalence check above.
    for (R2D::U32 y = 0U; y < kHeight; ++y) {
        for (R2D::U32 x = 0U; x < kWidth; ++x) {
            const R2D::U32 band = x / kBandWidth;
            const auto pixel_offset =
                (static_cast<R2D::Usize>(y) * static_cast<R2D::Usize>(kWidth) + static_cast<R2D::Usize>(x)) * 4U;
            const auto color_offset = static_cast<R2D::Usize>(band) * 4U;
            assert(bindless_pixels[pixel_offset] == kTextureColors[color_offset]);
            assert(bindless_pixels[pixel_offset + 1U] == kTextureColors[color_offset + 1U]);
            assert(bindless_pixels[pixel_offset + 2U] == kTextureColors[color_offset + 2U]);
            assert(bindless_pixels[pixel_offset + 3U] == 0xFFU);
        }
    }

    result = command_runtime.releaseCommandBufferRef(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.releaseFrameSync(frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.releasePipelineRef(fallback_pipeline);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.releasePipelineRef(bindless_pipeline);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.destroyShaderModule(fallback_frag);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.destroyShaderModule(fallback_vert);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.destroyShaderModule(bindless_frag);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.destroyShaderModule(bindless_vert);
    assert(result.code == R2D::NativeStatusCode::Ok);
    for (R2D::U32 index = 0U; index < kTextureCount; ++index) {
        result = descriptor_runtime.releaseDescriptorSlice(fallback_slices[index]);
        assert(result.code == R2D::NativeStatusCode::Ok);
    }
    // The bindless set references the texture views, so drop it before the images.
    bindless_table.shutdown();
    result = sampler_runtime.releaseSamplerRef(sampler_repeat);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sampler_runtime.releaseSamplerRef(sampler_clamp);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(fallback_readback);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(bindless_readback);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(instance_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(vertex_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(texture_upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    for (R2D::U32 index = 0U; index < kTextureCount; ++index) {
        result = resource_runtime.releaseImageRef(textures[index]);
        assert(result.code == R2D::NativeStatusCode::Ok);
    }
    result = resource_runtime.releaseImageRef(fallback_target);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseImageRef(bindless_target);
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

        testBindlessVsFallbackEquivalence(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
