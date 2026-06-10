#include <Render2D/Render2D.hpp>

#include "support/FullScreenTriangleShaders.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using DescriptorRuntime = R2D::VulkanDescriptorRuntime<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;
using SpritePipelineRuntime = R2D::VulkanSpritePipelineRuntime<Provider, Dim>;

void testSpritePipelineLifecycle(const Render2DTest::VulkanSmokeContext& context_)
{
    DescriptorRuntime descriptor_runtime;
    auto result = descriptor_runtime.initialize(
        SpritePipelineRuntime::makeDescriptorRuntimeConfig(
            context_.device,
            1U,
            R2D::kVulkanSpriteTextureDescriptorCount,
            0U,
            0U));
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(descriptor_runtime.nativeDescriptorSetLayout() != VK_NULL_HANDLE);
    auto capacity = descriptor_runtime.reserveDescriptorSets(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    DescriptorSlice descriptor_slice{};
    result = descriptor_runtime.allocateDescriptorSlice(0U, 1U, descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(descriptor_slice.count == 1U);

    PipelineRuntime pipeline_runtime;
    result = pipeline_runtime.initialize({
        .device = context_.device,
        .pipeline_cache_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = pipeline_runtime.reservePipelines(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    result = pipeline_runtime.createShaderModule(Render2DTest::kFullScreenTriangleVertSpv, vertex_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(vertex_shader != VK_NULL_HANDLE);

    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    result = pipeline_runtime.createShaderModule(Render2DTest::kMagentaFragSpv, fragment_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(fragment_shader != VK_NULL_HANDLE);

    if (context_.supports_dynamic_rendering) {
        PipelineRef pipeline_ref{};
        result = SpritePipelineRuntime::createPipelineRef(
            pipeline_runtime,
            {
                .vertex_shader = vertex_shader,
                .fragment_shader = fragment_shader,
                .descriptor_set_layout = descriptor_runtime.nativeDescriptorSetLayout(),
                .color_format = VK_FORMAT_R8G8B8A8_UNORM,
                .flags = 0U,
            },
            pipeline_ref);
        assert(result.code == R2D::NativeStatusCode::Ok);
        assert(pipeline_ref.pipeline_id == 0U);
        assert(pipeline_ref.generation == 1U);

        VkPipeline native_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout native_pipeline_layout = VK_NULL_HANDLE;
        result = pipeline_runtime.resolveNativePipeline(
            pipeline_ref,
            native_pipeline,
            native_pipeline_layout);
        assert(result.code == R2D::NativeStatusCode::Ok);
        assert(native_pipeline != VK_NULL_HANDLE);
        assert(native_pipeline_layout != VK_NULL_HANDLE);

        result = pipeline_runtime.releasePipelineRef(pipeline_ref);
        assert(result.code == R2D::NativeStatusCode::Ok);
    }

    result = pipeline_runtime.destroyShaderModule(fragment_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = pipeline_runtime.destroyShaderModule(vertex_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = descriptor_runtime.releaseDescriptorSlice(descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

int main()
{
    try {
        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testSpritePipelineLifecycle(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
