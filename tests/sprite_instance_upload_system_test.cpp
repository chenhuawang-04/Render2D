#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"

#include <array>
#include <cstdio>
#include <exception>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpriteInstanceUploadCommand = R2D::SpriteInstanceUploadCommand<Provider, Dim>;
using UploadCommand = R2D::UploadCommand<Provider, Dim>;

static_assert(R2D::StrictPodComponent<SpriteInstanceUploadCommand>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, SpriteInstanceUploadCommand>);

constexpr SpriteInstance makeInstance(R2D::U32 source_index_) noexcept
{
    return {
        .transform_m00 = 1.0F,
        .transform_m01 = 0.0F,
        .transform_m02 = 2.0F,
        .transform_m10 = 0.0F,
        .transform_m11 = 1.0F,
        .transform_m12 = 3.0F,
        .uv_min_x = 0.0F,
        .uv_min_y = 0.0F,
        .uv_max_x = 1.0F,
        .uv_max_y = 1.0F,
        .source_index = source_index_,
        .source_id = source_index_ + 10U,
        .texture_id = 20U,
        .material_id = 30U,
        .color_rgba8 = 0xFFFFFFFFU,
        .sort_key = 40U,
        .layer = 1U,
        .flags = 0U,
    };
}

void testBuildUploadCommands(R2DT::TestContext& context_)
{
    constexpr std::array<SpriteInstance, 4U> kInstances{{
        makeInstance(0U),
        makeInstance(1U),
        makeInstance(2U),
        makeInstance(3U),
    }};
    constexpr std::array<SpriteInstanceUploadCommand, 2U> kSpriteUploads{{
        {
            .instance_first = 1U,
            .instance_count = 2U,
            .destination_buffer_id = 7U,
            .destination_generation = 3U,
            .destination_offset = 256U,
            .frame_index = 5U,
            .flags = 9U,
        },
        {
            .instance_first = 3U,
            .instance_count = 1U,
            .destination_buffer_id = 7U,
            .destination_generation = 3U,
            .destination_offset = 256U + (2U * sizeof(SpriteInstance)),
            .frame_index = 5U,
            .flags = 9U,
        },
    }};
    std::array<UploadCommand, 2U> uploads{};

    const auto result = R2D::SpriteInstanceUploadSystem<Provider, Dim>::run(
        kSpriteUploads,
        kInstances,
        uploads);

    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 2U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 2U);

    R2D_TEST_CHECK_EQ(context_, uploads[0U].resource_id, 7U);
    R2D_TEST_CHECK_EQ(context_, uploads[0U].source_offset, sizeof(SpriteInstance));
    R2D_TEST_CHECK_EQ(context_, uploads[0U].destination_offset, 256U);
    R2D_TEST_CHECK_EQ(context_, uploads[0U].byte_count, 2U * sizeof(SpriteInstance));
    R2D_TEST_CHECK_EQ(context_, uploads[0U].upload_kind, R2D::kUploadKindSpriteInstance);
    R2D_TEST_CHECK_EQ(context_, uploads[0U].flags, 9U);

    R2D_TEST_CHECK_EQ(context_, uploads[1U].source_offset, 3U * sizeof(SpriteInstance));
    R2D_TEST_CHECK_EQ(context_, uploads[1U].byte_count, sizeof(SpriteInstance));
}

void testCapacityAndInvalidInput(R2DT::TestContext& context_)
{
    constexpr std::array<SpriteInstance, 1U> kInstances{{makeInstance(0U)}};
    constexpr std::array<SpriteInstanceUploadCommand, 2U> kTwoUploads{{
        {
            .instance_first = 0U,
            .instance_count = 1U,
            .destination_buffer_id = 1U,
            .destination_generation = 1U,
            .destination_offset = 0U,
            .frame_index = 0U,
            .flags = 0U,
        },
        {
            .instance_first = 0U,
            .instance_count = 1U,
            .destination_buffer_id = 1U,
            .destination_generation = 1U,
            .destination_offset = sizeof(SpriteInstance),
            .frame_index = 0U,
            .flags = 0U,
        },
    }};
    std::array<UploadCommand, 1U> short_uploads{};
    auto result = R2D::SpriteInstanceUploadSystem<Provider, Dim>::run(
        kTwoUploads,
        kInstances,
        short_uploads);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 1U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 1U);

    std::array<SpriteInstanceUploadCommand, 1U> invalid_uploads{{
        {
            .instance_first = 0U,
            .instance_count = 0U,
            .destination_buffer_id = 1U,
            .destination_generation = 1U,
            .destination_offset = 0U,
            .frame_index = 0U,
            .flags = 0U,
        },
    }};
    std::array<UploadCommand, 1U> uploads{};
    result = R2D::SpriteInstanceUploadSystem<Provider, Dim>::run(
        invalid_uploads,
        kInstances,
        uploads);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    invalid_uploads[0U].instance_count = 2U;
    result = R2D::SpriteInstanceUploadSystem<Provider, Dim>::run(
        invalid_uploads,
        kInstances,
        uploads);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    invalid_uploads[0U].instance_count = 1U;
    invalid_uploads[0U].destination_generation = 0U;
    result = R2D::SpriteInstanceUploadSystem<Provider, Dim>::run(
        invalid_uploads,
        kInstances,
        uploads);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);
}

void testUnsupportedDomain(R2DT::TestContext& context_)
{
    std::array<R2D::SpriteInstanceUploadCommand<int, Dim>, 1U> sprite_uploads{};
    std::array<R2D::SpriteInstance<int, Dim>, 1U> instances{};
    std::array<R2D::UploadCommand<int, Dim>, 1U> uploads{};
    const auto result = R2D::SpriteInstanceUploadSystem<int, Dim>::run(
        sprite_uploads,
        instances,
        uploads);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testBuildUploadCommands(context);
    testCapacityAndInvalidInput(context);
    testUnsupportedDomain(context);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("sprite_instance_upload_system_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("sprite_instance_upload_system_test unknown exception\n", stderr);
        return 1;
    }
}
