// Stage 24 Track 1: SpatialCullSystem must be byte-identical to running
// TransformSystem::run -> BoundsSystem::run -> CullingSystem::run in sequence.
// The fused system writes the dense WorldTransform array (consumed downstream by
// SpriteInstanceBuildSystem) and the compacted VisibleItem stream, while never
// materializing WorldBounds. This test memcmp-verifies both outputs against the
// three-system reference chain across rotation, masking, and capacity cases.
#include <Render2D/Render2D.hpp>

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
using WorldBounds = R2D::WorldBounds<Provider, Dim>;
using Camera = R2D::Camera<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using SpatialCull = R2D::SpatialCullSystem<Provider, Dim>;

inline constexpr R2D::U32 kItemCount = 257U;
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

struct Inputs {
    R2D::McVector<Transform> transforms;
    R2D::McVector<LocalBounds> local_bounds;
    R2D::McVector<VisibilityMask> visibility_masks;
};

// A varied workload: a third of the items rotate (forcing the sincos affine
// path), a quarter sit far outside the camera (cull by bounds), and every fifth
// is masked off (cull by layer mask). The rest are visible. Item indices are not
// powers of two so chunked callers exercise non-divisible counts.
[[nodiscard]] Inputs makeInputs(R2D::U32 count_)
{
    Inputs inputs{};
    inputs.transforms.resize(count_);
    inputs.local_bounds.resize(count_);
    inputs.visibility_masks.resize(count_);

    for (R2D::U32 index = 0U; index < count_; ++index) {
        const bool rotated = (index % 3U) == 0U;
        const bool far_offscreen = (index % 4U) == 1U;
        const bool masked_off = (index % 5U) == 0U;
        const auto grid_x = static_cast<float>(index % 16U) - 8.0F;
        const auto grid_y = static_cast<float>((index / 16U) % 16U) - 8.0F;

        inputs.transforms[index] = {
            .source_id = index,
            .position_x = far_offscreen ? 5000.0F + grid_x : grid_x,
            .position_y = far_offscreen ? 5000.0F + grid_y : grid_y,
            .rotation_radians = rotated ? (0.13F * static_cast<float>(index + 1U)) : 0.0F,
            .scale_x = 1.0F + static_cast<float>(index % 3U) * 0.25F,
            .scale_y = 1.0F + static_cast<float>(index % 2U) * 0.5F,
        };
        inputs.local_bounds[index] = {
            .source_id = index,
            .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F),
        };
        inputs.visibility_masks[index] = {.mask = masked_off ? kHiddenMask : kVisibleMask};
    }
    return inputs;
}

struct ReferenceOutput {
    R2D::McVector<WorldTransform> world_transforms;
    R2D::McVector<WorldBounds> world_bounds;
    R2D::McVector<VisibleItem> visible_items;
    R2D::SystemStatusCode code;
    R2D::U32 visible_count;
};

[[nodiscard]] ReferenceOutput runReference(
    const Camera& camera_,
    std::span<const Transform> transforms_,
    std::span<const LocalBounds> local_bounds_,
    std::span<const VisibilityMask> visibility_masks_)
{
    ReferenceOutput out{};
    out.world_transforms.resize(transforms_.size());
    out.world_bounds.resize(transforms_.size());
    out.visible_items.resize(transforms_.size());

    auto result = R2D::TransformSystem<Provider, Dim>::run(transforms_, out.world_transforms);
    if (result.code != R2D::SystemStatusCode::Ok) {
        out.code = result.code;
        return out;
    }
    result = R2D::BoundsSystem<Provider, Dim>::run(
        out.world_transforms,
        local_bounds_,
        out.world_bounds);
    if (result.code != R2D::SystemStatusCode::Ok) {
        out.code = result.code;
        return out;
    }
    result = R2D::CullingSystem<Provider, Dim>::run(
        camera_,
        out.world_bounds,
        visibility_masks_,
        out.visible_items);
    out.code = result.code;
    out.visible_count = result.write_count;
    return out;
}

// Runs the fused system over the same inputs and asserts byte-identity against
// the reference chain for the dense WorldTransform array and the visible stream.
void checkFusedMatchesReference(
    R2DT::TestContext& context_,
    const Camera& camera_,
    const Inputs& inputs_,
    std::span<const VisibilityMask> visibility_masks_)
{
    const auto reference = runReference(
        camera_,
        inputs_.transforms,
        inputs_.local_bounds,
        visibility_masks_);
    R2D_TEST_CHECK(context_, reference.code == R2D::SystemStatusCode::Ok);

    R2D::McVector<WorldTransform> fused_world_transforms{};
    R2D::McVector<VisibleItem> fused_visible_items{};
    fused_world_transforms.resize(inputs_.transforms.size());
    fused_visible_items.resize(inputs_.transforms.size());

    const auto fused = SpatialCull::run(
        camera_,
        inputs_.transforms,
        inputs_.local_bounds,
        visibility_masks_,
        fused_world_transforms,
        fused_visible_items);

    R2D_TEST_CHECK(context_, fused.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, fused.write_count, reference.visible_count);
    R2D_TEST_CHECK_EQ(context_, fused.read_count, static_cast<R2D::U32>(inputs_.transforms.size()));

    // The whole dense WorldTransform array must match byte-for-byte.
    const int world_cmp = std::memcmp(
        fused_world_transforms.data(),
        reference.world_transforms.data(),
        inputs_.transforms.size() * sizeof(WorldTransform));
    R2D_TEST_CHECK_EQ(context_, world_cmp, 0);

    // The compacted visible stream must match byte-for-byte up to visible_count.
    const int visible_cmp = std::memcmp(
        fused_visible_items.data(),
        reference.visible_items.data(),
        static_cast<R2D::Usize>(fused.write_count) * sizeof(VisibleItem));
    R2D_TEST_CHECK_EQ(context_, visible_cmp, 0);

    // A non-trivial workload must actually produce some visible and some culled
    // items, so the equivalence above is not vacuously true.
    R2D_TEST_CHECK(context_, fused.write_count > 0U);
    R2D_TEST_CHECK(context_, fused.write_count < inputs_.transforms.size());
}

[[nodiscard]] auto runTest() -> int
{
    R2DT::TestContext context{};

    const auto inputs = makeInputs(kItemCount);

    // Equivalence with explicit per-item masks.
    checkFusedMatchesReference(context, kCamera, inputs, inputs.visibility_masks);

    // Equivalence with an empty mask span (CullingSystem falls back to the
    // camera layer mask, so every item passes the mask test).
    checkFusedMatchesReference(context, kCamera, inputs, std::span<const VisibilityMask>{});

    // Empty input: Ok with zero visible, and matches the (empty) reference.
    {
        const Inputs empty = makeInputs(0U);
        R2D::McVector<WorldTransform> world_transforms{};
        R2D::McVector<VisibleItem> visible_items{};
        const auto result = SpatialCull::run(
            kCamera,
            empty.transforms,
            empty.local_bounds,
            empty.visibility_masks,
            world_transforms,
            visible_items);
        R2D_TEST_CHECK_EQ(context, static_cast<int>(result.code), static_cast<int>(R2D::SystemStatusCode::Ok));
        R2D_TEST_CHECK_EQ(context, result.write_count, 0U);
    }

    // Visible-item capacity below the visible count -> InsufficientCapacity,
    // matching the reference chain's CullingSystem behaviour.
    {
        R2D::McVector<WorldTransform> world_transforms{};
        world_transforms.resize(inputs.transforms.size());
        R2D::McVector<VisibleItem> short_visible{};
        short_visible.resize(1U);
        const auto result = SpatialCull::run(
            kCamera,
            inputs.transforms,
            inputs.local_bounds,
            inputs.visibility_masks,
            world_transforms,
            short_visible);
        R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InsufficientCapacity);
    }

    // WorldTransform capacity below the item count -> InsufficientCapacity,
    // matching TransformSystem's contract.
    {
        R2D::McVector<WorldTransform> short_world{};
        short_world.resize(inputs.transforms.size() - 1U);
        R2D::McVector<VisibleItem> visible_items{};
        visible_items.resize(inputs.transforms.size());
        const auto result = SpatialCull::run(
            kCamera,
            inputs.transforms,
            inputs.local_bounds,
            inputs.visibility_masks,
            short_world,
            visible_items);
        R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InsufficientCapacity);
    }

    // transforms.size() != local_bounds.size() -> InvalidInput, matching
    // BoundsSystem's contract.
    {
        R2D::McVector<WorldTransform> world_transforms{};
        world_transforms.resize(inputs.transforms.size());
        R2D::McVector<VisibleItem> visible_items{};
        visible_items.resize(inputs.transforms.size());
        const std::span<const LocalBounds> short_bounds{
            inputs.local_bounds.data(),
            inputs.local_bounds.size() - 1U,
        };
        const auto result = SpatialCull::run(
            kCamera,
            inputs.transforms,
            short_bounds,
            inputs.visibility_masks,
            world_transforms,
            visible_items);
        R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InvalidInput);
    }

    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    }
    catch (const std::exception& exception) {
        std::fputs("spatial_cull_system_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("spatial_cull_system_test unknown exception\n", stderr);
        return 1;
    }
}
