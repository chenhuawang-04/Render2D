// Present-path frame-timing benchmark for the window/present pipeline.
//
// The Stage 22/23 present tests prove the on-screen path is *correct* (the
// swapchain capture is byte-identical to the offscreen baseline). None of them
// measure how *fast* it is. This benchmark closes that gap: it drives the same
// color-only sprite present path the WindowTestHarness already exercises, but in
// a timed loop, and reports per-frame latency percentiles + throughput.
//
//   CPU (always runs): author a scalable grid of N sprites in NDC clip space via
//   the test-only MiniEcs, run it through the unchanged span-only chain
//   (SpatialCull -> CommandBuild -> SpriteInstanceBuild -> Batch) into a frame
//   arena, and assert it produced exactly N visible instances + at least one
//   batch. This is the deterministic, machine-independent part -- it gives the
//   ctest smoke real teeth even on a headless box where the GPU path skips.
//
//   GPU (skipped without a window/device): bring up a window + present-capable
//   Vulkan via WindowTestHarness, build the color-only sprite pipeline, and run a
//   timed present loop -- acquire, render the N instances offscreen, copy onto the
//   acquired swapchain image, present, wait the fence -- collecting the wall-clock
//   per frame. After a warmup it reports avg / min / max / p50 / p99 frame latency
//   and an approximate FPS. IMMEDIATE/MAILBOX present mode is requested so the
//   measurement reflects real frame cost rather than the vsync interval (it falls
//   back to FIFO when the surface lacks the preferred mode).
//
// This is a measurement tool, not a regression gate: GPU timing is machine- and
// load-dependent, so (unlike the deterministic CPU perf gate in bench/) it asserts
// no wall-clock budget. The ctest case runs a handful of frames purely to keep the
// timed path compiled + exercised and graceful-skipping; the real numbers come
// from a manual run with larger --frames / --sprites. Pass --visible to watch it.
//
// Test-only scaffolding over the optional present-host (SDL3), out of Render2D's
// core scope: it changes no runtime/component/system, the host owns the window +
// instance/device + surface at merge (ProjectMergeTODO red line #11), and every
// bring-up step is a graceful skip on a headless/limited box. McVector only.

#include <Render2D/Render2D.hpp>

#include <Render2D/Present/PresentHost.hpp>

#include "Render2D/Memory/RenderVector.hpp"
#include "support/HostLikeEcs.hpp"
#include "support/MiniEcs.hpp"
#include "support/SpriteShaders.hpp"
#include "support/TestHarness.hpp"
#include "support/WindowTestHarness.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

constexpr const char* kLogPrefix = "window-bench";

constexpr R2D::U64 kFenceTimeoutNs = 1'000'000'000ULL;
constexpr R2D::U64 kAcquireTimeoutNs = 1'000'000'000ULL;
constexpr R2D::U32 kClearColorRgba8 = 0x000000FFU; // opaque black; sprites draw over it

// In --visible mode the loop keeps presenting (so the scene can actually be
// watched) until the user closes the window; this only bounds it if no close
// event ever arrives. The non-visible measurement run uses warmup+frames instead.
constexpr R2D::U32 kMaxVisibleFrames = 100'000U;

// The cull box is the NDC [-1,1] square (camera viewport 2x2 at origin), exactly
// as the correctness demo: clip projection is the host's vertex-shader concern
// (ProjectMergeTODO #32), so the scene is authored directly in clip space.
constexpr Camera kCamera{
    .source_id = 0U,
    .position_x = 0.0F,
    .position_y = 0.0F,
    .rotation_radians = 0.0F,
    .viewport_width = 2.0F,
    .viewport_height = 2.0F,
    .near_z = 0.0F,
    .far_z = 1.0F,
    .layer_mask = 0xFFFF'FFFFU,
    .flags = 0U,
};

// Distinct opaque colours, cycled per sprite. rgba8 feeds an R8G8B8A8_UNORM vertex
// attribute (low byte = R), so 0xAABBGGRR -- e.g. 0xFF0000FF is opaque red.
constexpr std::array<R2D::U32, 8U> kPalette{{
    0xFF0000FFU, // red
    0xFF00FF00U, // green
    0xFFFF0000U, // blue
    0xFF00FFFFU, // yellow
    0xFFFF00FFU, // magenta
    0xFFFFFF00U, // cyan
    0xFF0080FFU, // orange
    0xFFFFFFFFU, // white
}};

// One full-screen quad (two triangles) in NDC; the per-instance affine shrinks +
// translates it into each sprite's clip-space rect. Same mesh the sprite tests use.
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

enum class PresentModePref : R2D::U8 { Fifo, Immediate, Mailbox };

struct BenchConfig {
    R2D::U32 sprite_count = 1024U;
    R2D::U32 frame_count = 300U;
    R2D::U32 warmup_count = 30U;
    PresentModePref present_mode = PresentModePref::Immediate;
    bool visible = false;
};

[[nodiscard]] const char* presentModeName(PresentModePref mode_) noexcept
{
    switch (mode_) {
    case PresentModePref::Fifo:
        return "fifo";
    case PresentModePref::Immediate:
        return "immediate";
    case PresentModePref::Mailbox:
        return "mailbox";
    }
    return "unknown";
}

[[nodiscard]] VkPresentModeKHR toVkPresentMode(PresentModePref mode_) noexcept
{
    switch (mode_) {
    case PresentModePref::Fifo:
        return VK_PRESENT_MODE_FIFO_KHR;
    case PresentModePref::Immediate:
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case PresentModePref::Mailbox:
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

[[nodiscard]] bool parseU32Arg(std::string_view text_, R2D::U32& out_value_) noexcept
{
    if (text_.empty()) {
        return false;
    }
    R2D::U32 value = 0U;
    for (const char character : text_) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10U + static_cast<R2D::U32>(character - '0');
    }
    out_value_ = value;
    return true;
}

// Parse the CLI. Unknown flags / bad values leave the field at its default and are
// reported once; a benchmark never hard-fails on argv (it just runs the default).
[[nodiscard]] BenchConfig parseArgs(int argc_, char** argv_)
{
    BenchConfig config{};
    for (int index = 1; index < argc_; ++index) {
        const std::string_view option{argv_[index]};
        if (option == "--visible") {
            config.visible = true;
        } else if (option == "--sprites" && index + 1 < argc_) {
            (void)parseU32Arg(argv_[++index], config.sprite_count);
        } else if (option == "--frames" && index + 1 < argc_) {
            (void)parseU32Arg(argv_[++index], config.frame_count);
        } else if (option == "--warmup" && index + 1 < argc_) {
            (void)parseU32Arg(argv_[++index], config.warmup_count);
        } else if (option == "--present-mode" && index + 1 < argc_) {
            const std::string_view value{argv_[++index]};
            if (value == "fifo") {
                config.present_mode = PresentModePref::Fifo;
            } else if (value == "immediate") {
                config.present_mode = PresentModePref::Immediate;
            } else if (value == "mailbox") {
                config.present_mode = PresentModePref::Mailbox;
            }
        }
    }
    if (config.sprite_count == 0U) {
        config.sprite_count = 1U;
    }
    if (config.frame_count == 0U) {
        config.frame_count = 1U;
    }
    return config;
}

// Author N sprites on a near-square grid filling the NDC box, all visible (mask on,
// inside the cull box) so the chain yields exactly N instances -- a deterministic,
// machine-independent work count the ctest smoke asserts.
void buildGridScene(SceneEcs& ecs_, R2D::U32 sprite_count_)
{
    ecs_.reserve(sprite_count_);
    const LocalBounds local_bounds{.source_id = 0U, .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F)};

    const auto columns =
        static_cast<R2D::U32>(std::ceil(std::sqrt(static_cast<double>(sprite_count_))));
    const float cell = 2.0F / static_cast<float>(columns); // NDC span / grid columns
    const float scale = cell * 0.35F;                      // quad half-extent; leaves gaps

    for (R2D::U32 index = 0U; index < sprite_count_; ++index) {
        const R2D::U32 column = index % columns;
        const R2D::U32 row = index / columns;
        const float position_x = -1.0F + (static_cast<float>(column) + 0.5F) * cell;
        const float position_y = -1.0F + (static_cast<float>(row) + 0.5F) * cell;

        const Transform transform{
            .source_id = index,
            .position_x = position_x,
            .position_y = position_y,
            .rotation_radians = 0.0F,
            .scale_x = scale,
            .scale_y = scale,
        };
        const Sprite sprite{
            .source_id = index,
            .texture_id = 0U,
            .texture_generation = 0U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = 0U,
            .material_generation = 0U,
            .color_rgba8 = kPalette[index % kPalette.size()],
            .layer = 0U,
            .flags = 0U,
        };

        const MiniEntity entity = ecs_.create();
        ecs_.add<Transform>(entity, transform);
        ecs_.add<LocalBounds>(entity, local_bounds);
        ecs_.add<VisibilityMask>(entity, VisibilityMask{.mask = 0xFFFF'FFFFU});
        ecs_.add<Sprite>(entity, sprite);
    }
}

struct ChainCounts {
    R2D::SystemStatusCode code;
    R2D::U32 visible_count;
    R2D::U32 batch_count;
};

// The production CPU front-end + sprite chain, fed entirely through spans, into the
// frame arena -- identical to the correctness demo's chain. Produces the
// SpriteInstance[] the GPU will draw.
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

// Record one present frame WITHOUT readback (the bench measures cost, it does not
// validate -- the capture tests already do that): render the N instances into the
// offscreen image, copy it onto the swapchain image, leave the swapchain image in
// PRESENT_SRC. Returns false on a resolve/record failure.
[[nodiscard]] bool recordScenePresent(
    R2DT::TestContext& context_,
    const NativeCommandBufferRef& command_ref_,
    const ImageRef& offscreen_image_,
    const PipelineRef& pipeline_ref_,
    const BufferRef& vertex_buffer_,
    const BufferRef& instance_buffer_,
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

    // Color-only sprite path: no descriptor sets (empty slice span); each instance
    // carries its own affine + colour. Leaves offscreen_image_ in COLOR_ATTACHMENT.
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
    if (!context_.ok()) {
        return false;
    }

    VkImage offscreen_image = VK_NULL_HANDLE;
    VkImageView offscreen_image_view = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.resolveNativeImage(offscreen_image_, offscreen_image, offscreen_image_view).code,
        R2D::NativeStatusCode::Ok);
    if (!context_.ok() || offscreen_image == VK_NULL_HANDLE) {
        return false;
    }

    // Offscreen COLOR_ATTACHMENT -> TRANSFER_SRC (the resource runtime tracks the
    // image's layout, so this is correct on every loop iteration).
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_
            .transitionImageLayout(
                command_buffer,
                offscreen_image_,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT)
            .code,
        R2D::NativeStatusCode::Ok);

    const VkImageCopy image_copy{
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0U,
            .baseArrayLayer = 0U,
            .layerCount = 1U,
        },
        .srcOffset = {.x = 0, .y = 0, .z = 0},
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0U,
            .baseArrayLayer = 0U,
            .layerCount = 1U,
        },
        .dstOffset = {.x = 0, .y = 0, .z = 0},
        .extent = {.width = extent_.width, .height = extent_.height, .depth = 1U},
    };

    // Swapchain UNDEFINED -> TRANSFER_DST, copy the offscreen image onto it, then
    // TRANSFER_DST -> PRESENT_SRC for presentation.
    Render2DTest::recordSwapchainBarrier(
        command_buffer,
        swapchain_image_,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImage(
        command_buffer,
        offscreen_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1U,
        &image_copy);
    Render2DTest::recordSwapchainBarrier(
        command_buffer,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        0U,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.endCommandBuffer(command_ref_).code,
        R2D::NativeStatusCode::Ok);
    return context_.ok();
}

// Per-frame latency statistics over the measured (post-warmup) frames.
struct FrameStats {
    R2D::U32 measured = 0U;
    double avg_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double p50_ms = 0.0;
    double p99_ms = 0.0;
};

[[nodiscard]] FrameStats summarize(R2D::McVector<double>& samples_)
{
    FrameStats stats{};
    if (samples_.empty()) {
        return stats;
    }
    std::ranges::sort(samples_);
    double sum = 0.0;
    for (const double sample : samples_) {
        sum += sample;
    }
    const auto count = samples_.size();
    const auto percentile = [&](double fraction_) {
        const auto position = static_cast<R2D::Usize>(fraction_ * static_cast<double>(count - 1U));
        return samples_[position];
    };
    stats.measured = static_cast<R2D::U32>(count);
    stats.avg_ms = sum / static_cast<double>(count);
    stats.min_ms = samples_.front();
    stats.max_ms = samples_.back();
    stats.p50_ms = percentile(0.50);
    stats.p99_ms = percentile(0.99);
    return stats;
}

// Bring up the GPU runtimes, build the color-only sprite pipeline, and run a timed
// present loop over the authored instances. Runtimes are locals so their
// destructors tear down Vulkan objects while the harness device is still alive.
void runPresentBench(
    R2DT::TestContext& context_,
    Render2DTest::WindowTestHarness& harness_,
    const BenchConfig& config_,
    R2D::U32 instance_count_,
    std::span<const SpriteInstance> instances_)
{
    const VkDevice device = harness_.device();
    const VkPhysicalDevice physical_device = harness_.physicalDevice();
    const R2D::U32 queue_family_index = harness_.queueFamilyIndex();
    const VkQueue queue = harness_.queue();

    // Copy-onto-swapchain needs TRANSFER_DST on the swapchain images (no readback,
    // so TRANSFER_SRC is not required here). An optional surface capability.
    constexpr VkImageUsageFlags kRequiredUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!Render2DTest::surfaceSupportsUsage(physical_device, harness_.surface(), kRequiredUsage, kLogPrefix)) {
        return;
    }
    const R2D::U32 image_usage =
        static_cast<R2D::U32>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | static_cast<R2D::U32>(kRequiredUsage);

    const VkPresentModeKHR present_mode =
        Render2DTest::selectPresentMode(physical_device, harness_.surface(), toVkPresentMode(config_.present_mode));

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
    R2D_TEST_CHECK_EQ(context_, resource_runtime.reserveBuffers(2U).code, R2D::NativeStatusCode::Ok);

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
    if (Render2DTest::createCaptureSwapchain(
            physical_device,
            harness_.host(),
            queue_family_index,
            image_usage,
            swapchain_runtime,
            active,
            static_cast<R2D::U32>(present_mode)) != R2D::NativeStatusCode::Ok) {
        std::cout << kLogPrefix << ": swapchain unavailable (e.g. minimized), skipping\n";
        return;
    }

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

    std::cout << kLogPrefix << ": " << active.extent.width << "x" << active.extent.height << " present-mode="
              << presentModeName(config_.present_mode);
    if (present_mode != toVkPresentMode(config_.present_mode)) {
        std::cout << " (unsupported, fell back to fifo)";
    }
    std::cout << " sprites=" << instance_count_ << " frames=" << config_.frame_count
              << " warmup=" << config_.warmup_count << "; timing...\n";
    if (config_.visible) {
        std::cout << kLogPrefix << ": window is visible -- close it (the X, or Esc) to exit\n";
    }
    std::cout << std::flush;

    // Timed present loop. Each iteration is self-contained (acquire -> reset fence
    // after the acquire so a submit re-signals it -> reset pool -> record -> submit
    // -> present -> wait fence), the same fence/pool discipline as present_loop_smoke
    // (Stage 22C). Frames before warmup_count are discarded from the statistics, and
    // only the first frame_count post-warmup frames are sampled. A non-visible run
    // stops once those are collected; a visible run keeps presenting (so the scene
    // can be watched) until the user closes the window or the safety cap is hit.
    R2D::McVector<double> samples{};
    samples.reserve(config_.frame_count);
    const R2D::U32 loop_frames = config_.visible ? kMaxVisibleFrames : config_.warmup_count + config_.frame_count;
    for (R2D::U32 frame = 0U; frame < loop_frames; ++frame) {
        if (config_.visible && harness_.host().pollShouldClose()) {
            break;
        }

        const auto frame_start = std::chrono::steady_clock::now();

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
            break;
        }

        R2D_TEST_CHECK_EQ(context_, sync_runtime.resetFence(frame_sync).code, R2D::NativeStatusCode::Ok);
        R2D_TEST_CHECK_EQ(context_, command_runtime.resetCommandPool(0U).code, R2D::NativeStatusCode::Ok);
        if (!context_.ok()) {
            break;
        }

        if (!recordScenePresent(
                context_,
                command_ref,
                offscreen_image,
                pipeline_ref,
                vertex_buffer,
                instance_buffer,
                instance_count_,
                swapchain_image,
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
                acquired,
                submit_runtime,
                present_runtime,
                command_runtime,
                swapchain_runtime,
                sync_runtime,
                kFenceTimeoutNs)) {
            break;
        }

        const auto frame_end = std::chrono::steady_clock::now();
        if (frame >= config_.warmup_count && samples.size() < config_.frame_count) {
            samples.push_back(std::chrono::duration<double, std::milli>(frame_end - frame_start).count());
        }
    }

    const FrameStats stats = summarize(samples);
    if (stats.measured == 0U) {
        std::cout << kLogPrefix << ": no frames measured (presented path skipped before warmup completed)\n";
    } else {
        const double fps = stats.avg_ms > 0.0 ? 1000.0 / stats.avg_ms : 0.0;
        std::cout << kLogPrefix << ": measured " << stats.measured << " frames -- avg " << stats.avg_ms
                  << " ms (~" << fps << " fps), min " << stats.min_ms << ", p50 " << stats.p50_ms << ", p99 "
                  << stats.p99_ms << ", max " << stats.max_ms << " ms\n";
    }

    vkDeviceWaitIdle(device);

    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.releasePipelineRef(pipeline_ref).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.destroyShaderModule(fragment_shader).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.destroyShaderModule(vertex_shader).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(instance_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(vertex_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseImageRef(offscreen_image).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, command_runtime.releaseCommandBufferRef(command_ref).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.releaseSwapchainState(active.state).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, sync_runtime.releaseFrameSync(frame_sync).code, R2D::NativeStatusCode::Ok);
}

[[nodiscard]] int runBench(const BenchConfig& config_)
{
    R2DT::TestContext context{};

    // --- CPU: author the grid scene -> the span-only chain -> instances ----------
    SceneEcs ecs{};
    buildGridScene(ecs, config_.sprite_count);

    Columns columns{};
    R2DT::gatherRenderInputs(ecs, columns);
    R2D_TEST_CHECK_EQ(context, columns.size(), static_cast<R2D::Usize>(config_.sprite_count));

    Arena arena{};
    const ChainCounts counts = runChain(columns, arena);
    R2D_TEST_CHECK_EQ(context, static_cast<int>(counts.code), static_cast<int>(R2D::SystemStatusCode::Ok));
    // All authored sprites are visible (in-box, mask on) -- a deterministic count.
    R2D_TEST_CHECK_EQ(context, counts.visible_count, config_.sprite_count);
    R2D_TEST_CHECK(context, counts.batch_count > 0U);
    if (!context.ok()) {
        return context.result();
    }

    std::fprintf(
        stdout,
        "window-bench: %u sprites -> %u visible, %u batches\n",
        config_.sprite_count,
        counts.visible_count,
        counts.batch_count);

    const std::span<const SpriteInstance> instances{arena.spriteInstances().data(), counts.visible_count};

    // --- GPU: time the present loop via the harness ------------------------------
    Render2DTest::WindowTestHarness harness{};
    if (harness.initialize({.window_title = "Render2D Present Benchmark (close to exit)",
                            .log_prefix = kLogPrefix,
                            .width = 1280,
                            .height = 720,
                            .require_dynamic_rendering = true,
                            .visible = config_.visible}) != Render2DTest::WindowTestHarness::Status::Ready) {
        return context.result();
    }

    runPresentBench(context, harness, config_, counts.visible_count, instances);
    return context.result();
}

} // namespace

int main(int argc, char** argv) noexcept
{
    // Default (e.g. under ctest, with no args beyond the smoke's small counts):
    // hidden window, a few frames, immediate present mode (falls back to fifo).
    // A manual run passes larger --frames / --sprites and optionally --visible.
    try {
        return runBench(parseArgs(argc, argv));
    } catch (const std::exception& exception) {
        std::fputs("window_scene_bench_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("window_scene_bench_test unknown exception\n", stderr);
        return 1;
    }
}
