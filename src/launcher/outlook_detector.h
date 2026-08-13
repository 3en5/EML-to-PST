// WLM2PST - layered classic-Outlook / bitness detector (spec section 4).
//
// No single registry key is trusted. The detector inspects several
// independent signals (Click-to-Run configuration, MSI/Office bitness keys,
// App Paths, and finally the OUTLOOK.EXE PE header itself) and reconciles
// them, preferring the PE header when it disagrees with registry hints.
// Portable: exercised entirely through IRegistryReader / IFileProbe so it is
// unit-testable off Windows.
#pragma once

#include <string>
#include <vector>

#include "launcher/registry_reader.h"

namespace wlm2pst {

enum class OutlookBitness { kUnknown, kX86, kX64 };

struct OutlookDetection {
    bool classic_installed = false;
    // Best-effort: a New Outlook (appx) alias was found but no classic
    // Outlook install could be located.
    bool new_outlook_only = false;
    OutlookBitness bitness = OutlookBitness::kUnknown;
    std::wstring outlook_exe_path;  // Set when a classic OUTLOOK.EXE was located.
    // Machine-readable source of the winning bitness determination, e.g.
    // "c2r-platform", "office-bitness", "pe-header", "app-paths" (exe located
    // but bitness could not be confirmed), or "" when nothing was found.
    std::string detection_source;
};

// Candidate absolute paths to probe for a New Outlook (appx) install when no
// classic Outlook could be located. Portable code cannot expand
// %LOCALAPPDATA% itself (no environment/codepage assumptions here), so this
// default is intentionally empty; main.cpp (Windows-only) builds the real
// env-expanded candidate (per spec: LOCALAPPDATA plus Microsoft, WindowsApps,
// olk.exe) and passes it in explicitly. Tests inject their own fake paths.
std::vector<std::wstring> default_new_outlook_candidate_paths();

OutlookDetection detect_outlook(
    IRegistryReader& registry, IFileProbe& files,
    std::vector<std::wstring> new_outlook_candidate_paths = default_new_outlook_candidate_paths());

}  // namespace wlm2pst
