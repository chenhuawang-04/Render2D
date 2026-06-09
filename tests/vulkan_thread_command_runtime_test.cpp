#include "support/VulkanSmokeContext.hpp"

#include <Render2D/Render2D.hpp>

#include <vulkan/vulkan.h>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using Runtime = R2D::VulkanThreadCommandRuntime<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<Runtime>);
static_assert(R2D::StrictPodComponent<R2D::VulkanThreadCommandRuntimeConfig>);
static_assert(R2D::StrictPodComponent<NativeCommandBufferRef>);

void testInvalidConfig()
{
    Runtime runtime;
    auto result = runtime.initialize({
        .device = VK_NULL_HANDLE,
        .queue_family_index = 0U,
        .command_pool_flags = 0U,
        .thread_count = 2U,
    });
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(!runtime.isInitialized());

    auto capacity = runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    NativeCommandBufferRef ref{};
    result = runtime.allocateCommandBufferRef(
        0U,
        0U,
        {.first = 0U, .count = 0U},
        {.first = 0U, .count = 0U},
        0U,
        ref);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
}

void testThreadCommandLifecycle(const Render2DTest::VulkanSmokeContext& context_)
{
    Runtime runtime;
    auto result = runtime.initialize({
        .device = context_.device,
        .queue_family_index = context_.queue_family_index,
        .command_pool_flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .thread_count = 2U,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.isInitialized());
    assert(runtime.threadCount() == 2U);
    assert(runtime.nativeCommandPool(0U) != VK_NULL_HANDLE);
    assert(runtime.nativeCommandPool(1U) != VK_NULL_HANDLE);
    assert(runtime.nativeCommandPool(2U) == VK_NULL_HANDLE);

    auto capacity = runtime.reserveCommandBuffers(2U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(runtime.commandBufferCapacity() >= 2U);

    NativeCommandBufferRef first_ref{};
    result = runtime.allocateCommandBufferRef(
        0U,
        3U,
        {.first = 4U, .count = 5U},
        {.first = 6U, .count = 7U},
        8U,
        first_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(first_ref.command_buffer_id == 0U);
    assert(first_ref.generation == 1U);
    assert(runtime.threadCommandBufferCount(0U) == 1U);

    NativeCommandBufferRef second_ref{};
    result = runtime.allocateCommandBufferRef(
        1U,
        9U,
        {.first = 10U, .count = 11U},
        {.first = 12U, .count = 13U},
        14U,
        second_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(second_ref.command_buffer_id == 1U);
    assert(second_ref.generation == 1U);
    assert(runtime.threadCommandBufferCount(1U) == 1U);
    assert(runtime.commandBufferCount() == 2U);

    NativeCommandBufferRef overflow_ref{};
    result = runtime.allocateCommandBufferRef(
        0U,
        0U,
        {.first = 0U, .count = 0U},
        {.first = 0U, .count = 0U},
        0U,
        overflow_ref);
    assert(result.code == R2D::NativeStatusCode::OutOfCapacity);

    result = runtime.allocateCommandBufferRef(
        2U,
        0U,
        {.first = 0U, .count = 0U},
        {.first = 0U, .count = 0U},
        0U,
        overflow_ref);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = runtime.resolveNativeCommandBuffer(first_ref, command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(command_buffer != VK_NULL_HANDLE);

    result = runtime.beginCommandBuffer(first_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.endCommandBuffer(first_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.beginCommandBuffer(second_ref, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.endCommandBuffer(second_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.resetThreadCommandPool(0U, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.beginCommandBuffer(first_ref, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.endCommandBuffer(first_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.resetCommandBuffer(second_ref, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.resetAllCommandPools(0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = runtime.releaseCommandBufferRef(first_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.threadCommandBufferCount(0U) == 0U);
    result = runtime.releaseCommandBufferRef(second_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.threadCommandBufferCount(1U) == 0U);
    assert(runtime.commandBufferCount() == 0U);

    result = runtime.resolveNativeCommandBuffer(first_ref, command_buffer);
    assert(result.code == R2D::NativeStatusCode::StaleReference);
    assert(command_buffer == VK_NULL_HANDLE);

    NativeCommandBufferRef reused_ref{};
    result = runtime.allocateCommandBufferRef(
        1U,
        15U,
        {.first = 0U, .count = 1U},
        {.first = 0U, .count = 0U},
        0U,
        reused_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_ref.command_buffer_id == second_ref.command_buffer_id);
    assert(reused_ref.generation == second_ref.generation + 1U);

    result = runtime.releaseCommandBufferRef(reused_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    runtime.shutdown();
    assert(!runtime.isInitialized());
}

int main()
{
    Render2DTest::VulkanSmokeContext context{};
    try {
        testInvalidConfig();

        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testThreadCommandLifecycle(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        Render2DTest::destroyVulkanSmokeContext(context);
        return 1;
    }

    return 0;
}
