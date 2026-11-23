#pragma once

// The workload's device side: 13 nodes, two shapes. The ablation (M3) uses the
// clean one; the pingpong exists so the linter has something to find.

#include <cuda_runtime.h>

#include <string>

namespace qtoolkit {

// 13 nodes and 14 buffers: buffers[0] is the input, buffers[i + 1] is the
// output of node i. Each node writes its own buffer, so two nodes that the
// graph lets run together never touch the same memory.
inline constexpr int kNodeCount = 13;
inline constexpr int kBufferCount = kNodeCount + 1;

// Stem, a 4-node and a 3-node branch on two streams, a join of both, a 4-node
// tail: width 2 and depth below the node count, the shape a linter should like.
[[nodiscard]] bool capture_clean(float* const* buffers, int count, cudaGraph_t& graph,
                                 std::string& error);

// One event per node passing between two streams: node i waits on i - 1 and is
// ordered after i - 2 by its own stream. Depth equals the node count and about
// half the edges are implied by a longer path: the anti-pattern the linter must
// catch, and the reason it is captured at all.
[[nodiscard]] bool capture_pingpong(float* const* buffers, int count, cudaGraph_t& graph,
                                    std::string& error);

}  // namespace qtoolkit
