// WLM2PST - preflight checks before creating/modifying a PST (spec section 6).
//
// Portable core: all decision logic runs against the injectable ISystemProbe
// interface so it is fully testable off-Windows. The real probe
// (preflight_win.cpp) talks to the filesystem, volume queries, the process
// list, and file-share locks; a portable fallback (guarded #ifndef _WIN32,
// in preflight.cpp) backs Linux dev/test builds with std::filesystem.
//
// NOTE: resume-specific and output-ownership checks are NOT performed here -
// they belong to the app layer, which owns the resume state database.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "common/errors/result.h"
#include "worker/cli/cli_options.h"
#include "worker/scanner/scanner.h"

namespace wlm2pst {

class ISystemProbe {
public:
    virtual ~ISystemProbe() = default;

    virtual bool directory_exists(const std::wstring& path) = 0;
    virtual bool file_exists(const std::wstring& path) = 0;
    virtual Result<uint64_t> available_free_bytes(const std::wstring& directory) = 0;
    virtual Result<bool> is_on_local_fixed_drive(const std::wstring& path) = 0;
    // `exe_name_lower` is already lowercased, e.g. L"wlmail.exe".
    virtual bool is_process_running(const std::wstring& exe_name_lower) = 0;
    // Open-for-read with no sharing; a sharing violation means "locked".
    // A file that does not exist is never locked (returns false).
    virtual Result<bool> is_file_locked(const std::wstring& path) = 0;
};

// Real implementation on Windows (preflight_win.cpp); a std::filesystem-based
// fallback for Linux dev/test builds (#ifndef _WIN32 in preflight.cpp) where
// process/lock checks are stubbed to their conservative "no problem" answer.
std::unique_ptr<ISystemProbe> make_system_probe();

struct PreflightInput {
    CliOptions options;
};

struct PreflightReport {
    uint64_t eml_count = 0;
    uint64_t folder_count = 0;
    uint64_t total_bytes = 0;
    uint64_t estimated_pst_bytes = 0;
    uint64_t required_free_bytes = 0;
    uint64_t available_free_bytes = 0;
};

constexpr uint64_t kGiB = 1024ull * 1024ull * 1024ull;
// Spec section 6: reject a job whose estimated output exceeds this ceiling
// (automatic PST splitting is intentionally unsupported).
constexpr uint64_t kPstSizeCeilingBytes = 45ull * kGiB;

// estimated_pst_bytes = total_eml_bytes * 1.30 + 1 GiB, computed with exact
// 64-bit integer arithmetic (no floating point): total + total*3/10 + 1 GiB.
uint64_t estimate_pst_bytes(uint64_t total_source_bytes) noexcept;

// required_free_bytes = estimated_pst_bytes + 2 GiB.
uint64_t required_free_bytes_for(uint64_t estimated_pst_bytes) noexcept;

// True when `estimated_pst_bytes` exceeds the safe single-PST size ceiling
// (spec section 6): estimated_pst_bytes > 45 GiB.
bool exceeds_pst_size_ceiling(uint64_t estimated_pst_bytes) noexcept;

// Builds the preflight summary (spec section 6) from an already-completed
// scan and a probed available-free-bytes figure. Pure arithmetic; does not
// touch the probe itself so free-space math stays independently testable.
PreflightReport build_preflight_report(const CliOptions& options, const ScanResult& scan,
                                        uint64_t available_free_bytes);

// Runs the ordered preflight checks from spec section 6 (excluding Outlook/
// MAPI validation and resume/overwrite ownership, which are the app layer's
// responsibility). Uses an already-completed scan for EML count / folder
// count / total bytes rather than re-scanning. Returns the first failure
// encountered, or nullopt if every check passes.
std::optional<CliValidationError> run_preflight_checks(const CliOptions& options,
                                                         const ScanResult& scan, ISystemProbe& probe);

}  // namespace wlm2pst
