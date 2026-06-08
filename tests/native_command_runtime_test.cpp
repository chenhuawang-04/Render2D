#include <Render2D/Render2D.hpp>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Runtime = R2D::NativeCommandRuntime<Provider, Dim>;
using NativeCommandBufferRef = R2D::NativeCommandBufferRef<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<Runtime>);
static_assert(R2D::StrictPodComponent<NativeCommandBufferRef>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, NativeCommandBufferRef>);

int main()
{
    try {
        Runtime runtime;

        auto capacity = runtime.reserveCommandBuffers(1U);
        assert(capacity.code == R2D::NativeStatusCode::Ok);
        assert(capacity.available_count >= 1U);
        assert(runtime.commandBufferCapacity() >= 1U);

        NativeCommandBufferRef first_ref{};
        auto result = runtime.createCommandBufferRef(
            4U,
            {.first = 2U, .count = 3U},
            {.first = 7U, .count = 1U},
            9U,
            first_ref);
        assert(result.code == R2D::NativeStatusCode::Ok);
        assert(result.object_kind == R2D::NativeObjectKind::CommandBuffer);
        assert(first_ref.command_buffer_id == 0U);
        assert(first_ref.generation == 1U);
        assert(first_ref.frame_index == 4U);
        assert(first_ref.batch_first == 2U);
        assert(first_ref.batch_count == 3U);
        assert(first_ref.upload_first == 7U);
        assert(first_ref.upload_count == 1U);
        assert(first_ref.flags == 9U);
        assert(runtime.commandBufferCount() == 1U);

        NativeCommandBufferRef overflow_ref{};
        result = runtime.createCommandBufferRef(
            5U,
            {.first = 0U, .count = 1U},
            {.first = 0U, .count = 0U},
            0U,
            overflow_ref);
        assert(result.code == R2D::NativeStatusCode::OutOfCapacity);
        assert(runtime.commandBufferCount() == 1U);

        NativeCommandBufferRef resolved_ref{};
        result = runtime.resolveCommandBufferRef(first_ref, resolved_ref);
        assert(result.code == R2D::NativeStatusCode::Ok);
        assert(resolved_ref.frame_index == first_ref.frame_index);
        assert(resolved_ref.batch_first == first_ref.batch_first);

        result = runtime.releaseCommandBufferRef(first_ref);
        assert(result.code == R2D::NativeStatusCode::Ok);
        assert(runtime.commandBufferCount() == 0U);

        result = runtime.resolveCommandBufferRef(first_ref, resolved_ref);
        assert(result.code == R2D::NativeStatusCode::StaleReference);

        NativeCommandBufferRef reused_ref{};
        result = runtime.createCommandBufferRef(
            6U,
            {.first = 8U, .count = 1U},
            {.first = 0U, .count = 2U},
            3U,
            reused_ref);
        assert(result.code == R2D::NativeStatusCode::Ok);
        assert(reused_ref.command_buffer_id == first_ref.command_buffer_id);
        assert(reused_ref.generation == first_ref.generation + 1U);
        assert(reused_ref.frame_index == 6U);
    } catch (...) {
        return 1;
    }

    return 0;
}
