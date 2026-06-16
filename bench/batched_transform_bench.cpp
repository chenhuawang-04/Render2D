// Stage 24 Track 2 benchmark: per-element TransformSystem (scalar sincos) vs
// SIMD-batched BatchedTransformSystem (MMath::mat3FromTrsArray, 8-wide AVX2 sincos)
// over the same rotation-heavy input -- the compute-bound case where sincos
// dominates. A per-frame memcmp asserts the batched world_transforms are
// byte-identical to the per-element build (all items rotated, so no -0.0 split).
//
// IMPORTANT: this target is compiled with -mavx2 -mfma so mat3FromTrsArray takes
// its SIMD path. The rest of Render2D builds at the baseline ISA; this bench
// measures the speedup that becomes available IF Render2D enables AVX2+FMA.
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
using TransformSys = R2D::TransformSystem<Provider, Dim>;
using Batched = R2D::BatchedTransformSystem<Provider, Dim>;

inline constexpr R2D::U32 kDefaultSpriteCount = 1000000U;
inline constexpr R2D::U32 kDefaultFrameCount = 8U;
inline constexpr R2D::U32 kDefaultWarmupCount = 2U;

struct BenchConfig {
    R2D::U32 sprite_count;
    R2D::U32 frame_count;
    R2D::U32 warmup_count;
    bool rotate;
};

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

[[nodiscard]] bool parseConfig(int argc_, char** argv_, BenchConfig& out_config_) noexcept
{
    out_config_ = {
        .sprite_count = kDefaultSpriteCount,
        .frame_count = kDefaultFrameCount,
        .warmup_count = kDefaultWarmupCount,
        .rotate = true,
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
        } else if (option == "--static") {
            out_config_.rotate = false;
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
    R2D::McVector<WorldTransform> scalar_world_transforms;
    R2D::McVector<WorldTransform> batched_world_transforms;
};

void makeState(const BenchConfig& config_, BenchState& out_state_)
{
    const auto count = static_cast<R2D::Usize>(config_.sprite_count);
    out_state_.transforms.resize(count);
    out_state_.scalar_world_transforms.resize(count);
    out_state_.batched_world_transforms.resize(count);

    // A constant nonzero rotation per item defeats the rotation==0 fast path and
    // forces the sincos affine, so the default measures the compute-bound case.
    for (R2D::U32 index = 0U; index < config_.sprite_count; ++index) {
        const auto grid_x = static_cast<float>(index % 64U) - 32.0F;
        const auto grid_y = static_cast<float>((index / 64U) % 64U) - 32.0F;
        const float rotation = config_.rotate
            ? (0.31F + 0.0007F * static_cast<float>(index % 977U))
            : 0.0F;
        out_state_.transforms[index] = {
            .source_id = index,
            .position_x = grid_x,
            .position_y = grid_y,
            .rotation_radians = rotation,
            .scale_x = 1.0F + static_cast<float>(index % 3U) * 0.25F,
            .scale_y = 1.0F + static_cast<float>(index % 2U) * 0.5F,
        };
    }
}

[[nodiscard]] bool isOk(R2D::SystemResult result_) noexcept
{
    return result_.code == R2D::SystemStatusCode::Ok;
}

} // namespace

int main(int argc_, char** argv_)
{
    try {
        BenchConfig config{};
        if (!parseConfig(argc_, argv_, config)) {
            std::fputs("Render2D batched transform benchmark\n"
                       "Options: --sprites <n> --frames <n> --warmup <n> --static\n",
                       stderr);
            return 1;
        }

        BenchState state{};
        makeState(config, state);

        double scalar_total_ms = 0.0;
        double batched_total_ms = 0.0;
        bool failure = false;

        const R2D::U32 total_frames = config.warmup_count + config.frame_count;
        for (R2D::U32 frame = 0U; frame < total_frames; ++frame) {
            const bool collect = frame >= config.warmup_count;

            auto start = std::chrono::steady_clock::now();
            const auto scalar_result = TransformSys::run(state.transforms, state.scalar_world_transforms);
            auto end = std::chrono::steady_clock::now();
            if (collect) { scalar_total_ms += elapsedMs(start, end); }

            start = std::chrono::steady_clock::now();
            const auto batched_result = Batched::run(state.transforms, state.batched_world_transforms);
            end = std::chrono::steady_clock::now();
            if (collect) { batched_total_ms += elapsedMs(start, end); }

            if (!isOk(scalar_result) || !isOk(batched_result)) {
                failure = true;
                break;
            }

            // All items rotated => no zero-rotation -0.0 split, so the batched
            // output must be byte-identical to the per-element scalar build.
            if (config.rotate) {
                const bool match = std::memcmp(
                    state.batched_world_transforms.data(),
                    state.scalar_world_transforms.data(),
                    static_cast<R2D::Usize>(config.sprite_count) * sizeof(WorldTransform)) == 0;
                if (!match) {
                    failure = true;
                    break;
                }
            }
        }

        if (failure) {
            std::fputs("batched transform bench: mismatch or system failure\n", stderr);
            return 1;
        }

        const auto divisor = static_cast<double>(config.frame_count);
        const double scalar_ms = scalar_total_ms / divisor;
        const double batched_ms = batched_total_ms / divisor;
        const double speedup = batched_ms > 0.0 ? scalar_ms / batched_ms : 0.0;

        std::printf("Render2D batched transform benchmark\n");
        std::printf("sprites=%u rotate=%d frames=%u\n",
            config.sprite_count, config.rotate ? 1 : 0, config.frame_count);
        std::printf("avg_scalar_ms=%.6f\n", scalar_ms);
        std::printf("avg_batched_ms=%.6f\n", batched_ms);
        std::printf("speedup=%.6f\n", speedup);
        return 0;
    } catch (const std::exception& exception) {
        std::fputs("batched transform bench exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("batched transform bench unknown exception\n", stderr);
        return 1;
    }
}
