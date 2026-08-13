#include "worker/cli/cli_options.h"

#include <set>

#include "common/paths/path_utils.h"
#include "common/unicode/utf.h"

namespace wlm2pst {

namespace {

// Every token this parser understands, normalized to a canonical key used
// for duplicate detection ("-h" and "--help" collide on purpose).
enum class OptionKind { kSource, kOutput, kRootName, kResume, kOverwrite, kQuiet, kVerbose, kHelp, kVersion };

struct OptionSpec {
    std::wstring_view flag;
    OptionKind kind;
    bool takes_value;
    const char* canonical;  // duplicate-detection / error-message key
};

constexpr OptionSpec kOptionTable[] = {
    {L"--source", OptionKind::kSource, true, "--source"},
    {L"--output", OptionKind::kOutput, true, "--output"},
    {L"--root-name", OptionKind::kRootName, true, "--root-name"},
    {L"--resume", OptionKind::kResume, false, "--resume"},
    {L"--overwrite", OptionKind::kOverwrite, false, "--overwrite"},
    {L"--quiet", OptionKind::kQuiet, false, "--quiet"},
    {L"--verbose", OptionKind::kVerbose, false, "--verbose"},
    {L"--help", OptionKind::kHelp, false, "--help"},
    {L"-h", OptionKind::kHelp, false, "--help"},
    {L"--version", OptionKind::kVersion, false, "--version"},
};

const OptionSpec* find_option(std::wstring_view name) {
    for (const auto& spec : kOptionTable) {
        if (compare_ordinal(spec.flag, name) == 0) {
            return &spec;
        }
    }
    return nullptr;
}

// Splits "--opt=value" into ("--opt", "value"); tokens without '=' return
// (token, nullopt).
std::pair<std::wstring, std::optional<std::wstring>> split_inline_value(const std::wstring& token) {
    size_t eq = token.find(L'=');
    if (eq == std::wstring::npos) {
        return {token, std::nullopt};
    }
    return {token.substr(0, eq), std::optional<std::wstring>(token.substr(eq + 1))};
}

CliParseResult fail(std::string message) {
    CliParseResult result;
    result.ok = false;
    result.error_utf8 = std::move(message);
    return result;
}

std::wstring trim_trailing_dots_and_spaces(std::wstring_view s) {
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == L' ' || s[end - 1] == L'.')) {
        --end;
    }
    return std::wstring(s.substr(0, end));
}

}  // namespace

CliParseResult parse_command_line(const std::vector<std::wstring>& args) {
    CliOptions options;
    std::set<std::string> seen;

    for (size_t i = 0; i < args.size(); ++i) {
        auto [name, inline_value] = split_inline_value(args[i]);

        const OptionSpec* spec = find_option(name);
        if (spec == nullptr) {
            return fail("unknown option: " + utf8_from_wide(name));
        }

        if (!seen.insert(spec->canonical).second) {
            return fail(std::string("duplicate option: ") + spec->canonical);
        }

        std::wstring value;
        if (spec->takes_value) {
            if (inline_value.has_value()) {
                value = *inline_value;
            } else {
                if (i + 1 >= args.size()) {
                    return fail(std::string("missing value for option: ") + spec->canonical);
                }
                value = args[++i];
            }
        } else if (inline_value.has_value()) {
            return fail(std::string("option does not take a value: ") + spec->canonical);
        }

        switch (spec->kind) {
            case OptionKind::kSource: options.source = std::move(value); break;
            case OptionKind::kOutput: options.output = std::move(value); break;
            case OptionKind::kRootName: options.root_name = std::move(value); break;
            case OptionKind::kResume: options.resume = true; break;
            case OptionKind::kOverwrite: options.overwrite = true; break;
            case OptionKind::kQuiet: options.quiet = true; break;
            case OptionKind::kVerbose: options.verbose = true; break;
            case OptionKind::kHelp: options.show_help = true; break;
            case OptionKind::kVersion: options.show_version = true; break;
        }
    }

    if (options.show_help || options.show_version) {
        CliParseResult result;
        result.options = std::move(options);
        return result;
    }

    if (options.resume && options.overwrite) {
        return fail("--resume and --overwrite are mutually exclusive");
    }
    if (options.quiet && options.verbose) {
        return fail("--quiet and --verbose are mutually exclusive");
    }
    if (options.source.empty()) {
        return fail("missing required option: --source");
    }
    if (options.output.empty()) {
        return fail("missing required option: --output");
    }

    CliParseResult result;
    result.options = std::move(options);
    return result;
}

std::string usage_text() {
    return
        "wlm2pst - convert a Windows Live Mail EML tree into one Unicode PST\n"
        "\n"
        "USAGE:\n"
        "  wlm2pst.exe --source <directory> --output <file.pst> [options]\n"
        "\n"
        "REQUIRED OPTIONS:\n"
        "  --source <directory>      Recursive source folder containing EML files.\n"
        "  --output <file.pst>       Path to a new PST file.\n"
        "\n"
        "OPTIONS:\n"
        "  --root-name <name>        Root folder name inside the PST.\n"
        "                             Default: Windows Live Mail\n"
        "  --resume                   Resume a compatible interrupted job.\n"
        "  --overwrite                Delete a previous tool-owned output and start again.\n"
        "  --quiet                    Show only errors and final summary.\n"
        "  --verbose                  Show additional technical progress without exposing\n"
        "                             message content.\n"
        "  --help, -h                 Show this help.\n"
        "  --version                  Show application, worker, architecture, and build\n"
        "                             versions.\n"
        "\n"
        "EXAMPLES:\n"
        "  wlm2pst.exe --source \"C:\\OldMail\" --output \"D:\\Migration\\OldMail.pst\"\n"
        "\n"
        "  wlm2pst.exe --source \"C:\\OldMail\" --output \"D:\\Migration\\OldMail.pst\" "
        "--root-name \"Rina old mail\"\n"
        "\n"
        "  wlm2pst.exe --source \"C:\\OldMail\" --output \"D:\\Migration\\OldMail.pst\" --resume\n";
}

std::optional<CliValidationError> validate_options_lexical(const CliOptions& options) {
    if (!has_pst_extension(options.output)) {
        return CliValidationError{ExitCode::kInvalidArguments,
                                   "--output must end in .pst"};
    }

    if (is_unc_path(options.source)) {
        return CliValidationError{
            ExitCode::kInvalidArguments,
            "--source must be a local filesystem path; UNC and network paths are rejected"};
    }
    if (is_unc_path(options.output)) {
        return CliValidationError{
            ExitCode::kInvalidArguments,
            "--output must be a local filesystem path; UNC and network paths are rejected"};
    }

    if (!drive_letter_of(options.source).has_value()) {
        return CliValidationError{
            ExitCode::kInvalidArguments,
            "--source must be an absolute path with a drive letter"};
    }
    if (!drive_letter_of(options.output).has_value()) {
        return CliValidationError{
            ExitCode::kInvalidArguments,
            "--output must be an absolute path with a drive letter"};
    }

    if (is_lexically_within(options.source, options.output) ||
        is_lexically_within(options.output, options.source)) {
        return CliValidationError{
            ExitCode::kInvalidArguments,
            "--output must not be inside --source, and --source must not be inside --output"};
    }

    if (trim_trailing_dots_and_spaces(options.root_name).empty()) {
        return CliValidationError{ExitCode::kInvalidArguments,
                                   "--root-name must not be empty"};
    }

    return std::nullopt;
}

}  // namespace wlm2pst
