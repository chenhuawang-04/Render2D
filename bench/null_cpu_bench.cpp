#include "support/BenchmarkFramework.hpp"

#include <Render2D/Render2D.hpp>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace {

namespace R2D = Render2D;
namespace R2DB = Render2D::Bench;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Transform = R2D::Transform<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using WorldBounds = R2D::WorldBounds<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using Text = R2D::Text<Provider, Dim>;
using TextState = R2D::TextState<Provider, Dim>;
using TextDirtyRange = R2D::TextDirtyRange<Provider, Dim>;
using FontAtlasRef = R2D::FontAtlasRef<Provider, Dim>;
using GlyphRun = R2D::GlyphRun<Provider, Dim>;
using GlyphInstance = R2D::GlyphInstance<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using CommandBuffer = R2D::CommandBuffer<Provider, Dim>;
using Camera = R2D::Camera<Provider, Dim>;

inline constexpr R2D::U32 kAtlasCount = 4U;
inline constexpr R2D::U32 kHiddenSpriteStride = 8U;
inline constexpr Camera kBenchmarkCamera{
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

struct ActiveCounts {
    R2D::Usize sprite_count;
    R2D::Usize text_count;
    R2D::Usize glyph_count;
    R2D::Usize draw_capacity;
};

struct BenchState {
    R2D::McVector<Transform> transforms;
    R2D::McVector<WorldTransform> world_transforms;
    R2D::McVector<LocalBounds> local_bounds;
    R2D::McVector<WorldBounds> world_bounds;
    R2D::McVector<VisibilityMask> visibility_masks;
    R2D::McVector<VisibleItem> visible_items;
    R2D::McVector<Sprite> sprites;

    R2D::McVector<Text> texts;
    R2D::McVector<TextState> previous_text_states;
    R2D::McVector<TextState> next_text_states;
    R2D::McVector<TextDirtyRange> dirty_ranges;
    R2D::McVector<FontAtlasRef> font_atlases;
    R2D::McVector<GlyphRun> glyph_runs;
    R2D::McVector<GlyphInstance> glyph_instances;

    R2D::McVector<DrawCommand> draw_commands;
    R2D::McVector<BatchCommand> batch_commands;
    R2D::McVector<CommandBuffer> command_buffers;

    bool text_state_initialized = false;
};

[[nodiscard]] bool hasOption(int argc_, char** argv_, std::string_view option_) noexcept
{
    for (int index = 1; index < argc_; ++index) {
        if (std::string_view{argv_[index]} == option_) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool hasSpritePath(const R2DB::BenchmarkConfig& config_) noexcept
{
    return config_.scenario != R2DB::BenchScenario::Text;
}

[[nodiscard]] bool hasTextPath(const R2DB::BenchmarkConfig& config_) noexcept
{
    return config_.scenario != R2DB::BenchScenario::Sprite;
}

void normalizeConfig(R2DB::BenchmarkConfig& config_) noexcept
{
    if (!hasSpritePath(config_)) {
        config_.sprite_count = 0U;
    }
    if (!hasTextPath(config_)) {
        config_.text_count = 0U;
    }
}

[[nodiscard]] bool makeActiveCounts(const R2DB::BenchmarkConfig& config_, ActiveCounts& out_counts_) noexcept
{
    constexpr R2D::Usize kMaxU32 = (std::numeric_limits<R2D::U32>::max)();

    const R2D::Usize sprite_count = hasSpritePath(config_) ? config_.sprite_count : 0U;
    const R2D::Usize text_count = hasTextPath(config_) ? config_.text_count : 0U;
    if (sprite_count > kMaxU32 || text_count > kMaxU32) {
        return false;
    }
    if (text_count != 0U && text_count > kMaxU32 / config_.glyphs_per_text) {
        return false;
    }

    const R2D::Usize glyph_count = text_count * config_.glyphs_per_text;
    if (sprite_count > kMaxU32 - text_count) {
        return false;
    }

    out_counts_ = {
        .sprite_count = sprite_count,
        .text_count = text_count,
        .glyph_count = glyph_count,
        .draw_capacity = sprite_count + text_count,
    };
    return true;
}

[[nodiscard]] R2D::Usize nonZeroCapacity(R2D::Usize value_) noexcept
{
    return value_ == 0U ? 1U : value_;
}

[[nodiscard]] bool isHiddenLowVisibilitySprite(
    const R2DB::BenchmarkConfig& config_,
    R2D::Usize index_) noexcept
{
    return config_.visibility == R2DB::VisibilityMode::Low &&
        index_ % kHiddenSpriteStride != 0U;
}

void fillSpriteInputs(const R2DB::BenchmarkConfig& config_, BenchState& state_) noexcept
{
    for (R2D::Usize index = 0U; index < state_.sprites.size(); ++index) {
        const bool hidden = isHiddenLowVisibilitySprite(config_, index);
        const auto source_id = static_cast<R2D::U32>(index);
        const auto grid_x = static_cast<float>(index % 64U) - 32.0F;
        const auto grid_y = static_cast<float>((index / 64U) % 64U) - 32.0F;
        const float x = hidden ? 10000.0F + grid_x : grid_x;
        const float y = hidden ? 10000.0F + grid_y : grid_y;

        state_.transforms[index] = {
            .source_id = source_id,
            .position_x = x,
            .position_y = y,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        };
        state_.local_bounds[index] = {
            .source_id = source_id,
            .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F),
        };
        state_.visibility_masks[index] = {.mask = hidden ? 0U : 0xFFFFFFFFU};
        state_.sprites[index] = {
            .source_id = source_id,
            .texture_id = static_cast<R2D::U32>((index / 128U) % 8U),
            .material_id = static_cast<R2D::U32>((index / 128U) % 4U),
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = static_cast<R2D::U32>((index / 128U) % 4U),
            .flags = 0U,
        };
    }
}

void fillTextInputs(const R2DB::BenchmarkConfig& config_, BenchState& state_) noexcept
{
    for (R2D::Usize index = 0U; index < state_.font_atlases.size(); ++index) {
        state_.font_atlases[index] = {
            .font_id = static_cast<R2D::U32>(index),
            .atlas_id = static_cast<R2D::U32>(100U + index),
            .generation = 1U,
            .texture_id = static_cast<R2D::U32>(200U + index),
            .flags = 0U,
        };
    }

    for (R2D::Usize index = 0U; index < state_.texts.size(); ++index) {
        const auto source_id = static_cast<R2D::U32>(index);
        const auto glyph_first = static_cast<R2D::U32>(index * config_.glyphs_per_text);
        state_.texts[index] = {
            .source_id = source_id,
            .font_id = static_cast<R2D::U32>(index % kAtlasCount),
            .utf8_buffer_id = 1U,
            .utf8_offset = glyph_first,
            .utf8_size = config_.glyphs_per_text,
            .color_rgba8 = 0xFFFFFFFFU,
            .pixel_size = 16.0F + static_cast<float>(index % 4U),
            .layer = static_cast<R2D::U32>((index / 64U) % 4U),
            .flags = 0U,
        };
    }
}

[[nodiscard]] BenchState makeState(
    const R2DB::BenchmarkConfig& config_,
    const ActiveCounts& counts_)
{
    BenchState state{};

    state.transforms.resize(counts_.sprite_count);
    state.world_transforms.resize(counts_.sprite_count);
    state.local_bounds.resize(counts_.sprite_count);
    state.world_bounds.resize(counts_.sprite_count);
    state.visibility_masks.resize(counts_.sprite_count);
    state.visible_items.resize(counts_.sprite_count);
    state.sprites.resize(counts_.sprite_count);

    state.texts.resize(counts_.text_count);
    state.previous_text_states.resize(counts_.text_count);
    state.next_text_states.resize(counts_.text_count);
    state.dirty_ranges.resize(counts_.text_count);
    state.font_atlases.resize(hasTextPath(config_) ? kAtlasCount : 0U);
    state.glyph_runs.resize(counts_.text_count);
    state.glyph_instances.resize(counts_.glyph_count);

    state.draw_commands.resize(nonZeroCapacity(counts_.draw_capacity));
    state.batch_commands.resize(nonZeroCapacity(counts_.draw_capacity));
    state.command_buffers.resize(1U);

    fillSpriteInputs(config_, state);
    fillTextInputs(config_, state);
    return state;
}

[[nodiscard]] bool isOk(R2D::SystemResult result_) noexcept
{
    return result_.code == R2D::SystemStatusCode::Ok;
}

[[nodiscard]] int reportSystemFailure(std::string_view stage_, R2D::SystemResult result_)
{
    std::cerr << "benchmark stage failed: " << stage_
              << " status=" << static_cast<int>(result_.code)
              << " read=" << result_.read_count
              << " write=" << result_.write_count << '\n';
    return 1;
}

void mutateDirtyTexts(
    const R2DB::BenchmarkConfig& config_,
    R2D::U32 frame_index_,
    BenchState& state_) noexcept
{
    if (config_.dirty_text_stride == 0U || state_.texts.empty()) {
        return;
    }

    const auto stride = static_cast<R2D::Usize>(config_.dirty_text_stride);
    const auto first = static_cast<R2D::Usize>(frame_index_ % config_.dirty_text_stride);
    for (R2D::Usize index = first; index < state_.texts.size(); index += stride) {
        const auto color_bits = static_cast<R2D::U32>((index + frame_index_ + 1U) & 0x00FFFFFFU);
        state_.texts[index].color_rgba8 = 0xFF000000U | color_bits;
    }
}

void mutateDirtyTransforms(
    const R2DB::BenchmarkConfig& config_,
    R2D::U32 frame_index_,
    BenchState& state_) noexcept
{
    if (config_.dirty_transform_stride == 0U || state_.transforms.empty()) {
        return;
    }

    const auto stride = static_cast<R2D::Usize>(config_.dirty_transform_stride);
    const auto first = static_cast<R2D::Usize>(frame_index_ % config_.dirty_transform_stride);
    const float frame_delta = static_cast<float>((frame_index_ & 0xFU) + 1U) * 0.001F;
    for (R2D::Usize index = first; index < state_.transforms.size(); index += stride) {
        state_.transforms[index].position_x += frame_delta;
        state_.transforms[index].position_y -= frame_delta;
        state_.transforms[index].rotation_radians += frame_delta;
    }
}

void copyTextStates(BenchState& state_) noexcept
{
    for (R2D::Usize index = 0U; index < state_.next_text_states.size(); ++index) {
        state_.previous_text_states[index] = state_.next_text_states[index];
    }
    state_.text_state_initialized = true;
}

[[nodiscard]] int runSpritePath(
    const R2DB::BenchmarkConfig& config_,
    R2D::U32 frame_index_,
    BenchState& state_,
    R2DB::BenchmarkTotals& frame_totals_)
{
    mutateDirtyTransforms(config_, frame_index_, state_);

    auto start = std::chrono::steady_clock::now();
    auto result = R2D::TransformSystem<Provider, Dim>::run(state_.transforms, state_.world_transforms);
    auto end = std::chrono::steady_clock::now();
    frame_totals_.times.transform_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("transform", result);
    }

    start = std::chrono::steady_clock::now();
    result = R2D::BoundsSystem<Provider, Dim>::run(
        state_.world_transforms,
        state_.local_bounds,
        state_.world_bounds);
    end = std::chrono::steady_clock::now();
    frame_totals_.times.bounds_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("bounds", result);
    }

    start = std::chrono::steady_clock::now();
    result = R2D::CullingSystem<Provider, Dim>::run(
        kBenchmarkCamera,
        state_.world_bounds,
        state_.visibility_masks,
        state_.visible_items);
    end = std::chrono::steady_clock::now();
    frame_totals_.times.culling_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("culling", result);
    }
    frame_totals_.visible_count = result.write_count;

    start = std::chrono::steady_clock::now();
    result = R2D::CommandBuildSystem<Provider, Dim>::run(
        std::span<const VisibleItem>{state_.visible_items.data(), frame_totals_.visible_count},
        state_.sprites,
        state_.draw_commands);
    end = std::chrono::steady_clock::now();
    frame_totals_.times.sprite_command_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("sprite_command", result);
    }
    frame_totals_.sprite_draw_count = result.write_count;
    frame_totals_.total_draw_count = result.write_count;
    return 0;
}

[[nodiscard]] int runTextPath(
    const R2DB::BenchmarkConfig& config_,
    R2D::U32 frame_index_,
    BenchState& state_,
    R2DB::BenchmarkTotals& frame_totals_)
{
    mutateDirtyTexts(config_, frame_index_, state_);

    const std::span<const TextState> previous_states = state_.text_state_initialized ?
        std::span<const TextState>{state_.previous_text_states.data(), state_.previous_text_states.size()} :
        std::span<const TextState>{};

    auto start = std::chrono::steady_clock::now();
    auto result = R2D::TextDirtySystem<Provider, Dim>::run(
        state_.texts,
        previous_states,
        state_.next_text_states,
        state_.dirty_ranges);
    auto end = std::chrono::steady_clock::now();
    frame_totals_.times.text_dirty_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("text_dirty", result);
    }
    frame_totals_.text_dirty_count = result.write_count;

    const std::span<const TextDirtyRange> dirty_ranges{
        state_.dirty_ranges.data(),
        frame_totals_.text_dirty_count,
    };

    start = std::chrono::steady_clock::now();
    result = R2D::GlyphRunBuildSystem<Provider, Dim>::runDirty(
        state_.texts,
        dirty_ranges,
        state_.font_atlases,
        state_.glyph_runs);
    end = std::chrono::steady_clock::now();
    frame_totals_.times.glyph_run_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("glyph_run", result);
    }

    start = std::chrono::steady_clock::now();
    result = R2D::GlyphInstanceBuildSystem<Provider, Dim>::runDirty(
        state_.glyph_runs,
        dirty_ranges,
        state_.texts,
        R2D::kDefaultGlyphBuildConfig,
        state_.glyph_instances);
    end = std::chrono::steady_clock::now();
    frame_totals_.times.glyph_instance_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("glyph_instance", result);
    }

    copyTextStates(state_);
    frame_totals_.glyph_count = static_cast<R2D::U32>(state_.glyph_instances.size());

    const auto draw_offset = static_cast<R2D::Usize>(frame_totals_.sprite_draw_count);
    const auto draw_capacity = state_.draw_commands.size() - draw_offset;
    start = std::chrono::steady_clock::now();
    result = R2D::GlyphBatchSystem<Provider, Dim>::run(
        state_.glyph_runs,
        state_.texts,
        state_.font_atlases,
        R2D::kDefaultGlyphDrawConfig,
        std::span<DrawCommand>{state_.draw_commands.data() + draw_offset, draw_capacity});
    end = std::chrono::steady_clock::now();
    frame_totals_.times.glyph_batch_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("glyph_batch", result);
    }

    frame_totals_.glyph_draw_count = result.write_count;
    frame_totals_.total_draw_count = frame_totals_.sprite_draw_count + result.write_count;
    return 0;
}

[[nodiscard]] int runCommonPath(
    R2D::U32 frame_index_,
    BenchState& state_,
    R2DB::BenchmarkTotals& frame_totals_)
{
    auto start = std::chrono::steady_clock::now();
    auto result = R2D::BatchSystem<Provider, Dim>::run(
        std::span<const DrawCommand>{state_.draw_commands.data(), frame_totals_.total_draw_count},
        state_.batch_commands);
    auto end = std::chrono::steady_clock::now();
    frame_totals_.times.batch_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("batch", result);
    }
    frame_totals_.batch_count = result.write_count;

    start = std::chrono::steady_clock::now();
    result = R2D::CommandBufferBuildSystem<Provider, Dim>::run(
        frame_index_,
        {.first = 0U, .count = frame_totals_.total_draw_count},
        {.first = 0U, .count = frame_totals_.batch_count},
        {.first = 0U, .count = 0U},
        {.first = 0U, .count = 0U},
        state_.command_buffers);
    end = std::chrono::steady_clock::now();
    frame_totals_.times.command_buffer_ms += R2DB::elapsedMs(start, end);
    if (!isOk(result)) {
        return reportSystemFailure("command_buffer", result);
    }
    return 0;
}

[[nodiscard]] int runFrame(
    const R2DB::BenchmarkConfig& config_,
    R2D::U32 frame_index_,
    BenchState& state_,
    bool collect_,
    R2DB::BenchmarkTotals& totals_)
{
    auto frame_totals = R2DB::kZeroBenchmarkTotals;

    if (hasSpritePath(config_)) {
        const int result = runSpritePath(config_, frame_index_, state_, frame_totals);
        if (result != 0) {
            return result;
        }
    }
    if (hasTextPath(config_)) {
        const int result = runTextPath(config_, frame_index_, state_, frame_totals);
        if (result != 0) {
            return result;
        }
    }

    const int common_result = runCommonPath(frame_index_, state_, frame_totals);
    if (common_result != 0) {
        return common_result;
    }

    if (collect_) {
        R2DB::accumulate(totals_, frame_totals);
    }
    return 0;
}

[[nodiscard]] int runBenchmark(
    const R2DB::BenchmarkConfig& config_,
    BenchState& state_,
    R2DB::BenchmarkTotals& totals_)
{
    for (R2D::U32 warmup_index = 0U; warmup_index < config_.warmup_count; ++warmup_index) {
        const int result = runFrame(config_, warmup_index, state_, false, totals_);
        if (result != 0) {
            return result;
        }
    }

    for (R2D::U32 frame_index = 0U; frame_index < config_.frame_count; ++frame_index) {
        const R2D::U32 runtime_frame_index = config_.warmup_count + frame_index;
        const int result = runFrame(config_, runtime_frame_index, state_, true, totals_);
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

} // namespace

int main(int argc_, char** argv_)
{
    try {
        R2DB::BenchmarkConfig config{};
        if (!R2DB::parseBenchmarkConfig(argc_, argv_, config)) {
            if (hasOption(argc_, argv_, "--list-scenarios")) {
                R2DB::printScenarioList(std::cout);
                return 0;
            }
            if (hasOption(argc_, argv_, "--help")) {
                R2DB::printBenchmarkUsage(std::cout);
                return 0;
            }
            R2DB::printBenchmarkUsage(std::cerr);
            return 1;
        }
        normalizeConfig(config);

        ActiveCounts counts{};
        if (!makeActiveCounts(config, counts)) {
            std::cerr << "benchmark config exceeds 32-bit component stream limits\n";
            return 1;
        }

        auto state = makeState(config, counts);
        auto totals = R2DB::kZeroBenchmarkTotals;
        const int result = runBenchmark(config, state, totals);
        if (result != 0) {
            return result;
        }
        R2DB::printBenchmarkReport(std::cout, config, totals);
        return 0;
    } catch (...) {
        static_cast<void>(std::fputs("benchmark failed with an unexpected exception\n", stderr));
        return 1;
    }
}
