#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "common/model.h"
#include "worker/reporting/json_writer.h"
#include "worker/reporting/report.h"

using namespace wlm2pst;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

}  // namespace

TEST_CASE("JsonValue escapes quotes and backslashes", "[reporting][json]") {
    JsonValue obj = JsonValue::object();
    obj.set("field", std::string(R"(a "quoted" \ value)"));
    CHECK(to_json(obj, 0) == R"({"field":"a \"quoted\" \\ value"})");
}

TEST_CASE("JsonValue escapes control characters, keeps \\n \\r \\t as shortcuts", "[reporting][json]") {
    JsonValue obj = JsonValue::object();
    std::string s;
    s += '\x01';
    s += '\n';
    s += '\r';
    s += '\t';
    s += '\x1f';
    obj.set("field", s);
    CHECK(to_json(obj, 0) == R"({"field":"\u0001\n\r\t\u001f"})");
}

TEST_CASE("JsonValue passes non-ASCII UTF-8 through unescaped (Hebrew)", "[reporting][json]") {
    JsonValue obj = JsonValue::object();
    const std::string hebrew = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";  // "שלום" (UTF-8 bytes)
    obj.set("field", hebrew);
    CHECK(to_json(obj, 0) == "{\"field\":\"" + hebrew + "\"}");
}

TEST_CASE("JsonValue passes non-ASCII UTF-8 through unescaped (emoji)", "[reporting][json]") {
    JsonValue obj = JsonValue::object();
    const std::string emoji = "\xf0\x9f\x98\x80";  // U+1F600 GRINNING FACE, UTF-8 bytes
    obj.set("field", emoji);
    CHECK(to_json(obj, 0) == "{\"field\":\"" + emoji + "\"}");
}

TEST_CASE("JsonValue emits uint64 above INT64_MAX correctly", "[reporting][json]") {
    JsonValue obj = JsonValue::object();
    const uint64_t big = 18446744073709551615ULL;  // UINT64_MAX
    obj.set("field", big);
    CHECK(to_json(obj, 0) == R"({"field":18446744073709551615})");
}

TEST_CASE("JsonValue emits plain-integer int64 with no scientific notation", "[reporting][json]") {
    JsonValue obj = JsonValue::object();
    obj.set("small", 42);
    obj.set("negative", static_cast<int64_t>(-7));
    obj.set("zero", static_cast<int64_t>(0));
    CHECK(to_json(obj, 0) == R"({"small":42,"negative":-7,"zero":0})");
}

TEST_CASE("JsonValue serializes nested arrays and objects, compact", "[reporting][json]") {
    JsonValue root = JsonValue::object();
    JsonValue arr = JsonValue::array();
    JsonValue item1 = JsonValue::object();
    item1.set("a", 1);
    item1.set("b", std::string("x"));
    arr.push(std::move(item1));
    arr.push(JsonValue(2));
    root.set("items", std::move(arr));
    root.set("empty_array", JsonValue::array());
    root.set("empty_object", JsonValue::object());

    CHECK(to_json(root, 0) ==
          R"({"items":[{"a":1,"b":"x"},2],"empty_array":[],"empty_object":{}})");
}

TEST_CASE("JsonValue serializes nested arrays and objects, pretty (indent=2)", "[reporting][json]") {
    JsonValue root = JsonValue::object();
    JsonValue arr = JsonValue::array();
    JsonValue item1 = JsonValue::object();
    item1.set("a", 1);
    arr.push(std::move(item1));
    root.set("items", std::move(arr));

    const std::string expected = R"({
  "items": [
    {
      "a": 1
    }
  ]
})";
    CHECK(to_json(root, 2) == expected);
}

TEST_CASE("JsonValue::set replaces an existing key in place, preserving order", "[reporting][json]") {
    JsonValue obj = JsonValue::object();
    obj.set("a", 1);
    obj.set("b", 2);
    obj.set("a", 3);
    CHECK(to_json(obj, 0) == R"({"a":3,"b":2})");
}

TEST_CASE("iso8601_from_filetime: epoch", "[reporting][json]") {
    CHECK(iso8601_from_filetime(kFiletimeUnixEpoch) == "1970-01-01T00:00:00Z");
}

TEST_CASE("iso8601_from_filetime: known value 2026-08-13T08:12:44Z", "[reporting][json]") {
    CHECK(iso8601_from_filetime(134310823640000000LL) == "2026-08-13T08:12:44Z");
}

TEST_CASE("iso8601_from_filetime: truncates sub-second precision", "[reporting][json]") {
    // Epoch plus 4,999,999 ticks (< 1 tick short of half a second) must still
    // truncate down to the whole second.
    CHECK(iso8601_from_filetime(kFiletimeUnixEpoch + 4'999'999) == "1970-01-01T00:00:00Z");
    CHECK(iso8601_from_filetime(kFiletimeUnixEpoch + kFiletimeTicksPerSecond - 1) == "1970-01-01T00:00:00Z");
    CHECK(iso8601_from_filetime(kFiletimeUnixEpoch + kFiletimeTicksPerSecond) == "1970-01-01T00:00:01Z");
}

TEST_CASE("write_json_report: golden full-report comparison", "[reporting][json]") {
    ReportData data;
    data.tool_version = "1.0.0";
    data.run_id = "20260813-081244-abcd1234";
    data.source = L"C:\\Mail\\Windows Live Mail";
    data.output = L"D:\\Migration\\Sample-Mail.pst";
    data.started_at_utc_iso = "2026-08-13T08:12:44Z";
    data.completed_at_utc_iso = "2026-08-13T09:03:18Z";
    data.counters.source_files = 18742;
    data.counters.source_folders = 137;
    data.counters.source_bytes = 14388140442ULL;
    data.counters.imported_normally = 18724;
    data.counters.preserved_as_attachment = 15;
    data.counters.failed_to_read = 3;
    data.counters.output_folders = 137;
    data.validation_status = to_string(ValidationStatus::kValidatedWithFallbacks);
    data.renamed_folders.push_back(
        RenamedFolder{L"Sent Items\\Sub:Folder", L"Sent Items\\Sub_Folder", "invalid-char"});
    data.warnings = {"Some warning message", "Another warning"};

    const std::filesystem::path out_path =
        std::filesystem::temp_directory_path() / "wlm2pst_test_golden_report.json";
    const Status status = write_json_report(out_path.wstring(), data);
    REQUIRE(status.ok());

    const std::string actual = read_file(out_path);
    std::filesystem::remove(out_path);

    const std::string expected = R"({
  "toolVersion": "1.0.0",
  "runId": "20260813-081244-abcd1234",
  "source": "C:\\Mail\\Windows Live Mail",
  "output": "D:\\Migration\\Sample-Mail.pst",
  "startedAtUtc": "2026-08-13T08:12:44Z",
  "completedAtUtc": "2026-08-13T09:03:18Z",
  "sourceFiles": 18742,
  "sourceFolders": 137,
  "sourceBytes": 14388140442,
  "importedNormally": 18724,
  "preservedAsAttachment": 15,
  "failedToRead": 3,
  "outputFolders": 137,
  "validationStatus": "VALIDATED_WITH_FALLBACKS",
  "renamedFolders": [
    {
      "sourceRelativeFolder": "Sent Items\\Sub:Folder",
      "targetRelativeFolder": "Sent Items\\Sub_Folder",
      "reason": "invalid-char"
    }
  ],
  "warnings": [
    "Some warning message",
    "Another warning"
  ]
}
)";
    CHECK(actual == expected);
}

TEST_CASE("write_json_report: empty renamedFolders and warnings serialize as []", "[reporting][json]") {
    ReportData data;
    data.tool_version = "1.0.0";
    data.run_id = "run-1";
    data.source = L"C:\\Src";
    data.output = L"C:\\Out.pst";
    data.started_at_utc_iso = "2026-01-01T00:00:00Z";
    data.completed_at_utc_iso = "2026-01-01T00:00:01Z";
    data.validation_status = to_string(ValidationStatus::kValidated);

    const std::filesystem::path out_path =
        std::filesystem::temp_directory_path() / "wlm2pst_test_empty_report.json";
    const Status status = write_json_report(out_path.wstring(), data);
    REQUIRE(status.ok());

    const std::string actual = read_file(out_path);
    std::filesystem::remove(out_path);

    CHECK(actual.find("\"renamedFolders\": []") != std::string::npos);
    CHECK(actual.find("\"warnings\": []") != std::string::npos);
    // No BOM, ends with exactly one trailing newline.
    CHECK(actual.substr(0, 1) == "{");
    REQUIRE(actual.size() >= 2);
    CHECK(actual.back() == '\n');
    CHECK(actual[actual.size() - 2] == '}');
}
