#include <Render2D/Render2D.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Transform = R2D::Transform<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using WorldBounds = R2D::WorldBounds<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using Sprite = R2D::Sprite<Provider, Dim>;
using DrawCommand = R2D::DrawCommand<Provider, Dim>;
using BatchCommand = R2D::BatchCommand<Provider, Dim>;
using CommandBuffer = R2D::CommandBuffer<Provider, Dim>;

struct BenchConfig {
    R2D::Usize sprite_count;
    R2D::U32 frame_count;
};

struct BenchState {
    R2D::McVector<Transform> transforms;
    R2D::McVector<WorldTransform> world_transforms;
    R2D::McVector<LocalBounds> local_bounds;
    R2D::McVector<WorldBounds> world_bounds;
    R2D::McVector<VisibilityMask> visibility_masks;
    R2D::McVector<VisibleItem> visible_items;
    R2D::McVector<Sprite> sprites;
    R2D::McVector<DrawCommand> draw_commands;
    R2D::McVector<BatchCommand> batch_commands;
    R2D::McVector<CommandBuffer> command_buffers;
};

struct BenchTotals {
    double transform_ms;
    double bounds_ms;
    double culling_ms;
    double command_ms;
    double batch_ms;
    double command_buffer_ms;
    R2D::U32 visible_count;
    R2D::U32 draw_count;
    R2D::U32 batch_count;
};

static R2D::Usize parseUsize(const char* text_, R2D::Usize fallback_) noexcept
{
    if (text_ == nullptr) {
        return fallback_;
    }
    char* end = nullptr;
    const auto value = std::strtoull(text_, &end, 10);
    if (end == text_ || value == 0ULL) {
        return fallback_;
    }
    return static_cast<R2D::Usize>(value);
}

static BenchConfig parseConfig(int argc_, char** argv_) noexcept
{
    BenchConfig config{
        .sprite_count = 10000U,
        .frame_count = 8U,
    };

    for (int index = 1; index + 1 < argc_; ++index) {
        const std::string_view option{argv_[index]};
        if (option == "--sprites") {
            config.sprite_count = parseUsize(argv_[index + 1], config.sprite_count);
            ++index;
        } else if (option == "--frames") {
            config.frame_count = static_cast<R2D::U32>(parseUsize(argv_[index + 1], config.frame_count));
            ++index;
        }
    }

    return config;
}

static BenchState makeState(BenchConfig config_)
{
    BenchState state{
        .transforms = {},
        .world_transforms = {},
        .local_bounds = {},
        .world_bounds = {},
        .visibility_masks = {},
        .visible_items = {},
        .sprites = {},
        .draw_commands = {},
        .batch_commands = {},
        .command_buffers = {},
    };

    state.transforms.resize(config_.sprite_count);
    state.world_transforms.resize(config_.sprite_count);
    state.local_bounds.resize(config_.sprite_count);
    state.world_bounds.resize(config_.sprite_count);
    state.visibility_masks.resize(config_.sprite_count);
    state.visible_items.resize(config_.sprite_count);
    state.sprites.resize(config_.sprite_count);
    state.draw_commands.resize(config_.sprite_count);
    state.batch_commands.resize(config_.sprite_count);
    state.command_buffers.resize(1U);

    for (R2D::Usize index = 0U; index < config_.sprite_count; ++index) {
        const auto source_id = static_cast<R2D::U32>(index);
        const auto x = static_cast<float>(index % 64U) - 32.0F;
        const auto y = static_cast<float>((index / 64U) % 64U) - 32.0F;

        state.transforms[index] = {
            .source_id = source_id,
            .position_x = x,
            .position_y = y,
            .rotation_radians = 0.0F,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        };
        state.local_bounds[index] = {
            .source_id = source_id,
            .bounds = {.min_x = -0.5F, .min_y = -0.5F, .max_x = 0.5F, .max_y = 0.5F},
        };
        state.visibility_masks[index] = {.mask = 0xFFFFFFFFU};
        state.sprites[index] = {
            .source_id = source_id,
            .texture_id = static_cast<R2D::U32>((index / 128U) % 8U),
            .material_id = static_cast<R2D::U32>((index / 128U) % 4U),
            .color_rgba8 = 0xFFFFFFFFU,
            .layer = static_cast<R2D::U32>((index / 128U) % 2U),
            .flags = 0U,
        };
    }

    return state;
}

static double elapsedMs(
    std::chrono::steady_clock::time_point start_,
    std::chrono::steady_clock::time_point end_) noexcept
{
    return std::chrono::duration<double, std::milli>(end_ - start_).count();
}

static int requireOk(R2D::SystemResult result_) noexcept
{
    return result_.code == R2D::SystemStatusCode::Ok ? 0 : 1;
}

static int runBenchmark(BenchConfig config_, BenchState& state_, BenchTotals& totals_) noexcept
{
    constexpr R2D::Camera<Provider, Dim> kCamera{
        .source_id = 0U,
        .position_x = 0.0F,
        .position_y = 0.0F,
        .rotation_radians = 0.0F,
        .viewport_width = 96.0F,
        .viewport_height = 96.0F,
        .near_z = 0.0F,
        .far_z = 1.0F,
        .layer_mask = 0xFFFFFFFFU,
        .flags = 0U,
    };

    for (R2D::U32 frame_index = 0U; frame_index < config_.frame_count; ++frame_index) {
        auto start = std::chrono::steady_clock::now();
        auto result = R2D::TransformSystem<Provider, Dim>::run(state_.transforms, state_.world_transforms);
        auto end = std::chrono::steady_clock::now();
        totals_.transform_ms += elapsedMs(start, end);
        if (requireOk(result) != 0) {
            return 1;
        }

        start = std::chrono::steady_clock::now();
        result = R2D::BoundsSystem<Provider, Dim>::run(
            state_.world_transforms,
            state_.local_bounds,
            state_.world_bounds);
        end = std::chrono::steady_clock::now();
        totals_.bounds_ms += elapsedMs(start, end);
        if (requireOk(result) != 0) {
            return 1;
        }

        start = std::chrono::steady_clock::now();
        result = R2D::CullingSystem<Provider, Dim>::run(
            kCamera,
            state_.world_bounds,
            state_.visibility_masks,
            state_.visible_items);
        end = std::chrono::steady_clock::now();
        totals_.culling_ms += elapsedMs(start, end);
        if (requireOk(result) != 0) {
            return 1;
        }
        totals_.visible_count = result.write_count;

        start = std::chrono::steady_clock::now();
        result = R2D::CommandBuildSystem<Provider, Dim>::run(
            std::span<const VisibleItem>{state_.visible_items.data(), totals_.visible_count},
            state_.sprites,
            state_.draw_commands);
        end = std::chrono::steady_clock::now();
        totals_.command_ms += elapsedMs(start, end);
        if (requireOk(result) != 0) {
            return 1;
        }
        totals_.draw_count = result.write_count;

        start = std::chrono::steady_clock::now();
        result = R2D::BatchSystem<Provider, Dim>::run(
            std::span<const DrawCommand>{state_.draw_commands.data(), totals_.draw_count},
            state_.batch_commands);
        end = std::chrono::steady_clock::now();
        totals_.batch_ms += elapsedMs(start, end);
        if (requireOk(result) != 0) {
            return 1;
        }
        totals_.batch_count = result.write_count;

        start = std::chrono::steady_clock::now();
        result = R2D::CommandBufferBuildSystem<Provider, Dim>::run(
            frame_index,
            {.first = 0U, .count = totals_.draw_count},
            {.first = 0U, .count = totals_.batch_count},
            {.first = 0U, .count = 0U},
            {.first = 0U, .count = 0U},
            state_.command_buffers);
        end = std::chrono::steady_clock::now();
        totals_.command_buffer_ms += elapsedMs(start, end);
        if (requireOk(result) != 0) {
            return 1;
        }
    }

    return 0;
}

static void printTotals(const BenchConfig& config_, const BenchTotals& totals_)
{
    const auto frame_count = static_cast<double>(std::max(config_.frame_count, 1U));
    std::cout << "Render2D null CPU benchmark\n";
    std::cout << "sprites=" << config_.sprite_count << " frames=" << config_.frame_count << '\n';
    std::cout << "visible=" << totals_.visible_count
              << " draws=" << totals_.draw_count
              << " batches=" << totals_.batch_count << '\n';
    std::cout << "avg_transform_ms=" << totals_.transform_ms / frame_count << '\n';
    std::cout << "avg_bounds_ms=" << totals_.bounds_ms / frame_count << '\n';
    std::cout << "avg_culling_ms=" << totals_.culling_ms / frame_count << '\n';
    std::cout << "avg_command_ms=" << totals_.command_ms / frame_count << '\n';
    std::cout << "avg_batch_ms=" << totals_.batch_ms / frame_count << '\n';
    std::cout << "avg_command_buffer_ms=" << totals_.command_buffer_ms / frame_count << '\n';
}

int main(int argc_, char** argv_)
{
    try {
        const auto config = parseConfig(argc_, argv_);
        auto state = makeState(config);
        BenchTotals totals{
            .transform_ms = 0.0,
            .bounds_ms = 0.0,
            .culling_ms = 0.0,
            .command_ms = 0.0,
            .batch_ms = 0.0,
            .command_buffer_ms = 0.0,
            .visible_count = 0U,
            .draw_count = 0U,
            .batch_count = 0U,
        };
        const auto result = runBenchmark(config, state, totals);
        if (result != 0) {
            return result;
        }
        printTotals(config, totals);
        return 0;
    } catch (...) {
        return 1;
    }
}

