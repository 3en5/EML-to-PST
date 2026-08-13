// WLM2PST - canonical manifest serialization for resume-integrity hashing
// (spec section 17: "manifest_hash").
//
// This module intentionally does not depend on a concrete SHA-256
// implementation (common/hashing is developed in parallel and not yet
// available here): callers supply a `sink` that receives the canonical byte
// stream and feed it into whatever hasher they have, then finalize/format the
// digest themselves.
#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "common/model.h"

namespace wlm2pst {

// Serializes `sorted_entries` (already sorted, e.g. by scan_source_tree) into
// the canonical byte form used for the manifest hash: for each entry, one
// UTF-8 line
//
//   <relative_path_utf8>|<size>|<last_write_utc>\n
//
// `sink` is invoked once per line with a pointer to UTF-8 bytes and their
// length; it does not need to buffer between calls. This function does not
// re-sort `sorted_entries` - callers must pass them in the deterministic
// order defined by the scanner.
void serialize_manifest_for_hash(const std::vector<ManifestEntry>& sorted_entries,
                                  const std::function<void(const void*, size_t)>& sink);

}  // namespace wlm2pst
