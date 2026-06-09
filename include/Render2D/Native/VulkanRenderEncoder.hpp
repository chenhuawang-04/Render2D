#pragma once

#include "Render2D/Component/Batch.hpp"
#include "Render2D/Native/VulkanCommandRuntime.hpp"
#include "Render2D/Native/VulkanPipelineRuntime.hpp"
#include "Render2D/Native/VulkanResourceRuntime.hpp"
#include "Render2D/Native/VulkanUploadRingRuntime.hpp"

#include <vulkan/vulkan.h>

#include <span>
#include <type_traits>

namespace Render2D {

struct VulkanDynamicRenderEncoderConfig {
    U32 width;
    U32 height;
    U32 clear_color_rgba8;
    U32 draw_vertex_count;
    U32 draw_instance_count;
    U32 flags;
};

template<class Provider, class Dim>
class VulkanDynamicRenderEncoder {
public:
    static NativeResult record(
        const NativeCommandBufferRef<Provider, Dim>& command_buffer_ref_,
        const ImageRef<Provider, Dim>& color_target_,
        const PipelineRef<Provider, Dim>& pipeline_ref_,
        std::span<const BatchCommand<Provider, Dim>> batch_commands_,
        const VulkanCommandRuntime<Provider, Dim>& command_runtime_,
        VulkanResourceRuntime<Provider, Dim>& resource_runtime_,
        const VulkanPipelineRuntime<Provider, Dim>& pipeline_runtime_,
        const VulkanDynamicRenderEncoderConfig& config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::CommandBuffer);
        } else {
            if (config_.width == 0U ||
                config_.height == 0U ||
                config_.draw_vertex_count == 0U ||
                batch_commands_.empty()) {
                return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::CommandBuffer);
            }

            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            NativeResult result = command_runtime_.resolveNativeCommandBuffer(
                command_buffer_ref_,
                command_buffer);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            VkImage color_image = VK_NULL_HANDLE;
            VkImageView color_image_view = VK_NULL_HANDLE;
            result = resource_runtime_.resolveNativeImage(
                color_target_,
                color_image,
                color_image_view);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
            result = pipeline_runtime_.resolveNativePipeline(
                pipeline_ref_,
                pipeline,
                pipeline_layout);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            result = resource_runtime_.transitionImageLayout(
                command_buffer,
                color_target_,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0U,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            const VkClearValue clear_value = makeClearValue(config_.clear_color_rgba8);
            const VkRenderingAttachmentInfo color_attachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .pNext = nullptr,
                .imageView = color_image_view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_NONE,
                .resolveImageView = VK_NULL_HANDLE,
                .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = clear_value,
            };
            const VkRenderingInfo rendering_info{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .pNext = nullptr,
                .flags = static_cast<VkRenderingFlags>(config_.flags),
                .renderArea = {
                    .offset = {.x = 0, .y = 0},
                    .extent = {.width = config_.width, .height = config_.height},
                },
                .layerCount = 1U,
                .viewMask = 0U,
                .colorAttachmentCount = 1U,
                .pColorAttachments = &color_attachment,
                .pDepthAttachment = nullptr,
                .pStencilAttachment = nullptr,
            };

            vkCmdBeginRendering(command_buffer, &rendering_info);
            const VkViewport viewport{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(config_.width),
                .height = static_cast<float>(config_.height),
                .minDepth = 0.0F,
                .maxDepth = 1.0F,
            };
            const VkRect2D scissor{
                .offset = {.x = 0, .y = 0},
                .extent = {.width = config_.width, .height = config_.height},
            };
            vkCmdSetViewport(command_buffer, 0U, 1U, &viewport);
            vkCmdSetScissor(command_buffer, 0U, 1U, &scissor);
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            for (const auto& batch : batch_commands_) {
                if (batch.draw_count != 0U) {
                    const U32 instance_count = config_.draw_instance_count == 0U ?
                        batch.draw_count :
                        config_.draw_instance_count;
                    vkCmdDraw(command_buffer, config_.draw_vertex_count, instance_count, 0U, 0U);
                }
            }

            vkCmdEndRendering(command_buffer);
            return makeResult(NativeStatusCode::Ok, NativeObjectKind::CommandBuffer);
        }
    }

    static NativeResult recordIndirect(
        const NativeCommandBufferRef<Provider, Dim>& command_buffer_ref_,
        const ImageRef<Provider, Dim>& color_target_,
        const PipelineRef<Provider, Dim>& pipeline_ref_,
        const UploadRingSlice<Provider, Dim>& indirect_slice_,
        std::span<const BatchCommand<Provider, Dim>> batch_commands_,
        const VulkanCommandRuntime<Provider, Dim>& command_runtime_,
        VulkanResourceRuntime<Provider, Dim>& resource_runtime_,
        const VulkanPipelineRuntime<Provider, Dim>& pipeline_runtime_,
        const VulkanUploadRingRuntime<Provider, Dim>& upload_ring_runtime_,
        const VulkanDynamicRenderEncoderConfig& config_,
        U32 indirect_draw_count_,
        U32 indirect_stride_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::CommandBuffer);
        } else {
            if (config_.width == 0U ||
                config_.height == 0U ||
                batch_commands_.empty() ||
                indirect_draw_count_ == 0U ||
                indirect_stride_ < sizeof(VkDrawIndirectCommand)) {
                return makeResult(NativeStatusCode::InvalidInput, NativeObjectKind::CommandBuffer);
            }

            VkCommandBuffer command_buffer = VK_NULL_HANDLE;
            NativeResult result = command_runtime_.resolveNativeCommandBuffer(
                command_buffer_ref_,
                command_buffer);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            VkImage color_image = VK_NULL_HANDLE;
            VkImageView color_image_view = VK_NULL_HANDLE;
            result = resource_runtime_.resolveNativeImage(
                color_target_,
                color_image,
                color_image_view);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
            result = pipeline_runtime_.resolveNativePipeline(
                pipeline_ref_,
                pipeline,
                pipeline_layout);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            VkBuffer indirect_buffer = VK_NULL_HANDLE;
            U64 indirect_offset = 0U;
            result = upload_ring_runtime_.resolveNativeBuffer(
                indirect_slice_,
                indirect_buffer,
                indirect_offset);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            result = resource_runtime_.transitionImageLayout(
                command_buffer,
                color_target_,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0U,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            const VkClearValue clear_value = makeClearValue(config_.clear_color_rgba8);
            const VkRenderingAttachmentInfo color_attachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .pNext = nullptr,
                .imageView = color_image_view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_NONE,
                .resolveImageView = VK_NULL_HANDLE,
                .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = clear_value,
            };
            const VkRenderingInfo rendering_info{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .pNext = nullptr,
                .flags = static_cast<VkRenderingFlags>(config_.flags),
                .renderArea = {
                    .offset = {.x = 0, .y = 0},
                    .extent = {.width = config_.width, .height = config_.height},
                },
                .layerCount = 1U,
                .viewMask = 0U,
                .colorAttachmentCount = 1U,
                .pColorAttachments = &color_attachment,
                .pDepthAttachment = nullptr,
                .pStencilAttachment = nullptr,
            };

            vkCmdBeginRendering(command_buffer, &rendering_info);
            const VkViewport viewport{
                .x = 0.0F,
                .y = 0.0F,
                .width = static_cast<float>(config_.width),
                .height = static_cast<float>(config_.height),
                .minDepth = 0.0F,
                .maxDepth = 1.0F,
            };
            const VkRect2D scissor{
                .offset = {.x = 0, .y = 0},
                .extent = {.width = config_.width, .height = config_.height},
            };
            vkCmdSetViewport(command_buffer, 0U, 1U, &viewport);
            vkCmdSetScissor(command_buffer, 0U, 1U, &scissor);
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdDrawIndirect(
                command_buffer,
                indirect_buffer,
                indirect_offset,
                indirect_draw_count_,
                indirect_stride_);
            vkCmdEndRendering(command_buffer);
            return makeResult(NativeStatusCode::Ok, NativeObjectKind::CommandBuffer);
        }
    }

private:
    static NativeResult makeResult(
        NativeStatusCode code_,
        NativeObjectKind object_kind_) noexcept
    {
        return {
            .code = code_,
            .object_kind = object_kind_,
            .object_id = {.value = 0U},
            .generation = {.value = 0U},
        };
    }

    static VkClearValue makeClearValue(U32 rgba8_) noexcept
    {
        constexpr float kInv255 = 1.0F / 255.0F;
        return {
            .color = {
                .float32 = {
                    static_cast<float>((rgba8_ >> 24U) & 0xFFU) * kInv255,
                    static_cast<float>((rgba8_ >> 16U) & 0xFFU) * kInv255,
                    static_cast<float>((rgba8_ >> 8U) & 0xFFU) * kInv255,
                    static_cast<float>(rgba8_ & 0xFFU) * kInv255,
                },
            },
        };
    }
};

static_assert(std::is_trivial_v<VulkanDynamicRenderEncoderConfig>);
static_assert(std::is_standard_layout_v<VulkanDynamicRenderEncoderConfig>);
static_assert(std::is_trivially_copyable_v<VulkanDynamicRenderEncoderConfig>);
static_assert(std::is_aggregate_v<VulkanDynamicRenderEncoderConfig>);

} // namespace Render2D
