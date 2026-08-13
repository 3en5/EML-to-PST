#include "worker/resume/state_db.h"

#include <chrono>
#include <ratio>
#include <string>
#include <utility>

namespace wlm2pst {

namespace {

// Schema (spec section 17). Created inside the same transaction as the job
// row and initial file rows so create_new() is all-or-nothing.
constexpr const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS job (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    run_id TEXT NOT NULL,
    schema_version INTEGER NOT NULL,
    tool_version TEXT NOT NULL,
    source_root TEXT NOT NULL,
    output_pst TEXT NOT NULL,
    root_name TEXT NOT NULL,
    outlook_bitness TEXT NOT NULL,
    created_at_utc INTEGER NOT NULL,
    last_updated_at_utc INTEGER NOT NULL,
    manifest_hash TEXT NOT NULL,
    status TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS files (
    id INTEGER PRIMARY KEY,
    relative_source_path TEXT NOT NULL UNIQUE,
    source_size INTEGER NOT NULL,
    source_last_write_utc INTEGER NOT NULL,
    source_sha256 TEXT,
    target_folder_path TEXT NOT NULL,
    target_entry_id BLOB,
    status TEXT NOT NULL,
    attempt_count INTEGER NOT NULL DEFAULT 0,
    last_hresult INTEGER,
    started_at_utc INTEGER,
    completed_at_utc INTEGER
);
CREATE INDEX IF NOT EXISTS idx_files_status ON files(status);
CREATE INDEX IF NOT EXISTS idx_files_target_folder_status ON files(target_folder_path, status);

CREATE TABLE IF NOT EXISTS folder_map (
    source_relative_folder TEXT PRIMARY KEY,
    target_relative_folder TEXT NOT NULL,
    target_entry_id BLOB,
    rename_reason TEXT
);
)SQL";

constexpr const char* kSelectFilesColumns =
    "SELECT id, relative_source_path, source_size, source_last_write_utc, source_sha256, "
    "target_folder_path, target_entry_id, status, attempt_count, last_hresult FROM files";

// Current wall-clock time in FileTimeUtc (FILETIME) ticks, used for
// housekeeping timestamps on API surfaces that do not take an explicit
// `now` parameter (spec: last_updated_at_utc bumped on every mutating call).
FileTimeUtc now_filetime() {
    using namespace std::chrono;
    using Ticks100ns = duration<int64_t, std::ratio<1, kFiletimeTicksPerSecond>>;
    int64_t ticks = duration_cast<Ticks100ns>(system_clock::now().time_since_epoch()).count();
    return ticks + kFiletimeUnixEpoch;
}

}  // namespace

// ---------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------

Status StateDb::prepare_statements() {
    auto assign = [this](sqlite_util::Statement& target, const char* sql) -> Status {
        Result<sqlite_util::Statement> prepared = sqlite_util::Statement::prepare(db_, sql);
        if (!prepared) return prepared.error();
        target = std::move(prepared.value());
        return Status::success();
    };

    Status s;
    if (!(s = assign(stmt_insert_file_,
                      "INSERT INTO files(relative_source_path, source_size, "
                      "source_last_write_utc, source_sha256, target_folder_path, status, "
                      "attempt_count) VALUES (?1, ?2, ?3, NULL, ?4, ?5, 0);")))
        return s;
    if (!(s = assign(stmt_mark_started_,
                      "UPDATE files SET attempt_count = attempt_count + 1, started_at_utc = ?1 "
                      "WHERE id = ?2;")))
        return s;
    if (!(s = assign(stmt_set_sha256_, "UPDATE files SET source_sha256 = ?1 WHERE id = ?2;")))
        return s;
    if (!(s = assign(stmt_mark_imported_,
                      "UPDATE files SET status = ?1, target_entry_id = ?2, completed_at_utc = "
                      "?3, last_hresult = 0 WHERE id = ?4;")))
        return s;
    if (!(s = assign(stmt_mark_status_,
                      "UPDATE files SET status = ?1, last_hresult = ?2, completed_at_utc = ?3 "
                      "WHERE id = ?4;")))
        return s;
    if (!(s = assign(stmt_set_job_status_,
                      "UPDATE job SET status = ?1, last_updated_at_utc = ?2 WHERE id = 1;")))
        return s;
    if (!(s = assign(stmt_touch_job_, "UPDATE job SET last_updated_at_utc = ?1 WHERE id = 1;")))
        return s;
    if (!(s = assign(stmt_upsert_folder_,
                      "INSERT INTO folder_map(source_relative_folder, target_relative_folder, "
                      "rename_reason) VALUES (?1, ?2, ?3) ON CONFLICT(source_relative_folder) DO "
                      "UPDATE SET target_relative_folder=excluded.target_relative_folder, "
                      "rename_reason=excluded.rename_reason;")))
        return s;
    if (!(s = assign(stmt_set_folder_entry_id_,
                      "UPDATE folder_map SET target_entry_id = ?1 WHERE target_relative_folder = "
                      "?2;")))
        return s;
    return Status::success();
}

Result<StateDb> StateDb::create_new(
    const std::wstring& db_path, const JobInfo& info, const std::vector<ManifestEntry>& manifest,
    const std::vector<std::pair<std::wstring, std::wstring>>& file_target_folders) {
    if (manifest.size() != file_target_folders.size()) {
        return make_error("StateDb::create_new",
                           "manifest and file_target_folders must be the same size");
    }

    Result<sqlite_util::Database> opened = sqlite_util::Database::open(db_path);
    if (!opened) return opened.error();

    StateDb db(std::move(opened.value()));

    Result<sqlite_util::Transaction> txn_result = sqlite_util::Transaction::begin_immediate(db.db_);
    if (!txn_result) return txn_result.error();
    sqlite_util::Transaction txn = std::move(txn_result.value());

    Status schema_status = db.db_.exec(kSchemaSql);
    if (!schema_status) return schema_status.error();

    Status version_status =
        db.db_.exec("PRAGMA user_version = " + std::to_string(kStateDbSchemaVersion) + ";");
    if (!version_status) return version_status.error();

    Status prepared_status = db.prepare_statements();
    if (!prepared_status) return prepared_status.error();

    Result<sqlite_util::Statement> job_stmt_result = sqlite_util::Statement::prepare(
        db.db_,
        "INSERT INTO job(id, run_id, schema_version, tool_version, source_root, output_pst, "
        "root_name, outlook_bitness, created_at_utc, last_updated_at_utc, manifest_hash, status) "
        "VALUES (1, ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);");
    if (!job_stmt_result) return job_stmt_result.error();
    sqlite_util::Statement job_stmt = std::move(job_stmt_result.value());

    Status s;
    if (!(s = job_stmt.bind_text_utf8(1, info.run_id))) return s.error();
    if (!(s = job_stmt.bind_int64(2, kStateDbSchemaVersion))) return s.error();
    if (!(s = job_stmt.bind_text_utf8(3, info.tool_version))) return s.error();
    if (!(s = job_stmt.bind_text(4, info.source_root))) return s.error();
    if (!(s = job_stmt.bind_text(5, info.output_pst))) return s.error();
    if (!(s = job_stmt.bind_text(6, info.root_name))) return s.error();
    if (!(s = job_stmt.bind_text_utf8(7, info.outlook_bitness))) return s.error();
    if (!(s = job_stmt.bind_int64(8, info.created_at_utc))) return s.error();
    if (!(s = job_stmt.bind_int64(9, info.created_at_utc))) return s.error();
    if (!(s = job_stmt.bind_text_utf8(10, info.manifest_hash))) return s.error();
    if (!(s = job_stmt.bind_text_utf8(11, "RUNNING"))) return s.error();
    Result<bool> job_step = job_stmt.step();
    if (!job_step) return job_step.error();

    for (size_t i = 0; i < manifest.size(); ++i) {
        const ManifestEntry& entry = manifest[i];
        const std::wstring& target_folder = file_target_folders[i].second;

        Status reset_status = db.stmt_insert_file_.reset();
        if (!reset_status) return reset_status.error();
        if (!(s = db.stmt_insert_file_.bind_text(1, entry.relative_path))) return s.error();
        if (!(s = db.stmt_insert_file_.bind_uint64(2, entry.size))) return s.error();
        if (!(s = db.stmt_insert_file_.bind_int64(3, entry.last_write_utc))) return s.error();
        if (!(s = db.stmt_insert_file_.bind_text(4, target_folder))) return s.error();
        if (!(s = db.stmt_insert_file_.bind_text_utf8(5, to_string(FileImportStatus::kPending))))
            return s.error();
        Result<bool> insert_step = db.stmt_insert_file_.step();
        if (!insert_step) return insert_step.error();
    }

    Status commit_status = txn.commit();
    if (!commit_status) return commit_status.error();

    return db;
}

Result<StateDb> StateDb::open_existing(const std::wstring& db_path) {
    Result<sqlite_util::Database> opened = sqlite_util::Database::open(db_path);
    if (!opened) return opened.error();

    StateDb db(std::move(opened.value()));
    Status prepared_status = db.prepare_statements();
    if (!prepared_status) return prepared_status.error();
    return db;
}

// ---------------------------------------------------------------------
// Job
// ---------------------------------------------------------------------

Result<LoadedJob> StateDb::load_job() {
    Result<sqlite_util::Statement> uv_prepared =
        sqlite_util::Statement::prepare(db_, "PRAGMA user_version;");
    if (!uv_prepared) return uv_prepared.error();
    sqlite_util::Statement uv_stmt = std::move(uv_prepared.value());
    Result<bool> uv_row = uv_stmt.step();
    if (!uv_row) return uv_row.error();
    int schema_version = uv_row.value() ? static_cast<int>(uv_stmt.column_int64(0)) : 0;

    Result<sqlite_util::Statement> prepared = sqlite_util::Statement::prepare(
        db_,
        "SELECT run_id, tool_version, source_root, output_pst, root_name, outlook_bitness, "
        "created_at_utc, manifest_hash, status FROM job WHERE id = 1;");
    if (!prepared) return prepared.error();
    sqlite_util::Statement stmt = std::move(prepared.value());
    Result<bool> has_row = stmt.step();
    if (!has_row) return has_row.error();
    if (!has_row.value()) {
        return make_error("StateDb::load_job", "job row not found (id=1)");
    }

    LoadedJob loaded;
    loaded.schema_version = schema_version;
    loaded.info.run_id = stmt.column_text_utf8(0);
    loaded.info.tool_version = stmt.column_text_utf8(1);
    loaded.info.source_root = stmt.column_text_wide(2);
    loaded.info.output_pst = stmt.column_text_wide(3);
    loaded.info.root_name = stmt.column_text_wide(4);
    loaded.info.outlook_bitness = stmt.column_text_utf8(5);
    loaded.info.created_at_utc = stmt.column_int64(6);
    loaded.info.manifest_hash = stmt.column_text_utf8(7);
    loaded.status = stmt.column_text_utf8(8);
    return loaded;
}

Status StateDb::set_job_status(const std::string& status, FileTimeUtc now) {
    Status reset_status = stmt_set_job_status_.reset();
    if (!reset_status) return reset_status;
    Status s;
    if (!(s = stmt_set_job_status_.bind_text_utf8(1, status))) return s;
    if (!(s = stmt_set_job_status_.bind_int64(2, now))) return s;
    Result<bool> stepped = stmt_set_job_status_.step();
    if (!stepped) return stepped.error();
    if (db_.changes() == 0) {
        return make_error("StateDb::set_job_status", "job row not found (id=1)");
    }
    return Status::success();
}

Status StateDb::check_resume_compatible(const JobInfo& expected, const std::string& manifest_hash,
                                         int expected_schema_version) {
    Result<LoadedJob> loaded = load_job();
    if (!loaded) return loaded.error();
    const JobInfo& info = loaded.value().info;

    if (info.source_root != expected.source_root) {
        return make_error("StateDb::check_resume_compatible", "resume mismatch: source_root");
    }
    if (info.output_pst != expected.output_pst) {
        return make_error("StateDb::check_resume_compatible", "resume mismatch: output_pst");
    }
    if (info.root_name != expected.root_name) {
        return make_error("StateDb::check_resume_compatible", "resume mismatch: root_name");
    }
    if (info.outlook_bitness != expected.outlook_bitness) {
        return make_error("StateDb::check_resume_compatible", "resume mismatch: outlook_bitness");
    }
    if (loaded.value().schema_version != expected_schema_version) {
        return make_error("StateDb::check_resume_compatible", "resume mismatch: schema_version");
    }
    if (info.manifest_hash != manifest_hash) {
        return make_error("StateDb::check_resume_compatible", "resume mismatch: manifest_hash");
    }
    return Status::success();
}

Status StateDb::touch_job_updated(FileTimeUtc now) {
    Status reset_status = stmt_touch_job_.reset();
    if (!reset_status) return reset_status;
    Status s = stmt_touch_job_.bind_int64(1, now);
    if (!s) return s;
    Result<bool> stepped = stmt_touch_job_.step();
    if (!stepped) return stepped.error();
    return Status::success();
}

// ---------------------------------------------------------------------
// Import lifecycle
// ---------------------------------------------------------------------

Status StateDb::mark_started(int64_t file_id, FileTimeUtc now) {
    Status reset_status = stmt_mark_started_.reset();
    if (!reset_status) return reset_status;
    Status s;
    if (!(s = stmt_mark_started_.bind_int64(1, now))) return s;
    if (!(s = stmt_mark_started_.bind_int64(2, file_id))) return s;
    Result<bool> stepped = stmt_mark_started_.step();
    if (!stepped) return stepped.error();
    if (db_.changes() == 0) {
        return make_error("StateDb::mark_started", "no files row found for id");
    }
    return touch_job_updated(now);
}

Status StateDb::set_sha256(int64_t file_id, const std::string& hex) {
    Status reset_status = stmt_set_sha256_.reset();
    if (!reset_status) return reset_status;
    Status s;
    if (!(s = stmt_set_sha256_.bind_text_utf8(1, hex))) return s;
    if (!(s = stmt_set_sha256_.bind_int64(2, file_id))) return s;
    Result<bool> stepped = stmt_set_sha256_.step();
    if (!stepped) return stepped.error();
    if (db_.changes() == 0) {
        return make_error("StateDb::set_sha256", "no files row found for id");
    }
    return touch_job_updated(now_filetime());
}

Status StateDb::mark_imported(int64_t file_id, const std::vector<uint8_t>& entry_id,
                               FileTimeUtc now) {
    Status reset_status = stmt_mark_imported_.reset();
    if (!reset_status) return reset_status;
    Status s;
    if (!(s = stmt_mark_imported_.bind_text_utf8(1, to_string(FileImportStatus::kImported))))
        return s;
    if (!(s = stmt_mark_imported_.bind_blob(2, entry_id))) return s;
    if (!(s = stmt_mark_imported_.bind_int64(3, now))) return s;
    if (!(s = stmt_mark_imported_.bind_int64(4, file_id))) return s;
    Result<bool> stepped = stmt_mark_imported_.step();
    if (!stepped) return stepped.error();
    if (db_.changes() == 0) {
        return make_error("StateDb::mark_imported", "no files row found for id");
    }
    return touch_job_updated(now);
}

Status StateDb::mark_status(int64_t file_id, FileImportStatus status, int32_t last_hresult,
                             FileTimeUtc now) {
    Status reset_status = stmt_mark_status_.reset();
    if (!reset_status) return reset_status;
    Status s;
    if (!(s = stmt_mark_status_.bind_text_utf8(1, to_string(status)))) return s;
    if (!(s = stmt_mark_status_.bind_int64(2, static_cast<int64_t>(last_hresult)))) return s;
    if (!(s = stmt_mark_status_.bind_int64(3, now))) return s;
    if (!(s = stmt_mark_status_.bind_int64(4, file_id))) return s;
    Result<bool> stepped = stmt_mark_status_.step();
    if (!stepped) return stepped.error();
    if (db_.changes() == 0) {
        return make_error("StateDb::mark_status", "no files row found for id");
    }
    return touch_job_updated(now);
}

Result<std::vector<StateDb::FileRow>> StateDb::query_files(const std::string& sql,
                                                             const FileImportStatus* filter) {
    Result<sqlite_util::Statement> prepared = sqlite_util::Statement::prepare(db_, sql);
    if (!prepared) return prepared.error();
    sqlite_util::Statement stmt = std::move(prepared.value());

    if (filter != nullptr) {
        Status bind_status = stmt.bind_text_utf8(1, to_string(*filter));
        if (!bind_status) return bind_status.error();
    }

    std::vector<FileRow> rows;
    for (;;) {
        Result<bool> has_row = stmt.step();
        if (!has_row) return has_row.error();
        if (!has_row.value()) break;

        FileRow row;
        row.id = stmt.column_int64(0);
        row.relative_source_path = stmt.column_text_wide(1);
        row.source_size = stmt.column_uint64(2);
        row.source_last_write_utc = stmt.column_int64(3);
        row.source_sha256 = stmt.column_is_null(4) ? std::string{} : stmt.column_text_utf8(4);
        row.target_folder_path = stmt.column_text_wide(5);
        row.target_entry_id = stmt.column_blob(6);

        std::string status_text = stmt.column_text_utf8(7);
        if (!file_import_status_from_string(status_text, row.status)) {
            return make_error("StateDb::query_files",
                               "unknown file status in database: " + status_text);
        }
        row.attempt_count = static_cast<int>(stmt.column_int64(8));
        row.last_hresult =
            stmt.column_is_null(9) ? 0 : static_cast<int32_t>(stmt.column_int64(9));

        rows.push_back(std::move(row));
    }
    return rows;
}

Result<std::vector<StateDb::FileRow>> StateDb::load_all_files() {
    return query_files(std::string(kSelectFilesColumns) + " ORDER BY id;", nullptr);
}

Result<std::vector<StateDb::FileRow>> StateDb::load_files_with_status(FileImportStatus status) {
    return query_files(std::string(kSelectFilesColumns) + " WHERE status = ?1 ORDER BY id;",
                        &status);
}

// ---------------------------------------------------------------------
// Folder map
// ---------------------------------------------------------------------

Status StateDb::upsert_folder(const std::wstring& source_rel, const std::wstring& target_rel,
                               const std::string& rename_reason) {
    Status reset_status = stmt_upsert_folder_.reset();
    if (!reset_status) return reset_status;
    Status s;
    if (!(s = stmt_upsert_folder_.bind_text(1, source_rel))) return s;
    if (!(s = stmt_upsert_folder_.bind_text(2, target_rel))) return s;
    if (!(s = stmt_upsert_folder_.bind_text_utf8(3, rename_reason))) return s;
    Result<bool> stepped = stmt_upsert_folder_.step();
    if (!stepped) return stepped.error();
    return touch_job_updated(now_filetime());
}

Status StateDb::set_folder_entry_id(const std::wstring& target_rel,
                                     const std::vector<uint8_t>& entry_id) {
    Status reset_status = stmt_set_folder_entry_id_.reset();
    if (!reset_status) return reset_status;
    Status s;
    if (!(s = stmt_set_folder_entry_id_.bind_blob(1, entry_id))) return s;
    if (!(s = stmt_set_folder_entry_id_.bind_text(2, target_rel))) return s;
    Result<bool> stepped = stmt_set_folder_entry_id_.step();
    if (!stepped) return stepped.error();
    if (db_.changes() == 0) {
        return make_error("StateDb::set_folder_entry_id",
                           "no folder_map row found for target_relative_folder");
    }
    return touch_job_updated(now_filetime());
}

Result<std::vector<StateDb::FolderRow>> StateDb::load_folders() {
    Result<sqlite_util::Statement> prepared = sqlite_util::Statement::prepare(
        db_,
        "SELECT source_relative_folder, target_relative_folder, target_entry_id, rename_reason "
        "FROM folder_map ORDER BY source_relative_folder;");
    if (!prepared) return prepared.error();
    sqlite_util::Statement stmt = std::move(prepared.value());

    std::vector<FolderRow> rows;
    for (;;) {
        Result<bool> has_row = stmt.step();
        if (!has_row) return has_row.error();
        if (!has_row.value()) break;

        FolderRow row;
        row.source_relative_folder = stmt.column_text_wide(0);
        row.target_relative_folder = stmt.column_text_wide(1);
        row.target_entry_id = stmt.column_blob(2);
        row.rename_reason = stmt.column_is_null(3) ? std::string{} : stmt.column_text_utf8(3);
        rows.push_back(std::move(row));
    }
    return rows;
}

// ---------------------------------------------------------------------
// Validation support
// ---------------------------------------------------------------------

Result<StateDb::StatusCounts> StateDb::count_by_status() {
    Result<sqlite_util::Statement> prepared =
        sqlite_util::Statement::prepare(db_, "SELECT status, COUNT(*) FROM files GROUP BY status;");
    if (!prepared) return prepared.error();
    sqlite_util::Statement stmt = std::move(prepared.value());

    StatusCounts counts;
    for (;;) {
        Result<bool> has_row = stmt.step();
        if (!has_row) return has_row.error();
        if (!has_row.value()) break;

        std::string status_text = stmt.column_text_utf8(0);
        uint64_t n = stmt.column_uint64(1);
        FileImportStatus status;
        if (!file_import_status_from_string(status_text, status)) {
            return make_error("StateDb::count_by_status",
                               "unknown file status in database: " + status_text);
        }
        switch (status) {
            case FileImportStatus::kPending: counts.pending = n; break;
            case FileImportStatus::kImported: counts.imported = n; break;
            case FileImportStatus::kPreservedAsAttachment: counts.preserved = n; break;
            case FileImportStatus::kFailedSourceRead: counts.failed_read = n; break;
            case FileImportStatus::kFailedMapi: counts.failed_mapi = n; break;
        }
    }
    return counts;
}

Result<std::vector<std::pair<std::wstring, uint64_t>>> StateDb::counts_per_folder_for_status(
    FileImportStatus status) {
    Result<sqlite_util::Statement> prepared = sqlite_util::Statement::prepare(
        db_,
        "SELECT target_folder_path, COUNT(*) FROM files WHERE status = ?1 GROUP BY "
        "target_folder_path ORDER BY target_folder_path;");
    if (!prepared) return prepared.error();
    sqlite_util::Statement stmt = std::move(prepared.value());
    Status bind_status = stmt.bind_text_utf8(1, to_string(status));
    if (!bind_status) return bind_status.error();

    std::vector<std::pair<std::wstring, uint64_t>> result;
    for (;;) {
        Result<bool> has_row = stmt.step();
        if (!has_row) return has_row.error();
        if (!has_row.value()) break;
        result.emplace_back(stmt.column_text_wide(0), stmt.column_uint64(1));
    }
    return result;
}

Result<std::vector<std::pair<std::wstring, uint64_t>>> StateDb::imported_counts_per_folder() {
    return counts_per_folder_for_status(FileImportStatus::kImported);
}

Result<std::vector<std::pair<std::wstring, uint64_t>>> StateDb::preserved_counts_per_folder() {
    return counts_per_folder_for_status(FileImportStatus::kPreservedAsAttachment);
}

// ---------------------------------------------------------------------
// Batching
// ---------------------------------------------------------------------

Result<sqlite_util::Transaction> StateDb::begin_batch() {
    return sqlite_util::Transaction::begin_immediate(db_);
}

}  // namespace wlm2pst
