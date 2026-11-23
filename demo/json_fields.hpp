#pragma once

// Schema accessors on top of json.hpp, shared by the two readers of a document:
// the advise stage (poolkit_advise's schema 1) and the verifier (the report).
// A missing member is an error with the path that names it, never a default:
// the suite's convention is that a number which lands in a report cannot be
// guessed.

#include "json.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace qtoolkit {

// Largest integer a JSON double identifies exactly (2^53): past this the token
// and the value disagree, and a silently rounded byte count is the failure this
// project exists to refuse.
inline constexpr std::uint64_t kMaxExactInteger = 9007199254740992ull;

[[nodiscard]] inline const JsonValue* member(const JsonValue& object, std::string_view name) {
    if (object.kind != JsonValue::Kind::object) {
        return nullptr;
    }
    return object.find(name);
}

[[nodiscard]] inline bool json_object(const JsonValue& parent, std::string_view name,
                                      const std::string& context, const JsonValue*& out,
                                      std::string& error) {
    out = member(parent, name);
    if (out == nullptr) {
        error = context + ": missing member '" + std::string(name) + "'";
        return false;
    }
    if (out->kind != JsonValue::Kind::object) {
        error = context + ": '" + std::string(name) + "' is not an object";
        return false;
    }
    return true;
}

[[nodiscard]] inline bool json_array(const JsonValue& parent, std::string_view name,
                                     const std::string& context, const JsonValue*& out,
                                     std::string& error) {
    out = member(parent, name);
    if (out == nullptr) {
        error = context + ": missing member '" + std::string(name) + "'";
        return false;
    }
    if (out->kind != JsonValue::Kind::array) {
        error = context + ": '" + std::string(name) + "' is not an array";
        return false;
    }
    return true;
}

[[nodiscard]] inline bool json_unsigned(const JsonValue& parent, std::string_view name,
                                        const std::string& context, std::uint64_t& out,
                                        std::string& error) {
    const JsonValue* value = member(parent, name);
    if (value == nullptr) {
        error = context + ": missing member '" + std::string(name) + "'";
        return false;
    }
    if (value->kind != JsonValue::Kind::number || !value->integral) {
        error = context + ": '" + std::string(name) + "' is not an integer";
        return false;
    }
    if (value->number < 0.0 || value->number > static_cast<double>(kMaxExactInteger)) {
        error = context + ": '" + std::string(name) + "' is outside the exact-integer range";
        return false;
    }
    out = static_cast<std::uint64_t>(value->number);
    return true;
}

[[nodiscard]] inline bool json_number(const JsonValue& parent, std::string_view name,
                                      const std::string& context, double& out,
                                      std::string& error) {
    const JsonValue* value = member(parent, name);
    if (value == nullptr) {
        error = context + ": missing member '" + std::string(name) + "'";
        return false;
    }
    if (value->kind != JsonValue::Kind::number) {
        error = context + ": '" + std::string(name) + "' is not a number";
        return false;
    }
    out = value->number;
    return true;
}

[[nodiscard]] inline bool json_boolean(const JsonValue& parent, std::string_view name,
                                       const std::string& context, bool& out, std::string& error) {
    const JsonValue* value = member(parent, name);
    if (value == nullptr) {
        error = context + ": missing member '" + std::string(name) + "'";
        return false;
    }
    if (value->kind != JsonValue::Kind::boolean) {
        error = context + ": '" + std::string(name) + "' is not a boolean";
        return false;
    }
    out = value->boolean;
    return true;
}

[[nodiscard]] inline bool json_string(const JsonValue& parent, std::string_view name,
                                      const std::string& context, std::string& out,
                                      std::string& error) {
    const JsonValue* value = member(parent, name);
    if (value == nullptr) {
        error = context + ": missing member '" + std::string(name) + "'";
        return false;
    }
    if (value->kind != JsonValue::Kind::string) {
        error = context + ": '" + std::string(name) + "' is not a string";
        return false;
    }
    out = value->text;
    return true;
}

// An element of an array of exact integers, by position.
[[nodiscard]] inline bool json_element(const JsonValue& array, std::size_t position,
                                       const std::string& context, std::uint64_t& out,
                                       std::string& error) {
    if (array.kind != JsonValue::Kind::array || position >= array.items.size()) {
        error = context + ": the array is shorter than the schema requires";
        return false;
    }
    const JsonValue& value = array.items[position];
    if (value.kind != JsonValue::Kind::number || !value.integral || value.number < 0.0 ||
        value.number > static_cast<double>(kMaxExactInteger)) {
        error = context + ": element " + std::to_string(position) +
                " is not an exact non-negative integer";
        return false;
    }
    out = static_cast<std::uint64_t>(value.number);
    return true;
}

}  // namespace qtoolkit
