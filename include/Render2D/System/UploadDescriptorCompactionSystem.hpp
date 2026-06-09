#pragma once

#include "Render2D/Component/Frame.hpp"
#include "Render2D/Component/Upload.hpp"
#include "Render2D/Core/Result.hpp"
#include "Render2D/Meta/Domain.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct UploadCoalesceSystem {
    static SystemResult run(
        std::span<const UploadCommand<Provider, Dim>> upload_commands_,
        std::span<UploadCommand<Provider, Dim>> coalesced_upload_commands_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (upload_commands_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }
            if (coalesced_upload_commands_.empty()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = 0U,
                    .write_count = 0U,
                };
            }

            U32 write_count = 0U;
            for (Usize read_index = 0U; read_index < upload_commands_.size(); ++read_index) {
                const auto command = upload_commands_[read_index];
                if (!isUploadCommandValid(command)) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(read_index),
                        .write_count = write_count,
                    };
                }

                if (write_count != 0U &&
                    canMerge(coalesced_upload_commands_[write_count - 1U], command)) {
                    auto& previous = coalesced_upload_commands_[write_count - 1U];
                    previous.byte_count += command.byte_count;
                    continue;
                }

                if (write_count >= coalesced_upload_commands_.size()) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = static_cast<U32>(read_index),
                        .write_count = write_count,
                    };
                }

                coalesced_upload_commands_[write_count] = command;
                ++write_count;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(upload_commands_.size()),
                .write_count = write_count,
            };
        }
    }

private:
    static constexpr U64 kMaxU64 = 0xFFFFFFFFFFFFFFFFULL;

    static bool isRangeValid(U64 offset_, U64 byte_count_) noexcept
    {
        return byte_count_ != 0U && offset_ <= kMaxU64 - byte_count_;
    }

    static bool isUploadCommandValid(const UploadCommand<Provider, Dim>& command_) noexcept
    {
        return isRangeValid(command_.source_offset, command_.byte_count) &&
            isRangeValid(command_.destination_offset, command_.byte_count);
    }

    static bool isAppendRange(U64 offset_, U64 byte_count_, U64 next_offset_) noexcept
    {
        return offset_ <= kMaxU64 - byte_count_ &&
            offset_ + byte_count_ == next_offset_;
    }

    static bool canMerge(
        const UploadCommand<Provider, Dim>& left_,
        const UploadCommand<Provider, Dim>& right_) noexcept
    {
        return left_.resource_id == right_.resource_id &&
            left_.upload_kind == right_.upload_kind &&
            left_.flags == right_.flags &&
            left_.byte_count <= kMaxU64 - right_.byte_count &&
            isAppendRange(left_.source_offset, left_.byte_count, right_.source_offset) &&
            isAppendRange(left_.destination_offset, left_.byte_count, right_.destination_offset);
    }
};

template<class Provider, class Dim>
struct DescriptorCompactionSystem {
    static SystemResult run(
        std::span<const DescriptorSlice<Provider, Dim>> descriptor_slices_,
        std::span<DescriptorSlice<Provider, Dim>> compacted_slices_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (descriptor_slices_.empty()) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }
            if (compacted_slices_.empty()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = 0U,
                    .write_count = 0U,
                };
            }

            U32 write_count = 0U;
            for (Usize read_index = 0U; read_index < descriptor_slices_.size(); ++read_index) {
                const auto slice = descriptor_slices_[read_index];
                if (!isDescriptorSliceValid(slice)) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = static_cast<U32>(read_index),
                        .write_count = write_count,
                    };
                }

                if (write_count != 0U && canMerge(compacted_slices_[write_count - 1U], slice)) {
                    auto& previous = compacted_slices_[write_count - 1U];
                    previous.count += slice.count;
                    continue;
                }

                if (write_count >= compacted_slices_.size()) {
                    return {
                        .code = SystemStatusCode::InsufficientCapacity,
                        .read_count = static_cast<U32>(read_index),
                        .write_count = write_count,
                    };
                }

                compacted_slices_[write_count] = slice;
                ++write_count;
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = static_cast<U32>(descriptor_slices_.size()),
                .write_count = write_count,
            };
        }
    }

private:
    static constexpr U32 kMaxU32 = 0xFFFFFFFFU;

    static bool isDescriptorSliceValid(const DescriptorSlice<Provider, Dim>& slice_) noexcept
    {
        return slice_.count != 0U && slice_.first <= kMaxU32 - slice_.count;
    }

    static bool canMerge(
        const DescriptorSlice<Provider, Dim>& left_,
        const DescriptorSlice<Provider, Dim>& right_) noexcept
    {
        return left_.descriptor_set_id == right_.descriptor_set_id &&
            left_.generation == right_.generation &&
            left_.count <= kMaxU32 - right_.count &&
            left_.first <= kMaxU32 - left_.count &&
            left_.first + left_.count == right_.first;
    }
};

} // namespace Render2D
