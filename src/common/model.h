// WLM2PST - shared data model used across scanner, resume, reporting,
// import pipeline, and validation. Portable: no Windows headers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wlm2pst {

// UTC timestamp in FILETIME semantics: 100-nanosecond intervals since
// 1601-01-01T00:00:00Z. Chosen because it round-trips Windows file times and
// MAPI PT_SYSTIME exactly; helpers convert to/from unix time where needed.
using FileTimeUtc = int64_t;

constexpr int64_t kFiletimeTicksPerSecond = 10'000'000;
constexpr int64_t kFiletimeUnixEpoch = 116444736000000000;  // 1970-01-01 in FILETIME ticks.

constexpr FileTimeUtc filetime_from_unix_seconds(int64_t unix_seconds) noexcept {
    return unix_seconds * kFiletimeTicksPerSecond + kFiletimeUnixEpoch;
}
constexpr int64_t unix_seconds_from_filetime(FileTimeUtc ft) noexcept {
    return (ft - kFiletimeUnixEpoch) / kFiletimeTicksPerSecond;
}

// One EML file discovered by the scanner. Paths use native wide strings;
// relative paths are normalized with '\\' separators and no leading separator.
struct ManifestEntry {
    std::wstring relative_path;   // e.g. L"account@example.com\\Inbox\\001.eml"
    std::wstring absolute_path;   // full native path, long-path capable
    uint64_t size = 0;            // bytes
    FileTimeUtc last_write_utc = 0;
};

// Per-file lifecycle states persisted in the resume database (spec section 17).
enum class FileImportStatus {
    kPending,
    kImported,
    kPreservedAsAttachment,
    kFailedSourceRead,
    kFailedMapi,
};

const char* to_string(FileImportStatus status) noexcept;
// Returns false for unknown text (forward-compat safe parsing of DB values).
bool file_import_status_from_string(const std::string& text, FileImportStatus& out) noexcept;

// Final validation result (spec section 20).
enum class ValidationStatus {
    kValidated,
    kValidatedWithFallbacks,
    kValidationFailed,
};

const char* to_string(ValidationStatus status) noexcept;

// A folder rename applied during normalization (spec section 9); reported.
struct RenamedFolder {
    std::wstring source_relative_folder;
    std::wstring target_relative_folder;
    std::string reason;  // machine-readable, e.g. "trailing-dot", "collision", "too-long"
};

// Aggregate counters for the final summary and JSON report.
struct JobCounters {
    uint64_t source_files = 0;
    uint64_t source_folders = 0;
    uint64_t source_bytes = 0;
    uint64_t imported_normally = 0;
    uint64_t preserved_as_attachment = 0;
    uint64_t failed_to_read = 0;
    uint64_t output_folders = 0;
};

}  // namespace wlm2pst
