#include <Render2D/System/ThreadedCpuPipeline.hpp>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <span>
#include <string_view>
#include <system_error>

namespace {

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Transform = R2D::Transform<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using WorldBounds = R2D::WorldBounds<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using PipelineRuntime = R2D::ThreadedCpuPipelineRuntime<Provider, Dim>;

inline constexpr R2D::U32 kDefaultSpriteCount = 10000U;
inline constexpr R2D::U32 kDefaultFrameCount = 8U;
inline constexpr R2D::U32 kDefaultWarmupCount = 2U;
inline constexpr R2D::U32 kDefaultMinItemsPerTask = 1024U;
inline constexpr R2D::U32 kHiddenSpriteStride = 8U;
inline constexpr R2D::Camera<Provider, Dim> kBenchmarkCamera{
    .source_id = 0U,
    .position_x = 0.0F,
    .position_y = 0.0F,
    .rotation_radians = 0.0F,
    .viewport_width = 96.0F,
    .viewport_height = 96.0F,
    .near_z = 0.0F,
    .far_z = 1.0F,
    .layer_mask = 0xFFFFFFFFU,
    .flags = 0U,
};

enum class VisibilityMode : R2D::U8 {
    High = 0,
    Low = 1,
};

struct BenchConfig {
    R2D::U32 sprite_count;
    R2D::U32 frame_count;
    R2D::U32 warmup_count;
    R2D::U32 worker_count;
    R2D::U32 min_items_per_task;
    VisibilityMode visibility;
};

struct BenchCounts {
    R2D::U32 visible_count;
    R2D::U32 draw_count;
    R2D::U32 batch_count;
};

struct BenchTotals {
    double reference_total_ms;
    double threaded_total_ms;
    BenchCounts reference_counts;
    BenchCounts threaded_counts;
};

struct BenchState {
    R2D::McVector<Transform> transforms;
    R2D::McVector<LocalBounds> local_bounds;
    R2D::McVector<VisibilityMask> visibility_masks;
    R2D::McVector<Sprite> sprites;

    R2D::McVector<WorldTransform> reference_world_transforms;
    R2D::McVector<WorldBounds> reference_world_bounds;
    R2D::McVector<VisibleItem> reference_visible_items;
    R2D::McVector<DrawCommand> reference_draw_commands;
    R2D::McVector<BatchCommand> reference_batch_commands;

    R2D::McVector<WorldTransform> threaded_world_transforms;
    R2D::McVector<WorldBounds> threaded_world_bounds;
    R2D::McVector<VisibleItem> threaded_visible_items;
    R2D::McVector<DrawCommand> threaded_draw_commands;
    R2D::McVector<BatchCommand> threaded_batch_commands;
};

[[nodiscard]] double elapsedMs(
    std::chrono::steady_clock::time_point start_,
    std::chrono::steady_clock::time_point end_) noexcept
{
    return std::chrono::duration<double, std::milli>(end_ - start_).count();
}

[[nodiscard]] const char* visibilityName(VisibilityMode visibility_) noexcept
{
    switch (visibility_) {
    case VisibilityMode::High:
        return "high";
    case VisibilityMode::Low:
        return "low";
    }
    return "unknown";
}

[[nodiscard]] bool parseU32(std::string_view text_, R2D::U32& out_value_) noexcept
{
    if (text_.empty() || text_.front() < '0' || text_.front() > '9') {
        return false;
    }

    unsigned long long value = 0ULL;
    const char* const first = text_.data();
    const char* const last = text_.data() + text_.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last || value > 0xFFFFFFFFULL) {
        return false;
    }

    out_value_ = static_cast<R2D::U32>(value);
    return true;
}

[[nodiscard]] bool parseVisibility(std::string_view text_, VisibilityMode& out_visibility_) noexcept
{
    if (text_ == "high") {
        out_visibility_ = VisibilityMode::High;
        return true;
    }
    if (text_ == "low") {
        out_visibility_ = VisibilityMode::Low;
        return true;
    }
    return false;
}

[[nodiscard]] bool parseConfig(int argc_, char** argv_, BenchConfig& out_config_) noexcept
{
    out_config_ = {
        .sprite_count = kDefaultSpriteCount,
        .frame_count = kDefaultFrameCount,
        .warmup_count = kDefaultWarmupCount,
        .worker_count = 0U,
        .min_items_per_task = kDefaultMinItemsPerTask,
        .visibility = VisibilityMode::High,
    };

    for (int index = 1; index < argc_; ++index) {
        const std::string_view option{argv_[index]};
        if (option == "--sprites" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.sprite_count)) {
                return false;
            }
            ++index;
        } else if (option == "--frames" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.frame_count)) {
                return false;
            }
            ++index;
        } else if (option == "--warmup" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.warmup_count)) {
                return false;
            }
            ++index;
        } else if (option == "--workers" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.worker_count)) {
                return false;
            }
            ++index;
        } else if (option == "--min-items-per-task" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.min_items_per_task)) {
                return false;
            }
            ++index;
        } else if (option == "--visibility" && index + 1 < argc_) {
            if (!parseVisibility(argv_[index + 1], out_config_.visibility)) {
                return false;
            }
            ++index;
        } else {
            return false;
        }
    }

    return out_config_.sprite_count != 0U &&
        out_config_.frame_count != 0U &&
        out_config_.min_items_per_task != 0U;
}

void printUsage() noexcept
{
    static_cast<void>(std::fputs(
        "Render2D threaded CPU pipeline benchmark\n"
        "Options:\n"
        "  --sprites <count>             (>0)\n"
        "  --frames <count>              (>0)\n"
        "  --warmup <count>              (>=0)\n"
        "  --workers <count>             (0 uses ThreadCenter default)\n"
        "  --min-items-per-task <count>  (>0)\n"
        "  --visibility high|low\n",
        stderr));
}

[[nodiscard]] bool isHiddenLowVisibilitySprite(
    const BenchConfig& config_,
    R2D::U32 index_) noexcept
{
    return config_.visibility == VisibilityMode::Low &&
        index_ % kHiddenSpriteStride != 0U;
}

void fillInputs(const BenchConfig& config_, BenchState& state_) noexcept
{
    for (R2D::U32 index = 0U; index < config_.sprite_count; ++index) {
        const bool hidden = isHiddenLowVisibilitySprite(config_, index);
        const auto grid_x = static_cast<float>(index % 64U) - 32.0F;
        const auto grid_y = static_cast<float>((index / 64U) % 64U) - 32.0F;
        state_.transforms[index] = {
            .source_id = index,
            .position_x = hidden ? 10000.0F + grid_x : grid_x,
            .position_y = hidden ? 10000.0F + grid_y : grid_y,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        };
        state_.local_bounds[index] = {
            .source_id = index,
            .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F),
        };
        state_.visibility_masks[index] = {.mask = hidden ? 0U : 0xFFFFFFFFU};
        state_.sprites[index] = {
            .source_id = index,
            .texture_id = static_cast<R2D::U32>((index / 128U) % 8U),
            .texture_generation = 0U,
            .material_id = static_cast<R2D::U32>((index / 128U) % 4U),
            .material_generation = 0U,
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = static_cast<R2D::U32>((index / 128U) % 4U),
            .flags = 0U,
        };
    }
}

[[nodiscard]] BenchState makeState(const BenchConfig& config_)
{
    BenchState state{};
    state.transforms.resize(config_.sprite_count);
    state.local_bounds.resize(config_.sprite_count);
    state.visibility_masks.resize(config_.sprite_count);
    state.sprites.resize(config_.sprite_count);
    state.reference_world_transforms.resize(config_.sprite_count);
    state.reference_world_bounds.resize(config_.sprite_count);
    state.reference_visible_items.resize(config_.sprite_count);
    state.reference_draw_commands.resize(config_.sprite_count);
    state.reference_batch_commands.resize(config_.sprite_count);
    state.threaded_world_transforms.resize(config_.sprite_count);
    state.threaded_world_bounds.resize(config_.sprite_count);
    state.threaded_visible_items.resize(config_.sprite_count);
    state.threaded_draw_commands.resize(config_.sprite_count);
    state.threaded_batch_commands.resize(config_.sprite_count);
    fillInputs(config_, state);
    return state;
}

[[nodiscard]] bool isOk(R2D::SystemResult result_) noexcept
{
    return result_.code == R2D::SystemStatusCode::Ok;
}

[[nodiscard]] bool runReferencePipeline(BenchState& state_, BenchCounts& out_counts_) noexcept
{
    auto result = R2D::TransformSystem<Provider, Dim>::run(
        state_.transforms,
        state_.reference_world_transforms);
    if (!isOk(result)) {
        return false;
    }

    result = R2D::BoundsSystem<Provider, Dim>::run(
        state_.reference_world_transforms,
        state_.local_bounds,
        state_.reference_world_bounds);
    if (!isOk(result)) {
        return false;
    }

    result = R2D::CullingSystem<Provider, Dim>::run(
        kBenchmarkCamera,
        state_.reference_world_bounds,
        state_.visibility_masks,
        state_.reference_visible_items);
    if (!isOk(result)) {
        return false;
    }
    out_counts_.visible_count = result.write_count;

    result = R2D::CommandBuildSystem<Provider, Dim>::run(
        std::span<const VisibleItem>{state_.reference_visible_items.data(), out_counts_.visible_count},
        state_.sprites,
        state_.reference_draw_commands);
    if (!isOk(result)) {
        return false;
    }
    out_counts_.draw_count = result.write_count;

    result = R2D::BatchSystem<Provider, Dim>::run(
        std::span<const DrawCommand>{state_.reference_draw_commands.data(), out_counts_.draw_count},
        state_.reference_batch_commands);
    if (!isOk(result)) {
        return false;
    }
    out_counts_.batch_count = result.write_count;
    return true;
}

[[nodiscard]] bool runThreadedPipeline(
    PipelineRuntime& runtime_,
    BenchState& state_,
    BenchCounts& out_counts_)
{
    const auto result = runtime_.runSpritePipeline(
        kBenchmarkCamera,
        state_.transforms,
        state_.local_bounds,
        state_.visibility_masks,
        state_.sprites,
        state_.threaded_world_transforms,
        state_.threaded_world_bounds,
        state_.threaded_visible_items,
        state_.threaded_draw_commands,
        state_.threaded_batch_commands);
    if (result.code != R2D::SystemStatusCode::Ok) {
        return false;
    }

    out_counts_ = {
        .visible_count = result.visible_count,
        .draw_count = result.draw_count,
        .batch_count = result.batch_count,
    };
    return true;
}

[[nodiscard]] bool countsMatch(const BenchCounts& left_, const BenchCounts& right_) noexcept
{
    return left_.visible_count == right_.visible_count &&
        left_.draw_count == right_.draw_count &&
        left_.batch_count == right_.batch_count;
}

[[nodiscard]] bool runFrame(
    PipelineRuntime& runtime_,
    BenchState& state_,
    bool collect_,
    BenchTotals& totals_)
{
    BenchCounts reference_counts{};
    auto start = std::chrono::steady_clock::now();
    if (!runReferencePipeline(state_, reference_counts)) {
        return false;
    }
    auto end = std::chrono::steady_clock::now();
    if (collect_) {
        totals_.reference_total_ms += elapsedMs(start, end);
        totals_.reference_counts = reference_counts;
    }

    BenchCounts threaded_counts{};
    start = std::chrono::steady_clock::now();
    if (!runThreadedPipeline(runtime_, state_, threaded_counts)) {
        return false;
    }
    end = std::chrono::steady_clock::now();
    if (!countsMatch(reference_counts, threaded_counts)) {
        return false;
    }
    if (collect_) {
        totals_.threaded_total_ms += elapsedMs(start, end);
        totals_.threaded_counts = threaded_counts;
    }

    return true;
}

[[nodiscard]] bool runBenchmark(
    const BenchConfig& config_,
    PipelineRuntime& runtime_,
    BenchState& state_,
    BenchTotals& totals_)
{
    for (R2D::U32 warmup_index = 0U; warmup_index < config_.warmup_count; ++warmup_index) {
        if (!runFrame(runtime_, state_, false, totals_)) {
            return false;
        }
    }

    for (R2D::U32 frame_index = 0U; frame_index < config_.frame_count; ++frame_index) {
        if (!runFrame(runtime_, state_, true, totals_)) {
            return false;
        }
    }

    return true;
}

void printReport(
    const BenchConfig& config_,
    const PipelineRuntime& runtime_,
    const BenchTotals& totals_) noexcept
{
    const auto divisor = static_cast<double>(config_.frame_count);
    const double reference_average_ms = totals_.reference_total_ms / divisor;
    const double threaded_average_ms = totals_.threaded_total_ms / divisor;
    const double speedup = threaded_average_ms > 0.0 ?
        reference_average_ms / threaded_average_ms :
        0.0;

    static_cast<void>(std::printf(
        "sprites,visibility,frames,warmup,requested_workers,actual_workers,min_items_per_task,"
        "visible,draws,batches,avg_reference_total_ms,avg_threaded_total_ms,threaded_speedup\n"
        "%u,%s,%u,%u,%u,%u,%u,%u,%u,%u,%.9f,%.9f,%.9f\n",
        config_.sprite_count,
        visibilityName(config_.visibility),
        config_.frame_count,
        config_.warmup_count,
        config_.worker_count,
        runtime_.workerCount(),
        config_.min_items_per_task,
        totals_.threaded_counts.visible_count,
        totals_.threaded_counts.draw_count,
        totals_.threaded_counts.batch_count,
        reference_average_ms,
        threaded_average_ms,
        speedup));
}

} // namespace

int main(int argc_, char** argv_)
{
    try {
        BenchConfig config{};
        if (!parseConfig(argc_, argv_, config)) {
            printUsage();
            return 1;
        }

        auto state = makeState(config);
        auto runtime = PipelineRuntime{{
            .worker_count = config.worker_count,
            .min_items_per_task = config.min_items_per_task,
        }};
        BenchTotals totals{
            .reference_total_ms = 0.0,
            .threaded_total_ms = 0.0,
            .reference_counts = {},
            .threaded_counts = {},
        };
        if (!runBenchmark(config, runtime, state, totals)) {
            static_cast<void>(std::fputs("threaded benchmark pipeline mismatch or failure\n", stderr));
            return 1;
        }

        printReport(config, runtime, totals);
    } catch (const std::exception& exception) {
        static_cast<void>(std::fputs("threaded benchmark exception: ", stderr));
        static_cast<void>(std::fputs(exception.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("threaded benchmark unknown exception\n", stderr));
        return 1;
    }

    return 0;
}
