#include <Render2D/Render2D.hpp>

#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using Runtime = R2D::VulkanSyncRuntime<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<Runtime>);
static_assert(R2D::StrictPodComponent<R2D::VulkanSyncRuntimeConfig>);
static_assert(R2D::StrictPodComponent<FrameSync>);

void testInvalidConfig()
{
    Runtime runtime;
    auto result = runtime.initialize({
        .device = VK_NULL_HANDLE,
        .fence_create_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(!runtime.isInitialized());
}

void testSyncLifecycle(const Render2DTest::VulkanSmokeContext& context_)
{
    Runtime runtime;
    auto result = runtime.initialize({
        .device = context_.device,
        .fence_create_flags = VK_FENCE_CREATE_SIGNALED_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.isInitialized());

    auto capacity = runtime.reserveFrameSyncs(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(runtime.frameSyncCapacity() >= 1U);

    FrameSync first_sync{};
    result = runtime.createFrameSync(2U, 3U, first_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(first_sync.frame_index == 2U);
    assert(first_sync.flags == 3U);
    assert(first_sync.sync_id == 0U);
    assert(first_sync.generation == 1U);
    assert(first_sync.image_available_semaphore_id == first_sync.sync_id);
    assert(first_sync.render_finished_semaphore_id == first_sync.sync_id);
    assert(first_sync.in_flight_fence_id == first_sync.sync_id);

    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;
    result = runtime.resolveNativeSync(first_sync, image_available, render_finished, in_flight);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(image_available != VK_NULL_HANDLE);
    assert(render_finished != VK_NULL_HANDLE);
    assert(in_flight != VK_NULL_HANDLE);

    result = runtime.waitFence(first_sync, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.resetFence(first_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.waitFence(first_sync, 0U);
    assert(result.code == R2D::NativeStatusCode::Timeout);

    result = runtime.releaseFrameSync(first_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.frameSyncCount() == 0U);

    FrameSync resolved_sync{};
    result = runtime.resolveFrameSync(first_sync, resolved_sync);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    FrameSync reused_sync{};
    result = runtime.createFrameSync(5U, 6U, reused_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_sync.sync_id == first_sync.sync_id);
    assert(reused_sync.generation == first_sync.generation + 1U);

    result = runtime.releaseFrameSync(reused_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

int main()
{
    try {
        testInvalidConfig();

        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testSyncLifecycle(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
