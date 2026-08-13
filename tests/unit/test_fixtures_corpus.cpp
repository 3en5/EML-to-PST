// WLM2PST - structural assertions over the synthetic EML fixture corpus
// (spec section 28). Every fixture in tests/fixtures/eml/ is inspected with
// worker/headers/header_inspector.h; this file is the executable contract
// for what each fixture is supposed to demonstrate.
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <ios>
#include <sstream>
#include <string>

#include "worker/dates/date_parser.h"
#include "worker/headers/header_inspector.h"

using wlm2pst::HeaderInspection;
using wlm2pst::inspect_headers;
using wlm2pst::parse_rfc5322_date;

namespace {

// Reads a fixture file's raw bytes. WLM2PST_FIXTURES_DIR is the tests/fixtures
// directory (compile definition from tests/CMakeLists.txt); fixtures live in
// its eml/ subdirectory.
std::string read_fixture(const std::string& name) {
    std::string path = std::string(WLM2PST_FIXTURES_DIR) + "/eml/" + name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool has_reason(const HeaderInspection& h, const std::string& reason) {
    for (const auto& r : h.malformed_reasons) {
        if (r == reason) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("utf8_plain.eml is well-formed with a plain-text content type", "[fixtures]") {
    auto raw = read_fixture("utf8_plain.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    REQUIRE(h.date().has_value());
    CHECK(parse_rfc5322_date(*h.date()).has_value());
    auto ct = h.first_value("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(ct->find("text/plain") != std::string::npos);
    CHECK(ct->find("utf-8") != std::string::npos);
}

TEST_CASE("utf8_html.eml is well-formed with an html content type", "[fixtures]") {
    auto raw = read_fixture("utf8_html.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto ct = h.first_value("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(ct->find("text/html") != std::string::npos);
}

TEST_CASE("utf8_bom.eml carries a leading BOM that is noted but not malformed", "[fixtures]") {
    auto raw = read_fixture("utf8_bom.eml");
    REQUIRE(raw.size() >= 3);
    CHECK(static_cast<unsigned char>(raw[0]) == 0xEF);
    CHECK(static_cast<unsigned char>(raw[1]) == 0xBB);
    CHECK(static_cast<unsigned char>(raw[2]) == 0xBF);

    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    CHECK(has_reason(h, "utf8-bom"));
    REQUIRE(h.first_value("Subject").has_value());
    CHECK(*h.first_value("Subject") == "UTF-8 BOM fixture");
}

TEST_CASE("windows1255_hebrew.eml body bytes are real cp1255-encoded Hebrew", "[fixtures]") {
    auto raw = read_fixture("windows1255_hebrew.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto ct = h.first_value("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(ct->find("windows-1255") != std::string::npos);

    REQUIRE(h.header_bytes <= raw.size());
    std::string body = raw.substr(h.header_bytes);
    REQUIRE(body.size() >= 2);
    // cp1255 encoding of the Hebrew letters Shin (ש) then Lamed (ל).
    CHECK(static_cast<unsigned char>(body[0]) == 0xF9);
    CHECK(static_cast<unsigned char>(body[1]) == 0xEC);
}

TEST_CASE("iso8859_8_hebrew.eml body bytes are real iso-8859-8-encoded Hebrew", "[fixtures]") {
    auto raw = read_fixture("iso8859_8_hebrew.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto ct = h.first_value("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(ct->find("iso-8859-8") != std::string::npos);

    REQUIRE(h.header_bytes <= raw.size());
    std::string body = raw.substr(h.header_bytes);
    REQUIRE(body.size() >= 4);
    // iso-8859-8 encoding of the Hebrew letters Shin (ש), Lamed (ל), Vav (ו).
    CHECK(static_cast<unsigned char>(body[0]) == 0xF9);
    CHECK(static_cast<unsigned char>(body[1]) == 0xEC);
    CHECK(static_cast<unsigned char>(body[2]) == 0xE5);
}

TEST_CASE("quoted_printable.eml declares quoted-printable transfer encoding", "[fixtures]") {
    auto raw = read_fixture("quoted_printable.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto cte = h.first_value("Content-Transfer-Encoding");
    REQUIRE(cte.has_value());
    CHECK(*cte == "quoted-printable");
    std::string body = raw.substr(h.header_bytes);
    CHECK(body.find("=3D") != std::string::npos);
    CHECK(body.find("=C3=A9") != std::string::npos);
}

TEST_CASE("base64_body.eml declares base64 transfer encoding", "[fixtures]") {
    auto raw = read_fixture("base64_body.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto cte = h.first_value("Content-Transfer-Encoding");
    REQUIRE(cte.has_value());
    CHECK(*cte == "base64");
}

TEST_CASE("rfc2047_subject.eml keeps the encoded-word verbatim (no RFC 2047 decode here)",
          "[fixtures]") {
    auto raw = read_fixture("rfc2047_subject.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto subject = h.first_value("Subject");
    REQUIRE(subject.has_value());
    CHECK(subject->rfind("=?UTF-8?B?", 0) == 0);
    CHECK(subject->find("?=") != std::string::npos);
}

TEST_CASE("hebrew_attachment_name.eml carries RFC 2231 and RFC 2047 filenames in the body",
          "[fixtures]") {
    auto raw = read_fixture("hebrew_attachment_name.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto ct = h.first_value("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(ct->find("multipart/mixed") != std::string::npos);

    std::string body = raw.substr(h.header_bytes);
    // RFC 2231 extended parameter: filename*=UTF-8''%D7%9E%D7%A1%D7%9E%D7%9A.txt
    CHECK(body.find("filename*=UTF-8''%D7%9E%D7%A1%D7%9E%D7%9A.txt") != std::string::npos);
    // RFC 2047 encoded-word filename alongside it.
    CHECK(body.find("filename=\"=?UTF-8?B?") != std::string::npos);
}

TEST_CASE("multipart_alternative.eml has plain and html parts", "[fixtures]") {
    auto raw = read_fixture("multipart_alternative.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto ct = h.first_value("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(ct->find("multipart/alternative") != std::string::npos);
    std::string body = raw.substr(h.header_bytes);
    CHECK(body.find("text/plain") != std::string::npos);
    CHECK(body.find("text/html") != std::string::npos);
}

TEST_CASE("inline_cid_image.eml references a cid: image with a matching Content-ID",
          "[fixtures]") {
    auto raw = read_fixture("inline_cid_image.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto ct = h.first_value("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(ct->find("multipart/related") != std::string::npos);
    std::string body = raw.substr(h.header_bytes);
    CHECK(body.find("cid:tinyimage001@example.invalid") != std::string::npos);
    CHECK(body.find("Content-ID: <tinyimage001@example.invalid>") != std::string::npos);
    CHECK(body.find("image/png") != std::string::npos);
}

TEST_CASE("single_attachment.eml has exactly one attachment part", "[fixtures]") {
    auto raw = read_fixture("single_attachment.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    std::string body = raw.substr(h.header_bytes);
    size_t first = body.find("Content-Disposition: attachment");
    REQUIRE(first != std::string::npos);
    size_t second = body.find("Content-Disposition: attachment", first + 1);
    CHECK(second == std::string::npos);
}

TEST_CASE("multiple_attachments.eml has two attachment parts", "[fixtures]") {
    auto raw = read_fixture("multiple_attachments.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    std::string body = raw.substr(h.header_bytes);
    size_t count = 0;
    size_t pos = 0;
    while ((pos = body.find("Content-Disposition: attachment", pos)) != std::string::npos) {
        ++count;
        pos += 1;
    }
    CHECK(count == 2);
}

TEST_CASE("nested_eml_attachment.eml embeds a complete inner EML", "[fixtures]") {
    auto raw = read_fixture("nested_eml_attachment.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    std::string body = raw.substr(h.header_bytes);
    CHECK(body.find("Content-Type: message/rfc822") != std::string::npos);
    // The inner message has its own independently valid header block.
    CHECK(body.find("Subject: Inner nested message") != std::string::npos);
    CHECK(body.find("This is the body of the inner, nested EML message.") != std::string::npos);
}

TEST_CASE("empty_body.eml has a separator and a zero-length body", "[fixtures]") {
    auto raw = read_fixture("empty_body.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    CHECK(h.header_bytes == raw.size());
    CHECK(raw.substr(h.header_bytes).empty());
}

TEST_CASE("missing_subject.eml has no Subject header", "[fixtures]") {
    auto raw = read_fixture("missing_subject.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    CHECK_FALSE(h.first_value("Subject").has_value());
    REQUIRE(h.date().has_value());
}

TEST_CASE("missing_date.eml has no Date header", "[fixtures]") {
    auto raw = read_fixture("missing_date.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    CHECK_FALSE(h.date().has_value());
}

TEST_CASE("invalid_date.eml has a Date header that fails calendar validation",
          "[fixtures]") {
    auto raw = read_fixture("invalid_date.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    REQUIRE(h.date().has_value());
    // "30 Feb" is structurally date-shaped but calendar-invalid.
    CHECK_FALSE(parse_rfc5322_date(*h.date()).has_value());
}

TEST_CASE("multiple_received.eml has exactly three Received headers in file order",
          "[fixtures]") {
    auto raw = read_fixture("multiple_received.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    auto received = h.received();
    REQUIRE(received.size() == 3);
    CHECK(received[0].find("mx1.example.invalid") != std::string::npos);
    CHECK(received[1].find("mx2.example.invalid") != std::string::npos);
    CHECK(received[2].find("mx3.example.invalid") != std::string::npos);
}

TEST_CASE("x_unsent.eml is flagged as unsent", "[fixtures]") {
    auto raw = read_fixture("x_unsent.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    CHECK(h.x_unsent());
}

TEST_CASE("malformed_line_endings.eml mixes bare LF and bare CR but still yields a body",
          "[fixtures]") {
    auto raw = read_fixture("malformed_line_endings.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK(h.malformed);
    CHECK(has_reason(h, "bare-cr"));
    REQUIRE(h.date().has_value());
    CHECK(parse_rfc5322_date(*h.date()).has_value());
    CHECK(raw.substr(h.header_bytes).find("Body text present after mixed line endings.") == 0);
}

TEST_CASE("header_nul.eml has a NUL byte inside a header value", "[fixtures]") {
    auto raw = read_fixture("header_nul.eml");
    CHECK(raw.find('\0') != std::string::npos);

    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK(h.malformed);
    CHECK(has_reason(h, "nul-in-header"));
    REQUIRE(h.date().has_value());
    // The NUL byte is dropped in place (no space inserted).
    auto subject = h.first_value("Subject");
    REQUIRE(subject.has_value());
    CHECK(*subject == "HelloWorld NUL fixture");
}

TEST_CASE("missing_separator.eml never finds a header/body separator", "[fixtures]") {
    auto raw = read_fixture("missing_separator.eml");
    auto h = inspect_headers(raw);
    CHECK_FALSE(h.separator_found);
    CHECK(h.header_bytes == raw.size());
    CHECK(h.malformed);
    CHECK(has_reason(h, "line-without-colon"));
    REQUIRE(h.fields.size() == 4);
    CHECK(h.fields[0].name == "From");
    CHECK(h.fields[1].name == "Subject");
    CHECK(h.fields[2].name == "Date");
    CHECK(h.fields[3].name == "Message-ID");
}

TEST_CASE("broken_mime_boundary.eml has well-formed top-level headers", "[fixtures]") {
    auto raw = read_fixture("broken_mime_boundary.eml");
    auto h = inspect_headers(raw);
    REQUIRE(h.separator_found);
    CHECK_FALSE(h.malformed);
    REQUIRE(h.date().has_value());
    auto ct = h.first_value("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(ct->find("boundary=\"fixture-boundary-024\"") != std::string::npos);
    // The closing boundary delimiter is deliberately absent from the body.
    std::string body = raw.substr(h.header_bytes);
    CHECK(body.find("--fixture-boundary-024--") == std::string::npos);
}

TEST_CASE("zero_byte.eml yields an entirely empty inspection", "[fixtures]") {
    auto raw = read_fixture("zero_byte.eml");
    CHECK(raw.empty());
    auto h = inspect_headers(raw);
    CHECK_FALSE(h.separator_found);
    CHECK(h.fields.empty());
    CHECK(h.header_bytes == 0);
    CHECK_FALSE(h.malformed);
    CHECK(h.malformed_reasons.empty());
    CHECK_FALSE(h.date().has_value());
    CHECK_FALSE(h.x_unsent());
    CHECK(h.received().empty());
}
