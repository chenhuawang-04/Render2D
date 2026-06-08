# Project Index

This document is the living file index for Render2D. It summarizes the purpose of each maintained source, test, benchmark, and architecture document.

## Root files

- `.clang-tidy` — Project clang-tidy configuration adapted from the Melosyne style rules.
- `.gitignore` — Ignores CMake/Ninja build output, user files, and generated compiler artifacts.
- `AGENTS.md` — Contributor guide for this repository.
- `CMakeLists.txt` — Root CMake project. Defines the `Render2D::Render2D` interface target, dependencies, warnings, tests, and benchmarks.
- `CMakePresets.json` — Default `clang-ninja-debug` configure/build/test preset.
- `Plan.md` — Long-term implementation plan and phase tracking.

## Public include tree

### Umbrella

- `include/Render2D/Render2D.hpp` — Umbrella public include for core, meta, component, native, and system contracts.

### Core

- `include/Render2D/Core/Types.hpp` — Fixed-width aliases and POD utility records such as `RangeU32`, `Aabb2`, and `Affine2X3`.
- `include/Render2D/Core/Result.hpp` — POD `SystemResult` and `SystemStatusCode` for CPU systems.
- `include/Render2D/Core/Version.hpp` — Version constants.

### Meta

- `include/Render2D/Meta/Provider.hpp` — Provider tag definitions and supported-provider gate.
- `include/Render2D/Meta/Dim.hpp` — Dimension tag definitions and supported-dimension gate.
- `include/Render2D/Meta/Domain.hpp` — Compile-time Provider/Dim domain gate.

### Component

- `include/Render2D/Component/ComponentFwd.hpp` — Forward declarations for ECS components.
- `include/Render2D/Component/StrictPod.hpp` — Strict POD component concept and validation helper.
- `include/Render2D/Component/ComponentTraits.hpp` — Supported component trait and `SupportedRenderComponent` concept.
- `include/Render2D/Component/Transform.hpp` — `Transform` and `WorldTransform`.
- `include/Render2D/Component/Bounds.hpp` — `LocalBounds` and `WorldBounds`.
- `include/Render2D/Component/Sprite.hpp` — Sprite-facing components and render references: `Sprite`, `MaterialRef`, `TextureRef`, `RenderLayer`, `VisibilityMask`.
- `include/Render2D/Component/Text.hpp` — Text input component and `FontRef`.
- `include/Render2D/Component/Camera.hpp` — Camera input component.
- `include/Render2D/Component/Command.hpp` — Visibility, sorting, draw command, and `CommandBuffer` descriptor components.
- `include/Render2D/Component/Batch.hpp` — `BatchCommand`.
- `include/Render2D/Component/Upload.hpp` — `UploadCommand` and `NativeSubmitCommand`.
- `include/Render2D/Component/Frame.hpp` — Frame/native state components such as `FrameIndex`, `FrameArenaState`, `DescriptorSlice`, `UploadRingSlice`, and `FenceState`.

### System

- `include/Render2D/System/SystemFwd.hpp` — Forward declarations for CPU systems.
- `include/Render2D/System/TransformSystem.hpp` — Converts `Transform[]` to `WorldTransform[]`.
- `include/Render2D/System/BoundsSystem.hpp` — Converts local bounds plus world transforms to world bounds.
- `include/Render2D/System/CullingSystem.hpp` — Produces `VisibleItem[]` using camera bounds and visibility masks.
- `include/Render2D/System/CommandBuildSystem.hpp` — Builds `DrawCommand[]` from visible items and sprites.
- `include/Render2D/System/BatchSystem.hpp` — Builds `BatchCommand[]` by merging compatible adjacent draw commands.
- `include/Render2D/System/CommandBufferSystem.hpp` — Builds and clears POD `CommandBuffer` range descriptors.

### Native

- `include/Render2D/Native/NativeFwd.hpp` — Forward declarations for native components and future runtime types.
- `include/Render2D/Native/NativeComponents.hpp` — Native POD ECS components such as `BufferRef`, `ImageRef`, `PipelineRef`, `DeviceHandle`, and `SwapchainState`.
- `include/Render2D/Native/NativeTypes.hpp` — Native runtime POD type contracts: status codes, object kinds, memory domains, IDs, generations, handles, and byte ranges.
- `include/Render2D/Native/NativeResult.hpp` — POD result records for future native runtime APIs.

### Memory and Storage

- `include/Render2D/Memory/RenderMemoryTags.hpp` — Memory domain tag types.
- `include/Render2D/Storage/StorageFwd.hpp` — Forward declaration for future native/resource runtime storage. ECS storage is intentionally not public production API.

## Tests

- `tests/CMakeLists.txt` — Test target registration.
- `tests/compile_smoke.cpp` — Umbrella compile smoke and broad static assertions.
- `tests/cpu_system_pipeline_test.cpp` — Full CPU pipeline test from transform to batch command.
- `tests/command_buffer_descriptor_test.cpp` — `CommandBuffer` descriptor build/clear behavior.
- `tests/native_components_test.cpp` — Native POD ECS component contract checks.
- `tests/native_runtime_contract_test.cpp` — Native runtime type/result POD contract checks.
- `tests/temporary_ecs_storage_test.cpp` — Test-only temporary ECS storage behavior.
- `tests/negative_non_pod_component.cpp` — Source used for expected compile failure.
- `tests/expect_compile_failure.cmake` — CMake script that validates negative compile tests.
- `tests/support/TemporaryEcsStorage.hpp` — Test-only temporary ECS storage. This is not production ECS.
- `tests/support/ComponentStreamView.hpp` — Test-only view helpers for temporary ECS storage.

## Benchmarks

- `bench/CMakeLists.txt` — Benchmark target registration.
- `bench/bench_smoke.cpp` — Minimal benchmark target smoke.
- `bench/null_cpu_bench.cpp` — Deterministic CPU-only ECS pipeline benchmark.

## Documentation

- `docs/ARCHITECTURE.md` — Top-level architecture overview.
- `docs/PROJECT_INDEX.md` — This file.
- `docs/ProjectMergeTODO.md` — Host-engine merge notes and migration constraints.
- `docs/adr/2026-06-08-component-first-vulkan-native-render2d.md` — Initial ADR for component-first Vulkan-native architecture.
- `docs/architecture/ECS_COMPONENT_STREAMS.md` — ECS stream and temporary storage boundary.
- `docs/architecture/STRICT_POD_COMPONENTS.md` — Strict POD component rules.
- `docs/architecture/PROVIDER_DIM_META.md` — Provider/Dim compile-time meta contract.
- `docs/architecture/VULKAN_NATIVE_ONLY.md` — Vulkan-native-only policy and native POD components.
- `docs/architecture/NATIVE_RUNTIME_CONTRACT.md` — Native runtime type and result contracts.
- `docs/architecture/NULL_CPU_BENCHMARK.md` — CPU-only benchmark design and usage.
