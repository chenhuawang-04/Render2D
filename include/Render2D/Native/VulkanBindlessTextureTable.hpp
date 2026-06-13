#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/VulkanBindlessCapability.hpp"
#include "Render2D/Native/VulkanResourceRuntime.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <type_traits>

namespace Render2D {

// The split bindless layout: one persistent descriptor set whose binding 0 is a
// large SAMPLED_IMAGE array indexed directly by texture_id (identity indexing,
// no map) and whose binding 1 is a small SAMPLER array. Decoupling samplers from
// images is the point of the split: a texture residency write touches only a
// view, never repeats a sampler.
inline constexpr U32 kVulkanBindlessSampledImageBinding = 0U;
inline constexpr U32 kVulkanBindlessSamplerBinding = 1U;

struct VulkanBindlessTextureTableConfig {
    VkDevice device;
    U32 texture_capacity;          // SAMPLED_IMAGE array size; rejected if > capability.max_descriptor_set_sampled_images
    U32 sampler_capacity;          // SAMPLER array size (small: a handful of filter/address combos)
    VkImageLayout sampled_layout;  // layout the views are sampled in (typically SHADER_READ_ONLY_OPTIMAL)
    VkImageView debug_fill_view;   // optional Debug hole-fill view; VK_NULL_HANDLE = pure partially-bound (Release default)
};

// Runtime-side bindless texture table. NOT an ECS component: the ECS keeps only
// texture_id + generation on each SpriteInstance, and this table turns that into
// a resident SAMPLED_IMAGE descriptor. It owns its own split descriptor set
// layout / pool / set rather than reusing VulkanDescriptorRuntime, because
// bindless wants one giant set bound once per frame, not many small slices. The
// CPU-side residency record is the only McVector here; the GPU array itself is a
// fixed-count Vulkan descriptor binding, so it cannot and must not be a McVector.
template<class Provider, class Dim>
class VulkanBindlessTextureTable {
public:
    VulkanBindlessTextureTable() = default;
    VulkanBindlessTextureTable(const VulkanBindlessTextureTable&) = delete;
    VulkanBindlessTextureTable& operator=(const VulkanBindlessTextureTable&) = delete;
    VulkanBindlessTextureTable(VulkanBindlessTextureTable&&) = delete;
    VulkanBindlessTextureTable& operator=(VulkanBindlessTextureTable&&) = delete;

    ~VulkanBindlessTextureTable() noexcept
    {
        shutdown();
    }

    NativeResult initialize(
        VulkanBindlessTextureTableConfig config_,
        const VulkanBindlessCapability& capability_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            // No bindless support: refuse so the caller takes the per-packet
            // combined-image-sampler fallback. UnsupportedDomain (not an error)
            // mirrors how the capability flag helpers fold back to 0.
            if (capability_.supported != kVulkanBindlessSupported) {
                return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
            }
            if (config_.device == VK_NULL_HANDLE ||
                config_.texture_capacity == 0U ||
                config_.sampler_capacity == 0U ||
                config_.texture_capacity > capability_.max_descriptor_set_sampled_images ||
                descriptor_pool != VK_NULL_HANDLE ||
                descriptor_set_layout != VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            device = config_.device;
            texture_capacity = config_.texture_capacity;
            sampler_capacity = config_.sampler_capacity;
            sampled_layout = config_.sampled_layout;
            debug_fill_view = config_.debug_fill_view;
            capability = capability_;

            NativeStatusCode create_code = createDescriptorSetLayout();
            if (create_code != NativeStatusCode::Ok) {
                shutdown();
                return makeResult(create_code, 0U, 0U);
            }
            create_code = createDescriptorPool();
            if (create_code != NativeStatusCode::Ok) {
                shutdown();
                return makeResult(create_code, 0U, 0U);
            }
            create_code = allocateDescriptorSet();
            if (create_code != NativeStatusCode::Ok) {
                shutdown();
                return makeResult(create_code, 0U, 0U);
            }

            // Reserve the residency record so the lazy per-texture_id growth in
            // setResident never reallocates. This is CPU memory only.
            try {
                slots.reserve(texture_capacity);
            } catch (...) {
                shutdown();
                return makeResult(NativeStatusCode::OutOfMemory, 0U, 0U);
            }

#ifndef NDEBUG
            // Debug-only safety net: point every slot at a 1x1 fill (e.g. magenta)
            // so a texture_id that escapes the CPU isResident gate samples loudly
            // instead of hitting undefined behaviour on a partially-bound slot.
            // Release leaves holes genuinely empty (zero writes, zero memory).
            if (debug_fill_view != VK_NULL_HANDLE) {
                for (U32 index = 0U; index < texture_capacity; ++index) {
                    writeSampledImage(index, debug_fill_view);
                }
            }
#endif
            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    void shutdown() noexcept
    {
        if (device != VK_NULL_HANDLE) {
            if (descriptor_pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            }
            if (descriptor_set_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
            }
        }

        slots.clear();
        texture_capacity = 0U;
        sampler_capacity = 0U;
        sampled_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        debug_fill_view = VK_NULL_HANDLE;
        capability = {};
        descriptor_set = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        descriptor_set_layout = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        last_vulkan_result = VK_SUCCESS;
    }

    // Writes one entry of the small SAMPLER array (binding 1). Samplers are set
    // once before the set is ever bound (the binding is not update-after-bind).
    NativeResult setSampler(U32 sampler_index_, VkSampler sampler_) noexcept
    {
        if (!isInitialized()) {
            return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
        }
        if (sampler_ == VK_NULL_HANDLE || sampler_index_ >= sampler_capacity) {
            return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
        }

        const VkDescriptorImageInfo image_info{
            .sampler = sampler_,
            .imageView = VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = descriptor_set,
            .dstBinding = kVulkanBindlessSamplerBinding,
            .dstArrayElement = sampler_index_,
            .descriptorCount = 1U,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo = &image_info,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };
        vkUpdateDescriptorSets(device, 1U, &write, 0U, nullptr);
        return makeResult(NativeStatusCode::Ok, sampler_index_, 0U);
    }

    // Makes texture_id resident: writes SAMPLED_IMAGE[texture_id] = image view and
    // records (texture_id, generation) for the CPU stale gate. Uses update-after-
    // bind, so it is safe even after the set has been bound for an earlier frame.
    NativeResult setResident(
        U32 texture_id_,
        U32 generation_,
        const ImageRef<Provider, Dim>& image_,
        const VulkanResourceRuntime<Provider, Dim>& resource_runtime_) noexcept
    {
        if (!isInitialized()) {
            return makeResult(NativeStatusCode::InvalidInput, texture_id_, generation_);
        }
        if (texture_id_ >= texture_capacity) {
            return makeResult(NativeStatusCode::InvalidInput, texture_id_, generation_);
        }

        VkImage native_image = VK_NULL_HANDLE;
        VkImageView native_image_view = VK_NULL_HANDLE;
        const NativeResult resolve = resource_runtime_.resolveNativeImage(image_, native_image, native_image_view);
        if (resolve.code != NativeStatusCode::Ok) {
            return resolve;
        }

        if (!ensureSlot(texture_id_)) {
            return makeResult(NativeStatusCode::OutOfMemory, texture_id_, generation_);
        }

        writeSampledImage(texture_id_, native_image_view);
        slots[texture_id_] = {.generation = generation_, .resident = 1U};
        return makeResult(NativeStatusCode::Ok, texture_id_, generation_);
    }

    // Marks texture_id non-resident. The (id, generation) must match the resident
    // record (rejects stale/double evicts). The backing VkImage's real destruction
    // stays with the deferred-destroy runtime; this only updates residency and,
    // in Debug, repoints the slot at the fill view.
    NativeResult evict(U32 texture_id_, U32 generation_) noexcept
    {
        if (!isInitialized()) {
            return makeResult(NativeStatusCode::InvalidInput, texture_id_, generation_);
        }
        if (!isResident(texture_id_, generation_)) {
            return makeResult(NativeStatusCode::StaleReference, texture_id_, generation_);
        }

        slots[texture_id_].resident = 0U;
#ifndef NDEBUG
        if (debug_fill_view != VK_NULL_HANDLE) {
            writeSampledImage(texture_id_, debug_fill_view);
        }
#endif
        return makeResult(NativeStatusCode::Ok, texture_id_, generation_);
    }

    // CPU stale gate: the shader indexes by texture_id alone, so a SpriteInstance's
    // (texture_id, generation) must be validated here before it reaches the GPU.
    bool isResident(U32 texture_id_, U32 generation_) const noexcept
    {
        if (texture_id_ >= slots.size()) {
            return false;
        }
        const Slot& slot = slots[texture_id_];
        return slot.resident != 0U && slot.generation == generation_;
    }

    bool isInitialized() const noexcept
    {
        return device != VK_NULL_HANDLE &&
            descriptor_pool != VK_NULL_HANDLE &&
            descriptor_set_layout != VK_NULL_HANDLE &&
            descriptor_set != VK_NULL_HANDLE;
    }

    VkDescriptorSet nativeDescriptorSet() const noexcept
    {
        return descriptor_set;
    }

    VkDescriptorSetLayout nativeDescriptorSetLayout() const noexcept
    {
        return descriptor_set_layout;
    }

    U32 textureCapacity() const noexcept
    {
        return texture_capacity;
    }

    U32 samplerCapacity() const noexcept
    {
        return sampler_capacity;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

private:
    struct Slot {
        U32 generation;
        U32 resident;
    };

    static NativeResult makeResult(
        NativeStatusCode code_,
        U32 object_id_,
        U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Descriptor,
            .object_id = {.value = object_id_},
            .generation = {.value = generation_},
        };
    }

    static NativeStatusCode mapVulkanResult(VkResult result_) noexcept
    {
        switch (result_) {
        case VK_SUCCESS:
            return NativeStatusCode::Ok;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return NativeStatusCode::OutOfMemory;
        case VK_ERROR_DEVICE_LOST:
            return NativeStatusCode::DeviceLost;
        default:
            return NativeStatusCode::InvalidInput;
        }
    }

    NativeStatusCode createDescriptorSetLayout() noexcept
    {
        const std::array<VkDescriptorSetLayoutBinding, 2U> bindings{
            VkDescriptorSetLayoutBinding{
                .binding = kVulkanBindlessSampledImageBinding,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = texture_capacity,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            },
            VkDescriptorSetLayoutBinding{
                .binding = kVulkanBindlessSamplerBinding,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = sampler_capacity,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            },
        };
        // Sampled-image array is partially bound + update-after-bind; the small
        // sampler array is only partially bound (filled once before any bind).
        const std::array<VkDescriptorBindingFlags, 2U> binding_flags{
            static_cast<VkDescriptorBindingFlags>(bindlessSampledImageBindingFlags(capability)),
            static_cast<VkDescriptorBindingFlags>(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT),
        };

        const VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .pNext = nullptr,
            .bindingCount = static_cast<U32>(bindings.size()),
            .pBindingFlags = binding_flags.data(),
        };
        const VkDescriptorSetLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = &binding_flags_info,
            .flags = static_cast<VkDescriptorSetLayoutCreateFlags>(bindlessDescriptorSetLayoutFlags(capability)),
            .bindingCount = static_cast<U32>(bindings.size()),
            .pBindings = bindings.data(),
        };
        const VkResult vk_result = vkCreateDescriptorSetLayout(device, &create_info, nullptr, &descriptor_set_layout);
        last_vulkan_result = vk_result;
        return mapVulkanResult(vk_result);
    }

    NativeStatusCode createDescriptorPool() noexcept
    {
        const std::array<VkDescriptorPoolSize, 2U> pool_sizes{
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = texture_capacity,
            },
            VkDescriptorPoolSize{
                .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = sampler_capacity,
            },
        };
        const VkDescriptorPoolCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = static_cast<VkDescriptorPoolCreateFlags>(bindlessDescriptorPoolFlags(capability)),
            .maxSets = 1U,
            .poolSizeCount = static_cast<U32>(pool_sizes.size()),
            .pPoolSizes = pool_sizes.data(),
        };
        const VkResult vk_result = vkCreateDescriptorPool(device, &create_info, nullptr, &descriptor_pool);
        last_vulkan_result = vk_result;
        return mapVulkanResult(vk_result);
    }

    NativeStatusCode allocateDescriptorSet() noexcept
    {
        const VkDescriptorSetAllocateInfo allocate_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = descriptor_pool,
            .descriptorSetCount = 1U,
            .pSetLayouts = &descriptor_set_layout,
        };
        const VkResult vk_result = vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set);
        last_vulkan_result = vk_result;
        return mapVulkanResult(vk_result);
    }

    void writeSampledImage(U32 index_, VkImageView image_view_) noexcept
    {
        const VkDescriptorImageInfo image_info{
            .sampler = VK_NULL_HANDLE,
            .imageView = image_view_,
            .imageLayout = sampled_layout,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = descriptor_set,
            .dstBinding = kVulkanBindlessSampledImageBinding,
            .dstArrayElement = index_,
            .descriptorCount = 1U,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &image_info,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };
        vkUpdateDescriptorSets(device, 1U, &write, 0U, nullptr);
    }

    // Grows the CPU residency record to cover texture_id (lazily, never past the
    // reserved texture_capacity). The GPU array is already sized; this only tracks
    // which ids the host has marked resident, for the stale gate.
    bool ensureSlot(U32 texture_id_) noexcept
    {
        try {
            while (slots.size() <= texture_id_) {
                slots.push_back({.generation = 0U, .resident = 0U});
            }
        } catch (...) {
            return false;
        }
        return true;
    }

    McVector<Slot> slots; // CPU residency record, indexed by texture_id
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VulkanBindlessCapability capability{};
    VkImageLayout sampled_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageView debug_fill_view = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 texture_capacity = 0U;
    U32 sampler_capacity = 0U;
};

static_assert(std::is_trivial_v<VulkanBindlessTextureTableConfig>);
static_assert(std::is_standard_layout_v<VulkanBindlessTextureTableConfig>);
static_assert(std::is_trivially_copyable_v<VulkanBindlessTextureTableConfig>);
static_assert(std::is_aggregate_v<VulkanBindlessTextureTableConfig>);

} // namespace Render2D
