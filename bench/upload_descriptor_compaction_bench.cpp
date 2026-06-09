#include <Render2D/Render2D.hpp>

#include <charconv>
#include <chrono>
#include <cstdio>
#include <span>
#include <string_view>
#include <system_error>

namespace {

namespace R2D = Render2D;

using Provider = R2D::VulkanNativeProvider;
using Dim = R2D::Dim2;
using UploadCommand = R2D::UploadCommand<Provider, Dim>;
using DescriptorSlice = R2D::DescriptorSlice<Provider, Dim>;

inline constexpr R2D::U32 kDefaultItemCount = 65536U;
inline constexpr R2D::U32 kDefaultFrameCount = 8U;
inline constexpr R2D::U32 kDefaultWarmupCount = 2U;
inline constexpr R2D::U32 kMergeGroupSize = 4U;
inline constexpr R2D::U64 kUploadBytes = 64U;

struct BenchConfig {
    R2D::U32 item_count;
    R2D::U32 frame_count;
    R2D::U32 warmup_count;
};

struct BenchState {
    R2D::McVector<UploadCommand> uploads;
    R2D::McVector<UploadCommand> coalesced_uploads;
    R2D::McVector<DescriptorSlice> descriptor_slices;
    R2D::McVector<DescriptorSlice> compacted_descriptor_slices;
};

struct BenchTotals {
    double upload_coalesce_ms;
    double descriptor_compaction_ms;
    R2D::U32 upload_write_count;
    R2D::U32 descriptor_write_count;
};

[[nodiscard]] double elapsedMs(
    std::chrono::steady_clock::time_point start_,
    std::chrono::steady_clock::time_point end_) noexcept
{
    return std::chrono::duration<double, std::milli>(end_ - start_).count();
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
    if (result.ec != std::errc{} || result.ptr != last || value > 0xFFFFFFFFULL) {
        return false;
    }

    out_value_ = static_cast<R2D::U32>(value);
    return true;
}

[[nodiscard]] bool parseConfig(int argc_, char** argv_, BenchConfig& out_config_) noexcept
{
    out_config_ = {
        .item_count = kDefaultItemCount,
        .frame_count = kDefaultFrameCount,
        .warmup_count = kDefaultWarmupCount,
    };

    for (int index = 1; index < argc_; ++index) {
        const std::string_view option{argv_[index]};
        if (option == "--items" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.item_count)) {
                return false;
            }
            ++index;
        } else if (option == "--frames" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.frame_count)) {
                return false;
            }
            ++index;
        } else if (option == "--warmup" && index + 1 < argc_) {
            if (!parseU32(argv_[index + 1], out_config_.warmup_count)) {
                return false;
            }
            ++index;
        } else {
            return false;
        }
    }

    return out_config_.item_count != 0U && out_config_.frame_count != 0U;
}

void printUsage() noexcept
{
    static_cast<void>(std::fputs(
        "Render2D upload/descriptor compaction benchmark\n"
        "Options:\n"
        "  --items <count>   (>0)\n"
        "  --frames <count>  (>0)\n"
        "  --warmup <count>  (>=0)\n",
        stderr));
}

[[nodiscard]] R2D::U32 compactedGroupCount(R2D::U32 item_count_) noexcept
{
    return item_count_ / kMergeGroupSize + (item_count_ % kMergeGroupSize == 0U ? 0U : 1U);
}

void fillInputs(const BenchConfig& config_, BenchState& state_) noexcept
{
    for (R2D::U32 index = 0U; index < config_.item_count; ++index) {
        const R2D::U32 group = index / kMergeGroupSize;
        const R2D::U32 in_group_index = index % kMergeGroupSize;
        const R2D::U64 base_offset = static_cast<R2D::U64>(group) * 4096U;
        const R2D::U64 group_offset = static_cast<R2D::U64>(in_group_index) * kUploadBytes;

        state_.uploads[index] = {
            .resource_id = group,
            .source_offset = base_offset + group_offset,
            .destination_offset = base_offset + 2048U + group_offset,
            .byte_count = kUploadBytes,
            .upload_kind = 1U + group % 2U,
            .flags = group % 4U,
        };
        state_.descriptor_slices[index] = {
            .descriptor_set_id = group,
            .first = in_group_index,
            .count = 1U,
            .generation = 1U + group % 3U,
        };
    }
}

[[nodiscard]] BenchState makeState(const BenchConfig& config_)
{
    BenchState state{};
    state.uploads.resize(config_.item_count);
    state.coalesced_uploads.resize(config_.item_count);
    state.descriptor_slices.resize(config_.item_count);
    state.compacted_descriptor_slices.resize(config_.item_count);
    fillInputs(config_, state);
    return state;
}

[[nodiscard]] bool runFrame(BenchState& state_, bool collect_, BenchTotals& totals_) noexcept
{
    const std::span<const UploadCommand> upload_input{
        state_.uploads.data(),
        state_.uploads.size(),
    };
    const std::span<UploadCommand> upload_output{
        state_.coalesced_uploads.data(),
        state_.coalesced_uploads.size(),
    };
    const std::span<const DescriptorSlice> descriptor_input{
        state_.descriptor_slices.data(),
        state_.descriptor_slices.size(),
    };
    const std::span<DescriptorSlice> descriptor_output{
        state_.compacted_descriptor_slices.data(),
        state_.compacted_descriptor_slices.size(),
    };

    auto start = std::chrono::steady_clock::now();
    auto result = R2D::UploadCoalesceSystem<Provider, Dim>::run(upload_input, upload_output);
    auto end = std::chrono::steady_clock::now();
    if (result.code != R2D::SystemStatusCode::Ok) {
        return false;
    }
    if (collect_) {
        totals_.upload_coalesce_ms += elapsedMs(start, end);
        totals_.upload_write_count = result.write_count;
    }

    start = std::chrono::steady_clock::now();
    result = R2D::DescriptorCompactionSystem<Provider, Dim>::run(descriptor_input, descriptor_output);
    end = std::chrono::steady_clock::now();
    if (result.code != R2D::SystemStatusCode::Ok) {
        return false;
    }
    if (collect_) {
        totals_.descriptor_compaction_ms += elapsedMs(start, end);
        totals_.descriptor_write_count = result.write_count;
    }

    return true;
}

[[nodiscard]] bool runBenchmark(
    const BenchConfig& config_,
    BenchState& state_,
    BenchTotals& totals_) noexcept
{
    for (R2D::U32 warmup_index = 0U; warmup_index < config_.warmup_count; ++warmup_index) {
        if (!runFrame(state_, false, totals_)) {
            return false;
        }
    }

    for (R2D::U32 frame_index = 0U; frame_index < config_.frame_count; ++frame_index) {
        if (!runFrame(state_, true, totals_)) {
            return false;
        }
    }

    const R2D::U32 expected_count = compactedGroupCount(config_.item_count);
    return totals_.upload_write_count == expected_count &&
        totals_.descriptor_write_count == expected_count;
}

void printReport(const BenchConfig& config_, const BenchTotals& totals_) noexcept
{
    const auto divisor = static_cast<double>(config_.frame_count);
    static_cast<void>(std::printf(
        "items,frames,warmup,upload_output,descriptor_output,avg_upload_coalesce_ms,avg_descriptor_compaction_ms\n"
        "%u,%u,%u,%u,%u,%.9f,%.9f\n",
        config_.item_count,
        config_.frame_count,
        config_.warmup_count,
        totals_.upload_write_count,
        totals_.descriptor_write_count,
        totals_.upload_coalesce_ms / divisor,
        totals_.descriptor_compaction_ms / divisor));
}

} // namespace

int main(int argc_, char** argv_)
{
    try {
        BenchConfig config{};
        if (!parseConfig(argc_, argv_, config)) {
            printUsage();
            return 1;
        }

        auto state = makeState(config);
        BenchTotals totals{
            .upload_coalesce_ms = 0.0,
            .descriptor_compaction_ms = 0.0,
            .upload_write_count = 0U,
            .descriptor_write_count = 0U,
        };
        if (!runBenchmark(config, state, totals)) {
            static_cast<void>(std::fputs("benchmark compaction result mismatch\n", stderr));
            return 1;
        }

        printReport(config, totals);
    } catch (...) {
        static_cast<void>(std::fputs("benchmark failed with an unexpected exception\n", stderr));
        return 1;
    }

    return 0;
}
