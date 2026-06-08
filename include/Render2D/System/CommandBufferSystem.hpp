#pragma once

#include "Render2D/Component/Command.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct CommandBufferBuildSystem {
    static SystemResult run(
        U32 frame_index_,
        RangeU32 draw_range_,
        RangeU32 batch_range_,
        RangeU32 upload_range_,
        RangeU32 native_submit_range_,
        std::span<CommandBuffer<Provider, Dim>> command_buffers_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (command_buffers_.empty()) {
                return {.code = SystemStatusCode::InsufficientCapacity, .read_count = 0U, .write_count = 0U};
            }

            command_buffers_[0U] = {
                .frame_index = frame_index_,
                .draw_first = draw_range_.first,
                .draw_count = draw_range_.count,
                .batch_first = batch_range_.first,
                .batch_count = batch_range_.count,
                .upload_first = upload_range_.first,
                .upload_count = upload_range_.count,
                .native_submit_first = native_submit_range_.first,
                .native_submit_count = native_submit_range_.count,
            };

            return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 1U};
        }
    }
};

template<class Provider, class Dim>
struct CommandBufferClearSystem {
    static SystemResult run(std::span<CommandBuffer<Provider, Dim>> command_buffers_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            for (auto& command_buffer : command_buffers_) {
                command_buffer = {};
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(command_buffers_.size()),
                .write_count = static_cast<U32>(command_buffers_.size()),
            };
        }
    }
};

} // namespace Render2D
