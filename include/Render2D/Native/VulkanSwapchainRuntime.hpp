#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <type_traits>

namespace Render2D {

inline constexpr U32 kVulkanSwapchainOwnsHandle = 1U << 0U;

struct VulkanSwapchainRuntimeConfig {
    VkDevice device;
};

struct VulkanSwapchainCreateConfig {
    VkSurfaceKHR surface;
    VkSwapchainKHR old_swapchain;
    const U32* queue_family_indices;
    U32 queue_family_index_count;
    U32 min_image_count;
    U32 width;
    U32 height;
    U32 format;
    U32 color_space;
    U32 image_usage;
    U32 sharing_mode;
    U32 pre_transform;
    U32 composite_alpha;
    U32 present_mode;
    U32 clipped;
    U32 image_array_layers;
    U32 image_view_type;
    U32 image_aspect_flags;
    U32 flags;
};

struct VulkanSwapchainAdoptConfig {
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    U32 width;
    U32 height;
    U32 format;
    U32 image_view_type;
    U32 image_aspect_flags;
    U32 flags;
};

template<class Provider, class Dim>
class VulkanSwapchainRuntime {
public:
    VulkanSwapchainRuntime() = default;
    VulkanSwapchainRuntime(const VulkanSwapchainRuntime&) = delete;
    VulkanSwapchainRuntime& operator=(const VulkanSwapchainRuntime&) = delete;
    VulkanSwapchainRuntime(VulkanSwapchainRuntime&&) = delete;
    VulkanSwapchainRuntime& operator=(VulkanSwapchainRuntime&&) = delete;

    ~VulkanSwapchainRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanSwapchainRuntimeConfig config_) noexcept
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
            for (auto& slot : swapchain_slots) {
                destroySwapchainSlot(slot);
            }
        }

        swapchain_slots.clear();
        image_slots.clear();
        free_swapchain_ids.clear();
        scratch_images.clear();
        scratch_image_views.clear();
        active_swapchain_count = 0U;
        active_image_count = 0U;
        device = VK_NULL_HANDLE;
        last_vulkan_result = VK_SUCCESS;
    }

    NativeCapacityResult reserveSwapchains(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                swapchain_slots.reserve(capacity_);
                free_swapchain_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(swapchain_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(swapchain_slots.capacity()),
            };
        }
    }

    NativeCapacityResult reserveSwapchainImages(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                image_slots.reserve(capacity_);
                scratch_images.reserve(capacity_);
                scratch_image_views.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(image_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(image_slots.capacity()),
            };
        }
    }

    NativeResult createSwapchain(
        const VulkanSwapchainCreateConfig& config_,
        SwapchainState<Provider, Dim>& out_state_,
        std::span<SwapchainImageRef<Provider, Dim>> out_images_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            out_state_ = {};
            clearOutputImages(out_images_);
            if (!isInitialized() || !isCreateConfigValid(config_)) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            const VkSwapchainCreateInfoKHR create_info{
                .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                .pNext = nullptr,
                .flags = static_cast<VkSwapchainCreateFlagsKHR>(config_.flags),
                .surface = config_.surface,
                .minImageCount = config_.min_image_count,
                .imageFormat = static_cast<VkFormat>(config_.format),
                .imageColorSpace = static_cast<VkColorSpaceKHR>(config_.color_space),
                .imageExtent = {.width = config_.width, .height = config_.height},
                .imageArrayLayers = config_.image_array_layers,
                .imageUsage = static_cast<VkImageUsageFlags>(config_.image_usage),
                .imageSharingMode = static_cast<VkSharingMode>(config_.sharing_mode),
                .queueFamilyIndexCount = config_.queue_family_index_count,
                .pQueueFamilyIndices = config_.queue_family_indices,
                .preTransform = static_cast<VkSurfaceTransformFlagBitsKHR>(config_.pre_transform),
                .compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(config_.composite_alpha),
                .presentMode = static_cast<VkPresentModeKHR>(config_.present_mode),
                .clipped = config_.clipped != 0U ? VK_TRUE : VK_FALSE,
                .oldSwapchain = config_.old_swapchain,
            };

            VkSwapchainKHR swapchain = VK_NULL_HANDLE;
            const VkResult vk_result = vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), 0U, 0U);
            }

            return registerSwapchain(
                config_.surface,
                swapchain,
                config_.width,
                config_.height,
                config_.format,
                config_.image_view_type,
                config_.image_aspect_flags,
                config_.flags | kVulkanSwapchainOwnsHandle,
                out_state_,
                out_images_);
        }
    }

    NativeResult adoptSwapchain(
        const VulkanSwapchainAdoptConfig& config_,
        SwapchainState<Provider, Dim>& out_state_,
        std::span<SwapchainImageRef<Provider, Dim>> out_images_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            out_state_ = {};
            clearOutputImages(out_images_);
            if (!isInitialized() ||
                config_.surface == VK_NULL_HANDLE ||
                config_.swapchain == VK_NULL_HANDLE ||
                config_.width == 0U ||
                config_.height == 0U ||
                config_.format == static_cast<U32>(VK_FORMAT_UNDEFINED) ||
                config_.image_view_type == 0U ||
                config_.image_aspect_flags == 0U) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            return registerSwapchain(
                config_.surface,
                config_.swapchain,
                config_.width,
                config_.height,
                config_.format,
                config_.image_view_type,
                config_.image_aspect_flags,
                config_.flags,
                out_state_,
                out_images_);
        }
    }

    NativeResult resolveSwapchainState(
        const SwapchainState<Provider, Dim>& state_,
        SwapchainState<Provider, Dim>& out_state_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveSwapchainState(state_)) {
                out_state_ = {};
                return makeResult(
                    NativeStatusCode::StaleReference,
                    state_.swapchain_id,
                    state_.generation);
            }

            out_state_ = swapchain_slots[state_.swapchain_id].state;
            return makeResult(NativeStatusCode::Ok, state_.swapchain_id, state_.generation);
        }
    }

    NativeResult resolveNativeSwapchain(
        const SwapchainState<Provider, Dim>& state_,
        VkSwapchainKHR& out_swapchain_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            out_swapchain_ = VK_NULL_HANDLE;
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveSwapchainState(state_)) {
                out_swapchain_ = VK_NULL_HANDLE;
                return makeResult(
                    NativeStatusCode::StaleReference,
                    state_.swapchain_id,
                    state_.generation);
            }

            out_swapchain_ = swapchain_slots[state_.swapchain_id].swapchain;
            return makeResult(NativeStatusCode::Ok, state_.swapchain_id, state_.generation);
        }
    }

    NativeResult resolveSwapchainImageRef(
        const SwapchainImageRef<Provider, Dim>& image_,
        SwapchainImageRef<Provider, Dim>& out_image_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            out_image_ = {};
            return makeResult(NativeStatusCode::UnsupportedDomain, image_.swapchain_id, image_.generation);
        } else {
            if (!isLiveSwapchainImageRef(image_)) {
                out_image_ = {};
                return makeResult(NativeStatusCode::StaleReference, image_.swapchain_id, image_.generation);
            }

            const auto& state = swapchain_slots[image_.swapchain_id].state;
            out_image_ = image_slots[static_cast<Usize>(state.image_first) + image_.image_index].ref;
            return makeResult(NativeStatusCode::Ok, image_.swapchain_id, image_.generation);
        }
    }

    NativeResult resolveNativeSwapchainImage(
        const SwapchainImageRef<Provider, Dim>& image_,
        VkImage& out_image_,
        VkImageView& out_image_view_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            out_image_ = VK_NULL_HANDLE;
            out_image_view_ = VK_NULL_HANDLE;
            return makeResult(NativeStatusCode::UnsupportedDomain, image_.swapchain_id, image_.generation);
        } else {
            if (!isLiveSwapchainImageRef(image_)) {
                out_image_ = VK_NULL_HANDLE;
                out_image_view_ = VK_NULL_HANDLE;
                return makeResult(NativeStatusCode::StaleReference, image_.swapchain_id, image_.generation);
            }

            const auto& state = swapchain_slots[image_.swapchain_id].state;
            const auto& slot = image_slots[static_cast<Usize>(state.image_first) + image_.image_index];
            out_image_ = slot.image;
            out_image_view_ = slot.image_view;
            return makeResult(NativeStatusCode::Ok, image_.swapchain_id, image_.generation);
        }
    }

    NativeResult releaseSwapchainState(const SwapchainState<Provider, Dim>& state_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveSwapchainState(state_)) {
                return makeResult(NativeStatusCode::StaleReference, state_.swapchain_id, state_.generation);
            }

            const NativeStatusCode release_code = pushFreeSwapchainId(state_.swapchain_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, state_.swapchain_id, state_.generation);
            }

            auto& slot = swapchain_slots[state_.swapchain_id];
            destroySwapchainSlot(slot);
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.state = {};
            --active_swapchain_count;

            return makeResult(NativeStatusCode::Ok, state_.swapchain_id, slot.generation.value);
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

    U32 swapchainCount() const noexcept
    {
        return active_swapchain_count;
    }

    U32 swapchainCapacity() const noexcept
    {
        return static_cast<U32>(swapchain_slots.capacity());
    }

    U32 swapchainImageCount() const noexcept
    {
        return active_image_count;
    }

    U32 swapchainImageCapacity() const noexcept
    {
        return static_cast<U32>(image_slots.capacity());
    }

private:
    struct SwapchainSlot {
        SwapchainState<Provider, Dim> state;
        NativeGeneration generation;
        VkSurfaceKHR surface;
        VkSwapchainKHR swapchain;
        U32 occupied;
        U32 owns_swapchain;
    };

    struct ImageSlot {
        SwapchainImageRef<Provider, Dim> ref;
        VkImage image;
        VkImageView image_view;
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
            .object_kind = NativeObjectKind::Swapchain,
            .object_id = {.value = object_id_},
            .generation = {.value = generation_},
        };
    }

    static NativeStatusCode mapVulkanResult(VkResult result_) noexcept
    {
        switch (result_) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR:
            return NativeStatusCode::Ok;
        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_ERROR_SURFACE_LOST_KHR:
            return NativeStatusCode::SwapchainOutOfDate;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return NativeStatusCode::OutOfMemory;
        case VK_ERROR_DEVICE_LOST:
            return NativeStatusCode::DeviceLost;
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return NativeStatusCode::UnsupportedFormat;
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

    static void clearOutputImages(std::span<SwapchainImageRef<Provider, Dim>> out_images_) noexcept
    {
        for (auto& image : out_images_) {
            image = {};
        }
    }

    static bool isCreateConfigValid(const VulkanSwapchainCreateConfig& config_) noexcept
    {
        if (config_.surface == VK_NULL_HANDLE ||
            config_.min_image_count == 0U ||
            config_.width == 0U ||
            config_.height == 0U ||
            config_.format == static_cast<U32>(VK_FORMAT_UNDEFINED) ||
            config_.image_usage == 0U ||
            config_.image_array_layers == 0U ||
            config_.image_view_type == 0U ||
            config_.image_aspect_flags == 0U ||
            config_.pre_transform == 0U ||
            config_.composite_alpha == 0U) {
            return false;
        }

        return config_.sharing_mode != static_cast<U32>(VK_SHARING_MODE_CONCURRENT) ||
            (config_.queue_family_indices != nullptr && config_.queue_family_index_count != 0U);
    }

    NativeResult registerSwapchain(
        VkSurfaceKHR surface_,
        VkSwapchainKHR swapchain_,
        U32 width_,
        U32 height_,
        U32 format_,
        U32 image_view_type_,
        U32 image_aspect_flags_,
        U32 flags_,
        SwapchainState<Provider, Dim>& out_state_,
        std::span<SwapchainImageRef<Provider, Dim>> out_images_)
    {
        U32 image_count = 0U;
        VkResult vk_result = vkGetSwapchainImagesKHR(device, swapchain_, &image_count, nullptr);
        last_vulkan_result = vk_result;
        if (vk_result != VK_SUCCESS || image_count == 0U) {
            destroySwapchainIfOwned(swapchain_, flags_);
            return makeResult(mapVulkanResult(vk_result), 0U, 0U);
        }

        if (out_images_.size() < image_count ||
            image_slots.capacity() - image_slots.size() < image_count) {
            destroySwapchainIfOwned(swapchain_, flags_);
            return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
        }

        try {
            scratch_images.resize(image_count);
            scratch_image_views.resize(image_count);
        } catch (...) {
            destroySwapchainIfOwned(swapchain_, flags_);
            return makeResult(NativeStatusCode::OutOfMemory, 0U, 0U);
        }
        for (auto& image_view : scratch_image_views) {
            image_view = VK_NULL_HANDLE;
        }

        vk_result = vkGetSwapchainImagesKHR(device, swapchain_, &image_count, scratch_images.data());
        last_vulkan_result = vk_result;
        if (vk_result != VK_SUCCESS) {
            destroySwapchainIfOwned(swapchain_, flags_);
            return makeResult(mapVulkanResult(vk_result), 0U, 0U);
        }

        NativeStatusCode create_views_code = createImageViews(
            image_count,
            format_,
            image_view_type_,
            image_aspect_flags_);
        if (create_views_code != NativeStatusCode::Ok) {
            destroyScratchImageViews();
            destroySwapchainIfOwned(swapchain_, flags_);
            return makeResult(create_views_code, 0U, 0U);
        }

        U32 swapchain_id = 0U;
        if (!acquireSwapchainSlot(swapchain_id)) {
            destroyScratchImageViews();
            destroySwapchainIfOwned(swapchain_, flags_);
            return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
        }

        const U32 image_first = static_cast<U32>(image_slots.size());
        auto& slot = swapchain_slots[swapchain_id];
        try {
            for (U32 image_index = 0U; image_index < image_count; ++image_index) {
                const auto ref = SwapchainImageRef<Provider, Dim>{
                    .image_handle = nativeHandleToU64(scratch_images[image_index]),
                    .image_view_handle = nativeHandleToU64(scratch_image_views[image_index]),
                    .swapchain_id = swapchain_id,
                    .image_index = image_index,
                    .width = width_,
                    .height = height_,
                    .format = format_,
                    .generation = slot.generation.value,
                    .flags = flags_,
                };
                image_slots.push_back({
                    .ref = ref,
                    .image = scratch_images[image_index],
                    .image_view = scratch_image_views[image_index],
                    .occupied = 1U,
                });
                out_images_[image_index] = ref;
                scratch_image_views[image_index] = VK_NULL_HANDLE;
            }
        } catch (...) {
            destroyScratchImageViews();
            destroyImageSlotRange(image_first, image_count, false);
            rollbackEmptySwapchainSlot(swapchain_id);
            destroySwapchainIfOwned(swapchain_, flags_);
            return makeResult(NativeStatusCode::OutOfMemory, 0U, 0U);
        }

        slot.surface = surface_;
        slot.swapchain = swapchain_;
        slot.occupied = 1U;
        slot.owns_swapchain = (flags_ & kVulkanSwapchainOwnsHandle) != 0U ? 1U : 0U;
        slot.state = {
            .handle = nativeHandleToU64(swapchain_),
            .swapchain_id = swapchain_id,
            .image_first = image_first,
            .image_count = image_count,
            .width = width_,
            .height = height_,
            .format = format_,
            .generation = slot.generation.value,
            .flags = flags_,
        };
        ++active_swapchain_count;
        active_image_count += image_count;
        out_state_ = slot.state;

        return makeResult(NativeStatusCode::Ok, swapchain_id, slot.generation.value);
    }

    NativeStatusCode createImageViews(
        U32 image_count_,
        U32 format_,
        U32 image_view_type_,
        U32 image_aspect_flags_) noexcept
    {
        for (U32 image_index = 0U; image_index < image_count_; ++image_index) {
            const VkImageViewCreateInfo view_info{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .image = scratch_images[image_index],
                .viewType = static_cast<VkImageViewType>(image_view_type_),
                .format = static_cast<VkFormat>(format_),
                .components = {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
                .subresourceRange = {
                    .aspectMask = static_cast<VkImageAspectFlags>(image_aspect_flags_),
                    .baseMipLevel = 0U,
                    .levelCount = 1U,
                    .baseArrayLayer = 0U,
                    .layerCount = 1U,
                },
            };
            const VkResult vk_result = vkCreateImageView(
                device,
                &view_info,
                nullptr,
                &scratch_image_views[image_index]);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return mapVulkanResult(vk_result);
            }
        }

        return NativeStatusCode::Ok;
    }

    void destroyScratchImageViews() noexcept
    {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        for (auto& image_view : scratch_image_views) {
            if (image_view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, image_view, nullptr);
                image_view = VK_NULL_HANDLE;
            }
        }
    }

    void destroySwapchainIfOwned(VkSwapchainKHR swapchain_, U32 flags_) noexcept
    {
        if (device != VK_NULL_HANDLE &&
            swapchain_ != VK_NULL_HANDLE &&
            (flags_ & kVulkanSwapchainOwnsHandle) != 0U) {
            vkDestroySwapchainKHR(device, swapchain_, nullptr);
        }
    }

    void destroySwapchainSlot(SwapchainSlot& slot_) noexcept
    {
        if (slot_.occupied == 0U) {
            return;
        }

        destroyImageSlotRange(slot_.state.image_first, slot_.state.image_count, true);
        if (slot_.owns_swapchain != 0U && slot_.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, slot_.swapchain, nullptr);
        }
        slot_.surface = VK_NULL_HANDLE;
        slot_.swapchain = VK_NULL_HANDLE;
        slot_.owns_swapchain = 0U;
    }

    void destroyImageSlotRange(
        U32 image_first_,
        U32 image_count_,
        bool count_active_) noexcept
    {
        for (U32 image_offset = 0U; image_offset < image_count_; ++image_offset) {
            const Usize slot_index = static_cast<Usize>(image_first_) + image_offset;
            if (slot_index >= image_slots.size()) {
                return;
            }

            auto& slot = image_slots[slot_index];
            if (slot.occupied == 0U) {
                continue;
            }
            if (slot.image_view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, slot.image_view, nullptr);
            }
            slot.ref = {};
            slot.image = VK_NULL_HANDLE;
            slot.image_view = VK_NULL_HANDLE;
            slot.occupied = 0U;
            if (count_active_) {
                --active_image_count;
            }
        }
    }

    bool acquireSwapchainSlot(U32& out_swapchain_id_)
    {
        if (!free_swapchain_ids.empty()) {
            out_swapchain_id_ = free_swapchain_ids.back();
            free_swapchain_ids.pop_back();
            return true;
        }
        if (swapchain_slots.size() >= swapchain_slots.capacity()) {
            return false;
        }

        try {
            out_swapchain_id_ = static_cast<U32>(swapchain_slots.size());
            swapchain_slots.push_back({
                .state = {},
                .generation = {.value = kFirstGeneration},
                .surface = VK_NULL_HANDLE,
                .swapchain = VK_NULL_HANDLE,
                .occupied = 0U,
                .owns_swapchain = 0U,
            });
        } catch (...) {
            return false;
        }

        return true;
    }

    void rollbackEmptySwapchainSlot(U32 swapchain_id_) noexcept
    {
        if (swapchain_id_ >= swapchain_slots.size()) {
            return;
        }

        auto& slot = swapchain_slots[swapchain_id_];
        slot = {
            .state = {},
            .generation = {.value = slot.generation.value},
            .surface = VK_NULL_HANDLE,
            .swapchain = VK_NULL_HANDLE,
            .occupied = 0U,
            .owns_swapchain = 0U,
        };
    }

    NativeStatusCode pushFreeSwapchainId(U32 swapchain_id_)
    {
        if (free_swapchain_ids.size() >= free_swapchain_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }
        try {
            free_swapchain_ids.push_back(swapchain_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }
        return NativeStatusCode::Ok;
    }

    bool isLiveSwapchainState(const SwapchainState<Provider, Dim>& state_) const noexcept
    {
        if (state_.swapchain_id >= swapchain_slots.size()) {
            return false;
        }

        const auto& slot = swapchain_slots[state_.swapchain_id];
        return slot.occupied != 0U && slot.generation.value == state_.generation;
    }

    bool isLiveSwapchainImageRef(const SwapchainImageRef<Provider, Dim>& image_) const noexcept
    {
        if (!isLiveSwapchainId(image_.swapchain_id, image_.generation)) {
            return false;
        }

        const auto& state = swapchain_slots[image_.swapchain_id].state;
        if (image_.image_index >= state.image_count) {
            return false;
        }

        const Usize slot_index = static_cast<Usize>(state.image_first) + image_.image_index;
        if (slot_index >= image_slots.size()) {
            return false;
        }

        const auto& slot = image_slots[slot_index];
        return slot.occupied != 0U &&
            slot.ref.generation == image_.generation &&
            slot.ref.image_index == image_.image_index;
    }

    bool isLiveSwapchainId(U32 swapchain_id_, U32 generation_) const noexcept
    {
        if (swapchain_id_ >= swapchain_slots.size()) {
            return false;
        }

        const auto& slot = swapchain_slots[swapchain_id_];
        return slot.occupied != 0U && slot.generation.value == generation_;
    }

    McVector<SwapchainSlot> swapchain_slots;
    McVector<ImageSlot> image_slots;
    McVector<U32> free_swapchain_ids;
    McVector<VkImage> scratch_images;
    McVector<VkImageView> scratch_image_views;
    VkDevice device = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 active_swapchain_count = 0U;
    U32 active_image_count = 0U;
};

static_assert(std::is_trivial_v<VulkanSwapchainRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanSwapchainRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanSwapchainRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanSwapchainRuntimeConfig>);

static_assert(std::is_trivial_v<VulkanSwapchainCreateConfig>);
static_assert(std::is_standard_layout_v<VulkanSwapchainCreateConfig>);
static_assert(std::is_trivially_copyable_v<VulkanSwapchainCreateConfig>);
static_assert(std::is_aggregate_v<VulkanSwapchainCreateConfig>);

static_assert(std::is_trivial_v<VulkanSwapchainAdoptConfig>);
static_assert(std::is_standard_layout_v<VulkanSwapchainAdoptConfig>);
static_assert(std::is_trivially_copyable_v<VulkanSwapchainAdoptConfig>);
static_assert(std::is_aggregate_v<VulkanSwapchainAdoptConfig>);

} // namespace Render2D
