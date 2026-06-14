#pragma once

#include "Render2D/Component/Batch.hpp"
#include "Render2D/Component/Bounds.hpp"
#include "Render2D/Component/Camera.hpp"
#include "Render2D/Component/Command.hpp"
#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Component/Transform.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Core/Types.hpp"
#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Meta/Domain.hpp"
#include "Render2D/System/BatchSystem.hpp"
#include "Render2D/System/BoundsSystem.hpp"
#include "Render2D/System/CommandBuildSystem.hpp"
#include "Render2D/System/CullingSystem.hpp"
#include "Render2D/System/ParallelPolicy.hpp"
#include "Render2D/System/TransformSystem.hpp"

#include <thread_center/thread_center.hpp>

#include <atomic>
#include <span>

namespace Render2D {

struct ThreadedCpuPipelineConfig {
    U32 worker_count;
    U32 min_items_per_task;
    U32 parallel_threshold;
};

inline constexpr ThreadedCpuPipelineConfig kDefaultThreadedCpuPipelineConfig{
    .worker_count = 0U,
    .min_items_per_task = 1024U,
    .parallel_threshold = kDefaultParallelThreshold,
};

struct ThreadedSpritePipelineResult {
    SystemStatusCode code;
    U32 transform_count;
    U32 bounds_count;
    U32 visible_count;
    U32 draw_count;
    U32 batch_count;
};

template<class Provider, class Dim>
class ThreadedCpuPipelineRuntime {
public:
    using TransformType = Transform<Provider, Dim>;
    using WorldTransformType = WorldTransform<Provider, Dim>;
    using LocalBoundsType = LocalBounds<Provider, Dim>;
    using WorldBoundsType = WorldBounds<Provider, Dim>;
    using CameraType = Camera<Provider, Dim>;
    using VisibilityMaskType = VisibilityMask<Provider, Dim>;
    using VisibleItemType = VisibleItem<Provider, Dim>;
    using SpriteType = Sprite<Provider, Dim>;
    using DrawCommandType = DrawCommand<Provider, Dim>;
    using BatchCommandType = BatchCommand<Provider, Dim>;

    explicit ThreadedCpuPipelineRuntime(
        ThreadedCpuPipelineConfig config_ = kDefaultThreadedCpuPipelineConfig)
        : config_(normalizeConfig(config_)),
          center_(makeExecutorConfig(config_))
    {
        this->config_.worker_count = center_.workerCount();
    }

    ThreadedCpuPipelineRuntime(const ThreadedCpuPipelineRuntime&) = delete;
    auto operator=(const ThreadedCpuPipelineRuntime&) -> ThreadedCpuPipelineRuntime& = delete;
    ThreadedCpuPipelineRuntime(ThreadedCpuPipelineRuntime&&) = delete;
    auto operator=(ThreadedCpuPipelineRuntime&&) -> ThreadedCpuPipelineRuntime& = delete;

    [[nodiscard]] auto workerCount() const noexcept -> U32
    {
        return center_.workerCount();
    }

    [[nodiscard]] auto config() const noexcept -> ThreadedCpuPipelineConfig
    {
        return config_;
    }

    // Stage 21E threshold gate: small workloads run on the single-thread
    // reference path (same systems, byte-identical output) to avoid ThreadCenter
    // scheduling/merge overhead. See ParallelPolicy.hpp / ProjectMergeTODO #22.
    [[nodiscard]] auto shouldParallelize(U32 item_count_) const noexcept -> bool
    {
        return shouldParallelizeItemCount(item_count_, config_.parallel_threshold, workerCount());
    }

    [[nodiscard]] auto runSpritePipeline(
        const CameraType& camera_,
        std::span<const TransformType> transforms_,
        std::span<const LocalBoundsType> local_bounds_,
        std::span<const VisibilityMaskType> visibility_masks_,
        std::span<const SpriteType> sprites_,
        std::span<WorldTransformType> world_transforms_,
        std::span<WorldBoundsType> world_bounds_,
        std::span<VisibleItemType> visible_items_,
        std::span<DrawCommandType> draw_commands_,
        std::span<BatchCommandType> batch_commands_) -> ThreadedSpritePipelineResult
    {
        auto result = ThreadedSpritePipelineResult{
            .code = SystemStatusCode::Ok,
            .transform_count = 0U,
            .bounds_count = 0U,
            .visible_count = 0U,
            .draw_count = 0U,
            .batch_count = 0U,
        };

        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            result.code = SystemStatusCode::UnsupportedDomain;
            return result;
        } else {
            const auto validation_code = validateSpriteInputs(
                transforms_,
                local_bounds_,
                visibility_masks_,
                sprites_,
                world_transforms_,
                world_bounds_,
                visible_items_,
                draw_commands_,
                batch_commands_);
            if (validation_code != SystemStatusCode::Ok) {
                result.code = validation_code;
                return result;
            }

            if (!shouldParallelize(static_cast<U32>(transforms_.size()))) {
                result.code = runSingleThreadedSpritePipeline(
                    camera_,
                    transforms_,
                    local_bounds_,
                    visibility_masks_,
                    sprites_,
                    world_transforms_,
                    world_bounds_,
                    visible_items_,
                    draw_commands_,
                    batch_commands_,
                    result);
                return result;
            }

            result.code = runSpatialAndCulling(
                camera_,
                transforms_,
                local_bounds_,
                visibility_masks_,
                world_transforms_,
                world_bounds_,
                visible_items_,
                result);
            if (result.code != SystemStatusCode::Ok) {
                return result;
            }

            if (draw_commands_.size() < result.visible_count) {
                result.code = SystemStatusCode::InsufficientCapacity;
                return result;
            }

            result.code = runCommandBuild(
                std::span<const VisibleItemType>{visible_items_.data(), result.visible_count},
                sprites_,
                draw_commands_);
            if (result.code != SystemStatusCode::Ok) {
                return result;
            }
            result.draw_count = result.visible_count;

            const auto batch_result = BatchSystem<Provider, Dim>::run(
                std::span<const DrawCommandType>{draw_commands_.data(), result.draw_count},
                batch_commands_);
            result.code = batch_result.code;
            result.batch_count = batch_result.write_count;
            return result;
        }
    }

private:
    [[nodiscard]] static auto normalizeConfig(ThreadedCpuPipelineConfig config_) noexcept
        -> ThreadedCpuPipelineConfig
    {
        if (config_.min_items_per_task == 0U) {
            config_.min_items_per_task = 1U;
        }
        return config_;
    }

    // Single-thread reference path used below the parallel threshold. Runs the
    // same deterministic systems in sequence as the threaded path's per-chunk
    // work, so its output is byte-identical; it simply avoids the executor.
    [[nodiscard]] auto runSingleThreadedSpritePipeline(
        const CameraType& camera_,
        std::span<const TransformType> transforms_,
        std::span<const LocalBoundsType> local_bounds_,
        std::span<const VisibilityMaskType> visibility_masks_,
        std::span<const SpriteType> sprites_,
        std::span<WorldTransformType> world_transforms_,
        std::span<WorldBoundsType> world_bounds_,
        std::span<VisibleItemType> visible_items_,
        std::span<DrawCommandType> draw_commands_,
        std::span<BatchCommandType> batch_commands_,
        ThreadedSpritePipelineResult& result_) const -> SystemStatusCode
    {
        const auto item_count = transforms_.size();

        auto system_result = TransformSystem<Provider, Dim>::run(
            transforms_,
            world_transforms_.subspan(0U, item_count));
        if (!isOk(system_result)) {
            return system_result.code;
        }
        result_.transform_count = static_cast<U32>(item_count);

        system_result = BoundsSystem<Provider, Dim>::run(
            world_transforms_.subspan(0U, item_count),
            local_bounds_,
            world_bounds_.subspan(0U, item_count));
        if (!isOk(system_result)) {
            return system_result.code;
        }
        result_.bounds_count = static_cast<U32>(local_bounds_.size());

        system_result = CullingSystem<Provider, Dim>::run(
            camera_,
            world_bounds_.subspan(0U, item_count),
            visibility_masks_,
            visible_items_);
        if (!isOk(system_result)) {
            return system_result.code;
        }
        result_.visible_count = system_result.write_count;

        if (draw_commands_.size() < result_.visible_count) {
            return SystemStatusCode::InsufficientCapacity;
        }

        system_result = CommandBuildSystem<Provider, Dim>::run(
            std::span<const VisibleItemType>{visible_items_.data(), result_.visible_count},
            sprites_,
            draw_commands_);
        if (!isOk(system_result)) {
            return system_result.code;
        }
        result_.draw_count = system_result.write_count;

        const auto batch_result = BatchSystem<Provider, Dim>::run(
            std::span<const DrawCommandType>{draw_commands_.data(), result_.draw_count},
            batch_commands_);
        result_.batch_count = batch_result.write_count;
        return batch_result.code;
    }

    [[nodiscard]] static auto makeExecutorConfig(ThreadedCpuPipelineConfig config_) noexcept
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

    [[nodiscard]] static auto isOk(SystemResult result_) noexcept -> bool
    {
        return result_.code == SystemStatusCode::Ok;
    }

    [[nodiscard]] static auto validateSpriteInputs(
        std::span<const TransformType> transforms_,
        std::span<const LocalBoundsType> local_bounds_,
        std::span<const VisibilityMaskType> visibility_masks_,
        std::span<const SpriteType> sprites_,
        std::span<WorldTransformType> world_transforms_,
        std::span<WorldBoundsType> world_bounds_,
        std::span<VisibleItemType> visible_items_,
        std::span<DrawCommandType> draw_commands_,
        std::span<BatchCommandType> batch_commands_) noexcept -> SystemStatusCode
    {
        if (!isSystemResultCountRepresentable(transforms_.size()) ||
            !isSystemResultCountRepresentable(local_bounds_.size()) ||
            !isSystemResultCountRepresentable(visibility_masks_.size()) ||
            !isSystemResultCountRepresentable(sprites_.size()) ||
            !isSystemResultCountRepresentable(world_transforms_.size()) ||
            !isSystemResultCountRepresentable(world_bounds_.size()) ||
            !isSystemResultCountRepresentable(visible_items_.size()) ||
            !isSystemResultCountRepresentable(draw_commands_.size()) ||
            !isSystemResultCountRepresentable(batch_commands_.size())) {
            return SystemStatusCode::InvalidInput;
        }
        if (transforms_.size() != local_bounds_.size() || transforms_.size() > sprites_.size()) {
            return SystemStatusCode::InvalidInput;
        }
        if (!visibility_masks_.empty() && visibility_masks_.size() != transforms_.size()) {
            return SystemStatusCode::InvalidInput;
        }
        if (world_transforms_.size() < transforms_.size() ||
            world_bounds_.size() < transforms_.size()) {
            return SystemStatusCode::InsufficientCapacity;
        }
        return SystemStatusCode::Ok;
    }

    [[nodiscard]] auto runSpatialAndCulling(
        const CameraType& camera_,
        std::span<const TransformType> transforms_,
        std::span<const LocalBoundsType> local_bounds_,
        std::span<const VisibilityMaskType> visibility_masks_,
        std::span<WorldTransformType> world_transforms_,
        std::span<WorldBoundsType> world_bounds_,
        std::span<VisibleItemType> visible_items_,
        ThreadedSpritePipelineResult& result_) -> SystemStatusCode
    {
        rebuildChunks(transforms_.size());
        resizeCullingScratch(transforms_.size(), chunks_.size());

        if (transforms_.empty()) {
            return SystemStatusCode::Ok;
        }

        auto stage_error = std::atomic<U32>{statusToU32(SystemStatusCode::Ok)};
        auto plan = center_.makePlan();

        const auto transform_task = plan.parallelFor<Usize>(
            makeTaskDesc("render2d.threaded.transform"),
            0U,
            chunks_.size(),
            1U,
            [this, transforms_, world_transforms_, &stage_error](Usize chunk_index_) {
                const auto chunk = chunks_[chunk_index_];
                const auto first = static_cast<Usize>(chunk.first);
                const auto count = static_cast<Usize>(chunk.count);
                const auto system_result = TransformSystem<Provider, Dim>::run(
                    transforms_.subspan(first, count),
                    world_transforms_.subspan(first, count));
                recordStageError(stage_error, system_result);
            });

        const auto bounds_task = plan.parallelFor<Usize>(
            makeTaskDesc("render2d.threaded.bounds"),
            0U,
            chunks_.size(),
            1U,
            [this, local_bounds_, world_transforms_, world_bounds_, &stage_error](
                Usize chunk_index_) {
                const auto chunk = chunks_[chunk_index_];
                const auto first = static_cast<Usize>(chunk.first);
                const auto count = static_cast<Usize>(chunk.count);
                const auto system_result = BoundsSystem<Provider, Dim>::run(
                    world_transforms_.subspan(first, count),
                    local_bounds_.subspan(first, count),
                    world_bounds_.subspan(first, count));
                recordStageError(stage_error, system_result);
            });

        const auto culling_task = plan.parallelFor<Usize>(
            makeTaskDesc("render2d.threaded.culling"),
            0U,
            chunks_.size(),
            1U,
            [this, camera_, visibility_masks_, world_bounds_, &stage_error](Usize chunk_index_) {
                const auto chunk = chunks_[chunk_index_];
                const auto first = static_cast<Usize>(chunk.first);
                const auto count = static_cast<Usize>(chunk.count);
                const auto visibility_masks = visibility_masks_.empty() ?
                    std::span<const VisibilityMaskType>{} :
                    visibility_masks_.subspan(first, count);
                const auto system_result = CullingSystem<Provider, Dim>::run(
                    camera_,
                    world_bounds_.subspan(first, count),
                    visibility_masks,
                    std::span<VisibleItemType>{culling_scratch_.data() + first, count});
                recordStageError(stage_error, system_result);
                visible_counts_[chunk_index_] = system_result.write_count;
                for (Usize item_index = 0U; item_index < system_result.write_count; ++item_index) {
                    auto& item = culling_scratch_[first + item_index];
                    item.source_index += chunk.first;
                    item.sort_key += chunk.first;
                }
            });

        plan.precede(transform_task, bounds_task);
        plan.precede(bounds_task, culling_task);

        auto run_handle = center_.dispatch(plan);
        run_handle.wait();
        center_.waitIdle();

        const auto status = statusFromU32(stage_error.load(std::memory_order_relaxed));
        if (status != SystemStatusCode::Ok) {
            return status;
        }

        result_.transform_count = static_cast<U32>(transforms_.size());
        result_.bounds_count = static_cast<U32>(local_bounds_.size());
        return mergeVisibleItems(visible_items_, result_);
    }

    [[nodiscard]] auto runCommandBuild(
        std::span<const VisibleItemType> visible_items_,
        std::span<const SpriteType> sprites_,
        std::span<DrawCommandType> draw_commands_) -> SystemStatusCode
    {
        rebuildChunks(visible_items_.size());
        if (visible_items_.empty()) {
            return SystemStatusCode::Ok;
        }

        auto stage_error = std::atomic<U32>{statusToU32(SystemStatusCode::Ok)};
        auto plan = center_.makePlan();
        static_cast<void>(plan.parallelFor<Usize>(
            makeTaskDesc("render2d.threaded.command_build"),
            0U,
            chunks_.size(),
            1U,
            [this, visible_items_, sprites_, draw_commands_, &stage_error](Usize chunk_index_) {
                const auto chunk = chunks_[chunk_index_];
                const auto first = static_cast<Usize>(chunk.first);
                const auto count = static_cast<Usize>(chunk.count);
                const auto system_result = CommandBuildSystem<Provider, Dim>::run(
                    visible_items_.subspan(first, count),
                    sprites_,
                    draw_commands_.subspan(first, count));
                recordStageError(stage_error, system_result);
                for (Usize draw_index = 0U; draw_index < system_result.write_count; ++draw_index) {
                    draw_commands_[first + draw_index].instance_first += chunk.first;
                }
            }));

        auto run_handle = center_.dispatch(plan);
        run_handle.wait();
        center_.waitIdle();
        return statusFromU32(stage_error.load(std::memory_order_relaxed));
    }

    static void recordStageError(
        std::atomic<U32>& stage_error_,
        SystemResult result_) noexcept
    {
        if (isOk(result_)) {
            return;
        }

        auto expected = statusToU32(SystemStatusCode::Ok);
        static_cast<void>(stage_error_.compare_exchange_strong(
            expected,
            statusToU32(result_.code),
            std::memory_order_relaxed,
            std::memory_order_relaxed));
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

    void resizeCullingScratch(Usize item_count_, Usize chunk_count_)
    {
        culling_scratch_.resize(item_count_);
        visible_counts_.resize(chunk_count_);
        for (Usize index = 0U; index < visible_counts_.size(); ++index) {
            visible_counts_[index] = 0U;
        }
    }

    [[nodiscard]] auto mergeVisibleItems(
        std::span<VisibleItemType> visible_items_,
        ThreadedSpritePipelineResult& result_) noexcept -> SystemStatusCode
    {
        Usize total_visible_count = 0U;
        for (Usize chunk_index = 0U; chunk_index < chunks_.size(); ++chunk_index) {
            total_visible_count += visible_counts_[chunk_index];
        }

        if (visible_items_.size() < total_visible_count) {
            result_.visible_count = static_cast<U32>(visible_items_.size());
            return SystemStatusCode::InsufficientCapacity;
        }

        Usize write_index = 0U;
        for (Usize chunk_index = 0U; chunk_index < chunks_.size(); ++chunk_index) {
            const auto chunk = chunks_[chunk_index];
            const auto first = static_cast<Usize>(chunk.first);
            const Usize visible_count = visible_counts_[chunk_index];
            for (Usize item_index = 0U; item_index < visible_count; ++item_index) {
                visible_items_[write_index] = culling_scratch_[first + item_index];
                ++write_index;
            }
        }

        result_.visible_count = static_cast<U32>(write_index);
        return SystemStatusCode::Ok;
    }

    ThreadedCpuPipelineConfig config_;
    ThreadCenter::Center center_;
    McVector<RangeU32> chunks_;
    McVector<VisibleItemType> culling_scratch_;
    McVector<U32> visible_counts_;
};

} // namespace Render2D
