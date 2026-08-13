// WLM2PST - the real Extended MAPI implementation of IMapiEnvironment /
// IPstSession (spec sections 8, 10, 15, 16, 17, 20).
#include "worker/import_engine.h"

#include "common/unicode/utf.h"
#include "common/version/version.h"
#include "worker/mapi/mapi_constants.h"
#include "worker/mapi/mapi_raii.h"
#include "worker/mapi/mapi_runtime.h"
#include "worker/mapi/message_properties.h"
#include "worker/mapi/mime_converter.h"
#include "worker/mapi/pst_store.h"
#include "worker/mapi/temporary_profile.h"

#include <algorithm>
#include <map>
#include <sstream>

namespace wlm2pst {

namespace {

using namespace wlm2pst::mapi;

std::wstring join_segments(const std::vector<std::wstring>& segments) {
    std::wstring joined;
    for (const auto& s : segments) {
        if (!joined.empty()) joined += L'\\';
        joined += s;
    }
    return joined;
}

std::wstring filename_of(const std::wstring& relative_path) {
    size_t pos = relative_path.find_last_of(L'\\');
    return pos == std::wstring::npos ? relative_path : relative_path.substr(pos + 1);
}

class MapiPstSession final : public IPstSession {
public:
    MapiPstSession(MapiRuntime& runtime, std::unique_ptr<TemporaryProfile> profile,
                   std::unique_ptr<PstStore> store, EntryId root, TrackingTags tags)
        : runtime_(&runtime), profile_(std::move(profile)), store_(std::move(store)),
          root_(std::move(root)), tags_(tags) {}

    ~MapiPstSession() override { (void)close(); }

    Result<EntryId> ensure_folder(const std::vector<std::wstring>& segments) override {
        if (segments.empty()) return root_;
        std::wstring key = join_segments(segments);
        if (auto it = folder_cache_.find(key); it != folder_cache_.end()) return it->second;

        EntryId current = root_;
        std::wstring prefix;
        for (const auto& segment : segments) {
            if (!prefix.empty()) prefix += L'\\';
            prefix += segment;
            if (auto it = folder_cache_.find(prefix); it != folder_cache_.end()) {
                current = it->second;
                continue;
            }
            auto child = store_->ensure_child_folder(current, segment);
            if (!child.ok()) return child.error();
            current = child.value();
            folder_cache_.emplace(prefix, current);
        }
        return current;
    }

    Result<ImportOutcome> import_eml(const EntryId& folder,
                                     const std::wstring& eml_absolute_path,
                                     const MessageMetadata& metadata,
                                     ImportFailure* failure_detail) override {
        if (Status s = ensure_converter(); !s.ok()) return s.error();

        auto folder_ptr = store_->open_folder(folder);
        if (!folder_ptr.ok()) return folder_ptr.error();

        ImportFailure failure;

        // Attempt 1: the original, unmodified bytes (spec section 16).
        {
            auto attempt = convert_into_new_message(*folder_ptr.value().get(), eml_absolute_path,
                                                    metadata, false);
            if (attempt.ok()) return std::move(attempt.value());
            failure.first_hresult = attempt.error().hresult;
        }

        // Attempt 2: conservative normalized copy in a secure temp file.
        auto normalized = write_normalized_copy(eml_absolute_path);
        if (normalized.ok()) {
            failure.second_attempted = true;
            auto attempt = convert_into_new_message(*folder_ptr.value().get(),
                                                    normalized.value().path(), metadata, true);
            if (attempt.ok()) return std::move(attempt.value());
            failure.second_hresult = attempt.error().hresult;
        }

        if (failure_detail) *failure_detail = failure;
        return make_hresult_error(
            failure.second_attempted ? failure.second_hresult : failure.first_hresult,
            "MIMEToMAPI", "both conversion attempts failed");
    }

    Result<ImportOutcome> import_fallback(const EntryId& errors_folder,
                                          const std::wstring& eml_absolute_path,
                                          const MessageMetadata& metadata,
                                          int32_t first_hresult,
                                          int32_t second_hresult) override {
        auto folder_ptr = store_->open_folder(errors_folder);
        if (!folder_ptr.ok()) return folder_ptr.error();
        auto message = store_->create_message(*folder_ptr.value().get());
        if (!message.ok()) return message.error();
        IMessage& msg = *message.value().get();

        // Envelope (spec section 16): IPM.Note, read, diagnostic subject/body.
        std::wstring subject = L"[EML conversion failed] " + filename_of(metadata.source_relative_path);
        std::wstringstream body;
        body << L"WLM2PST could not convert this message from MIME to MAPI.\r\n\r\n"
             << L"Relative source path: " << metadata.source_relative_path << L"\r\n"
             << L"Source size (bytes): " << metadata.source_size << L"\r\n"
             << L"SHA-256: " << wide_from_utf8(metadata.sha256_hex) << L"\r\n";
        {
            std::wstringstream codes;
            codes << std::hex << std::uppercase;
            codes << L"First attempt HRESULT: 0x" << static_cast<uint32_t>(first_hresult) << L"\r\n";
            codes << L"Second attempt HRESULT: 0x" << static_cast<uint32_t>(second_hresult) << L"\r\n";
            body << codes.str();
        }
        body << L"\r\nThe original EML file is attached to this message, byte-for-byte unchanged.\r\n";
        std::wstring body_str = body.str();

        SPropValue props[3]{};
        props[0].ulPropTag = PR_MESSAGE_CLASS_W;
        props[0].Value.lpszW = const_cast<LPWSTR>(kFallbackMessageClass);
        props[1].ulPropTag = PR_SUBJECT_W;
        props[1].Value.lpszW = const_cast<LPWSTR>(subject.c_str());
        props[2].ulPropTag = PR_BODY_W;
        props[2].Value.lpszW = const_cast<LPWSTR>(body_str.c_str());
        HRESULT hr = msg.SetProps(3, props, nullptr);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMessage::SetProps(fallback)");
        }

        if (Status s = apply_message_state(msg, *runtime_, false, false); !s.ok()) return s.error();
        if (auto r = apply_date_policy(msg, *runtime_, metadata); !r.ok()) return r.error();

        // Attach the original EML bytes unchanged.
        if (Status s = attach_original_eml(msg, eml_absolute_path, metadata); !s.ok()) {
            return s.error();
        }

        if (Status s = set_tracking_properties(msg, tags_, metadata); !s.ok()) return s.error();

        hr = msg.SaveChanges(KEEP_OPEN_READONLY);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMessage::SaveChanges(fallback)");
        }
        auto entry_id = store_->message_entry_id(msg);
        if (!entry_id.ok()) return entry_id.error();

        ImportOutcome outcome;
        outcome.entry_id = std::move(entry_id.value());
        return outcome;
    }

    Result<std::vector<TrackedMessage>> find_tracked_messages(
        const EntryId& folder, const std::string& run_id,
        const std::wstring& source_relative_path) override {
        auto all = read_tracked_messages(folder);
        if (!all.ok()) return all.error();
        std::vector<TrackedMessage> matches;
        for (auto& m : all.value()) {
            if (m.run_id == run_id && m.source_relative_path == source_relative_path) {
                matches.push_back(std::move(m));
            }
        }
        return matches;
    }

    Result<std::vector<ToolFolderInfo>> list_tool_folders() override {
        std::vector<ToolFolderInfo> result;
        // Iterative DFS (spec section 9: no recursion on user-shaped trees).
        std::vector<std::pair<EntryId, std::wstring>> stack{{root_, L""}};
        while (!stack.empty()) {
            auto [entry_id, rel_path] = std::move(stack.back());
            stack.pop_back();

            auto folder_ptr = store_->open_folder(entry_id);
            if (!folder_ptr.ok()) return folder_ptr.error();
            IMAPIFolder& folder = *folder_ptr.value().get();

            ToolFolderInfo info;
            info.relative_path = rel_path;
            info.entry_id = entry_id;

            // PR_CONTENT_COUNT = number of messages directly in the folder.
            SizedSPropTagArray(1, tags) = {1, {PR_CONTENT_COUNT}};
            ULONG count = 0;
            LPSPropValue values = nullptr;
            HRESULT hr = folder.GetProps(reinterpret_cast<LPSPropTagArray>(&tags), 0, &count, &values);
            MapiBuffer guard(runtime_->MAPIFreeBuffer);
            *guard.put() = values;
            if (SUCCEEDED(hr) && count > 0 && PROP_TYPE(values[0].ulPropTag) == PT_LONG) {
                info.message_count = values[0].Value.ul;
            }
            result.push_back(std::move(info));

            // Children via the hierarchy table.
            MapiPtr<IMAPITable> hierarchy;
            hr = folder.GetHierarchyTable(0, hierarchy.put());
            if (FAILED(hr)) {
                return make_hresult_error(static_cast<int32_t>(hr), "IMAPIFolder::GetHierarchyTable");
            }
            enum { kColEntryId, kColName, kColCount };
            SizedSPropTagArray(kColCount, columns) = {kColCount, {PR_ENTRYID, PR_DISPLAY_NAME_W}};
            hr = hierarchy->SetColumns(reinterpret_cast<LPSPropTagArray>(&columns), 0);
            if (FAILED(hr)) {
                return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::SetColumns");
            }
            for (;;) {
                LPSRowSet rows = nullptr;
                hr = hierarchy->QueryRows(64, 0, &rows);
                if (FAILED(hr)) {
                    return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::QueryRows");
                }
                RowSetGuard row_guard(rows, runtime_->MAPIFreeBuffer);
                if (!rows || rows->cRows == 0) break;
                for (ULONG i = 0; i < rows->cRows; ++i) {
                    const SPropValue& eid = rows->aRow[i].lpProps[kColEntryId];
                    const SPropValue& name = rows->aRow[i].lpProps[kColName];
                    if (PROP_TYPE(eid.ulPropTag) != PT_BINARY) continue;
                    std::wstring child_name =
                        PROP_TYPE(name.ulPropTag) == PT_UNICODE ? name.Value.lpszW : L"?";
                    std::wstring child_rel = rel_path.empty() ? child_name
                                                              : rel_path + L'\\' + child_name;
                    stack.emplace_back(
                        EntryId(eid.Value.bin.lpb, eid.Value.bin.lpb + eid.Value.bin.cb),
                        std::move(child_rel));
                }
            }
        }
        // Deterministic order for validation output.
        std::sort(result.begin(), result.end(),
                  [](const ToolFolderInfo& a, const ToolFolderInfo& b) {
                      return a.relative_path < b.relative_path;
                  });
        return result;
    }

    Result<std::vector<TrackedMessage>> read_tracked_messages(const EntryId& folder) override {
        auto folder_ptr = store_->open_folder(folder);
        if (!folder_ptr.ok()) return folder_ptr.error();

        MapiPtr<IMAPITable> contents;
        HRESULT hr = folder_ptr.value()->GetContentsTable(0, contents.put());
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMAPIFolder::GetContentsTable");
        }
        enum { kColEntryId, kColRelPath, kColSha, kColRunId, kColHasAttach, kColCount };
        SizedSPropTagArray(kColCount, columns) = {
            kColCount,
            {PR_ENTRYID, tags_.source_relative_path, tags_.source_sha256, tags_.run_id,
             PR_HASATTACH}};
        hr = contents->SetColumns(reinterpret_cast<LPSPropTagArray>(&columns), 0);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::SetColumns");
        }

        std::vector<TrackedMessage> result;
        for (;;) {
            LPSRowSet rows = nullptr;
            hr = contents->QueryRows(128, 0, &rows);
            if (FAILED(hr)) {
                return make_hresult_error(static_cast<int32_t>(hr), "IMAPITable::QueryRows");
            }
            RowSetGuard guard(rows, runtime_->MAPIFreeBuffer);
            if (!rows || rows->cRows == 0) break;
            for (ULONG i = 0; i < rows->cRows; ++i) {
                const SPropValue* p = rows->aRow[i].lpProps;
                TrackedMessage m;
                if (PROP_TYPE(p[kColEntryId].ulPropTag) == PT_BINARY) {
                    m.entry_id.assign(p[kColEntryId].Value.bin.lpb,
                                      p[kColEntryId].Value.bin.lpb + p[kColEntryId].Value.bin.cb);
                }
                if (PROP_TYPE(p[kColRelPath].ulPropTag) == PT_UNICODE) {
                    m.source_relative_path = p[kColRelPath].Value.lpszW;
                }
                if (PROP_TYPE(p[kColSha].ulPropTag) == PT_UNICODE) {
                    m.sha256_hex = utf8_from_wide(p[kColSha].Value.lpszW);
                }
                if (PROP_TYPE(p[kColRunId].ulPropTag) == PT_UNICODE) {
                    m.run_id = utf8_from_wide(p[kColRunId].Value.lpszW);
                }
                if (PROP_TYPE(p[kColHasAttach].ulPropTag) == PT_BOOLEAN) {
                    m.has_attachments = p[kColHasAttach].Value.b != 0;
                }
                result.push_back(std::move(m));
            }
        }
        return result;
    }

    Status close() override {
        if (closed_) return Status::success();
        closed_ = true;
        converter_.reset();
        if (store_) store_->close();
        store_.reset();
        Status profile_status = Status::success();
        if (profile_) profile_status = profile_->remove();
        profile_.reset();
        return profile_status;
    }

private:
    Status ensure_converter() {
        if (converter_) return Status::success();
        auto conv = MimeConverter::create();
        if (!conv.ok()) return conv.error();
        converter_ = std::make_unique<MimeConverter>(std::move(conv.value()));
        return Status::success();
    }

    // One conversion attempt: fresh message, MIMEToMAPI, state + dates +
    // tracking properties, save, entry id.
    Result<ImportOutcome> convert_into_new_message(IMAPIFolder& folder,
                                                   const std::wstring& stream_path,
                                                   const MessageMetadata& metadata,
                                                   bool is_normalized_retry) {
        auto stream = open_read_only_stream(stream_path);
        if (!stream.ok()) return stream.error();

        auto message = store_->create_message(folder);
        if (!message.ok()) return message.error();
        IMessage& msg = *message.value().get();

        if (Status s = converter_->convert(*stream.value().get(), msg); !s.ok()) {
            // Unsaved message is discarded when released; nothing persists.
            return s.error();
        }

        // Flags and dates must be right before the FIRST save (spec section 12).
        if (Status s = apply_message_state(msg, *runtime_, metadata.mark_sent,
                                           metadata.mark_draft); !s.ok()) {
            return s.error();
        }
        auto date_fallback = apply_date_policy(msg, *runtime_, metadata);
        if (!date_fallback.ok()) return date_fallback.error();
        if (Status s = set_tracking_properties(msg, tags_, metadata); !s.ok()) return s.error();

        HRESULT hr = msg.SaveChanges(KEEP_OPEN_READONLY);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMessage::SaveChanges");
        }
        auto entry_id = store_->message_entry_id(msg);
        if (!entry_id.ok()) return entry_id.error();

        ImportOutcome outcome;
        outcome.entry_id = std::move(entry_id.value());
        outcome.used_normalized_retry = is_normalized_retry;
        outcome.date_fallback_applied = date_fallback.value();
        return outcome;
    }

    Status attach_original_eml(IMessage& msg, const std::wstring& eml_absolute_path,
                               const MessageMetadata& metadata) {
        ULONG attach_num = 0;
        MapiPtr<IAttach> attach;
        HRESULT hr = msg.CreateAttach(nullptr, 0, &attach_num, attach.put());
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMessage::CreateAttach");
        }

        std::wstring filename = filename_of(metadata.source_relative_path);
        std::wstring mime_tag = L"message/rfc822";
        SPropValue props[4]{};
        props[0].ulPropTag = PR_ATTACH_METHOD;
        props[0].Value.ul = ATTACH_BY_VALUE;
        props[1].ulPropTag = PR_ATTACH_LONG_FILENAME_W;
        props[1].Value.lpszW = const_cast<LPWSTR>(filename.c_str());
        props[2].ulPropTag = PR_ATTACH_FILENAME_W;
        props[2].Value.lpszW = const_cast<LPWSTR>(filename.c_str());
        props[3].ulPropTag = PR_ATTACH_MIME_TAG_W;
        props[3].Value.lpszW = const_cast<LPWSTR>(mime_tag.c_str());
        hr = attach->SetProps(4, props, nullptr);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IAttach::SetProps");
        }

        // Stream the original bytes into PR_ATTACH_DATA_BIN (never loaded
        // whole into memory; CopyTo streams in provider-sized chunks).
        MapiPtr<IStream> dest;
        hr = attach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, 0,
                                  MAPI_CREATE | MAPI_MODIFY, dest.put_unknown());
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr),
                                      "IAttach::OpenProperty(PR_ATTACH_DATA_BIN)");
        }
        auto source = open_read_only_stream(eml_absolute_path);
        if (!source.ok()) return source.error();

        STATSTG stat{};
        hr = source.value()->Stat(&stat, STATFLAG_NONAME);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IStream::Stat");
        }
        ULARGE_INTEGER copied_read{}, copied_written{};
        hr = source.value()->CopyTo(dest.get(), stat.cbSize, &copied_read, &copied_written);
        if (FAILED(hr) || copied_written.QuadPart != stat.cbSize.QuadPart) {
            return make_hresult_error(static_cast<int32_t>(hr), "IStream::CopyTo",
                                      "original EML could not be attached completely");
        }
        hr = dest->Commit(STGC_DEFAULT);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IStream::Commit");
        }
        hr = attach->SaveChanges(0);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IAttach::SaveChanges");
        }
        return Status::success();
    }

    MapiRuntime* runtime_;
    std::unique_ptr<TemporaryProfile> profile_;
    std::unique_ptr<PstStore> store_;
    EntryId root_;
    TrackingTags tags_;
    std::unique_ptr<MimeConverter> converter_;
    std::map<std::wstring, EntryId> folder_cache_;
    bool closed_ = false;
};

class MapiEnvironment final : public IMapiEnvironment {
public:
    ~MapiEnvironment() override {
        runtime_.uninitialize();
        com_.uninitialize();
    }

    Status preflight_check() override {
        if (Status s = ensure_ready(); !s.ok()) return s;

        // MIME converter can be instantiated (spec section 6).
        auto conv = MimeConverter::create();
        if (!conv.ok()) return conv.error();

        // Temporary profile round-trip + Unicode PST provider availability.
        auto profile = TemporaryProfile::create(runtime_, "WLM2PST-");
        if (!profile.ok()) return profile.error();
        Status probe = profile.value()->probe_unicode_pst_service();
        Status removal = profile.value()->remove();  // never leave the probe behind
        if (!probe.ok()) return probe;
        return removal;
    }

    Result<std::unique_ptr<IPstSession>> create_session(
        const std::wstring& pst_path, const std::wstring& root_name, bool must_exist,
        const std::string& profile_purpose) override {
        if (Status s = ensure_ready(); !s.ok()) return s.error();

        if (must_exist) {
            DWORD attrs = GetFileAttributesW(pst_path.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                return make_win32_error(GetLastError(), "GetFileAttributesW",
                                        "PST file expected to exist but was not found");
            }
        }

        std::string prefix = profile_purpose == "validate" ? "WLM2PST-VALIDATE-" : "WLM2PST-";
        auto profile = TemporaryProfile::create(runtime_, prefix);
        if (!profile.ok()) return profile.error();

        if (Status s = profile.value()->add_unicode_pst_service(pst_path, L"WLM2PST Output");
            !s.ok()) {
            (void)profile.value()->remove();
            return s.error();
        }

        auto store = PstStore::logon_and_open(runtime_, profile.value()->name());
        if (!store.ok()) {
            (void)profile.value()->remove();
            return store.error();
        }

        auto root = store.value()->ensure_root_folder(root_name);
        if (!root.ok()) {
            store.value()->close();
            (void)profile.value()->remove();
            return root.error();
        }

        auto tags = resolve_tracking_tags(*store.value()->store(), runtime_);
        if (!tags.ok()) {
            store.value()->close();
            (void)profile.value()->remove();
            return tags.error();
        }

        return std::unique_ptr<IPstSession>(new MapiPstSession(
            runtime_, std::move(profile.value()), std::move(store.value()),
            std::move(root.value()), tags.value()));
    }

    Status cleanup_stale_profiles() override {
        if (Status s = ensure_ready(); !s.ok()) return s;
        auto removed = cleanup_stale_wlm2pst_profiles(runtime_);
        if (!removed.ok()) return removed.error();
        return Status::success();
    }

private:
    Status ensure_ready() {
        if (ready_) return Status::success();
        HRESULT hr = com_.initialize();
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "CoInitializeEx");
        }
        if (Status s = runtime_.load(); !s.ok()) return s;
        if (Status s = runtime_.initialize(); !s.ok()) return s;
        ready_ = true;
        return Status::success();
    }

    ComInit com_;
    MapiRuntime runtime_;
    bool ready_ = false;
};

}  // namespace

std::unique_ptr<IMapiEnvironment> make_mapi_environment() {
    return std::make_unique<MapiEnvironment>();
}

}  // namespace wlm2pst
