#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include <Render2D/Render2D.hpp>

#include <vulkan/vulkan.h>


namespace Render2DTest {

struct VulkanSmokeContext {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    Render2D::U32 queue_family_index;
    Render2D::U32 api_version;
    bool supports_dynamic_rendering;
    bool supports_descriptor_indexing;
};

struct VulkanSmokeFeatureSupport {
    Render2D::U32 api_version;
    bool supports_dynamic_rendering;
    bool supports_descriptor_indexing;
};

inline Render2D::U32 queryInstanceApiVersion() noexcept
{
    Render2D::U32 api_version = VK_API_VERSION_1_0;
    const VkResult result = vkEnumerateInstanceVersion(&api_version);
    return result == VK_SUCCESS ? api_version : VK_API_VERSION_1_0;
}

inline VkResult createInstance(VkInstance& out_instance_, Render2D::U32& out_api_version_) noexcept
{
    constexpr char kApplicationName[] = "Render2D Vulkan Smoke";
    out_api_version_ = queryInstanceApiVersion();
    const VkApplicationInfo application_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = kApplicationName,
        .applicationVersion = VK_MAKE_VERSION(0U, 1U, 0U),
        .pEngineName = kApplicationName,
        .engineVersion = VK_MAKE_VERSION(0U, 1U, 0U),
        .apiVersion = out_api_version_ >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_0,
    };
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0U,
        .ppEnabledExtensionNames = nullptr,
    };

    return vkCreateInstance(&instance_info, nullptr, &out_instance_);
}

inline VulkanSmokeFeatureSupport queryFeatureSupport(VkPhysicalDevice physical_device_) noexcept
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device_, &properties);

    VulkanSmokeFeatureSupport support{
        .api_version = properties.apiVersion,
        .supports_dynamic_rendering = false,
        .supports_descriptor_indexing = false,
    };

    if (properties.apiVersion >= VK_API_VERSION_1_3) {
        VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering{};
        dynamic_rendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        dynamic_rendering.dynamicRendering = VK_FALSE;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &dynamic_rendering;
        vkGetPhysicalDeviceFeatures2(physical_device_, &features);
        support.supports_dynamic_rendering = dynamic_rendering.dynamicRendering == VK_TRUE;
    }

    if (properties.apiVersion >= VK_API_VERSION_1_2) {
        VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing{};
        descriptor_indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &descriptor_indexing;
        vkGetPhysicalDeviceFeatures2(physical_device_, &features);
        support.supports_descriptor_indexing =
            descriptor_indexing.descriptorBindingPartiallyBound == VK_TRUE &&
            descriptor_indexing.runtimeDescriptorArray == VK_TRUE;
    }

    return support;
}

inline bool findQueueFamily(
    VkPhysicalDevice physical_device_,
    VkQueueFlags preferred_flags_,
    Render2D::U32& out_queue_family_index_)
{
    Render2D::U32 queue_family_count = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, nullptr);
    if (queue_family_count == 0U) {
        return false;
    }

    Render2D::McVector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device_,
        &queue_family_count,
        queue_families.data());

    for (Render2D::U32 queue_family_index = 0U; queue_family_index < queue_family_count; ++queue_family_index) {
        const auto& queue_family = queue_families[queue_family_index];
        if (queue_family.queueCount > 0U &&
            (queue_family.queueFlags & preferred_flags_) == preferred_flags_) {
            out_queue_family_index_ = queue_family_index;
            return true;
        }
    }

    for (Render2D::U32 queue_family_index = 0U; queue_family_index < queue_family_count; ++queue_family_index) {
        if (queue_families[queue_family_index].queueCount > 0U) {
            out_queue_family_index_ = queue_family_index;
            return true;
        }
    }

    return false;
}

inline bool selectPhysicalDevice(
    VkInstance instance_,
    VkPhysicalDevice& out_physical_device_,
    Render2D::U32& out_queue_family_index_,
    VulkanSmokeFeatureSupport& out_feature_support_)
{
    Render2D::U32 physical_device_count = 0U;
    VkResult result = vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr);
    if (result != VK_SUCCESS || physical_device_count == 0U) {
        return false;
    }

    Render2D::McVector<VkPhysicalDevice> physical_devices(physical_device_count);
    result = vkEnumeratePhysicalDevices(instance_, &physical_device_count, physical_devices.data());
    if (result != VK_SUCCESS) {
        return false;
    }

    VkPhysicalDevice fallback_device = VK_NULL_HANDLE;
    Render2D::U32 fallback_queue_family_index = 0U;
    VulkanSmokeFeatureSupport fallback_support{};

    for (auto physical_device : physical_devices) {
        Render2D::U32 queue_family_index = 0U;
        if (findQueueFamily(physical_device, VK_QUEUE_GRAPHICS_BIT, queue_family_index)) {
            const VulkanSmokeFeatureSupport support = queryFeatureSupport(physical_device);
            if (support.supports_dynamic_rendering) {
                out_physical_device_ = physical_device;
                out_queue_family_index_ = queue_family_index;
                out_feature_support_ = support;
                return true;
            }
            if (fallback_device == VK_NULL_HANDLE) {
                fallback_device = physical_device;
                fallback_queue_family_index = queue_family_index;
                fallback_support = support;
            }
        }
    }

    if (fallback_device != VK_NULL_HANDLE) {
        out_physical_device_ = fallback_device;
        out_queue_family_index_ = fallback_queue_family_index;
        out_feature_support_ = fallback_support;
        return true;
    }

    return false;
}

inline VkResult createDevice(
    VkPhysicalDevice physical_device_,
    Render2D::U32 queue_family_index_,
    const VulkanSmokeFeatureSupport& feature_support_,
    VkDevice& out_device_) noexcept
{
    float queue_priority = 1.0F;
    const VkDeviceQueueCreateInfo queue_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .queueFamilyIndex = queue_family_index_,
        .queueCount = 1U,
        .pQueuePriorities = &queue_priority,
    };

    VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering{};
    dynamic_rendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamic_rendering.dynamicRendering = feature_support_.supports_dynamic_rendering ? VK_TRUE : VK_FALSE;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing{};
    descriptor_indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descriptor_indexing.pNext = feature_support_.supports_dynamic_rendering ? &dynamic_rendering : nullptr;
    descriptor_indexing.descriptorBindingPartiallyBound = feature_support_.supports_descriptor_indexing ? VK_TRUE : VK_FALSE;
    descriptor_indexing.runtimeDescriptorArray = feature_support_.supports_descriptor_indexing ? VK_TRUE : VK_FALSE;

    const void* feature_chain = nullptr;
    if (feature_support_.supports_descriptor_indexing) {
        feature_chain = &descriptor_indexing;
    } else if (feature_support_.supports_dynamic_rendering) {
        feature_chain = &dynamic_rendering;
    }

    const VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = feature_chain,
        .flags = 0U,
        .queueCreateInfoCount = 1U,
        .pQueueCreateInfos = &queue_info,
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0U,
        .ppEnabledExtensionNames = nullptr,
        .pEnabledFeatures = nullptr,
    };

    return vkCreateDevice(physical_device_, &device_info, nullptr, &out_device_);
}

inline bool createVulkanSmokeContext(VulkanSmokeContext& out_context_)
{
    VkInstance instance = VK_NULL_HANDLE;
    Render2D::U32 instance_api_version = VK_API_VERSION_1_0;
    VkResult result = createInstance(instance, instance_api_version);
    if (result != VK_SUCCESS) {
        return false;
    }

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    Render2D::U32 queue_family_index = 0U;
    VulkanSmokeFeatureSupport feature_support{};
    if (!selectPhysicalDevice(instance, physical_device, queue_family_index, feature_support)) {
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    VkDevice device = VK_NULL_HANDLE;
    result = createDevice(physical_device, queue_family_index, feature_support, device);
    if (result != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return false;
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queue_family_index, 0U, &queue);
    out_context_ = {
        .instance = instance,
        .physical_device = physical_device,
        .device = device,
        .queue = queue,
        .queue_family_index = queue_family_index,
        .api_version = feature_support.api_version < instance_api_version ?
            feature_support.api_version :
            instance_api_version,
        .supports_dynamic_rendering = feature_support.supports_dynamic_rendering,
        .supports_descriptor_indexing = feature_support.supports_descriptor_indexing,
    };
    return queue != VK_NULL_HANDLE;
}

inline void destroyVulkanSmokeContext(const VulkanSmokeContext& context_) noexcept
{
    if (context_.device != VK_NULL_HANDLE) {
        vkDestroyDevice(context_.device, nullptr);
    }
    if (context_.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(context_.instance, nullptr);
    }
}

} // namespace Render2DTest
