// Stage 22D: prove that what reaches the on-screen (swapchain) image is
// byte-identical to the trusted offscreen render baseline -- a visible-capture
// regression guard for the present path.
//
// 22A/B/C brought up an SDL window -> surface -> swapchain and presented frames
// (clear-only). 22D renders a *real* scene and verifies it survives the trip to
// the swapchain unchanged. Within ONE command buffer / ONE submission:
//
//   1. render a deterministic gradient full-screen triangle into an offscreen
//      image (VulkanDynamicRenderEncoder), in the swapchain's exact format;
//   2. copy that offscreen image to a readback buffer  -> the OFFSCREEN BASELINE
//      (this is the very path every other vulkan_* render test trusts);
//   3. copy the offscreen image onto the acquired swapchain image;
//   4. copy the swapchain image back to a second readback buffer -> the CAPTURE;
//   5. transition the swapchain image to PRESENT_SRC and present it.
//
// After the fence, assert CAPTURE == BASELINE byte-for-byte. Matching the
// offscreen target's format to the swapchain's makes the image->image copy a
// byte-exact transfer (no format conversion), so a mismatch can only come from a
// real copy/layout/region defect -- which is exactly what 22D guards.
//
// The window/instance/device bring-up and the swapchain/acquire/capture/present
// plumbing live in WindowTestHarness (shared with 22E/23D and the window-scene
// demo); this file expresses only what is unique to 22D -- the gradient render.
// Why copy rather than render straight to the swapchain: the encoders resolve an
// ImageRef owned by VulkanResourceRuntime, and a swapchain image is not one, so
// rendering directly would change a runtime contract Stage 22 never touches. Every
// runtime stays unchanged (ProjectMergeTODO red line #11).
//
// Like 22B/22C, every bring-up step is a graceful SKIP on a headless/limited CI
// box: the harness returns Skipped (logged) and we only assert on the real path.

#include <Render2D/Render2D.hpp>

#include <Render2D/Present/PresentHost.hpp>

#include "support/FullScreenTriangleShaders.hpp"
#include "support/TestHarness.hpp"
#include "support/WindowTestHarness.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdio>
#include <exception>
#include <iostream>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using SwapchainRuntime = R2D::VulkanSwapchainRuntime<Provider, Dim>;
using PresentRuntime = R2D::VulkanPresentRuntime<Provider, Dim>;
using SyncRuntime = R2D::VulkanSyncRuntime<Provider, Dim>;
using CommandRuntime = R2D::VulkanCommandRuntime<Provider, Dim>;
using SubmitRuntime = R2D::VulkanSubmitRuntime<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;
using DynamicRenderEncoder = R2D::VulkanDynamicRenderEncoder<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using AcquiredImage = R2D::AcquiredImage<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using ActiveSwapchain = Render2DTest::ActiveSwapchain<Provider, Dim>;

constexpr const char* kLogPrefix = "present-capture 22D";
constexpr R2D::U32 kBytesPerPixel = 4U; // B8G8R8A8 / R8G8B8A8 are 4 bytes/pixel
constexpr R2D::U64 kFenceTimeoutNs = 1'000'000'000ULL;
constexpr R2D::U64 kAcquireTimeoutNs = 1'000'000'000ULL;
// The full-screen triangle covers the whole viewport, so the clear is fully
// overwritten by the gradient; opaque black is just a defined starting point.
constexpr R2D::U32 kClearColorRgba8 = 0x000000FFU;

// One draw of the full-screen triangle (the encoder draws a batch when its
// draw_count is non-zero).
constexpr std::array<BatchCommand, 1U> kBatches{{
    {
        .draw_first = 0U,
        .draw_count = 1U,
        .material_id = 0U,
        .material_generation = 0U,
        .texture_id = 0U,
        .texture_generation = 0U,
        .pipeline_id = 0U,
        .pipeline_generation = 0U,
        .descriptor_id = 0U,
        .descriptor_generation = 0U,
        .sort_key = 0U,
        .flags = 0U,
    },
}};

// Record the capture frame: render the gradient into the offscreen image, then
// (via the harness capture tail) copy it to the baseline buffer, onto the
// swapchain image, read the swapchain back to the capture buffer, and leave it in
// PRESENT_SRC. Returns false on a resolve/record failure.
[[nodiscard]] bool recordGradientFrame(
    R2DT::TestContext& context_,
    const NativeCommandBufferRef& command_ref_,
    const ImageRef& offscreen_image_,
    const PipelineRef& pipeline_ref_,
    const BufferRef& baseline_buffer_,
    const BufferRef& capture_buffer_,
    VkImage swapchain_image_,
    VkExtent2D extent_,
    CommandRuntime& command_runtime_,
    ResourceRuntime& resource_runtime_,
    PipelineRuntime& pipeline_runtime_)
{
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.resolveNativeCommandBuffer(command_ref_, command_buffer).code,
        R2D::NativeStatusCode::Ok);
    if (!context_.ok() || command_buffer == VK_NULL_HANDLE) {
        return false;
    }

    const R2D::VulkanDynamicRenderEncoderConfig render_config{
        .width = extent_.width,
        .height = extent_.height,
        .clear_color_rgba8 = kClearColorRgba8,
        .draw_vertex_count = 3U,
        .draw_instance_count = 1U,
        .flags = 0U,
    };

    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.beginCommandBuffer(command_ref_, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT).code,
        R2D::NativeStatusCode::Ok);

    // Render the gradient into the offscreen image (-> COLOR_ATTACHMENT layout).
    R2D_TEST_CHECK_EQ(
        context_,
        DynamicRenderEncoder::record(
            command_ref_,
            offscreen_image_,
            pipeline_ref_,
            kBatches,
            command_runtime_,
            resource_runtime_,
            pipeline_runtime_,
            render_config)
            .code,
        R2D::NativeStatusCode::Ok);

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

// Set up the swapchain + offscreen render resources, run the single capture frame,
// and assert the swapchain readback equals the offscreen baseline. Runtimes are
// locals here so their destructors tear down Vulkan objects while the harness
// device is still alive (the harness is destroyed by the caller after this returns).
void runVisibleCapture(R2DT::TestContext& context_, Render2DTest::WindowTestHarness& harness_)
{
    const VkDevice device = harness_.device();
    const VkPhysicalDevice physical_device = harness_.physicalDevice();
    const R2D::U32 queue_family_index = harness_.queueFamilyIndex();
    const VkQueue queue = harness_.queue();

    // The copy-onto-swapchain + readback path needs TRANSFER_DST (copy target) and
    // TRANSFER_SRC (readback source) on the swapchain images. Both are optional
    // surface capabilities; skip gracefully if either is missing.
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
            .allocateCommandBufferRef(0U, {.first = 0U, .count = 1U}, {.first = 0U, .count = 0U}, 0U, command_ref)
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
        pipeline_runtime.createShaderModule(Render2DTest::kFullScreenTriangleVertSpv, vertex_shader).code,
        R2D::NativeStatusCode::Ok);
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        pipeline_runtime.createShaderModule(Render2DTest::kGradientFragSpv, fragment_shader).code,
        R2D::NativeStatusCode::Ok);

    PipelineRef pipeline_ref{};
    R2D_TEST_CHECK_EQ(
        context_,
        pipeline_runtime
            .createGraphicsPipelineRef(
                {
                    .vertex_shader = vertex_shader,
                    .fragment_shader = fragment_shader,
                    .descriptor_set_layouts = nullptr,
                    .descriptor_set_layout_count = 0U,
                    .color_format = active.format,
                    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                    .cull_mode = VK_CULL_MODE_NONE,
                    .front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                    .polygon_mode = VK_POLYGON_MODE_FILL,
                    .sample_count = VK_SAMPLE_COUNT_1_BIT,
                    .flags = 0U,
                    .vertex_binding_descriptions = nullptr,
                    .vertex_attribute_descriptions = nullptr,
                    .vertex_binding_description_count = 0U,
                    .vertex_attribute_description_count = 0U,
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

    if (recordGradientFrame(
            context_,
            command_ref,
            offscreen_image,
            pipeline_ref,
            baseline_buffer,
            capture_buffer,
            swapchain_image,
            active.extent,
            command_runtime,
            resource_runtime,
            pipeline_runtime)) {
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
                      << " visible capture " << (comparison.identical ? "==" : "!=") << " offscreen baseline ("
                      << byte_count << " bytes)\n";
        }
    }

    vkDeviceWaitIdle(device);

    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.releasePipelineRef(pipeline_ref).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.destroyShaderModule(fragment_shader).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.destroyShaderModule(vertex_shader).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(capture_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(baseline_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseImageRef(offscreen_image).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, command_runtime.releaseCommandBufferRef(command_ref).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.releaseSwapchainState(active.state).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, sync_runtime.releaseFrameSync(frame_sync).code, R2D::NativeStatusCode::Ok);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};

    Render2DTest::WindowTestHarness harness{};
    if (harness.initialize({.window_title = "Render2D Present Visible Capture 22D",
                            .log_prefix = kLogPrefix,
                            .width = 640,
                            .height = 480,
                            .require_dynamic_rendering = true}) != Render2DTest::WindowTestHarness::Status::Ready) {
        return context.result();
    }

    runVisibleCapture(context, harness);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("present_visible_capture_smoke exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("present_visible_capture_smoke unknown exception\n", stderr);
        return 1;
    }
}
