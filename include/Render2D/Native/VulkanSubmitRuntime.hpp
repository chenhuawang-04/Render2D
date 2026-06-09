#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/VulkanCommandRuntime.hpp"
#include "Render2D/Native/VulkanSyncRuntime.hpp"

#include <vulkan/vulkan.h>

#include <span>
#include <type_traits>

namespace Render2D {

struct VulkanSubmitRuntimeConfig {
    VkQueue queue;
    U32 wait_stage_flags;
};

inline constexpr U32 kVulkanSubmitWaitImageAvailable = 1U << 0U;
inline constexpr U32 kVulkanSubmitSignalRenderFinished = 1U << 1U;

template<class Provider, class Dim>
class VulkanSubmitRuntime {
public:
    NativeResult initialize(VulkanSubmitRuntimeConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            if (config_.queue == VK_NULL_HANDLE || queue != VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput);
            }

            queue = config_.queue;
            wait_stage_flags = config_.wait_stage_flags == 0U ?
                static_cast<U32>(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) :
                config_.wait_stage_flags;
            return makeResult(NativeStatusCode::Ok);
        }
    }

    void shutdown() noexcept
    {
        queue = VK_NULL_HANDLE;
        wait_stage_flags = 0U;
        last_vulkan_result = VK_SUCCESS;
        submit_command_buffers.clear();
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
                submit_command_buffers.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(submit_command_buffers.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(submit_command_buffers.capacity()),
            };
        }
    }

    NativeResult submit(
        std::span<const NativeCommandBufferRef<Provider, Dim>> command_buffer_refs_,
        const FrameSync<Provider, Dim>& frame_sync_,
        const VulkanCommandRuntime<Provider, Dim>& command_runtime_,
        const VulkanSyncRuntime<Provider, Dim>& sync_runtime_,
        U32 flags_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            if (queue == VK_NULL_HANDLE || command_buffer_refs_.empty()) {
                return makeResult(NativeStatusCode::InvalidInput);
            }
            if (submit_command_buffers.capacity() < command_buffer_refs_.size()) {
                return makeResult(NativeStatusCode::OutOfCapacity);
            }

            submit_command_buffers.clear();
            for (const auto& command_buffer_ref : command_buffer_refs_) {
                VkCommandBuffer command_buffer = VK_NULL_HANDLE;
                const NativeResult resolve_result =
                    command_runtime_.resolveNativeCommandBuffer(command_buffer_ref, command_buffer);
                if (resolve_result.code != NativeStatusCode::Ok) {
                    submit_command_buffers.clear();
                    return resolve_result;
                }
                submit_command_buffers.push_back(command_buffer);
            }

            VkSemaphore image_available = VK_NULL_HANDLE;
            VkSemaphore render_finished = VK_NULL_HANDLE;
            VkFence in_flight = VK_NULL_HANDLE;
            NativeResult sync_result = sync_runtime_.resolveNativeSync(
                frame_sync_,
                image_available,
                render_finished,
                in_flight);
            if (sync_result.code != NativeStatusCode::Ok) {
                submit_command_buffers.clear();
                return sync_result;
            }

            const auto wait_stage =
                static_cast<VkPipelineStageFlags>(wait_stage_flags);
            const VkSemaphore wait_semaphore =
                (flags_ & kVulkanSubmitWaitImageAvailable) != 0U ? image_available : VK_NULL_HANDLE;
            const VkSemaphore signal_semaphore =
                (flags_ & kVulkanSubmitSignalRenderFinished) != 0U ? render_finished : VK_NULL_HANDLE;

            const VkSubmitInfo submit_info{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreCount = wait_semaphore == VK_NULL_HANDLE ? 0U : 1U,
                .pWaitSemaphores = wait_semaphore == VK_NULL_HANDLE ? nullptr : &wait_semaphore,
                .pWaitDstStageMask = wait_semaphore == VK_NULL_HANDLE ? nullptr : &wait_stage,
                .commandBufferCount = static_cast<U32>(submit_command_buffers.size()),
                .pCommandBuffers = submit_command_buffers.data(),
                .signalSemaphoreCount = signal_semaphore == VK_NULL_HANDLE ? 0U : 1U,
                .pSignalSemaphores = signal_semaphore == VK_NULL_HANDLE ? nullptr : &signal_semaphore,
            };

            const VkResult vk_result = vkQueueSubmit(queue, 1U, &submit_info, in_flight);
            last_vulkan_result = vk_result;
            submit_command_buffers.clear();
            return makeResult(mapVulkanResult(vk_result));
        }
    }

    bool isInitialized() const noexcept
    {
        return queue != VK_NULL_HANDLE;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

    U32 commandBufferCapacity() const noexcept
    {
        return static_cast<U32>(submit_command_buffers.capacity());
    }

private:
    static NativeResult makeResult(NativeStatusCode code_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Queue,
            .object_id = {.value = 0U},
            .generation = {.value = 0U},
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

    McVector<VkCommandBuffer> submit_command_buffers;
    VkQueue queue = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 wait_stage_flags = 0U;
};

static_assert(std::is_trivial_v<VulkanSubmitRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanSubmitRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanSubmitRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanSubmitRuntimeConfig>);

} // namespace Render2D
