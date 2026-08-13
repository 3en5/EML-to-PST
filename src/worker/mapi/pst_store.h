// WLM2PST - MAPI session + PST message store access (spec section 8).
//
// Owns the MAPILogonEx session for a temporary profile, locates the single
// PST store inside it, opens the IPM subtree, and provides folder operations.
// Lifetime rule: PstStore must be destroyed (or close()d) BEFORE the
// TemporaryProfile is removed and before MAPIUninitialize.
#pragma once

#include "common/errors/result.h"
#include "worker/import_engine.h"  // EntryId
#include "worker/mapi/mapi_raii.h"
#include "worker/mapi/mapi_runtime.h"

#include <memory>
#include <string>
#include <vector>

namespace wlm2pst::mapi {

class PstStore {
public:
    PstStore(const PstStore&) = delete;
    PstStore& operator=(const PstStore&) = delete;
    ~PstStore();

    // Logs on to `profile_name` (MAPI_EXTENDED | MAPI_NEW_SESSION |
    // MAPI_EXPLICIT_PROFILE | MAPI_NO_MAIL) and opens the PST store with
    // write access.
    static Result<std::unique_ptr<PstStore>> logon_and_open(MapiRuntime& runtime,
                                                            const std::string& profile_name);

    // Opens (OPEN_IF_EXISTS) or creates the named root folder directly under
    // the IPM subtree and returns its entry id.
    Result<EntryId> ensure_root_folder(const std::wstring& root_name);

    // Creates/opens a child folder under `parent`.
    Result<EntryId> ensure_child_folder(const EntryId& parent, const std::wstring& name);

    // Opens a folder by entry id.
    Result<MapiPtr<IMAPIFolder>> open_folder(const EntryId& entry_id);

    // Creates an empty message in the folder.
    Result<MapiPtr<IMessage>> create_message(IMAPIFolder& folder);

    // Reads PR_ENTRYID of a saved message.
    Result<EntryId> message_entry_id(IMessage& message);

    // Orderly release: store then session logoff. Idempotent.
    void close();

    [[nodiscard]] IMsgStore* store() const noexcept { return store_.get(); }
    [[nodiscard]] IMAPISession* session() const noexcept { return session_.get(); }
    [[nodiscard]] MapiRuntime& runtime() const noexcept { return *runtime_; }

private:
    explicit PstStore(MapiRuntime& runtime) : runtime_(&runtime) {}

    Result<EntryId> ipm_subtree_entry_id();

    MapiRuntime* runtime_;
    MapiPtr<IMAPISession> session_;
    MapiPtr<IMsgStore> store_;
};

}  // namespace wlm2pst::mapi
