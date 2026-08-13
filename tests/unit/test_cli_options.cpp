// WLM2PST - unit tests for worker/cli/cli_options (spec sections 5, 28).
#include "worker/cli/cli_options.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace wlm2pst;

namespace {

std::vector<std::wstring> args(std::initializer_list<const wchar_t*> tokens) {
    std::vector<std::wstring> out;
    for (const wchar_t* t : tokens) out.emplace_back(t);
    return out;
}

}  // namespace

// --- parse_command_line: happy paths -----------------------------------

TEST_CASE("parse_command_line accepts minimal required options", "[cli]") {
    auto result = parse_command_line(args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst"}));
    REQUIRE(result.ok);
    CHECK(result.options.source == L"C:\\Mail");
    CHECK(result.options.output == L"D:\\out.pst");
}

TEST_CASE("parse_command_line applies documented defaults", "[cli]") {
    auto result = parse_command_line(args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst"}));
    REQUIRE(result.ok);
    CHECK(result.options.root_name == L"Windows Live Mail");
    CHECK_FALSE(result.options.resume);
    CHECK_FALSE(result.options.overwrite);
    CHECK_FALSE(result.options.quiet);
    CHECK_FALSE(result.options.verbose);
    CHECK_FALSE(result.options.show_help);
    CHECK_FALSE(result.options.show_version);
}

TEST_CASE("parse_command_line accepts --opt value form for --root-name", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--root-name", L"Rina old mail"}));
    REQUIRE(result.ok);
    CHECK(result.options.root_name == L"Rina old mail");
}

TEST_CASE("parse_command_line accepts --opt=value form", "[cli]") {
    auto result = parse_command_line(
        args({L"--source=C:\\Mail", L"--output=D:\\out.pst", L"--root-name=Rina old mail"}));
    REQUIRE(result.ok);
    CHECK(result.options.source == L"C:\\Mail");
    CHECK(result.options.output == L"D:\\out.pst");
    CHECK(result.options.root_name == L"Rina old mail");
}

TEST_CASE("parse_command_line accepts Hebrew root name", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--root-name", L"\u05D3\u05D5\u05D0\u05E8"}));
    REQUIRE(result.ok);
    CHECK(result.options.root_name == L"\u05D3\u05D5\u05D0\u05E8");
}

TEST_CASE("parse_command_line accepts already-tokenized values with spaces", "[cli]") {
    // Shell/CRT tokenization already stripped the quotes; the value arrives
    // as one token containing spaces.
    auto result = parse_command_line(
        args({L"--source", L"C:\\Old Mail Folder", L"--output", L"D:\\Migration\\Old Mail.pst"}));
    REQUIRE(result.ok);
    CHECK(result.options.source == L"C:\\Old Mail Folder");
    CHECK(result.options.output == L"D:\\Migration\\Old Mail.pst");
}

TEST_CASE("parse_command_line sets each boolean flag independently", "[cli]") {
    {
        auto r = parse_command_line(args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--resume"}));
        REQUIRE(r.ok);
        CHECK(r.options.resume);
    }
    {
        auto r = parse_command_line(
            args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--overwrite"}));
        REQUIRE(r.ok);
        CHECK(r.options.overwrite);
    }
    {
        auto r = parse_command_line(args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--quiet"}));
        REQUIRE(r.ok);
        CHECK(r.options.quiet);
    }
    {
        auto r = parse_command_line(
            args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--verbose"}));
        REQUIRE(r.ok);
        CHECK(r.options.verbose);
    }
}

// --- help / version bypass -----------------------------------------------

TEST_CASE("parse_command_line --help alone is accepted with no other requirements", "[cli]") {
    auto result = parse_command_line(args({L"--help"}));
    REQUIRE(result.ok);
    CHECK(result.options.show_help);
    CHECK(result.options.source.empty());
    CHECK(result.options.output.empty());
}

TEST_CASE("parse_command_line -h is an alias for --help", "[cli]") {
    auto result = parse_command_line(args({L"-h"}));
    REQUIRE(result.ok);
    CHECK(result.options.show_help);
}

TEST_CASE("parse_command_line --version alone is accepted", "[cli]") {
    auto result = parse_command_line(args({L"--version"}));
    REQUIRE(result.ok);
    CHECK(result.options.show_version);
}

TEST_CASE("parse_command_line --help bypasses mutual-exclusion validation", "[cli]") {
    auto result = parse_command_line(args({L"--help", L"--resume", L"--overwrite"}));
    REQUIRE(result.ok);
    CHECK(result.options.show_help);
}

TEST_CASE("parse_command_line --version bypasses required-argument validation", "[cli]") {
    auto result = parse_command_line(args({L"--version"}));
    REQUIRE(result.ok);
}

// --- required arguments ----------------------------------------------------

TEST_CASE("parse_command_line requires --source", "[cli]") {
    auto result = parse_command_line(args({L"--output", L"D:\\out.pst"}));
    CHECK_FALSE(result.ok);
    CHECK(result.error_utf8.find("--source") != std::string::npos);
}

TEST_CASE("parse_command_line requires --output", "[cli]") {
    auto result = parse_command_line(args({L"--source", L"C:\\Mail"}));
    CHECK_FALSE(result.ok);
    CHECK(result.error_utf8.find("--output") != std::string::npos);
}

TEST_CASE("parse_command_line with no arguments at all fails on --source", "[cli]") {
    auto result = parse_command_line(args({}));
    CHECK_FALSE(result.ok);
}

// --- syntax errors -----------------------------------------------------

TEST_CASE("parse_command_line rejects an unknown option", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--frobnicate"}));
    CHECK_FALSE(result.ok);
    CHECK(result.error_utf8.find("--frobnicate") != std::string::npos);
}

TEST_CASE("parse_command_line reports a missing value for --source", "[cli]") {
    auto result = parse_command_line(args({L"--source"}));
    CHECK_FALSE(result.ok);
    CHECK(result.error_utf8.find("--source") != std::string::npos);
}

TEST_CASE("parse_command_line reports a missing value for --output", "[cli]") {
    auto result = parse_command_line(args({L"--source", L"C:\\Mail", L"--output"}));
    CHECK_FALSE(result.ok);
    CHECK(result.error_utf8.find("--output") != std::string::npos);
}

TEST_CASE("parse_command_line reports a missing value for --root-name", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--root-name"}));
    CHECK_FALSE(result.ok);
    CHECK(result.error_utf8.find("--root-name") != std::string::npos);
}

TEST_CASE("parse_command_line rejects an empty --root-name value at validation time", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--root-name", L""}));
    REQUIRE(result.ok);
    CHECK(result.options.root_name.empty());
    auto validation = validate_options_lexical(result.options);
    REQUIRE(validation.has_value());
    CHECK(validation->exit_code == ExitCode::kInvalidArguments);
}

TEST_CASE("parse_command_line rejects a duplicate --source", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\A", L"--source", L"C:\\B", L"--output", L"D:\\out.pst"}));
    CHECK_FALSE(result.ok);
    CHECK(result.error_utf8.find("--source") != std::string::npos);
}

TEST_CASE("parse_command_line rejects a duplicate boolean flag", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--quiet", L"--quiet"}));
    CHECK_FALSE(result.ok);
}

TEST_CASE("parse_command_line rejects -h combined with --help as a duplicate", "[cli]") {
    auto result = parse_command_line(args({L"-h", L"--help"}));
    CHECK_FALSE(result.ok);
}

TEST_CASE("parse_command_line rejects a value attached to a boolean flag", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--resume=yes"}));
    CHECK_FALSE(result.ok);
}

// --- mutual exclusions -----------------------------------------------------

TEST_CASE("parse_command_line rejects --resume with --overwrite", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--resume", L"--overwrite"}));
    CHECK_FALSE(result.ok);
}

TEST_CASE("parse_command_line rejects --quiet with --verbose", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--quiet", L"--verbose"}));
    CHECK_FALSE(result.ok);
}

TEST_CASE("parse_command_line allows --resume alone", "[cli]") {
    auto result = parse_command_line(args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--resume"}));
    CHECK(result.ok);
}

TEST_CASE("parse_command_line allows --overwrite alone", "[cli]") {
    auto result = parse_command_line(
        args({L"--source", L"C:\\Mail", L"--output", L"D:\\out.pst", L"--overwrite"}));
    CHECK(result.ok);
}

// --- usage_text --------------------------------------------------------

TEST_CASE("usage_text documents every option, the default root name, and 3 examples", "[cli]") {
    std::string text = usage_text();
    CHECK(text.find("--source") != std::string::npos);
    CHECK(text.find("--output") != std::string::npos);
    CHECK(text.find("--root-name") != std::string::npos);
    CHECK(text.find("--resume") != std::string::npos);
    CHECK(text.find("--overwrite") != std::string::npos);
    CHECK(text.find("--quiet") != std::string::npos);
    CHECK(text.find("--verbose") != std::string::npos);
    CHECK(text.find("--help") != std::string::npos);
    CHECK(text.find("--version") != std::string::npos);
    CHECK(text.find("Windows Live Mail") != std::string::npos);

    // 3 usage examples per spec section 5.
    size_t example_count = 0;
    size_t pos = 0;
    while ((pos = text.find("wlm2pst.exe --source", pos)) != std::string::npos) {
        ++example_count;
        pos += 1;
    }
    CHECK(example_count >= 3);
}

// --- validate_options_lexical: output extension -----------------------

TEST_CASE("validate_options_lexical accepts .pst and .PST", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Mail";
    opts.output = L"D:\\Migration\\out.pst";
    CHECK_FALSE(validate_options_lexical(opts).has_value());

    opts.output = L"D:\\Migration\\out.PST";
    CHECK_FALSE(validate_options_lexical(opts).has_value());
}

TEST_CASE("validate_options_lexical rejects a non-.pst extension", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Mail";
    opts.output = L"D:\\Migration\\out.ost";
    auto result = validate_options_lexical(opts);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

// --- validate_options_lexical: UNC rejection ----------------------------

TEST_CASE("validate_options_lexical rejects a UNC source path", "[cli]") {
    CliOptions opts;
    opts.source = LR"(\\server\share\Mail)";
    opts.output = L"D:\\Migration\\out.pst";
    auto result = validate_options_lexical(opts);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

TEST_CASE("validate_options_lexical rejects a UNC output path", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Mail";
    opts.output = LR"(\\server\share\out.pst)";
    auto result = validate_options_lexical(opts);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

// --- validate_options_lexical: containment -----------------------------

TEST_CASE("validate_options_lexical rejects output inside source", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Mail";
    opts.output = L"C:\\Mail\\Archive\\out.pst";
    auto result = validate_options_lexical(opts);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

TEST_CASE("validate_options_lexical rejects source inside output's parent tree", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Migration\\out.pst\\Mail";
    opts.output = L"C:\\Migration\\out.pst";
    auto result = validate_options_lexical(opts);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

TEST_CASE("validate_options_lexical allows sibling source and output trees", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Mail\\Windows Live Mail";
    opts.output = L"D:\\Migration\\Rina-Mail.pst";
    CHECK_FALSE(validate_options_lexical(opts).has_value());
}

// --- validate_options_lexical: absolute path requirement ----------------

TEST_CASE("validate_options_lexical rejects a relative source path", "[cli]") {
    CliOptions opts;
    opts.source = L"Mail\\Windows Live Mail";
    opts.output = L"D:\\Migration\\out.pst";
    auto result = validate_options_lexical(opts);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

TEST_CASE("validate_options_lexical rejects a relative output path", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Mail";
    opts.output = L"Migration\\out.pst";
    auto result = validate_options_lexical(opts);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

// --- validate_options_lexical: root name --------------------------------

TEST_CASE("validate_options_lexical rejects a root name that is only dots and spaces", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Mail";
    opts.output = L"D:\\Migration\\out.pst";
    opts.root_name = L"   ...";
    auto result = validate_options_lexical(opts);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == ExitCode::kInvalidArguments);
}

TEST_CASE("validate_options_lexical accepts a root name with trimmed trailing dots", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Mail";
    opts.output = L"D:\\Migration\\out.pst";
    opts.root_name = L"Rina old mail...";
    CHECK_FALSE(validate_options_lexical(opts).has_value());
}

TEST_CASE("validate_options_lexical accepts the default root name", "[cli]") {
    CliOptions opts;
    opts.source = L"C:\\Mail";
    opts.output = L"D:\\Migration\\out.pst";
    CHECK_FALSE(validate_options_lexical(opts).has_value());
}
