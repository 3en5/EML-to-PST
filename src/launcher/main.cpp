// WLM2PST - launcher entry point (spec section 4). Deliberately thin: all
// detection/selection logic lives in the portable wlm2pst_launcher_core
// library so it stays unit-testable; this file only wires real Windows
// resources to it and reports outcomes.
#include "common/win_compat.h"

#include "common/exit_codes.h"
#include "common/version/version.h"
#include "launcher/outlook_detector.h"
#include "launcher/registry_reader.h"
#include "launcher/worker_launcher.h"

namespace {

using wlm2pst::ExitCode;

// Writes a line to a console/redirected handle as UTF-8, regardless of
// whether the destination is a real console, a file, or a pipe.
void write_line(HANDLE handle, const std::wstring& text) {
    std::wstring line = text + L"\r\n";
    int needed = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return;
    }
    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), utf8.data(),
                         needed, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void write_out(const std::wstring& text) { write_line(GetStdHandle(STD_OUTPUT_HANDLE), text); }
void write_err(const std::wstring& text) { write_line(GetStdHandle(STD_ERROR_HANDLE), text); }

bool has_flag(int argc, wchar_t* argv[], const wchar_t* long_form, const wchar_t* short_form) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == std::wstring(long_form) ||
            (short_form != nullptr && argv[i] == std::wstring(short_form))) {
            return true;
        }
    }
    return false;
}

void print_help() {
    write_out(L"WLM2PST - Windows Live Mail EML tree to Unicode PST");
    write_out(L"");
    write_out(L"Usage:");
    write_out(L"  wlm2pst.exe --source <directory> --output <file.pst> [options]");
    write_out(L"");
    write_out(L"Required:");
    write_out(L"  --source <directory>   Recursive source folder containing EML files.");
    write_out(L"  --output <file.pst>    Path to a new PST file.");
    write_out(L"");
    write_out(L"Options:");
    write_out(L"  --root-name <name>     Root folder name inside the PST.");
    write_out(L"                         Default: Windows Live Mail");
    write_out(L"  --resume               Resume a compatible interrupted job.");
    write_out(L"  --overwrite            Delete a previous tool-owned output and start again.");
    write_out(L"  --quiet                Show only errors and final summary.");
    write_out(L"  --verbose              Show additional technical progress (no message content).");
    write_out(L"  --help                 Show this help.");
    write_out(L"  --version              Show application, worker, architecture, and build versions.");
    write_out(L"");
    write_out(L"Example:");
    write_out(
        L"  wlm2pst.exe --source \"C:\\OldMail\" --output \"D:\\Migration\\OldMail.pst\"");
}

void print_version() {
    std::wstring line = std::wstring(L"WLM2PST ") + wlm2pst::kToolVersionW + L" launcher (" +
                         wlm2pst::kBuildArchW + L")";
    write_out(line);
}

// Real %LOCALAPPDATA%\Microsoft\WindowsApps\olk.exe candidate for the New
// Outlook (appx) heuristic (spec section 4). Portable detection code cannot
// build this itself (no environment access), so it is assembled here.
std::vector<std::wstring> build_new_outlook_candidates() {
    std::vector<std::wstring> candidates;
    DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (needed == 0) {
        return candidates;
    }
    std::wstring local_appdata(needed, L'\0');
    DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", local_appdata.data(), needed);
    if (written == 0 || written >= needed) {
        return candidates;
    }
    local_appdata.resize(written);
    candidates.push_back(local_appdata + L"\\Microsoft\\WindowsApps\\olk.exe");
    return candidates;
}

std::wstring launcher_directory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) {
            return {};
        }
        if (len < path.size()) {
            path.resize(len);
            break;
        }
        // Truncated: grow and retry (long install paths).
        path.resize(path.size() * 2);
    }
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    return path.substr(0, slash);
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (has_flag(argc, argv, L"--help", L"-h")) {
        print_help();
        return static_cast<int>(ExitCode::kSuccess);
    }
    if (has_flag(argc, argv, L"--version", nullptr)) {
        print_version();
        return static_cast<int>(ExitCode::kSuccess);
    }

    auto registry = wlm2pst::make_registry_reader();
    auto files = wlm2pst::make_file_probe();

    wlm2pst::OutlookDetection detection =
        wlm2pst::detect_outlook(*registry, *files, build_new_outlook_candidates());

    if (detection.new_outlook_only) {
        write_err(
            L"Only the new Outlook app was found. WLM2PST requires classic Outlook with "
            L"Extended MAPI support; please install or switch back to classic Outlook.");
        return static_cast<int>(ExitCode::kOutlookUnavailable);
    }
    if (!detection.classic_installed) {
        write_err(
            L"Classic Outlook could not be found. WLM2PST requires a classic Outlook install "
            L"with Extended MAPI support.");
        return static_cast<int>(ExitCode::kOutlookUnavailable);
    }
    if (detection.bitness == wlm2pst::OutlookBitness::kUnknown) {
        write_err(
            L"Classic Outlook was found but its architecture (x86/x64) could not be "
            L"determined; cannot select a matching worker.");
        return static_cast<int>(ExitCode::kOutlookUnavailable);
    }

    std::wstring worker_name = wlm2pst::worker_exe_name(detection.bitness);
    std::wstring dir = launcher_directory();
    std::wstring worker_full_path = dir.empty() ? worker_name : dir + L"\\" + worker_name;

    if (!files->file_exists(worker_full_path)) {
        write_err(L"Matching worker unavailable: " + worker_name);
        return static_cast<int>(ExitCode::kBitnessMismatch);
    }

    std::wstring tail = wlm2pst::command_tail(GetCommandLineW());
    wlm2pst::Result<int> run_result = wlm2pst::run_worker(worker_full_path, tail);
    if (!run_result.ok()) {
        write_err(L"Failed to start worker process: " +
                  std::wstring(run_result.error().operation.begin(),
                               run_result.error().operation.end()));
        return static_cast<int>(ExitCode::kBitnessMismatch);
    }

    return run_result.value();
}
