组件可以脱离实体存在。

  ECS = component data streams + systems。

  Entity 只是可选 identity/index，不是 component 存在的前提。

  所有组件必须严格 POD。

  Provider / Dim 统一走模板元区分。

  当前唯一 Provider：VulkanNative。

  当前唯一 Dim：2D。



  0. 总原则



  Render2D 的核心不是“对象式 renderer”，而是：



  Strict POD Components

      ->

  Systems

      ->

  Derived Strict POD Components

      ->

  Vulkan Native Encode



  所有可持久化、可传递、可 batch、可排序的数据都以 POD component stream 存在。



  最终形态：



  Transform<VulkanNative, Dim2>[]

  Sprite<VulkanNative, Dim2>[]

  Text<VulkanNative, Dim2>[]

  Bounds<VulkanNative, Dim2>[]

  VisibleItem<VulkanNative, Dim2>[]

  DrawCommand<VulkanNative, Dim2>[]

  BatchCommand<VulkanNative, Dim2>[]

  UploadCommand<VulkanNative, Dim2>[]

  NativeSubmitCommand<VulkanNative, Dim2>[]



  系统只做转换：



  TransformSystem

  BoundsSystem

  CullingSystem

  CommandBuildSystem

  BatchSystem

  UploadSystem

  EncodeSystem

  SubmitSystem



  ———



  1. 严格 POD 组件定义



  C++23 里标准层面已经不推荐继续把 “POD” 当核心分类，所以工程上要显式定义 StrictPOD 规则。



  Render2D 的组件必须满足：



  std::is_trivial_v<T>

  std::is_standard_layout_v<T>

  std::is_trivially_copyable_v<T>

  std::is_aggregate_v<T>



  并且额外强制：



  不允许构造函数

  不允许析构函数

  不允许成员函数

  不允许虚函数

  不允许继承

  不允许 private/protected 字段

  不允许 std::string

  不允许 std::vector

  不允许 std::span

  不允许 std::string_view

  不允许 std::optional

  不允许 std::variant

  不允许 std::function

  不允许 owning pointer

  不允许 RAII 资源对象

  不允许默认成员初始化

  不允许异常语义



  允许：



  整数

  浮点

  固定底层类型 enum

  POD handle

  POD range

  POD offset/count

  POD pointer + length，前提是明确非 owning

  fast_math POD 类型

  Vulkan handle，前提是只作为值，不负责析构



  组件必须是这种形态：



  template<class Provider, class Dim>

  struct Sprite;



  不能是这种：



  class Sprite {

  public:

      Sprite();

      void update();

  private:

      Texture texture;

  };



  ———



  2. Provider / Dim 模板元规则



  任何名字只要牵扯 Provider 或 Dim，必须统一模板化。



  使用 tag type，不使用 runtime enum：



  struct VulkanNativeProvider {};

  struct Dim2 {};



  所有核心类型统一模板参数：



  template<class Provider, class Dim>

  struct Transform;



  template<class Provider, class Dim>

  struct Sprite;



  template<class Provider, class Dim>

  struct Text;



  template<class Provider, class Dim>

  struct DrawCommand;



  template<class Provider, class Dim>

  struct CommandBuffer;



  template<class Provider, class Dim>

  struct BatchCommand;



  template<class Provider, class Dim>

  struct UploadCommand;



  template<class Provider, class Dim>

  struct NativeSubmitCommand;



  当前只允许：



  Provider = VulkanNativeProvider

  Dim = Dim2



  计划中要有 compile-time gate：



  SupportedProvider<VulkanNativeProvider> == true

  SupportedDim<Dim2> == true



  其他 Provider / Dim 暂时全部编译失败。



  不使用：



  VulkanSprite2D

  OpenGLSprite2D

  Sprite2D

  SpriteVulkan



  统一使用：



  Sprite<VulkanNativeProvider, Dim2>



  必要时可以内部 alias：



  using Vk2Sprite = Sprite<VulkanNativeProvider, Dim2>;



  但 public architecture 仍以模板元为准。



  ———



  3. 组件分类



  组件分四类。



  3.1 输入组件



  这些来自用户 / 场景 / 上层逻辑：



  Transform<Provider, Dim>

  Sprite<Provider, Dim>

  Text<Provider, Dim>

  Camera<Provider, Dim>

  LocalBounds<Provider, Dim>

  VisibilityMask<Provider, Dim>

  RenderLayer<Provider, Dim>

  MaterialRef<Provider, Dim>

  TextureRef<Provider, Dim>

  FontRef<Provider, Dim>



  它们是原始 renderable data。



  3.2 派生组件



  这些由 system 生成：



  WorldTransform<Provider, Dim>

  WorldBounds<Provider, Dim>

  VisibleItem<Provider, Dim>

  SortedItem<Provider, Dim>

  DrawCommand<Provider, Dim>

  BatchCommand<Provider, Dim>

  UploadCommand<Provider, Dim>

  NativeSubmitCommand<Provider, Dim>



  它们不一定有 entity。



  例如：



  VisibleItem[] 可以只是一组 visible indices

  DrawCommand[] 可以只是一组可提交命令

  BatchCommand[] 可以只是一组批次描述



  3.3 帧组件



  这些表达 frame state：



  FrameIndex<Provider, Dim>

  FrameArenaState<Provider, Dim>

  CommandBuffer<Provider, Dim>

  UploadRingSlice<Provider, Dim>

  DescriptorSlice<Provider, Dim>

  FenceState<Provider, Dim>



  这些也必须是 POD。

  如果需要拥有 vector / allocator / RAII，那不叫 component，必须叫 storage 或 runtime object。



  3.4 Vulkan 原生组件



  这些只在 VulkanNativeProvider 下启用：



  DeviceHandle<VulkanNativeProvider, Dim2>

  QueueHandle<VulkanNativeProvider, Dim2>

  ImageHandle<VulkanNativeProvider, Dim2>

  BufferHandle<VulkanNativeProvider, Dim2>

  DescriptorRange<VulkanNativeProvider, Dim2>

  CommandPoolRef<VulkanNativeProvider, Dim2>

  NativeCommandBufferRef<VulkanNativeProvider, Dim2>



  注意：

  这些组件只保存 Vulkan handle / index / slice / state，不负责析构。



  资源销毁由 system + resource storage 处理。



  ———



  4. CommandBuffer 的严格 POD 设计



  因为组件必须 POD，所以 CommandBuffer<Provider, Dim> 不能直接持有 std::vector。



  错误设计：



  template<class Provider, class Dim>

  struct CommandBuffer {

      std::vector<DrawCommand<Provider, Dim>> commands;

  };



  正确设计：



  CommandBuffer<Provider, Dim>

    quad_first

    quad_count

    text_first

    text_count

    batch_first

    batch_count



  也就是说，CommandBuffer 是 POD descriptor。

  真正的数组由 storage 拥有：



  ComponentStorage<DrawCommand<Provider, Dim>>

  ComponentStorage<BatchCommand<Provider, Dim>>

  ComponentStorage<UploadCommand<Provider, Dim>>



  组件：



  CommandBuffer = offset/count 描述



  存储：



  CommandStorage = owning arrays



  这能同时满足：



  CommandBuffer 是 ECS component

  CommandBuffer 是严格 POD

  command 数据连续

  storage 可以用 MemoryCenter 管理



  ———



  5. Storage 与 Component 的边界



  必须严格区分：



  Component = POD 数据记录

  Storage = 临时/测试用 ECS 存储实现；最终由外部引擎 ECS 替换

  System = 转换数据的算法



  Storage 可以不是 POD，因为它负责内存：



  ComponentStreamStorage<T>

  SoAStorage<T>

  FrameArenaStorage<T>

  ResourceStorage<T>



  但任何叫 Component 的东西都必须 POD。



  命名规则：



  Sprite<Provider, Dim>                 // POD component

  SpriteStorage<Provider, Dim>          // owning storage

  SpriteStreamView<Provider, Dim>       // non-owning view，不是 component



  不能把 storage 伪装成 component。

  重要边界：VisibleItem、SortedItem、DrawCommand、BatchCommand、UploadCommand、NativeSubmitCommand 仍然视为 ECS component。当前仓库如果实现 Storage，只作为 tests / bench 使用的临时 ECS 框架，不作为最终生产 ECS。Render2D 生产接口应尽量依赖 view / range / descriptor，方便未来迁移到外部引擎 ECS。



  ———



  6. System 设计规则



  System 不持有业务状态。

  System 是模板化函数或 stateless 类型。



  形式：



  template<class Provider, class Dim>

  struct CommandBuildSystem;



  或者：



  template<class Provider, class Dim>

  SystemResult runCommandBuildSystem(SystemContext<Provider, Dim>* ctx) noexcept;



  System 规则：



  不使用 virtual

  不使用 std::function

  不在 hot path 分配内存

  不抛异常

  输入输出显式声明

  只读哪些 component stream

  写入哪些 component stream

  错误返回 POD status



  System 状态如果必须存在，也用 POD component 表示：



  CommandBuildStats<Provider, Dim>

  CullingStats<Provider, Dim>

  BatchStats<Provider, Dim>



  ———



  7. 核心数据管线



  主 pipeline：



  Input:

    Transform<Provider, Dim>[]

    Sprite<Provider, Dim>[]

    Text<Provider, Dim>[]

    Camera<Provider, Dim>[]



  Stage 1:

    WorldTransform<Provider, Dim>[]



  Stage 2:

    WorldBounds<Provider, Dim>[]



  Stage 3:

    VisibleItem<Provider, Dim>[]



  Stage 4:

    DrawCommand<Provider, Dim>[]



  Stage 5:

    BatchCommand<Provider, Dim>[]



  Stage 6:

    UploadCommand<Provider, Dim>[]



  Stage 7:

    NativeCommand<Provider, Dim>[]



  Stage 8:

    NativeSubmitCommand<Provider, Dim>[]



  每个阶段的输出仍然是 component。



  ———



  8. Text 组件的 POD 设计



  不能用 std::string 或 std::string_view。



  推荐：



  Utf8Slice

    buffer_id

    offset

    byte_count



  Text component：



  Text<Provider, Dim>

    font_id

    utf8_buffer_id

    utf8_offset

    utf8_size

    color_rgba8

    pixel_size

    layer

    flags



  Glyph 派生组件：



  GlyphRun<Provider, Dim>

    source_text_index

    glyph_first

    glyph_count

    atlas_id



  Glyph instance：



  GlyphInstance<Provider, Dim>

    glyph_id

    atlas_rect

    position

    color

    sort_key



  文本系统：



  Text<Provider, Dim>[]

    ->

  GlyphRun<Provider, Dim>[]

    ->

  GlyphInstance<Provider, Dim>[]

    ->

  DrawCommand<Provider, Dim>[]



  ———



  9. Vulkan Native Provider 规划



  当前唯一 Provider 是 Vulkan Native。



  Provider 层不做抽象后端兼容。

  先完整做好 Vulkan。



  Vulkan native runtime 需要这些 component/storage：



  DeviceHandle<VulkanNativeProvider, Dim2>

  QueueHandle<VulkanNativeProvider, Dim2>

  SwapchainState<VulkanNativeProvider, Dim2>

  FrameSync<VulkanNativeProvider, Dim2>

  DescriptorSlice<VulkanNativeProvider, Dim2>

  UploadSlice<VulkanNativeProvider, Dim2>

  PipelineRef<VulkanNativeProvider, Dim2>

  ImageRef<VulkanNativeProvider, Dim2>

  BufferRef<VulkanNativeProvider, Dim2>



  它们仍然是 POD handle/index/slice。



  实际 RAII 管理放在 storage/runtime：



  DeviceStorage

  ResourceStorage

  DescriptorStorage

  CommandStorage



  这些不叫 component。



  ———



  10. MemoryCenter 使用规则



  MemoryCenter 用于 storage，不直接塞进 component。



  分配域：



  PersistentComponentStorage   // test-only temporary ECS

  FrameComponentStorage        // test-only temporary ECS

  UploadStorage                // Render2D runtime

  GpuResourceStorage           // Render2D runtime

  Runtime slot arrays          // McVector / MemoryCenter-backed



  目标：



  warmup 后每帧 0 general heap allocation

  frame temporary 数据批量 reset

  component stream capacity 可复用

  GPU upload ring 多帧 in-flight 安全复用



  组件中只放：



  offset

  count

  handle

  id

  slice

  index



  不放 allocator。



  ———



  11. fast_math usage rule



  fast_math POD types are allowed in component fields only after static_assert validation.



  Required types:



  MMath::Vec2

  MMath::Mat3

  MMath::Aabb2



  Render2D exposes only these aliases:



  Render2D::Vec2

  Render2D::Mat3

  Render2D::Aabb2



  Do not reintroduce Render2D-owned vector, matrix, affine, or AABB types.



  Transform input components stay scalar:



  position_x

  position_y

  rotation

  scale_x

  scale_y



  Derived WorldTransform uses:



  Mat3



  ———



  12. 阶段划分



  ## 阶段 0：架构合同冻结（已完成）



  目标：先把规则写死，不写渲染功能。



  交付物：



  docs/architecture/STRICT_POD_COMPONENTS.md

  docs/architecture/PROVIDER_DIM_META.md

  docs/architecture/ECS_COMPONENT_STREAMS.md

  docs/architecture/VULKAN_NATIVE_ONLY.md



  必须确定：



  StrictPOD concept

  Provider tag

  Dim tag

  组件命名规则

  系统命名规则

  storage/component 边界



  验收：



  所有组件规则清晰

  所有 Provider/Dim 类型模板化

  确认当前只支持 VulkanNativeProvider + Dim2



  ———



  ## 阶段 1：工程骨架（已完成）



  目标：建立 C++23 工程，但只做基础设施。



  交付物：



  CMakeLists.txt

  include/Render2D/Core/

  include/Render2D/Meta/

  include/Render2D/Component/

  include/Render2D/System/

  include/Render2D/Storage/

  tests/

  bench/



  接入：



  MemoryCenterNew

  fast_math

  Vulkan SDK



  验收：



  C++23 build clean

  依赖接入 clean

  不构建依赖自己的 tests/bench

  基础 compile test 通过



  ———



  ## 阶段 2：Strict POD Component Contract（已完成）



  目标：实现所有组件静态验证机制。



  当前状态：StrictPodComponent concept、ComponentTraits、首批组件定义、static_assert suite、negative compile test 已完成。



  交付物：



  StrictPodComponent concept

  ComponentTraits<Provider, Dim, Component>

  static_assert suite

  negative compile tests



  首批组件：



  Transform<VulkanNativeProvider, Dim2>

  WorldTransform<VulkanNativeProvider, Dim2>

  Sprite<VulkanNativeProvider, Dim2>

  Text<VulkanNativeProvider, Dim2>

  Camera<VulkanNativeProvider, Dim2>

  Bounds<VulkanNativeProvider, Dim2>

  VisibleItem<VulkanNativeProvider, Dim2>

  DrawCommand<VulkanNativeProvider, Dim2>

  BatchCommand<VulkanNativeProvider, Dim2>

  CommandBuffer<VulkanNativeProvider, Dim2>



  验收：



  每个 component 都通过 StrictPOD static_assert

  非 POD 字段会编译失败

  Provider/Dim 错误组合会编译失败



  ———



  ## 阶段 3：临时测试 ECS Storage（已完成）

  当前状态：tests/support/TemporaryEcsStorage.hpp, tests/support/ComponentStreamView.hpp, storage behavior test completed.

  目标：为 tests / bench 搭建临时 ECS 组件存储，用于验证 Render2D 的 component 和 system；该 Storage 不是最终架构，未来由外部引擎 ECS 替换。

  交付物：

  tests/support/TemporaryEcsStorage.hpp
  tests/support/ComponentStreamView.hpp
  ComponentStreamStorage<Provider, Dim, Component>    // test-only
  FrameComponentStorage<Provider, Dim, Component>     // test-only
  PersistentComponentStorage<Provider, Dim, Component>// test-only

  要求：

  连续数组
  reserve / clear / reset
  offset/count 访问
  无每元素 heap allocation
  不进入 Render2D public production API

  验收：

  push / append / clear / reset 正确
  capacity 复用
  frame reset 后生命周期正确
  可替换为外部 ECS dense storage
  storage 不是 component，只是临时 ECS 框架




  ———



  ## 阶段 4：基础系统链路（已完成）



  目标：实现纯 CPU component -> component 转换。

  当前状态：TransformSystem、BoundsSystem、CullingSystem、CommandBuildSystem、BatchSystem 已完成；system 使用 std::span 作为非 owning 输入/输出边界，不依赖临时 Storage。



  系统：



  TransformSystem

  BoundsSystem

  CullingSystem

  CommandBuildSystem

  BatchSystem



  输入输出：



  Transform -> WorldTransform

  WorldTransform + LocalBounds -> WorldBounds

  WorldBounds + Camera -> VisibleItem

  VisibleItem + Sprite/Text -> DrawCommand

  DrawCommand -> BatchCommand



  验收：



  所有输出仍是 Strict POD component stream

  无 entity 依赖

  无 virtual

  无 hot-path allocation

  单元测试覆盖 offset/count/source_id



  ———



  ## 阶段 5：CommandBuffer ECS 化（已完成）



  目标：把 CommandBuffer 定义成 POD component descriptor。

  当前状态：CommandBufferBuildSystem、CommandBufferClearSystem、command_buffer_descriptor 测试已完成。



  组件：



  CommandBuffer<VulkanNativeProvider, Dim2>

    frame_index

    draw_first

    draw_count

    batch_first

    batch_count

    upload_first

    upload_count



  storage：



  DrawCommandStorage

  BatchCommandStorage

  UploadCommandStorage



  系统：



  CommandBufferBuildSystem

  CommandBufferClearSystem



  验收：



  CommandBuffer 是 Strict POD

  CommandBuffer 不持有 vector

  CommandBuffer 指向 command stream ranges



  ———



  ## 阶段 6：Null CPU Benchmark（已完成）

  当前状态：render2d_null_cpu_bench baseline/test framework upgraded (done): covers sprite, text, and mixed CPU component pipelines; supports --scenario, --sprites, --texts, --frames, --warmup, --visibility, --glyphs-per-text, --dirty-text-stride, and --format text|csv; CTest includes sprite/text/mixed benchmark smokes.




  目标：先不接 Vulkan，测 ECS/component pipeline 性能。



  场景：



  10k sprites

  100k sprites

  1M sprites

  随机 layer/material/texture

  高可见率

  低可见率

  大量 text

  大量 dirty transform



  指标：



  Transform pass time

  Bounds pass time

  Culling pass time

  Command build time

  Sort time

  Batch count

  Memory allocation count

  Bytes touched

  Cache behavior



  验收：



  warmup 后每帧 0 general heap allocation

  所有系统输出数量正确

  benchmark 可复现



  ———



  ## 阶段 7：Vulkan Native Resource Components（7A Native POD Components 已完成）

  7A 当前状态：NativeComponents.hpp 和 native_components 测试已完成；仅定义 POD ECS component，不做 Vulkan runtime/storage/RAII。

  7B-1 status: NativeTypes.hpp, NativeResult.hpp, and native_runtime_contract test are complete; this only defines runtime POD contracts and does not implement Vulkan resource tables.

  7B-2 status: NativeResourceRuntime CPU-side slot table is complete for BufferRef/ImageRef; it supports reserve, create placeholder refs, resolve, stale-reference rejection, release, and id reuse with generation increment. No Vulkan API is called.

  7B-3 status (done): NativeFrameRuntime / NativeDeviceRuntime / NativePipelineRuntime / NativeDescriptorRuntime / NativeSwapchainRuntime CPU-side skeletons are complete; covered frame slot rotation, device/queue, pipeline, descriptor, and swapchain reserve/create/resolve/release/reuse with generation stale checks. This stage still calls no Vulkan API.



  目标：建立 Vulkan 资源 POD component 与 runtime storage。



  组件：



  BufferRef<VulkanNativeProvider, Dim2>

  ImageRef<VulkanNativeProvider, Dim2>

  DescriptorSlice<VulkanNativeProvider, Dim2>

  UploadSlice<VulkanNativeProvider, Dim2>

  PipelineRef<VulkanNativeProvider, Dim2>

  FrameSync<VulkanNativeProvider, Dim2>



  storage：



  BufferStorage

  ImageStorage

  DescriptorStorage

  PipelineStorage

  FrameStorage



  验收：



  component 只保存 handle/index/slice

  storage 负责创建销毁

  Vulkan validation layer clean

  MemoryCenter Vulkan allocation 接入



  ———



  ## 阶段 8：Vulkan Native Encoder

  8A status (done): CPU-side NativeCommandBufferRef, NativeCommandRuntime, EncodeSystem, and SubmitSystem are complete. CommandBuffer + BatchCommand[] + UploadCommand[] now produce NativeCommandBufferRef, then NativeSubmitCommand, with id + generation validation and no Vulkan API calls.

  8B status (done): VulkanCommandRuntime is complete for real VkCommandPool and VkCommandBuffer lifecycle. It creates/destroys command pools, allocates/resolves/begins/ends/resets/releases command buffers behind NativeCommandBufferRef, keeps id + generation validation, and still does not record draw commands or submit queues.

  8C status (done): VulkanSyncRuntime and VulkanSubmitRuntime are complete for real VkSemaphore/VkFence lifecycle and vkQueueSubmit. FrameSync now carries sync_id + generation and stale sync references are rejected.

  8D status (done): VulkanResourceRuntime is complete for real VkBuffer/VkImage/VkImageView lifetimes, MemoryCenter-backed GPU allocation, host-visible upload/readback buffers, device-local buffers/images, buffer copy, image layout transition, image-to-buffer readback, and stale BufferRef/ImageRef rejection.

  8E status (done): VulkanDescriptorRuntime and VulkanPipelineRuntime are complete for descriptor pool/set/layout allocation, descriptor array updates, shader module creation, pipeline cache, dynamic-rendering graphics pipeline creation, and PipelineRef id + generation validation.

  8F status (done): VulkanUploadRingRuntime is complete as a MemoryCenter-backed persistent mapped, frame-segmented upload ring. UploadRingSlice is Strict POD and now includes generation; a frame slot cannot be reused until completeFrame is called after GPU completion.

  8G status (done): VulkanDynamicRenderEncoder records dynamic rendering, viewport/scissor, pipeline bind, direct draw, and indirect draw from UploadRingSlice. The offscreen smoke test renders a magenta full-screen sprite into an R8G8B8A8 image and verifies readback bytes.

  8H status (done): Project memory migration is complete. Render2D-owned dynamic arrays use `Render2D::McVector<T>` from Vector_New/MemoryCenter, and Vulkan resource/upload backing memory uses `VulkanMemoryCenterAllocator` instead of direct Vulkan memory allocation calls.



  目标：把 BatchCommand 编码为 Vulkan command buffer。



  系统：



  EncodeSystem<VulkanNativeProvider, Dim2>

  SubmitSystem<VulkanNativeProvider, Dim2>



  输入：



  CommandBuffer

  BatchCommand[]

  UploadCommand[]

  Provider native resource components

  FrameSync



  输出：



  NativeCommandBufferRef

  NativeSubmitCommand



  策略：



  dynamic rendering

  descriptor indexing

  persistent upload ring

  instance buffer

  indirect draw

  pipeline cache



  验收：



  可显示 sprite

  validation clean

  RenderDoc 可抓帧

  frame-in-flight 安全

  descriptor 不提前覆盖

  upload slice 不提前复用



  ———



  ## 阶段 9：Text / Glyph Pipeline

  9A status (done): Text/Glyph Strict POD component contract is complete. `Utf8Slice`, `GlyphRun`, `GlyphInstance`, and `FontAtlasRef` are registered as supported ECS components, with tests covering POD traits and temporary ECS stream storage. No font library, shaping, atlas packing, or Vulkan text draw is implemented in 9A.

  9B status (done): Deterministic test glyph systems are complete. `GlyphRunBuildSystem` maps `Text[]` to `GlyphRun[]` by font atlas id and UTF-8 byte count, while `GlyphInstanceBuildSystem` expands runs into placeholder `GlyphInstance[]` using `GlyphBuildConfig`. FreeType has been copied to `third_party/freetype` for future integration but is not connected to CMake or runtime yet.

  9C status (done): `TextDirtySystem` is complete. It compares `Text[]` with previous `TextState[]`, writes next `TextState[]`, and emits `TextDirtyRange[]` so unchanged static text does not need glyph rebuild work.

  9D status (done): Dirty update paths are complete. `GlyphRunBuildSystem::runDirty` and `GlyphInstanceBuildSystem::runDirty` update only dirty text/glyph ranges while preserving host-ECS-owned streams.

  9E status (done): `GlyphBatchSystem` is complete. Glyph runs are converted into regular `DrawCommand[]` ranges over `GlyphInstance[]`, then enter the existing `BatchSystem` pipeline.



  目标：文本也保持 Strict POD component stream。



  组件：



  Text

  TextState

  TextDirtyRange

  Utf8Slice

  GlyphRun

  GlyphInstance

  FontAtlasRef



  系统：



  TextDirtySystem

  GlyphRunBuildSystem

  GlyphInstanceBuildSystem

  GlyphBatchSystem



  验收：



  Text component 不含 string/string_view

  静态文本不每帧重建 glyph

  dirty text 只更新变化范围

  glyph instance 进入 DrawCommand pipeline



  ———



  ## 阶段 10：性能优化阶段



  优化项：



  10A status (done): test/bench framework is complete. Shared TestHarness and BenchmarkFramework exist; Null CPU benchmark now produces reproducible sprite/text/mixed baselines with warmup, visibility mode, dirty text cadence, and text/csv reports.


  10B status (done): standard benchmark baseline is complete. `scripts/run_null_cpu_benchmarks.ps1` runs sprite_high_10k, sprite_low_10k, text_static_2k, text_dirty_2k, and mixed_10k_2k, writes timestamped CSV/Markdown reports under build/bench_results, and `docs/architecture/BENCHMARK_BASELINE.md` records the current local reference capture and gate rule.


  10C status (done): fast_math migration is complete. Render2D-owned `Aabb2` / `Affine2X3` structs were removed; `Vec2`, `Mat3`, and `Aabb2` now alias `MMath` POD types. Transform, bounds, culling, and text atlas math use fast_math free functions. BoundsSystem uses the center/extents formula and reduced the local Debug 10k sprite bounds pass from ~3.64 ms to ~0.55 ms.


  10D status (done): benchmark/profile harness is complete. Added RelWithDebInfo `clang-ninja-perf`, release-like test assertion handling, dirty-transform benchmark mutation, `-BuildDir`, `-IncludeDirtyTransform`, `-IncludeLarge`, and `-IncludeHuge` runner support. Stage 10 completion TODO and verification are tracked in `docs/architecture/STAGE10_PERFORMANCE_TODO.md`.


  10E status (done): single-thread Transform/Bounds hot path is optimized. Added Strict POD `TransformDirtyItem`, `TransformSystem::runDirty`, `BoundsSystem::runDirty`, benchmark dirty-index feeding, and a zero-rotation transform fast path. Perf 10k static transform is now about 3.0x-3.9x faster; dirty transform/bounds scenarios now update sparse indices instead of full spatial streams.


  10F status (done): sort/batch-key foundation is complete. Added packed `makeDrawSortKey`, explicit `DrawSortSystem` stable radix sort over `DrawCommand.sort_key`, `--enable-sort` benchmark mode, and BatchSystem packed-key-first comparison with full collision-safe verification. Sorting is opt-in because it reduces batch counts sharply but has measurable CPU cost.



  SIMD bounds/culling

  dirty transform propagation

  SoA transform columns

  draw command compaction

  upload coalescing

  descriptor table compaction

  multi-thread command build

  per-thread command stream append

  per-thread Vulkan command pools



  验收：



  benchmark 前后对比

  不能只凭感觉优化

  每个优化有数据

  性能回归进入 CI gate



  ———



  ## 阶段 11：质量门和回归体系



  每个阶段必须有：



  unit tests

  component static_assert tests

  negative compile tests

  storage lifecycle tests

  system input/output tests

  benchmark smoke



  Vulkan 阶段增加：



  validation layer tests

  RenderDoc manual capture checklist

  GPU resource lifetime tests

  descriptor lifetime tests

  frame-in-flight tests



  CI / 本地命令目标：



  configure

  build

  ctest

  bench smoke

  vulkan validation smoke



  ———



  13. 命名规范



  组件：



  Transform<Provider, Dim>

  Sprite<Provider, Dim>

  Text<Provider, Dim>

  DrawCommand<Provider, Dim>

  BatchCommand<Provider, Dim>



  系统：



  TransformSystem<Provider, Dim>

  CullingSystem<Provider, Dim>

  CommandBuildSystem<Provider, Dim>

  EncodeSystem<Provider, Dim>



  storage：



  ComponentStreamStorage<Provider, Dim, Component>

  FrameStorage<Provider, Dim>

  ResourceStorage<Provider, Dim>



  禁止：



  Sprite2D

  VulkanSprite

  Vulkan2DSprite

  OpenGLText

  D3DCommand



  允许内部 alias，但不能污染主 API：



  using Vk2Sprite = Sprite<VulkanNativeProvider, Dim2>;



  ———



  14. 最终目标结构



  最终模块应长这样：



  Render2D

    Meta

      Provider.hpp

      Dim.hpp



    Component

      StrictPod.hpp

      Transform.hpp

      Sprite.hpp

      Text.hpp

      Bounds.hpp

      Camera.hpp

      Command.hpp

      Batch.hpp

      NativeComponents.hpp



    TestSupport

      TemporaryEcsStorage.hpp

      ComponentStreamView.hpp



    Native

      ResourceStorage.hpp

      FrameStorage.hpp



    System

      TransformSystem.hpp

      BoundsSystem.hpp

      CullingSystem.hpp

      CommandBuildSystem.hpp

      BatchSystem.hpp

      EncodeSystem.hpp

      SubmitSystem.hpp



    Memory

      RenderMemoryTags.hpp



    Native

      DeviceRuntime.hpp

      ResourceRuntime.hpp

      DescriptorRuntime.hpp

      FrameRuntime.hpp



    Bench

    Tests

    Docs



  ———



  15. 这版规划的关键结论



  1. 所有组件都是严格 POD。

  2. 组件可以脱离 entity 独立存在。

  3. CommandBuffer 是 ECS component，但只能是 POD descriptor。

  4. 真正 owning arrays 当前可放在 test-only temporary ECS storage；未来由外部引擎 ECS 接管。

  5. Provider / Dim 全部模板元区分。

  6. 当前唯一合法组合是 VulkanNativeProvider + Dim2。

  7. 系统是 component stream 到 component stream 的转换。

  8. Vulkan native 资源状态也可以是 POD component。

  9. Vulkan RAII 生命周期由 storage/runtime/system 处理。

  10. 性能先通过 Null CPU pipeline benchmark 验证，再进入 Vulkan。



