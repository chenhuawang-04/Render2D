#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdio>
#include <exception>
#include <type_traits>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Runtime = R2D::VulkanPresentRuntime<Provider, Dim>;
using SwapchainRuntime = R2D::VulkanSwapchainRuntime<Provider, Dim>;
using SyncRuntime = R2D::VulkanSyncRuntime<Provider, Dim>;
using SwapchainState = R2D::SwapchainState<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using AcquiredImage = R2D::AcquiredImage<Provider, Dim>;
using PresentCommand = R2D::PresentCommand<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<Runtime>);
static_assert(R2D::StrictPodComponent<R2D::VulkanPresentRuntimeConfig>);
static_assert(R2D::StrictPodComponent<AcquiredImage>);
static_assert(R2D::StrictPodComponent<PresentCommand>);

void testInvalidInitialize(R2DT::TestContext& context_)
{
    Runtime runtime{};
    auto result = runtime.initialize({
        .device = VK_NULL_HANDLE,
        .present_queue = VK_NULL_HANDLE,
    });
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::InvalidInput);
    R2D_TEST_CHECK(context_, !runtime.isInitialized());
}

void testStaleAcquireAndPresent(
    R2DT::TestContext& context_,
    const Render2DTest::VulkanSmokeContext& vulkan_context_)
{
    Runtime runtime{};
    auto result = runtime.initialize({
        .device = vulkan_context_.device,
        .present_queue = vulkan_context_.queue,
    });
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, runtime.isInitialized());

    result = runtime.initialize({
        .device = vulkan_context_.device,
        .present_queue = vulkan_context_.queue,
    });
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::InvalidInput);

    SwapchainRuntime swapchain_runtime{};
    result = swapchain_runtime.initialize({.device = vulkan_context_.device});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);

    SyncRuntime sync_runtime{};
    result = sync_runtime.initialize({
        .device = vulkan_context_.device,
        .fence_create_flags = 0U,
    });
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    auto capacity = sync_runtime.reserveFrameSyncs(1U);
    R2D_TEST_CHECK_EQ(context_, capacity.code, R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    result = sync_runtime.createFrameSync(0U, 0U, frame_sync);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);

    const SwapchainState stale_state{
        .handle = 0U,
        .swapchain_id = 9U,
        .image_first = 0U,
        .image_count = 2U,
        .width = 640U,
        .height = 480U,
        .format = static_cast<R2D::U32>(VK_FORMAT_B8G8R8A8_UNORM),
        .generation = 1U,
        .flags = 0U,
    };
    AcquiredImage acquired{};
    result = runtime.acquireNextImage(
        stale_state,
        frame_sync,
        swapchain_runtime,
        sync_runtime,
        0U,
        0U,
        acquired);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::StaleReference);
    R2D_TEST_CHECK_EQ(context_, acquired.generation, 0U);

    const PresentCommand invalid_command{
        .swapchain_id = stale_state.swapchain_id,
        .image_index = 0U,
        .wait_sync_id = frame_sync.sync_id,
        .wait_sync_generation = 0U,
        .frame_index = frame_sync.frame_index,
        .generation = stale_state.generation,
        .flags = 0U,
    };
    result = runtime.present(invalid_command, swapchain_runtime, sync_runtime);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::InvalidInput);

    const PresentCommand stale_command{
        .swapchain_id = stale_state.swapchain_id,
        .image_index = 0U,
        .wait_sync_id = frame_sync.sync_id,
        .wait_sync_generation = frame_sync.generation,
        .frame_index = frame_sync.frame_index,
        .generation = stale_state.generation,
        .flags = 0U,
    };
    result = runtime.present(stale_command, swapchain_runtime, sync_runtime);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::StaleReference);

    result = sync_runtime.releaseFrameSync(frame_sync);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    runtime.shutdown();
    R2D_TEST_CHECK(context_, !runtime.isInitialized());
}

void testUnsupportedDomain(
    R2DT::TestContext& context_,
    const Render2DTest::VulkanSmokeContext& vulkan_context_)
{
    R2D::VulkanPresentRuntime<int, Dim> runtime{};
    const auto result = runtime.initialize({
        .device = vulkan_context_.device,
        .present_queue = vulkan_context_.queue,
    });
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testInvalidInitialize(context);

    Render2DTest::VulkanSmokeContext vulkan_context{};
    if (!Render2DTest::createVulkanSmokeContext(vulkan_context)) {
        return context.result();
    }

    testStaleAcquireAndPresent(context, vulkan_context);
    testUnsupportedDomain(context, vulkan_context);
    Render2DTest::destroyVulkanSmokeContext(vulkan_context);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("vulkan_present_runtime_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("vulkan_present_runtime_test unknown exception\n", stderr);
        return 1;
    }
}
