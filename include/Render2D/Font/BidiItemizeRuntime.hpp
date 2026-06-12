#pragma once

// Stage 19E: bidirectional + script itemization (UAX#9) via SheenBidi.
//
// This header is part of the font/text runtime and links SheenBidi. It is NOT
// included by the public umbrella `Render2D.hpp`; consumers link the internal
// `render2d_font_runtime_support` target (the ThreadCenter isolation pattern).
// Inputs and outputs are Strict POD `std::span`s, so everything downstream of
// this stage stays pure and deterministic; only the implementation links a
// third-party library.

#include "Render2D/Component/Text.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Memory/RenderVector.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <SheenBidi/SheenBidi.h>

#include <span>

namespace Render2D {

// Resolves the Unicode Bidirectional Algorithm (embedding levels + per-line
// visual reordering) and script segmentation for each Text's Codepoint slice,
// emitting one ShapingRun per maximal span of uniform (embedding level + script)
// in visual order. Each run is single-script and single-direction, which is
// exactly what HarfBuzz requires per `hb_shape` call.
//
// Codepoints are expected grouped contiguously by ascending `text_index` (the
// order `Utf8DecodeSystem` produces). One SheenBidi paragraph is created per
// hard line break inside a text; a single line spans each paragraph (soft-wrap
// line breaking is layered on later in GlyphPositionSystem). `codepoint_first`/
// `codepoint_count` index the input Codepoint stream in logical order; the runs
// themselves are emitted in visual (left-to-right on screen) order.
template<class Provider, class Dim>
class BidiItemizeRuntime {
public:
    // Optional: pre-grow the scratch buffers to avoid reallocation during the
    // first itemize of up to max_codepoints_ codepoints in a single text.
    void reserve(U32 max_codepoints_)
    {
        utf32_scratch.reserve(max_codepoints_);
        script_of.reserve(max_codepoints_);
    }

    SystemResult itemize(
        std::span<const Codepoint<Provider, Dim>> codepoints_,
        std::span<const Text<Provider, Dim>> texts_,
        std::span<ShapingRun<Provider, Dim>> out_runs_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(codepoints_.size()) ||
                !isSystemResultCountRepresentable(texts_.size()) ||
                !isSystemResultCountRepresentable(out_runs_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }
            if (codepoints_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }

            const Usize total = codepoints_.size();
            U32 write_count = 0U;
            Usize text_begin = 0U;
            while (text_begin < total) {
                const U32 text_index = codepoints_[text_begin].text_index;
                Usize text_end = text_begin;
                while (text_end < total && codepoints_[text_end].text_index == text_index) {
                    ++text_end;
                }
                if (text_index >= texts_.size()) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(text_begin),
                        .write_count = write_count,
                    };
                }

                const SystemStatusCode code = itemizeText(
                    codepoints_, text_begin, text_end, text_index, texts_[text_index], out_runs_, write_count);
                if (code != SystemStatusCode::Ok) {
                    return {
                        .code = code,
                        .read_count = static_cast<U32>(text_begin),
                        .write_count = write_count,
                    };
                }
                text_begin = text_end;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(total),
                .write_count = write_count,
            };
        }
    }

private:
    struct ScriptSegment {
        U32 start;
        U32 length;
        U32 script;
    };

    SystemStatusCode itemizeText(
        std::span<const Codepoint<Provider, Dim>> codepoints_,
        Usize text_begin_,
        Usize text_end_,
        U32 text_index_,
        const Text<Provider, Dim>& text_,
        std::span<ShapingRun<Provider, Dim>> out_runs_,
        U32& write_count_)
    {
        const Usize count = text_end_ - text_begin_;
        if (count == 0U) {
            return SystemStatusCode::Ok;
        }

        utf32_scratch.resize(count);
        script_of.resize(count);
        for (Usize index = 0U; index < count; ++index) {
            utf32_scratch[index] = static_cast<SBCodepoint>(codepoints_[text_begin_ + index].codepoint);
            script_of[index] = static_cast<U32>(SBScriptZYYY);
        }

        const SBCodepointSequence sequence{
            .stringEncoding = SBStringEncodingUTF32,
            .stringBuffer = utf32_scratch.data(),
            .stringLength = static_cast<SBUInteger>(count),
        };

        fillScriptRuns(sequence, count);

        SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
        if (algorithm == nullptr) {
            return SystemStatusCode::InvalidInput;
        }

        SystemStatusCode code = SystemStatusCode::Ok;
        SBUInteger paragraph_start = 0U;
        while (paragraph_start < count && code == SystemStatusCode::Ok) {
            SBParagraphRef paragraph = SBAlgorithmCreateParagraph(
                algorithm, paragraph_start, static_cast<SBUInteger>(count) - paragraph_start, SBLevelDefaultLTR);
            if (paragraph == nullptr) {
                code = SystemStatusCode::InvalidInput;
                break;
            }

            const SBUInteger paragraph_offset = SBParagraphGetOffset(paragraph);
            const SBUInteger paragraph_length = SBParagraphGetLength(paragraph);
            SBLineRef line = SBParagraphCreateLine(paragraph, paragraph_offset, paragraph_length);
            if (line != nullptr) {
                const SBUInteger run_count = SBLineGetRunCount(line);
                const SBRun* runs = SBLineGetRunsPtr(line);
                for (SBUInteger run_index = 0U; run_index < run_count && code == SystemStatusCode::Ok; ++run_index) {
                    code = emitRun(runs[run_index], text_begin_, text_index_, text_, out_runs_, write_count_);
                }
                SBLineRelease(line);
            }

            paragraph_start = paragraph_offset + paragraph_length;
            SBParagraphRelease(paragraph);
        }

        SBAlgorithmRelease(algorithm);
        return code;
    }

    void fillScriptRuns(const SBCodepointSequence& sequence_, Usize count_)
    {
        SBScriptLocatorRef locator = SBScriptLocatorCreate();
        if (locator == nullptr) {
            return;
        }
        SBScriptLocatorLoadCodepoints(locator, &sequence_);
        while (SBScriptLocatorMoveNext(locator) != SBFalse) {
            const SBScriptAgent* agent = SBScriptLocatorGetAgent(locator);
            const Usize first = static_cast<Usize>(agent->offset);
            const Usize last = first + static_cast<Usize>(agent->length);
            for (Usize index = first; index < last && index < count_; ++index) {
                script_of[index] = static_cast<U32>(agent->script);
            }
        }
        SBScriptLocatorRelease(locator);
    }

    // Sub-divides one visual embedding-level run into single-script segments and
    // emits them in visual order: forward for LTR (even level), reversed for RTL
    // (odd level). The codepoint indices stay logical; only run emission order
    // reflects the visual direction (HarfBuzz reverses within a run itself).
    SystemStatusCode emitRun(
        const SBRun& run_,
        Usize text_begin_,
        U32 text_index_,
        const Text<Provider, Dim>& text_,
        std::span<ShapingRun<Provider, Dim>> out_runs_,
        U32& write_count_)
    {
        const U32 run_offset = static_cast<U32>(run_.offset);
        const U32 run_length = static_cast<U32>(run_.length);
        if (run_length == 0U) {
            return SystemStatusCode::Ok;
        }

        segments.clear();
        U32 segment_start = run_offset;
        U32 segment_script = script_of[run_offset];
        for (U32 index = run_offset + 1U; index < run_offset + run_length; ++index) {
            if (script_of[index] != segment_script) {
                segments.push_back({.start = segment_start, .length = index - segment_start, .script = segment_script});
                segment_start = index;
                segment_script = script_of[index];
            }
        }
        segments.push_back({
            .start = segment_start,
            .length = run_offset + run_length - segment_start,
            .script = segment_script,
        });

        const bool is_rtl = (run_.level & 1U) != 0U;
        const U32 direction = is_rtl ? kTextDirectionRtl : kTextDirectionLtr;
        const Usize segment_count = segments.size();
        for (Usize step = 0U; step < segment_count; ++step) {
            const ScriptSegment& segment = is_rtl ? segments[segment_count - 1U - step] : segments[step];
            if (static_cast<Usize>(write_count_) >= out_runs_.size()) {
                return SystemStatusCode::InsufficientCapacity;
            }
            out_runs_[write_count_] = {
                .text_index = text_index_,
                .codepoint_first = static_cast<U32>(text_begin_) + segment.start,
                .codepoint_count = segment.length,
                .font_id = text_.font_id,
                .script = segment.script,
                .direction = direction,
                .bidi_level = static_cast<U32>(run_.level),
                .flags = 0U,
            };
            ++write_count_;
        }
        return SystemStatusCode::Ok;
    }

    McVector<SBCodepoint> utf32_scratch;
    McVector<U32> script_of;
    McVector<ScriptSegment> segments;
};

} // namespace Render2D
