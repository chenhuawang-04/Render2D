#include <Render2D/Render2D.hpp>

#include <Center/Memory/MemoryCenter.hpp>
#include <fast_math/aabb2.h>
#include <fast_math/mat3.h>
#include <fast_math/vec2.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <type_traits>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;

struct SmokePodComponent {
    std::uint32_t id;
    float value;
};

struct SmokeNonPodComponent {
    std::string value;
};

static_assert(R2D::kSupportedProvider<Provider>);
static_assert(!R2D::kSupportedProvider<int>);
static_assert(R2D::kSupportedDim<Dim>);
static_assert(!R2D::kSupportedDim<int>);
static_assert(R2D::SupportedRenderDomain<Provider, Dim>);
static_assert(!R2D::SupportedRenderDomain<int, Dim>);
static_assert(!R2D::SupportedRenderDomain<Provider, int>);

static_assert(R2D::StrictPodComponent<SmokePodComponent>);
static_assert(!R2D::StrictPodComponent<SmokeNonPodComponent>);
static_assert(R2D::StrictPodComponent<R2D::RangeU32>);
static_assert(R2D::StrictPodComponent<R2D::SystemResult>);
static_assert(R2D::StrictPodComponent<R2D::Aabb2>);
static_assert(R2D::StrictPodComponent<R2D::Affine2X3>);

static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::Transform<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::WorldTransform<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::Sprite<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::Text<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::Camera<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::LocalBounds<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::WorldBounds<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::VisibilityMask<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::RenderLayer<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::MaterialRef<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::TextureRef<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FontRef<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::VisibleItem<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::SortedItem<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::DrawCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::BatchCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::UploadCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::NativeSubmitCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FrameIndex<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FrameArenaState<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::CommandBuffer<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::UploadRingSlice<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::DescriptorSlice<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FenceState<Provider, Dim>>);

static_assert(!R2D::SupportedRenderComponent<Provider, Dim, SmokePodComponent>);
static_assert(!R2D::SupportedRenderComponent<Provider, Dim, SmokeNonPodComponent>);
static_assert(!R2D::SupportedRenderComponent<int, Dim, R2D::Transform<int, Dim>>);
static_assert(!R2D::SupportedRenderComponent<Provider, int, R2D::Transform<Provider, int>>);

static_assert(R2D::kStrictPodComponentValue<R2D::DrawCommand<Provider, Dim>>);

static_assert(std::is_trivial_v<MMath::Vec2>);
static_assert(std::is_standard_layout_v<MMath::Vec2>);
static_assert(std::is_trivially_copyable_v<MMath::Vec2>);
static_assert(std::is_aggregate_v<MMath::Vec2>);

static_assert(std::is_trivial_v<MMath::Mat3>);
static_assert(std::is_standard_layout_v<MMath::Mat3>);
static_assert(std::is_trivially_copyable_v<MMath::Mat3>);
static_assert(std::is_aggregate_v<MMath::Mat3>);

static_assert(std::is_trivial_v<MMath::Aabb2>);
static_assert(std::is_standard_layout_v<MMath::Aabb2>);
static_assert(std::is_trivially_copyable_v<MMath::Aabb2>);
static_assert(std::is_aggregate_v<MMath::Aabb2>);

int main()
{
    constexpr auto kType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    constexpr MMath::Vec2 kP{.x = 1.0F, .y = 2.0F};
    return (kType == VK_STRUCTURE_TYPE_APPLICATION_INFO && kP.x == 1.0F) ? 0 : 1;
}
