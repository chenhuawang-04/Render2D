#pragma once

#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"
#include "Render2D/Native/VulkanDescriptorRuntime.hpp"
#include "Render2D/Native/VulkanPipelineRuntime.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <type_traits>

namespace Render2D {

inline constexpr U32 kVulkanSpriteVertexBinding = 0U;
inline constexpr U32 kVulkanSpriteInstanceBinding = 1U;
inline constexpr U32 kVulkanSpritePositionLocation = 0U;
inline constexpr U32 kVulkanSpriteUvLocation = 1U;
inline constexpr U32 kVulkanSpriteTransformRow0Location = 2U;
inline constexpr U32 kVulkanSpriteTransformRow1Location = 3U;
inline constexpr U32 kVulkanSpriteUvRectLocation = 4U;
inline constexpr U32 kVulkanSpriteColorLocation = 5U;
inline constexpr U32 kVulkanSpriteVertexBindingCount = 2U;
inline constexpr U32 kVulkanSpriteVertexAttributeCount = 6U;
inline constexpr U32 kVulkanSpriteTextureDescriptorCount = 1U;

struct VulkanSpritePipelineConfig {
    VkShaderModule vertex_shader;
    VkShaderModule fragment_shader;
    VkDescriptorSetLayout descriptor_set_layout;
    U32 color_format;
    U32 flags;
};

template<class Provider, class Dim>
struct VulkanSpritePipelineRuntime {
    using SpriteVertexType = SpriteVertex<Provider, Dim>;
    using SpriteInstanceType = SpriteInstance<Provider, Dim>;

    inline static constexpr std::array<VkVertexInputBindingDescription, kVulkanSpriteVertexBindingCount>
        kVertexBindings{{
            {
                .binding = kVulkanSpriteVertexBinding,
                .stride = static_cast<U32>(sizeof(SpriteVertexType)),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            },
            {
                .binding = kVulkanSpriteInstanceBinding,
                .stride = static_cast<U32>(sizeof(SpriteInstanceType)),
                .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
            },
        }};

    inline static constexpr std::array<VkVertexInputAttributeDescription, kVulkanSpriteVertexAttributeCount>
        kVertexAttributes{{
            {
                .location = kVulkanSpritePositionLocation,
                .binding = kVulkanSpriteVertexBinding,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = static_cast<U32>(offsetof(SpriteVertexType, position_x)),
            },
            {
                .location = kVulkanSpriteUvLocation,
                .binding = kVulkanSpriteVertexBinding,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = static_cast<U32>(offsetof(SpriteVertexType, uv_x)),
            },
            {
                .location = kVulkanSpriteTransformRow0Location,
                .binding = kVulkanSpriteInstanceBinding,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = static_cast<U32>(offsetof(SpriteInstanceType, transform_m00)),
            },
            {
                .location = kVulkanSpriteTransformRow1Location,
                .binding = kVulkanSpriteInstanceBinding,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = static_cast<U32>(offsetof(SpriteInstanceType, transform_m10)),
            },
            {
                .location = kVulkanSpriteUvRectLocation,
                .binding = kVulkanSpriteInstanceBinding,
                .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                .offset = static_cast<U32>(offsetof(SpriteInstanceType, uv_min_x)),
            },
            {
                .location = kVulkanSpriteColorLocation,
                .binding = kVulkanSpriteInstanceBinding,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .offset = static_cast<U32>(offsetof(SpriteInstanceType, color_rgba8)),
            },
        }};

    static VulkanDescriptorRuntimeConfig makeDescriptorRuntimeConfig(
        VkDevice device_,
        U32 max_descriptor_sets_,
        U32 texture_descriptor_count_,
        U32 descriptor_pool_flags_,
        U32 descriptor_set_layout_flags_) noexcept
    {
        return {
            .device = device_,
            .max_descriptor_sets = max_descriptor_sets_,
            .combined_image_sampler_count = texture_descriptor_count_,
            .storage_buffer_count = 0U,
            .descriptor_pool_flags = descriptor_pool_flags_,
            .descriptor_set_layout_flags = descriptor_set_layout_flags_,
            .combined_image_sampler_binding_flags = 0U,
            .storage_buffer_binding_flags = 0U,
        };
    }

    static NativeResult createPipelineRef(
        VulkanPipelineRuntime<Provider, Dim>& pipeline_runtime_,
        const VulkanSpritePipelineConfig& config_,
        PipelineRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .object_kind = NativeObjectKind::Pipeline,
                .object_id = {.value = 0U},
                .generation = {.value = 0U},
            };
        } else {
            if (config_.descriptor_set_layout == VK_NULL_HANDLE) {
                return {
                    .code = NativeStatusCode::InvalidInput,
                    .object_kind = NativeObjectKind::Pipeline,
                    .object_id = {.value = 0U},
                    .generation = {.value = 0U},
                };
            }

            const std::array<VkDescriptorSetLayout, 1U> descriptor_layouts{
                config_.descriptor_set_layout,
            };
            return pipeline_runtime_.createGraphicsPipelineRef(
                {
                    .vertex_shader = config_.vertex_shader,
                    .fragment_shader = config_.fragment_shader,
                    .descriptor_set_layouts = descriptor_layouts.data(),
                    .descriptor_set_layout_count = 1U,
                    .color_format = config_.color_format,
                    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                    .cull_mode = VK_CULL_MODE_NONE,
                    .front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                    .polygon_mode = VK_POLYGON_MODE_FILL,
                    .sample_count = VK_SAMPLE_COUNT_1_BIT,
                    .flags = config_.flags,
                    .vertex_binding_descriptions = kVertexBindings.data(),
                    .vertex_attribute_descriptions = kVertexAttributes.data(),
                    .vertex_binding_description_count = static_cast<U32>(kVertexBindings.size()),
                    .vertex_attribute_description_count = static_cast<U32>(kVertexAttributes.size()),
                },
                out_ref_);
        }
    }
};

static_assert(std::is_trivial_v<VulkanSpritePipelineConfig>);
static_assert(std::is_standard_layout_v<VulkanSpritePipelineConfig>);
static_assert(std::is_trivially_copyable_v<VulkanSpritePipelineConfig>);
static_assert(std::is_aggregate_v<VulkanSpritePipelineConfig>);

} // namespace Render2D
