#pragma once

#include "Render2D/Component/Batch.hpp"
#include "Render2D/Component/Command.hpp"
#include "Render2D/Component/Upload.hpp"
#include "Render2D/Native/CommandRuntime.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct EncodeSystem {
    static NativeResult run(
        const CommandBuffer<Provider, Dim>& command_buffer_,
        std::span<const BatchCommand<Provider, Dim>> batch_commands_,
        std::span<const UploadCommand<Provider, Dim>> upload_commands_,
        NativeCommandRuntime<Provider, Dim>& command_runtime_,
        NativeCommandBufferRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain);
        } else {
            const RangeU32 batch_range{
                .first = command_buffer_.batch_first,
                .count = command_buffer_.batch_count,
            };
            const RangeU32 upload_range{
                .first = command_buffer_.upload_first,
                .count = command_buffer_.upload_count,
            };

            if (!isRangeInside(batch_range, batch_commands_.size()) ||
                !isRangeInside(upload_range, upload_commands_.size())) {
                return makeResult(NativeStatusCode::InvalidInput);
            }

            return command_runtime_.createCommandBufferRef(
                command_buffer_.frame_index,
                batch_range,
                upload_range,
                0U,
                out_ref_);
        }
    }

private:
    static NativeResult makeResult(NativeStatusCode code_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::CommandBuffer,
            .object_id = {.value = 0U},
            .generation = {.value = 0U},
        };
    }

    static bool isRangeInside(RangeU32 range_, Usize size_) noexcept
    {
        return range_.first <= size_ &&
            range_.count <= size_ - range_.first;
    }
};

} // namespace Render2D
