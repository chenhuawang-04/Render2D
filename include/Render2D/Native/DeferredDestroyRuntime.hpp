#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <span>
#include <type_traits>

namespace Render2D {

inline constexpr U32 kNativeDeferredDestroyNoFlags = 0U;

struct NativeDeferredDestroyRuntimeConfig {
    U32 safe_frame_lag;
};

struct NativeDeferredDestroyDrainResult {
    NativeStatusCode code;
    U32 ready_count;
    U32 drained_count;
    U32 pending_count;
};

template<class Provider, class Dim>
class NativeDeferredDestroyRuntime {
public:
    NativeDeferredDestroyRuntime() = default;
    NativeDeferredDestroyRuntime(const NativeDeferredDestroyRuntime&) = delete;
    NativeDeferredDestroyRuntime& operator=(const NativeDeferredDestroyRuntime&) = delete;
    NativeDeferredDestroyRuntime(NativeDeferredDestroyRuntime&&) = delete;
    NativeDeferredDestroyRuntime& operator=(NativeDeferredDestroyRuntime&&) = delete;

    NativeResult configure(NativeDeferredDestroyRuntimeConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            safe_frame_lag = config_.safe_frame_lag;
            return makeResult(NativeStatusCode::Ok);
        }
    }

    NativeCapacityResult reserveDestroyCommands(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                pending_commands.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(pending_commands.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(pending_commands.capacity()),
            };
        }
    }

    NativeResult enqueue(const DeferredDestroyCommand<Provider, Dim>& command_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            if (!isValidCommand(command_)) {
                return makeResult(NativeStatusCode::InvalidInput);
            }
            if (pending_commands.size() >= pending_commands.capacity()) {
                return makeResult(NativeStatusCode::OutOfCapacity);
            }

            try {
                pending_commands.push_back(command_);
            } catch (...) {
                return makeResult(NativeStatusCode::OutOfMemory);
            }

            return makeResult(NativeStatusCode::Ok);
        }
    }

    NativeResult enqueueAfterSafeLag(
        DeferredDestroyCommand<Provider, Dim> command_,
        U32 current_frame_index_)
    {
        command_.retire_frame_index = current_frame_index_ + safe_frame_lag;
        return enqueue(command_);
    }

    NativeDeferredDestroyDrainResult drainReady(
        U32 completed_frame_index_,
        std::span<DeferredDestroyCommand<Provider, Dim>> out_commands_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeDrainResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            const U32 ready_count = countReady(completed_frame_index_);
            if (out_commands_.size() < ready_count) {
                return makeDrainResult(NativeStatusCode::OutOfCapacity, ready_count, 0U);
            }

            U32 drained_count = 0U;
            Usize write_index = 0U;
            for (Usize read_index = 0U; read_index < pending_commands.size(); ++read_index) {
                const auto& command = pending_commands[read_index];
                if (isFrameReached(completed_frame_index_, command.retire_frame_index)) {
                    out_commands_[drained_count] = command;
                    ++drained_count;
                } else {
                    if (write_index != read_index) {
                        pending_commands[write_index] = command;
                    }
                    ++write_index;
                }
            }

            pending_commands.resize(write_index);
            return makeDrainResult(NativeStatusCode::Ok, ready_count, drained_count);
        }
    }

    void clear() noexcept
    {
        pending_commands.clear();
    }

    U32 pendingCount() const noexcept
    {
        return static_cast<U32>(pending_commands.size());
    }

    U32 pendingCapacity() const noexcept
    {
        return static_cast<U32>(pending_commands.capacity());
    }

    U32 safeFrameLag() const noexcept
    {
        return safe_frame_lag;
    }

private:
    static NativeResult makeResult(NativeStatusCode code_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Unknown,
            .object_id = {.value = 0U},
            .generation = {.value = 0U},
        };
    }

    NativeDeferredDestroyDrainResult makeDrainResult(
        NativeStatusCode code_,
        U32 ready_count_,
        U32 drained_count_) const noexcept
    {
        return {
            .code = code_,
            .ready_count = ready_count_,
            .drained_count = drained_count_,
            .pending_count = static_cast<U32>(pending_commands.size()),
        };
    }

    static bool isValidCommand(const DeferredDestroyCommand<Provider, Dim>& command_) noexcept
    {
        return command_.object_kind != static_cast<U32>(NativeObjectKind::Unknown) &&
            command_.generation != 0U;
    }

    U32 countReady(U32 completed_frame_index_) const noexcept
    {
        U32 ready_count = 0U;
        for (const auto& command : pending_commands) {
            if (isFrameReached(completed_frame_index_, command.retire_frame_index)) {
                ++ready_count;
            }
        }
        return ready_count;
    }

    static bool isFrameReached(U32 completed_frame_index_, U32 retire_frame_index_) noexcept
    {
        return static_cast<U32>(completed_frame_index_ - retire_frame_index_) < 0x80000000U;
    }

    McVector<DeferredDestroyCommand<Provider, Dim>> pending_commands;
    U32 safe_frame_lag = 0U;
};

static_assert(std::is_trivial_v<NativeDeferredDestroyRuntimeConfig>);
static_assert(std::is_standard_layout_v<NativeDeferredDestroyRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<NativeDeferredDestroyRuntimeConfig>);
static_assert(std::is_aggregate_v<NativeDeferredDestroyRuntimeConfig>);

static_assert(std::is_trivial_v<NativeDeferredDestroyDrainResult>);
static_assert(std::is_standard_layout_v<NativeDeferredDestroyDrainResult>);
static_assert(std::is_trivially_copyable_v<NativeDeferredDestroyDrainResult>);
static_assert(std::is_aggregate_v<NativeDeferredDestroyDrainResult>);

} // namespace Render2D
