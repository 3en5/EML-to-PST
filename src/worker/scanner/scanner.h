// WLM2PST - recursive EML source inventory (spec sections 6 and 9).
//
// Produces a deterministic manifest of every ".eml" file (case-insensitive)
// reachable from a source root, without following reparse points/junctions/
// symlinks and without recursing the call stack (iterative, explicit stack).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/errors/result.h"
#include "common/model.h"
#include "worker/scanner/file_system_probe.h"

namespace wlm2pst {

struct ScanOptions {
    std::wstring source_root;  // absolute native path
    size_t max_depth = 100;    // component depth below root; deeper => error entry
};

struct ScanStats {
    uint64_t total_bytes = 0;
    uint64_t folder_count = 0;
};

struct ScanIssues {
    std::vector<std::wstring> skipped_reparse_points;  // relative paths, sorted
    std::vector<std::wstring> too_deep;                // relative paths, sorted
    std::vector<std::wstring> unreadable;               // dirs that failed to enumerate, sorted
};

struct ScanResult {
    std::vector<ManifestEntry> entries;
    ScanStats stats;
    ScanIssues issues;
};

// Scans `options.source_root` for ".eml" files. `probe` decides which
// directory entries are reparse points that must not be traversed; pass
// make_default_probe() in production, a fake in tests.
//
// Returns an Error only for a fatal precondition (source root missing or not
// a directory, or an unexpected filesystem exception). Per-entry problems
// (unreadable subdirectory, depth overflow, reparse point) are non-fatal and
// recorded in ScanResult::issues instead.
Result<ScanResult> scan_source_tree(const ScanOptions& options, IFileSystemProbe& probe);

}  // namespace wlm2pst
