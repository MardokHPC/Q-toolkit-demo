#include "advise_stage.hpp"

#include "json_fields.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

namespace qtoolkit {
namespace {

// std::to_string on a double would go through printf; to_chars does not, and a
// comma in "1,5" would make the advisor read a different growth factor.
std::string number_text(double value) {
    char buffer[64] = {};
    const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc()) {
        return "0";
    }
    return std::string(buffer, result.ptr);
}

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

// cmd.exe strips the first and the last quote of a line that starts with one,
// which would eat the quotes around the executable. One extra pair makes the
// outer quotes the ones it strips.
void run_child(const std::string& command, std::string& error) {
    std::printf("[qtoolkit] exec: %s\n", command.c_str());
#ifdef _WIN32
    const std::string line = "\"" + command + "\"";
#else
    const std::string line = command;
#endif
    const int status = std::system(line.c_str());
    if (status != 0) {
        error = "poolkit_advise exited with " + std::to_string(status);
    }
}

bool parse_advise(const JsonValue& root, const std::string& path, AdviseBlock& out,
                  std::string& error) {
    const std::string context = path;
    if (root.kind != JsonValue::Kind::object) {
        error = context + ": the document is not an object";
        return false;
    }

    std::uint64_t schema = 0;
    if (!json_unsigned(root, "schema", context, schema, error)) {
        return false;
    }
    if (schema != 1) {
        error = context + ": schema " + std::to_string(schema) + ", this demo reads 1";
        return false;
    }

    const JsonValue* trace = nullptr;
    std::uint64_t ops = 0;
    std::uint64_t allocs = 0;
    if (!json_object(root, "trace", context, trace, error) ||
        !json_unsigned(*trace, "ops", context + ".trace", ops, error) ||
        !json_unsigned(*trace, "allocs", context + ".trace", allocs, error)) {
        return false;
    }

    const JsonValue* config = nullptr;
    if (!json_object(root, "config", context, config, error)) {
        return false;
    }
    AdviseBlock block;
    if (!json_number(*config, "growth", context + ".config", block.config.growth, error) ||
        !json_unsigned(*config, "chunk_bytes", context + ".config", block.config.chunk_bytes,
                       error) ||
        !json_unsigned(*config, "min_class_bytes", context + ".config",
                       block.config.min_class_bytes, error) ||
        !json_unsigned(*config, "max_class_bytes", context + ".config",
                       block.config.max_class_bytes, error) ||
        !json_boolean(*config, "power_of_two", context + ".config", block.config.power_of_two,
                      error)) {
        return false;
    }
    const JsonValue* extra = nullptr;
    if (!json_array(*config, "extra_sizes", context + ".config", extra, error)) {
        return false;
    }
    for (const JsonValue& item : extra->items) {
        if (item.kind != JsonValue::Kind::number || !item.integral || item.number < 0.0 ||
            item.number > static_cast<double>(kMaxExactInteger)) {
            error = context + ".config: an extra size is not an exact non-negative integer";
            return false;
        }
        block.config.extra_sizes.push_back(static_cast<std::uint64_t>(item.number));
    }

    std::uint64_t classes = 0;
    if (!json_unsigned(root, "classes", context, classes, error)) {
        return false;
    }

    const JsonValue* waste = nullptr;
    if (!json_object(root, "waste", context, waste, error) ||
        !json_number(*waste, "internal", context + ".waste", block.waste_internal, error) ||
        !json_number(*waste, "tail", context + ".waste", block.waste_tail, error)) {
        return false;
    }

    const JsonValue* peak = nullptr;
    std::uint64_t peak_bytes = 0;
    std::uint64_t peak_blocks = 0;
    if (!json_object(root, "peak", context, peak, error) ||
        !json_unsigned(*peak, "resident_bytes", context + ".peak", peak_bytes, error) ||
        !json_unsigned(*peak, "blocks", context + ".peak", peak_blocks, error)) {
        return false;
    }

    std::uint64_t retention = 0;
    if (!json_unsigned(root, "retention_bound_bytes", context, retention, error)) {
        return false;
    }

    block.classes = static_cast<std::size_t>(classes);
    block.ops = static_cast<std::size_t>(ops);
    block.allocs = static_cast<std::size_t>(allocs);
    block.peak_sim_bytes = peak_bytes;
    block.peak_blocks = static_cast<std::size_t>(peak_blocks);
    block.retention_bound_bytes = retention;
    out = block;
    return true;
}

}  // namespace

bool run_advisor(const std::string& trace_path, const std::string& advise_path,
                 const PoolConfigRecord& reference, AdviseBlock& out, std::string& error) {
    error.clear();

    const std::string command = std::string("\"") + QTOOLKIT_POOLKIT_ADVISE + "\" --trace \"" +
                                trace_path + "\" --json \"" + advise_path + "\" --chunk " +
                                std::to_string(reference.chunk_bytes) + " --min-class " +
                                std::to_string(reference.min_class_bytes) + " --max-class " +
                                std::to_string(reference.max_class_bytes) + " --growth " +
                                number_text(reference.growth);
    run_child(command, error);
    if (!error.empty()) {
        return false;
    }

    std::string document;
    if (!read_document(advise_path, document, error)) {
        return false;
    }
    JsonValue root;
    if (!parse_json(document, root, error)) {
        error = advise_path + ": " + error;
        return false;
    }
    if (!parse_advise(root, advise_path, out, error)) {
        return false;
    }
    return true;
}

}  // namespace qtoolkit
