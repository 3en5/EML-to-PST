// WLM2PST - UTF-8 <-> wide string conversions and ordinal comparisons.
//
// Hand-written (no codecvt, no WideCharToMultiByte/MultiByteToWideChar) so
// behavior is byte-for-byte identical on Windows (2-byte UTF-16 wchar_t) and
// Linux test builds (4-byte UTF-32 wchar_t). Never throws; malformed input
// (invalid UTF-8 byte sequences, unpaired UTF-16 surrogates, out-of-range
// code points) is replaced with U+FFFD.
#pragma once

#include <string>
#include <string_view>

namespace wlm2pst {

// Converts a wide string (UTF-16 on Windows, UTF-32 on Linux test builds) to
// UTF-8. Unpaired surrogates and out-of-range values become U+FFFD.
std::string utf8_from_wide(std::wstring_view wide);

// Converts a UTF-8 string to a wide string (UTF-16 on Windows, UTF-32 on
// Linux test builds). Malformed UTF-8 (bad continuation bytes, overlong
// encodings, out-of-range code points, encoded surrogate values) becomes
// U+FFFD.
std::wstring wide_from_utf8(std::string_view utf8);

// Ordinal (code-unit-by-code-unit) comparison, analogous to Windows'
// CompareStringOrdinal with no case folding. Returns <0, 0, or >0.
int compare_ordinal(std::wstring_view a, std::wstring_view b) noexcept;

// Ordinal comparison after simple invariant case folding: only ASCII 'a'-'z'
// is uppercased; every other code unit passes through unchanged. This is
// NOT a full Unicode case fold - real Windows code should use
// CompareStringOrdinal with the ignore-case flag for correct handling of
// non-ASCII casing (e.g. Hebrew has no case, but Latin-1 accented letters
// do). This helper is intended for the ASCII-ish tokens WLM2PST compares
// case-insensitively (file extensions, drive letters, UNC/extended-length
// path prefixes).
int compare_ordinal_ignore_case(std::wstring_view a, std::wstring_view b) noexcept;

}  // namespace wlm2pst
