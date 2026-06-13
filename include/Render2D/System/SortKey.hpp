#pragma once

#include "Render2D/Component/Command.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

inline constexpr U32 kDrawSortKeyFlagBits = 4U;
inline constexpr U32 kDrawSortKeyTextureBits = 12U;
inline constexpr U32 kDrawSortKeyMaterialBits = 8U;
inline constexpr U32 kDrawSortKeyLayerBits = 8U;

inline constexpr U32 kDrawSortKeyFlagMask = (1U << kDrawSortKeyFlagBits) - 1U;
inline constexpr U32 kDrawSortKeyTextureMask = (1U << kDrawSortKeyTextureBits) - 1U;
inline constexpr U32 kDrawSortKeyMaterialMask = (1U << kDrawSortKeyMaterialBits) - 1U;
inline constexpr U32 kDrawSortKeyLayerMask = (1U << kDrawSortKeyLayerBits) - 1U;

inline constexpr U32 kDrawSortKeyTextureShift = kDrawSortKeyFlagBits;
inline constexpr U32 kDrawSortKeyMaterialShift =
    kDrawSortKeyTextureShift + kDrawSortKeyTextureBits;
inline constexpr U32 kDrawSortKeyLayerShift =
    kDrawSortKeyMaterialShift + kDrawSortKeyMaterialBits;

[[nodiscard]] constexpr U32 makeDrawSortKey(
    U32 layer_,
    U32 material_id_,
    U32 texture_id_,
    U32 flags_) noexcept
{
    return ((layer_ & kDrawSortKeyLayerMask) << kDrawSortKeyLayerShift) |
        ((material_id_ & kDrawSortKeyMaterialMask) << kDrawSortKeyMaterialShift) |
        ((texture_id_ & kDrawSortKeyTextureMask) << kDrawSortKeyTextureShift) |
        (flags_ & kDrawSortKeyFlagMask);
}

template<class Provider, class Dim>
[[nodiscard]] constexpr U32 makeDrawSortKey(
    const DrawCommand<Provider, Dim>& draw_command_) noexcept
{
    return makeDrawSortKey(
        draw_command_.layer,
        draw_command_.material_id,
        draw_command_.texture_id,
        draw_command_.flags);
}

// Bindless draw ordering: identical to makeDrawSortKey but with the texture
// field forced to zero, so draws differing only by texture share a sort key and
// become adjacent (hence mergeable) under the bindless batch path. The
// per-instance texture is carried in the instance stream and resolved in-shader
// via nonuniformEXT against a single descriptor set, so it must not split the
// sort order. The bindless path must use this for both the sort and the
// DrawCommand::sort_key field.
[[nodiscard]] constexpr U32 makeBindlessDrawSortKey(
    U32 layer_,
    U32 material_id_,
    U32 flags_) noexcept
{
    return makeDrawSortKey(layer_, material_id_, 0U, flags_);
}

template<class Provider, class Dim>
[[nodiscard]] constexpr U32 makeBindlessDrawSortKey(
    const DrawCommand<Provider, Dim>& draw_command_) noexcept
{
    return makeBindlessDrawSortKey(
        draw_command_.layer,
        draw_command_.material_id,
        draw_command_.flags);
}

template<class Provider, class Dim>
[[nodiscard]] bool drawCommandsHaveEqualBatchKey(
    const DrawCommand<Provider, Dim>& left_,
    const DrawCommand<Provider, Dim>& right_) noexcept
{
    return left_.sort_key == right_.sort_key &&
        left_.material_id == right_.material_id &&
        left_.material_generation == right_.material_generation &&
        left_.texture_id == right_.texture_id &&
        left_.texture_generation == right_.texture_generation &&
        left_.layer == right_.layer &&
        left_.flags == right_.flags;
}

// Bindless batch key: the non-bindless key minus the texture identity. With one
// descriptor set bound for the whole frame the texture no longer pins a draw to
// a descriptor, so draws differing only by texture may merge. Material identity
// and generation, packed sort key, layer and flags are still compared in full,
// so a stale or mismatched material generation can never be merged away.
// Expects draws built/sorted with makeBindlessDrawSortKey (equal-key adjacency).
template<class Provider, class Dim>
[[nodiscard]] bool drawCommandsHaveEqualBindlessBatchKey(
    const DrawCommand<Provider, Dim>& left_,
    const DrawCommand<Provider, Dim>& right_) noexcept
{
    return left_.sort_key == right_.sort_key &&
        left_.material_id == right_.material_id &&
        left_.material_generation == right_.material_generation &&
        left_.layer == right_.layer &&
        left_.flags == right_.flags;
}

} // namespace Render2D
