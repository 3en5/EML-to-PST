// WLM2PST - collapses Windows Live Mail container folders so the PST tree is
// navigable.
//
// Windows Live Mail stores mail under several layers of its own bookkeeping
// folders ("Storage Folders", "Imported Folder", "Recovered Items" plus a
// timestamped folder per recovery event, often nested more than once). Those
// carry no information a person navigates by, yet they push real mail four or
// more levels down and are reproduced verbatim in the PST.
//
// This module rewrites a source-relative folder path into the logical folder
// path a user would expect, by dropping pass-through segments. It also
// canonicalises case so that sibling folders differing only in case (WLM
// happily creates both "Sent Items" and "Sent items") merge into one instead
// of colliding.
//
// Nothing is lost: only container segments disappear, and messages keep their
// position relative to every meaningful folder.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace wlm2pst {

struct CollapseOptions {
    // Drop the built-in Windows Live Mail container folders.
    bool collapse_builtin_containers = true;
    // Extra folder names to treat as pass-through. Case-insensitive; a
    // trailing '*' matches any suffix (e.g. L"Zahav.net*").
    std::vector<std::wstring> extra_patterns;
    // Merge folders whose names differ only by case into the first spelling
    // seen (deterministic: inputs are sorted before processing).
    bool merge_case_variants = true;
};

// Maps every input source-relative directory to its collapsed logical path.
// Directories that collapse to the same logical path share one PST folder.
// Input order does not matter; the result is deterministic.
std::map<std::wstring, std::wstring> build_collapse_map(
    const std::vector<std::wstring>& source_dirs, const CollapseOptions& options);

// True when `segment` is a pass-through container under `options`. Exposed for
// unit tests.
bool is_container_segment(const std::wstring& segment, const CollapseOptions& options);

// Stable UTF-8 description of `options`, recorded with the job so that a
// resume cannot silently mix two folder layouts in one PST.
std::string describe_collapse_options(const CollapseOptions& options);

}  // namespace wlm2pst
