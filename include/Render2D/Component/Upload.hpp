#pragma once

#include "Render2D/Component/ComponentTraits.hpp"
#include "Render2D/Core/Types.hpp"

namespace Render2D {

inline constexpr U32 kUploadKindSpriteInstance = 0x5350494EU;

template<class Provider, class Dim>
struct UploadCommand {
    U32 resource_id;
    U64 source_offset;
    U64 destination_offset;
    U64 byte_count;
    U32 upload_kind;
    U32 flags;
};

template<class Provider, class Dim>
struct NativeSubmitCommand {
    U32 command_first;
    U32 command_count;
    U32 wait_first;
    U32 wait_count;
    U32 signal_first;
    U32 signal_count;
    U32 fence_id;
    U32 flags;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, UploadCommand<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<UploadCommand<Provider, Dim>>;
};

template<class Provider, class Dim>
struct ComponentTraits<Provider, Dim, NativeSubmitCommand<Provider, Dim>> {
    static constexpr bool kSupported =
        SupportedRenderDomain<Provider, Dim> &&
        StrictPodComponent<NativeSubmitCommand<Provider, Dim>>;
};

} // namespace Render2D
