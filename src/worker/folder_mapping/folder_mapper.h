// WLM2PST - maps source relative folder paths to PST folder paths (spec
// section 9).
#pragma once

#include <string>
#include <vector>

#include "common/errors/result.h"
#include "common/model.h"

namespace wlm2pst {

// One folder that must exist under the tool root in the PST.
struct MappedFolder {
    std::wstring source_relative;               // L"" for the source root
    std::vector<std::wstring> target_segments;   // normalized names under the tool root, root-first
};

struct FolderPlan {
    std::vector<MappedFolder> folders;   // deterministic order: target path, parents before children
    std::vector<RenamedFolder> renames;  // one record per applied normalization/collision
};

// Synthetic top-level folder that holds EML files found directly under the
// selected source root (spec section 9, rule 4).
inline constexpr const wchar_t* kRootMessagesFolder = L"Root Messages";

// Builds the folder plan for a set of source directories known to directly
// contain at least one EML file. `dirs_with_direct_eml` uses L"" to mean the
// source root itself; every other entry is a '\\'-separated relative path
// with no leading or trailing separator, matching ManifestEntry::relative_path
// prefixes. Ancestor directories (needed purely as containers, with no direct
// EML of their own) are derived automatically; callers do not need to list
// them.
//
// Returns an Error (operation "build_folder_plan") if any input path exceeds
// 100 path segments.
Result<FolderPlan> build_folder_plan(const std::vector<std::wstring>& dirs_with_direct_eml);

}  // namespace wlm2pst
