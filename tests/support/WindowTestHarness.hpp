#pragma once

// Reusable window/present test harness for the Stage 22/23 on-screen tests.
//
// The Stage 22D/22E/23D capture tests each repeated the same ~150 lines: open an
// SDL window, bring up a present-capable Vulkan 1.3 instance/device, create a
// capture-capable swapchain, acquire an image, copy a rendered offscreen image
// onto it, read it back, present, and tear everything down -- each with its own
// copies of ActiveSwapchain / recordSwapchainBarrier / createCaptureSwapchain.
// This header consolidates that scaffolding behind one RAII bring-up class plus a
// handful of frame helpers, so a new on-screen test (and the refactored capture
// tests) express only what is unique to them -- what to render.
//
// Like PresentBringup / VulkanSmokeContext, this is test-only scaffolding, not
// production architecture: it lives under tests/ and links the optional
// render2d_present_host_support target (SDL3). The host engine owns the window +
// instance/device + surface at merge (ProjectMergeTODO red line #11) -- the
// harness only turns an SDL window into a VkSurfaceKHR for the UNCHANGED
// swapchain/present runtimes. Every bring-up step is a graceful skip on a
// headless/limited box (no video driver, no Vulkan ICD, no present-capable GPU,
// no dynamic rendering), so tests using it stay green in CI. McVector only.

#include "Render2D/Memory/RenderVector.hpp"
#include "PresentBringup.hpp"
#include "TestHarness.hpp"

#include <Render2D/Render2D.hpp>

#include <Render2D/Present/PresentHost.hpp>

#include <vulkan/vulkan.h>

#include <array>
#include <cstring>
#include <iostream>

namespace Render2DTest {

// Room reserved for swapchain image refs. Desktop drivers hand out only a few;
// 16 is a generous upper bound the capture tests have always used.
inline constexpr Render2D::U32 kWindowSwapchainImageCapacity = 16U;

// The currently-live capture swapchain: the runtime's POD state ref, its image
// refs, and the extent/format it was created with. (Promoted verbatim from the
// 22D/22E/23D tests, which each kept an identical local copy.)
template<class Provider, class Dim>
struct ActiveSwapchain {
    Render2D::SwapchainState<Provider, Dim> state{};
    std::array<Render2D::SwapchainImageRef<Provider, Dim>, kWindowSwapchainImageCapacity> images{};
    VkExtent2D extent{};
    Render2D::U32 format = 0U;
};

// Raw image-memory barrier for a swapchain image. A swapchain image is not a
// VulkanResourceRuntime ImageRef, so the resource runtime's transitionImageLayout
// does not apply -- the capture path drives these barriers by hand on the
// resolved VkImage. All fields are set explicitly.
inline void recordSwapchainBarrier(
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

// Whether the window's surface grants every usage flag in required_usage_ on its
// swapchain images (e.g. TRANSFER_DST for copy-onto, TRANSFER_SRC for readback).
// Both are optional surface capabilities; prints a skip line and returns false if
// the query fails or a flag is missing.
[[nodiscard]] inline bool surfaceSupportsUsage(
    VkPhysicalDevice physical_device_,
    VkSurfaceKHR surface_,
    VkImageUsageFlags required_usage_,
    const char* log_prefix_)
{
    // Driver-filled; left uninitialized on purpose -- value-initializing `{}`
    // would set the VkSurfaceTransformFlagBitsKHR member to a non-enumerator zero,
    // tripping a clang-tidy bugprone warning. Read only after the VK_SUCCESS check.
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps) != VK_SUCCESS) {
        std::cout << log_prefix_ << ": surface capabilities query failed, skipping\n";
        return false;
    }
    if ((caps.supportedUsageFlags & required_usage_) != required_usage_) {
        std::cout << log_prefix_ << ": surface lacks required swapchain usage, skipping\n";
        return false;
    }
    return true;
}

// Return preferred_ if the surface supports it, otherwise VK_PRESENT_MODE_FIFO_KHR
// (the one mode the spec guarantees is always present). The capture tests keep
// FIFO (vsync-paced, deterministic); the present benchmark requests IMMEDIATE or
// MAILBOX to measure uncapped frame throughput instead of the vsync interval.
[[nodiscard]] inline VkPresentModeKHR selectPresentMode(
    VkPhysicalDevice physical_device_,
    VkSurfaceKHR surface_,
    VkPresentModeKHR preferred_)
{
    if (preferred_ == VK_PRESENT_MODE_FIFO_KHR) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    Render2D::U32 count = 0U;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &count, nullptr) != VK_SUCCESS ||
        count == 0U) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    Render2D::McVector<VkPresentModeKHR> modes(static_cast<Render2D::Usize>(count));
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &count, modes.data()) != VK_SUCCESS) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    for (const VkPresentModeKHR mode : modes) {
        if (mode == preferred_) {
            return preferred_;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// Query the surface and create a single swapchain whose images carry
// image_usage_ (callers pass COLOR_ATTACHMENT | TRANSFER_DST [| TRANSFER_SRC]).
// On Ok, out_active_ is filled. A zero extent (minimized window) returns
// InvalidInput so the caller can skip gracefully. (Promoted verbatim from the
// 22D/22E/23D tests.)
template<class Provider, class Dim>
[[nodiscard]] Render2D::NativeStatusCode createCaptureSwapchain(
    VkPhysicalDevice physical_device_,
    Render2D::PresentHost& present_host_,
    Render2D::U32 queue_family_index_,
    Render2D::U32 image_usage_,
    Render2D::VulkanSwapchainRuntime<Provider, Dim>& swapchain_runtime_,
    ActiveSwapchain<Provider, Dim>& out_active_,
    Render2D::U32 present_mode_ = static_cast<Render2D::U32>(VK_PRESENT_MODE_FIFO_KHR))
{
    out_active_ = {};
    const VkSurfaceKHR surface = present_host_.surfaceHandle();

    // Driver-filled; left uninitialized on purpose (see surfaceSupportsUsage).
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface, &caps) != VK_SUCCESS) {
        return Render2D::NativeStatusCode::InvalidInput;
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFU) {
        Render2D::U32 pixel_width = 0U;
        Render2D::U32 pixel_height = 0U;
        if (!present_host_.pixelSize(pixel_width, pixel_height)) {
            return Render2D::NativeStatusCode::InvalidInput;
        }
        extent.width = clampU32(pixel_width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = clampU32(pixel_height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0U || extent.height == 0U) {
        return Render2D::NativeStatusCode::InvalidInput;
    }

    VkSurfaceFormatKHR surface_format{};
    if (!selectSurfaceFormat(physical_device_, surface, surface_format)) {
        return Render2D::NativeStatusCode::InvalidInput;
    }

    Render2D::U32 min_image_count = caps.minImageCount;
    if (caps.maxImageCount != 0U && min_image_count > caps.maxImageCount) {
        min_image_count = caps.maxImageCount;
    }
    if (min_image_count == 0U || min_image_count > kWindowSwapchainImageCapacity) {
        return Render2D::NativeStatusCode::InvalidInput;
    }

    const Render2D::U32 queue_family_index = queue_family_index_;
    const auto create_result = swapchain_runtime_.createSwapchain(
        {
            .surface = surface,
            .old_swapchain = VK_NULL_HANDLE,
            .queue_family_indices = &queue_family_index,
            .queue_family_index_count = 1U,
            .min_image_count = min_image_count,
            .width = extent.width,
            .height = extent.height,
            .format = static_cast<Render2D::U32>(surface_format.format),
            .color_space = static_cast<Render2D::U32>(surface_format.colorSpace),
            .image_usage = image_usage_,
            .sharing_mode = static_cast<Render2D::U32>(VK_SHARING_MODE_EXCLUSIVE),
            .pre_transform = static_cast<Render2D::U32>(caps.currentTransform),
            .composite_alpha = static_cast<Render2D::U32>(selectCompositeAlpha(caps.supportedCompositeAlpha)),
            .present_mode = present_mode_,
            .clipped = 1U,
            .image_array_layers = 1U,
            .image_view_type = static_cast<Render2D::U32>(VK_IMAGE_VIEW_TYPE_2D),
            .image_aspect_flags = static_cast<Render2D::U32>(VK_IMAGE_ASPECT_COLOR_BIT),
            .flags = 0U,
        },
        out_active_.state,
        out_active_.images);
    if (create_result.code == Render2D::NativeStatusCode::Ok) {
        out_active_.extent = extent;
        out_active_.format = static_cast<Render2D::U32>(surface_format.format);
    }
    return create_result.code;
}

// A reusable SDL-backed window + present-capable Vulkan instance/device, brought
// up in one call with graceful headless skip. RAII: the destructor tears the
// surface, device, and instance down in the correct order (then the PresentHost
// member tears down the window + SDL).
//
// Usage:
//   WindowTestHarness harness{};
//   if (harness.initialize({.window_title = "...", .log_prefix = "demo"}) !=
//       WindowTestHarness::Status::Ready) {
//       return context.result();  // graceful skip, already logged
//   }
//   runFrame(harness, ...);       // runtimes are locals here, torn down before harness
//
// Like the Vulkan runtimes / PresentHost, this is non-copyable and non-movable
// (it owns process-global SDL state and Vulkan handles).
class WindowTestHarness
{
public:
    struct Config {
        const char* window_title = "Render2D Window Test";
        const char* log_prefix = "window-test";
        int width = 640;
        int height = 480;
        // 22D/22E/23D render through VulkanDynamicRenderEncoder /
        // VulkanSpriteRenderEncoder, which need a Vulkan 1.3 device with the
        // dynamicRendering feature. A pure clear/copy test can set this false to
        // use the 1.0 bring-up.
        bool require_dynamic_rendering = true;
        // Show the window on screen instead of hiding it. The automated/CI path
        // keeps it hidden (frames are validated by readback, so nothing needs to
        // be seen, and a hidden window stays deterministic on headless boxes). A
        // manual "look at it" run sets this true to get a real visible window;
        // pair it with PresentHost::pollShouldClose() to keep it responsive.
        bool visible = false;
    };
    enum class Status { Ready, Skipped };

    WindowTestHarness() = default;
    WindowTestHarness(const WindowTestHarness&) = delete;
    WindowTestHarness& operator=(const WindowTestHarness&) = delete;
    WindowTestHarness(WindowTestHarness&&) = delete;
    WindowTestHarness& operator=(WindowTestHarness&&) = delete;

    ~WindowTestHarness() noexcept
    {
        teardown();
    }

    // Open the window and bring up the instance/surface/device/queue. Prints a
    // "<log_prefix>: <reason>, skipping" line and returns Skipped at the first
    // gate that fails on a headless/limited box; returns Ready on the real path.
    // The window is hidden unless Config::visible is set (the automated tests keep
    // it hidden; a manual run shows it).
    [[nodiscard]] Status initialize(const Config& config_)
    {
        if (!present_host.initialize(config_.window_title, config_.width, config_.height, !config_.visible)) {
            std::cout << config_.log_prefix << ": no usable video driver, skipping ("
                      << present_host.lastError() << ")\n";
            return Status::Skipped;
        }

        const auto extensions = present_host.requiredInstanceExtensions();
        if (extensions.empty()) {
            std::cout << config_.log_prefix << ": no Vulkan instance extensions, skipping\n";
            return Status::Skipped;
        }

        VkInstance instance = VK_NULL_HANDLE;
        if (config_.require_dynamic_rendering) {
            Render2D::U32 api_version = 0U;
            if (createPresentRenderInstance(extensions, instance, api_version) != VK_SUCCESS ||
                instance == VK_NULL_HANDLE) {
                std::cout << config_.log_prefix << ": no Vulkan instance, skipping\n";
                return Status::Skipped;
            }
        } else if (createPresentInstance(extensions, instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
            std::cout << config_.log_prefix << ": no Vulkan instance, skipping\n";
            return Status::Skipped;
        }

        if (!present_host.createSurface(instance)) {
            std::cout << config_.log_prefix << ": surface creation failed, skipping ("
                      << present_host.lastError() << ")\n";
            vkDestroyInstance(instance, nullptr);
            return Status::Skipped;
        }

        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        Render2D::U32 queue_family_index = 0U;
        if (!selectPresentDevice(instance, present_host.surfaceHandle(), physical_device, queue_family_index)) {
            std::cout << config_.log_prefix << ": no present-capable device, skipping\n";
            present_host.destroySurface();
            vkDestroyInstance(instance, nullptr);
            return Status::Skipped;
        }

        if (config_.require_dynamic_rendering && !presentDeviceSupportsDynamicRendering(physical_device)) {
            std::cout << config_.log_prefix << ": device lacks dynamic rendering, skipping\n";
            present_host.destroySurface();
            vkDestroyInstance(instance, nullptr);
            return Status::Skipped;
        }

        VkDevice device = VK_NULL_HANDLE;
        const VkResult device_result = config_.require_dynamic_rendering
            ? createPresentRenderDevice(physical_device, queue_family_index, device)
            : createPresentDevice(physical_device, queue_family_index, device);
        if (device_result != VK_SUCCESS || device == VK_NULL_HANDLE) {
            std::cout << config_.log_prefix << ": device creation failed, skipping\n";
            present_host.destroySurface();
            vkDestroyInstance(instance, nullptr);
            return Status::Skipped;
        }

        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, queue_family_index, 0U, &queue);
        if (queue == VK_NULL_HANDLE) {
            std::cout << config_.log_prefix << ": no device queue, skipping\n";
            present_host.destroySurface();
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return Status::Skipped;
        }

        vk_instance = instance;
        vk_physical_device = physical_device;
        vk_device = device;
        vk_queue = queue;
        queue_family = queue_family_index;
        return Status::Ready;
    }

    [[nodiscard]] Render2D::PresentHost& host() noexcept
    {
        return present_host;
    }

    [[nodiscard]] VkSurfaceKHR surface() const noexcept
    {
        return present_host.surfaceHandle();
    }

    [[nodiscard]] VkInstance instance() const noexcept
    {
        return vk_instance;
    }

    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept
    {
        return vk_physical_device;
    }

    [[nodiscard]] VkDevice device() const noexcept
    {
        return vk_device;
    }

    [[nodiscard]] VkQueue queue() const noexcept
    {
        return vk_queue;
    }

    [[nodiscard]] Render2D::U32 queueFamilyIndex() const noexcept
    {
        return queue_family;
    }

private:
    // Teardown order: surface -> device -> instance, then the PresentHost member's
    // own destructor tears down the window + SDL. Any swapchain/runtime objects
    // must already be released by the caller (their RAII locals run first).
    void teardown() noexcept
    {
        present_host.destroySurface();
        if (vk_device != VK_NULL_HANDLE) {
            vkDestroyDevice(vk_device, nullptr);
            vk_device = VK_NULL_HANDLE;
        }
        if (vk_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(vk_instance, nullptr);
            vk_instance = VK_NULL_HANDLE;
        }
    }

    Render2D::PresentHost present_host{};
    VkInstance vk_instance = VK_NULL_HANDLE;
    VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
    VkDevice vk_device = VK_NULL_HANDLE;
    VkQueue vk_queue = VK_NULL_HANDLE;
    Render2D::U32 queue_family = 0U;
};

// Outcome of acquiring the next swapchain image: Ok (proceed), Skipped (a benign
// acquire failure on a hidden/limited window -- bail out of the frame, no test
// failure), or Failed (a check was recorded -- bail out).
enum class WindowFrameStatus { Ok, Skipped, Failed };

// Acquire the next image and resolve its VkImage. Mirrors the acquire/resolve
// block the capture tests share. On Ok, out_acquired_ and out_swapchain_image_
// are filled.
template<class Provider, class Dim>
[[nodiscard]] WindowFrameStatus acquireSwapchainImage(
    Render2D::TestSupport::TestContext& context_,
    const char* log_prefix_,
    Render2D::VulkanPresentRuntime<Provider, Dim>& present_runtime_,
    Render2D::VulkanSwapchainRuntime<Provider, Dim>& swapchain_runtime_,
    Render2D::VulkanSyncRuntime<Provider, Dim>& sync_runtime_,
    const ActiveSwapchain<Provider, Dim>& active_,
    const Render2D::FrameSync<Provider, Dim>& frame_sync_,
    Render2D::U64 acquire_timeout_ns_,
    Render2D::AcquiredImage<Provider, Dim>& out_acquired_,
    VkImage& out_swapchain_image_)
{
    out_swapchain_image_ = VK_NULL_HANDLE;
    const auto acquire_result = present_runtime_.acquireNextImage(
        active_.state,
        frame_sync_,
        swapchain_runtime_,
        sync_runtime_,
        acquire_timeout_ns_,
        0U,
        out_acquired_);
    if (acquire_result.code != Render2D::NativeStatusCode::Ok) {
        std::cout << log_prefix_ << ": image acquire did not succeed (code "
                  << static_cast<int>(acquire_result.code) << "), skipping\n";
        return WindowFrameStatus::Skipped;
    }

    R2D_TEST_CHECK(context_, out_acquired_.image_index < active_.state.image_count);
    if (out_acquired_.image_index >= active_.state.image_count) {
        return WindowFrameStatus::Failed;
    }

    VkImageView swapchain_image_view = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        swapchain_runtime_
            .resolveNativeSwapchainImage(active_.images[out_acquired_.image_index], out_swapchain_image_, swapchain_image_view)
            .code,
        Render2D::NativeStatusCode::Ok);
    if (!context_.ok() || out_swapchain_image_ == VK_NULL_HANDLE) {
        return WindowFrameStatus::Failed;
    }
    return WindowFrameStatus::Ok;
}

// After the caller has recorded the offscreen render into offscreen_image_ (now
// in COLOR_ATTACHMENT layout), record the rest of the capture frame into
// command_buffer_: copy the offscreen image to the baseline buffer, copy it onto
// the swapchain image, read the swapchain image back into the capture buffer, and
// leave the swapchain image in PRESENT_SRC. Resolves the offscreen VkImage +
// capture VkBuffer itself; returns false if either fails. (The exact step 2-6
// sequence the 22D/23D capture tests shared.)
template<class Provider, class Dim>
[[nodiscard]] bool recordOffscreenToSwapchainCapture(
    Render2D::TestSupport::TestContext& context_,
    VkCommandBuffer command_buffer_,
    const Render2D::ImageRef<Provider, Dim>& offscreen_image_,
    VkImage swapchain_image_,
    const Render2D::BufferRef<Provider, Dim>& baseline_buffer_,
    const Render2D::BufferRef<Provider, Dim>& capture_buffer_,
    VkExtent2D extent_,
    Render2D::VulkanResourceRuntime<Provider, Dim>& resource_runtime_)
{
    VkImage offscreen_image = VK_NULL_HANDLE;
    VkImageView offscreen_image_view = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.resolveNativeImage(offscreen_image_, offscreen_image, offscreen_image_view).code,
        Render2D::NativeStatusCode::Ok);
    VkBuffer capture_buffer = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.resolveNativeBuffer(capture_buffer_, capture_buffer).code,
        Render2D::NativeStatusCode::Ok);
    if (!context_.ok() || offscreen_image == VK_NULL_HANDLE || capture_buffer == VK_NULL_HANDLE) {
        return false;
    }

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

    // Offscreen -> TRANSFER_SRC, then copy to the baseline readback buffer.
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_
            .transitionImageLayout(
                command_buffer_,
                offscreen_image_,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT)
            .code,
        Render2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.recordCopyImageToBuffer(command_buffer_, offscreen_image_, baseline_buffer_).code,
        Render2D::NativeStatusCode::Ok);

    // Swapchain UNDEFINED -> TRANSFER_DST, then copy the offscreen image onto it.
    // The offscreen image is still in TRANSFER_SRC and is only read, so both
    // copies (to baseline buffer and to swapchain) share it safely.
    recordSwapchainBarrier(
        command_buffer_,
        swapchain_image_,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImage(
        command_buffer_,
        offscreen_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1U,
        &image_copy);

    // Swapchain TRANSFER_DST -> TRANSFER_SRC (wait for the copy write), then read
    // the swapchain image back into the capture buffer.
    recordSwapchainBarrier(
        command_buffer_,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImageToBuffer(
        command_buffer_,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        capture_buffer,
        1U,
        &buffer_copy);

    // Swapchain TRANSFER_SRC -> PRESENT_SRC for presentation.
    recordSwapchainBarrier(
        command_buffer_,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_READ_BIT,
        0U,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    // Make both readback buffers host-visible.
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_
            .recordBufferBarrier(
                command_buffer_,
                baseline_buffer_,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_HOST_READ_BIT)
            .code,
        Render2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_
            .recordBufferBarrier(
                command_buffer_,
                capture_buffer_,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_HOST_READ_BIT)
            .code,
        Render2D::NativeStatusCode::Ok);

    return context_.ok();
}

// Submit the recorded command buffer (waiting image_available, signaling
// render_finished + the fence), present the acquired image, and wait the fence.
// A benign SwapchainOutOfDate on present is tolerated (the capture buffer was
// filled before the present transition). Returns true if the fence was waited
// successfully -- i.e. it is safe to read the buffers back.
template<class Provider, class Dim>
[[nodiscard]] bool submitPresentWaitFence(
    Render2D::TestSupport::TestContext& context_,
    const Render2D::NativeCommandBufferRef<Provider, Dim>& command_ref_,
    const Render2D::FrameSync<Provider, Dim>& frame_sync_,
    const Render2D::AcquiredImage<Provider, Dim>& acquired_,
    Render2D::VulkanSubmitRuntime<Provider, Dim>& submit_runtime_,
    Render2D::VulkanPresentRuntime<Provider, Dim>& present_runtime_,
    Render2D::VulkanCommandRuntime<Provider, Dim>& command_runtime_,
    Render2D::VulkanSwapchainRuntime<Provider, Dim>& swapchain_runtime_,
    Render2D::VulkanSyncRuntime<Provider, Dim>& sync_runtime_,
    Render2D::U64 fence_timeout_ns_)
{
    const std::array<Render2D::NativeCommandBufferRef<Provider, Dim>, 1U> command_refs{command_ref_};
    const auto submit_result = submit_runtime_.submit(
        command_refs,
        frame_sync_,
        command_runtime_,
        sync_runtime_,
        Render2D::kVulkanSubmitWaitImageAvailable | Render2D::kVulkanSubmitSignalRenderFinished);
    R2D_TEST_CHECK_EQ(context_, submit_result.code, Render2D::NativeStatusCode::Ok);
    if (submit_result.code != Render2D::NativeStatusCode::Ok) {
        return false;
    }

    const Render2D::PresentCommand<Provider, Dim> present_command{
        .swapchain_id = acquired_.swapchain_id,
        .image_index = acquired_.image_index,
        .wait_sync_id = frame_sync_.sync_id,
        .wait_sync_generation = frame_sync_.generation,
        .frame_index = frame_sync_.frame_index,
        .generation = acquired_.generation,
        .flags = 0U,
    };
    const auto present_result = present_runtime_.present(present_command, swapchain_runtime_, sync_runtime_);
    if (present_result.code != Render2D::NativeStatusCode::Ok &&
        present_result.code != Render2D::NativeStatusCode::SwapchainOutOfDate) {
        R2D_TEST_CHECK_EQ(context_, present_result.code, Render2D::NativeStatusCode::Ok);
    }

    const auto wait_result = sync_runtime_.waitFence(frame_sync_, fence_timeout_ns_);
    R2D_TEST_CHECK_EQ(context_, wait_result.code, Render2D::NativeStatusCode::Ok);
    return wait_result.code == Render2D::NativeStatusCode::Ok;
}

// The result of comparing the swapchain capture against the offscreen baseline:
// `varies` guards against a blank render trivially matching a blank capture;
// `identical` is the byte-for-byte present-integrity check.
struct ReadbackComparison {
    bool varies = false;
    bool identical = false;
};

// Read both readback buffers, check the baseline actually varies, and compare the
// capture against it byte-for-byte. Records the readBuffer status checks into
// context_; the caller asserts varies/identical and prints its own summary.
template<class Provider, class Dim>
[[nodiscard]] ReadbackComparison readbackVariesAndIdentical(
    Render2D::TestSupport::TestContext& context_,
    const Render2D::BufferRef<Provider, Dim>& baseline_buffer_,
    const Render2D::BufferRef<Provider, Dim>& capture_buffer_,
    Render2D::U64 byte_count_,
    Render2D::VulkanResourceRuntime<Provider, Dim>& resource_runtime_)
{
    ReadbackComparison comparison{};
    Render2D::McVector<Render2D::U8> baseline_pixels(static_cast<Render2D::Usize>(byte_count_));
    Render2D::McVector<Render2D::U8> capture_pixels(static_cast<Render2D::Usize>(byte_count_));
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.readBuffer(baseline_buffer_, baseline_pixels.data(), byte_count_, 0U).code,
        Render2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.readBuffer(capture_buffer_, capture_pixels.data(), byte_count_, 0U).code,
        Render2D::NativeStatusCode::Ok);

    for (Render2D::Usize i = 1U; i < baseline_pixels.size(); ++i) {
        if (baseline_pixels[i] != baseline_pixels[0]) {
            comparison.varies = true;
            break;
        }
    }
    comparison.identical =
        std::memcmp(baseline_pixels.data(), capture_pixels.data(), static_cast<Render2D::Usize>(byte_count_)) == 0;
    return comparison;
}

} // namespace Render2DTest
