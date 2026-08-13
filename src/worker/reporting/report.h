// WLM2PST - machine-readable JSON job report (spec section 21).
//
// Portable: no Windows headers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/errors/result.h"
#include "common/model.h"

namespace wlm2pst {

// Everything needed to render the JSON report. Field names mirror the report
// schema documented in spec section 21; `warnings` must contain only
// privacy-safe strings (see docs/dev-conventions.md privacy rules).
struct ReportData {
    std::string tool_version;
    std::string run_id;
    std::wstring source;
    std::wstring output;
    std::string started_at_utc_iso;    // "2026-08-13T08:12:44Z"
    std::string completed_at_utc_iso;  // "2026-08-13T09:03:18Z"
    JobCounters counters;
    std::string validation_status;  // to_string(ValidationStatus)
    std::vector<RenamedFolder> renamed_folders;
    std::vector<std::string> warnings;  // privacy-safe strings only
};

// Writes the JSON report to `path` (UTF-8, no BOM, trailing newline). `path`
// is a native wide path; a std::filesystem::path is constructed from it.
Status write_json_report(const std::wstring& path, const ReportData& data);

// Formats a FileTimeUtc as "YYYY-MM-DDTHH:MM:SSZ" (UTC, seconds precision,
// truncating any sub-second remainder).
std::string iso8601_from_filetime(FileTimeUtc time);

}  // namespace wlm2pst
