#pragma once

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"
#include "Render2D/Native/VulkanSwapchainRuntime.hpp"
#include "Render2D/Native/VulkanSyncRuntime.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <type_traits>

namespace Render2D {

inline constexpr U32 kVulkanAcquireImageSuboptimal = 1U << 0U;
inline constexpr U32 kVulkanPresentSuboptimal = 1U << 0U;

struct VulkanPresentRuntimeConfig {
    VkDevice device;
    VkQueue present_queue;
};

template<class Provider, class Dim>
class VulkanPresentRuntime {
public:
    NativeResult initialize(VulkanPresentRuntimeConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            if (config_.device == VK_NULL_HANDLE ||
                config_.present_queue == VK_NULL_HANDLE ||
                device != VK_NULL_HANDLE ||
                present_queue != VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput);
            }

            device = config_.device;
            present_queue = config_.present_queue;
            return makeResult(NativeStatusCode::Ok);
        }
    }

    void shutdown() noexcept
    {
        device = VK_NULL_HANDLE;
        present_queue = VK_NULL_HANDLE;
        last_vulkan_result = VK_SUCCESS;
    }

    NativeResult acquireNextImage(
        const SwapchainState<Provider, Dim>& swapchain_state_,
        const FrameSync<Provider, Dim>& frame_sync_,
        const VulkanSwapchainRuntime<Provider, Dim>& swapchain_runtime_,
        const VulkanSyncRuntime<Provider, Dim>& sync_runtime_,
        U64 timeout_ns_,
        U32 flags_,
        AcquiredImage<Provider, Dim>& out_image_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            out_image_ = {};
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            out_image_ = {};
            if (!isInitialized()) {
                return makeResult(NativeStatusCode::InvalidInput);
            }

            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            NativeResult result = swapchain_runtime_.resolveNativeSwapchain(
                swapchain_state_,
                swapchain);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            VkSemaphore image_available = VK_NULL_HANDLE;
            VkSemaphore render_finished = VK_NULL_HANDLE;
            VkFence in_flight = VK_NULL_HANDLE;
            result = sync_runtime_.resolveNativeSync(
                frame_sync_,
                image_available,
                render_finished,
                in_flight);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            U32 image_index = 0U;
            const VkResult vk_result = vkAcquireNextImageKHR(
                device,
                swapchain,
                timeout_ns_,
                image_available,
                VK_NULL_HANDLE,
                &image_index);
            last_vulkan_result = vk_result;
            const NativeStatusCode code = mapVulkanResult(vk_result);
            if (code != NativeStatusCode::Ok) {
                return makeResult(code);
            }

            out_image_ = {
                .swapchain_id = swapchain_state_.swapchain_id,
                .image_index = image_index,
                .frame_index = frame_sync_.frame_index,
                .sync_id = frame_sync_.sync_id,
                .sync_generation = frame_sync_.generation,
                .generation = swapchain_state_.generation,
                .flags = vk_result == VK_SUBOPTIMAL_KHR ?
                    (flags_ | kVulkanAcquireImageSuboptimal) :
                    flags_,
            };
            return makeResult(NativeStatusCode::Ok);
        }
    }

    NativeResult present(
        const PresentCommand<Provider, Dim>& command_,
        const VulkanSwapchainRuntime<Provider, Dim>& swapchain_runtime_,
        const VulkanSyncRuntime<Provider, Dim>& sync_runtime_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            if (!isInitialized() || !isPresentCommandValid(command_)) {
                return makeResult(NativeStatusCode::InvalidInput);
            }

            const SwapchainState<Provider, Dim> state{
                .handle = 0U,
                .swapchain_id = command_.swapchain_id,
                .image_first = 0U,
                .image_count = 0U,
                .width = 0U,
                .height = 0U,
                .format = 0U,
                .generation = command_.generation,
                .flags = 0U,
            };
            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            NativeResult result = swapchain_runtime_.resolveNativeSwapchain(state, swapchain);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            const FrameSync<Provider, Dim> frame_sync{
                .frame_index = command_.frame_index,
                .image_available_semaphore_id = command_.wait_sync_id,
                .render_finished_semaphore_id = command_.wait_sync_id,
                .in_flight_fence_id = command_.wait_sync_id,
                .flags = 0U,
                .sync_id = command_.wait_sync_id,
                .generation = command_.wait_sync_generation,
            };
            VkSemaphore image_available = VK_NULL_HANDLE;
            VkSemaphore render_finished = VK_NULL_HANDLE;
            VkFence in_flight = VK_NULL_HANDLE;
            result = sync_runtime_.resolveNativeSync(
                frame_sync,
                image_available,
                render_finished,
                in_flight);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            const std::array<VkSemaphore, 1U> wait_semaphores{render_finished};
            const std::array<VkSwapchainKHR, 1U> swapchains{swapchain};
            const std::array<U32, 1U> image_indices{command_.image_index};
            VkResult present_result = VK_SUCCESS;
            const VkPresentInfoKHR present_info{
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .pNext = nullptr,
                .waitSemaphoreCount = 1U,
                .pWaitSemaphores = wait_semaphores.data(),
                .swapchainCount = 1U,
                .pSwapchains = swapchains.data(),
                .pImageIndices = image_indices.data(),
                .pResults = &present_result,
            };

            const VkResult vk_result = vkQueuePresentKHR(present_queue, &present_info);
            last_vulkan_result = vk_result == VK_SUCCESS ? present_result : vk_result;
            return makeResult(mapVulkanResult(last_vulkan_result));
        }
    }

    bool isInitialized() const noexcept
    {
        return device != VK_NULL_HANDLE && present_queue != VK_NULL_HANDLE;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

private:
    static NativeResult makeResult(NativeStatusCode code_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Swapchain,
            .object_id = {.value = 0U},
            .generation = {.value = 0U},
        };
    }

    static NativeStatusCode mapVulkanResult(VkResult result_) noexcept
    {
        switch (result_) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR:
            return NativeStatusCode::Ok;
        case VK_TIMEOUT:
            return NativeStatusCode::Timeout;
        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_ERROR_SURFACE_LOST_KHR:
            return NativeStatusCode::SwapchainOutOfDate;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return NativeStatusCode::OutOfMemory;
        case VK_ERROR_DEVICE_LOST:
            return NativeStatusCode::DeviceLost;
        default:
            return NativeStatusCode::InvalidInput;
        }
    }

    static bool isPresentCommandValid(const PresentCommand<Provider, Dim>& command_) noexcept
    {
        return command_.generation != 0U && command_.wait_sync_generation != 0U;
    }

    VkDevice device = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
};

static_assert(std::is_trivial_v<VulkanPresentRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanPresentRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanPresentRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanPresentRuntimeConfig>);

} // namespace Render2D
