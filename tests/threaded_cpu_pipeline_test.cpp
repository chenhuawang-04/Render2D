#include <Render2D/System/ThreadedCpuPipeline.hpp>
#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <span>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Transform = R2D::Transform<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using WorldBounds = R2D::WorldBounds<Provider, Dim>;
using Camera = R2D::Camera<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using PipelineRuntime = R2D::ThreadedCpuPipelineRuntime<Provider, Dim>;

inline constexpr R2D::U32 kItemCount = 16U;
inline constexpr R2D::U32 kVisibleMask = 0xFFFF'FFFFU;
inline constexpr R2D::U32 kHiddenMask = 0U;

inline constexpr Camera kCamera{
    .source_id = 0U,
    .position_x = 0.0F,
    .position_y = 0.0F,
    .rotation_radians = 0.0F,
    .viewport_width = 10.0F,
    .viewport_height = 10.0F,
    .near_z = 0.0F,
    .far_z = 1.0F,
    .layer_mask = kVisibleMask,
    .flags = 0U,
};

struct PipelineBuffers {
    std::array<WorldTransform, kItemCount> world_transforms{};
    std::array<WorldBounds, kItemCount> world_bounds{};
    std::array<VisibleItem, kItemCount> visible_items{};
    std::array<DrawCommand, kItemCount> draw_commands{};
    std::array<BatchCommand, kItemCount> batch_commands{};
};

struct ReferenceCounts {
    R2D::U32 visible_count;
    R2D::U32 draw_count;
    R2D::U32 batch_count;
};

[[nodiscard]] constexpr auto hiddenPosition(R2D::U32 index_) noexcept -> float
{
    return index_ % 4U == 0U ? 100.0F : 0.0F;
}

void fillInputs(
    std::array<Transform, kItemCount>& transforms_,
    std::array<LocalBounds, kItemCount>& local_bounds_,
    std::array<VisibilityMask, kItemCount>& visibility_masks_,
    std::array<Sprite, kItemCount>& sprites_) noexcept
{
    for (R2D::U32 index = 0U; index < kItemCount; ++index) {
        const bool hidden_by_position = (index % 4U) == 0U;
        const bool hidden_by_mask = (index % 5U) == 0U;
        const auto grid_y = static_cast<float>(index) / 3.0F;
        transforms_[index] = {
            .source_id = index,
            .position_x = hidden_by_position ? hiddenPosition(index) : static_cast<float>(index % 3U),
            .position_y = hidden_by_position ? hiddenPosition(index) : grid_y,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        };
        local_bounds_[index] = {
            .source_id = index,
            .bounds = R2D::makeAabb2(-0.25F, -0.25F, 0.25F, 0.25F),
        };
        visibility_masks_[index] = {.mask = hidden_by_mask ? kHiddenMask : kVisibleMask};
        sprites_[index] = {
            .source_id = index,
            .texture_id = static_cast<R2D::U32>((index / 2U) % 3U),
            .texture_generation = 0U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = static_cast<R2D::U32>((index / 4U) % 2U),
            .material_generation = 0U,
            .color_rgba8 = kVisibleMask,
            .layer = static_cast<R2D::U32>(index % 2U),
            .flags = 0U,
        };
    }
}

[[nodiscard]] auto runReference(
    std::span<const Transform> transforms_,
    std::span<const LocalBounds> local_bounds_,
    std::span<const VisibilityMask> visibility_masks_,
    std::span<const Sprite> sprites_,
    PipelineBuffers& buffers_) noexcept -> ReferenceCounts
{
    auto result = R2D::TransformSystem<Provider, Dim>::run(
        transforms_,
        buffers_.world_transforms);
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.visible_count = 0U, .draw_count = 0U, .batch_count = 0U};
    }

    result = R2D::BoundsSystem<Provider, Dim>::run(
        buffers_.world_transforms,
        local_bounds_,
        buffers_.world_bounds);
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.visible_count = 0U, .draw_count = 0U, .batch_count = 0U};
    }

    result = R2D::CullingSystem<Provider, Dim>::run(
        kCamera,
        buffers_.world_bounds,
        visibility_masks_,
        buffers_.visible_items);
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.visible_count = 0U, .draw_count = 0U, .batch_count = 0U};
    }
    const auto visible_count = result.write_count;

    result = R2D::CommandBuildSystem<Provider, Dim>::run(
        std::span<const VisibleItem>{buffers_.visible_items.data(), visible_count},
        sprites_,
        buffers_.draw_commands);
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.visible_count = visible_count, .draw_count = 0U, .batch_count = 0U};
    }
    const auto draw_count = result.write_count;

    result = R2D::BatchSystem<Provider, Dim>::run(
        std::span<const DrawCommand>{buffers_.draw_commands.data(), draw_count},
        buffers_.batch_commands);
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.visible_count = visible_count, .draw_count = draw_count, .batch_count = 0U};
    }
    return {
        .visible_count = visible_count,
        .draw_count = draw_count,
        .batch_count = result.write_count,
    };
}

void checkPipelineEqual(
    R2DT::TestContext& context_,
    const PipelineBuffers& reference_,
    const PipelineBuffers& threaded_,
    ReferenceCounts counts_) noexcept
{
    for (R2D::Usize index = 0U; index < kItemCount; ++index) {
        R2D_TEST_CHECK_EQ(context_, threaded_.world_transforms[index].source_id, reference_.world_transforms[index].source_id);
        R2D_TEST_CHECK_EQ(context_, threaded_.world_bounds[index].source_id, reference_.world_bounds[index].source_id);
        R2D_TEST_CHECK(context_, R2D::aabb2NearEqual(threaded_.world_bounds[index].bounds, reference_.world_bounds[index].bounds, 0.0001F));
    }

    for (R2D::Usize index = 0U; index < counts_.visible_count; ++index) {
        R2D_TEST_CHECK_EQ(context_, threaded_.visible_items[index].source_index, reference_.visible_items[index].source_index);
        R2D_TEST_CHECK_EQ(context_, threaded_.visible_items[index].sort_key, reference_.visible_items[index].sort_key);
    }

    for (R2D::Usize index = 0U; index < counts_.draw_count; ++index) {
        R2D_TEST_CHECK_EQ(context_, threaded_.draw_commands[index].source_index, reference_.draw_commands[index].source_index);
        R2D_TEST_CHECK_EQ(context_, threaded_.draw_commands[index].instance_first, reference_.draw_commands[index].instance_first);
        R2D_TEST_CHECK_EQ(context_, threaded_.draw_commands[index].sort_key, reference_.draw_commands[index].sort_key);
    }

    for (R2D::Usize index = 0U; index < counts_.batch_count; ++index) {
        R2D_TEST_CHECK_EQ(context_, threaded_.batch_commands[index].draw_first, reference_.batch_commands[index].draw_first);
        R2D_TEST_CHECK_EQ(context_, threaded_.batch_commands[index].draw_count, reference_.batch_commands[index].draw_count);
        R2D_TEST_CHECK_EQ(context_, threaded_.batch_commands[index].sort_key, reference_.batch_commands[index].sort_key);
    }
}

[[nodiscard]] auto runTest() -> int
{
    R2DT::TestContext context{};

    std::array<Transform, kItemCount> transforms{};
    std::array<LocalBounds, kItemCount> local_bounds{};
    std::array<VisibilityMask, kItemCount> visibility_masks{};
    std::array<Sprite, kItemCount> sprites{};
    fillInputs(transforms, local_bounds, visibility_masks, sprites);

    PipelineBuffers reference_buffers{};
    const auto reference_counts = runReference(
        transforms,
        local_bounds,
        visibility_masks,
        sprites,
        reference_buffers);

    PipelineBuffers threaded_buffers{};
    auto runtime = PipelineRuntime{{
        .worker_count = 4U,
        .min_items_per_task = 3U,
        .parallel_threshold = 1U,
    }};
    R2D_TEST_CHECK_EQ(context, runtime.workerCount(), 4U);
    R2D_TEST_CHECK(context, runtime.shouldParallelize(kItemCount));

    const auto threaded_result = runtime.runSpritePipeline(
        kCamera,
        transforms,
        local_bounds,
        visibility_masks,
        sprites,
        threaded_buffers.world_transforms,
        threaded_buffers.world_bounds,
        threaded_buffers.visible_items,
        threaded_buffers.draw_commands,
        threaded_buffers.batch_commands);

    R2D_TEST_REQUIRE(context, threaded_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, threaded_result.transform_count, kItemCount);
    R2D_TEST_CHECK_EQ(context, threaded_result.bounds_count, kItemCount);
    R2D_TEST_CHECK_EQ(context, threaded_result.visible_count, reference_counts.visible_count);
    R2D_TEST_CHECK_EQ(context, threaded_result.draw_count, reference_counts.draw_count);
    R2D_TEST_CHECK_EQ(context, threaded_result.batch_count, reference_counts.batch_count);
    checkPipelineEqual(context, reference_buffers, threaded_buffers, reference_counts);

    PipelineBuffers single_worker_buffers{};
    auto single_worker_runtime = PipelineRuntime{{
        .worker_count = 1U,
        .min_items_per_task = 1U,
        .parallel_threshold = 1U,
    }};
    const auto single_worker_result = single_worker_runtime.runSpritePipeline(
        kCamera,
        transforms,
        local_bounds,
        visibility_masks,
        sprites,
        single_worker_buffers.world_transforms,
        single_worker_buffers.world_bounds,
        single_worker_buffers.visible_items,
        single_worker_buffers.draw_commands,
        single_worker_buffers.batch_commands);
    R2D_TEST_REQUIRE(context, single_worker_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, single_worker_result.visible_count, reference_counts.visible_count);
    checkPipelineEqual(context, reference_buffers, single_worker_buffers, reference_counts);

    // Stage 21E: a multi-worker runtime whose threshold is above the workload
    // must route to the single-thread reference path and still produce output
    // byte-identical to the reference.
    PipelineBuffers gated_buffers{};
    auto gated_runtime = PipelineRuntime{{
        .worker_count = 4U,
        .min_items_per_task = 3U,
        .parallel_threshold = 1000U,
    }};
    R2D_TEST_CHECK(context, !gated_runtime.shouldParallelize(kItemCount));
    R2D_TEST_CHECK(context, gated_runtime.shouldParallelize(1000U));
    const auto gated_result = gated_runtime.runSpritePipeline(
        kCamera,
        transforms,
        local_bounds,
        visibility_masks,
        sprites,
        gated_buffers.world_transforms,
        gated_buffers.world_bounds,
        gated_buffers.visible_items,
        gated_buffers.draw_commands,
        gated_buffers.batch_commands);
    R2D_TEST_REQUIRE(context, gated_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, gated_result.transform_count, kItemCount);
    R2D_TEST_CHECK_EQ(context, gated_result.bounds_count, kItemCount);
    R2D_TEST_CHECK_EQ(context, gated_result.visible_count, reference_counts.visible_count);
    R2D_TEST_CHECK_EQ(context, gated_result.draw_count, reference_counts.draw_count);
    R2D_TEST_CHECK_EQ(context, gated_result.batch_count, reference_counts.batch_count);
    checkPipelineEqual(context, reference_buffers, gated_buffers, reference_counts);

    std::array<VisibleItem, 1U> short_visible_items{};
    const auto capacity_result = runtime.runSpritePipeline(
        kCamera,
        transforms,
        local_bounds,
        visibility_masks,
        sprites,
        threaded_buffers.world_transforms,
        threaded_buffers.world_bounds,
        short_visible_items,
        threaded_buffers.draw_commands,
        threaded_buffers.batch_commands);
    R2D_TEST_CHECK(context, capacity_result.code == R2D::SystemStatusCode::InsufficientCapacity);

    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("threaded_cpu_pipeline_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("threaded_cpu_pipeline_test unknown exception\n", stderr);
        return 1;
    }
}
