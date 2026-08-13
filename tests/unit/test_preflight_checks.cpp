// WLM2PST - unit tests for worker/preflight (spec sections 6, 28).
#include "worker/preflight/preflight.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace wlm2pst;

namespace {

// Settable fake ISystemProbe: every decision the preflight logic needs is a
// plain field, so each test can flip exactly the one knob it is exercising.
struct FakeSystemProbe : ISystemProbe {
    std::set<std::wstring> existing_dirs;
    std::set<std::wstring> existing_files;
    uint64_t free_bytes = UINT64_MAX;
    bool local_fixed_drive = true;
    bool local_fixed_drive_query_fails = false;
    bool wlmail_running = false;
    bool output_locked = false;
    bool lock_query_fails = false;

    bool directory_exists(const std::wstring& path) override {
        return existing_dirs.count(path) != 0;
    }
    bool file_exists(const std::wstring& path) override {
        return existing_files.count(path) != 0;
    }
    Result<uint64_t> available_free_bytes(const std::wstring& /*directory*/) override {
        return free_bytes;
    }
    Result<bool> is_on_local_fixed_drive(const std::wstring& /*path*/) override {
        if (local_fixed_drive_query_fails) {
            return make_error("probe", "simulated failure");
        }
        return local_fixed_drive;
    }
    bool is_process_running(const std::wstring& /*exe_name_lower*/) override {
        return wlmail_running;
    }
    Result<bool> is_file_locked(const std::wstring& /*path*/) override {
        if (lock_query_fails) {
            return make_error("probe", "simulated failure");
        }
        return output_locked;
    }
};

constexpr uint64_t kOneGiB = 1024ull * 1024ull * 1024ull;

ScanResult make_scan(uint64_t file_count, uint64_t total_bytes, uint64_t folder_count) {
    ScanResult scan;
    for (uint64_t i = 0; i < file_count; ++i) {
        ManifestEntry entry;
        entry.relative_path = L"msg" + std::to_wstring(i) + L".eml";
        entry.absolute_path = L"C:\\Mail\\" + entry.relative_path;
        entry.size = file_count > 0 ? total_bytes / file_count : 0;
        scan.entries.push_back(entry);
    }
    scan.stats.total_bytes = total_bytes;
    scan.stats.folder_count = folder_count;
    return scan;
}

// A baseline setup where every check passes; individual tests copy this and
// flip exactly one thing.
struct PreflightFixture {
    CliOptions options;
    ScanResult scan;
    FakeSystemProbe probe;

    PreflightFixture() {
        options.source = L"C:\\Mail";
        options.output = L"D:\\Migration\\out.pst";
        options.root_name = L"Windows Live Mail";

        scan = make_scan(3, 10 * kOneGiB, 2);  // estimate = 14 GiB, required = 16 GiB

        probe.existing_dirs = {L"C:\\Mail", L"D:\\Migration"};
        probe.free_bytes = 20 * kOneGiB;  // comfortably above the 16 GiB requirement
        probe.local_fixed_drive = true;
        probe.wlmail_running = false;
    }
};

}  // namespace

TEST_CASE("run_preflight_checks passes when every check succeeds", "[preflight]") {
    PreflightFixture fx;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("run_preflight_checks fails when the source directory does not exist", "[preflight]") {
    PreflightFixture fx;
    fx.probe.existing_dirs.erase(L"C:\\Mail");
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidSource);
}

TEST_CASE("run_preflight_checks fails when the scan found no EML files", "[preflight]") {
    PreflightFixture fx;
    fx.scan = make_scan(0, 0, 0);
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidSource);
}

TEST_CASE("run_preflight_checks fails when Windows Live Mail is running", "[preflight]") {
    PreflightFixture fx;
    fx.probe.wlmail_running = true;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidSource);
}

TEST_CASE("run_preflight_checks fails when the output directory does not exist", "[preflight]") {
    PreflightFixture fx;
    fx.probe.existing_dirs.erase(L"D:\\Migration");
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

TEST_CASE("run_preflight_checks fails when the output is not on a local fixed drive", "[preflight]") {
    PreflightFixture fx;
    fx.probe.local_fixed_drive = false;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

TEST_CASE("run_preflight_checks fails when the local-fixed-drive query itself fails", "[preflight]") {
    PreflightFixture fx;
    fx.probe.local_fixed_drive_query_fails = true;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

TEST_CASE("run_preflight_checks fails when output exists without --overwrite or --resume",
          "[preflight]") {
    PreflightFixture fx;
    fx.probe.existing_files.insert(L"D:\\Migration\\out.pst");
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kOutputExists);
}

TEST_CASE("run_preflight_checks passes when output exists and --overwrite is given and unlocked",
          "[preflight]") {
    PreflightFixture fx;
    fx.probe.existing_files.insert(L"D:\\Migration\\out.pst");
    fx.options.overwrite = true;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("run_preflight_checks passes when output exists and --resume is given and unlocked",
          "[preflight]") {
    PreflightFixture fx;
    fx.probe.existing_files.insert(L"D:\\Migration\\out.pst");
    fx.options.resume = true;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("run_preflight_checks fails when the existing output file is locked (--overwrite)",
          "[preflight]") {
    PreflightFixture fx;
    fx.probe.existing_files.insert(L"D:\\Migration\\out.pst");
    fx.options.overwrite = true;
    fx.probe.output_locked = true;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kOutputExists);
}

TEST_CASE("run_preflight_checks fails when the existing output file is locked (--resume)",
          "[preflight]") {
    PreflightFixture fx;
    fx.probe.existing_files.insert(L"D:\\Migration\\out.pst");
    fx.options.resume = true;
    fx.probe.output_locked = true;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kOutputExists);
}

TEST_CASE("run_preflight_checks fails when insufficient free disk space is available",
          "[preflight]") {
    PreflightFixture fx;
    // required = 16 GiB exactly; one byte short must fail.
    fx.probe.free_bytes = 16 * kOneGiB - 1;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInsufficientDiskSpace);
}

TEST_CASE("run_preflight_checks passes when free disk space exactly meets the requirement",
          "[preflight]") {
    PreflightFixture fx;
    fx.probe.free_bytes = 16 * kOneGiB;  // == required, not below it
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("run_preflight_checks reports both required and available bytes on failure",
          "[preflight]") {
    PreflightFixture fx;
    fx.probe.free_bytes = 1 * kOneGiB;
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->message_utf8.find(std::to_string(16 * kOneGiB)) != std::string::npos);
    CHECK(result->message_utf8.find(std::to_string(1 * kOneGiB)) != std::string::npos);
}

TEST_CASE("run_preflight_checks fails when the estimated PST size exceeds the safe ceiling",
          "[preflight]") {
    PreflightFixture fx;
    // A source large enough that total*1.30 + 1 GiB clears 45 GiB by a wide
    // margin, so there is no doubt which side of the ceiling it lands on.
    fx.scan = make_scan(1, 100 * kOneGiB, 1);
    fx.probe.free_bytes = UINT64_MAX;  // free space must not be the blocker here
    auto result = run_preflight_checks(fx.options, fx.scan, fx.probe);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kPstSizeCeilingExceeded);
}

// --- Free-space arithmetic: exact integer math (spec section 6) ------------

TEST_CASE("estimate_pst_bytes applies the 1.30 factor plus 1 GiB using exact integer math",
          "[preflight]") {
    CHECK(estimate_pst_bytes(10 * kOneGiB) == 14 * kOneGiB);
    CHECK(estimate_pst_bytes(0) == 1 * kOneGiB);
    // 3/10 uses floor division: 7 bytes * 3 / 10 == 2, not 2.1.
    CHECK(estimate_pst_bytes(7) == 7 + 2 + kOneGiB);
}

TEST_CASE("required_free_bytes_for adds exactly 2 GiB to the estimate", "[preflight]") {
    CHECK(required_free_bytes_for(14 * kOneGiB) == 16 * kOneGiB);
    CHECK(required_free_bytes_for(0) == 2 * kOneGiB);
}

// --- Safe single-PST size ceiling: exact 45 GiB boundary --------------------

TEST_CASE("exceeds_pst_size_ceiling passes at exactly 45 GiB and fails one byte over",
          "[preflight]") {
    constexpr uint64_t k45GiB = 45ull * kOneGiB;
    CHECK_FALSE(exceeds_pst_size_ceiling(k45GiB));
    CHECK(exceeds_pst_size_ceiling(k45GiB + 1));
}

// --- build_preflight_report ------------------------------------------------

TEST_CASE("build_preflight_report summarizes the scan and free-space arithmetic", "[preflight]") {
    PreflightFixture fx;
    PreflightReport report = build_preflight_report(fx.options, fx.scan, fx.probe.free_bytes);
    CHECK(report.eml_count == 3);
    CHECK(report.folder_count == 2);
    CHECK(report.total_bytes == 10 * kOneGiB);
    CHECK(report.estimated_pst_bytes == 14 * kOneGiB);
    CHECK(report.required_free_bytes == 16 * kOneGiB);
    CHECK(report.available_free_bytes == 20 * kOneGiB);
}
