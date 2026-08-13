#include "worker/reporting/json_writer.h"

#include <charconv>
#include <cstdio>

namespace wlm2pst {

namespace {

void write_indent(std::string& out, int indent, int depth) {
    if (indent <= 0) return;
    out.append(static_cast<size_t>(indent) * static_cast<size_t>(depth), ' ');
}

void write_json_string(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // Non-ASCII bytes (UTF-8 continuation/lead bytes) pass
                    // through unescaped, as does ordinary printable ASCII.
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    out += '"';
}

void write_double(std::string& out, double value) {
    char buf[64];
    auto result = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, result.ptr);
}

void write_value(const JsonValue& value, int indent, int depth, std::string& out) {
    switch (value.type()) {
        case JsonValue::Type::kNull:
            out += "null";
            break;
        case JsonValue::Type::kBool:
            out += value.as_bool() ? "true" : "false";
            break;
        case JsonValue::Type::kInt64:
            out += std::to_string(value.as_int64());
            break;
        case JsonValue::Type::kUInt64:
            out += std::to_string(value.as_uint64());
            break;
        case JsonValue::Type::kDouble:
            write_double(out, value.as_double());
            break;
        case JsonValue::Type::kString:
            write_json_string(out, value.as_string());
            break;
        case JsonValue::Type::kArray: {
            const auto& arr = value.as_array();
            if (arr.empty()) {
                out += "[]";
                break;
            }
            out += '[';
            if (indent > 0) out += '\n';
            for (size_t i = 0; i < arr.size(); ++i) {
                write_indent(out, indent, depth + 1);
                write_value(arr[i], indent, depth + 1, out);
                if (i + 1 != arr.size()) out += ',';
                if (indent > 0) out += '\n';
            }
            write_indent(out, indent, depth);
            out += ']';
            break;
        }
        case JsonValue::Type::kObject: {
            const auto& obj = value.as_object();
            if (obj.empty()) {
                out += "{}";
                break;
            }
            out += '{';
            if (indent > 0) out += '\n';
            for (size_t i = 0; i < obj.size(); ++i) {
                write_indent(out, indent, depth + 1);
                write_json_string(out, obj[i].first);
                out += indent > 0 ? ": " : ":";
                write_value(obj[i].second, indent, depth + 1, out);
                if (i + 1 != obj.size()) out += ',';
                if (indent > 0) out += '\n';
            }
            write_indent(out, indent, depth);
            out += '}';
            break;
        }
    }
}

}  // namespace

std::string to_json(const JsonValue& value, int indent) {
    std::string out;
    write_value(value, indent, 0, out);
    return out;
}

}  // namespace wlm2pst
