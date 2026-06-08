#include <Render2D/Render2D.hpp>

#include <type_traits>

namespace R2D = Render2D;

template<class Type>
consteval void requireNativePodContract()
{
    static_assert(std::is_trivial_v<Type>);
    static_assert(std::is_standard_layout_v<Type>);
    static_assert(std::is_trivially_copyable_v<Type>);
    static_assert(std::is_aggregate_v<Type>);
}

int main()
{
    requireNativePodContract<R2D::NativeHandle>();
    requireNativePodContract<R2D::NativeId>();
    requireNativePodContract<R2D::NativeGeneration>();
    requireNativePodContract<R2D::NativeResourceKey>();
    requireNativePodContract<R2D::NativeByteRange>();
    requireNativePodContract<R2D::NativeResult>();
    requireNativePodContract<R2D::NativeCapacityResult>();
    requireNativePodContract<R2D::VulkanCommandRuntimeConfig>();

    static_assert(std::is_enum_v<R2D::NativeStatusCode>);
    static_assert(std::is_enum_v<R2D::NativeObjectKind>);
    static_assert(std::is_enum_v<R2D::NativeMemoryDomain>);

    constexpr R2D::NativeResourceKey kKey{
        .id = {.value = 11U},
        .generation = {.value = 7U},
    };
    static_assert(kKey.id.value == 11U);
    static_assert(kKey.generation.value == 7U);

    constexpr R2D::NativeResult kBufferResult{
        .code = R2D::NativeStatusCode::Ok,
        .object_kind = R2D::NativeObjectKind::Buffer,
        .object_id = {.value = 3U},
        .generation = {.value = 2U},
    };
    static_assert(kBufferResult.code == R2D::NativeStatusCode::Ok);
    static_assert(kBufferResult.object_kind == R2D::NativeObjectKind::Buffer);
    static_assert(kBufferResult.object_id.value == 3U);
    static_assert(kBufferResult.generation.value == 2U);

    static_assert(R2D::kNativeOk.code == R2D::NativeStatusCode::Ok);

    return 0;
}
