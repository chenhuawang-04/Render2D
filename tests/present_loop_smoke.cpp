// Stage 22C: present *real frames* to a live SDL3 window through the unchanged
// Vulkan runtimes.
//
// Builds on 22B (which proved an SDL surface drives VulkanSwapchainRuntime) by
// running the full per-frame loop on that swapchain:
//
//   waitFence -> acquireNextImage (signals image_available)
//             -> record (clear the swapchain image to a solid colour)
//             -> submit  (waits image_available, signals render_finished + fence)
//             -> present (waits render_finished)
//
// and the resize path: a swapchain recreate that threads the retiring swapchain
// in as `old_swapchain`, then releases the old state. A real window resize
// surfaces as VK_ERROR_OUT_OF_DATE_KHR -> NativeStatusCode::SwapchainOutOfDate
// from acquire/present; we handle that here, and additionally *force* one
// recreate mid-loop so the recreate->release machinery is exercised
// deterministically (a hidden window never spontaneously resizes).
//
// All of this is windowless-core-preserving: Render2D's swapchain/sync/command/
// submit/present runtimes are untouched and merely consume the SDL-provided
// surface (ProjectMergeTODO red line #11). The instance/device/format bring-up
// is shared with the 22B test via support/PresentBringup.hpp.
//
// Like 22B, every bring-up step is a graceful SKIP on a headless CI box (no
// video driver, no Vulkan loader/ICD, no present-capable GPU): we return
// context.result() (0 unless an assertion already failed) and only assert with
// R2D_TEST_CHECK once we are on the real GPU+display path.

#include <Render2D/Render2D.hpp>

#include <Render2D/Present/PresentHost.hpp>

#include "support/PresentBringup.hpp"
#include "support/TestHarness.hpp"

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
using SwapchainState = R2D::SwapchainState<Provider, Dim>;
using SwapchainImageRef = R2D::SwapchainImageRef<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using AcquiredImage = R2D::AcquiredImage<Provider, Dim>;
using PresentCommand = R2D::PresentCommand<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;

constexpr R2D::U32 kSwapchainImageCapacity = 16U;
// Initial swapchain + a bounded number of recreates. The image-slot table grows
// per create (released slots are not reclaimed), so this caps reserved capacity
// and stops the loop if a pathological stream of OUT_OF_DATE keeps recreating.
constexpr R2D::U32 kMaxSwapchainGenerations = 4U;
constexpr R2D::U32 kFrameCount = 6U;
constexpr R2D::U32 kForcedRecreateFrame = 3U;
constexpr R2D::U64 kFenceTimeoutNs = 1'000'000'000ULL;
constexpr R2D::U64 kAcquireTimeoutNs = 1'000'000'000ULL;
constexpr std::array<float, 4U> kClearColor{0.10F, 0.20F, 0.55F, 1.0F};

// The currently-live swapchain: the runtime's POD state ref, its image refs, and
// the extent/format it was created with.
struct ActiveSwapchain {
    SwapchainState state{};
    std::array<SwapchainImageRef, kSwapchainImageCapacity> images{};
    VkExtent2D extent{};
    R2D::U32 format = 0U;
};

// Record a single frame into the resolved command buffer: transition the
// swapchain image into a writable layout, (optionally) clear it to a solid
// colour, then transition it to PRESENT_SRC. Raw vkCmd* on the resolved buffer
// is the established idiom (the encoders record the same way internally).
//
// `clear_supported_` reflects whether the surface granted TRANSFER_DST usage. If
// not, we still produce a valid (blank) presentable image via a single
// UNDEFINED->PRESENT_SRC transition -- the loop/present path is what 22C proves.
void recordClearFrame(VkCommandBuffer command_buffer_, VkImage image_, bool clear_supported_) noexcept
{
    const VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0U,
        .levelCount = 1U,
        .baseArrayLayer = 0U,
        .layerCount = 1U,
    };

    if (!clear_supported_) {
        const VkImageMemoryBarrier to_present{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0U,
            .dstAccessMask = 0U,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image_,
            .subresourceRange = range,
        };
        vkCmdPipelineBarrier(
            command_buffer_,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0U,
            0U,
            nullptr,
            0U,
            nullptr,
            1U,
            &to_present);
        return;
    }

    const VkImageMemoryBarrier to_transfer{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0U,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange = range,
    };
    vkCmdPipelineBarrier(
        command_buffer_,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &to_transfer);

    const VkClearColorValue clear_color{
        .float32 = {kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]},
    };
    vkCmdClearColorImage(
        command_buffer_,
        image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clear_color,
        1U,
        &range);

    const VkImageMemoryBarrier to_present{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0U,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange = range,
    };
    vkCmdPipelineBarrier(
        command_buffer_,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &to_present);
}

// Query the surface for the current extent/transform/format and create a
// swapchain (optionally threading `old_swapchain_` through for a resize
// recreate). Returns the runtime's status code; on Ok, `out_active_` is filled.
// A zero extent (e.g. a minimized window) returns InvalidInput so the caller can
// skip/stop gracefully.
[[nodiscard]] R2D::NativeStatusCode createActiveSwapchain(
    VkPhysicalDevice physical_device_,
    R2D::PresentHost& present_host_,
    R2D::U32 queue_family_index_,
    R2D::U32 image_usage_,
    VkSwapchainKHR old_swapchain_,
    SwapchainRuntime& swapchain_runtime_,
    ActiveSwapchain& out_active_)
{
    out_active_ = {};
    const VkSurfaceKHR surface = present_host_.surfaceHandle();

    // Driver-filled; left uninitialized on purpose (same reasoning as 22B):
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
            .old_swapchain = old_swapchain_,
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

// The real per-frame loop. Runtimes are locals here so their destructors tear
// down Vulkan objects (swapchain, image views, semaphores, command pool) while
// `device_` is still alive -- the caller destroys the device only after this
// returns (same ownership discipline as 22B's runSwapchainChecks).
void runPresentLoop(
    R2DT::TestContext& context_,
    R2D::PresentHost& present_host_,
    VkPhysicalDevice physical_device_,
    VkDevice device_,
    R2D::U32 queue_family_index_)
{
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device_, queue_family_index_, 0U, &queue);
    if (queue == VK_NULL_HANDLE) {
        std::cout << "present-loop 22C: no device queue, skipping\n";
        return;
    }

    // Decide swapchain usage + the matching submit wait stage once: COLOR_ATTACHMENT
    // is guaranteed, TRANSFER_DST (needed for vkCmdClearColorImage) is optional, so
    // gate the clear on it. These are stable across recreates (same surface/device).
    VkSurfaceCapabilitiesKHR initial_caps;  // driver-filled; see createActiveSwapchain note
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, present_host_.surfaceHandle(), &initial_caps) !=
        VK_SUCCESS) {
        std::cout << "present-loop 22C: surface capabilities query failed, skipping\n";
        return;
    }
    const bool clear_supported =
        (initial_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0U;
    const R2D::U32 image_usage = static_cast<R2D::U32>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
        (clear_supported ? static_cast<R2D::U32>(VK_IMAGE_USAGE_TRANSFER_DST_BIT) : 0U);
    const R2D::U32 wait_stage = clear_supported ?
        static_cast<R2D::U32>(VK_PIPELINE_STAGE_TRANSFER_BIT) :
        static_cast<R2D::U32>(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    SwapchainRuntime swapchain_runtime{};
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.initialize({.device = device_}).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.reserveSwapchains(kMaxSwapchainGenerations).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        swapchain_runtime.reserveSwapchainImages(kMaxSwapchainGenerations * kSwapchainImageCapacity).code,
        R2D::NativeStatusCode::Ok);

    PresentRuntime present_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        present_runtime.initialize({.device = device_, .present_queue = queue}).code,
        R2D::NativeStatusCode::Ok);

    SyncRuntime sync_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        sync_runtime.initialize({.device = device_, .fence_create_flags = VK_FENCE_CREATE_SIGNALED_BIT}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, sync_runtime.reserveFrameSyncs(1U).code, R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    R2D_TEST_CHECK_EQ(context_, sync_runtime.createFrameSync(0U, 0U, frame_sync).code, R2D::NativeStatusCode::Ok);

    CommandRuntime command_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime.initialize({.device = device_, .queue_family_index = queue_family_index_, .command_pool_flags = 0U}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, command_runtime.reserveCommandBuffers(1U).code, R2D::NativeStatusCode::Ok);
    NativeCommandBufferRef command_ref{};
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime.allocateCommandBufferRef(0U, {.first = 0U, .count = 0U}, {.first = 0U, .count = 0U}, 0U, command_ref).code,
        R2D::NativeStatusCode::Ok);

    SubmitRuntime submit_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        submit_runtime.initialize({.queue = queue, .wait_stage_flags = wait_stage}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, submit_runtime.reserveCommandBuffers(1U).code, R2D::NativeStatusCode::Ok);

    if (!context_.ok()) {
        return;
    }

    ActiveSwapchain active{};
    R2D::U32 generations = 1U;
    const auto initial_code = createActiveSwapchain(
        physical_device_,
        present_host_,
        queue_family_index_,
        image_usage,
        VK_NULL_HANDLE,
        swapchain_runtime,
        active);
    if (initial_code != R2D::NativeStatusCode::Ok) {
        std::cout << "present-loop 22C: initial swapchain unavailable (e.g. minimized), skipping\n";
        return;
    }

    // Recreate the swapchain, threading the retiring one through as oldSwapchain,
    // then release the old state. Returns false (stop the loop) at the generation
    // cap or if the new swapchain cannot be built (e.g. window now minimized).
    const auto recreate = [&]() -> bool {
        if (generations >= kMaxSwapchainGenerations) {
            std::cout << "present-loop 22C: swapchain generation cap reached, stopping\n";
            return false;
        }
        vkDeviceWaitIdle(device_);

        VkSwapchainKHR old_native = VK_NULL_HANDLE;
        if (swapchain_runtime.resolveNativeSwapchain(active.state, old_native).code != R2D::NativeStatusCode::Ok) {
            return false;
        }

        ActiveSwapchain next{};
        const auto code = createActiveSwapchain(
            physical_device_,
            present_host_,
            queue_family_index_,
            image_usage,
            old_native,
            swapchain_runtime,
            next);
        if (code != R2D::NativeStatusCode::Ok) {
            std::cout << "present-loop 22C: recreate unavailable (e.g. minimized), stopping\n";
            return false;
        }

        R2D_TEST_CHECK_EQ(context_, swapchain_runtime.releaseSwapchainState(active.state).code, R2D::NativeStatusCode::Ok);
        active = next;
        ++generations;
        return true;
    };

    R2D::U32 frames_presented = 0U;
    for (R2D::U32 frame = 0U; frame < kFrameCount; ++frame) {
        const auto wait_result = sync_runtime.waitFence(frame_sync, kFenceTimeoutNs);
        if (wait_result.code == R2D::NativeStatusCode::Timeout) {
            std::cout << "present-loop 22C: fence wait timed out, stopping\n";
            break;
        }
        R2D_TEST_CHECK_EQ(context_, wait_result.code, R2D::NativeStatusCode::Ok);
        if (wait_result.code != R2D::NativeStatusCode::Ok) {
            break;
        }

        AcquiredImage acquired{};
        const auto acquire_result = present_runtime.acquireNextImage(
            active.state,
            frame_sync,
            swapchain_runtime,
            sync_runtime,
            kAcquireTimeoutNs,
            0U,
            acquired);
        if (acquire_result.code == R2D::NativeStatusCode::SwapchainOutOfDate) {
            if (!recreate()) {
                break;
            }
            continue;
        }
        if (acquire_result.code == R2D::NativeStatusCode::Timeout) {
            std::cout << "present-loop 22C: image acquire timed out, stopping\n";
            break;
        }
        R2D_TEST_CHECK_EQ(context_, acquire_result.code, R2D::NativeStatusCode::Ok);
        if (acquire_result.code != R2D::NativeStatusCode::Ok) {
            break;
        }
        R2D_TEST_CHECK(context_, acquired.image_index < active.state.image_count);
        if (acquired.image_index >= active.state.image_count) {
            break;
        }

        // Reset the fence only now that a successful acquire guarantees we will
        // submit work to re-signal it -- resetting before the acquire would leave
        // the fence unsignaled on an OUT_OF_DATE skip and hang the next waitFence.
        R2D_TEST_CHECK_EQ(context_, sync_runtime.resetFence(frame_sync).code, R2D::NativeStatusCode::Ok);

        R2D_TEST_CHECK_EQ(context_, command_runtime.resetCommandPool(0U).code, R2D::NativeStatusCode::Ok);
        R2D_TEST_CHECK_EQ(
            context_,
            command_runtime.beginCommandBuffer(command_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT).code,
            R2D::NativeStatusCode::Ok);

        VkImage native_image = VK_NULL_HANDLE;
        VkImageView native_image_view = VK_NULL_HANDLE;
        R2D_TEST_CHECK_EQ(
            context_,
            swapchain_runtime.resolveNativeSwapchainImage(active.images[acquired.image_index], native_image, native_image_view).code,
            R2D::NativeStatusCode::Ok);

        VkCommandBuffer native_command_buffer = VK_NULL_HANDLE;
        R2D_TEST_CHECK_EQ(
            context_,
            command_runtime.resolveNativeCommandBuffer(command_ref, native_command_buffer).code,
            R2D::NativeStatusCode::Ok);
        if (!context_.ok() || native_image == VK_NULL_HANDLE || native_command_buffer == VK_NULL_HANDLE) {
            break;
        }

        recordClearFrame(native_command_buffer, native_image, clear_supported);

        R2D_TEST_CHECK_EQ(context_, command_runtime.endCommandBuffer(command_ref).code, R2D::NativeStatusCode::Ok);

        const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
        const auto submit_result = submit_runtime.submit(
            command_refs,
            frame_sync,
            command_runtime,
            sync_runtime,
            R2D::kVulkanSubmitWaitImageAvailable | R2D::kVulkanSubmitSignalRenderFinished);
        R2D_TEST_CHECK_EQ(context_, submit_result.code, R2D::NativeStatusCode::Ok);
        if (submit_result.code != R2D::NativeStatusCode::Ok) {
            break;
        }

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
        if (present_result.code == R2D::NativeStatusCode::SwapchainOutOfDate) {
            if (!recreate()) {
                break;
            }
            continue;
        }
        R2D_TEST_CHECK_EQ(context_, present_result.code, R2D::NativeStatusCode::Ok);
        if (present_result.code != R2D::NativeStatusCode::Ok) {
            break;
        }
        ++frames_presented;

        // Deterministically exercise the resize/recreate path once (a hidden
        // window never spontaneously goes OUT_OF_DATE).
        if (frame == kForcedRecreateFrame && frame + 1U < kFrameCount) {
            std::cout << "present-loop 22C: forcing swapchain recreate (resize path)\n";
            if (!recreate()) {
                break;
            }
        }
    }

    vkDeviceWaitIdle(device_);

    R2D_TEST_CHECK(context_, frames_presented >= 1U);
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.releaseSwapchainState(active.state).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, command_runtime.releaseCommandBufferRef(command_ref).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, sync_runtime.releaseFrameSync(frame_sync).code, R2D::NativeStatusCode::Ok);

    std::cout << "present-loop 22C: presented " << frames_presented << " frame(s) across " << generations
              << " swapchain generation(s), " << active.extent.width << "x" << active.extent.height
              << (clear_supported ? ", cleared" : ", blank") << '\n';
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};

    R2D::PresentHost present_host{};
    if (!present_host.initialize("Render2D Present Loop 22C", 640, 480, true)) {
        std::cout << "present-loop 22C: no usable video driver, skipping ("
                  << present_host.lastError() << ")\n";
        return context.result();
    }

    const auto extensions = present_host.requiredInstanceExtensions();
    if (extensions.empty()) {
        std::cout << "present-loop 22C: no Vulkan instance extensions, skipping\n";
        return context.result();
    }

    VkInstance instance = VK_NULL_HANDLE;
    if (Render2DTest::createPresentInstance(extensions, instance) != VK_SUCCESS ||
        instance == VK_NULL_HANDLE) {
        std::cout << "present-loop 22C: no Vulkan instance, skipping\n";
        return context.result();
    }

    if (!present_host.createSurface(instance)) {
        std::cout << "present-loop 22C: surface creation failed, skipping ("
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
        std::cout << "present-loop 22C: no present-capable device, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    VkDevice device = VK_NULL_HANDLE;
    if (Render2DTest::createPresentDevice(physical_device, queue_family_index, device) != VK_SUCCESS ||
        device == VK_NULL_HANDLE) {
        std::cout << "present-loop 22C: device creation failed, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    runPresentLoop(context, present_host, physical_device, device, queue_family_index);

    // Teardown order: runtimes (inside runPresentLoop) -> surface -> device ->
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
        std::fputs("present_loop_smoke exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("present_loop_smoke unknown exception\n", stderr);
        return 1;
    }
}
