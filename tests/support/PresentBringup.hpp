#pragma once

// Shared present-host bring-up helpers for the Stage 22 on-screen tests.
//
// These turn an SDL3-provided VkSurfaceKHR into a present-capable
// VkInstance/VkDevice and pick a swapchain surface format -- the part that is
// identical between `present_host_window_smoke` (22B, create+resolve+release)
// and `present_loop_smoke` (22C, acquire->record->submit->present loop). The
// frame-recording/loop logic stays in the 22C test; only the device/format
// bring-up lives here. This is test-only scaffolding (like VulkanSmokeContext),
// not production architecture -- the host engine owns instance/device creation
// at merge.

#include "Render2D/Memory/RenderVector.hpp"

#include <Render2D/Render2D.hpp>

#include <vulkan/vulkan.h>

#include <span>
#include <string_view>

namespace Render2DTest {

[[nodiscard]] inline Render2D::U32 clampU32(
    Render2D::U32 value_,
    Render2D::U32 low_,
    Render2D::U32 high_) noexcept
{
    if (value_ < low_) {
        return low_;
    }
    if (value_ > high_) {
        return high_;
    }
    return value_;
}

// Create a minimal VkInstance enabling exactly the extensions SDL reports as
// required for surface creation. apiVersion 1.0 keeps it maximally compatible --
// the surface/swapchain path needs no newer core.
[[nodiscard]] inline VkResult createPresentInstance(
    std::span<const char* const> extensions_,
    VkInstance& out_instance_) noexcept
{
    constexpr char kApplicationName[] = "Render2D Present Host";
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
        .enabledExtensionCount = static_cast<Render2D::U32>(extensions_.size()),
        .ppEnabledExtensionNames = extensions_.data(),
    };
    return vkCreateInstance(&instance_info, nullptr, &out_instance_);
}

[[nodiscard]] inline bool deviceSupportsSwapchain(VkPhysicalDevice physical_device_)
{
    Render2D::U32 extension_count = 0U;
    if (vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &extension_count, nullptr) !=
            VK_SUCCESS ||
        extension_count == 0U) {
        return false;
    }

    Render2D::McVector<VkExtensionProperties> extensions(extension_count);
    if (vkEnumerateDeviceExtensionProperties(
            physical_device_,
            nullptr,
            &extension_count,
            extensions.data()) != VK_SUCCESS) {
        return false;
    }

    for (const auto& extension : extensions) {
        if (std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
            return true;
        }
    }
    return false;
}

// Find a physical device + queue family that supports graphics, can present to
// `surface_`, and exposes VK_KHR_swapchain. Returns false if none exists (a
// headless/no-GPU skip).
[[nodiscard]] inline bool selectPresentDevice(
    VkInstance instance_,
    VkSurfaceKHR surface_,
    VkPhysicalDevice& out_physical_device_,
    Render2D::U32& out_queue_family_index_)
{
    Render2D::U32 device_count = 0U;
    if (vkEnumeratePhysicalDevices(instance_, &device_count, nullptr) != VK_SUCCESS ||
        device_count == 0U) {
        return false;
    }
    Render2D::McVector<VkPhysicalDevice> devices(device_count);
    if (vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()) != VK_SUCCESS) {
        return false;
    }

    for (auto physical_device : devices) {
        if (!deviceSupportsSwapchain(physical_device)) {
            continue;
        }

        Render2D::U32 family_count = 0U;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
        if (family_count == 0U) {
            continue;
        }
        Render2D::McVector<VkQueueFamilyProperties> families(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());

        for (Render2D::U32 family_index = 0U; family_index < family_count; ++family_index) {
            if (families[family_index].queueCount == 0U ||
                (families[family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0U) {
                continue;
            }

            VkBool32 surface_supported = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(
                    physical_device,
                    family_index,
                    surface_,
                    &surface_supported) != VK_SUCCESS ||
                surface_supported != VK_TRUE) {
                continue;
            }

            out_physical_device_ = physical_device;
            out_queue_family_index_ = family_index;
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline VkResult createPresentDevice(
    VkPhysicalDevice physical_device_,
    Render2D::U32 queue_family_index_,
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
[[nodiscard]] inline bool selectSurfaceFormat(
    VkPhysicalDevice physical_device_,
    VkSurfaceKHR surface_,
    VkSurfaceFormatKHR& out_format_)
{
    Render2D::U32 format_count = 0U;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr) !=
            VK_SUCCESS ||
        format_count == 0U) {
        return false;
    }
    Render2D::McVector<VkSurfaceFormatKHR> formats(format_count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data()) !=
        VK_SUCCESS) {
        return false;
    }

    out_format_ = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            out_format_ = format;
            break;
        }
    }
    return true;
}

[[nodiscard]] inline VkCompositeAlphaFlagBitsKHR selectCompositeAlpha(
    VkCompositeAlphaFlagsKHR supported_) noexcept
{
    if ((supported_ & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0U) {
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    for (Render2D::U32 bit = 1U; bit != 0U; bit <<= 1U) {
        if ((supported_ & bit) != 0U) {
            return static_cast<VkCompositeAlphaFlagBitsKHR>(bit);
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

} // namespace Render2DTest
