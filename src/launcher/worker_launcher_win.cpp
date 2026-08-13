// WLM2PST - actual worker process spawn. Windows-only translation unit.
#include "common/win_compat.h"
#include "launcher/worker_launcher.h"

namespace wlm2pst {

namespace {

// Returning TRUE tells the console subsystem this handler "handled" the
// event, i.e. the launcher itself ignores it. The worker shares the same
// console (CreateProcessW below does not detach it) and receives the same
// Ctrl+C / Ctrl+Break notification directly, so it can shut down cleanly
// while the launcher simply keeps waiting on it.
BOOL WINAPI ignore_ctrl_handler(DWORD /*ctrl_type*/) { return TRUE; }

}  // namespace

Result<int> run_worker(const std::wstring& worker_full_path,
                        const std::wstring& original_command_tail) {
    std::wstring command_line = L"\"" + worker_full_path + L"\"";
    if (!original_command_tail.empty()) {
        command_line += L" " + original_command_tail;
    }

    // Best-effort: even if this fails, the worst case is the launcher also
    // reacts to Ctrl+C directly instead of leaving the wait to the child.
    SetConsoleCtrlHandler(ignore_ctrl_handler, TRUE);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL created = CreateProcessW(worker_full_path.c_str(),
                                   command_line.data(),  // mutable buffer, per CreateProcessW contract
                                   nullptr, nullptr,
                                   TRUE,  // inherit handles so the console (and Ctrl+C) is shared
                                   0, nullptr, nullptr, &si, &pi);
    if (!created) {
        return make_win32_error(GetLastError(), "CreateProcessW", "failed to start worker process");
    }

    CloseHandle(pi.hThread);

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    BOOL got_exit_code = GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);

    if (!got_exit_code) {
        return make_win32_error(GetLastError(), "GetExitCodeProcess",
                                 "failed to retrieve worker exit code");
    }

    return static_cast<int>(exit_code);
}

}  // namespace wlm2pst
