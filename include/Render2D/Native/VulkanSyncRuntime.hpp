#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vulkan/vulkan.h>

#include <type_traits>

namespace Render2D {

struct VulkanSyncRuntimeConfig {
    VkDevice device;
    U32 fence_create_flags;
};

template<class Provider, class Dim>
class VulkanSyncRuntime {
public:
    VulkanSyncRuntime() = default;
    VulkanSyncRuntime(const VulkanSyncRuntime&) = delete;
    VulkanSyncRuntime& operator=(const VulkanSyncRuntime&) = delete;
    VulkanSyncRuntime(VulkanSyncRuntime&&) = delete;
    VulkanSyncRuntime& operator=(VulkanSyncRuntime&&) = delete;

    ~VulkanSyncRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanSyncRuntimeConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (config_.device == VK_NULL_HANDLE || device != VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            device = config_.device;
            fence_create_flags = config_.fence_create_flags;
            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    void shutdown() noexcept
    {
        if (device != VK_NULL_HANDLE) {
            for (auto& slot : sync_slots) {
                destroySlot(slot);
            }
        }

        sync_slots.clear();
        free_sync_ids.clear();
        active_sync_count = 0U;
        device = VK_NULL_HANDLE;
        fence_create_flags = 0U;
        last_vulkan_result = VK_SUCCESS;
    }

    NativeCapacityResult reserveFrameSyncs(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                sync_slots.reserve(capacity_);
                free_sync_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(sync_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(sync_slots.capacity()),
            };
        }
    }

    NativeResult createFrameSync(
        U32 frame_index_,
        U32 flags_,
        FrameSync<Provider, Dim>& out_frame_sync_)
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

            VkSemaphore image_available = VK_NULL_HANDLE;
            VkSemaphore render_finished = VK_NULL_HANDLE;
            VkFence in_flight = VK_NULL_HANDLE;
            NativeStatusCode create_code = createNativeSyncObjects(
                image_available,
                render_finished,
                in_flight);
            if (create_code != NativeStatusCode::Ok) {
                destroyNativeSyncObjects(image_available, render_finished, in_flight);
                return makeResult(create_code, 0U, 0U);
            }

            U32 sync_id = 0U;
            if (!acquireSyncSlot(sync_id)) {
                destroyNativeSyncObjects(image_available, render_finished, in_flight);
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            auto& slot = sync_slots[sync_id];
            slot.image_available = image_available;
            slot.render_finished = render_finished;
            slot.in_flight = in_flight;
            slot.occupied = 1U;
            slot.frame_sync = {
                .frame_index = frame_index_,
                .image_available_semaphore_id = sync_id,
                .render_finished_semaphore_id = sync_id,
                .in_flight_fence_id = sync_id,
                .flags = flags_,
                .sync_id = sync_id,
                .generation = slot.generation.value,
            };
            ++active_sync_count;
            out_frame_sync_ = slot.frame_sync;

            return makeResult(NativeStatusCode::Ok, sync_id, slot.generation.value);
        }
    }

    NativeResult resolveFrameSync(
        const FrameSync<Provider, Dim>& frame_sync_,
        FrameSync<Provider, Dim>& out_frame_sync_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveFrameSync(frame_sync_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    frame_sync_.sync_id,
                    frame_sync_.generation);
            }

            out_frame_sync_ = sync_slots[frame_sync_.sync_id].frame_sync;
            return makeResult(NativeStatusCode::Ok, frame_sync_.sync_id, frame_sync_.generation);
        }
    }

    NativeResult resolveNativeSync(
        const FrameSync<Provider, Dim>& frame_sync_,
        VkSemaphore& out_image_available_,
        VkSemaphore& out_render_finished_,
        VkFence& out_in_flight_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveFrameSync(frame_sync_)) {
                out_image_available_ = VK_NULL_HANDLE;
                out_render_finished_ = VK_NULL_HANDLE;
                out_in_flight_ = VK_NULL_HANDLE;
                return makeResult(
                    NativeStatusCode::StaleReference,
                    frame_sync_.sync_id,
                    frame_sync_.generation);
            }

            const auto& slot = sync_slots[frame_sync_.sync_id];
            out_image_available_ = slot.image_available;
            out_render_finished_ = slot.render_finished;
            out_in_flight_ = slot.in_flight;
            return makeResult(NativeStatusCode::Ok, frame_sync_.sync_id, frame_sync_.generation);
        }
    }

    NativeResult waitFence(const FrameSync<Provider, Dim>& frame_sync_, U64 timeout_ns_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveFrameSync(frame_sync_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    frame_sync_.sync_id,
                    frame_sync_.generation);
            }

            const auto& slot = sync_slots[frame_sync_.sync_id];
            const VkResult vk_result = vkWaitForFences(device, 1U, &slot.in_flight, VK_TRUE, timeout_ns_);
            last_vulkan_result = vk_result;
            return makeResult(mapVulkanResult(vk_result), frame_sync_.sync_id, frame_sync_.generation);
        }
    }

    NativeResult resetFence(const FrameSync<Provider, Dim>& frame_sync_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveFrameSync(frame_sync_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    frame_sync_.sync_id,
                    frame_sync_.generation);
            }

            auto& slot = sync_slots[frame_sync_.sync_id];
            const VkResult vk_result = vkResetFences(device, 1U, &slot.in_flight);
            last_vulkan_result = vk_result;
            return makeResult(mapVulkanResult(vk_result), frame_sync_.sync_id, frame_sync_.generation);
        }
    }

    NativeResult releaseFrameSync(const FrameSync<Provider, Dim>& frame_sync_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveFrameSync(frame_sync_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    frame_sync_.sync_id,
                    frame_sync_.generation);
            }

            const NativeStatusCode release_code = pushFreeSyncId(frame_sync_.sync_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, frame_sync_.sync_id, frame_sync_.generation);
            }

            auto& slot = sync_slots[frame_sync_.sync_id];
            destroySlot(slot);
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.frame_sync = {};
            --active_sync_count;

            return makeResult(NativeStatusCode::Ok, frame_sync_.sync_id, slot.generation.value);
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

    U32 frameSyncCount() const noexcept
    {
        return active_sync_count;
    }

    U32 frameSyncCapacity() const noexcept
    {
        return static_cast<U32>(sync_slots.capacity());
    }

private:
    struct SyncSlot {
        FrameSync<Provider, Dim> frame_sync;
        NativeGeneration generation;
        VkSemaphore image_available;
        VkSemaphore render_finished;
        VkFence in_flight;
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
            .object_kind = NativeObjectKind::Frame,
            .object_id = {.value = object_id_},
            .generation = {.value = generation_},
        };
    }

    static NativeStatusCode mapVulkanResult(VkResult result_) noexcept
    {
        switch (result_) {
        case VK_SUCCESS:
            return NativeStatusCode::Ok;
        case VK_TIMEOUT:
            return NativeStatusCode::Timeout;
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
        return !free_sync_ids.empty() || sync_slots.size() < sync_slots.capacity();
    }

    NativeStatusCode createNativeSyncObjects(
        VkSemaphore& out_image_available_,
        VkSemaphore& out_render_finished_,
        VkFence& out_in_flight_) noexcept
    {
        const VkSemaphoreCreateInfo semaphore_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
        };
        VkResult vk_result = vkCreateSemaphore(device, &semaphore_info, nullptr, &out_image_available_);
        last_vulkan_result = vk_result;
        if (vk_result != VK_SUCCESS) {
            return mapVulkanResult(vk_result);
        }

        vk_result = vkCreateSemaphore(device, &semaphore_info, nullptr, &out_render_finished_);
        last_vulkan_result = vk_result;
        if (vk_result != VK_SUCCESS) {
            return mapVulkanResult(vk_result);
        }

        const VkFenceCreateInfo fence_info{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = static_cast<VkFenceCreateFlags>(fence_create_flags),
        };
        vk_result = vkCreateFence(device, &fence_info, nullptr, &out_in_flight_);
        last_vulkan_result = vk_result;
        return mapVulkanResult(vk_result);
    }

    void destroyNativeSyncObjects(
        VkSemaphore image_available_,
        VkSemaphore render_finished_,
        VkFence in_flight_) noexcept
    {
        if (image_available_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, image_available_, nullptr);
        }
        if (render_finished_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, render_finished_, nullptr);
        }
        if (in_flight_ != VK_NULL_HANDLE) {
            vkDestroyFence(device, in_flight_, nullptr);
        }
    }

    void destroySlot(SyncSlot& slot_) noexcept
    {
        destroyNativeSyncObjects(slot_.image_available, slot_.render_finished, slot_.in_flight);
        slot_.image_available = VK_NULL_HANDLE;
        slot_.render_finished = VK_NULL_HANDLE;
        slot_.in_flight = VK_NULL_HANDLE;
    }

    bool acquireSyncSlot(U32& out_sync_id_)
    {
        if (!free_sync_ids.empty()) {
            out_sync_id_ = free_sync_ids.back();
            free_sync_ids.pop_back();
            return true;
        }

        if (sync_slots.size() >= sync_slots.capacity()) {
            return false;
        }

        try {
            out_sync_id_ = static_cast<U32>(sync_slots.size());
            sync_slots.push_back({
                .frame_sync = {},
                .generation = {.value = kFirstGeneration},
                .image_available = VK_NULL_HANDLE,
                .render_finished = VK_NULL_HANDLE,
                .in_flight = VK_NULL_HANDLE,
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }

        return true;
    }

    NativeStatusCode pushFreeSyncId(U32 sync_id_)
    {
        if (free_sync_ids.size() >= free_sync_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }

        try {
            free_sync_ids.push_back(sync_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }

        return NativeStatusCode::Ok;
    }

    bool isLiveFrameSync(const FrameSync<Provider, Dim>& frame_sync_) const noexcept
    {
        if (frame_sync_.sync_id >= sync_slots.size()) {
            return false;
        }

        const auto& slot = sync_slots[frame_sync_.sync_id];
        return slot.occupied != 0U && slot.generation.value == frame_sync_.generation;
    }

    McVector<SyncSlot> sync_slots;
    McVector<U32> free_sync_ids;
    VkDevice device = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 fence_create_flags = 0U;
    U32 active_sync_count = 0U;
};

static_assert(std::is_trivial_v<VulkanSyncRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanSyncRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanSyncRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanSyncRuntimeConfig>);

} // namespace Render2D
