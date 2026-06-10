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

} // namespace Render2D
