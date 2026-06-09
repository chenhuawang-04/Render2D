#pragma once

#include "Render2D/Component/Frame.hpp"
#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

namespace Render2D {

template<class Provider, class Dim>
class NativeFrameRuntime {
public:
    NativeCapacityResult configure(U32 frames_in_flight_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = frames_in_flight_,
                .available_count = 0U,
            };
        } else {
            if (frames_in_flight_ == 0U) {
                return {
                    .code = NativeStatusCode::InvalidInput,
                    .requested_count = frames_in_flight_,
                    .available_count = 0U,
                };
            }

            frames_in_flight = frames_in_flight_;
            current_frame_index = 0U;
            frame_open = 0U;
            return {
                .code = NativeStatusCode::Ok,
                .requested_count = frames_in_flight_,
                .available_count = frames_in_flight,
            };
        }
    }

    NativeResult beginFrame(FrameIndex<Provider, Dim>& out_frame_index_, FrameSync<Provider, Dim>& out_frame_sync_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            if (frames_in_flight == 0U || frame_open != 0U) {
                return makeResult(NativeStatusCode::InvalidInput);
            }

            frame_open = 1U;
            out_frame_index_ = {.value = current_frame_index};
            out_frame_sync_ = {
                .frame_index = current_frame_index,
                .image_available_semaphore_id = currentSlotIndex(),
                .render_finished_semaphore_id = currentSlotIndex(),
                .in_flight_fence_id = currentSlotIndex(),
                .flags = 0U,
                .sync_id = currentSlotIndex(),
                .generation = 1U,
            };
            return makeResult(NativeStatusCode::Ok);
        }
    }

    NativeResult endFrame() noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            if (frames_in_flight == 0U || frame_open == 0U) {
                return makeResult(NativeStatusCode::InvalidInput);
            }

            frame_open = 0U;
            ++current_frame_index;
            return makeResult(NativeStatusCode::Ok);
        }
    }

    U32 currentFrameIndex() const noexcept
    {
        return current_frame_index;
    }

    U32 currentSlotIndex() const noexcept
    {
        return frames_in_flight == 0U ? 0U : current_frame_index % frames_in_flight;
    }

    U32 framesInFlight() const noexcept
    {
        return frames_in_flight;
    }

private:
    static NativeResult makeResult(NativeStatusCode code_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Frame,
            .object_id = {.value = 0U},
            .generation = {.value = 0U},
        };
    }

    U32 frames_in_flight = 0U;
    U32 current_frame_index = 0U;
    U32 frame_open = 0U;
};

} // namespace Render2D
