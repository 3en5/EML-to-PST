#include <catch2/catch_test_macros.hpp>

#include <string>

#include "worker/headers/header_inspector.h"

using wlm2pst::HeaderInspection;
using wlm2pst::inspect_headers;

TEST_CASE("basic CRLF headers with separator", "[headers]") {
    std::string raw =
        "Subject: Hello\r\n"
        "From: a@example.com\r\n"
        "\r\n"
        "Body text here.";
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    REQUIRE_FALSE(h.malformed);
    REQUIRE(h.fields.size() == 2);
    CHECK(h.fields[0].name == "Subject");
    CHECK(h.fields[0].value == "Hello");
    CHECK(h.fields[1].name == "From");
    CHECK(h.fields[1].value == "a@example.com");
    // header_bytes should point right after the blank line's CRLF.
    CHECK(raw.substr(h.header_bytes) == "Body text here.");
}

TEST_CASE("bare LF headers with separator", "[headers]") {
    std::string raw =
        "Subject: Hello\n"
        "From: a@example.com\n"
        "\n"
        "Body";
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    REQUIRE(h.fields.size() == 2);
    CHECK(h.fields[0].value == "Hello");
    CHECK(raw.substr(h.header_bytes) == "Body");
}

TEST_CASE("mixed CRLF and LF line endings", "[headers]") {
    std::string raw =
        "Subject: Hello\r\n"
        "From: a@example.com\n"
        "To: b@example.com\r\n"
        "\r\n"
        "Body";
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    REQUIRE(h.fields.size() == 3);
    CHECK(h.fields[0].value == "Hello");
    CHECK(h.fields[1].value == "a@example.com");
    CHECK(h.fields[2].value == "b@example.com");
}

TEST_CASE("bare CR line ending is flagged malformed", "[headers]") {
    std::string raw = "Subject: Hello\rFrom: a@example.com\r\rBody";
    auto h = inspect_headers(raw);
    REQUIRE(h.malformed);
    bool found = false;
    for (auto& r : h.malformed_reasons) {
        if (r == "bare-cr") found = true;
    }
    CHECK(found);
    REQUIRE(h.separator_found);
    REQUIRE(h.fields.size() == 2);
}

TEST_CASE("unfolding: single continuation line", "[headers]") {
    std::string raw =
        "Subject: Hello\r\n"
        " world\r\n"
        "\r\n";
    auto h = inspect_headers(raw);
    REQUIRE(h.fields.size() == 1);
    CHECK(h.fields[0].value == "Hello world");
}

TEST_CASE("unfolding: multiple continuation lines with tab", "[headers]") {
    std::string raw =
        "Subject: Hello\r\n"
        "\tworld\r\n"
        "   again\r\n"
        "\r\n";
    auto h = inspect_headers(raw);
    REQUIRE(h.fields.size() == 1);
    CHECK(h.fields[0].value == "Hello world again");
}

TEST_CASE("unfolding: continuation after Received header", "[headers]") {
    std::string raw =
        "Received: from a.example.com\r\n"
        "\tby b.example.com; Tue, 1 Jul 2003 10:52:37 +0200\r\n"
        "\r\n";
    auto h = inspect_headers(raw);
    REQUIRE(h.fields.size() == 1);
    CHECK(h.fields[0].name == "Received");
    CHECK(h.fields[0].value == "from a.example.com by b.example.com; Tue, 1 Jul 2003 10:52:37 +0200");
}

TEST_CASE("missing separator: end of input reached with no blank line", "[headers]") {
    std::string raw =
        "Subject: Hello\r\n"
        "From: a@example.com\r\n";
    auto h = inspect_headers(raw);
    REQUIRE_FALSE(h.separator_found);
    CHECK(h.header_bytes == raw.size());
    REQUIRE(h.fields.size() == 2);
}

TEST_CASE("missing separator: trailing line with no terminator at all", "[headers]") {
    std::string raw = "Subject: Hello";
    auto h = inspect_headers(raw);
    REQUIRE_FALSE(h.separator_found);
    CHECK(h.header_bytes == raw.size());
    REQUIRE(h.fields.size() == 1);
    CHECK(h.fields[0].value == "Hello");
}

TEST_CASE("cap enforcement: header exceeding cap is truncated without finding separator", "[headers]") {
    // Build a synthetic header far larger than 1 MiB, with no blank line at all.
    std::string raw;
    raw.reserve(1024 * 1024 * 2);
    std::string one_line = "X-Filler: ";
    one_line.append(200, 'a');
    one_line += "\r\n";
    while (raw.size() < 1024 * 1024 + 4096) {
        raw += one_line;
    }
    // Ensure there is no blank line anywhere and body would follow far later.
    raw += "\r\nThis-should-not-be-seen: true\r\n";

    auto h = inspect_headers(raw, wlm2pst::kDefaultHeaderCap);
    REQUIRE_FALSE(h.separator_found);
    CHECK(h.header_bytes == wlm2pst::kDefaultHeaderCap);
    // Never scanned past the cap, so the separator further in was never found.
    CHECK(h.header_bytes <= raw.size());
}

TEST_CASE("cap enforcement: custom small cap truncates mid-header", "[headers]") {
    std::string raw =
        "Subject: Hello\r\n"
        "From: a@example.com\r\n"
        "\r\n"
        "Body";
    auto h = inspect_headers(raw, 10);
    REQUIRE_FALSE(h.separator_found);
    CHECK(h.header_bytes == 10);
}

TEST_CASE("NUL bytes in header section are skipped and flagged", "[headers]") {
    std::string raw = std::string("Subject: He") + '\0' + "llo\r\n\r\n";
    auto h = inspect_headers(raw);
    REQUIRE(h.malformed);
    bool found = false;
    for (auto& r : h.malformed_reasons) {
        if (r == "nul-in-header") found = true;
    }
    CHECK(found);
    REQUIRE(h.fields.size() == 1);
    CHECK(h.fields[0].value == "Hello");
}

TEST_CASE("UTF-8 BOM is stripped and noted but not malformed alone", "[headers]") {
    std::string raw = std::string("\xEF\xBB\xBF") + "Subject: Hello\r\n\r\n";
    auto h = inspect_headers(raw);
    REQUIRE_FALSE(h.malformed);
    bool found = false;
    for (auto& r : h.malformed_reasons) {
        if (r == "utf8-bom") found = true;
    }
    CHECK(found);
    REQUIRE(h.fields.size() == 1);
    CHECK(h.fields[0].name == "Subject");
    CHECK(h.fields[0].value == "Hello");
}

TEST_CASE("BOM combined with a real malformation still sets malformed", "[headers]") {
    std::string raw = std::string("\xEF\xBB\xBF") + "NoColonHere\r\n\r\n";
    auto h = inspect_headers(raw);
    REQUIRE(h.malformed);
}

TEST_CASE("X-Unsent variants", "[headers]") {
    SECTION("exactly 1") {
        auto h = inspect_headers("X-Unsent: 1\r\n\r\n");
        CHECK(h.x_unsent());
    }
    SECTION("padded with spaces") {
        auto h = inspect_headers("X-Unsent:  1  \r\n\r\n");
        CHECK(h.x_unsent());
    }
    SECTION("zero") {
        auto h = inspect_headers("X-Unsent: 0\r\n\r\n");
        CHECK_FALSE(h.x_unsent());
    }
    SECTION("absent") {
        auto h = inspect_headers("Subject: hi\r\n\r\n");
        CHECK_FALSE(h.x_unsent());
    }
}

TEST_CASE("case-insensitive header lookup", "[headers]") {
    std::string raw = "sUbJeCt: Hello\r\n\r\n";
    auto h = inspect_headers(raw);
    auto v = h.first_value("SUBJECT");
    REQUIRE(v.has_value());
    CHECK(*v == "Hello");
    CHECK(h.first_value("subject").has_value());
    CHECK_FALSE(h.first_value("Missing").has_value());
}

TEST_CASE("Date and Message-ID convenience accessors", "[headers]") {
    std::string raw =
        "Date: Tue, 1 Jul 2003 10:52:37 +0200\r\n"
        "Message-ID: <abc123@example.com>\r\n"
        "\r\n";
    auto h = inspect_headers(raw);
    REQUIRE(h.date().has_value());
    CHECK(*h.date() == "Tue, 1 Jul 2003 10:52:37 +0200");
    REQUIRE(h.message_id().has_value());
    CHECK(*h.message_id() == "<abc123@example.com>");
}

TEST_CASE("multiple Received headers preserved in file order", "[headers]") {
    std::string raw =
        "Received: from first.example.com; Tue, 1 Jul 2003 10:52:37 +0200\r\n"
        "Received: from second.example.com; Tue, 1 Jul 2003 10:50:00 +0200\r\n"
        "Subject: hi\r\n"
        "\r\n";
    auto h = inspect_headers(raw);
    auto received = h.received();
    REQUIRE(received.size() == 2);
    CHECK(received[0] == "from first.example.com; Tue, 1 Jul 2003 10:52:37 +0200");
    CHECK(received[1] == "from second.example.com; Tue, 1 Jul 2003 10:50:00 +0200");
}

TEST_CASE("first_received_date_candidate uses text after the LAST semicolon", "[headers]") {
    std::string raw =
        "Received: from a.example.com (comment; with a semicolon) by b.example.com "
        "id ABC123; Tue, 1 Jul 2003 10:52:37 +0200\r\n"
        "\r\n";
    auto h = inspect_headers(raw);
    auto candidate = h.first_received_date_candidate();
    REQUIRE(candidate.has_value());
    CHECK(*candidate == "Tue, 1 Jul 2003 10:52:37 +0200");
}

TEST_CASE("first_received_date_candidate is nullopt without Received header", "[headers]") {
    std::string raw = "Subject: hi\r\n\r\n";
    auto h = inspect_headers(raw);
    CHECK_FALSE(h.first_received_date_candidate().has_value());
}

TEST_CASE("first_received_date_candidate is nullopt when Received has no semicolon", "[headers]") {
    std::string raw = "Received: from a.example.com by b.example.com\r\n\r\n";
    auto h = inspect_headers(raw);
    CHECK_FALSE(h.first_received_date_candidate().has_value());
}

TEST_CASE("line without colon before separator is flagged malformed and skipped", "[headers]") {
    std::string raw =
        "Subject: Hello\r\n"
        "ThisLineHasNoColon\r\n"
        "From: a@example.com\r\n"
        "\r\n";
    auto h = inspect_headers(raw);
    REQUIRE(h.malformed);
    bool found = false;
    for (auto& r : h.malformed_reasons) {
        if (r == "line-without-colon") found = true;
    }
    CHECK(found);
    // The malformed line itself is not turned into a field.
    REQUIRE(h.fields.size() == 2);
    CHECK(h.fields[0].name == "Subject");
    CHECK(h.fields[1].name == "From");
}

TEST_CASE("empty input yields no separator and no fields", "[headers]") {
    auto h = inspect_headers("");
    CHECK_FALSE(h.separator_found);
    CHECK(h.fields.empty());
    CHECK(h.header_bytes == 0);
}

TEST_CASE("separator as the very first line", "[headers]") {
    std::string raw = "\r\nBody only";
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK(h.fields.empty());
    CHECK(raw.substr(h.header_bytes) == "Body only");
}
