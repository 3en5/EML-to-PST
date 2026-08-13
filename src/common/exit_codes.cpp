#include "common/exit_codes.h"

namespace wlm2pst {

const char* to_string(ExitCode code) noexcept {
    switch (code) {
        case ExitCode::kSuccess: return "SUCCESS";
        case ExitCode::kSuccessWithFallbacks: return "SUCCESS_WITH_FALLBACKS";
        case ExitCode::kSuccessWithReadFailures: return "SUCCESS_WITH_READ_FAILURES";
        case ExitCode::kInvalidArguments: return "INVALID_ARGUMENTS";
        case ExitCode::kInvalidSource: return "INVALID_SOURCE";
        case ExitCode::kOutputExists: return "OUTPUT_EXISTS";
        case ExitCode::kInsufficientDiskSpace: return "INSUFFICIENT_DISK_SPACE";
        case ExitCode::kOutlookUnavailable: return "OUTLOOK_UNAVAILABLE";
        case ExitCode::kBitnessMismatch: return "BITNESS_MISMATCH";
        case ExitCode::kPstProviderFailure: return "PST_PROVIDER_FAILURE";
        case ExitCode::kPstSizeCeilingExceeded: return "PST_SIZE_CEILING_EXCEEDED";
        case ExitCode::kOutputWriteFailure: return "OUTPUT_WRITE_FAILURE";
        case ExitCode::kValidationFailed: return "VALIDATION_FAILED";
        case ExitCode::kStateDbIncompatible: return "STATE_DB_INCOMPATIBLE";
        case ExitCode::kSourceChanged: return "SOURCE_CHANGED";
        case ExitCode::kResumeIntegrityError: return "RESUME_INTEGRITY_ERROR";
        case ExitCode::kProfileCleanupFailure: return "PROFILE_CLEANUP_FAILURE";
        case ExitCode::kCancelled: return "CANCELLED";
    }
    return "UNKNOWN";
}

}  // namespace wlm2pst
