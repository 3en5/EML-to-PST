#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "worker/reporting/errors_csv.h"

using namespace wlm2pst;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

std::filesystem::path temp_csv_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

}  // namespace

TEST_CASE("ErrorsCsvWriter: lazy creation, no rows means no file", "[reporting][csv]") {
    const auto path = temp_csv_path("wlm2pst_test_no_rows.csv");
    std::filesystem::remove(path);
    {
        ErrorsCsvWriter writer(path.wstring());
        CHECK_FALSE(writer.created());
        // Never call append().
    }
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("ErrorsCsvWriter: first append creates the file with BOM and header", "[reporting][csv]") {
    const auto path = temp_csv_path("wlm2pst_test_bom_header.csv");
    std::filesystem::remove(path);
    {
        ErrorsCsvWriter writer(path.wstring());
        ErrorsCsvRow row;
        row.relative_source_path = L"Inbox\\001.eml";
        row.status = "FAILED_SOURCE_READ";
        row.attempt_count = 1;
        row.target_folder = L"Inbox";
        row.source_size = 100;
        REQUIRE(writer.append(row).ok());
        CHECK(writer.created());
        REQUIRE(writer.close().ok());
    }

    const std::string content = read_file(path);
    REQUIRE(content.size() >= 3);
    CHECK(static_cast<unsigned char>(content[0]) == 0xEF);
    CHECK(static_cast<unsigned char>(content[1]) == 0xBB);
    CHECK(static_cast<unsigned char>(content[2]) == 0xBF);

    const std::string expected_header =
        "relative_source_path,status,attempt_count,hresult,win32_error,target_folder,"
        "source_size,sha256,details";
    CHECK(content.substr(3, expected_header.size()) == expected_header);

    std::filesystem::remove(path);
}

TEST_CASE("ErrorsCsvWriter: hresult formatted as 0x%08X, win32 as decimal", "[reporting][csv]") {
    const auto path = temp_csv_path("wlm2pst_test_hresult.csv");
    std::filesystem::remove(path);
    {
        ErrorsCsvWriter writer(path.wstring());
        ErrorsCsvRow row;
        row.relative_source_path = L"a.eml";
        row.status = "FAILED_MAPI";
        row.attempt_count = 2;
        row.hresult = static_cast<int32_t>(0x8004010FU);  // MAPI_E_NOT_FOUND bit pattern
        row.win32_error = 5;
        row.target_folder = L"Inbox";
        row.source_size = 42;
        REQUIRE(writer.append(row).ok());
        REQUIRE(writer.close().ok());
    }
    const std::string content = read_file(path);
    CHECK(content.find("0x8004010F") != std::string::npos);
    CHECK(content.find(",5,") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("ErrorsCsvWriter: hresult and win32 are empty when zero", "[reporting][csv]") {
    const auto path = temp_csv_path("wlm2pst_test_zero_codes.csv");
    std::filesystem::remove(path);
    {
        ErrorsCsvWriter writer(path.wstring());
        ErrorsCsvRow row;
        row.relative_source_path = L"a.eml";
        row.status = "PRESERVED_AS_ATTACHMENT";
        row.attempt_count = 1;
        row.hresult = 0;
        row.win32_error = 0;
        row.target_folder = L"Inbox";
        row.source_size = 10;
        REQUIRE(writer.append(row).ok());
        REQUIRE(writer.close().ok());
    }
    const std::string content = read_file(path);
    // Row: a.eml,PRESERVED_AS_ATTACHMENT,1,,,Inbox,10,,
    CHECK(content.find("a.eml,PRESERVED_AS_ATTACHMENT,1,,,Inbox,10,,\r\n") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("ErrorsCsvWriter: RFC-4180 quoting matrix", "[reporting][csv]") {
    const auto path = temp_csv_path("wlm2pst_test_quoting.csv");
    std::filesystem::remove(path);
    {
        ErrorsCsvWriter writer(path.wstring());

        ErrorsCsvRow comma_row;
        comma_row.relative_source_path = L"folder, with comma\\a.eml";
        comma_row.status = "FAILED_SOURCE_READ";
        comma_row.target_folder = L"Inbox";
        REQUIRE(writer.append(comma_row).ok());

        ErrorsCsvRow quote_row;
        quote_row.relative_source_path = L"a.eml";
        quote_row.status = "FAILED_SOURCE_READ";
        quote_row.details = R"(said "hello")";
        REQUIRE(writer.append(quote_row).ok());

        ErrorsCsvRow newline_row;
        newline_row.relative_source_path = L"b.eml";
        newline_row.status = "FAILED_SOURCE_READ";
        newline_row.details = "line one\nline two";
        REQUIRE(writer.append(newline_row).ok());

        ErrorsCsvRow hebrew_row;
        hebrew_row.relative_source_path = L"\x05e9\x05dc\x05d5\x05dd\\c.eml";  // Hebrew folder name
        hebrew_row.status = "FAILED_SOURCE_READ";
        REQUIRE(writer.append(hebrew_row).ok());

        REQUIRE(writer.close().ok());
    }
    const std::string content = read_file(path);

    CHECK(content.find("\"folder, with comma\\a.eml\"") != std::string::npos);
    CHECK(content.find(R"("said ""hello""")") != std::string::npos);
    CHECK(content.find("\"line one\nline two\"") != std::string::npos);
    // UTF-8 bytes for the Hebrew folder name pass through unescaped.
    const std::string hebrew_utf8 = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";
    CHECK(content.find(hebrew_utf8 + "\\c.eml") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("ErrorsCsvWriter: close() before any append is a no-op success", "[reporting][csv]") {
    const auto path = temp_csv_path("wlm2pst_test_close_noop.csv");
    std::filesystem::remove(path);
    ErrorsCsvWriter writer(path.wstring());
    CHECK(writer.close().ok());
    CHECK_FALSE(std::filesystem::exists(path));
}
