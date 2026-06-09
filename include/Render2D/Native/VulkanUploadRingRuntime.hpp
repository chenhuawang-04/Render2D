#pragma once

#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Memory/VulkanMemoryCenterAllocator.hpp"

#include "Render2D/Component/Frame.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace Render2D {

struct VulkanUploadRingRuntimeConfig {
    VkPhysicalDevice physical_device;
    VkDevice device;
    U64 byte_capacity;
    U32 frame_count;
    U32 usage_flags;
};

template<class Provider, class Dim>
class VulkanUploadRingRuntime {
public:
    VulkanUploadRingRuntime() = default;
    VulkanUploadRingRuntime(const VulkanUploadRingRuntime&) = delete;
    VulkanUploadRingRuntime& operator=(const VulkanUploadRingRuntime&) = delete;
    VulkanUploadRingRuntime(VulkanUploadRingRuntime&&) = delete;
    VulkanUploadRingRuntime& operator=(VulkanUploadRingRuntime&&) = delete;

    ~VulkanUploadRingRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanUploadRingRuntimeConfig config_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (config_.physical_device == VK_NULL_HANDLE ||
                config_.device == VK_NULL_HANDLE ||
                config_.byte_capacity == 0U ||
                config_.frame_count == 0U ||
                buffer != VK_NULL_HANDLE ||
                mapped_memory != nullptr) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            const U64 segment_size = config_.byte_capacity / config_.frame_count;
            if (segment_size == 0U) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            physical_device = config_.physical_device;
            device = config_.device;
            byte_capacity = config_.byte_capacity;
            frame_count = config_.frame_count;
            frame_segment_size = segment_size;
            usage_flags = config_.usage_flags | static_cast<U32>(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            if (!memory_allocator.initialize({
                    .physical_device = physical_device,
                    .device = device,
                })) {
                shutdown();
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            NativeStatusCode create_code = createNativeBuffer();
            if (create_code != NativeStatusCode::Ok) {
                shutdown();
                return makeResult(create_code, 0U, 0U);
            }

            try {
                frame_slots.resize(frame_count);
            } catch (...) {
                shutdown();
                return makeResult(NativeStatusCode::OutOfMemory, 0U, 0U);
            }

            for (U32 index = 0U; index < frame_count; ++index) {
                frame_slots[index] = {
                    .generation = {.value = kFirstGeneration},
                    .base_offset = static_cast<U64>(index) * frame_segment_size,
                    .cursor = 0U,
                    .active_frame_index = 0U,
                    .in_use = 0U,
                };
            }

            return makeResult(NativeStatusCode::Ok, 0U, kFirstGeneration);
        }
    }

    void shutdown() noexcept
    {
        if (device != VK_NULL_HANDLE) {
            if (buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, buffer, nullptr);
            }
            memory_allocator.deallocate(allocation);
            memory_allocator.shutdown();
        }

        frame_slots.clear();
        physical_device = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        buffer = VK_NULL_HANDLE;
        allocation = {};
        mapped_memory = nullptr;
        byte_capacity = 0U;
        frame_count = 0U;
        frame_segment_size = 0U;
        usage_flags = 0U;
        last_vulkan_result = VK_SUCCESS;
    }

    NativeResult beginFrame(U32 frame_index_) noexcept
    {
        if (!isInitialized()) {
            return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
        }

        auto& slot = frame_slots[slotIndex(frame_index_)];
        if (slot.in_use != 0U) {
            return makeResult(NativeStatusCode::InvalidInput, slotIndex(frame_index_), slot.generation.value);
        }

        slot.active_frame_index = frame_index_;
        slot.cursor = 0U;
        slot.in_use = 1U;
        return makeResult(NativeStatusCode::Ok, slotIndex(frame_index_), slot.generation.value);
    }

    NativeResult allocateSlice(
        U32 frame_index_,
        U64 byte_count_,
        U64 alignment_,
        UploadRingSlice<Provider, Dim>& out_slice_) noexcept
    {
        if (!isInitialized() || byte_count_ == 0U) {
            return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
        }

        const U32 index = slotIndex(frame_index_);
        auto& slot = frame_slots[index];
        if (slot.in_use == 0U || slot.active_frame_index != frame_index_) {
            return makeResult(NativeStatusCode::InvalidInput, index, slot.generation.value);
        }

        const U64 alignment = alignment_ == 0U ? 1U : alignment_;
        const U64 aligned_cursor = alignUp(slot.cursor, alignment);
        if (byte_count_ > frame_segment_size || aligned_cursor > frame_segment_size - byte_count_) {
            return makeResult(NativeStatusCode::OutOfCapacity, index, slot.generation.value);
        }

        out_slice_ = {
            .offset = slot.base_offset + aligned_cursor,
            .byte_count = byte_count_,
            .ring_id = index,
            .frame_index = frame_index_,
            .generation = slot.generation.value,
        };
        slot.cursor = aligned_cursor + byte_count_;
        return makeResult(NativeStatusCode::Ok, index, slot.generation.value);
    }

    NativeResult writeSlice(
        const UploadRingSlice<Provider, Dim>& slice_,
        const void* data_,
        U64 byte_count_,
        U64 destination_offset_) noexcept
    {
        if (!isLiveSlice(slice_) ||
            data_ == nullptr ||
            byte_count_ == 0U ||
            byte_count_ > slice_.byte_count ||
            destination_offset_ > slice_.byte_count - byte_count_) {
            return makeResult(NativeStatusCode::InvalidInput, slice_.ring_id, slice_.generation);
        }

        auto* destination = static_cast<std::byte*>(mapped_memory) + slice_.offset + destination_offset_;
        std::memcpy(destination, data_, static_cast<Usize>(byte_count_));
        if (!memory_allocator.flushMappedRange(allocation, slice_.offset + destination_offset_, byte_count_)) {
            return makeResult(mapMemoryCenterStatus(memory_allocator.lastStatus()), slice_.ring_id, slice_.generation);
        }
        return makeResult(NativeStatusCode::Ok, slice_.ring_id, slice_.generation);
    }

    NativeResult resolveNativeBuffer(
        const UploadRingSlice<Provider, Dim>& slice_,
        VkBuffer& out_buffer_,
        U64& out_offset_) const noexcept
    {
        if (!isLiveSlice(slice_)) {
            out_buffer_ = VK_NULL_HANDLE;
            out_offset_ = 0U;
            return makeResult(NativeStatusCode::StaleReference, slice_.ring_id, slice_.generation);
        }

        out_buffer_ = buffer;
        out_offset_ = slice_.offset;
        return makeResult(NativeStatusCode::Ok, slice_.ring_id, slice_.generation);
    }

    NativeResult completeFrame(U32 frame_index_) noexcept
    {
        if (!isInitialized()) {
            return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
        }

        const U32 index = slotIndex(frame_index_);
        auto& slot = frame_slots[index];
        if (slot.in_use == 0U || slot.active_frame_index != frame_index_) {
            return makeResult(NativeStatusCode::InvalidInput, index, slot.generation.value);
        }

        slot.in_use = 0U;
        slot.cursor = 0U;
        slot.generation.value = nextGeneration(slot.generation.value);
        return makeResult(NativeStatusCode::Ok, index, slot.generation.value);
    }

    bool isInitialized() const noexcept
    {
        return physical_device != VK_NULL_HANDLE &&
            device != VK_NULL_HANDLE &&
            buffer != VK_NULL_HANDLE &&
            allocation.valid() &&
            mapped_memory != nullptr;
    }

    VkBuffer nativeBuffer() const noexcept
    {
        return buffer;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

    U64 capacityBytes() const noexcept
    {
        return byte_capacity;
    }

    U64 frameSegmentBytes() const noexcept
    {
        return frame_segment_size;
    }

    U32 framesInFlight() const noexcept
    {
        return frame_count;
    }

private:
    struct FrameSlot {
        NativeGeneration generation;
        U64 base_offset;
        U64 cursor;
        U32 active_frame_index;
        U32 in_use;
    };

    static constexpr U32 kFirstGeneration = 1U;

    static NativeResult makeResult(
        NativeStatusCode code_,
        U32 object_id_,
        U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Buffer,
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

    static U64 alignUp(U64 value_, U64 alignment_) noexcept
    {
        const U64 remainder = value_ % alignment_;
        return remainder == 0U ? value_ : value_ + alignment_ - remainder;
    }

    U32 slotIndex(U32 frame_index_) const noexcept
    {
        return frame_index_ % frame_count;
    }

    NativeStatusCode createNativeBuffer() noexcept
    {
        const VkBufferCreateInfo buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .size = byte_capacity,
            .usage = static_cast<VkBufferUsageFlags>(usage_flags),
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0U,
            .pQueueFamilyIndices = nullptr,
        };
        VkResult vk_result = vkCreateBuffer(device, &buffer_info, nullptr, &buffer);
        last_vulkan_result = vk_result;
        if (vk_result != VK_SUCCESS) {
            return mapVulkanResult(vk_result);
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer, &requirements);
        allocation = memory_allocator.allocateAndBindBuffer(buffer, requirements, NativeMemoryDomain::Upload);
        if (!allocation.valid() || allocation.mappedData() == nullptr) {
            return mapMemoryCenterStatus(memory_allocator.lastStatus());
        }

        mapped_memory = allocation.mappedData();
        return NativeStatusCode::Ok;
    }

    bool isLiveSlice(const UploadRingSlice<Provider, Dim>& slice_) const noexcept
    {
        if (slice_.ring_id >= frame_slots.size()) {
            return false;
        }
        const auto& slot = frame_slots[slice_.ring_id];
        return slot.in_use != 0U &&
            slot.active_frame_index == slice_.frame_index &&
            slot.generation.value == slice_.generation &&
            slice_.byte_count <= frame_segment_size &&
            slice_.offset >= slot.base_offset &&
            slice_.offset - slot.base_offset <= frame_segment_size - slice_.byte_count;
    }

    McVector<FrameSlot> frame_slots;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanMemoryCenterAllocator memory_allocator;
    VulkanMemoryAllocation allocation{};
    void* mapped_memory = nullptr;
    VkResult last_vulkan_result = VK_SUCCESS;
    U64 byte_capacity = 0U;
    U64 frame_segment_size = 0U;
    U32 frame_count = 0U;
    U32 usage_flags = 0U;
};

static_assert(std::is_trivial_v<VulkanUploadRingRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanUploadRingRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanUploadRingRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanUploadRingRuntimeConfig>);

} // namespace Render2D
