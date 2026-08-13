// WLM2PST - lightweight, defensive RFC-style header inspector.
//
// This is NOT a MIME parser. It only reads the header section of an EML file
// (bounded by `cap` bytes) to answer specific metadata questions used for
// draft/sent detection, date fallback, and diagnostics (spec section 11). It
// never decodes or rewrites the message body, and never allocates
// proportionally to body size: callers should pass only a bounded prefix of
// the file.
//
// Portable: no Windows headers.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wlm2pst {

// One raw header field as it appears in the file, with continuation lines
// unfolded (leading SP/TAB on the next line joins to the previous value with
// a single space) and surrounding whitespace trimmed from the final value.
// Values are not otherwise decoded (no RFC 2047, no charset conversion).
struct HeaderField {
    std::string name;
    std::string value;
};

// Result of inspecting the header section of one message.
struct HeaderInspection {
    // True when a blank line (the header/body separator) was found within
    // the inspected window.
    bool separator_found = false;
    // Bytes consumed up to and including the separator's line terminator,
    // or, when no separator was found, the number of bytes actually
    // examined (min(cap, input size)).
    size_t header_bytes = 0;
    // True when at least one condition below (excluding a lone UTF-8 BOM)
    // was observed.
    bool malformed = false;
    // Machine-readable, deduplicated reason codes, e.g. "nul-in-header",
    // "line-without-colon", "bare-cr", "utf8-bom".
    std::vector<std::string> malformed_reasons;
    // All parsed fields, in file order.
    std::vector<HeaderField> fields;

    // Case-insensitive lookup of the first field named `name`.
    [[nodiscard]] std::optional<std::string> first_value(std::string_view name) const;
    // Case-insensitive lookup of every field named `name`, in file order.
    [[nodiscard]] std::vector<std::string> all_values(std::string_view name) const;

    // Convenience accessors for the fields used by the worker pipeline.
    [[nodiscard]] std::optional<std::string> date() const;
    [[nodiscard]] std::optional<std::string> message_id() const;
    // All Received: header values, in file order (topmost/first-received-by
    // header first).
    [[nodiscard]] std::vector<std::string> received() const;
    // Timestamp candidate taken from the FIRST (topmost) Received header:
    // the raw text after the LAST ';' in that header, trimmed. nullopt when
    // there is no Received header, or that header has no ';', or the text
    // after the last ';' is empty once trimmed.
    [[nodiscard]] std::optional<std::string> first_received_date_candidate() const;
    // True when X-Unsent's (trimmed) value is exactly "1".
    [[nodiscard]] bool x_unsent() const;
};

// Safety limit on how many bytes of `raw_prefix` are ever examined.
inline constexpr size_t kDefaultHeaderCap = 1024 * 1024;  // 1 MiB.

// Inspects the header section found at the start of `raw_prefix`, examining
// at most `cap` bytes. `raw_prefix` need not contain the whole file; callers
// should pass a bounded prefix so this function never touches (or is even
// given) the message body.
HeaderInspection inspect_headers(std::string_view raw_prefix, size_t cap = kDefaultHeaderCap);

}  // namespace wlm2pst
