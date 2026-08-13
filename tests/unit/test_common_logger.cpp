#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "common/logging/logger.h"
#include "common/unicode/utf.h"

using wlm2pst::LogLevel;
using wlm2pst::Logger;

namespace {

std::wstring temp_log_path(const char* name) {
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::filesystem::path file = dir / name;
    std::filesystem::remove(file);
    return wlm2pst::wide_from_utf8(file.string());
}

std::string read_file_utf8(const std::wstring& wide_path) {
    std::filesystem::path p(wlm2pst::utf8_from_wide(wide_path));
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("Logger writes UTF-8 lines with a UTC timestamp and level tag", "[logger]") {
    std::wstring path = temp_log_path("wlm2pst_logger_test_basic.log");

    Logger logger;
    logger.set_level(LogLevel::kVerbose);
    logger.set_log_file(path);
    logger.error("disk full: 0 bytes free");
    logger.clear_log_file();

    std::string contents = read_file_utf8(path);
    CAPTURE(contents);
    REQUIRE(contents.find("[ERROR] disk full: 0 bytes free") != std::string::npos);
    // ISO-8601 UTC timestamp: YYYY-MM-DDTHH:MM:SSZ
    REQUIRE(contents.size() >= 20);
    CHECK(contents[4] == '-');
    CHECK(contents[7] == '-');
    CHECK(contents[10] == 'T');
    CHECK(contents[13] == ':');
    CHECK(contents[16] == ':');
    CHECK(contents[19] == 'Z');

    std::filesystem::remove(std::filesystem::path(wlm2pst::utf8_from_wide(path)));
}

TEST_CASE("Logger filters by configured minimum level", "[logger]") {
    std::wstring path = temp_log_path("wlm2pst_logger_test_filter.log");

    Logger logger;
    logger.set_level(LogLevel::kWarn);
    logger.set_log_file(path);
    logger.error("err-line");
    logger.warn("warn-line");
    logger.info("info-line");     // below threshold: must not appear
    logger.verbose("verbose-line");  // below threshold: must not appear
    logger.clear_log_file();

    std::string contents = read_file_utf8(path);
    CHECK(contents.find("err-line") != std::string::npos);
    CHECK(contents.find("warn-line") != std::string::npos);
    CHECK(contents.find("info-line") == std::string::npos);
    CHECK(contents.find("verbose-line") == std::string::npos);

    std::filesystem::remove(std::filesystem::path(wlm2pst::utf8_from_wide(path)));
}

TEST_CASE("Logger writes every message at kVerbose", "[logger]") {
    std::wstring path = temp_log_path("wlm2pst_logger_test_verbose.log");

    Logger logger;
    logger.set_level(LogLevel::kVerbose);
    logger.set_log_file(path);
    logger.error("e");
    logger.warn("w");
    logger.info("i");
    logger.verbose("v");
    logger.clear_log_file();

    std::string contents = read_file_utf8(path);
    CHECK(contents.find("[ERROR] e") != std::string::npos);
    CHECK(contents.find("[WARN] w") != std::string::npos);
    CHECK(contents.find("[INFO] i") != std::string::npos);
    CHECK(contents.find("[VERBOSE] v") != std::string::npos);

    std::filesystem::remove(std::filesystem::path(wlm2pst::utf8_from_wide(path)));
}

TEST_CASE("global_logger is a process-wide singleton", "[logger]") {
    Logger& a = wlm2pst::global_logger();
    Logger& b = wlm2pst::global_logger();
    CHECK(&a == &b);

    // Restore a known level so this test doesn't leak state into others.
    wlm2pst::set_global_log_level(LogLevel::kInfo);
    CHECK(a.level() == LogLevel::kInfo);
}
