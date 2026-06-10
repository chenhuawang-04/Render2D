#pragma once

#include "Render2D/Memory/RenderVector.hpp"

#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Render2D {

struct VulkanPipelineRuntimeConfig {
    VkDevice device;
    U32 pipeline_cache_flags;
};

struct VulkanGraphicsPipelineConfig {
    VkShaderModule vertex_shader;
    VkShaderModule fragment_shader;
    const VkDescriptorSetLayout* descriptor_set_layouts;
    U32 descriptor_set_layout_count;
    U32 color_format;
    U32 topology;
    U32 cull_mode;
    U32 front_face;
    U32 polygon_mode;
    U32 sample_count;
    U32 flags;
    const VkVertexInputBindingDescription* vertex_binding_descriptions;
    const VkVertexInputAttributeDescription* vertex_attribute_descriptions;
    U32 vertex_binding_description_count;
    U32 vertex_attribute_description_count;
};

template<class Provider, class Dim>
class VulkanPipelineRuntime {
public:
    VulkanPipelineRuntime() = default;
    VulkanPipelineRuntime(const VulkanPipelineRuntime&) = delete;
    VulkanPipelineRuntime& operator=(const VulkanPipelineRuntime&) = delete;
    VulkanPipelineRuntime(VulkanPipelineRuntime&&) = delete;
    VulkanPipelineRuntime& operator=(VulkanPipelineRuntime&&) = delete;

    ~VulkanPipelineRuntime() noexcept
    {
        shutdown();
    }

    NativeResult initialize(VulkanPipelineRuntimeConfig config_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (config_.device == VK_NULL_HANDLE || device != VK_NULL_HANDLE || pipeline_cache != VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            const VkPipelineCacheCreateInfo cache_info{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
                .pNext = nullptr,
                .flags = static_cast<VkPipelineCacheCreateFlags>(config_.pipeline_cache_flags),
                .initialDataSize = 0U,
                .pInitialData = nullptr,
            };
            const VkResult vk_result = vkCreatePipelineCache(
                config_.device,
                &cache_info,
                nullptr,
                &pipeline_cache);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                pipeline_cache = VK_NULL_HANDLE;
                return makeResult(mapVulkanResult(vk_result), 0U, 0U);
            }

            device = config_.device;
            pipeline_cache_flags = config_.pipeline_cache_flags;
            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    void shutdown() noexcept
    {
        if (device != VK_NULL_HANDLE) {
            for (auto& slot : pipeline_slots) {
                destroyPipelineSlot(slot);
            }
            if (pipeline_cache != VK_NULL_HANDLE) {
                vkDestroyPipelineCache(device, pipeline_cache, nullptr);
            }
        }

        pipeline_slots.clear();
        free_pipeline_ids.clear();
        active_pipeline_count = 0U;
        pipeline_cache = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        pipeline_cache_flags = 0U;
        last_vulkan_result = VK_SUCCESS;
    }

    NativeCapacityResult reservePipelines(U32 capacity_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {
                .code = NativeStatusCode::UnsupportedDomain,
                .requested_count = capacity_,
                .available_count = 0U,
            };
        } else {
            try {
                pipeline_slots.reserve(capacity_);
                free_pipeline_ids.reserve(capacity_);
            } catch (...) {
                return {
                    .code = NativeStatusCode::OutOfMemory,
                    .requested_count = capacity_,
                    .available_count = static_cast<U32>(pipeline_slots.capacity()),
                };
            }

            return {
                .code = NativeStatusCode::Ok,
                .requested_count = capacity_,
                .available_count = static_cast<U32>(pipeline_slots.capacity()),
            };
        }
    }

    NativeResult createShaderModule(
        std::span<const U32> spirv_words_,
        VkShaderModule& out_shader_module_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            out_shader_module_ = VK_NULL_HANDLE;
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized() || spirv_words_.empty()) {
                out_shader_module_ = VK_NULL_HANDLE;
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            const VkShaderModuleCreateInfo create_info{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .codeSize = spirv_words_.size_bytes(),
                .pCode = spirv_words_.data(),
            };
            const VkResult vk_result = vkCreateShaderModule(
                device,
                &create_info,
                nullptr,
                &out_shader_module_);
            last_vulkan_result = vk_result;
            if (vk_result != VK_SUCCESS) {
                out_shader_module_ = VK_NULL_HANDLE;
                return makeResult(mapVulkanResult(vk_result), 0U, 0U);
            }

            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    NativeResult destroyShaderModule(VkShaderModule& shader_module_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized() || shader_module_ == VK_NULL_HANDLE) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }

            vkDestroyShaderModule(device, shader_module_, nullptr);
            shader_module_ = VK_NULL_HANDLE;
            return makeResult(NativeStatusCode::Ok, 0U, 0U);
        }
    }

    NativeResult createGraphicsPipelineRef(
        const VulkanGraphicsPipelineConfig& config_,
        PipelineRef<Provider, Dim>& out_ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isInitialized() ||
                config_.vertex_shader == VK_NULL_HANDLE ||
                config_.fragment_shader == VK_NULL_HANDLE ||
                config_.color_format == static_cast<U32>(VK_FORMAT_UNDEFINED) ||
                (config_.descriptor_set_layout_count != 0U && config_.descriptor_set_layouts == nullptr) ||
                (config_.vertex_binding_description_count != 0U &&
                    config_.vertex_binding_descriptions == nullptr) ||
                (config_.vertex_attribute_description_count != 0U &&
                    config_.vertex_attribute_descriptions == nullptr)) {
                return makeResult(NativeStatusCode::InvalidInput, 0U, 0U);
            }
            if (!hasAvailableSlot()) {
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
            NativeStatusCode create_code = createPipelineLayout(config_, pipeline_layout);
            if (create_code != NativeStatusCode::Ok) {
                return makeResult(create_code, 0U, 0U);
            }

            VkPipeline pipeline = VK_NULL_HANDLE;
            create_code = createGraphicsPipeline(config_, pipeline_layout, pipeline);
            if (create_code != NativeStatusCode::Ok) {
                vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                return makeResult(create_code, 0U, 0U);
            }

            U32 pipeline_id = 0U;
            if (!acquirePipelineSlot(pipeline_id)) {
                vkDestroyPipeline(device, pipeline, nullptr);
                vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
                return makeResult(NativeStatusCode::OutOfCapacity, 0U, 0U);
            }

            auto& slot = pipeline_slots[pipeline_id];
            slot.pipeline = pipeline;
            slot.pipeline_layout = pipeline_layout;
            slot.occupied = 1U;
            slot.ref = {
                .handle = nativeHandleToU64(pipeline),
                .layout_handle = nativeHandleToU64(pipeline_layout),
                .pipeline_id = pipeline_id,
                .generation = slot.generation.value,
                .flags = config_.flags,
            };
            ++active_pipeline_count;
            out_ref_ = slot.ref;
            return makeResult(NativeStatusCode::Ok, pipeline_id, slot.generation.value);
        }
    }

    NativeResult resolvePipelineRef(
        const PipelineRef<Provider, Dim>& ref_,
        PipelineRef<Provider, Dim>& out_ref_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLivePipelineRef(ref_)) {
                return makeResult(NativeStatusCode::StaleReference, ref_.pipeline_id, ref_.generation);
            }

            out_ref_ = pipeline_slots[ref_.pipeline_id].ref;
            return makeResult(NativeStatusCode::Ok, ref_.pipeline_id, ref_.generation);
        }
    }

    NativeResult resolveNativePipeline(
        const PipelineRef<Provider, Dim>& ref_,
        VkPipeline& out_pipeline_,
        VkPipelineLayout& out_pipeline_layout_) const noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLivePipelineRef(ref_)) {
                out_pipeline_ = VK_NULL_HANDLE;
                out_pipeline_layout_ = VK_NULL_HANDLE;
                return makeResult(NativeStatusCode::StaleReference, ref_.pipeline_id, ref_.generation);
            }

            const auto& slot = pipeline_slots[ref_.pipeline_id];
            out_pipeline_ = slot.pipeline;
            out_pipeline_layout_ = slot.pipeline_layout;
            return makeResult(NativeStatusCode::Ok, ref_.pipeline_id, ref_.generation);
        }
    }

    NativeResult releasePipelineRef(const PipelineRef<Provider, Dim>& ref_)
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, 0U, 0U);
        } else {
            if (!isLivePipelineRef(ref_)) {
                return makeResult(NativeStatusCode::StaleReference, ref_.pipeline_id, ref_.generation);
            }

            const NativeStatusCode release_code = pushFreePipelineId(ref_.pipeline_id);
            if (release_code != NativeStatusCode::Ok) {
                return makeResult(release_code, ref_.pipeline_id, ref_.generation);
            }

            auto& slot = pipeline_slots[ref_.pipeline_id];
            destroyPipelineSlot(slot);
            slot.occupied = 0U;
            slot.generation.value = nextGeneration(slot.generation.value);
            slot.ref = {};
            --active_pipeline_count;
            return makeResult(NativeStatusCode::Ok, ref_.pipeline_id, slot.generation.value);
        }
    }

    bool isInitialized() const noexcept
    {
        return device != VK_NULL_HANDLE && pipeline_cache != VK_NULL_HANDLE;
    }

    VkPipelineCache nativePipelineCache() const noexcept
    {
        return pipeline_cache;
    }

    VkResult lastVulkanResult() const noexcept
    {
        return last_vulkan_result;
    }

    U32 pipelineCount() const noexcept
    {
        return active_pipeline_count;
    }

    U32 pipelineCapacity() const noexcept
    {
        return static_cast<U32>(pipeline_slots.capacity());
    }

private:
    struct PipelineSlot {
        PipelineRef<Provider, Dim> ref;
        NativeGeneration generation;
        VkPipeline pipeline;
        VkPipelineLayout pipeline_layout;
        U32 occupied;
    };

    static constexpr U32 kFirstGeneration = 1U;

    static NativeResult makeResult(
        NativeStatusCode code_,
        U32 object_id_,
        U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = NativeObjectKind::Pipeline,
            .object_id = {.value = object_id_},
            .generation = {.value = generation_},
        };
    }

    static NativeStatusCode mapVulkanResult(VkResult result_) noexcept
    {
        switch (result_) {
        case VK_SUCCESS:
            return NativeStatusCode::Ok;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return NativeStatusCode::OutOfMemory;
        case VK_ERROR_DEVICE_LOST:
            return NativeStatusCode::DeviceLost;
        default:
            return NativeStatusCode::InvalidInput;
        }
    }

    static U32 nextGeneration(U32 generation_) noexcept
    {
        return generation_ == 0xFFFFFFFFU ? kFirstGeneration : generation_ + 1U;
    }

    template<class Handle>
    static U64 nativeHandleToU64(Handle handle_) noexcept
    {
        if constexpr (std::is_pointer_v<Handle>) {
            return static_cast<U64>(reinterpret_cast<std::uintptr_t>(handle_));
        } else {
            return static_cast<U64>(handle_);
        }
    }

    NativeStatusCode createPipelineLayout(
        const VulkanGraphicsPipelineConfig& config_,
        VkPipelineLayout& out_pipeline_layout_) noexcept
    {
        const VkPipelineLayoutCreateInfo layout_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .setLayoutCount = config_.descriptor_set_layout_count,
            .pSetLayouts = config_.descriptor_set_layouts,
            .pushConstantRangeCount = 0U,
            .pPushConstantRanges = nullptr,
        };
        const VkResult vk_result = vkCreatePipelineLayout(
            device,
            &layout_info,
            nullptr,
            &out_pipeline_layout_);
        last_vulkan_result = vk_result;
        return mapVulkanResult(vk_result);
    }

    NativeStatusCode createGraphicsPipeline(
        const VulkanGraphicsPipelineConfig& config_,
        VkPipelineLayout pipeline_layout_,
        VkPipeline& out_pipeline_) noexcept
    {
        constexpr char kEntryPoint[] = "main";
        const std::array<VkPipelineShaderStageCreateInfo, 2U> shader_stages{{
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = config_.vertex_shader,
                .pName = kEntryPoint,
                .pSpecializationInfo = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = config_.fragment_shader,
                .pName = kEntryPoint,
                .pSpecializationInfo = nullptr,
            },
        }};
        const VkPipelineVertexInputStateCreateInfo vertex_input{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .vertexBindingDescriptionCount = config_.vertex_binding_description_count,
            .pVertexBindingDescriptions = config_.vertex_binding_descriptions,
            .vertexAttributeDescriptionCount = config_.vertex_attribute_description_count,
            .pVertexAttributeDescriptions = config_.vertex_attribute_descriptions,
        };
        const VkPipelineInputAssemblyStateCreateInfo input_assembly{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .topology = static_cast<VkPrimitiveTopology>(config_.topology),
            .primitiveRestartEnable = VK_FALSE,
        };
        const VkPipelineViewportStateCreateInfo viewport_state{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .viewportCount = 1U,
            .pViewports = nullptr,
            .scissorCount = 1U,
            .pScissors = nullptr,
        };
        const VkPipelineRasterizationStateCreateInfo rasterization{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = static_cast<VkPolygonMode>(config_.polygon_mode),
            .cullMode = static_cast<VkCullModeFlags>(config_.cull_mode),
            .frontFace = static_cast<VkFrontFace>(config_.front_face),
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0F,
            .depthBiasClamp = 0.0F,
            .depthBiasSlopeFactor = 0.0F,
            .lineWidth = 1.0F,
        };
        const VkPipelineMultisampleStateCreateInfo multisample{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .rasterizationSamples = config_.sample_count == 0U ?
                VK_SAMPLE_COUNT_1_BIT :
                static_cast<VkSampleCountFlagBits>(config_.sample_count),
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0F,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };
        const VkPipelineColorBlendAttachmentState color_blend_attachment{
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT,
        };
        const VkPipelineColorBlendStateCreateInfo color_blend{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1U,
            .pAttachments = &color_blend_attachment,
            .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F},
        };
        constexpr std::array<VkDynamicState, 2U> kDynamicStates{
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        const VkPipelineDynamicStateCreateInfo dynamic_state{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .dynamicStateCount = static_cast<U32>(kDynamicStates.size()),
            .pDynamicStates = kDynamicStates.data(),
        };
        const auto color_format = static_cast<VkFormat>(config_.color_format);
        const VkPipelineRenderingCreateInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = nullptr,
            .viewMask = 0U,
            .colorAttachmentCount = 1U,
            .pColorAttachmentFormats = &color_format,
            .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
        };
        const VkGraphicsPipelineCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &rendering_info,
            .flags = 0U,
            .stageCount = static_cast<U32>(shader_stages.size()),
            .pStages = shader_stages.data(),
            .pVertexInputState = &vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pTessellationState = nullptr,
            .pViewportState = &viewport_state,
            .pRasterizationState = &rasterization,
            .pMultisampleState = &multisample,
            .pDepthStencilState = nullptr,
            .pColorBlendState = &color_blend,
            .pDynamicState = &dynamic_state,
            .layout = pipeline_layout_,
            .renderPass = VK_NULL_HANDLE,
            .subpass = 0U,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };
        const VkResult vk_result = vkCreateGraphicsPipelines(
            device,
            pipeline_cache,
            1U,
            &create_info,
            nullptr,
            &out_pipeline_);
        last_vulkan_result = vk_result;
        return mapVulkanResult(vk_result);
    }

    bool hasAvailableSlot() const noexcept
    {
        return !free_pipeline_ids.empty() || pipeline_slots.size() < pipeline_slots.capacity();
    }

    bool acquirePipelineSlot(U32& out_pipeline_id_)
    {
        if (!free_pipeline_ids.empty()) {
            out_pipeline_id_ = free_pipeline_ids.back();
            free_pipeline_ids.pop_back();
            return true;
        }
        if (pipeline_slots.size() >= pipeline_slots.capacity()) {
            return false;
        }

        try {
            out_pipeline_id_ = static_cast<U32>(pipeline_slots.size());
            pipeline_slots.push_back({
                .ref = {},
                .generation = {.value = kFirstGeneration},
                .pipeline = VK_NULL_HANDLE,
                .pipeline_layout = VK_NULL_HANDLE,
                .occupied = 0U,
            });
        } catch (...) {
            return false;
        }
        return true;
    }

    NativeStatusCode pushFreePipelineId(U32 pipeline_id_)
    {
        if (free_pipeline_ids.size() >= free_pipeline_ids.capacity()) {
            return NativeStatusCode::OutOfCapacity;
        }
        try {
            free_pipeline_ids.push_back(pipeline_id_);
        } catch (...) {
            return NativeStatusCode::OutOfMemory;
        }
        return NativeStatusCode::Ok;
    }

    void destroyPipelineSlot(PipelineSlot& slot_) noexcept
    {
        if (slot_.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, slot_.pipeline, nullptr);
        }
        if (slot_.pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, slot_.pipeline_layout, nullptr);
        }
        slot_.pipeline = VK_NULL_HANDLE;
        slot_.pipeline_layout = VK_NULL_HANDLE;
    }

    bool isLivePipelineRef(const PipelineRef<Provider, Dim>& ref_) const noexcept
    {
        if (ref_.pipeline_id >= pipeline_slots.size()) {
            return false;
        }

        const auto& slot = pipeline_slots[ref_.pipeline_id];
        return slot.occupied != 0U && slot.generation.value == ref_.generation;
    }

    McVector<PipelineSlot> pipeline_slots;
    McVector<U32> free_pipeline_ids;
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
    VkResult last_vulkan_result = VK_SUCCESS;
    U32 pipeline_cache_flags = 0U;
    U32 active_pipeline_count = 0U;
};

static_assert(std::is_trivial_v<VulkanPipelineRuntimeConfig>);
static_assert(std::is_standard_layout_v<VulkanPipelineRuntimeConfig>);
static_assert(std::is_trivially_copyable_v<VulkanPipelineRuntimeConfig>);
static_assert(std::is_aggregate_v<VulkanPipelineRuntimeConfig>);

static_assert(std::is_trivial_v<VulkanGraphicsPipelineConfig>);
static_assert(std::is_standard_layout_v<VulkanGraphicsPipelineConfig>);
static_assert(std::is_trivially_copyable_v<VulkanGraphicsPipelineConfig>);
static_assert(std::is_aggregate_v<VulkanGraphicsPipelineConfig>);

} // namespace Render2D
