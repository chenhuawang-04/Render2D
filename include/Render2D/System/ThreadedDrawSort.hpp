#pragma once

#include "Render2D/Component/Command.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Core/Types.hpp"
#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Meta/Domain.hpp"
#include "Render2D/System/ParallelPolicy.hpp"
#include "Render2D/System/SortSystem.hpp"

#include <thread_center/thread_center.hpp>

#include <span>

namespace Render2D {

struct ThreadedDrawSortConfig {
    U32 worker_count;
    U32 min_items_per_task;
    U32 parallel_threshold;
};

inline constexpr ThreadedDrawSortConfig kDefaultThreadedDrawSortConfig{
    .worker_count = 0U,
    .min_items_per_task = 4096U,
    .parallel_threshold = kDefaultParallelThreshold,
};

// Stage 21B: a ThreadCenter-backed parallel stable LSD radix sort for the draw
// stream. It produces byte-identical output to the single-thread reference
// DrawSortSystem::run (verified by render2d.threaded_draw_sort) and is gated by
// the shared Stage 21E threshold (ParallelPolicy.hpp / ProjectMergeTODO #22), so
// sub-threshold workloads route to the reference path.
//
// The single-thread radix is a stable 4-pass byte sort: within each radix bucket
// items keep increasing source-index order. The parallel form preserves exactly
// that order by partitioning the draws into contiguous chunks and computing the
// global scatter offset for (bucket, chunk) in bucket-major then chunk order:
// all of bucket b's items go before bucket b+1's, and within a bucket chunk 0's
// items precede chunk 1's. Because the chunks are contiguous in source order,
// this reproduces increasing-source-index order per bucket - i.e. the same stable
// permutation the serial sort produces. Each chunk then scatters into its own
// disjoint offset cursors, so the writes never overlap.
//
// The BatchSystem merge tail is deliberately NOT parallelized: it is an
// inherently sequential adjacent-merge scan and measured tiny (~0.02 ms at 12k
// draws) next to the sort (~0.18 ms), so there is no data-backed reason to pay
// ThreadCenter overhead for it.
template<class Provider, class Dim>
class ThreadedDrawSortRuntime {
public:
    using DrawCommandType = DrawCommand<Provider, Dim>;
    using SortedItemType = SortedItem<Provider, Dim>;

    explicit ThreadedDrawSortRuntime(
        ThreadedDrawSortConfig config_ = kDefaultThreadedDrawSortConfig)
        : config_(normalizeConfig(config_)),
          center_(makeExecutorConfig(config_))
    {
        this->config_.worker_count = center_.workerCount();
    }

    ThreadedDrawSortRuntime(const ThreadedDrawSortRuntime&) = delete;
    auto operator=(const ThreadedDrawSortRuntime&) -> ThreadedDrawSortRuntime& = delete;
    ThreadedDrawSortRuntime(ThreadedDrawSortRuntime&&) = delete;
    auto operator=(ThreadedDrawSortRuntime&&) -> ThreadedDrawSortRuntime& = delete;

    [[nodiscard]] auto workerCount() const noexcept -> U32
    {
        return center_.workerCount();
    }

    [[nodiscard]] auto config() const noexcept -> ThreadedDrawSortConfig
    {
        return config_;
    }

    [[nodiscard]] auto shouldParallelize(U32 item_count_) const noexcept -> bool
    {
        return shouldParallelizeItemCount(item_count_, config_.parallel_threshold, workerCount());
    }

    [[nodiscard]] auto runDrawSort(
        std::span<const DrawCommandType> draw_commands_,
        std::span<DrawCommandType> sorted_draw_commands_,
        std::span<SortedItemType> scratch_a_,
        std::span<SortedItemType> scratch_b_) -> SystemResult
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            const auto validation = validateInputs(
                draw_commands_, sorted_draw_commands_, scratch_a_, scratch_b_);
            if (validation.code != SystemStatusCode::Ok || draw_commands_.empty()) {
                return validation;
            }

            if (!shouldParallelize(static_cast<U32>(draw_commands_.size()))) {
                return DrawSortSystem<Provider, Dim>::run(
                    draw_commands_, sorted_draw_commands_, scratch_a_, scratch_b_);
            }

            return runParallel(draw_commands_, sorted_draw_commands_, scratch_a_, scratch_b_);
        }
    }

private:
    static constexpr U32 kRadixBuckets = 256U;
    static constexpr U32 kRadixPasses = 4U;

    [[nodiscard]] static auto normalizeConfig(ThreadedDrawSortConfig config_) noexcept
        -> ThreadedDrawSortConfig
    {
        if (config_.min_items_per_task == 0U) {
            config_.min_items_per_task = 1U;
        }
        return config_;
    }

    [[nodiscard]] static auto makeExecutorConfig(ThreadedDrawSortConfig config_) noexcept
        -> ThreadCenter::ExecutorConfig
    {
        auto executor_config = ThreadCenter::ExecutorConfig{};
        if (config_.worker_count != 0U) {
            executor_config.workers = config_.worker_count;
        }
        return executor_config;
    }

    [[nodiscard]] static auto makeTaskDesc(const char* name_) noexcept -> ThreadCenter::TaskDesc
    {
        return ThreadCenter::TaskDesc{
            .name = name_,
            .domain = ThreadCenter::ScheduleDomain::RENDERING,
            .task_kind = ThreadCenter::TaskKind::PARALLEL_FOR,
            .lane = ThreadCenter::ExecutionLane::RENDER_ASSIST,
            .priority = ThreadCenter::TaskPriority::NORMAL,
            .budget_class = ThreadCenter::BudgetClass::FRAME_BOUND,
            .min_grain_size = 1U,
        };
    }

    [[nodiscard]] static auto validateInputs(
        std::span<const DrawCommandType> draw_commands_,
        std::span<DrawCommandType> sorted_draw_commands_,
        std::span<SortedItemType> scratch_a_,
        std::span<SortedItemType> scratch_b_) noexcept -> SystemResult
    {
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
        return {
            .code = SystemStatusCode::Ok,
            .read_count = static_cast<U32>(draw_commands_.size()),
            .write_count = 0U,
        };
    }

    [[nodiscard]] auto runParallel(
        std::span<const DrawCommandType> draw_commands_,
        std::span<DrawCommandType> sorted_draw_commands_,
        std::span<SortedItemType> scratch_a_,
        std::span<SortedItemType> scratch_b_) -> SystemResult
    {
        const Usize draw_count = draw_commands_.size();
        rebuildChunks(draw_count);
        resizeHistograms(chunks_.size());

        auto* const buffer_a = scratch_a_.data();
        auto* const buffer_b = scratch_b_.data();

        auto plan = center_.makePlan();

        // Stage 0: seed sorted items {visible_index = source order, sort_key}.
        const auto seed_task = plan.parallelFor<Usize>(
            makeTaskDesc("render2d.threaded.sort.seed"),
            0U,
            chunks_.size(),
            1U,
            [this, draw_commands_, buffer_a](Usize chunk_index_) {
                const auto chunk = chunks_[chunk_index_];
                const Usize first = chunk.first;
                const Usize last = first + chunk.count;
                for (Usize index = first; index < last; ++index) {
                    buffer_a[index] = {
                        .visible_index = static_cast<U32>(index),
                        .sort_key = draw_commands_[index].sort_key,
                    };
                }
            });

        auto previous_task = seed_task;
        const SortedItemType* final_source = buffer_a;
        for (U32 pass = 0U; pass < kRadixPasses; ++pass) {
            const U32 shift = pass * 8U;
            const bool source_is_a = (pass % 2U) == 0U;
            const SortedItemType* source = source_is_a ? buffer_a : buffer_b;
            SortedItemType* destination = source_is_a ? buffer_b : buffer_a;
            final_source = destination;

            const auto histogram_task = plan.parallelFor<Usize>(
                makeTaskDesc("render2d.threaded.sort.histogram"),
                0U,
                chunks_.size(),
                1U,
                [this, source, shift](Usize chunk_index_) {
                    U32* const histogram = chunk_histograms_.data() + chunk_index_ * kRadixBuckets;
                    for (U32 bucket = 0U; bucket < kRadixBuckets; ++bucket) {
                        histogram[bucket] = 0U;
                    }
                    const auto chunk = chunks_[chunk_index_];
                    const Usize first = chunk.first;
                    const Usize last = first + chunk.count;
                    for (Usize index = first; index < last; ++index) {
                        ++histogram[(source[index].sort_key >> shift) & 0xFFU];
                    }
                });

            // Single task: bucket-major then chunk-order prefix sum. This exact
            // ordering is what makes the parallel scatter stable and identical to
            // the serial radix (see the class comment).
            const auto offset_task = plan.task(
                makeTaskDesc("render2d.threaded.sort.offset"),
                [this] {
                    const Usize chunk_count = chunks_.size();
                    U32 running = 0U;
                    for (U32 bucket = 0U; bucket < kRadixBuckets; ++bucket) {
                        for (Usize chunk_index = 0U; chunk_index < chunk_count; ++chunk_index) {
                            const Usize slot = chunk_index * kRadixBuckets + bucket;
                            chunk_offsets_[slot] = running;
                            running += chunk_histograms_[slot];
                        }
                    }
                });

            const auto scatter_task = plan.parallelFor<Usize>(
                makeTaskDesc("render2d.threaded.sort.scatter"),
                0U,
                chunks_.size(),
                1U,
                [this, source, destination, shift](Usize chunk_index_) {
                    U32* const cursor = chunk_offsets_.data() + chunk_index_ * kRadixBuckets;
                    const auto chunk = chunks_[chunk_index_];
                    const Usize first = chunk.first;
                    const Usize last = first + chunk.count;
                    for (Usize index = first; index < last; ++index) {
                        const U32 bucket = (source[index].sort_key >> shift) & 0xFFU;
                        destination[cursor[bucket]] = source[index];
                        ++cursor[bucket];
                    }
                });

            plan.precede(previous_task, histogram_task);
            plan.precede(histogram_task, offset_task);
            plan.precede(offset_task, scatter_task);
            previous_task = scatter_task;
        }

        // Gather the sorted draws from the final permutation. Disjoint output
        // indices, so byte-identical to the serial gather loop.
        const auto gather_task = plan.parallelFor<Usize>(
            makeTaskDesc("render2d.threaded.sort.gather"),
            0U,
            chunks_.size(),
            1U,
            [this, draw_commands_, sorted_draw_commands_, final_source](Usize chunk_index_) {
                const auto chunk = chunks_[chunk_index_];
                const Usize first = chunk.first;
                const Usize last = first + chunk.count;
                for (Usize index = first; index < last; ++index) {
                    sorted_draw_commands_[index] = draw_commands_[final_source[index].visible_index];
                }
            });
        plan.precede(previous_task, gather_task);

        auto run_handle = center_.dispatch(plan);
        run_handle.wait();
        center_.waitIdle();

        return {
            .code = SystemStatusCode::Ok,
            .read_count = static_cast<U32>(draw_count),
            .write_count = static_cast<U32>(draw_count),
        };
    }

    void rebuildChunks(Usize item_count_)
    {
        chunks_.clear();
        if (item_count_ == 0U) {
            return;
        }

        Usize target_chunk_count = item_count_ / config_.min_items_per_task;
        if ((item_count_ % config_.min_items_per_task) != 0U) {
            ++target_chunk_count;
        }
        if (target_chunk_count == 0U) {
            target_chunk_count = 1U;
        }

        const Usize worker_count = workerCount();
        if (target_chunk_count > worker_count) {
            target_chunk_count = worker_count;
        }
        if (target_chunk_count > item_count_) {
            target_chunk_count = item_count_;
        }

        const Usize chunk_size = (item_count_ + target_chunk_count - 1U) / target_chunk_count;
        chunks_.reserve(target_chunk_count);
        for (Usize first = 0U; first < item_count_; first += chunk_size) {
            Usize count = item_count_ - first;
            if (count > chunk_size) {
                count = chunk_size;
            }
            chunks_.push_back(RangeU32{
                .first = static_cast<U32>(first),
                .count = static_cast<U32>(count),
            });
        }
    }

    void resizeHistograms(Usize chunk_count_)
    {
        const Usize slot_count = chunk_count_ * kRadixBuckets;
        chunk_histograms_.resize(slot_count);
        chunk_offsets_.resize(slot_count);
    }

    ThreadedDrawSortConfig config_;
    ThreadCenter::Center center_;
    McVector<RangeU32> chunks_;
    McVector<U32> chunk_histograms_;
    McVector<U32> chunk_offsets_;
};

} // namespace Render2D
