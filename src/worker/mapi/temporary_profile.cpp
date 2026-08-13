#include "worker/mapi/temporary_profile.h"

#include "worker/mapi/mapi_constants.h"

#include <objbase.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace wlm2pst::mapi {

namespace {

// Profile-admin string parameters are ANSI when MAPI_UNICODE is not passed;
// LPTSTR in the signatures is historical. Names are ASCII by construction.
LPTSTR ansi_arg(const std::string& s) {
    return reinterpret_cast<LPTSTR>(const_cast<char*>(s.c_str()));
}

Result<MapiPtr<IProfAdmin>> open_prof_admin(MapiRuntime& runtime) {
    MapiPtr<IProfAdmin> admin;
    HRESULT hr = runtime.MAPIAdminProfiles(0, admin.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "MAPIAdminProfiles");
    }
    return admin;
}

}  // namespace

std::string new_guid_string() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        // CoCreateGuid failure is effectively impossible; fall back to tick
        // count so the name is still unique enough for a profile.
        std::array<char, 32> buf{};
        std::snprintf(buf.data(), buf.size(), "fallback-%08lx", GetTickCount());
        return buf.data();
    }
    std::array<char, 40> buf{};
    std::snprintf(buf.data(), buf.size(),
                  "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3,
                  guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                  guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buf.data();
}

TemporaryProfile::TemporaryProfile(MapiRuntime& runtime, std::string name)
    : runtime_(&runtime), name_(std::move(name)) {}

TemporaryProfile::~TemporaryProfile() {
    if (!removed_) {
        (void)remove();  // best effort on all exit paths (spec section 8)
    }
}

Result<std::unique_ptr<TemporaryProfile>> TemporaryProfile::create(MapiRuntime& runtime,
                                                                   const std::string& prefix) {
    auto admin = open_prof_admin(runtime);
    if (!admin.ok()) return admin.error();

    std::string name = prefix + new_guid_string();
    HRESULT hr = admin.value()->CreateProfile(ansi_arg(name), nullptr, 0, 0);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IProfAdmin::CreateProfile");
    }
    return std::unique_ptr<TemporaryProfile>(new TemporaryProfile(runtime, std::move(name)));
}

Result<MAPIUID> TemporaryProfile::create_pst_service(IMsgServiceAdmin& svc_admin) {
    std::string service_name = kUnicodePstServiceName;
    std::string service_display = "WLM2PST Output Store";
    MAPIUID service_uid{};
    bool have_uid = false;

    // Preferred: IMsgServiceAdmin2::CreateMsgServiceEx returns the UID
    // directly and races with nothing.
    HRESULT hr = S_OK;
    MapiPtr<IMsgServiceAdmin2> svc_admin2;
    if (SUCCEEDED(svc_admin.QueryInterface(kIidIMsgServiceAdmin2, svc_admin2.put_void()))) {
        hr = svc_admin2->CreateMsgServiceEx(ansi_arg(service_name), ansi_arg(service_display),
                                            0, 0, &service_uid);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr),
                                      "IMsgServiceAdmin2::CreateMsgServiceEx",
                                      "Unicode PST provider could not be added");
        }
        have_uid = true;
    } else {
        // Compatibility path: CreateMsgService + look the UID up in the
        // service table (fresh profile => exactly one MSUPST MS service).
        hr = svc_admin.CreateMsgService(ansi_arg(service_name), ansi_arg(service_display), 0, 0);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr),
                                      "IMsgServiceAdmin::CreateMsgService",
                                      "Unicode PST provider could not be added");
        }
        MapiPtr<IMAPITable> table;
        hr = svc_admin.GetMsgServiceTable(0, table.put());
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr),
                                      "IMsgServiceAdmin::GetMsgServiceTable");
        }
        enum { kColServiceName, kColServiceUid, kColCount };
        SizedSPropTagArray(kColCount, columns) = {
            kColCount, {PR_SERVICE_NAME_A, PR_SERVICE_UID}};
        hr = table->SetColumns(reinterpret_cast<LPSPropTagArray>(&columns), 0);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::SetColumns");
        }
        for (;;) {
            LPSRowSet rows = nullptr;
            hr = table->QueryRows(16, 0, &rows);
            if (FAILED(hr)) {
                return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::QueryRows");
            }
            RowSetGuard guard(rows, runtime_->MAPIFreeBuffer);
            if (!rows || rows->cRows == 0) break;
            for (ULONG i = 0; i < rows->cRows; ++i) {
                const SPropValue* name_prop = &rows->aRow[i].lpProps[kColServiceName];
                const SPropValue* uid_prop = &rows->aRow[i].lpProps[kColServiceUid];
                if (PROP_TYPE(name_prop->ulPropTag) == PT_STRING8 &&
                    PROP_TYPE(uid_prop->ulPropTag) == PT_BINARY &&
                    service_name == name_prop->Value.lpszA &&
                    uid_prop->Value.bin.cb == sizeof(MAPIUID)) {
                    std::memcpy(&service_uid, uid_prop->Value.bin.lpb, sizeof(MAPIUID));
                    have_uid = true;
                }
            }
        }
        if (!have_uid) {
            return make_error("GetMsgServiceTable",
                              "newly created Unicode PST service not found in service table");
        }
    }
    (void)have_uid;
    return service_uid;
}

Result<MapiPtr<IMsgServiceAdmin>> TemporaryProfile::open_service_admin() {
    auto admin = open_prof_admin(*runtime_);
    if (!admin.ok()) return admin.error();
    MapiPtr<IMsgServiceAdmin> svc_admin;
    HRESULT hr = admin.value()->AdminServices(ansi_arg(name_), nullptr, 0, 0, svc_admin.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IProfAdmin::AdminServices");
    }
    return svc_admin;
}

Status TemporaryProfile::add_unicode_pst_service(const std::wstring& pst_path,
                                                 const std::wstring& display_name) {
    auto svc_admin = open_service_admin();
    if (!svc_admin.ok()) return svc_admin.error();

    auto uid = create_pst_service(*svc_admin.value().get());
    if (!uid.ok()) return uid.error();
    MAPIUID service_uid = uid.value();

    // Point the provider at the output file. Passing PR_PST_PATH_W with a
    // non-existent path makes the provider create a new Unicode PST silently
    // (no UI: ulUIParam = 0 and no SERVICE_UI_ALWAYS).
    SPropValue props[2]{};
    props[0].ulPropTag = PR_PST_PATH_W;
    props[0].Value.lpszW = const_cast<LPWSTR>(pst_path.c_str());
    props[1].ulPropTag = PR_DISPLAY_NAME_W;
    props[1].Value.lpszW = const_cast<LPWSTR>(display_name.c_str());

    HRESULT hr = svc_admin.value()->ConfigureMsgService(&service_uid, 0, 0, 2, props);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr),
                                  "IMsgServiceAdmin::ConfigureMsgService",
                                  "Unicode PST service could not be configured with the output path");
    }
    return Status::success();
}

Status TemporaryProfile::probe_unicode_pst_service() {
    // Preflight-only: adds the Unicode PST service WITHOUT configuring a
    // path, proving the provider is registered while creating no file
    // (spec section 6). The whole profile is deleted right afterwards.
    auto svc_admin = open_service_admin();
    if (!svc_admin.ok()) return svc_admin.error();
    auto uid = create_pst_service(*svc_admin.value().get());
    if (!uid.ok()) return uid.error();
    return Status::success();
}

Status TemporaryProfile::remove() {
    if (removed_) return Status::success();
    auto admin = open_prof_admin(*runtime_);
    if (!admin.ok()) return admin.error();
    HRESULT hr = admin.value()->DeleteProfile(ansi_arg(name_), 0);
    if (FAILED(hr) && hr != MAPI_E_NOT_FOUND) {
        return make_hresult_error(static_cast<int32_t>(hr), "IProfAdmin::DeleteProfile");
    }
    removed_ = true;
    return Status::success();
}

Result<int> cleanup_stale_wlm2pst_profiles(MapiRuntime& runtime) {
    auto admin = open_prof_admin(runtime);
    if (!admin.ok()) return admin.error();

    MapiPtr<IMAPITable> table;
    HRESULT hr = admin.value()->GetProfileTable(0, table.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IProfAdmin::GetProfileTable");
    }
    enum { kColName, kColCount };
    SizedSPropTagArray(kColCount, columns) = {kColCount, {PR_DISPLAY_NAME_A}};
    hr = table->SetColumns(reinterpret_cast<LPSPropTagArray>(&columns), 0);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::SetColumns");
    }

    std::vector<std::string> stale;
    for (;;) {
        LPSRowSet rows = nullptr;
        hr = table->QueryRows(16, 0, &rows);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::QueryRows");
        }
        RowSetGuard guard(rows, runtime.MAPIFreeBuffer);
        if (!rows || rows->cRows == 0) break;
        for (ULONG i = 0; i < rows->cRows; ++i) {
            const SPropValue* name_prop = &rows->aRow[i].lpProps[kColName];
            if (PROP_TYPE(name_prop->ulPropTag) != PT_STRING8) continue;
            const char* name = name_prop->Value.lpszA;
            // Prefix check is the ownership proof: only WLM2PST-* profiles
            // are ever deleted (spec section 8 - never remove user profiles).
            if (std::strncmp(name, kProfilePrefixA, std::strlen(kProfilePrefixA)) == 0) {
                stale.emplace_back(name);
            }
        }
    }

    int removed = 0;
    for (const std::string& name : stale) {
        std::string mutable_name = name;
        if (SUCCEEDED(admin.value()->DeleteProfile(ansi_arg(mutable_name), 0))) {
            ++removed;
        }
    }
    return removed;
}

}  // namespace wlm2pst::mapi
