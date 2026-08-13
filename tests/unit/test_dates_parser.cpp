// Expected UTC values below are computed independently of date_parser.cpp,
// anchored on well-known Unix timestamps for January 1st of various years
// (e.g. 2000-01-01T00:00:00Z = 946684800, 2020-01-01T00:00:00Z =
// 1577836800), then adjusted by day-of-year offsets and time-of-day/zone
// arithmetic performed in this file's comments and constants.
#include <catch2/catch_test_macros.hpp>

#include "common/model.h"
#include "worker/dates/date_parser.h"

using wlm2pst::filetime_from_unix_seconds;
using wlm2pst::parse_rfc5322_date;
using wlm2pst::parse_rfc5322_date_bounded;

namespace {
constexpr int64_t kRfcExampleUnix = 1057049557;  // 2003-07-01T08:52:37Z
}

TEST_CASE("canonical RFC 5322 example date", "[dates]") {
    auto ft = parse_rfc5322_date("Tue, 1 Jul 2003 10:52:37 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kRfcExampleUnix));
}

TEST_CASE("named zone UT/GMT/Z all mean +0000", "[dates]") {
    constexpr int64_t kExpected = 1577836800;  // 2020-01-01T00:00:00Z
    CHECK(*parse_rfc5322_date("1 Jan 2020 00:00:00 UT") == filetime_from_unix_seconds(kExpected));
    CHECK(*parse_rfc5322_date("1 Jan 2020 00:00:00 GMT") == filetime_from_unix_seconds(kExpected));
    CHECK(*parse_rfc5322_date("1 Jan 2020 00:00:00 Z") == filetime_from_unix_seconds(kExpected));
}

TEST_CASE("named zone EST is -0500", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 EST");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(1577854800));  // 05:00:00Z
}

TEST_CASE("named zone EDT is -0400", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 EDT");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(1577851200));  // 04:00:00Z
}

TEST_CASE("named zone CST is -0600", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 CST");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(1577858400));  // 06:00:00Z
}

TEST_CASE("named zone CDT is -0500", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 CDT");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(1577854800));  // 05:00:00Z
}

TEST_CASE("named zone MST is -0700", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 MST");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(1577862000));  // 07:00:00Z
}

TEST_CASE("named zone MDT is -0600", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 MDT");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(1577858400));  // 06:00:00Z
}

TEST_CASE("named zone PST is -0800", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 PST");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(1577865600));  // 08:00:00Z
}

TEST_CASE("named zone PDT is -0700", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 PDT");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(1577862000));  // 07:00:00Z
}

TEST_CASE("single-letter military zone treated as +0000", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 A");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(1577836800));
}

TEST_CASE("2-digit year 00-49 maps to 2000s", "[dates]") {
    // "03" -> 2003, same instant as the canonical RFC example.
    auto ft = parse_rfc5322_date("1 Jul 03 10:52:37 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kRfcExampleUnix));
}

TEST_CASE("2-digit year 50-99 maps to 1900s", "[dates]") {
    // 1995-07-01T08:52:37Z: anchor 1995-01-01T00:00:00Z = 788918400,
    // + 181 days (Jan..Jun in a non-leap year) * 86400 = 15638400,
    // + 08:52:37 = 31957.
    constexpr int64_t kExpected = 788918400 + 181 * 86400 + 31957;
    auto ft = parse_rfc5322_date("1 Jul 95 10:52:37 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kExpected));
}

TEST_CASE("3-digit year adds 1900", "[dates]") {
    // "103" -> 2003, same instant as the canonical RFC example.
    auto ft = parse_rfc5322_date("1 Jul 103 10:52:37 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kRfcExampleUnix));
}

TEST_CASE("missing day-of-week is accepted", "[dates]") {
    auto ft = parse_rfc5322_date("1 Jul 2003 10:52:37 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kRfcExampleUnix));
}

TEST_CASE("inconsistent day-of-week is ignored, not validated", "[dates]") {
    // 1 Jul 2003 was actually a Tuesday; claiming Sunday must not matter.
    auto ft = parse_rfc5322_date("Sun, 1 Jul 2003 10:52:37 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kRfcExampleUnix));
}

TEST_CASE("missing seconds defaults to :00", "[dates]") {
    // local 10:52:00 +0200 -> UTC 08:52:00Z.
    constexpr int64_t kExpected = 1057049557 - 37;  // strip the 37s component
    auto ft = parse_rfc5322_date("Tue, 1 Jul 2003 10:52 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kExpected));
}

TEST_CASE("missing zone assumes +0000 and still succeeds", "[dates]") {
    // local 10:52:37 with no zone -> treated as UTC directly.
    constexpr int64_t kExpected = 1057017600 + 10 * 3600 + 52 * 60 + 37;  // day base + time
    auto ft = parse_rfc5322_date("Tue, 1 Jul 2003 10:52:37");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kExpected));
}

TEST_CASE("comment after the zone is stripped", "[dates]") {
    auto ft = parse_rfc5322_date("Tue, 1 Jul 2003 10:52:37 +0200 (CEST)");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kRfcExampleUnix));
}

TEST_CASE("comment in the middle of the date is stripped", "[dates]") {
    auto ft = parse_rfc5322_date("Tue, 1 Jul 2003 (weekday comment) 10:52:37 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kRfcExampleUnix));
}

TEST_CASE("nested comment is stripped", "[dates]") {
    auto ft = parse_rfc5322_date("Tue, 1 Jul 2003 10:52:37 +0200 (outer (inner) still outer)");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kRfcExampleUnix));
}

TEST_CASE("leap day is valid in a leap year", "[dates]") {
    // 2020-02-29T12:00:00Z: anchor 2020-01-01 = 1577836800, + 59 days (Jan
    // has 31 days, so Feb 29 is day-of-year index 59) * 86400 = 5097600,
    // + 12:00:00 = 43200.
    constexpr int64_t kExpected = 1577836800 + 59 * 86400 + 43200;
    auto ft = parse_rfc5322_date("29 Feb 2020 12:00:00 GMT");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kExpected));
}

TEST_CASE("February 30 is rejected in every year", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("30 Feb 2020 12:00:00 GMT").has_value());
}

TEST_CASE("February 29 is rejected in a non-leap year", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("29 Feb 2021 12:00:00 GMT").has_value());
}

TEST_CASE("leap second 60 is accepted and clamped to 59", "[dates]") {
    // Same as the canonical example but with ss=60 clamped to 59: +22s
    // relative to the ss=37 case (59 - 37 = 22).
    constexpr int64_t kExpected = kRfcExampleUnix + 22;
    auto ft = parse_rfc5322_date("Tue, 1 Jul 2003 10:52:60 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kExpected));
}

TEST_CASE("second 61 is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("Tue, 1 Jul 2003 10:52:61 +0200").has_value());
}

TEST_CASE("hour 24 is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("Tue, 1 Jul 2003 24:00:00 +0200").has_value());
}

TEST_CASE("minute 60 is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("Tue, 1 Jul 2003 10:60:00 +0200").has_value());
}

TEST_CASE("unknown month name is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("Tue, 1 Foo 2003 10:52:37 +0200").has_value());
}

TEST_CASE("month name is case-insensitive", "[dates]") {
    auto ft = parse_rfc5322_date("Tue, 1 JUL 2003 10:52:37 +0200");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kRfcExampleUnix));
    auto ft2 = parse_rfc5322_date("Tue, 1 jul 2003 10:52:37 +0200");
    REQUIRE(ft2.has_value());
    CHECK(*ft2 == filetime_from_unix_seconds(kRfcExampleUnix));
}

TEST_CASE("valid numeric zone offset +1234", "[dates]") {
    // local 00:00:00 +1234 -> UTC = local - 12h34m = -754 minutes.
    constexpr int64_t kExpected = 1577836800 - 754 * 60;
    auto ft = parse_rfc5322_date("1 Jan 2020 00:00:00 +1234");
    REQUIRE(ft.has_value());
    CHECK(*ft == filetime_from_unix_seconds(kExpected));
}

TEST_CASE("numeric zone hour out of range is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("1 Jan 2020 00:00:00 +1500").has_value());
}

TEST_CASE("numeric zone minute out of range is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("1 Jan 2020 00:00:00 +0060").has_value());
}

TEST_CASE("numeric zone far out of range (near +9959) is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("1 Jan 2020 00:00:00 +9959").has_value());
}

TEST_CASE("unrecognized named zone is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("1 Jan 2020 00:00:00 XYZ").has_value());
}

TEST_CASE("garbage input is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("this is not a date at all").has_value());
}

TEST_CASE("empty input is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("").has_value());
}

TEST_CASE("huge garbage input is rejected without crashing", "[dates]") {
    std::string huge(500000, 'x');
    CHECK_FALSE(parse_rfc5322_date(huge).has_value());
}

TEST_CASE("too few tokens is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("Jul 2003 10:52:37").has_value());
}

TEST_CASE("too many trailing tokens is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("Tue, 1 Jul 2003 10:52:37 +0200 extra junk").has_value());
}

TEST_CASE("day out of range is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("32 Jul 2003 10:52:37 +0200").has_value());
}

TEST_CASE("day zero is rejected", "[dates]") {
    CHECK_FALSE(parse_rfc5322_date("0 Jul 2003 10:52:37 +0200").has_value());
}

TEST_CASE("bounded: date before 1970 is rejected only by the bounded variant", "[dates]") {
    auto unbounded = parse_rfc5322_date("1 Jan 1969 00:00:00 GMT");
    REQUIRE(unbounded.has_value());
    CHECK(*unbounded < filetime_from_unix_seconds(0));

    wlm2pst::FileTimeUtc now = filetime_from_unix_seconds(1577836800);  // 2020-01-01
    auto bounded = parse_rfc5322_date_bounded("1 Jan 1969 00:00:00 GMT", now);
    CHECK_FALSE(bounded.has_value());
}

TEST_CASE("bounded: date more than ~1 year in the future is rejected", "[dates]") {
    wlm2pst::FileTimeUtc now = filetime_from_unix_seconds(1577836800);  // 2020-01-01T00:00:00Z
    // 2022-01-01T00:00:00Z is ~2 years after `now`.
    auto bounded = parse_rfc5322_date_bounded("1 Jan 2022 00:00:00 GMT", now);
    CHECK_FALSE(bounded.has_value());

    auto unbounded = parse_rfc5322_date("1 Jan 2022 00:00:00 GMT");
    REQUIRE(unbounded.has_value());
}

TEST_CASE("bounded: date ~11 months in the future is accepted", "[dates]") {
    wlm2pst::FileTimeUtc now = filetime_from_unix_seconds(1577836800);  // 2020-01-01T00:00:00Z
    // 2020-12-01T00:00:00Z is 11 months after `now`.
    auto bounded = parse_rfc5322_date_bounded("1 Dec 2020 00:00:00 GMT", now);
    REQUIRE(bounded.has_value());
    CHECK(*bounded == filetime_from_unix_seconds(1606780800));
}

TEST_CASE("bounded: an ordinary recent date is accepted", "[dates]") {
    wlm2pst::FileTimeUtc now = filetime_from_unix_seconds(1577836800);  // 2020-01-01
    auto bounded = parse_rfc5322_date_bounded("Tue, 1 Jul 2003 10:52:37 +0200", now);
    REQUIRE(bounded.has_value());
    CHECK(*bounded == filetime_from_unix_seconds(kRfcExampleUnix));
}
