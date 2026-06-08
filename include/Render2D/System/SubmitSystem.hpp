#pragma once

#include "Render2D/Component/Upload.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Native/NativeComponents.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct SubmitSystem {
    static SystemResult run(
        std::span<const NativeCommandBufferRef<Provider, Dim>> command_buffers_,
        const FrameSync<Provider, Dim>& frame_sync_,
        std::span<NativeSubmitCommand<Provider, Dim>> submit_commands_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (command_buffers_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }
            if (submit_commands_.empty()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(command_buffers_.size()),
                    .write_count = 0U,
                };
            }

            submit_commands_[0U] = {
                .command_first = 0U,
                .command_count = static_cast<U32>(command_buffers_.size()),
                .wait_first = frame_sync_.image_available_semaphore_id,
                .wait_count = 1U,
                .signal_first = frame_sync_.render_finished_semaphore_id,
                .signal_count = 1U,
                .fence_id = frame_sync_.in_flight_fence_id,
                .flags = frame_sync_.flags,
            };

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(command_buffers_.size()),
                .write_count = 1U,
            };
        }
    }
};

} // namespace Render2D
