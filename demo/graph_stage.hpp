#pragma once

// The graph-lint boundary: a live handle to the file, the file back to the
// report block, and the weights table, which is graph-lint's format bound to its
// graph by fingerprint and therefore read and written from the same place.

#include "report.hpp"

#include <cuda_runtime_api.h>

#include <cstdint>
#include <string>
#include <vector>

namespace qtoolkit {

// `handles_by_id` receives the canonical id -> handle map from from_cuda: it is
// the only correct way to name a node for the ablation (§0.4).
[[nodiscard]] bool write_graph(const cudaGraph_t& graph, const std::string& path,
                               std::string& error,
                               std::vector<cudaGraphNode_t>* handles_by_id = nullptr);

[[nodiscard]] bool analyze_graph(const std::string& path, GraphBlock& out, std::string& error);

struct WeightedPath {
    std::vector<int> nodes;
    std::uint64_t ns = 0;
};

[[nodiscard]] bool write_weights(const std::string& graph_path, const std::string& weights_path,
                                 const std::vector<std::uint64_t>& ns, std::string& error);

// Re-reads both files: the weighted path is recomputed from the committed
// artifacts, exactly as the verifier will (§6, check 2).
[[nodiscard]] bool weighted_path(const std::string& graph_path, const std::string& weights_path,
                                 WeightedPath& out, std::string& error);

}  // namespace qtoolkit
