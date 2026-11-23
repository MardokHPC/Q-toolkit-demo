#pragma once

// The ablation: one node enabled at a time on one exec, then every node
// disabled for the zero point. The protocol is MASTERHANDOFF §3.8, the
// statistics are cuda-timer's. The number is a lower bound (§0.5): subtracting
// the zero point is not calibrating, and the difference between the two floors
// is the error the method declares and cannot remove.

#include <cuda_runtime.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qtoolkit {

inline constexpr int kAblationWarmupRuns = 10;
inline constexpr int kAblationMeasureRuns = 100;

struct Ablation {
    std::int64_t zero_point_ns = 0;
    std::vector<std::int64_t> median_ns;   // by canonical id, zero point included
    std::vector<std::uint64_t> weight_ns;  // max(0, median - zero point)
    std::size_t clamped = 0;
};

// The handles are the canonical id -> handle map from from_cuda. Reading them
// off cudaGraphGetNodes would name a different node than the artifact does
// (§0.4), and nothing downstream could notice. The stream is the caller's: the
// buffers go back to the pool on it, so it must outlive the measurement.
[[nodiscard]] bool measure_nodes(cudaGraph_t graph,
                                 const std::vector<cudaGraphNode_t>& handles_by_id,
                                 cudaStream_t stream, Ablation& out, std::string& error);

}  // namespace qtoolkit
