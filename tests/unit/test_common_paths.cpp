#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "common/paths/path_utils.h"

using wlm2pst::drive_letter_of;
using wlm2pst::has_pst_extension;
using wlm2pst::is_extended_length_path;
using wlm2pst::is_lexically_within;
using wlm2pst::is_unc_path;
using wlm2pst::normalize_backslashes;
using wlm2pst::to_extended_length_path;

TEST_CASE("is_unc_path recognizes UNC and extended-UNC forms", "[paths]") {
    CHECK(is_unc_path(LR"(\\server\share)"));
    CHECK(is_unc_path(LR"(\\server\share\sub\file.pst)"));
    CHECK(is_unc_path(LR"(\\?\UNC\server\share)"));
    CHECK(is_unc_path(LR"(\\?\UNC\server\share\sub)"));

    CHECK_FALSE(is_unc_path(LR"(C:\a\b)"));
    CHECK_FALSE(is_unc_path(LR"(\\?\C:\a\b)"));  // extended-length but not UNC
    CHECK_FALSE(is_unc_path(LR"(\\)"));           // bare double backslash, nothing after
    CHECK_FALSE(is_unc_path(LR"(relative\path)"));
    CHECK_FALSE(is_unc_path(L""));
}

TEST_CASE("is_extended_length_path recognizes the \\\\?\\ prefix", "[paths]") {
    CHECK(is_extended_length_path(LR"(\\?\C:\a)"));
    CHECK(is_extended_length_path(LR"(\\?\UNC\server\share)"));
    CHECK_FALSE(is_extended_length_path(LR"(C:\a)"));
    CHECK_FALSE(is_extended_length_path(LR"(\\server\share)"));
    CHECK_FALSE(is_extended_length_path(L""));
}

TEST_CASE("to_extended_length_path converts and is idempotent", "[paths]") {
    CHECK(to_extended_length_path(LR"(C:\a\b)") == LR"(\\?\C:\a\b)");
    CHECK(to_extended_length_path(LR"(c:\a\b)") == LR"(\\?\c:\a\b)");
    CHECK(to_extended_length_path(LR"(\\server\share\x)") == LR"(\\?\UNC\server\share\x)");

    // Idempotent on already-extended input.
    CHECK(to_extended_length_path(LR"(\\?\C:\a\b)") == LR"(\\?\C:\a\b)");
    CHECK(to_extended_length_path(LR"(\\?\UNC\server\share\x)") == LR"(\\?\UNC\server\share\x)");

    // Forms that cannot be losslessly extended are returned unchanged.
    CHECK(to_extended_length_path(LR"(relative\path)") == LR"(relative\path)");
}

TEST_CASE("has_pst_extension is case-insensitive", "[paths]") {
    CHECK(has_pst_extension(LR"(C:\a\Mail.pst)"));
    CHECK(has_pst_extension(LR"(C:\a\Mail.PST)"));
    CHECK(has_pst_extension(LR"(C:\a\Mail.Pst)"));

    CHECK_FALSE(has_pst_extension(LR"(C:\a\Mail.psth)"));
    CHECK_FALSE(has_pst_extension(LR"(C:\a\Mail)"));
    CHECK_FALSE(has_pst_extension(L"pst"));
    CHECK_FALSE(has_pst_extension(L""));
}

TEST_CASE("normalize_backslashes converts slashes and collapses doubles", "[paths]") {
    CHECK(normalize_backslashes(L"C:/a/b") == LR"(C:\a\b)");
    CHECK(normalize_backslashes(LR"(C:\\a\\\b)") == LR"(C:\a\b)");
    CHECK(normalize_backslashes(L"C:/a\\/b") == LR"(C:\a\b)");

    // Leading UNC/extended-length double backslash is preserved, extra
    // backslashes elsewhere are collapsed.
    CHECK(normalize_backslashes(LR"(\\server\\share\\x)") == LR"(\\server\share\x)");
    CHECK(normalize_backslashes(LR"(\\?\C:\\a)") == LR"(\\?\C:\a)");
    CHECK(normalize_backslashes(LR"(\\\\\\server\share)") == LR"(\\server\share)");
}

TEST_CASE("drive_letter_of", "[paths]") {
    CHECK(drive_letter_of(LR"(C:\a\b)") == std::optional<wchar_t>(L'C'));
    CHECK(drive_letter_of(LR"(d:\a\b)") == std::optional<wchar_t>(L'd'));
    CHECK(drive_letter_of(LR"(\\?\D:\a\b)") == std::optional<wchar_t>(L'D'));
    CHECK(drive_letter_of(LR"(\\server\share)") == std::nullopt);
    CHECK(drive_letter_of(LR"(\\?\UNC\server\share)") == std::nullopt);
    CHECK(drive_letter_of(LR"(relative\path)") == std::nullopt);
    CHECK(drive_letter_of(L"") == std::nullopt);
}

TEST_CASE("is_lexically_within uses component containment, not string prefix", "[paths]") {
    CHECK(is_lexically_within(LR"(C:\a\b)", LR"(C:\a\b\c.pst)"));
    CHECK(is_lexically_within(LR"(C:\a\b)", LR"(C:\a\b)"));  // a path is within itself

    // The classic false-positive: "bc" is not nested inside component "b".
    CHECK_FALSE(is_lexically_within(LR"(C:\a\b)", LR"(C:\a\bc)"));
    CHECK_FALSE(is_lexically_within(LR"(C:\a\bc)", LR"(C:\a\b)"));

    CHECK_FALSE(is_lexically_within(LR"(C:\a\b)", LR"(C:\a)"));  // shallower than base

    // Case-insensitive.
    CHECK(is_lexically_within(LR"(C:\A\B)", LR"(c:\a\b\C)"));

    // Trailing separators tolerated.
    CHECK(is_lexically_within(LR"(C:\a\b\)", LR"(C:\a\b\c)"));
    CHECK(is_lexically_within(LR"(C:\a\b)", LR"(C:\a\b\)"));

    // \\?\ (and \\?\UNC\) prefixes are stripped before comparing.
    CHECK(is_lexically_within(LR"(\\?\C:\a\b)", LR"(C:\a\b\c)"));
    CHECK(is_lexically_within(LR"(C:\a\b)", LR"(\\?\C:\a\b\c)"));
    CHECK(is_lexically_within(LR"(\\server\share)", LR"(\\server\share\x\y)"));
    CHECK(is_lexically_within(LR"(\\?\UNC\server\share)", LR"(\\server\share\x)"));

    CHECK_FALSE(is_lexically_within(LR"(C:\a\b)", LR"(D:\a\b\c)"));  // different drive
}
