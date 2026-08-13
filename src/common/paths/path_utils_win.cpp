// Windows-only volume queries. Kept out of path_utils.cpp so portable code
// never pulls in <windows.h> (see docs/dev-conventions.md).
#include "common/paths/path_utils.h"

#include "common/unicode/utf.h"
#include "common/win_compat.h"

namespace wlm2pst {

namespace {

// Reduces a UNC path (either "\\server\share\..." or its extended-length
// spelling "\\?\UNC\server\share\...") to a "\\server\share\" volume root
// suitable for GetDriveTypeW.
std::wstring unc_root(const std::wstring& absolute_path) {
    std::wstring normalized = normalize_backslashes(absolute_path);
    std::wstring_view view = normalized;

    std::wstring root = L"\\\\";
    if (is_extended_length_path(view)) {
        view = view.substr(4);  // strip "\\?\"
        if (view.size() >= 4 && compare_ordinal_ignore_case(view.substr(0, 4), L"UNC\\") == 0) {
            view = view.substr(4);
        }
        root += view;
    } else {
        root = normalized;
    }

    // Truncate after the first two path components ("server\share").
    size_t pos = 2;
    int separators_seen = 0;
    while (pos < root.size() && separators_seen < 2) {
        if (root[pos] == L'\\') {
            ++separators_seen;
        }
        ++pos;
    }
    root = root.substr(0, pos);
    if (root.empty() || root.back() != L'\\') {
        root += L'\\';
    }
    return root;
}

}  // namespace

Result<bool> is_on_local_fixed_drive(const std::wstring& absolute_path) {
    std::wstring root;
    if (auto drive = drive_letter_of(absolute_path)) {
        root = std::wstring(1, *drive) + L":\\";
    } else if (is_unc_path(absolute_path)) {
        root = unc_root(absolute_path);
    } else {
        return make_error("GetDriveTypeW", "path is neither drive-absolute nor UNC");
    }

    UINT type = GetDriveTypeW(root.c_str());
    return type == DRIVE_FIXED;
}

Result<uint64_t> available_free_bytes(const std::wstring& directory) {
    ULARGE_INTEGER free_available{};
    ULARGE_INTEGER total_bytes{};
    ULARGE_INTEGER total_free{};
    if (!GetDiskFreeSpaceExW(directory.c_str(), &free_available, &total_bytes, &total_free)) {
        return make_win32_error(GetLastError(), "GetDiskFreeSpaceExW",
                                 "failed to query available disk space");
    }
    return static_cast<uint64_t>(free_available.QuadPart);
}

}  // namespace wlm2pst
