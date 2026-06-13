# Render2D 补强规划书 (Reinforcement Plan)

本文件是 `Plan.md` 的延续。`Plan.md` 覆盖到 Stage 16(已完成),本规划从 **Stage 17** 起,定义把 Render2D 从"离屏验证的库"推进到"可嵌入宿主引擎的生产渲染后端"所需的补强路线。

阅读前置:`Plan.md`(总规划)、`docs/ARCHITECTURE.md`(管线与 runtime 清单)、`docs/ProjectMergeTODO.md`(39 条宿主合并约束)、`docs/architecture/BENCHMARK_BASELINE.md`(性能基线)。

---

## 0. 现状基线(2026-06-11)

- 已完成 Stage 0–16:CPU ECS 管线、全套 Vulkan runtime、Sprite GPU 路径、纹理采样、多纹理/多 packet 批次、texture atlas UV region 采样。
- **所有可见性证明都是离屏 `R8G8B8A8` 像素回读;尚无一帧真正呈现到窗口。**
- **整个库仅在 `tests/support/` 的临时 ECS 上验证,尚未接入任何宿主引擎。**
- 性能阶段(Stage 10)已收口;`STAGE10_PERFORMANCE_TODO.md` 标注 "Remaining Route: None"。

补强要解决的六类缺口:① 生产级 atlas 图片 runtime;② 真实字体/文本 runtime;③ material/descriptor 扩展策略;④ 并行尾段与剩余性能项;⑤ 上屏呈现与抓帧;⑥ 宿主引擎集成。外加一类工程缺口:⑦ 构建可移植性与回归门禁(当前无 CI、依赖路径硬编码)。

---

## 1. 不可破坏的红线(贯穿所有阶段)

任何补强阶段都不得破坏以下既有合同。每条都已有 ADR 或架构文档背书:

1. **组件严格 POD**:trivial + standard_layout + trivially_copyable + aggregate;不得引入 `std::string/vector/span`、构造/析构/虚函数、RAII、owning 资源。
2. **宿主 ECS 拥有组件流**:Render2D 不引入生产 ECS storage;系统只通过 `std::span` 读写,`tests/support/` 仅为临时测试 ECS。
3. **Render2D 自有动态数组用 `Render2D::McVector<T>`**(MemoryCenter/Vector_New);src/tests/bench 不得出现 `std::vector`。
4. **Vulkan 显存只走 `VulkanMemoryCenterAllocator`**;禁止直接调用 `vkAllocateMemory/vkFreeMemory/vkMapMemory/vkBind*Memory`。
5. **数学只用 fast_math**:`Vec2/Mat3/Aabb2` 是 `MMath` 别名;AABB 经 `makeAabb2/aabb2Min/aabb2Max` 访问。
6. **原生句柄经 `id + generation` 暴露给 ECS**;runtime 表校验 generation 并拒绝 stale 引用;Vulkan 句柄不进组件。
7. **ThreadCenter 仅为 runtime/system 基础设施**;不进 ECS 组件,不进 public 伞形头;单线程系统始终是正确性参考。
8. **流尺寸 U32 有界**;不可表示的尺寸以 `SystemStatusCode::InvalidInput` 拒绝。
9. **契约变更必须配 ADR**(provider/storage/API/dependency 任一变动),并同步更新 `docs/PROJECT_INDEX.md` 与 `docs/ARCHITECTURE.md`。
10. **每个优化记录 benchmark before/after**;无数据不优化。
11. **窗口/`VkSurfaceKHR` 所有权属于宿主**;Render2D 可创建或 adopt swapchain,但不拥有窗口。

> 这些红线本身就是补强目标的一部分:Stage 17D 会把红线 3/4/5 的"手动扫描"脚本化并纳入门禁。

---

## 2. 补强总览与排序

两条轨道并行推进。全局编号只表示"建议顺序",不是强制链;依赖关系见下表。

| Stage | 主题 | 轨道 | 依赖 | 关键产物 |
|---|---|---|---|---|
| 17 | 构建可移植性 + 回归门禁 | 工程 | 无 | 依赖解耦、CI、脚本化约束扫描 |
| 18 | 生产级 Texture Atlas 图片 runtime | 功能 | 15/16 | `VulkanTextureAtlasRuntime`、真实合图、退役 |
| 19 | 真实字体/文本 runtime(FreeType) | 功能 | 18 | UTF-8 解码、shaping、字形光栅、文本绘制 |
| 20 | Material / Descriptor 扩展策略 | 功能 | 14;建议18 | bindless/descriptor-indexing、多纹理批次、material graph |
| 21 | 并行尾段与剩余性能项 | 工程/性能 | 17(门禁) | 文本并行、并行 batch/sort、SIMD/SoA(数据驱动) |
| 22 | 上屏呈现与可见抓帧 | 功能/集成 | 11 | 真窗口呈现一帧、抓帧/RenderDoc 自动化 |
| 23 | 宿主引擎集成 / Merge | 集成 | 全部 | ECS 适配层、39 条约束落实、端到端帧 |

**关键路径建议**:`17(安全网) → 18(atlas) → 19(文本)` 是主功能链;`20`、`21`、`22` 可在其后或并行;`23` 是收敛终点。若以功能交付为先,**Stage 18 不依赖 17,可立即启动**,17 作为并行的工程加固。

---

## 3. 阶段详述

### Stage 17 — 构建可移植性与回归门禁(工程补强 · 前置)

**目标**:在大改动前建立回归安全网,并让仓库可在干净环境(含 CI runner)构建。当前 `CMakeLists.txt` 硬编码 `E:/Project/MelosyneTest/...`,且仓库无任何 CI。

- **17A 依赖解耦**:为 `MemoryCenterNew / fast_math / Vector_New / ThreadCenter` 提供可移植获取方式(FetchContent 或 git submodule),保留 `RENDER2D_*_SOURCE_DIR` 覆盖能力作为本地 fallback;configure 失败信息给出明确指引。
- **17B CI 流水线**:configure→build→ctest(`clang-ninja-debug` 与 `clang-ninja-perf`)→clang-tidy→`git diff --check`→bench smoke。无 GPU 的 runner 上 Vulkan smoke 走既有 graceful-skip(`return 0`)。
- **17C Validation-layer smoke**:在具备 GPU 的环境启用 Vulkan validation layer 跑离屏 smoke,validation 必须 clean;无 GPU 时跳过并记录。
- **17D 约束扫描脚本化**:把 Stage 10K 手动做的 `std::vector` 扫描、直接 Vulkan memory API 扫描、旧 math 扫描固化为 `scripts/` 脚本并纳入 CI。

**边界**:不改变任何 ECS/系统契约;纯构建与门禁加固。
**验收**:干净环境可 configure/build;CI 全绿;约束扫描脚本可独立运行并在违例时失败;`docs/PROJECT_INDEX.md` 收录新脚本。

---

### Stage 18 — 生产级 Texture Atlas 图片 Runtime(功能补强)

**目标**:Stage 15/16 已证明 atlas region 的 UV 链路,但 `TextureAtlasBuildSystem` 只在调用方 span 里算 UV 矩形,**不创建、不上传、不拥有 atlas 图片**。本阶段补上图片侧 runtime。

- **18A `VulkanTextureAtlasRuntime` 骨架**:runtime 拥有 atlas `VkImage`/view,经 `VulkanResourceRuntime` + MemoryCenter 分配;对 ECS 仅暴露 `image_id/region` 的 `id + generation`。
- **18B 子图上传与合成**:用既有 `VulkanResourceRuntime::recordCopyBufferToImage` 把多张源子图打进单张 atlas image;校验 region 矩形不越界、格式字节一致。
- **18C 进阶 bin packing**:在确定性、无 hot-path 分配前提下,提供优于 shelf 的打包(如 MaxRects/skyline),输出仍为 `TextureAtlasRegion[]`,与 `TextureAtlasBuildSystem` 共用 region 契约。
- **18D 动态更新与退役**:支持运行时新增/失效 region,旧 atlas 图片经 Stage 11 `NativeDeferredDestroyRuntime` 帧安全退役。
- **18E 离屏 smoke**:把≥3 张不同颜色子图真实合成进一张 atlas,经 `runWithTextureRegions` 生成实例,单 descriptor 渲染,逐区域 readback 验证采样正确。
- **18F 收口**:ADR + `PROJECT_INDEX` + Debug/Perf 验证 + clang-tidy + 约束扫描。

**边界**:atlas 图片、源 raster 数据、打包元数据都是 runtime/host 资源;ECS 只持有 `TextureAtlasRegion` 与 `Sprite` 的 region 引用。打包系统保持无分配、不调用 Vulkan。
**验收**:多子图合图采样像素正确;region 越界/格式不符被拒;退役不释放仍在用的帧资源;warmup 后每帧 0 通用堆分配。

**进度**:18A status (done, 2026-06-11)。`VulkanTextureAtlasRuntime` 骨架完成:atlas 槽位走 `VulkanAtlasImageRef` 的 `id+generation`,镜像分配委托给 `VulkanResourceRuntime`,实现 create/resolve/release/槽位复用与 stale 拒绝;atlas 图片句柄是 runtime POD,ECS 侧身份仍用现有 `TextureAtlasRegion`,未触碰任何组件契约。`tests/vulkan_texture_atlas_runtime_test.cpp` 覆盖无设备的委托传播/容量校验与带 GPU 的生命周期 smoke。Debug `ctest` 42/42、`-Werror` 构建与 clang-tidy 均通过。

18B status (done, 2026-06-11)。`VulkanResourceRuntime::recordCopyBufferToImageRegion` 新增带 `imageOffset/imageExtent` 与越界校验的子矩形 buffer→image 拷贝;`VulkanTextureAtlasRuntime::recordUploadRegion` 把源子图上传到 atlas 的指定矩形(解析 atlas → backing `ImageRef` → 委托资源 runtime)。smoke 把 red/green 两个 1×1 子图合成进 2×1 atlas,整图回读逐像素验证,并覆盖越界 region 被拒。Debug `ctest` 42/42、`-Werror` 构建与 clang-tidy 均通过。18C(进阶 bin packing)起待续。

18E status (done, 2026-06-11)。端到端集成证明:在 `vulkan_textured_sprite_render_test.cpp` 新增 `testAtlasRuntimeRegionPacketDraw`——atlas 图片改由 `VulkanTextureAtlasRuntime::createAtlasImage` 创建、`recordUploadRegion` 逐区域合成(red→(0,0)、green→(1,0)),再经 `TextureAtlasBuildSystem` 打包 + `runWithTextureRegions` 生成 UV,通过真实采样着色器绘制,左右半屏回读验证采样到正确区域。证明 18A/18B 的图片 runtime 已接通 Stage 16 的采样路径。Debug `ctest` 42/42、`-Werror` 构建与 clang-tidy 均通过。剩余 18C/18D 不再视为可选,本轮一并完成收口。

18C status (done, 2026-06-11)。`TextureAtlasBuildSystem::runHeightSorted` —— First-Fit Decreasing Height 货架打包:按高度降序(同高按 index 稳定)对调用方提供的 scratch order span 排序后放置,对高度参差的 item 比 `run()` 的输入序货架紧实得多;确定性、无分配,region 仍按 id 查找故放置顺序对消费者不可见。CPU 测试覆盖"高者先放"的放置结果与 scratch 不足被拒。

18D status (done, 2026-06-11)。`VulkanTextureAtlasRuntime::retireAtlasImage` —— 立即释放 atlas 句柄(槽位/id 可即刻复用),但把 backing image 交给 `NativeDeferredDestroyRuntime` 延迟退役以越过在途帧;调用方在退役帧完成后 drain 队列、再经资源 runtime 真正释放。GPU 测试覆盖 retire → 未到帧不 drain → 到帧 drain → 真正释放 的完整时序。

18F 收口 (done, 2026-06-11):ADR 升级为 Accepted 并并入 18C/18D,`PROJECT_INDEX`/`ReinforcementPlan` 同步。**Stage 18 全部完成并关闭** —— atlas 图片 runtime 已具备 创建 / 子矩形合成 / 解析 / 即时释放 / 帧安全退役 的完整生命周期,外加 FFDH 打包。门禁:Debug `ctest` 42/42、Perf `ctest` 51/51、`-Werror` 构建、clang-tidy 全通过。更优打包(skyline/MaxRects)、字体 raster 接入、descriptor 更新策略、bindless 归 Stage 19+ 与未来优化。

---

### Stage 19 — 真实字体/文本 Runtime(功能补强 · 最大空洞)

**目标**:Stage 9 只搭了确定性 POD 骨架,glyph 全是占位符;FreeType 已 vendor 但 dormant。本阶段接入真实字体,复用 Stage 18 atlas(字形图集本质是动态 atlas)。

- **19A FreeType 接入**:把 `third_party/freetype` 纳入 CMake(独立可选开关,默认可关以不影响纯 sprite 构建);**先确定并在文档记录许可证策略(FTL vs GPLv2+)**;FreeType 句柄/face/glyph slot 全部置于 runtime,不进组件。
- **19B 字体光栅 runtime**:`VulkanFontRuntime`(或等价)拥有 `FT_Face`、glyph 光栅缓存,按 `id + generation` 暴露 `FontRef/FontAtlasRef`;光栅产物写入 Stage 18 atlas runtime。
- **19C 真实 UTF-8 解码 + 基础 shaping**:替换占位的 `GlyphRunBuildSystem/GlyphInstanceBuildSystem` 内部逻辑,保留其 ECS 接口与 dirty-range 增量(`TextDirtyRange` 仍只重建变化范围)。
- **19D 字形进 atlas**:glyph 光栅经 Stage 18 通道上传/退役;`GlyphInstance.atlas_rect` 由真实图集坐标填充。
- **19E Vulkan 文本绘制离屏 smoke**:类比 sprite smoke,渲染真实字形并 readback 验证(覆盖率/边缘像素)。
- **19F 收口**:ADR + `PROJECT_INDEX` + 验证。

**边界**:`Text` 组件仍不含 `string/string_view`;UTF-8 backing buffer、字体文件、shaping cache、glyph atlas 都在 ECS 外;单线程确定性路径保留为正确性参考。复杂排版(双向文本、连字、组合字)可拆分为 19 之后的增量。
**验收**:静态文本不每帧重建 glyph;dirty 文本只更新变化范围;真实字形采样像素正确;关闭 FreeType 开关时纯 sprite 构建不受影响。

**进度 / 设计固化**:完整设计见 `docs/architecture/STAGE19_TEXT_FONT_DESIGN.md`(ADR `docs/adr/2026-06-11-stage19-text-font-pipeline.md`),该设计取代上面的初版要点。按反馈定稿:塑形拆成**多 system + 三个 runtime 触点**——`Utf8DecodeSystem`/`GlyphPositionSystem`/`GlyphInstanceToSpriteSystem` 是纯 umbrella system;bidi-itemize / shape / atlas-residency 链接 FreeType/HarfBuzz/SheenBidi,落在内部 `render2d_font_runtime_support`,不进伞形头;**第一版即含完整双向文本**(SheenBidi UAX#9)。glyph 经 bridge 复用 Stage 12–18 sprite 路径,glyph atlas 复用 Stage 18(R8_UNORM)。

19B status (done, 2026-06-11):新增 POD 组件 `Codepoint`/`ShapingRun`/`ShapedGlyph`/`GlyphAtlasEntry`/`FontMetrics`,扩展 `GlyphInstance`(+`width`/`height`,glyph 即纹理化四边形)与 `FontRef`(→ `id+generation+flags`);含 traits、forward-decl、POD 合同测试。同步修正 Stage 9 `makeGlyphInstance` 与既有 glyph 构造以满足 `-Wmissing-designated-field-initializers`。门禁:Debug 42/42、Perf 51/51、`-Werror`、clang-tidy 全过。

19C status (done, 2026-06-12):`include/Render2D/System/TextShapingSystem.hpp` 落地三个纯 umbrella system,与 19B 一样零第三方依赖、可即时编译+单测:
- `Utf8DecodeSystem`:bytes 经非拥有 `Utf8BufferView` 入参 → `Codepoint[]`;畸形/越界/截断序列发 U+FFFD 并逐字节重新同步(含混合宽度/偏移/错误用例)。
- `GlyphPositionSystem`:单基线笔走位——`ShapedGlyph` 的 advance/offset + `GlyphAtlasEntry` 的 bearing/bitmap 尺寸/UV → 定位后的 `GlyphInstance[]`;非驻留字形写零尺寸四边形并打 `kGlyphInstanceMissingAtlasFlag`,仍推进笔位。多行/逐行 bidi 重排留待 itemizer 落地后叠加。
- `GlyphInstanceToSpriteSystem`:纯 bridge——2×3 仿射把单位四边形 `[0,1]²` 映射到字形矩形 `(position_x,position_y)+(width,height)`,`atlas_rect`→UV,纹理/材质取自 `GlyphSpriteBridgeConfig`;退化(空白/零尺寸)字形跳过,`write_count` 只计可见字形。
门禁:Debug 43/43、Perf 52/52、`-Werror` 构建、clang-tidy 全过。

**阻塞解除(更新 2026-06-12)**:用户提供 HarfBuzz **源码树**(`harfbuzz-main.zip`),已解压至 `third_party/harfbuzz` 并删除两个压缩包(含先前无用的 win64 运行时包)。三库齐备。

19A status (done, 2026-06-12):CMake 接入三库为静态库,落在内部 INTERFACE target `render2d_font_runtime_support`(类比 `render2d_thread_runtime_support`,不进伞形头),由选项 `RENDER2D_BUILD_FONT_RUNTIME`(默认 ON)门控。FreeType 自包含(关 zlib/png/bzip2/brotli,且关 HARFBUZZ 以断开 FreeType↔HarfBuzz 自动 hinting 环);HarfBuzz 仅核心 shaping + FreeType interop(关 subset/raster/vector/gpu/utils/glib/icu),`HB_HAVE_FREETYPE` 在 `freetype` target 存在时自动开;SheenBidi unity 构建。三库 include 目录提升为 SYSTEM,避免触发本项目 `-Werror`。顶层 `project()` 加入 C 语言(FreeType/SheenBidi 为 C)。三个 `.lib` 均成功构建。

19E status (done, 2026-06-12):`include/Render2D/Font/BidiItemizeRuntime.hpp` —— SheenBidi 驱动的 UAX#9 双向 + 脚本切分:按 `text_index` 分组的 `Codepoint[]` → **视觉序、单脚本** 的 `ShapingRun[]`。每个 text 一段;硬换行各成段落,单行覆盖段落;脚本切分(`SBScriptLocator`)与嵌入级 visual run(`SBLineGetRunsPtr`)求交,RTL run 内子段反序发出;`codepoint_first/count` 为逻辑序、run 发出为视觉序。测试覆盖 LTR+希伯来+LTR 混排、纯 RTL 阿拉伯、多 text、空/容量/越界/不支持域(SheenBidi 真实链接执行)。

19D status (done, 2026-06-12):`include/Render2D/Font/FontShapeRuntime.hpp` —— HarfBuzz-over-FreeType 塑形:`FT_Library` + 每字体 `FT_Face`/`hb_font_t` 走 `FontRef` 的 id+generation 槽表;`loadFontFromMemory`(`FT_New_Memory_Face` + `hb_ft_font_create_referenced`,产出 `FontRef`+`FontMetrics`),`shape`(逐 `ShapingRun`:`hb_buffer_add_utf32` + `guess_segment_properties` + 按 bidi 覆写方向 + `hb_shape` → `ShapedGlyph[]`,advance/offset 由 26.6 定点 /64 转像素)。生命周期:字体字节由调用方持有(FreeType 不拷贝)。测试加载系统字体(无则优雅跳过,返回 0,沿用 Vulkan smoke 范式)塑形 "Ayb" → 3 字形、advance>0、glyph_id≠0,并覆盖容量/未知字体/不支持域。

门禁(19A/D/E 落地时):Debug `ctest` 45/45、Perf `ctest` 54/54、`-Werror` 构建、clang-tidy 全过(`modernize-*` 为 warning、仅 `readability-identifier-naming` 为 error;新文件零 warning)。

19F status (done, 2026-06-12):端到端打通并收口。
- `VulkanResourceRuntime` 格式表新增 `R8_UNORM`/`R8_SRGB`(1 字节/像素),供字形覆盖率 atlas。
- `include/Render2D/Font/GlyphAtlasRuntime.hpp` —— 拥有一张 R8_UNORM 的 Stage 18 atlas + staging buffer + 货架打包;`ensureResident` 对每个唯一 (font, glyph, size) 仅光栅化一次(经 `FontShapeRuntime::rasterizeGlyph`,`FT_LOAD_RENDER` → 8-bit 覆盖率),打包并经 `recordUploadRegion` 上传到 atlas 子矩形,产出 `GlyphAtlasEntry[]`(UV+位图尺寸+bearing)。调用方在 `ensureResident` 期间须保持 atlas 处于 `TRANSFER_DST`。
- 覆盖率字形着色器 `kGlyphCoverageFragSpv`(`tests/support/SpriteShaders.hpp`,glslc 编译):`out = vec4(color.rgb*cov, color.a*cov)`(预乘,适配无混合管线)。复用既有 sprite 顶点着色器。
- `GlyphInstanceToSpriteSystem` 增加可选视口投影(布局像素 y-down → clip `[-1,1]`;视口为 0 时恒等,保持 19C 合同/测试)。
- `tests/vulkan_glyph_text_render_test.cpp` —— 离屏端到端:decode→bidi→shape→glyph atlas 驻留→position→bridge→真实覆盖率着色器绘制→回读,断言渲染出非空且有形状的覆盖率(无设备/无系统字体则优雅跳过)。本机有 GPU + arial.ttf,真实渲染 "Hi" 通过。

门禁(最终,全部真实跑过):Debug `ctest` 46/46、Perf `ctest` 55/55、`-Werror` 构建、clang-tidy 全过。**Stage 19 全部完成(19A–19F),真实 FreeType+HarfBuzz+SheenBidi 文本管线端到端在 GPU 上验证。** ADR 升为 Accepted。后续细化(skyline/MaxRects 打包、字形缓存退役、软换行、稳定槽 dirty 增量)留作未来优化,不阻塞。

---

### Stage 20 — Material / Descriptor 扩展策略(功能/扩展补强)

**目标**:当前仅透传 material/texture 的 `id + generation`,descriptor 用 combined image sampler array、`recordPackets` 按 packet 重绑。补上可扩展到"多纹理/多材质"的策略。

- **20A 能力探测**:检测 `descriptor indexing`/bindless 支持;现有 combined image sampler array 路径作为强制 fallback。
- **20B bindless 纹理表 runtime**:`texture id → bindless slot` 映射(runtime 所有,不进 ECS);descriptor set 单次绑定、按索引采样。
- **20C 多纹理批次合并**:允许 batch 跨 texture 合并以减少 rebind/draw call;batch 合并仍比较完整 `id + generation` 防止 stale 误合。
- **20D Material graph / 选择**:`material id + generation → pipeline + 参数集`,参数仍为 POD;为后续效果(blend/tint/自定义 shader)留扩展点。
- **20E 离屏 smoke**:N(≥8)张纹理单 descriptor set 一次性渲染;与非 bindless fallback 路径做等价性对照。
- **20F 收口**:ADR + `PROJECT_INDEX` + 验证。

**边界**:bindless/material 策略是 runtime 概念;ECS 仍只持有 `id + generation`;非 bindless 路径保留为正确性/兼容参考。
**验收**:bindless 与 fallback 渲染结果一致;不支持 bindless 的设备自动走 fallback 且 CTest 不失败;stale texture/material generation 负例覆盖。

**进度**:20A status (done, 2026-06-12)。`include/Render2D/Native/VulkanBindlessCapability.hpp` —— 生产侧 bindless 能力探测:`queryVulkanBindlessCapability(VkPhysicalDevice)` 读取四项 descriptor-indexing 特性(`descriptorBindingPartiallyBound` / `runtimeDescriptorArray` / `shaderSampledImageArrayNonUniformIndexing` / `descriptorBindingSampledImageUpdateAfterBind`)与 update-after-bind 采样图限额,`supported` 仅当四项齐备时置位;helper 把能力映射成 `VulkanDescriptorRuntime` 已支持的 pool/layout/binding flag(不支持时返回 0 → 退回每包 combined-image-sampler fallback,既有 descriptor runtime 无需改动)。测试侧 `VulkanSmokeContext` 复用该探测得出 `supports_bindless` 并在建设备时启用全部四项,供 20B/20E 在真实设备上跑 bindless。`tests/vulkan_bindless_capability_test.cpp` 覆盖 null 设备、flag helper 映射与设备探测一致性(优雅跳过)。门禁:Debug `ctest` 47/47、Perf `ctest` 56/56、`-Werror`、clang-tidy 全过。ADR 留待契约成型(20B+)随 20F 收口。

**进度**:20B status (done, 2026-06-13)。`include/Render2D/Native/VulkanBindlessTextureTable.hpp` —— bindless 纹理表 runtime,按商定采用**拆分布局**:自有一个持久 descriptor set,binding 0 = 身份直映射的 `SAMPLED_IMAGE` 数组(容量 `texture_capacity`,`PARTIALLY_BOUND | UPDATE_AFTER_BIND`)、binding 1 = 小 `SAMPLER` 数组;**不复用** `VulkanDescriptorRuntime`(通用 runtime 原封不动留作非 bindless fallback 的正确性参考)。`setResident`/`evict`/`isResident` 以 `texture_id` + generation 记账,stale 校验落在 CPU(着色器只看下标);唯一的 `McVector` 是 CPU 驻留记录(GPU 数组是定容 Vulkan binding,本就不能、也不该 McVector);容量超 `capability.max_descriptor_set_sampled_images` 即拒;不支持 bindless 的设备返回 `UnsupportedDomain` → 调用方走既有 combined-image-sampler fallback。空洞按最佳性能纯 `PARTIALLY_BOUND` 留空(零显存/零写入),Debug 可选 1×1 fill view 兜底(Release 零成本)。20A 的 binding-flag helper 随之改名 `bindlessSampledImageBindingFlags`(它探测的本就是 sampled-image 系特性)。`tests/vulkan_bindless_texture_table_test.cpp` 覆盖设备无关的拒绝路径 + 真实设备全生命周期(建 set、容量拒绝、采样器/驻留写、generation-stale 与越界、stale-image 与 double-evict、再驻留)。门禁:Debug `ctest` 48/48、Perf `ctest` 57/57、`-Werror`、clang-tidy 全过。ADR 仍随 20F 收口。20C(从 batch key 去 `texture_id` + `nonuniformEXT` 着色器 + 每实例 `sampler_index`)起待续。

**进度**:20C status (done, 2026-06-13)。多纹理批次合并的 CPU 系统 + bindless 着色器落地:
- `SortKey.hpp` 新增**纹理无关**的 `makeBindlessDrawSortKey`(把打包键的 texture 位清零,使仅纹理不同的 draw 同键相邻)与 `drawCommandsHaveEqualBindlessBatchKey`(在原 batch key 基础上去掉 `texture_id`/`texture_generation` 两项,仍全量比较 material id/generation、sort_key、layer、flags,故陈旧 material generation 绝不会被并掉)。
- `BatchSystem` 抽出 `runImpl<bool Bindless>` 共享循环,新增 `runBindless`:仅纹理不同的 draw 合并为一个 batch;非 bindless `run` 行为不变(仍按纹理拆分,作对照)。
- `SpriteDrawPacketBuildSystem::runBindless`:整帧单一 bindless descriptor set,无逐纹理查找,把跨纹理合并的 batch 收成**一个** `SpriteDrawPacket`(`makePacket<Bindless>` + `isDrawCompatible<Bindless>` 统一,bindless 下不比较纹理);material 身份/generation 仍全量校验,缺失/陈旧 material 与陈旧 descriptor 被拒。
- bindless 着色器 `kSpriteBindlessVertSpv`/`kSpriteBindlessFragSpv`(`tests/support/SpriteShaders.hpp`,glslc `--target-env=vulkan1.2`):**拆分** `texture2D textures[]`(binding 0)+ `sampler samplers[]`(binding 1),与 20B 拆分表布局一致;顶点把 `in_texture_id` 以 `flat` 传给片元,片元 `texture(sampler2D(textures[nonuniformEXT(in_texture_id)], samplers[0]), uv) * color`。
- `tests/bindless_batch_system_test.cpp`:键 helper 的 `static_assert`、跨纹理合并 vs 非 bindless 拆分对照、generation 陈旧边界、单包跨实例、缺失/陈旧 material 与 descriptor 拒绝、SPIR-V magic 校验。

**范围说明(如实)**:`SpriteInstance` **未改** —— 每实例 `sampler_index` 推迟到 **20D**(material graph 给它真实来源前,着色器恒用 `samplers[0]`);`CommandBuildSystem`/编码器/管线接线**未动** —— 真正出 bindless 画面、≥8 纹理单 set 离屏与 fallback 等价性对照是 **20E**。ADR 仍随 20F 收口。门禁:Debug `ctest` 49/49、Perf `ctest` 58/58、`-Werror`、clang-tidy 全过。**Next: 20D(material graph + 每实例 sampler 选择)或 20E(bindless 离屏渲染等价性对照)。**

---

### Stage 21 — 并行尾段与剩余性能项(工程/性能补强)

**目标**:Stage 10H 只并行了 sprite CPU 路径,BatchSystem/Sort 仍是单线程尾段。补上剩余并行与候选 SIMD/SoA 优化。每项必须 benchmark before/after,并按 `ProjectMergeTODO.md` #22 做阈值门控。

- **21A 文本 CPU 管线 ThreadCenter 化**:类比 `ThreadedCpuPipelineRuntime`,确定性 chunk merge;单线程仍是参考。
- **21B 并行 batch/sort 尾段**:per-thread stream append → 确定性合并;保证与单线程输出逐字节等价。
- **21C(数据驱动,可选)SIMD bounds/culling**:仅在 benchmark 证明收益时落地。
- **21D(数据驱动,可选)SoA transform columns / draw command compaction**:同上,需基线对照。
- **21E 阈值门控**:统一 `worker_count / min_items_per_task` 策略,小负载默认单线程;大负载启用并行。
- **21F 收口**:更新 `BENCHMARK_BASELINE.md`,记录 before/after。

**边界**:ThreadCenter 不进组件;并行路径必须写 per-thread 流并确定性合并;无数据不优化。
**验收**:并行输出与单线程逐字节一致;benchmark 显示目标负载下净收益;小负载未被并行开销拖慢(或已被阈值门控规避)。

---

### Stage 22 — 上屏呈现与可见抓帧(功能/集成补强)

**目标**:swapchain/acquire/present runtime 已具备但只做了 headless 状态测试。补上真实呈现一帧与抓帧自动化。窗口/surface 所有权按设计在宿主,故用 **test-only host harness** 扮演宿主角色来证明,不把窗口所有权放进 Render2D。

- **22A test-only 窗口/surface harness**:在测试侧创建窗口与 `VkSurfaceKHR`(host 角色),交给 `VulkanSwapchainRuntime` 创建/adopt swapchain。
- **22B 真实呈现一帧**:复用 Stage 11 `VulkanPresentRuntime` 完成 acquire→render→present,屏上出现已验证的 sprite/atlas/文本内容。
- **22C 可见抓帧自动化**:窗口帧截屏并与离屏 readback 基线对照,作为可见性回归。
- **22D RenderDoc 自动化 capture**:把 capture 目标从离屏 smoke exe 切到真实呈现帧,形成可重复的抓帧产物。

**边界**:Render2D 仍不拥有窗口/surface;本阶段是"宿主集成证明",harness 属于 test-only,不进生产 API。
**验收**:屏上呈现内容与离屏基线一致;present 路径 validation clean;out-of-date/resize 走 `SwapchainOutOfDate` 重建;无 GPU/无显示环境时该套件 graceful skip。

---

### Stage 23 — 宿主引擎集成 / Merge(终极收敛)

**目标**:落实 `docs/ProjectMergeTODO.md` 的 39 条约束,把 Render2D 作为后端 runtime 嵌入宿主 ECS。这是所有补强的收敛点。

- **23A 宿主 ECS 适配层设计**:定义宿主组件流 ↔ Render2D 系统的边界(全部经 `std::span`),给出替换 `tests/support/` 临时 ECS 的迁移指引。
- **23B native runtime 对接**:宿主提供 device/queue/surface;Render2D runtime 负责把 `id + generation` 解析为 Vulkan 对象并执行后端操作;生命周期/退役归属明确。
- **23C 逐条核对 39 项 merge 约束**:形成 checklist,确认每条在集成形态下成立(POD 流归属、窗口归属、显存归属、ThreadCenter 边界、upload ring 帧安全、deferred destroy 时机等)。
- **23D 端到端集成 smoke**:宿主 ECS 数据 → 真实帧(沿用 Stage 22 呈现或宿主自有呈现)。
- **23E 质量门复跑**:在宿主环境复跑测试与性能回归。
- **23F 收口**:隔离/移除 test-only ECS,`PROJECT_INDEX`/`ARCHITECTURE` 更新为集成形态,关闭 `ProjectMergeTODO` 中已落实条目。

**边界**:这是 Render2D 的最终形态——它是宿主的渲染后端,不是独立应用;窗口、场景 ECS、资源装载归宿主。
**验收**:宿主 ECS 驱动出真实帧;39 条约束逐条标注落实/不适用;test-only ECS 不再参与生产路径。

---

## 4. 风险与依赖

- **FreeType 许可证(Stage 19A)**:FTL/GPLv2+ 二选一,必须在接入前定策并写入文档,否则阻断发布。
- **bindless 设备能力(Stage 20)**:依赖 descriptor indexing 扩展;必须保留 fallback,CI 需覆盖两条路径。
- **GPU runner(Stage 17C/22)**:validation smoke、上屏、抓帧都需要带 GPU/显示的 CI 环境;无 GPU 时已有 graceful skip,但可见性回归可能需 manual checklist 兜底。
- **依赖路径硬编码(Stage 17A 前置)**:是 CI 与他人复现的前置;未解决前 CI 只能在预置环境运行。
- **并行确定性(Stage 21)**:任何并行尾段都必须保证与单线程逐字节等价,否则不得合入。

---

## 5. 统一质量门(每阶段必过)

每个子阶段完成时执行下述门禁;Vulkan/性能阶段附加项见下。命令沿用 `STAGE10_PERFORMANCE_TODO.md` 既定模板(脚本为 PowerShell `.ps1`)。

```powershell
cmake --preset clang-ninja-debug
cmake --build --preset clang-ninja-debug
ctest --preset clang-ninja-debug --output-on-failure
cmake --preset clang-ninja-perf
cmake --build --preset clang-ninja-perf
ctest --preset clang-ninja-perf
clang-tidy -p build <本阶段改动的源文件...> --quiet
clang-tidy --verify-config --config-file=.clang-tidy
git diff --check
# 约束扫描(Stage 17D 起脚本化):std::vector / 直接 Vulkan memory API / 旧 math
```

**通用验收**:Debug 与 Perf 的 ctest 全绿;clang-tidy clean;`git diff --check` 干净;约束扫描通过;契约变更已配 ADR;`docs/PROJECT_INDEX.md` 已更新。

**Vulkan 阶段附加**:validation layer clean;GPU 资源/descriptor 生命周期测试;frame-in-flight 安全;stale `id + generation` 负例覆盖;离屏 readback(或上屏抓帧)逐像素验证。

**性能阶段附加**:`scripts/run_*_benchmarks.ps1` 跑目标场景;before/after 记入 `docs/architecture/BENCHMARK_BASELINE.md`;并行路径与单线程逐字节等价。

---

## 6. 收尾约定

- 每个 Stage 收口时,在本文件对应小节追加 `status (done)` 与边界结论,**沿用 `Plan.md` 的阶段进度补充写法**。
- 新增/变更契约的 ADR 落在 `docs/adr/`,命名沿用 `YYYY-MM-DD-stageNN-<topic>.md`。
- 本文件与 `Plan.md`、`docs/ARCHITECTURE.md`、`docs/PROJECT_INDEX.md`、`docs/ProjectMergeTODO.md` 保持一致;出现分歧以红线(§1)为准。
