#include "worker/mapi/pst_store.h"

#include "worker/mapi/mapi_constants.h"

#include <cstring>

namespace wlm2pst::mapi {

namespace {

LPTSTR ansi_arg(const std::string& s) {
    return reinterpret_cast<LPTSTR>(const_cast<char*>(s.c_str()));
}

Result<EntryId> binary_prop_to_entry_id(const SPropValue& prop, const char* operation) {
    if (PROP_TYPE(prop.ulPropTag) != PT_BINARY || prop.Value.bin.cb == 0) {
        return make_error(operation, "expected binary entry-id property missing");
    }
    return EntryId(prop.Value.bin.lpb, prop.Value.bin.lpb + prop.Value.bin.cb);
}

}  // namespace

PstStore::~PstStore() {
    close();
}

void PstStore::close() {
    store_.reset();
    if (session_) {
        session_->Logoff(0, 0, 0);
        session_.reset();
    }
}

Result<std::unique_ptr<PstStore>> PstStore::logon_and_open(MapiRuntime& runtime,
                                                           const std::string& profile_name) {
    std::unique_ptr<PstStore> self(new PstStore(runtime));

    // ANSI profile name (names are ASCII GUIDs), hence no MAPI_UNICODE.
    HRESULT hr = runtime.MAPILogonEx(
        0, ansi_arg(profile_name), nullptr,
        MAPI_EXTENDED | MAPI_NEW_SESSION | MAPI_EXPLICIT_PROFILE | MAPI_NO_MAIL,
        self->session_.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "MAPILogonEx");
    }

    // The temporary profile contains exactly one message store: our PST.
    MapiPtr<IMAPITable> stores;
    hr = self->session_->GetMsgStoresTable(0, stores.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMAPISession::GetMsgStoresTable");
    }
    enum { kColEntryId, kColCount };
    SizedSPropTagArray(kColCount, columns) = {kColCount, {PR_ENTRYID}};
    hr = stores->SetColumns(reinterpret_cast<LPSPropTagArray>(&columns), 0);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::SetColumns");
    }
    LPSRowSet rows = nullptr;
    hr = stores->QueryRows(2, 0, &rows);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::QueryRows");
    }
    RowSetGuard guard(rows, runtime.MAPIFreeBuffer);
    if (!rows || rows->cRows == 0) {
        return make_error("GetMsgStoresTable", "temporary profile contains no message store");
    }
    auto store_eid = binary_prop_to_entry_id(rows->aRow[0].lpProps[kColEntryId],
                                             "GetMsgStoresTable");
    if (!store_eid.ok()) return store_eid.error();

    hr = self->session_->OpenMsgStore(
        0, static_cast<ULONG>(store_eid.value().size()),
        reinterpret_cast<LPENTRYID>(store_eid.value().data()), nullptr,
        MDB_WRITE | MDB_NO_DIALOG | MAPI_BEST_ACCESS, self->store_.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMAPISession::OpenMsgStore",
                                  "PST store could not be opened");
    }
    return self;
}

Result<EntryId> PstStore::ipm_subtree_entry_id() {
    SizedSPropTagArray(1, tags) = {1, {PR_IPM_SUBTREE_ENTRYID}};
    ULONG count = 0;
    LPSPropValue values = nullptr;
    HRESULT hr = store_->GetProps(reinterpret_cast<LPSPropTagArray>(&tags), 0, &count, &values);
    MapiBuffer buffer(runtime_->MAPIFreeBuffer);
    *buffer.put() = values;
    if (FAILED(hr) || count == 0) {
        return make_hresult_error(static_cast<int32_t>(hr),
                                  "IMsgStore::GetProps(PR_IPM_SUBTREE_ENTRYID)");
    }
    return binary_prop_to_entry_id(values[0], "PR_IPM_SUBTREE_ENTRYID");
}

Result<MapiPtr<IMAPIFolder>> PstStore::open_folder(const EntryId& entry_id) {
    MapiPtr<IMAPIFolder> folder;
    ULONG object_type = 0;
    HRESULT hr = store_->OpenEntry(static_cast<ULONG>(entry_id.size()),
                                   reinterpret_cast<LPENTRYID>(const_cast<uint8_t*>(entry_id.data())),
                                   nullptr, MAPI_MODIFY, &object_type, folder.put_unknown());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMsgStore::OpenEntry(folder)");
    }
    if (object_type != MAPI_FOLDER) {
        return make_error("OpenEntry", "entry id does not reference a folder");
    }
    return folder;
}

Result<EntryId> PstStore::ensure_root_folder(const std::wstring& root_name) {
    auto subtree_eid = ipm_subtree_entry_id();
    if (!subtree_eid.ok()) return subtree_eid.error();
    return ensure_child_folder(subtree_eid.value(), root_name);
}

Result<EntryId> PstStore::ensure_child_folder(const EntryId& parent, const std::wstring& name) {
    auto parent_folder = open_folder(parent);
    if (!parent_folder.ok()) return parent_folder.error();

    MapiPtr<IMAPIFolder> child;
    // MAPI_UNICODE: folder names are wide; OPEN_IF_EXISTS makes this
    // idempotent across resume runs.
    HRESULT hr = parent_folder.value()->CreateFolder(
        FOLDER_GENERIC,
        reinterpret_cast<LPTSTR>(const_cast<wchar_t*>(name.c_str())),
        nullptr, nullptr, MAPI_UNICODE | OPEN_IF_EXISTS, child.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMAPIFolder::CreateFolder");
    }

    SizedSPropTagArray(1, tags) = {1, {PR_ENTRYID}};
    ULONG count = 0;
    LPSPropValue values = nullptr;
    hr = child->GetProps(reinterpret_cast<LPSPropTagArray>(&tags), 0, &count, &values);
    MapiBuffer buffer(runtime_->MAPIFreeBuffer);
    *buffer.put() = values;
    if (FAILED(hr) || count == 0) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMAPIFolder::GetProps(PR_ENTRYID)");
    }
    return binary_prop_to_entry_id(values[0], "folder PR_ENTRYID");
}

Result<MapiPtr<IMessage>> PstStore::create_message(IMAPIFolder& folder) {
    MapiPtr<IMessage> message;
    HRESULT hr = folder.CreateMessage(nullptr, 0, message.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMAPIFolder::CreateMessage");
    }
    return message;
}

Result<EntryId> PstStore::message_entry_id(IMessage& message) {
    SizedSPropTagArray(1, tags) = {1, {PR_ENTRYID}};
    ULONG count = 0;
    LPSPropValue values = nullptr;
    HRESULT hr = message.GetProps(reinterpret_cast<LPSPropTagArray>(&tags), 0, &count, &values);
    MapiBuffer buffer(runtime_->MAPIFreeBuffer);
    *buffer.put() = values;
    if (FAILED(hr) || count == 0 || PROP_TYPE(values[0].ulPropTag) != PT_BINARY) {
        // PR_ENTRYID may legitimately be unavailable before SaveChanges.
        return make_hresult_error(static_cast<int32_t>(hr), "IMessage::GetProps(PR_ENTRYID)");
    }
    return binary_prop_to_entry_id(values[0], "message PR_ENTRYID");
}

}  // namespace wlm2pst::mapi
