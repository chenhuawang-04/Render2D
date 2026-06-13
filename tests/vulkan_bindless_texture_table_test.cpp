#include <Render2D/Render2D.hpp>

#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using SamplerRef = R2D::SamplerRef<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;
using SamplerRuntime = R2D::VulkanSamplerRuntime<Provider, Dim>;
using BindlessTable = R2D::VulkanBindlessTextureTable<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<BindlessTable>);
static_assert(R2D::StrictPodComponent<R2D::VulkanBindlessTextureTableConfig>);

static R2D::VulkanBindlessCapability makeSupportedCapability(R2D::U32 max_sampled_images_) noexcept
{
    R2D::VulkanBindlessCapability capability{};
    capability.supported = R2D::kVulkanBindlessSupported;
    capability.partially_bound = 1U;
    capability.runtime_descriptor_array = 1U;
    capability.sampled_image_non_uniform_indexing = 1U;
    capability.sampled_image_update_after_bind = 1U;
    capability.max_per_stage_sampled_images = max_sampled_images_;
    capability.max_descriptor_set_sampled_images = max_sampled_images_;
    capability.api_version = VK_API_VERSION_1_2;
    return capability;
}

// Without bindless support the table refuses to initialize (UnsupportedDomain,
// not an error) so the caller falls back to the per-packet sampler path.
void testUnsupportedCapabilityRefused()
{
    BindlessTable table;
    const R2D::VulkanBindlessCapability unsupported{};
    const auto result = table.initialize(
        {
            .device = VK_NULL_HANDLE,
            .texture_capacity = 16U,
            .sampler_capacity = 4U,
            .sampled_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .debug_fill_view = VK_NULL_HANDLE,
        },
        unsupported);
    assert(result.code == R2D::NativeStatusCode::UnsupportedDomain);
    assert(!table.isInitialized());
}

// A supported capability still needs a real device and a sane, in-range capacity;
// these are caught before any Vulkan call, so they run device-free.
void testInvalidConfigRejected()
{
    const R2D::VulkanBindlessCapability supported = makeSupportedCapability(8U);

    BindlessTable null_device;
    auto result = null_device.initialize(
        {
            .device = VK_NULL_HANDLE,
            .texture_capacity = 4U,
            .sampler_capacity = 4U,
            .sampled_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .debug_fill_view = VK_NULL_HANDLE,
        },
        supported);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(!null_device.isInitialized());

    BindlessTable zero_capacity;
    result = zero_capacity.initialize(
        {
            .device = VK_NULL_HANDLE,
            .texture_capacity = 0U,
            .sampler_capacity = 4U,
            .sampled_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .debug_fill_view = VK_NULL_HANDLE,
        },
        supported);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
}

// Full residency lifecycle on a real device. Gracefully skips when there is no
// device or no bindless support (CI / non-descriptor-indexing GPUs).
void testBindlessTableLifecycle(const Render2DTest::VulkanSmokeContext& context_)
{
    const R2D::VulkanBindlessCapability capability =
        R2D::queryVulkanBindlessCapability(context_.physical_device);
    if (capability.supported != R2D::kVulkanBindlessSupported) {
        return;
    }
    assert(context_.supports_bindless);

    ResourceRuntime resource_runtime;
    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.reserveImages(4U).code == R2D::NativeStatusCode::Ok);

    SamplerRuntime sampler_runtime;
    assert(sampler_runtime.initialize({.device = context_.device}).code == R2D::NativeStatusCode::Ok);
    assert(sampler_runtime.reserveSamplers(1U).code == R2D::NativeStatusCode::Ok);
    SamplerRef sampler_ref{};
    result = sampler_runtime.createSamplerRef(
        {
            .mag_filter = static_cast<R2D::U32>(VK_FILTER_LINEAR),
            .min_filter = static_cast<R2D::U32>(VK_FILTER_LINEAR),
            .mipmap_mode = static_cast<R2D::U32>(VK_SAMPLER_MIPMAP_MODE_NEAREST),
            .address_mode_u = static_cast<R2D::U32>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
            .address_mode_v = static_cast<R2D::U32>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
            .address_mode_w = static_cast<R2D::U32>(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
            .flags = 0U,
        },
        sampler_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    VkSampler sampler = VK_NULL_HANDLE;
    assert(sampler_runtime.resolveNativeSampler(sampler_ref, sampler).code == R2D::NativeStatusCode::Ok);
    assert(sampler != VK_NULL_HANDLE);

    const auto sampled_usage =
        static_cast<R2D::U32>(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    ImageRef texture_a{};
    ImageRef texture_b{};
    ImageRef texture_stale{};
    assert(resource_runtime.createImageRef(2U, 2U, VK_FORMAT_R8G8B8A8_UNORM, sampled_usage, texture_a).code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.createImageRef(2U, 2U, VK_FORMAT_R8G8B8A8_UNORM, sampled_usage, texture_b).code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.createImageRef(2U, 2U, VK_FORMAT_R8G8B8A8_UNORM, sampled_usage, texture_stale).code == R2D::NativeStatusCode::Ok);

    const R2D::U32 capacity = capability.max_descriptor_set_sampled_images < 64U ?
        capability.max_descriptor_set_sampled_images :
        64U;
    constexpr R2D::U32 kSamplerCapacity = 8U;

    BindlessTable table;
    result = table.initialize(
        {
            .device = context_.device,
            .texture_capacity = capacity,
            .sampler_capacity = kSamplerCapacity,
            .sampled_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .debug_fill_view = VK_NULL_HANDLE,
        },
        capability);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(table.isInitialized());
    assert(table.nativeDescriptorSet() != VK_NULL_HANDLE);
    assert(table.nativeDescriptorSetLayout() != VK_NULL_HANDLE);
    assert(table.textureCapacity() == capacity);
    assert(table.samplerCapacity() == kSamplerCapacity);

    // Capacity beyond the probed device limit is rejected up front (CPU check,
    // no Vulkan object created). Guard the +1 against U32 overflow.
    if (capability.max_descriptor_set_sampled_images < 0xFFFFFFFFU) {
        BindlessTable too_big;
        const auto over = too_big.initialize(
            {
                .device = context_.device,
                .texture_capacity = capability.max_descriptor_set_sampled_images + 1U,
                .sampler_capacity = kSamplerCapacity,
                .sampled_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .debug_fill_view = VK_NULL_HANDLE,
            },
            capability);
        assert(over.code == R2D::NativeStatusCode::InvalidInput);
        assert(!too_big.isInitialized());
    }

    // Samplers and identity-indexed residency.
    assert(table.setSampler(0U, sampler).code == R2D::NativeStatusCode::Ok);
    assert(table.setSampler(kSamplerCapacity, sampler).code == R2D::NativeStatusCode::InvalidInput); // out of range

    assert(table.setResident(0U, texture_a.generation, texture_a, resource_runtime).code == R2D::NativeStatusCode::Ok);
    assert(table.setResident(5U, texture_b.generation, texture_b, resource_runtime).code == R2D::NativeStatusCode::Ok);

    assert(table.isResident(0U, texture_a.generation));
    assert(table.isResident(5U, texture_b.generation));
    assert(!table.isResident(1U, texture_a.generation));               // never made resident
    assert(!table.isResident(0U, texture_a.generation + 1U));          // generation mismatch
    assert(table.setResident(capacity, texture_a.generation, texture_a, resource_runtime).code == R2D::NativeStatusCode::InvalidInput); // id out of range

    // A stale ImageRef cannot be made resident.
    assert(resource_runtime.releaseImageRef(texture_stale).code == R2D::NativeStatusCode::Ok);
    assert(table.setResident(6U, texture_stale.generation, texture_stale, resource_runtime).code == R2D::NativeStatusCode::StaleReference);
    assert(!table.isResident(6U, texture_stale.generation));

    // Evict: stale generation rejected, matching generation succeeds, then the
    // slot is no longer resident.
    assert(table.evict(0U, texture_a.generation + 1U).code == R2D::NativeStatusCode::StaleReference);
    assert(table.evict(0U, texture_a.generation).code == R2D::NativeStatusCode::Ok);
    assert(!table.isResident(0U, texture_a.generation));
    assert(table.evict(0U, texture_a.generation).code == R2D::NativeStatusCode::StaleReference); // double evict

    // Re-residency of the same id with a fresh image works.
    assert(table.setResident(0U, texture_b.generation, texture_b, resource_runtime).code == R2D::NativeStatusCode::Ok);
    assert(table.isResident(0U, texture_b.generation));

    // Tear down the descriptor set before releasing the images it references.
    table.shutdown();
    assert(!table.isInitialized());
    assert(resource_runtime.releaseImageRef(texture_a).code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.releaseImageRef(texture_b).code == R2D::NativeStatusCode::Ok);
    assert(sampler_runtime.releaseSamplerRef(sampler_ref).code == R2D::NativeStatusCode::Ok);
}

int main()
{
    try {
        testUnsupportedCapabilityRefused();
        testInvalidConfigRejected();

        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testBindlessTableLifecycle(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
