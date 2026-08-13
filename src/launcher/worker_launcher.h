// WLM2PST - worker selection and spawn (spec section 4). The decision logic
// (which worker exe name to use, how to split the original command line) is
// portable and unit-tested; the actual spawn only exists on Windows
// (worker_launcher_win.cpp).
#pragma once

#include <string>
#include <string_view>

#include "common/errors/result.h"
#include "launcher/outlook_detector.h"

namespace wlm2pst {

// Maps a detected bitness to the matching packaged worker executable name.
// Returns an empty string for OutlookBitness::kUnknown - callers must treat
// that as "no matching worker" (spec section 22, kBitnessMismatch).
std::wstring worker_exe_name(OutlookBitness bitness);

// GetCommandLineW() returns the full command line including the program's
// own argv[0], which may be a bare or double-quoted token (Windows argument
// parsing rules: a quoted token may contain spaces; an unquoted token ends at
// the first whitespace). command_tail skips exactly that first token and
// returns everything after it with a single separating space (if present)
// removed, so it can be re-glued after the worker's own path when building
// the child process command line. Returns an empty string when there is no
// tail (or no input).
std::wstring command_tail(std::wstring_view full_command_line);

#ifdef _WIN32
// Spawns worker_full_path with original_command_tail appended after a
// freshly quoted copy of worker_full_path, inheriting the console so
// Ctrl+C/output pass through naturally. Waits for the child and returns its
// exit code.
Result<int> run_worker(const std::wstring& worker_full_path,
                        const std::wstring& original_command_tail);
#endif

}  // namespace wlm2pst
