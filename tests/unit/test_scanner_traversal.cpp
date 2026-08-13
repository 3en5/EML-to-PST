// WLM2PST - unit tests for worker/scanner (spec sections 6, 9, 28).
#include "worker/scanner/manifest_hash.h"
#include "worker/scanner/scanner.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using wlm2pst::DefaultFileSystemProbe;
using wlm2pst::ManifestEntry;
using wlm2pst::ScanOptions;
using wlm2pst::scan_source_tree;

// Test-local UTF-8 <-> wstring helpers (duplicated from the scanner's own
// file-local helpers - see scanner.cpp for the rationale: std::filesystem's
// wstring()/string() conversions go through the "C" locale by default and
// mangle non-ASCII text, so both production code and these tests always talk
// to std::filesystem via the UTF-8-guaranteed u8string constructor/accessor.

void append_utf8(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string wstring_to_utf8(std::wstring_view s) {
    std::string out;
    if constexpr (sizeof(wchar_t) == 2) {
        for (size_t i = 0; i < s.size(); ++i) {
            char32_t cp = static_cast<char32_t>(static_cast<uint16_t>(s[i]));
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()) {
                char32_t low = static_cast<char32_t>(static_cast<uint16_t>(s[i + 1]));
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            }
            append_utf8(out, cp);
        }
    } else {
        for (wchar_t wc : s) append_utf8(out, static_cast<char32_t>(static_cast<uint32_t>(wc)));
    }
    return out;
}

std::wstring utf8_to_wstring(std::string_view s) {
    std::wstring out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c0 = static_cast<unsigned char>(s[i]);
        char32_t cp = 0;
        size_t len = 1;
        if ((c0 & 0x80) == 0) {
            cp = c0;
            len = 1;
        } else if ((c0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = c0 & 0x1F;
            len = 2;
        } else if ((c0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = c0 & 0x0F;
            len = 3;
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = c0 & 0x07;
            len = 4;
        } else {
            ++i;
            continue;
        }
        bool valid = true;
        for (size_t k = 1; k < len; ++k) {
            unsigned char ck = static_cast<unsigned char>(s[i + k]);
            if ((ck & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (ck & 0x3F);
        }
        if (!valid) {
            ++i;
            continue;
        }
        i += len;
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp <= 0xFFFF) {
                out.push_back(static_cast<wchar_t>(cp));
            } else {
                cp -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
            }
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

fs::path wpath(const std::wstring& w) {
    std::string utf8 = wstring_to_utf8(w);
    std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    return fs::path(u8);
}

std::wstring wstr(const fs::path& p) {
    std::u8string u8 = p.u8string();
    std::string bytes(reinterpret_cast<const char*>(u8.data()), u8.size());
    return utf8_to_wstring(bytes);
}

// RAII temp directory, unique per instance, recursively removed on teardown.
class TempDir {
public:
    TempDir() {
        auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                fs::path("wlm2pst_scan_" + std::to_string(unique) + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
    static inline int counter_ = 0;
};

void write_file(const fs::path& dir, const std::wstring& name, std::string_view content = "x") {
    std::ofstream ofs(dir / wpath(name), std::ios::binary);
    ofs << content;
}

ScanOptions options_for(const TempDir& dir, size_t max_depth = 100) {
    ScanOptions opt;
    opt.source_root = wstr(dir.path());
    opt.max_depth = max_depth;
    return opt;
}

}  // namespace

TEST_CASE("scanner finds eml files case-insensitively and ignores others", "[scanner]") {
    TempDir root;
    fs::create_directories(root.path() / "sub");
    write_file(root.path(), L"a.eml");
    write_file(root.path(), L"b.EML");
    write_file(root.path() / "sub", L"c.Eml");
    write_file(root.path(), L"notes.txt");

    DefaultFileSystemProbe probe;
    auto result = scan_source_tree(options_for(root), probe);
    REQUIRE(result.ok());
    CHECK(result.value().entries.size() == 3);
    for (const auto& e : result.value().entries) {
        CHECK(e.relative_path.find(L".txt") == std::wstring::npos);
    }
}

TEST_CASE("scanner recurses nested directories and builds backslash-joined relative paths", "[scanner]") {
    TempDir root;
    fs::create_directories(root.path() / "A" / "B" / "C");
    write_file(root.path() / "A" / "B" / "C", L"deep.eml");
    write_file(root.path() / "A", L"shallow.eml");

    DefaultFileSystemProbe probe;
    auto result = scan_source_tree(options_for(root), probe);
    REQUIRE(result.ok());
    REQUIRE(result.value().entries.size() == 2);

    std::vector<std::wstring> rels;
    for (const auto& e : result.value().entries) rels.push_back(e.relative_path);
    CHECK(std::find(rels.begin(), rels.end(), L"A\\B\\C\\deep.eml") != rels.end());
    CHECK(std::find(rels.begin(), rels.end(), L"A\\shallow.eml") != rels.end());
}

TEST_CASE("scanner ignores empty directories with no eml descendants", "[scanner]") {
    TempDir root;
    fs::create_directories(root.path() / "empty");
    write_file(root.path(), L"root.eml");

    DefaultFileSystemProbe probe;
    auto result = scan_source_tree(options_for(root), probe);
    REQUIRE(result.ok());
    CHECK(result.value().entries.size() == 1);
    // Only the root itself counts; "empty" has no EML anywhere below it.
    CHECK(result.value().stats.folder_count == 1);
}

TEST_CASE("scanner produces deterministic case-insensitive ordinal ordering", "[scanner]") {
    TempDir root;
    // Created out of sorted order on purpose.
    write_file(root.path(), L"Zebra.eml");
    write_file(root.path(), L"apple.eml");
    write_file(root.path(), L"Mango.eml");
    write_file(root.path(), L"apple2.eml");

    DefaultFileSystemProbe probe;
    auto result = scan_source_tree(options_for(root), probe);
    REQUIRE(result.ok());

    std::vector<std::wstring> names;
    for (const auto& e : result.value().entries) names.push_back(e.relative_path);
    std::vector<std::wstring> expected = {L"apple.eml", L"apple2.eml", L"Mango.eml", L"Zebra.eml"};
    CHECK(names == expected);
}

TEST_CASE("scanner computes folder_count over distinct ancestor dirs with eml at or below", "[scanner]") {
    TempDir root;
    fs::create_directories(root.path() / "A");
    fs::create_directories(root.path() / "B" / "C");
    fs::create_directories(root.path() / "D");  // empty, must not count
    write_file(root.path() / "A", L"a1.eml");
    write_file(root.path() / "B" / "C", L"c1.eml");
    write_file(root.path(), L"root1.eml");

    DefaultFileSystemProbe probe;
    auto result = scan_source_tree(options_for(root), probe);
    REQUIRE(result.ok());
    CHECK(result.value().entries.size() == 3);
    // "" (root), "A", "B", "B\C" => 4; "D" excluded.
    CHECK(result.value().stats.folder_count == 4);
}

TEST_CASE("scanner enforces max_depth: too-deep dirs are not traversed, too-deep files are excluded",
          "[scanner]") {
    TempDir root;
    fs::path cur = root.path();
    for (int i = 1; i <= 101; ++i) {
        cur = cur / wpath(L"L" + std::to_wstring(i));
        fs::create_directories(cur);
    }

    fs::path l100 = root.path();
    for (int i = 1; i <= 100; ++i) l100 = l100 / wpath(L"L" + std::to_wstring(i));
    write_file(l100, L"atL100.eml");   // file depth 101: too deep, excluded
    write_file(cur, L"insideL101.eml");  // inside a dir that itself is too deep: never visited

    DefaultFileSystemProbe probe;
    auto result = scan_source_tree(options_for(root, 100), probe);
    REQUIRE(result.ok());
    CHECK(result.value().entries.empty());

    std::wstring expected_dir_path;
    std::wstring expected_file_path;
    for (int i = 1; i <= 101; ++i) {
        if (i > 1) expected_dir_path += L"\\";
        expected_dir_path += L"L" + std::to_wstring(i);
        if (i <= 100) {
            if (i > 1) expected_file_path += L"\\";
            expected_file_path += L"L" + std::to_wstring(i);
        }
    }
    expected_file_path += L"\\atL100.eml";

    const auto& too_deep = result.value().issues.too_deep;
    CHECK(std::find(too_deep.begin(), too_deep.end(), expected_dir_path) != too_deep.end());
    CHECK(std::find(too_deep.begin(), too_deep.end(), expected_file_path) != too_deep.end());
    // "insideL101.eml" must not appear anywhere: its containing directory was
    // never traversed.
    for (const auto& t : too_deep) {
        CHECK(t.find(L"insideL101") == std::wstring::npos);
    }
}

#ifndef _WIN32
TEST_CASE("scanner never follows a symlinked directory, even a self-referential loop", "[scanner]") {
    TempDir root;
    fs::create_directories(root.path() / "real");
    write_file(root.path() / "real", L"real.eml");

    std::error_code ec;
    fs::create_directory_symlink(root.path() / "real", root.path() / "real" / "loop", ec);
    REQUIRE_FALSE(ec);

    DefaultFileSystemProbe probe;
    auto result = scan_source_tree(options_for(root), probe);
    REQUIRE(result.ok());
    CHECK(result.value().entries.size() == 1);

    const auto& skipped = result.value().issues.skipped_reparse_points;
    CHECK(std::find(skipped.begin(), skipped.end(), L"real\\loop") != skipped.end());
}
#endif

TEST_CASE("scanner preserves Hebrew folder and file names", "[scanner]") {
    TempDir root;
    std::wstring folder = L"תיבת דואר";
    std::wstring file = L"הודעה.eml";
    fs::create_directories(root.path() / wpath(folder));
    write_file(root.path() / wpath(folder), file);

    DefaultFileSystemProbe probe;
    auto result = scan_source_tree(options_for(root), probe);
    REQUIRE(result.ok());
    REQUIRE(result.value().entries.size() == 1);
    CHECK(result.value().entries[0].relative_path == folder + L"\\" + file);
}

TEST_CASE("scanner reports a fatal error for a missing source root", "[scanner]") {
    ScanOptions opt;
    opt.source_root = L"/this/path/should/not/exist/wlm2pst_missing";
    DefaultFileSystemProbe probe;
    auto result = scan_source_tree(opt, probe);
    CHECK_FALSE(result.ok());
}

TEST_CASE("manifest serialization produces the canonical golden bytes", "[scanner]") {
    std::vector<ManifestEntry> entries;
    ManifestEntry e1;
    e1.relative_path = L"A\\one.eml";
    e1.size = 100;
    e1.last_write_utc = 132900000000000000LL;
    entries.push_back(e1);

    ManifestEntry e2;
    e2.relative_path = L"B\\two.eml";
    e2.size = 0;
    e2.last_write_utc = 0;
    entries.push_back(e2);

    std::string captured;
    wlm2pst::serialize_manifest_for_hash(entries, [&](const void* data, size_t len) {
        captured.append(static_cast<const char*>(data), len);
    });

    std::string expected =
        "A\\one.eml|100|132900000000000000\n"
        "B\\two.eml|0|0\n";
    CHECK(captured == expected);
}

TEST_CASE("manifest serialization encodes non-ascii relative paths as utf-8", "[scanner]") {
    std::vector<ManifestEntry> entries;
    ManifestEntry e;
    e.relative_path = L"תיבת דואר\\one.eml";
    e.size = 5;
    e.last_write_utc = 1;
    entries.push_back(e);

    std::string captured;
    wlm2pst::serialize_manifest_for_hash(entries, [&](const void* data, size_t len) {
        captured.append(static_cast<const char*>(data), len);
    });

    std::u8string expected_u8 = u8"תיבת דואר\\one.eml|5|1\n";
    std::string expected(reinterpret_cast<const char*>(expected_u8.data()), expected_u8.size());
    CHECK(captured == expected);
}
