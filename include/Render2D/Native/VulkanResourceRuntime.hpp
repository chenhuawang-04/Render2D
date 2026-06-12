#pragma once

#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Memory/VulkanMemoryCenterAllocator.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Render2D {

struct VulkanResourceRuntimeConfig {
    VkPhysicalDevice physical_device;
    VkDevice device;
};

template<class Provider, class Dim>
class VulkanResourceRuntime {
public:
    VulkanResourceRuntime() = default;
    VulkanResourceRuntime(const VulkanResourceRuntime&) = delete;
    VulkanResourceRuntime& operator=(const VulkanResourceRuntime&) = delete;
    VulkanResourceRuntime(VulkanResourceRuntime&&) = delete;
    VulkanResourceRuntime& operator=(VulkanResourceRuntime&&) = delete;

    ~VulkanResourceRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanResourceRuntimeConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Unknown, 0U, 0U);
        } else {
            if (config_.physical_device == VK_NULL_HANDLE || config_.device == VK_NULL_HANDLE || device != VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Unknown, 0U, 0U);
            }

            physical_device = config_.physical_device;
            device = config_.device;
            if (!memory_allocator.initialize({
                    .physical_device = physical_device,
                    .device = device,
                })) {
                physical_device = VK_NULL_HANDLE;
                device = VK_NULL_HANDLE;
                return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Unknown, 0U, 0U);
            }
            return makeResult(NativeStatusCode::Ok, NativeObjectKind::Unknown, 0U, 0U);
        }
    }

    void shutdown() noexcept
    {
        if (device != VK_NULL_HANDLE) {
            for (auto& slot : buffer_slots) {
                destroyBufferSlot(slot);
            }
            for (auto& slot : image_slots) {
                destroyImageSlot(slot);
            }
            memory_allocator.shutdown();
        }

        buffer_slots.clear();
        image_slots.clear();
        free_buffer_ids.clear();
        free_image_ids.clear();
        active_buffer_count = 0U;
        active_image_count = 0U;
        physical_device = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        last_vulkan_result = VK_SUCCESS;
    }

    NativeCapacityResult reserveBuffers(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                buffer_slots.reserve(capacity_);
                free_buffer_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(buffer_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(buffer_slots.capacity()),
            };
        }
    }

    NativeCapacityResult reserveImages(U32 capacity_)
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
                free_image_ids.reserve(capacity_);
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

    NativeResult createBufferRef(
        U64 byte_size_,
        U32 usage_flags_,
        NativeMemoryDomain memory_domain_,
        BufferRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Buffer, 0U, 0U);
        } else {
            if (!isInitialized() || byte_size_ == 0U || usage_flags_ == 0U) {
                return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Buffer, 0U, 0U);
            }
            if (!hasAvailableBufferSlot()) {
                return makeResult(NativeStatusCode::OutOfCapacity, NativeObjectKind::Buffer, 0U, 0U);
            }

            VkBufferCreateInfo buffer_info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .size = byte_size_,
                .usage = static_cast<VkBufferUsageFlags>(usage_flags_),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0U,
                .pQueueFamilyIndices = nullptr,
            };

            VkBuffer buffer = VK_NULL_HANDLE;
            VkResult vk_result = vkCreateBuffer(device, &buffer_info, nullptr, &buffer);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), NativeObjectKind::Buffer, 0U, 0U);
            }

            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, buffer, &requirements);
            VulkanMemoryAllocation allocation = memory_allocator.allocateAndBindBuffer(buffer, requirements, memory_domain_);
            if (!allocation.valid()) {
                vkDestroyBuffer(device, buffer, nullptr);
                return makeResult(mapMemoryCenterStatus(memory_allocator.lastStatus()), NativeObjectKind::Buffer, 0U, 0U);
            }

            U32 buffer_id = 0U;
            if (!acquireBufferSlot(buffer_id)) {
                vkDestroyBuffer(device, buffer, nullptr);
                memory_allocator.deallocate(allocation);
                return makeResult(NativeStatusCode::OutOfCapacity, NativeObjectKind::Buffer, 0U, 0U);
            }

            auto& slot = buffer_slots[buffer_id];
            slot.buffer = buffer;
            slot.allocation = allocation;
            slot.byte_size = byte_size_;
            slot.occupied = 1U;
            slot.ref = {
                .handle = nativeHandleToU64(buffer),
                .byte_size = byte_size_,
                .buffer_id = buffer_id,
                .generation = slot.generation.value,
                .usage_flags = usage_flags_,
                .memory_domain = static_cast<U32>(memory_domain_),
            };
            ++active_buffer_count;
            out_ref_ = slot.ref;
            return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, buffer_id, slot.generation.value);
        }
    }

    NativeResult createImageRef(
        U32 width_,
        U32 height_,
        U32 format_,
        U32 usage_flags_,
        ImageRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Image, 0U, 0U);
        } else {
            if (!isInitialized() ||
                width_ == 0U ||
                height_ == 0U ||
                format_ == static_cast<U32>(VK_FORMAT_UNDEFINED) ||
                usage_flags_ == 0U) {
                return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Image, 0U, 0U);
            }
            if (!hasAvailableImageSlot()) {
                return makeResult(NativeStatusCode::OutOfCapacity, NativeObjectKind::Image, 0U, 0U);
            }

            VkImageCreateInfo image_info{
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = static_cast<VkFormat>(format_),
                .extent = {.width = width_, .height = height_, .depth = 1U},
                .mipLevels = 1U,
                .arrayLayers = 1U,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = static_cast<VkImageUsageFlags>(usage_flags_),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0U,
                .pQueueFamilyIndices = nullptr,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            };

            VkImage image = VK_NULL_HANDLE;
            VkResult vk_result = vkCreateImage(device, &image_info, nullptr, &image);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                return makeResult(mapVulkanResult(vk_result), NativeObjectKind::Image, 0U, 0U);
            }

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device, image, &requirements);
            VulkanMemoryAllocation allocation = memory_allocator.allocateAndBindImage(image, requirements);
            if (!allocation.valid()) {
                vkDestroyImage(device, image, nullptr);
                return makeResult(mapMemoryCenterStatus(memory_allocator.lastStatus()), NativeObjectKind::Image, 0U, 0U);
            }

            VkImageView image_view = VK_NULL_HANDLE;
            VkImageViewCreateInfo view_info{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .image = image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = static_cast<VkFormat>(format_),
                .components = {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0U,
                    .levelCount = 1U,
                    .baseArrayLayer = 0U,
                    .layerCount = 1U,
                },
            };
            vk_result = vkCreateImageView(device, &view_info, nullptr, &image_view);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                vkDestroyImage(device, image, nullptr);
                memory_allocator.deallocate(allocation);
                return makeResult(mapVulkanResult(vk_result), NativeObjectKind::Image, 0U, 0U);
            }

            U32 image_id = 0U;
            if (!acquireImageSlot(image_id)) {
                vkDestroyImageView(device, image_view, nullptr);
                vkDestroyImage(device, image, nullptr);
                memory_allocator.deallocate(allocation);
                return makeResult(NativeStatusCode::OutOfCapacity, NativeObjectKind::Image, 0U, 0U);
            }

            auto& slot = image_slots[image_id];
            slot.image = image;
            slot.image_view = image_view;
            slot.allocation = allocation;
            slot.layout = VK_IMAGE_LAYOUT_UNDEFINED;
            slot.occupied = 1U;
            slot.ref = {
                .image_handle = nativeHandleToU64(image),
                .image_view_handle = nativeHandleToU64(image_view),
                .image_id = image_id,
                .width = width_,
                .height = height_,
                .format = format_,
                .generation = slot.generation.value,
                .usage_flags = usage_flags_,
            };
            ++active_image_count;
            out_ref_ = slot.ref;
            return makeResult(NativeStatusCode::Ok, NativeObjectKind::Image, image_id, slot.generation.value);
        }
    }

    NativeResult writeBuffer(
        const BufferRef<Provider, Dim>& ref_,
        const void* data_,
        U64 byte_count_,
        U64 offset_) noexcept
    {
        if (!isLiveBufferRef(ref_) ||
            !isHostVisibleBufferRef(ref_) ||
            data_ == nullptr ||
            !isValidBufferRange(ref_, offset_, byte_count_)) {
            return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }
        if (byte_count_ == 0U) {
            return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }

        const auto& slot = buffer_slots[ref_.buffer_id];
        if (slot.allocation.mappedData() == nullptr) {
            return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }

        auto* mapped = slot.allocation.mappedData() + static_cast<Usize>(offset_);
        std::memcpy(mapped, data_, static_cast<Usize>(byte_count_));
        if (!memory_allocator.flushMappedRange(slot.allocation, offset_, byte_count_)) {
            return makeResult(mapMemoryCenterStatus(memory_allocator.lastStatus()), NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
    }

    NativeResult readBuffer(
        const BufferRef<Provider, Dim>& ref_,
        void* data_,
        U64 byte_count_,
        U64 offset_) noexcept
    {
        if (!isLiveBufferRef(ref_) ||
            !isHostVisibleBufferRef(ref_) ||
            data_ == nullptr ||
            !isValidBufferRange(ref_, offset_, byte_count_)) {
            return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }
        if (byte_count_ == 0U) {
            return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }

        const auto& slot = buffer_slots[ref_.buffer_id];
        if (slot.allocation.mappedData() == nullptr) {
            return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }

        if (!memory_allocator.invalidateMappedRange(slot.allocation, offset_, byte_count_)) {
            return makeResult(mapMemoryCenterStatus(memory_allocator.lastStatus()), NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }
        const auto* mapped = slot.allocation.mappedData() + static_cast<Usize>(offset_);
        std::memcpy(data_, mapped, static_cast<Usize>(byte_count_));
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
    }

    NativeResult resolveNativeBuffer(
        const BufferRef<Provider, Dim>& ref_,
        VkBuffer& out_buffer_) const noexcept
    {
        if (!isLiveBufferRef(ref_)) {
            out_buffer_ = VK_NULL_HANDLE;
            return makeResult(NativeStatusCode::StaleReference, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }

        out_buffer_ = buffer_slots[ref_.buffer_id].buffer;
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
    }

    NativeResult resolveNativeImage(
        const ImageRef<Provider, Dim>& ref_,
        VkImage& out_image_,
        VkImageView& out_image_view_) const noexcept
    {
        if (!isLiveImageRef(ref_)) {
            out_image_ = VK_NULL_HANDLE;
            out_image_view_ = VK_NULL_HANDLE;
            return makeResult(NativeStatusCode::StaleReference, NativeObjectKind::Image, ref_.image_id, ref_.generation);
        }

        const auto& slot = image_slots[ref_.image_id];
        out_image_ = slot.image;
        out_image_view_ = slot.image_view;
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Image, ref_.image_id, ref_.generation);
    }

    NativeResult transitionImageLayout(
        VkCommandBuffer command_buffer_,
        const ImageRef<Provider, Dim>& ref_,
        VkImageLayout new_layout_,
        VkPipelineStageFlags source_stage_,
        VkPipelineStageFlags destination_stage_,
        VkAccessFlags source_access_,
        VkAccessFlags destination_access_) noexcept
    {
        if (!isLiveImageRef(ref_) || command_buffer_ == VK_NULL_HANDLE) {
            return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Image, ref_.image_id, ref_.generation);
        }

        auto& slot = image_slots[ref_.image_id];
        const VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = source_access_,
            .dstAccessMask = destination_access_,
            .oldLayout = slot.layout,
            .newLayout = new_layout_,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = slot.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0U,
                .levelCount = 1U,
                .baseArrayLayer = 0U,
                .layerCount = 1U,
            },
        };
        vkCmdPipelineBarrier(
            command_buffer_,
            source_stage_,
            destination_stage_,
            0U,
            0U,
            nullptr,
            0U,
            nullptr,
            1U,
            &barrier);
        slot.layout = new_layout_;
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Image, ref_.image_id, ref_.generation);
    }

    NativeResult recordCopyBuffer(
        VkCommandBuffer command_buffer_,
        const BufferRef<Provider, Dim>& source_,
        const BufferRef<Provider, Dim>& destination_,
        U64 byte_count_) const noexcept
    {
        return recordCopyBuffer(command_buffer_, source_, destination_, 0U, 0U, byte_count_);
    }

    NativeResult recordCopyBuffer(
        VkCommandBuffer command_buffer_,
        const BufferRef<Provider, Dim>& source_,
        const BufferRef<Provider, Dim>& destination_,
        U64 source_offset_,
        U64 destination_offset_,
        U64 byte_count_) const noexcept
    {
        if (command_buffer_ == VK_NULL_HANDLE ||
            byte_count_ == 0U ||
            !isLiveBufferRef(source_) ||
            !isLiveBufferRef(destination_) ||
            !isValidBufferRange(source_, source_offset_, byte_count_) ||
            !isValidBufferRange(destination_, destination_offset_, byte_count_)) {
            return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Buffer, destination_.buffer_id, destination_.generation);
        }

        VkBuffer source_buffer = VK_NULL_HANDLE;
        VkBuffer destination_buffer = VK_NULL_HANDLE;
        NativeResult result = resolveNativeBuffer(source_, source_buffer);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }
        result = resolveNativeBuffer(destination_, destination_buffer);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }

        const VkBufferCopy copy_region{
            .srcOffset = source_offset_,
            .dstOffset = destination_offset_,
            .size = byte_count_,
        };
        vkCmdCopyBuffer(command_buffer_, source_buffer, destination_buffer, 1U, &copy_region);
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, destination_.buffer_id, destination_.generation);
    }

    NativeResult recordCopyNativeBufferToBuffer(
        VkCommandBuffer command_buffer_,
        VkBuffer source_buffer_,
        U64 source_offset_,
        const BufferRef<Provider, Dim>& destination_,
        U64 destination_offset_,
        U64 byte_count_) const noexcept
    {
        if (command_buffer_ == VK_NULL_HANDLE ||
            source_buffer_ == VK_NULL_HANDLE ||
            byte_count_ == 0U ||
            source_offset_ > kMaxU64 - byte_count_ ||
            !isLiveBufferRef(destination_) ||
            !isValidBufferRange(destination_, destination_offset_, byte_count_)) {
            return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Buffer, destination_.buffer_id, destination_.generation);
        }

        VkBuffer destination_buffer = VK_NULL_HANDLE;
        const NativeResult result = resolveNativeBuffer(destination_, destination_buffer);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }

        const VkBufferCopy copy_region{
            .srcOffset = source_offset_,
            .dstOffset = destination_offset_,
            .size = byte_count_,
        };
        vkCmdCopyBuffer(command_buffer_, source_buffer_, destination_buffer, 1U, &copy_region);
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, destination_.buffer_id, destination_.generation);
    }

    NativeResult recordBufferBarrier(
        VkCommandBuffer command_buffer_,
        const BufferRef<Provider, Dim>& ref_,
        VkPipelineStageFlags source_stage_,
        VkPipelineStageFlags destination_stage_,
        VkAccessFlags source_access_,
        VkAccessFlags destination_access_) const noexcept
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        NativeResult result = resolveNativeBuffer(ref_, buffer);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }
        if (command_buffer_ == VK_NULL_HANDLE) {
            return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }

        const VkBufferMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = source_access_,
            .dstAccessMask = destination_access_,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = buffer,
            .offset = 0U,
            .size = ref_.byte_size,
        };
        vkCmdPipelineBarrier(
            command_buffer_,
            source_stage_,
            destination_stage_,
            0U,
            0U,
            nullptr,
            1U,
            &barrier,
            0U,
            nullptr);
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
    }

    NativeResult recordCopyBufferToImage(
        VkCommandBuffer command_buffer_,
        const BufferRef<Provider, Dim>& source_,
        const ImageRef<Provider, Dim>& destination_) const noexcept
    {
        return recordCopyBufferToImage(command_buffer_, source_, destination_, 0U);
    }

    NativeResult recordCopyBufferToImage(
        VkCommandBuffer command_buffer_,
        const BufferRef<Provider, Dim>& source_,
        const ImageRef<Provider, Dim>& destination_,
        U64 source_offset_) const noexcept
    {
        U64 byte_count = 0U;
        if (command_buffer_ == VK_NULL_HANDLE ||
            !isLiveBufferRef(source_) ||
            !isLiveImageRef(destination_) ||
            (source_.usage_flags & static_cast<U32>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) == 0U ||
            (destination_.usage_flags & static_cast<U32>(VK_IMAGE_USAGE_TRANSFER_DST_BIT)) == 0U ||
            !makeImageByteCount(destination_, byte_count) ||
            !isValidBufferRange(source_, source_offset_, byte_count)) {
            return makeResult(
                NativeStatusCode::InvalidInput,
                NativeObjectKind::Image,
                destination_.image_id,
                destination_.generation);
        }

        VkBuffer source_buffer = VK_NULL_HANDLE;
        NativeResult result = resolveNativeBuffer(source_, source_buffer);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }

        VkImage destination_image = VK_NULL_HANDLE;
        VkImageView destination_view = VK_NULL_HANDLE;
        result = resolveNativeImage(destination_, destination_image, destination_view);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }

        const VkBufferImageCopy copy_region{
            .bufferOffset = source_offset_,
            .bufferRowLength = 0U,
            .bufferImageHeight = 0U,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0U,
                .baseArrayLayer = 0U,
                .layerCount = 1U,
            },
            .imageOffset = {.x = 0, .y = 0, .z = 0},
            .imageExtent = {.width = destination_.width, .height = destination_.height, .depth = 1U},
        };
        vkCmdCopyBufferToImage(
            command_buffer_,
            source_buffer,
            destination_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1U,
            &copy_region);
        return makeResult(
            NativeStatusCode::Ok,
            NativeObjectKind::Image,
            destination_.image_id,
            destination_.generation);
    }

    NativeResult recordCopyBufferToImageRegion(
        VkCommandBuffer command_buffer_,
        const BufferRef<Provider, Dim>& source_,
        const ImageRef<Provider, Dim>& destination_,
        U64 source_offset_,
        U32 destination_x_,
        U32 destination_y_,
        U32 region_width_,
        U32 region_height_) const noexcept
    {
        U64 byte_count = 0U;
        if (command_buffer_ == VK_NULL_HANDLE ||
            !isLiveBufferRef(source_) ||
            !isLiveImageRef(destination_) ||
            region_width_ == 0U ||
            region_height_ == 0U ||
            (source_.usage_flags & static_cast<U32>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) == 0U ||
            (destination_.usage_flags & static_cast<U32>(VK_IMAGE_USAGE_TRANSFER_DST_BIT)) == 0U ||
            destination_x_ > destination_.width ||
            destination_y_ > destination_.height ||
            region_width_ > destination_.width - destination_x_ ||
            region_height_ > destination_.height - destination_y_ ||
            !makeRegionByteCount(destination_.format, region_width_, region_height_, byte_count) ||
            !isValidBufferRange(source_, source_offset_, byte_count)) {
            return makeResult(
                NativeStatusCode::InvalidInput,
                NativeObjectKind::Image,
                destination_.image_id,
                destination_.generation);
        }

        VkBuffer source_buffer = VK_NULL_HANDLE;
        NativeResult result = resolveNativeBuffer(source_, source_buffer);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }

        VkImage destination_image = VK_NULL_HANDLE;
        VkImageView destination_view = VK_NULL_HANDLE;
        result = resolveNativeImage(destination_, destination_image, destination_view);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }

        const VkBufferImageCopy copy_region{
            .bufferOffset = source_offset_,
            .bufferRowLength = 0U,
            .bufferImageHeight = 0U,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0U,
                .baseArrayLayer = 0U,
                .layerCount = 1U,
            },
            .imageOffset = {
                .x = static_cast<std::int32_t>(destination_x_),
                .y = static_cast<std::int32_t>(destination_y_),
                .z = 0,
            },
            .imageExtent = {.width = region_width_, .height = region_height_, .depth = 1U},
        };
        vkCmdCopyBufferToImage(
            command_buffer_,
            source_buffer,
            destination_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1U,
            &copy_region);
        return makeResult(
            NativeStatusCode::Ok,
            NativeObjectKind::Image,
            destination_.image_id,
            destination_.generation);
    }

    NativeResult recordCopyImageToBuffer(
        VkCommandBuffer command_buffer_,
        const ImageRef<Provider, Dim>& source_,
        const BufferRef<Provider, Dim>& destination_) const noexcept
    {
        if (command_buffer_ == VK_NULL_HANDLE) {
            return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::Buffer, destination_.buffer_id, destination_.generation);
        }

        VkImage source_image = VK_NULL_HANDLE;
        VkImageView source_view = VK_NULL_HANDLE;
        VkBuffer destination_buffer = VK_NULL_HANDLE;
        NativeResult result = resolveNativeImage(source_, source_image, source_view);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }
        result = resolveNativeBuffer(destination_, destination_buffer);
        if (result.code != NativeStatusCode::Ok) {
            return result;
        }

        const VkBufferImageCopy copy_region{
            .bufferOffset = 0U,
            .bufferRowLength = 0U,
            .bufferImageHeight = 0U,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0U,
                .baseArrayLayer = 0U,
                .layerCount = 1U,
            },
            .imageOffset = {.x = 0, .y = 0, .z = 0},
            .imageExtent = {.width = source_.width, .height = source_.height, .depth = 1U},
        };
        vkCmdCopyImageToBuffer(
            command_buffer_,
            source_image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            destination_buffer,
            1U,
            &copy_region);
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, destination_.buffer_id, destination_.generation);
    }

    NativeResult releaseBufferRef(const BufferRef<Provider, Dim>& ref_)
    {
        if (!isLiveBufferRef(ref_)) {
            return makeResult(NativeStatusCode::StaleReference, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }

        const NativeStatusCode release_code = pushFreeBufferId(ref_.buffer_id);
        if (release_code != NativeStatusCode::Ok) {
            return makeResult(release_code, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }

        auto& slot = buffer_slots[ref_.buffer_id];
        destroyBufferSlot(slot);
        slot.occupied = 0U;
        slot.generation.value = nextGeneration(slot.generation.value);
        slot.ref = {};
        --active_buffer_count;
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, ref_.buffer_id, slot.generation.value);
    }

    NativeResult releaseImageRef(const ImageRef<Provider, Dim>& ref_)
    {
        if (!isLiveImageRef(ref_)) {
            return makeResult(NativeStatusCode::StaleReference, NativeObjectKind::Image, ref_.image_id, ref_.generation);
        }

        const NativeStatusCode release_code = pushFreeImageId(ref_.image_id);
        if (release_code != NativeStatusCode::Ok) {
            return makeResult(release_code, NativeObjectKind::Image, ref_.image_id, ref_.generation);
        }

        auto& slot = image_slots[ref_.image_id];
        destroyImageSlot(slot);
        slot.occupied = 0U;
        slot.generation.value = nextGeneration(slot.generation.value);
        slot.ref = {};
        --active_image_count;
        return makeResult(NativeStatusCode::Ok, NativeObjectKind::Image, ref_.image_id, slot.generation.value);
    }

    bool isInitialized() const noexcept
    {
        return physical_device != VK_NULL_HANDLE && device != VK_NULL_HANDLE;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

    U32 bufferCount() const noexcept
    {
        return active_buffer_count;
    }

    U32 bufferCapacity() const noexcept
    {
        return static_cast<U32>(buffer_slots.capacity());
    }

    U32 imageCount() const noexcept
    {
        return active_image_count;
    }

    U32 imageCapacity() const noexcept
    {
        return static_cast<U32>(image_slots.capacity());
    }

private:
    struct BufferSlot {
        BufferRef<Provider, Dim> ref;
        NativeGeneration generation;
        VkBuffer buffer;
        VulkanMemoryAllocation allocation;
        U64 byte_size;
        U32 occupied;
    };

    struct ImageSlot {
        ImageRef<Provider, Dim> ref;
        NativeGeneration generation;
        VkImage image;
        VkImageView image_view;
        VulkanMemoryAllocation allocation;
        VkImageLayout layout;
        U32 occupied;
    };

    static constexpr U32 kFirstGeneration = 1U;
    static constexpr U64 kMaxU64 = 0xFFFFFFFFFFFFFFFFULL;

    static NativeResult makeResult(
        NativeStatusCode code_,
        NativeObjectKind object_kind_,
        U32 object_id_,
        U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = object_kind_,
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

    static NativeStatusCode mapMemoryCenterStatus(
        Center::Memory::Vulkan::VulkanAllocStatus status_) noexcept
    {
        switch (status_) {
        case Center::Memory::Vulkan::VulkanAllocStatus::ok:
            return NativeStatusCode::Ok;
        case Center::Memory::Vulkan::VulkanAllocStatus::backend_allocate_failed:
        case Center::Memory::Vulkan::VulkanAllocStatus::backend_deallocate_failed:
        case Center::Memory::Vulkan::VulkanAllocStatus::map_failed:
        case Center::Memory::Vulkan::VulkanAllocStatus::out_of_capacity:
            return NativeStatusCode::OutOfMemory;
        case Center::Memory::Vulkan::VulkanAllocStatus::invalid_size:
        case Center::Memory::Vulkan::VulkanAllocStatus::invalid_alignment:
        case Center::Memory::Vulkan::VulkanAllocStatus::invalid_memory_type_bits:
        case Center::Memory::Vulkan::VulkanAllocStatus::invalid_memory_type_index:
        case Center::Memory::Vulkan::VulkanAllocStatus::backend_memory_type_mismatch:
        case Center::Memory::Vulkan::VulkanAllocStatus::bind_failed:
        case Center::Memory::Vulkan::VulkanAllocStatus::unsupported_backend_feature:
        case Center::Memory::Vulkan::VulkanAllocStatus::range_overflow:
        case Center::Memory::Vulkan::VulkanAllocStatus::not_found:
        case Center::Memory::Vulkan::VulkanAllocStatus::debug_validation_failed:
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

    bool hasAvailableBufferSlot() const noexcept
    {
        return !free_buffer_ids.empty() || buffer_slots.size() < buffer_slots.capacity();
    }

    bool hasAvailableImageSlot() const noexcept
    {
        return !free_image_ids.empty() || image_slots.size() < image_slots.capacity();
    }

    bool acquireBufferSlot(U32& out_buffer_id_)
    {
        if (!free_buffer_ids.empty()) {
            out_buffer_id_ = free_buffer_ids.back();
            free_buffer_ids.pop_back();
            return true;
        }
        if (buffer_slots.size() >= buffer_slots.capacity()) {
            return false;
        }

        try {
            out_buffer_id_ = static_cast<U32>(buffer_slots.size());
            buffer_slots.push_back({
                .ref = {},
                .generation = {.value = kFirstGeneration},
                .buffer = VK_NULL_HANDLE,
                .allocation = {},
                .byte_size = 0U,
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }
        return true;
    }

    bool acquireImageSlot(U32& out_image_id_)
    {
        if (!free_image_ids.empty()) {
            out_image_id_ = free_image_ids.back();
            free_image_ids.pop_back();
            return true;
        }
        if (image_slots.size() >= image_slots.capacity()) {
            return false;
        }

        try {
            out_image_id_ = static_cast<U32>(image_slots.size());
            image_slots.push_back({
                .ref = {},
                .generation = {.value = kFirstGeneration},
                .image = VK_NULL_HANDLE,
                .image_view = VK_NULL_HANDLE,
                .allocation = {},
                .layout = VK_IMAGE_LAYOUT_UNDEFINED,
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }
        return true;
    }

    NativeStatusCode pushFreeBufferId(U32 buffer_id_)
    {
        if (free_buffer_ids.size() >= free_buffer_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }
        try {
            free_buffer_ids.push_back(buffer_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }
        return NativeStatusCode::Ok;
    }

    NativeStatusCode pushFreeImageId(U32 image_id_)
    {
        if (free_image_ids.size() >= free_image_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }
        try {
            free_image_ids.push_back(image_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }
        return NativeStatusCode::Ok;
    }

    void destroyBufferSlot(BufferSlot& slot_) noexcept
    {
        if (slot_.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, slot_.buffer, nullptr);
        }
        memory_allocator.deallocate(slot_.allocation);
        slot_.buffer = VK_NULL_HANDLE;
        slot_.allocation = {};
        slot_.byte_size = 0U;
    }

    void destroyImageSlot(ImageSlot& slot_) noexcept
    {
        if (slot_.image_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, slot_.image_view, nullptr);
        }
        if (slot_.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, slot_.image, nullptr);
        }
        memory_allocator.deallocate(slot_.allocation);
        slot_.image = VK_NULL_HANDLE;
        slot_.image_view = VK_NULL_HANDLE;
        slot_.allocation = {};
        slot_.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    bool isLiveBufferRef(const BufferRef<Provider, Dim>& ref_) const noexcept
    {
        if (ref_.buffer_id >= buffer_slots.size()) {
            return false;
        }

        const auto& slot = buffer_slots[ref_.buffer_id];
        return slot.occupied != 0U && slot.generation.value == ref_.generation;
    }

    bool isHostVisibleBufferRef(const BufferRef<Provider, Dim>& ref_) const noexcept
    {
        return ref_.memory_domain == static_cast<U32>(NativeMemoryDomain::Upload) ||
            ref_.memory_domain == static_cast<U32>(NativeMemoryDomain::Readback);
    }

    bool isValidBufferRange(
        const BufferRef<Provider, Dim>& ref_,
        U64 offset_,
        U64 byte_count_) const noexcept
    {
        const auto& slot = buffer_slots[ref_.buffer_id];
        return byte_count_ <= slot.byte_size && offset_ <= slot.byte_size - byte_count_;
    }

    static bool makeImageByteCount(
        const ImageRef<Provider, Dim>& image_,
        U64& out_byte_count_) noexcept
    {
        U64 bytes_per_pixel = 0U;
        if (!formatBytesPerPixel(image_.format, bytes_per_pixel) ||
            image_.width == 0U ||
            image_.height == 0U ||
            static_cast<U64>(image_.width) > kMaxU64 / static_cast<U64>(image_.height)) {
            out_byte_count_ = 0U;
            return false;
        }

        const U64 pixel_count = static_cast<U64>(image_.width) * static_cast<U64>(image_.height);
        if (pixel_count > kMaxU64 / bytes_per_pixel) {
            out_byte_count_ = 0U;
            return false;
        }

        out_byte_count_ = pixel_count * bytes_per_pixel;
        return true;
    }

    static bool formatBytesPerPixel(
        U32 format_,
        U64& out_bytes_per_pixel_) noexcept
    {
        switch (static_cast<VkFormat>(format_)) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            out_bytes_per_pixel_ = 4U;
            return true;
        default:
            out_bytes_per_pixel_ = 0U;
            return false;
        }
    }

    bool isLiveImageRef(const ImageRef<Provider, Dim>& ref_) const noexcept
    {
        if (ref_.image_id >= image_slots.size()) {
            return false;
        }

        const auto& slot = image_slots[ref_.image_id];
        return slot.occupied != 0U && slot.generation.value == ref_.generation;
    }

    McVector<BufferSlot> buffer_slots;
    McVector<ImageSlot> image_slots;
    McVector<U32> free_buffer_ids;
    McVector<U32> free_image_ids;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VulkanMemoryCenterAllocator memory_allocator;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 active_buffer_count = 0U;
    U32 active_image_count = 0U;
};

static_assert(std::is_trivial_v<VulkanResourceRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanResourceRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanResourceRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanResourceRuntimeConfig>);

} // namespace Render2D
