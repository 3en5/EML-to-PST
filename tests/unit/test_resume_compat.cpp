// Unit tests for StateDb::check_resume_compatible (spec section 17, "Resume
// rules"): each identity field must be verified before a resumed run is
// allowed to reuse an existing state database / PST.
#include "worker/resume/state_db.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "common/model.h"

namespace {

using wlm2pst::JobInfo;
using wlm2pst::ManifestEntry;
using wlm2pst::StateDb;
using wlm2pst::Status;

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

class TempDbPath {
public:
    explicit TempDbPath(const std::string& tag) {
        static std::atomic<uint64_t> counter{0};
        uint64_t n = counter.fetch_add(1);
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path dir = std::filesystem::temp_directory_path();
        std::string name = "wlm2pst_resume_compat_" + tag + "_" + std::to_string(now) + "_" +
                            std::to_string(n) + ".sqlite";
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

private:
    std::string path_;
};

JobInfo make_baseline_job_info() {
    JobInfo info;
    info.run_id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    info.tool_version = "1.0.0-test";
    info.source_root = widen("/source/root");
    info.output_pst = widen("/out/Sample-Mail.pst");
    info.root_name = widen("Sample Mail");
    info.outlook_bitness = "x64";
    info.created_at_utc = wlm2pst::filetime_from_unix_seconds(1700000000);
    info.manifest_hash = "0123456789abcdef0123456789abcdef";
    return info;
}

// Returns a freshly created StateDb whose job row matches make_baseline_job_info().
StateDb create_baseline(const TempDbPath& db_path) {
    JobInfo info = make_baseline_job_info();
    ManifestEntry entry;
    entry.relative_path = L"Inbox\\0.eml";
    entry.absolute_path = L"/source/root/Inbox/0.eml";
    entry.size = 100;
    entry.last_write_utc = wlm2pst::filetime_from_unix_seconds(1700000000);
    std::vector<ManifestEntry> manifest{entry};
    std::vector<std::pair<std::wstring, std::wstring>> folders{{entry.relative_path, L"Inbox"}};

    auto created = StateDb::create_new(db_path.wide(), info, manifest, folders);
    REQUIRE(created.ok());
    return std::move(created.value());
}

}  // namespace

TEST_CASE("check_resume_compatible passes when every field matches", "[resume]") {
    TempDbPath db_path("match");
    StateDb db = create_baseline(db_path);
    JobInfo expected = make_baseline_job_info();

    Status result =
        db.check_resume_compatible(expected, expected.manifest_hash, wlm2pst::kStateDbSchemaVersion);
    CHECK(result.ok());
}

TEST_CASE("check_resume_compatible catches a source_root mismatch", "[resume]") {
    TempDbPath db_path("source_root");
    StateDb db = create_baseline(db_path);
    JobInfo expected = make_baseline_job_info();
    expected.source_root = L"/a/completely/different/source";

    Status result =
        db.check_resume_compatible(expected, expected.manifest_hash, wlm2pst::kStateDbSchemaVersion);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("source_root") != std::string::npos);
}

TEST_CASE("check_resume_compatible catches an output_pst mismatch", "[resume]") {
    TempDbPath db_path("output_pst");
    StateDb db = create_baseline(db_path);
    JobInfo expected = make_baseline_job_info();
    expected.output_pst = L"/somewhere/else/Other.pst";

    Status result =
        db.check_resume_compatible(expected, expected.manifest_hash, wlm2pst::kStateDbSchemaVersion);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("output_pst") != std::string::npos);
}

TEST_CASE("check_resume_compatible catches a root_name mismatch", "[resume]") {
    TempDbPath db_path("root_name");
    StateDb db = create_baseline(db_path);
    JobInfo expected = make_baseline_job_info();
    expected.root_name = L"Different Root";

    Status result =
        db.check_resume_compatible(expected, expected.manifest_hash, wlm2pst::kStateDbSchemaVersion);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("root_name") != std::string::npos);
}

TEST_CASE("check_resume_compatible catches an outlook_bitness mismatch", "[resume]") {
    TempDbPath db_path("outlook_bitness");
    StateDb db = create_baseline(db_path);
    JobInfo expected = make_baseline_job_info();
    expected.outlook_bitness = "x86";

    Status result =
        db.check_resume_compatible(expected, expected.manifest_hash, wlm2pst::kStateDbSchemaVersion);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("outlook_bitness") != std::string::npos);
}

TEST_CASE("check_resume_compatible catches a schema_version mismatch", "[resume]") {
    TempDbPath db_path("schema_version");
    StateDb db = create_baseline(db_path);
    JobInfo expected = make_baseline_job_info();

    Status result = db.check_resume_compatible(expected, expected.manifest_hash,
                                                 wlm2pst::kStateDbSchemaVersion + 1);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("schema_version") != std::string::npos);
}

TEST_CASE("check_resume_compatible catches a manifest_hash mismatch", "[resume]") {
    TempDbPath db_path("manifest_hash");
    StateDb db = create_baseline(db_path);
    JobInfo expected = make_baseline_job_info();

    Status result =
        db.check_resume_compatible(expected, "ffffffffffffffffffffffffffffffff", wlm2pst::kStateDbSchemaVersion);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().message.find("manifest_hash") != std::string::npos);
}
