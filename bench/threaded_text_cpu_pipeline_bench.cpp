#include <Render2D/System/ThreadedTextCpuPipeline.hpp>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>

namespace {

namespace R2D = Render2D;

using Text = R2D::Text<R2D::VulkanNativeProvider, R2D::Dim2>;
using TextState = R2D::TextState<R2D::VulkanNativeProvider, R2D::Dim2>;
using TextDirtyRange = R2D::TextDirtyRange<R2D::VulkanNativeProvider, R2D::Dim2>;
using FontAtlasRef = R2D::FontAtlasRef<R2D::VulkanNativeProvider, R2D::Dim2>;
using GlyphRun = R2D::GlyphRun<R2D::VulkanNativeProvider, R2D::Dim2>;
using GlyphInstance = R2D::GlyphInstance<R2D::VulkanNativeProvider, R2D::Dim2>;
using TextRuntime = R2D::ThreadedTextCpuPipelineRuntime<R2D::VulkanNativeProvider, R2D::Dim2>;
using GlyphInstanceBuild = R2D::GlyphInstanceBuildSystem<R2D::VulkanNativeProvider, R2D::Dim2>;

inline constexpr R2D::U32 kDefaultTextCount = 131072U;
inline constexpr R2D::U32 kDefaultGlyphsPerText = 8U;
inline constexpr R2D::U32 kDefaultFrameCount = 8U;
inline constexpr R2D::U32 kDefaultWarmupCount = 2U;
inline constexpr R2D::U32 kDefaultWorkerCount = 0U;
inline constexpr R2D::U32 kDefaultMinGlyphsPerTask = 4096U;
inline constexpr R2D::U32 kFontId = 0U;

struct BenchConfig {
    R2D::U32 text_count;
    R2D::U32 glyphs_per_text;
    R2D::U32 frame_count;
    R2D::U32 warmup_count;
    R2D::U32 worker_count;
    R2D::U32 min_glyphs_per_task;
};

[[nodiscard]] bool parseU32(std::string_view text_, R2D::U32& out_value_) noexcept
{
    if (text_.empty() || text_.front() < '0' || text_.front() > '9') {
        return false;
    }
    unsigned long long value = 0ULL;
    const char* const first = text_.data();
    const char* const last = text_.data() + text_.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last ||
        value > (std::numeric_limits<R2D::U32>::max)()) {
        return false;
    }
    out_value_ = static_cast<R2D::U32>(value);
    return true;
}

[[nodiscard]] bool parseConfig(int argc_, char** argv_, BenchConfig& out_config_) noexcept
{
    out_config_ = {
        .text_count = kDefaultTextCount,
        .glyphs_per_text = kDefaultGlyphsPerText,
        .frame_count = kDefaultFrameCount,
        .warmup_count = kDefaultWarmupCount,
        .worker_count = kDefaultWorkerCount,
        .min_glyphs_per_task = kDefaultMinGlyphsPerTask,
    };
    for (int index = 1; index < argc_; ++index) {
        const std::string_view option{argv_[index]};
        if (option == "--texts" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.text_count)) { return false; }
            ++index;
        } else if (option == "--glyphs-per-text" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.glyphs_per_text)) { return false; }
            ++index;
        } else if (option == "--frames" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.frame_count)) { return false; }
            ++index;
        } else if (option == "--warmup" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.warmup_count)) { return false; }
            ++index;
        } else if (option == "--workers" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.worker_count)) { return false; }
            ++index;
        } else if (option == "--min-glyphs-per-task" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.min_glyphs_per_task)) { return false; }
            ++index;
        } else {
            return false;
        }
    }
    return out_config_.frame_count != 0U && out_config_.glyphs_per_text != 0U &&
        out_config_.text_count != 0U;
}

[[nodiscard]] double elapsedMs(
    std::chrono::steady_clock::time_point start_,
    std::chrono::steady_clock::time_point end_) noexcept
{
    return std::chrono::duration<double, std::milli>(end_ - start_).count();
}

struct BenchState {
    R2D::McVector<Text> texts;
    R2D::McVector<TextState> next_states;
    R2D::McVector<TextDirtyRange> dirty_ranges;
    R2D::McVector<FontAtlasRef> atlases;
    R2D::McVector<GlyphRun> glyph_runs;
    R2D::McVector<GlyphInstance> reference_instances;
    R2D::McVector<GlyphInstance> threaded_instances;
    R2D::U32 dirty_count;
    R2D::U32 glyph_count;
};

[[nodiscard]] bool makeState(const BenchConfig& config_, BenchState& out_state_)
{
    const auto text_count = static_cast<R2D::Usize>(config_.text_count);
    const auto glyph_count = text_count * config_.glyphs_per_text;

    out_state_.texts.resize(text_count);
    out_state_.next_states.resize(text_count);
    out_state_.dirty_ranges.resize(text_count);
    out_state_.glyph_runs.resize(text_count);
    out_state_.reference_instances.resize(glyph_count);
    out_state_.threaded_instances.resize(glyph_count);
    out_state_.atlases.resize(1U);
    out_state_.atlases[0] = {
        .font_id = kFontId,
        .atlas_id = 100U,
        .generation = 1U,
        .texture_id = 200U,
        .texture_generation = 0U,
        .flags = 0U,
    };

    for (R2D::Usize index = 0U; index < text_count; ++index) {
        out_state_.texts[index] = {
            .source_id = static_cast<R2D::U32>(index),
            .font_id = kFontId,
            .utf8_buffer_id = 1U,
            .utf8_offset = 0U,
            .utf8_size = config_.glyphs_per_text,
            .color_rgba8 = 0xFFFFFFFFU,
            .pixel_size = 16.0F + static_cast<float>(index % 4U),
            .layer = static_cast<R2D::U32>(index % 4U),
            .flags = 0U,
        };
    }

    const auto dirty_result = R2D::TextDirtySystem<R2D::VulkanNativeProvider, R2D::Dim2>::run(
        out_state_.texts,
        std::span<const TextState>{},
        out_state_.next_states,
        out_state_.dirty_ranges);
    if (dirty_result.code != R2D::SystemStatusCode::Ok) {
        return false;
    }
    out_state_.dirty_count = dirty_result.write_count;

    const auto run_result = R2D::GlyphRunBuildSystem<R2D::VulkanNativeProvider, R2D::Dim2>::runDirty(
        out_state_.texts,
        std::span<const TextDirtyRange>{out_state_.dirty_ranges.data(), out_state_.dirty_count},
        out_state_.atlases,
        out_state_.glyph_runs);
    if (run_result.code != R2D::SystemStatusCode::Ok) {
        return false;
    }
    out_state_.glyph_count = static_cast<R2D::U32>(glyph_count);
    return true;
}

} // namespace

int main(int argc_, char** argv_)
{
    try {
        BenchConfig config{};
        if (!parseConfig(argc_, argv_, config)) {
            std::fputs("Render2D threaded text CPU benchmark\n"
                       "Options: --texts <n> --glyphs-per-text <n> --frames <n>\n"
                       "         --warmup <n> --workers <n> --min-glyphs-per-task <n>\n",
                       stderr);
            return 1;
        }

        BenchState state{};
        if (!makeState(config, state)) {
            std::fputs("threaded text bench setup failed\n", stderr);
            return 1;
        }

        TextRuntime runtime{{
            .worker_count = config.worker_count,
            .min_glyphs_per_task = config.min_glyphs_per_task,
            // Force the threaded path so the benchmark always measures it.
            .parallel_threshold = 1U,
        }};

        const std::span<const TextDirtyRange> ranges{
            state.dirty_ranges.data(),
            state.dirty_count,
        };

        double reference_total_ms = 0.0;
        double threaded_total_ms = 0.0;
        bool mismatch = false;

        const R2D::U32 total_frames = config.warmup_count + config.frame_count;
        for (R2D::U32 frame = 0U; frame < total_frames; ++frame) {
            const bool collect = frame >= config.warmup_count;

            auto start = std::chrono::steady_clock::now();
            const auto reference_result = GlyphInstanceBuild::runDirty(
                state.glyph_runs, ranges, state.texts,
                R2D::kDefaultGlyphBuildConfig, state.reference_instances);
            auto end = std::chrono::steady_clock::now();
            if (collect) { reference_total_ms += elapsedMs(start, end); }

            start = std::chrono::steady_clock::now();
            const auto threaded_result = runtime.runGlyphInstanceBuildDirty(
                state.glyph_runs, ranges, state.texts,
                R2D::kDefaultGlyphBuildConfig, state.threaded_instances);
            end = std::chrono::steady_clock::now();
            if (collect) { threaded_total_ms += elapsedMs(start, end); }

            if (reference_result.code != R2D::SystemStatusCode::Ok ||
                threaded_result.code != R2D::SystemStatusCode::Ok ||
                reference_result.write_count != threaded_result.write_count) {
                mismatch = true;
                break;
            }
        }

        if (mismatch) {
            std::fputs("threaded text bench: reference/threaded result mismatch\n", stderr);
            return 1;
        }

        const auto divisor = static_cast<double>(config.frame_count == 0U ? 1U : config.frame_count);
        const double reference_ms = reference_total_ms / divisor;
        const double threaded_ms = threaded_total_ms / divisor;
        const double speedup = threaded_ms > 0.0 ? reference_ms / threaded_ms : 0.0;

        std::printf("Render2D threaded text CPU benchmark\n");
        std::printf("texts=%u glyphs_per_text=%u glyphs=%u workers=%u min_glyphs_per_task=%u frames=%u\n",
            config.text_count, config.glyphs_per_text, state.glyph_count,
            runtime.workerCount(), config.min_glyphs_per_task, config.frame_count);
        std::printf("avg_reference_ms=%.6f\n", reference_ms);
        std::printf("avg_threaded_ms=%.6f\n", threaded_ms);
        std::printf("speedup=%.6f\n", speedup);
        return 0;
    } catch (const std::exception& exception) {
        std::fputs("threaded text bench exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("threaded text bench unknown exception\n", stderr);
        return 1;
    }
}
