#include <Render2D/Render2D.hpp>

#include <array>
#include <cassert>
#include <span>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using UploadCommand = R2D::UploadCommand<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;

static_assert(R2D::StrictPodComponent<UploadCommand>);
static_assert(R2D::StrictPodComponent<DescriptorSlice>);

void testUploadCoalescesAdjacentCommands()
{
    std::array<UploadCommand, 5U> uploads{
        UploadCommand{
            .resource_id = 7U,
            .source_offset = 0U,
            .destination_offset = 1024U,
            .byte_count = 64U,
            .upload_kind = 1U,
            .flags = 0U,
        },
        UploadCommand{
            .resource_id = 7U,
            .source_offset = 64U,
            .destination_offset = 1088U,
            .byte_count = 128U,
            .upload_kind = 1U,
            .flags = 0U,
        },
        UploadCommand{
            .resource_id = 7U,
            .source_offset = 192U,
            .destination_offset = 1400U,
            .byte_count = 64U,
            .upload_kind = 1U,
            .flags = 0U,
        },
        UploadCommand{
            .resource_id = 7U,
            .source_offset = 256U,
            .destination_offset = 1464U,
            .byte_count = 64U,
            .upload_kind = 1U,
            .flags = 2U,
        },
        UploadCommand{
            .resource_id = 9U,
            .source_offset = 320U,
            .destination_offset = 1528U,
            .byte_count = 64U,
            .upload_kind = 1U,
            .flags = 2U,
        },
    };
    std::array<UploadCommand, 5U> coalesced{};

    const auto result = R2D::UploadCoalesceSystem<Provider, Dim>::run(uploads, coalesced);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.read_count == 5U);
    assert(result.write_count == 4U);
    assert(coalesced[0U].resource_id == 7U);
    assert(coalesced[0U].source_offset == 0U);
    assert(coalesced[0U].destination_offset == 1024U);
    assert(coalesced[0U].byte_count == 192U);
    assert(coalesced[1U].destination_offset == 1400U);
    assert(coalesced[1U].byte_count == 64U);
    assert(coalesced[2U].flags == 2U);
    assert(coalesced[3U].resource_id == 9U);
}

void testUploadCoalescesInPlace()
{
    std::array<UploadCommand, 3U> uploads{
        UploadCommand{
            .resource_id = 1U,
            .source_offset = 0U,
            .destination_offset = 0U,
            .byte_count = 8U,
            .upload_kind = 2U,
            .flags = 3U,
        },
        UploadCommand{
            .resource_id = 1U,
            .source_offset = 8U,
            .destination_offset = 8U,
            .byte_count = 8U,
            .upload_kind = 2U,
            .flags = 3U,
        },
        UploadCommand{
            .resource_id = 1U,
            .source_offset = 16U,
            .destination_offset = 20U,
            .byte_count = 8U,
            .upload_kind = 2U,
            .flags = 3U,
        },
    };

    const auto result = R2D::UploadCoalesceSystem<Provider, Dim>::run(
        std::span<const UploadCommand>{uploads.data(), uploads.size()},
        std::span<UploadCommand>{uploads.data(), uploads.size()});
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.write_count == 2U);
    assert(uploads[0U].byte_count == 16U);
    assert(uploads[1U].destination_offset == 20U);
}

void testUploadCapacityAndInvalidInput()
{
    const std::array<UploadCommand, 2U> uploads{
        UploadCommand{
            .resource_id = 1U,
            .source_offset = 0U,
            .destination_offset = 0U,
            .byte_count = 8U,
            .upload_kind = 1U,
            .flags = 0U,
        },
        UploadCommand{
            .resource_id = 2U,
            .source_offset = 8U,
            .destination_offset = 8U,
            .byte_count = 8U,
            .upload_kind = 1U,
            .flags = 0U,
        },
    };
    std::array<UploadCommand, 1U> short_output{};
    auto result = R2D::UploadCoalesceSystem<Provider, Dim>::run(uploads, short_output);
    assert(result.code == R2D::SystemStatusCode::InsufficientCapacity);
    assert(result.read_count == 1U);
    assert(result.write_count == 1U);

    const std::array<UploadCommand, 1U> invalid_upload{
        UploadCommand{
            .resource_id = 1U,
            .source_offset = 0U,
            .destination_offset = 0U,
            .byte_count = 0U,
            .upload_kind = 1U,
            .flags = 0U,
        },
    };
    std::array<UploadCommand, 1U> output{};
    result = R2D::UploadCoalesceSystem<Provider, Dim>::run(invalid_upload, output);
    assert(result.code == R2D::SystemStatusCode::InvalidInput);
}

void testDescriptorCompactsAdjacentSlices()
{
    const std::array<DescriptorSlice, 5U> slices{
        DescriptorSlice{.descriptor_set_id = 3U, .first = 0U, .count = 2U, .generation = 1U},
        DescriptorSlice{.descriptor_set_id = 3U, .first = 2U, .count = 1U, .generation = 1U},
        DescriptorSlice{.descriptor_set_id = 3U, .first = 3U, .count = 1U, .generation = 2U},
        DescriptorSlice{.descriptor_set_id = 4U, .first = 4U, .count = 1U, .generation = 2U},
        DescriptorSlice{.descriptor_set_id = 4U, .first = 8U, .count = 1U, .generation = 2U},
    };
    std::array<DescriptorSlice, 5U> compacted{};

    const auto result = R2D::DescriptorCompactionSystem<Provider, Dim>::run(slices, compacted);
    assert(result.code == R2D::SystemStatusCode::Ok);
    assert(result.read_count == 5U);
    assert(result.write_count == 4U);
    assert(compacted[0U].descriptor_set_id == 3U);
    assert(compacted[0U].first == 0U);
    assert(compacted[0U].count == 3U);
    assert(compacted[1U].generation == 2U);
    assert(compacted[2U].descriptor_set_id == 4U);
    assert(compacted[3U].first == 8U);
}

void testDescriptorCapacityAndInvalidInput()
{
    const std::array<DescriptorSlice, 2U> slices{
        DescriptorSlice{.descriptor_set_id = 1U, .first = 0U, .count = 1U, .generation = 1U},
        DescriptorSlice{.descriptor_set_id = 2U, .first = 0U, .count = 1U, .generation = 1U},
    };
    std::array<DescriptorSlice, 1U> short_output{};
    auto result = R2D::DescriptorCompactionSystem<Provider, Dim>::run(slices, short_output);
    assert(result.code == R2D::SystemStatusCode::InsufficientCapacity);
    assert(result.read_count == 1U);
    assert(result.write_count == 1U);

    const std::array<DescriptorSlice, 1U> invalid_slices{
        DescriptorSlice{.descriptor_set_id = 1U, .first = 0U, .count = 0U, .generation = 1U},
    };
    std::array<DescriptorSlice, 1U> output{};
    result = R2D::DescriptorCompactionSystem<Provider, Dim>::run(invalid_slices, output);
    assert(result.code == R2D::SystemStatusCode::InvalidInput);
}

void testUnsupportedDomain()
{
    struct UnsupportedProvider {
    };
    std::array<R2D::UploadCommand<UnsupportedProvider, Dim>, 1U> uploads{};
    std::array<R2D::UploadCommand<UnsupportedProvider, Dim>, 1U> output{};
    const auto result = R2D::UploadCoalesceSystem<UnsupportedProvider, Dim>::run(uploads, output);
    assert(result.code == R2D::SystemStatusCode::UnsupportedDomain);
}

int main()
{
    testUploadCoalescesAdjacentCommands();
    testUploadCoalescesInPlace();
    testUploadCapacityAndInvalidInput();
    testDescriptorCompactsAdjacentSlices();
    testDescriptorCapacityAndInvalidInput();
    testUnsupportedDomain();
    return 0;
}
