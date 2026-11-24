// qtoolkit_verify: the six checks, on the committed
// artifacts, on a machine with no GPU.
//
// Exit 0: every number in report.json was reconstructed. Exit 1: a comparison
// failed, and the line names it. Exit 2: cannot decide — an artifact is missing
// or unreadable, or the pins of the report disagree with cmake/pins.cmake.
//
// A proven mismatch wins over an undecidable check: "I can show this is wrong"
// is a stronger answer than "I could not look".

#include "json.hpp"
#include "json_fields.hpp"

#include <graphlint/analysis.hpp>
#include <graphlint/json.hpp>
#include <graphlint/weights.hpp>

#include <poolkit/pool.hpp>
#include <poolkit/sim_backend.hpp>
#include <poolkit/trace.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#ifndef QTOOLKIT_PINS_FILE
#  error "QTOOLKIT_PINS_FILE must point at cmake/pins.cmake"
#endif

namespace {

namespace fs = std::filesystem;
using qtoolkit::JsonValue;

constexpr int kOk = 0;
constexpr int kMismatch = 1;
constexpr int kUndecidable = 2;

struct Verdict {
    int code = kOk;
    std::string name;
    std::string detail;
};

class Suite {
public:
    void add(int code, std::string name, std::string detail) {
        verdicts_.push_back(Verdict{code, std::move(name), std::move(detail)});
    }

    [[nodiscard]] int exit_code() const {
        int undecidable = kOk;
        for (const Verdict& verdict : verdicts_) {
            if (verdict.code == kMismatch) {
                return kMismatch;
            }
            if (verdict.code == kUndecidable) {
                undecidable = kUndecidable;
            }
        }
        return undecidable;
    }

    void print() const {
        for (const Verdict& verdict : verdicts_) {
            const char* token = verdict.code == kOk ? "OK  " : verdict.code == kMismatch ? "FAIL"
                                                                                          : "?   ";
            std::printf("%s %-28s %s\n", token, verdict.name.c_str(), verdict.detail.c_str());
        }
    }

private:
    std::vector<Verdict> verdicts_;
};

bool read_file(const fs::path& path, std::string& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path.string() + ": cannot open the file for reading";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (!file.eof() && file.fail()) {
        error = path.string() + ": short read";
        return false;
    }
    return true;
}

std::string text(long long value) {
    return std::to_string(value);
}

struct GraphNumbers {
    std::uint64_t nodes = 0;
    std::uint64_t edges = 0;
    std::uint64_t distinct_edges = 0;
    std::uint64_t minimal_edges = 0;
    std::uint64_t redundant_edges = 0;
    std::uint64_t depth = 0;
    std::vector<std::uint64_t> width;
};

bool read_int_array(const JsonValue& parent, std::string_view name, const std::string& context,
                    std::vector<std::uint64_t>& out, std::string& error) {
    const JsonValue* array = nullptr;
    if (!qtoolkit::json_array(parent, name, context, array, error)) {
        return false;
    }
    out.clear();
    for (const JsonValue& item : array->items) {
        if (item.kind != JsonValue::Kind::number || !item.integral || item.number < 0.0 ||
            item.number > static_cast<double>(qtoolkit::kMaxExactInteger)) {
            error = context + "." + std::string(name) +
                    ": an element is not an exact non-negative integer";
            return false;
        }
        out.push_back(static_cast<std::uint64_t>(item.number));
    }
    return true;
}

bool read_graph_numbers(const JsonValue& block, const std::string& context, GraphNumbers& out,
                        std::string& error) {
    return qtoolkit::json_unsigned(block, "nodes", context, out.nodes, error) &&
           qtoolkit::json_unsigned(block, "edges", context, out.edges, error) &&
           qtoolkit::json_unsigned(block, "distinct_edges", context, out.distinct_edges, error) &&
           qtoolkit::json_unsigned(block, "depth", context, out.depth, error) &&
           read_int_array(block, "width", context, out.width, error) &&
           qtoolkit::json_unsigned(block, "minimal_edges", context, out.minimal_edges, error) &&
           qtoolkit::json_unsigned(block, "redundant_edges", context, out.redundant_edges, error);
}

// The first difference between the report's block and the artifact, or an empty
// string when they agree. Field by field: a report that is right on depth and
// wrong on width has to say which one.
std::string graph_difference(const GraphNumbers& reported, const graphlint::Graph& model,
                             const graphlint::Topology& topology) {
    if (reported.nodes != model.nodes.size()) {
        return "nodes: report " + text(static_cast<long long>(reported.nodes)) + ", artifact " +
               text(static_cast<long long>(model.nodes.size()));
    }
    if (reported.edges != model.edges.size()) {
        return "edges: report " + text(static_cast<long long>(reported.edges)) + ", artifact " +
               text(static_cast<long long>(model.edges.size()));
    }
    if (reported.distinct_edges != static_cast<std::uint64_t>(topology.distinct_edges)) {
        return "distinct_edges: report " + text(static_cast<long long>(reported.distinct_edges)) +
               ", artifact " + text(topology.distinct_edges);
    }
    if (reported.depth != static_cast<std::uint64_t>(topology.depth)) {
        return "depth: report " + text(static_cast<long long>(reported.depth)) + ", artifact " +
               text(topology.depth);
    }
    if (reported.width.size() != topology.width.size()) {
        return "width: report has " + text(static_cast<long long>(reported.width.size())) +
               " levels, artifact " + text(static_cast<long long>(topology.width.size()));
    }
    for (std::size_t level = 0; level < topology.width.size(); ++level) {
        if (reported.width[level] != static_cast<std::uint64_t>(topology.width[level])) {
            return "width[" + text(static_cast<long long>(level)) + "]: report " +
                   text(static_cast<long long>(reported.width[level])) + ", artifact " +
                   text(topology.width[level]);
        }
    }
    if (reported.minimal_edges != topology.edges_minimal.size()) {
        return "minimal_edges: report " + text(static_cast<long long>(reported.minimal_edges)) +
               ", artifact " + text(static_cast<long long>(topology.edges_minimal.size()));
    }
    if (reported.redundant_edges != topology.edges_redundant.size()) {
        return "redundant_edges: report " +
               text(static_cast<long long>(reported.redundant_edges)) + ", artifact " +
               text(static_cast<long long>(topology.edges_redundant.size()));
    }
    return {};
}

bool pin_value(const std::string& document, std::string_view name, std::string& out) {
    const std::string marker = "set(QTOOLKIT_PIN_" + std::string(name) + " \"";
    const std::size_t start = document.find(marker);
    if (start == std::string::npos) {
        return false;
    }
    const std::size_t value_start = start + marker.size();
    const std::size_t end = document.find('"', value_start);
    if (end == std::string::npos) {
        return false;
    }
    out = document.substr(value_start, end - value_start);
    return true;
}

// The pins file: the one the build compiled in, or the copy the release archive
// ships beside the binary. An archive has no build tree, and a check 5 that
// cannot look answers "undecidable" on every archived run — a verifier that only
// works inside the checkout it was built from is not the tool the archive
// promises.
std::string environment_variable(const char* name) {
#ifdef _MSC_VER
    // getenv is deprecated under MSVC's /W4 /WX; _dupenv_s is the checked form.
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return {};
    }
    const std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

std::vector<fs::path> pins_candidates(const fs::path& artifacts, const fs::path& exe_dir) {
    std::vector<fs::path> candidates;
    const std::string from_environment = environment_variable("QTOOLKIT_PINS_FILE");
    if (!from_environment.empty()) {
        candidates.emplace_back(from_environment);
    }
    if (!exe_dir.empty()) {
        candidates.push_back(exe_dir / "pins.cmake");
    }
    candidates.emplace_back(QTOOLKIT_PINS_FILE);
    if (!artifacts.parent_path().empty()) {
        candidates.push_back(artifacts.parent_path() / "cmake" / "pins.cmake");
    }
    candidates.emplace_back("cmake/pins.cmake");
    return candidates;
}

fs::path executable_directory(const char* argv0) {
    std::error_code ignored;
    const fs::path path = argv0 == nullptr ? fs::path() : fs::path(argv0);
    if (path.has_parent_path()) {
        const fs::path absolute = fs::absolute(path, ignored);
        if (!ignored) {
            return absolute.parent_path();
        }
    }
    return fs::current_path(ignored);
}

// --- the five checks ------------------------------------------------------

// check 1: both graph artifacts, through the same reader the linter uses.
void check_graphs(const fs::path& artifacts, const JsonValue& report, Suite& suite) {
    const std::pair<const char*, const char*> shapes[] = {
        {"graph.json", "clean"},
        {"graph.pingpong.json", "pingpong"},
    };
    for (const auto& shape : shapes) {
        const std::string name = shape.first;
        std::string error;
        std::string document;
        if (!read_file(artifacts / shape.first, document, error)) {
            suite.add(kUndecidable, name, error);
            continue;
        }
        graphlint::Graph model;
        graphlint::GraphError graph_error;
        if (!graphlint::parse_graph(document, model, graph_error)) {
            suite.add(kUndecidable, name, std::string(shape.first) + ": " + graph_error.detail);
            continue;
        }
        graphlint::Topology topology;
        if (!graphlint::topology(model, topology, graph_error)) {
            suite.add(kUndecidable, name, std::string(shape.first) + ": " + graph_error.detail);
            continue;
        }

        const JsonValue* block = nullptr;
        const JsonValue* graph = qtoolkit::member(report, "graph");
        if (graph == nullptr ||
            !qtoolkit::json_object(*graph, shape.second,
                                   std::string("report.graph.") + shape.second, block, error)) {
            suite.add(kMismatch, name, "report: " + error);
            continue;
        }
        GraphNumbers numbers;
        if (!read_graph_numbers(*block, std::string("report.graph.") + shape.second, numbers,
                                error)) {
            suite.add(kMismatch, name, error);
            continue;
        }
        const std::string difference = graph_difference(numbers, model, topology);
        if (!difference.empty()) {
            suite.add(kMismatch, name, std::string(shape.first) + ": " + difference);
            continue;
        }
        suite.add(kOk, name,
                  text(static_cast<long long>(model.nodes.size())) + " nodes, depth " +
                      text(topology.depth) + ", width " +
                      text(topology.width.empty() ? 0
                                                  : *std::max_element(topology.width.begin(),
                                                                      topology.width.end())) +
                      ", " + text(static_cast<long long>(topology.edges_redundant.size())) +
                      " of " + text(static_cast<long long>(model.edges.size())) +
                      " edges redundant");
    }
}

// check 2: the table is bound to the graph and the weighted path follows.
void check_weights(const fs::path& artifacts, const JsonValue& report, Suite& suite) {
    std::string error;
    std::string graph_document;
    std::string weights_document;
    if (!read_file(artifacts / "graph.json", graph_document, error) ||
        !read_file(artifacts / "weights.json", weights_document, error)) {
        suite.add(kUndecidable, "weights.json", error);
        return;
    }

    graphlint::Graph model;
    graphlint::GraphError graph_error;
    if (!graphlint::parse_graph(graph_document, model, graph_error)) {
        suite.add(kUndecidable, "weights.json", "graph.json: " + graph_error.detail);
        return;
    }
    graphlint::WeightsTable table;
    if (!graphlint::parse_weights(weights_document, table, graph_error)) {
        suite.add(kUndecidable, "weights.json", "weights.json: " + graph_error.detail);
        return;
    }
    if (!graphlint::apply_weights(model, table, graph_error)) {
        suite.add(kMismatch, "weights.json",
                  "the table is not bound to this graph: " + graph_error.detail);
        return;
    }
    graphlint::Topology topology;
    if (!graphlint::topology(model, topology, graph_error)) {
        suite.add(kUndecidable, "weights.json", "topology: " + graph_error.detail);
        return;
    }
    graphlint::CriticalPath path;
    if (!graphlint::critical_path(model, topology, path, graph_error)) {
        suite.add(kUndecidable, "weights.json", "critical_path: " + graph_error.detail);
        return;
    }

    const JsonValue* weights = qtoolkit::member(report, "weights");
    if (weights == nullptr) {
        suite.add(kMismatch, "weights.json", "report: missing block 'weights'");
        return;
    }
    std::uint64_t reported_path_ns = 0;
    if (!qtoolkit::json_unsigned(*weights, "path_ns", "report.weights", reported_path_ns, error)) {
        suite.add(kMismatch, "weights.json", error);
        return;
    }
    if (reported_path_ns != path.weight_sum) {
        suite.add(kMismatch, "weights.json",
                  "path_ns: report " + text(static_cast<long long>(reported_path_ns)) +
                      ", recomputed " + text(static_cast<long long>(path.weight_sum)));
        return;
    }

    std::vector<std::uint64_t> reported_path;
    if (!read_int_array(*weights, "path", "report.weights", reported_path, error)) {
        suite.add(kMismatch, "weights.json", error);
        return;
    }
    if (reported_path.size() != path.nodes.size()) {
        suite.add(kMismatch, "weights.json", "path: the report names " +
                                                 text(static_cast<long long>(reported_path.size())) +
                                                 " nodes, recomputed " +
                                                 text(static_cast<long long>(path.nodes.size())));
        return;
    }
    for (std::size_t i = 0; i < path.nodes.size(); ++i) {
        if (reported_path[i] != static_cast<std::uint64_t>(path.nodes[i])) {
            suite.add(kMismatch, "weights.json", "path[" + text(static_cast<long long>(i)) +
                                                     "]: report " +
                                                     text(static_cast<long long>(reported_path[i])) +
                                                     ", recomputed " + text(path.nodes[i]));
            return;
        }
    }
    suite.add(kOk, "weights.json",
              text(static_cast<long long>(table.ns.size())) + " rows, path_ns " +
                  text(static_cast<long long>(path.weight_sum)));
}

// check 4: the arithmetic the ablation promises, node by node.
void check_arithmetic(const fs::path& artifacts, const JsonValue& report, Suite& suite) {
    std::string error;
    const JsonValue* weights = qtoolkit::member(report, "weights");
    if (weights == nullptr) {
        suite.add(kMismatch, "weights arithmetic", "report: missing block 'weights'");
        return;
    }
    std::string weights_document;
    if (!read_file(artifacts / "weights.json", weights_document, error)) {
        suite.add(kUndecidable, "weights arithmetic", error);
        return;
    }

    std::vector<std::uint64_t> medians;
    std::uint64_t zero_point = 0;
    std::uint64_t clamped = 0;
    if (!read_int_array(*weights, "median_ns", "report.weights", medians, error) ||
        !qtoolkit::json_unsigned(*weights, "zero_point_ns", "report.weights", zero_point, error) ||
        !qtoolkit::json_unsigned(*weights, "clamped", "report.weights", clamped, error)) {
        suite.add(kMismatch, "weights arithmetic", error);
        return;
    }

    graphlint::WeightsTable table;
    graphlint::GraphError graph_error;
    if (!graphlint::parse_weights(weights_document, table, graph_error)) {
        suite.add(kUndecidable, "weights arithmetic", graph_error.detail);
        return;
    }
    if (medians.size() != table.ns.size()) {
        suite.add(kMismatch, "weights arithmetic",
                  "the report has " + text(static_cast<long long>(medians.size())) +
                      " medians for " + text(static_cast<long long>(table.ns.size())) + " rows");
        return;
    }

    std::uint64_t zeros = 0;
    for (std::size_t id = 0; id < medians.size(); ++id) {
        const std::int64_t difference =
            static_cast<std::int64_t>(medians[id]) - static_cast<std::int64_t>(zero_point);
        const std::uint64_t expected =
            difference > 0 ? static_cast<std::uint64_t>(difference) : 0;
        if (table.ns[id] != expected) {
            suite.add(kMismatch, "weights arithmetic",
                      "id " + text(static_cast<long long>(id)) + ": weights.json says " +
                          text(static_cast<long long>(table.ns[id])) + ", max(0, " +
                          text(static_cast<long long>(medians[id])) + " - " +
                          text(static_cast<long long>(zero_point)) + ") is " +
                          text(static_cast<long long>(expected)));
            return;
        }
        if (expected == 0) {
            ++zeros;
        }
    }
    if (zeros != clamped) {
        suite.add(kMismatch, "weights arithmetic",
                  "clamped: report " + text(static_cast<long long>(clamped)) +
                      ", the table has " + text(static_cast<long long>(zeros)) + " zeros");
        return;
    }

    // The sum is the other half of the same table: path_ns says how long the
    // critical path is, sum_ns says how much work there is to overlap.
    std::uint64_t total = 0;
    for (const std::uint64_t weight : table.ns) {
        total += weight;
    }
    std::uint64_t reported_sum = 0;
    if (!qtoolkit::json_unsigned(*weights, "sum_ns", "report.weights", reported_sum, error)) {
        suite.add(kMismatch, "weights arithmetic", error);
        return;
    }
    if (reported_sum != total) {
        suite.add(kMismatch, "weights arithmetic",
                  "sum_ns: report " + text(static_cast<long long>(reported_sum)) +
                      ", the table adds up to " + text(static_cast<long long>(total)));
        return;
    }
    suite.add(kOk, "weights arithmetic",
              text(static_cast<long long>(table.ns.size())) + " nodes, " +
                  text(static_cast<long long>(zeros)) + " clamped, sum " +
                  text(static_cast<long long>(total)) + " ns");
}

// check 3: the trace replayed on a CPU with the table the advisor recommended.
void check_trace(const fs::path& artifacts, const JsonValue& report, Suite& suite) {
    std::string error;
    const JsonValue* advise = qtoolkit::member(report, "advise");
    const JsonValue* config = nullptr;
    if (advise == nullptr) {
        suite.add(kMismatch, "trace.jsonl", "report: missing block 'advise'");
        return;
    }
    if (!qtoolkit::json_object(*advise, "config", "report.advise", config, error)) {
        suite.add(kMismatch, "trace.jsonl", error);
        return;
    }

    poolkit::Pool::Config pool_config;
    std::uint64_t chunk = 0;
    std::uint64_t min_class = 0;
    std::uint64_t max_class = 0;
    const JsonValue* extra = nullptr;
    if (!qtoolkit::json_number(*config, "growth", "report.advise.config", pool_config.growth,
                               error) ||
        !qtoolkit::json_unsigned(*config, "chunk_bytes", "report.advise.config", chunk, error) ||
        !qtoolkit::json_unsigned(*config, "min_class_bytes", "report.advise.config", min_class,
                                 error) ||
        !qtoolkit::json_unsigned(*config, "max_class_bytes", "report.advise.config", max_class,
                                 error) ||
        !qtoolkit::json_boolean(*config, "power_of_two", "report.advise.config",
                                pool_config.include_power_of_two, error) ||
        !qtoolkit::json_array(*config, "extra_sizes", "report.advise.config", extra, error)) {
        suite.add(kMismatch, "trace.jsonl", error);
        return;
    }
    pool_config.chunk_bytes = static_cast<std::size_t>(chunk);
    pool_config.min_class_bytes = static_cast<std::size_t>(min_class);
    pool_config.max_class_bytes = static_cast<std::size_t>(max_class);
    for (std::size_t i = 0; i < extra->items.size(); ++i) {
        std::uint64_t size = 0;
        if (!qtoolkit::json_element(*extra, i, "report.advise.config.extra_sizes", size, error)) {
            suite.add(kMismatch, "trace.jsonl", error);
            return;
        }
        pool_config.extra_sizes.push_back(static_cast<std::size_t>(size));
    }

    std::uint64_t reported_ops = 0;
    std::uint64_t reported_allocs = 0;
    std::uint64_t reported_peak = 0;
    std::uint64_t reported_blocks = 0;
    if (!qtoolkit::json_unsigned(*advise, "ops", "report.advise", reported_ops, error) ||
        !qtoolkit::json_unsigned(*advise, "allocs", "report.advise", reported_allocs, error) ||
        !qtoolkit::json_unsigned(*advise, "peak_sim_bytes", "report.advise", reported_peak,
                                 error) ||
        !qtoolkit::json_unsigned(*advise, "peak_blocks", "report.advise", reported_blocks, error)) {
        suite.add(kMismatch, "trace.jsonl", error);
        return;
    }

    poolkit::Trace trace;
    try {
        trace = poolkit::parse_trace_file((artifacts / "trace.jsonl").string());
    } catch (const std::exception& caught) {
        suite.add(kUndecidable, "trace.jsonl", caught.what());
        return;
    }

    poolkit::SimBackend backend;
    poolkit::Pool pool(backend, pool_config);
    poolkit::TraceReplayResult replay;
    try {
        replay = poolkit::replay_trace(pool, trace);
    } catch (const std::exception& caught) {
        suite.add(kUndecidable, "trace.jsonl", caught.what());
        return;
    }

    const std::uint64_t ops = replay.alloc_count + replay.free_count;
    if (ops != reported_ops) {
        suite.add(kMismatch, "trace.jsonl", "ops: report " +
                                                text(static_cast<long long>(reported_ops)) +
                                                ", replay " +
                                                text(static_cast<long long>(ops)));
        return;
    }
    if (replay.alloc_count != reported_allocs) {
        suite.add(kMismatch, "trace.jsonl", "allocs: report " +
                                                text(static_cast<long long>(reported_allocs)) +
                                                ", replay " +
                                                text(static_cast<long long>(replay.alloc_count)));
        return;
    }
    if (replay.peak_bytes != reported_peak) {
        suite.add(kMismatch, "trace.jsonl", "peak_sim_bytes: report " +
                                                text(static_cast<long long>(reported_peak)) +
                                                ", replay " +
                                                text(static_cast<long long>(replay.peak_bytes)));
        return;
    }
    if (replay.peak_live_blocks != reported_blocks) {
        suite.add(kMismatch, "trace.jsonl", "peak_blocks: report " +
                                                text(static_cast<long long>(reported_blocks)) +
                                                ", replay " +
                                                text(static_cast<long long>(
                                                    replay.peak_live_blocks)));
        return;
    }
    suite.add(kOk, "trace.jsonl",
              text(static_cast<long long>(ops)) + " ops, " +
                  text(static_cast<long long>(replay.alloc_count)) + " allocs, peak " +
                  text(static_cast<long long>(replay.peak_bytes)) + " B");
}

// check 6: the shape's price. The two numbers are measurements of one card and
// no file reproduces them; what a reader without the card can hold is that both
// exist, that they were taken with a declared protocol, and that each delta is
// the subtraction of the pair beside it. A sign is not checked: a pingpong that
// came out cheaper is a measurement, not a schema error.
void check_shape_cost(const JsonValue& report, Suite& suite) {
    std::string error;
    const JsonValue* block = qtoolkit::member(report, "shape_cost");
    if (block == nullptr) {
        suite.add(kMismatch, "shape cost", "report: missing block 'shape_cost'");
        return;
    }
    const JsonValue* launch = nullptr;
    const JsonValue* instantiate = nullptr;
    if (!qtoolkit::json_object(*block, "launch", "report.shape_cost", launch, error) ||
        !qtoolkit::json_object(*block, "instantiate", "report.shape_cost", instantiate, error)) {
        suite.add(kMismatch, "shape cost", error);
        return;
    }

    std::string unit;
    if (!qtoolkit::json_string(*block, "unit", "report.shape_cost", unit, error)) {
        suite.add(kMismatch, "shape cost", error);
        return;
    }
    if (unit != "ns") {
        suite.add(kMismatch, "shape cost", "unit is \"" + unit + "\", the schema says \"ns\"");
        return;
    }

    // The launch is the ablation's clock on the ablation's protocol: two blocks
    // in one document that time the same kind of thing with different run
    // counts would make the medians incomparable without saying so.
    const JsonValue* weights = qtoolkit::member(report, "weights");
    std::uint64_t ablation_warmup = 0;
    std::uint64_t ablation_measure = 0;
    if (weights == nullptr ||
        !qtoolkit::json_unsigned(*weights, "warmup_runs", "report.weights", ablation_warmup,
                                 error) ||
        !qtoolkit::json_unsigned(*weights, "measure_runs", "report.weights", ablation_measure,
                                 error)) {
        suite.add(kMismatch, "shape cost", "report.weights: " + error);
        return;
    }
    std::uint64_t launch_warmup = 0;
    std::uint64_t launch_measure = 0;
    if (!qtoolkit::json_unsigned(*launch, "warmup_runs", "report.shape_cost.launch", launch_warmup,
                                 error) ||
        !qtoolkit::json_unsigned(*launch, "measure_runs", "report.shape_cost.launch",
                                 launch_measure, error)) {
        suite.add(kMismatch, "shape cost", error);
        return;
    }
    if (launch_warmup != ablation_warmup || launch_measure != ablation_measure) {
        suite.add(kMismatch, "shape cost",
                  "launch protocol " + text(static_cast<long long>(launch_warmup)) + "+" +
                      text(static_cast<long long>(launch_measure)) +
                      " runs, the ablation measured " +
                      text(static_cast<long long>(ablation_warmup)) + "+" +
                      text(static_cast<long long>(ablation_measure)));
        return;
    }

    std::string launch_clock;
    std::string instantiate_clock;
    if (!qtoolkit::json_string(*launch, "clock", "report.shape_cost.launch", launch_clock,
                               error) ||
        !qtoolkit::json_string(*instantiate, "clock", "report.shape_cost.instantiate",
                               instantiate_clock, error)) {
        suite.add(kMismatch, "shape cost", error);
        return;
    }
    if (launch_clock != "cuda-events" || instantiate_clock != "steady_clock") {
        suite.add(kMismatch, "shape cost",
                  "clocks: launch \"" + launch_clock + "\", instantiate \"" + instantiate_clock +
                      "\"; a host call timed with events measures the empty stream");
        return;
    }

    std::int64_t launch_delta = 0;
    std::int64_t launch_clean = 0;
    std::int64_t launch_pingpong = 0;
    std::int64_t instantiate_delta = 0;
    const std::pair<const char*, const JsonValue*> pairs[] = {
        {"launch", launch},
        {"instantiate", instantiate},
    };
    for (const auto& pair : pairs) {
        const std::string context = std::string("report.shape_cost.") + pair.first;
        std::int64_t clean = 0;
        std::int64_t pingpong = 0;
        std::int64_t delta = 0;
        if (!qtoolkit::json_signed(*pair.second, "clean_median_ns", context, clean, error) ||
            !qtoolkit::json_signed(*pair.second, "pingpong_median_ns", context, pingpong, error) ||
            !qtoolkit::json_signed(*pair.second, "delta_median_ns", context, delta, error)) {
            suite.add(kMismatch, "shape cost", error);
            return;
        }
        if (clean <= 0 || pingpong <= 0) {
            suite.add(kMismatch, "shape cost",
                      context + ": clean " + text(clean) + " ns, pingpong " + text(pingpong) +
                          " ns — a zero here is a broken measurement, not a fast shape");
            return;
        }
        if (pingpong - clean != delta) {
            suite.add(kMismatch, "shape cost",
                      context + ".delta_median_ns: report " + text(delta) +
                          ", pingpong - clean is " + text(pingpong - clean));
            return;
        }
        if (std::string(pair.first) == "launch") {
            launch_delta = delta;
            launch_clean = clean;
            launch_pingpong = pingpong;
        } else {
            instantiate_delta = delta;
        }
    }

    // The spread is part of the number: a median outside its own [min, max] is a
    // transcription error, and a reader who cannot see the spread cannot tell a
    // two-microsecond delta from noise.
    std::int64_t clean_min = 0;
    std::int64_t clean_max = 0;
    std::int64_t pingpong_min = 0;
    std::int64_t pingpong_max = 0;
    if (!qtoolkit::json_signed(*launch, "clean_min_ns", "report.shape_cost.launch", clean_min,
                               error) ||
        !qtoolkit::json_signed(*launch, "clean_max_ns", "report.shape_cost.launch", clean_max,
                               error) ||
        !qtoolkit::json_signed(*launch, "pingpong_min_ns", "report.shape_cost.launch",
                               pingpong_min, error) ||
        !qtoolkit::json_signed(*launch, "pingpong_max_ns", "report.shape_cost.launch",
                               pingpong_max, error)) {
        suite.add(kMismatch, "shape cost", error);
        return;
    }
    if (clean_min > launch_clean || launch_clean > clean_max || pingpong_min > launch_pingpong ||
        launch_pingpong > pingpong_max) {
        suite.add(kMismatch, "shape cost",
                  "the launch median is outside its own range: clean " + text(launch_clean) +
                      " ns in [" + text(clean_min) + ", " + text(clean_max) + "], pingpong " +
                      text(launch_pingpong) + " ns in [" + text(pingpong_min) + ", " +
                      text(pingpong_max) + "]");
        return;
    }

    suite.add(kOk, "shape cost",
              "pingpong +" + text(launch_delta) + " ns per launch (clean " +
                  text(launch_clean) + " ns in [" + text(clean_min) + ", " + text(clean_max) +
                  "]), +" + text(instantiate_delta) + " ns to instantiate");
}

// check 5: the pins, in one file and in the report.
void check_pins(const fs::path& artifacts, const fs::path& exe_dir, const JsonValue& report,
                Suite& suite) {
    std::string document;
    std::string error;
    fs::path chosen;
    for (const fs::path& candidate : pins_candidates(artifacts, exe_dir)) {
        std::error_code ignored;
        if (!fs::is_regular_file(candidate, ignored)) {
            continue;
        }
        if (read_file(candidate, document, error)) {
            chosen = candidate;
            break;
        }
    }
    if (chosen.empty()) {
        suite.add(kUndecidable, "pins",
                  "no pins file: set QTOOLKIT_PINS_FILE or run beside cmake/pins.cmake");
        return;
    }

    const JsonValue* pins = qtoolkit::member(report, "pins");
    if (pins == nullptr) {
        suite.add(kUndecidable, "pins", "report: missing block 'pins'");
        return;
    }

    const std::pair<const char*, const char*> names[] = {
        {"CUDA_TIMER", "cuda_timer"},
        {"POOLKIT", "poolkit"},
        {"ULPGRAD", "ulpgrad"},
        {"GRAPH_LINT", "graph_lint"},
    };
    for (const auto& name : names) {
        std::string from_file;
        std::string from_report;
        if (!pin_value(document, name.first, from_file)) {
            suite.add(kUndecidable, "pins",
                      std::string("cmake/pins.cmake has no QTOOLKIT_PIN_") + name.first);
            return;
        }
        if (!qtoolkit::json_string(*pins, name.second, "report.pins", from_report, error)) {
            suite.add(kUndecidable, "pins", error);
            return;
        }
        if (from_file != from_report) {
            suite.add(kUndecidable, "pins", std::string("report.pins.") + name.second + " is " +
                                                from_report + ", cmake/pins.cmake says " +
                                                from_file + " (cannot decide)");
            return;
        }
    }
    suite.add(kOk, "pins", "4 pins match " + chosen.generic_string());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "usage: qtoolkit_verify <report.json> [<artifacts-dir>]\n");
        return 2;
    }

    const fs::path report_path = argv[1];
    std::string document;
    std::string error;
    if (!read_file(report_path, document, error)) {
        std::fprintf(stderr, "qtoolkit_verify: %s\n", error.c_str());
        return 2;
    }

    JsonValue report;
    if (!qtoolkit::parse_json(document, report, error)) {
        std::fprintf(stderr, "qtoolkit_verify: %s: %s\n", report_path.string().c_str(),
                     error.c_str());
        return 2;
    }
    if (report.kind != JsonValue::Kind::object) {
        std::fprintf(stderr, "qtoolkit_verify: %s: the report is not an object\n",
                     report_path.string().c_str());
        return 2;
    }

    std::uint64_t schema = 0;
    if (!qtoolkit::json_unsigned(report, "schema_version", "report", schema, error)) {
        std::fprintf(stderr, "qtoolkit_verify: %s\n", error.c_str());
        return 2;
    }
    if (schema != 2) {
        std::fprintf(stderr, "qtoolkit_verify: schema %s, this verifier reads 2\n",
                     text(static_cast<long long>(schema)).c_str());
        return 2;
    }

    // The artifacts directory defaults to the report's: a checkout verifies
    // itself. An explicit one lets a test point at a report copied elsewhere
    // without copying the five artifacts beside it.
    const fs::path artifacts =
        argc == 3 ? fs::path(argv[2])
                  : (report_path.has_parent_path() ? report_path.parent_path() : fs::path("."));
    Suite suite;
    check_pins(artifacts, executable_directory(argc > 0 ? argv[0] : nullptr), report, suite);
    check_graphs(artifacts, report, suite);
    check_weights(artifacts, report, suite);
    check_arithmetic(artifacts, report, suite);
    check_shape_cost(report, suite);
    check_trace(artifacts, report, suite);

    suite.print();
    return suite.exit_code();
}
