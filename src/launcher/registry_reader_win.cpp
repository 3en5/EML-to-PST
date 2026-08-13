// WLM2PST - real Win32-backed IRegistryReader / IFileProbe implementations.
// Windows-only translation unit (see docs/dev-conventions.md).
#include "common/win_compat.h"
#include "launcher/registry_reader.h"

#include <algorithm>

namespace wlm2pst {

namespace {

HKEY to_hkey(RegistryRoot root) {
    switch (root) {
        case RegistryRoot::kLocalMachine: return HKEY_LOCAL_MACHINE;
        case RegistryRoot::kCurrentUser: return HKEY_CURRENT_USER;
    }
    return HKEY_LOCAL_MACHINE;
}

REGSAM view_flags(RegistryView view) {
    switch (view) {
        case RegistryView::kWow6432: return KEY_WOW64_32KEY;
        case RegistryView::kWin64: return KEY_WOW64_64KEY;
        case RegistryView::kDefault: return 0;
    }
    return 0;
}

class Win32RegistryReader final : public IRegistryReader {
public:
    std::optional<std::wstring> read_string(RegistryRoot root, const std::wstring& subkey,
                                              const std::wstring& value,
                                              RegistryView view) override {
        HKEY hkey = nullptr;
        REGSAM sam = KEY_READ | view_flags(view);
        LONG rc = RegOpenKeyExW(to_hkey(root), subkey.c_str(), 0, sam, &hkey);
        if (rc != ERROR_SUCCESS || hkey == nullptr) {
            return std::nullopt;
        }

        DWORD type = 0;
        DWORD byte_size = 0;
        rc = RegQueryValueExW(hkey, value.c_str(), nullptr, &type, nullptr, &byte_size);
        if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || byte_size == 0) {
            RegCloseKey(hkey);
            return std::nullopt;
        }

        // Buffer sized in wchar_t units, with room for a null terminator even
        // if the stored data was not itself null-terminated.
        size_t char_count = byte_size / sizeof(wchar_t);
        std::wstring raw(char_count, L'\0');
        DWORD read_bytes = byte_size;
        rc = RegQueryValueExW(hkey, value.c_str(), nullptr, &type,
                               reinterpret_cast<LPBYTE>(raw.data()), &read_bytes);
        RegCloseKey(hkey);
        if (rc != ERROR_SUCCESS) {
            return std::nullopt;
        }

        // Trim to the actual null terminator, if any, so trailing NULs in the
        // registry data do not leak into the returned string.
        size_t nul_pos = raw.find(L'\0');
        if (nul_pos != std::wstring::npos) {
            raw.resize(nul_pos);
        }

        if (type == REG_EXPAND_SZ) {
            DWORD needed = ExpandEnvironmentStringsW(raw.c_str(), nullptr, 0);
            if (needed == 0) {
                return raw;
            }
            std::wstring expanded(needed, L'\0');
            DWORD written = ExpandEnvironmentStringsW(raw.c_str(), expanded.data(), needed);
            if (written == 0 || written > expanded.size()) {
                return raw;
            }
            size_t exp_nul = expanded.find(L'\0');
            if (exp_nul != std::wstring::npos) {
                expanded.resize(exp_nul);
            }
            return expanded;
        }

        return raw;
    }
};

class Win32FileProbe final : public IFileProbe {
public:
    bool file_exists(const std::wstring& path) override {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::vector<uint8_t> read_prefix(const std::wstring& path, size_t max_bytes) override {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return {};
        }

        std::vector<uint8_t> buffer(max_bytes);
        DWORD read_bytes = 0;
        BOOL ok = ReadFile(h, buffer.data(), static_cast<DWORD>(buffer.size()), &read_bytes,
                            nullptr);
        CloseHandle(h);
        if (!ok) {
            return {};
        }
        buffer.resize(read_bytes);
        return buffer;
    }
};

}  // namespace

std::unique_ptr<IRegistryReader> make_registry_reader() {
    return std::make_unique<Win32RegistryReader>();
}

std::unique_ptr<IFileProbe> make_file_probe() {
    return std::make_unique<Win32FileProbe>();
}

}  // namespace wlm2pst
