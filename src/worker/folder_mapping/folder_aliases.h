// WLM2PST - centralized sent/drafts folder alias lists (spec section 12).
//
// Matching is exact-match on a single path COMPONENT (not a substring match:
// a folder literally named "Sentinel" is not a sent folder), case-insensitive
// ordinal. These lists are the single source of truth; do not duplicate them
// elsewhere.
#pragma once

#include <string_view>

namespace wlm2pst {

// True if `component` (one path segment, no separators) is a recognized
// "sent" folder name.
bool is_sent_folder_component(std::wstring_view component);

// True if `component` (one path segment, no separators) is a recognized
// "drafts" folder name.
bool is_drafts_folder_component(std::wstring_view component);

// True if any '\\'-separated component of `relative_path` is a recognized
// "sent" folder name.
bool path_has_sent_component(std::wstring_view relative_path);

// True if any '\\'-separated component of `relative_path` is a recognized
// "drafts" folder name.
bool path_has_drafts_component(std::wstring_view relative_path);

}  // namespace wlm2pst
