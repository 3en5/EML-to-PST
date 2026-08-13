// WLM2PST - tool-owned temporary MAPI profile lifecycle (spec section 8).
//
// One unique profile per run: WLM2PST-{GUID} (or WLM2PST-VALIDATE-{GUID}).
// Profile names are ASCII by construction, so the ANSI profile-admin calls
// are lossless. The destructor best-effort deletes the profile; call
// remove() explicitly to surface failures.
#pragma once

#include "common/errors/result.h"
#include "worker/mapi/mapi_runtime.h"
#include "worker/mapi/mapi_raii.h"

#include <memory>
#include <string>

namespace wlm2pst::mapi {

class TemporaryProfile {
public:
    TemporaryProfile(const TemporaryProfile&) = delete;
    TemporaryProfile& operator=(const TemporaryProfile&) = delete;
    TemporaryProfile(TemporaryProfile&&) = default;
    TemporaryProfile& operator=(TemporaryProfile&&) = default;
    ~TemporaryProfile();

    // Creates a fresh profile named <prefix><GUID>. `runtime` must outlive
    // the returned object and be initialize()d.
    static Result<std::unique_ptr<TemporaryProfile>> create(MapiRuntime& runtime,
                                                            const std::string& prefix);

    // Adds and configures the Unicode PST service ("MSUPST MS") pointing at
    // `pst_path`. Uses IMsgServiceAdmin2::CreateMsgServiceEx when available,
    // otherwise CreateMsgService + service-table UID lookup (compatibility
    // path, spec section 33.16).
    Status add_unicode_pst_service(const std::wstring& pst_path,
                                   const std::wstring& display_name);

    // Preflight probe: adds the Unicode PST service without configuring a
    // path (no file is created).
    Status probe_unicode_pst_service();

    // Deletes the profile. Idempotent.
    Status remove();

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    TemporaryProfile(MapiRuntime& runtime, std::string name);

    Result<MapiPtr<IMsgServiceAdmin>> open_service_admin();
    Result<MAPIUID> create_pst_service(IMsgServiceAdmin& svc_admin);

    MapiRuntime* runtime_;
    std::string name_;
    bool removed_ = false;
};

// Deletes stale "WLM2PST-*" profiles left behind by crashed runs. Only
// profiles carrying the WLM2PST prefix are ever touched (spec section 8).
// Returns the number of profiles removed.
Result<int> cleanup_stale_wlm2pst_profiles(MapiRuntime& runtime);

// ASCII GUID string like "5f0c...-..." (lowercase, no braces).
std::string new_guid_string();

}  // namespace wlm2pst::mapi
