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
// Why copy rather than render straight to the swapchain: the encoders resolve an
// ImageRef owned by VulkanResourceRuntime, and a swapchain image is not one.
// Rendering directly would mean changing a runtime contract, which Stage 22 has
// deliberately never done. So the rendered content reaches the swapchain by a
// raw vkCmdCopyImage on resolved handles -- the same windowless-core-preserving
// idiom 22C uses for its raw barriers/clears. Every runtime stays unchanged
// (ProjectMergeTODO red line #11) and PresentHost is unchanged from 22B.
//
// Like 22B/22C, every bring-up step is a graceful SKIP on a headless/limited CI
// box (no video driver, no Vulkan loader/ICD, no present-capable GPU, no dynamic
// rendering, or a surface that does not grant TRANSFER usage): we return
// context.result() and only assert with R2D_TEST_CHECK once on the real path.

#include <Render2D/Render2D.hpp>

#include <Render2D/Present/PresentHost.hpp>

#include "Render2D/Memory/RenderVector.hpp"
#include "support/FullScreenTriangleShaders.hpp"
#include "support/PresentBringup.hpp"
#include "support/TestHarness.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdio>
#include <cstring>
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
using SwapchainState = R2D::SwapchainState<Provider, Dim>;
using SwapchainImageRef = R2D::SwapchainImageRef<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using AcquiredImage = R2D::AcquiredImage<Provider, Dim>;
using PresentCommand = R2D::PresentCommand<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;

constexpr R2D::U32 kSwapchainImageCapacity = 16U;
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

// The currently-live swapchain: the runtime's POD state ref, its image refs, and
// the extent/format it was created with.
struct ActiveSwapchain {
    SwapchainState state{};
    std::array<SwapchainImageRef, kSwapchainImageCapacity> images{};
    VkExtent2D extent{};
    R2D::U32 format = 0U;
};

// Raw image-memory barrier for the swapchain image (which is not an ImageRef, so
// the resource runtime's transitionImageLayout does not apply -- same idiom 22C
// uses). All fields are set explicitly.
void recordSwapchainBarrier(
    VkCommandBuffer command_buffer_,
    VkImage image_,
    VkImageLayout old_layout_,
    VkImageLayout new_layout_,
    VkAccessFlags src_access_,
    VkAccessFlags dst_access_,
    VkPipelineStageFlags src_stage_,
    VkPipelineStageFlags dst_stage_) noexcept
{
    const VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0U,
        .levelCount = 1U,
        .baseArrayLayer = 0U,
        .layerCount = 1U,
    };
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = src_access_,
        .dstAccessMask = dst_access_,
        .oldLayout = old_layout_,
        .newLayout = new_layout_,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange = range,
    };
    vkCmdPipelineBarrier(command_buffer_, src_stage_, dst_stage_, 0U, 0U, nullptr, 0U, nullptr, 1U, &barrier);
}

// Query the surface and create a single swapchain whose images carry
// COLOR_ATTACHMENT | TRANSFER_DST | TRANSFER_SRC (copy target + readback source).
// Returns the runtime status; on Ok, out_active_ is filled. A zero extent
// (minimized window) returns InvalidInput so the caller can skip gracefully.
[[nodiscard]] R2D::NativeStatusCode createCaptureSwapchain(
    VkPhysicalDevice physical_device_,
    R2D::PresentHost& present_host_,
    R2D::U32 queue_family_index_,
    R2D::U32 image_usage_,
    SwapchainRuntime& swapchain_runtime_,
    ActiveSwapchain& out_active_)
{
    out_active_ = {};
    const VkSurfaceKHR surface = present_host_.surfaceHandle();

    // Driver-filled; left uninitialized on purpose (same reasoning as 22B/22C):
    // value-initializing `{}` would set the VkSurfaceTransformFlagBitsKHR member
    // to a non-enumerator zero, tripping a clang-tidy bugprone warning. Read only
    // after the VK_SUCCESS check below.
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface, &caps) != VK_SUCCESS) {
        return R2D::NativeStatusCode::InvalidInput;
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFU) {
        R2D::U32 pixel_width = 0U;
        R2D::U32 pixel_height = 0U;
        if (!present_host_.pixelSize(pixel_width, pixel_height)) {
            return R2D::NativeStatusCode::InvalidInput;
        }
        extent.width = Render2DTest::clampU32(pixel_width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height =
            Render2DTest::clampU32(pixel_height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0U || extent.height == 0U) {
        return R2D::NativeStatusCode::InvalidInput;
    }

    VkSurfaceFormatKHR surface_format{};
    if (!Render2DTest::selectSurfaceFormat(physical_device_, surface, surface_format)) {
        return R2D::NativeStatusCode::InvalidInput;
    }

    R2D::U32 min_image_count = caps.minImageCount;
    if (caps.maxImageCount != 0U && min_image_count > caps.maxImageCount) {
        min_image_count = caps.maxImageCount;
    }
    if (min_image_count == 0U || min_image_count > kSwapchainImageCapacity) {
        return R2D::NativeStatusCode::InvalidInput;
    }

    const R2D::U32 queue_family_index = queue_family_index_;
    const auto create_result = swapchain_runtime_.createSwapchain(
        {
            .surface = surface,
            .old_swapchain = VK_NULL_HANDLE,
            .queue_family_indices = &queue_family_index,
            .queue_family_index_count = 1U,
            .min_image_count = min_image_count,
            .width = extent.width,
            .height = extent.height,
            .format = static_cast<R2D::U32>(surface_format.format),
            .color_space = static_cast<R2D::U32>(surface_format.colorSpace),
            .image_usage = image_usage_,
            .sharing_mode = static_cast<R2D::U32>(VK_SHARING_MODE_EXCLUSIVE),
            .pre_transform = static_cast<R2D::U32>(caps.currentTransform),
            .composite_alpha = static_cast<R2D::U32>(Render2DTest::selectCompositeAlpha(caps.supportedCompositeAlpha)),
            .present_mode = static_cast<R2D::U32>(VK_PRESENT_MODE_FIFO_KHR),
            .clipped = 1U,
            .image_array_layers = 1U,
            .image_view_type = static_cast<R2D::U32>(VK_IMAGE_VIEW_TYPE_2D),
            .image_aspect_flags = static_cast<R2D::U32>(VK_IMAGE_ASPECT_COLOR_BIT),
            .flags = 0U,
        },
        out_active_.state,
        out_active_.images);
    if (create_result.code == R2D::NativeStatusCode::Ok) {
        out_active_.extent = extent;
        out_active_.format = static_cast<R2D::U32>(surface_format.format);
    }
    return create_result.code;
}

// Record the whole capture frame into one command buffer: render the gradient
// offscreen, read it back (baseline), copy it onto the swapchain image, read that
// back (capture), and leave the swapchain image in PRESENT_SRC. Returns false if
// any native handle fails to resolve (the caller then bails out of the frame).
[[nodiscard]] bool recordCaptureFrame(
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
    VkImage offscreen_image = VK_NULL_HANDLE;
    VkImageView offscreen_image_view = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.resolveNativeImage(offscreen_image_, offscreen_image, offscreen_image_view).code,
        R2D::NativeStatusCode::Ok);
    VkBuffer capture_buffer = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.resolveNativeBuffer(capture_buffer_, capture_buffer).code,
        R2D::NativeStatusCode::Ok);
    if (!context_.ok() || command_buffer == VK_NULL_HANDLE || offscreen_image == VK_NULL_HANDLE ||
        capture_buffer == VK_NULL_HANDLE) {
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
    const VkBufferImageCopy buffer_copy{
        .bufferOffset = 0U,
        .bufferRowLength = 0U,
        .bufferImageHeight = 0U,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0U,
            .baseArrayLayer = 0U,
            .layerCount = 1U,
        },
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent = {.width = extent_.width, .height = extent_.height, .depth = 1U},
    };

    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.beginCommandBuffer(command_ref_, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT).code,
        R2D::NativeStatusCode::Ok);

    // 1. Render the gradient into the offscreen image (-> COLOR_ATTACHMENT layout).
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

    // 2. Offscreen -> TRANSFER_SRC, then copy to the baseline readback buffer.
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
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.recordCopyImageToBuffer(command_buffer, offscreen_image_, baseline_buffer_).code,
        R2D::NativeStatusCode::Ok);

    // 3. Swapchain UNDEFINED -> TRANSFER_DST, then copy the offscreen image onto it.
    //    The offscreen image is still in TRANSFER_SRC and is only read, so both
    //    copies (to baseline buffer and to swapchain) can share it safely.
    recordSwapchainBarrier(
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

    // 4. Swapchain TRANSFER_DST -> TRANSFER_SRC (wait for the copy write), then
    //    read the swapchain image back into the capture buffer.
    recordSwapchainBarrier(
        command_buffer,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImageToBuffer(
        command_buffer,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        capture_buffer,
        1U,
        &buffer_copy);

    // 5. Swapchain TRANSFER_SRC -> PRESENT_SRC for presentation.
    recordSwapchainBarrier(
        command_buffer,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_READ_BIT,
        0U,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    // 6. Make both readback buffers host-visible.
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_
            .recordBufferBarrier(
                command_buffer,
                baseline_buffer_,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_HOST_READ_BIT)
            .code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_
            .recordBufferBarrier(
                command_buffer,
                capture_buffer_,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_HOST_READ_BIT)
            .code,
        R2D::NativeStatusCode::Ok);

    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.endCommandBuffer(command_ref_).code,
        R2D::NativeStatusCode::Ok);
    return context_.ok();
}

// Set up the swapchain + offscreen render resources, run the single capture
// frame, and assert the swapchain readback equals the offscreen baseline.
// Runtimes are locals here so their destructors tear down Vulkan objects while
// device_ is still alive (the caller destroys the device after this returns).
void runVisibleCapture(
    R2DT::TestContext& context_,
    R2D::PresentHost& present_host_,
    VkPhysicalDevice physical_device_,
    VkDevice device_,
    R2D::U32 queue_family_index_)
{
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device_, queue_family_index_, 0U, &queue);
    if (queue == VK_NULL_HANDLE) {
        std::cout << "present-capture 22D: no device queue, skipping\n";
        return;
    }

    // The copy-onto-swapchain + readback path needs TRANSFER_DST (copy target)
    // and TRANSFER_SRC (readback source) on the swapchain images. Both are
    // optional surface capabilities; if either is missing this platform cannot
    // do the visible-capture comparison, so skip gracefully.
    VkSurfaceCapabilitiesKHR caps; // driver-filled; see createCaptureSwapchain note
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, present_host_.surfaceHandle(), &caps) !=
        VK_SUCCESS) {
        std::cout << "present-capture 22D: surface capabilities query failed, skipping\n";
        return;
    }
    constexpr VkImageUsageFlags kRequiredUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((caps.supportedUsageFlags & kRequiredUsage) != kRequiredUsage) {
        std::cout << "present-capture 22D: surface lacks TRANSFER_SRC/DST swapchain usage, skipping\n";
        return;
    }
    const R2D::U32 image_usage =
        static_cast<R2D::U32>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | static_cast<R2D::U32>(kRequiredUsage);

    SwapchainRuntime swapchain_runtime{};
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.initialize({.device = device_}).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.reserveSwapchains(1U).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        swapchain_runtime.reserveSwapchainImages(kSwapchainImageCapacity).code,
        R2D::NativeStatusCode::Ok);

    PresentRuntime present_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        present_runtime.initialize({.device = device_, .present_queue = queue}).code,
        R2D::NativeStatusCode::Ok);

    SyncRuntime sync_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        sync_runtime.initialize({.device = device_, .fence_create_flags = 0U}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, sync_runtime.reserveFrameSyncs(1U).code, R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    R2D_TEST_CHECK_EQ(context_, sync_runtime.createFrameSync(0U, 0U, frame_sync).code, R2D::NativeStatusCode::Ok);

    CommandRuntime command_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime
            .initialize({.device = device_, .queue_family_index = queue_family_index_, .command_pool_flags = 0U})
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
        resource_runtime.initialize({.physical_device = physical_device_, .device = device_}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.reserveImages(1U).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.reserveBuffers(2U).code, R2D::NativeStatusCode::Ok);

    PipelineRuntime pipeline_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        pipeline_runtime.initialize({.device = device_, .pipeline_cache_flags = 0U}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.reservePipelines(1U).code, R2D::NativeStatusCode::Ok);

    if (!context_.ok()) {
        return;
    }

    ActiveSwapchain active{};
    if (createCaptureSwapchain(physical_device_, present_host_, queue_family_index_, image_usage, swapchain_runtime, active) !=
        R2D::NativeStatusCode::Ok) {
        std::cout << "present-capture 22D: swapchain unavailable (e.g. minimized), skipping\n";
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

    // Fence starts unsignaled (no prior frame); acquire signals image_available,
    // the submit signals the fence, and we wait on it before reading back.
    AcquiredImage acquired{};
    const auto acquire_result = present_runtime.acquireNextImage(
        active.state,
        frame_sync,
        swapchain_runtime,
        sync_runtime,
        kAcquireTimeoutNs,
        0U,
        acquired);
    if (acquire_result.code != R2D::NativeStatusCode::Ok) {
        std::cout << "present-capture 22D: image acquire did not succeed (code "
                  << static_cast<int>(acquire_result.code) << "), skipping\n";
        return;
    }
    R2D_TEST_CHECK(context_, acquired.image_index < active.state.image_count);
    if (acquired.image_index >= active.state.image_count) {
        return;
    }

    VkImage swapchain_image = VK_NULL_HANDLE;
    VkImageView swapchain_image_view = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        swapchain_runtime.resolveNativeSwapchainImage(active.images[acquired.image_index], swapchain_image, swapchain_image_view)
            .code,
        R2D::NativeStatusCode::Ok);
    if (!context_.ok() || swapchain_image == VK_NULL_HANDLE) {
        return;
    }

    if (recordCaptureFrame(
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
        const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
        const auto submit_result = submit_runtime.submit(
            command_refs,
            frame_sync,
            command_runtime,
            sync_runtime,
            R2D::kVulkanSubmitWaitImageAvailable | R2D::kVulkanSubmitSignalRenderFinished);
        R2D_TEST_CHECK_EQ(context_, submit_result.code, R2D::NativeStatusCode::Ok);

        if (submit_result.code == R2D::NativeStatusCode::Ok) {
            const PresentCommand present_command{
                .swapchain_id = acquired.swapchain_id,
                .image_index = acquired.image_index,
                .wait_sync_id = frame_sync.sync_id,
                .wait_sync_generation = frame_sync.generation,
                .frame_index = frame_sync.frame_index,
                .generation = acquired.generation,
                .flags = 0U,
            };
            const auto present_result = present_runtime.present(present_command, swapchain_runtime, sync_runtime);
            // The capture buffer was filled before the present transition, so a
            // benign OUT_OF_DATE on present does not invalidate the comparison.
            if (present_result.code != R2D::NativeStatusCode::Ok &&
                present_result.code != R2D::NativeStatusCode::SwapchainOutOfDate) {
                R2D_TEST_CHECK_EQ(context_, present_result.code, R2D::NativeStatusCode::Ok);
            }

            const auto wait_result = sync_runtime.waitFence(frame_sync, kFenceTimeoutNs);
            R2D_TEST_CHECK_EQ(context_, wait_result.code, R2D::NativeStatusCode::Ok);

            if (wait_result.code == R2D::NativeStatusCode::Ok) {
                R2D::McVector<R2D::U8> baseline_pixels(static_cast<R2D::Usize>(byte_count));
                R2D::McVector<R2D::U8> capture_pixels(static_cast<R2D::Usize>(byte_count));
                R2D_TEST_CHECK_EQ(
                    context_,
                    resource_runtime.readBuffer(baseline_buffer, baseline_pixels.data(), byte_count, 0U).code,
                    R2D::NativeStatusCode::Ok);
                R2D_TEST_CHECK_EQ(
                    context_,
                    resource_runtime.readBuffer(capture_buffer, capture_pixels.data(), byte_count, 0U).code,
                    R2D::NativeStatusCode::Ok);

                // Guard against a blank/failed render trivially matching a blank
                // capture: the gradient must actually vary across the image.
                bool baseline_varies = false;
                for (R2D::Usize i = 1U; i < baseline_pixels.size(); ++i) {
                    if (baseline_pixels[i] != baseline_pixels[0]) {
                        baseline_varies = true;
                        break;
                    }
                }
                R2D_TEST_CHECK(context_, baseline_varies);

                const bool identical =
                    std::memcmp(baseline_pixels.data(), capture_pixels.data(), static_cast<R2D::Usize>(byte_count)) == 0;
                R2D_TEST_CHECK(context_, identical);

                std::cout << "present-capture 22D: " << active.extent.width << "x" << active.extent.height
                          << " visible capture " << (identical ? "==" : "!=") << " offscreen baseline ("
                          << byte_count << " bytes)\n";
            }
        }
    }

    vkDeviceWaitIdle(device_);

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

    R2D::PresentHost present_host{};
    if (!present_host.initialize("Render2D Present Visible Capture 22D", 640, 480, true)) {
        std::cout << "present-capture 22D: no usable video driver, skipping ("
                  << present_host.lastError() << ")\n";
        return context.result();
    }

    const auto extensions = present_host.requiredInstanceExtensions();
    if (extensions.empty()) {
        std::cout << "present-capture 22D: no Vulkan instance extensions, skipping\n";
        return context.result();
    }

    VkInstance instance = VK_NULL_HANDLE;
    R2D::U32 api_version = 0U;
    if (Render2DTest::createPresentRenderInstance(extensions, instance, api_version) != VK_SUCCESS ||
        instance == VK_NULL_HANDLE) {
        std::cout << "present-capture 22D: no Vulkan instance, skipping\n";
        return context.result();
    }

    if (!present_host.createSurface(instance)) {
        std::cout << "present-capture 22D: surface creation failed, skipping ("
                  << present_host.lastError() << ")\n";
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    R2D::U32 queue_family_index = 0U;
    if (!Render2DTest::selectPresentDevice(
            instance,
            present_host.surfaceHandle(),
            physical_device,
            queue_family_index)) {
        std::cout << "present-capture 22D: no present-capable device, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    if (!Render2DTest::presentDeviceSupportsDynamicRendering(physical_device)) {
        std::cout << "present-capture 22D: device lacks dynamic rendering, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    VkDevice device = VK_NULL_HANDLE;
    if (Render2DTest::createPresentRenderDevice(physical_device, queue_family_index, device) != VK_SUCCESS ||
        device == VK_NULL_HANDLE) {
        std::cout << "present-capture 22D: device creation failed, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    runVisibleCapture(context, present_host, physical_device, device, queue_family_index);

    // Teardown order: runtimes (inside runVisibleCapture) -> surface -> device ->
    // instance -> window/SDL (present_host destructor).
    present_host.destroySurface();
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
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
