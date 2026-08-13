#include "worker/validation/validator.h"

#include "common/unicode/utf.h"

#include <algorithm>
#include <map>
#include <set>

namespace wlm2pst {

namespace {

struct FolderExpectation {
    uint64_t message_count = 0;
};

}  // namespace

Result<ValidationOutcome> validate_pst(IPstSession& session, StateDb& db,
                                       const std::string& run_id) {
    ValidationOutcome outcome;

    // --- Expected state from the database ---
    auto rows = db.load_all_files();
    if (!rows.ok()) return rows.error();

    std::map<std::wstring, FolderExpectation> expected;  // target folder -> counts
    uint64_t expected_preserved = 0;
    for (const auto& row : rows.value()) {
        if (row.status == FileImportStatus::kImported) {
            expected[row.target_folder_path].message_count += 1;
        } else if (row.status == FileImportStatus::kPreservedAsAttachment) {
            ++expected_preserved;
        }
    }
    if (expected_preserved > 0) {
        expected[kConversionErrorsFolderName].message_count += expected_preserved;
    }
    // Folders created but left without direct messages still must exist.
    auto folder_rows = db.load_folders();
    if (!folder_rows.ok()) return folder_rows.error();
    for (const auto& f : folder_rows.value()) {
        // Only folders that were actually materialized (entry id recorded).
        if (!f.target_entry_id.empty()) {
            expected.try_emplace(f.target_relative_folder);
        }
    }

    // --- Actual state from the PST (fresh session) ---
    auto actual_folders = session.list_tool_folders();
    if (!actual_folders.ok()) return actual_folders.error();

    std::map<std::wstring, const ToolFolderInfo*> actual;
    for (const auto& f : actual_folders.value()) {
        actual.emplace(f.relative_path, &f);
    }
    outcome.folders_checked = actual.size();

    // Step 9-10: every expected folder exists with the expected count.
    for (const auto& [folder, expectation] : expected) {
        auto it = actual.find(folder);
        if (it == actual.end()) {
            outcome.issues.push_back({folder, L"", "expected folder missing from PST"});
            continue;
        }
        if (it->second->message_count != expectation.message_count) {
            outcome.issues.push_back(
                {folder, L"",
                 "message count mismatch: expected " + std::to_string(expectation.message_count) +
                     ", found " + std::to_string(it->second->message_count)});
        }
    }

    // Steps 11-14: tracked-property verification per folder.
    // Group DB rows by target folder for one folder-local pass each.
    std::map<std::wstring, std::vector<const StateDb::FileRow*>> imported_by_folder;
    std::vector<const StateDb::FileRow*> preserved_rows;
    for (const auto& row : rows.value()) {
        if (row.status == FileImportStatus::kImported) {
            imported_by_folder[row.target_folder_path].push_back(&row);
        } else if (row.status == FileImportStatus::kPreservedAsAttachment) {
            preserved_rows.push_back(&row);
        }
    }

    auto verify_folder = [&](const std::wstring& folder_path,
                             const std::vector<const StateDb::FileRow*>& folder_rows_expected,
                             bool require_attachment) {
        auto it = actual.find(folder_path);
        if (it == actual.end()) return;  // missing-folder issue already recorded

        auto tracked = session.read_tracked_messages(it->second->entry_id);
        if (!tracked.ok()) {
            outcome.issues.push_back({folder_path, L"",
                                      "tracked messages unreadable: " + tracked.error().message});
            return;
        }
        outcome.messages_checked += tracked.value().size();

        // Index tool-owned messages by source path; detect duplicates.
        std::map<std::wstring, std::vector<const TrackedMessage*>> by_path;
        for (const auto& m : tracked.value()) {
            if (m.run_id.empty()) {
                outcome.issues.push_back(
                    {folder_path, m.source_relative_path,
                     "message without WLM2PST tracking properties in tool folder"});
                continue;
            }
            if (m.run_id == run_id) by_path[m.source_relative_path].push_back(&m);
        }

        for (const auto* row : folder_rows_expected) {
            auto found = by_path.find(row->relative_source_path);
            if (found == by_path.end() || found->second.empty()) {
                outcome.issues.push_back({folder_path, row->relative_source_path,
                                          "no matching tool-owned message found"});
                continue;
            }
            if (found->second.size() > 1) {
                outcome.issues.push_back({folder_path, row->relative_source_path,
                                          "unexpected duplicate tool-owned messages: " +
                                              std::to_string(found->second.size())});
                continue;
            }
            const TrackedMessage& msg = *found->second.front();
            if (!row->source_sha256.empty() && !msg.sha256_hex.empty() &&
                row->source_sha256 != msg.sha256_hex) {
                outcome.issues.push_back({folder_path, row->relative_source_path,
                                          "SHA-256 tracking property mismatch"});
            }
            if (require_attachment && !msg.has_attachments) {
                outcome.issues.push_back({folder_path, row->relative_source_path,
                                          "fallback message is missing its EML attachment"});
            }
        }
    };

    for (const auto& [folder_path, folder_rows_expected] : imported_by_folder) {
        verify_folder(folder_path, folder_rows_expected, false);
    }
    if (!preserved_rows.empty()) {
        verify_folder(kConversionErrorsFolderName, preserved_rows, true);
    }

    if (!outcome.issues.empty()) {
        outcome.status = ValidationStatus::kValidationFailed;
    } else if (expected_preserved > 0) {
        outcome.status = ValidationStatus::kValidatedWithFallbacks;
    } else {
        outcome.status = ValidationStatus::kValidated;
    }
    return outcome;
}

}  // namespace wlm2pst
