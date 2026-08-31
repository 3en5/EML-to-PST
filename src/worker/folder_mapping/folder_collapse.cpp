#include "worker/folder_mapping/folder_collapse.h"

#include "common/unicode/utf.h"

#include <algorithm>
#include <cwctype>

namespace wlm2pst {
namespace {

std::wstring to_lower(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) out.push_back(static_cast<wchar_t>(std::towlower(c)));
    return out;
}

// Strips a trailing Windows Live Mail duplicate marker: "Inbox (1)" -> "Inbox".
std::wstring_view without_dup_suffix(std::wstring_view s) {
    if (s.size() < 4 || s.back() != L')') return s;
    size_t open = s.rfind(L'(');
    if (open == std::wstring_view::npos || open == 0) return s;
    for (size_t i = open + 1; i + 1 < s.size(); ++i) {
        if (!std::iswdigit(s[i])) return s;
    }
    size_t end = open;
    while (end > 0 && s[end - 1] == L' ') --end;
    return end ? s.substr(0, end) : s;
}

// Windows Live Mail's own bookkeeping folders. Hebrew and English UI variants
// are both present in real archives.
const wchar_t* const kBuiltinContainers[] = {
    L"storage folders",
    L"imported folder",
    L"recovered items",
    L"פריטים משוחזרים",  // "recovered items"
    L"תיקיות אחסון",                    // "storage folders"
    L"תיקיות מקומיות",        // "local folders"
};

// A recovery-event folder: "25-12-2023   f3", "08-30-2022  90b",
// "2023-12-25-074633". Digits and separators only, plus an optional short
// hexish tag - never a name a person chose.
bool is_recovery_timestamp(std::wstring_view s) {
    size_t digits = 0, dashes = 0;
    for (wchar_t c : s) {
        if (std::iswdigit(c)) ++digits;
        else if (c == L'-') ++dashes;
        else if (c == L' ') continue;
        else if (std::iswalnum(c)) continue;  // trailing tag such as "f3" / "90b"
        else return false;
    }
    return dashes >= 2 && digits >= 6;
}

bool matches_pattern(std::wstring_view name_lower, std::wstring_view pattern_lower) {
    if (!pattern_lower.empty() && pattern_lower.back() == L'*') {
        pattern_lower.remove_suffix(1);
        return name_lower.size() >= pattern_lower.size() &&
               name_lower.compare(0, pattern_lower.size(), pattern_lower) == 0;
    }
    return name_lower == pattern_lower;
}

std::vector<std::wstring> split(const std::wstring& path) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= path.size()) {
        size_t sep = path.find(L'\\', start);
        if (sep == std::wstring::npos) {
            if (start < path.size()) parts.push_back(path.substr(start));
            break;
        }
        if (sep > start) parts.push_back(path.substr(start, sep - start));
        start = sep + 1;
    }
    return parts;
}

std::wstring join(const std::vector<std::wstring>& parts) {
    std::wstring out;
    for (const auto& p : parts) {
        if (!out.empty()) out += L'\\';
        out += p;
    }
    return out;
}

}  // namespace

bool is_container_segment(const std::wstring& segment, const CollapseOptions& options) {
    const std::wstring lower = to_lower(without_dup_suffix(segment));
    if (options.collapse_builtin_containers) {
        for (const wchar_t* c : kBuiltinContainers) {
            if (lower == c) return true;
        }
        if (is_recovery_timestamp(segment)) return true;
    }
    for (const auto& pattern : options.extra_patterns) {
        if (matches_pattern(lower, to_lower(pattern))) return true;
    }
    return false;
}

std::string describe_collapse_options(const CollapseOptions& options) {
    std::string out = options.collapse_builtin_containers ? "builtin=1" : "builtin=0";
    out += options.merge_case_variants ? ";case=1" : ";case=0";
    std::vector<std::wstring> extras;
    extras.reserve(options.extra_patterns.size());
    for (const auto& pattern : options.extra_patterns) extras.push_back(to_lower(pattern));
    std::sort(extras.begin(), extras.end());  // after lowering, so case cannot reorder
    for (const auto& pattern : extras) {
        out += ";extra=" + utf8_from_wide(pattern);
    }
    return out;
}

std::map<std::wstring, std::wstring> build_collapse_map(
    const std::vector<std::wstring>& source_dirs, const CollapseOptions& options) {
    // Sorted input makes the "first spelling wins" case rule deterministic.
    std::vector<std::wstring> sorted(source_dirs.begin(), source_dirs.end());
    std::sort(sorted.begin(), sorted.end());

    std::map<std::wstring, std::wstring> canonical;  // lowercased logical path -> chosen spelling
    std::map<std::wstring, std::wstring> result;

    for (const std::wstring& dir : sorted) {
        std::vector<std::wstring> kept;
        for (const std::wstring& segment : split(dir)) {
            if (is_container_segment(segment, options)) continue;
            std::wstring name = segment;
            if (options.merge_case_variants) {
                std::vector<std::wstring> probe = kept;
                probe.push_back(name);
                const std::wstring key = to_lower(join(probe));
                auto it = canonical.find(key);
                if (it != canonical.end()) {
                    name = it->second;  // reuse the spelling already chosen
                } else {
                    canonical.emplace(key, name);
                }
            }
            kept.push_back(std::move(name));
        }
        result.emplace(dir, join(kept));
    }
    return result;
}

}  // namespace wlm2pst
