#include <Render2D/Render2D.hpp>

#include <array>
#include <cassert>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using CommandBuffer = R2D::CommandBuffer<Provider, Dim>;

static_assert(R2D::StrictPodComponent<CommandBuffer>);
static_assert(R2D::SupportedRenderComponent<Provider, Dim, CommandBuffer>);

int main()
{
    constexpr auto kFrameIndex = 3U;
    constexpr R2D::RangeU32 kDrawRange{.first = 4U, .count = 5U};
    constexpr R2D::RangeU32 kBatchRange{.first = 9U, .count = 2U};
    constexpr R2D::RangeU32 kUploadRange{.first = 11U, .count = 7U};
    constexpr R2D::RangeU32 kSubmitRange{.first = 18U, .count = 1U};

    std::array<CommandBuffer, 1U> command_buffers{};
    auto result = R2D::CommandBufferBuildSystem<Provider, Dim>::run(
        kFrameIndex,
        kDrawRange,
        kBatchRange,
        kUploadRange,
        kSubmitRange,
        command_buffers);

    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 1U);
    assert(command_buffers[0U].frame_index == kFrameIndex);
    assert(command_buffers[0U].draw_first == kDrawRange.first);
    assert(command_buffers[0U].draw_count == kDrawRange.count);
    assert(command_buffers[0U].batch_first == kBatchRange.first);
    assert(command_buffers[0U].batch_count == kBatchRange.count);
    assert(command_buffers[0U].upload_first == kUploadRange.first);
    assert(command_buffers[0U].upload_count == kUploadRange.count);
    assert(command_buffers[0U].native_submit_first == kSubmitRange.first);
    assert(command_buffers[0U].native_submit_count == kSubmitRange.count);

    std::array<CommandBuffer, 0U> empty_buffers{};
    result = R2D::CommandBufferBuildSystem<Provider, Dim>::run(
        kFrameIndex,
        kDrawRange,
        kBatchRange,
        kUploadRange,
        kSubmitRange,
        empty_buffers);
    assert(result.code == R2D::SystemStatusCode::InsufficientCapacity);

    result = R2D::CommandBufferClearSystem<Provider, Dim>::run(command_buffers);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 1U);
    assert(command_buffers[0U].frame_index == 0U);
    assert(command_buffers[0U].draw_first == 0U);
    assert(command_buffers[0U].draw_count == 0U);
    assert(command_buffers[0U].batch_first == 0U);
    assert(command_buffers[0U].batch_count == 0U);
    assert(command_buffers[0U].upload_first == 0U);
    assert(command_buffers[0U].upload_count == 0U);
    assert(command_buffers[0U].native_submit_first == 0U);
    assert(command_buffers[0U].native_submit_count == 0U);

    return 0;
}
