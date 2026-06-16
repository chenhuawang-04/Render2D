// Stage 24 Track 2: BatchedTransformSystem builds the dense WorldTransform array
// via MMath::mat3FromTrsArray (8-wide AVX2 sincos) for the compute-bound rotating
// workload. This test pins two properties:
//
//  1. Determinism contract (byte-identity): the batched output is bit-for-bit
//     equal to a single-threaded per-element build using the SAME general formula
//     (MMath::sincos, no rotation==0 fast path). Because fast_math's scalar and
//     SIMD sincos share one bit-identical kernel, this holds regardless of tile
//     boundaries -- so a chunked/threaded driver calling run() per chunk merges
//     bit-for-bit. Verified by memcmp over the whole array.
//
//  2. Numerical equivalence to TransformSystem::run: every affine element compares
//     equal under float == (which treats +0.0 == -0.0). They are byte-identical
//     for rotated items; for rotation==0 they differ only in the sign bit of
//     m01 (TransformSystem's fast path writes +0.0, the general formula -0.0),
//     which is numerically irrelevant downstream.
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
using Batched = R2D::BatchedTransformSystem<Provider, Dim>;
using TransformSys = R2D::TransformSystem<Provider, Dim>;

// A varied workload: every third item rotates (forcing the sincos path), the rest
// are axis-aligned (rotation==0, exercising the -0.0 vs +0.0 m01 case). Counts are
// chosen to span the internal tile size (256) and its remainder.
[[nodiscard]] R2D::McVector<Transform> makeTransforms(R2D::U32 count_)
{
    R2D::McVector<Transform> transforms{};
    transforms.resize(count_);
    for (R2D::U32 index = 0U; index < count_; ++index) {
        const bool rotated = (index % 3U) == 0U;
        transforms[index] = {
            .source_id = index,
            .position_x = static_cast<float>(index % 32U) - 16.0F,
            .position_y = static_cast<float>((index / 32U) % 32U) - 16.0F,
            .rotation_radians = rotated ? (0.137F * static_cast<float>(index + 1U)) : 0.0F,
            .scale_x = 1.0F + static_cast<float>(index % 3U) * 0.25F,
            .scale_y = 1.0F + static_cast<float>(index % 2U) * 0.5F,
        };
    }
    return transforms;
}

// Per-element reference using the SAME general formula as mat3FromTrsArray
// (MMath::sincos, no fast path) -- the batched output must equal this byte-for-byte.
[[nodiscard]] WorldTransform referenceOne(const Transform& transform_)
{
    MMath::SinCos sin_cos{};
    MMath::sincos(MMath::Angle{.value = transform_.rotation_radians}, &sin_cos);
    return WorldTransform{
        .source_id = transform_.source_id,
        .affine = R2D::Mat3{
            .m00 = transform_.scale_x * sin_cos.cos,
            .m01 = -transform_.scale_y * sin_cos.sin,
            .m02 = transform_.position_x,
            .m10 = transform_.scale_x * sin_cos.sin,
            .m11 = transform_.scale_y * sin_cos.cos,
            .m12 = transform_.position_y,
            .m20 = 0.0F,
            .m21 = 0.0F,
            .m22 = 1.0F,
        },
    };
}

void checkBatchedEquivalence(R2DT::TestContext& context_, R2D::U32 count_)
{
    const auto transforms = makeTransforms(count_);

    R2D::McVector<WorldTransform> batched{};
    batched.resize(count_);
    const auto result = Batched::run(transforms, batched);
    R2D_TEST_CHECK(context_, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.read_count, count_);
    R2D_TEST_CHECK_EQ(context_, result.write_count, count_);

    // (1) Byte-identity vs the general-formula reference (the determinism contract).
    R2D::McVector<WorldTransform> general_reference{};
    general_reference.resize(count_);
    for (R2D::U32 index = 0U; index < count_; ++index) {
        general_reference[index] = referenceOne(transforms[index]);
    }
    const int reference_cmp = std::memcmp(
        batched.data(),
        general_reference.data(),
        static_cast<R2D::Usize>(count_) * sizeof(WorldTransform));
    R2D_TEST_CHECK_EQ(context_, reference_cmp, 0);

    // (2) Numerical equivalence to TransformSystem::run (float == treats +-0 equal).
    R2D::McVector<WorldTransform> transform_system{};
    transform_system.resize(count_);
    const auto ts_result = TransformSys::run(transforms, transform_system);
    R2D_TEST_CHECK(context_, ts_result.code == R2D::SystemStatusCode::Ok);
    for (R2D::U32 index = 0U; index < count_; ++index) {
        R2D_TEST_CHECK_EQ(context_, batched[index].source_id, transform_system[index].source_id);
        const auto& a = batched[index].affine;
        const auto& b = transform_system[index].affine;
        R2D_TEST_CHECK(context_, a.m00 == b.m00);
        R2D_TEST_CHECK(context_, a.m01 == b.m01);
        R2D_TEST_CHECK(context_, a.m02 == b.m02);
        R2D_TEST_CHECK(context_, a.m10 == b.m10);
        R2D_TEST_CHECK(context_, a.m11 == b.m11);
        R2D_TEST_CHECK(context_, a.m12 == b.m12);
        R2D_TEST_CHECK(context_, a.m22 == b.m22);
    }
}

[[nodiscard]] auto runTest() -> int
{
    R2DT::TestContext context{};

    // Counts spanning the internal tile size (256): sub-tile, exact, +1, multi-tile.
    for (const R2D::U32 count : {1U, 7U, 8U, 9U, 64U, 255U, 256U, 257U, 520U}) {
        checkBatchedEquivalence(context, count);
    }

    // Empty input: Ok with zero writes.
    {
        R2D::McVector<Transform> transforms{};
        R2D::McVector<WorldTransform> world_transforms{};
        const auto result = Batched::run(transforms, world_transforms);
        R2D_TEST_CHECK_EQ(context, static_cast<int>(result.code), static_cast<int>(R2D::SystemStatusCode::Ok));
        R2D_TEST_CHECK_EQ(context, result.write_count, 0U);
    }

    // WorldTransform capacity below the item count -> InsufficientCapacity,
    // matching TransformSystem's contract.
    {
        const auto transforms = makeTransforms(300U);
        R2D::McVector<WorldTransform> short_world{};
        short_world.resize(transforms.size() - 1U);
        const auto result = Batched::run(transforms, short_world);
        R2D_TEST_CHECK(context, result.code == R2D::SystemStatusCode::InsufficientCapacity);
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
        std::fputs("batched_transform_system_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...) {
        std::fputs("batched_transform_system_test unknown exception\n", stderr);
        return 1;
    }
}
