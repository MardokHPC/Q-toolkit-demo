# Q-toolkit-demo

Four CUDA libraries in one command. A workload becomes a `cudaGraph_t`,
[graph-lint](https://github.com/MardokHPC/graph-lint) reads its topology,
[cuda-timer](https://github.com/MardokHPC/cuda-timer) weighs the graph one node at a time,
[poolkit](https://github.com/MardokHPC/poolkit) turns the workload's requests into a trace and
dimensions the pool, and [ulpgrad](https://github.com/MardokHPC/ulpgrad) is the math inside the
kernels. Everything lands in one `report.json`, and a host-only verifier reconstructs that report
from the committed artifacts on a machine with no GPU.

The four are pinned, never vendored: `cuda-timer v1.2.2`, `poolkit v1.0.0`, `ulpgrad v0.1.0`,
`graph-lint v0.3.1`, in `cmake/pins.cmake` and nowhere else.

## What it is not

Not a fifth tool, not a wrapper (the four formats stay visible), and not a benchmark of the four.
It replaces an afternoon of running four terminals by hand with one command and one diffable file.
The reasoning behind every non-decision is in `MASTERHANDOFF.md`.

## The run

Two 13-node shapes are captured on the reference card (RTX 2060 SUPER, sm_75, CUDA 12.6, one run):

| | `graph.json` (clean) | `graph.pingpong.json` |
|---|---:|---:|
| nodes | 13 | 13 |
| edges | 13 | 23 |
| depth | 10 | 13 |
| widest level | 2 | 1 |
| edges implied by a longer path | 0 | 11 |

- The clean shape is a fork/join: a stem, a 4-node branch, a 3-node branch on a second stream, a join
  that reads both, a 4-node tail. The pingpong is the anti-pattern: one event per node between two
  streams, so node `i` waits on `i - 1` and its own stream orders it after `i - 2`. graph-lint reports
  it as `11 of 23 edges are implied by a longer path; the minimal DAG has 12 edges`.
- The clean graph's weighted critical path is `0 4 5 6 7 8 9 10 11 12`, **349808 ns**. None of the
  13 nodes clamped to zero, and the zero point the weights were measured against is in the report.
  The number is a **lower bound**: subtracting the zero point does not calibrate the timer.
- The workload's requests are 28 trace operations — 14 allocations of 4 MiB, then 14 frees — and the
  advisor's replay puts the simulated peak at **58720256 B** with 14 blocks live.

Every number above is in `artifacts/report.json`, and `qtoolkit_verify artifacts/report.json`
reconstructs it from the other artifacts, exit 0. That is also the rule: a number in this README
that the verifier cannot rebuild from a committed file does not belong here.

A timing is a measurement of one machine. Re-running the pipeline moves `weights.json` and the
report's `weights` and `pool_measured` blocks; the graph blocks and the trace stay byte-identical.

## Status

M5, the MVP: the five artifacts (`graph.json`, `graph.pingpong.json`, `weights.json`, `trace.jsonl`,
`advise.json`) and the report, with `qtoolkit_verify` checking the five comparisons of the contract's
§6 without a toolkit. `MASTERHANDOFF.md` §7 lists what came before and what is left.

## Build

Two profiles. The host one needs no CUDA toolkit, no nvcc and no GPU:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DQTOOLKIT_DEMO_WITH_CUDA=OFF
cmake --build build
ctest --test-dir build --output-on-failure -E "^poolkit_"
qtoolkit_verify artifacts/report.json     # exit 0: the committed report checks out
```

The device profile needs CUDA 12.6 and a GPU; on Windows the two scripts call `vcvars64` and pin
`CMAKE_CUDA_COMPILER` so the 12.1 first on `PATH` cannot win:

```bash
build-host.bat      # configure, build, ctest (profile OFF)
build-device.bat    # configure, build, run the pipeline, write artifacts/ (profile ON)
```

The run starts from the repository root and writes `artifacts/`.

## License

MIT, see `LICENSE`.
