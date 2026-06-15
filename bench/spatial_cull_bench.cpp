// Stage 24 Track 1 benchmark: the granular Transform -> Bounds -> Culling
// front-end vs the fused SpatialCullSystem over the same inputs, single-threaded.
// A per-frame memcmp asserts the fused world_transforms and visible_items are
// byte-identical to the granular chain. --rotate forces the sincos affine path
// (compute case); --visibility low hides 7/8 of the sprites (cull-heavy case).
// See docs/architecture/BENCHMARK_BASELINE.md.
#include <Render2D/Render2D.hpp>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>

namespace {

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using Transform = R2D::Transform<Provider, Dim>;
using WorldTransform = R2D::WorldTransform<Provider, Dim>;
using LocalBounds = R2D::LocalBounds<Provider, Dim>;
using WorldBounds = R2D::WorldBounds<Provider, Dim>;
using VisibilityMask = R2D::VisibilityMask<Provider, Dim>;
using VisibleItem = R2D::VisibleItem<Provider, Dim>;
using Camera = R2D::Camera<Provider, Dim>;
using SpatialCull = R2D::SpatialCullSystem<Provider, Dim>;

inline constexpr R2D::U32 kDefaultSpriteCount = 1000000U;
inline constexpr R2D::U32 kDefaultFrameCount = 8U;
inline constexpr R2D::U32 kDefaultWarmupCount = 2U;
inline constexpr R2D::U32 kHiddenSpriteStride = 8U;
inline constexpr Camera kBenchmarkCamera{
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

enum class VisibilityMode : R2D::U8 {
    High = 0,
    Low = 1,
};

struct BenchConfig {
    R2D::U32 sprite_count;
    R2D::U32 frame_count;
    R2D::U32 warmup_count;
    VisibilityMode visibility;
    bool rotate;
};

[[nodiscard]] const char* visibilityName(VisibilityMode visibility_) noexcept
{
    switch (visibility_) {
    case VisibilityMode::High:
        return "high";
    case VisibilityMode::Low:
        return "low";
    }
    return "unknown";
}

[[nodiscard]] bool parseU32(std::string_view text_, R2D::U32& out_value_) noexcept
{
    if (text_.empty() || text_.front() < '0' || text_.front() > '9') {
        return false;
    }
    unsigned long long value = 0ULL;
    const char* const first = text_.data();
    const char* const last = text_.data() + text_.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last ||
        value > (std::numeric_limits<R2D::U32>::max)()) {
        return false;
    }
    out_value_ = static_cast<R2D::U32>(value);
    return true;
}

[[nodiscard]] bool parseVisibility(std::string_view text_, VisibilityMode& out_visibility_) noexcept
{
    if (text_ == "high") {
        out_visibility_ = VisibilityMode::High;
        return true;
    }
    if (text_ == "low") {
        out_visibility_ = VisibilityMode::Low;
        return true;
    }
    return false;
}

[[nodiscard]] bool parseConfig(int argc_, char** argv_, BenchConfig& out_config_) noexcept
{
    out_config_ = {
        .sprite_count = kDefaultSpriteCount,
        .frame_count = kDefaultFrameCount,
        .warmup_count = kDefaultWarmupCount,
        .visibility = VisibilityMode::High,
        .rotate = false,
    };
    for (int index = 1; index < argc_; ++index) {
        const std::string_view option{argv_[index]};
        if (option == "--sprites" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.sprite_count)) { return false; }
            ++index;
        } else if (option == "--frames" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.frame_count)) { return false; }
            ++index;
        } else if (option == "--warmup" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.warmup_count)) { return false; }
            ++index;
        } else if (option == "--visibility" && index + 1 < argc_) {
            if (!parseVisibility(argv_[index + 1], out_config_.visibility)) { return false; }
            ++index;
        } else if (option == "--rotate") {
            out_config_.rotate = true;
        } else {
            return false;
        }
    }
    return out_config_.sprite_count != 0U && out_config_.frame_count != 0U;
}

[[nodiscard]] double elapsedMs(
    std::chrono::steady_clock::time_point start_,
    std::chrono::steady_clock::time_point end_) noexcept
{
    return std::chrono::duration<double, std::milli>(end_ - start_).count();
}

struct BenchState {
    R2D::McVector<Transform> transforms;
    R2D::McVector<LocalBounds> local_bounds;
    R2D::McVector<VisibilityMask> visibility_masks;

    R2D::McVector<WorldTransform> granular_world_transforms;
    R2D::McVector<WorldBounds> granular_world_bounds;
    R2D::McVector<VisibleItem> granular_visible_items;

    R2D::McVector<WorldTransform> fused_world_transforms;
    R2D::McVector<VisibleItem> fused_visible_items;
};

[[nodiscard]] bool isHiddenLowVisibilitySprite(const BenchConfig& config_, R2D::U32 index_) noexcept
{
    return config_.visibility == VisibilityMode::Low && index_ % kHiddenSpriteStride != 0U;
}

void makeState(const BenchConfig& config_, BenchState& out_state_)
{
    const auto count = static_cast<R2D::Usize>(config_.sprite_count);
    out_state_.transforms.resize(count);
    out_state_.local_bounds.resize(count);
    out_state_.visibility_masks.resize(count);
    out_state_.granular_world_transforms.resize(count);
    out_state_.granular_world_bounds.resize(count);
    out_state_.granular_visible_items.resize(count);
    out_state_.fused_world_transforms.resize(count);
    out_state_.fused_visible_items.resize(count);

    // A constant nonzero rotation defeats the rotation==0 fast path and forces
    // the MMath::sincos affine, so --rotate measures the compute-bound case.
    const float rotation = config_.rotate ? 0.78539816F : 0.0F;
    for (R2D::U32 index = 0U; index < config_.sprite_count; ++index) {
        const bool hidden = isHiddenLowVisibilitySprite(config_, index);
        const auto grid_x = static_cast<float>(index % 64U) - 32.0F;
        const auto grid_y = static_cast<float>((index / 64U) % 64U) - 32.0F;
        out_state_.transforms[index] = {
            .source_id = index,
            .position_x = hidden ? 10000.0F + grid_x : grid_x,
            .position_y = hidden ? 10000.0F + grid_y : grid_y,
            .rotation_radians = rotation,
            .scale_x = 1.0F,
            .scale_y = 1.0F,
        };
        out_state_.local_bounds[index] = {
            .source_id = index,
            .bounds = R2D::makeAabb2(-0.5F, -0.5F, 0.5F, 0.5F),
        };
        out_state_.visibility_masks[index] = {.mask = hidden ? 0U : 0xFFFFFFFFU};
    }
}

[[nodiscard]] bool isOk(R2D::SystemResult result_) noexcept
{
    return result_.code == R2D::SystemStatusCode::Ok;
}

// Granular front-end: the three systems the fused pass replaces. Returns the
// visible count, or a negative value on failure.
[[nodiscard]] long runGranular(BenchState& state_) noexcept
{
    auto result = R2D::TransformSystem<Provider, Dim>::run(
        state_.transforms,
        state_.granular_world_transforms);
    if (!isOk(result)) { return -1; }
    result = R2D::BoundsSystem<Provider, Dim>::run(
        state_.granular_world_transforms,
        state_.local_bounds,
        state_.granular_world_bounds);
    if (!isOk(result)) { return -1; }
    result = R2D::CullingSystem<Provider, Dim>::run(
        kBenchmarkCamera,
        state_.granular_world_bounds,
        state_.visibility_masks,
        state_.granular_visible_items);
    if (!isOk(result)) { return -1; }
    return static_cast<long>(result.write_count);
}

[[nodiscard]] long runFused(BenchState& state_) noexcept
{
    const auto result = SpatialCull::run(
        kBenchmarkCamera,
        state_.transforms,
        state_.local_bounds,
        state_.visibility_masks,
        state_.fused_world_transforms,
        state_.fused_visible_items);
    if (!isOk(result)) { return -1; }
    return static_cast<long>(result.write_count);
}

} // namespace

int main(int argc_, char** argv_)
{
    try {
        BenchConfig config{};
        if (!parseConfig(argc_, argv_, config)) {
            std::fputs("Render2D fused spatial front-end benchmark\n"
                       "Options: --sprites <n> --frames <n> --warmup <n>\n"
                       "         --visibility high|low --rotate\n",
                       stderr);
            return 1;
        }

        BenchState state{};
        makeState(config, state);

        double granular_total_ms = 0.0;
        double fused_total_ms = 0.0;
        R2D::U32 visible_count = 0U;
        bool failure = false;

        const R2D::U32 total_frames = config.warmup_count + config.frame_count;
        for (R2D::U32 frame = 0U; frame < total_frames; ++frame) {
            const bool collect = frame >= config.warmup_count;

            auto start = std::chrono::steady_clock::now();
            const long granular_visible = runGranular(state);
            auto end = std::chrono::steady_clock::now();
            if (collect) { granular_total_ms += elapsedMs(start, end); }

            start = std::chrono::steady_clock::now();
            const long fused_visible = runFused(state);
            end = std::chrono::steady_clock::now();
            if (collect) { fused_total_ms += elapsedMs(start, end); }

            if (granular_visible < 0 || fused_visible < 0 || granular_visible != fused_visible) {
                failure = true;
                break;
            }
            visible_count = static_cast<R2D::U32>(fused_visible);

            // The dense world_transforms and the compacted visible stream must be
            // byte-identical between the granular chain and the fused pass.
            const bool world_match = std::memcmp(
                state.fused_world_transforms.data(),
                state.granular_world_transforms.data(),
                static_cast<R2D::Usize>(config.sprite_count) * sizeof(WorldTransform)) == 0;
            const bool visible_match = std::memcmp(
                state.fused_visible_items.data(),
                state.granular_visible_items.data(),
                static_cast<R2D::Usize>(visible_count) * sizeof(VisibleItem)) == 0;
            if (!world_match || !visible_match) {
                failure = true;
                break;
            }
        }

        if (failure) {
            std::fputs("spatial cull bench: granular/fused mismatch or system failure\n", stderr);
            return 1;
        }

        const auto divisor = static_cast<double>(config.frame_count);
        const double granular_ms = granular_total_ms / divisor;
        const double fused_ms = fused_total_ms / divisor;
        const double speedup = fused_ms > 0.0 ? granular_ms / fused_ms : 0.0;

        std::printf("Render2D fused spatial front-end benchmark\n");
        std::printf("sprites=%u visibility=%s rotate=%d visible=%u frames=%u\n",
            config.sprite_count, visibilityName(config.visibility),
            config.rotate ? 1 : 0, visible_count, config.frame_count);
        std::printf("avg_granular_ms=%.6f\n", granular_ms);
        std::printf("avg_fused_ms=%.6f\n", fused_ms);
        std::printf("speedup=%.6f\n", speedup);
        return 0;
    } catch (const std::exception& exception) {
        std::fputs("spatial cull bench exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("spatial cull bench unknown exception\n", stderr);
        return 1;
    }
}
