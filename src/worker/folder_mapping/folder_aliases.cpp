// WLM2PST - sent/drafts folder alias matching. See folder_aliases.h.
#include "worker/folder_mapping/folder_aliases.h"

#include <cstddef>
#include <iterator>

namespace wlm2pst {
namespace {

// Alias lists are the single source of truth for this module (spec section
// 12). Store as constexpr arrays of raw string literals in one place.
constexpr const wchar_t* kSentAliases[] = {
    L"Sent",
    L"Sent Items",
    L"Sent Mail",
    L"Sent Messages",
    L"Items Sent",
    L"פריטים שנשלחו",
    L"דואר שנשלח",
    L"נשלח",
};

constexpr const wchar_t* kDraftsAliases[] = {
    L"Drafts",
    L"Draft",
    L"טיוטות",
    L"טיוטה",
};

// File-local case-insensitive exact-match comparison: ASCII fold only.
// Hebrew (and other non-ASCII text) has no case and compares by raw code
// point, which is correct for the alias lists above.
namespace detail {
bool equals_ci(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t ca = a[i];
        wchar_t cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb - L'A' + L'a');
        if (ca != cb) return false;
    }
    return true;
}
}  // namespace detail

bool matches_any(std::wstring_view component, const wchar_t* const* list, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (detail::equals_ci(component, list[i])) return true;
    }
    return false;
}

bool path_has_component_matching(std::wstring_view relative_path, bool (*pred)(std::wstring_view)) {
    size_t start = 0;
    while (start <= relative_path.size()) {
        size_t pos = relative_path.find(L'\\', start);
        std::wstring_view component = (pos == std::wstring_view::npos)
                                           ? relative_path.substr(start)
                                           : relative_path.substr(start, pos - start);
        if (!component.empty() && pred(component)) return true;
        if (pos == std::wstring_view::npos) break;
        start = pos + 1;
    }
    return false;
}

}  // namespace

bool is_sent_folder_component(std::wstring_view component) {
    return matches_any(component, kSentAliases, std::size(kSentAliases));
}

bool is_drafts_folder_component(std::wstring_view component) {
    return matches_any(component, kDraftsAliases, std::size(kDraftsAliases));
}

bool path_has_sent_component(std::wstring_view relative_path) {
    return path_has_component_matching(relative_path, is_sent_folder_component);
}

bool path_has_drafts_component(std::wstring_view relative_path) {
    return path_has_component_matching(relative_path, is_drafts_folder_component);
}

}  // namespace wlm2pst
