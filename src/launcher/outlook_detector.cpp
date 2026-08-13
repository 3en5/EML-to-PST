#include "launcher/outlook_detector.h"

#include <array>

#include "launcher/pe_arch.h"

namespace wlm2pst {

namespace {

constexpr size_t kPeProbeBytes = 4096;

// Registry redirection is opaque to a 32-bit launcher process unless the
// WOW64 view flags are given explicitly: without them, HKLM\SOFTWARE reads
// are silently redirected to WOW6432Node and a 64-bit Office install is
// invisible. We therefore always probe both explicit views rather than
// relying on RegistryView::kDefault.
constexpr std::array<RegistryView, 2> kBothViews = {RegistryView::kWow6432, RegistryView::kWin64};

std::optional<std::wstring> read_both_views(IRegistryReader& registry, RegistryRoot root,
                                              const std::wstring& subkey,
                                              const std::wstring& value) {
    for (RegistryView view : kBothViews) {
        auto result = registry.read_string(root, subkey, value, view);
        if (result.has_value()) {
            return result;
        }
    }
    return std::nullopt;
}

OutlookBitness bitness_from_string(const std::wstring& text) {
    if (text == L"x64") return OutlookBitness::kX64;
    if (text == L"x86") return OutlookBitness::kX86;
    return OutlookBitness::kUnknown;
}

// Layer 1: Click-to-Run declares the installed Office platform directly.
OutlookBitness detect_click_to_run_bitness(IRegistryReader& registry) {
    auto platform = read_both_views(registry, RegistryRoot::kLocalMachine,
                                     L"SOFTWARE\\Microsoft\\Office\\ClickToRun\\Configuration",
                                     L"Platform");
    if (!platform.has_value()) {
        return OutlookBitness::kUnknown;
    }
    return bitness_from_string(*platform);
}

// Layer 2: MSI-installed Office publishes a per-version Bitness value under
// its Outlook subkey. Newest version first.
OutlookBitness detect_msi_bitness(IRegistryReader& registry) {
    static constexpr std::array<const wchar_t*, 3> kVersions = {L"16.0", L"15.0", L"14.0"};
    for (const wchar_t* version : kVersions) {
        std::wstring subkey = L"SOFTWARE\\Microsoft\\Office\\" + std::wstring(version) +
                               L"\\Outlook";
        auto bitness_value = read_both_views(registry, RegistryRoot::kLocalMachine, subkey,
                                              L"Bitness");
        if (bitness_value.has_value()) {
            OutlookBitness resolved = bitness_from_string(*bitness_value);
            if (resolved != OutlookBitness::kUnknown) {
                return resolved;
            }
        }
    }
    return OutlookBitness::kUnknown;
}

// Layer 3: locate OUTLOOK.EXE via the App Paths alias, falling back to
// Click-to-Run's InstallationPath combined with the conventional per-version
// office root layout.
std::wstring locate_outlook_exe(IRegistryReader& registry, IFileProbe& files) {
    auto app_paths = read_both_views(
        registry, RegistryRoot::kLocalMachine,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\OUTLOOK.EXE", L"");
    if (app_paths.has_value() && !app_paths->empty() && files.file_exists(*app_paths)) {
        return *app_paths;
    }

    auto install_path = read_both_views(registry, RegistryRoot::kLocalMachine,
                                         L"SOFTWARE\\Microsoft\\Office\\ClickToRun\\Configuration",
                                         L"InstallationPath");
    if (install_path.has_value() && !install_path->empty()) {
        static constexpr std::array<const wchar_t*, 2> kOfficeRoots = {L"Office16", L"Office15"};
        for (const wchar_t* office_root : kOfficeRoots) {
            std::wstring candidate =
                *install_path + L"\\root\\" + office_root + L"\\OUTLOOK.EXE";
            if (files.file_exists(candidate)) {
                return candidate;
            }
        }
    }

    // Even if nothing existed on disk, prefer to surface the App Paths value
    // (if any) for diagnostics rather than an empty string.
    if (app_paths.has_value() && !app_paths->empty()) {
        return *app_paths;
    }
    return {};
}

}  // namespace

std::vector<std::wstring> default_new_outlook_candidate_paths() { return {}; }

OutlookDetection detect_outlook(IRegistryReader& registry, IFileProbe& files,
                                 std::vector<std::wstring> new_outlook_candidate_paths) {
    OutlookDetection result;

    // Layer 1: Click-to-Run Platform value.
    OutlookBitness bitness = detect_click_to_run_bitness(registry);
    if (bitness != OutlookBitness::kUnknown) {
        result.bitness = bitness;
        result.detection_source = "c2r-platform";
    }

    // Layer 2: MSI/Office per-version Bitness value (only if layer 1 was
    // inconclusive).
    if (result.bitness == OutlookBitness::kUnknown) {
        bitness = detect_msi_bitness(registry);
        if (bitness != OutlookBitness::kUnknown) {
            result.bitness = bitness;
            result.detection_source = "office-bitness";
        }
    }

    // Layer 3: locate OUTLOOK.EXE regardless of whether bitness is already
    // known - we still want the path for launching diagnostics and for the
    // PE confirmation step below.
    result.outlook_exe_path = locate_outlook_exe(registry, files);

    // Layer 4: PE header confirmation. When a path was found and the file is
    // readable, the PE header is authoritative and overrides any registry
    // hint on conflict.
    if (!result.outlook_exe_path.empty() && files.file_exists(result.outlook_exe_path)) {
        std::vector<uint8_t> prefix = files.read_prefix(result.outlook_exe_path, kPeProbeBytes);
        PeArch arch = parse_pe_machine(prefix);
        switch (arch) {
            case PeArch::kX86:
                result.bitness = OutlookBitness::kX86;
                result.detection_source = "pe-header";
                break;
            case PeArch::kX64:
                result.bitness = OutlookBitness::kX64;
                result.detection_source = "pe-header";
                break;
            case PeArch::kArm64:
                // Extended MAPI does not have a distinct ARM64 worker; leave
                // whatever registry-derived hint (if any) stood, since the PE
                // result here is not directly actionable for worker
                // selection.
                break;
            case PeArch::kUnknown:
                // Unreadable/garbled PE header: retain the registry-derived
                // bitness (if any) rather than downgrading it.
                break;
        }
    }

    if (!result.outlook_exe_path.empty() && result.detection_source.empty()) {
        // Found the exe but never confirmed a bitness (e.g. PE unreadable
        // and no registry hint resolved).
        result.detection_source = "app-paths";
    }

    result.classic_installed =
        !result.outlook_exe_path.empty() || result.bitness != OutlookBitness::kUnknown;

    if (!result.classic_installed) {
        for (const std::wstring& candidate : new_outlook_candidate_paths) {
            if (files.file_exists(candidate)) {
                result.new_outlook_only = true;
                result.detection_source = "new-outlook-appx";
                break;
            }
        }
    }

    return result;
}

}  // namespace wlm2pst
