#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"


namespace Render2D {

template<class Provider, class Dim>
class NativePipelineRuntime {
public:
    NativeCapacityResult reservePipelines(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                pipeline_slots.reserve(capacity_);
                free_pipeline_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(pipeline_slots.capacity()),
                };
            }
            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(pipeline_slots.capacity()),
            };
        }
    }

    NativeResult createPipelineRef(NativeHandle handle_, NativeHandle layout_handle_, U32 flags_, PipelineRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            U32 id = 0U;
            if (!acquireSlot(id)) {
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }
            auto& slot = pipeline_slots[id];
            slot.occupied = 1U;
            slot.ref = {
                .handle = handle_.value,
                .layout_handle = layout_handle_.value,
                .pipeline_id = id,
                .generation = slot.generation.value,
                .flags = flags_,
            };
            ++active_count;
            out_ref_ = slot.ref;
            return makeResult(NativeStatusCode::Ok, id, slot.generation.value);
        }
    }

    NativeResult resolvePipelineRef(const PipelineRef<Provider, Dim>& ref_, PipelineRef<Provider, Dim>& out_ref_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLive(ref_)) {
                return makeResult(NativeStatusCode::StaleReference, ref_.pipeline_id, ref_.generation);
            }
            out_ref_ = pipeline_slots[ref_.pipeline_id].ref;
            return makeResult(NativeStatusCode::Ok, ref_.pipeline_id, ref_.generation);
        }
    }

    NativeResult releasePipelineRef(const PipelineRef<Provider, Dim>& ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLive(ref_)) {
                return makeResult(NativeStatusCode::StaleReference, ref_.pipeline_id, ref_.generation);
            }
            const NativeStatusCode release_code = releaseId(ref_.pipeline_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, ref_.pipeline_id, ref_.generation);
            }
            auto& slot = pipeline_slots[ref_.pipeline_id];
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.ref = {};
            --active_count;
            return makeResult(NativeStatusCode::Ok, ref_.pipeline_id, slot.generation.value);
        }
    }

    U32 pipelineCount() const noexcept
    {
        return active_count;
    }

    U32 pipelineCapacity() const noexcept
    {
        return static_cast<U32>(pipeline_slots.capacity());
    }

private:
    struct PipelineSlot {
        PipelineRef<Provider, Dim> ref;
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
            .object_kind = NativeObjectKind::Pipeline,
            .object_id = {.value = id_},
            .generation = {.value = generation_},
        };
    }

    bool acquireSlot(U32& out_id_)
    {
        if (!free_pipeline_ids.empty()) {
            out_id_ = free_pipeline_ids.back();
            free_pipeline_ids.pop_back();
            return true;
        }
        if (pipeline_slots.size() >= pipeline_slots.capacity()) {
            return false;
        }
        try {
            out_id_ = static_cast<U32>(pipeline_slots.size());
            pipeline_slots.push_back({
                .ref = {},
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
        if (free_pipeline_ids.size() >= free_pipeline_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }
        try {
            free_pipeline_ids.push_back(id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }
        return NativeStatusCode::Ok;
    }

    bool isLive(const PipelineRef<Provider, Dim>& ref_) const noexcept
    {
        return ref_.pipeline_id < pipeline_slots.size() &&
            pipeline_slots[ref_.pipeline_id].occupied != 0U &&
            pipeline_slots[ref_.pipeline_id].generation.value == ref_.generation;
    }

    McVector<PipelineSlot> pipeline_slots;
    McVector<U32> free_pipeline_ids;
    U32 active_count = 0U;
};

} // namespace Render2D
