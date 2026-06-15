#pragma once

#include "Render2D/Component/Batch.hpp"
#include "Render2D/Component/Command.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Core/Types.hpp"
#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Meta/Domain.hpp"
#include "Render2D/System/BatchSystem.hpp"
#include "Render2D/System/ParallelPolicy.hpp"
#include "Render2D/System/SortKey.hpp"

#include <thread_center/thread_center.hpp>

#include <span>

namespace Render2D {

struct ThreadedBatchConfig {
    U32 worker_count;
    U32 min_items_per_task;
    U32 parallel_threshold;
};

inline constexpr ThreadedBatchConfig kDefaultThreadedBatchConfig{
    .worker_count = 0U,
    .min_items_per_task = 4096U,
    .parallel_threshold = kDefaultParallelThreshold,
};

// Stage 21B batch tail: a ThreadCenter-backed parallel form of BatchSystem, the
// adjacent-merge scan that follows the (parallel) draw sort. It produces
// byte-identical output to the single-thread reference BatchSystem::run /
// runBindless (verified by render2d.threaded_batch_system) and is gated by the
// shared Stage 21E threshold (ParallelPolicy.hpp / ProjectMergeTODO #22), so
// sub-threshold workloads route to the reference path unchanged.
//
// The original Stage 21B ADR (2026-06-15) deliberately left batching
// single-threaded, noting "revisit only if a workload makes the merge hot". The
// at-scale sweep did: the batch scan is 5-15 ms at 1-2M draws (see
// BENCHMARK_BASELINE.md), and on the realistic sorted/clustered stream it
// parallelizes 3-4.6x. This runtime is that revisit; see the dedicated ADR
// 2026-06-15-stage21-parallel-deterministic-batch.md.
//
// Determinism contract. A batch is a maximal run of adjacent draws with equal
// batch key; a "start" at index i is (i == 0) || !equalBatchKey(draw[i-1],
// draw[i]). The start predicate is chunk-independent (a chunk's first index reads
// the previous draw read-only across the boundary), so the set of starts - and
// therefore the set of batches - is identical regardless of how the stream is
// partitioned. Each batch begins at a start and runs to the next start, which may
// cross chunk boundaries (a run can span several chunks). The parallel form finds
// starts per chunk (parallel), prefix-sums the per-chunk start counts into global
// batch offsets plus the first start strictly after each chunk (single serial
// task), then writes each chunk's batches at its offset (parallel). Chunks write
// disjoint output ranges, so the result is byte-identical to the serial scan.
template<class Provider, class Dim>
class ThreadedBatchRuntime {
public:
    using DrawCommandType = DrawCommand<Provider, Dim>;
    using BatchCommandType = BatchCommand<Provider, Dim>;

    explicit ThreadedBatchRuntime(ThreadedBatchConfig config_ = kDefaultThreadedBatchConfig)
        : config_(normalizeConfig(config_)),
          center_(makeExecutorConfig(config_))
    {
        this->config_.worker_count = center_.workerCount();
    }

    ThreadedBatchRuntime(const ThreadedBatchRuntime&) = delete;
    auto operator=(const ThreadedBatchRuntime&) -> ThreadedBatchRuntime& = delete;
    ThreadedBatchRuntime(ThreadedBatchRuntime&&) = delete;
    auto operator=(ThreadedBatchRuntime&&) -> ThreadedBatchRuntime& = delete;

    [[nodiscard]] auto workerCount() const noexcept -> U32
    {
        return center_.workerCount();
    }

    [[nodiscard]] auto config() const noexcept -> ThreadedBatchConfig
    {
        return config_;
    }

    [[nodiscard]] auto shouldParallelize(U32 item_count_) const noexcept -> bool
    {
        return shouldParallelizeItemCount(item_count_, config_.parallel_threshold, workerCount());
    }

    // Parallel equivalent of BatchSystem::run (compares the full batch key).
    [[nodiscard]] auto runBatch(
        std::span<const DrawCommandType> draw_commands_,
        std::span<BatchCommandType> batch_commands_) -> SystemResult
    {
        return runImpl<false>(draw_commands_, batch_commands_);
    }

    // Parallel equivalent of BatchSystem::runBindless (drops texture identity from
    // the key, so draws differing only by texture merge under the bindless path).
    [[nodiscard]] auto runBatchBindless(
        std::span<const DrawCommandType> draw_commands_,
        std::span<BatchCommandType> batch_commands_) -> SystemResult
    {
        return runImpl<true>(draw_commands_, batch_commands_);
    }

private:
    [[nodiscard]] static auto normalizeConfig(ThreadedBatchConfig config_) noexcept
        -> ThreadedBatchConfig
    {
        if (config_.min_items_per_task == 0U) {
            config_.min_items_per_task = 1U;
        }
        return config_;
    }

    [[nodiscard]] static auto makeExecutorConfig(ThreadedBatchConfig config_) noexcept
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

    // Mirrors BatchSystem::makeBatch exactly; the render2d.threaded_batch_system
    // byte-equality test fails immediately if the two ever drift.
    [[nodiscard]] static auto makeBatch(
        const DrawCommandType& draw_command_,
        U32 draw_first_) noexcept -> BatchCommandType
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

    template<bool Bindless>
    [[nodiscard]] static auto canMerge(
        const DrawCommandType& left_,
        const DrawCommandType& right_) noexcept -> bool
    {
        if constexpr (Bindless) {
            return drawCommandsHaveEqualBindlessBatchKey(left_, right_);
        } else {
            return drawCommandsHaveEqualBatchKey(left_, right_);
        }
    }

    [[nodiscard]] static auto validateInputs(
        std::span<const DrawCommandType> draw_commands_,
        std::span<BatchCommandType> batch_commands_) noexcept -> SystemResult
    {
        if (!isSystemResultCountRepresentable(draw_commands_.size()) ||
            !isSystemResultCountRepresentable(batch_commands_.size())) {
            return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
        }
        // Full-capacity contract (as ThreadedDrawSort requires of its outputs): a
        // batch stream can hold at most one batch per draw, so requiring capacity
        // >= draw count lets the parallel scatter run without a mid-flight bounds
        // check. The single-thread BatchSystem reference accepts smaller buffers.
        if (batch_commands_.size() < draw_commands_.size()) {
            return {
                .code = SystemStatusCode::InsufficientCapacity,
                .read_count = static_cast<U32>(draw_commands_.size()),
                .write_count = 0U,
            };
        }
        return {
            .code = SystemStatusCode::Ok,
            .read_count = static_cast<U32>(draw_commands_.size()),
            .write_count = 0U,
        };
    }

    template<bool Bindless>
    [[nodiscard]] auto runImpl(
        std::span<const DrawCommandType> draw_commands_,
        std::span<BatchCommandType> batch_commands_) -> SystemResult
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            const auto validation = validateInputs(draw_commands_, batch_commands_);
            if (validation.code != SystemStatusCode::Ok || draw_commands_.empty()) {
                return validation;
            }

            if (!shouldParallelize(static_cast<U32>(draw_commands_.size()))) {
                if constexpr (Bindless) {
                    return BatchSystem<Provider, Dim>::runBindless(draw_commands_, batch_commands_);
                } else {
                    return BatchSystem<Provider, Dim>::run(draw_commands_, batch_commands_);
                }
            }

            return runParallel<Bindless>(draw_commands_, batch_commands_);
        }
    }

    template<bool Bindless>
    [[nodiscard]] auto runParallel(
        std::span<const DrawCommandType> draw_commands_,
        std::span<BatchCommandType> batch_commands_) -> SystemResult
    {
        const Usize draw_count = draw_commands_.size();
        rebuildChunks(draw_count);
        const Usize chunk_count = chunks_.size();
        resizeScratch(chunk_count, draw_count);

        const auto* const draws = draw_commands_.data();
        auto* const batches = batch_commands_.data();
        U32* const starts = starts_.data();

        auto plan = center_.makePlan();

        // Pass 1: per chunk, find batch starts and write them densely into the
        // chunk's own slice starts_[first .. first + count). The slice lives inside
        // [first, end), so chunks never overlap and need no precomputed offset.
        const auto find_task = plan.parallelFor<Usize>(
            makeTaskDesc("render2d.threaded.batch.find"),
            0U,
            chunk_count,
            1U,
            [this, draws, starts](Usize chunk_index_) {
                const auto chunk = chunks_[chunk_index_];
                const Usize first = chunk.first;
                const Usize last = first + chunk.count;
                Usize write = first;
                for (Usize index = first; index < last; ++index) {
                    const bool is_start =
                        (index == 0U) || !canMerge<Bindless>(draws[index - 1U], draws[index]);
                    if (is_start) {
                        starts[write] = static_cast<U32>(index);
                        ++write;
                    }
                }
                chunk_start_counts_[chunk_index_] = static_cast<U32>(write - first);
            });

        // Single task: prefix-sum the per-chunk start counts into global batch
        // offsets, then record the first start strictly after each chunk (a
        // right-to-left pass), so a chunk's last batch knows where its run ends
        // even when the run continues through later chunks that have no start.
        const auto offset_task = plan.task(
            makeTaskDesc("render2d.threaded.batch.offset"),
            [this, starts, draw_count] {
                const Usize chunk_count_local = chunks_.size();
                U32 running = 0U;
                for (Usize chunk_index = 0U; chunk_index < chunk_count_local; ++chunk_index) {
                    chunk_offsets_[chunk_index] = running;
                    running += chunk_start_counts_[chunk_index];
                }

                U32 next_start = static_cast<U32>(draw_count);
                for (Usize chunk_index = chunk_count_local; chunk_index-- > 0U;) {
                    chunk_boundary_next_[chunk_index] = next_start;
                    if (chunk_start_counts_[chunk_index] > 0U) {
                        next_start = starts[chunks_[chunk_index].first];
                    }
                }
            });

        // Pass 2: per chunk, emit its batches at its global offset. Each batch's
        // draw_count is the gap to the next start - the next start within the chunk,
        // or the first start after the chunk for the chunk's last batch.
        const auto write_task = plan.parallelFor<Usize>(
            makeTaskDesc("render2d.threaded.batch.write"),
            0U,
            chunk_count,
            1U,
            [this, draws, batches, starts](Usize chunk_index_) {
                const U32 count = chunk_start_counts_[chunk_index_];
                if (count == 0U) {
                    return;
                }
                const Usize base = chunks_[chunk_index_].first;
                const U32 offset = chunk_offsets_[chunk_index_];
                for (U32 local = 0U; local < count; ++local) {
                    const U32 start_index = starts[base + local];
                    const U32 next_start = (local + 1U < count) ?
                        starts[base + local + 1U] :
                        chunk_boundary_next_[chunk_index_];
                    auto batch = makeBatch(draws[start_index], start_index);
                    batch.draw_count = next_start - start_index;
                    batches[offset + local] = batch;
                }
            });

        plan.precede(find_task, offset_task);
        plan.precede(offset_task, write_task);

        auto run_handle = center_.dispatch(plan);
        run_handle.wait();
        center_.waitIdle();

        U32 total_batches = 0U;
        for (Usize chunk_index = 0U; chunk_index < chunk_count; ++chunk_index) {
            total_batches += chunk_start_counts_[chunk_index];
        }

        return {
            .code = SystemStatusCode::Ok,
            .read_count = static_cast<U32>(draw_count),
            .write_count = total_batches,
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

    void resizeScratch(Usize chunk_count_, Usize item_count_)
    {
        chunk_start_counts_.resize(chunk_count_);
        chunk_offsets_.resize(chunk_count_);
        chunk_boundary_next_.resize(chunk_count_);
        starts_.resize(item_count_);
    }

    ThreadedBatchConfig config_;
    ThreadCenter::Center center_;
    McVector<RangeU32> chunks_;
    McVector<U32> chunk_start_counts_;
    McVector<U32> chunk_offsets_;
    McVector<U32> chunk_boundary_next_;
    McVector<U32> starts_;
};

} // namespace Render2D
