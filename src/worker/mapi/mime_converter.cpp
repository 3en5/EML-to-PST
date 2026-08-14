#include "worker/mapi/mime_converter.h"

#include "common/logging/logger.h"
#include "common/unicode/utf.h"
#include "worker/mapi/mapi_runtime.h"
#include "worker/normalize/eml_normalizer.h"

#include <shlwapi.h>

#include <algorithm>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace wlm2pst::mapi {

Result<MapiPtr<IStream>> open_read_only_stream(const std::wstring& path) {
    MapiPtr<IStream> stream;
    // Deny writers while the file is being converted (spec section 7); the
    // stream reads incrementally, never loading the file into memory.
    HRESULT hr = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
                                        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, stream.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "SHCreateStreamOnFileEx");
    }
    return stream;
}

TempFile::~TempFile() {
    if (!path_.empty()) {
        DeleteFileW(path_.c_str());
    }
}

Result<TempFile> TempFile::create() {
    wchar_t temp_dir[MAX_PATH + 1] = {};
    DWORD n = GetTempPathW(MAX_PATH + 1, temp_dir);
    if (n == 0 || n > MAX_PATH) {
        return make_win32_error(GetLastError(), "GetTempPathW");
    }
    // GetTempFileNameW creates the file atomically with a non-predictable-
    // enough unique name inside the per-user temp directory, whose default
    // ACL restricts access to the current user (spec section 24).
    wchar_t temp_path[MAX_PATH + 1] = {};
    if (GetTempFileNameW(temp_dir, L"wlm", 0, temp_path) == 0) {
        return make_win32_error(GetLastError(), "GetTempFileNameW");
    }
    TempFile file;
    file.path_ = temp_path;
    return file;
}

Result<TempFile> write_normalized_copy(const std::wstring& source_path) {
    // Read up to the header cap plus the remainder streamed in chunks.
    std::ifstream in(std::filesystem::path(source_path), std::ios::binary);
    if (!in) {
        return make_error("write_normalized_copy", "source EML could not be reopened");
    }
    std::string prefix(1024 * 1024, '\0');
    in.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    prefix.resize(static_cast<size_t>(in.gcount()));

    EmlNormalization norm = normalize_eml_header(prefix);
    if (!norm.changed) {
        return make_error("write_normalized_copy", "no conservative repair applicable");
    }

    auto temp = TempFile::create();
    if (!temp.ok()) return temp.error();

    std::ofstream out(std::filesystem::path(temp.value().path()),
                      std::ios::binary | std::ios::trunc);
    if (!out) {
        return make_error("write_normalized_copy", "temporary file could not be opened");
    }
    out.write(norm.normalized_header.data(),
              static_cast<std::streamsize>(norm.normalized_header.size()));

    // Body bytes are preserved exactly (spec section 16): copy verbatim from
    // the original boundary onward.
    if (norm.original_header_bytes < prefix.size()) {
        out.write(prefix.data() + norm.original_header_bytes,
                  static_cast<std::streamsize>(prefix.size() - norm.original_header_bytes));
    }
    std::vector<char> chunk(256 * 1024);
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        std::streamsize got = in.gcount();
        if (got <= 0) break;
        out.write(chunk.data(), got);
    }
    out.flush();
    if (!out) {
        return make_error("write_normalized_copy", "temporary file write failed");
    }
    return std::move(temp.value());
}

namespace {

std::wstring read_registry_string(HKEY root, const wchar_t* key, const wchar_t* value,
                                  REGSAM view) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE | view, &handle) != ERROR_SUCCESS) {
        return {};
    }
    DWORD type = 0;
    DWORD bytes = 0;
    std::wstring result;
    if (RegQueryValueExW(handle, value, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= sizeof(wchar_t)) {
        result.resize(bytes / sizeof(wchar_t));
        if (RegQueryValueExW(handle, value, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(result.data()), &bytes) != ERROR_SUCCESS) {
            result.clear();
        } else {
            while (!result.empty() && result.back() == L'\0') result.pop_back();
        }
    }
    RegCloseKey(handle);
    if (type == REG_EXPAND_SZ && !result.empty()) {
        wchar_t expanded[1024] = {};
        DWORD n = ExpandEnvironmentStringsW(result.c_str(), expanded,
                                            static_cast<DWORD>(std::size(expanded)));
        if (n > 0 && n <= std::size(expanded)) result.assign(expanded);
    }
    return result;
}

// Click-to-Run Office does not publish Outlook's COM classes in the ordinary
// registry (CoCreateInstance -> REGDB_E_CLASSNOTREG on a healthy install).
// It DOES publish them in its virtualized registry hive, mirrored for
// outside processes under:
//   HKLM\SOFTWARE\Microsoft\Office\ClickToRun\REGISTRY\MACHINE\Software\Classes
// The supported workaround (this is what MFCMAPI's MyCoCreateInstance does)
// is to read the class's InprocServer32 from that hive and ask THAT dll for
// the class factory via DllGetClassObject.
//
// History note: an earlier fallback guessed OUTLMIME.DLL by name. That dll
// hands out an object which accepts MIMEToMAPI and reports success WITHOUT
// converting (raw EML text becomes the body, no subject, no attachments) -
// verified against a real C2R installation. Never guess the dll; only load
// what the C2R hive registers, and let the preflight self-test be the final
// judge (see run_converter_self_test).
std::wstring clsid_to_string(REFCLSID clsid) {
    wchar_t buf[64] = {};
    std::swprintf(buf, std::size(buf),
                  L"{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}",
                  static_cast<unsigned long>(clsid.Data1), clsid.Data2, clsid.Data3,
                  clsid.Data4[0], clsid.Data4[1], clsid.Data4[2], clsid.Data4[3],
                  clsid.Data4[4], clsid.Data4[5], clsid.Data4[6], clsid.Data4[7]);
    return buf;
}

std::vector<std::wstring> c2r_inproc_server_candidates(REFCLSID clsid) {
    const std::wstring id = clsid_to_string(clsid);
    const std::wstring hive = L"SOFTWARE\\Microsoft\\Office\\ClickToRun\\REGISTRY\\MACHINE\\"
                              L"Software\\Classes";
    const std::wstring keys[] = {
        hive + L"\\CLSID\\" + id + L"\\InprocServer32",
        hive + L"\\Wow6432Node\\CLSID\\" + id + L"\\InprocServer32",
    };
    std::vector<std::wstring> candidates;
    for (const auto& key : keys) {
        for (REGSAM view : {REGSAM{0}, REGSAM{KEY_WOW64_64KEY}, REGSAM{KEY_WOW64_32KEY}}) {
            std::wstring dll = read_registry_string(HKEY_LOCAL_MACHINE, key.c_str(), L"", view);
            if (dll.empty()) continue;
            if (std::find(candidates.begin(), candidates.end(), dll) == candidates.end()) {
                candidates.push_back(std::move(dll));
            }
        }
    }
    return candidates;
}

HRESULT create_from_dll(const std::wstring& dll, REFCLSID clsid, REFIID iid, void** out) {
    HMODULE mod = GetModuleHandleW(dll.c_str());
    if (!mod) {
        mod = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!mod) return HRESULT_FROM_WIN32(GetLastError());
    }
    using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
    auto get_class_object = reinterpret_cast<DllGetClassObjectFn>(
        reinterpret_cast<void*>(GetProcAddress(mod, "DllGetClassObject")));
    if (!get_class_object) return REGDB_E_CLASSNOTREG;

    MapiPtr<IClassFactory> factory;
    HRESULT hr = get_class_object(clsid, IID_IClassFactory, factory.put_void());
    if (FAILED(hr)) return hr;
    return factory->CreateInstance(nullptr, iid, out);
}

}  // namespace

Result<MimeConverter> MimeConverter::create(MapiRuntime& runtime) {
    MimeConverter converter;
    HRESULT hr = CoCreateInstance(kClsidIConverterSession, nullptr, CLSCTX_INPROC_SERVER,
                                  kIidIConverterSession, converter.session_.put_void());
    if (SUCCEEDED(hr)) {
        // Which path served the converter matters for field diagnosis; DLL
        // paths are operational data, never message content.
        global_logger().info("MIME converter acquired via CoCreateInstance (registry COM)");
        return converter;
    }
    if (hr != REGDB_E_CLASSNOTREG && hr != CLASS_E_CLASSNOTAVAILABLE) {
        return make_hresult_error(static_cast<int32_t>(hr), "CoCreateInstance(IConverterSession)",
                                  "Outlook MIME converter unavailable");
    }

    // Click-to-Run route: the dll registered in the C2R virtual hive.
    HRESULT last_hr = hr;
    for (const std::wstring& dll : c2r_inproc_server_candidates(kClsidIConverterSession)) {
        hr = create_from_dll(dll, kClsidIConverterSession, kIidIConverterSession,
                             converter.session_.put_void());
        if (SUCCEEDED(hr)) {
            global_logger().info("MIME converter acquired via C2R hive InprocServer32: " +
                                 utf8_from_wide(dll));
            return converter;
        }
        global_logger().verbose("C2R hive candidate rejected (" + utf8_from_wide(dll) +
                                "): hr=" + std::to_string(hr));
        last_hr = hr;
    }

    // Last resort: the already-loaded Outlook MAPI module itself. Never
    // guess further dlls by name - a wrong module can hand out an object
    // that "succeeds" without converting (see history note above); the
    // preflight self-test still gates whatever this returns.
    if (runtime.module()) {
        using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
        auto get_class_object = reinterpret_cast<DllGetClassObjectFn>(reinterpret_cast<void*>(
            GetProcAddress(runtime.module(), "DllGetClassObject")));
        if (get_class_object) {
            MapiPtr<IClassFactory> factory;
            hr = get_class_object(kClsidIConverterSession, IID_IClassFactory, factory.put_void());
            if (SUCCEEDED(hr)) {
                hr = factory->CreateInstance(nullptr, kIidIConverterSession,
                                             converter.session_.put_void());
                if (SUCCEEDED(hr)) {
                    global_logger().info("MIME converter acquired via loaded MAPI module: " +
                                         utf8_from_wide(runtime.dll_path()));
                    return converter;
                }
            }
            last_hr = hr;
        }
    }

    return make_hresult_error(static_cast<int32_t>(last_hr), "CoCreateInstance(IConverterSession)",
                              "Outlook MIME converter unavailable (COM registration not found, "
                              "no working C2R hive registration)");
}

Status MimeConverter::set_address_book(LPADRBOOK address_book) {
    HRESULT hr = session_->SetAdrBook(address_book);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IConverterSession::SetAdrBook");
    }
    return Status::success();
}

Status MimeConverter::convert(IStream& eml, IMessage& message, ULONG flags) {
    LARGE_INTEGER zero{};
    HRESULT hr = eml.Seek(zero, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IStream::Seek");
    }
    hr = session_->MIMEToMAPI(&eml, &message, nullptr, flags);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IConverterSession::MIMEToMAPI");
    }
    if (hr != S_OK) {
        // SUCCEEDED but not S_OK (e.g. S_FALSE): accepted, but recorded -
        // the per-message integrity gate is the behavioral arbiter.
        global_logger().info("MIMEToMAPI returned non-S_OK success: 0x" +
                             [hr] { char b[16]; std::snprintf(b, sizeof(b), "%08X",
                                    static_cast<uint32_t>(hr)); return std::string(b); }());
    }
    return Status::success();
}

}  // namespace wlm2pst::mapi
