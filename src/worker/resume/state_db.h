// WLM2PST - crash-safe SQLite resume state database (spec section 17).
//
// Portable: no Windows headers. Tracks per-file import lifecycle, the
// source/output folder mapping, and job identity/compatibility information
// so an interrupted conversion can resume without re-importing files or
// losing track of what has already been written to the PST.
//
// Privacy: this database stores ONLY paths, sizes, timestamps, hashes,
// statuses, attempt counts and result codes. It must never store message
// subjects, bodies, addresses, or attachment content.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/errors/result.h"
#include "common/model.h"
#include "worker/resume/sqlite_util.h"

namespace wlm2pst {

// Identity/compatibility fields for a job, shared by create_new/open_existing
// and the --resume compatibility check.
struct JobInfo {
    std::string run_id;         // GUID string.
    std::string tool_version;
    std::wstring source_root;   // UTF-8 in storage; absolute, native wide in memory.
    std::wstring output_pst;    // Absolute path to the output .pst.
    std::wstring root_name;     // Top-level folder name created inside the PST.
    std::string outlook_bitness;  // "x86" or "x64".
    FileTimeUtc created_at_utc = 0;
    std::string manifest_hash;  // Lowercase hex SHA-256 of the source manifest.
};

// Everything load_job() can tell the caller about the persisted job row.
struct LoadedJob {
    JobInfo info;
    int schema_version = 0;  // Read from PRAGMA user_version.
    std::string status;      // "RUNNING" | "COMPLETED" | "FAILED" | "CANCELLED".
};

// The current schema version this build of WLM2PST writes/expects.
constexpr int kStateDbSchemaVersion = 1;

// Owns one SQLite connection to a job's resume state database
// (`<output>.wlm2pst-state.sqlite`). Movable, non-copyable.
class StateDb {
public:
    StateDb() = default;

    StateDb(const StateDb&) = delete;
    StateDb& operator=(const StateDb&) = delete;
    StateDb(StateDb&&) = default;
    StateDb& operator=(StateDb&&) = default;

    // Creates the schema, then inserts the job row and every manifest entry
    // as PENDING with its target folder, all inside one transaction. Fails
    // cleanly (no partial visible state) if any step fails, e.g. a
    // read-only destination directory or a duplicate relative path.
    // `file_target_folders` must be the same size as `manifest` and parallel
    // to it: file_target_folders[i] is the (source relative path, target
    // relative folder) pair for manifest[i].
    static Result<StateDb> create_new(
        const std::wstring& db_path, const JobInfo& info,
        const std::vector<ManifestEntry>& manifest,
        const std::vector<std::pair<std::wstring, std::wstring>>& file_target_folders);

    // Opens a state database created by a previous run. Does not create or
    // modify the schema; does not itself validate compatibility with the
    // current job (see check_resume_compatible).
    static Result<StateDb> open_existing(const std::wstring& db_path);

    [[nodiscard]] bool valid() const noexcept { return db_.valid(); }

    Result<LoadedJob> load_job();
    Status set_job_status(const std::string& status, FileTimeUtc now);

    // Resume compatibility (spec section 17). On mismatch, returns an Error
    // whose message names the first mismatching field: "source_root",
    // "output_pst", "root_name", "outlook_bitness", "schema_version", or
    // "manifest_hash".
    Status check_resume_compatible(const JobInfo& expected, const std::string& manifest_hash,
                                    int expected_schema_version);

    // --- Import lifecycle -------------------------------------------------

    Status mark_started(int64_t file_id, FileTimeUtc now);
    Status set_sha256(int64_t file_id, const std::string& hex);
    Status mark_imported(int64_t file_id, const std::vector<uint8_t>& entry_id, FileTimeUtc now);
    Status mark_status(int64_t file_id, FileImportStatus status, int32_t last_hresult,
                        FileTimeUtc now);

    struct FileRow {
        int64_t id = 0;
        std::wstring relative_source_path;
        uint64_t source_size = 0;
        FileTimeUtc source_last_write_utc = 0;
        std::string source_sha256;  // Empty when not yet hashed.
        std::wstring target_folder_path;
        std::vector<uint8_t> target_entry_id;  // Empty when not yet imported.
        FileImportStatus status = FileImportStatus::kPending;
        int attempt_count = 0;
        int32_t last_hresult = 0;
    };
    Result<std::vector<FileRow>> load_all_files();
    Result<std::vector<FileRow>> load_files_with_status(FileImportStatus status);

    // --- Folder map ---------------------------------------------------

    Status upsert_folder(const std::wstring& source_rel, const std::wstring& target_rel,
                          const std::string& rename_reason);
    Status set_folder_entry_id(const std::wstring& target_rel, const std::vector<uint8_t>& entry_id);

    struct FolderRow {
        std::wstring source_relative_folder;
        std::wstring target_relative_folder;
        std::vector<uint8_t> target_entry_id;
        std::string rename_reason;
    };
    Result<std::vector<FolderRow>> load_folders();

    // --- Validation support ---------------------------------------------

    struct StatusCounts {
        uint64_t pending = 0;
        uint64_t imported = 0;
        uint64_t preserved = 0;
        uint64_t failed_read = 0;
        uint64_t failed_mapi = 0;
    };
    Result<StatusCounts> count_by_status();
    Result<std::vector<std::pair<std::wstring, uint64_t>>> imported_counts_per_folder();
    Result<std::vector<std::pair<std::wstring, uint64_t>>> preserved_counts_per_folder();

    // --- Batching ---------------------------------------------------------

    // Explicit transaction control for the import loop. The returned
    // Transaction rolls back automatically if it is destroyed without a
    // commit() call.
    Result<sqlite_util::Transaction> begin_batch();

private:
    explicit StateDb(sqlite_util::Database db) : db_(std::move(db)) {}

    Status prepare_statements();
    // Touches job.last_updated_at_utc; used by every mutating call.
    Status touch_job_updated(FileTimeUtc now);
    Result<std::vector<FileRow>> query_files(const std::string& sql, const FileImportStatus* filter);
    Result<std::vector<std::pair<std::wstring, uint64_t>>> counts_per_folder_for_status(
        FileImportStatus status);

    // NOTE: db_ must be declared before every Statement member below so
    // that, on destruction, the (reverse-order) member teardown finalizes
    // all prepared statements before the connection itself is closed.
    sqlite_util::Database db_;

    sqlite_util::Statement stmt_insert_file_;
    sqlite_util::Statement stmt_mark_started_;
    sqlite_util::Statement stmt_set_sha256_;
    sqlite_util::Statement stmt_mark_imported_;
    sqlite_util::Statement stmt_mark_status_;
    sqlite_util::Statement stmt_set_job_status_;
    sqlite_util::Statement stmt_touch_job_;
    sqlite_util::Statement stmt_upsert_folder_;
    sqlite_util::Statement stmt_set_folder_entry_id_;
};

}  // namespace wlm2pst
