// WLM2PST - minimal internal JSON writer (no third-party dependency).
//
// Portable: no Windows headers. JsonValue is a small variant-based tree
// builder; to_json() serializes it deterministically (insertion order for
// object keys). Strings are treated as raw UTF-8 byte sequences: non-ASCII
// bytes pass through unescaped, only JSON-structural characters and control
// characters are escaped.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace wlm2pst {

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::string, JsonValue>>;

    enum class Type { kNull, kBool, kInt64, kUInt64, kDouble, kString, kArray, kObject };

    JsonValue() noexcept : storage_(std::monostate{}) {}
    JsonValue(std::nullptr_t) noexcept : storage_(std::monostate{}) {}
    JsonValue(bool value) noexcept : storage_(value) {}
    JsonValue(int value) noexcept : storage_(static_cast<int64_t>(value)) {}
    JsonValue(int64_t value) noexcept : storage_(value) {}
    JsonValue(uint64_t value) noexcept : storage_(value) {}
    JsonValue(double value) noexcept : storage_(value) {}
    JsonValue(std::string value) : storage_(std::move(value)) {}
    JsonValue(const char* value) : storage_(std::string(value)) {}

    static JsonValue object() noexcept {
        JsonValue v;
        v.storage_ = Object{};
        return v;
    }
    static JsonValue array() noexcept {
        JsonValue v;
        v.storage_ = Array{};
        return v;
    }

    [[nodiscard]] Type type() const noexcept { return static_cast<Type>(storage_.index()); }

    // Object builder: replaces the value if `key` is already present,
    // otherwise appends preserving insertion order. No-op if this JsonValue
    // is not an object (i.e. not created via JsonValue::object()).
    JsonValue& set(std::string key, JsonValue value) {
        if (auto* obj = std::get_if<Object>(&storage_)) {
            for (auto& entry : *obj) {
                if (entry.first == key) {
                    entry.second = std::move(value);
                    return *this;
                }
            }
            obj->emplace_back(std::move(key), std::move(value));
        }
        return *this;
    }

    // Array builder: appends. No-op if this JsonValue is not an array.
    JsonValue& push(JsonValue value) {
        if (auto* arr = std::get_if<Array>(&storage_)) {
            arr->push_back(std::move(value));
        }
        return *this;
    }

    [[nodiscard]] bool as_bool() const noexcept { return std::get<bool>(storage_); }
    [[nodiscard]] int64_t as_int64() const noexcept { return std::get<int64_t>(storage_); }
    [[nodiscard]] uint64_t as_uint64() const noexcept { return std::get<uint64_t>(storage_); }
    [[nodiscard]] double as_double() const noexcept { return std::get<double>(storage_); }
    [[nodiscard]] const std::string& as_string() const noexcept { return std::get<std::string>(storage_); }
    [[nodiscard]] const Array& as_array() const noexcept { return std::get<Array>(storage_); }
    [[nodiscard]] const Object& as_object() const noexcept { return std::get<Object>(storage_); }

private:
    std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, Array, Object> storage_;
};

// Serializes `value`. `indent` is the number of spaces per nesting level;
// `indent <= 0` produces compact output with no extraneous whitespace.
// Key order within objects is insertion order. Integers are always emitted
// as plain decimal digits (never scientific notation); uint64 values above
// INT64_MAX are emitted correctly.
std::string to_json(const JsonValue& value, int indent = 2);

}  // namespace wlm2pst
