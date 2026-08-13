#include "worker/reporting/progress.h"

#include <chrono>
#include <cstdio>
#include <limits>

namespace wlm2pst {

namespace {

// File-local, surrogate-correct wide -> UTF-8 conversion. Duplicated
// per-translation-unit on purpose (see docs/dev-conventions.md): this module
// must not depend on common/unicode, which is developed in parallel.
void append_utf8_codepoint(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string wide_to_utf8(const std::wstring& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp;
        if constexpr (sizeof(wchar_t) == 2) {
            uint32_t unit = static_cast<uint16_t>(s[i]);
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < s.size()) {
                uint32_t low = static_cast<uint16_t>(s[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                } else {
                    cp = 0xFFFD;
                    i += 1;
                }
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                cp = 0xFFFD;
                i += 1;
            } else {
                cp = unit;
                i += 1;
            }
        } else {
            cp = static_cast<uint32_t>(s[i]);
            i += 1;
        }
        append_utf8_codepoint(out, cp);
    }
    return out;
}

int64_t steady_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string format_percent(uint64_t processed, uint64_t total) {
    double percent = total > 0 ? (100.0 * static_cast<double>(processed) / static_cast<double>(total)) : 0.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", percent);
    return std::string(buf);
}

std::string format_rate(double rate_per_sec) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", rate_per_sec);
    return std::string(buf);
}

}  // namespace

std::string human_gib(uint64_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    const double value = static_cast<double>(bytes) / kGiB;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f GiB", value);
    return std::string(buf);
}

std::string with_thousands(uint64_t value) {
    const std::string digits = std::to_string(value);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    int since_group = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (since_group != 0 && since_group % 3 == 0) out.push_back(',');
        out.push_back(*it);
        ++since_group;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

ProgressPrinter::ProgressPrinter(bool quiet, bool verbose, std::ostream& out)
    : ProgressPrinter(quiet, verbose, out, &steady_now_ms) {}

ProgressPrinter::ProgressPrinter(bool quiet, bool verbose, std::ostream& out, std::function<int64_t()> now_ms)
    : quiet_(quiet),
      verbose_(verbose),
      out_(out),
      now_ms_(std::move(now_ms)),
      last_emit_ms_(std::numeric_limits<int64_t>::min() / 2) {}

void ProgressPrinter::print_preflight(const std::wstring& source, uint64_t files, uint64_t folders, uint64_t bytes,
                                       const std::wstring& output, const std::string& outlook_desc) {
    out_ << "Source:       " << wide_to_utf8(source) << "\n";
    out_ << "EML files:   " << with_thousands(files) << "\n";
    out_ << "Folders:     " << with_thousands(folders) << "\n";
    out_ << "Source size: " << human_gib(bytes) << "\n";
    out_ << "Output:      " << wide_to_utf8(output) << "\n";
    out_ << "Outlook:     " << outlook_desc << "\n";
}

bool ProgressPrinter::should_emit(const ProgressSnapshot& snapshot) const {
    const int64_t now = now_ms_();
    const bool time_ok = (now - last_emit_ms_) >= 500;
    const uint64_t processed_delta =
        snapshot.processed >= last_emit_processed_ ? snapshot.processed - last_emit_processed_ : snapshot.processed;
    const bool count_ok = processed_delta >= 250;
    return time_ok || count_ok;
}

void ProgressPrinter::mark_emitted(const ProgressSnapshot& snapshot) {
    last_emit_ms_ = now_ms_();
    last_emit_processed_ = snapshot.processed;
}

void ProgressPrinter::update(const ProgressSnapshot& snapshot) {
    if (quiet_) return;
    if (!should_emit(snapshot)) return;
    mark_emitted(snapshot);

    out_ << "\n[" << with_thousands(snapshot.processed) << " / " << with_thousands(snapshot.total) << "] "
         << format_percent(snapshot.processed, snapshot.total) << "%\n";
    out_ << "Folder: " << wide_to_utf8(snapshot.current_folder) << "\n";
    out_ << "Imported: " << with_thousands(snapshot.imported) << "\n";
    out_ << "Fallback: " << with_thousands(snapshot.fallback) << "\n";
    out_ << "Failed: " << with_thousands(snapshot.failed) << "\n";
    out_ << "Rate: " << format_rate(snapshot.rate_per_sec) << " messages/sec\n";
    if (verbose_) {
        out_ << "  (elapsed_ms=" << now_ms_() << ")\n";
    }
}

void ProgressPrinter::print_final_summary(const JobCounters& counters, const std::string& validation_status,
                                           const std::wstring& output, bool with_warnings) {
    out_ << (with_warnings ? "Conversion completed with warnings.\n\n" : "Conversion completed successfully.\n\n");
    out_ << "Imported normally:       " << with_thousands(counters.imported_normally) << "\n";
    out_ << "Preserved as attachment: " << with_thousands(counters.preserved_as_attachment) << "\n";
    out_ << "Failed to read:          " << with_thousands(counters.failed_to_read) << "\n";
    out_ << "Folders created:         " << with_thousands(counters.output_folders) << "\n";
    out_ << "PST validation:          " << (validation_status == "VALIDATION_FAILED" ? "FAILED" : "PASSED") << "\n";
    out_ << "\nOutput:\n" << wide_to_utf8(output) << "\n";
}

}  // namespace wlm2pst
