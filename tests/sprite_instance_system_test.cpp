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
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using SpriteDrawPacket = R2D::SpriteDrawPacket<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;

static_assert(R2D::StrictPodComponent<SpriteVertex>);
static_assert(R2D::StrictPodComponent<SpriteInstance>);
static_assert(R2D::StrictPodComponent<SpriteDrawPacket>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, SpriteVertex>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, SpriteInstance>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, SpriteDrawPacket>);

constexpr R2D::Mat3 makeAffine(
    float m00_,
    float m01_,
    float m02_,
    float m10_,
    float m11_,
    float m12_) noexcept
{
    return {
        .m00 = m00_,
        .m01 = m01_,
        .m02 = m02_,
        .m10 = m10_,
        .m11 = m11_,
        .m12 = m12_,
        .m20 = 0.0F,
        .m21 = 0.0F,
        .m22 = 1.0F,
    };
}

constexpr DrawCommand makeDraw(
    R2D::U32 source_index_,
    R2D::U32 instance_first_,
    R2D::U32 material_id_,
    R2D::U32 texture_id_,
    R2D::U32 layer_,
    R2D::U32 flags_) noexcept
{
    return {
        .source_index = source_index_,
        .material_id = material_id_,
        .texture_id = texture_id_,
        .vertex_first = 0U,
        .vertex_count = 4U,
        .index_first = 0U,
        .index_count = 6U,
        .instance_first = instance_first_,
        .instance_count = 1U,
        .sort_key = R2D::makeDrawSortKey(layer_, material_id_, texture_id_, flags_),
        .layer = layer_,
        .flags = flags_,
    };
}

void testBuildSpriteInstances(R2DT::TestContext& context_)
{
    constexpr std::array<WorldTransform, 3U> kWorldTransforms{{
        {.source_id = 10U, .affine = makeAffine(1.0F, 0.0F, 2.0F, 0.0F, 1.0F, 3.0F)},
        {.source_id = 11U, .affine = makeAffine(2.0F, 0.0F, 4.0F, 0.0F, 2.0F, 5.0F)},
        {.source_id = 12U, .affine = makeAffine(3.0F, 1.0F, 6.0F, 1.0F, 3.0F, 7.0F)},
    }};
    constexpr std::array<Sprite, 3U> kSprites{{
        {
            .source_id = 10U,
            .texture_id = 100U,
            .material_id = 200U,
            .color_rgba8 = 0x11223344U,
            .layer = 1U,
            .flags = 0U,
        },
        {
            .source_id = 11U,
            .texture_id = 101U,
            .material_id = 201U,
            .color_rgba8 = 0x55667788U,
            .layer = 2U,
            .flags = 1U,
        },
        {
            .source_id = 12U,
            .texture_id = 102U,
            .material_id = 202U,
            .color_rgba8 = 0x99AABBCCU,
            .layer = 3U,
            .flags = 2U,
        },
    }};
    constexpr std::array<DrawCommand, 2U> kDraws{{
        makeDraw(2U, 1U, 202U, 102U, 3U, 2U),
        makeDraw(0U, 0U, 200U, 100U, 1U, 0U),
    }};
    std::array<SpriteInstance, 2U> instances{};

    const auto result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::run(
        kDraws,
        kWorldTransforms,
        kSprites,
        instances);

    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 2U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 2U);

    R2D_TEST_CHECK_EQ(context_, instances[0U].source_index, 0U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].source_id, 10U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].texture_id, 100U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].material_id, 200U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].color_rgba8, 0x11223344U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].transform_m02, 2.0F);
    R2D_TEST_CHECK_EQ(context_, instances[0U].transform_m12, 3.0F);
    R2D_TEST_CHECK_EQ(context_, instances[0U].uv_min_x, 0.0F);
    R2D_TEST_CHECK_EQ(context_, instances[0U].uv_max_y, 1.0F);

    R2D_TEST_CHECK_EQ(context_, instances[1U].source_index, 2U);
    R2D_TEST_CHECK_EQ(context_, instances[1U].source_id, 12U);
    R2D_TEST_CHECK_EQ(context_, instances[1U].texture_id, 102U);
    R2D_TEST_CHECK_EQ(context_, instances[1U].material_id, 202U);
    R2D_TEST_CHECK_EQ(context_, instances[1U].transform_m00, 3.0F);
    R2D_TEST_CHECK_EQ(context_, instances[1U].transform_m01, 1.0F);
    R2D_TEST_CHECK_EQ(context_, instances[1U].transform_m12, 7.0F);
    R2D_TEST_CHECK_EQ(context_, instances[1U].sort_key, kDraws[0U].sort_key);
    R2D_TEST_CHECK_EQ(context_, instances[1U].flags, 2U);
}

void testCapacityAndInvalidInput(R2DT::TestContext& context_)
{
    constexpr std::array<WorldTransform, 1U> kWorldTransforms{{
        {.source_id = 1U, .affine = makeAffine(1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F)},
    }};
    constexpr std::array<Sprite, 1U> kSprites{{
        {
            .source_id = 1U,
            .texture_id = 2U,
            .material_id = 3U,
            .color_rgba8 = 4U,
            .layer = 5U,
            .flags = 6U,
        },
    }};

    constexpr std::array<DrawCommand, 1U> kSparseDraws{{
        makeDraw(0U, 1U, 3U, 2U, 5U, 6U),
    }};
    std::array<SpriteInstance, 1U> instances{};
    auto result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::run(
        kSparseDraws,
        kWorldTransforms,
        kSprites,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InsufficientCapacity);
    R2D_TEST_CHECK_EQ(context_, result.read_count, 0U);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 0U);

    std::array<DrawCommand, 1U> invalid_draws{{
        makeDraw(1U, 0U, 3U, 2U, 5U, 6U),
    }};
    result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::run(
        invalid_draws,
        kWorldTransforms,
        kSprites,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);

    invalid_draws[0U] = makeDraw(0U, 0U, 3U, 2U, 5U, 6U);
    invalid_draws[0U].instance_count = 2U;
    result = R2D::SpriteInstanceBuildSystem<Provider, Dim>::run(
        invalid_draws,
        kWorldTransforms,
        kSprites,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::InvalidInput);
}

void testUnsupportedDomain(R2DT::TestContext& context_)
{
    std::array<R2D::DrawCommand<int, Dim>, 1U> draws{};
    std::array<R2D::WorldTransform<int, Dim>, 1U> world_transforms{};
    std::array<R2D::Sprite<int, Dim>, 1U> sprites{};
    std::array<R2D::SpriteInstance<int, Dim>, 1U> instances{};
    const auto result = R2D::SpriteInstanceBuildSystem<int, Dim>::run(
        draws,
        world_transforms,
        sprites,
        instances);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::SystemStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testBuildSpriteInstances(context);
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
        std::fputs("sprite_instance_system_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("sprite_instance_system_test unknown exception\n", stderr);
        return 1;
    }
}
