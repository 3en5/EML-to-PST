// WLM2PST - Outlook-dependent integration tests (spec section 28).
//
// These tests drive the REAL Extended MAPI implementation
// (make_mapi_environment(), src/worker/mapi/mapi_environment.cpp) against a
// classic Outlook installation. They create only temporary PSTs and
// temporary WLM2PST-* profiles under %TEMP%, and clean up everything they
// create (spec section 28: "use temporary directories and clean up only
// their own profiles and files").
//
// Windows-only, entirely: this whole file is a no-op on any other platform.
// Build via tests/integration/CMakeLists.txt (see integration_readme.md for
// how the maintainer wires this into a Windows build).
//
// Every scenario first runs IMapiEnvironment::preflight_check(); when no
// classic Outlook / Extended MAPI is available the test SKIPs with an
// explicit reason rather than silently passing or failing (matching the
// end-to-end harness's rule in tests/e2e/run_e2e.ps1).
#ifdef _WIN32

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "common/errors/result.h"
#include "common/hashing/sha256.h"
#include "common/model.h"
#include "common/unicode/utf.h"
#include "worker/import_engine.h"
#include "worker/mapi/mapi_constants.h"
#include "worker/mapi/mapi_raii.h"
#include "worker/mapi/mapi_runtime.h"
#include "worker/mapi/pst_store.h"
#include "worker/mapi/temporary_profile.h"

using namespace wlm2pst;

namespace {

// ---------------------------------------------------------------------------
// Shared scaffolding
// ---------------------------------------------------------------------------

// A fixture file under tests/fixtures/eml/, addressed by absolute path.
// WLM2PST_FIXTURES_DIR is set by tests/integration/CMakeLists.txt to
// tests/fixtures (the parent of eml/), matching tests/CMakeLists.txt's
// convention for the portable unit tests.
std::wstring fixture_path(const wchar_t* name) {
    std::wstring dir = wide_from_utf8(std::string(WLM2PST_FIXTURES_DIR) + "/eml");
    return dir + L"\\" + name;
}

// Every test gets its own throwaway directory under %TEMP%, removed on
// destruction regardless of test outcome.
class ScratchDir {
public:
    explicit ScratchDir(const wchar_t* label) {
        namespace fs = std::filesystem;
        std::wstring unique =
            std::wstring(L"wlm2pst-itest-") + label + L"-" + wide_from_utf8(mapi::new_guid_string());
        path_ = fs::temp_directory_path() / unique;
        std::error_code ec;
        fs::create_directories(path_, ec);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] std::wstring pst_path(const wchar_t* file_name) const {
        return (path_ / file_name).wstring();
    }

private:
    std::filesystem::path path_;
};

std::string describe(const Error& e) {
    std::ostringstream out;
    out << (e.operation.empty() ? "<no operation>" : e.operation);
    if (e.hresult != 0) out << " hresult=0x" << std::hex << static_cast<uint32_t>(e.hresult) << std::dec;
    if (e.win32 != 0) out << " win32=" << e.win32;
    if (!e.message.empty()) out << ": " << e.message;
    return out.str();
}

// Skips the current test with an explicit reason when classic Outlook /
// Extended MAPI is not available in this environment (spec section 28).
// Returns the ready environment on success.
std::unique_ptr<IMapiEnvironment> require_outlook_or_skip() {
    auto env = make_mapi_environment();
    Status pf = env->preflight_check();
    if (!pf.ok()) {
        SKIP("Classic Outlook / Extended MAPI preflight failed: " << describe(pf.error()));
    }
    return env;
}

MessageMetadata make_metadata(const std::wstring& relative_path, const std::wstring& absolute_path,
                               const std::string& run_id, bool mark_sent = false,
                               bool mark_draft = false) {
    MessageMetadata m;
    m.source_relative_path = relative_path;
    auto digest = sha256_file(absolute_path);
    REQUIRE(digest.ok());
    m.sha256_hex = to_hex_lower(digest.value().data(), digest.value().size());
    m.run_id = run_id;
    m.import_version = "wlm2pst-integration-test";
    std::error_code ec;
    auto size = std::filesystem::file_size(absolute_path, ec);
    m.source_size = ec ? 0 : static_cast<uint64_t>(size);
    m.source_last_write_utc = 0;  // not exercised by these scenarios
    m.mark_sent = mark_sent;
    m.mark_draft = mark_draft;
    m.now_utc = filetime_from_unix_seconds(static_cast<int64_t>(std::time(nullptr)));
    return m;
}

// ---------------------------------------------------------------------------
// Raw MAPI verification helpers.
//
// IPstSession (worker/import_engine.h) is the portable orchestration
// boundary and deliberately does not expose message content (spec section
// 21 privacy rules apply to the tool's OWN logs/reports, not to what a test
// may inspect). To verify Subject/Body/attachment-name/flag content that
// actually reached the PST (items 6, 7, 8, 9 below), these helpers open an
// INDEPENDENT raw MAPI session against the already-created PST file - a
// second, from-scratch reopen, exactly mirroring item 3 ("reopen and verify
// it") - reusing the same building blocks the real implementation uses
// (worker/mapi/mapi_runtime.h, temporary_profile.h, pst_store.h).
// ---------------------------------------------------------------------------

// Owns a second, independent MAPI session opened against an existing PST.
// Field order matters: COM must outlive the MAPI runtime, which must outlive
// the profile, which must outlive the store (reverse-order destruction).
struct RawSession {
    mapi::ComInit com;
    mapi::MapiRuntime runtime;
    std::unique_ptr<mapi::TemporaryProfile> profile;
    std::unique_ptr<mapi::PstStore> store;

    RawSession() = default;
    RawSession(const RawSession&) = delete;
    RawSession& operator=(const RawSession&) = delete;

    ~RawSession() {
        if (store) store->close();
        store.reset();
        if (profile) (void)profile->remove();
        profile.reset();
        runtime.uninitialize();
        com.uninitialize();
    }
};

Result<std::unique_ptr<RawSession>> reopen_raw(const std::wstring& pst_path) {
    auto rs = std::make_unique<RawSession>();
    HRESULT hr = rs->com.initialize();
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "CoInitializeEx");
    if (Status s = rs->runtime.load(); !s.ok()) return s.error();
    if (Status s = rs->runtime.initialize(); !s.ok()) return s.error();

    auto profile = mapi::TemporaryProfile::create(rs->runtime, "WLM2PST-ITEST-");
    if (!profile.ok()) return profile.error();
    if (Status s = profile.value()->add_unicode_pst_service(pst_path, L"WLM2PST Integration Verify");
        !s.ok()) {
        (void)profile.value()->remove();
        return s.error();
    }
    auto store = mapi::PstStore::logon_and_open(rs->runtime, profile.value()->name());
    if (!store.ok()) {
        (void)profile.value()->remove();
        return store.error();
    }
    rs->profile = std::move(profile.value());
    rs->store = std::move(store.value());
    return rs;
}

struct RawMessageProps {
    std::wstring message_class;
    std::wstring subject;
    std::wstring body;
    ULONG message_flags = 0;
    bool has_attach = false;
};

Result<std::vector<RawMessageProps>> read_raw_message_props(mapi::MapiRuntime& runtime,
                                                             IMAPIFolder& folder) {
    mapi::MapiPtr<IMAPITable> contents;
    HRESULT hr = folder.GetContentsTable(0, contents.put());
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMAPIFolder::GetContentsTable");

    enum { kClass, kSubject, kBody, kFlags, kHasAttach, kCount };
    SizedSPropTagArray(kCount, cols) = {
        kCount, {PR_MESSAGE_CLASS_W, PR_SUBJECT_W, PR_BODY_W, PR_MESSAGE_FLAGS, PR_HASATTACH}};
    hr = contents->SetColumns(reinterpret_cast<LPSPropTagArray>(&cols), 0);
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::SetColumns");

    std::vector<RawMessageProps> out;
    for (;;) {
        LPSRowSet rows = nullptr;
        hr = contents->QueryRows(64, 0, &rows);
        if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::QueryRows");
        mapi::RowSetGuard guard(rows, runtime.MAPIFreeBuffer);
        if (!rows || rows->cRows == 0) break;
        for (ULONG i = 0; i < rows->cRows; ++i) {
            const SPropValue* p = rows->aRow[i].lpProps;
            RawMessageProps rp;
            if (PROP_TYPE(p[kClass].ulPropTag) == PT_UNICODE) rp.message_class = p[kClass].Value.lpszW;
            if (PROP_TYPE(p[kSubject].ulPropTag) == PT_UNICODE) rp.subject = p[kSubject].Value.lpszW;
            if (PROP_TYPE(p[kBody].ulPropTag) == PT_UNICODE) rp.body = p[kBody].Value.lpszW;
            if (PROP_TYPE(p[kFlags].ulPropTag) == PT_LONG) rp.message_flags = p[kFlags].Value.ul;
            if (PROP_TYPE(p[kHasAttach].ulPropTag) == PT_BOOLEAN) rp.has_attach = p[kHasAttach].Value.b != 0;
            out.push_back(std::move(rp));
        }
    }
    return out;
}

Result<mapi::MapiPtr<IMessage>> open_message(IMsgStore& store, const EntryId& entry_id) {
    mapi::MapiPtr<IMessage> msg;
    ULONG object_type = 0;
    HRESULT hr = store.OpenEntry(
        static_cast<ULONG>(entry_id.size()),
        reinterpret_cast<LPENTRYID>(const_cast<uint8_t*>(entry_id.data())),
        nullptr, MAPI_BEST_ACCESS, &object_type, msg.put_unknown());
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMsgStore::OpenEntry(message)");
    if (object_type != MAPI_MESSAGE) {
        return make_error("IMsgStore::OpenEntry", "entry id does not reference a message");
    }
    return msg;
}

Result<std::wstring> first_attachment_long_filename(mapi::MapiRuntime& runtime, IMessage& message) {
    mapi::MapiPtr<IMAPITable> attach_table;
    HRESULT hr = message.GetAttachmentTable(0, attach_table.put());
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMessage::GetAttachmentTable");

    enum { kAttachNum, kAttachCount };
    SizedSPropTagArray(kAttachCount, cols) = {kAttachCount, {PR_ATTACH_NUM}};
    hr = attach_table->SetColumns(reinterpret_cast<LPSPropTagArray>(&cols), 0);
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::SetColumns(attach)");

    LPSRowSet rows = nullptr;
    hr = attach_table->QueryRows(1, 0, &rows);
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::QueryRows(attach)");
    mapi::RowSetGuard guard(rows, runtime.MAPIFreeBuffer);
    if (!rows || rows->cRows == 0) {
        return make_error("IMAPITable::QueryRows(attach)", "message has no attachments");
    }
    ULONG attach_num = rows->aRow[0].lpProps[kAttachNum].Value.ul;

    mapi::MapiPtr<IAttach> attach;
    hr = message.OpenAttach(attach_num, nullptr, MAPI_BEST_ACCESS, attach.put());
    if (FAILED(hr)) return make_hresult_error(static_cast<int32_t>(hr), "IMessage::OpenAttach");

    SizedSPropTagArray(1, name_cols) = {1, {PR_ATTACH_LONG_FILENAME_W}};
    ULONG count = 0;
    LPSPropValue values = nullptr;
    hr = attach->GetProps(reinterpret_cast<LPSPropTagArray>(&name_cols), 0, &count, &values);
    mapi::MapiBuffer value_guard(runtime.MAPIFreeBuffer);
    *value_guard.put() = values;
    if (FAILED(hr) || count == 0 || PROP_TYPE(values[0].ulPropTag) != PT_UNICODE) {
        return make_error("IAttach::GetProps(PR_ATTACH_LONG_FILENAME_W)",
                          "attachment has no long filename");
    }
    return std::wstring(values[0].Value.lpszW);
}

constexpr ULONG kMsgFlagRead = 0x1;
constexpr ULONG kMsgFlagUnsent = 0x8;
constexpr ULONG kMsgFlagFromMe = 0x20;

}  // namespace

// ---------------------------------------------------------------------------
// 1. Create a temporary Unicode PST.
// ---------------------------------------------------------------------------
TEST_CASE("1: create a temporary Unicode PST", "[.outlook][mapi]") {
    auto env = require_outlook_or_skip();

    ScratchDir scratch(L"create-pst");
    std::wstring pst_path = scratch.pst_path(L"create-pst.pst");
    REQUIRE_FALSE(std::filesystem::exists(pst_path));

    auto session = env->create_session(pst_path, L"Windows Live Mail", /*must_exist=*/false, "convert");
    REQUIRE(session.ok());

    auto folders = session.value()->list_tool_folders();
    REQUIRE(folders.ok());
    CHECK_FALSE(folders.value().empty());  // the tool root itself always exists

    REQUIRE(session.value()->close().ok());
    CHECK(std::filesystem::exists(pst_path));
}

// ---------------------------------------------------------------------------
// 2 + 3. Import one message; reopen and verify it.
// ---------------------------------------------------------------------------
TEST_CASE("2-3: import one message, then reopen the PST and verify it", "[.outlook][mapi]") {
    auto env = require_outlook_or_skip();

    ScratchDir scratch(L"import-one");
    std::wstring pst_path = scratch.pst_path(L"one-message.pst");
    std::string run_id = mapi::new_guid_string();
    EntryId saved_entry_id;

    {
        auto session = env->create_session(pst_path, L"Windows Live Mail", false, "convert");
        REQUIRE(session.ok());
        auto folder = session.value()->ensure_folder({});
        REQUIRE(folder.ok());

        auto metadata = make_metadata(L"utf8_plain.eml", fixture_path(L"utf8_plain.eml"), run_id);
        ImportFailure failure{};
        auto outcome = session.value()->import_eml(folder.value(), fixture_path(L"utf8_plain.eml"),
                                                    metadata, &failure);
        REQUIRE(outcome.ok());
        CHECK_FALSE(outcome.value().used_normalized_retry);  // clean fixture: attempt 1 must succeed
        CHECK_FALSE(outcome.value().entry_id.empty());
        saved_entry_id = outcome.value().entry_id;

        REQUIRE(session.value()->close().ok());
    }

    // Reopen (spec item 3): a fresh session against the same PST, must_exist=true.
    {
        auto session = env->create_session(pst_path, L"Windows Live Mail", /*must_exist=*/true, "validate");
        REQUIRE(session.ok());
        auto folder = session.value()->ensure_folder({});
        REQUIRE(folder.ok());

        auto tracked = session.value()->read_tracked_messages(folder.value());
        REQUIRE(tracked.ok());
        REQUIRE(tracked.value().size() == 1);
        CHECK(tracked.value()[0].entry_id == saved_entry_id);
        CHECK(tracked.value()[0].run_id == run_id);
        CHECK(tracked.value()[0].source_relative_path == L"utf8_plain.eml");

        REQUIRE(session.value()->close().ok());
    }
}

// ---------------------------------------------------------------------------
// 4 + 5. Import a folder tree; verify Unicode (Hebrew) folder names.
// ---------------------------------------------------------------------------
TEST_CASE("4-5: import a folder tree and verify Unicode folder names", "[.outlook][mapi]") {
    auto env = require_outlook_or_skip();

    ScratchDir scratch(L"folder-tree");
    std::wstring pst_path = scratch.pst_path(L"folder-tree.pst");
    std::string run_id = mapi::new_guid_string();
    const std::wstring root_name = L"Windows Live Mail";

    {
        auto session = env->create_session(pst_path, root_name, false, "convert");
        REQUIRE(session.ok());

        // A Hebrew folder ("folder") and Latin sub-folder, matching the kind
        // of tree run_e2e.ps1 synthesizes (account@example.invalid, a
        // Hebrew-named folder, etc.).
        std::vector<std::wstring> hebrew_folder{L"תיקייה", L"Client A"};
        auto folder = session.value()->ensure_folder(hebrew_folder);
        REQUIRE(folder.ok());
        auto metadata =
            make_metadata(L"תיקייה\\Client A\\utf8_plain.eml",
                          fixture_path(L"utf8_plain.eml"), run_id);
        ImportFailure failure{};
        auto outcome = session.value()->import_eml(folder.value(), fixture_path(L"utf8_plain.eml"),
                                                    metadata, &failure);
        REQUIRE(outcome.ok());

        // A second, plain-ASCII folder alongside it (Root Messages style tree).
        auto root_messages = session.value()->ensure_folder({L"Root Messages"});
        REQUIRE(root_messages.ok());
        auto metadata2 = make_metadata(L"root-level.eml", fixture_path(L"utf8_html.eml"), run_id);
        auto outcome2 = session.value()->import_eml(root_messages.value(),
                                                     fixture_path(L"utf8_html.eml"), metadata2, &failure);
        REQUIRE(outcome2.ok());

        REQUIRE(session.value()->close().ok());
    }

    {
        auto session = env->create_session(pst_path, root_name, true, "validate");
        REQUIRE(session.ok());
        auto all_folders = session.value()->list_tool_folders();
        REQUIRE(all_folders.ok());

        bool hebrew_found = false;
        bool root_messages_found = false;
        for (const auto& f : all_folders.value()) {
            if (f.relative_path == L"תיקייה\\Client A") {
                hebrew_found = true;
                CHECK(f.message_count == 1);
            }
            if (f.relative_path == L"Root Messages") {
                root_messages_found = true;
                CHECK(f.message_count == 1);
            }
        }
        CHECK(hebrew_found);
        CHECK(root_messages_found);

        REQUIRE(session.value()->close().ok());
    }
}

// ---------------------------------------------------------------------------
// 6. Verify Hebrew subject/body/attachment name.
// ---------------------------------------------------------------------------
TEST_CASE("6: Hebrew subject, body, and attachment filename survive conversion",
          "[.outlook][mapi]") {
    auto env = require_outlook_or_skip();

    ScratchDir scratch(L"hebrew-content");
    std::wstring pst_path = scratch.pst_path(L"hebrew-content.pst");
    std::string run_id = mapi::new_guid_string();
    EntryId root;
    EntryId attachment_message_id;

    {
        auto session = env->create_session(pst_path, L"Windows Live Mail", false, "convert");
        REQUIRE(session.ok());
        auto folder = session.value()->ensure_folder({});
        REQUIRE(folder.ok());
        root = folder.value();

        ImportFailure failure{};

        auto subject_meta =
            make_metadata(L"rfc2047_subject.eml", fixture_path(L"rfc2047_subject.eml"), run_id);
        auto subject_outcome = session.value()->import_eml(
            root, fixture_path(L"rfc2047_subject.eml"), subject_meta, &failure);
        REQUIRE(subject_outcome.ok());

        auto body_meta =
            make_metadata(L"windows1255_hebrew.eml", fixture_path(L"windows1255_hebrew.eml"), run_id);
        auto body_outcome = session.value()->import_eml(
            root, fixture_path(L"windows1255_hebrew.eml"), body_meta, &failure);
        REQUIRE(body_outcome.ok());

        auto attach_meta = make_metadata(L"hebrew_attachment_name.eml",
                                         fixture_path(L"hebrew_attachment_name.eml"), run_id);
        auto attach_outcome = session.value()->import_eml(
            root, fixture_path(L"hebrew_attachment_name.eml"), attach_meta, &failure);
        REQUIRE(attach_outcome.ok());
        attachment_message_id = attach_outcome.value().entry_id;

        REQUIRE(session.value()->close().ok());
    }

    auto raw = reopen_raw(pst_path);
    REQUIRE(raw.ok());

    auto folder_ptr = raw.value()->store->open_folder(root);
    REQUIRE(folder_ptr.ok());
    auto props = read_raw_message_props(raw.value()->runtime, *folder_ptr.value().get());
    REQUIRE(props.ok());
    REQUIRE(props.value().size() == 3);

    bool subject_ok = false;
    bool body_ok = false;
    for (const auto& p : props.value()) {
        // "שלום עולם" - synthetic Hebrew test text (see tests/fixtures/README.md).
        if (p.subject.find(L"שלום עולם") !=
            std::wstring::npos) {
            subject_ok = true;
        }
        if (p.body.find(L"שלום עולם") != std::wstring::npos) {
            body_ok = true;
        }
    }
    CHECK(subject_ok);
    CHECK(body_ok);

    auto attach_msg = open_message(*raw.value()->store->store(), attachment_message_id);
    REQUIRE(attach_msg.ok());
    auto filename = first_attachment_long_filename(raw.value()->runtime, *attach_msg.value().get());
    REQUIRE(filename.ok());
    // "מסמך.txt" - synthetic Hebrew attachment filename (see tests/fixtures/README.md).
    CHECK(filename.value() == L"מסמך.txt");
}

// ---------------------------------------------------------------------------
// 7. Verify sent flags.
// ---------------------------------------------------------------------------
TEST_CASE("7: sent-folder messages are marked sent (MSGFLAG_FROMME, not UNSENT)",
          "[.outlook][mapi]") {
    auto env = require_outlook_or_skip();

    ScratchDir scratch(L"sent-flag");
    std::wstring pst_path = scratch.pst_path(L"sent-flag.pst");
    std::string run_id = mapi::new_guid_string();
    EntryId root;

    {
        auto session = env->create_session(pst_path, L"Windows Live Mail", false, "convert");
        REQUIRE(session.ok());
        auto folder = session.value()->ensure_folder({L"Sent Items"});
        REQUIRE(folder.ok());
        root = folder.value();

        auto metadata = make_metadata(L"Sent Items\\utf8_plain.eml", fixture_path(L"utf8_plain.eml"),
                                      run_id, /*mark_sent=*/true, /*mark_draft=*/false);
        ImportFailure failure{};
        auto outcome =
            session.value()->import_eml(root, fixture_path(L"utf8_plain.eml"), metadata, &failure);
        REQUIRE(outcome.ok());

        REQUIRE(session.value()->close().ok());
    }

    auto raw = reopen_raw(pst_path);
    REQUIRE(raw.ok());
    auto folder_ptr = raw.value()->store->open_folder(root);
    REQUIRE(folder_ptr.ok());
    auto props = read_raw_message_props(raw.value()->runtime, *folder_ptr.value().get());
    REQUIRE(props.ok());
    REQUIRE(props.value().size() == 1);
    ULONG flags = props.value()[0].message_flags;
    CHECK((flags & kMsgFlagRead) != 0);
    CHECK((flags & kMsgFlagFromMe) != 0);
    CHECK((flags & kMsgFlagUnsent) == 0);
}

// ---------------------------------------------------------------------------
// 8. Verify draft flags.
// ---------------------------------------------------------------------------
TEST_CASE("8: X-Unsent messages are marked draft (MSGFLAG_UNSENT)", "[.outlook][mapi]") {
    auto env = require_outlook_or_skip();

    ScratchDir scratch(L"draft-flag");
    std::wstring pst_path = scratch.pst_path(L"draft-flag.pst");
    std::string run_id = mapi::new_guid_string();
    EntryId root;

    {
        auto session = env->create_session(pst_path, L"Windows Live Mail", false, "convert");
        REQUIRE(session.ok());
        auto folder = session.value()->ensure_folder({L"Drafts"});
        REQUIRE(folder.ok());
        root = folder.value();

        auto metadata = make_metadata(L"Drafts\\x_unsent.eml", fixture_path(L"x_unsent.eml"), run_id,
                                      /*mark_sent=*/false, /*mark_draft=*/true);
        ImportFailure failure{};
        auto outcome =
            session.value()->import_eml(root, fixture_path(L"x_unsent.eml"), metadata, &failure);
        REQUIRE(outcome.ok());

        REQUIRE(session.value()->close().ok());
    }

    auto raw = reopen_raw(pst_path);
    REQUIRE(raw.ok());
    auto folder_ptr = raw.value()->store->open_folder(root);
    REQUIRE(folder_ptr.ok());
    auto props = read_raw_message_props(raw.value()->runtime, *folder_ptr.value().get());
    REQUIRE(props.ok());
    REQUIRE(props.value().size() == 1);
    CHECK((props.value()[0].message_flags & kMsgFlagUnsent) != 0);
}

// ---------------------------------------------------------------------------
// 9. Verify fallback EML attachment (spec section 16 final fallback path).
// ---------------------------------------------------------------------------
TEST_CASE("9: fallback preserves the original EML as an unchanged attachment",
          "[.outlook][mapi]") {
    auto env = require_outlook_or_skip();

    ScratchDir scratch(L"fallback");
    std::wstring pst_path = scratch.pst_path(L"fallback.pst");
    std::string run_id = mapi::new_guid_string();
    EntryId errors_folder;
    EntryId fallback_message_id;
    std::wstring source = fixture_path(L"broken_mime_boundary.eml");

    {
        auto session = env->create_session(pst_path, L"Windows Live Mail", false, "convert");
        REQUIRE(session.ok());
        auto folder = session.value()->ensure_folder({std::wstring(kConversionErrorsFolderName)});
        REQUIRE(folder.ok());
        errors_folder = folder.value();

        auto metadata = make_metadata(L"broken_mime_boundary.eml", source, run_id);
        // Simulates both conversion attempts having already failed (spec
        // section 16); this is the direct, deterministic way to exercise
        // import_fallback() without depending on the real converter
        // rejecting a specific byte pattern.
        auto outcome = session.value()->import_fallback(errors_folder, source, metadata,
                                                         /*first_hresult=*/0x80004005,
                                                         /*second_hresult=*/0x80070005);
        REQUIRE(outcome.ok());
        fallback_message_id = outcome.value().entry_id;

        REQUIRE(session.value()->close().ok());
    }

    auto raw = reopen_raw(pst_path);
    REQUIRE(raw.ok());
    auto folder_ptr = raw.value()->store->open_folder(errors_folder);
    REQUIRE(folder_ptr.ok());
    auto props = read_raw_message_props(raw.value()->runtime, *folder_ptr.value().get());
    REQUIRE(props.ok());
    REQUIRE(props.value().size() == 1);
    const auto& fallback = props.value()[0];
    CHECK(fallback.message_class == std::wstring(mapi::kFallbackMessageClass));
    CHECK(fallback.subject.rfind(L"[EML conversion failed]", 0) == 0);
    CHECK(fallback.subject.find(L"broken_mime_boundary.eml") != std::wstring::npos);
    CHECK((fallback.message_flags & kMsgFlagRead) != 0);
    CHECK(fallback.has_attach);
    // Diagnostic body: relative path and SHA-256 present, never message
    // content (there is none to leak - import_fallback() never receives
    // the original subject/body in the first place).
    CHECK(fallback.body.find(L"broken_mime_boundary.eml") != std::wstring::npos);

    auto msg = open_message(*raw.value()->store->store(), fallback_message_id);
    REQUIRE(msg.ok());
    auto filename = first_attachment_long_filename(raw.value()->runtime, *msg.value().get());
    REQUIRE(filename.ok());
    CHECK(filename.value() == L"broken_mime_boundary.eml");
}

// ---------------------------------------------------------------------------
// 10. Verify tracking named properties.
// ---------------------------------------------------------------------------
TEST_CASE("10: WLM2PST tracking named properties round-trip through the PST",
          "[.outlook][mapi]") {
    auto env = require_outlook_or_skip();

    ScratchDir scratch(L"tracking-props");
    std::wstring pst_path = scratch.pst_path(L"tracking-props.pst");
    std::string run_id = mapi::new_guid_string();

    auto session = env->create_session(pst_path, L"Windows Live Mail", false, "convert");
    REQUIRE(session.ok());
    auto folder = session.value()->ensure_folder({});
    REQUIRE(folder.ok());

    auto metadata =
        make_metadata(L"single_attachment.eml", fixture_path(L"single_attachment.eml"), run_id);
    ImportFailure failure{};
    auto outcome = session.value()->import_eml(folder.value(), fixture_path(L"single_attachment.eml"),
                                               metadata, &failure);
    REQUIRE(outcome.ok());

    auto tracked = session.value()->read_tracked_messages(folder.value());
    REQUIRE(tracked.ok());
    REQUIRE(tracked.value().size() == 1);
    const auto& t = tracked.value()[0];
    CHECK(t.entry_id == outcome.value().entry_id);
    CHECK(t.source_relative_path == L"single_attachment.eml");
    CHECK(t.sha256_hex == metadata.sha256_hex);
    CHECK(t.run_id == run_id);
    CHECK(t.has_attachments);  // the fixture carries one MIME attachment

    REQUIRE(session.value()->close().ok());
}

// ---------------------------------------------------------------------------
// 11. Verify resume after simulated crash window.
// ---------------------------------------------------------------------------
TEST_CASE("11: find_tracked_messages recovers an import across a simulated crash window",
          "[.outlook][mapi]") {
    auto env = require_outlook_or_skip();

    ScratchDir scratch(L"crash-resume");
    std::wstring pst_path = scratch.pst_path(L"crash-resume.pst");
    std::string run_id = mapi::new_guid_string();
    EntryId root;

    // "Before the crash": import succeeds and the PST session is closed
    // (simulating the process dying right after IMessage::SaveChanges but
    // before the resume database's mark_imported() commit, spec section
    // 17's crash window).
    {
        auto session = env->create_session(pst_path, L"Windows Live Mail", false, "convert");
        REQUIRE(session.ok());
        auto folder = session.value()->ensure_folder({});
        REQUIRE(folder.ok());
        root = folder.value();

        auto metadata = make_metadata(L"utf8_plain.eml", fixture_path(L"utf8_plain.eml"), run_id);
        ImportFailure failure{};
        auto outcome = session.value()->import_eml(root, fixture_path(L"utf8_plain.eml"), metadata,
                                                    &failure);
        REQUIRE(outcome.ok());

        REQUIRE(session.value()->close().ok());
    }

    // "After the crash": a fresh --resume session reopens the PST and asks
    // whether this exact (run_id, source_relative_path) was already
    // imported, without any resume-database record to rely on.
    {
        auto session = env->create_session(pst_path, L"Windows Live Mail", /*must_exist=*/true,
                                           "convert");
        REQUIRE(session.ok());
        auto folder = session.value()->ensure_folder({});
        REQUIRE(folder.ok());

        auto found = session.value()->find_tracked_messages(folder.value(), run_id, L"utf8_plain.eml");
        REQUIRE(found.ok());
        REQUIRE(found.value().size() == 1);
        CHECK(found.value()[0].source_relative_path == L"utf8_plain.eml");

        // A different run id must never match (crash recovery is scoped to
        // THIS job, not any tool-owned message with the same path).
        auto not_found = session.value()->find_tracked_messages(folder.value(), "different-run-id",
                                                                 L"utf8_plain.eml");
        REQUIRE(not_found.ok());
        CHECK(not_found.value().empty());

        REQUIRE(session.value()->close().ok());
    }
}

// ---------------------------------------------------------------------------
// 12 + 13. Verify worker/Outlook bitness agreement.
//
// preflight_check() itself fails with kBitnessMismatch (spec section 6) when
// this worker's bitness does not match the installed classic Outlook's
// Extended MAPI bitness, so reaching a successful preflight already proves
// compatibility for whichever bitness this test binary was built as. A
// single compiled binary is only ever one bitness, so exactly one of these
// two tests can meaningfully pass in a given build; the other SKIPs by
// design, which is the correct, non-false-passing outcome (run both the x86
// and x64 worker test binaries in CI to cover both).
// ---------------------------------------------------------------------------
TEST_CASE("12: x86 worker matches a 32-bit classic Outlook installation", "[.outlook][mapi]") {
    if constexpr (sizeof(void*) != 4) {
        SKIP("This test binary is not the 32-bit worker build; run the x86 build to exercise "
             "this scenario.");
    }
    auto env = require_outlook_or_skip();
    (void)env;  // preflight_check() already validated bitness agreement.
    SUCCEED("x86 worker preflight succeeded against a 32-bit Outlook MAPI installation.");
}

TEST_CASE("13: x64 worker matches a 64-bit classic Outlook installation", "[.outlook][mapi]") {
    if constexpr (sizeof(void*) != 8) {
        SKIP("This test binary is not the 64-bit worker build; run the x64 build to exercise "
             "this scenario.");
    }
    auto env = require_outlook_or_skip();
    (void)env;  // preflight_check() already validated bitness agreement.
    SUCCEED("x64 worker preflight succeeded against a 64-bit Outlook MAPI installation.");
}

#endif  // _WIN32
