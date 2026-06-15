// Stage 21B batch tail benchmark: single-thread BatchSystem::run vs the parallel
// ThreadedBatchRuntime over the same draw stream, with a per-frame memcmp that
// asserts byte-identical batches. --run-length controls the batch structure
// (draws per batch run): small values stress the write-bound, no-merge case;
// larger values model the sorted/clustered stream where batching is actually
// used. See docs/architecture/BENCHMARK_BASELINE.md.
#include <Render2D/System/ThreadedBatchSystem.hpp>

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

using DrawCommand = R2D::DrawCommand<R2D::VulkanNativeProvider, R2D::Dim2>;
using BatchCommand = R2D::BatchCommand<R2D::VulkanNativeProvider, R2D::Dim2>;
using BatchRuntime = R2D::ThreadedBatchRuntime<R2D::VulkanNativeProvider, R2D::Dim2>;
using BatchRef = R2D::BatchSystem<R2D::VulkanNativeProvider, R2D::Dim2>;

inline constexpr R2D::U32 kDefaultDrawCount = 524288U;
inline constexpr R2D::U32 kDefaultFrameCount = 8U;
inline constexpr R2D::U32 kDefaultWarmupCount = 2U;
inline constexpr R2D::U32 kDefaultWorkerCount = 0U;
inline constexpr R2D::U32 kDefaultMinItemsPerTask = 4096U;
inline constexpr R2D::U32 kDefaultRunLength = 8U;

struct BenchConfig {
    R2D::U32 draw_count;
    R2D::U32 frame_count;
    R2D::U32 warmup_count;
    R2D::U32 worker_count;
    R2D::U32 min_items_per_task;
    R2D::U32 run_length;
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
        .draw_count = kDefaultDrawCount,
        .frame_count = kDefaultFrameCount,
        .warmup_count = kDefaultWarmupCount,
        .worker_count = kDefaultWorkerCount,
        .min_items_per_task = kDefaultMinItemsPerTask,
        .run_length = kDefaultRunLength,
    };
    for (int index = 1; index < argc_; ++index) {
        const std::string_view option{argv_[index]};
        if (option == "--draws" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.draw_count)) { return false; }
            ++index;
        } else if (option == "--frames" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.frame_count)) { return false; }
            ++index;
        } else if (option == "--warmup" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.warmup_count)) { return false; }
            ++index;
        } else if (option == "--workers" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.worker_count)) { return false; }
            ++index;
        } else if (option == "--min-items-per-task" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.min_items_per_task)) { return false; }
            ++index;
        } else if (option == "--run-length" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.run_length)) { return false; }
            ++index;
        } else {
            return false;
        }
    }
    return out_config_.frame_count != 0U && out_config_.draw_count != 0U &&
        out_config_.run_length != 0U;
}

[[nodiscard]] double elapsedMs(
    std::chrono::steady_clock::time_point start_,
    std::chrono::steady_clock::time_point end_) noexcept
{
    return std::chrono::duration<double, std::milli>(end_ - start_).count();
}

struct BenchState {
    R2D::McVector<DrawCommand> draws;
    R2D::McVector<BatchCommand> reference_batches;
    R2D::McVector<BatchCommand> threaded_batches;
};

void makeState(const BenchConfig& config_, BenchState& out_state_)
{
    const auto draw_count = static_cast<R2D::Usize>(config_.draw_count);
    out_state_.draws.resize(draw_count);
    out_state_.reference_batches.resize(draw_count);
    out_state_.threaded_batches.resize(draw_count);

    // sort_key changes every run_length draws; the other batch-key fields are held
    // constant, so adjacent draws merge within a run and split at run boundaries -
    // i.e. runs of exactly run_length, giving draw_count / run_length batches.
    for (R2D::Usize index = 0U; index < draw_count; ++index) {
        const auto i = static_cast<R2D::U32>(index);
        out_state_.draws[index] = {
            .source_index = i,
            .material_id = 7U,
            .material_generation = 1U,
            .texture_id = 3U,
            .texture_generation = 0U,
            .vertex_first = i * 4U,
            .vertex_count = 4U,
            .index_first = i * 6U,
            .index_count = 6U,
            .instance_first = i,
            .instance_count = 1U,
            .sort_key = i / config_.run_length,
            .layer = 0U,
            .flags = 0U,
        };
    }
}

} // namespace

int main(int argc_, char** argv_)
{
    try {
        BenchConfig config{};
        if (!parseConfig(argc_, argv_, config)) {
            std::fputs("Render2D threaded batch benchmark\n"
                       "Options: --draws <n> --frames <n> --warmup <n>\n"
                       "         --workers <n> --min-items-per-task <n> --run-length <n>\n",
                       stderr);
            return 1;
        }

        BenchState state{};
        makeState(config, state);

        BatchRuntime runtime{{
            .worker_count = config.worker_count,
            .min_items_per_task = config.min_items_per_task,
            // Force the threaded path so the benchmark always measures it.
            .parallel_threshold = 1U,
        }};

        double reference_total_ms = 0.0;
        double threaded_total_ms = 0.0;
        R2D::U32 batch_count = 0U;
        bool mismatch = false;

        const R2D::U32 total_frames = config.warmup_count + config.frame_count;
        for (R2D::U32 frame = 0U; frame < total_frames; ++frame) {
            const bool collect = frame >= config.warmup_count;

            auto start = std::chrono::steady_clock::now();
            const auto reference_result = BatchRef::run(state.draws, state.reference_batches);
            auto end = std::chrono::steady_clock::now();
            if (collect) { reference_total_ms += elapsedMs(start, end); }

            start = std::chrono::steady_clock::now();
            const auto threaded_result = runtime.runBatch(state.draws, state.threaded_batches);
            end = std::chrono::steady_clock::now();
            if (collect) { threaded_total_ms += elapsedMs(start, end); }

            batch_count = threaded_result.write_count;
            if (reference_result.code != R2D::SystemStatusCode::Ok ||
                threaded_result.code != R2D::SystemStatusCode::Ok ||
                reference_result.write_count != threaded_result.write_count ||
                std::memcmp(
                    state.reference_batches.data(),
                    state.threaded_batches.data(),
                    static_cast<R2D::Usize>(batch_count) * sizeof(BatchCommand)) != 0) {
                mismatch = true;
                break;
            }
        }

        if (mismatch) {
            std::fputs("threaded batch bench: reference/threaded mismatch\n", stderr);
            return 1;
        }

        const auto divisor = static_cast<double>(config.frame_count);
        const double reference_ms = reference_total_ms / divisor;
        const double threaded_ms = threaded_total_ms / divisor;
        const double speedup = threaded_ms > 0.0 ? reference_ms / threaded_ms : 0.0;

        std::printf("Render2D threaded batch benchmark\n");
        std::printf("draws=%u run_length=%u batches=%u workers=%u min_items_per_task=%u frames=%u\n",
            config.draw_count, config.run_length, batch_count, runtime.workerCount(),
            config.min_items_per_task, config.frame_count);
        std::printf("avg_reference_ms=%.6f\n", reference_ms);
        std::printf("avg_threaded_ms=%.6f\n", threaded_ms);
        std::printf("speedup=%.6f\n", speedup);
        return 0;
    } catch (const std::exception& exception) {
        std::fputs("threaded batch bench exception: ", stderr);
        std::fputs(exception.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    } catch (...) {
        std::fputs("threaded batch bench unknown exception\n", stderr);
        return 1;
    }
}
