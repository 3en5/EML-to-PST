// WLM2PST - unit tests for the conservative attempt-2 EML repair (spec
// section 16) implemented in worker/normalize/eml_normalizer.h.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>

#include "worker/normalize/eml_normalizer.h"

using wlm2pst::EmlNormalization;
using wlm2pst::normalize_eml_header;

namespace {

std::string read_fixture(const std::string& name) {
    std::string path = std::string(WLM2PST_FIXTURES_DIR) + "/eml/" + name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool has_repair(const EmlNormalization& n, const std::string& repair) {
    return std::find(n.repairs.begin(), n.repairs.end(), repair) != n.repairs.end();
}

std::string_view header_view(const EmlNormalization& n) {
    return std::string_view(n.normalized_header.data(), n.normalized_header.size());
}

}  // namespace

TEST_CASE("LF-only input is normalized to CRLF and recorded as a line-endings repair",
          "[normalize]") {
    std::string raw =
        "Subject: Hello\n"
        "From: test-sender@example.invalid\n"
        "\n"
        "Body\n";
    auto n = normalize_eml_header(raw);
    CHECK(n.changed);
    CHECK(has_repair(n, "line-endings"));
    CHECK_FALSE(n.separator_inserted);
    // Every header line ends CRLF, including the blank separator line.
    auto view = header_view(n);
    CHECK(view.find("Subject: Hello\r\n") != std::string_view::npos);
    CHECK(view.find("From: test-sender@example.invalid\r\n") != std::string_view::npos);
    CHECK(view.size() >= 2);
    CHECK(view.substr(view.size() - 2) == "\r\n");
    // Body starts right after the blank line in the ORIGINAL buffer.
    CHECK(raw.substr(n.original_header_bytes) == "Body\n");
}

TEST_CASE("UTF-8 BOM is stripped and recorded as a repair", "[normalize]") {
    std::string raw = std::string("\xEF\xBB\xBF") + "Subject: Hello\r\nFrom: test-sender@example.invalid\r\n\r\nBody";
    auto n = normalize_eml_header(raw);
    CHECK(n.changed);
    CHECK(has_repair(n, "utf8-bom"));
    auto view = header_view(n);
    // The BOM bytes must not appear anywhere in the rewritten header.
    CHECK(view.find("\xEF\xBB\xBF") == std::string_view::npos);
    CHECK(view.substr(0, 9) == "Subject: ");
    CHECK(raw.substr(n.original_header_bytes) == "Body");
}

TEST_CASE("NUL byte in a header value is removed and recorded as a repair", "[normalize]") {
    std::string raw = std::string("Subject: Hello") + '\0' + "World\r\nFrom: test-sender@example.invalid\r\n\r\nBody";
    auto n = normalize_eml_header(raw);
    CHECK(n.changed);
    CHECK(has_repair(n, "nul"));
    auto view = header_view(n);
    CHECK(view.find('\0') == std::string_view::npos);
    CHECK(view.find("Subject: HelloWorld\r\n") != std::string_view::npos);
    CHECK(raw.substr(n.original_header_bytes) == "Body");
}

TEST_CASE("missing separator: boundary identified at the first non-header-shaped line",
          "[normalize]") {
    std::string raw =
        "Subject: Missing separator test\r\n"
        "From: test-sender@example.invalid\r\n"
        "This is the body text with no separator.\r\n";
    auto n = normalize_eml_header(raw);
    CHECK(n.changed);
    CHECK(n.separator_inserted);
    CHECK(has_repair(n, "separator-inserted"));
    // original_header_bytes must point at the body's first byte: the start
    // of the "This is the body..." line in the ORIGINAL buffer.
    size_t expected_body_start = raw.find("This is the body");
    REQUIRE(expected_body_start != std::string::npos);
    CHECK(n.original_header_bytes == expected_body_start);
    CHECK(raw.substr(n.original_header_bytes) == "This is the body text with no separator.\r\n");
    // The rewritten header still ends with a blank CRLF separator line.
    auto view = header_view(n);
    CHECK(view.size() >= 2);
    CHECK(view.substr(view.size() - 2) == "\r\n");
    CHECK(view.substr(view.size() - 4) == "\r\n\r\n");
    // No header line for the body content was carried over.
    CHECK(view.find("This is the body") == std::string_view::npos);
}

TEST_CASE("clean CRLF input with a normal separator is reported unchanged", "[normalize]") {
    std::string raw =
        "Subject: Hello\r\n"
        "From: test-sender@example.invalid\r\n"
        "\r\n"
        "Body text.";
    auto n = normalize_eml_header(raw);
    CHECK_FALSE(n.changed);
    CHECK_FALSE(n.separator_inserted);
    CHECK(n.repairs.empty());
    CHECK(raw.substr(n.original_header_bytes) == "Body text.");
}

TEST_CASE("empty input is reported as an inserted separator over an empty header",
          "[normalize]") {
    auto n = normalize_eml_header("");
    CHECK(n.changed);
    CHECK(n.separator_inserted);
    CHECK(has_repair(n, "separator-inserted"));
    CHECK(n.original_header_bytes == 0);
    auto view = header_view(n);
    CHECK(view == "\r\n");
}

TEST_CASE("input that is only headers, with no body at all, gets a separator appended",
          "[normalize]") {
    std::string raw =
        "Subject: Only headers\r\n"
        "From: test-sender@example.invalid\r\n";
    auto n = normalize_eml_header(raw);
    CHECK(n.changed);
    CHECK(n.separator_inserted);
    CHECK(has_repair(n, "separator-inserted"));
    CHECK(n.original_header_bytes == raw.size());
    CHECK(raw.substr(n.original_header_bytes).empty());
    auto view = header_view(n);
    CHECK(view.substr(view.size() - 2) == "\r\n");
}

TEST_CASE("bare-CR-only line endings are normalized to CRLF", "[normalize]") {
    std::string raw = "Subject: Hello\rFrom: test-sender@example.invalid\r\rBody";
    auto n = normalize_eml_header(raw);
    CHECK(n.changed);
    CHECK(has_repair(n, "line-endings"));
    auto view = header_view(n);
    CHECK(view.find("Subject: Hello\r\n") != std::string_view::npos);
    CHECK(view.find("From: test-sender@example.invalid\r\n") != std::string_view::npos);
    CHECK(raw.substr(n.original_header_bytes) == "Body");
}

TEST_CASE("normalized_header always ends with a blank CRLF separator line", "[normalize]") {
    SECTION("clean input") {
        auto n = normalize_eml_header("Subject: Hi\r\n\r\nBody");
        auto view = header_view(n);
        CHECK(view.substr(view.size() - 4) == "\r\n\r\n");
    }
    SECTION("LF input") {
        auto n = normalize_eml_header("Subject: Hi\n\nBody");
        auto view = header_view(n);
        CHECK(view.substr(view.size() - 4) == "\r\n\r\n");
    }
    SECTION("no separator at all") {
        auto n = normalize_eml_header("Subject: Hi\r\nFrom: a@example.invalid\r\n");
        auto view = header_view(n);
        CHECK(view.substr(view.size() - 4) == "\r\n\r\n");
    }
}

// --- Fixture-driven checks: run the normalizer over the actual malformed
// fixtures used across the corpus (spec section 28) and assert the same
// facts the byte-level tests above establish synthetically.

TEST_CASE("normalizing malformed_line_endings.eml reports changed with a line-endings repair",
          "[normalize]") {
    auto raw = read_fixture("malformed_line_endings.eml");
    auto n = normalize_eml_header(raw);
    CHECK(n.changed);
    CHECK(has_repair(n, "line-endings"));
    CHECK_FALSE(n.separator_inserted);
    CHECK(raw.substr(n.original_header_bytes).rfind("Body text present after mixed line endings.",
                                                     0) == 0);
}

TEST_CASE("normalizing header_nul.eml reports changed with a nul repair", "[normalize]") {
    auto raw = read_fixture("header_nul.eml");
    auto n = normalize_eml_header(raw);
    CHECK(n.changed);
    CHECK(has_repair(n, "nul"));
    auto view = header_view(n);
    CHECK(view.find('\0') == std::string_view::npos);
    CHECK(view.find("Subject: HelloWorld NUL fixture\r\n") != std::string_view::npos);
}

TEST_CASE("normalizing missing_separator.eml reports changed with a separator-inserted repair",
          "[normalize]") {
    auto raw = read_fixture("missing_separator.eml");
    auto n = normalize_eml_header(raw);
    CHECK(n.changed);
    CHECK(n.separator_inserted);
    CHECK(has_repair(n, "separator-inserted"));
    size_t expected_body_start = raw.find("This is the body");
    REQUIRE(expected_body_start != std::string::npos);
    CHECK(n.original_header_bytes == expected_body_start);
}

TEST_CASE("normalizing utf8_plain.eml (already clean CRLF) reports unchanged", "[normalize]") {
    auto raw = read_fixture("utf8_plain.eml");
    auto n = normalize_eml_header(raw);
    CHECK_FALSE(n.changed);
    CHECK(n.repairs.empty());
}
