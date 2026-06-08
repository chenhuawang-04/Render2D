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
struct UploadSystem;

template<class Provider, class Dim>
struct EncodeSystem;

template<class Provider, class Dim>
struct SubmitSystem;

} // namespace Render2D
