// Stage 22B: drive the unchanged VulkanSwapchainRuntime from a *real*
// SDL3-provided VkSurfaceKHR.
//
// Flow: PresentHost opens a (hidden) Vulkan window -> we build a VkInstance with
// the instance extensions SDL requires -> PresentHost turns that instance into a
// real VkSurfaceKHR -> we pick a present-capable physical device + queue family
// and a device with VK_KHR_swapchain -> the existing
// VulkanSwapchainRuntime::createSwapchain consumes the surface and produces real
// swapchain images, which we resolve and release.
//
// Every bring-up step can fail on a headless CI box (no usable video driver, no
// Vulkan loader/ICD, no present-capable GPU). Each such failure is a graceful
// SKIP: we return context.result(), which is 0 as long as no assertion has
// failed. Only once we are on the real GPU+display path do we assert with
// R2D_TEST_CHECK. So this test is green-by-skip in CI and exercises the real
// surface->swapchain path on a developer machine with a GPU.

#include <Render2D/Render2D.hpp>

#include <Render2D/Present/PresentHost.hpp>

#include "support/TestHarness.hpp"

#include "Render2D/Memory/RenderVector.hpp"

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
using Runtime = R2D::VulkanSwapchainRuntime<Provider, Dim>;
using SwapchainState = R2D::SwapchainState<Provider, Dim>;
using SwapchainImageRef = R2D::SwapchainImageRef<Provider, Dim>;

constexpr R2D::U32 kSwapchainImageCapacity = 16U;

[[nodiscard]] R2D::U32 clampU32(R2D::U32 value_, R2D::U32 low_, R2D::U32 high_) noexcept
{
    if (value_ < low_)
    {
        return low_;
    }
    if (value_ > high_)
    {
        return high_;
    }
    return value_;
}

// Create a minimal VkInstance enabling exactly the extensions SDL reports as
// required for surface creation. apiVersion 1.0 keeps it maximally compatible --
// the surface/swapchain path needs no newer core.
[[nodiscard]] VkResult createPresentInstance(
    std::span<const char* const> extensions_,
    VkInstance& out_instance_) noexcept
{
    constexpr char kApplicationName[] = "Render2D Present Host 22B";
    const VkApplicationInfo application_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = kApplicationName,
        .applicationVersion = VK_MAKE_VERSION(0U, 1U, 0U),
        .pEngineName = kApplicationName,
        .engineVersion = VK_MAKE_VERSION(0U, 1U, 0U),
        .apiVersion = VK_API_VERSION_1_0,
    };
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<R2D::U32>(extensions_.size()),
        .ppEnabledExtensionNames = extensions_.data(),
    };
    return vkCreateInstance(&instance_info, nullptr, &out_instance_);
}

[[nodiscard]] bool deviceSupportsSwapchain(VkPhysicalDevice physical_device_)
{
    R2D::U32 extension_count = 0U;
    if (vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &extension_count, nullptr) != VK_SUCCESS ||
        extension_count == 0U)
    {
        return false;
    }

    R2D::McVector<VkExtensionProperties> extensions(extension_count);
    if (vkEnumerateDeviceExtensionProperties(
            physical_device_,
            nullptr,
            &extension_count,
            extensions.data()) != VK_SUCCESS)
    {
        return false;
    }

    for (const auto& extension : extensions)
    {
        if (std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        {
            return true;
        }
    }
    return false;
}

// Find a physical device + queue family that supports graphics, can present to
// `surface_`, and exposes VK_KHR_swapchain. Returns false if none exists (a
// headless/no-GPU skip).
[[nodiscard]] bool selectPresentDevice(
    VkInstance instance_,
    VkSurfaceKHR surface_,
    VkPhysicalDevice& out_physical_device_,
    R2D::U32& out_queue_family_index_)
{
    R2D::U32 device_count = 0U;
    if (vkEnumeratePhysicalDevices(instance_, &device_count, nullptr) != VK_SUCCESS || device_count == 0U)
    {
        return false;
    }
    R2D::McVector<VkPhysicalDevice> devices(device_count);
    if (vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()) != VK_SUCCESS)
    {
        return false;
    }

    for (auto physical_device : devices)
    {
        if (!deviceSupportsSwapchain(physical_device))
        {
            continue;
        }

        R2D::U32 family_count = 0U;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
        if (family_count == 0U)
        {
            continue;
        }
        R2D::McVector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());

        for (R2D::U32 family_index = 0U; family_index < family_count; ++family_index)
        {
            if (families[family_index].queueCount == 0U ||
                (families[family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0U)
            {
                continue;
            }

            VkBool32 surface_supported = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(
                    physical_device,
                    family_index,
                    surface_,
                    &surface_supported) != VK_SUCCESS ||
                surface_supported != VK_TRUE)
            {
                continue;
            }

            out_physical_device_ = physical_device;
            out_queue_family_index_ = family_index;
            return true;
        }
    }
    return false;
}

[[nodiscard]] VkResult createPresentDevice(
    VkPhysicalDevice physical_device_,
    R2D::U32 queue_family_index_,
    VkDevice& out_device_) noexcept
{
    const float queue_priority = 1.0F;
    const VkDeviceQueueCreateInfo queue_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .queueFamilyIndex = queue_family_index_,
        .queueCount = 1U,
        .pQueuePriorities = &queue_priority,
    };
    const char* const device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .queueCreateInfoCount = 1U,
        .pQueueCreateInfos = &queue_info,
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 1U,
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = nullptr,
    };
    return vkCreateDevice(physical_device_, &device_info, nullptr, &out_device_);
}

// Pick a surface format, preferring B8G8R8A8_UNORM / sRGB-nonlinear. Returns
// false if the surface advertises no formats.
[[nodiscard]] bool selectSurfaceFormat(
    VkPhysicalDevice physical_device_,
    VkSurfaceKHR surface_,
    VkSurfaceFormatKHR& out_format_)
{
    R2D::U32 format_count = 0U;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr) != VK_SUCCESS ||
        format_count == 0U)
    {
        return false;
    }
    R2D::McVector<VkSurfaceFormatKHR> formats(format_count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data()) !=
        VK_SUCCESS)
    {
        return false;
    }

    out_format_ = formats[0];
    for (const auto& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            out_format_ = format;
            break;
        }
    }
    return true;
}

[[nodiscard]] VkCompositeAlphaFlagBitsKHR selectCompositeAlpha(
    VkCompositeAlphaFlagsKHR supported_) noexcept
{
    if ((supported_ & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0U)
    {
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    for (R2D::U32 bit = 1U; bit != 0U; bit <<= 1U)
    {
        if ((supported_ & bit) != 0U)
        {
            return static_cast<VkCompositeAlphaFlagBitsKHR>(bit);
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

// The real path: build a swapchain from the live surface and assert the runtime
// reports back consistent state and resolvable native handles, then release it.
void runSwapchainChecks(
    R2DT::TestContext& context_,
    R2D::PresentHost& present_host_,
    VkPhysicalDevice physical_device_,
    VkDevice device_,
    R2D::U32 queue_family_index_)
{
    const VkSurfaceKHR surface = present_host_.surfaceHandle();

    // Filled by the driver below; left uninitialized on purpose -- value-
    // initializing `{}` would set the VkSurfaceTransformFlagBitsKHR member to a
    // non-enumerator zero (a clang-tidy bugprone warning), and it is only read
    // after the VK_SUCCESS check.
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface, &caps) != VK_SUCCESS)
    {
        return;
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFU)
    {
        R2D::U32 pixel_width = 0U;
        R2D::U32 pixel_height = 0U;
        if (!present_host_.pixelSize(pixel_width, pixel_height))
        {
            return;
        }
        extent.width = clampU32(pixel_width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = clampU32(pixel_height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0U || extent.height == 0U)
    {
        return;
    }

    VkSurfaceFormatKHR surface_format{};
    if (!selectSurfaceFormat(physical_device_, surface, surface_format))
    {
        return;
    }

    R2D::U32 min_image_count = caps.minImageCount;
    if (caps.maxImageCount != 0U && min_image_count > caps.maxImageCount)
    {
        min_image_count = caps.maxImageCount;
    }
    if (min_image_count == 0U || min_image_count > kSwapchainImageCapacity)
    {
        return;
    }

    Runtime runtime{};
    R2D_TEST_CHECK_EQ(context_, runtime.initialize({.device = device_}).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, runtime.reserveSwapchains(1U).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        runtime.reserveSwapchainImages(kSwapchainImageCapacity).code,
        R2D::NativeStatusCode::Ok);

    const R2D::U32 queue_family_index = queue_family_index_;
    SwapchainState state{};
    std::array<SwapchainImageRef, kSwapchainImageCapacity> images{};
    const auto create_result = runtime.createSwapchain(
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
            .image_usage = static_cast<R2D::U32>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
            .sharing_mode = static_cast<R2D::U32>(VK_SHARING_MODE_EXCLUSIVE),
            .pre_transform = static_cast<R2D::U32>(caps.currentTransform),
            .composite_alpha = static_cast<R2D::U32>(selectCompositeAlpha(caps.supportedCompositeAlpha)),
            .present_mode = static_cast<R2D::U32>(VK_PRESENT_MODE_FIFO_KHR),
            .clipped = 1U,
            .image_array_layers = 1U,
            .image_view_type = static_cast<R2D::U32>(VK_IMAGE_VIEW_TYPE_2D),
            .image_aspect_flags = static_cast<R2D::U32>(VK_IMAGE_ASPECT_COLOR_BIT),
            .flags = 0U,
        },
        state,
        images);
    R2D_TEST_CHECK_EQ(context_, create_result.code, R2D::NativeStatusCode::Ok);
    if (create_result.code != R2D::NativeStatusCode::Ok)
    {
        return;
    }

    R2D_TEST_CHECK(context_, state.image_count >= min_image_count);
    R2D_TEST_CHECK(context_, state.image_count <= kSwapchainImageCapacity);
    R2D_TEST_CHECK_EQ(context_, state.width, extent.width);
    R2D_TEST_CHECK_EQ(context_, state.height, extent.height);
    R2D_TEST_CHECK_EQ(context_, state.format, static_cast<R2D::U32>(surface_format.format));
    R2D_TEST_CHECK_EQ(context_, runtime.swapchainCount(), 1U);

    SwapchainState resolved_state{};
    R2D_TEST_CHECK_EQ(
        context_,
        runtime.resolveSwapchainState(state, resolved_state).code,
        R2D::NativeStatusCode::Ok);

    VkSwapchainKHR native_swapchain = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        runtime.resolveNativeSwapchain(state, native_swapchain).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, native_swapchain != VK_NULL_HANDLE);

    VkImage native_image = VK_NULL_HANDLE;
    VkImageView native_image_view = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        runtime.resolveNativeSwapchainImage(images[0], native_image, native_image_view).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, native_image != VK_NULL_HANDLE);
    R2D_TEST_CHECK(context_, native_image_view != VK_NULL_HANDLE);

    R2D_TEST_CHECK_EQ(
        context_,
        runtime.releaseSwapchainState(state).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, runtime.swapchainCount(), 0U);

    std::cout << "present-host 22B: surface+swapchain OK, " << state.image_count << " images, "
              << extent.width << "x" << extent.height << ", format "
              << static_cast<R2D::U32>(surface_format.format) << '\n';
    // `runtime` is local: its destructor tears down the swapchain + image views
    // here, while `device_` is still alive (the caller destroys the device after
    // this returns).
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};

    R2D::PresentHost present_host{};
    if (!present_host.initialize("Render2D Present Host 22B", 640, 480, true))
    {
        std::cout << "present-host 22B: no usable video driver, skipping ("
                  << present_host.lastError() << ")\n";
        return context.result();
    }

    const auto extensions = present_host.requiredInstanceExtensions();
    if (extensions.empty())
    {
        std::cout << "present-host 22B: no Vulkan instance extensions, skipping\n";
        return context.result();
    }

    VkInstance instance = VK_NULL_HANDLE;
    if (createPresentInstance(extensions, instance) != VK_SUCCESS || instance == VK_NULL_HANDLE)
    {
        std::cout << "present-host 22B: no Vulkan instance, skipping\n";
        return context.result();
    }

    if (!present_host.createSurface(instance))
    {
        std::cout << "present-host 22B: surface creation failed, skipping ("
                  << present_host.lastError() << ")\n";
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    R2D::U32 queue_family_index = 0U;
    if (!selectPresentDevice(instance, present_host.surfaceHandle(), physical_device, queue_family_index))
    {
        std::cout << "present-host 22B: no present-capable device, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    // SDL's own present query should agree with the surface-support result for
    // the family we picked.
    R2D_TEST_CHECK(
        context,
        present_host.presentationSupport(instance, physical_device, queue_family_index));

    VkDevice device = VK_NULL_HANDLE;
    if (createPresentDevice(physical_device, queue_family_index, device) != VK_SUCCESS ||
        device == VK_NULL_HANDLE)
    {
        std::cout << "present-host 22B: device creation failed, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    runSwapchainChecks(context, present_host, physical_device, device, queue_family_index);

    // Teardown order: swapchain (inside runSwapchainChecks) -> surface -> device
    // -> instance -> window/SDL (present_host destructor).
    present_host.destroySurface();
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return context.result();
}

} // namespace

int main() noexcept
{
    try
    {
        return runTest();
    }
    catch (const std::exception& exception)
    {
        std::fputs("present_host_window_smoke exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...)
    {
        std::fputs("present_host_window_smoke unknown exception\n", stderr);
        return 1;
    }
}
