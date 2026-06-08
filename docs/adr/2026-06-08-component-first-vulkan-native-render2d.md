# ADR: Component-First Vulkan Native Render2D

Status: Accepted

Date: 2026-06-08

## Context

Render2D 需要以 C++23 实现高性能 2D 渲染模块。用户明确要求：

- 所有 component 必须是严格 POD。
- component 可以脱离 entity 独立存在。
- 所有涉及 Provider / Dim 的命名统一使用模板元区分。
- 当前唯一 Provider 是 Vulkan native。
- 当前唯一 Dim 是 2D。

## Decision

Render2D 采用 Component-First ECS。核心数据以 Strict POD component stream 表示，system 只执行 component stream 到 component stream 的转换。Entity 仅作为可选 identity/index，不作为 component 存在的前提。

Provider / Dim 通过 tag type 与 compile-time gate 表示：

```cpp
VulkanNativeProvider
Dim2
SupportedRenderDomain<Provider, Dim>
```

当前唯一合法 domain 是 `VulkanNativeProvider + Dim2`。

Provider native 资源状态可以作为 POD component 存在，但只能保存 handle/index/slice/state。类型名不重复编码 Vulkan；Vulkan 语义由 `VulkanNativeProvider` 表达。RAII 生命周期、内存拥有权和 Vulkan 对象销毁必须放在 `Storage<Provider, Dim>` / `Runtime<Provider, Dim>` / `System<Provider, Dim>` 形态的非 component 对象中。

`CommandBuffer<Provider, Dim>` 是 ECS component，但只能是 POD descriptor。真正的 command arrays 由 storage 拥有。

## Alternatives Considered

Entity-first ECS：拒绝。它会把 component 的存在绑定到 entity，不符合“组件可以脱离实体存在”的要求。

Runtime backend enum：拒绝。它会把 Provider 分发推到运行时，破坏当前只支持 Vulkan native 的编译期约束。

Object-oriented renderer：拒绝。对象封装容易把生命周期、行为和数据混在 component 中，不适合严格 POD component stream。

CommandBuffer owning vector：拒绝。它让 `CommandBuffer` 变成 owning container，不再是严格 POD component。

## Consequences

- 数据布局、批处理、排序、上传和命令构建可以围绕连续 component stream 优化。
- 后续新增 Provider 或 Dim 必须先扩展 compile-time domain gate。
- Storage 可以使用 MemoryCenter 管理内存，但 storage 永远不是 component。
- Strict POD 的普通成员函数禁令无法仅靠标准 C++23 type traits 完整证明，后续质量门需要通过代码生成约束或 AST 检查强化。
