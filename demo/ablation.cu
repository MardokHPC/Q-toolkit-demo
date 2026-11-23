#include "ablation.hpp"

#include <cuda_timer/benchmark.cuh>

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

// -1 disables every node: that is the zero point.
bool set_only(cudaGraphExec_t exec, const std::vector<cudaGraphNode_t>& handles, int only,
              std::string& error) {
    for (std::size_t id = 0; id < handles.size(); ++id) {
        const unsigned int enabled = static_cast<int>(id) == only ? 1u : 0u;
        const cudaError_t status = cudaGraphNodeSetEnabled(exec, handles[id], enabled);
        if (status != cudaSuccess) {
            failure("cudaGraphNodeSetEnabled", status, error);
            return false;
        }
    }
    return true;
}

bool measure(cudaGraphExec_t exec, cudaStream_t stream, std::int64_t& median_ns,
             std::string& error) {
    cuda_timer::BenchmarkConfig config;
    config.warmup_runs = static_cast<std::size_t>(kAblationWarmupRuns);
    config.measure_runs = static_cast<std::size_t>(kAblationMeasureRuns);
    config.store_samples = true;
    config.stream = stream;

    // benchmark() cannot check the launch it times; a failed launch would be
    // measured as a fast one.
    cudaError_t launch_status = cudaSuccess;
    const auto launch = [&exec, stream, &launch_status] {
        const cudaError_t status = cudaGraphLaunch(exec, stream);
        if (status != cudaSuccess) {
            launch_status = status;
        }
    };

    try {
        const cuda_timer::Stats stats = cuda_timer::benchmark(launch, config);
        if (launch_status != cudaSuccess) {
            failure("cudaGraphLaunch", launch_status, error);
            return false;
        }
        // cuda-timer reports milliseconds in a float; the document is integer ns.
        median_ns = std::llround(stats.median() * 1.0e6);
        return true;
    } catch (const std::exception& caught) {
        error = caught.what();
        return false;
    }
}

}  // namespace

bool measure_nodes(cudaGraph_t graph, const std::vector<cudaGraphNode_t>& handles_by_id,
                   cudaStream_t stream, Ablation& out, std::string& error) {
    error.clear();
    out = Ablation{};
    if (handles_by_id.empty()) {
        error = "no node handles: from_cuda returned none";
        return false;
    }

    cudaGraphExec_t exec = nullptr;
    const cudaError_t status = cudaGraphInstantiate(&exec, graph, 0);
    if (status != cudaSuccess) {
        failure("cudaGraphInstantiate", status, error);
        return false;
    }

    const std::size_t nodes = handles_by_id.size();
    out.median_ns.assign(nodes, 0);
    out.weight_ns.assign(nodes, 0);

    bool measured = true;
    for (std::size_t id = 0; id < nodes && measured; ++id) {
        measured = set_only(exec, handles_by_id, static_cast<int>(id), error) &&
                   measure(exec, stream, out.median_ns[id], error);
    }
    // The zero point closes the sweep, so it belongs to the clock the nodes
    // were measured at and not to a second warm-up.
    if (measured) {
        measured = set_only(exec, handles_by_id, -1, error) &&
                   measure(exec, stream, out.zero_point_ns, error);
    }

    (void)cudaGraphExecDestroy(exec);
    if (!measured) {
        return false;
    }

    for (std::size_t id = 0; id < nodes; ++id) {
        const std::int64_t weight = out.median_ns[id] - out.zero_point_ns;
        if (weight <= 0) {
            out.weight_ns[id] = 0;
            ++out.clamped;
        } else {
            out.weight_ns[id] = static_cast<std::uint64_t>(weight);
        }
    }
    return true;
}

}  // namespace qtoolkit
