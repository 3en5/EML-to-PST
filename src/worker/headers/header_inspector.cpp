#include "worker/headers/header_inspector.h"

#include <algorithm>
#include <cctype>

namespace wlm2pst {

namespace {

bool ascii_ieq(unsigned char a, unsigned char b) {
    return std::tolower(a) == std::tolower(b);
}

bool ascii_ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!ascii_ieq(static_cast<unsigned char>(a[i]), static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool is_ws(char c) {
    return c == ' ' || c == '\t';
}

std::string trim(std::string_view s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && is_ws(s[begin])) ++begin;
    while (end > begin && is_ws(s[end - 1])) --end;
    return std::string(s.substr(begin, end - begin));
}

void add_reason(std::vector<std::string>& reasons, const char* reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.emplace_back(reason);
    }
}

// Removes NUL bytes from `line`, returning the cleaned copy. Sets
// `had_nul` when at least one NUL byte was present.
std::string strip_nul(std::string_view line, bool& had_nul) {
    std::string out;
    out.reserve(line.size());
    for (char c : line) {
        if (c == '\0') {
            had_nul = true;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

std::optional<std::string> HeaderInspection::first_value(std::string_view name) const {
    for (const auto& f : fields) {
        if (ascii_ieq(f.name, name)) return f.value;
    }
    return std::nullopt;
}

std::vector<std::string> HeaderInspection::all_values(std::string_view name) const {
    std::vector<std::string> out;
    for (const auto& f : fields) {
        if (ascii_ieq(f.name, name)) out.push_back(f.value);
    }
    return out;
}

std::optional<std::string> HeaderInspection::date() const {
    return first_value("Date");
}

std::optional<std::string> HeaderInspection::message_id() const {
    return first_value("Message-ID");
}

std::vector<std::string> HeaderInspection::received() const {
    return all_values("Received");
}

std::optional<std::string> HeaderInspection::first_received_date_candidate() const {
    auto values = received();
    if (values.empty()) return std::nullopt;
    const std::string& first = values.front();
    auto pos = first.rfind(';');
    if (pos == std::string::npos) return std::nullopt;
    std::string candidate = trim(std::string_view(first).substr(pos + 1));
    if (candidate.empty()) return std::nullopt;
    return candidate;
}

bool HeaderInspection::x_unsent() const {
    auto v = first_value("X-Unsent");
    return v.has_value() && *v == "1";
}

HeaderInspection inspect_headers(std::string_view raw_prefix, size_t cap) {
    HeaderInspection result;

    const size_t window_size = std::min(cap, raw_prefix.size());
    std::string_view window = raw_prefix.substr(0, window_size);

    size_t pos = 0;

    // Strip a UTF-8 BOM at offset 0.
    static constexpr std::string_view kBom = "\xEF\xBB\xBF";
    if (window.size() >= kBom.size() && window.substr(0, kBom.size()) == kBom) {
        add_reason(result.malformed_reasons, "utf8-bom");
        pos = kBom.size();
    }

    bool saw_nul = false;
    bool have_current_field = false;
    size_t current_field_index = 0;

    while (pos < window.size()) {
        // Find the next line terminator (CR, LF, or CRLF) starting at pos.
        size_t content_start = pos;
        size_t scan = pos;
        while (scan < window.size() && window[scan] != '\r' && window[scan] != '\n') {
            ++scan;
        }
        size_t content_end = scan;

        if (scan >= window.size()) {
            // No terminator before the end of the window: an incomplete
            // trailing line. Treat it as ordinary content if non-empty
            // (there is nothing more to read), then stop: no separator was
            // found within the window.
            std::string_view raw_line = window.substr(content_start, content_end - content_start);
            if (!raw_line.empty()) {
                bool line_had_nul = false;
                std::string cleaned = strip_nul(raw_line, line_had_nul);
                if (line_had_nul) {
                    saw_nul = true;
                    add_reason(result.malformed_reasons, "nul-in-header");
                }
                if (!cleaned.empty() && is_ws(cleaned.front())) {
                    if (have_current_field) {
                        std::string cont = trim(cleaned);
                        auto& field = result.fields[current_field_index];
                        if (!field.value.empty() && !cont.empty()) field.value += ' ';
                        field.value += cont;
                    }
                    // Orphan continuation with no preceding field: ignore.
                } else {
                    auto colon = cleaned.find(':');
                    if (colon == std::string::npos) {
                        add_reason(result.malformed_reasons, "line-without-colon");
                        have_current_field = false;
                    } else {
                        HeaderField field;
                        field.name = trim(std::string_view(cleaned).substr(0, colon));
                        field.value = trim(std::string_view(cleaned).substr(colon + 1));
                        result.fields.push_back(std::move(field));
                        current_field_index = result.fields.size() - 1;
                        have_current_field = true;
                    }
                }
            }
            result.separator_found = false;
            result.header_bytes = window.size();
            pos = window.size();
            break;
        }

        size_t next_pos;
        if (window[content_end] == '\r') {
            if (content_end + 1 < window.size() && window[content_end + 1] == '\n') {
                next_pos = content_end + 2;  // CRLF
            } else {
                add_reason(result.malformed_reasons, "bare-cr");
                next_pos = content_end + 1;  // bare CR
            }
        } else {
            next_pos = content_end + 1;  // LF
        }

        std::string_view raw_line = window.substr(content_start, content_end - content_start);

        if (raw_line.empty()) {
            // Blank line: header/body separator.
            result.separator_found = true;
            result.header_bytes = next_pos;
            pos = next_pos;
            break;
        }

        bool line_had_nul = false;
        std::string cleaned = strip_nul(raw_line, line_had_nul);
        if (line_had_nul) {
            saw_nul = true;
            add_reason(result.malformed_reasons, "nul-in-header");
        }

        if (!cleaned.empty() && is_ws(cleaned.front())) {
            // Continuation line: unfold onto the current field.
            if (have_current_field) {
                std::string cont = trim(cleaned);
                auto& field = result.fields[current_field_index];
                if (!field.value.empty() && !cont.empty()) field.value += ' ';
                field.value += cont;
            }
            // Orphan continuation (no preceding field): ignore silently.
        } else {
            auto colon = cleaned.find(':');
            if (colon == std::string::npos) {
                add_reason(result.malformed_reasons, "line-without-colon");
                have_current_field = false;
            } else {
                HeaderField field;
                field.name = trim(std::string_view(cleaned).substr(0, colon));
                field.value = trim(std::string_view(cleaned).substr(colon + 1));
                result.fields.push_back(std::move(field));
                current_field_index = result.fields.size() - 1;
                have_current_field = true;
            }
        }

        pos = next_pos;
    }

    if (!result.separator_found) {
        result.header_bytes = window.size();
    }
    if (saw_nul || !result.malformed_reasons.empty()) {
        // "utf8-bom" alone must not mark the inspection malformed.
        for (const auto& reason : result.malformed_reasons) {
            if (reason != "utf8-bom") {
                result.malformed = true;
                break;
            }
        }
    }

    return result;
}

}  // namespace wlm2pst
