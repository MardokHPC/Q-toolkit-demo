#pragma once

// The price of the shape. The same thirteen kernels, two graphs: the clean
// fork/join and the pingpong anti-pattern. The linter says the second one has
// eleven edges a longer path already implies and three levels more; this stage
// says what that costs, in the two places a caller pays for it.
//
// Two clocks, because the two costs are not the same kind of thing:
//   - cudaGraphInstantiate is a host call and never touches a stream, so a pair
//     of CUDA events would measure the empty stream. The host clock sees it.
//   - cudaGraphLaunch is asynchronous, so the launch is timed with cuda-timer's
//     Timer, on the ablation's protocol (10 warm-up, 100 measured runs), with the
//     two shapes interleaved in one loop so the difference is what gets measured.
//
// The launch keeps its min and max beside the median: a delta of a few
// microseconds is only a number if the reader can see the spread it came from.

#include <cuda_runtime.h>

#include <cstdint>
#include <string>

namespace qtoolkit {

inline constexpr int kShapeWarmupRuns = 10;
inline constexpr int kShapeMeasureRuns = 100;
inline constexpr int kInstantiateWarmupRuns = 5;
inline constexpr int kInstantiateMeasureRuns = 50;

struct ShapeCost {
    std::int64_t instantiate_clean_ns = 0;
    std::int64_t instantiate_pingpong_ns = 0;
    // pingpong - clean. Signed on purpose: a shape that came out cheaper is a
    // number the report has to be able to carry, not a case to hide.
    std::int64_t instantiate_delta_ns = 0;

    std::int64_t launch_clean_ns = 0;
    std::int64_t launch_clean_min_ns = 0;
    std::int64_t launch_clean_max_ns = 0;
    std::int64_t launch_pingpong_ns = 0;
    std::int64_t launch_pingpong_min_ns = 0;
    std::int64_t launch_pingpong_max_ns = 0;
    std::int64_t launch_delta_ns = 0;
};

// Both graphs must outlive the call, and the stream must be the one the buffers
// were allocated on: the pool's events are recorded there.
[[nodiscard]] bool measure_shapes(cudaGraph_t clean, cudaGraph_t pingpong, cudaStream_t stream,
                                  ShapeCost& out, std::string& error);

}  // namespace qtoolkit
