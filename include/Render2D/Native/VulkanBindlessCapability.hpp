#pragma once

#include "Render2D/Core/Types.hpp"

#include <vulkan/vulkan.h>

#include <type_traits>

namespace Render2D {

// Runtime-side bindless texturing capability probed from a VkPhysicalDevice.
// This is NOT an ECS component; it is a plain POD info record the bindless
// texture table consults to choose between the descriptor-indexing path and
// the per-packet combined-image-sampler fallback. The host owns the device, so
// the probe takes the physical device and reads it without enabling anything.
struct VulkanBindlessCapability {
    U32 supported;                          // 1 (kVulkanBindlessSupported) = full sampler-array bindless usable
    U32 partially_bound;                    // descriptorBindingPartiallyBound
    U32 runtime_descriptor_array;           // runtimeDescriptorArray
    U32 sampled_image_non_uniform_indexing; // shaderSampledImageArrayNonUniformIndexing
    U32 sampled_image_update_after_bind;    // descriptorBindingSampledImageUpdateAfterBind
    U32 max_per_stage_sampled_images;       // maxPerStageDescriptorUpdateAfterBindSampledImages
    U32 max_descriptor_set_sampled_images;  // maxDescriptorSetUpdateAfterBindSampledImages
    U32 api_version;                        // VkPhysicalDeviceProperties::apiVersion
};

inline constexpr U32 kVulkanBindlessSupported = 1U;

// Probes the four descriptor-indexing features and update-after-bind sampled
// image limits required for a single large, partially-bound, runtime-sized,
// non-uniformly-indexed combined-image-sampler array. Returns an all-zero
// capability (supported == 0) for a null device or below Vulkan 1.2.
inline VulkanBindlessCapability queryVulkanBindlessCapability(VkPhysicalDevice physical_device_) noexcept
{
    VulkanBindlessCapability capability{};
    if (physical_device_ == VK_NULL_HANDLE) {
        return capability;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device_, &properties);
    capability.api_version = properties.apiVersion;
    if (properties.apiVersion < VK_API_VERSION_1_2) {
        return capability;
    }

    VkPhysicalDeviceDescriptorIndexingFeatures features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features;
    vkGetPhysicalDeviceFeatures2(physical_device_, &features2);

    VkPhysicalDeviceDescriptorIndexingProperties indexing_properties{};
    indexing_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &indexing_properties;
    vkGetPhysicalDeviceProperties2(physical_device_, &properties2);

    capability.partially_bound =
        features.descriptorBindingPartiallyBound == VK_TRUE ? 1U : 0U;
    capability.runtime_descriptor_array =
        features.runtimeDescriptorArray == VK_TRUE ? 1U : 0U;
    capability.sampled_image_non_uniform_indexing =
        features.shaderSampledImageArrayNonUniformIndexing == VK_TRUE ? 1U : 0U;
    capability.sampled_image_update_after_bind =
        features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE ? 1U : 0U;
    capability.max_per_stage_sampled_images =
        indexing_properties.maxPerStageDescriptorUpdateAfterBindSampledImages;
    capability.max_descriptor_set_sampled_images =
        indexing_properties.maxDescriptorSetUpdateAfterBindSampledImages;
    capability.supported =
        (capability.partially_bound != 0U &&
         capability.runtime_descriptor_array != 0U &&
         capability.sampled_image_non_uniform_indexing != 0U &&
         capability.sampled_image_update_after_bind != 0U) ?
        kVulkanBindlessSupported :
        0U;
    return capability;
}

// Binding flags for the combined-image-sampler array binding under bindless:
// partially bound (not every array slot is resident) plus update-after-bind
// (descriptors written after the set is bound). Returns 0 when unsupported,
// which makes VulkanDescriptorRuntime build a plain array binding (the
// fallback path) rather than a bindless one.
inline U32 bindlessCombinedImageSamplerBindingFlags(const VulkanBindlessCapability& capability_) noexcept
{
    if (capability_.supported != kVulkanBindlessSupported) {
        return 0U;
    }
    return static_cast<U32>(
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
}

// Descriptor pool flag required to allocate update-after-bind sets. 0 when
// unsupported. VulkanDescriptorRuntime always also ORs in the free-set bit.
inline U32 bindlessDescriptorPoolFlags(const VulkanBindlessCapability& capability_) noexcept
{
    if (capability_.supported != kVulkanBindlessSupported) {
        return 0U;
    }
    return static_cast<U32>(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT);
}

// Descriptor set layout flag required for an update-after-bind layout. 0 when
// unsupported.
inline U32 bindlessDescriptorSetLayoutFlags(const VulkanBindlessCapability& capability_) noexcept
{
    if (capability_.supported != kVulkanBindlessSupported) {
        return 0U;
    }
    return static_cast<U32>(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);
}

static_assert(std::is_trivial_v<VulkanBindlessCapability>);
static_assert(std::is_standard_layout_v<VulkanBindlessCapability>);
static_assert(std::is_trivially_copyable_v<VulkanBindlessCapability>);
static_assert(std::is_aggregate_v<VulkanBindlessCapability>);

} // namespace Render2D
