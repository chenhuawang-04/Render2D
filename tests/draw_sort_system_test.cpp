#include "support/TestHarness.hpp"

#include <Render2D/Render2D.hpp>

#include <array>
#include <span>

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using SortedItem = R2D::SortedItem<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;

[[nodiscard]] constexpr DrawCommand makeDraw(
    R2D::U32 source_index_,
    R2D::U32 layer_,
    R2D::U32 material_id_,
    R2D::U32 texture_id_,
    R2D::U32 flags_) noexcept
{
    return {
        .source_index = source_index_,
        .material_id = material_id_,
        .material_generation = 0U,
        .texture_id = texture_id_,
        .texture_generation = 0U,
        .vertex_first = 0U,
        .vertex_count = 4U,
        .index_first = 0U,
        .index_count = 6U,
        .instance_first = source_index_,
        .instance_count = 1U,
        .sort_key = R2D::makeDrawSortKey(layer_, material_id_, texture_id_, flags_),
        .layer = layer_,
        .flags = flags_,
    };
}

int main()
{
    R2DT::TestContext context{};

    static_assert(R2D::makeDrawSortKey(1U, 2U, 3U, 4U) ==
        ((1U << R2D::kDrawSortKeyLayerShift) |
            (2U << R2D::kDrawSortKeyMaterialShift) |
            (3U << R2D::kDrawSortKeyTextureShift) |
            4U));

    const std::array<DrawCommand, 6U> draws{{
        makeDraw(0U, 2U, 1U, 1U, 0U),
        makeDraw(1U, 0U, 1U, 2U, 0U),
        makeDraw(2U, 0U, 1U, 1U, 0U),
        makeDraw(3U, 0U, 1U, 1U, 0U),
        makeDraw(4U, 1U, 0U, 1U, 0U),
        makeDraw(5U, 0U, 2U, 0U, 0U),
    }};

    std::array<DrawCommand, draws.size()> sorted_draws{};
    std::array<SortedItem, draws.size()> scratch_a{};
    std::array<SortedItem, draws.size()> scratch_b{};
    auto result = R2D::DrawSortSystem<Provider, Dim>::run(
        draws,
        sorted_draws,
        scratch_a,
        scratch_b);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, draws.size());

    for (R2D::Usize index = 1U; index < sorted_draws.size(); ++index) {
        R2D_TEST_CHECK(context, sorted_draws[index - 1U].sort_key <= sorted_draws[index].sort_key);
    }
    R2D_TEST_CHECK_EQ(context, sorted_draws[0U].source_index, 2U);
    R2D_TEST_CHECK_EQ(context, sorted_draws[1U].source_index, 3U);

    std::array<BatchCommand, draws.size()> batches{};
    result = R2D::BatchSystem<Provider, Dim>::run(sorted_draws, batches);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, 5U);
    R2D_TEST_CHECK_EQ(context, batches[0U].draw_first, 0U);
    R2D_TEST_CHECK_EQ(context, batches[0U].draw_count, 2U);

    std::array<DrawCommand, 2U> collision_draws{{
        makeDraw(0U, 0U, 1U, 1U, 0U),
        makeDraw(1U, 0U, 2U, 1U, 0U),
    }};
    collision_draws[1U].sort_key = collision_draws[0U].sort_key;
    result = R2D::BatchSystem<Provider, Dim>::run(collision_draws, batches);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, 2U);

    std::array<DrawCommand, 1U> short_output{};
    result = R2D::DrawSortSystem<Provider, Dim>::run(
        draws,
        short_output,
        scratch_a,
        scratch_b);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InsufficientCapacity);

    return context.result();
}
