// WLM2PST - privacy-safe logging (spec section 21).
//
// PRIVACY CONTRACT: callers must NEVER pass a message subject, body, HTML,
// sender/recipient address, raw MIME header, or attachment content to any
// method on this class, at ANY log level, including kVerbose. Only
// operationally-safe fields belong in a message: relative source paths,
// target folder paths, sizes, SHA-256 hex digests, statuses, attempt
// counts, HRESULT/Win32 codes, timings, counts, and bitness. This class
// performs no content redaction of its own - it trusts every call site to
// uphold this contract, so review new call sites against spec section 21
// before adding them.
#pragma once

#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace wlm2pst {

enum class LogLevel { kError, kWarn, kInfo, kVerbose };

class Logger {
public:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Minimum level emitted to both console and log file. Default: kInfo.
    void set_level(LogLevel level);
    LogLevel level() const;

    // Opens (or creates) a UTF-8 log file in append mode. On failure, a
    // notice is written to stderr and no file is attached (subsequent log
    // calls simply skip the file sink).
    void set_log_file(const std::wstring& path);

    // Flushes and detaches the current log file, if any.
    void clear_log_file();

    // utf8_message must already be privacy-safe UTF-8 text; see the class
    // comment above.
    void error(std::string_view utf8_message);
    void warn(std::string_view utf8_message);
    void info(std::string_view utf8_message);
    void verbose(std::string_view utf8_message);

private:
    void write(LogLevel level, std::string_view utf8_message);

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::kInfo;
    std::optional<std::ofstream> file_stream_;
};

// Process-wide logger. Modules that don't want to thread a Logger& through
// their call chain use this; it is safe to call from multiple threads.
Logger& global_logger();
void set_global_log_file(const std::wstring& path);
void set_global_log_level(LogLevel level);

}  // namespace wlm2pst
