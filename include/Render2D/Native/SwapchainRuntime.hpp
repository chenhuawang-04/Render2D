#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"


namespace Render2D {

template<class Provider, class Dim>
class NativeSwapchainRuntime {
public:
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

    NativeResult createSwapchainState(
        NativeHandle handle_,
        U32 image_first_,
        U32 image_count_,
        U32 width_,
        U32 height_,
        U32 format_,
        U32 flags_,
        SwapchainState<Provider, Dim>& out_state_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (image_count_ == 0U || width_ == 0U || height_ == 0U) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            U32 swapchain_id = 0U;
            if (!acquireSwapchainSlot(swapchain_id)) {
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            auto& slot = swapchain_slots[swapchain_id];
            slot.occupied = 1U;
            slot.state = {
                .handle = handle_.value,
                .swapchain_id = swapchain_id,
                .image_first = image_first_,
                .image_count = image_count_,
                .width = width_,
                .height = height_,
                .format = format_,
                .generation = slot.generation.value,
                .flags = flags_,
            };
            ++active_swapchain_count;
            out_state_ = slot.state;

            return makeResult(NativeStatusCode::Ok, swapchain_id, slot.generation.value);
        }
    }

    NativeResult resizeSwapchainState(
        const SwapchainState<Provider, Dim>& state_,
        U32 width_,
        U32 height_,
        SwapchainState<Provider, Dim>& out_state_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (width_ == 0U || height_ == 0U) {
                return makeResult(NativeStatusCode::InvalidInput, state_.swapchain_id, state_.generation);
            }

            if (!isLiveSwapchainState(state_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    state_.swapchain_id,
                    state_.generation);
            }

            auto& slot = swapchain_slots[state_.swapchain_id];
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.state.width = width_;
            slot.state.height = height_;
            slot.state.generation = slot.generation.value;
            out_state_ = slot.state;

            return makeResult(
                NativeStatusCode::Ok,
                state_.swapchain_id,
                slot.generation.value);
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
                return makeResult(
                    NativeStatusCode::StaleReference,
                    state_.swapchain_id,
                    state_.generation);
            }

            out_state_ = swapchain_slots[state_.swapchain_id].state;
            return makeResult(
                NativeStatusCode::Ok,
                state_.swapchain_id,
                state_.generation);
        }
    }

    NativeResult releaseSwapchainState(const SwapchainState<Provider, Dim>& state_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLiveSwapchainState(state_)) {
                return makeResult(
                    NativeStatusCode::StaleReference,
                    state_.swapchain_id,
                    state_.generation);
            }

            const NativeStatusCode release_code = pushFreeSwapchainId(state_.swapchain_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, state_.swapchain_id, state_.generation);
            }

            auto& slot = swapchain_slots[state_.swapchain_id];
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.state = {};
            --active_swapchain_count;

            return makeResult(
                NativeStatusCode::Ok,
                state_.swapchain_id,
                slot.generation.value);
        }
    }

    U32 swapchainCount() const noexcept
    {
        return active_swapchain_count;
    }

    U32 swapchainCapacity() const noexcept
    {
        return static_cast<U32>(swapchain_slots.capacity());
    }

private:
    struct SwapchainSlot {
        SwapchainState<Provider, Dim> state;
        NativeGeneration generation;
        U32 occupied;
    };

    static constexpr U32 kFirstGeneration = 1U;

    static NativeResult makeResult(NativeStatusCode code_, U32 id_, U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Swapchain,
            .object_id = {.value = id_},
            .generation = {.value = generation_},
        };
    }

    static U32 nextGeneration(U32 generation_) noexcept
    {
        return generation_ == 0xFFFFFFFFU ? kFirstGeneration : generation_ + 1U;
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
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }

        return true;
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

    McVector<SwapchainSlot> swapchain_slots;
    McVector<U32> free_swapchain_ids;
    U32 active_swapchain_count = 0U;
};

} // namespace Render2D
