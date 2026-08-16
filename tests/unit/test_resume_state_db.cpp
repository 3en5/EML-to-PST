// Unit tests for wlm2pst::StateDb / wlm2pst::sqlite_util (spec section 17).
#include "worker/resume/state_db.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "common/errors/result.h"
#include "common/model.h"
#include "worker/resume/sqlite_util.h"

namespace {

using wlm2pst::FileImportStatus;
using wlm2pst::FileTimeUtc;
using wlm2pst::JobInfo;
using wlm2pst::ManifestEntry;
using wlm2pst::StateDb;

// ASCII-only widening: adequate for the synthetic temp-file paths this test
// file constructs itself. (Hebrew round-trip is exercised separately using
// wide string literals, not this helper.)
std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// Unique temp DB path per test invocation. Removes itself (and WAL/SHM/
// journal sidecar files) on destruction.
class TempDbPath {
public:
    explicit TempDbPath(const std::string& tag) {
        static std::atomic<uint64_t> counter{0};
        uint64_t n = counter.fetch_add(1);
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path dir = std::filesystem::temp_directory_path();
        std::string name =
            "wlm2pst_resume_test_" + tag + "_" + std::to_string(now) + "_" + std::to_string(n) + ".sqlite";
        path_ = (dir / name).string();
    }

    ~TempDbPath() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        std::filesystem::remove(path_ + "-wal", ec);
        std::filesystem::remove(path_ + "-shm", ec);
        std::filesystem::remove(path_ + "-journal", ec);
    }

    TempDbPath(const TempDbPath&) = delete;
    TempDbPath& operator=(const TempDbPath&) = delete;

    [[nodiscard]] std::wstring wide() const { return widen(path_); }
    [[nodiscard]] const std::string& narrow_path() const { return path_; }

private:
    std::string path_;
};

JobInfo make_job_info() {
    JobInfo info;
    info.run_id = "11111111-2222-3333-4444-555555555555";
    info.tool_version = "1.0.0-test";
    info.source_root = widen("/source/root");
    info.output_pst = widen("/out/Sample-Mail.pst");
    info.root_name = widen("Sample Mail");
    info.outlook_bitness = "x64";
    info.created_at_utc = wlm2pst::filetime_from_unix_seconds(1700000000);
    info.manifest_hash = "deadbeefcafebabe0011223344556677";
    return info;
}

std::vector<ManifestEntry> make_manifest(int count, const std::wstring& folder_prefix = L"Inbox") {
    std::vector<ManifestEntry> manifest;
    manifest.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        ManifestEntry entry;
        entry.relative_path = folder_prefix + L"\\" + std::to_wstring(i) + L".eml";
        entry.absolute_path = L"/source/root/" + folder_prefix + L"/" + std::to_wstring(i) + L".eml";
        entry.size = 1000ull + static_cast<uint64_t>(i);
        entry.last_write_utc = wlm2pst::filetime_from_unix_seconds(1700000000 + i);
        manifest.push_back(entry);
    }
    return manifest;
}

std::vector<std::pair<std::wstring, std::wstring>> target_folders_for(
    const std::vector<ManifestEntry>& manifest, const std::wstring& target_folder = L"Inbox") {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    out.reserve(manifest.size());
    for (const auto& entry : manifest) {
        out.emplace_back(entry.relative_path, target_folder);
    }
    return out;
}

}  // namespace

TEST_CASE("create_new inserts job and files atomically", "[resume]") {
    TempDbPath db_path("create_new");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(5);
    auto folders = target_folders_for(manifest);

    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());

    auto job = db.load_job();
    REQUIRE(job.ok());
    CHECK(job.value().info.run_id == info.run_id);
    CHECK(job.value().info.tool_version == info.tool_version);
    CHECK(job.value().info.source_root == info.source_root);
    CHECK(job.value().info.output_pst == info.output_pst);
    CHECK(job.value().info.root_name == info.root_name);
    CHECK(job.value().info.outlook_bitness == info.outlook_bitness);
    CHECK(job.value().info.created_at_utc == info.created_at_utc);
    CHECK(job.value().info.manifest_hash == info.manifest_hash);
    CHECK(job.value().status == "RUNNING");
    CHECK(job.value().schema_version == wlm2pst::kStateDbSchemaVersion);

    auto files = db.load_all_files();
    REQUIRE(files.ok());
    REQUIRE(files.value().size() == 5);
    for (const auto& row : files.value()) {
        CHECK(row.status == FileImportStatus::kPending);
        CHECK(row.attempt_count == 0);
        CHECK(row.target_folder_path == L"Inbox");
        CHECK(row.source_sha256.empty());
        CHECK(row.target_entry_id.empty());
    }
}

TEST_CASE("create_new to an unreachable path returns a clean error", "[resume]") {
    // A path component that is a regular file (not a directory) makes
    // sqlite3_open_v2 fail deterministically regardless of process
    // privilege (this differs from a permission-denied directory, which
    // root can write through) -- this simulates "cannot create the state
    // database at the destination" (spec section 17/18) in a way that is
    // robust to running tests as root.
    TempDbPath blocker("blocker_file");
    {
        std::ofstream touch(blocker.narrow_path());
        touch << "not a directory";
    }
    std::wstring bad_path = blocker.wide() + widen("/state.sqlite");

    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(1);
    auto folders = target_folders_for(manifest);

    auto created = StateDb::create_new(bad_path, info, manifest, folders);
    REQUIRE_FALSE(created.ok());
    CHECK_FALSE(created.error().message.empty());
}

TEST_CASE("reopen round-trips job and Hebrew wide paths", "[resume]") {
    TempDbPath db_path("hebrew");
    JobInfo info = make_job_info();
    info.root_name = L"תיבת דואר לדוגמה";  // "sample mailbox" in Hebrew
    info.source_root = L"/source/מקור/root";

    std::vector<ManifestEntry> manifest;
    ManifestEntry entry;
    entry.relative_path = L"תיקייה\\הודעה.eml";  // folder\message.eml
    entry.absolute_path = L"/source/root/" + entry.relative_path;
    entry.size = 4242;
    entry.last_write_utc = wlm2pst::filetime_from_unix_seconds(1700001234);
    manifest.push_back(entry);
    auto folders = target_folders_for(manifest, L"תיקייה");

    {
        auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
        REQUIRE(created.ok());
        // created.value() destructs here, closing the connection.
    }

    auto reopened = StateDb::open_existing(db_path.wide());
    REQUIRE(reopened.ok());
    StateDb db = std::move(reopened.value());

    auto job = db.load_job();
    REQUIRE(job.ok());
    CHECK(job.value().info.root_name == info.root_name);
    CHECK(job.value().info.source_root == info.source_root);

    auto files = db.load_all_files();
    REQUIRE(files.ok());
    REQUIRE(files.value().size() == 1);
    CHECK(files.value()[0].relative_source_path == entry.relative_path);
    CHECK(files.value()[0].target_folder_path == folders[0].second);
}

TEST_CASE("mark lifecycle transitions and attempt_count increments", "[resume]") {
    TempDbPath db_path("lifecycle");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(1);
    auto folders = target_folders_for(manifest);
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());

    int64_t file_id = db.load_all_files().value().front().id;

    FileTimeUtc t1 = wlm2pst::filetime_from_unix_seconds(1700000100);
    REQUIRE(db.mark_started(file_id, t1).ok());
    {
        auto files = db.load_all_files();
        REQUIRE(files.ok());
        CHECK(files.value()[0].attempt_count == 1);
        CHECK(files.value()[0].status == FileImportStatus::kPending);
    }

    FileTimeUtc t2 = wlm2pst::filetime_from_unix_seconds(1700000200);
    REQUIRE(db.mark_started(file_id, t2).ok());
    {
        auto files = db.load_all_files();
        REQUIRE(files.ok());
        CHECK(files.value()[0].attempt_count == 2);
    }

    REQUIRE(db.set_sha256(file_id, "aabbccddeeff00112233445566778899").ok());
    {
        auto files = db.load_all_files();
        REQUIRE(files.ok());
        CHECK(files.value()[0].source_sha256 == "aabbccddeeff00112233445566778899");
    }

    std::vector<uint8_t> entry_id{0x01, 0x02, 0x03, 0x00, 0xFF};
    FileTimeUtc t3 = wlm2pst::filetime_from_unix_seconds(1700000300);
    REQUIRE(db.mark_imported(file_id, entry_id, t3).ok());
    {
        auto files = db.load_all_files();
        REQUIRE(files.ok());
        CHECK(files.value()[0].status == FileImportStatus::kImported);
        CHECK(files.value()[0].target_entry_id == entry_id);
        CHECK(files.value()[0].attempt_count == 2);
    }

    // Unknown file id -> clean error, not silent no-op.
    CHECK_FALSE(db.mark_started(999999, t3).ok());
}

TEST_CASE("mark_status records failure codes", "[resume]") {
    TempDbPath db_path("mark_status");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(1);
    auto folders = target_folders_for(manifest);
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());
    int64_t file_id = db.load_all_files().value().front().id;

    FileTimeUtc now = wlm2pst::filetime_from_unix_seconds(1700000500);
    REQUIRE(db.mark_status(file_id, FileImportStatus::kFailedMapi, -2147024891, now).ok());

    auto files = db.load_all_files();
    REQUIRE(files.ok());
    CHECK(files.value()[0].status == FileImportStatus::kFailedMapi);
    CHECK(files.value()[0].last_hresult == -2147024891);
}

TEST_CASE("entry_id blob round-trips, including empty and embedded NUL", "[resume]") {
    TempDbPath db_path("blob_roundtrip");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(2);
    auto folders = target_folders_for(manifest);
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());
    auto files = db.load_all_files().value();

    FileTimeUtc now = wlm2pst::filetime_from_unix_seconds(1700000600);
    std::vector<uint8_t> empty_id;
    std::vector<uint8_t> binary_id{0x00, 0x01, 0x00, 0xAB, 0xCD, 0x00, 0xFF};

    REQUIRE(db.mark_imported(files[0].id, empty_id, now).ok());
    REQUIRE(db.mark_imported(files[1].id, binary_id, now).ok());

    auto reloaded = db.load_all_files();
    REQUIRE(reloaded.ok());
    CHECK(reloaded.value()[0].target_entry_id.empty());
    CHECK(reloaded.value()[1].target_entry_id == binary_id);
}

TEST_CASE("count_by_status aggregates correctly", "[resume]") {
    TempDbPath db_path("counts");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(6);
    auto folders = target_folders_for(manifest);
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());
    auto files = db.load_all_files().value();

    FileTimeUtc now = wlm2pst::filetime_from_unix_seconds(1700000700);
    REQUIRE(db.mark_imported(files[0].id, {0x01}, now).ok());
    REQUIRE(db.mark_imported(files[1].id, {0x02}, now).ok());
    REQUIRE(db.mark_status(files[2].id, FileImportStatus::kPreservedAsAttachment, 0, now).ok());
    REQUIRE(db.mark_status(files[3].id, FileImportStatus::kFailedSourceRead, 5, now).ok());
    REQUIRE(db.mark_status(files[4].id, FileImportStatus::kFailedMapi, 6, now).ok());
    // files[5] stays PENDING.

    auto counts = db.count_by_status();
    REQUIRE(counts.ok());
    CHECK(counts.value().pending == 1);
    CHECK(counts.value().imported == 2);
    CHECK(counts.value().preserved == 1);
    CHECK(counts.value().failed_read == 1);
    CHECK(counts.value().failed_mapi == 1);
}

TEST_CASE("imported/preserved counts per folder", "[resume]") {
    TempDbPath db_path("counts_per_folder");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> inbox = make_manifest(3, L"Inbox");
    std::vector<ManifestEntry> sent = make_manifest(2, L"Sent Items");
    std::vector<ManifestEntry> manifest = inbox;
    manifest.insert(manifest.end(), sent.begin(), sent.end());

    std::vector<std::pair<std::wstring, std::wstring>> folders;
    for (const auto& e : inbox) folders.emplace_back(e.relative_path, L"Inbox");
    for (const auto& e : sent) folders.emplace_back(e.relative_path, L"Sent Items");

    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());
    auto files = db.load_all_files().value();

    FileTimeUtc now = wlm2pst::filetime_from_unix_seconds(1700000800);
    // Inbox: 2 imported, 1 preserved. Sent Items: 1 imported.
    REQUIRE(db.mark_imported(files[0].id, {0x01}, now).ok());
    REQUIRE(db.mark_imported(files[1].id, {0x02}, now).ok());
    REQUIRE(db.mark_status(files[2].id, FileImportStatus::kPreservedAsAttachment, 0, now).ok());
    REQUIRE(db.mark_imported(files[3].id, {0x03}, now).ok());
    // files[4] (Sent Items) stays PENDING.

    auto imported = db.imported_counts_per_folder();
    REQUIRE(imported.ok());
    REQUIRE(imported.value().size() == 2);
    CHECK(imported.value()[0].first == L"Inbox");
    CHECK(imported.value()[0].second == 2);
    CHECK(imported.value()[1].first == L"Sent Items");
    CHECK(imported.value()[1].second == 1);

    auto preserved = db.preserved_counts_per_folder();
    REQUIRE(preserved.ok());
    REQUIRE(preserved.value().size() == 1);
    CHECK(preserved.value()[0].first == L"Inbox");
    CHECK(preserved.value()[0].second == 1);
}

TEST_CASE("folder upsert and entry_id", "[resume]") {
    TempDbPath db_path("folders");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(1);
    auto folders = target_folders_for(manifest);
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());

    REQUIRE(db.upsert_folder(L"Inbox", L"Inbox", "").ok());
    REQUIRE(db.upsert_folder(L"Sent Items", L"Sent Items", "sent-alias").ok());

    {
        auto rows = db.load_folders();
        REQUIRE(rows.ok());
        REQUIRE(rows.value().size() == 2);
    }

    // Re-upserting the same source folder updates in place, not a new row.
    REQUIRE(db.upsert_folder(L"Inbox", L"Inbox (2)", "collision").ok());
    {
        auto rows = db.load_folders();
        REQUIRE(rows.ok());
        REQUIRE(rows.value().size() == 2);
        bool found = false;
        for (const auto& row : rows.value()) {
            if (row.source_relative_folder == L"Inbox") {
                found = true;
                CHECK(row.target_relative_folder == L"Inbox (2)");
                CHECK(row.rename_reason == "collision");
            }
        }
        CHECK(found);
    }

    std::vector<uint8_t> entry_id{0xAA, 0xBB, 0xCC};
    REQUIRE(db.set_folder_entry_id(L"Inbox (2)", entry_id).ok());
    {
        auto rows = db.load_folders();
        REQUIRE(rows.ok());
        bool found = false;
        for (const auto& row : rows.value()) {
            if (row.target_relative_folder == L"Inbox (2)") {
                found = true;
                CHECK(row.target_entry_id == entry_id);
            }
        }
        CHECK(found);
    }

    // Unknown target folder -> clean error.
    CHECK_FALSE(db.set_folder_entry_id(L"Does Not Exist", entry_id).ok());
}

TEST_CASE("load_files_with_status after mixed updates", "[resume]") {
    TempDbPath db_path("status_filter");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(4);
    auto folders = target_folders_for(manifest);
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());
    auto files = db.load_all_files().value();

    FileTimeUtc now = wlm2pst::filetime_from_unix_seconds(1700000900);
    REQUIRE(db.mark_imported(files[0].id, {0x01}, now).ok());
    REQUIRE(db.mark_status(files[1].id, FileImportStatus::kFailedSourceRead, 1, now).ok());
    // files[2], files[3] stay PENDING.

    auto pending = db.load_files_with_status(FileImportStatus::kPending);
    REQUIRE(pending.ok());
    REQUIRE(pending.value().size() == 2);
    CHECK(pending.value()[0].id == files[2].id);
    CHECK(pending.value()[1].id == files[3].id);

    auto imported = db.load_files_with_status(FileImportStatus::kImported);
    REQUIRE(imported.ok());
    REQUIRE(imported.value().size() == 1);
    CHECK(imported.value()[0].id == files[0].id);
}

TEST_CASE("batch transaction commits and rolls back on destruction", "[resume]") {
    TempDbPath db_path("batch_txn");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(1);
    auto folders = target_folders_for(manifest);
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());
    int64_t file_id = db.load_all_files().value().front().id;

    // Committed batch: change is visible afterward.
    {
        auto txn = db.begin_batch();
        REQUIRE(txn.ok());
        REQUIRE(db.set_sha256(file_id, "committed-hash-aaaa").ok());
        REQUIRE(txn.value().commit().ok());
    }
    CHECK(db.load_all_files().value()[0].source_sha256 == "committed-hash-aaaa");

    // Uncommitted batch: destroyed without commit() -> rolled back.
    {
        auto txn = db.begin_batch();
        REQUIRE(txn.ok());
        REQUIRE(db.set_sha256(file_id, "should-not-persist").ok());
        // txn goes out of scope here without commit() -> auto-rollback.
    }
    CHECK(db.load_all_files().value()[0].source_sha256 == "committed-hash-aaaa");
}

TEST_CASE("WAL journal mode is active", "[resume]") {
    TempDbPath db_path("wal_mode");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(1);
    auto folders = target_folders_for(manifest);
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());

    auto opened = wlm2pst::sqlite_util::Database::open(db_path.wide());
    REQUIRE(opened.ok());
    auto stmt = wlm2pst::sqlite_util::Statement::prepare(opened.value(), "PRAGMA journal_mode;");
    REQUIRE(stmt.ok());
    auto row = stmt.value().step();
    REQUIRE(row.ok());
    REQUIRE(row.value());
    CHECK(stmt.value().column_text_utf8(0) == "wal");
}

TEST_CASE("schema_version is stored and read via user_version", "[resume]") {
    TempDbPath db_path("schema_version");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(1);
    auto folders = target_folders_for(manifest);
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    StateDb db = std::move(created.value());

    auto job = db.load_job();
    REQUIRE(job.ok());
    CHECK(job.value().schema_version == wlm2pst::kStateDbSchemaVersion);

    auto opened = wlm2pst::sqlite_util::Database::open(db_path.wide());
    REQUIRE(opened.ok());
    auto stmt = wlm2pst::sqlite_util::Statement::prepare(opened.value(), "PRAGMA user_version;");
    REQUIRE(stmt.ok());
    auto row = stmt.value().step();
    REQUIRE(row.ok());
    REQUIRE(row.value());
    CHECK(stmt.value().column_int64(0) == wlm2pst::kStateDbSchemaVersion);
}

TEST_CASE("UNIQUE violation on duplicate relative path is a clean error and rolls back", "[resume]") {
    TempDbPath db_path("unique_violation");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(1);
    manifest.push_back(manifest[0]);  // duplicate relative_path
    auto folders = target_folders_for(manifest);

    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE_FALSE(created.ok());
    CHECK(created.error().hresult != 0);

    // The whole transaction (schema included) must have rolled back: the
    // schema itself is gone, so even opening the database for resumed use
    // fails cleanly (no tables to prepare statements against) instead of
    // exposing a half-populated job/files table.
    auto reopened = StateDb::open_existing(db_path.wide());
    CHECK_FALSE(reopened.ok());
}

TEST_CASE("10k row insert performance sanity", "[resume]") {
    TempDbPath db_path("perf");
    JobInfo info = make_job_info();
    std::vector<ManifestEntry> manifest = make_manifest(10000);
    auto folders = target_folders_for(manifest);

    auto start = std::chrono::steady_clock::now();
    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(created.ok());

    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 2000);

    auto files = created.value().load_all_files();
    REQUIRE(files.ok());
    CHECK(files.value().size() == 10000);
}
