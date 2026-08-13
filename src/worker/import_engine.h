// WLM2PST - portable abstraction over the Extended MAPI import engine.
//
// The orchestration layer (preflight, import loop, resume, validation) depends
// only on these interfaces, so it can be unit-tested off-Windows with fakes.
// The single real implementation lives in src/worker/mapi/ (Windows-only) and
// is obtained through make_mapi_environment(), which is defined only in the
// wlm2pst_mapi library and linked into the worker executable.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/errors/result.h"
#include "common/model.h"

namespace wlm2pst {

// Opaque MAPI entry identifier (PR_ENTRYID bytes).
using EntryId = std::vector<uint8_t>;

// Metadata applied to every imported message (spec sections 12, 13, 15).
struct MessageMetadata {
    std::wstring source_relative_path;   // tracking property + diagnostics
    std::string sha256_hex;              // lowercase hex, tracking property
    std::string run_id;                  // job GUID, tracking property
    std::string import_version;          // tool version, tracking property
    uint64_t source_size = 0;
    FileTimeUtc source_last_write_utc = 0;
    bool mark_sent = false;              // sent-folder alias matched
    bool mark_draft = false;             // X-Unsent or drafts-folder alias
    FileTimeUtc now_utc = 0;             // for converter-date sanity bounds (spec section 13)
    // Conversion-integrity expectations derived from the source headers: a
    // converted message must not silently lose what the source declared
    // (guards against a non-converting IConverterSession implementation).
    bool source_declared_subject = false;  // "Subject:" present in source
    bool source_multipart_mixed = false;   // Content-Type: multipart/mixed
    // Date fallbacks, already resolved by the pipeline (spec section 13):
    // applied only when the converter produced no valid corresponding value.
    std::optional<FileTimeUtc> fallback_message_delivery_time;
    std::optional<FileTimeUtc> fallback_client_submit_time;
};

struct ImportOutcome {
    EntryId entry_id;
    bool used_normalized_retry = false;  // attempt 2 (conservative repairs) succeeded
    bool date_fallback_applied = false;  // converter produced no usable date property
};

// Populated when both conversion attempts fail (spec section 16).
struct ImportFailure {
    int32_t first_hresult = 0;
    int32_t second_hresult = 0;
    bool second_attempted = false;  // false: no conservative repair applied
};

// Folder metadata for validation (spec section 20).
struct ToolFolderInfo {
    std::wstring relative_path;          // path under the tool root, '\\'-separated; L"" = root
    EntryId entry_id;
    uint64_t message_count = 0;
};

// Tracking-property view of one tool-owned message (spec sections 15, 17, 20).
struct TrackedMessage {
    EntryId entry_id;
    std::wstring source_relative_path;
    std::string sha256_hex;
    std::string run_id;
    bool has_attachments = false;        // fallback messages must carry the EML
};

// One open conversion (or validation) session: temporary profile + PST store.
class IPstSession {
public:
    virtual ~IPstSession() = default;

    // Creates (or opens, OPEN_IF_EXISTS) the folder chain under the tool root.
    // `segments` are already-normalized folder names; empty = tool root itself.
    virtual Result<EntryId> ensure_folder(const std::vector<std::wstring>& segments) = 0;

    // Attempt 1 + attempt 2 (conservative normalization) EML import into the
    // given folder; applies read/sent/draft flags, date fallbacks, and
    // tracking properties before the final save (spec sections 10, 12, 13, 15, 16).
    // On failure, `failure_detail` (when non-null) receives both attempts'
    // HRESULTs for the fallback record and errors CSV.
    virtual Result<ImportOutcome> import_eml(const EntryId& folder,
                                             const std::wstring& eml_absolute_path,
                                             const MessageMetadata& metadata,
                                             ImportFailure* failure_detail) = 0;

    // Final fallback (spec section 16): plain IPM.Note carrying the original
    // EML bytes as an attachment, in the _Conversion Errors folder.
    virtual Result<ImportOutcome> import_fallback(const EntryId& errors_folder,
                                                  const std::wstring& eml_absolute_path,
                                                  const MessageMetadata& metadata,
                                                  int32_t first_hresult,
                                                  int32_t second_hresult) = 0;

    // Crash-window recovery (spec section 17): folder-local search for
    // tool-owned messages matching run id + source relative path.
    virtual Result<std::vector<TrackedMessage>> find_tracked_messages(
        const EntryId& folder,
        const std::string& run_id,
        const std::wstring& source_relative_path) = 0;

    // Validation support (spec section 20): enumerate every folder under the
    // tool root (depth-first, deterministic) with message counts.
    virtual Result<std::vector<ToolFolderInfo>> list_tool_folders() = 0;

    // Validation support: read tracking properties of every message in a folder.
    // Messages without WLM2PST tracking properties are reported with empty
    // run_id so validation can flag foreign content.
    virtual Result<std::vector<TrackedMessage>> read_tracked_messages(const EntryId& folder) = 0;

    // Orderly teardown: release store, log off, delete the temporary profile.
    // Also invoked by the destructor; explicit call surfaces errors.
    virtual Status close() = 0;
};

class IMapiEnvironment {
public:
    virtual ~IMapiEnvironment() = default;

    // Preflight (spec section 6): load classic Outlook MAPI, verify bitness,
    // instantiate the MIME converter, create+delete a probe profile, confirm
    // the Unicode PST provider exists. Leaves nothing behind.
    virtual Status preflight_check() = 0;

    // Opens a conversion session. When `must_exist` is false a new PST is
    // created at `pst_path`; when true (resume/validation) the existing file
    // is attached. `root_name` is created with OPEN_IF_EXISTS semantics.
    virtual Result<std::unique_ptr<IPstSession>> create_session(
        const std::wstring& pst_path,
        const std::wstring& root_name,
        bool must_exist,
        const std::string& profile_purpose) = 0;  // "convert" / "validate": profile naming

    // Removes stale WLM2PST-owned temporary profiles from crashed runs.
    // Never touches non-WLM2PST profiles (spec section 8).
    virtual Status cleanup_stale_profiles() = 0;
};

// Defined in the Windows-only wlm2pst_mapi library.
std::unique_ptr<IMapiEnvironment> make_mapi_environment();

}  // namespace wlm2pst
