#pragma once

// One JSONL line per request, written where the request is made. poolkit's trace
// is the sequence of what the workload asked the pool for; the CUPTI recorder
// would hand back the driver's chunks instead, which is why the demo writes its
// own (§0.6).

#include <poolkit/pool.hpp>

#include <cstddef>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace qtoolkit {

class TraceSink {
public:
    [[nodiscard]] bool open(const std::string& path, std::string& error);
    [[nodiscard]] bool close(std::string& error);

    poolkit::DevicePtr allocate(poolkit::Pool& pool, std::size_t bytes,
                                poolkit::StreamHandle stream);
    void release(poolkit::Pool& pool, poolkit::DevicePtr handle);

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::size_t allocations() const noexcept { return allocations_; }

private:
    void line(const char* op, std::size_t size, int ptr);

    std::ofstream file_;
    std::unordered_map<void*, int> ids_;
    int next_id_ = 0;
    int ordinal_ = 0;
    std::size_t allocations_ = 0;
    bool failed_ = false;
};

inline bool TraceSink::open(const std::string& path, std::string& error) {
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_) {
        error = path + ": cannot open the trace for writing";
        return false;
    }
    return true;
}

// Integers through std::to_string and not through operator<<: a stream's integer
// formatting goes through the locale, and a thousands separator would make the
// file unreadable by poolkit's parser.
inline void TraceSink::line(const char* op, std::size_t size, int ptr) {
    file_ << "{\"op\":\"" << op << "\",\"size\":" << std::to_string(size) << ",\"ptr\":"
          << std::to_string(ptr) << ",\"stream\":0,\"ordinal\":" << std::to_string(ordinal_++)
          << "}\n";
    if (!file_) {
        failed_ = true;
    }
}

inline poolkit::DevicePtr TraceSink::allocate(poolkit::Pool& pool, std::size_t bytes,
                                              poolkit::StreamHandle stream) {
    poolkit::DevicePtr handle = pool.allocate(bytes, stream);
    const int id = next_id_++;
    ids_.emplace(handle.get(), id);
    ++allocations_;
    line("alloc", bytes, id);
    return handle;
}

inline void TraceSink::release(poolkit::Pool& pool, poolkit::DevicePtr handle) {
    const auto found = ids_.find(handle.get());
    if (found != ids_.end()) {
        line("free", 0, found->second);
        ids_.erase(found);
    } else {
        // A handle the sink never wrote is a bug in the caller, not a trace line.
        failed_ = true;
    }
    pool.free(std::move(handle));
}

inline bool TraceSink::close(std::string& error) {
    file_.close();
    if (!file_) {
        error = "the trace was not written completely";
        return false;
    }
    if (failed_) {
        error = "a trace line was lost, or a handle was released that the sink never wrote";
        return false;
    }
    return true;
}

}  // namespace qtoolkit
