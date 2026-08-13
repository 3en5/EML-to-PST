// WLM2PST - Windows-only ISystemProbe implementation (spec section 6).
// Excluded from non-Windows builds by the `_win.cpp` filename suffix.
#include "common/win_compat.h"
#include "worker/preflight/preflight.h"

#include <tlhelp32.h>

#include "common/paths/path_utils.h"
#include "common/unicode/utf.h"

namespace wlm2pst {

namespace {

std::wstring lowercase_ascii(std::wstring s) {
    for (auto& c : s) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return s;
}

class WindowsSystemProbe : public ISystemProbe {
public:
    bool directory_exists(const std::wstring& path) override {
        const DWORD attrs = ::GetFileAttributesW(to_extended_length_path(path).c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool file_exists(const std::wstring& path) override {
        const DWORD attrs = ::GetFileAttributesW(to_extended_length_path(path).c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    Result<uint64_t> available_free_bytes(const std::wstring& directory) override {
        return wlm2pst::available_free_bytes(directory);
    }

    Result<bool> is_on_local_fixed_drive(const std::wstring& path) override {
        return wlm2pst::is_on_local_fixed_drive(path);
    }

    bool is_process_running(const std::wstring& exe_name_lower) override {
        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            // Cannot enumerate processes; report "not found" rather than
            // blocking the run on an unrelated system failure.
            return false;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (::Process32FirstW(snapshot, &entry)) {
            do {
                if (lowercase_ascii(entry.szExeFile) == exe_name_lower) {
                    found = true;
                    break;
                }
            } while (::Process32NextW(snapshot, &entry));
        }
        ::CloseHandle(snapshot);
        return found;
    }

    Result<bool> is_file_locked(const std::wstring& path) override {
        std::wstring extended = to_extended_length_path(path);
        const DWORD attrs = ::GetFileAttributesW(extended.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            // File does not exist (or is otherwise unreachable): never
            // "locked" - callers only ask this after confirming existence.
            return false;
        }

        HANDLE handle = ::CreateFileW(extended.c_str(), GENERIC_READ,
                                       0,  // no sharing: a locked file fails to open here
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
            return false;
        }

        const DWORD error = ::GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
            return true;
        }
        return make_win32_error(error, "CreateFileW", "failed to probe output file lock state");
    }
};

}  // namespace

std::unique_ptr<ISystemProbe> make_system_probe() {
    return std::make_unique<WindowsSystemProbe>();
}

}  // namespace wlm2pst
