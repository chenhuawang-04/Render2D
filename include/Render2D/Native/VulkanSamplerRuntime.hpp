#pragma once

#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <type_traits>

namespace Render2D {

struct VulkanSamplerRuntimeConfig {
    VkDevice device;
};

struct VulkanSamplerConfig {
    U32 mag_filter;
    U32 min_filter;
    U32 mipmap_mode;
    U32 address_mode_u;
    U32 address_mode_v;
    U32 address_mode_w;
    U32 flags;
};

template<class Provider, class Dim>
class VulkanSamplerRuntime {
public:
    VulkanSamplerRuntime() = default;
    VulkanSamplerRuntime(const VulkanSamplerRuntime&) = delete;
    VulkanSamplerRuntime& operator=(const VulkanSamplerRuntime&) = delete;
    VulkanSamplerRuntime(VulkanSamplerRuntime&&) = delete;
    VulkanSamplerRuntime& operator=(VulkanSamplerRuntime&&) = delete;

    ~VulkanSamplerRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanSamplerRuntimeConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (config_.device == VK_NULL_HANDLE || device != VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            device = config_.device;
            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    void shutdown() noexcept
    {
        if (device != VK_NULL_HANDLE) {
            for (auto& slot : sampler_slots) {
                destroySamplerSlot(slot);
            }
        }

        sampler_slots.clear();
        free_sampler_ids.clear();
        active_sampler_count = 0U;
        device = VK_NULL_HANDLE;
        last_vulkan_result = VK_SUCCESS;
    }

    NativeCapacityResult reserveSamplers(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                sampler_slots.reserve(capacity_);
                free_sampler_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(sampler_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(sampler_slots.capacity()),
            };
        }
    }

    NativeResult createSamplerRef(
        const VulkanSamplerConfig& config_,
        SamplerRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized()) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }
            if (!hasAvailableSamplerSlot()) {
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            const VkSamplerCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = nullptr,
                .flags = static_cast<VkSamplerCreateFlags>(config_.flags),
                .magFilter = static_cast<VkFilter>(config_.mag_filter),
                .minFilter = static_cast<VkFilter>(config_.min_filter),
                .mipmapMode = static_cast<VkSamplerMipmapMode>(config_.mipmap_mode),
                .addressModeU = static_cast<VkSamplerAddressMode>(config_.address_mode_u),
                .addressModeV = static_cast<VkSamplerAddressMode>(config_.address_mode_v),
                .addressModeW = static_cast<VkSamplerAddressMode>(config_.address_mode_w),
                .mipLodBias = 0.0F,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1.0F,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .minLod = 0.0F,
                .maxLod = 0.0F,
                .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                .unnormalizedCoordinates = VK_FALSE,
            };

            VkSampler sampler = VK_NULL_HANDLE;
            const VkResult vk_result = vkCreateSampler(device, &create_info, nullptr, &sampler);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), 0U, 0U);
            }

            U32 sampler_id = 0U;
            if (!acquireSamplerSlot(sampler_id)) {
                vkDestroySampler(device, sampler, nullptr);
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            auto& slot = sampler_slots[sampler_id];
            slot.sampler = sampler;
            slot.occupied = 1U;
            slot.ref = {
                .sampler_handle = nativeHandleToU64(sampler),
                .sampler_id = sampler_id,
                .generation = slot.generation.value,
                .flags = config_.flags,
            };
            ++active_sampler_count;
            out_ref_ = slot.ref;
            return makeResult(NativeStatusCode::Ok, sampler_id, slot.generation.value);
        }
    }

    NativeResult resolveNativeSampler(
        const SamplerRef<Provider, Dim>& ref_,
        VkSampler& out_sampler_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            out_sampler_ = VK_NULL_HANDLE;
            return makeResult(NativeStatusCode::UnsupportedDomain, ref_.sampler_id, ref_.generation);
        } else {
            if (!isLiveSamplerRef(ref_)) {
                out_sampler_ = VK_NULL_HANDLE;
                return makeResult(NativeStatusCode::StaleReference, ref_.sampler_id, ref_.generation);
            }

            out_sampler_ = sampler_slots[ref_.sampler_id].sampler;
            return makeResult(NativeStatusCode::Ok, ref_.sampler_id, ref_.generation);
        }
    }

    NativeResult releaseSamplerRef(const SamplerRef<Provider, Dim>& ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, ref_.sampler_id, ref_.generation);
        } else {
            if (!isLiveSamplerRef(ref_)) {
                return makeResult(NativeStatusCode::StaleReference, ref_.sampler_id, ref_.generation);
            }

            const NativeStatusCode release_code = pushFreeSamplerId(ref_.sampler_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, ref_.sampler_id, ref_.generation);
            }

            auto& slot = sampler_slots[ref_.sampler_id];
            destroySamplerSlot(slot);
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.ref = {};
            --active_sampler_count;
            return makeResult(NativeStatusCode::Ok, ref_.sampler_id, slot.generation.value);
        }
    }

    bool isInitialized() const noexcept
    {
        return device != VK_NULL_HANDLE;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

    U32 samplerCount() const noexcept
    {
        return active_sampler_count;
    }

    U32 samplerCapacity() const noexcept
    {
        return static_cast<U32>(sampler_slots.capacity());
    }

private:
    struct SamplerSlot {
        SamplerRef<Provider, Dim> ref;
        NativeGeneration generation;
        VkSampler sampler;
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
            .object_kind = NativeObjectKind::Sampler,
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

    template<class Handle>
    static U64 nativeHandleToU64(Handle handle_) noexcept
    {
        if constexpr (std::is_pointer_v<Handle>) {
            return static_cast<U64>(reinterpret_cast<std::uintptr_t>(handle_));
        } else {
            return static_cast<U64>(handle_);
        }
    }

    bool hasAvailableSamplerSlot() const noexcept
    {
        return !free_sampler_ids.empty() || sampler_slots.size() < sampler_slots.capacity();
    }

    bool acquireSamplerSlot(U32& out_sampler_id_)
    {
        if (!free_sampler_ids.empty()) {
            out_sampler_id_ = free_sampler_ids.back();
            free_sampler_ids.pop_back();
            return true;
        }
        if (sampler_slots.size() >= sampler_slots.capacity()) {
            return false;
        }

        try {
            out_sampler_id_ = static_cast<U32>(sampler_slots.size());
            sampler_slots.push_back({
                .ref = {},
                .generation = {.value = kFirstGeneration},
                .sampler = VK_NULL_HANDLE,
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }
        return true;
    }

    NativeStatusCode pushFreeSamplerId(U32 sampler_id_)
    {
        if (free_sampler_ids.size() >= free_sampler_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }
        try {
            free_sampler_ids.push_back(sampler_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }
        return NativeStatusCode::Ok;
    }

    bool isLiveSamplerRef(const SamplerRef<Provider, Dim>& ref_) const noexcept
    {
        if (ref_.sampler_id >= sampler_slots.size()) {
            return false;
        }

        const auto& slot = sampler_slots[ref_.sampler_id];
        return slot.occupied != 0U && slot.generation.value == ref_.generation;
    }

    void destroySamplerSlot(SamplerSlot& slot_) noexcept
    {
        if (slot_.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, slot_.sampler, nullptr);
        }
        slot_.sampler = VK_NULL_HANDLE;
    }

    McVector<SamplerSlot> sampler_slots;
    McVector<U32> free_sampler_ids;
    VkDevice device = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 active_sampler_count = 0U;
};

static_assert(std::is_trivial_v<VulkanSamplerRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanSamplerRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanSamplerRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanSamplerRuntimeConfig>);

static_assert(std::is_trivial_v<VulkanSamplerConfig>);
static_assert(std::is_standard_layout_v<VulkanSamplerConfig>);
static_assert(std::is_trivially_copyable_v<VulkanSamplerConfig>);
static_assert(std::is_aggregate_v<VulkanSamplerConfig>);

} // namespace Render2D
