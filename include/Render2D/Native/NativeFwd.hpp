#pragma once

namespace Render2D {

struct VulkanCommandRuntimeConfig;
struct VulkanDescriptorRuntimeConfig;
struct VulkanGraphicsPipelineConfig;
struct VulkanDynamicRenderEncoderConfig;
struct VulkanPipelineRuntimeConfig;
struct VulkanPresentRuntimeConfig;
struct VulkanResourceRuntimeConfig;
struct VulkanSyncRuntimeConfig;
struct VulkanSubmitRuntimeConfig;
struct VulkanSwapchainRuntimeConfig;
struct VulkanSwapchainCreateConfig;
struct VulkanSwapchainAdoptConfig;
struct VulkanThreadCommandRuntimeConfig;
struct VulkanUploadRingRuntimeConfig;

template<class Provider, class Dim>
struct DeviceHandle;

template<class Provider, class Dim>
struct QueueHandle;

template<class Provider, class Dim>
struct SwapchainState;

template<class Provider, class Dim>
struct SwapchainImageRef;

template<class Provider, class Dim>
struct AcquiredImage;

template<class Provider, class Dim>
struct PresentCommand;

template<class Provider, class Dim>
struct FrameSync;

template<class Provider, class Dim>
struct DeferredDestroyCommand;

template<class Provider, class Dim>
struct NativeCommandBufferRef;

template<class Provider, class Dim>
struct DescriptorSlice;

template<class Provider, class Dim>
struct UploadSlice;

template<class Provider, class Dim>
struct PipelineRef;

template<class Provider, class Dim>
struct ImageRef;

template<class Provider, class Dim>
struct BufferRef;

template<class Provider, class Dim>
class DeviceStorage;

template<class Provider, class Dim>
class ResourceStorageRuntime;

template<class Provider, class Dim>
class NativeResourceRuntime;

template<class Provider, class Dim>
class NativeFrameRuntime;

template<class Provider, class Dim>
class NativeDescriptorRuntime;

template<class Provider, class Dim>
class NativePipelineRuntime;

template<class Provider, class Dim>
class NativeDeviceRuntime;

template<class Provider, class Dim>
class NativeSwapchainRuntime;

template<class Provider, class Dim>
class NativeDeferredDestroyRuntime;

template<class Provider, class Dim>
class NativeCommandRuntime;

template<class Provider, class Dim>
class VulkanCommandRuntime;

template<class Provider, class Dim>
class VulkanDescriptorRuntime;

template<class Provider, class Dim>
class VulkanPipelineRuntime;

template<class Provider, class Dim>
class VulkanPresentRuntime;

template<class Provider, class Dim>
class VulkanDynamicRenderEncoder;

template<class Provider, class Dim>
class VulkanResourceRuntime;

template<class Provider, class Dim>
class VulkanSyncRuntime;

template<class Provider, class Dim>
class VulkanSubmitRuntime;

template<class Provider, class Dim>
class VulkanSwapchainRuntime;

template<class Provider, class Dim>
class VulkanThreadCommandRuntime;

template<class Provider, class Dim>
class VulkanUploadRingRuntime;

template<class Provider, class Dim>
struct VulkanSpriteInstanceUploadRuntime;

template<class Provider, class Dim>
class DescriptorStorage;

template<class Provider, class Dim>
class CommandStorage;

} // namespace Render2D
