#pragma once

#include "Render2D/Core/Types.hpp"
#include "Render2D/Native/NativeTypes.hpp"

#include <Center/Memory/MemoryCenter.hpp>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace Render2D {

struct VulkanMemoryCenterConfig {
    VkPhysicalDevice physical_device;
    VkDevice device;
};

struct VulkanMemoryAllocation {
    Center::Memory::Vulkan::Slice slice;

    [[nodiscard]] bool valid() const noexcept
    {
        return slice.valid();
    }

    [[nodiscard]] std::byte* mappedData() const noexcept
    {
        return slice.mapped_ptr;
    }
};

class VulkanMemoryCenterAllocator {
public:
    VulkanMemoryCenterAllocator() = default;
    VulkanMemoryCenterAllocator(const VulkanMemoryCenterAllocator&) = delete;
    VulkanMemoryCenterAllocator& operator=(const VulkanMemoryCenterAllocator&) = delete;
    VulkanMemoryCenterAllocator(VulkanMemoryCenterAllocator&&) = delete;
    VulkanMemoryCenterAllocator& operator=(VulkanMemoryCenterAllocator&&) = delete;
    ~VulkanMemoryCenterAllocator() noexcept = default;

    bool initialize(VulkanMemoryCenterConfig config_) noexcept
    {
        if (config_.physical_device == VK_NULL_HANDLE ||
            config_.device == VK_NULL_HANDLE ||
            initialized) {
            return false;
        }

        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceMemoryProperties(config_.physical_device, &memory_properties);

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(config_.physical_device, &properties);
        buffer_image_granularity = static_cast<std::size_t>(properties.limits.bufferImageGranularity);
        if (buffer_image_granularity == 0U) {
            buffer_image_granularity = 1U;
        }

        const Center::Memory::VulkanNativeContext context{
            .device = config_.device,
            .memory_properties = memory_properties,
            .non_coherent_atom_size = properties.limits.nonCoherentAtomSize,
            .enable_dedicated_allocation = true,
            .device_profile = makeDeviceProfile(properties, memory_properties),
        };
        Center::Memory::VulkanNativeProvider linear_provider{
            Center::Memory::VulkanNativeBackend{context},
        };
        Center::Memory::VulkanNativeProvider buddy_provider{
            Center::Memory::VulkanNativeBackend{context},
        };
        allocator = VulkanAutoAllocator{
            std::move(linear_provider),
            std::move(buddy_provider),
            Center::Memory::Vulkan::AllocationPolicy::throughput_first(),
        };
        initialized = true;
        return true;
    }

    void shutdown() noexcept
    {
        if (initialized) {
            allocator.trim();
        }
        initialized = false;
    }

    VulkanMemoryAllocation allocateAndBindBuffer(
        VkBuffer buffer_,
        const VkMemoryRequirements& requirements_,
        NativeMemoryDomain domain_) noexcept
    {
        if (!initialized || buffer_ == VK_NULL_HANDLE || requirements_.size == 0U) {
            return {};
        }

        Center::Memory::Vulkan::ResourceRequirements request{
            .resource = Center::Memory::VulkanNativeBackend::to_resource_handle(buffer_),
            .size = static_cast<std::size_t>(requirements_.size),
            .alignment = static_cast<std::size_t>(requirements_.alignment),
            .selector = {
                .memory_type_bits = requirements_.memoryTypeBits,
                .required_flags = memoryFlagsForDomain(domain_),
                .preferred_flags = preferredMemoryFlagsForDomain(domain_),
            },
            .persistent_map = isHostVisibleDomain(domain_),
            .dedicated_required = false,
            .dedicated_preferred = false,
            .allocation_kind = Center::Memory::Vulkan::AllocationKind::buffer,
            .lifetime_hint = lifetimeForDomain(domain_),
            .host_access = hostAccessForDomain(domain_),
            .allocation_flags = Center::Memory::Vulkan::allocation_flag_none,
            .buffer_image_granularity = buffer_image_granularity,
        };

        Center::Memory::Vulkan::ResourceAllocator binder{allocator};
        const auto result = binder.allocate_and_bind(request);
        return result.ok() ? VulkanMemoryAllocation{.slice = result.slice} : VulkanMemoryAllocation{};
    }

    VulkanMemoryAllocation allocateAndBindImage(
        VkImage image_,
        const VkMemoryRequirements& requirements_) noexcept
    {
        if (!initialized || image_ == VK_NULL_HANDLE || requirements_.size == 0U) {
            return {};
        }

        Center::Memory::Vulkan::ResourceRequirements request{
            .resource = Center::Memory::VulkanNativeBackend::to_resource_handle(image_),
            .size = static_cast<std::size_t>(requirements_.size),
            .alignment = static_cast<std::size_t>(requirements_.alignment),
            .selector = {
                .memory_type_bits = requirements_.memoryTypeBits,
                .required_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                .preferred_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            },
            .persistent_map = false,
            .dedicated_required = false,
            .dedicated_preferred = false,
            .allocation_kind = Center::Memory::Vulkan::AllocationKind::image_optimal,
            .lifetime_hint = Center::Memory::Vulkan::LifetimeHint::long_lived,
            .host_access = Center::Memory::Vulkan::HostAccess::none,
            .allocation_flags = Center::Memory::Vulkan::allocation_flag_none,
            .buffer_image_granularity = buffer_image_granularity,
        };

        Center::Memory::Vulkan::ResourceAllocator binder{allocator};
        const auto result = binder.allocate_and_bind(request);
        return result.ok() ? VulkanMemoryAllocation{.slice = result.slice} : VulkanMemoryAllocation{};
    }

    void deallocate(VulkanMemoryAllocation& allocation_) noexcept
    {
        if (allocation_.slice.valid()) {
            allocator.deallocate(allocation_.slice);
            allocation_.slice = {};
        }
    }

    bool flushMappedRange(
        const VulkanMemoryAllocation& allocation_,
        U64 offset_,
        U64 byte_count_) noexcept
    {
        return syncMappedRange(allocation_, offset_, byte_count_, true);
    }

    bool invalidateMappedRange(
        const VulkanMemoryAllocation& allocation_,
        U64 offset_,
        U64 byte_count_) noexcept
    {
        return syncMappedRange(allocation_, offset_, byte_count_, false);
    }

    bool isInitialized() const noexcept
    {
        return initialized;
    }

    Center::Memory::Vulkan::VulkanAllocStatus lastStatus() const noexcept
    {
        return allocator.last_failure().status;
    }

    static bool isHostVisibleDomain(NativeMemoryDomain domain_) noexcept
    {
        return domain_ == NativeMemoryDomain::Upload ||
            domain_ == NativeMemoryDomain::Readback ||
            domain_ == NativeMemoryDomain::Transient;
    }

private:
    using VulkanAutoAllocator = Center::Memory::VulkanAutoAdaptor<
        Center::Memory::VulkanNativeProvider,
        Center::Memory::VulkanNativeProvider>;

    static Center::Memory::Vulkan::DeviceProfile makeDeviceProfile(
        const VkPhysicalDeviceProperties& properties_,
        const VkPhysicalDeviceMemoryProperties& memory_properties_) noexcept
    {
        return {
            .device_type = toMemoryCenterDeviceType(properties_.deviceType),
            .is_uma = isUma(memory_properties_),
            .supports_dedicated_allocation = true,
            .host_visible_device_local_overlap = hasHostVisibleDeviceLocalOverlap(memory_properties_),
            .buffer_image_granularity = static_cast<std::size_t>(properties_.limits.bufferImageGranularity),
            .non_coherent_atom_size = static_cast<std::size_t>(properties_.limits.nonCoherentAtomSize),
            .memory_type_count = memory_properties_.memoryTypeCount,
            .heap_count = memory_properties_.memoryHeapCount,
        };
    }

    static Center::Memory::Vulkan::DeviceType toMemoryCenterDeviceType(
        VkPhysicalDeviceType type_) noexcept
    {
        switch (type_) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return Center::Memory::Vulkan::DeviceType::integrated_gpu;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return Center::Memory::Vulkan::DeviceType::discrete_gpu;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return Center::Memory::Vulkan::DeviceType::virtual_gpu;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return Center::Memory::Vulkan::DeviceType::cpu;
        default:
            return Center::Memory::Vulkan::DeviceType::unknown;
        }
    }

    static bool isUma(const VkPhysicalDeviceMemoryProperties& memory_properties_) noexcept
    {
        return memory_properties_.memoryHeapCount <= 1U;
    }

    static bool hasHostVisibleDeviceLocalOverlap(
        const VkPhysicalDeviceMemoryProperties& memory_properties_) noexcept
    {
        for (U32 index = 0U; index < memory_properties_.memoryTypeCount; ++index) {
            const auto flags = memory_properties_.memoryTypes[index].propertyFlags;
            if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0U &&
                (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U) {
                return true;
            }
        }
        return false;
    }

    static U32 memoryFlagsForDomain(NativeMemoryDomain domain_) noexcept
    {
        switch (domain_) {
        case NativeMemoryDomain::DeviceLocal:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case NativeMemoryDomain::Transient:
        case NativeMemoryDomain::Upload:
        case NativeMemoryDomain::Readback:
        case NativeMemoryDomain::Unknown:
        default:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        }
    }

    static U32 preferredMemoryFlagsForDomain(NativeMemoryDomain domain_) noexcept
    {
        switch (domain_) {
        case NativeMemoryDomain::DeviceLocal:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case NativeMemoryDomain::Readback:
            return VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        case NativeMemoryDomain::Upload:
        case NativeMemoryDomain::Transient:
        case NativeMemoryDomain::Unknown:
        default:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
    }

    static Center::Memory::Vulkan::LifetimeHint lifetimeForDomain(
        NativeMemoryDomain domain_) noexcept
    {
        switch (domain_) {
        case NativeMemoryDomain::Transient:
            return Center::Memory::Vulkan::LifetimeHint::frame_local;
        case NativeMemoryDomain::Upload:
        case NativeMemoryDomain::Readback:
            return Center::Memory::Vulkan::LifetimeHint::transient;
        case NativeMemoryDomain::DeviceLocal:
        case NativeMemoryDomain::Unknown:
        default:
            return Center::Memory::Vulkan::LifetimeHint::long_lived;
        }
    }

    static Center::Memory::Vulkan::HostAccess hostAccessForDomain(
        NativeMemoryDomain domain_) noexcept
    {
        switch (domain_) {
        case NativeMemoryDomain::Upload:
        case NativeMemoryDomain::Transient:
            return Center::Memory::Vulkan::HostAccess::sequential_write;
        case NativeMemoryDomain::Readback:
            return Center::Memory::Vulkan::HostAccess::random_read;
        case NativeMemoryDomain::DeviceLocal:
        case NativeMemoryDomain::Unknown:
        default:
            return Center::Memory::Vulkan::HostAccess::none;
        }
    }

    bool syncMappedRange(
        const VulkanMemoryAllocation& allocation_,
        U64 offset_,
        U64 byte_count_,
        bool flush_) noexcept
    {
        if (byte_count_ == 0U) {
            return true;
        }
        if (!initialized || !allocation_.slice.valid()) {
            return false;
        }
        if (offset_ > static_cast<U64>((std::numeric_limits<std::size_t>::max)()) ||
            byte_count_ > static_cast<U64>((std::numeric_limits<std::size_t>::max)())) {
            return false;
        }

        const auto offset = static_cast<std::size_t>(offset_);
        const auto byte_count = static_cast<std::size_t>(byte_count_);
        if (offset > allocation_.slice.size || byte_count > allocation_.slice.size - offset) {
            return false;
        }
        if (offset > (std::numeric_limits<std::size_t>::max)() - allocation_.slice.offset ||
            offset > static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)())) {
            return false;
        }

        Center::Memory::Vulkan::Slice range_slice = allocation_.slice;
        range_slice.offset += offset;
        range_slice.size = byte_count;
        if (range_slice.mapped_ptr != nullptr) {
            range_slice.mapped_ptr += static_cast<std::ptrdiff_t>(offset);
        }
        return flush_ ? allocator.flush_slice(range_slice) : allocator.invalidate_slice(range_slice);
    }

    VulkanAutoAllocator allocator{};
    std::size_t buffer_image_granularity = 1U;
    bool initialized = false;
};

static_assert(std::is_trivial_v<VulkanMemoryCenterConfig>);
static_assert(std::is_standard_layout_v<VulkanMemoryCenterConfig>);
static_assert(std::is_trivially_copyable_v<VulkanMemoryCenterConfig>);
static_assert(std::is_aggregate_v<VulkanMemoryCenterConfig>);

static_assert(std::is_trivially_copyable_v<VulkanMemoryAllocation>);
static_assert(std::is_aggregate_v<VulkanMemoryAllocation>);

} // namespace Render2D
