#include "common/paths/path_utils.h"

#include <vector>

#include "common/unicode/utf.h"

namespace wlm2pst {

namespace {

bool starts_with_ci(std::wstring_view s, std::wstring_view prefix) noexcept {
    if (s.size() < prefix.size()) return false;
    return compare_ordinal_ignore_case(s.substr(0, prefix.size()), prefix) == 0;
}

constexpr std::wstring_view kExtendedPrefix = LR"(\\?\)";
constexpr std::wstring_view kExtendedUncPrefix = LR"(\\?\UNC\)";

}  // namespace

bool is_extended_length_path(std::wstring_view path) noexcept {
    return starts_with_ci(path, kExtendedPrefix);
}

bool is_unc_path(std::wstring_view path) noexcept {
    if (starts_with_ci(path, kExtendedUncPrefix)) {
        return true;
    }
    if (is_extended_length_path(path)) {
        // \\?\C:\... etc: extended-length but not UNC.
        return false;
    }
    // Plain "\\server\share": exactly two leading backslashes followed by
    // at least one more character.
    return path.size() > 2 && path[0] == L'\\' && path[1] == L'\\';
}

std::optional<wchar_t> drive_letter_of(std::wstring_view path) noexcept {
    std::wstring_view p = path;
    if (is_extended_length_path(p)) {
        p = p.substr(kExtendedPrefix.size());
    }
    if (p.size() >= 2 && p[1] == L':') {
        wchar_t c = p[0];
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) {
            return c;
        }
    }
    return std::nullopt;
}

std::wstring to_extended_length_path(std::wstring path) {
    if (is_extended_length_path(path)) {
        return path;
    }
    if (is_unc_path(path)) {
        // "\\server\share..." -> "\\?\UNC\server\share..."
        std::wstring_view rest = std::wstring_view(path).substr(2);
        return std::wstring(kExtendedUncPrefix) + std::wstring(rest);
    }
    if (drive_letter_of(path).has_value() && path.size() >= 3 && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/')) {
        return std::wstring(kExtendedPrefix) + path;
    }
    return path;
}

bool has_pst_extension(std::wstring_view path) noexcept {
    constexpr std::wstring_view kExt = L".pst";
    if (path.size() < kExt.size()) return false;
    return compare_ordinal_ignore_case(path.substr(path.size() - kExt.size()), kExt) == 0;
}

std::wstring normalize_backslashes(std::wstring path) {
    for (auto& c : path) {
        if (c == L'/') c = L'\\';
    }

    std::wstring out;
    out.reserve(path.size());

    size_t i = 0;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        // Preserve exactly one leading UNC/extended-length marker pair, and
        // collapse any additional backslashes that immediately follow it.
        out += L"\\\\";
        i = 2;
        while (i < path.size() && path[i] == L'\\') {
            ++i;
        }
    }

    bool prev_backslash = false;
    for (; i < path.size(); ++i) {
        wchar_t c = path[i];
        if (c == L'\\') {
            if (prev_backslash) continue;
            prev_backslash = true;
            out += c;
        } else {
            prev_backslash = false;
            out += c;
        }
    }
    return out;
}

namespace {

// Splits a normalized path into components for containment comparison,
// stripping a leading \\?\ (and \\?\UNC\) prefix first so that extended and
// non-extended spellings of the same path compare equal.
std::vector<std::wstring> split_components(std::wstring_view path) {
    std::wstring normalized = normalize_backslashes(std::wstring(path));
    std::wstring_view view = normalized;

    if (is_extended_length_path(view)) {
        view = view.substr(kExtendedPrefix.size());
        if (starts_with_ci(view, L"UNC\\")) {
            view = view.substr(4);
        }
    }

    while (!view.empty() && view.back() == L'\\') {
        view.remove_suffix(1);
    }

    std::vector<std::wstring> parts;
    size_t start = 0;
    for (size_t i = 0; i <= view.size(); ++i) {
        if (i == view.size() || view[i] == L'\\') {
            if (i > start) {
                parts.emplace_back(view.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    return parts;
}

}  // namespace

bool is_lexically_within(std::wstring_view base, std::wstring_view candidate) {
    std::vector<std::wstring> base_parts = split_components(base);
    std::vector<std::wstring> candidate_parts = split_components(candidate);
    if (candidate_parts.size() < base_parts.size()) {
        return false;
    }
    for (size_t i = 0; i < base_parts.size(); ++i) {
        if (compare_ordinal_ignore_case(base_parts[i], candidate_parts[i]) != 0) {
            return false;
        }
    }
    return true;
}

}  // namespace wlm2pst
