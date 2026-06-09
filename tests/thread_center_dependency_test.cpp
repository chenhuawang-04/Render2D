#include <Render2D/Render2D.hpp>
#include <thread_center/thread_center.hpp>

#include "support/TestHarness.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <exception>

namespace {

using Provider = Render2D::VulkanNativeProvider;
using Dim = Render2D::Dim2;

[[nodiscard]] auto makeRenderTaskDesc(const char* name_) noexcept -> ThreadCenter::TaskDesc
{
    return ThreadCenter::TaskDesc{
        .name = name_,
        .domain = ThreadCenter::ScheduleDomain::RENDERING,
        .task_kind = ThreadCenter::TaskKind::COMPUTE,
        .lane = ThreadCenter::ExecutionLane::RENDER_ASSIST,
        .priority = ThreadCenter::TaskPriority::NORMAL,
        .budget_class = ThreadCenter::BudgetClass::FRAME_BOUND,
    };
}

[[nodiscard]] auto runTest() -> int
{
    auto context = Render2D::TestSupport::TestContext{};

    static_assert(Render2D::SupportedRenderComponent<Provider, Dim, Render2D::DrawCommand<Provider, Dim>>);
    static_assert(ThreadCenter::isRealtimeBudget(ThreadCenter::BudgetClass::REALTIME));
    static_assert(!ThreadCenter::isBlockingLane(ThreadCenter::ExecutionLane::RENDER_ASSIST));

    auto executor_config = ThreadCenter::ExecutorConfig{};
    executor_config.workers = 2U;

    auto center = ThreadCenter::Center{executor_config};
    R2D_TEST_CHECK_EQ(context, center.workerCount(), 2U);

    auto observed_stage = std::atomic<std::uint32_t>{0U};
    auto plan = center.makePlan();

    const auto prepare_task = plan.task(makeRenderTaskDesc("render2d.prepare"), [&observed_stage] {
        observed_stage.store(1U, std::memory_order_release);
    });

    const auto build_task = plan.task(makeRenderTaskDesc("render2d.build"), [&observed_stage] {
        const auto previous = observed_stage.load(std::memory_order_acquire);
        observed_stage.store(previous == 1U ? 2U : 0xFFFF'FFFFU, std::memory_order_release);
    });

    plan.precede(prepare_task, build_task);

    auto run_handle = center.dispatch(plan);
    run_handle.wait();
    center.waitIdle();

    R2D_TEST_CHECK_EQ(context, observed_stage.load(std::memory_order_acquire), 2U);

    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("thread_center_dependency_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("thread_center_dependency_test unknown exception\n", stderr);
        return 1;
    }
}
