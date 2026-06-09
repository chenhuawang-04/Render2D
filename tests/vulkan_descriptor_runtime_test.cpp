#include <Render2D/Render2D.hpp>

#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;
using DescriptorRuntime = R2D::VulkanDescriptorRuntime<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<DescriptorRuntime>);
static_assert(R2D::StrictPodComponent<R2D::VulkanDescriptorRuntimeConfig>);
static_assert(R2D::StrictPodComponent<DescriptorSlice>);

void testInvalidConfig()
{
    DescriptorRuntime runtime;
    auto result = runtime.initialize({
        .device = VK_NULL_HANDLE,
        .max_descriptor_sets = 1U,
        .combined_image_sampler_count = 0U,
        .storage_buffer_count = 1U,
        .descriptor_pool_flags = 0U,
        .descriptor_set_layout_flags = 0U,
        .combined_image_sampler_binding_flags = 0U,
        .storage_buffer_binding_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(!runtime.isInitialized());
}

void testDescriptorLifecycle(const Render2DTest::VulkanSmokeContext& context_)
{
    ResourceRuntime resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    auto capacity = resource_runtime.reserveBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    BufferRef storage_buffer{};
    result = resource_runtime.createBufferRef(
        64U,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        R2D::NativeMemoryDomain::Upload,
        storage_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    DescriptorRuntime descriptor_runtime;
    result = descriptor_runtime.initialize({
        .device = context_.device,
        .max_descriptor_sets = 1U,
        .combined_image_sampler_count = 0U,
        .storage_buffer_count = 2U,
        .descriptor_pool_flags = 0U,
        .descriptor_set_layout_flags = 0U,
        .combined_image_sampler_binding_flags = 0U,
        .storage_buffer_binding_flags = 0U,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(descriptor_runtime.isInitialized());
    assert(descriptor_runtime.nativeDescriptorPool() != VK_NULL_HANDLE);
    assert(descriptor_runtime.nativeDescriptorSetLayout() != VK_NULL_HANDLE);
    assert(descriptor_runtime.descriptorArrayCapacity() == 2U);

    capacity = descriptor_runtime.reserveDescriptorSets(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(descriptor_runtime.descriptorCapacity() >= 1U);

    DescriptorSlice descriptor_slice{};
    result = descriptor_runtime.allocateDescriptorSlice(0U, 2U, descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(descriptor_slice.descriptor_set_id == 0U);
    assert(descriptor_slice.first == 0U);
    assert(descriptor_slice.count == 2U);
    assert(descriptor_slice.generation == 1U);
    assert(descriptor_runtime.descriptorCount() == 1U);

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    result = descriptor_runtime.resolveNativeDescriptorSet(descriptor_slice, descriptor_set);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(descriptor_set != VK_NULL_HANDLE);

    result = descriptor_runtime.updateStorageBuffer(
        descriptor_slice,
        1U,
        storage_buffer,
        resource_runtime,
        0U,
        64U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = descriptor_runtime.updateStorageBuffer(
        descriptor_slice,
        2U,
        storage_buffer,
        resource_runtime,
        0U,
        64U);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);

    result = descriptor_runtime.releaseDescriptorSlice(descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(descriptor_runtime.descriptorCount() == 0U);
    result = descriptor_runtime.resolveNativeDescriptorSet(descriptor_slice, descriptor_set);
    assert(result.code == R2D::NativeStatusCode::StaleReference);
    assert(descriptor_set == VK_NULL_HANDLE);

    DescriptorSlice reused_slice{};
    result = descriptor_runtime.allocateDescriptorSlice(0U, 1U, reused_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_slice.descriptor_set_id == descriptor_slice.descriptor_set_id);
    assert(reused_slice.generation == descriptor_slice.generation + 1U);

    result = descriptor_runtime.releaseDescriptorSlice(reused_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(storage_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
}

int main()
{
    try {
        testInvalidConfig();

        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testDescriptorLifecycle(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
