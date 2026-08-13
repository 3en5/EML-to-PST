// WLM2PST - console progress and summary printing (spec section 21).
//
// Portable: no Windows headers. Never prints subjects, addresses, or other
// message content - only counts, paths, rates, and statuses.
#pragma once

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>

#include "common/model.h"

namespace wlm2pst {

struct ProgressSnapshot {
    uint64_t processed = 0;
    uint64_t total = 0;
    uint64_t imported = 0;
    uint64_t fallback = 0;
    uint64_t failed = 0;
    std::wstring current_folder;
    double rate_per_sec = 0;
};

// Prints preflight summary, rate-limited progress updates, and the final
// summary to an injected ostream.
//
// `quiet` suppresses per-message progress lines (update()) but never
// suppresses the final summary or preflight banner. `verbose` adds extra
// technical detail to progress lines; it never adds message content.
class ProgressPrinter {
public:
    ProgressPrinter(bool quiet, bool verbose, std::ostream& out);

    // Testable overload: `now_ms` supplies a monotonic millisecond clock so
    // tests can control rate-limiting deterministically without sleeping.
    ProgressPrinter(bool quiet, bool verbose, std::ostream& out, std::function<int64_t()> now_ms);

    void print_preflight(const std::wstring& source, uint64_t files, uint64_t folders, uint64_t bytes,
                          const std::wstring& output, const std::string& outlook_desc);

    // Rate-limited: emits at most once per 500ms, but always emits at least
    // once every 250 processed messages since the last emission. Suppressed
    // entirely when `quiet` was set.
    void update(const ProgressSnapshot& snapshot);

    void print_final_summary(const JobCounters& counters, const std::string& validation_status,
                              const std::wstring& output, bool with_warnings);

private:
    [[nodiscard]] bool should_emit(const ProgressSnapshot& snapshot) const;
    void mark_emitted(const ProgressSnapshot& snapshot);

    bool quiet_;
    bool verbose_;
    std::ostream& out_;
    std::function<int64_t()> now_ms_;
    int64_t last_emit_ms_;
    uint64_t last_emit_processed_ = 0;
};

// Formats bytes as e.g. "13.4 GiB" (binary GiB, one decimal place).
std::string human_gib(uint64_t bytes);

// Formats an integer with thousand separators, e.g. 18742 -> "18,742".
std::string with_thousands(uint64_t value);

}  // namespace wlm2pst
