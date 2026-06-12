#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <span>

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Text = R2D::Text<Provider, Dim>;
using Codepoint = R2D::Codepoint<Provider, Dim>;
using Utf8BufferView = R2D::Utf8BufferView;
using DecodeSystem = R2D::Utf8DecodeSystem<Provider, Dim>;
using ShapingRun = R2D::ShapingRun<Provider, Dim>;
using ShapedGlyph = R2D::ShapedGlyph<Provider, Dim>;
using GlyphAtlasEntry = R2D::GlyphAtlasEntry<Provider, Dim>;
using GlyphInstance = R2D::GlyphInstance<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using PositionSystem = R2D::GlyphPositionSystem<Provider, Dim>;
using BridgeSystem = R2D::GlyphInstanceToSpriteSystem<Provider, Dim>;

static_assert(R2D::StrictPodComponent<Codepoint>);

constexpr Text makeText(R2D::U32 buffer_id_, R2D::U32 offset_, R2D::U32 size_) noexcept
{
    return {
        .source_id = 1U,
        .font_id = 2U,
        .utf8_buffer_id = buffer_id_,
        .utf8_offset = offset_,
        .utf8_size = size_,
        .color_rgba8 = 0xFFFFFFFFU,
        .pixel_size = 16.0F,
        .layer = 0U,
        .flags = 0U,
    };
}

void testDecodeMixedWidths(R2DT::TestContext& context_)
{
    // "A" U+0041 | "é" U+00E9 (C3 A9) | "€" U+20AC (E2 82 AC) | "𝄞" U+1D11E (F0 9D 84 9E)
    static constexpr std::array<R2D::U8, 10U> kBytes{
        0x41U,
        0xC3U, 0xA9U,
        0xE2U, 0x82U, 0xACU,
        0xF0U, 0x9DU, 0x84U, 0x9EU,
    };
    const std::array<Utf8BufferView, 1U> buffers{{
        {.buffer_id = 7U, .bytes = kBytes.data(), .byte_count = static_cast<R2D::U32>(kBytes.size())},
    }};
    const std::array<Text, 1U> texts{{makeText(7U, 0U, static_cast<R2D::U32>(kBytes.size()))}};
    std::array<Codepoint, 8U> out{};

    const auto result = DecodeSystem::run(texts, buffers, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 1U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 4U);

    R2D_TEST_CHECK_EQ(context_, out[0U].codepoint, 0x41U);
    R2D_TEST_CHECK_EQ(context_, out[1U].codepoint, 0x00E9U);
    R2D_TEST_CHECK_EQ(context_, out[2U].codepoint, 0x20ACU);
    R2D_TEST_CHECK_EQ(context_, out[3U].codepoint, 0x1D11EU);

    R2D_TEST_CHECK_EQ(context_, out[0U].byte_offset, 0U);
    R2D_TEST_CHECK_EQ(context_, out[1U].byte_offset, 1U);
    R2D_TEST_CHECK_EQ(context_, out[2U].byte_offset, 3U);
    R2D_TEST_CHECK_EQ(context_, out[3U].byte_offset, 6U);

    R2D_TEST_CHECK_EQ(context_, out[3U].text_index, 0U);
    R2D_TEST_CHECK_EQ(context_, out[0U].flags, 0U);
}

void testDecodeOffsetAndMultiText(R2DT::TestContext& context_)
{
    // Two texts share one buffer; the second reads a sub-slice.
    static constexpr std::array<R2D::U8, 4U> kBytes{0x68U, 0x69U, 0x4AU, 0x4BU}; // "hiJK"
    const std::array<Utf8BufferView, 1U> buffers{{
        {.buffer_id = 1U, .bytes = kBytes.data(), .byte_count = static_cast<R2D::U32>(kBytes.size())},
    }};
    const std::array<Text, 2U> texts{{
        makeText(1U, 0U, 2U), // "hi"
        makeText(1U, 2U, 2U), // "JK"
    }};
    std::array<Codepoint, 8U> out{};

    const auto result = DecodeSystem::run(texts, buffers, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 4U);
    R2D_TEST_CHECK_EQ(context_, out[0U].text_index, 0U);
    R2D_TEST_CHECK_EQ(context_, out[1U].codepoint, 0x69U);
    R2D_TEST_CHECK_EQ(context_, out[2U].text_index, 1U);
    R2D_TEST_CHECK_EQ(context_, out[2U].codepoint, 0x4AU);
    R2D_TEST_CHECK_EQ(context_, out[2U].byte_offset, 0U); // offset is relative to the text slice
}

void testDecodeInvalidBytes(R2DT::TestContext& context_)
{
    // 0xFF is never a valid lead byte; "C3" then "41" is a truncated 2-byte
    // sequence (continuation missing). Each bad byte yields one U+FFFD.
    static constexpr std::array<R2D::U8, 4U> kBytes{0xFFU, 0x41U, 0xC3U, 0x42U};
    const std::array<Utf8BufferView, 1U> buffers{{
        {.buffer_id = 0U, .bytes = kBytes.data(), .byte_count = static_cast<R2D::U32>(kBytes.size())},
    }};
    const std::array<Text, 1U> texts{{makeText(0U, 0U, static_cast<R2D::U32>(kBytes.size()))}};
    std::array<Codepoint, 8U> out{};

    const auto result = DecodeSystem::run(texts, buffers, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 4U);
    R2D_TEST_CHECK_EQ(context_, out[0U].codepoint, R2D::kUnicodeReplacement);
    R2D_TEST_CHECK(context_, (out[0U].flags & R2D::kCodepointInvalidFlag) != 0U);
    R2D_TEST_CHECK_EQ(context_, out[1U].codepoint, 0x41U);
    R2D_TEST_CHECK_EQ(context_, out[1U].flags, 0U);
    R2D_TEST_CHECK_EQ(context_, out[2U].codepoint, R2D::kUnicodeReplacement); // truncated C3
    R2D_TEST_CHECK_EQ(context_, out[3U].codepoint, 0x42U);
}

void testDecodeErrors(R2DT::TestContext& context_)
{
    static constexpr std::array<R2D::U8, 2U> kBytes{0x41U, 0x42U};
    const std::array<Utf8BufferView, 1U> buffers{{
        {.buffer_id = 5U, .bytes = kBytes.data(), .byte_count = static_cast<R2D::U32>(kBytes.size())},
    }};
    std::array<Codepoint, 8U> out{};

    // Missing buffer id.
    const std::array<Text, 1U> missing{{makeText(99U, 0U, 2U)}};
    auto result = DecodeSystem::run(missing, buffers, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    // Slice runs past the buffer.
    const std::array<Text, 1U> overrun{{makeText(5U, 1U, 4U)}};
    result = DecodeSystem::run(overrun, buffers, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    // Output capacity too small.
    const std::array<Text, 1U> ok{{makeText(5U, 0U, 2U)}};
    std::array<Codepoint, 1U> tiny{};
    result = DecodeSystem::run(ok, buffers, tiny);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);
}

void testDecodeUnsupportedDomain(R2DT::TestContext& context_)
{
    const auto result = R2D::Utf8DecodeSystem<int, Dim>::run({}, {}, {});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::UnsupportedDomain);
}

// Shared fixture for the position + bridge tests: one LTR text, two resident
// glyphs (A, b) plus a third non-resident glyph standing in for whitespace.
constexpr Text kLayoutText{
    .source_id = 1U,
    .font_id = 2U,
    .utf8_buffer_id = 0U,
    .utf8_offset = 0U,
    .utf8_size = 0U,
    .color_rgba8 = 0xAABBCCDDU,
    .pixel_size = 16.0F,
    .layer = 3U,
    .flags = 0U,
};
constexpr std::array<ShapingRun, 1U> kLayoutRuns{{
    {
        .text_index = 0U,
        .codepoint_first = 0U,
        .codepoint_count = 3U,
        .font_id = 2U,
        .script = 0U,
        .direction = R2D::kTextDirectionLtr,
        .bidi_level = 0U,
        .flags = 0U,
    },
}};

void testGlyphPositionPenWalk(R2DT::TestContext& context_)
{
    const std::array<GlyphAtlasEntry, 2U> entries{{
        {
            .font_id = 2U, .generation = 1U, .glyph_id = 70U, .pixel_size = 16.0F,
            .atlas_id = 5U, .atlas_generation = 1U,
            .atlas_rect = R2D::makeAabb2(0.0F, 0.0F, 0.5F, 0.5F),
            .bitmap_width = 6.0F, .bitmap_height = 10.0F, .bearing_x = 1.0F, .bearing_y = 8.0F, .flags = 0U,
        },
        {
            .font_id = 2U, .generation = 1U, .glyph_id = 71U, .pixel_size = 16.0F,
            .atlas_id = 5U, .atlas_generation = 1U,
            .atlas_rect = R2D::makeAabb2(0.5F, 0.0F, 1.0F, 0.5F),
            .bitmap_width = 5.0F, .bitmap_height = 7.0F, .bearing_x = 0.0F, .bearing_y = 7.0F, .flags = 0U,
        },
    }};
    const std::array<ShapedGlyph, 3U> shaped{{
        {.run_index = 0U, .glyph_id = 70U, .cluster = 0U, .x_advance = 7.0F, .y_advance = 0.0F, .x_offset = 0.0F, .y_offset = 0.0F, .flags = 0U},
        {.run_index = 0U, .glyph_id = 71U, .cluster = 1U, .x_advance = 6.0F, .y_advance = 0.0F, .x_offset = 0.0F, .y_offset = 0.0F, .flags = 0U},
        {.run_index = 0U, .glyph_id = 99U, .cluster = 2U, .x_advance = 4.0F, .y_advance = 0.0F, .x_offset = 0.0F, .y_offset = 0.0F, .flags = 0U},
    }};
    const std::array<Text, 1U> texts{{kLayoutText}};
    std::array<GlyphInstance, 3U> out{};

    const auto result = PositionSystem::run(shaped, kLayoutRuns, texts, entries,
        {.origin_x = 100.0F, .baseline_y = 50.0F, .flags = 0U}, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 3U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 3U);

    // Glyph A: quad top-left = pen(100) + offset(0) + bearing_x(1) = 101; y = baseline(50) - bearing_y(8) = 42.
    R2D_TEST_CHECK_EQ(context_, out[0U].position_x, 101.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].position_y, 42.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].width, 6.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].height, 10.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].color_rgba8, 0xAABBCCDDU);
    R2D_TEST_CHECK_EQ(context_, out[0U].layer, 3U);
    R2D_TEST_CHECK_EQ(context_, R2D::aabb2Min(out[0U].atlas_rect).x, 0.0F);
    R2D_TEST_CHECK_EQ(context_, R2D::aabb2Max(out[0U].atlas_rect).x, 0.5F);
    R2D_TEST_CHECK_EQ(context_, out[0U].flags, 0U);

    // Glyph b: pen advanced to 107; bearing_x 0 -> x = 107; y = 50 - 7 = 43.
    R2D_TEST_CHECK_EQ(context_, out[1U].position_x, 107.0F);
    R2D_TEST_CHECK_EQ(context_, out[1U].position_y, 43.0F);
    R2D_TEST_CHECK_EQ(context_, out[1U].width, 5.0F);

    // Non-resident glyph: zero-size quad, flagged, but still advances the pen.
    R2D_TEST_CHECK_EQ(context_, out[2U].position_x, 113.0F);
    R2D_TEST_CHECK_EQ(context_, out[2U].width, 0.0F);
    R2D_TEST_CHECK_EQ(context_, out[2U].height, 0.0F);
    R2D_TEST_CHECK(context_, (out[2U].flags & R2D::kGlyphInstanceMissingAtlasFlag) != 0U);
}

void testGlyphPositionErrors(R2DT::TestContext& context_)
{
    const std::array<Text, 1U> texts{{kLayoutText}};
    const std::array<GlyphAtlasEntry, 0U> entries{};
    const std::array<ShapedGlyph, 1U> shaped{{
        {.run_index = 0U, .glyph_id = 70U, .cluster = 0U, .x_advance = 7.0F, .y_advance = 0.0F, .x_offset = 0.0F, .y_offset = 0.0F, .flags = 0U},
    }};
    constexpr R2D::GlyphLayoutConfig kConfig{.origin_x = 0.0F, .baseline_y = 0.0F, .flags = 0U};

    // Empty input is a clean no-op.
    std::array<GlyphInstance, 1U> out{};
    auto result = PositionSystem::run({}, kLayoutRuns, texts, entries, kConfig, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 0U);

    // Output too small.
    std::array<GlyphInstance, 0U> tiny{};
    result = PositionSystem::run(shaped, kLayoutRuns, texts, entries, kConfig, tiny);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);

    // Glyph references a run index past the run stream.
    const std::array<ShapedGlyph, 1U> bad_run{{
        {.run_index = 5U, .glyph_id = 70U, .cluster = 0U, .x_advance = 7.0F, .y_advance = 0.0F, .x_offset = 0.0F, .y_offset = 0.0F, .flags = 0U},
    }};
    result = PositionSystem::run(bad_run, kLayoutRuns, texts, entries, kConfig, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    // Run references a text index past the text stream.
    const std::array<ShapingRun, 1U> bad_text_run{{
        {.text_index = 9U, .codepoint_first = 0U, .codepoint_count = 1U, .font_id = 2U, .script = 0U, .direction = 0U, .bidi_level = 0U, .flags = 0U},
    }};
    result = PositionSystem::run(shaped, bad_text_run, texts, entries, kConfig, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    result = R2D::GlyphPositionSystem<int, Dim>::run({}, {}, {}, {}, kConfig, {});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::UnsupportedDomain);
}

void testGlyphBridge(R2DT::TestContext& context_)
{
    const std::array<GlyphInstance, 3U> glyphs{{
        {
            .glyph_run_index = 0U, .glyph_id = 70U,
            .atlas_rect = R2D::makeAabb2(0.0F, 0.0F, 0.5F, 0.5F),
            .position_x = 101.0F, .position_y = 42.0F, .width = 6.0F, .height = 10.0F,
            .color_rgba8 = 0x11223344U, .sort_key = 3U, .layer = 3U, .flags = 0U,
        },
        // Whitespace / non-resident: zero-size, must be skipped.
        {
            .glyph_run_index = 0U, .glyph_id = 99U,
            .atlas_rect = R2D::makeAabb2(0.0F, 0.0F, 0.0F, 0.0F),
            .position_x = 113.0F, .position_y = 50.0F, .width = 0.0F, .height = 0.0F,
            .color_rgba8 = 0x11223344U, .sort_key = 3U, .layer = 3U, .flags = R2D::kGlyphInstanceMissingAtlasFlag,
        },
        {
            .glyph_run_index = 0U, .glyph_id = 71U,
            .atlas_rect = R2D::makeAabb2(0.5F, 0.0F, 1.0F, 0.5F),
            .position_x = 117.0F, .position_y = 43.0F, .width = 5.0F, .height = 7.0F,
            .color_rgba8 = 0x11223344U, .sort_key = 3U, .layer = 3U, .flags = 0U,
        },
    }};
    constexpr R2D::GlyphSpriteBridgeConfig kConfig{
        .texture_id = 9U, .texture_generation = 2U, .material_id = 4U, .material_generation = 1U, .viewport_width = 0.0F, .viewport_height = 0.0F, .flags = 0U,
    };
    std::array<SpriteInstance, 3U> out{};

    const auto result = BridgeSystem::run(glyphs, kConfig, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 3U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 2U); // whitespace skipped

    // First visible glyph -> affine maps unit quad onto (101,42)+(6,10).
    R2D_TEST_CHECK_EQ(context_, out[0U].transform_m00, 6.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].transform_m01, 0.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].transform_m02, 101.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].transform_m10, 0.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].transform_m11, 10.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].transform_m12, 42.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].uv_min_x, 0.0F);
    R2D_TEST_CHECK_EQ(context_, out[0U].uv_max_x, 0.5F);
    R2D_TEST_CHECK_EQ(context_, out[0U].uv_max_y, 0.5F);
    R2D_TEST_CHECK_EQ(context_, out[0U].texture_id, 9U);
    R2D_TEST_CHECK_EQ(context_, out[0U].material_id, 4U);
    R2D_TEST_CHECK_EQ(context_, out[0U].color_rgba8, 0x11223344U);
    R2D_TEST_CHECK_EQ(context_, out[0U].source_index, 0U);

    // The third glyph compacts into output slot 1 (the whitespace was skipped).
    R2D_TEST_CHECK_EQ(context_, out[1U].transform_m00, 5.0F);
    R2D_TEST_CHECK_EQ(context_, out[1U].transform_m02, 117.0F);
    R2D_TEST_CHECK_EQ(context_, out[1U].uv_min_x, 0.5F);
    R2D_TEST_CHECK_EQ(context_, out[1U].source_index, 1U);
}

void testGlyphBridgeErrors(R2DT::TestContext& context_)
{
    const std::array<GlyphInstance, 2U> glyphs{{
        {
            .glyph_run_index = 0U, .glyph_id = 70U, .atlas_rect = R2D::makeAabb2(0.0F, 0.0F, 0.5F, 0.5F),
            .position_x = 0.0F, .position_y = 0.0F, .width = 6.0F, .height = 10.0F,
            .color_rgba8 = 0U, .sort_key = 0U, .layer = 0U, .flags = 0U,
        },
        {
            .glyph_run_index = 0U, .glyph_id = 71U, .atlas_rect = R2D::makeAabb2(0.5F, 0.0F, 1.0F, 0.5F),
            .position_x = 6.0F, .position_y = 0.0F, .width = 5.0F, .height = 7.0F,
            .color_rgba8 = 0U, .sort_key = 0U, .layer = 0U, .flags = 0U,
        },
    }};
    constexpr R2D::GlyphSpriteBridgeConfig kConfig{
        .texture_id = 9U, .texture_generation = 2U, .material_id = 4U, .material_generation = 1U, .viewport_width = 0.0F, .viewport_height = 0.0F, .flags = 0U,
    };

    // Two visible glyphs but room for one: writes the first, then reports the shortfall.
    std::array<SpriteInstance, 1U> tiny{};
    auto result = BridgeSystem::run(glyphs, kConfig, tiny);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 1U);

    // Empty input is a clean no-op.
    std::array<SpriteInstance, 1U> out{};
    result = BridgeSystem::run({}, kConfig, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 0U);

    result = R2D::GlyphInstanceToSpriteSystem<int, Dim>::run({}, kConfig, {});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testDecodeMixedWidths(context);
    testDecodeOffsetAndMultiText(context);
    testDecodeInvalidBytes(context);
    testDecodeErrors(context);
    testDecodeUnsupportedDomain(context);
    testGlyphPositionPenWalk(context);
    testGlyphPositionErrors(context);
    testGlyphBridge(context);
    testGlyphBridgeErrors(context);
    return context.result();
}

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("text_shaping_system_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("text_shaping_system_test unknown exception\n", stderr);
        return 1;
    }
}
