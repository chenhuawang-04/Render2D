#pragma once

namespace Render2D {

template<class Provider, class Dim>
struct DeviceHandle;

template<class Provider, class Dim>
struct QueueHandle;

template<class Provider, class Dim>
struct SwapchainState;

template<class Provider, class Dim>
struct FrameSync;

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
class DescriptorStorage;

template<class Provider, class Dim>
class CommandStorage;

} // namespace Render2D
