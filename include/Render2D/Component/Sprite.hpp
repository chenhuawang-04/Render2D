#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

template<class Provider, class Dim>
struct Sprite {
    U32 source_id;
    U32 texture_id;
    U32 material_id;
    U32 color_rgba8;
    U32 layer;
    U32 flags;
};

template<class Provider, class Dim>
struct MaterialRef {
    U32 id;
};

template<class Provider, class Dim>
struct TextureRef {
    U32 id;
};

template<class Provider, class Dim>
struct RenderLayer {
    U32 value;
};

template<class Provider, class Dim>
struct VisibilityMask {
    U32 mask;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, Sprite<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<Sprite<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, MaterialRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<MaterialRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, TextureRef<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<TextureRef<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, RenderLayer<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<RenderLayer<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, VisibilityMask<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<VisibilityMask<Provider, Dim>>;
};

} // namespace Render2D
