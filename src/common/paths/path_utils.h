// WLM2PST - lexical path helpers for Windows-style paths.
//
// The portable functions below are pure lexical operations (no filesystem
// access) so they can be exercised on Linux with Windows-style input paths.
// The Windows-only functions (implemented in path_utils_win.cpp) query the
// real filesystem/volume and are declared here so callers include a single
// header; their bodies hide <windows.h> from portable translation units.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "common/errors/result.h"

namespace wlm2pst {

// True for \\server\share... and \\?\UNC\server\share... forms.
bool is_unc_path(std::wstring_view path) noexcept;

// True for the \\?\ extended-length prefix (covers \\?\UNC\ too).
bool is_extended_length_path(std::wstring_view path) noexcept;

// Prefixes a drive-absolute path ("C:\...") with \\?\ and a UNC path
// ("\\server\share...") with \\?\UNC\. Idempotent: already-extended paths
// are returned unchanged. Paths that are neither drive-absolute nor UNC
// (relative paths, etc.) are returned unchanged - they cannot be losslessly
// extended without first resolving them to an absolute path.
std::wstring to_extended_length_path(std::wstring path);

// Case-insensitive check for a trailing ".pst".
bool has_pst_extension(std::wstring_view path) noexcept;

// Converts '/' to '\\' and collapses runs of consecutive backslashes to a
// single backslash, except for the mandatory leading "\\" that marks a UNC
// or \\?\ extended-length path (that leading pair is preserved as-is; any
// extra backslashes immediately following it are still collapsed away).
std::wstring normalize_backslashes(std::wstring path);

// Case-insensitive, component-wise containment check: true when `candidate`
// is `base` itself or lexically nested inside it. Both inputs are assumed to
// be absolute Windows paths; a \\?\ (and \\?\UNC\) prefix is stripped before
// comparing, and trailing separators are tolerated. Comparison is by path
// component, not by character prefix, so "C:\a\bc" is NOT considered within
// "C:\a\b".
bool is_lexically_within(std::wstring_view base, std::wstring_view candidate);

// Returns the drive letter (e.g. L'C') for "C:..." and "\\?\C:..." forms,
// or nullopt for UNC paths, relative paths, or anything else.
std::optional<wchar_t> drive_letter_of(std::wstring_view path) noexcept;

// --- Windows-only (path_utils_win.cpp); declared here for a single header. ---

// True when `absolute_path` resolves to a local fixed drive (GetDriveTypeW
// == DRIVE_FIXED). Remote/network shares (DRIVE_REMOTE, including UNC
// paths), removable/optical/RAM/unknown drives all yield `false`, not an
// error - callers decide how to report the rejection. An Error is only
// returned when `absolute_path` is neither drive-absolute nor UNC, so no
// volume root can be derived.
Result<bool> is_on_local_fixed_drive(const std::wstring& absolute_path);

// Free bytes available to the current user on the volume containing
// `directory` (GetDiskFreeSpaceExW).
Result<uint64_t> available_free_bytes(const std::wstring& directory);

}  // namespace wlm2pst
