#pragma once

#include "Render2D/Core/Result.hpp"
#include "Render2D/Native/NativeComponents.hpp"

#include <span>

namespace Render2D {

template<class Provider, class Dim>
struct PresentCommandBuildSystem {
    static SystemResult run(
        std::span<const AcquiredImage<Provider, Dim>> acquired_images_,
        std::span<PresentCommand<Provider, Dim>> present_commands_) noexcept
    {
        if constexpr (!SupportedRenderDomain<Provider, Dim>) {
            return {.code = SystemStatusCode::UnsupportedDomain, .read_count = 0U, .write_count = 0U};
        } else {
            if (!isSystemResultCountRepresentable(acquired_images_.size()) ||
                !isSystemResultCountRepresentable(present_commands_.size())) {
                return {.code = SystemStatusCode::InvalidInput, .read_count = 0U, .write_count = 0U};
            }

            const U32 acquired_count = static_cast<U32>(acquired_images_.size());
            if (acquired_count == 0U) {
                return {.code = SystemStatusCode::Ok, .read_count = 0U, .write_count = 0U};
            }

            if (present_commands_.size() < acquired_images_.size()) {
                return {
                    .code = SystemStatusCode::InsufficientCapacity,
                    .read_count = acquired_count,
                    .write_count = static_cast<U32>(present_commands_.size()),
                };
            }

            for (U32 image_index = 0U; image_index < acquired_count; ++image_index) {
                if (!isAcquiredImageValid(acquired_images_[image_index])) {
                    return {
                        .code = SystemStatusCode::InvalidInput,
                        .read_count = image_index,
                        .write_count = 0U,
                    };
                }
            }

            for (U32 image_index = 0U; image_index < acquired_count; ++image_index) {
                const auto& acquired = acquired_images_[image_index];
                present_commands_[image_index] = {
                    .swapchain_id = acquired.swapchain_id,
                    .image_index = acquired.image_index,
                    .wait_sync_id = acquired.sync_id,
                    .wait_sync_generation = acquired.sync_generation,
                    .frame_index = acquired.frame_index,
                    .generation = acquired.generation,
                    .flags = acquired.flags,
                };
            }

            return {
                .code = SystemStatusCode::Ok,
                .read_count = acquired_count,
                .write_count = acquired_count,
            };
        }
    }

private:
    static bool isAcquiredImageValid(const AcquiredImage<Provider, Dim>& acquired_) noexcept
    {
        return acquired_.generation != 0U && acquired_.sync_generation != 0U;
    }
};

} // namespace Render2D
