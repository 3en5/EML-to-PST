// WLM2PST - portable interfaces for registry and file-prefix access used by
// the layered Outlook detector (spec section 4). Real implementations live in
// registry_reader_win.cpp (Windows-only); tests inject fakes so detection
// logic is exercised on any platform.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wlm2pst {

// Which registry redirection view to open. On 32-bit processes running under
// WOW64, kWin64 forces access to the native 64-bit view; kWow6432 forces the
// 32-bit view. kDefault uses whatever the process would see unqualified.
enum class RegistryView { kDefault, kWow6432, kWin64 };

enum class RegistryRoot { kLocalMachine, kCurrentUser };

class IRegistryReader {
public:
    virtual ~IRegistryReader() = default;

    // Reads a REG_SZ / REG_EXPAND_SZ value (environment strings expanded).
    // Returns std::nullopt when the key or value does not exist, or the
    // stored data is not a string type.
    virtual std::optional<std::wstring> read_string(RegistryRoot root, const std::wstring& subkey,
                                                      const std::wstring& value,
                                                      RegistryView view) = 0;
};

class IFileProbe {
public:
    virtual ~IFileProbe() = default;

    virtual bool file_exists(const std::wstring& path) = 0;

    // Reads up to max_bytes from the start of the file. Returns an empty
    // vector on any failure (missing file, access denied, empty file).
    virtual std::vector<uint8_t> read_prefix(const std::wstring& path, size_t max_bytes) = 0;
};

#ifdef _WIN32
std::unique_ptr<IRegistryReader> make_registry_reader();
std::unique_ptr<IFileProbe> make_file_probe();
#endif

}  // namespace wlm2pst
