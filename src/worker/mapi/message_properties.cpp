#include "worker/mapi/message_properties.h"

#include "common/unicode/utf.h"
#include "worker/mapi/mapi_constants.h"

#include <array>
#include <cstring>

namespace wlm2pst::mapi {

FILETIME filetime_from_ticks(FileTimeUtc ticks) noexcept {
    FILETIME ft{};
    ft.dwLowDateTime = static_cast<DWORD>(static_cast<uint64_t>(ticks) & 0xFFFFFFFFull);
    ft.dwHighDateTime = static_cast<DWORD>(static_cast<uint64_t>(ticks) >> 32);
    return ft;
}

FileTimeUtc ticks_from_filetime(const FILETIME& ft) noexcept {
    return static_cast<FileTimeUtc>(
        (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
}

Result<TrackingTags> resolve_tracking_tags(IMsgStore& store, MapiRuntime& runtime) {
    constexpr size_t kCount = 6;
    const wchar_t* names[kCount] = {
        kPropSourceRelativePath, kPropSourceSha256, kPropImportVersion,
        kPropRunId, kPropSourceSize, kPropSourceLastWriteUtc,
    };
    std::array<MAPINAMEID, kCount> name_ids{};
    std::array<LPMAPINAMEID, kCount> name_ptrs{};
    GUID prop_set = kWlm2PstPropertySetGuid;
    for (size_t i = 0; i < kCount; ++i) {
        name_ids[i].lpguid = &prop_set;
        name_ids[i].ulKind = MNID_STRING;
        name_ids[i].Kind.lpwstrName = const_cast<LPWSTR>(names[i]);
        name_ptrs[i] = &name_ids[i];
    }

    LPSPropTagArray tags_raw = nullptr;
    HRESULT hr = store.GetIDsFromNames(kCount, name_ptrs.data(), MAPI_CREATE, &tags_raw);
    MapiBuffer guard(runtime.MAPIFreeBuffer);
    *guard.put() = tags_raw;
    // MAPI_W_ERRORS_RETURNED (warning) means some names failed: treat as error.
    if (FAILED(hr) || hr == MAPI_W_ERRORS_RETURNED || !tags_raw || tags_raw->cValues != kCount) {
        return make_hresult_error(static_cast<int32_t>(hr), "GetIDsFromNames",
                                  "WLM2PST tracking properties could not be registered");
    }
    for (ULONG i = 0; i < kCount; ++i) {
        if (PROP_ID(tags_raw->aulPropTag[i]) == 0 ||
            PROP_TYPE(tags_raw->aulPropTag[i]) == PT_ERROR) {
            return make_error("GetIDsFromNames", "tracking property id resolution failed");
        }
    }

    TrackingTags tags;
    tags.source_relative_path = CHANGE_PROP_TYPE(tags_raw->aulPropTag[0], PT_UNICODE);
    tags.source_sha256 = CHANGE_PROP_TYPE(tags_raw->aulPropTag[1], PT_UNICODE);
    tags.import_version = CHANGE_PROP_TYPE(tags_raw->aulPropTag[2], PT_UNICODE);
    tags.run_id = CHANGE_PROP_TYPE(tags_raw->aulPropTag[3], PT_UNICODE);
    tags.source_size = CHANGE_PROP_TYPE(tags_raw->aulPropTag[4], PT_I8);
    tags.source_last_write_utc = CHANGE_PROP_TYPE(tags_raw->aulPropTag[5], PT_SYSTIME);
    return tags;
}

Status set_tracking_properties(IMessage& message, const TrackingTags& tags,
                               const MessageMetadata& metadata) {
    std::wstring sha_w = wide_from_utf8(metadata.sha256_hex);
    std::wstring run_w = wide_from_utf8(metadata.run_id);
    std::wstring version_w = wide_from_utf8(metadata.import_version);

    SPropValue props[6]{};
    props[0].ulPropTag = tags.source_relative_path;
    props[0].Value.lpszW = const_cast<LPWSTR>(metadata.source_relative_path.c_str());
    props[1].ulPropTag = tags.source_sha256;
    props[1].Value.lpszW = const_cast<LPWSTR>(sha_w.c_str());
    props[2].ulPropTag = tags.import_version;
    props[2].Value.lpszW = const_cast<LPWSTR>(version_w.c_str());
    props[3].ulPropTag = tags.run_id;
    props[3].Value.lpszW = const_cast<LPWSTR>(run_w.c_str());
    props[4].ulPropTag = tags.source_size;
    props[4].Value.li.QuadPart = static_cast<LONGLONG>(metadata.source_size);
    props[5].ulPropTag = tags.source_last_write_utc;
    props[5].Value.ft = filetime_from_ticks(metadata.source_last_write_utc);

    HRESULT hr = message.SetProps(6, props, nullptr);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMessage::SetProps(tracking)");
    }
    return Status::success();
}

namespace {

// Reads a single property; empty optional when absent.
std::optional<SPropValue> read_prop(IMessage& message, MapiRuntime& runtime, ULONG tag,
                                    MapiBuffer& keep_alive) {
    SizedSPropTagArray(1, tags) = {1, {tag}};
    ULONG count = 0;
    LPSPropValue values = nullptr;
    HRESULT hr = message.GetProps(reinterpret_cast<LPSPropTagArray>(&tags), 0, &count, &values);
    keep_alive.set_free_fn(runtime.MAPIFreeBuffer);
    *keep_alive.put() = values;
    if (FAILED(hr) || count == 0 || PROP_TYPE(values[0].ulPropTag) == PT_ERROR) {
        return std::nullopt;
    }
    return values[0];
}

}  // namespace

Status apply_message_state(IMessage& message, MapiRuntime& runtime,
                           bool mark_sent, bool mark_draft) {
    MapiBuffer keep_alive;
    ULONG flags = 0;
    if (auto existing = read_prop(message, runtime, PR_MESSAGE_FLAGS, keep_alive)) {
        flags = existing->Value.ul;
    }

    // Every imported archive message is read (spec section 12: EML carries no
    // reliable per-user read state; never guess from timestamps).
    flags |= MSGFLAG_READ;
    if (mark_draft) {
        flags |= MSGFLAG_UNSENT;
    } else if (mark_sent) {
        flags &= ~static_cast<ULONG>(MSGFLAG_UNSENT);
        flags |= MSGFLAG_FROMME;
    } else {
        // Received archive mail: not unsent.
        flags &= ~static_cast<ULONG>(MSGFLAG_UNSENT);
    }

    SPropValue prop{};
    prop.ulPropTag = PR_MESSAGE_FLAGS;
    prop.Value.ul = flags;
    HRESULT hr = message.SetProps(1, &prop, nullptr);
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "IMessage::SetProps(PR_MESSAGE_FLAGS)");
    }
    return Status::success();
}

namespace {

// Documented bounds (spec section 13): reject before 1970-01-01T00:00:00Z or
// more than one year past now. Also rejects the zero/uninitialized value.
bool filetime_in_bounds(FileTimeUtc value, FileTimeUtc now_utc) {
    constexpr int64_t kTicksPerYear = 365ll * 24 * 60 * 60 * kFiletimeTicksPerSecond;
    if (value < kFiletimeUnixEpoch) return false;
    if (now_utc > 0 && value > now_utc + kTicksPerYear) return false;
    return true;
}

}  // namespace

Status verify_conversion_integrity(IMessage& message, MapiRuntime& runtime,
                                   const MessageMetadata& metadata) {
    if (metadata.source_declared_subject) {
        MapiBuffer keep_alive;
        auto subject = read_prop(message, runtime, PR_SUBJECT_W, keep_alive);
        bool ok = subject && PROP_TYPE(subject->ulPropTag) == PT_UNICODE &&
                  subject->Value.lpszW != nullptr && subject->Value.lpszW[0] != L'\0';
        if (!ok) {
            return make_error("conversion_integrity",
                              "source declared a Subject but the converted message has none "
                              "(converter did not actually convert)");
        }
    }
    if (metadata.source_multipart_mixed) {
        MapiPtr<IMAPITable> attachments;
        HRESULT hr = message.GetAttachmentTable(0, attachments.put());
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "conversion_integrity",
                                      "attachment table unreadable after conversion");
        }
        ULONG rows = 0;
        hr = attachments->GetRowCount(0, &rows);
        if (FAILED(hr) || rows == 0) {
            return make_error("conversion_integrity",
                              "source is multipart/mixed but the converted message has no "
                              "attachments (converter did not actually convert)");
        }
    }
    return Status::success();
}

Result<bool> apply_date_policy(IMessage& message, MapiRuntime& runtime,
                               const MessageMetadata& metadata) {
    bool fallback_applied = false;
    SPropValue to_set[2]{};
    ULONG set_count = 0;

    {
        MapiBuffer keep_alive;
        auto delivery = read_prop(message, runtime, PR_MESSAGE_DELIVERY_TIME, keep_alive);
        bool valid = delivery &&
                     filetime_in_bounds(ticks_from_filetime(delivery->Value.ft), metadata.now_utc);
        if (!valid && metadata.fallback_message_delivery_time) {
            to_set[set_count].ulPropTag = PR_MESSAGE_DELIVERY_TIME;
            to_set[set_count].Value.ft =
                filetime_from_ticks(*metadata.fallback_message_delivery_time);
            ++set_count;
            fallback_applied = true;
        }
    }
    {
        MapiBuffer keep_alive;
        auto submit = read_prop(message, runtime, PR_CLIENT_SUBMIT_TIME, keep_alive);
        bool valid = submit &&
                     filetime_in_bounds(ticks_from_filetime(submit->Value.ft), metadata.now_utc);
        // Submit time matters for sent messages (spec section 12: preserve or
        // set the client submit time); harmless and useful elsewhere only
        // when the pipeline provided a value.
        if (!valid && metadata.fallback_client_submit_time) {
            to_set[set_count].ulPropTag = PR_CLIENT_SUBMIT_TIME;
            to_set[set_count].Value.ft =
                filetime_from_ticks(*metadata.fallback_client_submit_time);
            ++set_count;
            fallback_applied = true;
        }
    }

    if (set_count > 0) {
        HRESULT hr = message.SetProps(set_count, to_set, nullptr);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IMessage::SetProps(dates)");
        }
    }
    return fallback_applied;
}

}  // namespace wlm2pst::mapi
