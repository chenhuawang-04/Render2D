#include <Render2D/Render2D.hpp>

#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace R2D = Render2D;

static_assert(R2D::StrictPodComponent<R2D::VulkanBindlessCapability>);

// A null physical device probes to an all-zero, unsupported capability, and the
// flag helpers then yield 0 so VulkanDescriptorRuntime builds the plain
// (fallback) array binding rather than a bindless one.
void testNullDeviceCapability()
{
    const R2D::VulkanBindlessCapability capability =
        R2D::queryVulkanBindlessCapability(VK_NULL_HANDLE);
    assert(capability.supported == 0U);
    assert(capability.partially_bound == 0U);
    assert(capability.runtime_descriptor_array == 0U);
    assert(capability.sampled_image_non_uniform_indexing == 0U);
    assert(capability.sampled_image_update_after_bind == 0U);
    assert(capability.max_per_stage_sampled_images == 0U);
    assert(capability.max_descriptor_set_sampled_images == 0U);
    assert(capability.api_version == 0U);

    assert(R2D::bindlessSampledImageBindingFlags(capability) == 0U);
    assert(R2D::bindlessDescriptorPoolFlags(capability) == 0U);
    assert(R2D::bindlessDescriptorSetLayoutFlags(capability) == 0U);
}

// The flag helpers map a supported capability onto exactly the descriptor
// pool/layout/binding flags VulkanDescriptorRuntime needs for an
// update-after-bind, partially-bound sampler array, and yield 0 otherwise.
void testFlagHelpers()
{
    R2D::VulkanBindlessCapability supported{};
    supported.supported = R2D::kVulkanBindlessSupported;
    assert(R2D::bindlessSampledImageBindingFlags(supported) ==
        static_cast<R2D::U32>(
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT));
    assert(R2D::bindlessDescriptorPoolFlags(supported) ==
        static_cast<R2D::U32>(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT));
    assert(R2D::bindlessDescriptorSetLayoutFlags(supported) ==
        static_cast<R2D::U32>(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT));

    R2D::VulkanBindlessCapability unsupported{};
    unsupported.supported = 0U;
    unsupported.partially_bound = 1U; // partial features alone never unlock the flags
    assert(R2D::bindlessSampledImageBindingFlags(unsupported) == 0U);
    assert(R2D::bindlessDescriptorPoolFlags(unsupported) == 0U);
    assert(R2D::bindlessDescriptorSetLayoutFlags(unsupported) == 0U);
}

// On a real device the probe must agree with the smoke context's own bindless
// flag, and a supported capability must carry the four enabling features plus
// positive update-after-bind sampled-image limits.
void testDeviceCapability(const Render2DTest::VulkanSmokeContext& context_)
{
    const R2D::VulkanBindlessCapability capability =
        R2D::queryVulkanBindlessCapability(context_.physical_device);
    assert(capability.api_version != 0U);
    assert((capability.supported == R2D::kVulkanBindlessSupported) == context_.supports_bindless);

    if (capability.supported == R2D::kVulkanBindlessSupported) {
        assert(capability.partially_bound == 1U);
        assert(capability.runtime_descriptor_array == 1U);
        assert(capability.sampled_image_non_uniform_indexing == 1U);
        assert(capability.sampled_image_update_after_bind == 1U);
        assert(capability.max_per_stage_sampled_images > 0U);
        assert(capability.max_descriptor_set_sampled_images > 0U);
        assert(R2D::bindlessSampledImageBindingFlags(capability) != 0U);
        assert(R2D::bindlessDescriptorPoolFlags(capability) != 0U);
        assert(R2D::bindlessDescriptorSetLayoutFlags(capability) != 0U);
    }
}

int main()
{
    try {
        testNullDeviceCapability();
        testFlagHelpers();

        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testDeviceCapability(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
