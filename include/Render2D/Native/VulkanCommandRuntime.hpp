#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vulkan/vulkan.h>

#include <type_traits>

namespace Render2D {

struct VulkanCommandRuntimeConfig {
    VkDevice device;
    U32 queue_family_index;
    U32 command_pool_flags;
};

template<class Provider, class Dim>
class VulkanCommandRuntime {
public:
    VulkanCommandRuntime() = default;
    VulkanCommandRuntime(const VulkanCommandRuntime&) = delete;
    VulkanCommandRuntime& operator=(const VulkanCommandRuntime&) = delete;
    VulkanCommandRuntime(VulkanCommandRuntime&&) = delete;
    VulkanCommandRuntime& operator=(VulkanCommandRuntime&&) = delete;

    ~VulkanCommandRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanCommandRuntimeConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (config_.device == VK_NULL_HANDLE || command_pool != VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

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
                command_pool = VK_NULL_HANDLE;
                return makeResult(mapVulkanResult(vk_result), 0U, 0U);
            }

            device = config_.device;
            queue_family_index = config_.queue_family_index;
            command_pool_flags = config_.command_pool_flags;
            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    void shutdown() noexcept
    {
        if (device != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, command_pool, nullptr);
        }

        command_buffer_slots.clear();
        free_command_buffer_ids.clear();
        active_command_buffer_count = 0U;
        command_pool = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        queue_family_index = 0U;
        command_pool_flags = 0U;
        last_vulkan_result = VK_SUCCESS;
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
        U32 frame_index_,
        RangeU32 batch_range_,
        RangeU32 upload_range_,
        U32 flags_,
        NativeCommandBufferRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized()) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }
            if (!hasAvailableSlot()) {
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            VkCommandBufferAllocateInfo allocate_info{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .pNext = nullptr,
                .commandPool = command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1U,
            };
            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            const VkResult vk_result = vkAllocateCommandBuffers(device, &allocate_info, &command_buffer);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), 0U, 0U);
            }

            U32 command_buffer_id = 0U;
            if (!acquireCommandBufferSlot(command_buffer_id)) {
                vkFreeCommandBuffers(device, command_pool, 1U, &command_buffer);
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            auto& slot = command_buffer_slots[command_buffer_id];
            slot.command_buffer = command_buffer;
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
            ++active_command_buffer_count;
            out_ref_ = slot.ref;

            return makeResult(
                NativeStatusCode::Ok,
                command_buffer_id,
                slot.generation.value);
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
            return makeResult(
                NativeStatusCode::Ok,
                ref_.command_buffer_id,
                ref_.generation);
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
            return makeResult(
                NativeStatusCode::Ok,
                ref_.command_buffer_id,
                ref_.generation);
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

    NativeResult resetCommandPool(U32 flags_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized()) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            const VkResult vk_result = vkResetCommandPool(
                device,
                command_pool,
                static_cast<VkCommandPoolResetFlags>(flags_));
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), 0U, 0U);
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
            if (slot.recording != 0U) {
                return makeResult(NativeStatusCode::InvalidInput, ref_.command_buffer_id, ref_.generation);
            }

            const NativeStatusCode release_code = pushFreeCommandBufferId(ref_.command_buffer_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, ref_.command_buffer_id, ref_.generation);
            }

            vkFreeCommandBuffers(device, command_pool, 1U, &slot.command_buffer);
            slot.command_buffer = VK_NULL_HANDLE;
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.ref = {};
            --active_command_buffer_count;

            return makeResult(
                NativeStatusCode::Ok,
                ref_.command_buffer_id,
                slot.generation.value);
        }
    }

    bool isInitialized() const noexcept
    {
        return device != VK_NULL_HANDLE && command_pool != VK_NULL_HANDLE;
    }

    VkCommandPool nativeCommandPool() const noexcept
    {
        return command_pool;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

    U32 commandBufferCount() const noexcept
    {
        return active_command_buffer_count;
    }

    U32 commandBufferCapacity() const noexcept
    {
        return static_cast<U32>(command_buffer_slots.capacity());
    }

private:
    struct CommandBufferSlot {
        NativeCommandBufferRef<Provider, Dim> ref;
        NativeGeneration generation;
        VkCommandBuffer command_buffer;
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

    McVector<CommandBufferSlot> command_buffer_slots;
    McVector<U32> free_command_buffer_ids;
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 queue_family_index = 0U;
    U32 command_pool_flags = 0U;
    U32 active_command_buffer_count = 0U;
};

static_assert(std::is_trivial_v<VulkanCommandRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanCommandRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanCommandRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanCommandRuntimeConfig>);

} // namespace Render2D
