// WLM2PST - loads classic Outlook's Extended MAPI implementation.
//
// Follows the Microsoft MAPI Stub Library approach: resolve the true MAPI DLL
// registered by Outlook (HKLM\SOFTWARE\Clients\Mail\Microsoft Outlook,
// DllPathEx -> typically olmapi32.dll) instead of trusting whatever
// mapi32.dll the search path yields, then GetProcAddress every entry point.
// This guarantees the loaded MAPI matches the installed classic Outlook and
// fails cleanly when only New Outlook (no Extended MAPI) is present.
#pragma once

#include "common/errors/result.h"
#include "common/win_compat.h"

#include <mapidefs.h>
#include <mapix.h>

#include <string>

namespace wlm2pst::mapi {

class MapiRuntime {
public:
    MapiRuntime() = default;
    MapiRuntime(const MapiRuntime&) = delete;
    MapiRuntime& operator=(const MapiRuntime&) = delete;
    ~MapiRuntime();

    // Loads the DLL and resolves entry points. Does NOT call MAPIInitialize.
    Status load();

    // MAPIInitialize / MAPIUninitialize pair, ref-counted per instance.
    Status initialize();
    void uninitialize();

    [[nodiscard]] bool loaded() const noexcept { return module_ != nullptr; }
    [[nodiscard]] const std::wstring& dll_path() const noexcept { return dll_path_; }

    // Resolved entry points (valid after load() succeeds).
    LPMAPILOGONEX MAPILogonEx = nullptr;
    LPMAPIADMINPROFILES MAPIAdminProfiles = nullptr;
    LPMAPIALLOCATEBUFFER MAPIAllocateBuffer = nullptr;
    LPMAPIALLOCATEMORE MAPIAllocateMore = nullptr;
    LPMAPIFREEBUFFER MAPIFreeBuffer = nullptr;

private:
    // Locates the MAPI DLL path via the mail-client registration (both
    // registry views), falling back to the system stub.
    static std::wstring resolve_mapi_dll_path();

    HMODULE module_ = nullptr;
    std::wstring dll_path_;
    bool initialized_ = false;

    LPMAPIINITIALIZE mapi_initialize_ = nullptr;
    LPMAPIUNINITIALIZE mapi_uninitialize_ = nullptr;
};

}  // namespace wlm2pst::mapi
