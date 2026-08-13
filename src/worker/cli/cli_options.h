// WLM2PST - command-line parsing and lexical validation (spec section 5).
//
// Fully portable: no filesystem access, no Windows headers. `parse_command_line`
// only tokenizes and enforces syntactic rules (unknown/duplicate/missing-value
// options, mutually exclusive flags). Semantic path validation lives in
// `validate_options_lexical`, which is pure string manipulation over already
// -parsed options (real path/filesystem checks belong to worker/preflight).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common/exit_codes.h"

namespace wlm2pst {

struct CliOptions {
    std::wstring source;      // required for conversion
    std::wstring output;      // required for conversion
    std::wstring root_name = L"Windows Live Mail";
    bool resume = false;
    bool overwrite = false;
    bool quiet = false;
    bool verbose = false;
    bool show_help = false;
    bool show_version = false;
};

struct CliParseResult {
    CliOptions options;
    bool ok = true;
    // Privacy-safe: may name the offending OPTION, never the value of any
    // argument (values can contain filesystem paths from other machines).
    std::string error_utf8;
};

// Parses `args` (argv[0] excluded). Never throws.
CliParseResult parse_command_line(const std::vector<std::wstring>& args);

// Full --help text: options, defaults, and usage examples (spec section 5).
std::string usage_text();

// A validation failure paired with the stable process exit code it maps to
// (spec section 22). Shared by CLI lexical validation and worker/preflight.
struct CliValidationError {
    ExitCode exit_code = ExitCode::kInvalidArguments;
    std::string message_utf8;
};

// Pure lexical/string validation of already-parsed options: no filesystem
// access. Assumes `options.source` and `options.output` are non-empty (the
// parser enforces that when neither --help nor --version was requested).
std::optional<CliValidationError> validate_options_lexical(const CliOptions& options);

}  // namespace wlm2pst
