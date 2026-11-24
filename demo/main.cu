// The command. M4: the workload's buffers are pool allocations, every request
// becomes a line of trace.jsonl, poolkit_advise turns that trace into a
// recommendation, and the report carries both sides with names that cannot be
// mistaken for one another.

#include "ablation.hpp"
#include "advise_stage.hpp"
#include "file_io.hpp"
#include "graph_stage.hpp"
#include "report.hpp"
#include "trace_sink.hpp"
#include "workload.hpp"

#include <cuda_runtime.h>

#include <poolkit/pool.cuh>
#include <poolkit/pool.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

void check(cudaError_t status, const char* call) {
    if (status != cudaSuccess) {
        std::fprintf(stderr, "qtoolkit_workload: %s: %s\n", call, cudaGetErrorString(status));
        std::exit(1);
    }
}

int fail(const std::string& detail) {
    std::fprintf(stderr, "qtoolkit_workload: %s\n", detail.c_str());
    return 1;
}

std::string cuda_version(int version) {
    return std::to_string(version / 1000) + "." + std::to_string((version % 1000) / 10);
}

int widest(const std::vector<int>& width) {
    return width.empty() ? 0 : *std::max_element(width.begin(), width.end());
}

// More than 30 % clamped means the graph is smaller than this card's
// measurement floor, and the table would be a number about the timer.
bool too_small_for_the_card(const qtoolkit::Ablation& ablation) {
    return ablation.clamped * 10 > ablation.weight_ns.size() * 3;
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path out_dir =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("artifacts");
    std::filesystem::create_directories(out_dir);
    const std::string clean_path = (out_dir / "graph.json").string();
    const std::string pingpong_path = (out_dir / "graph.pingpong.json").string();
    const std::string weights_path = (out_dir / "weights.json").string();
    const std::string trace_path = (out_dir / "trace.jsonl").string();
    const std::string advise_path = (out_dir / "advise.json").string();
    const std::string report_path = (out_dir / "report.json").string();

    constexpr int kCount = 1 << 20;
    constexpr std::size_t kBytes = static_cast<std::size_t>(kCount) * sizeof(float);

    cudaStream_t stream = nullptr;
    check(cudaStreamCreate(&stream), "cudaStreamCreate");

    // The reference configuration belongs to the workload: 4 MiB buffers, so a
    // 4 MiB class and one block per chunk. The pool's defaults cap a class at
    // 2 MiB, which would send every request down the large path and measure
    // nothing.
    poolkit::CudaBackend backend;
    poolkit::Pool::Config config;
    config.chunk_bytes = std::size_t{4} << 20;
    config.max_class_bytes = std::size_t{4} << 20;
    config.default_stream = stream;
    poolkit::Pool pool(backend, config);

    qtoolkit::PoolConfigRecord reference;
    reference.chunk_bytes = config.chunk_bytes;
    reference.min_class_bytes = config.min_class_bytes;
    reference.max_class_bytes = config.max_class_bytes;
    reference.growth = config.growth;
    reference.power_of_two = config.include_power_of_two;

    std::string error;
    qtoolkit::TraceSink sink;
    if (!sink.open(trace_path, error)) {
        return fail(error);
    }

    std::vector<poolkit::DevicePtr> handles;
    std::vector<float*> buffers;
    handles.reserve(qtoolkit::kBufferCount);
    buffers.reserve(qtoolkit::kBufferCount);
    std::uint64_t bytes_requested = 0;
    for (int index = 0; index < qtoolkit::kBufferCount; ++index) {
        handles.push_back(sink.allocate(pool, kBytes, stream));
        buffers.push_back(static_cast<float*>(handles.back().get()));
        bytes_requested += kBytes;
    }
    check(cudaMemset(buffers[0], 0, kBytes), "cudaMemset");

    std::vector<cudaGraphNode_t> handles_by_id;
    cudaGraph_t clean = nullptr;
    if (!qtoolkit::capture_clean(buffers.data(), kCount, clean, error)) {
        return fail(error);
    }
    if (!qtoolkit::write_graph(clean, clean_path, error, &handles_by_id)) {
        return fail(error);
    }

    cudaGraph_t pingpong = nullptr;
    if (!qtoolkit::capture_pingpong(buffers.data(), kCount, pingpong, error)) {
        return fail(error);
    }
    const bool pingpong_written = qtoolkit::write_graph(pingpong, pingpong_path, error);
    (void)cudaGraphDestroy(pingpong);
    if (!pingpong_written) {
        return fail(error);
    }

    qtoolkit::Report report;
    report.version = QTOOLKIT_VERSION;
    report.pins.cuda_timer = QTOOLKIT_PIN_CUDA_TIMER;
    report.pins.poolkit = QTOOLKIT_PIN_POOLKIT;
    report.pins.ulpgrad = QTOOLKIT_PIN_ULPGRAD;
    report.pins.graph_lint = QTOOLKIT_PIN_GRAPH_LINT;

    cudaDeviceProp properties{};
    check(cudaGetDeviceProperties(&properties, 0), "cudaGetDeviceProperties");
    report.environment.device = properties.name;
    report.environment.compute_major = properties.major;
    report.environment.compute_minor = properties.minor;
    report.environment.cuda_runtime = cuda_version(CUDART_VERSION);

    if (!qtoolkit::analyze_graph(clean_path, report.clean, error)) {
        return fail(error);
    }
    if (!qtoolkit::analyze_graph(pingpong_path, report.pingpong, error)) {
        return fail(error);
    }

    qtoolkit::Ablation ablation;
    const bool measured = qtoolkit::measure_nodes(clean, handles_by_id, stream, ablation, error);
    (void)cudaGraphDestroy(clean);
    if (!measured) {
        return fail(error);
    }

    for (std::size_t id = 0; id < ablation.weight_ns.size(); ++id) {
        std::printf("node %2zu: median %8lld ns, weight %8llu ns%s\n", id,
                    static_cast<long long>(ablation.median_ns[id]),
                    static_cast<unsigned long long>(ablation.weight_ns[id]),
                    ablation.weight_ns[id] == 0 ? " (clamped)" : "");
    }
    std::printf("zero point: %lld ns, %zu of %zu nodes clamped\n",
                static_cast<long long>(ablation.zero_point_ns), ablation.clamped,
                ablation.weight_ns.size());

    if (too_small_for_the_card(ablation)) {
        return fail("more than 30 % of the nodes clamped to zero: the graph is too small for " +
                    report.environment.device + ", grow the workload");
    }

    if (!qtoolkit::write_weights(clean_path, weights_path, ablation.weight_ns, error)) {
        return fail(error);
    }

    qtoolkit::WeightedPath path;
    if (!qtoolkit::weighted_path(clean_path, weights_path, path, error)) {
        return fail(error);
    }

    // The graphs are gone: the buffers go back to the pool, the events complete,
    // and the pool's own counters describe what it had to keep resident.
    for (poolkit::DevicePtr& handle : handles) {
        sink.release(pool, std::move(handle));
    }
    handles.clear();
    buffers.clear();
    check(cudaStreamSynchronize(stream), "cudaStreamSynchronize");
    (void)pool.poll();
    const poolkit::PoolSnapshot snapshot = pool.snapshot();
    if (!sink.close(error)) {
        return fail(error);
    }

    qtoolkit::AdviseBlock advise;
    if (!qtoolkit::run_advisor(trace_path, advise_path, reference, advise, error)) {
        return fail(error);
    }

    report.weights.zero_point_ns = ablation.zero_point_ns;
    report.weights.warmup_runs = qtoolkit::kAblationWarmupRuns;
    report.weights.measure_runs = qtoolkit::kAblationMeasureRuns;
    report.weights.clamped = ablation.clamped;
    report.weights.median_ns = ablation.median_ns;
    report.weights.path = path.nodes;
    report.weights.path_ns = path.ns;

    report.pool.config = reference;
    report.pool.requests = sink.allocations();
    report.pool.bytes_requested = bytes_requested;
    report.pool.chunks = snapshot.chunk_count;
    report.pool.high_water_mark_bytes = snapshot.high_water_mark_bytes;
    report.pool.growth_count = snapshot.growth_count;
    report.pool.reuse_count = snapshot.reuse_count;
    report.pool.large_path_count = snapshot.large_path_count;
    report.pool.free_errors = snapshot.free_errors;
    report.pool.quarantined_blocks = snapshot.quarantined_blocks;
    report.advise = advise;

    if (!qtoolkit::write_text(report_path, qtoolkit::render_report(report), error)) {
        return fail(error);
    }

    std::printf("graph.json:          %zu nodes, depth %d, width %d, %zu of %zu edges redundant\n",
                report.clean.nodes, report.clean.depth, widest(report.clean.width),
                report.clean.redundant_edges, report.clean.edges);
    std::printf("graph.pingpong.json: %zu nodes, depth %d, width %d, %zu of %zu edges redundant\n",
                report.pingpong.nodes, report.pingpong.depth, widest(report.pingpong.width),
                report.pingpong.redundant_edges, report.pingpong.edges);
    std::printf("weights.json:        path_ns %llu over %zu nodes\n",
                static_cast<unsigned long long>(report.weights.path_ns), path.nodes.size());
    std::printf("trace.jsonl:         %zu requests, %zu chunks, high water mark %llu B\n",
                report.pool.requests, report.pool.chunks,
                static_cast<unsigned long long>(report.pool.high_water_mark_bytes));
    std::printf("advise.json:         %zu classes, peak sim %llu B, growth %.2f, chunk %llu B\n",
                report.advise.classes,
                static_cast<unsigned long long>(report.advise.peak_sim_bytes),
                report.advise.config.growth,
                static_cast<unsigned long long>(report.advise.config.chunk_bytes));
    return 0;
}
