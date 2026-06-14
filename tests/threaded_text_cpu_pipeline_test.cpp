#include <Render2D/System/ThreadedTextCpuPipeline.hpp>
#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <span>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Text = R2D::Text<Provider, Dim>;
using TextState = R2D::TextState<Provider, Dim>;
using TextDirtyRange = R2D::TextDirtyRange<Provider, Dim>;
using FontAtlasRef = R2D::FontAtlasRef<Provider, Dim>;
using GlyphRun = R2D::GlyphRun<Provider, Dim>;
using GlyphInstance = R2D::GlyphInstance<Provider, Dim>;
using TextRuntime = R2D::ThreadedTextCpuPipelineRuntime<Provider, Dim>;

inline constexpr R2D::U32 kTextCount = 96U;
inline constexpr R2D::U32 kFontId = 0U;
inline constexpr R2D::Usize kGlyphCapacity = static_cast<R2D::Usize>(kTextCount) * 8U;

// Build texts with non-uniform glyph counts so chunk boundaries fall on uneven
// glyph slices (a stricter test of the disjoint-write merge than uniform texts).
[[nodiscard]] R2D::U32 fillTexts(std::span<Text> texts_) noexcept
{
    R2D::U32 total_glyphs = 0U;
    for (R2D::Usize index = 0U; index < texts_.size(); ++index) {
        const auto utf8_size = 4U + static_cast<R2D::U32>(index % 5U);
        texts_[index] = {
            .source_id = static_cast<R2D::U32>(index),
            .font_id = kFontId,
            .utf8_buffer_id = 1U,
            .utf8_offset = 0U,
            .utf8_size = utf8_size,
            .color_rgba8 = 0xFF00FF00U + static_cast<R2D::U32>(index),
            .pixel_size = 16.0F + static_cast<float>(index % 4U),
            .layer = static_cast<R2D::U32>(index % 3U),
            .flags = 0U,
        };
        total_glyphs += utf8_size;
    }
    return total_glyphs;
}

[[nodiscard]] bool buildRunsAndRanges(
    std::span<const Text> texts_,
    std::span<const FontAtlasRef> atlases_,
    std::span<TextState> next_states_,
    std::span<TextDirtyRange> dirty_ranges_,
    std::span<GlyphRun> glyph_runs_,
    R2D::U32& out_dirty_count_) noexcept
{
    const auto dirty_result = R2D::TextDirtySystem<Provider, Dim>::run(
        texts_,
        std::span<const TextState>{},
        next_states_,
        dirty_ranges_);
    if (dirty_result.code != R2D::SystemStatusCode::Ok) {
        return false;
    }
    out_dirty_count_ = dirty_result.write_count;

    const auto run_result = R2D::GlyphRunBuildSystem<Provider, Dim>::runDirty(
        texts_,
        std::span<const TextDirtyRange>{dirty_ranges_.data(), out_dirty_count_},
        atlases_,
        glyph_runs_);
    return run_result.code == R2D::SystemStatusCode::Ok;
}

[[nodiscard]] auto runTest() -> int
{
    R2DT::TestContext context{};

    std::array<Text, kTextCount> texts{};
    const R2D::U32 total_glyphs = fillTexts(texts);

    const std::array<FontAtlasRef, 1U> atlases{
        FontAtlasRef{
            .font_id = kFontId,
            .atlas_id = 100U,
            .generation = 1U,
            .texture_id = 200U,
            .texture_generation = 0U,
            .flags = 0U,
        },
    };

    std::array<TextState, kTextCount> next_states{};
    std::array<TextDirtyRange, kTextCount> dirty_ranges{};
    std::array<GlyphRun, kTextCount> glyph_runs{};
    R2D::U32 dirty_count = 0U;
    R2D_TEST_REQUIRE(context, buildRunsAndRanges(
        texts, atlases, next_states, dirty_ranges, glyph_runs, dirty_count));
    R2D_TEST_CHECK_EQ(context, dirty_count, kTextCount);

    const std::span<const TextDirtyRange> ranges{dirty_ranges.data(), dirty_count};

    // Single-thread reference.
    std::array<GlyphInstance, kGlyphCapacity> reference_instances{};
    const auto reference_result = R2D::GlyphInstanceBuildSystem<Provider, Dim>::runDirty(
        glyph_runs,
        ranges,
        texts,
        R2D::kDefaultGlyphBuildConfig,
        reference_instances);
    R2D_TEST_REQUIRE(context, reference_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, reference_result.write_count, total_glyphs);

    // Threaded path forced on (threshold 1, multiple workers, fine grain) so the
    // ranges split across several chunks; output must be byte-identical.
    std::array<GlyphInstance, kGlyphCapacity> threaded_instances{};
    TextRuntime runtime{{
        .worker_count = 4U,
        .min_glyphs_per_task = 1U,
        .parallel_threshold = 1U,
    }};
    R2D_TEST_CHECK(context, runtime.shouldParallelize(total_glyphs));
    const auto threaded_result = runtime.runGlyphInstanceBuildDirty(
        glyph_runs,
        ranges,
        texts,
        R2D::kDefaultGlyphBuildConfig,
        threaded_instances);
    R2D_TEST_REQUIRE(context, threaded_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, threaded_result.write_count, total_glyphs);
    R2D_TEST_CHECK(context, std::memcmp(
        reference_instances.data(),
        threaded_instances.data(),
        static_cast<R2D::Usize>(total_glyphs) * sizeof(GlyphInstance)) == 0);

    // Above-threshold gate: a multi-worker runtime whose threshold exceeds the
    // workload routes to the single-thread path and still equals the reference.
    std::array<GlyphInstance, kGlyphCapacity> gated_instances{};
    TextRuntime gated_runtime{{
        .worker_count = 4U,
        .min_glyphs_per_task = 1U,
        .parallel_threshold = total_glyphs + 1U,
    }};
    R2D_TEST_CHECK(context, !gated_runtime.shouldParallelize(total_glyphs));
    const auto gated_result = gated_runtime.runGlyphInstanceBuildDirty(
        glyph_runs,
        ranges,
        texts,
        R2D::kDefaultGlyphBuildConfig,
        gated_instances);
    R2D_TEST_REQUIRE(context, gated_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, gated_result.write_count, total_glyphs);
    R2D_TEST_CHECK(context, std::memcmp(
        reference_instances.data(),
        gated_instances.data(),
        static_cast<R2D::Usize>(total_glyphs) * sizeof(GlyphInstance)) == 0);

    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("threaded_text_cpu_pipeline_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("threaded_text_cpu_pipeline_test unknown exception\n", stderr);
        return 1;
    }
}
