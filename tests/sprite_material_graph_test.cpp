#include "support/TestHarness.hpp"

#include <Render2D/Render2D.hpp>

#include <array>
#include <cstdio>
#include <exception>
#include <span>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using MaterialGraph = R2D::VulkanSpriteMaterialGraph<Provider, Dim>;
using MaterialParams = R2D::VulkanSpriteMaterialParams;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using SpriteMaterialBinding = R2D::SpriteMaterialBinding<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using InstanceBuild = R2D::SpriteInstanceBuildSystem<Provider, Dim>;

static_assert(R2D::StrictPodComponent<MaterialParams>);
static_assert(R2D::StrictPodComponent<R2D::VulkanSpriteMaterialGraphConfig>);
static_assert(!R2D::SupportedRenderComponent<Provider, Dim, MaterialGraph>);

[[nodiscard]] PipelineRef makePipelineRef(R2D::U32 pipeline_id_, R2D::U32 generation_) noexcept
{
    return {
        .handle = 0U,
        .layout_handle = 0U,
        .pipeline_id = pipeline_id_,
        .generation = generation_,
        .flags = 0U,
    };
}

[[nodiscard]] SpriteInstance makeInstance(R2D::U32 material_id_, R2D::U32 material_generation_) noexcept
{
    SpriteInstance instance{};
    instance.material_id = material_id_;
    instance.material_generation = material_generation_;
    instance.sampler_index = 0xDEADBEEFU; // poison: resolve must overwrite, never leave it
    return instance;
}

void testRegisterResolve(R2DT::TestContext& context_)
{
    MaterialGraph graph;
    R2D_TEST_CHECK(context_, !graph.isInitialized());
    R2D_TEST_CHECK(
        context_,
        graph.initialize({.material_capacity = 8U, .sampler_capacity = 4U}).code == R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, graph.isInitialized());

    const auto register_result = graph.registerMaterial(
        2U,
        5U,
        makePipelineRef(7U, 3U),
        2U,
        {.tint_rgba8 = 0x11223344U, .blend_mode = 1U, .binding_flags = 0x40U, .reserved = 0U});
    R2D_TEST_CHECK(context_, register_result.code == R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, graph.isRegistered(2U, 5U));
    R2D_TEST_CHECK(context_, !graph.isRegistered(2U, 4U)); // generation mismatch
    R2D_TEST_CHECK(context_, !graph.isRegistered(3U, 5U)); // unregistered id

    SpriteMaterialBinding binding{};
    R2D_TEST_CHECK(context_, graph.resolveBinding(2U, 5U, binding).code == R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, binding.material_id, 2U);
    R2D_TEST_CHECK_EQ(context_, binding.material_generation, 5U);
    R2D_TEST_CHECK_EQ(context_, binding.pipeline_id, 7U);
    R2D_TEST_CHECK_EQ(context_, binding.pipeline_generation, 3U);
    R2D_TEST_CHECK_EQ(context_, binding.sampler_index, 2U);
    R2D_TEST_CHECK_EQ(context_, binding.flags, 0x40U);

    R2D::U32 sampler_index = 0U;
    R2D_TEST_CHECK(context_, graph.resolveSamplerIndex(2U, 5U, sampler_index));
    R2D_TEST_CHECK_EQ(context_, sampler_index, 2U);

    MaterialParams params{};
    R2D_TEST_CHECK(context_, graph.resolveParams(2U, 5U, params));
    R2D_TEST_CHECK_EQ(context_, params.tint_rgba8, 0x11223344U);
    R2D_TEST_CHECK_EQ(context_, params.blend_mode, 1U);
}

void testRejections(R2DT::TestContext& context_)
{
    MaterialGraph graph;
    // Resolve / register before initialize is rejected, not silently accepted.
    SpriteMaterialBinding binding{};
    R2D_TEST_CHECK(context_, graph.resolveBinding(0U, 1U, binding).code == R2D::NativeStatusCode::StaleReference);
    R2D_TEST_CHECK(
        context_,
        graph.registerMaterial(0U, 1U, makePipelineRef(1U, 1U), 0U, {}).code == R2D::NativeStatusCode::InvalidInput);

    R2D_TEST_CHECK(
        context_,
        graph.initialize({.material_capacity = 4U, .sampler_capacity = 2U}).code == R2D::NativeStatusCode::Ok);
    // Double initialize is rejected.
    R2D_TEST_CHECK(
        context_,
        graph.initialize({.material_capacity = 4U, .sampler_capacity = 2U}).code == R2D::NativeStatusCode::InvalidInput);

    // material_id past capacity, sampler_index past the sampler array, zero
    // material generation, and zero pipeline generation are all rejected.
    R2D_TEST_CHECK(
        context_,
        graph.registerMaterial(4U, 1U, makePipelineRef(1U, 1U), 0U, {}).code == R2D::NativeStatusCode::InvalidInput);
    R2D_TEST_CHECK(
        context_,
        graph.registerMaterial(0U, 1U, makePipelineRef(1U, 1U), 2U, {}).code == R2D::NativeStatusCode::InvalidInput);
    R2D_TEST_CHECK(
        context_,
        graph.registerMaterial(0U, 0U, makePipelineRef(1U, 1U), 0U, {}).code == R2D::NativeStatusCode::InvalidInput);
    R2D_TEST_CHECK(
        context_,
        graph.registerMaterial(0U, 1U, makePipelineRef(1U, 0U), 0U, {}).code == R2D::NativeStatusCode::InvalidInput);

    // Evicting an unregistered / stale material is a StaleReference, and after a
    // successful evict the material no longer resolves.
    R2D_TEST_CHECK(
        context_,
        graph.registerMaterial(1U, 9U, makePipelineRef(3U, 2U), 1U, {}).code == R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, graph.evict(1U, 8U).code == R2D::NativeStatusCode::StaleReference);
    R2D_TEST_CHECK(context_, graph.evict(1U, 9U).code == R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, !graph.isRegistered(1U, 9U));
    R2D_TEST_CHECK(context_, graph.evict(1U, 9U).code == R2D::NativeStatusCode::StaleReference); // double evict
}

void testSamplerResolvePass(R2DT::TestContext& context_)
{
    MaterialGraph graph;
    R2D_TEST_CHECK(
        context_,
        graph.initialize({.material_capacity = 8U, .sampler_capacity = 4U}).code == R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(
        context_,
        graph.registerMaterial(1U, 1U, makePipelineRef(10U, 1U), 1U, {.tint_rgba8 = 0U, .blend_mode = 0U, .binding_flags = 0U, .reserved = 0U}).code == R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(
        context_,
        graph.registerMaterial(2U, 1U, makePipelineRef(11U, 1U), 3U, {.tint_rgba8 = 0U, .blend_mode = 0U, .binding_flags = 0U, .reserved = 0U}).code == R2D::NativeStatusCode::Ok);

    // Build the binding rows the pure resolve pass consumes, identity-indexed by
    // material_id (slot 0 is an unused placeholder so material_id maps to index).
    std::array<SpriteMaterialBinding, 3U> bindings{};
    R2D_TEST_CHECK(context_, graph.resolveBinding(1U, 1U, bindings[1U]).code == R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK(context_, graph.resolveBinding(2U, 1U, bindings[2U]).code == R2D::NativeStatusCode::Ok);
    bindings[0U] = bindings[1U]; // keep slot 0 valid; never referenced by an instance below

    // Two materials -> two distinct sampler slots stamped onto their instances.
    std::array<SpriteInstance, 3U> instances{{
        makeInstance(1U, 1U),
        makeInstance(2U, 1U),
        makeInstance(1U, 1U),
    }};
    const auto result = InstanceBuild::resolveSamplerIndices(instances, bindings);
    R2D_TEST_CHECK(context_, result.code == R2D::SystemStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, result.write_count, 3U);
    R2D_TEST_CHECK_EQ(context_, instances[0U].sampler_index, 1U);
    R2D_TEST_CHECK_EQ(context_, instances[1U].sampler_index, 3U);
    R2D_TEST_CHECK_EQ(context_, instances[2U].sampler_index, 1U);

    // A stale material generation is rejected, never resolved to slot 0.
    std::array<SpriteInstance, 1U> stale{{makeInstance(1U, 2U)}};
    const auto stale_result = InstanceBuild::resolveSamplerIndices(stale, bindings);
    R2D_TEST_CHECK(context_, stale_result.code == R2D::SystemStatusCode::InvalidInput);
    R2D_TEST_CHECK_EQ(context_, stale.front().sampler_index, 0xDEADBEEFU); // untouched
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testRegisterResolve(context);
    testRejections(context);
    testSamplerResolvePass(context);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("sprite_material_graph_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("sprite_material_graph_test unknown exception\n", stderr);
        return 1;
    }
}
