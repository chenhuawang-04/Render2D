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
using AtlasRuntime = R2D::VulkanTextureAtlasRuntime<Provider, Dim>;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using DescriptorRuntime = R2D::VulkanDescriptorRuntime<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;
using SamplerRef = R2D::SamplerRef<Provider, Dim>;
using SamplerRuntime = R2D::VulkanSamplerRuntime<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using SpriteDrawPacket = R2D::SpriteDrawPacket<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpritePipelineRuntime = R2D::VulkanSpritePipelineRuntime<Provider, Dim>;
using SpriteRenderEncoder = R2D::VulkanSpriteRenderEncoder<Provider, Dim>;
using SpriteRenderEncoderConfig = R2D::VulkanSpriteRenderEncoderConfig;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;
using TextureAtlasItem = R2D::TextureAtlasItem<Provider, Dim>;
using TextureAtlasRegion = R2D::TextureAtlasRegion<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;

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

constexpr R2D::Mat3 makeAffine(
    float m00_,
    float m01_,
    float m02_,
    float m10_,
    float m11_,
    float m12_) noexcept
{
    return {
        .m00 = m00_,
        .m01 = m01_,
        .m02 = m02_,
        .m10 = m10_,
        .m11 = m11_,
        .m12 = m12_,
        .m20 = 0.0F,
        .m21 = 0.0F,
        .m22 = 1.0F,
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
        .texture_generation = 0U,
        .material_id = 1U,
        .material_generation = 0U,
        .color_rgba8 = 0xFFFFFFFFU,
        .sort_key = 0U,
        .layer = 0U,
        .flags = 0U,
        .sampler_index = 0U,
    };
}

constexpr SpriteInstance makePacketInstance(
    float scale_x_,
    float translate_x_,
    R2D::U32 source_index_,
    R2D::U32 texture_id_) noexcept
{
    auto instance = makeInstance();
    instance.transform_m00 = scale_x_;
    instance.transform_m02 = translate_x_;
    instance.source_index = source_index_;
    instance.source_id = source_index_ + 1U;
    instance.texture_id = texture_id_;
    return instance;
}

constexpr DrawCommand makeAtlasDraw(
    R2D::U32 source_index_,
    R2D::U32 instance_first_,
    R2D::U32 texture_id_,
    R2D::U32 texture_generation_) noexcept
{
    return {
        .source_index = source_index_,
        .material_id = 1U,
        .material_generation = 1U,
        .texture_id = texture_id_,
        .texture_generation = texture_generation_,
        .vertex_first = 0U,
        .vertex_count = 6U,
        .index_first = 0U,
        .index_count = 0U,
        .instance_first = instance_first_,
        .instance_count = 1U,
        .sort_key = R2D::makeDrawSortKey(0U, 1U, texture_id_, 0U),
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

void testAtlasTextureRegionPacketDraw(const Render2DTest::VulkanSmokeContext& context_)
{
    constexpr R2D::U32 kWidth = 8U;
    constexpr R2D::U32 kHeight = 4U;
    constexpr R2D::U32 kTextureWidth = 2U;
    constexpr R2D::U32 kTextureHeight = 1U;
    constexpr R2D::U64 kReadbackByteCount = static_cast<R2D::U64>(kWidth) * kHeight * 4U;
    constexpr R2D::U64 kTextureByteCount = static_cast<R2D::U64>(kTextureWidth) * kTextureHeight * 4U;
    constexpr std::array<R2D::U8, static_cast<R2D::Usize>(kTextureByteCount)> kTexturePixels{
        0xFFU, 0x00U, 0x00U, 0xFFU,
        0x00U, 0xFFU, 0x00U, 0xFFU,
    };
    constexpr std::array<SpriteVertex, 6U> kVertices{{
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, -1.0F, 1.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, 1.0F, 0.0F, 1.0F),
    }};
    constexpr std::array<TextureAtlasItem, 2U> kAtlasItems{{
        {.region_id = 1U, .generation = 10U, .width = 1U, .height = 1U, .padding = 0U, .flags = 0U},
        {.region_id = 2U, .generation = 20U, .width = 1U, .height = 1U, .padding = 0U, .flags = 0U},
    }};
    constexpr auto kVertexByteCount = static_cast<R2D::U64>(sizeof(kVertices));

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

    ImageRef atlas_texture{};
    result = resource_runtime.createImageRef(
        kTextureWidth,
        kTextureHeight,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        atlas_texture);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<TextureAtlasRegion, kAtlasItems.size()> atlas_regions{};
    auto system_result = R2D::TextureAtlasBuildSystem<Provider, Dim>::run(
        kAtlasItems,
        atlas_regions,
        {
            .atlas_width = kTextureWidth,
            .atlas_height = kTextureHeight,
            .atlas_id = atlas_texture.image_id,
            .atlas_generation = atlas_texture.generation,
            .texture_id = atlas_texture.image_id,
            .texture_generation = atlas_texture.generation,
            .padding = 0U,
            .flags = 0U,
        });
    assert(system_result.code == R2D::SystemStatusCode::Ok);

    const std::array<WorldTransform, 2U> world_transforms{{
        {.source_id = 1U, .affine = makeAffine(0.5F, 0.0F, -0.5F, 0.0F, 1.0F, 0.0F)},
        {.source_id = 2U, .affine = makeAffine(0.5F, 0.0F, 0.5F, 0.0F, 1.0F, 0.0F)},
    }};
    const std::array<Sprite, 2U> sprites{{
        {
            .source_id = 1U,
            .texture_id = atlas_texture.image_id,
            .texture_generation = atlas_texture.generation,
            .texture_region_id = 1U,
            .texture_region_generation = 10U,
            .material_id = 1U,
            .material_generation = 1U,
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = 0U,
            .flags = 0U,
        },
        {
            .source_id = 2U,
            .texture_id = atlas_texture.image_id,
            .texture_generation = atlas_texture.generation,
            .texture_region_id = 2U,
            .texture_region_generation = 20U,
            .material_id = 1U,
            .material_generation = 1U,
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = 0U,
            .flags = 0U,
        },
    }};
    const std::array<DrawCommand, 2U> draw_commands{{
        makeAtlasDraw(0U, 0U, atlas_texture.image_id, atlas_texture.generation),
        makeAtlasDraw(1U, 1U, atlas_texture.image_id, atlas_texture.generation),
    }};
    std::array<SpriteInstance, 2U> instances{};
    system_result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::runWithTextureRegions(
        draw_commands,
        world_transforms,
        sprites,
        atlas_regions,
        instances);
    assert(system_result.code == R2D::SystemStatusCode::Ok);
    assert(instances[0U].uv_min_x == 0.0F);
    assert(instances[0U].uv_max_x == 0.5F);
    assert(instances[1U].uv_min_x == 0.5F);
    assert(instances[1U].uv_max_x == 1.0F);
    constexpr auto kInstanceByteCount = static_cast<R2D::U64>(sizeof(instances));

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
    result = resource_runtime.writeBuffer(instance_buffer, instances.data(), kInstanceByteCount, 0U);
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
        atlas_texture,
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
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        atlas_texture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyBufferToImage(native_command_buffer, texture_upload_buffer, atlas_texture);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        atlas_texture,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    const std::array<SpriteDrawPacket, 1U> packets{{
        {
            .batch_index = 0U,
            .draw_first = 0U,
            .draw_count = static_cast<R2D::U32>(draw_commands.size()),
            .instance_first = 0U,
            .instance_count = static_cast<R2D::U32>(instances.size()),
            .vertex_first = 0U,
            .vertex_count = static_cast<R2D::U32>(kVertices.size()),
            .index_first = 0U,
            .index_count = 0U,
            .material_id = 1U,
            .material_generation = 1U,
            .texture_id = atlas_texture.image_id,
            .texture_generation = atlas_texture.generation,
            .pipeline_id = pipeline_ref.pipeline_id,
            .pipeline_generation = pipeline_ref.generation,
            .descriptor_id = descriptor_slice.descriptor_set_id,
            .descriptor_generation = descriptor_slice.generation,
            .descriptor_first = descriptor_slice.first,
            .descriptor_count = descriptor_slice.count,
            .flags = 0U,
        },
    }};
    constexpr SpriteRenderEncoderConfig kRenderConfig{
        .vertex_buffer_offset = 0U,
        .instance_buffer_offset = 0U,
        .width = kWidth,
        .height = kHeight,
        .clear_color_rgba8 = 0x000000FFU,
        .vertex_count = static_cast<R2D::U32>(kVertices.size()),
        .instance_count = static_cast<R2D::U32>(instances.size()),
        .first_vertex = 0U,
        .first_instance = 0U,
        .flags = 0U,
    };
    result = SpriteRenderEncoder::recordPackets(
        command_ref,
        color_target,
        vertex_buffer,
        instance_buffer,
        packets,
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
    for (R2D::U32 y = 0U; y < kHeight; ++y) {
        for (R2D::U32 x = 0U; x < kWidth; ++x) {
            const auto byte_offset =
                (static_cast<R2D::Usize>(y) * static_cast<R2D::Usize>(kWidth) + static_cast<R2D::Usize>(x)) * 4U;
            if (x < kWidth / 2U) {
                assert(pixels[byte_offset] == 0xFFU);
                assert(pixels[byte_offset + 1U] == 0x00U);
                assert(pixels[byte_offset + 2U] == 0x00U);
            } else {
                assert(pixels[byte_offset] == 0x00U);
                assert(pixels[byte_offset + 1U] == 0xFFU);
                assert(pixels[byte_offset + 2U] == 0x00U);
            }
            assert(pixels[byte_offset + 3U] == 0xFFU);
        }
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
    result = resource_runtime.releaseBufferRef(readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(instance_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(vertex_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(texture_upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseImageRef(atlas_texture);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseImageRef(color_target);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

// Stage 18E: the same atlas-region sampling proof as testAtlasTextureRegionPacketDraw,
// but the atlas image is created and composited through VulkanTextureAtlasRuntime
// (createAtlasImage + per-region recordUploadRegion) instead of ad-hoc resource
// runtime calls. This proves the atlas image runtime feeds the real sampled sprite
// path: two 1x1 sub-images uploaded into one 2x1 atlas are sampled back as the
// left (red) and right (green) screen halves through region UVs.
void testAtlasRuntimeRegionPacketDraw(const Render2DTest::VulkanSmokeContext& context_)
{
    constexpr R2D::U32 kWidth = 8U;
    constexpr R2D::U32 kHeight = 4U;
    constexpr R2D::U32 kTextureWidth = 2U;
    constexpr R2D::U32 kTextureHeight = 1U;
    constexpr R2D::U64 kPixelBytes = 4U;
    constexpr R2D::U64 kReadbackByteCount = static_cast<R2D::U64>(kWidth) * kHeight * 4U;
    constexpr R2D::U64 kTextureByteCount = static_cast<R2D::U64>(kTextureWidth) * kTextureHeight * 4U;
    constexpr std::array<R2D::U8, static_cast<R2D::Usize>(kTextureByteCount)> kTexturePixels{
        0xFFU, 0x00U, 0x00U, 0xFFU, // red   -> region (0,0)
        0x00U, 0xFFU, 0x00U, 0xFFU, // green -> region (1,0)
    };
    constexpr std::array<SpriteVertex, 6U> kVertices{{
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, -1.0F, 1.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, 1.0F, 0.0F, 1.0F),
    }};
    constexpr std::array<TextureAtlasItem, 2U> kAtlasItems{{
        {.region_id = 1U, .generation = 10U, .width = 1U, .height = 1U, .padding = 0U, .flags = 0U},
        {.region_id = 2U, .generation = 20U, .width = 1U, .height = 1U, .padding = 0U, .flags = 0U},
    }};
    constexpr auto kVertexByteCount = static_cast<R2D::U64>(sizeof(kVertices));

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

    // The atlas image is owned by the Stage 18 atlas runtime, which delegates the
    // sampled-image allocation to the resource runtime.
    AtlasRuntime atlas_runtime;
    result = atlas_runtime.initialize({.resource_runtime = &resource_runtime});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = atlas_runtime.reserveAtlases(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    R2D::VulkanAtlasImageRef atlas_image_ref{};
    result = atlas_runtime.createAtlasImage(
        {.width = kTextureWidth, .height = kTextureHeight, .format = VK_FORMAT_R8G8B8A8_UNORM, .flags = 0U},
        atlas_image_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    ImageRef atlas_texture{};
    result = atlas_runtime.resolveImageRef(atlas_image_ref, atlas_texture);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<TextureAtlasRegion, kAtlasItems.size()> atlas_regions{};
    auto system_result = R2D::TextureAtlasBuildSystem<Provider, Dim>::run(
        kAtlasItems,
        atlas_regions,
        {
            .atlas_width = kTextureWidth,
            .atlas_height = kTextureHeight,
            .atlas_id = atlas_texture.image_id,
            .atlas_generation = atlas_texture.generation,
            .texture_id = atlas_texture.image_id,
            .texture_generation = atlas_texture.generation,
            .padding = 0U,
            .flags = 0U,
        });
    assert(system_result.code == R2D::SystemStatusCode::Ok);

    const std::array<WorldTransform, 2U> world_transforms{{
        {.source_id = 1U, .affine = makeAffine(0.5F, 0.0F, -0.5F, 0.0F, 1.0F, 0.0F)},
        {.source_id = 2U, .affine = makeAffine(0.5F, 0.0F, 0.5F, 0.0F, 1.0F, 0.0F)},
    }};
    const std::array<Sprite, 2U> sprites{{
        {
            .source_id = 1U,
            .texture_id = atlas_texture.image_id,
            .texture_generation = atlas_texture.generation,
            .texture_region_id = 1U,
            .texture_region_generation = 10U,
            .material_id = 1U,
            .material_generation = 1U,
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = 0U,
            .flags = 0U,
        },
        {
            .source_id = 2U,
            .texture_id = atlas_texture.image_id,
            .texture_generation = atlas_texture.generation,
            .texture_region_id = 2U,
            .texture_region_generation = 20U,
            .material_id = 1U,
            .material_generation = 1U,
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = 0U,
            .flags = 0U,
        },
    }};
    const std::array<DrawCommand, 2U> draw_commands{{
        makeAtlasDraw(0U, 0U, atlas_texture.image_id, atlas_texture.generation),
        makeAtlasDraw(1U, 1U, atlas_texture.image_id, atlas_texture.generation),
    }};
    std::array<SpriteInstance, 2U> instances{};
    system_result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::runWithTextureRegions(
        draw_commands,
        world_transforms,
        sprites,
        atlas_regions,
        instances);
    assert(system_result.code == R2D::SystemStatusCode::Ok);
    assert(instances[0U].uv_min_x == 0.0F);
    assert(instances[0U].uv_max_x == 0.5F);
    assert(instances[1U].uv_min_x == 0.5F);
    assert(instances[1U].uv_max_x == 1.0F);
    constexpr auto kInstanceByteCount = static_cast<R2D::U64>(sizeof(instances));

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
    result = resource_runtime.writeBuffer(instance_buffer, instances.data(), kInstanceByteCount, 0U);
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
        atlas_texture,
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
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        atlas_texture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    // Composite the two 1x1 sub-images into their atlas rectangles.
    result = atlas_runtime.recordUploadRegion(
        native_command_buffer, atlas_image_ref, texture_upload_buffer, 0U, 0U, 0U, 1U, 1U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = atlas_runtime.recordUploadRegion(
        native_command_buffer, atlas_image_ref, texture_upload_buffer, kPixelBytes, 1U, 0U, 1U, 1U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        atlas_texture,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    const std::array<SpriteDrawPacket, 1U> packets{{
        {
            .batch_index = 0U,
            .draw_first = 0U,
            .draw_count = static_cast<R2D::U32>(draw_commands.size()),
            .instance_first = 0U,
            .instance_count = static_cast<R2D::U32>(instances.size()),
            .vertex_first = 0U,
            .vertex_count = static_cast<R2D::U32>(kVertices.size()),
            .index_first = 0U,
            .index_count = 0U,
            .material_id = 1U,
            .material_generation = 1U,
            .texture_id = atlas_texture.image_id,
            .texture_generation = atlas_texture.generation,
            .pipeline_id = pipeline_ref.pipeline_id,
            .pipeline_generation = pipeline_ref.generation,
            .descriptor_id = descriptor_slice.descriptor_set_id,
            .descriptor_generation = descriptor_slice.generation,
            .descriptor_first = descriptor_slice.first,
            .descriptor_count = descriptor_slice.count,
            .flags = 0U,
        },
    }};
    constexpr SpriteRenderEncoderConfig kRenderConfig{
        .vertex_buffer_offset = 0U,
        .instance_buffer_offset = 0U,
        .width = kWidth,
        .height = kHeight,
        .clear_color_rgba8 = 0x000000FFU,
        .vertex_count = static_cast<R2D::U32>(kVertices.size()),
        .instance_count = static_cast<R2D::U32>(instances.size()),
        .first_vertex = 0U,
        .first_instance = 0U,
        .flags = 0U,
    };
    result = SpriteRenderEncoder::recordPackets(
        command_ref,
        color_target,
        vertex_buffer,
        instance_buffer,
        packets,
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
    for (R2D::U32 y = 0U; y < kHeight; ++y) {
        for (R2D::U32 x = 0U; x < kWidth; ++x) {
            const auto byte_offset =
                (static_cast<R2D::Usize>(y) * static_cast<R2D::Usize>(kWidth) + static_cast<R2D::Usize>(x)) * 4U;
            if (x < kWidth / 2U) {
                assert(pixels[byte_offset] == 0xFFU);
                assert(pixels[byte_offset + 1U] == 0x00U);
                assert(pixels[byte_offset + 2U] == 0x00U);
            } else {
                assert(pixels[byte_offset] == 0x00U);
                assert(pixels[byte_offset + 1U] == 0xFFU);
                assert(pixels[byte_offset + 2U] == 0x00U);
            }
            assert(pixels[byte_offset + 3U] == 0xFFU);
        }
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
    result = resource_runtime.releaseBufferRef(readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(instance_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(vertex_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(texture_upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = atlas_runtime.releaseAtlasImage(atlas_image_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseImageRef(color_target);
    assert(result.code == R2D::NativeStatusCode::Ok);
    atlas_runtime.shutdown();
}

void testMultiTexturePacketDraw(const Render2DTest::VulkanSmokeContext& context_)
{
    constexpr R2D::U32 kWidth = 8U;
    constexpr R2D::U32 kHeight = 4U;
    constexpr R2D::U32 kTextureWidth = 1U;
    constexpr R2D::U32 kTextureHeight = 1U;
    constexpr R2D::U64 kReadbackByteCount = static_cast<R2D::U64>(kWidth) * kHeight * 4U;
    constexpr R2D::U64 kSingleTextureByteCount = static_cast<R2D::U64>(kTextureWidth) * kTextureHeight * 4U;
    constexpr R2D::U64 kTextureByteCount = kSingleTextureByteCount * 2U;
    constexpr std::array<R2D::U8, static_cast<R2D::Usize>(kTextureByteCount)> kTexturePixels{
        0xFFU, 0x00U, 0x00U, 0xFFU,
        0x00U, 0xFFU, 0x00U, 0xFFU,
    };
    constexpr std::array<SpriteVertex, 6U> kVertices{{
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, -1.0F, 1.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, 1.0F, 0.0F, 1.0F),
    }};
    constexpr std::array<SpriteInstance, 2U> kInstances{{
        makePacketInstance(0.5F, -0.5F, 0U, 1U),
        makePacketInstance(0.5F, 0.5F, 1U, 2U),
    }};
    constexpr auto kVertexByteCount = static_cast<R2D::U64>(sizeof(kVertices));
    constexpr auto kInstanceByteCount = static_cast<R2D::U64>(sizeof(kInstances));

    R2D::VulkanResourceRuntime<Provider, Dim> resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    auto capacity = resource_runtime.reserveImages(3U);
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

    ImageRef red_texture{};
    result = resource_runtime.createImageRef(
        kTextureWidth,
        kTextureHeight,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        red_texture);
    assert(result.code == R2D::NativeStatusCode::Ok);
    ImageRef green_texture{};
    result = resource_runtime.createImageRef(
        kTextureWidth,
        kTextureHeight,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        green_texture);
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
            2U,
            R2D::kVulkanSpriteTextureDescriptorCount,
            0U,
            0U));
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = descriptor_runtime.reserveDescriptorSets(2U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    DescriptorSlice red_descriptor{};
    result = descriptor_runtime.allocateDescriptorSlice(0U, 1U, red_descriptor);
    assert(result.code == R2D::NativeStatusCode::Ok);
    DescriptorSlice green_descriptor{};
    result = descriptor_runtime.allocateDescriptorSlice(0U, 1U, green_descriptor);
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
        red_descriptor,
        0U,
        red_texture,
        resource_runtime,
        native_sampler,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = descriptor_runtime.updateCombinedImageSampler(
        green_descriptor,
        0U,
        green_texture,
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
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        red_texture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyBufferToImage(native_command_buffer, texture_upload_buffer, red_texture, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        red_texture,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        green_texture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyBufferToImage(
        native_command_buffer,
        texture_upload_buffer,
        green_texture,
        kSingleTextureByteCount);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.transitionImageLayout(
        native_command_buffer,
        green_texture,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    const std::array<SpriteDrawPacket, 2U> packets{{
        {
            .batch_index = 0U,
            .draw_first = 0U,
            .draw_count = 1U,
            .instance_first = 0U,
            .instance_count = 1U,
            .vertex_first = 0U,
            .vertex_count = static_cast<R2D::U32>(kVertices.size()),
            .index_first = 0U,
            .index_count = 0U,
            .material_id = 1U,
            .material_generation = 1U,
            .texture_id = 1U,
            .texture_generation = red_texture.generation,
            .pipeline_id = pipeline_ref.pipeline_id,
            .pipeline_generation = pipeline_ref.generation,
            .descriptor_id = red_descriptor.descriptor_set_id,
            .descriptor_generation = red_descriptor.generation,
            .descriptor_first = red_descriptor.first,
            .descriptor_count = red_descriptor.count,
            .flags = 0U,
        },
        {
            .batch_index = 1U,
            .draw_first = 1U,
            .draw_count = 1U,
            .instance_first = 1U,
            .instance_count = 1U,
            .vertex_first = 0U,
            .vertex_count = static_cast<R2D::U32>(kVertices.size()),
            .index_first = 0U,
            .index_count = 0U,
            .material_id = 1U,
            .material_generation = 1U,
            .texture_id = 2U,
            .texture_generation = green_texture.generation,
            .pipeline_id = pipeline_ref.pipeline_id,
            .pipeline_generation = pipeline_ref.generation,
            .descriptor_id = green_descriptor.descriptor_set_id,
            .descriptor_generation = green_descriptor.generation,
            .descriptor_first = green_descriptor.first,
            .descriptor_count = green_descriptor.count,
            .flags = 0U,
        },
    }};
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
    std::array<SpriteDrawPacket, 2U> stale_packets{packets};
    ++stale_packets[0U].descriptor_generation;
    result = SpriteRenderEncoder::recordPackets(
        command_ref,
        color_target,
        vertex_buffer,
        instance_buffer,
        stale_packets,
        command_runtime,
        resource_runtime,
        pipeline_runtime,
        descriptor_runtime,
        kRenderConfig);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    result = SpriteRenderEncoder::recordPackets(
        command_ref,
        color_target,
        vertex_buffer,
        instance_buffer,
        packets,
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
    for (R2D::U32 y = 0U; y < kHeight; ++y) {
        for (R2D::U32 x = 0U; x < kWidth; ++x) {
            const auto byte_offset =
                (static_cast<R2D::Usize>(y) * static_cast<R2D::Usize>(kWidth) + static_cast<R2D::Usize>(x)) * 4U;
            if (x < kWidth / 2U) {
                assert(pixels[byte_offset] == 0xFFU);
                assert(pixels[byte_offset + 1U] == 0x00U);
                assert(pixels[byte_offset + 2U] == 0x00U);
            } else {
                assert(pixels[byte_offset] == 0x00U);
                assert(pixels[byte_offset + 1U] == 0xFFU);
                assert(pixels[byte_offset + 2U] == 0x00U);
            }
            assert(pixels[byte_offset + 3U] == 0xFFU);
        }
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
    result = descriptor_runtime.releaseDescriptorSlice(green_descriptor);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = descriptor_runtime.releaseDescriptorSlice(red_descriptor);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sampler_runtime.releaseSamplerRef(sampler_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(instance_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(vertex_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(texture_upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseImageRef(green_texture);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseImageRef(red_texture);
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
        testAtlasTextureRegionPacketDraw(context);
        testAtlasRuntimeRegionPacketDraw(context);
        testMultiTexturePacketDraw(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
