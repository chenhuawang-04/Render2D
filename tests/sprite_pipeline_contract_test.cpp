#include <Render2D/Render2D.hpp>

#include "support/TestHarness.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdio>
#include <exception>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using SpritePipelineRuntime = R2D::VulkanSpritePipelineRuntime<Provider, Dim>;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;

static_assert(R2D::StrictPodComponent<R2D::VulkanSpritePipelineConfig>);
static_assert(!R2D::SupportedRenderComponent<Provider, Dim, SpritePipelineRuntime>);

void testVertexInputContract(R2DT::TestContext& context_)
{
    const auto& bindings = SpritePipelineRuntime::kVertexBindings;
    R2D_TEST_CHECK_EQ(context_, bindings.size(), R2D::kVulkanSpriteVertexBindingCount);
    R2D_TEST_CHECK_EQ(context_, bindings[0U].binding, R2D::kVulkanSpriteVertexBinding);
    R2D_TEST_CHECK_EQ(context_, bindings[0U].stride, sizeof(SpriteVertex));
    R2D_TEST_CHECK_EQ(context_, bindings[0U].inputRate, VK_VERTEX_INPUT_RATE_VERTEX);
    R2D_TEST_CHECK_EQ(context_, bindings[1U].binding, R2D::kVulkanSpriteInstanceBinding);
    R2D_TEST_CHECK_EQ(context_, bindings[1U].stride, sizeof(SpriteInstance));
    R2D_TEST_CHECK_EQ(context_, bindings[1U].inputRate, VK_VERTEX_INPUT_RATE_INSTANCE);

    const auto& attributes = SpritePipelineRuntime::kVertexAttributes;
    R2D_TEST_CHECK_EQ(context_, attributes.size(), R2D::kVulkanSpriteVertexAttributeCount);
    R2D_TEST_CHECK_EQ(context_, attributes[0U].location, R2D::kVulkanSpritePositionLocation);
    R2D_TEST_CHECK_EQ(context_, attributes[0U].binding, R2D::kVulkanSpriteVertexBinding);
    R2D_TEST_CHECK_EQ(context_, attributes[0U].format, VK_FORMAT_R32G32_SFLOAT);
    R2D_TEST_CHECK_EQ(context_, attributes[0U].offset, offsetof(SpriteVertex, position_x));
    R2D_TEST_CHECK_EQ(context_, attributes[1U].location, R2D::kVulkanSpriteUvLocation);
    R2D_TEST_CHECK_EQ(context_, attributes[1U].offset, offsetof(SpriteVertex, uv_x));
    R2D_TEST_CHECK_EQ(context_, attributes[2U].location, R2D::kVulkanSpriteTransformRow0Location);
    R2D_TEST_CHECK_EQ(context_, attributes[2U].binding, R2D::kVulkanSpriteInstanceBinding);
    R2D_TEST_CHECK_EQ(context_, attributes[2U].format, VK_FORMAT_R32G32B32_SFLOAT);
    R2D_TEST_CHECK_EQ(context_, attributes[2U].offset, offsetof(SpriteInstance, transform_m00));
    R2D_TEST_CHECK_EQ(context_, attributes[3U].location, R2D::kVulkanSpriteTransformRow1Location);
    R2D_TEST_CHECK_EQ(context_, attributes[3U].offset, offsetof(SpriteInstance, transform_m10));
    R2D_TEST_CHECK_EQ(context_, attributes[4U].location, R2D::kVulkanSpriteUvRectLocation);
    R2D_TEST_CHECK_EQ(context_, attributes[4U].format, VK_FORMAT_R32G32B32A32_SFLOAT);
    R2D_TEST_CHECK_EQ(context_, attributes[4U].offset, offsetof(SpriteInstance, uv_min_x));
    R2D_TEST_CHECK_EQ(context_, attributes[5U].location, R2D::kVulkanSpriteColorLocation);
    R2D_TEST_CHECK_EQ(context_, attributes[5U].format, VK_FORMAT_R8G8B8A8_UNORM);
    R2D_TEST_CHECK_EQ(context_, attributes[5U].offset, offsetof(SpriteInstance, color_rgba8));
}

void testBindlessVertexInputContract(R2DT::TestContext& context_)
{
    // The bindless attribute set is the six base attributes plus the two R32_UINT
    // per-instance selectors at locations 6/7, all on the instance binding at the
    // SpriteInstance field offsets. The bindings (and stride) are unchanged.
    const auto& base = SpritePipelineRuntime::kVertexAttributes;
    const auto& attributes = SpritePipelineRuntime::kBindlessVertexAttributes;
    R2D_TEST_CHECK_EQ(context_, attributes.size(), R2D::kVulkanSpriteBindlessVertexAttributeCount);
    R2D_TEST_CHECK_EQ(context_, attributes.size(), base.size() + 2U);

    for (std::size_t index = 0U; index < base.size(); ++index) {
        R2D_TEST_CHECK_EQ(context_, attributes[index].location, base[index].location);
        R2D_TEST_CHECK_EQ(context_, attributes[index].binding, base[index].binding);
        R2D_TEST_CHECK_EQ(context_, attributes[index].format, base[index].format);
        R2D_TEST_CHECK_EQ(context_, attributes[index].offset, base[index].offset);
    }

    R2D_TEST_CHECK_EQ(context_, attributes[6U].location, R2D::kVulkanSpriteTextureIdLocation);
    R2D_TEST_CHECK_EQ(context_, attributes[6U].binding, R2D::kVulkanSpriteInstanceBinding);
    R2D_TEST_CHECK_EQ(context_, attributes[6U].format, VK_FORMAT_R32_UINT);
    R2D_TEST_CHECK_EQ(context_, attributes[6U].offset, offsetof(SpriteInstance, texture_id));
    R2D_TEST_CHECK_EQ(context_, attributes[7U].location, R2D::kVulkanSpriteSamplerIndexLocation);
    R2D_TEST_CHECK_EQ(context_, attributes[7U].binding, R2D::kVulkanSpriteInstanceBinding);
    R2D_TEST_CHECK_EQ(context_, attributes[7U].format, VK_FORMAT_R32_UINT);
    R2D_TEST_CHECK_EQ(context_, attributes[7U].offset, offsetof(SpriteInstance, sampler_index));
}

void testDescriptorConfig(R2DT::TestContext& context_)
{
    const auto config = SpritePipelineRuntime::makeDescriptorRuntimeConfig(
        VK_NULL_HANDLE,
        3U,
        2U,
        4U,
        8U);

    R2D_TEST_CHECK_EQ(context_, config.device, VK_NULL_HANDLE);
    R2D_TEST_CHECK_EQ(context_, config.max_descriptor_sets, 3U);
    R2D_TEST_CHECK_EQ(context_, config.combined_image_sampler_count, 2U);
    R2D_TEST_CHECK_EQ(context_, config.storage_buffer_count, 0U);
    R2D_TEST_CHECK_EQ(context_, config.descriptor_pool_flags, 4U);
    R2D_TEST_CHECK_EQ(context_, config.descriptor_set_layout_flags, 8U);
    R2D_TEST_CHECK_EQ(context_, config.combined_image_sampler_binding_flags, 0U);
    R2D_TEST_CHECK_EQ(context_, config.storage_buffer_binding_flags, 0U);
}

void testUnsupportedDomain(R2DT::TestContext& context_)
{
    R2D::VulkanPipelineRuntime<int, Dim> pipeline_runtime;
    R2D::PipelineRef<int, Dim> pipeline_ref{};
    const auto result = R2D::VulkanSpritePipelineRuntime<int, Dim>::createPipelineRef(
        pipeline_runtime,
        {
            .vertex_shader = VK_NULL_HANDLE,
            .fragment_shader = VK_NULL_HANDLE,
            .descriptor_set_layout = VK_NULL_HANDLE,
            .color_format = VK_FORMAT_R8G8B8A8_UNORM,
            .flags = 0U,
        },
        pipeline_ref);
    R2D_TEST_CHECK_EQ(context_, result.code, R2D::NativeStatusCode::UnsupportedDomain);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};
    testVertexInputContract(context);
    testBindlessVertexInputContract(context);
    testDescriptorConfig(context);
    testUnsupportedDomain(context);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("sprite_pipeline_contract_test exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("sprite_pipeline_contract_test unknown exception\n", stderr);
        return 1;
    }
}
