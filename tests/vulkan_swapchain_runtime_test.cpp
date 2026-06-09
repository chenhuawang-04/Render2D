#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdio>
#include <exception>
#include <type_traits>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Runtime = R2D::VulkanSwapchainRuntime<Provider, Dim>;
using SwapchainState = R2D::SwapchainState<Provider, Dim>;
using SwapchainImageRef = R2D::SwapchainImageRef<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<Runtime>);
static_assert(R2D::StrictPodComponent<R2D::VulkanSwapchainRuntimeConfig>);
static_assert(R2D::StrictPodComponent<R2D::VulkanSwapchainCreateConfig>);
static_assert(R2D::StrictPodComponent<R2D::VulkanSwapchainAdoptConfig>);
static_assert(R2D::StrictPodComponent<SwapchainState>);
static_assert(R2D::StrictPodComponent<SwapchainImageRef>);

void testInvalidAndReserve(R2DT::TestContext& context_, VkDevice device_)
{
    Runtime runtime{};
    auto result = runtime.initialize({.device = VK_NULL_HANDLE});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::InvalidInput);
    R2D_TEST_CHECK(context_, !runtime.isInitialized());

    result = runtime.initialize({.device = device_});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, runtime.isInitialized());

    result = runtime.initialize({.device = device_});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::InvalidInput);

    auto capacity = runtime.reserveSwapchains(2U);
    R2D_TEST_CHECK_EQ(context_, capacity.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, runtime.swapchainCapacity() >= 2U);
    capacity = runtime.reserveSwapchainImages(6U);
    R2D_TEST_CHECK_EQ(context_, capacity.code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, runtime.swapchainImageCapacity() >= 6U);

    R2D_TEST_CHECK_EQ(context_, runtime.swapchainCount(), 0U);
    R2D_TEST_CHECK_EQ(context_, runtime.swapchainImageCount(), 0U);
    R2D_TEST_CHECK_EQ(context_, runtime.lastVulkanResult(), VK_SUCCESS);
}

void testInvalidCreateAndAdoptDoNotTouchVulkan(R2DT::TestContext& context_, VkDevice device_)
{
    Runtime runtime{};
    auto result = runtime.initialize({.device = device_});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);
    auto capacity = runtime.reserveSwapchains(1U);
    R2D_TEST_CHECK_EQ(context_, capacity.code, R2D::NativeStatusCode::Ok);
    capacity = runtime.reserveSwapchainImages(3U);
    R2D_TEST_CHECK_EQ(context_, capacity.code, R2D::NativeStatusCode::Ok);

    SwapchainState state{};
    std::array<SwapchainImageRef, 3U> images{};
    result = runtime.createSwapchain(
        {
            .surface = VK_NULL_HANDLE,
            .old_swapchain = VK_NULL_HANDLE,
            .queue_family_indices = nullptr,
            .queue_family_index_count = 0U,
            .min_image_count = 2U,
            .width = 640U,
            .height = 480U,
            .format = static_cast<R2D::U32>(VK_FORMAT_B8G8R8A8_UNORM),
            .color_space = static_cast<R2D::U32>(VK_COLOR_SPACE_SRGB_NONLINEAR_KHR),
            .image_usage = static_cast<R2D::U32>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
            .sharing_mode = static_cast<R2D::U32>(VK_SHARING_MODE_EXCLUSIVE),
            .pre_transform = static_cast<R2D::U32>(VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR),
            .composite_alpha = static_cast<R2D::U32>(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR),
            .present_mode = static_cast<R2D::U32>(VK_PRESENT_MODE_FIFO_KHR),
            .clipped = 1U,
            .image_array_layers = 1U,
            .image_view_type = static_cast<R2D::U32>(VK_IMAGE_VIEW_TYPE_2D),
            .image_aspect_flags = static_cast<R2D::U32>(VK_IMAGE_ASPECT_COLOR_BIT),
            .flags = 0U,
        },
        state,
        images);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::InvalidInput);
    R2D_TEST_CHECK_EQ(context_, runtime.swapchainCount(), 0U);
    R2D_TEST_CHECK_EQ(context_, runtime.swapchainImageCount(), 0U);
    R2D_TEST_CHECK_EQ(context_, state.swapchain_id, 0U);

    result = runtime.adoptSwapchain(
        {
            .surface = VK_NULL_HANDLE,
            .swapchain = VK_NULL_HANDLE,
            .width = 640U,
            .height = 480U,
            .format = static_cast<R2D::U32>(VK_FORMAT_B8G8R8A8_UNORM),
            .image_view_type = static_cast<R2D::U32>(VK_IMAGE_VIEW_TYPE_2D),
            .image_aspect_flags = static_cast<R2D::U32>(VK_IMAGE_ASPECT_COLOR_BIT),
            .flags = 0U,
        },
        state,
        images);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::InvalidInput);
    R2D_TEST_CHECK_EQ(context_, runtime.swapchainCount(), 0U);
}

void testStaleResolution(R2DT::TestContext& context_, VkDevice device_)
{
    Runtime runtime{};
    auto result = runtime.initialize({.device = device_});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::Ok);

    const SwapchainState stale_state{
        .handle = 0x1111U,
        .swapchain_id = 2U,
        .image_first = 0U,
        .image_count = 2U,
        .width = 640U,
        .height = 480U,
        .format = static_cast<R2D::U32>(VK_FORMAT_B8G8R8A8_UNORM),
        .generation = 1U,
        .flags = 0U,
    };
    SwapchainState resolved_state{};
    result = runtime.resolveSwapchainState(stale_state, resolved_state);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::StaleReference);

    VkSwapchainKHR native_swapchain = VK_NULL_HANDLE;
    result = runtime.resolveNativeSwapchain(stale_state, native_swapchain);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::StaleReference);
    R2D_TEST_CHECK_EQ(context_, native_swapchain, VK_NULL_HANDLE);

    const SwapchainImageRef stale_image{
        .image_handle = 0x2222U,
        .image_view_handle = 0x3333U,
        .swapchain_id = 2U,
        .image_index = 1U,
        .width = 640U,
        .height = 480U,
        .format = static_cast<R2D::U32>(VK_FORMAT_B8G8R8A8_UNORM),
        .generation = 1U,
        .flags = 0U,
    };
    SwapchainImageRef resolved_image{};
    result = runtime.resolveSwapchainImageRef(stale_image, resolved_image);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::StaleReference);

    VkImage native_image = VK_NULL_HANDLE;
    VkImageView native_image_view = VK_NULL_HANDLE;
    result = runtime.resolveNativeSwapchainImage(stale_image, native_image, native_image_view);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::StaleReference);
    R2D_TEST_CHECK_EQ(context_, native_image, VK_NULL_HANDLE);
    R2D_TEST_CHECK_EQ(context_, native_image_view, VK_NULL_HANDLE);

    result = runtime.releaseSwapchainState(stale_state);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::StaleReference);
}

void testUnsupportedDomain(R2DT::TestContext& context_, VkDevice device_)
{
    R2D::VulkanSwapchainRuntime<int, Dim> runtime{};
    const auto result = runtime.initialize({.device = device_});
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    Runtime invalid_runtime{};
    const auto invalid_result = invalid_runtime.initialize({.device = VK_NULL_HANDLE});
    R2D_TEST_CHECK_EQ(context, invalid_result.code, R2D::NativeStatusCode::InvalidInput);

    Render2DTest::VulkanSmokeContext vulkan_context{};
    if (!Render2DTest::createVulkanSmokeContext(vulkan_context)) {
        return context.result();
    }

    testInvalidAndReserve(context, vulkan_context.device);
    testInvalidCreateAndAdoptDoNotTouchVulkan(context, vulkan_context.device);
    testStaleResolution(context, vulkan_context.device);
    testUnsupportedDomain(context, vulkan_context.device);
    Render2DTest::destroyVulkanSmokeContext(vulkan_context);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("vulkan_swapchain_runtime_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("vulkan_swapchain_runtime_test unknown exception\n", stderr);
        return 1;
    }
}
