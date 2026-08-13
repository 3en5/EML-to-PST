#include "worker/pipeline/import_pipeline.h"

#include "fake_pst_session.h"
#include "worker/resume/state_db.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace wlm2pst;
using test::FakePstSession;
namespace fs = std::filesystem;

namespace {

constexpr FileTimeUtc kNow = filetime_from_unix_seconds(1'770'000'000);  // 2026-02-02

struct PipelineHarness {
    fs::path root;
    std::optional<StateDb> db;
    FakePstSession session;
    CancelToken cancel;
    std::ostringstream progress_out;
    std::optional<ProgressPrinter> progress;
    std::optional<ErrorsCsvWriter> csv;
    PipelineContext ctx;

    explicit PipelineHarness(const std::vector<std::pair<std::wstring, std::string>>& files) {
        root = fs::temp_directory_path() /
               fs::path("wlm2pst-pipe-" + std::to_string(std::random_device{}()) + "-" +
                        std::to_string(counter()));
        fs::create_directories(root);

        std::vector<ManifestEntry> manifest;
        std::vector<std::pair<std::wstring, std::wstring>> targets;
        for (const auto& [rel, content] : files) {
            // rel may contain backslashes: create parent dirs from segments.
            fs::path native = root;
            std::vector<std::wstring> parts;
            size_t start = 0;
            while (start <= rel.size()) {
                size_t end = rel.find(L'\\', start);
                if (end == std::wstring::npos) end = rel.size();
                parts.push_back(rel.substr(start, end - start));
                start = end + 1;
            }
            for (size_t i = 0; i + 1 < parts.size(); ++i) native /= fs::path(parts[i]);
            fs::create_directories(native);
            native /= fs::path(parts.back());
            std::ofstream out(native, std::ios::binary);
            out << content;
            out.close();

            ManifestEntry entry;
            entry.relative_path = rel;
            entry.absolute_path = native.wstring();
            entry.size = content.size();
            entry.last_write_utc = kNow - 1000 * kFiletimeTicksPerSecond;
            manifest.push_back(entry);

            std::wstring dir;
            if (parts.size() > 1) {
                for (size_t i = 0; i + 1 < parts.size(); ++i) {
                    if (!dir.empty()) dir += L'\\';
                    dir += parts[i];
                }
            } else {
                dir = L"Root Messages";
            }
            targets.emplace_back(rel, dir);
        }

        JobInfo info;
        info.run_id = "run-test-1";
        info.tool_version = "1.0.0";
        info.source_root = root.wstring();
        info.output_pst = (root / "out.pst").wstring();
        info.root_name = L"Windows Live Mail";
        info.outlook_bitness = "x64";
        info.created_at_utc = kNow;
        info.manifest_hash = "hash";
        auto created = StateDb::create_new((root / "state.sqlite").wstring(), info, manifest,
                                           targets);
        REQUIRE(created.ok());
        db.emplace(std::move(created.value()));

        progress.emplace(true, false, progress_out);
        csv.emplace((root / "errors.csv").wstring());

        ctx.db = &db.value();
        ctx.session = &session;
        ctx.cancel = &cancel;
        ctx.progress = &progress.value();
        ctx.errors_csv = &csv.value();
        ctx.source_root = root.wstring();
        ctx.run_id = "run-test-1";
        ctx.tool_version = "1.0.0";
        ctx.now_utc = [] { return kNow; };
    }

    ~PipelineHarness() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    static int counter() {
        static int n = 0;
        return ++n;
    }
};

const char* kSimpleEml =
    "From: a@example.invalid\r\n"
    "To: b@example.invalid\r\n"
    "Subject: t\r\n"
    "Date: Tue, 01 Jul 2003 10:52:37 +0200\r\n"
    "\r\n"
    "body\r\n";

const char* kNoDateEml =
    "From: a@example.invalid\r\n"
    "Subject: t\r\n"
    "\r\n"
    "body\r\n";

const char* kMultipartMixedEml =
    "From: a@example.invalid\r\n"
    "Subject: with attachment\r\n"
    "Date: Tue, 01 Jul 2003 10:52:37 +0200\r\n"
    "MIME-Version: 1.0\r\n"
    "Content-Type: Multipart/Mixed; boundary=\"b1\"\r\n"
    "\r\n"
    "--b1\r\n"
    "Content-Type: text/plain\r\n\r\nhello\r\n"
    "--b1--\r\n";

const char* kNoSubjectEml =
    "From: a@example.invalid\r\n"
    "Date: Tue, 01 Jul 2003 10:52:37 +0200\r\n"
    "\r\n"
    "body\r\n";

}  // namespace

TEST_CASE("pipeline imports pending files and records state", "[pipeline]") {
    PipelineHarness h({{L"one.eml", kSimpleEml}, {L"two.eml", kSimpleEml}});
    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    CHECK(result.value().counters.imported_normally == 2);
    CHECK(result.value().counters.preserved_as_attachment == 0);
    CHECK_FALSE(result.value().cancelled);
    CHECK_FALSE(result.value().fatal.has_value());

    auto rows = h.db->load_all_files();
    REQUIRE(rows.ok());
    for (const auto& row : rows.value()) {
        CHECK(row.status == FileImportStatus::kImported);
        CHECK_FALSE(row.target_entry_id.empty());
        CHECK_FALSE(row.source_sha256.empty());
        CHECK(row.attempt_count == 1);
    }
    CHECK(h.session.import_calls == 2);
}

TEST_CASE("pipeline preserves unconvertible files as fallback attachments", "[pipeline]") {
    PipelineHarness h({{L"good.eml", kSimpleEml}, {L"bad.eml", kSimpleEml}});
    h.session.fail_convert.insert(L"bad.eml");

    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    CHECK(result.value().counters.imported_normally == 1);
    CHECK(result.value().counters.preserved_as_attachment == 1);
    CHECK(h.session.fallback_calls == 1);

    // The fallback landed in _Conversion Errors with the attachment flag.
    bool found = false;
    for (auto& [key, folder] : h.session.folders()) {
        if (folder.relative_path == kConversionErrorsFolderName) {
            REQUIRE(folder.messages.size() == 1);
            CHECK(folder.messages[0].tracked.has_attachments);
            found = true;
        }
    }
    CHECK(found);

    auto preserved = h.db->load_files_with_status(FileImportStatus::kPreservedAsAttachment);
    REQUIRE(preserved.ok());
    REQUIRE(preserved.value().size() == 1);
    CHECK(preserved.value()[0].relative_source_path == L"bad.eml");
}

TEST_CASE("pipeline records unreadable sources and continues", "[pipeline]") {
    PipelineHarness h({{L"exists.eml", kSimpleEml}, {L"gone.eml", kSimpleEml}});
    std::filesystem::remove(h.root / "gone.eml");

    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    CHECK(result.value().counters.imported_normally == 1);
    CHECK(result.value().counters.failed_to_read == 1);
    CHECK_FALSE(result.value().fatal.has_value());
}

TEST_CASE("pre-set cancellation leaves everything pending", "[pipeline]") {
    PipelineHarness h({{L"one.eml", kSimpleEml}});
    h.cancel.request_cancel();

    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    CHECK(result.value().cancelled);
    auto pending = h.db->load_files_with_status(FileImportStatus::kPending);
    REQUIRE(pending.ok());
    CHECK(pending.value().size() == 1);
    CHECK(h.session.import_calls == 0);
}

TEST_CASE("resume adopts messages saved before the crash window", "[pipeline][resume]") {
    PipelineHarness h({{L"one.eml", kSimpleEml}});
    h.ctx.resume_mode = true;

    TrackedMessage existing;
    existing.source_relative_path = L"one.eml";
    existing.run_id = "run-test-1";
    existing.sha256_hex = "";
    h.session.seed_message(L"Root Messages", existing);

    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    CHECK(result.value().counters.imported_normally == 1);
    CHECK(h.session.import_calls == 0);  // adopted, not re-imported

    auto rows = h.db->load_all_files();
    REQUIRE(rows.ok());
    CHECK(rows.value()[0].status == FileImportStatus::kImported);
}

TEST_CASE("resume stops deterministically on ambiguous duplicates", "[pipeline][resume]") {
    PipelineHarness h({{L"one.eml", kSimpleEml}});
    h.ctx.resume_mode = true;

    TrackedMessage existing;
    existing.source_relative_path = L"one.eml";
    existing.run_id = "run-test-1";
    h.session.seed_message(L"Root Messages", existing);
    h.session.seed_message(L"Root Messages", existing);

    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    REQUIRE(result.value().fatal.has_value());
    CHECK(result.value().fatal->exit_code == ExitCode::kResumeIntegrityError);
}

TEST_CASE("circuit breaker aborts on repeated systemic store failures", "[pipeline]") {
    std::vector<std::pair<std::wstring, std::string>> files;
    for (int i = 0; i < 15; ++i) {
        files.emplace_back(L"f" + std::to_wstring(i) + L".eml", kSimpleEml);
    }
    PipelineHarness h(files);
    for (const auto& [rel, _] : files) h.session.fail_convert.insert(rel);
    h.session.convert_fail_operation = "IMessage::SaveChanges";
    h.session.fallback_fail_operation = "IMessage::SaveChanges(fallback)";

    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    REQUIRE(result.value().fatal.has_value());
    CHECK(result.value().fatal->exit_code == ExitCode::kOutputWriteFailure);
    CHECK(h.session.import_calls < files.size());  // stopped early
}

TEST_CASE("sent and draft folder aliases drive message metadata", "[pipeline]") {
    PipelineHarness h({{L"Sent Items\\s.eml", kSimpleEml}, {L"Drafts\\d.eml", kSimpleEml},
                       {L"Inbox\\i.eml", kSimpleEml}});
    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    REQUIRE(result.value().counters.imported_normally == 3);

    bool checked_sent = false, checked_draft = false, checked_inbox = false;
    for (auto& [key, folder] : h.session.folders()) {
        for (auto& msg : folder.messages) {
            if (msg.metadata.source_relative_path == L"Sent Items\\s.eml") {
                CHECK(msg.metadata.mark_sent);
                CHECK_FALSE(msg.metadata.mark_draft);
                CHECK(msg.metadata.fallback_client_submit_time.has_value());
                checked_sent = true;
            } else if (msg.metadata.source_relative_path == L"Drafts\\d.eml") {
                CHECK(msg.metadata.mark_draft);
                checked_draft = true;
            } else if (msg.metadata.source_relative_path == L"Inbox\\i.eml") {
                CHECK_FALSE(msg.metadata.mark_sent);
                CHECK_FALSE(msg.metadata.mark_draft);
                CHECK(msg.metadata.fallback_message_delivery_time.has_value());
                checked_inbox = true;
            }
        }
    }
    CHECK(checked_sent);
    CHECK(checked_draft);
    CHECK(checked_inbox);
}

TEST_CASE("conversion-integrity expectations are derived from source headers", "[pipeline]") {
    PipelineHarness h({{L"plain.eml", kSimpleEml},
                       {L"mixed.eml", kMultipartMixedEml},
                       {L"nosubject.eml", kNoSubjectEml}});
    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    REQUIRE(result.value().counters.imported_normally == 3);

    int checked = 0;
    for (auto& [key, folder] : h.session.folders()) {
        for (auto& msg : folder.messages) {
            if (msg.metadata.source_relative_path == L"plain.eml") {
                CHECK(msg.metadata.source_declared_subject);
                CHECK_FALSE(msg.metadata.source_multipart_mixed);
                ++checked;
            } else if (msg.metadata.source_relative_path == L"mixed.eml") {
                CHECK(msg.metadata.source_declared_subject);
                CHECK(msg.metadata.source_multipart_mixed);  // case-insensitive match
                ++checked;
            } else if (msg.metadata.source_relative_path == L"nosubject.eml") {
                CHECK_FALSE(msg.metadata.source_declared_subject);
                ++checked;
            }
        }
    }
    CHECK(checked == 3);
}

TEST_CASE("repeated conversion-integrity failures trip the circuit breaker", "[pipeline]") {
    std::vector<std::pair<std::wstring, std::string>> files;
    for (int i = 0; i < 15; ++i) {
        files.emplace_back(L"f" + std::to_wstring(i) + L".eml", kSimpleEml);
    }
    PipelineHarness h(files);
    for (const auto& [rel, _] : files) h.session.fail_convert.insert(rel);
    h.session.convert_fail_operation = "conversion_integrity";
    h.session.fallback_fail_operation = "IMessage::SaveChanges(fallback)";

    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());
    REQUIRE(result.value().fatal.has_value());
    CHECK(result.value().fatal->exit_code == ExitCode::kOutputWriteFailure);
}

TEST_CASE("filesystem date fallback produces a warning", "[pipeline][dates]") {
    PipelineHarness h({{L"nodate.eml", kNoDateEml}, {L"withdate.eml", kSimpleEml}});
    auto result = run_import_pipeline(h.ctx);
    REQUIRE(result.ok());

    bool warned_nodate = false;
    for (const auto& w : result.value().warnings) {
        if (w.find("nodate.eml") != std::string::npos) warned_nodate = true;
        CHECK(w.find("withdate.eml") == std::string::npos);
    }
    CHECK(warned_nodate);
}
