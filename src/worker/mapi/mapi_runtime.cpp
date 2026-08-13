#include "worker/mapi/mapi_runtime.h"

#include "worker/mapi/mapi_constants.h"

#include <string>

namespace wlm2pst::mapi {

namespace {

// Reads a REG_SZ / REG_EXPAND_SZ value; tries the requested registry view.
std::wstring read_registry_string(HKEY root, const wchar_t* subkey, const wchar_t* value,
                                  REGSAM view_flag) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | view_flag, &key) != ERROR_SUCCESS) {
        return {};
    }
    std::wstring result;
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && bytes > 0) {
        std::wstring raw(bytes / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(key, value, nullptr, &type,
                             reinterpret_cast<LPBYTE>(raw.data()), &bytes) == ERROR_SUCCESS) {
            raw.resize(wcsnlen(raw.c_str(), raw.size()));
            if (type == REG_EXPAND_SZ) {
                wchar_t expanded[MAX_PATH * 2] = {};
                DWORD n = ExpandEnvironmentStringsW(raw.c_str(), expanded,
                                                    static_cast<DWORD>(std::size(expanded)));
                if (n > 0 && n <= std::size(expanded)) {
                    result.assign(expanded);
                }
            } else {
                result = std::move(raw);
            }
        }
    }
    RegCloseKey(key);
    return result;
}

}  // namespace

MapiRuntime::~MapiRuntime() {
    uninitialize();
    if (module_) {
        FreeLibrary(module_);
        module_ = nullptr;
    }
}

std::wstring MapiRuntime::resolve_mapi_dll_path() {
    // Layered lookup per MAPI Stub Library guidance: prefer the mail client's
    // registered full MAPI DLL (DllPathEx), then DllPath, in both registry
    // views; final fallback is the system stub, which forwards to the
    // registered provider on healthy systems.
    const REGSAM views[] = {0, KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    for (const wchar_t* value : {kMailClientDllPathEx, kMailClientDllPath}) {
        for (REGSAM view : views) {
            std::wstring path = read_registry_string(HKEY_LOCAL_MACHINE, kMailClientKey, value, view);
            if (!path.empty()) return path;
        }
    }
    return kMapiStubDll;
}

Status MapiRuntime::load() {
    if (module_) return Status::success();

    dll_path_ = resolve_mapi_dll_path();
    // LOAD_WITH_ALTERED_SEARCH_PATH so olmapi32.dll resolves its private
    // dependencies from the Office directory.
    module_ = LoadLibraryExW(dll_path_.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module_ && dll_path_ != kMapiStubDll) {
        // Registered path unusable (e.g. stale registration); try the stub.
        dll_path_ = kMapiStubDll;
        module_ = LoadLibraryExW(dll_path_.c_str(), nullptr, 0);
    }
    if (!module_) {
        return make_win32_error(GetLastError(), "LoadLibraryExW",
                                "classic Outlook Extended MAPI could not be loaded");
    }

    // Cast through void* to satisfy -Wcast-function-type: FARPROC's generic
    // signature differs from every real entry point by design.
    auto resolve = [this](const char* name) -> void* {
        return reinterpret_cast<void*>(GetProcAddress(module_, name));
    };
    mapi_initialize_ = reinterpret_cast<LPMAPIINITIALIZE>(resolve("MAPIInitialize"));
    mapi_uninitialize_ = reinterpret_cast<LPMAPIUNINITIALIZE>(resolve("MAPIUninitialize"));
    MAPILogonEx = reinterpret_cast<LPMAPILOGONEX>(resolve("MAPILogonEx"));
    MAPIAdminProfiles = reinterpret_cast<LPMAPIADMINPROFILES>(resolve("MAPIAdminProfiles"));
    MAPIAllocateBuffer = reinterpret_cast<LPMAPIALLOCATEBUFFER>(resolve("MAPIAllocateBuffer"));
    MAPIAllocateMore = reinterpret_cast<LPMAPIALLOCATEMORE>(resolve("MAPIAllocateMore"));
    MAPIFreeBuffer = reinterpret_cast<LPMAPIFREEBUFFER>(resolve("MAPIFreeBuffer"));

    if (!mapi_initialize_ || !mapi_uninitialize_ || !MAPILogonEx || !MAPIAdminProfiles ||
        !MAPIAllocateBuffer || !MAPIAllocateMore || !MAPIFreeBuffer) {
        FreeLibrary(module_);
        module_ = nullptr;
        return make_error("GetProcAddress",
                          "loaded MAPI DLL is missing required Extended MAPI entry points "
                          "(is only New Outlook installed?)");
    }
    return Status::success();
}

Status MapiRuntime::initialize() {
    if (initialized_) return Status::success();
    if (!module_) {
        if (Status s = load(); !s.ok()) return s;
    }
    // Single-threaded worker: PST writes are serialized on one thread
    // (spec section 25), so no MAPI_MULTITHREAD_NOTIFICATIONS needed.
    MAPIINIT_0 init{MAPI_INIT_VERSION, 0};
    HRESULT hr = mapi_initialize_(&init);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "MAPIInitialize");
    }
    initialized_ = true;
    return Status::success();
}

void MapiRuntime::uninitialize() {
    if (initialized_ && mapi_uninitialize_) {
        mapi_uninitialize_();
    }
    initialized_ = false;
}

}  // namespace wlm2pst::mapi
