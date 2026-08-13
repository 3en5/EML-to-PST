#include "common/logging/logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iostream>

#include "common/unicode/utf.h"

namespace wlm2pst {

namespace {

// Treats the wide path as UTF-8 bytes for std::filesystem::path on
// platforms where that is the native narrow encoding (everywhere but
// Windows, where wchar_t already matches path::value_type and no
// conversion is needed at all). Avoids std::filesystem's locale-dependent
// wide-to-native conversion path.
std::filesystem::path native_path_from_wide(const std::wstring& path) {
    if constexpr (sizeof(wchar_t) == 2) {
        return std::filesystem::path(path);
    } else {
        return std::filesystem::path(utf8_from_wide(path));
    }
}

const char* level_tag(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::kError: return "ERROR";
        case LogLevel::kWarn: return "WARN";
        case LogLevel::kInfo: return "INFO";
        case LogLevel::kVerbose: return "VERBOSE";
    }
    return "UNKNOWN";
}

std::string utc_timestamp() {
    std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &tt);
#else
    gmtime_r(&tt, &tm_utc);
#endif
    // tm_year + 1900 is an int and could in principle need more than four
    // digits; size generously so gcc's -Wformat-truncation cannot fire, then
    // clamp to the conventional ISO-8601 length below.
    char buf[64];
    int written = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                                 tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                                 tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    if (written < 0) {
        return std::string("1970-01-01T00:00:00Z");
    }
    return std::string(buf, static_cast<size_t>(written));
}

}  // namespace

Logger::Logger() = default;

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_ && file_stream_->is_open()) {
        file_stream_->flush();
    }
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::set_log_file(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_stream_.emplace();
    file_stream_->open(native_path_from_wide(path), std::ios::app | std::ios::binary);
    if (!file_stream_->is_open()) {
        std::cerr << "wlm2pst: failed to open log file\n";
        file_stream_.reset();
    }
}

void Logger::clear_log_file() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_) {
        file_stream_->flush();
    }
    file_stream_.reset();
}

void Logger::error(std::string_view utf8_message) { write(LogLevel::kError, utf8_message); }
void Logger::warn(std::string_view utf8_message) { write(LogLevel::kWarn, utf8_message); }
void Logger::info(std::string_view utf8_message) { write(LogLevel::kInfo, utf8_message); }
void Logger::verbose(std::string_view utf8_message) { write(LogLevel::kVerbose, utf8_message); }

void Logger::write(LogLevel level, std::string_view utf8_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) > static_cast<int>(level_)) {
        return;
    }

    std::string line;
    line.reserve(utf8_message.size() + 32);
    line += utc_timestamp();
    line += " [";
    line += level_tag(level);
    line += "] ";
    line += utf8_message;
    line += "\n";

    if (level == LogLevel::kError || level == LogLevel::kWarn) {
        std::cerr << line;
    } else {
        std::cout << line;
    }

    if (file_stream_ && file_stream_->is_open()) {
        (*file_stream_) << line;
        if (level == LogLevel::kError) {
            file_stream_->flush();
        }
    }
}

Logger& global_logger() {
    static Logger instance;
    return instance;
}

void set_global_log_file(const std::wstring& path) { global_logger().set_log_file(path); }

void set_global_log_level(LogLevel level) { global_logger().set_level(level); }

}  // namespace wlm2pst
