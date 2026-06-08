#pragma once

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vector>

namespace Render2D {

template<class Provider, class Dim>
class NativeResourceRuntime {
public:
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
        NativeHandle handle_,
        U64 byte_size_,
        U32 usage_flags_,
        NativeMemoryDomain memory_domain_,
        BufferRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Buffer, 0U, 0U);
        } else {
            U32 buffer_id = 0U;
            if (!acquireBufferSlot(buffer_id)) {
                return makeResult(NativeStatusCode::OutOfCapacity, NativeObjectKind::Buffer, 0U, 0U);
            }

            auto& slot = buffer_slots[buffer_id];
            slot.occupied = 1U;
            slot.ref = {
                .handle = handle_.value,
                .byte_size = byte_size_,
                .buffer_id = buffer_id,
                .generation = slot.generation.value,
                .usage_flags = usage_flags_,
                .memory_domain = static_cast<U32>(memory_domain_),
            };
            ++active_buffer_count;
            out_ref_ = slot.ref;

            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Buffer,
                buffer_id,
                slot.generation.value);
        }
    }

    NativeResult createImageRef(
        NativeHandle image_handle_,
        NativeHandle image_view_handle_,
        U32 width_,
        U32 height_,
        U32 format_,
        U32 usage_flags_,
        ImageRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Image, 0U, 0U);
        } else {
            U32 image_id = 0U;
            if (!acquireImageSlot(image_id)) {
                return makeResult(NativeStatusCode::OutOfCapacity, NativeObjectKind::Image, 0U, 0U);
            }

            auto& slot = image_slots[image_id];
            slot.occupied = 1U;
            slot.ref = {
                .image_handle = image_handle_.value,
                .image_view_handle = image_view_handle_.value,
                .image_id = image_id,
                .width = width_,
                .height = height_,
                .format = format_,
                .generation = slot.generation.value,
                .usage_flags = usage_flags_,
            };
            ++active_image_count;
            out_ref_ = slot.ref;

            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Image,
                image_id,
                slot.generation.value);
        }
    }

    NativeResult resolveBufferRef(
        const BufferRef<Provider, Dim>& ref_,
        BufferRef<Provider, Dim>& out_ref_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Buffer, 0U, 0U);
        } else {
            if (!isLiveBufferRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    NativeObjectKind::Buffer,
                    ref_.buffer_id,
                    ref_.generation);
            }

            out_ref_ = buffer_slots[ref_.buffer_id].ref;
            return makeResult(NativeStatusCode::Ok, NativeObjectKind::Buffer, ref_.buffer_id, ref_.generation);
        }
    }

    NativeResult resolveImageRef(
        const ImageRef<Provider, Dim>& ref_,
        ImageRef<Provider, Dim>& out_ref_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Image, 0U, 0U);
        } else {
            if (!isLiveImageRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    NativeObjectKind::Image,
                    ref_.image_id,
                    ref_.generation);
            }

            out_ref_ = image_slots[ref_.image_id].ref;
            return makeResult(NativeStatusCode::Ok, NativeObjectKind::Image, ref_.image_id, ref_.generation);
        }
    }

    NativeResult releaseBufferRef(const BufferRef<Provider, Dim>& ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Buffer, 0U, 0U);
        } else {
            if (!isLiveBufferRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    NativeObjectKind::Buffer,
                    ref_.buffer_id,
                    ref_.generation);
            }

            if (free_buffer_ids.size() >= free_buffer_ids.capacity()) {
                return makeResult(
                    NativeStatusCode::OutOfCapacity,
                    NativeObjectKind::Buffer,
                    ref_.buffer_id,
                    ref_.generation);
            }

            try {
                free_buffer_ids.push_back(ref_.buffer_id);
            } catch (...) {
                return makeResult(
                    NativeStatusCode::OutOfMemory,
                    NativeObjectKind::Buffer,
                    ref_.buffer_id,
                    ref_.generation);
            }

            auto& slot = buffer_slots[ref_.buffer_id];
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.ref = {};
            --active_buffer_count;

            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Buffer,
                ref_.buffer_id,
                slot.generation.value);
        }
    }

    NativeResult releaseImageRef(const ImageRef<Provider, Dim>& ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Image, 0U, 0U);
        } else {
            if (!isLiveImageRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    NativeObjectKind::Image,
                    ref_.image_id,
                    ref_.generation);
            }

            if (free_image_ids.size() >= free_image_ids.capacity()) {
                return makeResult(
                    NativeStatusCode::OutOfCapacity,
                    NativeObjectKind::Image,
                    ref_.image_id,
                    ref_.generation);
            }

            try {
                free_image_ids.push_back(ref_.image_id);
            } catch (...) {
                return makeResult(
                    NativeStatusCode::OutOfMemory,
                    NativeObjectKind::Image,
                    ref_.image_id,
                    ref_.generation);
            }

            auto& slot = image_slots[ref_.image_id];
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.ref = {};
            --active_image_count;

            return makeResult(
                NativeStatusCode::Ok,
                NativeObjectKind::Image,
                ref_.image_id,
                slot.generation.value);
        }
    }

    U32 bufferCount() const noexcept
    {
        return active_buffer_count;
    }

    U32 imageCount() const noexcept
    {
        return active_image_count;
    }

    U32 bufferCapacity() const noexcept
    {
        return static_cast<U32>(buffer_slots.capacity());
    }

    U32 imageCapacity() const noexcept
    {
        return static_cast<U32>(image_slots.capacity());
    }

private:
    struct BufferSlot {
        BufferRef<Provider, Dim> ref;
        NativeGeneration generation;
        U32 occupied;
    };

    struct ImageSlot {
        ImageRef<Provider, Dim> ref;
        NativeGeneration generation;
        U32 occupied;
    };

    static constexpr U32 kFirstGeneration = 1U;

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

    static U32 nextGeneration(U32 generation_) noexcept
    {
        return generation_ == 0xFFFFFFFFU ? kFirstGeneration : generation_ + 1U;
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
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }

        return true;
    }

    bool isLiveBufferRef(const BufferRef<Provider, Dim>& ref_) const noexcept
    {
        if (ref_.buffer_id >= buffer_slots.size()) {
            return false;
        }

        const auto& slot = buffer_slots[ref_.buffer_id];
        return slot.occupied != 0U && slot.generation.value == ref_.generation;
    }

    bool isLiveImageRef(const ImageRef<Provider, Dim>& ref_) const noexcept
    {
        if (ref_.image_id >= image_slots.size()) {
            return false;
        }

        const auto& slot = image_slots[ref_.image_id];
        return slot.occupied != 0U && slot.generation.value == ref_.generation;
    }

    std::vector<BufferSlot> buffer_slots;
    std::vector<ImageSlot> image_slots;
    std::vector<U32> free_buffer_ids;
    std::vector<U32> free_image_ids;
    U32 active_buffer_count = 0U;
    U32 active_image_count = 0U;
};

} // namespace Render2D
