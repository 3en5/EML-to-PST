#include <cstdint>
#include <functional>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "common/model.h"
#include "worker/reporting/progress.h"

using namespace wlm2pst;

namespace {

size_t count_occurrences(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// Deterministic, manually-advanced clock for rate-limit tests.
class FakeClock {
public:
    int64_t operator()() const { return now_ms_; }
    void advance(int64_t ms) { now_ms_ += ms; }

private:
    int64_t now_ms_ = 0;
};

}  // namespace

TEST_CASE("human_gib: zero bytes", "[reporting][progress]") { CHECK(human_gib(0) == "0.0 GiB"); }

TEST_CASE("human_gib: sub-GiB byte counts round to 0.0 GiB", "[reporting][progress]") {
    CHECK(human_gib(1023) == "0.0 GiB");
}

TEST_CASE("human_gib: exactly 1 GiB", "[reporting][progress]") {
    CHECK(human_gib(1073741824ULL) == "1.0 GiB");
}

TEST_CASE("human_gib: 13.4 GiB case from spec example", "[reporting][progress]") {
    CHECK(human_gib(14388140442ULL) == "13.4 GiB");
}

TEST_CASE("with_thousands: thousand separators", "[reporting][progress]") {
    CHECK(with_thousands(0) == "0");
    CHECK(with_thousands(999) == "999");
    CHECK(with_thousands(1000) == "1,000");
    CHECK(with_thousands(18742) == "18,742");
    CHECK(with_thousands(1234567) == "1,234,567");
}

TEST_CASE("ProgressPrinter: percent math matches spec example", "[reporting][progress]") {
    std::ostringstream out;
    FakeClock clock;
    ProgressPrinter printer(/*quiet=*/false, /*verbose=*/false, out, std::ref(clock));

    ProgressSnapshot snapshot;
    snapshot.processed = 1244;
    snapshot.total = 18742;
    snapshot.imported = 1242;
    snapshot.fallback = 1;
    snapshot.failed = 1;
    snapshot.current_folder = L"account@example.com\\Inbox";
    snapshot.rate_per_sec = 8.1;
    printer.update(snapshot);

    const std::string text = out.str();
    CHECK(text.find("[1,244 / 18,742] 6.6%") != std::string::npos);
    CHECK(text.find("Imported: 1,242") != std::string::npos);
    CHECK(text.find("Fallback: 1") != std::string::npos);
    CHECK(text.find("Failed: 1") != std::string::npos);
    CHECK(text.find("Rate: 8.1 messages/sec") != std::string::npos);
}

TEST_CASE("ProgressPrinter: percent math with zero total does not divide by zero", "[reporting][progress]") {
    std::ostringstream out;
    FakeClock clock;
    ProgressPrinter printer(false, false, out, std::ref(clock));

    ProgressSnapshot snapshot;
    snapshot.processed = 0;
    snapshot.total = 0;
    printer.update(snapshot);

    CHECK(out.str().find("[0 / 0] 0.0%") != std::string::npos);
}

TEST_CASE("ProgressPrinter: rate limit honored - at most one emission per 250 messages without elapsed time",
          "[reporting][progress]") {
    std::ostringstream out;
    FakeClock clock;
    ProgressPrinter printer(false, false, out, std::ref(clock));

    for (uint64_t i = 1; i <= 300; ++i) {
        ProgressSnapshot snapshot;
        snapshot.processed = i;
        snapshot.total = 300;
        printer.update(snapshot);
    }

    // First call (processed=1) always emits; the next emission happens once
    // the processed delta since the last emission reaches 250, i.e. at
    // processed=251. No further emission occurs before processed=300 since
    // the clock never advances.
    CHECK(count_occurrences(out.str(), "Rate:") == 2);
}

TEST_CASE("ProgressPrinter: rate limit honored - emits again once 500ms elapse", "[reporting][progress]") {
    std::ostringstream out;
    FakeClock clock;
    ProgressPrinter printer(false, false, out, std::ref(clock));

    ProgressSnapshot snapshot;
    snapshot.processed = 1;
    snapshot.total = 1000;
    printer.update(snapshot);  // first call always emits
    CHECK(count_occurrences(out.str(), "Rate:") == 1);

    snapshot.processed = 2;  // far short of the 250-message threshold
    printer.update(snapshot);
    CHECK(count_occurrences(out.str(), "Rate:") == 1);  // still rate-limited

    clock.advance(499);
    printer.update(snapshot);
    CHECK(count_occurrences(out.str(), "Rate:") == 1);  // still under 500ms

    clock.advance(1);  // now exactly 500ms since the last emission
    printer.update(snapshot);
    CHECK(count_occurrences(out.str(), "Rate:") == 2);
}

TEST_CASE("ProgressPrinter: quiet suppresses progress but not the final summary", "[reporting][progress]") {
    std::ostringstream out;
    FakeClock clock;
    ProgressPrinter printer(/*quiet=*/true, /*verbose=*/false, out, std::ref(clock));

    ProgressSnapshot snapshot;
    snapshot.processed = 1;
    snapshot.total = 10;
    printer.update(snapshot);
    CHECK(out.str().empty());

    JobCounters counters;
    counters.imported_normally = 10;
    printer.print_final_summary(counters, "VALIDATED", L"C:\\Out.pst", /*with_warnings=*/false);
    CHECK_FALSE(out.str().empty());
    CHECK(out.str().find("Conversion completed successfully.") != std::string::npos);
}

TEST_CASE("ProgressPrinter: final summary reports validation FAILED for VALIDATION_FAILED", "[reporting][progress]") {
    std::ostringstream out;
    FakeClock clock;
    ProgressPrinter printer(false, false, out, std::ref(clock));

    JobCounters counters;
    printer.print_final_summary(counters, "VALIDATION_FAILED", L"C:\\Out.pst", /*with_warnings=*/true);
    CHECK(out.str().find("PST validation:          FAILED") != std::string::npos);
    CHECK(out.str().find("Conversion completed with warnings.") != std::string::npos);
}

TEST_CASE("ProgressPrinter: preflight banner includes source, counts, output, outlook bitness",
          "[reporting][progress]") {
    std::ostringstream out;
    FakeClock clock;
    ProgressPrinter printer(false, false, out, std::ref(clock));

    printer.print_preflight(L"C:\\Mail\\Windows Live Mail", 18742, 137, 14388140442ULL, L"D:\\Migration\\Rina-Mail.pst",
                             "Classic Outlook 64-bit");

    const std::string text = out.str();
    CHECK(text.find("C:\\Mail\\Windows Live Mail") != std::string::npos);
    CHECK(text.find("18,742") != std::string::npos);
    CHECK(text.find("137") != std::string::npos);
    CHECK(text.find("13.4 GiB") != std::string::npos);
    CHECK(text.find("D:\\Migration\\Rina-Mail.pst") != std::string::npos);
    CHECK(text.find("Classic Outlook 64-bit") != std::string::npos);
}
