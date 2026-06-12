#include <Render2D/Render2D.hpp>

#include "Render2D/Font/FontShapeRuntime.hpp"
#include "support/TestHarness.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <span>

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Codepoint = R2D::Codepoint<Provider, Dim>;
using ShapingRun = R2D::ShapingRun<Provider, Dim>;
using ShapedGlyph = R2D::ShapedGlyph<Provider, Dim>;
using FontRef = R2D::FontRef<Provider, Dim>;
using FontMetrics = R2D::FontMetrics<Provider, Dim>;
using ShapeRuntime = R2D::FontShapeRuntime<Provider, Dim>;

constexpr Codepoint makeCodepoint(R2D::U32 codepoint_, R2D::U32 byte_offset_) noexcept
{
    return {.text_index = 0U, .codepoint = codepoint_, .byte_offset = byte_offset_, .flags = 0U};
}

// Reads a whole file into a caller-owned byte buffer (empty if it cannot open).
R2D::McVector<R2D::U8> readFile(const char* path_)
{
    R2D::McVector<R2D::U8> bytes;
    std::FILE* file = std::fopen(path_, "rb");
    if (file == nullptr) {
        return bytes;
    }
    if (std::fseek(file, 0, SEEK_END) == 0) {
        const long size = std::ftell(file);
        if (size > 0 && std::fseek(file, 0, SEEK_SET) == 0) {
            const auto expected = static_cast<std::size_t>(size);
            bytes.resize(expected);
            const std::size_t read = std::fread(bytes.data(), 1U, expected, file);
            if (read != expected) {
                bytes.clear();
            }
        }
    }
    std::fclose(file);
    return bytes;
}

// Loads the first available system font; leaves bytes empty if none is present.
R2D::McVector<R2D::U8> loadSystemFont()
{
    static constexpr std::array<const char*, 5U> kCandidates{
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/verdana.ttf",
    };
    for (const char* candidate : kCandidates) {
        R2D::McVector<R2D::U8> bytes = readFile(candidate);
        if (!bytes.empty()) {
            return bytes;
        }
    }
    return {};
}

void testShapeLatin(R2DT::TestContext& context_, std::span<const R2D::U8> font_bytes_)
{
    ShapeRuntime runtime;
    auto result = runtime.initialize();
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);

    FontRef font_ref{};
    FontMetrics metrics{};
    result = runtime.loadFontFromMemory(1U, font_bytes_, 32.0F, font_ref, metrics);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, font_ref.id, 1U);
    R2D_TEST_CHECK_EQ(context_, runtime.fontCount(), 1U);
    R2D_TEST_CHECK(context_, metrics.ascent > 0.0F);
    R2D_TEST_CHECK(context_, metrics.line_height > 0.0F);

    // "Ayb" -> three Latin glyphs, no ligatures, all with positive advance.
    const std::array<Codepoint, 3U> codepoints{{
        makeCodepoint(0x41U, 0U), makeCodepoint(0x79U, 1U), makeCodepoint(0x62U, 2U),
    }};
    const std::array<ShapingRun, 1U> runs{{
        {
            .text_index = 0U, .codepoint_first = 0U, .codepoint_count = 3U,
            .font_id = 1U, .script = 0U, .direction = R2D::kTextDirectionLtr, .bidi_level = 0U, .flags = 0U,
        },
    }};
    std::array<ShapedGlyph, 8U> shaped{};
    result = runtime.shape(runs, codepoints, shaped);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 3U);
    for (R2D::U32 index = 0U; index < 3U; ++index) {
        R2D_TEST_CHECK_EQ(context_, shaped[index].run_index, 0U);
        R2D_TEST_CHECK(context_, shaped[index].glyph_id != 0U); // resolved, not .notdef
        R2D_TEST_CHECK(context_, shaped[index].x_advance > 0.0F);
    }

    // Capacity: a 3-glyph run cannot fit into a 2-slot output.
    std::array<ShapedGlyph, 2U> tiny{};
    result = runtime.shape(runs, codepoints, tiny);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);

    // Unknown font id is rejected.
    const std::array<ShapingRun, 1U> bad_font{{
        {
            .text_index = 0U, .codepoint_first = 0U, .codepoint_count = 3U,
            .font_id = 99U, .script = 0U, .direction = R2D::kTextDirectionLtr, .bidi_level = 0U, .flags = 0U,
        },
    }};
    result = runtime.shape(bad_font, codepoints, shaped);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    runtime.shutdown();
    R2D_TEST_CHECK_EQ(context_, runtime.fontCount(), 0U);
}

void testUnsupportedDomain(R2DT::TestContext& context_)
{
    R2D::FontShapeRuntime<int, Dim> runtime;
    const auto result = runtime.initialize();
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testUnsupportedDomain(context);

    const R2D::McVector<R2D::U8> font_bytes = loadSystemFont();
    if (font_bytes.empty()) {
        std::fputs("font_shape_runtime_test: no system font found; skipping shaping checks (pass).\n", stderr);
        return context.result();
    }
    testShapeLatin(context, std::span<const R2D::U8>(font_bytes.data(), font_bytes.size()));
    return context.result();
}

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("font_shape_runtime_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("font_shape_runtime_test unknown exception\n", stderr);
        return 1;
    }
}
