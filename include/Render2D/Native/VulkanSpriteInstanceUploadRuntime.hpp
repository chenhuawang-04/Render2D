#pragma once

#include "Render2D/Component/Frame.hpp"
#include "Render2D/Component/Sprite.hpp"
#include "Render2D/Native/NativeComponents.hpp"
#include "Render2D/Native/NativeResult.hpp"
#include "Render2D/Native/VulkanResourceRuntime.hpp"
#include "Render2D/Native/VulkanUploadRingRuntime.hpp"

#include <vulkan/vulkan.h>

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct VulkanSpriteInstanceUploadRuntime {
    static NativeResult recordUpload(
        const SpriteInstanceUploadCommand<Provider, Dim>& command_,
        std::span<const SpriteInstance<Provider, Dim>> sprite_instances_,
        const BufferRef<Provider, Dim>& destination_buffer_,
        VulkanUploadRingRuntime<Provider, Dim>& upload_ring_runtime_,
        VulkanResourceRuntime<Provider, Dim>& resource_runtime_,
        VkCommandBuffer command_buffer_,
        UploadRingSlice<Provider, Dim>& out_slice_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return makeResult(NativeStatusCode::UnsupportedDomain, NativeObjectKind::Buffer, 0U, 0U);
        } else {
            U64 byte_count = 0U;
            if (command_buffer_ == VK_NULL_HANDLE ||
                command_.destination_buffer_id != destination_buffer_.buffer_id ||
                command_.destination_generation != destination_buffer_.generation ||
                !makeInstanceByteRange(command_, sprite_instances_.size(), byte_count) ||
                byte_count > destination_buffer_.byte_size ||
                command_.destination_offset > destination_buffer_.byte_size - byte_count) {
                return makeResult(
                    NativeStatusCode::InvalidInput,
                    NativeObjectKind::Buffer,
                    destination_buffer_.buffer_id,
                    destination_buffer_.generation);
            }

            NativeResult result = upload_ring_runtime_.allocateSlice(
                command_.frame_index,
                byte_count,
                static_cast<U64>(alignof(SpriteInstance<Provider, Dim>)),
                out_slice_);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            const auto* source_data = sprite_instances_.data() + command_.instance_first;
            result = upload_ring_runtime_.writeSlice(out_slice_, source_data, byte_count, 0U);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            VkBuffer upload_buffer = VK_NULL_HANDLE;
            U64 upload_offset = 0U;
            result = upload_ring_runtime_.resolveNativeBuffer(out_slice_, upload_buffer, upload_offset);
            if (result.code != NativeStatusCode::Ok) {
                return result;
            }

            return resource_runtime_.recordCopyNativeBufferToBuffer(
                command_buffer_,
                upload_buffer,
                upload_offset,
                destination_buffer_,
                command_.destination_offset,
                byte_count);
        }
    }

private:
    static constexpr U64 kMaxU64 = 0xFFFFFFFFFFFFFFFFULL;
    static constexpr U64 kInstanceByteSize = static_cast<U64>(sizeof(SpriteInstance<Provider, Dim>));

    static NativeResult makeResult(
        NativeStatusCode code_,
        NativeObjectKind object_kind_,
        U32 object_id_,
        U32 generation_) noexcept
    {
        return {
            .code = code_,
            .object_kind = object_kind_,
            .object_id = {.value = object_id_},
            .generation = {.value = generation_},
        };
    }

    static bool makeInstanceByteRange(
        const SpriteInstanceUploadCommand<Provider, Dim>& command_,
        Usize instance_count_,
        U64& out_byte_count_) noexcept
    {
        if (command_.instance_count == 0U ||
            command_.destination_generation == 0U ||
            command_.instance_first > instance_count_ ||
            command_.instance_count > instance_count_ - command_.instance_first ||
            static_cast<U64>(command_.instance_count) > kMaxU64 / kInstanceByteSize) {
            return false;
        }

        out_byte_count_ = static_cast<U64>(command_.instance_count) * kInstanceByteSize;
        return command_.destination_offset <= kMaxU64 - out_byte_count_;
    }
};

} // namespace Render2D
