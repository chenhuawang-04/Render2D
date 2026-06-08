#include <Render2D/Render2D.hpp>

#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;

using BufferRef = R2D::BufferRef<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;
using DeviceHandle = R2D::DeviceHandle<Provider, Dim>;
using FrameIndex = R2D::FrameIndex<Provider, Dim>;
using FrameSync = R2D::FrameSync<Provider, Dim>;
using ImageRef = R2D::ImageRef<Provider, Dim>;
using PipelineRef = R2D::PipelineRef<Provider, Dim>;
using QueueHandle = R2D::QueueHandle<Provider, Dim>;
using SwapchainState = R2D::SwapchainState<Provider, Dim>;

static_assert(!R2D::StrictPodComponent<R2D::NativeFrameRuntime<Provider, Dim>>);
static_assert(!R2D::StrictPodComponent<R2D::NativeDeviceRuntime<Provider, Dim>>);
static_assert(!R2D::StrictPodComponent<R2D::NativePipelineRuntime<Provider, Dim>>);
static_assert(!R2D::StrictPodComponent<R2D::NativeDescriptorRuntime<Provider, Dim>>);
static_assert(!R2D::StrictPodComponent<R2D::NativeSwapchainRuntime<Provider, Dim>>);
static_assert(!R2D::StrictPodComponent<R2D::NativeResourceRuntime<Provider, Dim>>);
static_assert(R2D::StrictPodComponent<FrameSync>);
static_assert(R2D::StrictPodComponent<DeviceHandle>);
static_assert(R2D::StrictPodComponent<QueueHandle>);
static_assert(R2D::StrictPodComponent<SwapchainState>);
static_assert(R2D::StrictPodComponent<PipelineRef>);
static_assert(R2D::StrictPodComponent<DescriptorSlice>);
static_assert(R2D::StrictPodComponent<BufferRef>);
static_assert(R2D::StrictPodComponent<ImageRef>);

void testFrameRuntime()
{
    R2D::NativeFrameRuntime<Provider, Dim> runtime;

    FrameIndex frame_index{};
    FrameSync frame_sync{};
    auto result = runtime.beginFrame(frame_index, frame_sync);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);
    assert(result.object_kind == R2D::NativeObjectKind::Frame);

    auto capacity = runtime.configure(0U);
    assert(capacity.code == R2D::NativeStatusCode::InvalidInput);

    capacity = runtime.configure(3U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(runtime.framesInFlight() == 3U);
    assert(runtime.currentSlotIndex() == 0U);

    result = runtime.beginFrame(frame_index, frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(frame_index.value == 0U);
    assert(frame_sync.frame_index == 0U);
    assert(frame_sync.in_flight_fence_id == 0U);

    result = runtime.beginFrame(frame_index, frame_sync);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);

    result = runtime.endFrame();
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.currentFrameIndex() == 1U);
    assert(runtime.currentSlotIndex() == 1U);

    result = runtime.beginFrame(frame_index, frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(frame_index.value == 1U);
    assert(frame_sync.image_available_semaphore_id == 1U);

    result = runtime.endFrame();
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.beginFrame(frame_index, frame_sync);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(frame_index.value == 2U);
    result = runtime.endFrame();
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(runtime.currentSlotIndex() == 0U);
}

void testDeviceRuntime()
{
    R2D::NativeDeviceRuntime<Provider, Dim> runtime;

    auto capacity = runtime.reserveDevices(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(runtime.deviceCapacity() >= 1U);

    capacity = runtime.reserveQueues(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(runtime.queueCapacity() >= 1U);

    DeviceHandle first_device{};
    auto result = runtime.createDeviceHandle({.value = 0xD001U}, first_device);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(result.object_kind == R2D::NativeObjectKind::Device);
    assert(first_device.device_id == 0U);
    assert(first_device.generation == 1U);
    assert(first_device.handle == 0xD001U);
    assert(runtime.deviceCount() == 1U);

    DeviceHandle overflow_device{};
    result = runtime.createDeviceHandle({.value = 0xD002U}, overflow_device);
    assert(result.code == R2D::NativeStatusCode::OutOfCapacity);

    DeviceHandle resolved_device{};
    result = runtime.resolveDeviceHandle(first_device, resolved_device);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resolved_device.handle == first_device.handle);

    result = runtime.releaseDeviceHandle(first_device);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.resolveDeviceHandle(first_device, resolved_device);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    DeviceHandle reused_device{};
    result = runtime.createDeviceHandle({.value = 0xD003U}, reused_device);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_device.device_id == first_device.device_id);
    assert(reused_device.generation == first_device.generation + 1U);

    QueueHandle first_queue{};
    result = runtime.createQueueHandle({.value = 0xA001U}, 2U, 1U, 7U, first_queue);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(result.object_kind == R2D::NativeObjectKind::Queue);
    assert(first_queue.queue_id == 0U);
    assert(first_queue.generation == 1U);
    assert(first_queue.queue_family_index == 2U);
    assert(first_queue.queue_index == 1U);
    assert(first_queue.flags == 7U);

    QueueHandle overflow_queue{};
    result = runtime.createQueueHandle({.value = 0xA002U}, 3U, 0U, 1U, overflow_queue);
    assert(result.code == R2D::NativeStatusCode::OutOfCapacity);

    QueueHandle resolved_queue{};
    result = runtime.resolveQueueHandle(first_queue, resolved_queue);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resolved_queue.handle == first_queue.handle);

    result = runtime.releaseQueueHandle(first_queue);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.resolveQueueHandle(first_queue, resolved_queue);
    assert(result.code == R2D::NativeStatusCode::StaleReference);
}

void testPipelineRuntime()
{
    R2D::NativePipelineRuntime<Provider, Dim> runtime;

    auto capacity = runtime.reservePipelines(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(runtime.pipelineCapacity() >= 1U);

    PipelineRef first_pipeline{};
    auto result = runtime.createPipelineRef({.value = 0x1000U}, {.value = 0x2000U}, 3U, first_pipeline);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(result.object_kind == R2D::NativeObjectKind::Pipeline);
    assert(first_pipeline.pipeline_id == 0U);
    assert(first_pipeline.generation == 1U);
    assert(first_pipeline.layout_handle == 0x2000U);
    assert(runtime.pipelineCount() == 1U);

    PipelineRef overflow_pipeline{};
    result = runtime.createPipelineRef({.value = 0x3000U}, {.value = 0x4000U}, 0U, overflow_pipeline);
    assert(result.code == R2D::NativeStatusCode::OutOfCapacity);

    PipelineRef resolved_pipeline{};
    result = runtime.resolvePipelineRef(first_pipeline, resolved_pipeline);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resolved_pipeline.handle == first_pipeline.handle);

    result = runtime.releasePipelineRef(first_pipeline);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.resolvePipelineRef(first_pipeline, resolved_pipeline);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    PipelineRef reused_pipeline{};
    result = runtime.createPipelineRef({.value = 0x5000U}, {.value = 0x6000U}, 9U, reused_pipeline);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_pipeline.pipeline_id == first_pipeline.pipeline_id);
    assert(reused_pipeline.generation == first_pipeline.generation + 1U);
}

void testDescriptorRuntime()
{
    R2D::NativeDescriptorRuntime<Provider, Dim> runtime;

    auto capacity = runtime.reserveDescriptorSets(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(runtime.descriptorCapacity() >= 1U);

    DescriptorSlice descriptor_slice{};
    auto result = runtime.allocateDescriptorSlice(0U, 0U, descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);

    result = runtime.allocateDescriptorSlice(4U, 8U, descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(result.object_kind == R2D::NativeObjectKind::Descriptor);
    assert(descriptor_slice.descriptor_set_id == 0U);
    assert(descriptor_slice.first == 4U);
    assert(descriptor_slice.count == 8U);
    assert(descriptor_slice.generation == 1U);

    DescriptorSlice overflow_slice{};
    result = runtime.allocateDescriptorSlice(12U, 4U, overflow_slice);
    assert(result.code == R2D::NativeStatusCode::OutOfCapacity);

    DescriptorSlice resolved_slice{};
    result = runtime.resolveDescriptorSlice(descriptor_slice, resolved_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resolved_slice.first == descriptor_slice.first);

    result = runtime.releaseDescriptorSlice(descriptor_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.resolveDescriptorSlice(descriptor_slice, resolved_slice);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    DescriptorSlice reused_slice{};
    result = runtime.allocateDescriptorSlice(16U, 2U, reused_slice);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_slice.descriptor_set_id == descriptor_slice.descriptor_set_id);
    assert(reused_slice.generation == descriptor_slice.generation + 1U);
}

void testSwapchainRuntime()
{
    R2D::NativeSwapchainRuntime<Provider, Dim> runtime;

    auto capacity = runtime.reserveSwapchains(1U);
    assert(capacity.code == R2D::NativeStatusCode::Ok);
    assert(runtime.swapchainCapacity() >= 1U);

    SwapchainState swapchain{};
    auto result = runtime.createSwapchainState(
        {.value = 0x7000U},
        0U,
        0U,
        1280U,
        720U,
        44U,
        5U,
        swapchain);
    assert(result.code == R2D::NativeStatusCode::InvalidInput);

    result = runtime.createSwapchainState(
        {.value = 0x7000U},
        3U,
        2U,
        1280U,
        720U,
        44U,
        5U,
        swapchain);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(result.object_kind == R2D::NativeObjectKind::Swapchain);
    assert(swapchain.swapchain_id == 0U);
    assert(swapchain.generation == 1U);
    assert(swapchain.image_first == 3U);
    assert(swapchain.image_count == 2U);
    assert(runtime.swapchainCount() == 1U);

    SwapchainState overflow_swapchain{};
    result = runtime.createSwapchainState(
        {.value = 0x8000U},
        0U,
        2U,
        640U,
        480U,
        44U,
        0U,
        overflow_swapchain);
    assert(result.code == R2D::NativeStatusCode::OutOfCapacity);

    SwapchainState resolved_swapchain{};
    result = runtime.resolveSwapchainState(swapchain, resolved_swapchain);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resolved_swapchain.width == 1280U);

    SwapchainState resized_swapchain{};
    result = runtime.resizeSwapchainState(swapchain, 1920U, 1080U, resized_swapchain);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(resized_swapchain.swapchain_id == swapchain.swapchain_id);
    assert(resized_swapchain.generation == swapchain.generation + 1U);
    assert(resized_swapchain.width == 1920U);
    assert(resized_swapchain.height == 1080U);

    result = runtime.resolveSwapchainState(swapchain, resolved_swapchain);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    result = runtime.releaseSwapchainState(resized_swapchain);
    assert(result.code == R2D::NativeStatusCode::Ok);
    result = runtime.resolveSwapchainState(resized_swapchain, resolved_swapchain);
    assert(result.code == R2D::NativeStatusCode::StaleReference);

    SwapchainState reused_swapchain{};
    result = runtime.createSwapchainState(
        {.value = 0x9000U},
        9U,
        3U,
        800U,
        600U,
        50U,
        1U,
        reused_swapchain);
    assert(result.code == R2D::NativeStatusCode::Ok);
    assert(reused_swapchain.swapchain_id == swapchain.swapchain_id);
    assert(reused_swapchain.generation == resized_swapchain.generation + 1U);
}

int main()
{
    try {
        testFrameRuntime();
        testDeviceRuntime();
        testPipelineRuntime();
        testDescriptorRuntime();
        testSwapchainRuntime();
    } catch (...) {
        return 1;
    }

    return 0;
}
