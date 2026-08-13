#include "common/unicode/utf.h"

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace wlm2pst {

namespace {

constexpr char32_t kReplacementChar = 0xFFFD;

bool is_high_surrogate(uint32_t u) noexcept { return u >= 0xD800 && u <= 0xDBFF; }
bool is_low_surrogate(uint32_t u) noexcept { return u >= 0xDC00 && u <= 0xDFFF; }
bool is_surrogate(uint32_t u) noexcept { return u >= 0xD800 && u <= 0xDFFF; }

// Widens a wchar_t to an unsigned 32-bit value without sign-extending
// negative values some platforms give wchar_t (it is signed on Linux).
uint32_t widen(wchar_t c) noexcept {
    return static_cast<uint32_t>(static_cast<std::make_unsigned_t<wchar_t>>(c));
}

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

void append_wide(std::wstring& out, char32_t cp) {
    if (cp > 0x10FFFF || is_surrogate(cp)) {
        cp = kReplacementChar;
    }
    if constexpr (sizeof(wchar_t) == 2) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            char32_t v = cp - 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (v & 0x3FF)));
        }
    } else {
        out.push_back(static_cast<wchar_t>(cp));
    }
}

}  // namespace

std::string utf8_from_wide(std::wstring_view wide) {
    std::string out;
    out.reserve(wide.size());

    if constexpr (sizeof(wchar_t) == 2) {
        // UTF-16: decode surrogate pairs into a single code point.
        for (size_t i = 0; i < wide.size(); ++i) {
            uint32_t unit = widen(wide[i]);
            if (is_high_surrogate(unit)) {
                if (i + 1 < wide.size()) {
                    uint32_t low = widen(wide[i + 1]);
                    if (is_low_surrogate(low)) {
                        char32_t cp = static_cast<char32_t>(
                            0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                        append_utf8(out, cp);
                        ++i;
                        continue;
                    }
                }
                append_utf8(out, kReplacementChar);
            } else if (is_low_surrogate(unit)) {
                append_utf8(out, kReplacementChar);
            } else {
                append_utf8(out, static_cast<char32_t>(unit));
            }
        }
    } else {
        // UTF-32: each unit is already a code point.
        for (wchar_t wc : wide) {
            uint32_t unit = widen(wc);
            if (unit > 0x10FFFF || is_surrogate(unit)) {
                append_utf8(out, kReplacementChar);
            } else {
                append_utf8(out, static_cast<char32_t>(unit));
            }
        }
    }
    return out;
}

std::wstring wide_from_utf8(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());

    size_t i = 0;
    const size_t n = utf8.size();
    while (i < n) {
        uint8_t b0 = static_cast<uint8_t>(utf8[i]);
        if (b0 < 0x80) {
            append_wide(out, b0);
            ++i;
            continue;
        }

        int extra = 0;
        char32_t cp = 0;
        uint32_t min_cp = 0;
        if ((b0 & 0xE0) == 0xC0) {
            extra = 1;
            cp = b0 & 0x1F;
            min_cp = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            extra = 2;
            cp = b0 & 0x0F;
            min_cp = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            extra = 3;
            cp = b0 & 0x07;
            min_cp = 0x10000;
        } else {
            // Stray continuation byte or an invalid leading byte.
            append_wide(out, kReplacementChar);
            ++i;
            continue;
        }

        // "Maximal subpart" error recovery (matches common practice in
        // WHATWG/ICU/Rust decoders): consume the leading byte plus every
        // continuation byte that is actually present and well-formed, then
        // treat the whole consumed run as ONE malformed unit -> ONE U+FFFD,
        // rather than re-examining already-consumed bytes as fresh input.
        // This keeps e.g. a truncated 3-byte sequence, an overlong
        // encoding, or a UTF-8-encoded surrogate from producing multiple
        // replacement characters for what is clearly a single bad sequence.
        size_t consumed = 1;
        char32_t value = cp;
        bool format_ok = true;
        for (int k = 1; k <= extra; ++k) {
            if (i + static_cast<size_t>(k) >= n) {
                format_ok = false;  // ran out of input before the sequence completed
                break;
            }
            uint8_t bk = static_cast<uint8_t>(utf8[i + static_cast<size_t>(k)]);
            if ((bk & 0xC0) != 0x80) {
                format_ok = false;  // next byte doesn't continue this sequence
                break;
            }
            value = (value << 6) | (bk & 0x3F);
            ++consumed;
        }

        if (!format_ok) {
            append_wide(out, kReplacementChar);
            i += consumed;
            continue;
        }

        if (value < min_cp || value > 0x10FFFF || is_surrogate(value)) {
            // Well-formed continuation bytes, but the resulting value is an
            // overlong encoding, out of range, or an encoded surrogate.
            append_wide(out, kReplacementChar);
            i += consumed;
            continue;
        }

        append_wide(out, value);
        i += consumed;
    }
    return out;
}

int compare_ordinal(std::wstring_view a, std::wstring_view b) noexcept {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        uint32_t ua = widen(a[i]);
        uint32_t ub = widen(b[i]);
        if (ua != ub) {
            return ua < ub ? -1 : 1;
        }
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

namespace {

wchar_t fold_ascii_upper(wchar_t c) noexcept {
    uint32_t u = widen(c);
    if (u >= static_cast<uint32_t>(L'a') && u <= static_cast<uint32_t>(L'z')) {
        return static_cast<wchar_t>(u - (static_cast<uint32_t>(L'a') - static_cast<uint32_t>(L'A')));
    }
    return c;
}

}  // namespace

int compare_ordinal_ignore_case(std::wstring_view a, std::wstring_view b) noexcept {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        uint32_t ua = widen(fold_ascii_upper(a[i]));
        uint32_t ub = widen(fold_ascii_upper(b[i]));
        if (ua != ub) {
            return ua < ub ? -1 : 1;
        }
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

}  // namespace wlm2pst
