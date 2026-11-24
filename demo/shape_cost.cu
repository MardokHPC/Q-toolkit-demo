#include "shape_cost.hpp"

#include <cuda_timer/timer.cuh>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace qtoolkit {
namespace {

void failure(const char* call, cudaError_t status, std::string& error) {
    error = std::string(call) + ": " + cudaGetErrorString(status);
}

// Sorts the samples and returns their median, leaving the vector ordered so the
// caller reads min and max off the ends. The same convention as
// cuda_timer::Stats::median(): average the two middle elements on an even count.
// Two medians in one document that disagree on a tie would be a defect nobody
// would ever find.
std::int64_t sort_and_median(std::vector<std::int64_t>& samples) {
    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2;
    if (samples.size() % 2 == 0) {
        const double pair = (static_cast<double>(samples[middle - 1]) +
                             static_cast<double>(samples[middle])) /
                            2.0;
        return static_cast<std::int64_t>(std::llround(pair));
    }
    return samples[middle];
}

// cudaGraphInstantiate is a host call: it never touches the stream, so a pair of
// CUDA events would time the empty stream and report a few hundred nanoseconds
// for every graph. The host clock is the only clock that sees it. The exec is
// destroyed outside the window: this is the price of instantiating, not of
// tearing down.
bool instantiate_median(cudaGraph_t graph, std::int64_t& out_ns, std::string& error) {
    std::vector<std::int64_t> samples;
    samples.reserve(static_cast<std::size_t>(kInstantiateMeasureRuns));
    for (int run = 0; run < kInstantiateWarmupRuns + kInstantiateMeasureRuns; ++run) {
        const auto start = std::chrono::steady_clock::now();
        cudaGraphExec_t exec = nullptr;
        const cudaError_t status = cudaGraphInstantiate(&exec, graph, 0);
        const auto stop = std::chrono::steady_clock::now();
        if (status != cudaSuccess) {
            failure("cudaGraphInstantiate", status, error);
            return false;
        }
        (void)cudaGraphExecDestroy(exec);
        if (run >= kInstantiateWarmupRuns) {
            samples.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
        }
    }
    out_ns = sort_and_median(samples);
    return true;
}

struct LaunchStats {
    std::int64_t median_ns = 0;
    std::int64_t min_ns = 0;
    std::int64_t max_ns = 0;
};

// Both shapes are launched inside one loop, alternating which one goes first.
// Two separate sweeps measure the card twice: in the runs that produced this
// block, a sweep of the clean graph and then a sweep of the pingpong disagreed
// in sign with the reverse order. Alternating makes the difference the measured
// quantity — a clock that drifts during the minute moves both shapes together
// instead of landing in the delta.
bool launch_pair(cudaGraph_t clean, cudaGraph_t pingpong, cudaStream_t stream,
                 LaunchStats& clean_stats, LaunchStats& pingpong_stats, std::string& error) {
    cudaGraphExec_t clean_exec = nullptr;
    cudaGraphExec_t pingpong_exec = nullptr;
    cudaError_t status = cudaGraphInstantiate(&clean_exec, clean, 0);
    if (status != cudaSuccess) {
        failure("cudaGraphInstantiate", status, error);
        return false;
    }
    status = cudaGraphInstantiate(&pingpong_exec, pingpong, 0);
    if (status != cudaSuccess) {
        failure("cudaGraphInstantiate", status, error);
        (void)cudaGraphExecDestroy(clean_exec);
        return false;
    }

    bool measured = true;
    try {
        cuda_timer::Timer timer(stream);
        cudaError_t launch_status = cudaSuccess;
        const auto timed = [&timer, &launch_status, stream](cudaGraphExec_t exec,
                                                            std::int64_t& out_ns) {
            timer.start();
            const cudaError_t current = cudaGraphLaunch(exec, stream);
            // stop() blocks on the event: this is SyncMode::Blocking, the same
            // wait cuda_timer::benchmark() does.
            const float ms = timer.stop();
            if (current != cudaSuccess) {
                launch_status = current;
                return;
            }
            out_ns = std::llround(static_cast<double>(ms) * 1.0e6);
        };

        std::vector<std::int64_t> clean_samples;
        std::vector<std::int64_t> pingpong_samples;
        clean_samples.reserve(static_cast<std::size_t>(kShapeMeasureRuns));
        pingpong_samples.reserve(static_cast<std::size_t>(kShapeMeasureRuns));

        for (int run = 0; run < kShapeWarmupRuns + kShapeMeasureRuns; ++run) {
            std::int64_t clean_ns = 0;
            std::int64_t pingpong_ns = 0;
            if (run % 2 == 0) {
                timed(clean_exec, clean_ns);
                timed(pingpong_exec, pingpong_ns);
            } else {
                timed(pingpong_exec, pingpong_ns);
                timed(clean_exec, clean_ns);
            }
            if (launch_status != cudaSuccess) {
                failure("cudaGraphLaunch", launch_status, error);
                measured = false;
                break;
            }
            if (run >= kShapeWarmupRuns) {
                clean_samples.push_back(clean_ns);
                pingpong_samples.push_back(pingpong_ns);
            }
        }

        if (measured) {
            clean_stats.median_ns = sort_and_median(clean_samples);
            clean_stats.min_ns = clean_samples.front();
            clean_stats.max_ns = clean_samples.back();
            pingpong_stats.median_ns = sort_and_median(pingpong_samples);
            pingpong_stats.min_ns = pingpong_samples.front();
            pingpong_stats.max_ns = pingpong_samples.back();
        }
    } catch (const std::exception& caught) {
        error = caught.what();
        measured = false;
    }

    (void)cudaGraphExecDestroy(pingpong_exec);
    (void)cudaGraphExecDestroy(clean_exec);
    return measured;
}

}  // namespace

bool measure_shapes(cudaGraph_t clean, cudaGraph_t pingpong, cudaStream_t stream, ShapeCost& out,
                    std::string& error) {
    error.clear();
    out = ShapeCost{};
    if (clean == nullptr || pingpong == nullptr) {
        error = "measure_shapes: a null graph";
        return false;
    }

    if (!instantiate_median(clean, out.instantiate_clean_ns, error) ||
        !instantiate_median(pingpong, out.instantiate_pingpong_ns, error)) {
        return false;
    }

    LaunchStats clean_launch;
    LaunchStats pingpong_launch;
    if (!launch_pair(clean, pingpong, stream, clean_launch, pingpong_launch, error)) {
        return false;
    }
    out.launch_clean_ns = clean_launch.median_ns;
    out.launch_clean_min_ns = clean_launch.min_ns;
    out.launch_clean_max_ns = clean_launch.max_ns;
    out.launch_pingpong_ns = pingpong_launch.median_ns;
    out.launch_pingpong_min_ns = pingpong_launch.min_ns;
    out.launch_pingpong_max_ns = pingpong_launch.max_ns;

    out.instantiate_delta_ns = out.instantiate_pingpong_ns - out.instantiate_clean_ns;
    out.launch_delta_ns = out.launch_pingpong_ns - out.launch_clean_ns;
    return true;
}

}  // namespace qtoolkit
