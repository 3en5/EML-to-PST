#include "worker/mapi/mime_converter.h"

#include "common/logging/logger.h"
#include "common/unicode/utf.h"
#include "worker/mapi/mapi_runtime.h"
#include "worker/normalize/eml_normalizer.h"

#include <shlwapi.h>

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
    return result;
}

// Outlook's MIME converter lives in OUTLMIME.DLL, next to OUTLOOK.EXE.
std::wstring resolve_outlmime_path() {
    static constexpr const wchar_t* kAppPathsOutlook =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\OUTLOOK.EXE";
    for (REGSAM view : {REGSAM{0}, REGSAM{KEY_WOW64_64KEY}, REGSAM{KEY_WOW64_32KEY}}) {
        std::wstring exe = read_registry_string(HKEY_LOCAL_MACHINE, kAppPathsOutlook, L"", view);
        if (exe.empty()) continue;
        const size_t slash = exe.find_last_of(L"\\/");
        if (slash == std::wstring::npos) continue;
        std::wstring candidate = exe.substr(0, slash + 1) + L"OUTLMIME.DLL";
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) return candidate;
    }
    return L"OUTLMIME.DLL";  // let the loader search; better than giving up
}

// Click-to-Run Office does not publish Outlook's COM classes in the ordinary
// registry, so CoCreateInstance(CLSID_IConverterSession) returns
// REGDB_E_CLASSNOTREG on an otherwise healthy classic Outlook install
// (observed on Microsoft 365 / ProPlusRetail C2R, Office16). The implementing
// DLL still exports DllGetClassObject, so ask it for the class factory
// directly - the same fallback MFCMAPI relies on. Verified empirically:
// MSMAPI32/OLMAPI32 return CLASS_E_CLASSNOTAVAILABLE for this CLSID, and
// OUTLMIME.DLL is the module that actually serves it.
HRESULT create_converter_via_outlook_dll(REFCLSID clsid, REFIID iid, void** out,
                                         std::wstring& used_dll) {
    const std::wstring dll = resolve_outlmime_path();
    used_dll = dll;
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

Result<MimeConverter> MimeConverter::create() {
    MimeConverter converter;
    HRESULT hr = CoCreateInstance(kClsidIConverterSession, nullptr, CLSCTX_INPROC_SERVER,
                                  kIidIConverterSession, converter.session_.put_void());
    if (SUCCEEDED(hr)) {
        // Which path served the converter matters for field diagnosis (the
        // C2R fallback is the prime suspect whenever converted content looks
        // wrong); DLL paths are operational data, never message content.
        global_logger().verbose("MIME converter acquired via CoCreateInstance (registry COM)");
    } else if (hr == REGDB_E_CLASSNOTREG || hr == CLASS_E_CLASSNOTAVAILABLE) {
        std::wstring used_dll;
        hr = create_converter_via_outlook_dll(kClsidIConverterSession, kIidIConverterSession,
                                              converter.session_.put_void(), used_dll);
        if (SUCCEEDED(hr)) {
            global_logger().verbose("MIME converter acquired via DllGetClassObject fallback: " +
                                    utf8_from_wide(used_dll));
        }
    }
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "CoCreateInstance(IConverterSession)",
                                  "Outlook MIME converter unavailable");
    }
    return converter;
}

Status MimeConverter::convert(IStream& eml, IMessage& message) {
    // CCSF_GLOBAL_MESSAGE enables international (EAI) header handling on
    // Outlook 2010+; older converters reject unknown flags, so retry without
    // it (spec sections 10, 33.16). Rewind the stream before each attempt.
    const ULONG flag_sets[] = {
        kCcsfSmtp | kCcsfIncludeBcc | kCcsfGlobalMessage,
        kCcsfSmtp | kCcsfIncludeBcc,
    };
    HRESULT last_hr = E_FAIL;
    for (ULONG flags : flag_sets) {
        LARGE_INTEGER zero{};
        HRESULT hr = eml.Seek(zero, STREAM_SEEK_SET, nullptr);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IStream::Seek");
        }
        hr = session_->MIMEToMAPI(&eml, &message, nullptr, flags);
        if (SUCCEEDED(hr)) {
            return Status::success();
        }
        last_hr = hr;
        if (hr != E_INVALIDARG && hr != MAPI_E_UNKNOWN_FLAGS) {
            break;  // real conversion failure; dropping flags will not help
        }
    }
    return make_hresult_error(static_cast<int32_t>(last_hr), "IConverterSession::MIMEToMAPI");
}

}  // namespace wlm2pst::mapi
