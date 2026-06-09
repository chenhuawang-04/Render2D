#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/VulkanResourceRuntime.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <type_traits>

namespace Render2D {

inline constexpr U32 kVulkanDescriptorCombinedImageSamplerBinding = 0U;
inline constexpr U32 kVulkanDescriptorStorageBufferBinding = 1U;

struct VulkanDescriptorRuntimeConfig {
    VkDevice device;
    U32 max_descriptor_sets;
    U32 combined_image_sampler_count;
    U32 storage_buffer_count;
    U32 descriptor_pool_flags;
    U32 descriptor_set_layout_flags;
    U32 combined_image_sampler_binding_flags;
    U32 storage_buffer_binding_flags;
};

template<class Provider, class Dim>
class VulkanDescriptorRuntime {
public:
    VulkanDescriptorRuntime() = default;
    VulkanDescriptorRuntime(const VulkanDescriptorRuntime&) = delete;
    VulkanDescriptorRuntime& operator=(const VulkanDescriptorRuntime&) = delete;
    VulkanDescriptorRuntime(VulkanDescriptorRuntime&&) = delete;
    VulkanDescriptorRuntime& operator=(VulkanDescriptorRuntime&&) = delete;

    ~VulkanDescriptorRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanDescriptorRuntimeConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (config_.device == VK_NULL_HANDLE ||
                config_.max_descriptor_sets == 0U ||
                (config_.combined_image_sampler_count == 0U && config_.storage_buffer_count == 0U) ||
                descriptor_pool != VK_NULL_HANDLE ||
                descriptor_set_layout != VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }
            if (hasDescriptorCountOverflow(
                    config_.combined_image_sampler_count,
                    config_.max_descriptor_sets) ||
                hasDescriptorCountOverflow(
                    config_.storage_buffer_count,
                    config_.max_descriptor_sets)) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            device = config_.device;
            max_descriptor_sets = config_.max_descriptor_sets;
            combined_image_sampler_count = config_.combined_image_sampler_count;
            storage_buffer_count = config_.storage_buffer_count;
            descriptor_array_capacity = combined_image_sampler_count > storage_buffer_count ?
                combined_image_sampler_count :
                storage_buffer_count;
            descriptor_pool_flags =
                config_.descriptor_pool_flags | static_cast<U32>(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
            descriptor_set_layout_flags = config_.descriptor_set_layout_flags;
            combined_image_sampler_binding_flags = config_.combined_image_sampler_binding_flags;
            storage_buffer_binding_flags = config_.storage_buffer_binding_flags;

            NativeStatusCode create_code = createDescriptorPool();
            if (create_code != NativeStatusCode::Ok) {
                shutdown();
                return makeResult(create_code, 0U, 0U);
            }

            create_code = createDescriptorSetLayout();
            if (create_code != NativeStatusCode::Ok) {
                shutdown();
                return makeResult(create_code, 0U, 0U);
            }

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

        descriptor_slots.clear();
        free_descriptor_ids.clear();
        active_descriptor_count = 0U;
        max_descriptor_sets = 0U;
        combined_image_sampler_count = 0U;
        storage_buffer_count = 0U;
        descriptor_array_capacity = 0U;
        descriptor_pool_flags = 0U;
        descriptor_set_layout_flags = 0U;
        combined_image_sampler_binding_flags = 0U;
        storage_buffer_binding_flags = 0U;
        descriptor_pool = VK_NULL_HANDLE;
        descriptor_set_layout = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        last_vulkan_result = VK_SUCCESS;
    }

    NativeCapacityResult reserveDescriptorSets(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            if (capacity_ > max_descriptor_sets) {
                return {
                    .code = NativeStatusCode::OutOfCapacity,
                    .requested_count = capacity_,
                    .available_count = max_descriptor_sets,
                };
            }

            try {
                descriptor_slots.reserve(capacity_);
                free_descriptor_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(descriptor_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(descriptor_slots.capacity()),
            };
        }
    }

    NativeResult allocateDescriptorSlice(
        U32 first_,
        U32 count_,
        DescriptorSlice<Provider, Dim>& out_slice_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized() || count_ == 0U || !isDescriptorRangeValid(first_, count_)) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }
            if (!hasAvailableSlot()) {
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            const VkDescriptorSetAllocateInfo allocate_info{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .pNext = nullptr,
                .descriptorPool = descriptor_pool,
                .descriptorSetCount = 1U,
                .pSetLayouts = &descriptor_set_layout,
            };
            VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
            const VkResult vk_result = vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), 0U, 0U);
            }

            U32 descriptor_id = 0U;
            if (!acquireDescriptorSlot(descriptor_id)) {
                vkFreeDescriptorSets(device, descriptor_pool, 1U, &descriptor_set);
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            auto& slot = descriptor_slots[descriptor_id];
            slot.descriptor_set = descriptor_set;
            slot.occupied = 1U;
            slot.slice = {
                .descriptor_set_id = descriptor_id,
                .first = first_,
                .count = count_,
                .generation = slot.generation.value,
            };
            ++active_descriptor_count;
            out_slice_ = slot.slice;

            return makeResult(NativeStatusCode::Ok, descriptor_id, slot.generation.value);
        }
    }

    NativeResult resolveDescriptorSlice(
        const DescriptorSlice<Provider, Dim>& slice_,
        DescriptorSlice<Provider, Dim>& out_slice_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveDescriptorSlice(slice_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    slice_.descriptor_set_id,
                    slice_.generation);
            }

            out_slice_ = descriptor_slots[slice_.descriptor_set_id].slice;
            return makeResult(NativeStatusCode::Ok, slice_.descriptor_set_id, slice_.generation);
        }
    }

    NativeResult resolveNativeDescriptorSet(
        const DescriptorSlice<Provider, Dim>& slice_,
        VkDescriptorSet& out_descriptor_set_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveDescriptorSlice(slice_)) {
                out_descriptor_set_ = VK_NULL_HANDLE;
                return makeResult(
                    NativeStatusCode::StaleReference,
                    slice_.descriptor_set_id,
                    slice_.generation);
            }

            out_descriptor_set_ = descriptor_slots[slice_.descriptor_set_id].descriptor_set;
            return makeResult(NativeStatusCode::Ok, slice_.descriptor_set_id, slice_.generation);
        }
    }

    NativeResult updateCombinedImageSampler(
        const DescriptorSlice<Provider, Dim>& slice_,
        U32 relative_index_,
        const ImageRef<Provider, Dim>& image_,
        const VulkanResourceRuntime<Provider, Dim>& resource_runtime_,
        VkSampler sampler_,
        VkImageLayout image_layout_) noexcept
    {
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        NativeResult result = resolveNativeDescriptorSet(slice_, descriptor_set);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }
        if (sampler_ == VK_NULL_HANDLE ||
            !isRelativeIndexValid(slice_, relative_index_) ||
            !isCombinedImageSamplerIndexValid(slice_.first + relative_index_)) {
            return makeResult(NativeStatusCode::InvalidInput, slice_.descriptor_set_id, slice_.generation);
        }

        VkImage native_image = VK_NULL_HANDLE;
        VkImageView native_image_view = VK_NULL_HANDLE;
        result = resource_runtime_.resolveNativeImage(image_, native_image, native_image_view);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }

        const VkDescriptorImageInfo image_info{
            .sampler = sampler_,
            .imageView = native_image_view,
            .imageLayout = image_layout_,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = descriptor_set,
            .dstBinding = kVulkanDescriptorCombinedImageSamplerBinding,
            .dstArrayElement = slice_.first + relative_index_,
            .descriptorCount = 1U,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };
        vkUpdateDescriptorSets(device, 1U, &write, 0U, nullptr);
        return makeResult(NativeStatusCode::Ok, slice_.descriptor_set_id, slice_.generation);
    }

    NativeResult updateStorageBuffer(
        const DescriptorSlice<Provider, Dim>& slice_,
        U32 relative_index_,
        const BufferRef<Provider, Dim>& buffer_,
        const VulkanResourceRuntime<Provider, Dim>& resource_runtime_,
        U64 offset_,
        U64 byte_count_) noexcept
    {
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        NativeResult result = resolveNativeDescriptorSet(slice_, descriptor_set);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }
        if (!isRelativeIndexValid(slice_, relative_index_) ||
            !isStorageBufferIndexValid(slice_.first + relative_index_) ||
            !isStorageBufferRangeValid(buffer_, offset_, byte_count_)) {
            return makeResult(NativeStatusCode::InvalidInput, slice_.descriptor_set_id, slice_.generation);
        }

        VkBuffer native_buffer = VK_NULL_HANDLE;
        result = resource_runtime_.resolveNativeBuffer(buffer_, native_buffer);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }

        const VkDescriptorBufferInfo buffer_info{
            .buffer = native_buffer,
            .offset = offset_,
            .range = byte_count_ == 0U ? VK_WHOLE_SIZE : byte_count_,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = descriptor_set,
            .dstBinding = kVulkanDescriptorStorageBufferBinding,
            .dstArrayElement = slice_.first + relative_index_,
            .descriptorCount = 1U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo = nullptr,
            .pBufferInfo = &buffer_info,
            .pTexelBufferView = nullptr,
        };
        vkUpdateDescriptorSets(device, 1U, &write, 0U, nullptr);
        return makeResult(NativeStatusCode::Ok, slice_.descriptor_set_id, slice_.generation);
    }

    NativeResult releaseDescriptorSlice(const DescriptorSlice<Provider, Dim>& slice_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveDescriptorSlice(slice_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    slice_.descriptor_set_id,
                    slice_.generation);
            }

            const NativeStatusCode release_code = pushFreeDescriptorId(slice_.descriptor_set_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, slice_.descriptor_set_id, slice_.generation);
            }

            auto& slot = descriptor_slots[slice_.descriptor_set_id];
            if (slot.descriptor_set != VK_NULL_HANDLE) {
                const VkResult vk_result = vkFreeDescriptorSets(
                    device,
                    descriptor_pool,
                    1U,
                    &slot.descriptor_set);
                last_vulkan_result = vk_result;
                if (vk_result != VK_SUCCESS) {
                    return makeResult(mapVulkanResult(vk_result), slice_.descriptor_set_id, slice_.generation);
                }
            }

            slot.descriptor_set = VK_NULL_HANDLE;
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.slice = {};
            --active_descriptor_count;
            return makeResult(NativeStatusCode::Ok, slice_.descriptor_set_id, slot.generation.value);
        }
    }

    bool isInitialized() const noexcept
    {
        return device != VK_NULL_HANDLE &&
            descriptor_pool != VK_NULL_HANDLE &&
            descriptor_set_layout != VK_NULL_HANDLE;
    }

    VkDescriptorPool nativeDescriptorPool() const noexcept
    {
        return descriptor_pool;
    }

    VkDescriptorSetLayout nativeDescriptorSetLayout() const noexcept
    {
        return descriptor_set_layout;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

    U32 descriptorCount() const noexcept
    {
        return active_descriptor_count;
    }

    U32 descriptorCapacity() const noexcept
    {
        return static_cast<U32>(descriptor_slots.capacity());
    }

    U32 descriptorArrayCapacity() const noexcept
    {
        return descriptor_array_capacity;
    }

private:
    struct DescriptorSlot {
        DescriptorSlice<Provider, Dim> slice;
        NativeGeneration generation;
        VkDescriptorSet descriptor_set;
        U32 occupied;
    };

    static constexpr U32 kFirstGeneration = 1U;

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

    static U32 nextGeneration(U32 generation_) noexcept
    {
        return generation_ == 0xFFFFFFFFU ? kFirstGeneration : generation_ + 1U;
    }

    static bool hasDescriptorCountOverflow(U32 count_, U32 set_count_) noexcept
    {
        return count_ != 0U && set_count_ > 0xFFFFFFFFU / count_;
    }

    NativeStatusCode createDescriptorPool() noexcept
    {
        std::array<VkDescriptorPoolSize, 2U> pool_sizes{};
        U32 pool_size_count = 0U;
        if (combined_image_sampler_count != 0U) {
            pool_sizes[pool_size_count] = {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = combined_image_sampler_count * max_descriptor_sets,
            };
            ++pool_size_count;
        }
        if (storage_buffer_count != 0U) {
            pool_sizes[pool_size_count] = {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = storage_buffer_count * max_descriptor_sets,
            };
            ++pool_size_count;
        }

        const VkDescriptorPoolCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = static_cast<VkDescriptorPoolCreateFlags>(descriptor_pool_flags),
            .maxSets = max_descriptor_sets,
            .poolSizeCount = pool_size_count,
            .pPoolSizes = pool_sizes.data(),
        };
        const VkResult vk_result = vkCreateDescriptorPool(device, &create_info, nullptr, &descriptor_pool);
        last_vulkan_result = vk_result;
        return mapVulkanResult(vk_result);
    }

    NativeStatusCode createDescriptorSetLayout() noexcept
    {
        std::array<VkDescriptorSetLayoutBinding, 2U> bindings{};
        std::array<VkDescriptorBindingFlags, 2U> binding_flags{};
        U32 binding_count = 0U;
        if (combined_image_sampler_count != 0U) {
            bindings[binding_count] = {
                .binding = kVulkanDescriptorCombinedImageSamplerBinding,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = combined_image_sampler_count,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            };
            binding_flags[binding_count] =
                static_cast<VkDescriptorBindingFlags>(combined_image_sampler_binding_flags);
            ++binding_count;
        }
        if (storage_buffer_count != 0U) {
            bindings[binding_count] = {
                .binding = kVulkanDescriptorStorageBufferBinding,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = storage_buffer_count,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            };
            binding_flags[binding_count] =
                static_cast<VkDescriptorBindingFlags>(storage_buffer_binding_flags);
            ++binding_count;
        }

        const VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .pNext = nullptr,
            .bindingCount = binding_count,
            .pBindingFlags = binding_flags.data(),
        };
        const bool has_binding_flags =
            combined_image_sampler_binding_flags != 0U ||
            storage_buffer_binding_flags != 0U;
        const VkDescriptorSetLayoutCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = has_binding_flags ? &binding_flags_info : nullptr,
            .flags = static_cast<VkDescriptorSetLayoutCreateFlags>(descriptor_set_layout_flags),
            .bindingCount = binding_count,
            .pBindings = bindings.data(),
        };
        const VkResult vk_result = vkCreateDescriptorSetLayout(
            device,
            &create_info,
            nullptr,
            &descriptor_set_layout);
        last_vulkan_result = vk_result;
        return mapVulkanResult(vk_result);
    }

    bool isDescriptorRangeValid(U32 first_, U32 count_) const noexcept
    {
        return count_ <= descriptor_array_capacity &&
            first_ <= descriptor_array_capacity - count_;
    }

    bool isRelativeIndexValid(const DescriptorSlice<Provider, Dim>& slice_, U32 relative_index_) const noexcept
    {
        return relative_index_ < slice_.count;
    }

    bool isCombinedImageSamplerIndexValid(U32 index_) const noexcept
    {
        return index_ < combined_image_sampler_count;
    }

    bool isStorageBufferIndexValid(U32 index_) const noexcept
    {
        return index_ < storage_buffer_count;
    }

    static bool isStorageBufferRangeValid(
        const BufferRef<Provider, Dim>& buffer_,
        U64 offset_,
        U64 byte_count_) noexcept
    {
        if (offset_ > buffer_.byte_size) {
            return false;
        }
        if (byte_count_ == 0U) {
            return true;
        }
        return byte_count_ <= buffer_.byte_size && offset_ <= buffer_.byte_size - byte_count_;
    }

    bool hasAvailableSlot() const noexcept
    {
        return !free_descriptor_ids.empty() ||
            (descriptor_slots.size() < descriptor_slots.capacity() &&
                descriptor_slots.size() < max_descriptor_sets);
    }

    bool acquireDescriptorSlot(U32& out_descriptor_id_)
    {
        if (!free_descriptor_ids.empty()) {
            out_descriptor_id_ = free_descriptor_ids.back();
            free_descriptor_ids.pop_back();
            return true;
        }
        if (descriptor_slots.size() >= descriptor_slots.capacity() ||
            descriptor_slots.size() >= max_descriptor_sets) {
            return false;
        }

        try {
            out_descriptor_id_ = static_cast<U32>(descriptor_slots.size());
            descriptor_slots.push_back({
                .slice = {},
                .generation = {.value = kFirstGeneration},
                .descriptor_set = VK_NULL_HANDLE,
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }
        return true;
    }

    NativeStatusCode pushFreeDescriptorId(U32 descriptor_id_)
    {
        if (free_descriptor_ids.size() >= free_descriptor_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }
        try {
            free_descriptor_ids.push_back(descriptor_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }
        return NativeStatusCode::Ok;
    }

    bool isLiveDescriptorSlice(const DescriptorSlice<Provider, Dim>& slice_) const noexcept
    {
        if (slice_.descriptor_set_id >= descriptor_slots.size()) {
            return false;
        }

        const auto& slot = descriptor_slots[slice_.descriptor_set_id];
        return slot.occupied != 0U && slot.generation.value == slice_.generation;
    }

    McVector<DescriptorSlot> descriptor_slots;
    McVector<U32> free_descriptor_ids;
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 max_descriptor_sets = 0U;
    U32 combined_image_sampler_count = 0U;
    U32 storage_buffer_count = 0U;
    U32 descriptor_array_capacity = 0U;
    U32 descriptor_pool_flags = 0U;
    U32 descriptor_set_layout_flags = 0U;
    U32 combined_image_sampler_binding_flags = 0U;
    U32 storage_buffer_binding_flags = 0U;
    U32 active_descriptor_count = 0U;
};

static_assert(std::is_trivial_v<VulkanDescriptorRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanDescriptorRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanDescriptorRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanDescriptorRuntimeConfig>);

} // namespace Render2D
