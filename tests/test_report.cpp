#include "report.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string document() {
    qtoolkit::JsonWriter writer;
    writer.begin_object();
    writer.field("schema");
    writer.integer(1);
    writer.field("pins");
    writer.begin_object();
    writer.field("cuda_timer");
    writer.text("v1.2.2");
    writer.field("poolkit");
    writer.text("v1.0.0");
    writer.end_object();
    writer.field("nodes");
    writer.begin_array();
    writer.integer(0);
    writer.integer(1);
    writer.end_array();
    writer.field("partial");
    writer.boolean(false);
    writer.end_object();
    return writer.str();
}

}  // namespace

TEST(JsonWriter, KeepsTheCallersKeyOrderAndOneValuePerLine) {
    // Not inside EXPECT_EQ: MSVC's traditional preprocessor mis-reads escapes in
    // a raw string that reaches it as a macro argument.
    const std::string expected = R"({
  "schema": 1,
  "pins": {
    "cuda_timer": "v1.2.2",
    "poolkit": "v1.0.0"
  },
  "nodes": [
    0,
    1
  ],
  "partial": false
})";
    EXPECT_EQ(document(), expected);
}

TEST(JsonWriter, EscapesWhatJsonRequires) {
    qtoolkit::JsonWriter writer;
    writer.begin_array();
    writer.text("a\"b\\c");
    writer.text("\b\f\n\r\t");
    writer.text(std::string("bell") + '\x01');
    writer.end_array();
    const std::string expected = R"([
  "a\"b\\c",
  "\b\f\n\r\t",
  "bell\u0001"
])";
    EXPECT_EQ(writer.str(), expected);
}

TEST(JsonWriter, PassesUtf8ThroughByteForByte) {
    qtoolkit::JsonWriter writer;
    writer.begin_array();
    writer.text("caff\xC3\xA8");
    writer.end_array();
    EXPECT_EQ(writer.str(), "[\n  \"caff\xC3\xA8\"\n]");
}

TEST(JsonWriter, EmptyContainersTakeNoBreak) {
    qtoolkit::JsonWriter writer;
    writer.begin_object();
    writer.field("classes");
    writer.begin_array();
    writer.end_array();
    writer.field("nested");
    writer.begin_object();
    writer.end_object();
    writer.end_object();
    EXPECT_EQ(writer.str(), "{\n  \"classes\": [],\n  \"nested\": {}\n}");
}

TEST(JsonWriter, NestedArraysIndentByDepth) {
    qtoolkit::JsonWriter writer;
    writer.begin_array();
    writer.begin_array();
    writer.integer(1);
    writer.integer(2);
    writer.end_array();
    writer.begin_array();
    writer.end_array();
    writer.end_array();
    const std::string expected = R"([
  [
    1,
    2
  ],
  []
])";
    EXPECT_EQ(writer.str(), expected);
}

TEST(Report, TheSchemaIsByteStable) {
    qtoolkit::Report report;
    report.version = "0.1.0";
    report.pins.cuda_timer = "v1.2.2";
    report.pins.poolkit = "v1.0.0";
    report.pins.ulpgrad = "v0.1.0";
    report.pins.graph_lint = "v0.3.1";
    report.environment.device = "NVIDIA GeForce RTX 2060 SUPER";
    report.environment.compute_major = 7;
    report.environment.compute_minor = 5;
    report.environment.cuda_runtime = "12.6";

    report.clean.nodes = 2;
    report.clean.edges = 1;
    report.clean.distinct_edges = 1;
    report.clean.minimal_edges = 1;
    report.clean.depth = 2;
    report.clean.width = {1, 1};

    report.pingpong = report.clean;
    report.pingpong.findings.push_back(
        qtoolkit::FindingRecord{"redundant-edge", "warn", {0, 1}, "1 of 1 edges is implied"});

    report.weights.zero_point_ns = 4000;
    report.weights.warmup_runs = 10;
    report.weights.measure_runs = 100;
    report.weights.clamped = 0;
    report.weights.median_ns = {4000, 20000};
    report.weights.path = {0, 1};
    report.weights.path_ns = 20000;

    report.pool.config.chunk_bytes = 8388608;
    report.pool.config.min_class_bytes = 256;
    report.pool.config.max_class_bytes = 4194304;
    report.pool.config.growth = 1.5;
    report.pool.config.power_of_two = true;
    report.pool.requests = 2;
    report.pool.bytes_requested = 8192;
    report.pool.chunks = 1;
    report.pool.high_water_mark_bytes = 8192;
    report.pool.growth_count = 2;
    report.pool.reuse_count = 0;
    report.pool.large_path_count = 0;
    report.pool.free_errors = 0;
    report.pool.quarantined_blocks = 0;

    report.advise.config.growth = 2.0;
    report.advise.config.chunk_bytes = 4096;
    report.advise.config.min_class_bytes = 256;
    report.advise.config.max_class_bytes = 4096;
    report.advise.config.power_of_two = false;
    report.advise.classes = 9;
    report.advise.ops = 4;
    report.advise.allocs = 2;
    report.advise.waste_internal = 0.5;
    report.advise.waste_tail = 0.25;
    report.advise.peak_sim_bytes = 8192;
    report.advise.peak_blocks = 2;
    report.advise.retention_bound_bytes = 36864;

    const std::string expected = R"({
  "schema_version": 1,
  "tool": "qtoolkit-demo",
  "version": "0.1.0",
  "pins": {
    "cuda_timer": "v1.2.2",
    "poolkit": "v1.0.0",
    "ulpgrad": "v0.1.0",
    "graph_lint": "v0.3.1"
  },
  "environment": {
    "device": "NVIDIA GeForce RTX 2060 SUPER",
    "compute_major": 7,
    "compute_minor": 5,
    "cuda_runtime": "12.6"
  },
  "graph": {
    "clean": {
      "nodes": 2,
      "edges": 1,
      "distinct_edges": 1,
      "depth": 2,
      "width": [
        1,
        1
      ],
      "minimal_edges": 1,
      "redundant_edges": 0,
      "findings": []
    },
    "pingpong": {
      "nodes": 2,
      "edges": 1,
      "distinct_edges": 1,
      "depth": 2,
      "width": [
        1,
        1
      ],
      "minimal_edges": 1,
      "redundant_edges": 0,
      "findings": [
        {
          "rule": "redundant-edge",
          "severity": "warn",
          "nodes": [
            0,
            1
          ],
          "message": "1 of 1 edges is implied"
        }
      ]
    }
  },
  "weights": {
    "unit": "ns",
    "zero_point_ns": 4000,
    "warmup_runs": 10,
    "measure_runs": 100,
    "clamped": 0,
    "median_ns": [
      4000,
      20000
    ],
    "path": [
      0,
      1
    ],
    "path_ns": 20000
  },
  "pool_measured": {
    "config": {
      "chunk_bytes": 8388608,
      "min_class_bytes": 256,
      "max_class_bytes": 4194304,
      "growth": 1.5,
      "power_of_two": true
    },
    "requests": 2,
    "bytes_requested": 8192,
    "chunks": 1,
    "high_water_mark_bytes": 8192,
    "growth_count": 2,
    "reuse_count": 0,
    "large_path_count": 0,
    "free_errors": 0,
    "quarantined_blocks": 0
  },
  "advise": {
    "config": {
      "growth": 2,
      "chunk_bytes": 4096,
      "min_class_bytes": 256,
      "max_class_bytes": 4096,
      "power_of_two": false,
      "extra_sizes": []
    },
    "classes": 9,
    "ops": 4,
    "allocs": 2,
    "waste": {
      "internal": 0.5,
      "tail": 0.25
    },
    "peak_sim_bytes": 8192,
    "peak_blocks": 2,
    "retention_bound_bytes": 36864
  }
}
)";
    EXPECT_EQ(qtoolkit::render_report(report), expected);
}

TEST(JsonReader, ReadsBackWhatTheWriterWrote) {
    qtoolkit::Report report;
    report.version = "0.1.0";
    report.weights.path_ns = 20000;
    report.advise.config.growth = 1.5;

    qtoolkit::JsonValue root;
    std::string error;
    ASSERT_TRUE(qtoolkit::parse_json(qtoolkit::render_report(report), root, error)) << error;
    ASSERT_EQ(root.kind, qtoolkit::JsonValue::Kind::object);

    const qtoolkit::JsonValue* version = root.find("version");
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(version->text, "0.1.0");

    const qtoolkit::JsonValue* weights = root.find("weights");
    ASSERT_NE(weights, nullptr);
    const qtoolkit::JsonValue* path_ns = weights->find("path_ns");
    ASSERT_NE(path_ns, nullptr);
    EXPECT_TRUE(path_ns->integral);
    EXPECT_EQ(path_ns->number, 20000.0);
    EXPECT_EQ(path_ns->token, "20000");

    const qtoolkit::JsonValue* grow = root.find("advise")->find("config")->find("growth");
    ASSERT_NE(grow, nullptr);
    EXPECT_FALSE(grow->integral);
    EXPECT_EQ(grow->number, 1.5);
}

TEST(JsonReader, RefusesWhatTheContractCallsInvalid) {
    const auto rejects = [](const std::string& document) {
        qtoolkit::JsonValue value;
        std::string error;
        return !qtoolkit::parse_json(document, value, error) && !error.empty();
    };
    EXPECT_TRUE(rejects("{\"a\": 1, \"a\": 2}")) << "duplicate key";
    EXPECT_TRUE(rejects("{\"a\": 1} x")) << "trailing content";
    EXPECT_TRUE(rejects("[1, 2")) << "unterminated array";
    EXPECT_TRUE(rejects("{\"a\" 1}")) << "missing colon";
    EXPECT_TRUE(rejects("\"\\q\"")) << "unknown escape";
    EXPECT_TRUE(rejects("01")) << "leading zero";
    EXPECT_TRUE(rejects("1.")) << "a fraction with no digits";
    EXPECT_TRUE(rejects("")) << "empty document";
    EXPECT_FALSE(rejects("{\"a\": [1, -2.5e3, true, null, \"x\"]}")) << "a valid document";
}
