#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vulkan/vulkan.h>

#include <type_traits>

namespace Render2D {

struct VulkanThreadCommandRuntimeConfig {
    VkDevice device;
    U32 queue_family_index;
    U32 command_pool_flags;
    U32 thread_count;
};

template<class Provider, class Dim>
class VulkanThreadCommandRuntime {
public:
    VulkanThreadCommandRuntime() = default;
    VulkanThreadCommandRuntime(const VulkanThreadCommandRuntime&) = delete;
    VulkanThreadCommandRuntime& operator=(const VulkanThreadCommandRuntime&) = delete;
    VulkanThreadCommandRuntime(VulkanThreadCommandRuntime&&) = delete;
    VulkanThreadCommandRuntime& operator=(VulkanThreadCommandRuntime&&) = delete;

    ~VulkanThreadCommandRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanThreadCommandRuntimeConfig config_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (config_.device == VK_NULL_HANDLE ||
                config_.thread_count == 0U ||
                isInitialized()) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            device = config_.device;
            queue_family_index = config_.queue_family_index;
            command_pool_flags = config_.command_pool_flags;

            try {
                thread_pools.reserve(config_.thread_count);
            } catch (...) {
                shutdown();
                return makeResult(NativeStatusCode::OutOfMemory, 0U, 0U);
            }

            for (U32 thread_index = 0U; thread_index < config_.thread_count; ++thread_index) {
                VkCommandPool command_pool = VK_NULL_HANDLE;
                VkCommandPoolCreateInfo create_info{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = static_cast<VkCommandPoolCreateFlags>(config_.command_pool_flags),
                    .queueFamilyIndex = config_.queue_family_index,
                };
                const VkResult vk_result = vkCreateCommandPool(
                    config_.device,
                    &create_info,
                    nullptr,
                    &command_pool);
                last_vulkan_result = vk_result;
                if (vk_result != VK_SUCCESS) {
                    shutdown();
                    return makeResult(mapVulkanResult(vk_result), thread_index, 0U);
                }

                try {
                    thread_pools.push_back({
                        .command_pool = command_pool,
                        .active_command_buffer_count = 0U,
                    });
                } catch (...) {
                    vkDestroyCommandPool(config_.device, command_pool, nullptr);
                    shutdown();
                    return makeResult(NativeStatusCode::OutOfMemory, thread_index, 0U);
                }
            }

            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    void shutdown() noexcept
    {
        if (device != VK_NULL_HANDLE) {
            for (auto& pool : thread_pools) {
                if (pool.command_pool != VK_NULL_HANDLE) {
                    vkDestroyCommandPool(device, pool.command_pool, nullptr);
                }
                pool.command_pool = VK_NULL_HANDLE;
                pool.active_command_buffer_count = 0U;
            }
        }

        command_buffer_slots.clear();
        free_command_buffer_ids.clear();
        thread_pools.clear();
        device = VK_NULL_HANDLE;
        last_vulkan_result = VK_SUCCESS;
        queue_family_index = 0U;
        command_pool_flags = 0U;
        active_command_buffer_count = 0U;
    }

    NativeCapacityResult reserveCommandBuffers(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                command_buffer_slots.reserve(capacity_);
                free_command_buffer_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(command_buffer_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(command_buffer_slots.capacity()),
            };
        }
    }

    NativeResult allocateCommandBufferRef(
        U32 thread_index_,
        U32 frame_index_,
        RangeU32 batch_range_,
        RangeU32 upload_range_,
        U32 flags_,
        NativeCommandBufferRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized() || thread_index_ >= thread_pools.size()) {
                return makeResult(NativeStatusCode::InvalidInput, thread_index_, 0U);
            }
            if (!hasAvailableSlot()) {
                return makeResult(NativeStatusCode::OutOfCapacity, thread_index_, 0U);
            }

            VkCommandBufferAllocateInfo allocate_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .pNext = nullptr,
                .commandPool = thread_pools[thread_index_].command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1U,
            };
            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            const VkResult vk_result = vkAllocateCommandBuffers(device, &allocate_info, &command_buffer);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), thread_index_, 0U);
            }

            U32 command_buffer_id = 0U;
            if (!acquireCommandBufferSlot(command_buffer_id)) {
                vkFreeCommandBuffers(device, thread_pools[thread_index_].command_pool, 1U, &command_buffer);
                return makeResult(NativeStatusCode::OutOfCapacity, thread_index_, 0U);
            }

            auto& slot = command_buffer_slots[command_buffer_id];
            slot.command_buffer = command_buffer;
            slot.thread_index = thread_index_;
            slot.occupied = 1U;
            slot.recording = 0U;
            slot.ref = {
                .command_buffer_id = command_buffer_id,
                .generation = slot.generation.value,
                .frame_index = frame_index_,
                .batch_first = batch_range_.first,
                .batch_count = batch_range_.count,
                .upload_first = upload_range_.first,
                .upload_count = upload_range_.count,
                .flags = flags_,
            };
            ++thread_pools[thread_index_].active_command_buffer_count;
            ++active_command_buffer_count;
            out_ref_ = slot.ref;

            return makeResult(NativeStatusCode::Ok, command_buffer_id, slot.generation.value);
        }
    }

    NativeResult resolveCommandBufferRef(
        const NativeCommandBufferRef<Provider, Dim>& ref_,
        NativeCommandBufferRef<Provider, Dim>& out_ref_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveCommandBufferRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    ref_.command_buffer_id,
                    ref_.generation);
            }

            out_ref_ = command_buffer_slots[ref_.command_buffer_id].ref;
            return makeResult(NativeStatusCode::Ok, ref_.command_buffer_id, ref_.generation);
        }
    }

    NativeResult resolveNativeCommandBuffer(
        const NativeCommandBufferRef<Provider, Dim>& ref_,
        VkCommandBuffer& out_command_buffer_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveCommandBufferRef(ref_)) {
                out_command_buffer_ = VK_NULL_HANDLE;
                return makeResult(
                    NativeStatusCode::StaleReference,
                    ref_.command_buffer_id,
                    ref_.generation);
            }

            out_command_buffer_ = command_buffer_slots[ref_.command_buffer_id].command_buffer;
            return makeResult(NativeStatusCode::Ok, ref_.command_buffer_id, ref_.generation);
        }
    }

    NativeResult beginCommandBuffer(
        const NativeCommandBufferRef<Provider, Dim>& ref_,
        U32 usage_flags_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveCommandBufferRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    ref_.command_buffer_id,
                    ref_.generation);
            }

            auto& slot = command_buffer_slots[ref_.command_buffer_id];
            if (slot.recording != 0U) {
                return makeResult(NativeStatusCode::InvalidInput, ref_.command_buffer_id, ref_.generation);
            }

            const VkCommandBufferBeginInfo begin_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = nullptr,
                .flags = static_cast<VkCommandBufferUsageFlags>(usage_flags_),
                .pInheritanceInfo = nullptr,
            };
            const VkResult vk_result = vkBeginCommandBuffer(slot.command_buffer, &begin_info);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), ref_.command_buffer_id, ref_.generation);
            }

            slot.recording = 1U;
            return makeResult(NativeStatusCode::Ok, ref_.command_buffer_id, ref_.generation);
        }
    }

    NativeResult endCommandBuffer(const NativeCommandBufferRef<Provider, Dim>& ref_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveCommandBufferRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    ref_.command_buffer_id,
                    ref_.generation);
            }

            auto& slot = command_buffer_slots[ref_.command_buffer_id];
            if (slot.recording == 0U) {
                return makeResult(NativeStatusCode::InvalidInput, ref_.command_buffer_id, ref_.generation);
            }

            const VkResult vk_result = vkEndCommandBuffer(slot.command_buffer);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), ref_.command_buffer_id, ref_.generation);
            }

            slot.recording = 0U;
            return makeResult(NativeStatusCode::Ok, ref_.command_buffer_id, ref_.generation);
        }
    }

    NativeResult resetCommandBuffer(
        const NativeCommandBufferRef<Provider, Dim>& ref_,
        U32 flags_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveCommandBufferRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    ref_.command_buffer_id,
                    ref_.generation);
            }

            auto& slot = command_buffer_slots[ref_.command_buffer_id];
            const VkResult vk_result = vkResetCommandBuffer(
                slot.command_buffer,
                static_cast<VkCommandBufferResetFlags>(flags_));
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), ref_.command_buffer_id, ref_.generation);
            }

            slot.recording = 0U;
            return makeResult(NativeStatusCode::Ok, ref_.command_buffer_id, ref_.generation);
        }
    }

    NativeResult resetThreadCommandPool(U32 thread_index_, U32 flags_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized() || thread_index_ >= thread_pools.size()) {
                return makeResult(NativeStatusCode::InvalidInput, thread_index_, 0U);
            }

            const VkResult vk_result = vkResetCommandPool(
                device,
                thread_pools[thread_index_].command_pool,
                static_cast<VkCommandPoolResetFlags>(flags_));
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), thread_index_, 0U);
            }

            for (auto& slot : command_buffer_slots) {
                if (slot.occupied != 0U && slot.thread_index == thread_index_) {
                    slot.recording = 0U;
                }
            }
            return makeResult(NativeStatusCode::Ok, thread_index_, 0U);
        }
    }

    NativeResult resetAllCommandPools(U32 flags_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized()) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            for (Usize thread_index = 0U; thread_index < thread_pools.size(); ++thread_index) {
                const VkResult vk_result = vkResetCommandPool(
                    device,
                    thread_pools[thread_index].command_pool,
                    static_cast<VkCommandPoolResetFlags>(flags_));
                last_vulkan_result = vk_result;
                if (vk_result != VK_SUCCESS) {
                    return makeResult(mapVulkanResult(vk_result), static_cast<U32>(thread_index), 0U);
                }
            }

            for (auto& slot : command_buffer_slots) {
                slot.recording = 0U;
            }
            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    NativeResult releaseCommandBufferRef(const NativeCommandBufferRef<Provider, Dim>& ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveCommandBufferRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    ref_.command_buffer_id,
                    ref_.generation);
            }

            auto& slot = command_buffer_slots[ref_.command_buffer_id];
            if (slot.recording != 0U || slot.thread_index >= thread_pools.size()) {
                return makeResult(NativeStatusCode::InvalidInput, ref_.command_buffer_id, ref_.generation);
            }

            const NativeStatusCode release_code = pushFreeCommandBufferId(ref_.command_buffer_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, ref_.command_buffer_id, ref_.generation);
            }

            auto& pool = thread_pools[slot.thread_index];
            vkFreeCommandBuffers(device, pool.command_pool, 1U, &slot.command_buffer);
            if (pool.active_command_buffer_count != 0U) {
                --pool.active_command_buffer_count;
            }
            slot.command_buffer = VK_NULL_HANDLE;
            slot.thread_index = 0U;
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.ref = {};
            --active_command_buffer_count;

            return makeResult(NativeStatusCode::Ok, ref_.command_buffer_id, slot.generation.value);
        }
    }

    bool isInitialized() const noexcept
    {
        return device != VK_NULL_HANDLE && !thread_pools.empty();
    }

    VkCommandPool nativeCommandPool(U32 thread_index_) const noexcept
    {
        if (thread_index_ >= thread_pools.size()) {
            return VK_NULL_HANDLE;
        }
        return thread_pools[thread_index_].command_pool;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

    U32 threadCount() const noexcept
    {
        return static_cast<U32>(thread_pools.size());
    }

    U32 commandBufferCount() const noexcept
    {
        return active_command_buffer_count;
    }

    U32 commandBufferCapacity() const noexcept
    {
        return static_cast<U32>(command_buffer_slots.capacity());
    }

    U32 threadCommandBufferCount(U32 thread_index_) const noexcept
    {
        if (thread_index_ >= thread_pools.size()) {
            return 0U;
        }
        return thread_pools[thread_index_].active_command_buffer_count;
    }

private:
    struct ThreadPoolSlot {
        VkCommandPool command_pool;
        U32 active_command_buffer_count;
    };

    struct CommandBufferSlot {
        NativeCommandBufferRef<Provider, Dim> ref;
        NativeGeneration generation;
        VkCommandBuffer command_buffer;
        U32 thread_index;
        U32 occupied;
        U32 recording;
    };

    static constexpr U32 kFirstGeneration = 1U;

    static NativeResult makeResult(
        NativeStatusCode code_,
        U32 object_id_,
        U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::CommandBuffer,
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

    bool hasAvailableSlot() const noexcept
    {
        return !free_command_buffer_ids.empty() ||
            command_buffer_slots.size() < command_buffer_slots.capacity();
    }

    bool acquireCommandBufferSlot(U32& out_command_buffer_id_)
    {
        if (!free_command_buffer_ids.empty()) {
            out_command_buffer_id_ = free_command_buffer_ids.back();
            free_command_buffer_ids.pop_back();
            return true;
        }

        if (command_buffer_slots.size() >= command_buffer_slots.capacity()) {
            return false;
        }

        try {
            out_command_buffer_id_ = static_cast<U32>(command_buffer_slots.size());
            command_buffer_slots.push_back({
                .ref = {},
                .generation = {.value = kFirstGeneration},
                .command_buffer = VK_NULL_HANDLE,
                .thread_index = 0U,
                .occupied = 0U,
                .recording = 0U,
            });
        } catch (...) {
            return false;
        }

        return true;
    }

    NativeStatusCode pushFreeCommandBufferId(U32 command_buffer_id_)
    {
        if (free_command_buffer_ids.size() >= free_command_buffer_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }

        try {
            free_command_buffer_ids.push_back(command_buffer_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }

        return NativeStatusCode::Ok;
    }

    bool isLiveCommandBufferRef(const NativeCommandBufferRef<Provider, Dim>& ref_) const noexcept
    {
        if (ref_.command_buffer_id >= command_buffer_slots.size()) {
            return false;
        }

        const auto& slot = command_buffer_slots[ref_.command_buffer_id];
        return slot.occupied != 0U && slot.generation.value == ref_.generation;
    }

    McVector<ThreadPoolSlot> thread_pools;
    McVector<CommandBufferSlot> command_buffer_slots;
    McVector<U32> free_command_buffer_ids;
    VkDevice device = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 queue_family_index = 0U;
    U32 command_pool_flags = 0U;
    U32 active_command_buffer_count = 0U;
};

static_assert(std::is_trivial_v<VulkanThreadCommandRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanThreadCommandRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanThreadCommandRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanThreadCommandRuntimeConfig>);

} // namespace Render2D
