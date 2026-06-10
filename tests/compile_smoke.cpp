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
static_assert(R2D::StrictPodComponent<R2D::VulkanSamplerConfig>);
static_assert(R2D::StrictPodComponent<R2D::VulkanSamplerRuntimeConfig>);
static_assert(R2D::StrictPodComponent<R2D::VulkanSpritePipelineConfig>);
static_assert(R2D::StrictPodComponent<R2D::VulkanSpriteRenderEncoderConfig>);
static_assert(R2D::isSystemResultCountRepresentable(0U));
static_assert(R2D::isSystemResultCountRepresentable(0xFFFFFFFFULL));
static_assert(!R2D::isSystemResultCountRepresentable(0x1'0000'0000ULL));
static_assert(R2D::StrictPodComponent<R2D::Vec2>);
static_assert(R2D::StrictPodComponent<R2D::Mat3>);
static_assert(R2D::StrictPodComponent<R2D::Aabb2>);
static_assert(std::is_same_v<R2D::Vec2, MMath::Vec2>);
static_assert(std::is_same_v<R2D::Mat3, MMath::Mat3>);
static_assert(std::is_same_v<R2D::Aabb2, MMath::Aabb2>);

static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::Transform<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::WorldTransform<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::TransformDirtyItem<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::Sprite<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::SpriteVertex<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::SpriteInstance<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::SpriteDrawPacket<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::SpriteInstanceUploadCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::Text<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::TextState<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::TextDirtyRange<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::Utf8Slice<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::GlyphRun<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::GlyphInstance<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::Camera<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::LocalBounds<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::WorldBounds<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::VisibilityMask<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::RenderLayer<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::MaterialRef<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::TextureRef<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FontRef<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FontAtlasRef<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::VisibleItem<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::SortedItem<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::DrawCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::BatchCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::UploadCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::NativeSubmitCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::NativeCommandBufferRef<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::SwapchainImageRef<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::AcquiredImage<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::PresentCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::DeferredDestroyCommand<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FrameIndex<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FrameArenaState<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::CommandBuffer<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::UploadRingSlice<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::DescriptorSlice<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::FenceState<Provider, Dim>>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, R2D::SamplerRef<Provider, Dim>>);

static_assert(!R2D::SupportedRenderComponent<Provider, Dim, SmokePodComponent>);
static_assert(!R2D::SupportedRenderComponent<Provider, Dim, SmokeNonPodComponent>);
static_assert(!R2D::SupportedRenderComponent<int, Dim, R2D::Transform<int, Dim>>);
static_assert(!R2D::SupportedRenderComponent<Provider, int, R2D::Transform<Provider, int>>);

static_assert(R2D::kStrictPodComponentValue<R2D::DrawCommand<Provider, Dim>>);
static_assert(R2D::makeDrawSortKey(1U, 2U, 3U, 4U) != 0U);

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
