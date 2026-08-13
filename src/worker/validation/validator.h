// WLM2PST - mandatory post-conversion PST validation (spec section 20).
//
// Runs against a FRESH validation session (new temporary profile attached to
// the finished PST) and the state database. Portable: exercised in unit tests
// with a fake IPstSession.
#pragma once

#include "common/errors/result.h"
#include "common/model.h"
#include "worker/import_engine.h"
#include "worker/resume/state_db.h"

#include <string>
#include <vector>

namespace wlm2pst {

struct ValidationIssue {
    std::wstring folder;                // target folder path (may be empty)
    std::wstring relative_source_path;  // offending source file (may be empty)
    std::string detail;                 // privacy-safe technical description
};

struct ValidationOutcome {
    ValidationStatus status = ValidationStatus::kValidationFailed;
    std::vector<ValidationIssue> issues;
    uint64_t folders_checked = 0;
    uint64_t messages_checked = 0;
};

// Verifies: every expected folder exists; per-folder message counts match the
// database; every IMPORTED row has exactly one tracked message; every
// PRESERVED_AS_ATTACHMENT row has exactly one fallback message carrying an
// attachment; no tool-owned duplicates for the same run id + source path.
Result<ValidationOutcome> validate_pst(IPstSession& session, StateDb& db,
                                       const std::string& run_id);

}  // namespace wlm2pst
