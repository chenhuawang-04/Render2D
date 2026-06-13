#include "support/SpriteShaders.hpp"
#include "support/TestHarness.hpp"

#include <Render2D/Render2D.hpp>

#include <array>
#include <span>

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;
namespace R2DShaders = Render2DTest;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using SpriteMaterialBinding = R2D::SpriteMaterialBinding<Provider, Dim>;
using SpriteTextureBinding = R2D::SpriteTextureBinding<Provider, Dim>;
using SpriteDrawPacket = R2D::SpriteDrawPacket<Provider, Dim>;
using Batch = R2D::BatchSystem<Provider, Dim>;
using PacketBuild = R2D::SpriteDrawPacketBuildSystem<Provider, Dim>;

// A draw built for the bindless path: sort key is texture-agnostic, so draws
// differing only by texture share a key and become adjacent (hence mergeable).
[[nodiscard]] constexpr DrawCommand makeBindlessDraw(
    R2D::U32 source_index_,
    R2D::U32 layer_,
    R2D::U32 material_id_,
    R2D::U32 material_generation_,
    R2D::U32 texture_id_,
    R2D::U32 flags_) noexcept
{
    return {
        .source_index = source_index_,
        .material_id = material_id_,
        .material_generation = material_generation_,
        .texture_id = texture_id_,
        .texture_generation = 1U,
        .vertex_first = 0U,
        .vertex_count = 4U,
        .index_first = 0U,
        .index_count = 6U,
        .instance_first = source_index_,
        .instance_count = 1U,
        .sort_key = R2D::makeBindlessDrawSortKey(layer_, material_id_, flags_),
        .layer = layer_,
        .flags = flags_,
    };
}

// The bindless sort key is the packed key with the texture field cleared.
static_assert(R2D::makeBindlessDrawSortKey(1U, 2U, 4U) == R2D::makeDrawSortKey(1U, 2U, 0U, 4U));
static_assert(R2D::makeBindlessDrawSortKey(3U, 5U, 1U) != R2D::makeDrawSortKey(3U, 5U, 7U, 1U));

// The Stage 20C bindless shaders are embedded as well-formed SPIR-V modules.
static_assert(R2DShaders::kSpriteBindlessVertSpv[0U] == 0x07230203U);
static_assert(R2DShaders::kSpriteBindlessFragSpv[0U] == 0x07230203U);
static_assert(R2DShaders::kSpriteBindlessVertSpv.size() > 16U);
static_assert(R2DShaders::kSpriteBindlessFragSpv.size() > 16U);

int main()
{
    R2DT::TestContext context{};

    // 1. The bindless batch key drops texture identity but still compares material
    //    id/generation, layer and flags, so a stale generation can never merge.
    const DrawCommand base = makeBindlessDraw(0U, 0U, 1U, 1U, 5U, 0U);
    const DrawCommand other_texture = makeBindlessDraw(1U, 0U, 1U, 1U, 9U, 0U);
    R2D_TEST_CHECK(context, R2D::drawCommandsHaveEqualBindlessBatchKey(base, other_texture));
    // The non-bindless key still splits on texture, proving the paths differ.
    R2D_TEST_CHECK(context, !R2D::drawCommandsHaveEqualBatchKey(base, other_texture));

    const DrawCommand newer_generation = makeBindlessDraw(2U, 0U, 1U, 2U, 5U, 0U);
    const DrawCommand other_material = makeBindlessDraw(3U, 0U, 2U, 1U, 5U, 0U);
    const DrawCommand other_flags = makeBindlessDraw(4U, 0U, 1U, 1U, 5U, 1U);
    R2D_TEST_CHECK(context, !R2D::drawCommandsHaveEqualBindlessBatchKey(base, newer_generation));
    R2D_TEST_CHECK(context, !R2D::drawCommandsHaveEqualBindlessBatchKey(base, other_material));
    R2D_TEST_CHECK(context, !R2D::drawCommandsHaveEqualBindlessBatchKey(base, other_flags));

    // 2. Cross-texture merge: three draws differing only by texture collapse into a
    //    single bindless batch, while the non-bindless path keeps them split.
    const std::array<DrawCommand, 3U> cross_texture{{
        makeBindlessDraw(0U, 0U, 1U, 1U, 1U, 0U),
        makeBindlessDraw(1U, 0U, 1U, 1U, 2U, 0U),
        makeBindlessDraw(2U, 0U, 1U, 1U, 3U, 0U),
    }};

    std::array<BatchCommand, 3U> batches{};
    auto result = Batch::runBindless(cross_texture, batches);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, 1U);
    R2D_TEST_CHECK_EQ(context, batches[0U].draw_first, 0U);
    R2D_TEST_CHECK_EQ(context, batches[0U].draw_count, 3U);

    std::array<BatchCommand, 3U> fallback_batches{};
    result = Batch::run(cross_texture, fallback_batches);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, 3U);

    // 3. Stale guard: a differing material generation in the run is never merged
    //    away, so the bindless batches break at the generation boundary.
    const std::array<DrawCommand, 3U> mixed_generation{{
        makeBindlessDraw(0U, 0U, 1U, 1U, 1U, 0U),
        makeBindlessDraw(1U, 0U, 1U, 1U, 2U, 0U),
        makeBindlessDraw(2U, 0U, 1U, 2U, 3U, 0U),
    }};
    result = Batch::runBindless(mixed_generation, batches);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, 2U);
    R2D_TEST_CHECK_EQ(context, batches[0U].draw_count, 2U);
    R2D_TEST_CHECK_EQ(context, batches[1U].draw_count, 1U);

    // 4. Bindless packet build: the merged batch becomes one packet spanning every
    //    instance across textures, bound to the single bindless descriptor set.
    std::array<BatchCommand, 1U> packet_batches{};
    result = Batch::runBindless(cross_texture, packet_batches);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_REQUIRE(context, result.write_count == 1U);

    const SpriteMaterialBinding material{
        .material_id = 1U,
        .material_generation = 1U,
        .pipeline_id = 7U,
        .pipeline_generation = 1U,
        .flags = 0U,
    };
    const std::array<SpriteMaterialBinding, 1U> material_bindings{{material}};
    const SpriteTextureBinding bindless_binding{
        .texture_id = 0U,
        .texture_generation = 0U,
        .descriptor_id = 3U,
        .descriptor_generation = 1U,
        .descriptor_first = 0U,
        .descriptor_count = 1U,
        .flags = 0U,
    };

    std::array<SpriteDrawPacket, 1U> packets{};
    result = PacketBuild::runBindless(
        packet_batches,
        cross_texture,
        material_bindings,
        bindless_binding,
        packets);
    R2D_TEST_REQUIRE(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, 1U);
    R2D_TEST_CHECK_EQ(context, packets[0U].draw_count, 3U);
    R2D_TEST_CHECK_EQ(context, packets[0U].instance_first, 0U);
    R2D_TEST_CHECK_EQ(context, packets[0U].instance_count, 3U);
    R2D_TEST_CHECK_EQ(context, packets[0U].pipeline_id, 7U);
    R2D_TEST_CHECK_EQ(context, packets[0U].pipeline_generation, 1U);
    R2D_TEST_CHECK_EQ(context, packets[0U].descriptor_id, 3U);
    R2D_TEST_CHECK_EQ(context, packets[0U].descriptor_generation, 1U);
    R2D_TEST_CHECK_EQ(context, packets[0U].descriptor_count, 1U);

    // 5. Bindless packet build rejects a missing material, a stale material
    //    pipeline, and a stale bindless descriptor.
    result = PacketBuild::runBindless(
        packet_batches,
        cross_texture,
        std::span<const SpriteMaterialBinding>{},
        bindless_binding,
        packets);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InvalidInput);

    const SpriteMaterialBinding stale_material{
        .material_id = 1U,
        .material_generation = 1U,
        .pipeline_id = 7U,
        .pipeline_generation = 0U,
        .flags = 0U,
    };
    const std::array<SpriteMaterialBinding, 1U> stale_material_bindings{{stale_material}};
    result = PacketBuild::runBindless(
        packet_batches,
        cross_texture,
        stale_material_bindings,
        bindless_binding,
        packets);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InvalidInput);

    const SpriteTextureBinding stale_binding{
        .texture_id = 0U,
        .texture_generation = 0U,
        .descriptor_id = 3U,
        .descriptor_generation = 0U,
        .descriptor_first = 0U,
        .descriptor_count = 1U,
        .flags = 0U,
    };
    result = PacketBuild::runBindless(
        packet_batches,
        cross_texture,
        material_bindings,
        stale_binding,
        packets);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InvalidInput);

    // 6. Empty input is accepted with no output on both bindless entry points.
    result = Batch::runBindless(std::span<const DrawCommand>{}, batches);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, 0U);

    result = PacketBuild::runBindless(
        std::span<const BatchCommand>{},
        cross_texture,
        material_bindings,
        bindless_binding,
        packets);
    R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context, result.write_count, 0U);

    return context.result();
}
