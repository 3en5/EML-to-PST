#include <catch2/catch_test_macros.hpp>

#include "launcher/outlook_detector.h"
#include "launcher/worker_launcher.h"

using wlm2pst::OutlookBitness;
using wlm2pst::command_tail;
using wlm2pst::worker_exe_name;

TEST_CASE("worker_exe_name maps x86", "[launcher][worker_select]") {
    REQUIRE(worker_exe_name(OutlookBitness::kX86) == L"wlm2pst-worker-x86.exe");
}

TEST_CASE("worker_exe_name maps x64", "[launcher][worker_select]") {
    REQUIRE(worker_exe_name(OutlookBitness::kX64) == L"wlm2pst-worker-x64.exe");
}

TEST_CASE("worker_exe_name returns empty for unknown bitness", "[launcher][worker_select]") {
    REQUIRE(worker_exe_name(OutlookBitness::kUnknown).empty());
}

TEST_CASE("command_tail skips a bare argv[0] token", "[launcher][worker_select]") {
    REQUIRE(command_tail(L"C:\\Tools\\wlm2pst.exe --source C:\\Mail --output D:\\out.pst") ==
            L"--source C:\\Mail --output D:\\out.pst");
}

TEST_CASE("command_tail skips a quoted argv[0] token containing spaces", "[launcher][worker_select]") {
    REQUIRE(command_tail(L"\"C:\\Program Files\\WLM2PST\\wlm2pst.exe\" --source C:\\Mail") ==
            L"--source C:\\Mail");
}

TEST_CASE("command_tail preserves quotes within the tail", "[launcher][worker_select]") {
    REQUIRE(command_tail(L"wlm2pst.exe --source \"C:\\Old Mail\" --output \"D:\\out.pst\"") ==
            L"--source \"C:\\Old Mail\" --output \"D:\\out.pst\"");
}

TEST_CASE("command_tail returns empty when there is no tail (bare token only)",
          "[launcher][worker_select]") {
    REQUIRE(command_tail(L"wlm2pst.exe").empty());
}

TEST_CASE("command_tail returns empty when there is no tail (quoted token only)",
          "[launcher][worker_select]") {
    REQUIRE(command_tail(L"\"C:\\Tools\\wlm2pst.exe\"").empty());
}

TEST_CASE("command_tail returns empty for empty input", "[launcher][worker_select]") {
    REQUIRE(command_tail(L"").empty());
}

TEST_CASE("command_tail handles an unterminated quote in argv[0] gracefully",
          "[launcher][worker_select]") {
    // Malformed input (no closing quote): the whole string is consumed as the
    // token, leaving an empty tail rather than reading out of bounds.
    REQUIRE(command_tail(L"\"C:\\Tools\\wlm2pst.exe --source C:\\Mail").empty());
}

TEST_CASE("command_tail collapses only a single separating space", "[launcher][worker_select]") {
    // Two spaces between the token and the tail: only one is stripped, so the
    // remaining leading space in the tail is preserved verbatim.
    REQUIRE(command_tail(L"wlm2pst.exe  --source C:\\Mail") == L" --source C:\\Mail");
}
