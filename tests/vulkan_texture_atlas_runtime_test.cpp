#include <Render2D/Render2D.hpp>

#include "support/VulkanSmokeContext.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using DeferredDestroyCommand = R2D::DeferredDestroyCommand<Provider, Dim>;
using DeferredDestroyRuntime = R2D::NativeDeferredDestroyRuntime<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;
using AtlasRuntime = R2D::VulkanTextureAtlasRuntime<Provider, Dim>;
using AtlasRuntimeConfig = R2D::VulkanTextureAtlasRuntimeConfig<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<AtlasRuntime>);
static_assert(R2D::StrictPodComponent<AtlasRuntimeConfig>);
static_assert(R2D::StrictPodComponent<R2D::VulkanAtlasImageConfig>);
static_assert(R2D::StrictPodComponent<R2D::VulkanAtlasImageRef>);

void testInvalidConfig()
{
    AtlasRuntime atlas;
    auto result = atlas.initialize({.resource_runtime = nullptr});
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(!atlas.isInitialized());

    R2D::VulkanAtlasImageRef atlas_ref{};
    result = atlas.createAtlasImage(
        {.width = 8U, .height = 8U, .format = VK_FORMAT_R8G8B8A8_UNORM, .flags = 0U},
        atlas_ref);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
}

void testDelegationPropagatesWithoutDevice()
{
    ResourceRuntime resource_runtime; // intentionally left uninitialized
    AtlasRuntime atlas;

    auto result = atlas.initialize({.resource_runtime = &resource_runtime});
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(atlas.isInitialized());

    const auto capacity = atlas.reserveAtlases(2U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(atlas.atlasCapacity() >= 2U);

    R2D::VulkanAtlasImageRef atlas_ref{};
    result = atlas.createAtlasImage(
        {.width = 0U, .height = 8U, .format = VK_FORMAT_R8G8B8A8_UNORM, .flags = 0U},
        atlas_ref);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);

    // Valid args, but the backing resource runtime is uninitialized, so image
    // creation fails and the failure is propagated with no atlas slot consumed.
    result = atlas.createAtlasImage(
        {.width = 8U, .height = 8U, .format = VK_FORMAT_R8G8B8A8_UNORM, .flags = 0U},
        atlas_ref);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(atlas.atlasCount() == 0U);
}

void testAtlasImageLifecycle(const Render2DTest::VulkanSmokeContext& context_)
{
    ResourceRuntime resource_runtime;
    AtlasRuntime atlas;

    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    auto capacity = resource_runtime.reserveImages(2U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    result = atlas.initialize({.resource_runtime = &resource_runtime});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = atlas.reserveAtlases(2U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanAtlasImageRef atlas_ref{};
    result = atlas.createAtlasImage(
        {.width = 8U, .height = 8U, .format = VK_FORMAT_R8G8B8A8_UNORM, .flags = 0U},
        atlas_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(atlas_ref.atlas_id == 0U);
    assert(atlas_ref.generation == 1U);
    assert(atlas_ref.width == 8U);
    assert(atlas_ref.height == 8U);
    assert(atlas.atlasCount() == 1U);

    ImageRef backing_image{};
    result = atlas.resolveImageRef(atlas_ref, backing_image);
    assert(result.code == R2D::NativeStatusCode::Ok);

    VkImage native_image = VK_NULL_HANDLE;
    VkImageView native_view = VK_NULL_HANDLE;
    result = resource_runtime.resolveNativeImage(backing_image, native_image, native_view);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(native_image != VK_NULL_HANDLE);
    assert(native_view != VK_NULL_HANDLE);

    result = atlas.releaseAtlasImage(atlas_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(atlas.atlasCount() == 0U);

    result = atlas.resolveImageRef(atlas_ref, backing_image);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    // The slot id is reused with a bumped generation.
    R2D::VulkanAtlasImageRef reused_ref{};
    result = atlas.createAtlasImage(
        {.width = 4U, .height = 4U, .format = VK_FORMAT_R8G8B8A8_UNORM, .flags = 0U},
        reused_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_ref.atlas_id == 0U);
    assert(reused_ref.generation == 2U);
    assert(atlas.atlasCount() == 1U);

    atlas.shutdown();
    assert(!atlas.isInitialized());
    assert(atlas.atlasCount() == 0U);
}

// Composites two distinct 1x1 sub-images into a 2x1 atlas through
// recordUploadRegion, then reads the whole atlas back to prove each region
// landed in its own rectangle.
void testAtlasRegionUpload(const Render2DTest::VulkanSmokeContext& context_)
{
    ResourceRuntime resource_runtime;
    AtlasRuntime atlas;

    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    auto capacity = resource_runtime.reserveBuffers(2U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    capacity = resource_runtime.reserveImages(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    result = atlas.initialize({.resource_runtime = &resource_runtime});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = atlas.reserveAtlases(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanAtlasImageRef atlas_ref{};
    result = atlas.createAtlasImage(
        {.width = 2U, .height = 1U, .format = VK_FORMAT_R8G8B8A8_UNORM, .flags = 0U},
        atlas_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    ImageRef atlas_image{};
    result = atlas.resolveImageRef(atlas_ref, atlas_image);
    assert(result.code == R2D::NativeStatusCode::Ok);

    constexpr R2D::U64 kPixelBytes = 4U;
    constexpr R2D::U64 kSourceBytes = kPixelBytes * 2U;
    const std::array<R2D::U8, static_cast<R2D::Usize>(kSourceBytes)> source_pixels{
        0xFFU, 0x00U, 0x00U, 0xFFU, // red   -> region (0,0)
        0x00U, 0xFFU, 0x00U, 0xFFU, // green -> region (1,0)
    };

    BufferRef upload_buffer{};
    result = resource_runtime.createBufferRef(
        kSourceBytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        R2D::NativeMemoryDomain::Upload,
        upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.writeBuffer(upload_buffer, source_pixels.data(), kSourceBytes, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    BufferRef readback_buffer{};
    result = resource_runtime.createBufferRef(
        kSourceBytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        R2D::NativeMemoryDomain::Readback,
        readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanCommandRuntime<Provider, Dim> command_runtime;
    result = command_runtime.initialize({
        .device = context_.device,
        .queue_family_index = context_.queue_family_index,
        .command_pool_flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = command_runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    NativeCommandBufferRef command_ref{};
    result = command_runtime.allocateCommandBufferRef(
        0U,
        {.first = 0U, .count = 0U},
        {.first = 0U, .count = 0U},
        0U,
        command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = command_runtime.beginCommandBuffer(command_ref, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    result = command_runtime.resolveNativeCommandBuffer(command_ref, command_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = resource_runtime.transitionImageLayout(
        command_buffer,
        atlas_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = atlas.recordUploadRegion(command_buffer, atlas_ref, upload_buffer, 0U, 0U, 0U, 1U, 1U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = atlas.recordUploadRegion(command_buffer, atlas_ref, upload_buffer, kPixelBytes, 1U, 0U, 1U, 1U);
    assert(result.code == R2D::NativeStatusCode::Ok);

    // Region outside the atlas extent must be rejected.
    result = atlas.recordUploadRegion(command_buffer, atlas_ref, upload_buffer, 0U, 2U, 0U, 1U, 1U);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);

    result = resource_runtime.transitionImageLayout(
        command_buffer,
        atlas_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = resource_runtime.recordCopyImageToBuffer(command_buffer, atlas_image, readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    result = command_runtime.endCommandBuffer(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSyncRuntime<Provider, Dim> sync_runtime;
    result = sync_runtime.initialize({.device = context_.device, .fence_create_flags = 0U});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = sync_runtime.reserveFrameSyncs(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    result = sync_runtime.createFrameSync(0U, 0U, frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanSubmitRuntime<Provider, Dim> submit_runtime;
    result = submit_runtime.initialize({
        .queue = context_.queue,
        .wait_stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = submit_runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
    result = submit_runtime.submit(command_refs, frame_sync, command_runtime, sync_runtime, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.waitFence(frame_sync, 1'000'000'000ULL);
    assert(result.code == R2D::NativeStatusCode::Ok);

    std::array<R2D::U8, static_cast<R2D::Usize>(kSourceBytes)> readback_pixels{};
    result = resource_runtime.readBuffer(readback_buffer, readback_pixels.data(), kSourceBytes, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(readback_pixels == source_pixels);

    result = command_runtime.releaseCommandBufferRef(command_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = sync_runtime.releaseFrameSync(frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(upload_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = resource_runtime.releaseBufferRef(readback_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);

    atlas.shutdown();
    assert(atlas.atlasCount() == 0U);
}

// Stage 18D: retiring an atlas frees its handle immediately but defers the
// backing image through NativeDeferredDestroyRuntime; the image is only released
// once the retire frame completes and the drained command is handed back.
void testAtlasImageRetire(const Render2DTest::VulkanSmokeContext& context_)
{
    ResourceRuntime resource_runtime;
    AtlasRuntime atlas;
    DeferredDestroyRuntime deferred_runtime;

    auto result = resource_runtime.initialize({
        .physical_device = context_.physical_device,
        .device = context_.device,
    });
    assert(result.code == R2D::NativeStatusCode::Ok);
    auto capacity = resource_runtime.reserveImages(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    result = atlas.initialize({.resource_runtime = &resource_runtime});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = atlas.reserveAtlases(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    result = deferred_runtime.configure({.safe_frame_lag = 1U});
    assert(result.code == R2D::NativeStatusCode::Ok);
    capacity = deferred_runtime.reserveDestroyCommands(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    R2D::VulkanAtlasImageRef atlas_ref{};
    result = atlas.createAtlasImage(
        {.width = 8U, .height = 8U, .format = VK_FORMAT_R8G8B8A8_UNORM, .flags = 0U},
        atlas_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(atlas.atlasCount() == 1U);
    assert(resource_runtime.imageCount() == 1U);

    ImageRef backing{};
    result = atlas.resolveImageRef(atlas_ref, backing);
    assert(result.code == R2D::NativeStatusCode::Ok);
    const auto backing_image_id = backing.image_id;
    const auto backing_generation = backing.generation;

    // Retire at frame 0 with safe lag 1 -> the backing image retires at frame 1.
    result = atlas.retireAtlasImage(atlas_ref, deferred_runtime, 0U);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(atlas.atlasCount() == 0U);
    result = atlas.resolveImageRef(atlas_ref, backing);
    assert(result.code == R2D::NativeStatusCode::StaleReference);
    assert(resource_runtime.imageCount() == 1U); // backing image still alive
    assert(deferred_runtime.pendingCount() == 1U);

    std::array<DeferredDestroyCommand, 1U> drained{};
    auto drain = deferred_runtime.drainReady(0U, drained); // not yet at retire frame
    assert(drain.code == R2D::NativeStatusCode::Ok);
    assert(drain.drained_count == 0U);
    assert(deferred_runtime.pendingCount() == 1U);

    drain = deferred_runtime.drainReady(1U, drained);
    assert(drain.code == R2D::NativeStatusCode::Ok);
    assert(drain.drained_count == 1U);
    assert(drained[0U].object_kind == static_cast<R2D::U32>(R2D::NativeObjectKind::Image));
    assert(drained[0U].object_id == backing_image_id);
    assert(drained[0U].generation == backing_generation);

    // The drained command identifies the backing image; release it for real now.
    ImageRef retire_ref{};
    retire_ref.image_id = drained[0U].object_id;
    retire_ref.generation = drained[0U].generation;
    result = resource_runtime.releaseImageRef(retire_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resource_runtime.imageCount() == 0U);

    atlas.shutdown();
}

int main()
{
    try {
        testInvalidConfig();
        testDelegationPropagatesWithoutDevice();

        Render2DTest::VulkanSmokeContext context{};
        if (!Render2DTest::createVulkanSmokeContext(context)) {
            return 0;
        }

        testAtlasImageLifecycle(context);
        testAtlasRegionUpload(context);
        testAtlasImageRetire(context);
        Render2DTest::destroyVulkanSmokeContext(context);
    } catch (...) {
        return 1;
    }

    return 0;
}
