#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Component/Frame.hpp"
#include "Render2D/Native/NativeResult.hpp"


namespace Render2D {

template<class Provider, class Dim>
class NativeDescriptorRuntime {
public:
    NativeCapacityResult reserveDescriptorSets(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                descriptor_slots.reserve(capacity_);
                free_descriptor_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(descriptor_slots.capacity()),
                };
            }
            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(descriptor_slots.capacity()),
            };
        }
    }

    NativeResult allocateDescriptorSlice(U32 first_, U32 count_, DescriptorSlice<Provider, Dim>& out_slice_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (count_ == 0U) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            U32 id = 0U;
            if (!acquireSlot(id)) {
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }
            auto& slot = descriptor_slots[id];
            slot.occupied = 1U;
            slot.slice = {
                .descriptor_set_id = id,
                .first = first_,
                .count = count_,
                .generation = slot.generation.value,
            };
            ++active_count;
            out_slice_ = slot.slice;
            return makeResult(NativeStatusCode::Ok, id, slot.generation.value);
        }
    }

    NativeResult resolveDescriptorSlice(const DescriptorSlice<Provider, Dim>& slice_, DescriptorSlice<Provider, Dim>& out_slice_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLive(slice_)) {
                return makeResult(NativeStatusCode::StaleReference, slice_.descriptor_set_id, slice_.generation);
            }
            out_slice_ = descriptor_slots[slice_.descriptor_set_id].slice;
            return makeResult(NativeStatusCode::Ok, slice_.descriptor_set_id, slice_.generation);
        }
    }

    NativeResult releaseDescriptorSlice(const DescriptorSlice<Provider, Dim>& slice_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLive(slice_)) {
                return makeResult(NativeStatusCode::StaleReference, slice_.descriptor_set_id, slice_.generation);
            }
            const NativeStatusCode release_code = releaseId(slice_.descriptor_set_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, slice_.descriptor_set_id, slice_.generation);
            }
            auto& slot = descriptor_slots[slice_.descriptor_set_id];
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.slice = {};
            --active_count;
            return makeResult(NativeStatusCode::Ok, slice_.descriptor_set_id, slot.generation.value);
        }
    }

    U32 descriptorCount() const noexcept
    {
        return active_count;
    }

    U32 descriptorCapacity() const noexcept
    {
        return static_cast<U32>(descriptor_slots.capacity());
    }

private:
    struct DescriptorSlot {
        DescriptorSlice<Provider, Dim> slice;
        NativeGeneration generation;
        U32 occupied;
    };

    static constexpr U32 kFirstGeneration = 1U;

    static U32 nextGeneration(U32 generation_) noexcept
    {
        return generation_ == 0xFFFFFFFFU ? kFirstGeneration : generation_ + 1U;
    }

    static NativeResult makeResult(NativeStatusCode code_, U32 id_, U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Descriptor,
            .object_id = {.value = id_},
            .generation = {.value = generation_},
        };
    }

    bool acquireSlot(U32& out_id_)
    {
        if (!free_descriptor_ids.empty()) {
            out_id_ = free_descriptor_ids.back();
            free_descriptor_ids.pop_back();
            return true;
        }
        if (descriptor_slots.size() >= descriptor_slots.capacity()) {
            return false;
        }
        try {
            out_id_ = static_cast<U32>(descriptor_slots.size());
            descriptor_slots.push_back({
                .slice = {},
                .generation = {.value = kFirstGeneration},
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }
        return true;
    }

    NativeStatusCode releaseId(U32 id_)
    {
        if (free_descriptor_ids.size() >= free_descriptor_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }
        try {
            free_descriptor_ids.push_back(id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }
        return NativeStatusCode::Ok;
    }

    bool isLive(const DescriptorSlice<Provider, Dim>& slice_) const noexcept
    {
        return slice_.descriptor_set_id < descriptor_slots.size() &&
            descriptor_slots[slice_.descriptor_set_id].occupied != 0U &&
            descriptor_slots[slice_.descriptor_set_id].generation.value == slice_.generation;
    }

    McVector<DescriptorSlot> descriptor_slots;
    McVector<U32> free_descriptor_ids;
    U32 active_count = 0U;
};

} // namespace Render2D
