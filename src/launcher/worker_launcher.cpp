// WLM2PST - portable worker-selection logic (no OS calls). See
// worker_launcher_win.cpp for the actual process spawn.
#include "launcher/worker_launcher.h"

namespace wlm2pst {

std::wstring worker_exe_name(OutlookBitness bitness) {
    switch (bitness) {
        case OutlookBitness::kX86: return L"wlm2pst-worker-x86.exe";
        case OutlookBitness::kX64: return L"wlm2pst-worker-x64.exe";
        case OutlookBitness::kUnknown: return L"";
    }
    return L"";
}

std::wstring command_tail(std::wstring_view full_command_line) {
    size_t pos = 0;
    size_t len = full_command_line.size();

    // Skip leading whitespace before the program-name token, mirroring what
    // GetCommandLineW callers typically see (none in practice, but be safe).
    while (pos < len && full_command_line[pos] == L' ') {
        ++pos;
    }

    size_t token_end;
    if (pos < len && full_command_line[pos] == L'"') {
        // Quoted argv[0]: terminated by the next double quote (Windows
        // special-cases argv[0] parsing - no backslash-escaping rules apply
        // here, unlike subsequent arguments).
        size_t closing = full_command_line.find(L'"', pos + 1);
        token_end = (closing == std::wstring_view::npos) ? len : closing + 1;
    } else {
        // Bare argv[0]: terminated by the first whitespace, or end of string.
        size_t space = full_command_line.find(L' ', pos);
        token_end = (space == std::wstring_view::npos) ? len : space;
    }

    if (token_end >= len) {
        return {};
    }

    std::wstring_view tail = full_command_line.substr(token_end);
    // Strip a single separating space between the token and the tail, if
    // present, so callers can glue it directly after their own quoted path.
    if (!tail.empty() && tail.front() == L' ') {
        tail.remove_prefix(1);
    }
    return std::wstring(tail);
}

}  // namespace wlm2pst
