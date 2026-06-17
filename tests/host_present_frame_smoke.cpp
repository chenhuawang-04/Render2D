// Stage 23D (host-engine merge readiness -- capstone): drive a full frame from
// HOST-SHAPED ECS data all the way to an on-screen swapchain present, using the
// REAL sprite GPU path -- the one end-to-end proof that ties the whole library
// together for merge.
//
// The thread is: HostLikeEcs (Stage 23A, a host-shaped SoA archetype + frame
// arena) -> the span-only CPU systems (SpatialCull -> CommandBuild ->
// SpriteInstanceBuild -> Batch) -> real SpriteInstance[] -> the real sprite
// instanced draw (VulkanSpriteRenderEncoder) into an offscreen image -> the
// Stage 22 present tail (copy onto the acquired swapchain image, present) ->
// readback. The sprite colors on screen are literally the host scene's
// Sprite.color_rgba8 carried the whole way down through the span boundary.
//
// What is novel here (nothing else in the repo does it): the real sprite draw
// reaches a SWAPCHAIN. Stage 12/13/16 sprite draws are offscreen-readback only;
// Stage 22's present draws are gradients, not the sprite pipeline. 23D closes
// ProjectMergeTODO #9 ("current visible proof is offscreen, not swapchain-
// presented") for the sprite path, and exercises #1 (span boundary, fed from a
// host-shaped store), #24-#28 (frame/present/swapchain/acquire), and #29/#32
// (host SpriteInstance[] -> color-only encoder -> presented) from host data to
// frame.
//
// Within ONE command buffer / ONE submission (the 22D idiom, render swapped from
// gradient to sprites):
//   1. render the host-built sprite instances into an offscreen image
//      (VulkanSpriteRenderEncoder), in the swapchain's exact format;
//   2. copy that offscreen image to a readback buffer -> the OFFSCREEN BASELINE;
//   3. copy the offscreen image onto the acquired swapchain image;
//   4. copy the swapchain image back to a second readback buffer -> the CAPTURE;
//   5. transition the swapchain image to PRESENT_SRC and present it.
// After the fence, assert CAPTURE == BASELINE byte-for-byte (present integrity)
// and that the baseline actually varies (the sprites drew -- not a blank frame).
//
// Scope boundary (deliberate): the repo's CPU pipeline stops at a WORLD-SPACE
// affine; clip-space projection is the host's vertex-shader concern. The
// color-only sprite shader applies the instance affine straight to the NDC quad
// verts with NO projection uniform (see SpriteShaders.hpp / ProjectMergeTODO
// #32), so this scene is authored directly in that clip space (camera = NDC box,
// visible sprites placed in NDC quadrants) rather than inventing a camera->clip
// projection Render2D does not own. The content reaches the swapchain by a raw
// vkCmdCopyImage on resolved handles (encoders resolve a VulkanResourceRuntime
// ImageRef; a swapchain image is not one) -- the same windowless-core-preserving
// idiom 22D uses. Every runtime + PresentHost stays byte-for-byte unchanged.
//
// Like the other Stage 22 present tests, every bring-up step is a graceful SKIP
// on a headless/limited box (no video driver, no Vulkan loader/ICD, no present-
// capable GPU, no dynamic rendering, or a surface that does not grant TRANSFER
// usage): we return context.result() and only assert on the real path.

#include <Render2D/Render2D.hpp>

#include <Render2D/Present/PresentHost.hpp>

#include "Render2D/Memory/RenderVector.hpp"
#include "support/HostLikeEcs.hpp"
#include "support/PresentBringup.hpp"
#include "support/SpriteShaders.hpp"
#include "support/TestHarness.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstring>
#include <exception>
#include <iostream>
#include <span>

namespace {

namespace R2D = Render2D;
namespace R2DT = Render2D::TestSupport;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;

using Transform = R2D::Transform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using Camera = R2D::Camera<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using SpriteVertex = R2D::SpriteVertex<Provider, Dim>;
using SpriteInstance = R2D::SpriteInstance<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;

using SpatialCull = R2D::SpatialCullSystem<Provider, Dim>;
using CommandBuild = R2D::CommandBuildSystem<Provider, Dim>;
using SpriteInstanceBuild = R2D::SpriteInstanceBuildSystem<Provider, Dim>;
using Batch = R2D::BatchSystem<Provider, Dim>;

using HostEntityTable = R2DT::HostEntityTable<Provider, Dim>;
using HostFrameArena = R2DT::HostFrameArena<Provider, Dim>;

using SwapchainRuntime = R2D::VulkanSwapchainRuntime<Provider, Dim>;
using PresentRuntime = R2D::VulkanPresentRuntime<Provider, Dim>;
using SyncRuntime = R2D::VulkanSyncRuntime<Provider, Dim>;
using CommandRuntime = R2D::VulkanCommandRuntime<Provider, Dim>;
using SubmitRuntime = R2D::VulkanSubmitRuntime<Provider, Dim>;
using ResourceRuntime = R2D::VulkanResourceRuntime<Provider, Dim>;
using PipelineRuntime = R2D::VulkanPipelineRuntime<Provider, Dim>;
using DescriptorRuntime = R2D::VulkanDescriptorRuntime<Provider, Dim>;
using SpritePipelineRuntime = R2D::VulkanSpritePipelineRuntime<Provider, Dim>;
using SpriteRenderEncoder = R2D::VulkanSpriteRenderEncoder<Provider, Dim>;
using SpriteRenderEncoderConfig = R2D::VulkanSpriteRenderEncoderConfig;

using SwapchainState = R2D::SwapchainState<Provider, Dim>;
using SwapchainImageRef = R2D::SwapchainImageRef<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using AcquiredImage = R2D::AcquiredImage<Provider, Dim>;
using PresentCommand = R2D::PresentCommand<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;

constexpr R2D::U32 kSwapchainImageCapacity = 16U;
constexpr R2D::U32 kBytesPerPixel = 4U; // B8G8R8A8 / R8G8B8A8 are 4 bytes/pixel
constexpr R2D::U64 kFenceTimeoutNs = 1'000'000'000ULL;
constexpr R2D::U64 kAcquireTimeoutNs = 1'000'000'000ULL;
constexpr R2D::U32 kClearColorRgba8 = 0x000000FFU; // opaque black; sprites draw over it

// --- The host scene, authored directly in the sprite shader's clip space ------
// Camera viewport 2x2 at the origin => an NDC [-1,1] cull box. Visible sprites
// sit in the four NDC quadrants at scale 0.30 (the full-screen quad mesh maps to
// a [pos +/- 0.30] rect), each a distinct opaque colour, so the frame is four
// colored rectangles on black -- deterministic, spatially varied, non-vacuous.
// The rest are culled: some pushed far off-screen (bounds cull), some masked off
// (layer cull). So visible_count (4) is strictly between 0 and the entity count.
constexpr R2D::U32 kVisibleSpriteCount = 4U;
constexpr R2D::U32 kEntityCount = 12U;
constexpr float kVisibleScale = 0.30F;
constexpr R2D::U32 kVisibleMask = 0xFFFF'FFFFU;
constexpr R2D::U32 kHiddenMask = 0U;
constexpr R2D::U32 kSharedTextureId = 1U;
constexpr R2D::U32 kSharedMaterialId = 1U;

constexpr Camera kCamera{
    .source_id = 0U,
    .position_x = 0.0F,
    .position_y = 0.0F,
    .rotation_radians = 0.0F,
    .viewport_width = 2.0F,
    .viewport_height = 2.0F,
    .near_z = 0.0F,
    .far_z = 1.0F,
    .layer_mask = kVisibleMask,
    .flags = 0U,
};

// rgba8 is consumed by an R8G8B8A8_UNORM vertex attribute (low byte = R), i.e.
// R | (G<<8) | (B<<16) | (A<<24). Four distinct opaque colours.
constexpr std::array<R2D::U32, kVisibleSpriteCount> kVisibleColors{{
    0xFF0000FFU, // red
    0xFF00FF00U, // green
    0xFFFF0000U, // blue
    0xFF00FFFFU, // yellow
}};
constexpr std::array<float, kVisibleSpriteCount> kVisibleX{{-0.45F, 0.45F, -0.45F, 0.45F}};
constexpr std::array<float, kVisibleSpriteCount> kVisibleY{{-0.45F, -0.45F, 0.45F, 0.45F}};

// One full-screen quad (two triangles) in NDC; the per-instance affine shrinks +
// translates it into the sprite's clip-space rect. Same mesh the offscreen
// sprite smoke uses.
constexpr SpriteVertex makeVertex(float position_x_, float position_y_, float uv_x_, float uv_y_) noexcept
{
    return {.position_x = position_x_, .position_y = position_y_, .uv_x = uv_x_, .uv_y = uv_y_};
}

constexpr std::array<SpriteVertex, 6U> kQuadVertices{{
    makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
    makeVertex(1.0F, -1.0F, 1.0F, 0.0F),
    makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
    makeVertex(-1.0F, -1.0F, 0.0F, 0.0F),
    makeVertex(1.0F, 1.0F, 1.0F, 1.0F),
    makeVertex(-1.0F, 1.0F, 0.0F, 1.0F),
}};

// Fill the host-shaped entity table with the NDC scene above.
void buildHostScene(HostEntityTable& table_)
{
    table_.reserve(kEntityCount);
    const LocalBounds local_bounds{.source_id = 0U, .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F)};
    const auto sprite_for = [](R2D::U32 source_id_, R2D::U32 color_rgba8_) noexcept {
        return Sprite{
            .source_id = source_id_,
            .texture_id = kSharedTextureId,
            .texture_generation = 1U,
            .texture_region_id = 0U,
            .texture_region_generation = 0U,
            .material_id = kSharedMaterialId,
            .material_generation = 1U,
            .color_rgba8 = color_rgba8_,
            .layer = 0U,
            .flags = 0U,
        };
    };

    for (R2D::U32 index = 0U; index < kEntityCount; ++index) {
        Transform transform{
            .source_id = index,
            .position_x = 0.0F,
            .position_y = 0.0F,
            .rotation_radians = 0.0F,
            .scale_x = kVisibleScale,
            .scale_y = kVisibleScale,
        };
        R2D::U32 mask = kVisibleMask;
        R2D::U32 color = 0xFF808080U;

        if (index < kVisibleSpriteCount) {
            // Visible: an NDC quadrant, distinct colour.
            transform.position_x = kVisibleX[index];
            transform.position_y = kVisibleY[index];
            color = kVisibleColors[index];
        } else if (index < kVisibleSpriteCount + 4U) {
            // Bounds-culled: far outside the NDC box.
            transform.position_x = 5000.0F + static_cast<float>(index);
            transform.position_y = 5000.0F;
            transform.scale_x = 1.0F;
            transform.scale_y = 1.0F;
        } else {
            // Mask-culled: inside the box but layer mask is zero.
            transform.position_x = 0.1F * static_cast<float>(index);
            transform.position_y = -0.1F * static_cast<float>(index);
            mask = kHiddenMask;
        }

        table_.pushEntity(transform, local_bounds, VisibilityMask{.mask = mask}, sprite_for(index, color));
    }
}

struct ChainCounts {
    R2D::SystemStatusCode code;
    R2D::U32 visible_count;
    R2D::U32 batch_count;
};

// Run the production CPU front-end + sprite chain, fed entirely through the host
// table's spans, into the host frame arena. Identical to the 23A adapter chain,
// here producing the SpriteInstance[] that the GPU will draw.
[[nodiscard]] ChainCounts runHostChain(const HostEntityTable& table_, HostFrameArena& arena_)
{
    arena_.resizeForEntities(table_.size());

    auto result = SpatialCull::run(
        kCamera,
        table_.transforms(),
        table_.localBounds(),
        table_.visibilityMasks(),
        arena_.worldTransforms(),
        arena_.visibleItems());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = 0U, .batch_count = 0U};
    }
    const R2D::U32 visible_count = result.write_count;

    result = CommandBuild::run(
        std::span<const VisibleItem>{arena_.visibleItems().data(), visible_count},
        table_.sprites(),
        arena_.drawCommands());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }

    result = SpriteInstanceBuild::run(
        std::span<const DrawCommand>{arena_.drawCommands().data(), visible_count},
        arena_.worldTransforms(),
        table_.sprites(),
        arena_.spriteInstances());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }

    result = Batch::run(
        std::span<const DrawCommand>{arena_.drawCommands().data(), visible_count},
        arena_.batchCommands());
    if (result.code != R2D::SystemStatusCode::Ok) {
        return {.code = result.code, .visible_count = visible_count, .batch_count = 0U};
    }

    return {.code = R2D::SystemStatusCode::Ok, .visible_count = visible_count, .batch_count = result.write_count};
}

// --- Swapchain-capture helpers (mirror present_visible_capture_smoke / 22D) ----
// 22D keeps these local rather than in PresentBringup (the capture swapchain's
// TRANSFER usage + ActiveSwapchain shape are specific to the readback tests), so
// 23D follows that precedent and keeps its own copies self-contained.

struct ActiveSwapchain {
    SwapchainState state{};
    std::array<SwapchainImageRef, kSwapchainImageCapacity> images{};
    VkExtent2D extent{};
    R2D::U32 format = 0U;
};

// Raw image-memory barrier for the swapchain image (not an ImageRef, so the
// resource runtime's transitionImageLayout does not apply). All fields explicit.
void recordSwapchainBarrier(
    VkCommandBuffer command_buffer_,
    VkImage image_,
    VkImageLayout old_layout_,
    VkImageLayout new_layout_,
    VkAccessFlags src_access_,
    VkAccessFlags dst_access_,
    VkPipelineStageFlags src_stage_,
    VkPipelineStageFlags dst_stage_) noexcept
{
    const VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0U,
        .levelCount = 1U,
        .baseArrayLayer = 0U,
        .layerCount = 1U,
    };
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = src_access_,
        .dstAccessMask = dst_access_,
        .oldLayout = old_layout_,
        .newLayout = new_layout_,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange = range,
    };
    vkCmdPipelineBarrier(command_buffer_, src_stage_, dst_stage_, 0U, 0U, nullptr, 0U, nullptr, 1U, &barrier);
}

// Create a single swapchain whose images carry COLOR_ATTACHMENT | TRANSFER_DST |
// TRANSFER_SRC. A zero extent (minimized) returns InvalidInput so the caller can
// skip gracefully.
[[nodiscard]] R2D::NativeStatusCode createCaptureSwapchain(
    VkPhysicalDevice physical_device_,
    R2D::PresentHost& present_host_,
    R2D::U32 queue_family_index_,
    R2D::U32 image_usage_,
    SwapchainRuntime& swapchain_runtime_,
    ActiveSwapchain& out_active_)
{
    out_active_ = {};
    const VkSurfaceKHR surface = present_host_.surfaceHandle();

    // Driver-filled; left uninitialized on purpose (same reasoning as 22B/22C/22D):
    // value-initializing `{}` would set the VkSurfaceTransformFlagBitsKHR member to
    // a non-enumerator zero, tripping a clang-tidy bugprone warning. Read only
    // after the VK_SUCCESS check.
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface, &caps) != VK_SUCCESS) {
        return R2D::NativeStatusCode::InvalidInput;
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFU) {
        R2D::U32 pixel_width = 0U;
        R2D::U32 pixel_height = 0U;
        if (!present_host_.pixelSize(pixel_width, pixel_height)) {
            return R2D::NativeStatusCode::InvalidInput;
        }
        extent.width = Render2DTest::clampU32(pixel_width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height =
            Render2DTest::clampU32(pixel_height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0U || extent.height == 0U) {
        return R2D::NativeStatusCode::InvalidInput;
    }

    VkSurfaceFormatKHR surface_format{};
    if (!Render2DTest::selectSurfaceFormat(physical_device_, surface, surface_format)) {
        return R2D::NativeStatusCode::InvalidInput;
    }

    R2D::U32 min_image_count = caps.minImageCount;
    if (caps.maxImageCount != 0U && min_image_count > caps.maxImageCount) {
        min_image_count = caps.maxImageCount;
    }
    if (min_image_count == 0U || min_image_count > kSwapchainImageCapacity) {
        return R2D::NativeStatusCode::InvalidInput;
    }

    const R2D::U32 queue_family_index = queue_family_index_;
    const auto create_result = swapchain_runtime_.createSwapchain(
        {
            .surface = surface,
            .old_swapchain = VK_NULL_HANDLE,
            .queue_family_indices = &queue_family_index,
            .queue_family_index_count = 1U,
            .min_image_count = min_image_count,
            .width = extent.width,
            .height = extent.height,
            .format = static_cast<R2D::U32>(surface_format.format),
            .color_space = static_cast<R2D::U32>(surface_format.colorSpace),
            .image_usage = image_usage_,
            .sharing_mode = static_cast<R2D::U32>(VK_SHARING_MODE_EXCLUSIVE),
            .pre_transform = static_cast<R2D::U32>(caps.currentTransform),
            .composite_alpha = static_cast<R2D::U32>(Render2DTest::selectCompositeAlpha(caps.supportedCompositeAlpha)),
            .present_mode = static_cast<R2D::U32>(VK_PRESENT_MODE_FIFO_KHR),
            .clipped = 1U,
            .image_array_layers = 1U,
            .image_view_type = static_cast<R2D::U32>(VK_IMAGE_VIEW_TYPE_2D),
            .image_aspect_flags = static_cast<R2D::U32>(VK_IMAGE_ASPECT_COLOR_BIT),
            .flags = 0U,
        },
        out_active_.state,
        out_active_.images);
    if (create_result.code == R2D::NativeStatusCode::Ok) {
        out_active_.extent = extent;
        out_active_.format = static_cast<R2D::U32>(surface_format.format);
    }
    return create_result.code;
}

// Record the whole capture frame: render the host-built sprite instances into the
// offscreen image, read it back (baseline), copy it onto the swapchain image, read
// that back (capture), and leave the swapchain image in PRESENT_SRC. Returns false
// if any native handle fails to resolve.
[[nodiscard]] bool recordSpriteCaptureFrame(
    R2DT::TestContext& context_,
    const NativeCommandBufferRef& command_ref_,
    const ImageRef& offscreen_image_,
    const PipelineRef& pipeline_ref_,
    const BufferRef& vertex_buffer_,
    const BufferRef& instance_buffer_,
    const BufferRef& baseline_buffer_,
    const BufferRef& capture_buffer_,
    R2D::U32 instance_count_,
    VkImage swapchain_image_,
    VkExtent2D extent_,
    CommandRuntime& command_runtime_,
    ResourceRuntime& resource_runtime_,
    PipelineRuntime& pipeline_runtime_,
    DescriptorRuntime& descriptor_runtime_)
{
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.resolveNativeCommandBuffer(command_ref_, command_buffer).code,
        R2D::NativeStatusCode::Ok);
    VkImage offscreen_image = VK_NULL_HANDLE;
    VkImageView offscreen_image_view = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.resolveNativeImage(offscreen_image_, offscreen_image, offscreen_image_view).code,
        R2D::NativeStatusCode::Ok);
    VkBuffer capture_buffer = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.resolveNativeBuffer(capture_buffer_, capture_buffer).code,
        R2D::NativeStatusCode::Ok);
    if (!context_.ok() || command_buffer == VK_NULL_HANDLE || offscreen_image == VK_NULL_HANDLE ||
        capture_buffer == VK_NULL_HANDLE) {
        return false;
    }

    const SpriteRenderEncoderConfig encoder_config{
        .vertex_buffer_offset = 0U,
        .instance_buffer_offset = 0U,
        .width = extent_.width,
        .height = extent_.height,
        .clear_color_rgba8 = kClearColorRgba8,
        .vertex_count = static_cast<R2D::U32>(kQuadVertices.size()),
        .instance_count = instance_count_,
        .first_vertex = 0U,
        .first_instance = 0U,
        .flags = 0U,
    };

    const VkImageCopy image_copy{
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0U,
            .baseArrayLayer = 0U,
            .layerCount = 1U,
        },
        .srcOffset = {.x = 0, .y = 0, .z = 0},
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0U,
            .baseArrayLayer = 0U,
            .layerCount = 1U,
        },
        .dstOffset = {.x = 0, .y = 0, .z = 0},
        .extent = {.width = extent_.width, .height = extent_.height, .depth = 1U},
    };
    const VkBufferImageCopy buffer_copy{
        .bufferOffset = 0U,
        .bufferRowLength = 0U,
        .bufferImageHeight = 0U,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0U,
            .baseArrayLayer = 0U,
            .layerCount = 1U,
        },
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent = {.width = extent_.width, .height = extent_.height, .depth = 1U},
    };

    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.beginCommandBuffer(command_ref_, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT).code,
        R2D::NativeStatusCode::Ok);

    // 1. Render the host-built sprite instances into the offscreen image. The
    //    color-only sprite path binds no descriptor sets (empty slice span);
    //    each instance carries its own affine + colour from the host scene.
    R2D_TEST_CHECK_EQ(
        context_,
        SpriteRenderEncoder::record(
            command_ref_,
            offscreen_image_,
            pipeline_ref_,
            vertex_buffer_,
            instance_buffer_,
            std::span<const DescriptorSlice>{},
            command_runtime_,
            resource_runtime_,
            pipeline_runtime_,
            descriptor_runtime_,
            encoder_config)
            .code,
        R2D::NativeStatusCode::Ok);

    // 2. Offscreen -> TRANSFER_SRC, then copy to the baseline readback buffer.
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_
            .transitionImageLayout(
                command_buffer,
                offscreen_image_,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT)
            .code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_.recordCopyImageToBuffer(command_buffer, offscreen_image_, baseline_buffer_).code,
        R2D::NativeStatusCode::Ok);

    // 3. Swapchain UNDEFINED -> TRANSFER_DST, then copy the offscreen image onto it.
    recordSwapchainBarrier(
        command_buffer,
        swapchain_image_,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0U,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImage(
        command_buffer,
        offscreen_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1U,
        &image_copy);

    // 4. Swapchain TRANSFER_DST -> TRANSFER_SRC, then read it back (capture).
    recordSwapchainBarrier(
        command_buffer,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyImageToBuffer(
        command_buffer,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        capture_buffer,
        1U,
        &buffer_copy);

    // 5. Swapchain TRANSFER_SRC -> PRESENT_SRC for presentation.
    recordSwapchainBarrier(
        command_buffer,
        swapchain_image_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_READ_BIT,
        0U,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    // 6. Make both readback buffers host-visible.
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_
            .recordBufferBarrier(
                command_buffer,
                baseline_buffer_,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_HOST_READ_BIT)
            .code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime_
            .recordBufferBarrier(
                command_buffer,
                capture_buffer_,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_HOST_READ_BIT)
            .code,
        R2D::NativeStatusCode::Ok);

    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime_.endCommandBuffer(command_ref_).code,
        R2D::NativeStatusCode::Ok);
    return context_.ok();
}

// Bring up the GPU runtimes, build the sprite pipeline, run the recorded frame,
// and assert the swapchain readback equals the offscreen baseline. Runtimes are
// locals so their destructors tear down Vulkan objects while device_ is alive.
void runHostPresentFrame(
    R2DT::TestContext& context_,
    R2D::PresentHost& present_host_,
    VkPhysicalDevice physical_device_,
    VkDevice device_,
    R2D::U32 queue_family_index_,
    R2D::U32 instance_count_,
    std::span<const SpriteInstance> instances_)
{
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device_, queue_family_index_, 0U, &queue);
    if (queue == VK_NULL_HANDLE) {
        std::cout << "host-present 23D: no device queue, skipping\n";
        return;
    }

    // Copy-onto-swapchain + readback needs TRANSFER_DST and TRANSFER_SRC on the
    // swapchain images. Both are optional surface capabilities; skip if missing.
    VkSurfaceCapabilitiesKHR caps; // driver-filled; see createCaptureSwapchain note
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, present_host_.surfaceHandle(), &caps) !=
        VK_SUCCESS) {
        std::cout << "host-present 23D: surface capabilities query failed, skipping\n";
        return;
    }
    constexpr VkImageUsageFlags kRequiredUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if ((caps.supportedUsageFlags & kRequiredUsage) != kRequiredUsage) {
        std::cout << "host-present 23D: surface lacks TRANSFER_SRC/DST swapchain usage, skipping\n";
        return;
    }
    const R2D::U32 image_usage =
        static_cast<R2D::U32>(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) | static_cast<R2D::U32>(kRequiredUsage);

    SwapchainRuntime swapchain_runtime{};
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.initialize({.device = device_}).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.reserveSwapchains(1U).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        swapchain_runtime.reserveSwapchainImages(kSwapchainImageCapacity).code,
        R2D::NativeStatusCode::Ok);

    PresentRuntime present_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        present_runtime.initialize({.device = device_, .present_queue = queue}).code,
        R2D::NativeStatusCode::Ok);

    SyncRuntime sync_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        sync_runtime.initialize({.device = device_, .fence_create_flags = 0U}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, sync_runtime.reserveFrameSyncs(1U).code, R2D::NativeStatusCode::Ok);
    FrameSync frame_sync{};
    R2D_TEST_CHECK_EQ(context_, sync_runtime.createFrameSync(0U, 0U, frame_sync).code, R2D::NativeStatusCode::Ok);

    CommandRuntime command_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime
            .initialize({.device = device_, .queue_family_index = queue_family_index_, .command_pool_flags = 0U})
            .code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, command_runtime.reserveCommandBuffers(1U).code, R2D::NativeStatusCode::Ok);
    NativeCommandBufferRef command_ref{};
    R2D_TEST_CHECK_EQ(
        context_,
        command_runtime
            .allocateCommandBufferRef(0U, {.first = 0U, .count = 0U}, {.first = 0U, .count = 0U}, 0U, command_ref)
            .code,
        R2D::NativeStatusCode::Ok);

    SubmitRuntime submit_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        submit_runtime.initialize({.queue = queue, .wait_stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, submit_runtime.reserveCommandBuffers(1U).code, R2D::NativeStatusCode::Ok);

    ResourceRuntime resource_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime.initialize({.physical_device = physical_device_, .device = device_}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.reserveImages(1U).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.reserveBuffers(4U).code, R2D::NativeStatusCode::Ok);

    PipelineRuntime pipeline_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        pipeline_runtime.initialize({.device = device_, .pipeline_cache_flags = 0U}).code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.reservePipelines(1U).code, R2D::NativeStatusCode::Ok);

    DescriptorRuntime descriptor_runtime{};
    R2D_TEST_CHECK_EQ(
        context_,
        descriptor_runtime
            .initialize(SpritePipelineRuntime::makeDescriptorRuntimeConfig(
                device_,
                1U,
                R2D::kVulkanSpriteTextureDescriptorCount,
                0U,
                0U))
            .code,
        R2D::NativeStatusCode::Ok);

    if (!context_.ok()) {
        return;
    }

    ActiveSwapchain active{};
    if (createCaptureSwapchain(physical_device_, present_host_, queue_family_index_, image_usage, swapchain_runtime, active) !=
        R2D::NativeStatusCode::Ok) {
        std::cout << "host-present 23D: swapchain unavailable (e.g. minimized), skipping\n";
        return;
    }

    const R2D::U64 byte_count =
        static_cast<R2D::U64>(active.extent.width) * active.extent.height * kBytesPerPixel;
    const auto vertex_byte_count = static_cast<R2D::U64>(sizeof(kQuadVertices));
    const auto instance_byte_count = static_cast<R2D::U64>(instance_count_) * sizeof(SpriteInstance);

    // Offscreen render target, in the swapchain's exact format (byte-exact copy).
    ImageRef offscreen_image{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createImageRef(
                active.extent.width,
                active.extent.height,
                active.format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                offscreen_image)
            .code,
        R2D::NativeStatusCode::Ok);

    BufferRef vertex_buffer{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createBufferRef(vertex_byte_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, R2D::NativeMemoryDomain::Upload, vertex_buffer)
            .code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime.writeBuffer(vertex_buffer, kQuadVertices.data(), vertex_byte_count, 0U).code,
        R2D::NativeStatusCode::Ok);

    // The instance buffer IS the host-built SpriteInstance[] -- the data path.
    BufferRef instance_buffer{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createBufferRef(instance_byte_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, R2D::NativeMemoryDomain::Upload, instance_buffer)
            .code,
        R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime.writeBuffer(instance_buffer, instances_.data(), instance_byte_count, 0U).code,
        R2D::NativeStatusCode::Ok);

    BufferRef baseline_buffer{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createBufferRef(byte_count, VK_BUFFER_USAGE_TRANSFER_DST_BIT, R2D::NativeMemoryDomain::Readback, baseline_buffer)
            .code,
        R2D::NativeStatusCode::Ok);
    BufferRef capture_buffer{};
    R2D_TEST_CHECK_EQ(
        context_,
        resource_runtime
            .createBufferRef(byte_count, VK_BUFFER_USAGE_TRANSFER_DST_BIT, R2D::NativeMemoryDomain::Readback, capture_buffer)
            .code,
        R2D::NativeStatusCode::Ok);

    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        pipeline_runtime.createShaderModule(Render2DTest::kSpriteVertSpv, vertex_shader).code,
        R2D::NativeStatusCode::Ok);
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        pipeline_runtime.createShaderModule(Render2DTest::kSpriteFragSpv, fragment_shader).code,
        R2D::NativeStatusCode::Ok);

    PipelineRef pipeline_ref{};
    R2D_TEST_CHECK_EQ(
        context_,
        SpritePipelineRuntime::createPipelineRef(
            pipeline_runtime,
            {
                .vertex_shader = vertex_shader,
                .fragment_shader = fragment_shader,
                .descriptor_set_layout = descriptor_runtime.nativeDescriptorSetLayout(),
                .color_format = active.format,
                .flags = 0U,
            },
            pipeline_ref)
            .code,
        R2D::NativeStatusCode::Ok);

    if (!context_.ok()) {
        return;
    }

    // Acquire signals image_available; submit signals the fence; we wait on it.
    AcquiredImage acquired{};
    const auto acquire_result = present_runtime.acquireNextImage(
        active.state,
        frame_sync,
        swapchain_runtime,
        sync_runtime,
        kAcquireTimeoutNs,
        0U,
        acquired);
    if (acquire_result.code != R2D::NativeStatusCode::Ok) {
        std::cout << "host-present 23D: image acquire did not succeed (code "
                  << static_cast<int>(acquire_result.code) << "), skipping\n";
        return;
    }
    R2D_TEST_CHECK(context_, acquired.image_index < active.state.image_count);
    if (acquired.image_index >= active.state.image_count) {
        return;
    }

    VkImage swapchain_image = VK_NULL_HANDLE;
    VkImageView swapchain_image_view = VK_NULL_HANDLE;
    R2D_TEST_CHECK_EQ(
        context_,
        swapchain_runtime.resolveNativeSwapchainImage(active.images[acquired.image_index], swapchain_image, swapchain_image_view)
            .code,
        R2D::NativeStatusCode::Ok);
    if (!context_.ok() || swapchain_image == VK_NULL_HANDLE) {
        return;
    }

    if (recordSpriteCaptureFrame(
            context_,
            command_ref,
            offscreen_image,
            pipeline_ref,
            vertex_buffer,
            instance_buffer,
            baseline_buffer,
            capture_buffer,
            instance_count_,
            swapchain_image,
            active.extent,
            command_runtime,
            resource_runtime,
            pipeline_runtime,
            descriptor_runtime)) {
        const std::array<NativeCommandBufferRef, 1U> command_refs{command_ref};
        const auto submit_result = submit_runtime.submit(
            command_refs,
            frame_sync,
            command_runtime,
            sync_runtime,
            R2D::kVulkanSubmitWaitImageAvailable | R2D::kVulkanSubmitSignalRenderFinished);
        R2D_TEST_CHECK_EQ(context_, submit_result.code, R2D::NativeStatusCode::Ok);

        if (submit_result.code == R2D::NativeStatusCode::Ok) {
            const PresentCommand present_command{
                .swapchain_id = acquired.swapchain_id,
                .image_index = acquired.image_index,
                .wait_sync_id = frame_sync.sync_id,
                .wait_sync_generation = frame_sync.generation,
                .frame_index = frame_sync.frame_index,
                .generation = acquired.generation,
                .flags = 0U,
            };
            const auto present_result = present_runtime.present(present_command, swapchain_runtime, sync_runtime);
            // The capture buffer was filled before the present transition, so a
            // benign OUT_OF_DATE on present does not invalidate the comparison.
            if (present_result.code != R2D::NativeStatusCode::Ok &&
                present_result.code != R2D::NativeStatusCode::SwapchainOutOfDate) {
                R2D_TEST_CHECK_EQ(context_, present_result.code, R2D::NativeStatusCode::Ok);
            }

            const auto wait_result = sync_runtime.waitFence(frame_sync, kFenceTimeoutNs);
            R2D_TEST_CHECK_EQ(context_, wait_result.code, R2D::NativeStatusCode::Ok);

            if (wait_result.code == R2D::NativeStatusCode::Ok) {
                R2D::McVector<R2D::U8> baseline_pixels(static_cast<R2D::Usize>(byte_count));
                R2D::McVector<R2D::U8> capture_pixels(static_cast<R2D::Usize>(byte_count));
                R2D_TEST_CHECK_EQ(
                    context_,
                    resource_runtime.readBuffer(baseline_buffer, baseline_pixels.data(), byte_count, 0U).code,
                    R2D::NativeStatusCode::Ok);
                R2D_TEST_CHECK_EQ(
                    context_,
                    resource_runtime.readBuffer(capture_buffer, capture_pixels.data(), byte_count, 0U).code,
                    R2D::NativeStatusCode::Ok);

                // The host sprites must actually have drawn: the frame varies
                // (four colored rects on black), not a blank clear.
                bool baseline_varies = false;
                for (R2D::Usize i = 1U; i < baseline_pixels.size(); ++i) {
                    if (baseline_pixels[i] != baseline_pixels[0]) {
                        baseline_varies = true;
                        break;
                    }
                }
                R2D_TEST_CHECK(context_, baseline_varies);

                const bool identical =
                    std::memcmp(baseline_pixels.data(), capture_pixels.data(), static_cast<R2D::Usize>(byte_count)) == 0;
                R2D_TEST_CHECK(context_, identical);

                std::cout << "host-present 23D: " << active.extent.width << "x" << active.extent.height
                          << " host scene -> " << instance_count_ << " sprite instances presented; swapchain "
                          << (identical ? "==" : "!=") << " offscreen baseline (" << byte_count << " bytes)\n";
            }
        }
    }

    vkDeviceWaitIdle(device_);

    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.releasePipelineRef(pipeline_ref).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.destroyShaderModule(fragment_shader).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, pipeline_runtime.destroyShaderModule(vertex_shader).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(capture_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(baseline_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(instance_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseBufferRef(vertex_buffer).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, resource_runtime.releaseImageRef(offscreen_image).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, command_runtime.releaseCommandBufferRef(command_ref).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, swapchain_runtime.releaseSwapchainState(active.state).code, R2D::NativeStatusCode::Ok);
    R2D_TEST_CHECK_EQ(context_, sync_runtime.releaseFrameSync(frame_sync).code, R2D::NativeStatusCode::Ok);
}

[[nodiscard]] int runTest()
{
    R2DT::TestContext context{};

    // --- CPU: host-shaped ECS data -> the span-only chain -> SpriteInstance[] ---
    // This part needs no device; it is the merge contract (#1) being exercised.
    HostEntityTable host_table{};
    buildHostScene(host_table);

    HostFrameArena arena{};
    const ChainCounts counts = runHostChain(host_table, arena);
    R2D_TEST_CHECK_EQ(context, static_cast<int>(counts.code), static_cast<int>(R2D::SystemStatusCode::Ok));
    // Culling is non-vacuous (some visible, some culled) and batching merged the
    // shared-key visible draws.
    R2D_TEST_CHECK_EQ(context, counts.visible_count, kVisibleSpriteCount);
    R2D_TEST_CHECK(context, counts.visible_count < kEntityCount);
    R2D_TEST_CHECK(context, counts.batch_count > 0U);
    R2D_TEST_CHECK(context, counts.batch_count <= counts.visible_count);
    if (!context.ok()) {
        return context.result();
    }

    const std::span<const SpriteInstance> instances{arena.spriteInstances().data(), counts.visible_count};

    // --- GPU bring-up (graceful skip at every gate on a headless/limited box) ---
    R2D::PresentHost present_host{};
    if (!present_host.initialize("Render2D Host Present Frame 23D", 640, 480, true)) {
        std::cout << "host-present 23D: no usable video driver, skipping (" << present_host.lastError() << ")\n";
        return context.result();
    }

    const auto extensions = present_host.requiredInstanceExtensions();
    if (extensions.empty()) {
        std::cout << "host-present 23D: no Vulkan instance extensions, skipping\n";
        return context.result();
    }

    VkInstance instance = VK_NULL_HANDLE;
    R2D::U32 api_version = 0U;
    if (Render2DTest::createPresentRenderInstance(extensions, instance, api_version) != VK_SUCCESS ||
        instance == VK_NULL_HANDLE) {
        std::cout << "host-present 23D: no Vulkan instance, skipping\n";
        return context.result();
    }

    if (!present_host.createSurface(instance)) {
        std::cout << "host-present 23D: surface creation failed, skipping (" << present_host.lastError() << ")\n";
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    R2D::U32 queue_family_index = 0U;
    if (!Render2DTest::selectPresentDevice(instance, present_host.surfaceHandle(), physical_device, queue_family_index)) {
        std::cout << "host-present 23D: no present-capable device, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    if (!Render2DTest::presentDeviceSupportsDynamicRendering(physical_device)) {
        std::cout << "host-present 23D: device lacks dynamic rendering, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    VkDevice device = VK_NULL_HANDLE;
    if (Render2DTest::createPresentRenderDevice(physical_device, queue_family_index, device) != VK_SUCCESS ||
        device == VK_NULL_HANDLE) {
        std::cout << "host-present 23D: device creation failed, skipping\n";
        present_host.destroySurface();
        vkDestroyInstance(instance, nullptr);
        return context.result();
    }

    runHostPresentFrame(context, present_host, physical_device, device, queue_family_index, counts.visible_count, instances);

    // Teardown order: runtimes (inside runHostPresentFrame) -> surface -> device ->
    // instance -> window/SDL (present_host destructor).
    present_host.destroySurface();
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return context.result();
}

} // namespace

int main() noexcept
{
    try {
        return runTest();
    } catch (const std::exception& exception) {
        std::fputs("host_present_frame_smoke exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("host_present_frame_smoke unknown exception\n", stderr);
        return 1;
    }
}
