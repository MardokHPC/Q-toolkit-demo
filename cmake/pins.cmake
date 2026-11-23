# The four pins, in one place and nowhere else (§3.3). report.json copies these
# values and qtoolkit_verify refuses a report whose pins disagree with this file.
# A branch would make the verdict depend on somebody else's push.

set(QTOOLKIT_PIN_CUDA_TIMER "v1.2.2")
set(QTOOLKIT_PIN_POOLKIT "v1.0.0")
set(QTOOLKIT_PIN_ULPGRAD "v0.1.0")
set(QTOOLKIT_PIN_GRAPH_LINT "v0.3.1")

set(QTOOLKIT_REPO_CUDA_TIMER "https://github.com/MardokHPC/cuda-timer.git")
set(QTOOLKIT_REPO_POOLKIT "https://github.com/MardokHPC/poolkit.git")
set(QTOOLKIT_REPO_ULPGRAD "https://github.com/MardokHPC/ulpgrad.git")
set(QTOOLKIT_REPO_GRAPH_LINT "https://github.com/MardokHPC/graph-lint.git")
