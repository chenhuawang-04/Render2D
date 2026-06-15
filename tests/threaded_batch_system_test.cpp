#include <Render2D/System/ThreadedBatchSystem.hpp>
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
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using BatchRuntime = R2D::ThreadedBatchRuntime<Provider, Dim>;
using BatchRef = R2D::BatchSystem<Provider, Dim>;

// Deliberately not divisible by the worker count, so chunk boundaries fall on an
// uneven draw slice and a batch run that crosses a boundary must still merge into
// the same global batch as the single-thread scan.
inline constexpr R2D::U32 kDrawCount = 4099U;

// run_length controls the batch structure: sort_key changes every run_length
// draws while the other batch-key fields stay constant, giving runs of exactly
// run_length. run_length == 1 yields all-singleton batches; a run_length larger
// than a chunk forces runs to span chunk boundaries; run_length == kDrawCount is
// a single batch spanning every chunk. BatchSystem is the oracle, so the fill
// only needs to exercise the start / merge / cross-chunk cases.
void fillDrawCommands(std::span<DrawCommand> draws_, R2D::U32 run_length_) noexcept
{
    for (R2D::Usize index = 0U; index < draws_.size(); ++index) {
        const auto i = static_cast<R2D::U32>(index);
        draws_[index] = {
            .source_index = i,
            .material_id = 5U,
            .material_generation = 1U,
            .texture_id = 2U,
            .texture_generation = 0U,
            .vertex_first = i * 4U,
            .vertex_count = 4U,
            .index_first = i * 6U,
            .index_count = 6U,
            .instance_first = i,
            .instance_count = 1U,
            .sort_key = i / run_length_,
            .layer = 0U,
            .flags = 0U,
        };
    }
}

[[nodiscard]] bool batchesEqual(
    std::span<const BatchCommand> reference_,
    std::span<const BatchCommand> threaded_,
    R2D::U32 count_) noexcept
{
    return std::memcmp(
        reference_.data(),
        threaded_.data(),
        static_cast<R2D::Usize>(count_) * sizeof(BatchCommand)) == 0;
}

// Forces the threaded path on (threshold 1, several workers) for one fill and
// asserts byte-identical output to the single-thread BatchSystem reference. Uses
// only R2D_TEST_CHECK (not R2D_TEST_REQUIRE, which returns) so it can stay void.
void checkThreadedEqualsReference(
    R2DT::TestContext& context_,
    BatchRuntime& runtime_,
    std::span<DrawCommand> draws_,
    std::span<BatchCommand> reference_,
    std::span<BatchCommand> threaded_,
    R2D::U32 run_length_)
{
    fillDrawCommands(draws_, run_length_);

    const auto reference_result = BatchRef::run(draws_, reference_);
    R2D_TEST_CHECK(context_, reference_result.code == R2D::SystemStatusCode::Ok);

    R2D_TEST_CHECK(context_, runtime_.shouldParallelize(static_cast<R2D::U32>(draws_.size())));
    const auto threaded_result = runtime_.runBatch(draws_, threaded_);
    R2D_TEST_CHECK(context_, threaded_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, threaded_result.write_count, reference_result.write_count);
    R2D_TEST_CHECK(context_, batchesEqual(reference_, threaded_, reference_result.write_count));
}

[[nodiscard]] auto runTest() -> int
{
    R2DT::TestContext context{};

    // Heap-backed (McVector) rather than stack std::array, mirroring the draw-sort
    // test: at kDrawCount these buffers are large enough to overflow the 1 MB
    // default thread stack.
    R2D::McVector<DrawCommand> draws;
    R2D::McVector<BatchCommand> reference;
    R2D::McVector<BatchCommand> threaded;
    draws.resize(kDrawCount);
    reference.resize(kDrawCount);
    threaded.resize(kDrawCount);

    BatchRuntime runtime{{
        .worker_count = 4U,
        .min_items_per_task = 1U,
        .parallel_threshold = 1U,
    }};

    // Several run structures: all-singleton batches, medium runs that cross chunk
    // boundaries, and one run spanning the whole stream across every chunk.
    checkThreadedEqualsReference(context, runtime, draws, reference, threaded, 1U);
    checkThreadedEqualsReference(context, runtime, draws, reference, threaded, 37U);
    checkThreadedEqualsReference(context, runtime, draws, reference, threaded, kDrawCount);

    // The parallel bindless path matches the bindless reference (same oracle).
    fillDrawCommands(draws, 37U);
    const auto bindless_reference = BatchRef::runBindless(draws, reference);
    R2D_TEST_REQUIRE(context, bindless_reference.code == R2D::SystemStatusCode::Ok);
    const auto bindless_threaded = runtime.runBatchBindless(draws, threaded);
    R2D_TEST_REQUIRE(context, bindless_threaded.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, bindless_threaded.write_count, bindless_reference.write_count);
    R2D_TEST_CHECK(context, batchesEqual(reference, threaded, bindless_reference.write_count));

    // Above-threshold gate: a multi-worker runtime whose threshold exceeds the
    // workload routes to the single-thread path and still equals the reference.
    fillDrawCommands(draws, 37U);
    const auto gated_reference = BatchRef::run(draws, reference);
    R2D_TEST_REQUIRE(context, gated_reference.code == R2D::SystemStatusCode::Ok);
    BatchRuntime gated_runtime{{
        .worker_count = 4U,
        .min_items_per_task = 1U,
        .parallel_threshold = kDrawCount + 1U,
    }};
    R2D_TEST_CHECK(context, !gated_runtime.shouldParallelize(kDrawCount));
    const auto gated_result = gated_runtime.runBatch(draws, threaded);
    R2D_TEST_REQUIRE(context, gated_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, gated_result.write_count, gated_reference.write_count);
    R2D_TEST_CHECK(context, batchesEqual(reference, threaded, gated_reference.write_count));

    // Empty input is valid and writes nothing on the threaded path too.
    const auto empty_result = runtime.runBatch(std::span<const DrawCommand>{}, threaded);
    R2D_TEST_CHECK(context, empty_result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, empty_result.write_count, 0U);

    // Capacity below the draw count is rejected before any parallel work (the
    // full-capacity contract that lets the scatter run without bounds checks).
    fillDrawCommands(draws, 37U);
    const auto short_result = runtime.runBatch(
        draws,
        std::span<BatchCommand>{threaded.data(), static_cast<R2D::Usize>(kDrawCount - 1U)});
    R2D_TEST_CHECK(context, short_result.code == R2D::SystemStatusCode::InsufficientCapacity);

    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("threaded_batch_system_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("threaded_batch_system_test unknown exception\n", stderr);
        return 1;
    }
}
