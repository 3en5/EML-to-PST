#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "launcher/outlook_detector.h"

using wlm2pst::IFileProbe;
using wlm2pst::IRegistryReader;
using wlm2pst::OutlookBitness;
using wlm2pst::OutlookDetection;
using wlm2pst::RegistryRoot;
using wlm2pst::RegistryView;
using wlm2pst::detect_outlook;

namespace {

// Simple map-backed fake: exercises the detector exactly the way the real
// registry would (per-root/subkey/value/view), without touching Windows.
class FakeRegistryReader final : public IRegistryReader {
public:
    void set(RegistryRoot root, const std::wstring& subkey, const std::wstring& value,
              RegistryView view, std::wstring data) {
        values_[key(root, subkey, value, view)] = std::move(data);
    }

    std::optional<std::wstring> read_string(RegistryRoot root, const std::wstring& subkey,
                                              const std::wstring& value,
                                              RegistryView view) override {
        auto it = values_.find(key(root, subkey, value, view));
        if (it == values_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    using Key = std::tuple<int, std::wstring, std::wstring, int>;

    static Key key(RegistryRoot root, const std::wstring& subkey, const std::wstring& value,
                    RegistryView view) {
        return {static_cast<int>(root), subkey, value, static_cast<int>(view)};
    }

    std::map<Key, std::wstring> values_;
};

class FakeFileProbe final : public IFileProbe {
public:
    // Registers a file as existing. `prefix` is what read_prefix returns; an
    // empty prefix simulates a file that exists but cannot be parsed as PE
    // (e.g. access denied, non-PE content, or a genuinely empty file).
    void add(const std::wstring& path, std::vector<uint8_t> prefix = {}) {
        existing_.insert(path);
        prefixes_[path] = std::move(prefix);
    }

    bool file_exists(const std::wstring& path) override { return existing_.count(path) > 0; }

    std::vector<uint8_t> read_prefix(const std::wstring& path, size_t max_bytes) override {
        auto it = prefixes_.find(path);
        if (it == prefixes_.end()) {
            return {};
        }
        std::vector<uint8_t> result = it->second;
        if (result.size() > max_bytes) {
            result.resize(max_bytes);
        }
        return result;
    }

private:
    std::set<std::wstring> existing_;
    std::map<std::wstring, std::vector<uint8_t>> prefixes_;
};

constexpr const wchar_t* kC2rConfigKey = L"SOFTWARE\\Microsoft\\Office\\ClickToRun\\Configuration";
constexpr const wchar_t* kAppPathsKey =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\OUTLOOK.EXE";

void put_u32_le(std::vector<uint8_t>& buf, size_t offset, uint32_t value) {
    buf[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}
void put_u16_le(std::vector<uint8_t>& buf, size_t offset, uint16_t value) {
    buf[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

std::vector<uint8_t> make_pe_prefix(uint16_t machine) {
    std::vector<uint8_t> buf(0x80, 0);
    buf[0] = 'M';
    buf[1] = 'Z';
    put_u32_le(buf, 0x3C, 0x40);
    buf[0x40] = 'P';
    buf[0x41] = 'E';
    buf[0x42] = 0;
    buf[0x43] = 0;
    put_u16_le(buf, 0x44, machine);
    return buf;
}

constexpr uint16_t kMachineX86 = 0x014c;
constexpr uint16_t kMachineX64 = 0x8664;

}  // namespace

TEST_CASE("detect_outlook: nothing installed -> not installed, unknown bitness",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE_FALSE(result.classic_installed);
    REQUIRE_FALSE(result.new_outlook_only);
    REQUIRE(result.bitness == OutlookBitness::kUnknown);
    REQUIRE(result.outlook_exe_path.empty());
    REQUIRE(result.detection_source.empty());
}

TEST_CASE("detect_outlook: Click-to-Run Platform alone resolves bitness",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, kC2rConfigKey, L"Platform", RegistryView::kWow6432,
                 L"x64");

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);
    REQUIRE(result.bitness == OutlookBitness::kX64);
    REQUIRE(result.detection_source == "c2r-platform");
}

TEST_CASE("detect_outlook: MSI/Office Bitness value alone resolves bitness",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, L"SOFTWARE\\Microsoft\\Office\\16.0\\Outlook",
                 L"Bitness", RegistryView::kWow6432, L"x86");

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);
    REQUIRE(result.bitness == OutlookBitness::kX86);
    REQUIRE(result.detection_source == "office-bitness");
}

TEST_CASE("detect_outlook: falls back through Office versions for MSI Bitness",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    // Only the older 14.0 key is populated; 16.0/15.0 are absent.
    registry.set(RegistryRoot::kLocalMachine, L"SOFTWARE\\Microsoft\\Office\\14.0\\Outlook",
                 L"Bitness", RegistryView::kWin64, L"x64");

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);
    REQUIRE(result.bitness == OutlookBitness::kX64);
    REQUIRE(result.detection_source == "office-bitness");
}

TEST_CASE("detect_outlook: PE header alone resolves bitness when no registry hints exist",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, kAppPathsKey, L"", RegistryView::kWow6432,
                 L"C:\\Program Files\\Microsoft Office\\root\\Office16\\OUTLOOK.EXE");
    files.add(L"C:\\Program Files\\Microsoft Office\\root\\Office16\\OUTLOOK.EXE",
              make_pe_prefix(kMachineX64));

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);
    REQUIRE(result.bitness == OutlookBitness::kX64);
    REQUIRE(result.detection_source == "pe-header");
    REQUIRE(result.outlook_exe_path ==
            L"C:\\Program Files\\Microsoft Office\\root\\Office16\\OUTLOOK.EXE");
}

TEST_CASE("detect_outlook: PE header wins over a conflicting Click-to-Run hint",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, kC2rConfigKey, L"Platform", RegistryView::kWow6432,
                 L"x64");
    registry.set(RegistryRoot::kLocalMachine, kAppPathsKey, L"", RegistryView::kWow6432,
                 L"C:\\Office\\OUTLOOK.EXE");
    files.add(L"C:\\Office\\OUTLOOK.EXE", make_pe_prefix(kMachineX86));  // Conflicts with C2R.

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);
    REQUIRE(result.bitness == OutlookBitness::kX86);  // PE wins.
    REQUIRE(result.detection_source == "pe-header");
}

TEST_CASE("detect_outlook: value only visible in the Wow6432 view is still found",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, kC2rConfigKey, L"Platform", RegistryView::kWow6432,
                 L"x86");
    // Deliberately not set under kWin64.

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);
    REQUIRE(result.bitness == OutlookBitness::kX86);
    REQUIRE(result.detection_source == "c2r-platform");
}

TEST_CASE("detect_outlook: value only visible in the Win64 view is still found",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, kC2rConfigKey, L"Platform", RegistryView::kWin64,
                 L"x64");
    // Deliberately not set under kWow6432 - proves both views are tried, not
    // just the first one.

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);
    REQUIRE(result.bitness == OutlookBitness::kX64);
    REQUIRE(result.detection_source == "c2r-platform");
}

TEST_CASE("detect_outlook: exe found but unreadable PE retains the registry-derived bitness",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, L"SOFTWARE\\Microsoft\\Office\\16.0\\Outlook",
                 L"Bitness", RegistryView::kWow6432, L"x86");
    registry.set(RegistryRoot::kLocalMachine, kAppPathsKey, L"", RegistryView::kWow6432,
                 L"C:\\Office\\OUTLOOK.EXE");
    files.add(L"C:\\Office\\OUTLOOK.EXE");  // Exists, but empty/unreadable prefix.

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);
    REQUIRE(result.bitness == OutlookBitness::kX86);
    REQUIRE(result.detection_source == "office-bitness");  // Not overwritten by a failed PE parse.
}

TEST_CASE("detect_outlook: exe located via App Paths but no bitness signal at all",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, kAppPathsKey, L"", RegistryView::kWow6432,
                 L"C:\\Office\\OUTLOOK.EXE");
    files.add(L"C:\\Office\\OUTLOOK.EXE");  // Exists but unreadable/non-PE.

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);  // Path alone counts as classic-installed.
    REQUIRE(result.bitness == OutlookBitness::kUnknown);
    REQUIRE(result.detection_source == "app-paths");
}

TEST_CASE("detect_outlook: locates OUTLOOK.EXE via Click-to-Run InstallationPath fallback",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, kC2rConfigKey, L"InstallationPath",
                 RegistryView::kWow6432, L"C:\\Program Files\\Microsoft Office");
    files.add(L"C:\\Program Files\\Microsoft Office\\root\\Office16\\OUTLOOK.EXE",
              make_pe_prefix(kMachineX64));

    OutlookDetection result = detect_outlook(registry, files);

    REQUIRE(result.classic_installed);
    REQUIRE(result.outlook_exe_path ==
            L"C:\\Program Files\\Microsoft Office\\root\\Office16\\OUTLOOK.EXE");
    REQUIRE(result.bitness == OutlookBitness::kX64);
    REQUIRE(result.detection_source == "pe-header");
}

TEST_CASE("detect_outlook: new-outlook-only when no classic signal but an appx alias exists",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    std::wstring olk_path = L"C:\\Users\\Test\\AppData\\Local\\Microsoft\\WindowsApps\\olk.exe";
    files.add(olk_path);

    OutlookDetection result = detect_outlook(registry, files, {olk_path});

    REQUIRE_FALSE(result.classic_installed);
    REQUIRE(result.new_outlook_only);
    REQUIRE(result.bitness == OutlookBitness::kUnknown);
    REQUIRE(result.detection_source == "new-outlook-appx");
}

TEST_CASE("detect_outlook: new-outlook heuristic is skipped once classic is already installed",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;
    registry.set(RegistryRoot::kLocalMachine, kC2rConfigKey, L"Platform", RegistryView::kWow6432,
                 L"x64");
    std::wstring olk_path = L"C:\\Users\\Test\\AppData\\Local\\Microsoft\\WindowsApps\\olk.exe";
    files.add(olk_path);

    OutlookDetection result = detect_outlook(registry, files, {olk_path});

    REQUIRE(result.classic_installed);
    REQUIRE_FALSE(result.new_outlook_only);
    REQUIRE(result.detection_source == "c2r-platform");
}

TEST_CASE("detect_outlook: no new-outlook candidates and nothing classic -> not installed",
          "[launcher][detector]") {
    FakeRegistryReader registry;
    FakeFileProbe files;

    OutlookDetection result = detect_outlook(registry, files, {});

    REQUIRE_FALSE(result.classic_installed);
    REQUIRE_FALSE(result.new_outlook_only);
}
