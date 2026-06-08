#include <Render2D/Render2D.hpp>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Runtime = R2D::NativeResourceRuntime<Provider, Dim>;
using BufferRef = R2D::BufferRef<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<Runtime>);
static_assert(R2D::StrictPodComponent<BufferRef>);
static_assert(R2D::StrictPodComponent<ImageRef>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, BufferRef>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, ImageRef>);

int main()
{
    Runtime runtime;

    auto capacity = runtime.reserveBuffers(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(capacity.available_count >= 1U);
    assert(runtime.bufferCapacity() >= 1U);

    capacity = runtime.reserveImages(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(capacity.available_count >= 1U);
    assert(runtime.imageCapacity() >= 1U);

    BufferRef first_buffer{};
    auto result = runtime.createBufferRef(
        {.value = 0x1000U},
        4096U,
        7U,
        R2D::NativeMemoryDomain::DeviceLocal,
        first_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(result.object_kind == R2D::NativeObjectKind::Buffer);
    assert(first_buffer.buffer_id == 0U);
    assert(first_buffer.generation == 1U);
    assert(first_buffer.handle == 0x1000U);
    assert(first_buffer.byte_size == 4096U);
    assert(runtime.bufferCount() == 1U);

    BufferRef overflow_buffer{};
    result = runtime.createBufferRef(
        {.value = 0x2000U},
        8192U,
        3U,
        R2D::NativeMemoryDomain::Upload,
        overflow_buffer);
    assert(result.code == R2D::NativeStatusCode::OutOfCapacity);
    assert(runtime.bufferCount() == 1U);

    BufferRef resolved_buffer{};
    result = runtime.resolveBufferRef(first_buffer, resolved_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resolved_buffer.handle == first_buffer.handle);
    assert(resolved_buffer.generation == first_buffer.generation);

    result = runtime.releaseBufferRef(first_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.bufferCount() == 0U);

    result = runtime.resolveBufferRef(first_buffer, resolved_buffer);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    BufferRef reused_buffer{};
    result = runtime.createBufferRef(
        {.value = 0x3000U},
        16384U,
        5U,
        R2D::NativeMemoryDomain::Readback,
        reused_buffer);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_buffer.buffer_id == first_buffer.buffer_id);
    assert(reused_buffer.generation == first_buffer.generation + 1U);
    assert(reused_buffer.handle == 0x3000U);

    ImageRef first_image{};
    result = runtime.createImageRef(
        {.value = 0x4000U},
        {.value = 0x5000U},
        128U,
        64U,
        44U,
        9U,
        first_image);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(result.object_kind == R2D::NativeObjectKind::Image);
    assert(first_image.image_id == 0U);
    assert(first_image.generation == 1U);
    assert(first_image.width == 128U);
    assert(first_image.height == 64U);
    assert(runtime.imageCount() == 1U);

    ImageRef resolved_image{};
    result = runtime.resolveImageRef(first_image, resolved_image);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resolved_image.image_handle == first_image.image_handle);
    assert(resolved_image.image_view_handle == first_image.image_view_handle);

    result = runtime.releaseImageRef(first_image);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.imageCount() == 0U);

    result = runtime.resolveImageRef(first_image, resolved_image);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    return 0;
}
