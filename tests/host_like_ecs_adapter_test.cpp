// Stage 23A (host-engine merge readiness): prove Render2D systems are
// storage-agnostic. The SAME unchanged production CPU chain (SpatialCull ->
// CommandBuild -> SpriteInstanceBuild -> Batch) is driven once from the canonical
// per-type ComponentStreamStorage (the temporary test ECS named in
// ProjectMergeTODO #1) and once from HostLikeEcs's archetype-shaped SoA columns
// + frame arena. Both are fed byte-identical input, and every derived stream
// (WorldTransform / VisibleItem / DrawCommand / SpriteInstance / BatchCommand) is
// asserted byte-identical between the two runs.
//
// This is the in-repo proof that a host engine can replace the test ECS with its
// own storage shape and feed the systems through std::span alone, with zero
// system changes -- and a worked template for that merge step. The strongest
// guarantee is at compile time: HostLikeEcs is a brand-new store type the systems
// have never seen, so it compiling against their spans demonstrates the boundary
// is span-only. The only variable between the two runs is the INPUT store shape;
// outputs go into the same HostFrameArena, which also exercises the output span
// boundary against a non-ComponentStreamStorage container.
#include <Render2D/Render2D.hpp>

#include "support/HostLikeEcs.hpp"
#include "support/TemporaryEcsStorage.hpp"
#include "support/TestHarness.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <span>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Transform = R2D::Transform<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using Camera = R2D::Camera<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpatialCull = R2D::SpatialCullSystem<Provider, Dim>;
using CommandBuild = R2D::CommandBuildSystem<Provider, Dim>;
using SpriteInstanceBuild = R2D::SpriteInstanceBuildSystem<Provider, Dim>;
using Batch = R2D::BatchSystem<Provider, Dim>;
using Arena = R2DT::HostFrameArena<Provider, Dim>;

inline constexpr R2D::U32 kEntityCount = 129U;
inline constexpr R2D::U32 kVisibleMask = 0xFFFF'FFFFU;
inline constexpr R2D::U32 kHiddenMask = 0U;

inline constexpr Camera kCamera{
    .source_id = 0U,
    .position_x = 0.0F,
    .position_y = 0.0F,
    .rotation_radians = 0.0F,
    .viewport_width = 48.0F,
    .viewport_height = 48.0F,
    .near_z = 0.0F,
    .far_z = 1.0F,
    .layer_mask = kVisibleMask,
    .flags = 0U,
};

// Canonical scene built once into plain arrays, then loaded identically into both
// stores. A mix of rotated (sincos affine path), far-offscreen (culled by
// bounds), and layer-masked-off (culled by mask) entities, with a couple of
// distinct textures/materials so batching has adjacent draws to consider.
struct Scene {
    R2D::McVector<Transform> transforms;
    R2D::McVector<LocalBounds> local_bounds;
    R2D::McVector<VisibilityMask> visibility_masks;
    R2D::McVector<Sprite> sprites;
};

[[nodiscard]] Scene makeScene(R2D::U32 count_)
{
    Scene scene{};
    scene.transforms.resize(count_);
    scene.local_bounds.resize(count_);
    scene.visibility_masks.resize(count_);
    scene.sprites.resize(count_);

    for (R2D::U32 index = 0U; index < count_; ++index) {
        const bool rotated = (index % 3U) == 0U;
        const bool far_offscreen = (index % 4U) == 1U;
        const bool masked_off = (index % 7U) == 0U;
        const auto grid_x = static_cast<float>(index % 16U) - 8.0F;
        const auto grid_y = static_cast<float>((index / 16U) % 16U) - 8.0F;

        scene.transforms[index] = {
            .source_id = index,
            .position_x = far_offscreen ? 5000.0F + grid_x : grid_x,
            .position_y = far_offscreen ? 5000.0F + grid_y : grid_y,
            .rotation_radians = rotated ? (0.13F * static_cast<float>(index + 1U)) : 0.0F,
            .scale_x = 1.0F + static_cast<float>(index % 3U) * 0.25F,
            .scale_y = 1.0F + static_cast<float>(index % 2U) * 0.5F,
        };
        scene.local_bounds[index] = {
            .source_id = index,
            .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F),
        };
        scene.visibility_masks[index] = {.mask = masked_off ? kHiddenMask : kVisibleMask};
        scene.sprites[index] = {
            .source_id = index,
            .texture_id = 10U + ((index / 4U) % 3U),
            .texture_generation = 1U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = 20U + ((index / 4U) % 2U),
            .material_generation = 1U,
            .color_rgba8 = 0xAABBCC00U | (index & 0xFFU),
            .layer = 1U,
            .flags = 0U,
        };
    }
    return scene;
}

struct ChainCounts {
    R2D::SystemStatusCode code;
    R2D::U32 visible_count;
    R2D::U32 batch_count;
};

// Run the production CPU front-end + sprite chain, fed entirely through spans,
// writing every derived stream into the supplied frame arena. Identical code for
// both stores -- only where the input spans come from differs.
[[nodiscard]] ChainCounts runChain(
    const Camera& camera_,
    std::span<const Transform> transforms_,
    std::span<const LocalBounds> local_bounds_,
    std::span<const VisibilityMask> visibility_masks_,
    std::span<const Sprite> sprites_,
    Arena& arena_)
{
    arena_.resizeForEntities(transforms_.size());

    auto result = SpatialCull::run(
        camera_,
        transforms_,
        local_bounds_,
        visibility_masks_,
        arena_.worldTransforms(),
        arena_.visibleItems());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = 0U, .batch_count = 0U};
    }
    const R2D::U32 visible_count = result.write_count;

    result = CommandBuild::run(
        std::span<const VisibleItem>{arena_.visibleItems().data(), visible_count},
        sprites_,
        arena_.drawCommands());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }

    result = SpriteInstanceBuild::run(
        std::span<const DrawCommand>{arena_.drawCommands().data(), visible_count},
        arena_.worldTransforms(),
        sprites_,
        arena_.spriteInstances());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }

    result = Batch::run(
        std::span<const DrawCommand>{arena_.drawCommands().data(), visible_count},
        arena_.batchCommands());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }

    return {.code = R2D::SystemStatusCode::Ok, .visible_count = visible_count, .batch_count = result.write_count};
}

[[nodiscard]] bool bytesEqual(const void* left_, const void* right_, R2D::Usize byte_count_) noexcept
{
    return std::memcmp(left_, right_, byte_count_) == 0;
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};

    const Scene scene = makeScene(kEntityCount);

    // Reference store: the canonical per-type ComponentStreamStorage (the
    // "temporary test ECS" of ProjectMergeTODO #1).
    R2DT::ComponentStreamStorage<Provider, Dim, Transform> ref_transforms{};
    R2DT::ComponentStreamStorage<Provider, Dim, LocalBounds> ref_local_bounds{};
    R2DT::ComponentStreamStorage<Provider, Dim, VisibilityMask> ref_visibility_masks{};
    R2DT::ComponentStreamStorage<Provider, Dim, Sprite> ref_sprites{};
    for (R2D::U32 index = 0U; index < kEntityCount; ++index) {
        ref_transforms.push(scene.transforms[index]);
        ref_local_bounds.push(scene.local_bounds[index]);
        ref_visibility_masks.push(scene.visibility_masks[index]);
        ref_sprites.push(scene.sprites[index]);
    }

    // Host store: archetype-shaped SoA columns -- a structurally different shape.
    R2DT::HostEntityTable<Provider, Dim> host_table{};
    host_table.reserve(kEntityCount);
    for (R2D::U32 index = 0U; index < kEntityCount; ++index) {
        host_table.pushEntity(
            scene.transforms[index],
            scene.local_bounds[index],
            scene.visibility_masks[index],
            scene.sprites[index]);
    }

    R2D_TEST_CHECK_EQ(context, host_table.size(), static_cast<R2D::Usize>(kEntityCount));
    R2D_TEST_CHECK_EQ(context, ref_transforms.size(), static_cast<R2D::Usize>(kEntityCount));

    // The two stores differ only in SHAPE, not bytes: each input column is
    // byte-identical between them. This isolates "container differs" from "data
    // differs", so the output equality below can only be about the span boundary.
    R2D_TEST_CHECK(context, bytesEqual(
        host_table.transforms().data(), ref_transforms.data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(Transform)));
    R2D_TEST_CHECK(context, bytesEqual(
        host_table.localBounds().data(), ref_local_bounds.data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(LocalBounds)));
    R2D_TEST_CHECK(context, bytesEqual(
        host_table.visibilityMasks().data(), ref_visibility_masks.data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(VisibilityMask)));
    R2D_TEST_CHECK(context, bytesEqual(
        host_table.sprites().data(), ref_sprites.data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(Sprite)));

    // Run the SAME chain from each store into its own arena.
    Arena ref_arena{};
    const auto ref_counts = runChain(
        kCamera,
        std::span<const Transform>{ref_transforms.data(), ref_transforms.size()},
        std::span<const LocalBounds>{ref_local_bounds.data(), ref_local_bounds.size()},
        std::span<const VisibilityMask>{ref_visibility_masks.data(), ref_visibility_masks.size()},
        std::span<const Sprite>{ref_sprites.data(), ref_sprites.size()},
        ref_arena);

    Arena host_arena{};
    const auto host_counts = runChain(
        kCamera,
        host_table.transforms(),
        host_table.localBounds(),
        host_table.visibilityMasks(),
        host_table.sprites(),
        host_arena);

    R2D_TEST_CHECK_EQ(context, static_cast<int>(ref_counts.code), static_cast<int>(R2D::SystemStatusCode::Ok));
    R2D_TEST_CHECK_EQ(context, static_cast<int>(host_counts.code), static_cast<int>(R2D::SystemStatusCode::Ok));
    R2D_TEST_CHECK_EQ(context, host_counts.visible_count, ref_counts.visible_count);
    R2D_TEST_CHECK_EQ(context, host_counts.batch_count, ref_counts.batch_count);

    // Non-vacuous: the scene actually produces some visible and some culled items,
    // and batching merges adjacent same-key draws (batch_count strictly below the
    // draw count), so the equality below is not trivial.
    R2D_TEST_CHECK(context, ref_counts.visible_count > 0U);
    R2D_TEST_CHECK(context, ref_counts.visible_count < kEntityCount);
    R2D_TEST_CHECK(context, ref_counts.batch_count > 0U);
    R2D_TEST_CHECK(context, ref_counts.batch_count < ref_counts.visible_count);

    // Every derived stream must be byte-identical between the two stores.
    const R2D::U32 visible = ref_counts.visible_count;
    const R2D::U32 batches = ref_counts.batch_count;

    R2D_TEST_CHECK(context, bytesEqual(
        host_arena.worldTransforms().data(), ref_arena.worldTransforms().data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(WorldTransform)));
    R2D_TEST_CHECK(context, bytesEqual(
        host_arena.visibleItems().data(), ref_arena.visibleItems().data(),
        static_cast<R2D::Usize>(visible) * sizeof(VisibleItem)));
    R2D_TEST_CHECK(context, bytesEqual(
        host_arena.drawCommands().data(), ref_arena.drawCommands().data(),
        static_cast<R2D::Usize>(visible) * sizeof(DrawCommand)));
    R2D_TEST_CHECK(context, bytesEqual(
        host_arena.spriteInstances().data(), ref_arena.spriteInstances().data(),
        static_cast<R2D::Usize>(visible) * sizeof(SpriteInstance)));
    R2D_TEST_CHECK(context, bytesEqual(
        host_arena.batchCommands().data(), ref_arena.batchCommands().data(),
        static_cast<R2D::Usize>(batches) * sizeof(BatchCommand)));

    std::fprintf(
        stdout,
        "host_like_ecs_adapter: %u entities -> %u visible, %u batches; byte-identical across stores\n",
        kEntityCount,
        visible,
        batches);

    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("host_like_ecs_adapter_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("host_like_ecs_adapter_test unknown exception\n", stderr);
        return 1;
    }
}
