#include <Render2D/Render2D.hpp>

#include "Render2D/Font/BidiItemizeRuntime.hpp"
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
using ShapingRun = R2D::ShapingRun<Provider, Dim>;
using BidiRuntime = R2D::BidiItemizeRuntime<Provider, Dim>;

constexpr Text makeText(R2D::U32 font_id_) noexcept
{
    return {
        .source_id = 1U,
        .font_id = font_id_,
        .utf8_buffer_id = 0U,
        .utf8_offset = 0U,
        .utf8_size = 0U,
        .color_rgba8 = 0xFFFFFFFFU,
        .pixel_size = 16.0F,
        .layer = 0U,
        .flags = 0U,
    };
}

constexpr Codepoint makeCodepoint(R2D::U32 text_index_, R2D::U32 codepoint_) noexcept
{
    return {.text_index = text_index_, .codepoint = codepoint_, .byte_offset = 0U, .flags = 0U};
}

// "abc" + Hebrew "אבג" (U+05D0..05D2) + "de" under an LTR base direction.
// UAX#9 must yield three visual runs: Latin (LTR), Hebrew (RTL), Latin (LTR),
// each single-script, with the Hebrew run at odd embedding level.
void testMixedBidiRuns(R2DT::TestContext& context_)
{
    const std::array<Codepoint, 8U> codepoints{{
        makeCodepoint(0U, 0x61U), makeCodepoint(0U, 0x62U), makeCodepoint(0U, 0x63U),
        makeCodepoint(0U, 0x05D0U), makeCodepoint(0U, 0x05D1U), makeCodepoint(0U, 0x05D2U),
        makeCodepoint(0U, 0x64U), makeCodepoint(0U, 0x65U),
    }};
    const std::array<Text, 1U> texts{{makeText(7U)}};
    std::array<ShapingRun, 8U> out{};

    BidiRuntime runtime;
    runtime.reserve(8U);
    const auto result = runtime.itemize(codepoints, texts, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 8U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 3U);

    // Run 0: Latin, logical [0,3), LTR (even level).
    R2D_TEST_CHECK_EQ(context_, out[0U].codepoint_first, 0U);
    R2D_TEST_CHECK_EQ(context_, out[0U].codepoint_count, 3U);
    R2D_TEST_CHECK_EQ(context_, out[0U].direction, R2D::kTextDirectionLtr);
    R2D_TEST_CHECK_EQ(context_, out[0U].bidi_level, 0U);
    R2D_TEST_CHECK_EQ(context_, out[0U].font_id, 7U);

    // Run 1: Hebrew, logical [3,6), RTL (odd level), different script than Latin.
    R2D_TEST_CHECK_EQ(context_, out[1U].codepoint_first, 3U);
    R2D_TEST_CHECK_EQ(context_, out[1U].codepoint_count, 3U);
    R2D_TEST_CHECK_EQ(context_, out[1U].direction, R2D::kTextDirectionRtl);
    R2D_TEST_CHECK(context_, (out[1U].bidi_level & 1U) != 0U);
    R2D_TEST_CHECK(context_, out[1U].script != out[0U].script);

    // Run 2: Latin again, logical [6,8), LTR, same script as run 0.
    R2D_TEST_CHECK_EQ(context_, out[2U].codepoint_first, 6U);
    R2D_TEST_CHECK_EQ(context_, out[2U].codepoint_count, 2U);
    R2D_TEST_CHECK_EQ(context_, out[2U].direction, R2D::kTextDirectionLtr);
    R2D_TEST_CHECK_EQ(context_, out[2U].script, out[0U].script);
}

// A run that is entirely RTL (Arabic U+0627..0629) collapses to one RTL run.
void testPureRtlRun(R2DT::TestContext& context_)
{
    const std::array<Codepoint, 3U> codepoints{{
        makeCodepoint(0U, 0x0627U), makeCodepoint(0U, 0x0628U), makeCodepoint(0U, 0x0629U),
    }};
    const std::array<Text, 1U> texts{{makeText(2U)}};
    std::array<ShapingRun, 4U> out{};

    BidiRuntime runtime;
    const auto result = runtime.itemize(codepoints, texts, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 1U);
    R2D_TEST_CHECK_EQ(context_, out[0U].codepoint_first, 0U);
    R2D_TEST_CHECK_EQ(context_, out[0U].codepoint_count, 3U);
    R2D_TEST_CHECK_EQ(context_, out[0U].direction, R2D::kTextDirectionRtl);
    R2D_TEST_CHECK(context_, (out[0U].bidi_level & 1U) != 0U);
}

// Two texts each itemize independently; runs carry the right text/font ids.
void testMultiTextItemize(R2DT::TestContext& context_)
{
    const std::array<Codepoint, 4U> codepoints{{
        makeCodepoint(0U, 0x41U), makeCodepoint(0U, 0x42U),
        makeCodepoint(1U, 0x43U), makeCodepoint(1U, 0x44U),
    }};
    const std::array<Text, 2U> texts{{makeText(5U), makeText(6U)}};
    std::array<ShapingRun, 4U> out{};

    BidiRuntime runtime;
    const auto result = runtime.itemize(codepoints, texts, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 2U);
    R2D_TEST_CHECK_EQ(context_, out[0U].text_index, 0U);
    R2D_TEST_CHECK_EQ(context_, out[0U].font_id, 5U);
    R2D_TEST_CHECK_EQ(context_, out[1U].text_index, 1U);
    R2D_TEST_CHECK_EQ(context_, out[1U].font_id, 6U);
}

void testItemizeErrors(R2DT::TestContext& context_)
{
    const std::array<Codepoint, 2U> codepoints{{makeCodepoint(0U, 0x41U), makeCodepoint(0U, 0x42U)}};
    const std::array<Text, 1U> texts{{makeText(1U)}};

    // Empty input is a clean no-op.
    std::array<ShapingRun, 2U> out{};
    BidiRuntime runtime;
    auto result = runtime.itemize({}, texts, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 0U);

    // Output too small to hold the single produced run.
    std::array<ShapingRun, 0U> tiny{};
    result = runtime.itemize(codepoints, texts, tiny);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);

    // Codepoint references a text index past the text stream.
    const std::array<Codepoint, 1U> bad{{makeCodepoint(9U, 0x41U)}};
    result = runtime.itemize(bad, texts, out);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);
}

void testUnsupportedDomain(R2DT::TestContext& context_)
{
    R2D::BidiItemizeRuntime<int, Dim> runtime;
    const auto result = runtime.itemize({}, {}, {});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testMixedBidiRuns(context);
    testPureRtlRun(context);
    testMultiTextItemize(context);
    testItemizeErrors(context);
    testUnsupportedDomain(context);
    return context.result();
}

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("bidi_itemize_runtime_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("bidi_itemize_runtime_test unknown exception\n", stderr);
        return 1;
    }
}
