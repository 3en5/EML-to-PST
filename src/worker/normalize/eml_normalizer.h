// WLM2PST - attempt-2 conservative EML repair (spec section 16).
//
// Portable, pure byte-level logic so it is unit-testable off-Windows. Only
// the HEADER SECTION is ever rewritten; body bytes are preserved exactly.
// Allowed repairs: strip a leading UTF-8 BOM, normalize header line endings
// to CRLF, drop NUL bytes in the header section, and insert a missing
// header/body separator only when the boundary is safely identifiable.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace wlm2pst {

struct EmlNormalization {
    // Rewritten header section (already CRLF-terminated, includes the blank
    // separator line). Body starts at original_header_bytes of the ORIGINAL
    // buffer and must be appended verbatim by the caller.
    std::vector<char> normalized_header;
    size_t original_header_bytes = 0;  // bytes of the original consumed as header
    bool changed = false;              // false => normalization has nothing to offer
    bool separator_inserted = false;
    std::vector<std::string> repairs;  // "utf8-bom", "line-endings", "nul", "separator-inserted"
};

// `prefix` should contain at least the whole header section (callers pass up
// to `cap` bytes of the file start; kDefaultHeaderCap from the header
// inspector is a sensible cap). If no separator exists within the prefix, the
// boundary is inferred as the first line that cannot be a header line
// (no ':' and not a continuation); if even that fails, the entire prefix is
// treated as header and a separator is appended.
EmlNormalization normalize_eml_header(std::string_view prefix, size_t cap = 1024 * 1024);

}  // namespace wlm2pst
