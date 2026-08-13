// Test double for IPstSession: an in-memory "PST" used by pipeline and
// validation unit tests (no Outlook required).
#pragma once

#include "worker/import_engine.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace wlm2pst::test {

class FakePstSession : public IPstSession {
public:
    struct FakeMessage {
        TrackedMessage tracked;
        MessageMetadata metadata;  // exactly what the pipeline passed in
        bool is_fallback = false;
    };
    struct FakeFolder {
        std::wstring relative_path;
        std::vector<FakeMessage> messages;
    };

    // --- Test configuration ---
    // Relative source paths whose MIME conversion must fail.
    std::set<std::wstring> fail_convert;
    // Error the failed conversion reports (operation drives circuit breaker).
    std::string convert_fail_operation = "IConverterSession::MIMEToMAPI";
    int32_t convert_fail_hresult = static_cast<int32_t>(0x8004010F);
    // When set, import_fallback also fails with this operation.
    std::string fallback_fail_operation;

    // --- Introspection ---
    std::map<std::wstring, FakeFolder>& folders() { return folders_; }
    uint64_t import_calls = 0;
    uint64_t fallback_calls = 0;
    bool closed = false;

    // Seeds a folder + message (crash-window tests).
    void seed_message(const std::wstring& folder_rel, const TrackedMessage& msg) {
        EntryId id = ensure_folder_internal(folder_rel);
        FakeMessage fm;
        fm.tracked = msg;
        if (fm.tracked.entry_id.empty()) fm.tracked.entry_id = next_entry_id();
        folders_[key_of(id)].messages.push_back(std::move(fm));
    }

    // --- IPstSession ---
    Result<EntryId> ensure_folder(const std::vector<std::wstring>& segments) override {
        std::wstring joined;
        for (const auto& s : segments) {
            if (!joined.empty()) joined += L'\\';
            joined += s;
        }
        return ensure_folder_internal(joined);
    }

    Result<ImportOutcome> import_eml(const EntryId& folder, const std::wstring&,
                                     const MessageMetadata& metadata,
                                     ImportFailure* failure_detail) override {
        ++import_calls;
        if (fail_convert.count(metadata.source_relative_path) > 0) {
            if (failure_detail) {
                failure_detail->first_hresult = convert_fail_hresult;
                failure_detail->second_attempted = false;
            }
            return make_hresult_error(convert_fail_hresult, convert_fail_operation);
        }
        ImportOutcome outcome;
        outcome.entry_id = add_message(folder, metadata, false);
        outcome.date_fallback_applied = metadata.fallback_message_delivery_time.has_value() ||
                                        metadata.fallback_client_submit_time.has_value();
        return outcome;
    }

    Result<ImportOutcome> import_fallback(const EntryId& errors_folder, const std::wstring&,
                                          const MessageMetadata& metadata, int32_t,
                                          int32_t) override {
        ++fallback_calls;
        if (!fallback_fail_operation.empty()) {
            return make_hresult_error(static_cast<int32_t>(0x80040115), fallback_fail_operation);
        }
        ImportOutcome outcome;
        outcome.entry_id = add_message(errors_folder, metadata, true);
        return outcome;
    }

    Result<std::vector<TrackedMessage>> find_tracked_messages(
        const EntryId& folder, const std::string& run_id,
        const std::wstring& source_relative_path) override {
        std::vector<TrackedMessage> matches;
        for (const auto& m : folders_[key_of(folder)].messages) {
            if (m.tracked.run_id == run_id &&
                m.tracked.source_relative_path == source_relative_path) {
                matches.push_back(m.tracked);
            }
        }
        return matches;
    }

    Result<std::vector<ToolFolderInfo>> list_tool_folders() override {
        std::vector<ToolFolderInfo> result;
        result.push_back({L"", root_id_, 0});
        for (const auto& [key, folder] : folders_) {
            ToolFolderInfo info;
            info.relative_path = folder.relative_path;
            info.entry_id = entry_from_key(key);
            info.message_count = folder.messages.size();
            result.push_back(std::move(info));
        }
        return result;
    }

    Result<std::vector<TrackedMessage>> read_tracked_messages(const EntryId& folder) override {
        std::vector<TrackedMessage> result;
        for (const auto& m : folders_[key_of(folder)].messages) result.push_back(m.tracked);
        return result;
    }

    Status close() override {
        closed = true;
        return Status::success();
    }

private:
    static std::wstring key_of(const EntryId& id) {
        return std::wstring(id.begin(), id.end());
    }
    static EntryId entry_from_key(const std::wstring& key) {
        EntryId id;
        for (wchar_t c : key) id.push_back(static_cast<uint8_t>(c));
        return id;
    }
    EntryId next_entry_id() {
        ++counter_;
        EntryId id;
        for (int i = 0; i < 8; ++i) id.push_back(static_cast<uint8_t>((counter_ >> (i * 8)) & 0xFF));
        return id;
    }
    EntryId ensure_folder_internal(const std::wstring& joined) {
        if (auto it = folder_ids_.find(joined); it != folder_ids_.end()) return it->second;
        EntryId id = next_entry_id();
        folder_ids_.emplace(joined, id);
        folders_[key_of(id)].relative_path = joined;
        return id;
    }
    EntryId add_message(const EntryId& folder, const MessageMetadata& metadata, bool fallback) {
        FakeMessage fm;
        fm.tracked.entry_id = next_entry_id();
        fm.tracked.source_relative_path = metadata.source_relative_path;
        fm.tracked.sha256_hex = metadata.sha256_hex;
        fm.tracked.run_id = metadata.run_id;
        fm.tracked.has_attachments = fallback;
        fm.metadata = metadata;
        fm.is_fallback = fallback;
        folders_[key_of(folder)].messages.push_back(std::move(fm));
        return folders_[key_of(folder)].messages.back().tracked.entry_id;
    }

    std::map<std::wstring, FakeFolder> folders_;      // key: entry id as wstring
    std::map<std::wstring, EntryId> folder_ids_;      // joined segments -> id
    EntryId root_id_ = {0xFF};
    uint64_t counter_ = 0;
};

}  // namespace wlm2pst::test
