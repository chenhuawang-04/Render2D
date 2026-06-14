#pragma once

#include "Render2D/Component/Command.hpp"
#include "Render2D/Component/Text.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Core/Types.hpp"
#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Meta/Domain.hpp"
#include "Render2D/System/ParallelPolicy.hpp"
#include "Render2D/System/TextSystem.hpp"

#include <thread_center/thread_center.hpp>

#include <atomic>
#include <limits>
#include <span>

namespace Render2D {

struct ThreadedTextCpuPipelineConfig {
    U32 worker_count;
    U32 min_glyphs_per_task;
    U32 parallel_threshold;
};

inline constexpr ThreadedTextCpuPipelineConfig kDefaultThreadedTextCpuPipelineConfig{
    .worker_count = 0U,
    .min_glyphs_per_task = 4096U,
    .parallel_threshold = kDefaultParallelThreshold,
};

// Stage 21A: ThreadCenter-backed parallelization of the dominant text-path
// stage, GlyphInstanceBuildSystem::runDirty (the only text stage that is both
// expensive at scale -- see docs/architecture/BENCHMARK_BASELINE.md -- and
// embarrassingly parallel). Each dirty range writes a disjoint glyph slice
// (glyph_first .. glyph_first + glyph_count), so partitioning the ranges across
// workers needs no merge and produces byte-identical output to the single-thread
// system. The runtime reuses GlyphInstanceBuildSystem::runDirty per chunk -- it
// adds no glyph-building logic of its own.
//
// The cheaper text stages (TextDirtySystem's prefix-sum, GlyphBatchSystem's
// stream compaction) remain the single-thread reference; the host runs them as
// before and feeds this runtime the resulting glyph runs + dirty ranges.
template<class Provider, class Dim>
class ThreadedTextCpuPipelineRuntime {
public:
    using TextType = Text<Provider, Dim>;
    using TextDirtyRangeType = TextDirtyRange<Provider, Dim>;
    using GlyphRunType = GlyphRun<Provider, Dim>;
    using GlyphInstanceType = GlyphInstance<Provider, Dim>;

    explicit ThreadedTextCpuPipelineRuntime(
        ThreadedTextCpuPipelineConfig config_ = kDefaultThreadedTextCpuPipelineConfig)
        : config_(normalizeConfig(config_)),
          center_(makeExecutorConfig(config_))
    {
        this->config_.worker_count = center_.workerCount();
    }

    ThreadedTextCpuPipelineRuntime(const ThreadedTextCpuPipelineRuntime&) = delete;
    auto operator=(const ThreadedTextCpuPipelineRuntime&) -> ThreadedTextCpuPipelineRuntime& = delete;
    ThreadedTextCpuPipelineRuntime(ThreadedTextCpuPipelineRuntime&&) = delete;
    auto operator=(ThreadedTextCpuPipelineRuntime&&) -> ThreadedTextCpuPipelineRuntime& = delete;

    [[nodiscard]] auto workerCount() const noexcept -> U32
    {
        return center_.workerCount();
    }

    [[nodiscard]] auto config() const noexcept -> ThreadedTextCpuPipelineConfig
    {
        return config_;
    }

    [[nodiscard]] auto shouldParallelize(U32 glyph_count_) const noexcept -> bool
    {
        return shouldParallelizeItemCount(glyph_count_, config_.parallel_threshold, workerCount());
    }

    // Parallel equivalent of GlyphInstanceBuildSystem::runDirty. Below the glyph
    // threshold (or with <=1 worker) it falls straight through to the single-
    // thread system. Precondition (held by the text pipeline): dirty ranges
    // reference distinct texts, so their glyph slices are disjoint.
    [[nodiscard]] auto runGlyphInstanceBuildDirty(
        std::span<const GlyphRunType> glyph_runs_,
        std::span<const TextDirtyRangeType> dirty_ranges_,
        std::span<const TextType> texts_,
        GlyphBuildConfig config_,
        std::span<GlyphInstanceType> glyph_instances_) -> SystemResult
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (dirty_ranges_.size() > (std::numeric_limits<U32>::max)()) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            const U32 total_glyph_count = totalGlyphCount(dirty_ranges_);
            if (!shouldParallelize(total_glyph_count)) {
                return GlyphInstanceBuildSystem<Provider, Dim>::runDirty(
                    glyph_runs_,
                    dirty_ranges_,
                    texts_,
                    config_,
                    glyph_instances_);
            }

            rebuildRangeChunks(dirty_ranges_.size(), total_glyph_count);
            if (chunks_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }

            auto stage_error = std::atomic<U32>{statusToU32(SystemStatusCode::Ok)};
            auto plan = center_.makePlan();
            static_cast<void>(plan.parallelFor<Usize>(
                makeTaskDesc("render2d.threaded.glyph_instance"),
                0U,
                chunks_.size(),
                1U,
                [this, glyph_runs_, dirty_ranges_, texts_, config_, glyph_instances_, &stage_error](
                    Usize chunk_index_) {
                    const auto chunk = chunks_[chunk_index_];
                    const auto system_result = GlyphInstanceBuildSystem<Provider, Dim>::runDirty(
                        glyph_runs_,
                        dirty_ranges_.subspan(
                            static_cast<Usize>(chunk.first),
                            static_cast<Usize>(chunk.count)),
                        texts_,
                        config_,
                        glyph_instances_);
                    chunk_write_counts_[chunk_index_] = system_result.write_count;
                    recordStageError(stage_error, system_result);
                }));

            auto run_handle = center_.dispatch(plan);
            run_handle.wait();
            center_.waitIdle();

            const auto status = statusFromU32(stage_error.load(std::memory_order_relaxed));
            if (status != SystemStatusCode::Ok) {
                return {
                    .code = status,
                    .read_count = static_cast<U32>(dirty_ranges_.size()),
                    .write_count = 0U,
                };
            }

            U32 total_written = 0U;
            for (Usize chunk_index = 0U; chunk_index < chunks_.size(); ++chunk_index) {
                total_written += chunk_write_counts_[chunk_index];
            }
            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(dirty_ranges_.size()),
                .write_count = total_written,
            };
        }
    }

private:
    [[nodiscard]] static auto normalizeConfig(ThreadedTextCpuPipelineConfig config_) noexcept
        -> ThreadedTextCpuPipelineConfig
    {
        if (config_.min_glyphs_per_task == 0U) {
            config_.min_glyphs_per_task = 1U;
        }
        return config_;
    }

    [[nodiscard]] static auto makeExecutorConfig(ThreadedTextCpuPipelineConfig config_) noexcept
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

    [[nodiscard]] static auto statusToU32(SystemStatusCode code_) noexcept -> U32
    {
        return static_cast<U32>(code_);
    }

    [[nodiscard]] static auto statusFromU32(U32 code_) noexcept -> SystemStatusCode
    {
        return static_cast<SystemStatusCode>(code_);
    }

    static void recordStageError(std::atomic<U32>& stage_error_, SystemResult result_) noexcept
    {
        if (result_.code == SystemStatusCode::Ok) {
            return;
        }
        auto expected = statusToU32(SystemStatusCode::Ok);
        static_cast<void>(stage_error_.compare_exchange_strong(
            expected,
            statusToU32(result_.code),
            std::memory_order_relaxed,
            std::memory_order_relaxed));
    }

    [[nodiscard]] static auto totalGlyphCount(
        std::span<const TextDirtyRangeType> dirty_ranges_) noexcept -> U32
    {
        constexpr U32 kMaxU32 = (std::numeric_limits<U32>::max)();
        U32 total = 0U;
        for (const auto& range : dirty_ranges_) {
            if (range.new_glyph_count > kMaxU32 - total) {
                return kMaxU32;
            }
            total += range.new_glyph_count;
        }
        return total;
    }

    void rebuildRangeChunks(Usize range_count_, U32 total_glyph_count_)
    {
        chunks_.clear();
        chunk_write_counts_.clear();
        if (range_count_ == 0U) {
            return;
        }

        Usize target_chunk_count = total_glyph_count_ / config_.min_glyphs_per_task;
        if ((total_glyph_count_ % config_.min_glyphs_per_task) != 0U) {
            ++target_chunk_count;
        }
        if (target_chunk_count == 0U) {
            target_chunk_count = 1U;
        }

        const Usize worker_count = workerCount();
        if (target_chunk_count > worker_count) {
            target_chunk_count = worker_count;
        }
        if (target_chunk_count > range_count_) {
            target_chunk_count = range_count_;
        }

        const Usize chunk_size = (range_count_ + target_chunk_count - 1U) / target_chunk_count;
        chunks_.reserve(target_chunk_count);
        for (Usize first = 0U; first < range_count_; first += chunk_size) {
            Usize count = range_count_ - first;
            if (count > chunk_size) {
                count = chunk_size;
            }
            chunks_.push_back(RangeU32{
                .first = static_cast<U32>(first),
                .count = static_cast<U32>(count),
            });
        }
        chunk_write_counts_.resize(chunks_.size());
        for (Usize index = 0U; index < chunk_write_counts_.size(); ++index) {
            chunk_write_counts_[index] = 0U;
        }
    }

    ThreadedTextCpuPipelineConfig config_;
    ThreadCenter::Center center_;
    McVector<RangeU32> chunks_;
    McVector<U32> chunk_write_counts_;
};

} // namespace Render2D
