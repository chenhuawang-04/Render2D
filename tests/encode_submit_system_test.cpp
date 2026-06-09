#include <Render2D/Render2D.hpp>

#include <array>
#include <cassert>
#include <span>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using CommandBuffer = R2D::CommandBuffer<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;
using NativeSubmitCommand = R2D::NativeSubmitCommand<Provider, Dim>;
using UploadCommand = R2D::UploadCommand<Provider, Dim>;

static_assert(R2D::StrictPodComponent<NativeCommandBufferRef>);
static_assert(R2D::StrictPodComponent<NativeSubmitCommand>);

void testEncodeSystem()
{
    std::array<BatchCommand, 3U> batches{
        BatchCommand{
            .draw_first = 0U,
            .draw_count = 1U,
            .material_id = 1U,
            .texture_id = 2U,
            .pipeline_id = 3U,
            .descriptor_id = 4U,
            .sort_key = 0U,
            .flags = 0U,
        },
        BatchCommand{
            .draw_first = 1U,
            .draw_count = 2U,
            .material_id = 1U,
            .texture_id = 2U,
            .pipeline_id = 3U,
            .descriptor_id = 4U,
            .sort_key = 1U,
            .flags = 0U,
        },
        BatchCommand{
            .draw_first = 3U,
            .draw_count = 1U,
            .material_id = 5U,
            .texture_id = 6U,
            .pipeline_id = 7U,
            .descriptor_id = 8U,
            .sort_key = 2U,
            .flags = 1U,
        },
    };
    std::array<UploadCommand, 1U> uploads{
        UploadCommand{
            .resource_id = 5U,
            .source_offset = 0U,
            .destination_offset = 256U,
            .byte_count = 512U,
            .upload_kind = 1U,
            .flags = 0U,
        },
    };
    const CommandBuffer command_buffer{
        .frame_index = 7U,
        .draw_first = 0U,
        .draw_count = 0U,
        .batch_first = 1U,
        .batch_count = 2U,
        .upload_first = 0U,
        .upload_count = 1U,
        .native_submit_first = 0U,
        .native_submit_count = 0U,
    };

    R2D::NativeCommandRuntime<Provider, Dim> runtime;
    auto capacity = runtime.reserveCommandBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);

    NativeCommandBufferRef native_ref{};
    auto result = R2D::EncodeSystem<Provider, Dim>::run(
        command_buffer,
        batches,
        uploads,
        runtime,
        native_ref);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(result.object_kind == R2D::NativeObjectKind::CommandBuffer);
    assert(native_ref.command_buffer_id == 0U);
    assert(native_ref.generation == 1U);
    assert(native_ref.frame_index == command_buffer.frame_index);
    assert(native_ref.batch_first == command_buffer.batch_first);
    assert(native_ref.batch_count == command_buffer.batch_count);
    assert(native_ref.upload_first == command_buffer.upload_first);
    assert(native_ref.upload_count == command_buffer.upload_count);

    const CommandBuffer invalid_command_buffer{
        .frame_index = 8U,
        .draw_first = 0U,
        .draw_count = 0U,
        .batch_first = 2U,
        .batch_count = 2U,
        .upload_first = 0U,
        .upload_count = 1U,
        .native_submit_first = 0U,
        .native_submit_count = 0U,
    };
    NativeCommandBufferRef invalid_ref{};
    result = R2D::EncodeSystem<Provider, Dim>::run(
        invalid_command_buffer,
        batches,
        uploads,
        runtime,
        invalid_ref);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
}

void testSubmitSystem()
{
    const std::array<NativeCommandBufferRef, 1U> native_refs{
        NativeCommandBufferRef{
            .command_buffer_id = 2U,
            .generation = 3U,
            .frame_index = 4U,
            .batch_first = 5U,
            .batch_count = 6U,
            .upload_first = 7U,
            .upload_count = 8U,
            .flags = 9U,
        },
    };
    const FrameSync frame_sync{
        .frame_index = 4U,
        .image_available_semaphore_id = 10U,
        .render_finished_semaphore_id = 11U,
        .in_flight_fence_id = 12U,
        .flags = 13U,
        .sync_id = 14U,
        .generation = 15U,
    };
    std::array<NativeSubmitCommand, 1U> submits{};

    auto result = R2D::SubmitSystem<Provider, Dim>::run(native_refs, frame_sync, submits);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.read_count == 1U);
    assert(result.write_count == 1U);
    assert(submits[0U].command_first == 0U);
    assert(submits[0U].command_count == 1U);
    assert(submits[0U].wait_first == frame_sync.image_available_semaphore_id);
    assert(submits[0U].wait_count == 1U);
    assert(submits[0U].signal_first == frame_sync.render_finished_semaphore_id);
    assert(submits[0U].signal_count == 1U);
    assert(submits[0U].fence_id == frame_sync.in_flight_fence_id);
    assert(submits[0U].flags == frame_sync.flags);

    result = R2D::SubmitSystem<Provider, Dim>::run(native_refs, frame_sync, {});
    assert(result.code == R2D::SystemStatusCode::InsufficientCapacity);

    result = R2D::SubmitSystem<Provider, Dim>::run(
        std::span<const NativeCommandBufferRef>{},
        frame_sync,
        submits);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 0U);
}

int main()
{
    try {
        testEncodeSystem();
        testSubmitSystem();
    } catch (...) {
        return 1;
    }

    return 0;
}
