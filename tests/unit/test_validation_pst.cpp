#include "worker/validation/validator.h"

#include "fake_pst_session.h"
#include "worker/resume/state_db.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <random>

using namespace wlm2pst;
using test::FakePstSession;
namespace fs = std::filesystem;

namespace {

constexpr FileTimeUtc kNow = filetime_from_unix_seconds(1'770'000'000);
constexpr const char* kRunId = "run-validate-1";

struct ValidationHarness {
    fs::path root;
    std::optional<StateDb> db;
    FakePstSession session;

    // files: {relative path, target folder, final status}
    explicit ValidationHarness(
        const std::vector<std::tuple<std::wstring, std::wstring, FileImportStatus>>& files) {
        root = fs::temp_directory_path() /
               fs::path("wlm2pst-val-" + std::to_string(std::random_device{}()));
        fs::create_directories(root);

        std::vector<ManifestEntry> manifest;
        std::vector<std::pair<std::wstring, std::wstring>> targets;
        for (const auto& [rel, folder, status] : files) {
            ManifestEntry e;
            e.relative_path = rel;
            e.absolute_path = rel;
            e.size = 10;
            e.last_write_utc = kNow;
            manifest.push_back(e);
            targets.emplace_back(rel, folder);
        }
        JobInfo info;
        info.run_id = kRunId;
        info.tool_version = "1.0.0";
        info.source_root = L"C:\\src";
        info.output_pst = L"C:\\out\\o.pst";
        info.root_name = L"Windows Live Mail";
        info.outlook_bitness = "x64";
        info.created_at_utc = kNow;
        info.manifest_hash = "h";
        auto created = StateDb::create_new((root / "s.sqlite").wstring(), info, manifest, targets);
        REQUIRE(created.ok());
        db.emplace(std::move(created.value()));

        int64_t id = 1;
        for (const auto& [rel, folder, status] : files) {
            if (status == FileImportStatus::kImported) {
                TrackedMessage m;
                m.source_relative_path = rel;
                m.run_id = kRunId;
                m.sha256_hex = "aa";
                session.seed_message(folder, m);
                REQUIRE(db->set_sha256(id, "aa").ok());
                REQUIRE(db->mark_imported(id, {1, 2, 3}, kNow).ok());
            } else if (status == FileImportStatus::kPreservedAsAttachment) {
                TrackedMessage m;
                m.source_relative_path = rel;
                m.run_id = kRunId;
                m.has_attachments = true;
                session.seed_message(kConversionErrorsFolderName, m);
                REQUIRE(db->mark_status(id, status, 0, kNow).ok());
            }
            ++id;
        }
    }

    ~ValidationHarness() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

}  // namespace

TEST_CASE("validation passes when PST matches the database", "[validation]") {
    ValidationHarness h({{L"Inbox\\a.eml", L"Inbox", FileImportStatus::kImported},
                         {L"Inbox\\b.eml", L"Inbox", FileImportStatus::kImported}});
    auto outcome = validate_pst(h.session, h.db.value(), kRunId);
    REQUIRE(outcome.ok());
    CHECK(outcome.value().status == ValidationStatus::kValidated);
    CHECK(outcome.value().issues.empty());
}

TEST_CASE("validation reports fallbacks distinctly", "[validation]") {
    ValidationHarness h({{L"Inbox\\a.eml", L"Inbox", FileImportStatus::kImported},
                         {L"bad.eml", L"Root Messages",
                          FileImportStatus::kPreservedAsAttachment}});
    auto outcome = validate_pst(h.session, h.db.value(), kRunId);
    REQUIRE(outcome.ok());
    CHECK(outcome.value().status == ValidationStatus::kValidatedWithFallbacks);
    CHECK(outcome.value().issues.empty());
}

TEST_CASE("validation fails when an expected folder is missing", "[validation]") {
    ValidationHarness h({{L"Inbox\\a.eml", L"Inbox", FileImportStatus::kImported}});
    // Simulate a vanished folder by clearing the fake store.
    h.session.folders().clear();
    auto outcome = validate_pst(h.session, h.db.value(), kRunId);
    REQUIRE(outcome.ok());
    CHECK(outcome.value().status == ValidationStatus::kValidationFailed);
    REQUIRE_FALSE(outcome.value().issues.empty());
}

TEST_CASE("validation fails on message count mismatch", "[validation]") {
    ValidationHarness h({{L"Inbox\\a.eml", L"Inbox", FileImportStatus::kImported}});
    TrackedMessage extra;
    extra.source_relative_path = L"Inbox\\a.eml";
    extra.run_id = kRunId;
    extra.sha256_hex = "aa";
    h.session.seed_message(L"Inbox", extra);  // duplicate in the PST
    auto outcome = validate_pst(h.session, h.db.value(), kRunId);
    REQUIRE(outcome.ok());
    CHECK(outcome.value().status == ValidationStatus::kValidationFailed);
    bool count_issue = false, dup_issue = false;
    for (const auto& i : outcome.value().issues) {
        if (i.detail.find("count mismatch") != std::string::npos) count_issue = true;
        if (i.detail.find("duplicate") != std::string::npos) dup_issue = true;
    }
    CHECK(count_issue);
    CHECK(dup_issue);
}

TEST_CASE("validation fails when an imported message vanished", "[validation]") {
    ValidationHarness h({{L"Inbox\\a.eml", L"Inbox", FileImportStatus::kImported},
                         {L"Inbox\\b.eml", L"Inbox", FileImportStatus::kImported}});
    // Remove one message from the fake PST.
    for (auto& [key, folder] : h.session.folders()) {
        if (folder.relative_path == L"Inbox") folder.messages.pop_back();
    }
    auto outcome = validate_pst(h.session, h.db.value(), kRunId);
    REQUIRE(outcome.ok());
    CHECK(outcome.value().status == ValidationStatus::kValidationFailed);
}

TEST_CASE("validation fails on sha mismatch", "[validation]") {
    ValidationHarness h({{L"Inbox\\a.eml", L"Inbox", FileImportStatus::kImported}});
    for (auto& [key, folder] : h.session.folders()) {
        for (auto& m : folder.messages) m.tracked.sha256_hex = "bb";
    }
    auto outcome = validate_pst(h.session, h.db.value(), kRunId);
    REQUIRE(outcome.ok());
    CHECK(outcome.value().status == ValidationStatus::kValidationFailed);
}

TEST_CASE("validation fails when fallback lacks its attachment", "[validation]") {
    ValidationHarness h({{L"bad.eml", L"Root Messages",
                          FileImportStatus::kPreservedAsAttachment}});
    for (auto& [key, folder] : h.session.folders()) {
        for (auto& m : folder.messages) m.tracked.has_attachments = false;
    }
    auto outcome = validate_pst(h.session, h.db.value(), kRunId);
    REQUIRE(outcome.ok());
    CHECK(outcome.value().status == ValidationStatus::kValidationFailed);
}

TEST_CASE("validation flags foreign messages in tool folders", "[validation]") {
    ValidationHarness h({{L"Inbox\\a.eml", L"Inbox", FileImportStatus::kImported}});
    TrackedMessage foreign;  // no run id => no WLM2PST tracking props
    foreign.source_relative_path = L"";
    foreign.run_id = "";
    h.session.seed_message(L"Inbox", foreign);
    auto outcome = validate_pst(h.session, h.db.value(), kRunId);
    REQUIRE(outcome.ok());
    CHECK(outcome.value().status == ValidationStatus::kValidationFailed);
}
