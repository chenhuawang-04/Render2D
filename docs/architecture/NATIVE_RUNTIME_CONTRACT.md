# Native Runtime Contract

Native Runtime owns actual backend objects. ECS owns only POD references such as `BufferRef`, `ImageRef`, and `PipelineRef`.

## Stage 7B-1: Type Contracts

Stage 7B-1 defines POD-only runtime contract types. It does not call Vulkan and does not implement resource tables yet.

Implemented files:

```text
include/Render2D/Native/NativeTypes.hpp
include/Render2D/Native/NativeResult.hpp
```

Implemented types:

```cpp
NativeStatusCode
NativeObjectKind
NativeMemoryDomain
NativeHandle
NativeId
NativeGeneration
NativeResourceKey
NativeByteRange
NativeResult
NativeCapacityResult
```

## id + generation

Native references exposed to ECS use compact IDs plus generation counters. Runtime tables can use IDs as direct indices and generations to reject stale references after slot reuse.

## Error handling

Native Runtime APIs should return POD result values such as `NativeResult` or `NativeCapacityResult`. They should not throw exceptions across the runtime boundary.

## Not Implemented Yet

Stage 7B-2 adds `NativeResourceRuntime<Provider, Dim>` as a CPU-side slot table skeleton for `BufferRef` and `ImageRef`.

It implements:

- buffer/image reserve
- buffer/image placeholder reference creation
- id + generation resolve
- stale reference rejection
- release
- slot reuse with generation increment

It still does not call Vulkan.

## Not Implemented Yet

The native runtime still does not implement:

- deferred destroy queues
- Vulkan object creation or destruction
- MemoryCenter allocator integration
