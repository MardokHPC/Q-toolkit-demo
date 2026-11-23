#pragma once

// The report's schema and its only writer. The JSON core (writer and strict
// reader) lives in json.hpp; this file decides what a report contains and in
// which order, so a committed artifact changes only when a block changes.
//
// Every block is derived from artifacts and never from the run: the graph block
// from graph.json, the weights block from graph.json + weights.json, the pool
// block from the pool's own counters, the advise block from what poolkit_advise
// wrote. The verifier will redo those derivations; nothing here is typed by
// hand and nothing is quoted from a terminal.

#include "json.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qtoolkit {

inline constexpr int kReportSchemaVersion = 1;

struct FindingRecord {
    std::string rule;
    std::string severity;
    std::vector<int> nodes;
    std::string message;
};

struct GraphBlock {
    std::size_t nodes = 0;
    std::size_t edges = 0;
    std::size_t distinct_edges = 0;
    std::size_t minimal_edges = 0;
    std::size_t redundant_edges = 0;
    int depth = 0;
    std::vector<int> width;
    std::vector<FindingRecord> findings;
};

struct Environment {
    std::string device;
    int compute_major = 0;
    int compute_minor = 0;
    std::string cuda_runtime;
};

// The weights table owns the per-node numbers; this block owns what the table
// cannot say: the floor it was subtracted from, the protocol, and how many nodes
// fell under the timer's resolution. path_ns is a conclusion the verifier
// recomputes from graph.json and weights.json (check 2).
struct WeightBlock {
    std::int64_t zero_point_ns = 0;
    int warmup_runs = 0;
    int measure_runs = 0;
    std::size_t clamped = 0;
    std::vector<std::int64_t> median_ns;  // by canonical id, zero point included
    std::vector<int> path;
    std::uint64_t path_ns = 0;
};

struct PoolConfigRecord {
    std::uint64_t chunk_bytes = 0;
    std::uint64_t min_class_bytes = 0;
    std::uint64_t max_class_bytes = 0;
    double growth = 0.0;
    bool power_of_two = false;
};

// What the pool did on this run, with its own configuration beside it: a
// high-water mark without the table that produced it is a number without units.
// The name is deliberately not the advisor's `peak_sim_bytes`.
struct PoolBlock {
    PoolConfigRecord config;
    std::size_t requests = 0;
    std::uint64_t bytes_requested = 0;
    std::size_t chunks = 0;
    std::uint64_t high_water_mark_bytes = 0;
    std::size_t growth_count = 0;
    std::size_t reuse_count = 0;
    std::size_t large_path_count = 0;
    std::size_t free_errors = 0;
    std::size_t quarantined_blocks = 0;
};

struct AdviseConfig {
    double growth = 0.0;
    std::uint64_t chunk_bytes = 0;
    std::uint64_t min_class_bytes = 0;
    std::uint64_t max_class_bytes = 0;
    bool power_of_two = false;
    std::vector<std::uint64_t> extra_sizes;
};

struct AdviseBlock {
    AdviseConfig config;
    std::size_t classes = 0;
    std::size_t ops = 0;
    std::size_t allocs = 0;
    double waste_internal = 0.0;
    double waste_tail = 0.0;
    std::uint64_t peak_sim_bytes = 0;
    std::size_t peak_blocks = 0;
    std::uint64_t retention_bound_bytes = 0;
};

struct Pins {
    std::string cuda_timer;
    std::string poolkit;
    std::string ulpgrad;
    std::string graph_lint;
};

struct Report {
    std::string version;
    Pins pins;
    Environment environment;
    GraphBlock clean;
    GraphBlock pingpong;
    WeightBlock weights;
    PoolBlock pool;
    AdviseBlock advise;
};

[[nodiscard]] inline std::string render_report(const Report& report) {
    const auto count = [](std::size_t value) { return static_cast<long long>(value); };
    const auto unsigned_value = [](std::uint64_t value) {
        return static_cast<long long>(value);
    };
    const auto write_graph = [count](JsonWriter& json, const GraphBlock& block) {
        json.begin_object();
        json.field("nodes");
        json.integer(count(block.nodes));
        json.field("edges");
        json.integer(count(block.edges));
        json.field("distinct_edges");
        json.integer(count(block.distinct_edges));
        json.field("depth");
        json.integer(block.depth);
        json.field("width");
        json.begin_array();
        for (const int level : block.width) {
            json.integer(level);
        }
        json.end_array();
        json.field("minimal_edges");
        json.integer(count(block.minimal_edges));
        json.field("redundant_edges");
        json.integer(count(block.redundant_edges));
        json.field("findings");
        json.begin_array();
        for (const FindingRecord& finding : block.findings) {
            json.begin_object();
            json.field("rule");
            json.text(finding.rule);
            json.field("severity");
            json.text(finding.severity);
            json.field("nodes");
            json.begin_array();
            for (const int node : finding.nodes) {
                json.integer(node);
            }
            json.end_array();
            json.field("message");
            json.text(finding.message);
            json.end_object();
        }
        json.end_array();
        json.end_object();
    };

    JsonWriter json;
    json.begin_object();
    json.field("schema_version");
    json.integer(kReportSchemaVersion);
    json.field("tool");
    json.text("qtoolkit-demo");
    json.field("version");
    json.text(report.version);
    json.field("pins");
    json.begin_object();
    json.field("cuda_timer");
    json.text(report.pins.cuda_timer);
    json.field("poolkit");
    json.text(report.pins.poolkit);
    json.field("ulpgrad");
    json.text(report.pins.ulpgrad);
    json.field("graph_lint");
    json.text(report.pins.graph_lint);
    json.end_object();
    json.field("environment");
    json.begin_object();
    json.field("device");
    json.text(report.environment.device);
    json.field("compute_major");
    json.integer(report.environment.compute_major);
    json.field("compute_minor");
    json.integer(report.environment.compute_minor);
    json.field("cuda_runtime");
    json.text(report.environment.cuda_runtime);
    json.end_object();
    json.field("graph");
    json.begin_object();
    json.field("clean");
    write_graph(json, report.clean);
    json.field("pingpong");
    write_graph(json, report.pingpong);
    json.end_object();
    json.field("weights");
    json.begin_object();
    json.field("unit");
    json.text("ns");
    json.field("zero_point_ns");
    json.integer(report.weights.zero_point_ns);
    json.field("warmup_runs");
    json.integer(report.weights.warmup_runs);
    json.field("measure_runs");
    json.integer(report.weights.measure_runs);
    json.field("clamped");
    json.integer(count(report.weights.clamped));
    json.field("median_ns");
    json.begin_array();
    for (const std::int64_t median : report.weights.median_ns) {
        json.integer(median);
    }
    json.end_array();
    json.field("path");
    json.begin_array();
    for (const int node : report.weights.path) {
        json.integer(node);
    }
    json.end_array();
    json.field("path_ns");
    json.integer(unsigned_value(report.weights.path_ns));
    json.end_object();

    const PoolConfigRecord& pool = report.pool.config;
    json.field("pool_measured");
    json.begin_object();
    json.field("config");
    json.begin_object();
    json.field("chunk_bytes");
    json.integer(unsigned_value(pool.chunk_bytes));
    json.field("min_class_bytes");
    json.integer(unsigned_value(pool.min_class_bytes));
    json.field("max_class_bytes");
    json.integer(unsigned_value(pool.max_class_bytes));
    json.field("growth");
    json.number(pool.growth);
    json.field("power_of_two");
    json.boolean(pool.power_of_two);
    json.end_object();
    json.field("requests");
    json.integer(count(report.pool.requests));
    json.field("bytes_requested");
    json.integer(unsigned_value(report.pool.bytes_requested));
    json.field("chunks");
    json.integer(count(report.pool.chunks));
    json.field("high_water_mark_bytes");
    json.integer(unsigned_value(report.pool.high_water_mark_bytes));
    json.field("growth_count");
    json.integer(count(report.pool.growth_count));
    json.field("reuse_count");
    json.integer(count(report.pool.reuse_count));
    json.field("large_path_count");
    json.integer(count(report.pool.large_path_count));
    json.field("free_errors");
    json.integer(count(report.pool.free_errors));
    json.field("quarantined_blocks");
    json.integer(count(report.pool.quarantined_blocks));
    json.end_object();

    const AdviseConfig& advise = report.advise.config;
    json.field("advise");
    json.begin_object();
    json.field("config");
    json.begin_object();
    json.field("growth");
    json.number(advise.growth);
    json.field("chunk_bytes");
    json.integer(unsigned_value(advise.chunk_bytes));
    json.field("min_class_bytes");
    json.integer(unsigned_value(advise.min_class_bytes));
    json.field("max_class_bytes");
    json.integer(unsigned_value(advise.max_class_bytes));
    json.field("power_of_two");
    json.boolean(advise.power_of_two);
    json.field("extra_sizes");
    json.begin_array();
    for (const std::uint64_t size : advise.extra_sizes) {
        json.integer(unsigned_value(size));
    }
    json.end_array();
    json.end_object();
    json.field("classes");
    json.integer(count(report.advise.classes));
    json.field("ops");
    json.integer(count(report.advise.ops));
    json.field("allocs");
    json.integer(count(report.advise.allocs));
    json.field("waste");
    json.begin_object();
    json.field("internal");
    json.number(report.advise.waste_internal);
    json.field("tail");
    json.number(report.advise.waste_tail);
    json.end_object();
    json.field("peak_sim_bytes");
    json.integer(unsigned_value(report.advise.peak_sim_bytes));
    json.field("peak_blocks");
    json.integer(count(report.advise.peak_blocks));
    json.field("retention_bound_bytes");
    json.integer(unsigned_value(report.advise.retention_bound_bytes));
    json.end_object();
    json.end_object();

    std::string document = json.str();
    document += '\n';
    return document;
}

}  // namespace qtoolkit
