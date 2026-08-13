// WLM2PST - preflight check implementation. See preflight.h.
//
// Portable: no <windows.h>. The `#ifndef _WIN32` block at the bottom supplies
// a std::filesystem-backed ISystemProbe for Linux dev/test builds; the real
// Windows implementation lives in preflight_win.cpp.
#include "worker/preflight/preflight.h"

#include <sstream>

#include "common/paths/path_utils.h"
#include "common/unicode/utf.h"

#ifndef _WIN32
#include <filesystem>
#include <system_error>
#endif

namespace wlm2pst {

namespace {

// Lexical (string-only) parent-directory extraction for a Windows-style
// absolute path. Deliberately not std::filesystem: paths here are always
// backslash-separated Windows paths even in Linux test builds, and lexical
// handling avoids any locale-dependent narrow/wide round-trip.
std::wstring parent_directory_of(std::wstring_view path) {
    std::wstring normalized = normalize_backslashes(std::wstring(path));
    while (!normalized.empty() && normalized.back() == L'\\') {
        normalized.pop_back();
    }
    size_t pos = normalized.find_last_of(L'\\');
    if (pos == std::wstring::npos) {
        return L"";
    }
    std::wstring parent = normalized.substr(0, pos);
    // "C:" (drive letter with no separator left) needs its root slash back.
    if (parent.size() == 2 && parent[1] == L':') {
        parent += L'\\';
    }
    return parent;
}

std::string format_bytes_for_message(uint64_t bytes) {
    return std::to_string(bytes) + " bytes";
}

}  // namespace

uint64_t estimate_pst_bytes(uint64_t total_source_bytes) noexcept {
    return total_source_bytes + (total_source_bytes * 3 / 10) + kGiB;
}

uint64_t required_free_bytes_for(uint64_t estimated_pst_bytes) noexcept {
    return estimated_pst_bytes + 2 * kGiB;
}

bool exceeds_pst_size_ceiling(uint64_t estimated_pst_bytes) noexcept {
    return estimated_pst_bytes > kPstSizeCeilingBytes;
}

PreflightReport build_preflight_report(const CliOptions& options, const ScanResult& scan,
                                        uint64_t available_free_bytes) {
    PreflightReport report;
    report.eml_count = scan.entries.size();
    report.folder_count = scan.stats.folder_count;
    report.total_bytes = scan.stats.total_bytes;
    report.estimated_pst_bytes = estimate_pst_bytes(report.total_bytes);
    report.required_free_bytes = required_free_bytes_for(report.estimated_pst_bytes);
    report.available_free_bytes = available_free_bytes;
    (void)options;
    return report;
}

std::optional<CliValidationError> run_preflight_checks(const CliOptions& options,
                                                         const ScanResult& scan, ISystemProbe& probe) {
    // --- Source validation ---
    if (!probe.directory_exists(options.source)) {
        return CliValidationError{ExitCode::kInvalidSource, "source directory does not exist"};
    }
    if (scan.entries.empty()) {
        return CliValidationError{ExitCode::kInvalidSource,
                                   "no .eml files were found in the source directory"};
    }
    if (probe.is_process_running(L"wlmail.exe")) {
        return CliValidationError{
            ExitCode::kInvalidSource,
            "Windows Live Mail (wlmail.exe) is running; close it so the source stays stable "
            "while converting"};
    }

    // --- Output location validation ---
    std::wstring output_parent = parent_directory_of(options.output);
    if (output_parent.empty() || !probe.directory_exists(output_parent)) {
        return CliValidationError{ExitCode::kInvalidArguments,
                                   "output directory does not exist"};
    }

    Result<bool> local_fixed = probe.is_on_local_fixed_drive(options.output);
    if (!local_fixed.ok()) {
        return CliValidationError{ExitCode::kInvalidArguments,
                                   "could not determine whether the output path is on a local "
                                   "fixed drive: " + local_fixed.error().message};
    }
    if (!local_fixed.value()) {
        return CliValidationError{ExitCode::kInvalidArguments,
                                   "output path must be on a local fixed drive"};
    }

    // --- Output file existence rules ---
    if (probe.file_exists(options.output)) {
        if (!options.overwrite && !options.resume) {
            return CliValidationError{
                ExitCode::kOutputExists,
                "output file already exists; pass --overwrite or --resume"};
        }

        Result<bool> locked = probe.is_file_locked(options.output);
        if (!locked.ok()) {
            return CliValidationError{
                ExitCode::kOutputExists,
                "could not determine whether the output file is open by another process: " +
                    locked.error().message};
        }
        if (locked.value()) {
            return CliValidationError{ExitCode::kOutputExists,
                                       "output file is open by another process"};
        }
    }

    // --- Free space check ---
    uint64_t estimated = estimate_pst_bytes(scan.stats.total_bytes);
    uint64_t required = required_free_bytes_for(estimated);

    Result<uint64_t> available = probe.available_free_bytes(output_parent);
    if (!available.ok()) {
        return CliValidationError{
            ExitCode::kInsufficientDiskSpace,
            "could not determine available free disk space: " + available.error().message};
    }
    if (available.value() < required) {
        std::ostringstream message;
        message << "insufficient free disk space: required " << format_bytes_for_message(required)
                << ", available " << format_bytes_for_message(available.value());
        return CliValidationError{ExitCode::kInsufficientDiskSpace, message.str()};
    }

    // --- Safe single-PST size ceiling ---
    if (exceeds_pst_size_ceiling(estimated)) {
        return CliValidationError{
            ExitCode::kPstSizeCeilingExceeded,
            "estimated output size exceeds the safe single-PST ceiling (45 GiB); automatic PST "
            "splitting is intentionally unsupported"};
    }

    return std::nullopt;
}

#ifndef _WIN32

namespace {

namespace fs = std::filesystem;

// Interprets a wide path as UTF-8 (never the "C" locale / ANSI code page),
// matching worker/scanner's convention for talking to std::filesystem.
fs::path to_fs_path(std::wstring_view w) {
    std::string utf8 = utf8_from_wide(w);
    std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    return fs::path(u8);
}

// Dev/test-only fallback used off-Windows: real filesystem existence and
// free-space queries, but process/lock checks conservatively report "no
// problem" since there is no Windows process list or share-mode API here.
class PortableSystemProbe : public ISystemProbe {
public:
    bool directory_exists(const std::wstring& path) override {
        std::error_code ec;
        return fs::is_directory(to_fs_path(path), ec) && !ec;
    }

    bool file_exists(const std::wstring& path) override {
        std::error_code ec;
        return fs::is_regular_file(to_fs_path(path), ec) && !ec;
    }

    Result<uint64_t> available_free_bytes(const std::wstring& directory) override {
        std::error_code ec;
        fs::space_info info = fs::space(to_fs_path(directory), ec);
        if (ec) {
            return make_error("std::filesystem::space", "failed to query available disk space");
        }
        return static_cast<uint64_t>(info.available);
    }

    Result<bool> is_on_local_fixed_drive(const std::wstring& /*path*/) override { return true; }

    bool is_process_running(const std::wstring& /*exe_name_lower*/) override { return false; }

    Result<bool> is_file_locked(const std::wstring& /*path*/) override { return false; }
};

}  // namespace

std::unique_ptr<ISystemProbe> make_system_probe() {
    return std::make_unique<PortableSystemProbe>();
}

#endif  // !_WIN32

}  // namespace wlm2pst
