#pragma once

#include "Render2D/Component/Batch.hpp"
#include "Render2D/Component/Command.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"
#include "Render2D/System/SortKey.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct BatchSystem {
    static SystemResult run(
        std::span<const DrawCommand<Provider, Dim>> draw_commands_,
        std::span<BatchCommand<Provider, Dim>> batch_commands_) noexcept
    {
        return runImpl<false>(draw_commands_, batch_commands_);
    }

    // Bindless batching: draws differing only by texture merge into one batch
    // because the per-instance texture is resolved in-shader against a single
    // descriptor set, so it no longer pins a draw to a descriptor binding.
    // Material/pipeline/layer/flags are still compared in full (see
    // drawCommandsHaveEqualBindlessBatchKey), so a stale material generation can
    // never be merged away. Requires draws sorted with makeBindlessDrawSortKey so
    // equal-key draws are adjacent.
    static SystemResult runBindless(
        std::span<const DrawCommand<Provider, Dim>> draw_commands_,
        std::span<BatchCommand<Provider, Dim>> batch_commands_) noexcept
    {
        return runImpl<true>(draw_commands_, batch_commands_);
    }

private:
    template<bool Bindless>
    static SystemResult runImpl(
        std::span<const DrawCommand<Provider, Dim>> draw_commands_,
        std::span<BatchCommand<Provider, Dim>> batch_commands_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(draw_commands_.size()) ||
                !isSystemResultCountRepresentable(batch_commands_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (draw_commands_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }
            if (batch_commands_.empty()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(draw_commands_.size()),
                    .write_count = 0U,
                };
            }

            Usize batch_index = 0U;
            batch_commands_[batch_index] = makeBatch(draw_commands_[0], 0U);

            for (Usize draw_index = 1U; draw_index < draw_commands_.size(); ++draw_index) {
                const auto& previous_draw = draw_commands_[draw_index - 1U];
                const auto& current_draw = draw_commands_[draw_index];
                if (canMerge<Bindless>(previous_draw, current_draw)) {
                    ++batch_commands_[batch_index].draw_count;
                    continue;
                }

                ++batch_index;
                if (batch_index >= batch_commands_.size()) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = static_cast<U32>(draw_index),
                        .write_count = static_cast<U32>(batch_index),
                    };
                }
                batch_commands_[batch_index] = makeBatch(current_draw, static_cast<U32>(draw_index));
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(draw_commands_.size()),
                .write_count = static_cast<U32>(batch_index + 1U),
            };
        }
    }

    template<bool Bindless>
    static bool canMerge(
        const DrawCommand<Provider, Dim>& left_,
        const DrawCommand<Provider, Dim>& right_) noexcept
    {
        if constexpr (Bindless) {
            return drawCommandsHaveEqualBindlessBatchKey(left_, right_);
        } else {
            return drawCommandsHaveEqualBatchKey(left_, right_);
        }
    }

    static BatchCommand<Provider, Dim> makeBatch(
        const DrawCommand<Provider, Dim>& draw_command_,
        U32 draw_first_) noexcept
    {
        return {
            .draw_first = draw_first_,
            .draw_count = 1U,
            .material_id = draw_command_.material_id,
            .material_generation = draw_command_.material_generation,
            .texture_id = draw_command_.texture_id,
            .texture_generation = draw_command_.texture_generation,
            .pipeline_id = 0U,
            .pipeline_generation = 0U,
            .descriptor_id = 0U,
            .descriptor_generation = 0U,
            .sort_key = draw_command_.sort_key,
            .flags = draw_command_.flags,
        };
    }
};

} // namespace Render2D
