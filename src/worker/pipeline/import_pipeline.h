// WLM2PST - the conversion loop (spec sections 10, 12, 13, 16, 17, 19, 23).
//
// Fully portable: drives IPstSession (real MAPI on Windows, fakes in tests),
// the resume database, progress output, and the errors CSV. PST writes are
// inherently serialized: everything below runs on one thread (spec section 25).
#pragma once

#include "common/errors/result.h"
#include "common/exit_codes.h"
#include "common/model.h"
#include "worker/import_engine.h"
#include "worker/cancellation/cancel_token.h"
#include "worker/reporting/errors_csv.h"
#include "worker/reporting/progress.h"
#include "worker/resume/state_db.h"

#include <functional>
#include <string>
#include <vector>

namespace wlm2pst {

struct PipelineFatal {
    ExitCode exit_code;
    std::string message_utf8;  // privacy-safe
};

struct PipelineResult {
    JobCounters counters;               // failed_to_read = read failures only
    uint64_t failed_mapi = 0;           // fallback creation itself failed (systemic smell)
    bool cancelled = false;
    std::optional<PipelineFatal> fatal;
    std::vector<std::string> warnings;  // privacy-safe strings for the JSON report
};

struct PipelineContext {
    StateDb* db = nullptr;
    IPstSession* session = nullptr;
    CancelToken* cancel = nullptr;
    ProgressPrinter* progress = nullptr;
    ErrorsCsvWriter* errors_csv = nullptr;

    std::wstring source_root;      // absolute; row.relative_source_path appends to it
    std::string run_id;
    std::string tool_version;
    bool resume_mode = false;      // enables the crash-window recovery scan
    std::function<FileTimeUtc()> now_utc;

    // Commit the state DB every N processed files (spec section 17: batch
    // conservatively; the crash window is closed by resume recovery).
    size_t db_batch_size = 25;
    // Circuit breaker (spec section 23): consecutive files failing at store
    // level abort the run instead of generating thousands of fallbacks.
    int circuit_breaker_threshold = 10;
};

// Processes every PENDING row in the state database. Returns a fatal only for
// systemic conditions; per-message failures are recorded and skipped.
Result<PipelineResult> run_import_pipeline(PipelineContext& ctx);

}  // namespace wlm2pst
