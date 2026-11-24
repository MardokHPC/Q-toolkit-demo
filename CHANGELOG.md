# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-11-24

### Added

- The shape's price. `report.shape_cost` measures the clean and the pingpong graph
  as whole execs — one `cudaGraphExec_t` per shape, launched 10 + 100 times with
  cuda-timer's `Timer`, and 50 `cudaGraphInstantiate` calls on the host clock,
  because instantiation never touches a stream — and stores both medians, the
  launch's `min` and `max`, and the two deltas. The two shapes are launched inside
  one loop, alternating which one goes first: two separate sweeps measured the card
  twice and disagreed in sign between runs. On the reference card the pingpong
  costs 10,192 ns more per launch and 9,100 ns more to instantiate; the README and
  `docs/pipeline.md` explain why that is far less than the node weights suggest.
- `weights.sum_ns`: the sum of every node's weight, beside the critical path. The
  verifier recomputes it from `weights.json`; `sum_ns - path_ns` is the work the
  clean shape overlaps on paper.
- `qtoolkit_verify` check 6: each delta equals the subtraction of its pair, the
  launch carries the ablation's run counts, and each median sits inside its own
  `[min, max]`.
- `qtoolkit_verify` resolves `pins.cmake` at runtime — `QTOOLKIT_PINS_FILE`, then
  the file beside the executable, then the build tree, then the checkout — so a
  release archive with no build tree can still run check 5.

### Changed

- `report.json` is schema version 2. The `shape_cost` block is required, and a
  verifier that reads 1 cannot check it, so the number is not cosmetic.
- `qtoolkit_verify` reads schema 2 and answers 2 on a report that says 1.

## [0.1.0] - 2026-11-23

### Added

- Two configure profiles. `QTOOLKIT_DEMO_WITH_CUDA=ON` fetches the four pinned
  libraries and builds the capture path; `OFF` builds the host half on a runner
  with no toolkit, no nvcc and no GPU.
- `qtoolkit_workload`: a 13-node workload with `ulpgrad::silu` and `ulpgrad::exp2`,
  captured in two shapes — the clean fork/join graph and the pingpong
  anti-pattern — and written to `graph.json` and `graph.pingpong.json` through
  graph-lint's `write_graph_json`.
- `artifacts/report.json` begins: schema version, tool and project version, the
  four pins, the environment (device, compute capability, CUDA runtime), and one
  graph block per shape with node and edge counts, depth, width, the transitive
  reduction and the linter's findings. Every block is derived from its committed
  artifact with `parse_graph`, `topology` and `lint`, never from the capture.
- The ablation (M3): one exec, one node enabled at a time, `cuda_timer::benchmark`
  at 10 warm-up runs and 100 measured ones, the zero point measured with every
  node disabled, `weight = max(0, median - zero_point)`, clamped nodes counted.
  More than 30 % clamped fails the run instead of writing a table of zeros.
- `artifacts/weights.json` in graph-lint's format, bound to its graph by the
  fingerprint, accepted by `graphlint_report --weights` — a ctest entry runs that
  CLI on the committed pair, on a runner with no GPU.
- The report's `weights` block: unit, zero point, protocol, clamp count, and the
  critical path recomputed from `graph.json` and `weights.json` on disk.
- The workload's fourteen buffers come from `poolkit::Pool` (`CudaBackend`, 4 MiB
  buffers, a 4 MiB class), and every request is one line of
  `artifacts/trace.jsonl` written at the call site, not by a recorder.
- `artifacts/advise.json` from `poolkit_advise`, run as a child process with the
  run's own reference configuration, and read back with a strict JSON reader: a
  missing field is an error, never a zero.
- The report's `pool_measured` and `advise` blocks, with names that cannot be
  mistaken for one another (`high_water_mark_bytes` against `peak_sim_bytes`).
- `qtoolkit_verify`, the five checks, on a machine with no GPU: the two
  graphs reopened with `parse_graph` + `topology`, the weights table bound by
  fingerprint and its critical path recomputed, the trace replayed on
  `SimBackend` against `report.advise`, the per-node arithmetic, and the pins
  against `cmake/pins.cmake`. Exit 0 reconstructed, 1 a comparison failed with the
  line that names it, 2 cannot decide.
- A README whose numbers all come from `artifacts/report.json`, and a ctest entry
  that proves the committed report still checks out.
- CI job `cuda`: the ON profile configures and builds with nvcc 12.1.1 and no GPU,
  on Linux. The Windows half and the `gpu` job are written in the workflow and
  left commented with what they need.
- `qtoolkit_verify`: the exit-code contract (0 reconstructed, 1 mismatch, 2
  cannot decide). The five checks land in M5; until then a
  readable report is exit 2, never a silent 0.
- Host test suite on the report's JSON writer and schema, and ctest entries that
  pin the verifier's exit codes.
- GoogleTest v1.15.2, the only one in the build.
- CI job `host` on `ubuntu-22.04` and `windows-2022`, profile `OFF`.
