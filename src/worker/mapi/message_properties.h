// WLM2PST - message state flags, date handling, and tracking named
// properties (spec sections 12, 13, 15).
#pragma once

#include "common/errors/result.h"
#include "common/model.h"
#include "worker/import_engine.h"
#include "worker/mapi/mapi_raii.h"
#include "worker/mapi/mapi_runtime.h"

#include <optional>
#include <string>

namespace wlm2pst::mapi {

// Resolved MAPI property tags for the six WLM2PST tracking properties.
// Named-property IDs are store-wide: resolve once per session against the
// store and reuse for every message (GetIDsFromNames, spec section 15).
struct TrackingTags {
    ULONG source_relative_path = 0;  // PT_UNICODE
    ULONG source_sha256 = 0;         // PT_UNICODE, lowercase hex
    ULONG import_version = 0;        // PT_UNICODE
    ULONG run_id = 0;                // PT_UNICODE
    ULONG source_size = 0;           // PT_I8
    ULONG source_last_write_utc = 0; // PT_SYSTIME
};

Result<TrackingTags> resolve_tracking_tags(IMsgStore& store, MapiRuntime& runtime);

// Sets all six tracking properties. Must run before the final SaveChanges.
Status set_tracking_properties(IMessage& message, const TrackingTags& tags,
                               const MessageMetadata& metadata);

// Read/sent/draft state (spec section 12), applied before the first save:
//  * every archive message: MSGFLAG_READ
//  * sent: clear MSGFLAG_UNSENT, set MSGFLAG_FROMME
//  * draft: set MSGFLAG_UNSENT (and never SubmitMessage anywhere)
Status apply_message_state(IMessage& message, MapiRuntime& runtime,
                           bool mark_sent, bool mark_draft);

// Date handling (spec section 13). Reads the converter-produced
// PR_MESSAGE_DELIVERY_TIME / PR_CLIENT_SUBMIT_TIME, validates them against
// documented bounds (>= 1970-01-01, <= now + 1 year), and applies the
// pipeline-resolved fallbacks only where the converter value is missing or
// invalid. Returns true when any fallback was written.
Result<bool> apply_date_policy(IMessage& message, MapiRuntime& runtime,
                               const MessageMetadata& metadata);

// FILETIME <-> FileTimeUtc helpers.
FILETIME filetime_from_ticks(FileTimeUtc ticks) noexcept;
FileTimeUtc ticks_from_filetime(const FILETIME& ft) noexcept;

// Conversion-integrity gate (defence against a converter that reports
// success without converting - observed on Click-to-Run when the wrong dll
// serves IConverterSession). Fails when the source declared a Subject but
// the converted message has none, or the source was multipart/mixed but the
// message has an empty attachment table. A failure routes the message into
// the normalized-retry / preserve-as-attachment path instead of persisting
// silently corrupt output.
Status verify_conversion_integrity(IMessage& message, MapiRuntime& runtime,
                                   const MessageMetadata& metadata);

}  // namespace wlm2pst::mapi
