#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <type_traits>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using DestroyCommand = R2D::DeferredDestroyCommand<Provider, Dim>;
using Runtime = R2D::NativeDeferredDestroyRuntime<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<Runtime>);
static_assert(R2D::StrictPodComponent<R2D::NativeDeferredDestroyRuntimeConfig>);
static_assert(R2D::StrictPodComponent<R2D::NativeDeferredDestroyDrainResult>);
static_assert(R2D::StrictPodComponent<DestroyCommand>);

[[nodiscard]] constexpr DestroyCommand makeCommand(
    R2D::NativeObjectKind kind_,
    R2D::U32 object_id_,
    R2D::U32 generation_,
    R2D::U32 retire_frame_index_) noexcept
{
    return {
        .handle = static_cast<R2D::U64>(0x1000U + object_id_),
        .aux_handle = static_cast<R2D::U64>(0x2000U + object_id_),
        .object_kind = static_cast<R2D::U32>(kind_),
        .object_id = object_id_,
        .generation = generation_,
        .retire_frame_index = retire_frame_index_,
        .flags = R2D::kNativeDeferredDestroyNoFlags,
        .reserved = 0U,
    };
}

void testInvalidAndCapacity(R2DT::TestContext& context_)
{
    Runtime runtime{};
    auto result = runtime.configure({.safe_frame_lag = 2U});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, runtime.safeFrameLag(), 2U);

    auto capacity = runtime.reserveDestroyCommands(1U);
    R2D_TEST_CHECK_EQ(context_, capacity.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, runtime.pendingCapacity() >= 1U);

    DestroyCommand invalid{};
    invalid.object_kind = static_cast<R2D::U32>(R2D::NativeObjectKind::Buffer);
    result = runtime.enqueue(invalid);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::InvalidInput);
    R2D_TEST_CHECK_EQ(context_, runtime.pendingCount(), 0U);

    result = runtime.enqueue(makeCommand(R2D::NativeObjectKind::Buffer, 7U, 1U, 4U));
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, runtime.pendingCount(), 1U);

    result = runtime.enqueue(makeCommand(R2D::NativeObjectKind::Image, 8U, 1U, 4U));
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::OutOfCapacity);
    R2D_TEST_CHECK_EQ(context_, runtime.pendingCount(), 1U);
}

void testDrainReadyPreservesPendingOrder(R2DT::TestContext& context_)
{
    Runtime runtime{};
    auto result = runtime.configure({.safe_frame_lag = 2U});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    auto capacity = runtime.reserveDestroyCommands(4U);
    R2D_TEST_CHECK_EQ(context_, capacity.code, R2D::NativeStatusCode::Ok);

    result = runtime.enqueue(makeCommand(R2D::NativeObjectKind::Buffer, 1U, 2U, 3U));
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    result = runtime.enqueue(makeCommand(R2D::NativeObjectKind::Image, 2U, 3U, 5U));
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);

    std::array<DestroyCommand, 4U> drained{};
    auto drain = runtime.drainReady(2U, drained);
    R2D_TEST_CHECK_EQ(context_, drain.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, drain.ready_count, 0U);
    R2D_TEST_CHECK_EQ(context_, drain.drained_count, 0U);
    R2D_TEST_CHECK_EQ(context_, drain.pending_count, 2U);

    drain = runtime.drainReady(3U, drained);
    R2D_TEST_CHECK_EQ(context_, drain.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, drain.ready_count, 1U);
    R2D_TEST_CHECK_EQ(context_, drain.drained_count, 1U);
    R2D_TEST_CHECK_EQ(context_, drain.pending_count, 1U);
    R2D_TEST_CHECK_EQ(context_, drained[0].object_id, 1U);
    R2D_TEST_CHECK_EQ(context_, runtime.pendingCount(), 1U);

    result = runtime.enqueueAfterSafeLag(
        makeCommand(R2D::NativeObjectKind::Pipeline, 3U, 4U, 0U),
        4U);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    result = runtime.enqueue(makeCommand(R2D::NativeObjectKind::Descriptor, 4U, 5U, 6U));
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, runtime.pendingCount(), 3U);

    std::array<DestroyCommand, 1U> short_output{};
    drain = runtime.drainReady(6U, short_output);
    R2D_TEST_CHECK_EQ(context_, drain.code, R2D::NativeStatusCode::OutOfCapacity);
    R2D_TEST_CHECK_EQ(context_, drain.ready_count, 3U);
    R2D_TEST_CHECK_EQ(context_, drain.drained_count, 0U);
    R2D_TEST_CHECK_EQ(context_, drain.pending_count, 3U);
    R2D_TEST_CHECK_EQ(context_, runtime.pendingCount(), 3U);

    drain = runtime.drainReady(6U, drained);
    R2D_TEST_CHECK_EQ(context_, drain.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, drain.ready_count, 3U);
    R2D_TEST_CHECK_EQ(context_, drain.drained_count, 3U);
    R2D_TEST_CHECK_EQ(context_, drain.pending_count, 0U);
    R2D_TEST_CHECK_EQ(context_, drained[0].object_id, 2U);
    R2D_TEST_CHECK_EQ(context_, drained[1].object_id, 3U);
    R2D_TEST_CHECK_EQ(context_, drained[2].object_id, 4U);
}

void testFrameWrapComparison(R2DT::TestContext& context_)
{
    Runtime runtime{};
    const auto capacity = runtime.reserveDestroyCommands(1U);
    R2D_TEST_CHECK_EQ(context_, capacity.code, R2D::NativeStatusCode::Ok);
    const auto result = runtime.enqueue(makeCommand(
        R2D::NativeObjectKind::CommandBuffer,
        9U,
        2U,
        0xFFFF'FFFEU));
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);

    std::array<DestroyCommand, 1U> drained{};
    const auto drain = runtime.drainReady(0U, drained);
    R2D_TEST_CHECK_EQ(context_, drain.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, drain.ready_count, 1U);
    R2D_TEST_CHECK_EQ(context_, drain.drained_count, 1U);
    R2D_TEST_CHECK_EQ(context_, drained[0].object_id, 9U);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testInvalidAndCapacity(context);
    testDrainReadyPreservesPendingOrder(context);
    testFrameWrapComparison(context);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("native_deferred_destroy_runtime_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("native_deferred_destroy_runtime_test unknown exception\n", stderr);
        return 1;
    }
}
