// WLM2PST - recursive EML source inventory implementation. See scanner.h.
//
// Portable: no <windows.h>. Traversal is iterative (explicit stack) per spec
// section 9, never using std::filesystem::recursive_directory_iterator (which
// hides its own recursion and makes reparse-point / depth policy awkward to
// enforce per level).
#include "worker/scanner/scanner.h"

#include "common/paths/path_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace wlm2pst {
namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// File-local UTF-8 <-> wstring helpers.
//
// std::filesystem::path::wstring()/string() round-trip through the global C
// locale's codecvt facet, which defaults to the "C" locale and mangles any
// non-ASCII text (Hebrew folder/file names in particular) regardless of the
// actual on-disk encoding. To stay Unicode-correct without depending on
// common/unicode (developed in parallel, not yet available here), this module
// always talks to std::filesystem via the UTF-8-guaranteed std::u8string
// constructor/accessor and does its own UTF-8 <-> wstring conversion.
//
// wchar_t width varies by platform (UTF-16 on Windows, UTF-32 on Linux/test
// builds) so both encoder and decoder handle surrogate pairs explicitly only
// when compiled for the 2-byte case.
namespace detail {

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
    out.reserve(s.size() * 2);
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
        for (wchar_t wc : s) {
            append_utf8(out, static_cast<char32_t>(static_cast<uint32_t>(wc)));
        }
    }
    return out;
}

std::wstring utf8_to_wstring(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
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
            continue;  // invalid lead byte; skip
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

// Case-insensitive comparison: ASCII fold only (Hebrew and other non-ASCII
// text has no case and compares by raw code point, matching Windows ordinal
// semantics for the alphabet this tool's folder/file names actually use).
// Returns <0, 0, >0. Integration may later swap this for
// common/unicode::compare_ordinal_ignore_case.
int compare_ci(std::wstring_view a, std::wstring_view b) noexcept {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        wchar_t ca = a[i];
        wchar_t cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb - L'A' + L'a');
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return 0;
}

}  // namespace detail

// Constructs a std::filesystem::path from a wide string, always interpreting
// it as UTF-8 regardless of platform (never through the ANSI code page).
fs::path to_fs_path(std::wstring_view w) {
    std::string utf8 = detail::wstring_to_utf8(w);
    std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    return fs::path(u8);
}

// Converts a whole std::filesystem::path (component or full path, native
// separators preserved) back to a wide string, via guaranteed-UTF-8 u8string.
std::wstring from_fs_path(const fs::path& p) {
    std::u8string u8 = p.u8string();
    std::string bytes(reinterpret_cast<const char*>(u8.data()), u8.size());
    return detail::utf8_to_wstring(bytes);
}

Result<FileTimeUtc> last_write_time_utc(const fs::path& p) {
    std::error_code ec;
    fs::file_time_type ftime = fs::last_write_time(p, ec);
    if (ec) {
        return make_win32_error(static_cast<uint32_t>(ec.value()), "last_write_time",
                                 "failed to read last write time");
    }
    std::chrono::system_clock::time_point sys_time =
        std::chrono::clock_cast<std::chrono::system_clock>(ftime);
    using Ticks100ns = std::chrono::duration<int64_t, std::ratio<1, 10'000'000>>;
    int64_t unix_ticks_100ns =
        std::chrono::duration_cast<Ticks100ns>(sys_time.time_since_epoch()).count();
    return static_cast<FileTimeUtc>(unix_ticks_100ns + kFiletimeUnixEpoch);
}

struct PendingDir {
    fs::path absolute;
    std::wstring relative;  // L"" for the source root
    size_t depth;           // 0 for the source root
};

}  // namespace

Result<ScanResult> scan_source_tree(const ScanOptions& options, IFileSystemProbe& probe) {
    try {
        std::wstring root_wide = options.source_root;
#ifdef _WIN32
        // Extended-length prefix so traversal (and every derived child path)
        // works past MAX_PATH regardless of the machine-wide
        // FileSystem\LongPathsEnabled policy. Real WLM archives contain
        // deeply nested trees with paths well over 255 characters.
        root_wide = to_extended_length_path(std::move(root_wide));
#endif
        fs::path root_path = to_fs_path(root_wide);

        std::error_code root_ec;
        fs::file_status root_status = fs::status(root_path, root_ec);
        if (root_ec || !fs::is_directory(root_status)) {
            return make_error("scan_source_tree",
                               "source root does not exist or is not a directory");
        }

        ScanResult result;
        std::vector<PendingDir> stack;
        stack.push_back(PendingDir{root_path, L"", 0});

        while (!stack.empty()) {
            PendingDir current = std::move(stack.back());
            stack.pop_back();

            std::error_code iter_ec;
            fs::directory_iterator it(current.absolute, fs::directory_options::skip_permission_denied,
                                       iter_ec);
            if (iter_ec) {
                result.issues.unreadable.push_back(current.relative);
                continue;
            }

            fs::directory_iterator end_it;
            while (it != end_it) {
                fs::directory_entry entry = *it;

                std::error_code st_ec;
                fs::file_status st = entry.symlink_status(st_ec);
                if (st_ec) {
                    // Entry vanished or is unreadable; skip it, keep going.
                    std::error_code adv_ec;
                    it.increment(adv_ec);
                    if (adv_ec) {
                        result.issues.unreadable.push_back(current.relative);
                        break;
                    }
                    continue;
                }

                std::wstring name = from_fs_path(entry.path().filename());
                std::wstring child_relative =
                    current.relative.empty() ? name : current.relative + L'\\' + name;

                // Reparse points (symlinks, junctions) are never traversed and
                // never treated as EML files, whether they nominally point at
                // a directory or a file: resolving the target would require
                // following the link, which the traversal policy forbids.
                if (probe.is_reparse_point(entry.path())) {
                    result.issues.skipped_reparse_points.push_back(child_relative);
                } else if (fs::is_directory(st)) {
                    size_t child_depth = current.depth + 1;
                    if (child_depth > options.max_depth) {
                        result.issues.too_deep.push_back(child_relative);
                    } else {
                        stack.push_back(PendingDir{entry.path(), child_relative, child_depth});
                    }
                } else if (fs::is_regular_file(st)) {
                    std::wstring ext = from_fs_path(entry.path().extension());
                    if (detail::compare_ci(ext, L".eml") == 0) {
                        size_t file_depth = current.depth + 1;
                        if (file_depth > options.max_depth) {
                            result.issues.too_deep.push_back(child_relative);
                        } else {
                            std::error_code sz_ec;
                            uintmax_t size = fs::file_size(entry.path(), sz_ec);
                            Result<FileTimeUtc> ft = last_write_time_utc(entry.path());
                            if (sz_ec || !ft.ok()) {
                                result.issues.unreadable.push_back(child_relative);
                            } else {
                                ManifestEntry me;
                                me.relative_path = child_relative;
                                me.absolute_path = from_fs_path(entry.path());
                                me.size = static_cast<uint64_t>(size);
                                me.last_write_utc = ft.value();
                                result.stats.total_bytes += me.size;
                                result.entries.push_back(std::move(me));
                            }
                        }
                    }
                }
                // Other entry kinds (fifo, socket, device, etc.) are silently
                // ignored: not relevant to an EML source tree.

                std::error_code adv_ec;
                it.increment(adv_ec);
                if (adv_ec) {
                    result.issues.unreadable.push_back(current.relative);
                    break;
                }
            }
        }

        // Deterministic order: case-insensitive ordinal, exact ordinal tie-break.
        std::sort(result.entries.begin(), result.entries.end(),
                  [](const ManifestEntry& a, const ManifestEntry& b) {
                      int c = detail::compare_ci(a.relative_path, b.relative_path);
                      if (c != 0) return c < 0;
                      return a.relative_path < b.relative_path;
                  });
        auto sort_issue_list = [](std::vector<std::wstring>& v) {
            std::sort(v.begin(), v.end(), [](const std::wstring& a, const std::wstring& b) {
                int c = detail::compare_ci(a, b);
                if (c != 0) return c < 0;
                return a < b;
            });
        };
        sort_issue_list(result.issues.skipped_reparse_points);
        sort_issue_list(result.issues.too_deep);
        sort_issue_list(result.issues.unreadable);

        // folder_count: distinct relative directories (including "" for the
        // root, and every intermediate ancestor) that contain >=1 EML at or
        // below them.
        std::set<std::wstring> folders_with_eml;
        for (const ManifestEntry& e : result.entries) {
            folders_with_eml.insert(L"");
            size_t pos = 0;
            while (true) {
                size_t sep = e.relative_path.find(L'\\', pos);
                if (sep == std::wstring::npos) break;
                folders_with_eml.insert(e.relative_path.substr(0, sep));
                pos = sep + 1;
            }
        }
        result.stats.folder_count = folders_with_eml.size();

        return result;
    } catch (const std::exception& ex) {
        return make_error("scan_source_tree", std::string("unexpected exception: ") + ex.what());
    }
}

}  // namespace wlm2pst
