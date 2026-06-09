#pragma once

#include "Render2D/Component/Command.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <array>
#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct DrawSortSystem {
    static SystemResult run(
        std::span<const DrawCommand<Provider, Dim>> draw_commands_,
        std::span<DrawCommand<Provider, Dim>> sorted_draw_commands_,
        std::span<SortedItem<Provider, Dim>> scratch_a_,
        std::span<SortedItem<Provider, Dim>> scratch_b_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(draw_commands_.size()) ||
                !isSystemResultCountRepresentable(sorted_draw_commands_.size()) ||
                !isSystemResultCountRepresentable(scratch_a_.size()) ||
                !isSystemResultCountRepresentable(scratch_b_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (sorted_draw_commands_.size() < draw_commands_.size() ||
                scratch_a_.size() < draw_commands_.size() ||
                scratch_b_.size() < draw_commands_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = static_cast<U32>(draw_commands_.size()),
                    .write_count = static_cast<U32>(sorted_draw_commands_.size()),
                };
            }
            if (draw_commands_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }

            const Usize draw_count = draw_commands_.size();
            auto* const items_a = scratch_a_.data();
            auto* const items_b = scratch_b_.data();
            for (Usize index = 0U; index < draw_count; ++index) {
                items_a[index] = {
                    .visible_index = static_cast<U32>(index),
                    .sort_key = draw_commands_[index].sort_key,
                };
            }

            radixSortItems(items_a, items_b, draw_count);

            auto* const sorted_draw_commands = sorted_draw_commands_.data();
            for (Usize index = 0U; index < draw_count; ++index) {
                sorted_draw_commands[index] = draw_commands_[items_a[index].visible_index];
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(draw_count),
                .write_count = static_cast<U32>(draw_count),
            };
        }
    }

private:
    static void radixSortItems(
        SortedItem<Provider, Dim>* items_a_,
        SortedItem<Provider, Dim>* items_b_,
        Usize item_count_) noexcept
    {
        std::array<U32, 256U> counts{};
        auto* source = items_a_;
        auto* destination = items_b_;

        for (U32 pass = 0U; pass < 4U; ++pass) {
            for (U32 bucket = 0U; bucket < counts.size(); ++bucket) {
                counts[bucket] = 0U;
            }

            const U32 shift = pass * 8U;
            for (Usize index = 0U; index < item_count_; ++index) {
                ++counts[(source[index].sort_key >> shift) & 0xFFU];
            }

            U32 offset = 0U;
            for (U32 bucket = 0U; bucket < counts.size(); ++bucket) {
                const U32 bucket_count = counts[bucket];
                counts[bucket] = offset;
                offset += bucket_count;
            }

            for (Usize index = 0U; index < item_count_; ++index) {
                const U32 bucket = (source[index].sort_key >> shift) & 0xFFU;
                destination[counts[bucket]] = source[index];
                ++counts[bucket];
            }

            auto* const previous_source = source;
            source = destination;
            destination = previous_source;
        }
    }
};

} // namespace Render2D
