#include <Render2D/System/ThreadedDrawSort.hpp>
#include <Render2D/Memory/RenderVector.hpp>
#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <span>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using SortedItem = R2D::SortedItem<Provider, Dim>;
using SortRuntime = R2D::ThreadedDrawSortRuntime<Provider, Dim>;

// Deliberately not divisible by the worker count, so chunk boundaries fall on an
// uneven draw slice and the per-chunk scatter offsets must still merge into the
// same global order as the single-thread radix sort.
inline constexpr R2D::U32 kDrawCount = 4099U;

// Build draws whose sort keys collide heavily (small layer/material/texture
// moduli) so most draws share a key. Stability then matters: equal-key draws must
// keep their original relative order. Each draw also carries a unique payload
// (source_index = i and i-derived fields) so a non-stable parallel sort would
// reorder equal-key draws and break the byte comparison below.
void fillDrawCommands(std::span<DrawCommand> draws_) noexcept
{
    for (R2D::Usize index = 0U; index < draws_.size(); ++index) {
        const auto i = static_cast<R2D::U32>(index);
        const R2D::U32 layer = i % 3U;
        const R2D::U32 material_id = (i / 3U) % 4U;
        const R2D::U32 texture_id = (i / 7U) % 5U;
        const R2D::U32 flags = i % 2U;
        const R2D::U32 sort_key = R2D::makeDrawSortKey(layer, material_id, texture_id, flags);
        draws_[index] = {
            .source_index = i,
            .material_id = material_id,
            .material_generation = 1U,
            .texture_id = texture_id,
            .texture_generation = 0U,
            .vertex_first = i * 4U,
            .vertex_count = 4U,
            .index_first = i * 6U,
            .index_count = 6U,
            .instance_first = i,
            .instance_count = 1U,
            .sort_key = sort_key,
            .layer = layer,
            .flags = flags,
        };
    }
}

[[nodiscard]] bool sortedEqualsReference(
    std::span<const DrawCommand> reference_,
    std::span<const DrawCommand> threaded_,
    R2D::U32 count_) noexcept
{
    return std::memcmp(
        reference_.data(),
        threaded_.data(),
        static_cast<R2D::Usize>(count_) * sizeof(DrawCommand)) == 0;
}

[[nodiscard]] auto runTest() -> int
{
    R2DT::TestContext context{};

    // Heap-backed (McVector) rather than stack std::array: at kDrawCount the ten
    // buffers below total ~1 MB and would overflow the 1 MB default thread stack.
    R2D::McVector<DrawCommand> draws;
    draws.resize(kDrawCount);
    fillDrawCommands(draws);

    // Single-thread reference: the canonical deterministic stable radix sort.
    R2D::McVector<DrawCommand> reference_sorted;
    R2D::McVector<SortedItem> reference_scratch_a;
    R2D::McVector<SortedItem> reference_scratch_b;
    reference_sorted.resize(kDrawCount);
    reference_scratch_a.resize(kDrawCount);
    reference_scratch_b.resize(kDrawCount);
    const auto reference_result = R2D::DrawSortSystem<Provider, Dim>::run(
        draws,
        reference_sorted,
        reference_scratch_a,
        reference_scratch_b);
    R2D_TEST_REQUIRE(context, reference_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, reference_result.write_count, kDrawCount);

    // Threaded path forced on (threshold 1, several workers) so the draws split
    // across chunks; the parallel radix output must be byte-identical.
    R2D::McVector<DrawCommand> threaded_sorted;
    R2D::McVector<SortedItem> threaded_scratch_a;
    R2D::McVector<SortedItem> threaded_scratch_b;
    threaded_sorted.resize(kDrawCount);
    threaded_scratch_a.resize(kDrawCount);
    threaded_scratch_b.resize(kDrawCount);
    SortRuntime runtime{{
        .worker_count = 4U,
        .min_items_per_task = 1U,
        .parallel_threshold = 1U,
    }};
    R2D_TEST_CHECK(context, runtime.shouldParallelize(kDrawCount));
    const auto threaded_result = runtime.runDrawSort(
        draws,
        threaded_sorted,
        threaded_scratch_a,
        threaded_scratch_b);
    R2D_TEST_REQUIRE(context, threaded_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, threaded_result.write_count, kDrawCount);
    R2D_TEST_CHECK(context, sortedEqualsReference(reference_sorted, threaded_sorted, kDrawCount));

    // Above-threshold gate: a multi-worker runtime whose threshold exceeds the
    // workload routes to the single-thread path and still equals the reference.
    R2D::McVector<DrawCommand> gated_sorted;
    R2D::McVector<SortedItem> gated_scratch_a;
    R2D::McVector<SortedItem> gated_scratch_b;
    gated_sorted.resize(kDrawCount);
    gated_scratch_a.resize(kDrawCount);
    gated_scratch_b.resize(kDrawCount);
    SortRuntime gated_runtime{{
        .worker_count = 4U,
        .min_items_per_task = 1U,
        .parallel_threshold = kDrawCount + 1U,
    }};
    R2D_TEST_CHECK(context, !gated_runtime.shouldParallelize(kDrawCount));
    const auto gated_result = gated_runtime.runDrawSort(
        draws,
        gated_sorted,
        gated_scratch_a,
        gated_scratch_b);
    R2D_TEST_REQUIRE(context, gated_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, gated_result.write_count, kDrawCount);
    R2D_TEST_CHECK(context, sortedEqualsReference(reference_sorted, gated_sorted, kDrawCount));

    // Empty input is valid and writes nothing on the threaded path too.
    const auto empty_result = runtime.runDrawSort(
        std::span<const DrawCommand>{},
        threaded_sorted,
        threaded_scratch_a,
        threaded_scratch_b);
    R2D_TEST_CHECK(context, empty_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, empty_result.write_count, 0U);

    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("threaded_draw_sort_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("threaded_draw_sort_test unknown exception\n", stderr);
        return 1;
    }
}
