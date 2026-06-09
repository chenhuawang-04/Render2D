#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"


namespace Render2D {

template<class Provider, class Dim>
class NativeCommandRuntime {
public:
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
                command_buffer_slots.reserve(capacity_);
                free_command_buffer_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(command_buffer_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(command_buffer_slots.capacity()),
            };
        }
    }

    NativeResult createCommandBufferRef(
        U32 frame_index_,
        RangeU32 batch_range_,
        RangeU32 upload_range_,
        U32 flags_,
        NativeCommandBufferRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            U32 command_buffer_id = 0U;
            if (!acquireCommandBufferSlot(command_buffer_id)) {
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            auto& slot = command_buffer_slots[command_buffer_id];
            slot.occupied = 1U;
            slot.ref = {
                .command_buffer_id = command_buffer_id,
                .generation = slot.generation.value,
                .frame_index = frame_index_,
                .batch_first = batch_range_.first,
                .batch_count = batch_range_.count,
                .upload_first = upload_range_.first,
                .upload_count = upload_range_.count,
                .flags = flags_,
            };
            ++active_command_buffer_count;
            out_ref_ = slot.ref;

            return makeResult(
                NativeStatusCode::Ok,
                command_buffer_id,
                slot.generation.value);
        }
    }

    NativeResult resolveCommandBufferRef(
        const NativeCommandBufferRef<Provider, Dim>& ref_,
        NativeCommandBufferRef<Provider, Dim>& out_ref_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveCommandBufferRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    ref_.command_buffer_id,
                    ref_.generation);
            }

            out_ref_ = command_buffer_slots[ref_.command_buffer_id].ref;
            return makeResult(
                NativeStatusCode::Ok,
                ref_.command_buffer_id,
                ref_.generation);
        }
    }

    NativeResult releaseCommandBufferRef(const NativeCommandBufferRef<Provider, Dim>& ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveCommandBufferRef(ref_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    ref_.command_buffer_id,
                    ref_.generation);
            }

            const NativeStatusCode release_code = pushFreeCommandBufferId(ref_.command_buffer_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, ref_.command_buffer_id, ref_.generation);
            }

            auto& slot = command_buffer_slots[ref_.command_buffer_id];
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.ref = {};
            --active_command_buffer_count;

            return makeResult(
                NativeStatusCode::Ok,
                ref_.command_buffer_id,
                slot.generation.value);
        }
    }

    U32 commandBufferCount() const noexcept
    {
        return active_command_buffer_count;
    }

    U32 commandBufferCapacity() const noexcept
    {
        return static_cast<U32>(command_buffer_slots.capacity());
    }

private:
    struct CommandBufferSlot {
        NativeCommandBufferRef<Provider, Dim> ref;
        NativeGeneration generation;
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
            .object_kind = NativeObjectKind::CommandBuffer,
            .object_id = {.value = object_id_},
            .generation = {.value = generation_},
        };
    }

    static U32 nextGeneration(U32 generation_) noexcept
    {
        return generation_ == 0xFFFFFFFFU ? kFirstGeneration : generation_ + 1U;
    }

    bool acquireCommandBufferSlot(U32& out_command_buffer_id_)
    {
        if (!free_command_buffer_ids.empty()) {
            out_command_buffer_id_ = free_command_buffer_ids.back();
            free_command_buffer_ids.pop_back();
            return true;
        }

        if (command_buffer_slots.size() >= command_buffer_slots.capacity()) {
            return false;
        }

        try {
            out_command_buffer_id_ = static_cast<U32>(command_buffer_slots.size());
            command_buffer_slots.push_back({
                .ref = {},
                .generation = {.value = kFirstGeneration},
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }

        return true;
    }

    NativeStatusCode pushFreeCommandBufferId(U32 command_buffer_id_)
    {
        if (free_command_buffer_ids.size() >= free_command_buffer_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }

        try {
            free_command_buffer_ids.push_back(command_buffer_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }

        return NativeStatusCode::Ok;
    }

    bool isLiveCommandBufferRef(const NativeCommandBufferRef<Provider, Dim>& ref_) const noexcept
    {
        if (ref_.command_buffer_id >= command_buffer_slots.size()) {
            return false;
        }

        const auto& slot = command_buffer_slots[ref_.command_buffer_id];
        return slot.occupied != 0U && slot.generation.value == ref_.generation;
    }

    McVector<CommandBufferSlot> command_buffer_slots;
    McVector<U32> free_command_buffer_ids;
    U32 active_command_buffer_count = 0U;
};

} // namespace Render2D
