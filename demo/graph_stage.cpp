#include "graph_stage.hpp"

#include "file_io.hpp"

#include <graphlint/analysis.hpp>
#include <graphlint/cuda_source.hpp>
#include <graphlint/json.hpp>
#include <graphlint/rules.hpp>
#include <graphlint/weights.hpp>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace qtoolkit {
namespace {

bool read_document(const std::string& path, std::string& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open the file for reading";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (!file.eof() && file.fail()) {
        error = path + ": short read";
        return false;
    }
    return true;
}

bool read_graph_model(const std::string& path, graphlint::Graph& model, std::string& error) {
    std::string document;
    if (!read_document(path, document, error)) {
        return false;
    }
    graphlint::GraphError graph_error;
    if (!graphlint::parse_graph(document, model, graph_error)) {
        error = path + ": " + graph_error.detail;
        return false;
    }
    return true;
}

}  // namespace

bool write_graph(const cudaGraph_t& graph, const std::string& path, std::string& error,
                 std::vector<cudaGraphNode_t>* handles_by_id) {
    if (handles_by_id != nullptr) {
        graphlint::Graph model;
        if (!graphlint::from_cuda(graph, graphlint::SourceKind::Capture, model, error,
                                  handles_by_id)) {
            return false;
        }
    }
    return graphlint::write_graph_json(graph, path, error);
}

bool analyze_graph(const std::string& path, GraphBlock& out, std::string& error) {
    error.clear();

    graphlint::Graph model;
    if (!read_graph_model(path, model, error)) {
        return false;
    }

    graphlint::GraphError graph_error;
    graphlint::Topology topology;
    if (!graphlint::topology(model, topology, graph_error)) {
        error = path + ": " + graph_error.detail;
        return false;
    }

    graphlint::LintResult lint;
    if (!graphlint::lint(model, topology, graph_error, lint)) {
        // lint refuses only what topology already refused; the graph is a DAG
        // here, so this is a library invariant and not a user error.
        error = path + ": the linter refused an analysed DAG";
        return false;
    }

    out = GraphBlock{};
    out.nodes = model.nodes.size();
    out.edges = model.edges.size();
    out.distinct_edges = static_cast<std::size_t>(topology.distinct_edges);
    out.minimal_edges = topology.edges_minimal.size();
    out.redundant_edges = topology.edges_redundant.size();
    out.depth = topology.depth;
    out.width = topology.width;
    out.findings.reserve(lint.findings.size());
    for (const graphlint::Finding& finding : lint.findings) {
        FindingRecord record;
        record.rule = graphlint::to_string(finding.rule);
        record.severity = graphlint::to_string(finding.severity);
        record.nodes = finding.nodes;
        record.message = finding.message;
        out.findings.push_back(std::move(record));
    }
    return true;
}

bool write_weights(const std::string& graph_path, const std::string& weights_path,
                   const std::vector<std::uint64_t>& ns, std::string& error) {
    error.clear();

    graphlint::Graph model;
    if (!read_graph_model(graph_path, model, error)) {
        return false;
    }
    if (ns.size() != model.nodes.size()) {
        error = weights_path + ": " + std::to_string(ns.size()) + " weights for " +
                std::to_string(model.nodes.size()) + " nodes";
        return false;
    }

    // docs/weights.md section 1: sorted keys, one row per id, integers, LF.
    char fingerprint[17] = {};
    std::snprintf(fingerprint, sizeof(fingerprint), "%016llx",
                  static_cast<unsigned long long>(graphlint::graph_fingerprint(model)));

    std::string document;
    document.reserve(ns.size() * 16 + 160);
    document += "{\n  \"graph\": \"";
    document += fingerprint;
    document += "\", \"node_count\": ";
    document += std::to_string(ns.size());
    document += ", \"schema_version\": 1, \"unit\": \"ns\",\n  \"weights\": [";
    for (std::size_t id = 0; id < ns.size(); ++id) {
        document += id == 0 ? "[" : ", [";
        document += std::to_string(id);
        document += ", ";
        document += std::to_string(ns[id]);
        document += ']';
    }
    document += "]\n}\n";
    return write_text(weights_path, document, error);
}

bool weighted_path(const std::string& graph_path, const std::string& weights_path,
                   WeightedPath& out, std::string& error) {
    error.clear();

    graphlint::Graph model;
    if (!read_graph_model(graph_path, model, error)) {
        return false;
    }

    std::string weights_document;
    if (!read_document(weights_path, weights_document, error)) {
        return false;
    }
    graphlint::GraphError graph_error;
    graphlint::WeightsTable table;
    if (!graphlint::parse_weights(weights_document, table, graph_error)) {
        error = weights_path + ": " + graph_error.detail;
        return false;
    }
    if (!graphlint::apply_weights(model, table, graph_error)) {
        error = weights_path + ": " + graph_error.detail;
        return false;
    }

    graphlint::Topology topology;
    if (!graphlint::topology(model, topology, graph_error)) {
        error = graph_path + ": " + graph_error.detail;
        return false;
    }
    graphlint::CriticalPath path;
    if (!graphlint::critical_path(model, topology, path, graph_error)) {
        error = graph_path + ": " + graph_error.detail;
        return false;
    }

    out.nodes = path.nodes;
    out.ns = path.weight_sum;
    return true;
}

}  // namespace qtoolkit
