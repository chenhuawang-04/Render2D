#include <Render2D/Render2D.hpp>

#include "support/FullScreenTriangleShaders.hpp"
#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<PipelineRuntime>);
static_assert(R2D::StrictPodComponent<R2D::VulkanPipelineRuntimeConfig>);
static_assert(R2D::StrictPodComponent<R2D::VulkanGraphicsPipelineConfig>);
static_assert(R2D::StrictPodComponent<PipelineRef>);

void testInvalidConfig()
{
    PipelineRuntime runtime;
    const auto result = runtime.initialize({
        .device = VK_NULL_HANDLE,
        .pipeline_cache_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(!runtime.isInitialized());
}

void testPipelineLifecycle(const Render2DTest::VulkanSmokeContext& context_)
{
    PipelineRuntime runtime;
    auto result = runtime.initialize({
        .device = context_.device,
        .pipeline_cache_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.isInitialized());
    assert(runtime.nativePipelineCache() != VK_NULL_HANDLE);

    auto capacity = runtime.reservePipelines(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(runtime.pipelineCapacity() >= 1U);

    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    result = runtime.createShaderModule(Render2DTest::kFullScreenTriangleVertSpv, vertex_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(vertex_shader != VK_NULL_HANDLE);

    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    result = runtime.createShaderModule(Render2DTest::kMagentaFragSpv, fragment_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(fragment_shader != VK_NULL_HANDLE);

    if (context_.supports_dynamic_rendering) {
        PipelineRef pipeline_ref{};
        result = runtime.createGraphicsPipelineRef(
            {
                .vertex_shader = vertex_shader,
                .fragment_shader = fragment_shader,
                .descriptor_set_layouts = nullptr,
                .descriptor_set_layout_count = 0U,
                .color_format = VK_FORMAT_R8G8B8A8_UNORM,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                .cull_mode = VK_CULL_MODE_NONE,
                .front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                .polygon_mode = VK_POLYGON_MODE_FILL,
                .sample_count = VK_SAMPLE_COUNT_1_BIT,
                .flags = 0U,
            },
            pipeline_ref);
        assert(result.code == R2D::NativeStatusCode::Ok);
        assert(pipeline_ref.pipeline_id == 0U);
        assert(pipeline_ref.generation == 1U);
        assert(pipeline_ref.handle != 0U);
        assert(pipeline_ref.layout_handle != 0U);
        assert(runtime.pipelineCount() == 1U);

        VkPipeline native_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout native_pipeline_layout = VK_NULL_HANDLE;
        result = runtime.resolveNativePipeline(pipeline_ref, native_pipeline, native_pipeline_layout);
        assert(result.code == R2D::NativeStatusCode::Ok);
        assert(native_pipeline != VK_NULL_HANDLE);
        assert(native_pipeline_layout != VK_NULL_HANDLE);

        result = runtime.releasePipelineRef(pipeline_ref);
        assert(result.code == R2D::NativeStatusCode::Ok);
        assert(runtime.pipelineCount() == 0U);
        result = runtime.resolveNativePipeline(pipeline_ref, native_pipeline, native_pipeline_layout);
        assert(result.code == R2D::NativeStatusCode::StaleReference);
        assert(native_pipeline == VK_NULL_HANDLE);
        assert(native_pipeline_layout == VK_NULL_HANDLE);
    }

    result = runtime.destroyShaderModule(fragment_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(fragment_shader == VK_NULL_HANDLE);
    result = runtime.destroyShaderModule(vertex_shader);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(vertex_shader == VK_NULL_HANDLE);
}

int main()
{
    try {
        testInvalidConfig();

        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testPipelineLifecycle(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
