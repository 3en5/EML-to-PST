#include "worker/normalize/eml_normalizer.h"

#include <algorithm>

namespace wlm2pst {

namespace {

bool is_header_like_line(std::string_view line) {
    if (line.empty()) return false;
    if (line.front() == ' ' || line.front() == '\t') return true;  // continuation
    size_t colon = line.find(':');
    if (colon == 0 || colon == std::string_view::npos) return false;
    // Field names are printable US-ASCII excluding ':' (RFC 5322 ftext).
    for (char c : line.substr(0, colon)) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 33 || uc > 126 || c == ':') return false;
    }
    return true;
}

}  // namespace

EmlNormalization normalize_eml_header(std::string_view prefix, size_t cap) {
    EmlNormalization out;
    std::string_view window = prefix.substr(0, std::min(prefix.size(), cap));

    size_t pos = 0;
    if (window.size() >= 3 && static_cast<unsigned char>(window[0]) == 0xEF &&
        static_cast<unsigned char>(window[1]) == 0xBB &&
        static_cast<unsigned char>(window[2]) == 0xBF) {
        pos = 3;
        out.changed = true;
        out.repairs.emplace_back("utf8-bom");
    }

    bool saw_nul = false;
    bool saw_bad_ending = false;
    bool separator_found = false;
    size_t insert_separator_at_header_len = std::string::npos;  // position in normalized_header

    // Walk line by line, rebuilding the header section with CRLF endings and
    // NULs removed. Stop at the separator (blank line) or window end.
    while (pos < window.size()) {
        size_t line_start = pos;
        size_t line_end = pos;
        size_t next = pos;
        while (line_end < window.size()) {
            char c = window[line_end];
            if (c == '\n') { next = line_end + 1; saw_bad_ending = true; break; }
            if (c == '\r') {
                if (line_end + 1 < window.size() && window[line_end + 1] == '\n') {
                    next = line_end + 2;
                } else {
                    next = line_end + 1;
                    saw_bad_ending = true;  // bare CR
                }
                break;
            }
            ++line_end;
        }
        if (line_end == window.size()) next = window.size();  // unterminated final line

        std::string_view line = window.substr(line_start, line_end - line_start);

        if (line.empty()) {  // separator
            separator_found = true;
            pos = next;
            break;
        }

        // Missing-separator heuristic: the first line that cannot be a header
        // line marks the body boundary (safe identification, spec section 16).
        if (!is_header_like_line(line)) {
            bool has_nul = line.find('\0') != std::string_view::npos;
            if (!has_nul) {
                insert_separator_at_header_len = out.normalized_header.size();
                out.original_header_bytes = line_start;
                break;
            }
            // NUL-carrying junk line inside the header: scrub and keep.
        }

        for (char c : line) {
            if (c == '\0') { saw_nul = true; continue; }
            out.normalized_header.push_back(c);
        }
        out.normalized_header.push_back('\r');
        out.normalized_header.push_back('\n');
        pos = next;
    }

    if (separator_found) {
        out.original_header_bytes = pos;  // body starts right after the blank line
        out.normalized_header.push_back('\r');
        out.normalized_header.push_back('\n');
    } else if (insert_separator_at_header_len != std::string::npos) {
        out.normalized_header.push_back('\r');
        out.normalized_header.push_back('\n');
        out.separator_inserted = true;
        out.repairs.emplace_back("separator-inserted");
        out.changed = true;
        // original_header_bytes already set to the boundary line start.
    } else {
        // Entire window consumed without a separator or clear boundary:
        // treat everything seen as header and terminate it.
        out.original_header_bytes = pos;
        out.normalized_header.push_back('\r');
        out.normalized_header.push_back('\n');
        out.separator_inserted = true;
        out.repairs.emplace_back("separator-inserted");
        out.changed = true;
    }

    if (saw_nul) { out.repairs.emplace_back("nul"); out.changed = true; }
    if (saw_bad_ending) { out.repairs.emplace_back("line-endings"); out.changed = true; }
    return out;
}

}  // namespace wlm2pst
