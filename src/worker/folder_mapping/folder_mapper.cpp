// WLM2PST - folder mapping and normalization implementation. See
// folder_mapper.h and spec section 9.
#include "worker/folder_mapping/folder_mapper.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace wlm2pst {
namespace {

constexpr size_t kMaxDepthSegments = 100;
constexpr size_t kLongNameThreshold = 120;  // strictly greater than this triggers truncation
constexpr size_t kTruncatedNameLength = 100;

// File-local helpers (deliberately duplicated per-module rather than shared;
// see scanner.cpp for the sibling copy). Not dependent on common/unicode,
// which is developed in parallel and not yet available here.
namespace detail {

// Case-insensitive comparison: ASCII fold only, non-ASCII (including Hebrew,
// which has no case) compares by raw code point. Returns <0, 0, >0.
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

// 32-bit FNV-1a over the wchar_t sequence, treating each code unit as 4
// little-endian bytes regardless of platform wchar_t width. This is NOT true
// UTF-8 (documented deliberate simplification per spec): it only needs to be
// deterministic for a given original name on a given platform, to produce a
// stable disambiguating suffix for over-long folder names. Values may differ
// between UTF-16 (Windows) and UTF-32 (Linux test) builds for names
// containing astral-plane characters; this tool's names are BMP (Latin,
// Hebrew), so that does not arise in practice.
uint32_t fnv1a_hash(std::wstring_view s) noexcept {
    uint32_t h = 2166136261u;
    for (wchar_t wc : s) {
        uint32_t v = static_cast<uint32_t>(static_cast<std::make_unsigned_t<wchar_t>>(wc));
        for (int shift = 0; shift < 32; shift += 8) {
            uint8_t byte = static_cast<uint8_t>((v >> shift) & 0xFF);
            h ^= byte;
            h *= 16777619u;
        }
    }
    return h;
}

std::wstring to_hex8(uint32_t v) {
    static const wchar_t kDigits[] = L"0123456789abcdef";
    std::wstring out(8, L'0');
    for (int i = 7; i >= 0; --i) {
        out[static_cast<size_t>(i)] = kDigits[v & 0xF];
        v >>= 4;
    }
    return out;
}

}  // namespace detail

std::vector<std::wstring> split_components(std::wstring_view path) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= path.size()) {
        size_t pos = path.find(L'\\', start);
        std::wstring_view part =
            (pos == std::wstring_view::npos) ? path.substr(start) : path.substr(start, pos - start);
        if (!part.empty()) parts.emplace_back(part);
        if (pos == std::wstring_view::npos) break;
        start = pos + 1;
    }
    return parts;
}

std::wstring join_components(const std::vector<std::wstring>& parts) {
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += L'\\';
        out += parts[i];
    }
    return out;
}

// One segment's individual normalization: trims trailing spaces/periods,
// replaces control characters, falls back to "_" if left empty, and
// shortens+hashes if over-long. Appends machine-readable reason codes for
// every transform actually applied.
std::wstring normalize_segment(const std::wstring& original, std::vector<std::string>& reasons_out) {
    std::wstring s = original;

    bool had_control = false;
    for (wchar_t& ch : s) {
        if (ch <= 0x1F) {
            ch = L'_';
            had_control = true;
        }
    }
    if (had_control) reasons_out.emplace_back("control-chars");

    size_t end = s.size();
    while (end > 0 && (s[end - 1] == L' ' || s[end - 1] == L'.')) --end;
    bool had_trim = (end != s.size());
    s.resize(end);
    if (had_trim) reasons_out.emplace_back("trailing-trim");

    if (s.empty()) {
        s = L"_";
        if (!had_control && !had_trim) {
            // Name was empty/whitespace-only in some way not already covered;
            // still record a reason since the result diverges from the input.
            reasons_out.emplace_back("trailing-trim");
        }
    }

    if (s.size() > kLongNameThreshold) {
        std::wstring truncated = s.substr(0, kTruncatedNameLength);
        uint32_t hash = detail::fnv1a_hash(original);
        s = truncated + L"~" + detail::to_hex8(hash);
        reasons_out.emplace_back("too-long");
    }

    return s;
}

// One node of the source-directory trie (spec-driven ancestor closure of
// `dirs_with_direct_eml`), plus the synthetic "Root Messages" pseudo-child of
// the virtual root when the source root itself has direct EML.
struct TreeNode {
    std::wstring original_name;
    bool is_root_messages = false;
    std::vector<std::unique_ptr<TreeNode>> children;
};

TreeNode* find_or_create_child(TreeNode& parent, const std::wstring& name) {
    for (auto& child : parent.children) {
        if (!child->is_root_messages && child->original_name == name) return child.get();
    }
    parent.children.push_back(std::make_unique<TreeNode>());
    TreeNode* child = parent.children.back().get();
    child->original_name = name;
    return child;
}

struct WorkItem {
    TreeNode* node;
    std::wstring parent_source_relative;
    std::vector<std::wstring> parent_target_segments;
};

// Normalizes and collision-resolves one sibling group (children that share a
// common parent), emits their MappedFolder/RenamedFolder records into `plan`,
// and queues their own children for the next level.
void process_sibling_group(std::vector<TreeNode*> siblings, const std::wstring& parent_source_relative,
                            const std::vector<std::wstring>& parent_target_segments, FolderPlan& plan,
                            std::deque<WorkItem>& queue) {
    // Deterministic processing order: case-insensitive ordinal on the
    // original name, exact ordinal tie-break. This only needs to be
    // reproducible, not "first in the source listing" - collision resolution
    // below checks every already-assigned sibling name regardless of order.
    std::sort(siblings.begin(), siblings.end(), [](TreeNode* a, TreeNode* b) {
        int c = detail::compare_ci(a->original_name, b->original_name);
        if (c != 0) return c < 0;
        return a->original_name < b->original_name;
    });

    std::vector<std::wstring> used_names_ci;
    for (TreeNode* node : siblings) {
        std::vector<std::string> reasons;
        std::wstring base_name =
            node->is_root_messages ? node->original_name : normalize_segment(node->original_name, reasons);

        std::wstring final_name = base_name;
        int suffix = 2;
        bool collided = false;
        while (std::any_of(used_names_ci.begin(), used_names_ci.end(), [&](const std::wstring& used) {
                   return detail::compare_ci(final_name, used) == 0;
               })) {
            collided = true;
            final_name = base_name + L" (" + std::to_wstring(suffix) + L")";
            ++suffix;
        }
        if (collided) reasons.emplace_back("collision");
        used_names_ci.push_back(final_name);

        std::wstring node_source_relative;
        if (node->is_root_messages) {
            node_source_relative = L"";
        } else {
            node_source_relative = parent_source_relative.empty()
                                        ? node->original_name
                                        : parent_source_relative + L'\\' + node->original_name;
        }
        std::vector<std::wstring> node_target_segments = parent_target_segments;
        node_target_segments.push_back(final_name);

        plan.folders.push_back(MappedFolder{node_source_relative, node_target_segments});
        if (!reasons.empty()) {
            std::wstring target_relative_folder = join_components(node_target_segments);
            for (const std::string& reason : reasons) {
                plan.renames.push_back(RenamedFolder{node_source_relative, target_relative_folder, reason});
            }
        }

        for (auto& child : node->children) {
            queue.push_back(WorkItem{child.get(), node_source_relative, node_target_segments});
        }
    }
}

}  // namespace

Result<FolderPlan> build_folder_plan(const std::vector<std::wstring>& dirs_with_direct_eml) {
    bool has_root_direct = false;
    std::set<std::wstring> all_needed_dirs;  // nonempty dirs: given + every ancestor prefix

    for (const std::wstring& dir : dirs_with_direct_eml) {
        if (dir.empty()) {
            has_root_direct = true;
            continue;
        }
        std::vector<std::wstring> comps = split_components(dir);
        if (comps.size() > kMaxDepthSegments) {
            return make_error("build_folder_plan", "source folder path exceeds maximum supported depth");
        }
        std::wstring running;
        for (const std::wstring& comp : comps) {
            running = running.empty() ? comp : running + L'\\' + comp;
            all_needed_dirs.insert(running);
        }
    }

    TreeNode root;  // virtual; never itself emitted
    for (const std::wstring& dir : all_needed_dirs) {
        std::vector<std::wstring> comps = split_components(dir);
        TreeNode* cur = &root;
        for (const std::wstring& comp : comps) {
            cur = find_or_create_child(*cur, comp);
        }
    }
    if (has_root_direct) {
        root.children.push_back(std::make_unique<TreeNode>());
        root.children.back()->is_root_messages = true;
        root.children.back()->original_name = kRootMessagesFolder;
    }

    FolderPlan plan;
    std::deque<WorkItem> queue;

    {
        std::vector<TreeNode*> depth1;
        depth1.reserve(root.children.size());
        for (auto& child : root.children) depth1.push_back(child.get());
        process_sibling_group(std::move(depth1), L"", {}, plan, queue);
    }

    // Remaining levels: process one parent's children at a time as a sibling
    // group (breadth-first overall, so a parent's resolved name/path is
    // always available before its children need it).
    while (!queue.empty()) {
        WorkItem first = queue.front();
        queue.pop_front();
        std::vector<TreeNode*> siblings{first.node};
        const std::wstring parent_source_relative = first.parent_source_relative;
        const std::vector<std::wstring> parent_target_segments = first.parent_target_segments;
        // Distinct nodes always have distinct source_relative paths, so this
        // equality test safely groups exactly one parent's children even
        // though the queue interleaves different parents' entries.
        while (!queue.empty() && queue.front().parent_source_relative == parent_source_relative) {
            siblings.push_back(queue.front().node);
            queue.pop_front();
        }
        process_sibling_group(std::move(siblings), parent_source_relative, parent_target_segments, plan, queue);
    }

    // Deterministic output order: by target path, which also guarantees
    // parents sort before children (a parent's joined path is always a
    // strict prefix of its children's).
    std::sort(plan.folders.begin(), plan.folders.end(),
              [](const MappedFolder& a, const MappedFolder& b) {
                  return join_components(a.target_segments) < join_components(b.target_segments);
              });

    return plan;
}

}  // namespace wlm2pst
