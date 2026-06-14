#pragma once

#include "Render2D/Core/Types.hpp"

namespace Render2D {

// Shared CPU-parallelism threshold policy (Stage 21E).
//
// Every threaded CPU pipeline (the sprite pipeline today; the text path and the
// batch/sort tail as Stage 21 lands them) consults the same gate so that small
// workloads run on the deterministic single-thread reference path instead of
// paying ThreadCenter scheduling + per-chunk scratch + merge overhead. Stage 10H
// benchmark evidence (see docs/architecture/BENCHMARK_BASELINE.md and
// docs/ProjectMergeTODO.md #22) showed the threaded sprite path was a net loss at
// 10k high-visibility sprites and only a net win around 100k, so the crossover is
// workload- and platform-dependent and the threshold is tunable.
//
// The gate is purely a performance routing decision: both branches run the same
// deterministic systems, so the chosen path never changes the produced output.

inline constexpr U32 kDefaultParallelThreshold = 32768U;

// True when a workload of item_count_ items should run on worker threads rather
// than the single-thread reference path. Single-threaded whenever there is at
// most one worker (nothing to parallelize across) or the workload is below the
// threshold.
[[nodiscard]] constexpr bool shouldParallelizeItemCount(
    U32 item_count_,
    U32 parallel_threshold_,
    U32 worker_count_) noexcept
{
    if (worker_count_ <= 1U) {
        return false;
    }
    return item_count_ >= parallel_threshold_;
}

} // namespace Render2D
