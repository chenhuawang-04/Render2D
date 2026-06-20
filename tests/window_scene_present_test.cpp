// Window test framework usability proof: author a scene with the test-only
// MiniEcs + AssetRegistry, run it through the unchanged span-only CPU chain, and
// present the resulting sprites to a REAL SDL window via the WindowTestHarness,
// verifying the on-screen swapchain image matches the offscreen render baseline.
//
//   CPU (always runs): write a PNG, load it through the AssetRegistry (a real stb
//   decode), register a material, and author a MiniEcs scene whose sprites
//   reference those asset handles. The scene runs through the chain (SpatialCull
//   -> CommandBuild -> SpriteInstanceBuild -> Batch) into a frame arena, yielding
//   the SpriteInstance[] the GPU will draw. We assert it culls and batches.
//
//   GPU (skipped without a window/device): bring up a window + present-capable
//   Vulkan via WindowTestHarness, render the host-built sprite instances into an
//   offscreen image (the swapchain's exact format), copy that onto the acquired
//   swapchain image, read both back, present, and assert the swapchain capture ==
//   the offscreen baseline byte-for-byte (and that the frame actually varies --
//   the authored sprites drew).
//
// This is the new harness exercising the full window/present path with almost no
// per-test plumbing: the WindowTestHarness owns the window/instance/device
// bring-up and the swapchain/acquire/capture/present helpers (the same ones the
// Stage 22D/22E/23D capture tests share). The render here uses the color-only
// sprite path (each instance's own affine + colour, no descriptor binding) -- the
// proven Stage 23D present path; textured-sampled-on-GPU is already covered by
// render2d.asset_scene_render. Authored directly in the sprite shader's NDC clip
// space (camera = NDC box, sprites in NDC quadrants), since clip projection is
// the host's vertex-shader concern (ProjectMergeTODO #32), exactly as 23D does.
//
// Test-only scaffolding: MiniEcs/AssetRegistry and the present-host (SDL3) are out
// of Render2D's core scope (the host engine owns them at merge). Every bring-up
// step is a graceful skip on a headless/limited box. McVector only.

#include <Render2D/Render2D.hpp>

#include <Render2D/Present/PresentHost.hpp>

#include "Render2D/Memory/RenderVector.hpp"
#include "support/AssetRegistry.hpp"
#include "support/HostLikeEcs.hpp"
#include "support/MiniEcs.hpp"
#include "support/SpriteShaders.hpp"
#include "support/TestHarness.hpp"
#include "support/WindowTestHarness.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdio>
#include <exception>
#include <iostream>
#include <span>
#include <string_view>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;

using Transform = R2D::Transform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using Camera = R2D::Camera<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;

using SpatialCull = R2D::SpatialCullSystem<Provider, Dim>;
using CommandBuild = R2D::CommandBuildSystem<Provider, Dim>;
using SpriteInstanceBuild = R2D::SpriteInstanceBuildSystem<Provider, Dim>;
using Batch = R2D::BatchSystem<Provider, Dim>;

using SceneEcs = R2DT::SceneEcs<Provider, Dim>;
using MiniEntity = R2DT::MiniEntity;
using Columns = R2DT::RenderInputColumns<Provider, Dim>;
using Arena = R2DT::HostFrameArena<Provider, Dim>;
using Registry = R2DT::AssetRegistry<Provider, Dim>;
using DecodedImage = R2DT::DecodedImage;

using SwapchainRuntime = R2D::VulkanSwapchainRuntime<Provider, Dim>;
using PresentRuntime = R2D::VulkanPresentRuntime<Provider, Dim>;
using SyncRuntime = R2D::VulkanSyncRuntime<Provider, Dim>;
using CommandRuntime = R2D::VulkanCommandRuntime<Provider, Dim>;
using SubmitRuntime = R2D::VulkanSubmitRuntime<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;
using DescriptorRuntime = R2D::VulkanDescriptorRuntime<Provider, Dim>;
using SpritePipelineRuntime = R2D::VulkanSpritePipelineRuntime<Provider, Dim>;
using SpriteRenderEncoder = R2D::VulkanSpriteRenderEncoder<Provider, Dim>;
using SpriteRenderEncoderConfig = R2D::VulkanSpriteRenderEncoderConfig;

using FrameSync = R2D::FrameSync<Provider, Dim>;
using AcquiredImage = R2D::AcquiredImage<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using ActiveSwapchain = Render2DTest::ActiveSwapchain<Provider, Dim>;

constexpr const char* kLogPrefix = "window-scene";
constexpr const char* kTexturePath = "render2d_window_scene.png";

constexpr R2D::U32 kBytesPerPixel = 4U;
constexpr R2D::U64 kFenceTimeoutNs = 1'000'000'000ULL;
constexpr R2D::U64 kAcquireTimeoutNs = 1'000'000'000ULL;
constexpr R2D::U32 kClearColorRgba8 = 0x000000FFU; // opaque black; sprites draw over it

// Visible-mode safety cap. At FIFO vsync (~60 Hz) this is many minutes of
// on-screen time; the real exit is the user closing the window (the X, or Esc).
// The cap only guarantees the loop terminates if no close event ever arrives.
constexpr R2D::U32 kMaxVisibleFrames = 100'000U;

// The scene, authored directly in the sprite shader's clip space. Camera viewport
// 2x2 at the origin => an NDC [-1,1] cull box; visible sprites sit in the four NDC
// quadrants at scale 0.30 (the full-screen quad mesh maps to a [pos +/- 0.30]
// rect), each a distinct opaque colour. The rest are culled (bounds or layer
// mask), so visible_count (4) is strictly between 0 and the entity count.
constexpr R2D::U32 kVisibleSpriteCount = 4U;
constexpr R2D::U32 kEntityCount = 12U;
constexpr float kVisibleScale = 0.30F;
constexpr R2D::U32 kVisibleMask = 0xFFFF'FFFFU;
constexpr R2D::U32 kHiddenMask = 0U;

constexpr Camera kCamera{
    .source_id = 0U,
    .position_x = 0.0F,
    .position_y = 0.0F,
    .rotation_radians = 0.0F,
    .viewport_width = 2.0F,
    .viewport_height = 2.0F,
    .near_z = 0.0F,
    .far_z = 1.0F,
    .layer_mask = kVisibleMask,
    .flags = 0U,
};

// rgba8 is consumed by an R8G8B8A8_UNORM vertex attribute (low byte = R).
constexpr std::array<R2D::U32, kVisibleSpriteCount> kVisibleColors{{
    0xFF0000FFU, // red
    0xFF00FF00U, // green
    0xFFFF0000U, // blue
    0xFF00FFFFU, // yellow
}};
constexpr std::array<float, kVisibleSpriteCount> kVisibleX{{-0.45F, 0.45F, -0.45F, 0.45F}};
constexpr std::array<float, kVisibleSpriteCount> kVisibleY{{-0.45F, -0.45F, 0.45F, 0.45F}};

// One full-screen quad (two triangles) in NDC; the per-instance affine shrinks +
// translates it into the sprite's clip-space rect. Same mesh the sprite tests use.
constexpr SpriteVertex makeVertex(float position_x_, float position_y_, float uv_x_, float uv_y_) noexcept
{
    return {.position_x = position_x_, .position_y = position_y_, .uv_x = uv_x_, .uv_y = uv_y_};
}

constexpr std::array<SpriteVertex, 6U> kQuadVertices{{
    makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
    makeVertex(1.0F, -1.0F, 1.0F, 0.0F),
    makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
    makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
    makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
    makeVertex(-1.0F, 1.0F, 0.0F, 1.0F),
}};

void fillSolidRgba(R2D::McVector<R2D::U8>& out_, R2D::U32 width_, R2D::U32 height_)
{
    const auto pixel_count = static_cast<R2D::Usize>(width_) * static_cast<R2D::Usize>(height_);
    out_.resize(pixel_count * 4U);
    for (R2D::Usize pixel = 0U; pixel < pixel_count; ++pixel) {
        out_[pixel * 4U + 0U] = 0x40U;
        out_[pixel * 4U + 1U] = 0x80U;
        out_[pixel * 4U + 2U] = 0xC0U;
        out_[pixel * 4U + 3U] = 0xFFU;
    }
}

// Write a PNG, load it through the registry (a real decode), and register a
// material. Returns invalid handles (after recording the failure) on I/O failure.
struct SceneAssets {
    Registry::TextureHandle texture{};
    Registry::MaterialHandle material{};
};

[[nodiscard]] SceneAssets authorAssets(R2DT::TestContext& context_, Registry& registry_)
{
    constexpr R2D::U32 kTexW = 4U;
    constexpr R2D::U32 kTexH = 4U;
    R2D::McVector<R2D::U8> pixels{};
    fillSolidRgba(pixels, kTexW, kTexH);

    const bool wrote = R2DT::writeImageRgba8Png(kTexturePath, kTexW, kTexH, pixels.data());
    R2D_TEST_CHECK(context_, wrote);
    if (!wrote) {
        return {};
    }

    const auto texture = registry_.loadTexture("scene", kTexturePath);
    const auto material = registry_.registerMaterial("opaque");
    R2D_TEST_CHECK(context_, Registry::valid(texture));
    R2D_TEST_CHECK(context_, Registry::valid(material));

    // The decode round-tripped the real PNG (right dimensions).
    const DecodedImage* image = registry_.image(texture);
    R2D_TEST_CHECK(context_, image != nullptr);
    if (image != nullptr) {
        R2D_TEST_CHECK_EQ(context_, image->width, kTexW);
        R2D_TEST_CHECK_EQ(context_, image->height, kTexH);
    }
    return {.texture = texture, .material = material};
}

// Author the NDC scene into the MiniEcs, with every sprite referencing the loaded
// asset handles (so a host-style author-it-yourself scene drives the frame).
void buildScene(SceneEcs& ecs_, const Registry& registry_, const SceneAssets& assets_)
{
    ecs_.reserve(kEntityCount);
    const LocalBounds local_bounds{.source_id = 0U, .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F)};

    for (R2D::U32 index = 0U; index < kEntityCount; ++index) {
        Transform transform{
            .source_id = index,
            .position_x = 0.0F,
            .position_y = 0.0F,
            .rotation_radians = 0.0F,
            .scale_x = kVisibleScale,
            .scale_y = kVisibleScale,
        };
        R2D::U32 mask = kVisibleMask;
        R2D::U32 color = 0xFF808080U; // neutral; only the visible sprites' colours show

        if (index < kVisibleSpriteCount) {
            transform.position_x = kVisibleX[index];
            transform.position_y = kVisibleY[index];
            color = kVisibleColors[index];
        } else if (index < kVisibleSpriteCount + 4U) {
            // Bounds-culled: far outside the NDC box.
            transform.position_x = 5000.0F + static_cast<float>(index);
            transform.position_y = 5000.0F;
            transform.scale_x = 1.0F;
            transform.scale_y = 1.0F;
        } else {
            // Mask-culled: inside the box but the layer mask is zero.
            transform.position_x = 0.1F * static_cast<float>(index);
            transform.position_y = -0.1F * static_cast<float>(index);
            mask = kHiddenMask;
        }

        const MiniEntity entity = ecs_.create();
        ecs_.add<Transform>(entity, transform);
        ecs_.add<LocalBounds>(entity, local_bounds);
        ecs_.add<VisibilityMask>(entity, VisibilityMask{.mask = mask});
        ecs_.add<Sprite>(entity, registry_.makeSprite(assets_.texture, assets_.material, index, color, 0U));
    }
}

struct ChainCounts {
    R2D::SystemStatusCode code;
    R2D::U32 visible_count;
    R2D::U32 batch_count;
};

// The production CPU front-end + sprite chain, fed entirely through spans, into the
// frame arena. Produces the SpriteInstance[] the GPU will draw.
[[nodiscard]] ChainCounts runChain(const Columns& columns_, Arena& arena_)
{
    arena_.resizeForEntities(columns_.size());

    auto result = SpatialCull::run(
        kCamera,
        columns_.transformSpan(),
        columns_.localBoundsSpan(),
        columns_.visibilityMaskSpan(),
        arena_.worldTransforms(),
        arena_.visibleItems());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = 0U, .batch_count = 0U};
    }
    const R2D::U32 visible_count = result.write_count;

    result = CommandBuild::run(
        std::span<const VisibleItem>{arena_.visibleItems().data(), visible_count},
        columns_.spriteSpan(),
        arena_.drawCommands());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }

    result = SpriteInstanceBuild::run(
        std::span<const DrawCommand>{arena_.drawCommands().data(), visible_count},
        arena_.worldTransforms(),
        columns_.spriteSpan(),
        arena_.spriteInstances());
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

// Record the scene frame: render the host-built sprite instances into the offscreen
// image, then (via the harness capture tail) copy onto the swapchain image, read
// both back, and leave the swapchain image in PRESENT_SRC. Returns false on a
// resolve/record failure.
[[nodiscard]] bool recordSceneFrame(
    R2DT::TestContext& context_,
    const NativeCommandBufferRef& command_ref_,
    const ImageRef& offscreen_image_,
    const PipelineRef& pipeline_ref_,
    const BufferRef& vertex_buffer_,
    const BufferRef& instance_buffer_,
    const BufferRef& baseline_buffer_,
    const BufferRef& capture_buffer_,
    R2D::U32 instance_count_,
    VkImage swapchain_image_,
    VkExtent2D extent_,
    CommandRuntime& command_runtime_,
    ResourceRuntime& resource_runtime_,
    PipelineRuntime& pipeline_runtime_,
    DescriptorRuntime& descriptor_runtime_)
{
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.resolveNativeCommandBuffer(command_ref_, command_buffer).code,
        R2D::NativeStatusCode::Ok);
    if (!context_.ok() || command_buffer == VK_NULL_HANDLE) {
        return false;
    }

    const SpriteRenderEncoderConfig encoder_config{
        .vertex_buffer_offset = 0U,
        .instance_buffer_offset = 0U,
        .width = extent_.width,
        .height = extent_.height,
        .clear_color_rgba8 = kClearColorRgba8,
        .vertex_count = static_cast<R2D::U32>(kQuadVertices.size()),
        .instance_count = instance_count_,
        .first_vertex = 0U,
        .first_instance = 0U,
        .flags = 0U,
    };

    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.beginCommandBuffer(command_ref_, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT).code,
        R2D::NativeStatusCode::Ok);

    // Render the host-built sprite instances into the offscreen image. The
    // color-only sprite path binds no descriptor sets (empty slice span); each
    // instance carries its own affine + colour from the authored scene.
    R2D_TEST_CHECK_EQ(
        context_,
        SpriteRenderEncoder::record(
            command_ref_,
            offscreen_image_,
            pipeline_ref_,
            vertex_buffer_,
            instance_buffer_,
            std::span<const DescriptorSlice>{},
            command_runtime_,
            resource_runtime_,
            pipeline_runtime_,
            descriptor_runtime_,
            encoder_config)
            .code,
        R2D::NativeStatusCode::Ok);

    // Offscreen -> baseline, onto the swapchain, swapchain -> capture, -> PRESENT_SRC.
    const bool tail = Render2DTest::recordOffscreenToSwapchainCapture(
        context_,
        command_buffer,
        offscreen_image_,
        swapchain_image_,
        baseline_buffer_,
        capture_buffer_,
        extent_,
        resource_runtime_);

    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.endCommandBuffer(command_ref_).code,
        R2D::NativeStatusCode::Ok);
    return tail && context_.ok();
}

// Bring up the GPU runtimes, build the color-only sprite pipeline, present one
// frame of the authored scene, and assert the swapchain capture == the offscreen
// baseline. Runtimes are locals here so their destructors tear down Vulkan objects
// while the harness device is still alive (the harness is destroyed by the caller
// after this returns).
void runWindowScene(
    R2DT::TestContext& context_,
    Render2DTest::WindowTestHarness& harness_,
    R2D::U32 instance_count_,
    std::span<const SpriteInstance> instances_,
    bool visible_)
{
    const VkDevice device = harness_.device();
    const VkPhysicalDevice physical_device = harness_.physicalDevice();
    const R2D::U32 queue_family_index = harness_.queueFamilyIndex();
    const VkQueue queue = harness_.queue();

    // Copy-onto-swapchain + readback needs TRANSFER_DST and TRANSFER_SRC on the
    // swapchain images. Both are optional surface capabilities; skip if missing.
    constexpr VkImageUsageFlags kRequiredUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!Render2DTest::surfaceSupportsUsage(physical_device, harness_.surface(), kRequiredUsage, kLogPrefix)) {
        return;
    }
    const R2D::U32 image_usage =
        static_cast<R2D::U32>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | static_cast<R2D::U32>(kRequiredUsage);

    SwapchainRuntime swapchain_runtime{};
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.initialize({.device = device}).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.reserveSwapchains(1U).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        swapchain_runtime.reserveSwapchainImages(Render2DTest::kWindowSwapchainImageCapacity).code,
        R2D::NativeStatusCode::Ok);

    PresentRuntime present_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        present_runtime.initialize({.device = device, .present_queue = queue}).code,
        R2D::NativeStatusCode::Ok);

    SyncRuntime sync_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        sync_runtime.initialize({.device = device, .fence_create_flags = 0U}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, sync_runtime.reserveFrameSyncs(1U).code, R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    R2D_TEST_CHECK_EQ(context_, sync_runtime.createFrameSync(0U, 0U, frame_sync).code, R2D::NativeStatusCode::Ok);

    CommandRuntime command_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime
            .initialize({.device = device, .queue_family_index = queue_family_index, .command_pool_flags = 0U})
            .code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, command_runtime.reserveCommandBuffers(1U).code, R2D::NativeStatusCode::Ok);
    NativeCommandBufferRef command_ref{};
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime
            .allocateCommandBufferRef(0U, {.first = 0U, .count = 0U}, {.first = 0U, .count = 0U}, 0U, command_ref)
            .code,
        R2D::NativeStatusCode::Ok);

    SubmitRuntime submit_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        submit_runtime.initialize({.queue = queue, .wait_stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, submit_runtime.reserveCommandBuffers(1U).code, R2D::NativeStatusCode::Ok);

    ResourceRuntime resource_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime.initialize({.physical_device = physical_device, .device = device}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.reserveImages(1U).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.reserveBuffers(4U).code, R2D::NativeStatusCode::Ok);

    PipelineRuntime pipeline_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        pipeline_runtime.initialize({.device = device, .pipeline_cache_flags = 0U}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.reservePipelines(1U).code, R2D::NativeStatusCode::Ok);

    DescriptorRuntime descriptor_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        descriptor_runtime
            .initialize(SpritePipelineRuntime::makeDescriptorRuntimeConfig(
                device,
                1U,
                R2D::kVulkanSpriteTextureDescriptorCount,
                0U,
                0U))
            .code,
        R2D::NativeStatusCode::Ok);

    if (!context_.ok()) {
        return;
    }

    ActiveSwapchain active{};
    if (Render2DTest::createCaptureSwapchain(physical_device, harness_.host(), queue_family_index, image_usage, swapchain_runtime, active) !=
        R2D::NativeStatusCode::Ok) {
        std::cout << kLogPrefix << ": swapchain unavailable (e.g. minimized), skipping\n";
        return;
    }

    const R2D::U64 byte_count =
        static_cast<R2D::U64>(active.extent.width) * active.extent.height * kBytesPerPixel;
    const auto vertex_byte_count = static_cast<R2D::U64>(sizeof(kQuadVertices));
    const auto instance_byte_count = static_cast<R2D::U64>(instance_count_) * sizeof(SpriteInstance);

    ImageRef offscreen_image{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createImageRef(
                active.extent.width,
                active.extent.height,
                active.format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                offscreen_image)
            .code,
        R2D::NativeStatusCode::Ok);

    BufferRef vertex_buffer{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createBufferRef(vertex_byte_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, R2D::NativeMemoryDomain::Upload, vertex_buffer)
            .code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime.writeBuffer(vertex_buffer, kQuadVertices.data(), vertex_byte_count, 0U).code,
        R2D::NativeStatusCode::Ok);

    // The instance buffer IS the authored SpriteInstance[] -- the data path.
    BufferRef instance_buffer{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createBufferRef(instance_byte_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, R2D::NativeMemoryDomain::Upload, instance_buffer)
            .code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime.writeBuffer(instance_buffer, instances_.data(), instance_byte_count, 0U).code,
        R2D::NativeStatusCode::Ok);

    BufferRef baseline_buffer{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createBufferRef(byte_count, VK_BUFFER_USAGE_TRANSFER_DST_BIT, R2D::NativeMemoryDomain::Readback, baseline_buffer)
            .code,
        R2D::NativeStatusCode::Ok);
    BufferRef capture_buffer{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createBufferRef(byte_count, VK_BUFFER_USAGE_TRANSFER_DST_BIT, R2D::NativeMemoryDomain::Readback, capture_buffer)
            .code,
        R2D::NativeStatusCode::Ok);

    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        pipeline_runtime.createShaderModule(Render2DTest::kSpriteVertSpv, vertex_shader).code,
        R2D::NativeStatusCode::Ok);
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        pipeline_runtime.createShaderModule(Render2DTest::kSpriteFragSpv, fragment_shader).code,
        R2D::NativeStatusCode::Ok);

    PipelineRef pipeline_ref{};
    R2D_TEST_CHECK_EQ(
        context_,
        SpritePipelineRuntime::createPipelineRef(
            pipeline_runtime,
            {
                .vertex_shader = vertex_shader,
                .fragment_shader = fragment_shader,
                .descriptor_set_layout = descriptor_runtime.nativeDescriptorSetLayout(),
                .color_format = active.format,
                .flags = 0U,
            },
            pipeline_ref)
            .code,
        R2D::NativeStatusCode::Ok);

    if (!context_.ok()) {
        return;
    }

    AcquiredImage acquired{};
    VkImage swapchain_image = VK_NULL_HANDLE;
    if (Render2DTest::acquireSwapchainImage(
            context_,
            kLogPrefix,
            present_runtime,
            swapchain_runtime,
            sync_runtime,
            active,
            frame_sync,
            kAcquireTimeoutNs,
            acquired,
            swapchain_image) != Render2DTest::WindowFrameStatus::Ok) {
        return;
    }

    if (recordSceneFrame(
            context_,
            command_ref,
            offscreen_image,
            pipeline_ref,
            vertex_buffer,
            instance_buffer,
            baseline_buffer,
            capture_buffer,
            instance_count_,
            swapchain_image,
            active.extent,
            command_runtime,
            resource_runtime,
            pipeline_runtime,
            descriptor_runtime)) {
        if (Render2DTest::submitPresentWaitFence(
                context_,
                command_ref,
                frame_sync,
                acquired,
                submit_runtime,
                present_runtime,
                command_runtime,
                swapchain_runtime,
                sync_runtime,
                kFenceTimeoutNs)) {
            const auto comparison =
                Render2DTest::readbackVariesAndIdentical(context_, baseline_buffer, capture_buffer, byte_count, resource_runtime);
            R2D_TEST_CHECK(context_, comparison.varies);
            R2D_TEST_CHECK(context_, comparison.identical);
            std::cout << kLogPrefix << ": " << active.extent.width << "x" << active.extent.height
                      << " authored scene -> " << instance_count_ << " sprite instances presented; swapchain "
                      << (comparison.identical ? "==" : "!=") << " offscreen baseline (" << byte_count << " bytes)\n";
        }
    }

    // Visible mode: keep the window on screen, re-presenting the authored scene
    // each vsync until the user closes it (or the safety cap). The single frame
    // above already validated the present path by readback; this exists only so
    // the scene can actually be *seen*. It reuses the same runtimes / buffers /
    // pipeline / offscreen image -- the resource runtime tracks the offscreen
    // image's layout, so re-rendering it each frame transitions correctly -- and
    // simply overwrites (and ignores) the readback buffers. The window is not
    // resizable, so a non-Ok acquire (e.g. minimize -> OUT_OF_DATE) just ends the
    // demo cleanly. Same fence/pool discipline as present_loop_smoke (Stage 22C).
    if (visible_ && context_.ok()) {
        std::cout << kLogPrefix << ": window is visible -- close it (the X, or Esc) to exit\n" << std::flush;
        for (R2D::U32 frame = 0U; frame < kMaxVisibleFrames; ++frame) {
            if (harness_.host().pollShouldClose()) {
                break;
            }

            AcquiredImage loop_acquired{};
            VkImage loop_swapchain_image = VK_NULL_HANDLE;
            if (Render2DTest::acquireSwapchainImage(
                    context_,
                    kLogPrefix,
                    present_runtime,
                    swapchain_runtime,
                    sync_runtime,
                    active,
                    frame_sync,
                    kAcquireTimeoutNs,
                    loop_acquired,
                    loop_swapchain_image) != Render2DTest::WindowFrameStatus::Ok) {
                break;
            }

            // Reset the fence only now that a successful acquire guarantees a
            // submit will re-signal it, then reset the pool so the one command
            // buffer can be re-recorded for this frame.
            R2D_TEST_CHECK_EQ(context_, sync_runtime.resetFence(frame_sync).code, R2D::NativeStatusCode::Ok);
            R2D_TEST_CHECK_EQ(context_, command_runtime.resetCommandPool(0U).code, R2D::NativeStatusCode::Ok);
            if (!context_.ok()) {
                break;
            }

            if (!recordSceneFrame(
                    context_,
                    command_ref,
                    offscreen_image,
                    pipeline_ref,
                    vertex_buffer,
                    instance_buffer,
                    baseline_buffer,
                    capture_buffer,
                    instance_count_,
                    loop_swapchain_image,
                    active.extent,
                    command_runtime,
                    resource_runtime,
                    pipeline_runtime,
                    descriptor_runtime)) {
                break;
            }
            if (!Render2DTest::submitPresentWaitFence(
                    context_,
                    command_ref,
                    frame_sync,
                    loop_acquired,
                    submit_runtime,
                    present_runtime,
                    command_runtime,
                    swapchain_runtime,
                    sync_runtime,
                    kFenceTimeoutNs)) {
                break;
            }
        }
    }

    vkDeviceWaitIdle(device);

    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.releasePipelineRef(pipeline_ref).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.destroyShaderModule(fragment_shader).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.destroyShaderModule(vertex_shader).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(capture_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(baseline_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(instance_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(vertex_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseImageRef(offscreen_image).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, command_runtime.releaseCommandBufferRef(command_ref).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.releaseSwapchainState(active.state).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, sync_runtime.releaseFrameSync(frame_sync).code, R2D::NativeStatusCode::Ok);
}

[[nodiscard]] int runTest(bool visible_)
{
    R2DT::TestContext context{};

    // --- CPU: MiniEcs + AssetRegistry scene -> the span-only chain -> instances --
    Registry registry{};
    const SceneAssets assets = authorAssets(context, registry);
    if (!Registry::valid(assets.texture) || !Registry::valid(assets.material)) {
        return context.result();
    }

    SceneEcs ecs{};
    buildScene(ecs, registry, assets);

    Columns columns{};
    R2DT::gatherRenderInputs(ecs, columns);
    R2D_TEST_CHECK_EQ(context, columns.size(), static_cast<R2D::Usize>(kEntityCount));

    Arena arena{};
    const ChainCounts counts = runChain(columns, arena);
    R2D_TEST_CHECK_EQ(context, static_cast<int>(counts.code), static_cast<int>(R2D::SystemStatusCode::Ok));
    R2D_TEST_CHECK_EQ(context, counts.visible_count, kVisibleSpriteCount);
    R2D_TEST_CHECK(context, counts.visible_count < kEntityCount);
    R2D_TEST_CHECK(context, counts.batch_count > 0U);
    R2D_TEST_CHECK(context, counts.batch_count <= counts.visible_count);
    if (!context.ok()) {
        return context.result();
    }

    std::fprintf(
        stdout,
        "window-scene: %u entities -> %u visible, %u batches (assets loaded from PNG)\n",
        kEntityCount,
        counts.visible_count,
        counts.batch_count);

    const std::span<const SpriteInstance> instances{arena.spriteInstances().data(), counts.visible_count};

    // --- GPU: present the authored scene to a real window via the harness --------
    Render2DTest::WindowTestHarness harness{};
    if (harness.initialize({.window_title = "Render2D Window Scene (close to exit)",
                            .log_prefix = kLogPrefix,
                            .width = 640,
                            .height = 480,
                            .require_dynamic_rendering = true,
                            .visible = visible_}) != Render2DTest::WindowTestHarness::Status::Ready) {
        return context.result();
    }

    runWindowScene(context, harness, counts.visible_count, instances, visible_);
    return context.result();
}

} // namespace

int main(int argc, char** argv) noexcept
{
    // Default (e.g. under ctest, with no args): hidden window, validated by
    // readback. Pass --visible for a manual run that actually shows the window.
    bool visible = false;
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--visible") {
            visible = true;
        }
    }
    try {
        return runTest(visible);
    } catch (const std::exception& exception) {
        std::fputs("window_scene_present_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("window_scene_present_test unknown exception\n", stderr);
        return 1;
    }
}
