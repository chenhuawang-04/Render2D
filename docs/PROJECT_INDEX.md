# Project Index

This document is the living file index for Render2D. It summarizes the purpose of each maintained source, test, benchmark, and architecture document.

## Root files

- `.clang-tidy` - Project clang-tidy configuration adapted from the Melosyne style rules.
- `.gitignore` - Ignores CMake/Ninja build output, user files, and generated compiler artifacts.
- `AGENTS.md` - Contributor guide for this repository.
- `CMakeLists.txt` - Root CMake project. Defines the `Render2D::Render2D` interface target, the internal `render2d_thread_runtime_support` target, MemoryCenter/Vector_New/fast_math/ThreadCenter/Vulkan dependencies, warnings, tests, and benchmarks. Dormant `third_party/freetype` is intentionally not added here yet.
- `CMakePresets.json` - Debug and Perf configure/build/test presets, now aligned to the CMake 3.28 minimum required by embedded ThreadCenter.
- `Plan.md` - Long-term implementation plan and phase tracking.

## Public include tree

### Umbrella

- `include/Render2D/Render2D.hpp` - Umbrella public include for core, meta, component, native, and system contracts.

### Core

- `include/Render2D/Core/Types.hpp` - Fixed-width aliases, POD ranges, and fast_math-backed aliases/helpers for `Vec2`, `Mat3`, and `Aabb2`.
- `include/Render2D/Core/Result.hpp` - POD `SystemResult` and `SystemStatusCode` for CPU systems.
- `include/Render2D/Core/Version.hpp` - Version constants.

### Meta

- `include/Render2D/Meta/Provider.hpp` - Provider tag definitions and supported-provider gate.
- `include/Render2D/Meta/Dim.hpp` - Dimension tag definitions and supported-dimension gate.
- `include/Render2D/Meta/Domain.hpp` - Compile-time Provider/Dim domain gate.

### Component

- `include/Render2D/Component/ComponentFwd.hpp` - Forward declarations for ECS components.
- `include/Render2D/Component/StrictPod.hpp` - Strict POD component concept and validation helper.
- `include/Render2D/Component/ComponentTraits.hpp` - Supported component trait and `SupportedRenderComponent` concept.
- `include/Render2D/Component/Transform.hpp` - `Transform`, `WorldTransform`, and `TransformDirtyItem`; derived transforms store fast_math `Mat3`.
- `include/Render2D/Component/Bounds.hpp` - `LocalBounds` and `WorldBounds`; bounds store fast_math `Aabb2`.
- `include/Render2D/Component/Sprite.hpp` - Sprite-facing components and render references.
- `include/Render2D/Component/Text.hpp` - Text input, text dirty state/range, UTF-8 slice, font atlas, glyph run, and glyph instance POD components.
- `include/Render2D/Component/Camera.hpp` - Camera input component.
- `include/Render2D/Component/Command.hpp` - Visibility, sorting, draw command, and `CommandBuffer` descriptor components.
- `include/Render2D/Component/Batch.hpp` - `BatchCommand`.
- `include/Render2D/Component/Upload.hpp` - `UploadCommand` and `NativeSubmitCommand`.
- `include/Render2D/Component/Frame.hpp` - Frame/native state components such as `FrameIndex`, `DescriptorSlice`, and `FenceState`.

### System

- `include/Render2D/System/SystemFwd.hpp` - Forward declarations for CPU systems.
- `include/Render2D/System/TransformSystem.hpp` - Converts `Transform[]` to `WorldTransform[]`, including dirty-index updates and a zero-rotation fast path.
- `include/Render2D/System/BoundsSystem.hpp` - Converts local bounds plus world transforms to world bounds, including dirty-index updates.
- `include/Render2D/System/CullingSystem.hpp` - Produces `VisibleItem[]` using camera bounds and visibility masks.
- `include/Render2D/System/CommandBuildSystem.hpp` - Builds `DrawCommand[]` from visible items and sprites.
- `include/Render2D/System/BatchSystem.hpp` - Builds `BatchCommand[]` by merging compatible adjacent draw commands.
- `include/Render2D/System/SortKey.hpp` - Packed draw sort/batch key helpers.
- `include/Render2D/System/SortSystem.hpp` - Stable radix sort over `DrawCommand.sort_key` using caller-owned scratch spans.
- `include/Render2D/System/UploadDescriptorCompactionSystem.hpp` - Stage 10I allocation-free upload coalescing and descriptor slice compaction over ECS component streams.
- `include/Render2D/System/CommandBufferSystem.hpp` - Builds and clears POD `CommandBuffer` range descriptors.
- `include/Render2D/System/EncodeSystem.hpp` - CPU-only encode contract from `CommandBuffer` ranges to `NativeCommandBufferRef`.
- `include/Render2D/System/SubmitSystem.hpp` - CPU-only submit contract from native command refs and `FrameSync` to `NativeSubmitCommand`.
- `include/Render2D/System/TextSystem.hpp` - Stage 9 text dirty detection, dirty glyph-run/instance updates, and glyph-to-DrawCommand batching using `GlyphBuildConfig` / `GlyphDrawConfig`.
- `include/Render2D/System/ThreadedCpuPipeline.hpp` - Stage 10H ThreadCenter-backed runtime facade for deterministic chunked sprite CPU pipeline execution. It is not included by the umbrella header because consumers must link `render2d_thread_runtime_support`.

### Native

- `include/Render2D/Native/NativeFwd.hpp` - Forward declarations for native components and runtime types.
- `include/Render2D/Native/NativeComponents.hpp` - Native POD ECS components such as `BufferRef`, `ImageRef`, `PipelineRef`, `NativeCommandBufferRef`, `DeviceHandle`, `SwapchainState`, `AcquiredImage`, `PresentCommand`, and `DeferredDestroyCommand`.
- `include/Render2D/Native/NativeTypes.hpp` - Native runtime POD type contracts: status codes, object kinds, memory domains, IDs, generations, handles, and byte ranges.
- `include/Render2D/Native/NativeResult.hpp` - POD result records for native runtime APIs.
- `include/Render2D/Native/FrameRuntime.hpp` - CPU-side frame-in-flight runtime skeleton.
- `include/Render2D/Native/DeviceRuntime.hpp` - CPU-side device and queue handle slot runtime.
- `include/Render2D/Native/ResourceRuntime.hpp` - CPU-side buffer and image reference slot runtime.
- `include/Render2D/Native/PipelineRuntime.hpp` - CPU-side pipeline reference slot runtime.
- `include/Render2D/Native/DescriptorRuntime.hpp` - CPU-side descriptor slice slot runtime.
- `include/Render2D/Native/SwapchainRuntime.hpp` - CPU-side swapchain state slot runtime.
- `include/Render2D/Native/CommandRuntime.hpp` - CPU-side native command buffer ref slot runtime.
- `include/Render2D/Native/DeferredDestroyRuntime.hpp` - Stage 11 runtime queue for frame-safe deferred resource retirement using `McVector`.
- `include/Render2D/Native/VulkanCommandRuntime.hpp` - Vulkan command pool and command buffer lifecycle runtime behind `NativeCommandBufferRef`.
- `include/Render2D/Native/VulkanThreadCommandRuntime.hpp` - Stage 10J per-thread Vulkan command pool runtime behind `NativeCommandBufferRef`.
- `include/Render2D/Native/VulkanSyncRuntime.hpp` - Vulkan semaphore/fence lifecycle runtime behind `FrameSync`.
- `include/Render2D/Native/VulkanSubmitRuntime.hpp` - Vulkan queue submit runtime for resolved command buffers and frame sync.
- `include/Render2D/Native/VulkanResourceRuntime.hpp` - Vulkan buffer/image/image-view runtime with MemoryCenter-backed GPU allocation, upload/readback, and copy helpers.
- `include/Render2D/Native/VulkanDescriptorRuntime.hpp` - Vulkan descriptor pool, set layout, set allocation, and descriptor update runtime.
- `include/Render2D/Native/VulkanPipelineRuntime.hpp` - Vulkan shader module, pipeline cache, pipeline layout, and dynamic-rendering pipeline runtime.
- `include/Render2D/Native/VulkanUploadRingRuntime.hpp` - MemoryCenter-backed persistent mapped, frame-segmented upload ring runtime exposing `UploadRingSlice`.
- `include/Render2D/Native/VulkanRenderEncoder.hpp` - Dynamic rendering encoder for direct and indirect draw recording.

### Memory and Storage

- `include/Render2D/Memory/RenderMemoryTags.hpp` - Render2D memory-domain aliases mapped to MemoryCenter customer tags.
- `include/Render2D/Memory/RenderVector.hpp` - Project-wide `Render2D::McVector<T>` alias backed by Vector_New `mc_vector` and MemoryCenter.
- `include/Render2D/Memory/VulkanMemoryCenterAllocator.hpp` - Vulkan buffer/image allocation and mapped-range synchronization wrapper over MemoryCenter Vulkan adaptors.
- `include/Render2D/Storage/StorageFwd.hpp` - Forward declaration for future runtime storage. ECS storage is intentionally not public production API.

## Tests

- `tests/CMakeLists.txt` - Test target registration, release-like test assertion handling, and manual negative-compile include wiring for MemoryCenter, Vector_New, and fast_math.
- `tests/compile_smoke.cpp` - Umbrella compile smoke and broad static assertions.
- `tests/test_harness_test.cpp` - Self-test for the lightweight test assertion harness.
- `tests/thread_center_dependency_test.cpp` - Stage 10G smoke test proving ThreadCenter is available to runtime/system code without entering ECS components.
- `tests/threaded_cpu_pipeline_test.cpp` - Stage 10H single-thread equivalence and deterministic chunk merge coverage for `ThreadedCpuPipelineRuntime`.
- `tests/cpu_system_pipeline_test.cpp` - Full CPU pipeline test from transform to batch command.
- `tests/draw_sort_system_test.cpp` - Packed sort key, radix draw sort, batch merge, and collision-safety coverage.
- `tests/upload_descriptor_compaction_test.cpp` - Stage 10I upload coalescing, descriptor compaction, in-place, capacity, invalid-input, and unsupported-domain coverage.
- `tests/transform_dirty_system_test.cpp` - Sparse dirty transform/bounds update coverage.
- `tests/bounds_system_test.cpp` - fast_math AABB transform regression coverage for translation, scale, rotation, shear, and error paths.
- `tests/command_buffer_descriptor_test.cpp` - `CommandBuffer` descriptor build/clear behavior.
- `tests/text_glyph_components_test.cpp` - Text/Glyph Strict POD component contract and temporary stream storage behavior.
- `tests/text_glyph_system_test.cpp` - Stage 9B glyph-run and glyph-instance system behavior and error paths.
- `tests/text_stage9_pipeline_test.cpp` - Full Stage 9 text dirty, dirty glyph update, glyph draw command, and batch pipeline behavior.
- `tests/native_components_test.cpp` - Native POD ECS component contract checks.
- `tests/native_runtime_contract_test.cpp` - Native runtime type/result POD contract checks.
- `tests/native_deferred_destroy_runtime_test.cpp` - Stage 11 deferred destroy queue validation for safe-lag enqueue, capacity, drain ordering, and frame-index wrap.
- `tests/native_resource_runtime_test.cpp` - Buffer/image native slot table stale-reference and generation reuse checks.
- `tests/native_runtime_skeleton_test.cpp` - Frame/device/queue/pipeline/descriptor/swapchain runtime skeleton lifecycle checks.
- `tests/native_command_runtime_test.cpp` - Native command buffer ref slot lifecycle, stale-reference, and reuse checks.
- `tests/encode_submit_system_test.cpp` - CPU-only EncodeSystem and SubmitSystem contract checks.
- `tests/vulkan_command_runtime_test.cpp` - Optional Vulkan command pool / command buffer lifecycle smoke test.
- `tests/vulkan_thread_command_runtime_test.cpp` - Optional Stage 10J per-thread Vulkan command pool / command buffer ownership smoke test.
- `tests/vulkan_sync_runtime_test.cpp` - Optional Vulkan semaphore/fence lifecycle smoke test.
- `tests/vulkan_submit_runtime_test.cpp` - Optional Vulkan queue submit smoke test.
- `tests/vulkan_resource_runtime_test.cpp` - Optional Vulkan buffer/image/upload/readback/copy lifecycle smoke test.
- `tests/vulkan_descriptor_runtime_test.cpp` - Optional Vulkan descriptor pool/set/layout/update lifecycle smoke test.
- `tests/vulkan_pipeline_runtime_test.cpp` - Optional Vulkan shader module, pipeline cache, and dynamic-rendering pipeline lifecycle smoke test.
- `tests/vulkan_upload_ring_runtime_test.cpp` - Optional Vulkan persistent upload ring frame-slot reuse smoke test.
- `tests/vulkan_dynamic_render_encoder_test.cpp` - Optional offscreen dynamic rendering + indirect draw + readback smoke test.
- `tests/temporary_ecs_storage_test.cpp` - Test-only temporary ECS storage behavior.
- `tests/negative_non_pod_component.cpp` - Source used for expected compile failure.
- `tests/expect_compile_failure.cmake` - CMake script that validates negative compile tests with Render2D dependency include paths.
- `tests/support/TemporaryEcsStorage.hpp` - Test-only temporary ECS storage. This is not production ECS.
- `tests/support/ComponentStreamView.hpp` - Test-only view helpers for temporary ECS storage.
- `tests/support/VulkanSmokeContext.hpp` - Optional Vulkan instance/device/queue setup helper for smoke tests.
- `tests/support/FullScreenTriangleShaders.hpp` - Embedded SPIR-V for the offscreen full-screen triangle smoke test.
- `tests/support/TestHarness.hpp` - Lightweight no-dependency assertion helpers for CTest executables.

## Scripts

- `scripts/run_null_cpu_benchmarks.ps1` - Runs standard, dirty-transform, sorted, large, and huge local Null CPU benchmark suites; supports `-BuildDir` for Debug/Perf trees and writes timestamped CSV/Markdown reports.
- `scripts/run_threaded_cpu_benchmarks.ps1` - Runs Stage 10H ThreadCenter-backed sprite CPU pipeline benchmark scenarios and writes timestamped CSV/Markdown reports.

## Benchmarks

- `bench/CMakeLists.txt` - Benchmark target registration.
- `bench/bench_smoke.cpp` - Minimal benchmark target smoke.
- `bench/null_cpu_bench.cpp` - Deterministic CPU-only sprite/text/mixed ECS pipeline benchmark.
- `bench/upload_descriptor_compaction_bench.cpp` - Stage 10I Perf benchmark for synthetic upload-command coalescing and descriptor-slice compaction.
- `bench/threaded_cpu_pipeline_bench.cpp` - Stage 10H Perf benchmark comparing single-thread sprite CPU reference against `ThreadedCpuPipelineRuntime`.
- `bench/support/BenchmarkFramework.hpp` - Shared benchmark config parsing, timing accumulation, and text/CSV report helpers.

## Documentation

- `docs/ARCHITECTURE.md` - Top-level architecture overview.
- `docs/PROJECT_INDEX.md` - This file.
- `docs/ProjectMergeTODO.md` - Host-engine merge notes and migration constraints.
- `docs/adr/2026-06-08-component-first-vulkan-native-render2d.md` - Initial ADR for component-first Vulkan-native architecture.
- `docs/adr/2026-06-09-memorycenter-mcvector-runtime-memory.md` - ADR for MemoryCenter/McVector CPU runtime storage and Vulkan GPU allocation.
- `docs/adr/2026-06-09-text-glyph-pod-components.md` - ADR for Text/Glyph Strict POD data contracts.
- `docs/adr/2026-06-09-freetype-vendor-and-test-glyph-systems.md` - ADR for dormant FreeType vendor source and deterministic Stage 9B glyph systems.
- `docs/adr/2026-06-09-stage9-text-dirty-glyph-pipeline.md` - ADR for Stage 9 text dirty ranges, dirty glyph updates, and glyph draw-command batching.
- `docs/adr/2026-06-09-fast-math-pod-math-types.md` - ADR for replacing Render2D-owned math structs with fast_math POD aliases and free-function math.
- `docs/adr/2026-06-09-threadcenter-runtime-infrastructure.md` - ADR for embedding ThreadCenter as an internal runtime/system dependency only.
- `docs/adr/2026-06-09-threaded-cpu-pipeline-runtime.md` - ADR for ThreadCenter-backed deterministic CPU pipeline runtime execution.
- `docs/adr/2026-06-09-stage10-stream-compaction-thread-command-runtime.md` - ADR for Stage 10I upload/descriptor compaction and Stage 10J per-thread Vulkan command pools.
- `docs/adr/2026-06-09-stage11-frame-present-deferred-destroy.md` - ADR for Stage 11 frame/present POD contracts and the deferred destroy runtime queue.
- `docs/architecture/ECS_COMPONENT_STREAMS.md` - ECS stream and temporary storage boundary.
- `docs/architecture/STRICT_POD_COMPONENTS.md` - Strict POD component rules.
- `docs/architecture/PROVIDER_DIM_META.md` - Provider/Dim compile-time meta contract.
- `docs/architecture/VULKAN_NATIVE_ONLY.md` - Vulkan-native-only policy and native POD components.
- `docs/architecture/NATIVE_RUNTIME_CONTRACT.md` - Native runtime type, result, and CPU-side skeleton contracts.
- `docs/architecture/NULL_CPU_BENCHMARK.md` - CPU-only benchmark design and usage.
- `docs/architecture/BENCHMARK_BASELINE.md` - Stage 10 benchmark scenarios, runner usage, Debug/Perf captures, and optimization gate rule.
- `docs/architecture/STAGE10_PERFORMANCE_TODO.md` - Stage 10 completion checklist, ThreadCenter boundary, and remaining performance work items.
- `docs/architecture/STAGE11_NATIVE_FRAME_TODO.md` - Stage 11 native frame-loop checklist covering frame/present contracts, deferred destroy, swapchain acquire, and present.


## Third-party source snapshots

- `third_party/freetype/` - Copied FreeType source snapshot from `E:/Project/MelosyneTest/VulkanRender_New/freetype-master`. It is stored for future text work and is not currently built, linked, or included by Render2D CMake.
