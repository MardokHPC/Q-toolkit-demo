#include "workload.hpp"

#include <ulpgrad/exp.h>
#include <ulpgrad/silu.h>

namespace qtoolkit {
namespace {

constexpr int kThreads = 256;

void failure(const char* call, cudaError_t status, std::string& error) {
    error = std::string(call) + ": " + cudaGetErrorString(status);
}

__global__ void silu_kernel(const float* in, float* out, int count) {
    const int index = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (index < count) {
        out[index] = ulpgrad::silu(in[index]);
    }
}

// exp2(-x) rather than exp2(x): a chain of exp2 is monotone in its input and
// reaches infinity after a few nodes, and a buffer full of inf measures the same
// instruction but is not a workload anybody would ship.
__global__ void exp2_kernel(const float* in, float* out, int count) {
    const int index = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (index < count) {
        out[index] = ulpgrad::exp2(-in[index]);
    }
}

__global__ void join_kernel(const float* left, const float* right, float* out, int count) {
    const int index = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (index < count) {
        out[index] = ulpgrad::silu(left[index]) + ulpgrad::exp2(-right[index]);
    }
}

bool launch_node(int index, const float* in, float* out, int count, cudaStream_t stream,
                 std::string& error) {
    const int blocks = (count + kThreads - 1) / kThreads;
    if (index % 2 == 0) {
        silu_kernel<<<blocks, kThreads, 0, stream>>>(in, out, count);
    } else {
        exp2_kernel<<<blocks, kThreads, 0, stream>>>(in, out, count);
    }
    const cudaError_t status = cudaGetLastError();
    if (status != cudaSuccess) {
        failure("kernel launch", status, error);
        return false;
    }
    return true;
}

bool launch_join(const float* left, const float* right, float* out, int count,
                 cudaStream_t stream, std::string& error) {
    const int blocks = (count + kThreads - 1) / kThreads;
    join_kernel<<<blocks, kThreads, 0, stream>>>(left, right, out, count);
    const cudaError_t status = cudaGetLastError();
    if (status != cudaSuccess) {
        failure("kernel launch", status, error);
        return false;
    }
    return true;
}

bool wait_event(cudaStream_t stream, cudaEvent_t event, std::string& error) {
    const cudaError_t status = cudaStreamWaitEvent(stream, event);
    if (status != cudaSuccess) {
        failure("cudaStreamWaitEvent", status, error);
        return false;
    }
    return true;
}

bool record_event(cudaEvent_t event, cudaStream_t stream, std::string& error) {
    const cudaError_t status = cudaEventRecord(event, stream);
    if (status != cudaSuccess) {
        failure("cudaEventRecord", status, error);
        return false;
    }
    return true;
}

struct Session {
    cudaStream_t streams[2] = {nullptr, nullptr};
    cudaEvent_t events[kNodeCount] = {};

    bool create(std::string& error) {
        for (cudaStream_t& stream : streams) {
            const cudaError_t status = cudaStreamCreate(&stream);
            if (status != cudaSuccess) {
                failure("cudaStreamCreate", status, error);
                return false;
            }
        }
        for (cudaEvent_t& event : events) {
            const cudaError_t status = cudaEventCreateWithFlags(&event, cudaEventDisableTiming);
            if (status != cudaSuccess) {
                failure("cudaEventCreateWithFlags", status, error);
                return false;
            }
        }
        return true;
    }

    ~Session() {
        for (cudaEvent_t event : events) {
            if (event != nullptr) {
                (void)cudaEventDestroy(event);
            }
        }
        for (cudaStream_t stream : streams) {
            if (stream != nullptr) {
                (void)cudaStreamDestroy(stream);
            }
        }
    }
};

// An open capture is an error the process cannot recover from: the destructor
// aborts it, and declaring this after the Session makes it run before the
// streams it points at are destroyed.
class Capture {
public:
    ~Capture() {
        if (open_) {
            cudaGraph_t discarded = nullptr;
            (void)cudaStreamEndCapture(origin_, &discarded);
            if (discarded != nullptr) {
                (void)cudaGraphDestroy(discarded);
            }
        }
    }

    bool begin(cudaStream_t origin, std::string& error) {
        const cudaError_t status = cudaStreamBeginCapture(origin, cudaStreamCaptureModeGlobal);
        if (status != cudaSuccess) {
            failure("cudaStreamBeginCapture", status, error);
            return false;
        }
        origin_ = origin;
        open_ = true;
        return true;
    }

    bool end(cudaGraph_t& graph, std::string& error) {
        const cudaError_t status = cudaStreamEndCapture(origin_, &graph);
        open_ = false;
        if (status != cudaSuccess) {
            graph = nullptr;
            failure("cudaStreamEndCapture", status, error);
            return false;
        }
        return true;
    }

private:
    cudaStream_t origin_ = nullptr;
    bool open_ = false;
};

bool validate(float* const* buffers, int count, cudaGraph_t& graph, std::string& error) {
    graph = nullptr;
    error.clear();
    if (buffers == nullptr) {
        error = "the buffer table is null";
        return false;
    }
    if (count <= 0) {
        error = "the element count must be > 0";
        return false;
    }
    return true;
}

}  // namespace

bool capture_clean(float* const* buffers, int count, cudaGraph_t& graph, std::string& error) {
    if (!validate(buffers, count, graph, error)) {
        return false;
    }

    Session session;
    if (!session.create(error)) {
        return false;
    }
    Capture capture;
    if (!capture.begin(session.streams[0], error)) {
        return false;
    }

    // Stem on the origin stream, then the branches. buffer i + 1 is node i's
    // output: the pairs below are the whole data flow, written out so no
    // arithmetic can put a branch on the other branch's buffer.
    if (!launch_node(0, buffers[0], buffers[1], count, session.streams[0], error)) {
        return false;
    }
    if (!record_event(session.events[0], session.streams[0], error)) {
        return false;
    }

    if (!wait_event(session.streams[1], session.events[0], error)) {
        return false;
    }
    if (!launch_node(1, buffers[1], buffers[2], count, session.streams[1], error) ||
        !launch_node(2, buffers[2], buffers[3], count, session.streams[1], error) ||
        !launch_node(3, buffers[3], buffers[4], count, session.streams[1], error)) {
        return false;
    }
    if (!record_event(session.events[1], session.streams[1], error)) {
        return false;
    }

    // The long branch stays on the origin stream: stream order already puts it
    // after the stem, and it needs no event of its own.
    if (!launch_node(4, buffers[1], buffers[5], count, session.streams[0], error) ||
        !launch_node(5, buffers[5], buffers[6], count, session.streams[0], error) ||
        !launch_node(6, buffers[6], buffers[7], count, session.streams[0], error) ||
        !launch_node(7, buffers[7], buffers[8], count, session.streams[0], error)) {
        return false;
    }

    // The join reads both branches: buffers[8] is the long branch's last output,
    // buffers[4] the short one's.
    if (!wait_event(session.streams[0], session.events[1], error)) {
        return false;
    }
    if (!launch_join(buffers[8], buffers[4], buffers[9], count, session.streams[0], error)) {
        return false;
    }

    if (!launch_node(9, buffers[9], buffers[10], count, session.streams[0], error) ||
        !launch_node(10, buffers[10], buffers[11], count, session.streams[0], error) ||
        !launch_node(11, buffers[11], buffers[12], count, session.streams[0], error) ||
        !launch_node(12, buffers[12], buffers[13], count, session.streams[0], error)) {
        return false;
    }

    return capture.end(graph, error);
}

bool capture_pingpong(float* const* buffers, int count, cudaGraph_t& graph, std::string& error) {
    if (!validate(buffers, count, graph, error)) {
        return false;
    }

    Session session;
    if (!session.create(error)) {
        return false;
    }
    Capture capture;
    if (!capture.begin(session.streams[0], error)) {
        return false;
    }

    for (int node = 0; node < kNodeCount; ++node) {
        cudaStream_t current = session.streams[node % 2];
        // Waiting on the other stream's previous node adds i - 1; the stream
        // itself already orders this node after i - 2.
        if (node > 0 && !wait_event(current, session.events[node - 1], error)) {
            return false;
        }
        if (!launch_node(node, buffers[node], buffers[node + 1], count, current, error)) {
            return false;
        }
        if (!record_event(session.events[node], current, error)) {
            return false;
        }
    }

    // Node 12 runs on the origin stream after waiting for node 11: the two
    // streams are already joined, and the capture can close here.
    return capture.end(graph, error);
}

}  // namespace qtoolkit
