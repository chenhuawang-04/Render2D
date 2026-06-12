#pragma once

#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Native/DeferredDestroyRuntime.hpp"
#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"
#include "Render2D/Native/VulkanResourceRuntime.hpp"

#include <vulkan/vulkan.h>

#include <type_traits>

namespace Render2D {

template<class Provider, class Dim>
struct VulkanTextureAtlasRuntimeConfig {
    VulkanResourceRuntime<Provider, Dim>* resource_runtime;
};

struct VulkanAtlasImageConfig {
    U32 width;
    U32 height;
    U32 format;
    U32 flags;
};

struct VulkanAtlasImageRef {
    U32 atlas_id;
    U32 generation;
    U32 width;
    U32 height;
    U32 format;
    U32 flags;
};

// VulkanTextureAtlasRuntime owns atlas image slots behind id + generation.
// The backing sampled image lifetime is delegated to VulkanResourceRuntime
// (MemoryCenter-backed); this runtime only records which managed image backs
// each atlas slot and rejects stale references after slot reuse. ECS-facing
// atlas identity stays in TextureAtlasRegion; VulkanAtlasImageRef is a runtime
// handle, not an ECS component.
//
// Lifetime: the bound VulkanResourceRuntime must outlive this runtime, because
// shutdown() and releaseAtlasImage() release the backing image through it.
template<class Provider, class Dim>
class VulkanTextureAtlasRuntime {
public:
    VulkanTextureAtlasRuntime() = default;
    VulkanTextureAtlasRuntime(const VulkanTextureAtlasRuntime&) = delete;
    VulkanTextureAtlasRuntime& operator=(const VulkanTextureAtlasRuntime&) = delete;
    VulkanTextureAtlasRuntime(VulkanTextureAtlasRuntime&&) = delete;
    VulkanTextureAtlasRuntime& operator=(VulkanTextureAtlasRuntime&&) = delete;

    ~VulkanTextureAtlasRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanTextureAtlasRuntimeConfig<Provider, Dim> config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (config_.resource_runtime == nullptr || resource_runtime != nullptr) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            resource_runtime = config_.resource_runtime;
            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    void shutdown() noexcept
    {
        if (resource_runtime != nullptr) {
            for (auto& slot : atlas_slots) {
                if (slot.occupied != 0U) {
                    resource_runtime->releaseImageRef(slot.image);
                }
            }
        }

        atlas_slots.clear();
        free_atlas_ids.clear();
        active_atlas_count = 0U;
        resource_runtime = nullptr;
    }

    NativeCapacityResult reserveAtlases(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                atlas_slots.reserve(capacity_);
                free_atlas_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(atlas_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(atlas_slots.capacity()),
            };
        }
    }

    NativeResult createAtlasImage(
        const VulkanAtlasImageConfig& config_,
        VulkanAtlasImageRef& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized() ||
                config_.width == 0U ||
                config_.height == 0U ||
                config_.format == static_cast<U32>(VK_FORMAT_UNDEFINED)) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }
            if (!hasAvailableAtlasSlot()) {
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            ImageRef<Provider, Dim> image{};
            const U32 usage_flags =
                static_cast<U32>(VK_IMAGE_USAGE_SAMPLED_BIT) |
                static_cast<U32>(VK_IMAGE_USAGE_TRANSFER_DST_BIT) |
                static_cast<U32>(VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            const NativeResult image_result = resource_runtime->createImageRef(
                config_.width,
                config_.height,
                config_.format,
                usage_flags,
                image);
            if (image_result.code != NativeStatusCode::Ok) {
                return image_result;
            }

            U32 atlas_id = 0U;
            if (!acquireAtlasSlot(atlas_id)) {
                resource_runtime->releaseImageRef(image);
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            auto& slot = atlas_slots[atlas_id];
            slot.image = image;
            slot.flags = config_.flags;
            slot.occupied = 1U;
            ++active_atlas_count;
            out_ref_ = {
                .atlas_id = atlas_id,
                .generation = slot.generation.value,
                .width = image.width,
                .height = image.height,
                .format = image.format,
                .flags = config_.flags,
            };
            return makeResult(NativeStatusCode::Ok, atlas_id, slot.generation.value);
        }
    }

    NativeResult resolveImageRef(
        const VulkanAtlasImageRef& ref_,
        ImageRef<Provider, Dim>& out_image_) const noexcept
    {
        if (!isLiveAtlasRef(ref_)) {
            out_image_ = {};
            return makeResult(NativeStatusCode::StaleReference, ref_.atlas_id, ref_.generation);
        }

        out_image_ = atlas_slots[ref_.atlas_id].image;
        return makeResult(NativeStatusCode::Ok, ref_.atlas_id, ref_.generation);
    }

    NativeResult recordUploadRegion(
        VkCommandBuffer command_buffer_,
        const VulkanAtlasImageRef& atlas_ref_,
        const BufferRef<Provider, Dim>& source_,
        U64 source_offset_,
        U32 region_x_,
        U32 region_y_,
        U32 region_width_,
        U32 region_height_) const
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, atlas_ref_.atlas_id, atlas_ref_.generation);
        } else {
            if (!isInitialized()) {
                return makeResult(NativeStatusCode::InvalidInput, atlas_ref_.atlas_id, atlas_ref_.generation);
            }
            if (!isLiveAtlasRef(atlas_ref_)) {
                return makeResult(NativeStatusCode::StaleReference, atlas_ref_.atlas_id, atlas_ref_.generation);
            }

            return resource_runtime->recordCopyBufferToImageRegion(
                command_buffer_,
                source_,
                atlas_slots[atlas_ref_.atlas_id].image,
                source_offset_,
                region_x_,
                region_y_,
                region_width_,
                region_height_);
        }
    }

    NativeResult releaseAtlasImage(const VulkanAtlasImageRef& ref_)
    {
        if (!isInitialized()) {
            return makeResult(NativeStatusCode::InvalidInput, ref_.atlas_id, ref_.generation);
        }
        if (!isLiveAtlasRef(ref_)) {
            return makeResult(NativeStatusCode::StaleReference, ref_.atlas_id, ref_.generation);
        }

        const NativeStatusCode release_code = pushFreeAtlasId(ref_.atlas_id);
        if (release_code != NativeStatusCode::Ok) {
            return makeResult(release_code, ref_.atlas_id, ref_.generation);
        }

        auto& slot = atlas_slots[ref_.atlas_id];
        resource_runtime->releaseImageRef(slot.image);
        slot.image = {};
        slot.flags = 0U;
        slot.occupied = 0U;
        slot.generation.value = nextGeneration(slot.generation.value);
        --active_atlas_count;
        return makeResult(NativeStatusCode::Ok, ref_.atlas_id, slot.generation.value);
    }

    // Frame-safe retirement: free the atlas handle now (its slot/id can be
    // reused immediately) but hand the backing image to the deferred-destroy
    // queue so it survives any in-flight frames. The caller drains the queue
    // once the retire frame completes and then releases each backing image
    // through the resource runtime.
    NativeResult retireAtlasImage(
        const VulkanAtlasImageRef& ref_,
        NativeDeferredDestroyRuntime<Provider, Dim>& deferred_runtime_,
        U32 current_frame_index_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, ref_.atlas_id, ref_.generation);
        } else {
            if (!isInitialized()) {
                return makeResult(NativeStatusCode::InvalidInput, ref_.atlas_id, ref_.generation);
            }
            if (!isLiveAtlasRef(ref_)) {
                return makeResult(NativeStatusCode::StaleReference, ref_.atlas_id, ref_.generation);
            }

            const ImageRef<Provider, Dim> image = atlas_slots[ref_.atlas_id].image;
            const NativeStatusCode release_code = pushFreeAtlasId(ref_.atlas_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, ref_.atlas_id, ref_.generation);
            }

            auto& slot = atlas_slots[ref_.atlas_id];
            slot.image = {};
            slot.flags = 0U;
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            --active_atlas_count;

            const DeferredDestroyCommand<Provider, Dim> command{
                .handle = image.image_handle,
                .aux_handle = image.image_view_handle,
                .object_kind = static_cast<U32>(NativeObjectKind::Image),
                .object_id = image.image_id,
                .generation = image.generation,
                .retire_frame_index = 0U,
                .flags = 0U,
                .reserved = 0U,
            };
            const NativeResult enqueue_result =
                deferred_runtime_.enqueueAfterSafeLag(command, current_frame_index_);
            if (enqueue_result.code != NativeStatusCode::Ok) {
                return enqueue_result;
            }
            return makeResult(NativeStatusCode::Ok, ref_.atlas_id, slot.generation.value);
        }
    }

    bool isInitialized() const noexcept
    {
        return resource_runtime != nullptr;
    }

    U32 atlasCount() const noexcept
    {
        return active_atlas_count;
    }

    U32 atlasCapacity() const noexcept
    {
        return static_cast<U32>(atlas_slots.capacity());
    }

private:
    struct AtlasSlot {
        ImageRef<Provider, Dim> image;
        NativeGeneration generation;
        U32 flags;
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
            .object_kind = NativeObjectKind::Image,
            .object_id = {.value = object_id_},
            .generation = {.value = generation_},
        };
    }

    static U32 nextGeneration(U32 generation_) noexcept
    {
        return generation_ == 0xFFFFFFFFU ? kFirstGeneration : generation_ + 1U;
    }

    bool hasAvailableAtlasSlot() const noexcept
    {
        return !free_atlas_ids.empty() || atlas_slots.size() < atlas_slots.capacity();
    }

    bool acquireAtlasSlot(U32& out_atlas_id_)
    {
        if (!free_atlas_ids.empty()) {
            out_atlas_id_ = free_atlas_ids.back();
            free_atlas_ids.pop_back();
            return true;
        }
        if (atlas_slots.size() >= atlas_slots.capacity()) {
            return false;
        }

        try {
            out_atlas_id_ = static_cast<U32>(atlas_slots.size());
            atlas_slots.push_back({
                .image = {},
                .generation = {.value = kFirstGeneration},
                .flags = 0U,
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }
        return true;
    }

    NativeStatusCode pushFreeAtlasId(U32 atlas_id_)
    {
        if (free_atlas_ids.size() >= free_atlas_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }
        try {
            free_atlas_ids.push_back(atlas_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }
        return NativeStatusCode::Ok;
    }

    bool isLiveAtlasRef(const VulkanAtlasImageRef& ref_) const noexcept
    {
        if (ref_.atlas_id >= atlas_slots.size()) {
            return false;
        }

        const auto& slot = atlas_slots[ref_.atlas_id];
        return slot.occupied != 0U && slot.generation.value == ref_.generation;
    }

    McVector<AtlasSlot> atlas_slots;
    McVector<U32> free_atlas_ids;
    VulkanResourceRuntime<Provider, Dim>* resource_runtime = nullptr;
    U32 active_atlas_count = 0U;
};

static_assert(std::is_trivial_v<VulkanAtlasImageConfig>);
static_assert(std::is_standard_layout_v<VulkanAtlasImageConfig>);
static_assert(std::is_trivially_copyable_v<VulkanAtlasImageConfig>);
static_assert(std::is_aggregate_v<VulkanAtlasImageConfig>);

static_assert(std::is_trivial_v<VulkanAtlasImageRef>);
static_assert(std::is_standard_layout_v<VulkanAtlasImageRef>);
static_assert(std::is_trivially_copyable_v<VulkanAtlasImageRef>);
static_assert(std::is_aggregate_v<VulkanAtlasImageRef>);

} // namespace Render2D
