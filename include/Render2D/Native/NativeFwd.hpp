#pragma once

namespace Render2D {

struct VulkanCommandRuntimeConfig;

template<class Provider, class Dim>
struct DeviceHandle;

template<class Provider, class Dim>
struct QueueHandle;

template<class Provider, class Dim>
struct SwapchainState;

template<class Provider, class Dim>
struct FrameSync;

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
class NativeCommandRuntime;

template<class Provider, class Dim>
class VulkanCommandRuntime;

template<class Provider, class Dim>
class DescriptorStorage;

template<class Provider, class Dim>
class CommandStorage;

} // namespace Render2D
