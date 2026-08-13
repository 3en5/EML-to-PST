// WLM2PST - tolerant RFC 5322 date-time parser for real-world mail (spec
// section 13). Portable: no Windows headers.
#pragma once

#include <optional>
#include <string_view>

#include "common/model.h"

namespace wlm2pst {

// Parses an RFC-5322-style date-time, e.g. "Tue, 1 Jul 2003 10:52:37 +0200",
// tolerating the obsolete forms real-world mail still produces: optional
// (and possibly inconsistent) day-of-week, 1-2 digit day, case-insensitive
// month names, 2- and 3-digit years, missing seconds, missing zone,
// obsolete named zones, single-letter military zones, and parenthesized
// comments anywhere in the string. Returns nullopt when the text cannot be
// parsed as a plausible date-time (including calendar-invalid dates such as
// February 30).
std::optional<FileTimeUtc> parse_rfc5322_date(std::string_view text);

// Same as parse_rfc5322_date, additionally rejecting results outside
// documented sanity bounds (spec section 13): earlier than
// 1970-01-01T00:00:00Z, or later than `now_utc` plus one year.
std::optional<FileTimeUtc> parse_rfc5322_date_bounded(std::string_view text, FileTimeUtc now_utc);

}  // namespace wlm2pst
