#pragma once

#include <Render2D/Core/Types.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <iostream>
#include <limits>
#include <string_view>
#include <system_error>

namespace Render2D::Bench {

enum class BenchScenario : U8 {
    Sprite = 0,
    Text = 1,
    Mixed = 2,
};

enum class VisibilityMode : U8 {
    High = 0,
    Low = 1,
};

enum class OutputFormat : U8 {
    Text = 0,
    Csv = 1,
};

struct BenchmarkConfig {
    Usize sprite_count;
    Usize text_count;
    U32 frame_count;
    U32 warmup_count;
    U32 glyphs_per_text;
    U32 dirty_text_stride;
    BenchScenario scenario;
    VisibilityMode visibility;
    OutputFormat output_format;
};

struct StageTimeTotals {
    double transform_ms;
    double bounds_ms;
    double culling_ms;
    double sprite_command_ms;
    double text_dirty_ms;
    double glyph_run_ms;
    double glyph_instance_ms;
    double glyph_batch_ms;
    double batch_ms;
    double command_buffer_ms;
};

struct BenchmarkTotals {
    StageTimeTotals times;
    U32 visible_count;
    U32 sprite_draw_count;
    U32 text_dirty_count;
    U32 glyph_count;
    U32 glyph_draw_count;
    U32 total_draw_count;
    U32 batch_count;
};

inline constexpr BenchmarkConfig kDefaultBenchmarkConfig{
    .sprite_count = 10000U,
    .text_count = 2048U,
    .frame_count = 8U,
    .warmup_count = 2U,
    .glyphs_per_text = 8U,
    .dirty_text_stride = 0U,
    .scenario = BenchScenario::Mixed,
    .visibility = VisibilityMode::High,
    .output_format = OutputFormat::Text,
};

inline constexpr StageTimeTotals kZeroStageTimeTotals{
    .transform_ms = 0.0,
    .bounds_ms = 0.0,
    .culling_ms = 0.0,
    .sprite_command_ms = 0.0,
    .text_dirty_ms = 0.0,
    .glyph_run_ms = 0.0,
    .glyph_instance_ms = 0.0,
    .glyph_batch_ms = 0.0,
    .batch_ms = 0.0,
    .command_buffer_ms = 0.0,
};

inline constexpr BenchmarkTotals kZeroBenchmarkTotals{
    .times = kZeroStageTimeTotals,
    .visible_count = 0U,
    .sprite_draw_count = 0U,
    .text_dirty_count = 0U,
    .glyph_count = 0U,
    .glyph_draw_count = 0U,
    .total_draw_count = 0U,
    .batch_count = 0U,
};

inline const char* scenarioName(BenchScenario scenario_) noexcept
{
    switch (scenario_) {
    case BenchScenario::Sprite:
        return "sprite";
    case BenchScenario::Text:
        return "text";
    case BenchScenario::Mixed:
        return "mixed";
    }
    return "unknown";
}

inline const char* visibilityName(VisibilityMode visibility_) noexcept
{
    switch (visibility_) {
    case VisibilityMode::High:
        return "high";
    case VisibilityMode::Low:
        return "low";
    }
    return "unknown";
}

inline const char* outputFormatName(OutputFormat format_) noexcept
{
    switch (format_) {
    case OutputFormat::Text:
        return "text";
    case OutputFormat::Csv:
        return "csv";
    }
    return "unknown";
}

inline bool parseScenario(std::string_view text_, BenchScenario& out_scenario_) noexcept
{
    if (text_ == "sprite") {
        out_scenario_ = BenchScenario::Sprite;
        return true;
    }
    if (text_ == "text") {
        out_scenario_ = BenchScenario::Text;
        return true;
    }
    if (text_ == "mixed") {
        out_scenario_ = BenchScenario::Mixed;
        return true;
    }
    return false;
}

inline bool parseVisibility(std::string_view text_, VisibilityMode& out_visibility_) noexcept
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

inline bool parseOutputFormat(std::string_view text_, OutputFormat& out_format_) noexcept
{
    if (text_ == "text") {
        out_format_ = OutputFormat::Text;
        return true;
    }
    if (text_ == "csv") {
        out_format_ = OutputFormat::Csv;
        return true;
    }
    return false;
}

inline bool parseUsize(std::string_view text_, Usize& out_value_) noexcept
{
    if (text_.empty() || text_.front() < '0' || text_.front() > '9') {
        return false;
    }

    unsigned long long value = 0ULL;
    const char* const first = text_.data();
    const char* const last = text_.data() + text_.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;
    }
    if (value > (std::numeric_limits<Usize>::max)()) {
        return false;
    }

    out_value_ = static_cast<Usize>(value);
    return true;
}

inline bool parseU32(std::string_view text_, U32& out_value_) noexcept
{
    Usize value = 0U;
    if (!parseUsize(text_, value) || value > (std::numeric_limits<U32>::max)()) {
        return false;
    }

    out_value_ = static_cast<U32>(value);
    return true;
}

inline bool parseBenchmarkConfig(int argc_, char** argv_, BenchmarkConfig& out_config_) noexcept
{
    out_config_ = kDefaultBenchmarkConfig;
    for (int index = 1; index < argc_; ++index) {
        const std::string_view option{argv_[index]};
        if (option == "--sprites" && index + 1 < argc_) {
            if (!parseUsize(argv_[index + 1], out_config_.sprite_count)) {
                return false;
            }
            ++index;
        } else if (option == "--texts" && index + 1 < argc_) {
            if (!parseUsize(argv_[index + 1], out_config_.text_count)) {
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
        } else if (option == "--glyphs-per-text" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.glyphs_per_text)) {
                return false;
            }
            ++index;
        } else if (option == "--dirty-text-stride" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.dirty_text_stride)) {
                return false;
            }
            ++index;
        } else if (option == "--scenario" && index + 1 < argc_) {
            if (!parseScenario(argv_[index + 1], out_config_.scenario)) {
                return false;
            }
            ++index;
        } else if (option == "--visibility" && index + 1 < argc_) {
            if (!parseVisibility(argv_[index + 1], out_config_.visibility)) {
                return false;
            }
            ++index;
        } else if (option == "--format" && index + 1 < argc_) {
            if (!parseOutputFormat(argv_[index + 1], out_config_.output_format)) {
                return false;
            }
            ++index;
        } else if (option == "--list-scenarios") {
            return false;
        } else if (option == "--help") {
            return false;
        } else {
            return false;
        }
    }

    if (out_config_.frame_count == 0U || out_config_.glyphs_per_text == 0U) {
        return false;
    }
    return true;
}


inline void printScenarioList(std::ostream& out_)
{
    out_ << "sprite\ntext\nmixed\n";
}

inline void printBenchmarkUsage(std::ostream& out_)
{
    out_ << "Render2D null CPU benchmark\n"
         << "Options:\n"
         << "  --scenario sprite|text|mixed\n"
         << "  --sprites <count>\n"
         << "  --texts <count>\n"
         << "  --frames <count>         (>0)\n"
         << "  --warmup <count>         (>=0)\n"
         << "  --visibility high|low\n"
         << "  --glyphs-per-text <count> (>0)\n"
         << "  --dirty-text-stride <0|N>\n"
         << "  --format text|csv\n";
}

inline double elapsedMs(
    std::chrono::steady_clock::time_point start_,
    std::chrono::steady_clock::time_point end_) noexcept
{
    return std::chrono::duration<double, std::milli>(end_ - start_).count();
}

inline double averageMs(double total_ms_, U32 frame_count_) noexcept
{
    const auto divisor = static_cast<double>(std::max(frame_count_, 1U));
    return total_ms_ / divisor;
}

inline void accumulate(BenchmarkTotals& left_, const BenchmarkTotals& right_) noexcept
{
    left_.times.transform_ms += right_.times.transform_ms;
    left_.times.bounds_ms += right_.times.bounds_ms;
    left_.times.culling_ms += right_.times.culling_ms;
    left_.times.sprite_command_ms += right_.times.sprite_command_ms;
    left_.times.text_dirty_ms += right_.times.text_dirty_ms;
    left_.times.glyph_run_ms += right_.times.glyph_run_ms;
    left_.times.glyph_instance_ms += right_.times.glyph_instance_ms;
    left_.times.glyph_batch_ms += right_.times.glyph_batch_ms;
    left_.times.batch_ms += right_.times.batch_ms;
    left_.times.command_buffer_ms += right_.times.command_buffer_ms;
    left_.visible_count = right_.visible_count;
    left_.sprite_draw_count = right_.sprite_draw_count;
    left_.text_dirty_count = right_.text_dirty_count;
    left_.glyph_count = right_.glyph_count;
    left_.glyph_draw_count = right_.glyph_draw_count;
    left_.total_draw_count = right_.total_draw_count;
    left_.batch_count = right_.batch_count;
}

inline void printTextReport(
    std::ostream& out_,
    const BenchmarkConfig& config_,
    const BenchmarkTotals& totals_)
{
    out_ << "Render2D null CPU benchmark\n";
    out_ << "scenario=" << scenarioName(config_.scenario)
         << " visibility=" << visibilityName(config_.visibility)
         << " sprites=" << config_.sprite_count
         << " texts=" << config_.text_count
         << " frames=" << config_.frame_count
         << " warmup=" << config_.warmup_count
         << " glyphs_per_text=" << config_.glyphs_per_text
         << " dirty_text_stride=" << config_.dirty_text_stride << '\n';
    out_ << "visible=" << totals_.visible_count
         << " sprite_draws=" << totals_.sprite_draw_count
         << " text_dirty=" << totals_.text_dirty_count
         << " glyphs=" << totals_.glyph_count
         << " glyph_draws=" << totals_.glyph_draw_count
         << " total_draws=" << totals_.total_draw_count
         << " batches=" << totals_.batch_count << '\n';
    out_ << "avg_transform_ms=" << averageMs(totals_.times.transform_ms, config_.frame_count) << '\n';
    out_ << "avg_bounds_ms=" << averageMs(totals_.times.bounds_ms, config_.frame_count) << '\n';
    out_ << "avg_culling_ms=" << averageMs(totals_.times.culling_ms, config_.frame_count) << '\n';
    out_ << "avg_sprite_command_ms=" << averageMs(totals_.times.sprite_command_ms, config_.frame_count) << '\n';
    out_ << "avg_text_dirty_ms=" << averageMs(totals_.times.text_dirty_ms, config_.frame_count) << '\n';
    out_ << "avg_glyph_run_ms=" << averageMs(totals_.times.glyph_run_ms, config_.frame_count) << '\n';
    out_ << "avg_glyph_instance_ms=" << averageMs(totals_.times.glyph_instance_ms, config_.frame_count) << '\n';
    out_ << "avg_glyph_batch_ms=" << averageMs(totals_.times.glyph_batch_ms, config_.frame_count) << '\n';
    out_ << "avg_batch_ms=" << averageMs(totals_.times.batch_ms, config_.frame_count) << '\n';
    out_ << "avg_command_buffer_ms=" << averageMs(totals_.times.command_buffer_ms, config_.frame_count) << '\n';
}

inline void printCsvReport(
    std::ostream& out_,
    const BenchmarkConfig& config_,
    const BenchmarkTotals& totals_)
{
    out_ << "scenario,visibility,sprites,texts,frames,warmup,glyphs_per_text,dirty_text_stride,"
         << "visible,sprite_draws,text_dirty,glyphs,glyph_draws,total_draws,batches,"
         << "avg_transform_ms,avg_bounds_ms,avg_culling_ms,avg_sprite_command_ms,"
         << "avg_text_dirty_ms,avg_glyph_run_ms,avg_glyph_instance_ms,avg_glyph_batch_ms,"
         << "avg_batch_ms,avg_command_buffer_ms\n";
    out_ << scenarioName(config_.scenario) << ','
         << visibilityName(config_.visibility) << ','
         << config_.sprite_count << ','
         << config_.text_count << ','
         << config_.frame_count << ','
         << config_.warmup_count << ','
         << config_.glyphs_per_text << ','
         << config_.dirty_text_stride << ','
         << totals_.visible_count << ','
         << totals_.sprite_draw_count << ','
         << totals_.text_dirty_count << ','
         << totals_.glyph_count << ','
         << totals_.glyph_draw_count << ','
         << totals_.total_draw_count << ','
         << totals_.batch_count << ','
         << averageMs(totals_.times.transform_ms, config_.frame_count) << ','
         << averageMs(totals_.times.bounds_ms, config_.frame_count) << ','
         << averageMs(totals_.times.culling_ms, config_.frame_count) << ','
         << averageMs(totals_.times.sprite_command_ms, config_.frame_count) << ','
         << averageMs(totals_.times.text_dirty_ms, config_.frame_count) << ','
         << averageMs(totals_.times.glyph_run_ms, config_.frame_count) << ','
         << averageMs(totals_.times.glyph_instance_ms, config_.frame_count) << ','
         << averageMs(totals_.times.glyph_batch_ms, config_.frame_count) << ','
         << averageMs(totals_.times.batch_ms, config_.frame_count) << ','
         << averageMs(totals_.times.command_buffer_ms, config_.frame_count) << '\n';
}

inline void printBenchmarkReport(
    std::ostream& out_,
    const BenchmarkConfig& config_,
    const BenchmarkTotals& totals_)
{
    if (config_.output_format == OutputFormat::Csv) {
        printCsvReport(out_, config_, totals_);
        return;
    }
    printTextReport(out_, config_, totals_);
}

} // namespace Render2D::Bench
