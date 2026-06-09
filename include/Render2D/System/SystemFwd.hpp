#pragma once

namespace Render2D {

template<class Provider, class Dim>
struct TransformSystem;

template<class Provider, class Dim>
struct BoundsSystem;

template<class Provider, class Dim>
struct CullingSystem;

template<class Provider, class Dim>
struct CommandBuildSystem;

template<class Provider, class Dim>
struct CommandBufferBuildSystem;

template<class Provider, class Dim>
struct CommandBufferClearSystem;

template<class Provider, class Dim>
struct BatchSystem;

template<class Provider, class Dim>
struct DrawSortSystem;

template<class Provider, class Dim>
struct TextDirtySystem;

template<class Provider, class Dim>
struct GlyphRunBuildSystem;

template<class Provider, class Dim>
struct GlyphInstanceBuildSystem;

template<class Provider, class Dim>
struct GlyphBatchSystem;

template<class Provider, class Dim>
struct UploadSystem;

template<class Provider, class Dim>
struct UploadCoalesceSystem;

template<class Provider, class Dim>
struct DescriptorCompactionSystem;

template<class Provider, class Dim>
struct EncodeSystem;

template<class Provider, class Dim>
struct SubmitSystem;

} // namespace Render2D
