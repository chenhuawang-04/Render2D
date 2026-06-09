#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <span>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using AcquiredImage = R2D::AcquiredImage<Provider, Dim>;
using PresentCommand = R2D::PresentCommand<Provider, Dim>;

static_assert(R2D::StrictPodComponent<AcquiredImage>);
static_assert(R2D::StrictPodComponent<PresentCommand>);

void testBuildPresentCommands(R2DT::TestContext& context_)
{
    constexpr std::array<AcquiredImage, 2U> kAcquired{
        AcquiredImage{
            .swapchain_id = 3U,
            .image_index = 1U,
            .frame_index = 9U,
            .sync_id = 5U,
            .sync_generation = 7U,
            .generation = 11U,
            .flags = R2D::kVulkanAcquireImageSuboptimal,
        },
        AcquiredImage{
            .swapchain_id = 4U,
            .image_index = 0U,
            .frame_index = 10U,
            .sync_id = 6U,
            .sync_generation = 8U,
            .generation = 12U,
            .flags = 0U,
        },
    };
    std::array<PresentCommand, 2U> presents{};

    const auto result = R2D::PresentCommandBuildSystem<Provider, Dim>::run(kAcquired, presents);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 2U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 2U);

    R2D_TEST_CHECK_EQ(context_, presents[0U].swapchain_id, kAcquired[0U].swapchain_id);
    R2D_TEST_CHECK_EQ(context_, presents[0U].image_index, kAcquired[0U].image_index);
    R2D_TEST_CHECK_EQ(context_, presents[0U].wait_sync_id, kAcquired[0U].sync_id);
    R2D_TEST_CHECK_EQ(context_, presents[0U].wait_sync_generation, kAcquired[0U].sync_generation);
    R2D_TEST_CHECK_EQ(context_, presents[0U].frame_index, kAcquired[0U].frame_index);
    R2D_TEST_CHECK_EQ(context_, presents[0U].generation, kAcquired[0U].generation);
    R2D_TEST_CHECK_EQ(context_, presents[0U].flags, kAcquired[0U].flags);

    R2D_TEST_CHECK_EQ(context_, presents[1U].swapchain_id, kAcquired[1U].swapchain_id);
    R2D_TEST_CHECK_EQ(context_, presents[1U].wait_sync_generation, kAcquired[1U].sync_generation);
}

void testEmptyAndCapacity(R2DT::TestContext& context_)
{
    std::array<PresentCommand, 1U> presents{};
    auto result = R2D::PresentCommandBuildSystem<Provider, Dim>::run(
        std::span<const AcquiredImage>{},
        presents);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 0U);

    constexpr std::array<AcquiredImage, 2U> kAcquired{
        AcquiredImage{
            .swapchain_id = 1U,
            .image_index = 0U,
            .frame_index = 0U,
            .sync_id = 2U,
            .sync_generation = 3U,
            .generation = 4U,
            .flags = 0U,
        },
        AcquiredImage{
            .swapchain_id = 1U,
            .image_index = 1U,
            .frame_index = 0U,
            .sync_id = 2U,
            .sync_generation = 3U,
            .generation = 4U,
            .flags = 0U,
        },
    };

    result = R2D::PresentCommandBuildSystem<Provider, Dim>::run(kAcquired, presents);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 2U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 1U);
}

void testInvalidAcquiredImageDoesNotMutateOutput(R2DT::TestContext& context_)
{
    constexpr std::array<AcquiredImage, 1U> kInvalidAcquired{
        AcquiredImage{
            .swapchain_id = 1U,
            .image_index = 0U,
            .frame_index = 2U,
            .sync_id = 3U,
            .sync_generation = 0U,
            .generation = 4U,
            .flags = 0U,
        },
    };
    std::array<PresentCommand, 1U> presents{
        PresentCommand{
            .swapchain_id = 99U,
            .image_index = 98U,
            .wait_sync_id = 97U,
            .wait_sync_generation = 96U,
            .frame_index = 95U,
            .generation = 94U,
            .flags = 93U,
        },
    };

    const auto result = R2D::PresentCommandBuildSystem<Provider, Dim>::run(kInvalidAcquired, presents);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 0U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 0U);
    R2D_TEST_CHECK_EQ(context_, presents[0U].swapchain_id, 99U);
    R2D_TEST_CHECK_EQ(context_, presents[0U].wait_sync_generation, 96U);
}

void testUnsupportedDomain(R2DT::TestContext& context_)
{
    std::array<R2D::AcquiredImage<int, Dim>, 1U> acquired{};
    std::array<R2D::PresentCommand<int, Dim>, 1U> presents{};
    const auto result = R2D::PresentCommandBuildSystem<int, Dim>::run(acquired, presents);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testBuildPresentCommands(context);
    testEmptyAndCapacity(context);
    testInvalidAcquiredImageDoesNotMutateOutput(context);
    testUnsupportedDomain(context);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("present_command_system_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("present_command_system_test unknown exception\n", stderr);
        return 1;
    }
}
