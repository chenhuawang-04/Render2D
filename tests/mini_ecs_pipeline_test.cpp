// Lightweight-ECS usability proof. Two things are demonstrated:
//
//   A. Pipeline parity. A non-trivial scene is authored into the test-only
//      MiniEcs (create entities, attach Strict POD components), then
//      gatherRenderInputs() packs it into row-aligned dense columns and the
//      unchanged span-only CPU chain (SpatialCull -> CommandBuild ->
//      SpriteInstanceBuild -> Batch) runs over those spans. The exact same scene
//      is also built directly into plain McVector arrays and run through the same
//      chain; every derived stream is asserted byte-identical between the two.
//      This proves the ECS feeds the systems correctly -- identical to the
//      canonical array path -- without the systems knowing the ECS exists.
//
//   B. Entity lifecycle. create / destroy / alive / add / get / has / remove,
//      generational stale-handle rejection, and slot recycling.
//
// MiniEcs is test-only scaffolding (the host engine owns the production ECS at
// merge); this test exists to show Render2D is usable end-to-end from an
// author-it-yourself world. McVector only, never the standard dynamic vector.
#include <Render2D/Render2D.hpp>

#include "support/HostLikeEcs.hpp"
#include "support/MiniEcs.hpp"
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
using SceneEcs = R2DT::SceneEcs<Provider, Dim>;
using MiniEntity = R2DT::MiniEntity;
using Columns = R2DT::RenderInputColumns<Provider, Dim>;

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

// The canonical scene, built once into plain arrays. A mix of rotated, far-
// offscreen (culled by bounds), and layer-masked-off (culled by mask) entities
// across a few textures/materials so batching has adjacent draws to merge. Same
// formula the proven host_like_ecs_adapter test uses, so the visible/batch counts
// are known to be non-trivial.
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

// The production CPU front-end + sprite chain, fed entirely through spans into the
// supplied frame arena -- identical code regardless of where the input spans come
// from (plain arrays or the ECS gather).
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

// Section A: author the scene into the ECS, gather it, run the chain, and prove
// the result is byte-identical to the plain-array path.
void testPipelineParity(R2DT::TestContext& context_)
{
    const Scene scene = makeScene(kEntityCount);

    SceneEcs ecs{};
    ecs.reserve(kEntityCount);
    for (R2D::U32 index = 0U; index < kEntityCount; ++index) {
        const MiniEntity entity = ecs.create();
        ecs.add<Transform>(entity, scene.transforms[index]);
        ecs.add<LocalBounds>(entity, scene.local_bounds[index]);
        ecs.add<VisibilityMask>(entity, scene.visibility_masks[index]);
        ecs.add<Sprite>(entity, scene.sprites[index]);
    }
    R2D_TEST_CHECK_EQ(context_, ecs.liveCount(), kEntityCount);
    R2D_TEST_CHECK_EQ(context_, ecs.slotCount(), kEntityCount);

    Columns columns{};
    R2DT::gatherRenderInputs(ecs, columns);
    R2D_TEST_CHECK_EQ(context_, columns.size(), static_cast<R2D::Usize>(kEntityCount));

    // Gather correctness: the packed columns are byte-identical to the authoring
    // arrays (same data, same order), so any output difference below is the chain
    // alone, not the gather.
    R2D_TEST_CHECK(context_, bytesEqual(
        columns.transforms.data(), scene.transforms.data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(Transform)));
    R2D_TEST_CHECK(context_, bytesEqual(
        columns.local_bounds.data(), scene.local_bounds.data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(LocalBounds)));
    R2D_TEST_CHECK(context_, bytesEqual(
        columns.visibility_masks.data(), scene.visibility_masks.data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(VisibilityMask)));
    R2D_TEST_CHECK(context_, bytesEqual(
        columns.sprites.data(), scene.sprites.data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(Sprite)));

    Arena ref_arena{};
    const auto ref_counts = runChain(
        kCamera,
        std::span<const Transform>{scene.transforms.data(), scene.transforms.size()},
        std::span<const LocalBounds>{scene.local_bounds.data(), scene.local_bounds.size()},
        std::span<const VisibilityMask>{scene.visibility_masks.data(), scene.visibility_masks.size()},
        std::span<const Sprite>{scene.sprites.data(), scene.sprites.size()},
        ref_arena);

    Arena ecs_arena{};
    const auto ecs_counts = runChain(
        kCamera,
        columns.transformSpan(),
        columns.localBoundsSpan(),
        columns.visibilityMaskSpan(),
        columns.spriteSpan(),
        ecs_arena);

    R2D_TEST_CHECK_EQ(context_, static_cast<int>(ref_counts.code), static_cast<int>(R2D::SystemStatusCode::Ok));
    R2D_TEST_CHECK_EQ(context_, static_cast<int>(ecs_counts.code), static_cast<int>(R2D::SystemStatusCode::Ok));
    R2D_TEST_CHECK_EQ(context_, ecs_counts.visible_count, ref_counts.visible_count);
    R2D_TEST_CHECK_EQ(context_, ecs_counts.batch_count, ref_counts.batch_count);

    // Non-vacuous: some visible, some culled, and batching actually merges.
    R2D_TEST_CHECK(context_, ref_counts.visible_count > 0U);
    R2D_TEST_CHECK(context_, ref_counts.visible_count < kEntityCount);
    R2D_TEST_CHECK(context_, ref_counts.batch_count > 0U);
    R2D_TEST_CHECK(context_, ref_counts.batch_count < ref_counts.visible_count);

    const R2D::U32 visible = ref_counts.visible_count;
    const R2D::U32 batches = ref_counts.batch_count;
    R2D_TEST_CHECK(context_, bytesEqual(
        ecs_arena.worldTransforms().data(), ref_arena.worldTransforms().data(),
        static_cast<R2D::Usize>(kEntityCount) * sizeof(WorldTransform)));
    R2D_TEST_CHECK(context_, bytesEqual(
        ecs_arena.visibleItems().data(), ref_arena.visibleItems().data(),
        static_cast<R2D::Usize>(visible) * sizeof(VisibleItem)));
    R2D_TEST_CHECK(context_, bytesEqual(
        ecs_arena.drawCommands().data(), ref_arena.drawCommands().data(),
        static_cast<R2D::Usize>(visible) * sizeof(DrawCommand)));
    R2D_TEST_CHECK(context_, bytesEqual(
        ecs_arena.spriteInstances().data(), ref_arena.spriteInstances().data(),
        static_cast<R2D::Usize>(visible) * sizeof(SpriteInstance)));
    R2D_TEST_CHECK(context_, bytesEqual(
        ecs_arena.batchCommands().data(), ref_arena.batchCommands().data(),
        static_cast<R2D::Usize>(batches) * sizeof(BatchCommand)));

    std::fprintf(
        stdout,
        "mini_ecs_pipeline: %u entities -> %u visible, %u batches; byte-identical to array path\n",
        kEntityCount,
        visible,
        batches);
}

// Section B: entity lifecycle -- handles, generational invalidation, slot reuse,
// and per-entity component add/get/has/remove.
void testEntityLifecycle(R2DT::TestContext& context_)
{
    SceneEcs ecs{};

    const MiniEntity a = ecs.create();
    const MiniEntity b = ecs.create();
    const MiniEntity c = ecs.create();
    R2D_TEST_CHECK_EQ(context_, ecs.liveCount(), 3U);
    R2D_TEST_CHECK_EQ(context_, ecs.slotCount(), 3U);
    R2D_TEST_CHECK(context_, ecs.alive(a) && ecs.alive(b) && ecs.alive(c));

    // Component add / get / has on a live entity.
    const Transform a_transform{
        .source_id = 7U,
        .position_x = 1.5F,
        .position_y = -2.0F,
        .rotation_radians = 0.0F,
        .scale_x = 1.0F,
        .scale_y = 1.0F,
    };
    ecs.add<Transform>(a, a_transform);
    R2D_TEST_CHECK(context_, ecs.has<Transform>(a));
    R2D_TEST_CHECK(context_, !ecs.has<LocalBounds>(a));
    const Transform* a_got = ecs.get<Transform>(a);
    R2D_TEST_CHECK(context_, a_got != nullptr);
    if (a_got != nullptr) {
        R2D_TEST_CHECK_EQ(context_, a_got->source_id, 7U);
        R2D_TEST_CHECK(context_, a_got->position_x == 1.5F);
    }

    ecs.add<Sprite>(b, Sprite{
        .source_id = 99U,
        .texture_id = 5U,
        .texture_generation = 1U,
        .texture_region_id = 0U,
        .texture_region_generation = 0U,
        .material_id = 6U,
        .material_generation = 1U,
        .color_rgba8 = kVisibleMask,
        .layer = 1U,
        .flags = 0U,
    });
    R2D_TEST_CHECK(context_, ecs.has<Sprite>(b));

    // Component remove.
    ecs.remove<Transform>(a);
    R2D_TEST_CHECK(context_, !ecs.has<Transform>(a));
    R2D_TEST_CHECK(context_, ecs.get<Transform>(a) == nullptr);

    // Destroy b: the handle goes stale and its components are dropped.
    const R2D::U32 b_slot = b.index;
    ecs.destroy(b);
    R2D_TEST_CHECK(context_, !ecs.alive(b));
    R2D_TEST_CHECK(context_, ecs.get<Sprite>(b) == nullptr);
    R2D_TEST_CHECK_EQ(context_, ecs.liveCount(), 2U);
    R2D_TEST_CHECK_EQ(context_, ecs.store<Sprite>().size(), static_cast<R2D::Usize>(0U));

    // Destroying a stale handle is a no-op.
    ecs.destroy(b);
    R2D_TEST_CHECK_EQ(context_, ecs.liveCount(), 2U);

    // Slot recycling: the next create reuses b's slot with a bumped generation, so
    // the old handle stays stale and the slot starts componentless.
    const MiniEntity d = ecs.create();
    R2D_TEST_CHECK_EQ(context_, d.index, b_slot);
    R2D_TEST_CHECK(context_, d.generation != b.generation);
    R2D_TEST_CHECK(context_, ecs.alive(d));
    R2D_TEST_CHECK(context_, !ecs.alive(b));
    R2D_TEST_CHECK(context_, !ecs.has<Sprite>(d));
    R2D_TEST_CHECK_EQ(context_, ecs.liveCount(), 3U);
    R2D_TEST_CHECK_EQ(context_, ecs.slotCount(), 3U);

    // c was never touched and stays live and distinct.
    R2D_TEST_CHECK(context_, ecs.alive(c));
    R2D_TEST_CHECK(context_, !(c == d));

    std::fputs("mini_ecs_pipeline: entity lifecycle (create/destroy/generation/reuse) ok\n", stdout);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testPipelineParity(context);
    testEntityLifecycle(context);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("mini_ecs_pipeline_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("mini_ecs_pipeline_test unknown exception\n", stderr);
        return 1;
    }
}
