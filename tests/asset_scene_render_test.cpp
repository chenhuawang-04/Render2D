// Asset framework usability proof, end to end:
//
//   CPU (always runs): author two solid textures as real PNG files on disk, load
//   them back through the asset registry (a genuine stb decode), register
//   materials, then author a MiniEcs scene whose sprites reference those asset
//   handles. The scene runs through the unchanged span-only CPU chain (SpatialCull
//   -> CommandBuild -> SpriteInstanceBuild -> Batch) and we assert it culls,
//   batches, and -- critically -- that the loaded textures' ids flow all the way
//   into the draw commands.
//
//   GPU (skipped without a device): upload the loaded green texture to a real
//   VkImage, sample it across a full-screen sprite via the unchanged sprite
//   pipeline + render encoder, read the framebuffer back, and assert every pixel
//   is the texture's green -- i.e. the bytes decoded from the PNG reached the GPU
//   and were drawn.
//
// This is test-only scaffolding (the host owns the production asset pipeline at
// merge); stb lives behind render2d_test_image_io and never touches the core.
#include <Render2D/Render2D.hpp>

#include "support/AssetRegistry.hpp"
#include "support/HostLikeEcs.hpp"
#include "support/MiniEcs.hpp"
#include "support/SpriteShaders.hpp"
#include "support/TestHarness.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <exception>
#include <span>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Transform = R2D::Transform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using Camera = R2D::Camera<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using SpatialCull = R2D::SpatialCullSystem<Provider, Dim>;
using CommandBuild = R2D::CommandBuildSystem<Provider, Dim>;
using SpriteInstanceBuild = R2D::SpriteInstanceBuildSystem<Provider, Dim>;
using Batch = R2D::BatchSystem<Provider, Dim>;
using Arena = R2DT::HostFrameArena<Provider, Dim>;
using SceneEcs = R2DT::SceneEcs<Provider, Dim>;
using MiniEntity = R2DT::MiniEntity;
using Columns = R2DT::RenderInputColumns<Provider, Dim>;
using Registry = R2DT::AssetRegistry<Provider, Dim>;
using DecodedImage = R2DT::DecodedImage;

// GPU-path runtime aliases (mirror vulkan_textured_sprite_render_test).
using BufferRef = R2D::BufferRef<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using DescriptorRuntime = R2D::VulkanDescriptorRuntime<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;
using SamplerRuntime = R2D::VulkanSamplerRuntime<Provider, Dim>;
using SamplerRef = R2D::SamplerRef<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using SpritePipelineRuntime = R2D::VulkanSpritePipelineRuntime<Provider, Dim>;
using SpriteRenderEncoder = R2D::VulkanSpriteRenderEncoder<Provider, Dim>;
using SpriteRenderEncoderConfig = R2D::VulkanSpriteRenderEncoderConfig;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;

constexpr const char* kGreenPath = "render2d_asset_scene_green.png";
constexpr const char* kRedPath = "render2d_asset_scene_red.png";

inline constexpr R2D::U32 kEntityCount = 12U;
inline constexpr R2D::U32 kVisibleMask = 0xFFFF'FFFFU;

inline constexpr Camera kCamera{
    .source_id = 0U,
    .position_x = 0.0F,
    .position_y = 0.0F,
    .rotation_radians = 0.0F,
    .viewport_width = 16.0F,
    .viewport_height = 16.0F,
    .near_z = 0.0F,
    .far_z = 1.0F,
    .layer_mask = kVisibleMask,
    .flags = 0U,
};

struct ChainCounts {
    R2D::SystemStatusCode code;
    R2D::U32 visible_count;
    R2D::U32 batch_count;
};

[[nodiscard]] ChainCounts runChain(
    const Camera& camera_,
    std::span<const Transform> transforms_,
    std::span<const LocalBounds> local_bounds_,
    std::span<const VisibilityMask> visibility_masks_,
    std::span<const Sprite> sprites_,
    Arena& arena_)
{
    arena_.resizeForEntities(transforms_.size());

    auto result = SpatialCull::run(
        camera_, transforms_, local_bounds_, visibility_masks_,
        arena_.worldTransforms(), arena_.visibleItems());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = 0U, .batch_count = 0U};
    }
    const R2D::U32 visible_count = result.write_count;

    result = CommandBuild::run(
        std::span<const VisibleItem>{arena_.visibleItems().data(), visible_count},
        sprites_, arena_.drawCommands());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }

    result = SpriteInstanceBuild::run(
        std::span<const DrawCommand>{arena_.drawCommands().data(), visible_count},
        arena_.worldTransforms(), sprites_, arena_.spriteInstances());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }

    result = Batch::run(
        std::span<const DrawCommand>{arena_.drawCommands().data(), visible_count},
        arena_.batchCommands());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }
    return {.code = R2D::SystemStatusCode::Ok, .visible_count = visible_count, .batch_count = result.write_count};
}

void fillSolidRgba(
    R2D::McVector<R2D::U8>& out_,
    R2D::U32 width_,
    R2D::U32 height_,
    R2D::U8 r_,
    R2D::U8 g_,
    R2D::U8 b_,
    R2D::U8 a_)
{
    const auto pixel_count = static_cast<R2D::Usize>(width_) * static_cast<R2D::Usize>(height_);
    out_.resize(pixel_count * 4U);
    for (R2D::Usize pixel = 0U; pixel < pixel_count; ++pixel) {
        out_[pixel * 4U + 0U] = r_;
        out_[pixel * 4U + 1U] = g_;
        out_[pixel * 4U + 2U] = b_;
        out_[pixel * 4U + 3U] = a_;
    }
}

// Author two solid PNGs on disk, load them back through the registry (real decode),
// register materials, and verify the decode round-trip + handle lookups. Returns
// false (after recording the failure) if the PNGs could not be written/loaded.
[[nodiscard]] bool authorAssets(R2DT::TestContext& context_, Registry& registry_)
{
    constexpr R2D::U32 kTexW = 4U;
    constexpr R2D::U32 kTexH = 4U;
    R2D::McVector<R2D::U8> green{};
    R2D::McVector<R2D::U8> red{};
    fillSolidRgba(green, kTexW, kTexH, 0x00U, 0xFFU, 0x00U, 0xFFU);
    fillSolidRgba(red, kTexW, kTexH, 0xFFU, 0x00U, 0x00U, 0xFFU);

    const bool wrote_green = R2DT::writeImageRgba8Png(kGreenPath, kTexW, kTexH, green.data());
    const bool wrote_red = R2DT::writeImageRgba8Png(kRedPath, kTexW, kTexH, red.data());
    R2D_TEST_CHECK(context_, wrote_green);
    R2D_TEST_CHECK(context_, wrote_red);
    if (!wrote_green || !wrote_red) {
        return false;
    }

    const auto green_handle = registry_.loadTexture("green", kGreenPath);
    const auto red_handle = registry_.loadTexture("red", kRedPath);
    R2D_TEST_CHECK(context_, Registry::valid(green_handle));
    R2D_TEST_CHECK(context_, Registry::valid(red_handle));
    R2D_TEST_CHECK_EQ(context_, registry_.textureCount(), 2U);

    // The decode round-tripped the real PNG: right dimensions and a green texel.
    const DecodedImage* green_img = registry_.image(green_handle);
    R2D_TEST_CHECK(context_, green_img != nullptr);
    if (green_img != nullptr) {
        R2D_TEST_CHECK_EQ(context_, green_img->width, kTexW);
        R2D_TEST_CHECK_EQ(context_, green_img->height, kTexH);
        R2D_TEST_CHECK_EQ(context_, green_img->pixels.size(),
            static_cast<R2D::Usize>(kTexW) * kTexH * 4U);
        if (green_img->pixels.size() >= 4U) {
            R2D_TEST_CHECK_EQ(context_, static_cast<R2D::U32>(green_img->pixels[0U]), 0x00U);
            R2D_TEST_CHECK_EQ(context_, static_cast<R2D::U32>(green_img->pixels[1U]), 0xFFU);
            R2D_TEST_CHECK_EQ(context_, static_cast<R2D::U32>(green_img->pixels[2U]), 0x00U);
            R2D_TEST_CHECK_EQ(context_, static_cast<R2D::U32>(green_img->pixels[3U]), 0xFFU);
        }
    }

    // Name lookup hits and misses.
    R2D_TEST_CHECK(context_, registry_.findTexture("green").id == green_handle.id);
    R2D_TEST_CHECK(context_, !Registry::valid(registry_.findTexture("missing")));

    registry_.registerMaterial("opaque");
    registry_.registerMaterial("alpha");
    R2D_TEST_CHECK_EQ(context_, registry_.materialCount(), 2U);
    return true;
}

// Author a MiniEcs scene whose sprites reference the loaded assets and prove the
// CPU chain culls, batches, and carries the asset texture ids into the draws.
void runCpuScene(R2DT::TestContext& context_, const Registry& registry_)
{
    const auto green = registry_.findTexture("green");
    const auto red = registry_.findTexture("red");
    const auto mat_a = registry_.findMaterial("opaque");
    const auto mat_b = registry_.findMaterial("alpha");

    SceneEcs ecs{};
    ecs.reserve(kEntityCount);
    for (R2D::U32 index = 0U; index < kEntityCount; ++index) {
        const bool far_offscreen = (index % 4U) == 1U;
        const bool masked_off = (index % 6U) == 0U;
        const R2D::U32 row = index / 4U;
        const auto grid_x = static_cast<float>(index % 4U) - 2.0F;
        const auto grid_y = static_cast<float>(row) - 1.5F;

        const MiniEntity entity = ecs.create();
        ecs.add<Transform>(entity, Transform{
            .source_id = index,
            .position_x = far_offscreen ? 5000.0F : grid_x,
            .position_y = far_offscreen ? 5000.0F : grid_y,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        });
        ecs.add<LocalBounds>(entity, LocalBounds{
            .source_id = index,
            .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F),
        });
        ecs.add<VisibilityMask>(entity, VisibilityMask{.mask = masked_off ? 0U : kVisibleMask});
        const auto texture = ((index % 2U) == 0U) ? green : red;
        const auto material = ((index % 3U) == 0U) ? mat_a : mat_b;
        ecs.add<Sprite>(entity, registry_.makeSprite(texture, material, index, 0xFFFFFFFFU, 1U));
    }

    Columns columns{};
    R2DT::gatherRenderInputs(ecs, columns);
    R2D_TEST_CHECK_EQ(context_, columns.size(), static_cast<R2D::Usize>(kEntityCount));

    Arena arena{};
    const auto counts = runChain(
        kCamera,
        columns.transformSpan(),
        columns.localBoundsSpan(),
        columns.visibilityMaskSpan(),
        columns.spriteSpan(),
        arena);
    R2D_TEST_CHECK_EQ(context_, static_cast<int>(counts.code), static_cast<int>(R2D::SystemStatusCode::Ok));
    R2D_TEST_CHECK(context_, counts.visible_count > 0U);
    R2D_TEST_CHECK(context_, counts.visible_count < kEntityCount);
    R2D_TEST_CHECK(context_, counts.batch_count > 0U);

    // Every visible draw command must carry one of the loaded textures' ids.
    bool all_ids_are_assets = true;
    for (R2D::U32 index = 0U; index < counts.visible_count; ++index) {
        const R2D::U32 texture_id = arena.drawCommands()[index].texture_id;
        if (texture_id != green.id && texture_id != red.id) {
            all_ids_are_assets = false;
        }
    }
    R2D_TEST_CHECK(context_, all_ids_are_assets);

    std::fprintf(
        stdout,
        "asset_scene_render: %u entities -> %u visible, %u batches; textures from loaded PNGs\n",
        kEntityCount,
        counts.visible_count,
        counts.batch_count);
}

constexpr SpriteVertex makeVertex(float position_x_, float position_y_, float uv_x_, float uv_y_) noexcept
{
    return {.position_x = position_x_, .position_y = position_y_, .uv_x = uv_x_, .uv_y = uv_y_};
}

constexpr SpriteInstance makeFullscreenInstance() noexcept
{
    return {
        .transform_m00 = 1.0F, .transform_m01 = 0.0F, .transform_m02 = 0.0F,
        .transform_m10 = 0.0F, .transform_m11 = 1.0F, .transform_m12 = 0.0F,
        .uv_min_x = 0.0F, .uv_min_y = 0.0F, .uv_max_x = 1.0F, .uv_max_y = 1.0F,
        .source_index = 0U, .source_id = 1U,
        .texture_id = 1U, .texture_generation = 0U,
        .material_id = 1U, .material_generation = 0U,
        .color_rgba8 = 0xFFFFFFFFU, .sort_key = 0U, .layer = 0U, .flags = 0U,
        .sampler_index = 0U,
    };
}

// Upload the loaded texture to the GPU, sample it across a full-screen sprite via
// the unchanged sprite pipeline, read back, and assert every pixel is its green.
// Runtimes are stack-local and RAII (their destructors call shutdown), so only the
// shader modules need explicit cleanup.
void renderLoadedTexture(
    R2DT::TestContext& context_,
    const Render2DTest::VulkanSmokeContext& smoke_,
    const DecodedImage& texture_)
{
    constexpr R2D::U32 kWidth = 4U;
    constexpr R2D::U32 kHeight = 4U;
    constexpr R2D::U64 kReadbackByteCount = static_cast<R2D::U64>(kWidth) * kHeight * 4U;
    const auto texture_byte_count = static_cast<R2D::U64>(texture_.pixels.size());

    const std::array<SpriteVertex, 6U> vertices{{
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, -1.0F, 1.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(-1.0F, 1.0F, 0.0F, 1.0F),
    }};
    const std::array<SpriteInstance, 1U> instances{{makeFullscreenInstance()}};
    const auto vertex_byte_count = static_cast<R2D::U64>(sizeof(vertices));
    const auto instance_byte_count = static_cast<R2D::U64>(sizeof(instances));

    R2D::VulkanResourceRuntime<Provider, Dim> resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = smoke_.physical_device,
        .device = smoke_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.reserveImages(2U).code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.reserveBuffers(4U).code == R2D::NativeStatusCode::Ok);

    ImageRef color_target{};
    result = resource_runtime.createImageRef(
        kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, color_target);
    assert(result.code == R2D::NativeStatusCode::Ok);

    ImageRef texture_image{};
    result = resource_runtime.createImageRef(
        texture_.width, texture_.height, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, texture_image);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef texture_upload_buffer{};
    result = resource_runtime.createBufferRef(
        texture_byte_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, R2D::NativeMemoryDomain::Upload, texture_upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(texture_upload_buffer, texture_.pixels.data(), texture_byte_count, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef vertex_buffer{};
    result = resource_runtime.createBufferRef(
        vertex_byte_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, R2D::NativeMemoryDomain::Upload, vertex_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(vertex_buffer, vertices.data(), vertex_byte_count, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef instance_buffer{};
    result = resource_runtime.createBufferRef(
        instance_byte_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, R2D::NativeMemoryDomain::Upload, instance_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(instance_buffer, instances.data(), instance_byte_count, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef readback_buffer{};
    result = resource_runtime.createBufferRef(
        kReadbackByteCount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, R2D::NativeMemoryDomain::Readback, readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    DescriptorRuntime descriptor_runtime;
    result = descriptor_runtime.initialize(
        SpritePipelineRuntime::makeDescriptorRuntimeConfig(
            smoke_.device, 1U, R2D::kVulkanSpriteTextureDescriptorCount, 0U, 0U));
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(descriptor_runtime.reserveDescriptorSets(1U).code == R2D::NativeStatusCode::Ok);
    DescriptorSlice descriptor_slice{};
    result = descriptor_runtime.allocateDescriptorSlice(0U, 1U, descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);

    SamplerRuntime sampler_runtime;
    result = sampler_runtime.initialize({.device = smoke_.device});
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(sampler_runtime.reserveSamplers(1U).code == R2D::NativeStatusCode::Ok);
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
        descriptor_slice, 0U, texture_image, resource_runtime, native_sampler,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    assert(result.code == R2D::NativeStatusCode::Ok);

    PipelineRuntime pipeline_runtime;
    result = pipeline_runtime.initialize({.device = smoke_.device, .pipeline_cache_flags = 0U});
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(pipeline_runtime.reservePipelines(1U).code == R2D::NativeStatusCode::Ok);
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
        .device = smoke_.device,
        .queue_family_index = smoke_.queue_family_index,
        .command_pool_flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(command_runtime.reserveCommandBuffers(1U).code == R2D::NativeStatusCode::Ok);
    NativeCommandBufferRef command_ref{};
    result = command_runtime.allocateCommandBufferRef(
        0U, {.first = 0U, .count = 0U}, {.first = 0U, .count = 0U}, 0U, command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = command_runtime.beginCommandBuffer(command_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    VkCommandBuffer native_command_buffer = VK_NULL_HANDLE;
    result = command_runtime.resolveNativeCommandBuffer(command_ref, native_command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = resource_runtime.transitionImageLayout(
        native_command_buffer, texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyBufferToImage(native_command_buffer, texture_upload_buffer, texture_image);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.transitionImageLayout(
        native_command_buffer, texture_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    const std::array<DescriptorSlice, 1U> descriptor_slices{descriptor_slice};
    const SpriteRenderEncoderConfig render_config{
        .vertex_buffer_offset = 0U,
        .instance_buffer_offset = 0U,
        .width = kWidth,
        .height = kHeight,
        .clear_color_rgba8 = 0x000000FFU,
        .vertex_count = static_cast<R2D::U32>(vertices.size()),
        .instance_count = static_cast<R2D::U32>(instances.size()),
        .first_vertex = 0U,
        .first_instance = 0U,
        .flags = 0U,
    };
    result = SpriteRenderEncoder::record(
        command_ref, color_target, pipeline_ref, vertex_buffer, instance_buffer, descriptor_slices,
        command_runtime, resource_runtime, pipeline_runtime, descriptor_runtime, render_config);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = resource_runtime.transitionImageLayout(
        native_command_buffer, color_target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyImageToBuffer(native_command_buffer, color_target, readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordBufferBarrier(
        native_command_buffer, readback_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = command_runtime.endCommandBuffer(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSyncRuntime<Provider, Dim> sync_runtime;
    result = sync_runtime.initialize({.device = smoke_.device, .fence_create_flags = 0U});
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(sync_runtime.reserveFrameSyncs(1U).code == R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    result = sync_runtime.createFrameSync(0U, 0U, frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSubmitRuntime<Provider, Dim> submit_runtime;
    result = submit_runtime.initialize({
        .queue = smoke_.queue,
        .wait_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(submit_runtime.reserveCommandBuffers(1U).code == R2D::NativeStatusCode::Ok);
    const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
    result = submit_runtime.submit(command_refs, frame_sync, command_runtime, sync_runtime, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.waitFence(frame_sync, 1'000'000'000ULL);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<R2D::U8, static_cast<R2D::Usize>(kReadbackByteCount)> pixels{};
    result = resource_runtime.readBuffer(readback_buffer, pixels.data(), kReadbackByteCount, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    bool all_green = true;
    for (R2D::Usize offset = 0U; offset < pixels.size(); offset += 4U) {
        all_green = all_green
            && static_cast<R2D::U32>(pixels[offset + 0U]) == 0x00U
            && static_cast<R2D::U32>(pixels[offset + 1U]) == 0xFFU
            && static_cast<R2D::U32>(pixels[offset + 2U]) == 0x00U
            && static_cast<R2D::U32>(pixels[offset + 3U]) == 0xFFU;
    }
    R2D_TEST_CHECK(context_, all_green);

    result = pipeline_runtime.destroyShaderModule(fragment_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.destroyShaderModule(vertex_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::fputs("asset_scene_render: loaded texture sampled + rendered on GPU; readback == green\n", stdout);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};

    Registry registry{};
    if (!authorAssets(context, registry)) {
        return context.result();
    }
    runCpuScene(context, registry);

    Render2DTest::VulkanSmokeContext smoke{};
    if (Render2DTest::createVulkanSmokeContext(smoke)) {
        if (smoke.supports_dynamic_rendering) {
            const DecodedImage* green = registry.image(registry.findTexture("green"));
            if (green != nullptr) {
                renderLoadedTexture(context, smoke, *green);
            }
        }
        else {
            std::fputs("asset_scene_render: device lacks dynamic rendering; GPU draw skipped\n", stdout);
        }
        Render2DTest::destroyVulkanSmokeContext(smoke);
    }
    else {
        std::fputs("asset_scene_render: no Vulkan device; GPU draw skipped (CPU scene validated)\n", stdout);
    }

    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("asset_scene_render_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("asset_scene_render_test unknown exception\n", stderr);
        return 1;
    }
}
