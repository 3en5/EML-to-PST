// WLM2PST - canonical manifest serialization implementation. See
// manifest_hash.h.
#include "worker/scanner/manifest_hash.h"

#include <string>
#include <string_view>

namespace wlm2pst {
namespace {

// File-local UTF-8 encoder (deliberately duplicated from scanner.cpp rather
// than shared: both are small, and integration may later swap either for
// common/unicode). Handles both wchar_t widths: UTF-16 (Windows, surrogate
// pairs) and UTF-32 (Linux/test builds).
namespace detail {

void append_utf8(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string wstring_to_utf8(std::wstring_view s) {
    std::string out;
    out.reserve(s.size() * 2);
    if constexpr (sizeof(wchar_t) == 2) {
        for (size_t i = 0; i < s.size(); ++i) {
            char32_t cp = static_cast<char32_t>(static_cast<uint16_t>(s[i]));
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()) {
                char32_t low = static_cast<char32_t>(static_cast<uint16_t>(s[i + 1]));
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            }
            append_utf8(out, cp);
        }
    } else {
        for (wchar_t wc : s) {
            append_utf8(out, static_cast<char32_t>(static_cast<uint32_t>(wc)));
        }
    }
    return out;
}

}  // namespace detail
}  // namespace

void serialize_manifest_for_hash(const std::vector<ManifestEntry>& sorted_entries,
                                  const std::function<void(const void*, size_t)>& sink) {
    for (const ManifestEntry& entry : sorted_entries) {
        std::string line = detail::wstring_to_utf8(entry.relative_path);
        line += '|';
        line += std::to_string(entry.size);
        line += '|';
        line += std::to_string(entry.last_write_utc);
        line += '\n';
        sink(line.data(), line.size());
    }
}

}  // namespace wlm2pst
