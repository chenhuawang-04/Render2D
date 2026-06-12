#include <Render2D/Render2D.hpp>

#include "Render2D/Font/BidiItemizeRuntime.hpp"
#include "Render2D/Font/FontShapeRuntime.hpp"
#include "Render2D/Font/GlyphAtlasRuntime.hpp"
#include "support/SpriteShaders.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <span>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;
using AtlasRuntime = R2D::VulkanTextureAtlasRuntime<Provider, Dim>;
using ShapeRuntime = R2D::FontShapeRuntime<Provider, Dim>;
using GlyphAtlas = R2D::GlyphAtlasRuntime<Provider, Dim>;
using BidiRuntime = R2D::BidiItemizeRuntime<Provider, Dim>;
using Text = R2D::Text<Provider, Dim>;
using Codepoint = R2D::Codepoint<Provider, Dim>;
using Utf8BufferView = R2D::Utf8BufferView;
using ShapingRun = R2D::ShapingRun<Provider, Dim>;
using ShapedGlyph = R2D::ShapedGlyph<Provider, Dim>;
using GlyphAtlasEntry = R2D::GlyphAtlasEntry<Provider, Dim>;
using GlyphInstance = R2D::GlyphInstance<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;
using SpriteDrawPacket = R2D::SpriteDrawPacket<Provider, Dim>;
using DescriptorRuntime = R2D::VulkanDescriptorRuntime<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;
using SamplerRuntime = R2D::VulkanSamplerRuntime<Provider, Dim>;
using SamplerRef = R2D::SamplerRef<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using SpritePipelineRuntime = R2D::VulkanSpritePipelineRuntime<Provider, Dim>;
using SpriteRenderEncoder = R2D::VulkanSpriteRenderEncoder<Provider, Dim>;
using SpriteRenderEncoderConfig = R2D::VulkanSpriteRenderEncoderConfig;
using FontRef = R2D::FontRef<Provider, Dim>;
using FontMetrics = R2D::FontMetrics<Provider, Dim>;

constexpr SpriteVertex makeVertex(float x_, float y_, float u_, float v_) noexcept
{
    return {.position_x = x_, .position_y = y_, .uv_x = u_, .uv_y = v_};
}

R2D::McVector<R2D::U8> readFile(const char* path_)
{
    R2D::McVector<R2D::U8> bytes;
    std::FILE* file = std::fopen(path_, "rb");
    if (file == nullptr) {
        return bytes;
    }
    if (std::fseek(file, 0, SEEK_END) == 0) {
        const long size = std::ftell(file);
        if (size > 0 && std::fseek(file, 0, SEEK_SET) == 0) {
            const auto expected = static_cast<std::size_t>(size);
            bytes.resize(expected);
            if (std::fread(bytes.data(), 1U, expected, file) != expected) {
                bytes.clear();
            }
        }
    }
    std::fclose(file);
    return bytes;
}

R2D::McVector<R2D::U8> loadSystemFont()
{
    static constexpr std::array<const char*, 5U> kCandidates{
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/verdana.ttf",
    };
    for (const char* candidate : kCandidates) {
        R2D::McVector<R2D::U8> bytes = readFile(candidate);
        if (!bytes.empty()) {
            return bytes;
        }
    }
    return {};
}

// End-to-end Stage 19 proof: decode -> bidi itemize -> shape -> glyph atlas
// residency (FreeType raster into an R8 Stage 18 atlas) -> position -> bridge to
// sprite instances -> real coverage-shader draw -> readback. Asserts the text
// produced non-empty coverage and did not fill the whole target (it has shape).
void testGlyphTextRender(const Render2DTest::VulkanSmokeContext& context_, std::span<const R2D::U8> font_bytes_)
{
    constexpr R2D::U32 kWidth = 64U;
    constexpr R2D::U32 kHeight = 32U;
    constexpr R2D::U64 kReadbackBytes = static_cast<R2D::U64>(kWidth) * kHeight * 4U;
    constexpr float kPixelSize = 16.0F;

    ResourceRuntime resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    auto capacity = resource_runtime.reserveImages(2U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    capacity = resource_runtime.reserveBuffers(4U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    AtlasRuntime atlas_runtime;
    result = atlas_runtime.initialize({.resource_runtime = &resource_runtime});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = atlas_runtime.reserveAtlases(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    ShapeRuntime shape_runtime;
    auto shape_result = shape_runtime.initialize();
    assert(shape_result.code == R2D::SystemStatusCode::Ok);
    FontRef font_ref{};
    FontMetrics metrics{};
    shape_result = shape_runtime.loadFontFromMemory(1U, font_bytes_, kPixelSize, font_ref, metrics);
    assert(shape_result.code == R2D::SystemStatusCode::Ok);

    GlyphAtlas glyph_atlas;
    shape_result = glyph_atlas.initialize({
        .resource_runtime = &resource_runtime,
        .atlas_runtime = &atlas_runtime,
        .atlas_width = 128U,
        .atlas_height = 64U,
        .padding = 1U,
    });
    assert(shape_result.code == R2D::SystemStatusCode::Ok);
    const ImageRef atlas_texture = glyph_atlas.atlasTexture();

    // CPU pipeline head: "Hi" -> codepoints -> shaping runs -> shaped glyphs.
    static constexpr std::array<R2D::U8, 2U> kBytes{0x48U, 0x69U}; // "Hi"
    const std::array<Utf8BufferView, 1U> buffers{{
        {.buffer_id = 0U, .bytes = kBytes.data(), .byte_count = static_cast<R2D::U32>(kBytes.size())},
    }};
    const std::array<Text, 1U> texts{{
        {
            .source_id = 1U, .font_id = 1U, .utf8_buffer_id = 0U, .utf8_offset = 0U, .utf8_size = 2U,
            .color_rgba8 = 0xFFFFFFFFU, .pixel_size = kPixelSize, .layer = 0U, .flags = 0U,
        },
    }};
    std::array<Codepoint, 8U> codepoints{};
    auto system_result = R2D::Utf8DecodeSystem<Provider, Dim>::run(texts, buffers, codepoints);
    assert(system_result.code == R2D::SystemStatusCode::Ok);
    const R2D::U32 codepoint_count = system_result.write_count;

    std::array<ShapingRun, 8U> runs_storage{};
    BidiRuntime bidi_runtime;
    system_result = bidi_runtime.itemize(
        std::span<const Codepoint>(codepoints.data(), codepoint_count), texts, runs_storage);
    assert(system_result.code == R2D::SystemStatusCode::Ok);
    const std::span<const ShapingRun> runs(runs_storage.data(), system_result.write_count);

    std::array<ShapedGlyph, 16U> shaped_storage{};
    system_result = shape_runtime.shape(
        runs, std::span<const Codepoint>(codepoints.data(), codepoint_count), shaped_storage);
    assert(system_result.code == R2D::SystemStatusCode::Ok);
    const std::span<const ShapedGlyph> shaped(shaped_storage.data(), system_result.write_count);
    assert(shaped.size() >= 2U);

    // GPU resources: unit-quad vertices (the bridge projects them via the affine),
    // an instance buffer, and a readback buffer.
    constexpr std::array<SpriteVertex, 6U> kVertices{{
        makeVertex(0.0F, 0.0F, 0.0F, 0.0F),
        makeVertex(1.0F, 0.0F, 1.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(0.0F, 0.0F, 0.0F, 0.0F),
        makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
        makeVertex(0.0F, 1.0F, 0.0F, 1.0F),
    }};
    constexpr auto kVertexBytes = static_cast<R2D::U64>(sizeof(kVertices));
    constexpr auto kInstanceBytes = static_cast<R2D::U64>(sizeof(SpriteInstance) * 16U);

    ImageRef color_target{};
    result = resource_runtime.createImageRef(
        kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, color_target);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef vertex_buffer{};
    result = resource_runtime.createBufferRef(kVertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, R2D::NativeMemoryDomain::Upload, vertex_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(vertex_buffer, kVertices.data(), kVertexBytes, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef instance_buffer{};
    result = resource_runtime.createBufferRef(kInstanceBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, R2D::NativeMemoryDomain::Upload, instance_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef readback_buffer{};
    result = resource_runtime.createBufferRef(kReadbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, R2D::NativeMemoryDomain::Readback, readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    DescriptorRuntime descriptor_runtime;
    result = descriptor_runtime.initialize(
        SpritePipelineRuntime::makeDescriptorRuntimeConfig(context_.device, 1U, R2D::kVulkanSpriteTextureDescriptorCount, 0U, 0U));
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = descriptor_runtime.reserveDescriptorSets(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    DescriptorSlice descriptor_slice{};
    result = descriptor_runtime.allocateDescriptorSlice(0U, 1U, descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);

    SamplerRuntime sampler_runtime;
    result = sampler_runtime.initialize({.device = context_.device});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = sampler_runtime.reserveSamplers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    SamplerRef sampler_ref{};
    result = sampler_runtime.createSamplerRef(
        {
            .mag_filter = VK_FILTER_NEAREST, .min_filter = VK_FILTER_NEAREST,
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
        descriptor_slice, 0U, atlas_texture, resource_runtime, native_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    assert(result.code == R2D::NativeStatusCode::Ok);

    PipelineRuntime pipeline_runtime;
    result = pipeline_runtime.initialize({.device = context_.device, .pipeline_cache_flags = 0U});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = pipeline_runtime.reservePipelines(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    result = pipeline_runtime.createShaderModule(Render2DTest::kSpriteVertSpv, vertex_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    result = pipeline_runtime.createShaderModule(Render2DTest::kGlyphCoverageFragSpv, fragment_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    PipelineRef pipeline_ref{};
    result = SpritePipelineRuntime::createPipelineRef(
        pipeline_runtime,
        {
            .vertex_shader = vertex_shader, .fragment_shader = fragment_shader,
            .descriptor_set_layout = descriptor_runtime.nativeDescriptorSetLayout(),
            .color_format = VK_FORMAT_R8G8B8A8_UNORM, .flags = 0U,
        },
        pipeline_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanCommandRuntime<Provider, Dim> command_runtime;
    result = command_runtime.initialize({
        .device = context_.device, .queue_family_index = context_.queue_family_index,
        .command_pool_flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = command_runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    NativeCommandBufferRef command_ref{};
    result = command_runtime.allocateCommandBufferRef(0U, {.first = 0U, .count = 0U}, {.first = 0U, .count = 0U}, 0U, command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = command_runtime.beginCommandBuffer(command_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = command_runtime.resolveNativeCommandBuffer(command_ref, command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    // Atlas: TRANSFER_DST -> rasterize+upload glyphs -> SHADER_READ.
    result = resource_runtime.transitionImageLayout(
        command_buffer, atlas_texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<GlyphAtlasEntry, 16U> entries_storage{};
    system_result = glyph_atlas.ensureResident(shaped, runs, texts, shape_runtime, command_buffer, entries_storage);
    assert(system_result.code == R2D::SystemStatusCode::Ok);
    const std::span<const GlyphAtlasEntry> entries(entries_storage.data(), system_result.write_count);
    assert(!entries.empty());

    result = resource_runtime.transitionImageLayout(
        command_buffer, atlas_texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    // CPU: position glyphs on a baseline, then bridge to sprite instances in clip
    // space via the viewport projection.
    std::array<GlyphInstance, 16U> glyph_instances{};
    system_result = R2D::GlyphPositionSystem<Provider, Dim>::run(
        shaped, runs, texts, entries, {.origin_x = 6.0F, .baseline_y = 22.0F, .flags = 0U}, glyph_instances);
    assert(system_result.code == R2D::SystemStatusCode::Ok);
    const std::span<const GlyphInstance> positioned(glyph_instances.data(), system_result.write_count);

    std::array<SpriteInstance, 16U> sprite_instances{};
    system_result = R2D::GlyphInstanceToSpriteSystem<Provider, Dim>::run(
        positioned,
        {
            .texture_id = atlas_texture.image_id, .texture_generation = atlas_texture.generation,
            .material_id = 1U, .material_generation = 1U,
            .viewport_width = static_cast<float>(kWidth), .viewport_height = static_cast<float>(kHeight), .flags = 0U,
        },
        sprite_instances);
    assert(system_result.code == R2D::SystemStatusCode::Ok);
    const R2D::U32 instance_count = system_result.write_count;
    assert(instance_count >= 1U);
    result = resource_runtime.writeBuffer(instance_buffer, sprite_instances.data(), static_cast<R2D::U64>(sizeof(SpriteInstance)) * instance_count, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    const std::array<SpriteDrawPacket, 1U> packets{{
        {
            .batch_index = 0U, .draw_first = 0U, .draw_count = instance_count,
            .instance_first = 0U, .instance_count = instance_count,
            .vertex_first = 0U, .vertex_count = static_cast<R2D::U32>(kVertices.size()),
            .index_first = 0U, .index_count = 0U,
            .material_id = 1U, .material_generation = 1U,
            .texture_id = atlas_texture.image_id, .texture_generation = atlas_texture.generation,
            .pipeline_id = pipeline_ref.pipeline_id, .pipeline_generation = pipeline_ref.generation,
            .descriptor_id = descriptor_slice.descriptor_set_id, .descriptor_generation = descriptor_slice.generation,
            .descriptor_first = descriptor_slice.first, .descriptor_count = descriptor_slice.count,
            .flags = 0U,
        },
    }};
    const SpriteRenderEncoderConfig render_config{
        .vertex_buffer_offset = 0U, .instance_buffer_offset = 0U,
        .width = kWidth, .height = kHeight, .clear_color_rgba8 = 0x000000FFU,
        .vertex_count = static_cast<R2D::U32>(kVertices.size()), .instance_count = instance_count,
        .first_vertex = 0U, .first_instance = 0U, .flags = 0U,
    };
    result = SpriteRenderEncoder::recordPackets(
        command_ref, color_target, vertex_buffer, instance_buffer, packets,
        command_runtime, resource_runtime, pipeline_runtime, descriptor_runtime, render_config);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = resource_runtime.transitionImageLayout(
        command_buffer, color_target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordCopyImageToBuffer(command_buffer, color_target, readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.recordBufferBarrier(
        command_buffer, readback_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = command_runtime.endCommandBuffer(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSyncRuntime<Provider, Dim> sync_runtime;
    result = sync_runtime.initialize({.device = context_.device, .fence_create_flags = 0U});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = sync_runtime.reserveFrameSyncs(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    result = sync_runtime.createFrameSync(0U, 0U, frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSubmitRuntime<Provider, Dim> submit_runtime;
    result = submit_runtime.initialize({.queue = context_.queue, .wait_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = submit_runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
    result = submit_runtime.submit(command_refs, frame_sync, command_runtime, sync_runtime, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.waitFence(frame_sync, 1'000'000'000ULL);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<R2D::U8, static_cast<R2D::Usize>(kReadbackBytes)> pixels{};
    result = resource_runtime.readBuffer(readback_buffer, pixels.data(), kReadbackBytes, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    // The glyphs must have produced coverage somewhere, but must not fill the
    // whole target (text has shape and background remains).
    R2D::U32 covered = 0U;
    for (R2D::U32 pixel = 0U; pixel < kWidth * kHeight; ++pixel) {
        if (pixels[static_cast<R2D::Usize>(pixel) * 4U] > 32U) {
            ++covered;
        }
    }
    assert(covered > 0U);
    assert(covered < kWidth * kHeight);

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
    result = resource_runtime.releaseImageRef(color_target);
    assert(result.code == R2D::NativeStatusCode::Ok);
    glyph_atlas.shutdown();
    shape_runtime.shutdown();
    atlas_runtime.shutdown();
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
        const R2D::McVector<R2D::U8> font_bytes = loadSystemFont();
        if (font_bytes.empty()) {
            std::fputs("vulkan_glyph_text_render_test: no system font found; skipping (pass).\n", stderr);
            Render2DTest::destroyVulkanSmokeContext(context);
            return 0;
        }
        testGlyphTextRender(context, std::span<const R2D::U8>(font_bytes.data(), font_bytes.size()));
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }
    return 0;
}
