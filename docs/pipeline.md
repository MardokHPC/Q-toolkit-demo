# The pipeline

One command, five hand-offs. Each hand-off is a format that a process without the GPU can read, and
each one has exactly one owner: a number that is not in `artifacts/report.json` does not exist.

| # | from → to | artifact | state |
|---|---|---|---|
| 1 | workload → graph-lint | `artifacts/graph.json`, `artifacts/graph.pingpong.json` | implemented (M2) |
| 2 | graph.json → `report.graph` | `artifacts/report.json` (block `graph`) | implemented (M2) |
| 3 | clean graph → cuda-timer → `report.weights` | `artifacts/weights.json` | implemented (M3) |
| 4 | workload → `artifacts/trace.jsonl` → poolkit_advise | `artifacts/advise.json` | implemented (M4) |
| 5 | both graphs → cuda-timer → `report.shape_cost` | `artifacts/report.json` (block `shape_cost`) | implemented |
| 6 | the five artifacts → `report.json` → `qtoolkit_verify` | `artifacts/report.json` | M5 |

This file records what the code does at each boundary, and why it does it that way: the artifact each
stage writes, the one number it owns, and the assumption it does not close.

## 1. A live handle to two files

`demo/workload.cu` builds two 13-node shapes with `ulpgrad::silu` and `ulpgrad::exp2`, one file each:

- `graph.json` — the **clean** shape: stem, a 4-node branch and a 3-node branch on two streams, a
  join that reads both, a 4-node tail. Width 2, depth below the node count, no redundant edges. This
  is the only shape the ablation measures (M3).
- `graph.pingpong.json` — the anti-pattern: one event per node passing between two streams, so node
  `i` waits on `i - 1` and is ordered after `i - 2` by its own stream. Depth equals the node count
  and about half the edges are implied by a longer path. It exists so the linter has something to
  find; without it the demo would claim a linter that reports zero findings.

Every node writes its own buffer (`buffers[i + 1]`, 14 buffers, `buffers[0]` is the input), so two
nodes the graph lets run together never write the same memory. `exp2` is called on the negated
input: a chain of `exp2` on positive values reaches infinity after a few nodes, and a buffer of
`inf` measures the same instruction but is not a workload.

`demo/graph_stage.cpp` calls `graphlint::write_graph_json`, the only bridge from the process that
owns the GPU to the world that reads JSON. Two rules:

- the node `id` in the file is a **canonical** id, not the order `cudaGraphGetNodes` returned. The
  map id → handle comes from `from_cuda`'s `handles_by_id`, never rebuilt;
- after `write_graph`, the handle is dead to us. Nothing downstream may read it.

## 2. The file to the report block

`analyze_graph` reads the artifact back with `parse_graph`, `topology` and `lint`, and fills the
report block: node and edge counts, `depth`, the full `width` vector, `minimal_edges`,
`redundant_edges`, and the linter's findings with their rule, severity, node list and message.
Nothing in the block comes from the capture: the run writes the file, the file feeds the block, so a
block and its artifact cannot disagree.

`artifacts/report.json` also carries the four pins — copied from `cmake/pins.cmake` through compile
definitions, never retyped — and the environment: device name, compute capability and CUDA runtime
version. That is the machine the numbers belong to, recorded in the artifact instead of in prose.

`demo/report.hpp` is the only JSON writer: keys keep the caller's order, one value per line, fixed
separators, and a number never goes through a locale. That is what makes a committed artifact
diffable: when a number moves, the diff names it.

## 3. The clean graph to the weights table

The ablation is the same protocol `graph-lint/tools/weights.cu` uses (that file
is the producer of graph-lint's own table, hardcoded and not exported, so the demo writes its own
table and copies the policy, not the code):

- one `cudaGraphExec_t` for the whole sweep, so what changes between measurements is which work the
  launch does, never the shape of the graph;
- node `i` enabled and every other node disabled with `cudaGraphNodeSetEnabled`, timed with
  `cuda_timer::benchmark` at 10 warm-up runs and 100 measured ones, median of the stored samples;
- the **zero point** closes the sweep: every node disabled, same call, same clock;
- `weight = max(0, median - zero_point)`, and every clamped node is counted.

The handles come from `from_cuda`'s `handles_by_id`. Reading them off `cudaGraphGetNodes` would label
node 3 with another node's handle, and both the table and the report would look right.

**The number is a lower bound.** Subtracting the zero point is not calibrating the timer: the floor
with one node enabled is not the floor with all of them disabled, and graph-lint measured the gap on
this suite (9 011 ns of zero point against roughly 8 µs of additive floor, weights understated by
19 % on the smallest node). Nothing in the demo corrects that; `weights.json` and the report say
"lower bound" and the clamp count is printed.

**The 30 % criterion** is enforced, not described: if more than 30 % of the nodes clamp to zero, the
run fails with the device name and writes no table. A table of zeros is a measurement of the timer,
not of the graph, and committing it would be worse than having none.

`weights.json` is graph-lint's format (`docs/weights.md` §1): one row per canonical id, integers in
ns, sorted keys, bound to its graph by a fingerprint. graph-lint exposes the reader and the
fingerprint but no writer, so the demo formats the file; the acceptance test is graph-lint's own CLI,
run by ctest on the committed pair: `graphlint_report --weights artifacts/weights.json
artifacts/graph.json` must exit 0 and print the weighted path.

The table owns the per-node numbers. The report block owns what the table cannot carry: the unit, the
zero point it was subtracted from, the protocol that produced it, how many nodes clamped, and the
critical path. `path_ns` is recomputed from `graph.json` and `weights.json` after both are on disk —
the same computation the verifier will redo (its check 2) — never taken from the measurement in
memory.

A timing is a property of the machine and the run; the structural numbers are not. Re-running the
pipeline on this card moves `weights.json` and the report's `weights` block, and leaves the graph
blocks byte-identical. That is expected, and it is why the verifier checks consistency and not
stability.

## 4. The workload's requests to the advisor

The workload's fourteen buffers are `poolkit::Pool` allocations, and every `allocate`/`free` writes
one JSONL line at the moment the request is made (`demo/trace_sink.hpp`). The trace is the sequence
of **requests**, not of the driver's allocations: poolkit's CUPTI recorder would have handed back the
chunks the pool asks `cudaMalloc` for, which are a property of the pool and not of the workload.
That is why the demo writes the file itself.

The format is poolkit's (`docs/trace-format.md` §1): five keys, `ordinal` strictly increasing, `ptr`
a monotone id that is unique among live blocks, `stream` a logical id — `0` here, because the whole
run is one stream — and `ops`/`allocs` in the file as the advisor counts them.

The reference configuration is the workload's, not the advisor's default: 4 MiB buffers, so a 4 MiB
class and one block per chunk. A recommendation computed against poolkit's default table (2 MiB max
class) would send every request down the large path and describe a different pool, so the demo passes
its own `--chunk/--min-class/--max-class/--growth` to `poolkit_advise`. The same `Pool::Config` fills
`pool_measured.config`, so the number and the table that produced it are in the same document.

**What this trace can and cannot show.** The fourteen buffers are live at the same time, for the
whole run: the trace has fourteen allocations and then fourteen frees, the pool grows fourteen times
and reuses nothing. That is the workload's live set, not a defect, and it is why `advise.peak_sim_bytes`
and `pool_measured.high_water_mark_bytes` agree here by coincidence and not by validation. The replay
assumes a block is reusable the moment it is freed; the pool reuses it once the event recorded at the
free completes (`docs/advise.md` §8). The two names exist so that nobody reads an equality as a proof.

`poolkit_advise` has no API — `tools/advise.hpp` is not installed — so the pipeline runs it as a child
process and reads the `--json` it writes. `demo/advise_stage.cpp` parses that document with the same
strict reader the verifier will use: a missing field is an error, never a zero.

## 5. The shape's price

The ablation measures one node at a time on the clean graph. That answers "which node is expensive"; it
does not answer "what does the wrong shape cost". `demo/shape_cost.cu` answers the second question with
the two graphs both alive: one exec per shape, launched whole, no node enabled or disabled.

Two clocks, because the two costs are not the same kind of thing:

- **the launch** is asynchronous, so it is timed with `cuda_timer::Timer` on the ablation's protocol —
  10 warm-up runs, 100 measured, median — and the block carries the launch's `min` and `max` beside the
  median. The two shapes are launched inside one loop, alternating which one goes first. Two separate
  sweeps measure the card twice, and the two orders disagreed in sign between runs; interleaved, a clock
  that drifts during the minute moves both shapes together instead of landing in the delta. The delta is
  then the measured quantity, and it is the only reason the number is worth quoting;
- **the instantiation** is a host call and never touches a stream, so a pair of CUDA events would time
  the empty stream. It is timed with `std::chrono::steady_clock` around `cudaGraphInstantiate`, 5
  warm-up and 50 measured, the exec destroyed outside the window. The block names the clock, so nobody
  can read a host measurement as a device one.

The deltas are stored, not derived by a reader, and a negative delta is a legitimate answer: the report
carries it instead of hiding it. The verifier (its check 6) redoes the subtraction, holds the launch to
the ablation's run counts, and refuses a median outside its own `[min, max]`.

**What the number says here, and what it does not.** On the reference card the pingpong costs 10,192 ns
more per launch and 9,100 ns more to instantiate. That is much less than the difference between
`sum_ns` and `path_ns` — the work the clean shape overlaps on paper — and the gap is the finding: a
4 MiB kernel already fills this GPU, so the clean graph's two branches never ran at the same time. What
the pingpong pays is dependency overhead and one more instantiation, not lost overlap. On a graph of
small kernels the same three levels would cost the sum. The block is a measurement of this workload; it
is not a price list for topologies.

## 6. The verifier

`qtoolkit_verify` reads `report.json` and rebuilds every number it can from the other committed
files, with no toolkit linked and no GPU touched:

1. both graphs, reopened with `parse_graph` + `topology`, against `report.graph.clean` and
   `report.graph.pingpong`;
2. `weights.json`, with `parse_weights` + `apply_weights` — the fingerprint must bind it to
   `graph.json` — and the critical path recomputed against `report.weights.path_ns` and `path`;
3. `trace.jsonl`, parsed and replayed on `SimBackend` with the table the report's `advise` block
   recommends, against `report.advise.ops`, `allocs`, `peak_sim_bytes` and `peak_blocks`;
4. the arithmetic: for every id, `weights.json[id] == max(0, median_ns[id] - zero_point_ns)` from the
   report's own medians, `clamped` equal to the number of zeros, and `sum_ns` equal to the sum of the
   table;
5. the four pins against `cmake/pins.cmake` — or the `pins.cmake` beside the binary, when the verifier
   runs from the release archive and has no build tree to look at;
6. the shape's price: both medians, both deltas, each delta equal to the subtraction of the pair beside
   it, the launch on the ablation's run counts, and each median inside its own `[min, max]`.

Exit 0 means every comparison held. Exit 1 names the comparison that failed, with the value the
report claims and the value the artifact gives. Exit 2 means cannot decide — a missing or unreadable
artifact, or pins that disagree with the file — because a verifier that answers without being able
to see is worse than one that says so. A proven mismatch wins over an undecidable check: "I can show
this is wrong" is a stronger answer than "I could not look".

What the verifier cannot check, and does not pretend to: the zero point, the medians themselves and
everything in `pool_measured` are measurements of one card. They live in the report, they are what
the other numbers were derived from, and no reading of the files can reproduce them. That is why
this README quotes only numbers the verifier rebuilds.
