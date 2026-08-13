// WLM2PST - centralized process exit codes (spec section 22).
// Keep this the ONLY place exit codes are defined. README.md documents the same table.
#pragma once

namespace wlm2pst {

enum class ExitCode : int {
    kSuccess = 0,                  // Conversion and validation completed with no warnings.
    kSuccessWithFallbacks = 2,     // Completed and validated, some EMLs preserved as fallback attachments.
    kSuccessWithReadFailures = 3,  // Completed and validated, some source files could not be read.
    kInvalidArguments = 10,        // Invalid command-line arguments.
    kInvalidSource = 11,           // Invalid source directory or no EML files found.
    kOutputExists = 12,            // Output already exists or cannot be safely overwritten.
    kInsufficientDiskSpace = 13,   // Insufficient free disk space.
    kOutlookUnavailable = 14,      // Classic Outlook or Extended MAPI is unavailable.
    kBitnessMismatch = 15,         // Worker and Outlook bitness mismatch.
    kPstProviderFailure = 16,      // Unicode PST provider could not be created or configured.
    kPstSizeCeilingExceeded = 17,  // Estimated output exceeds the safe single-PST ceiling.
    kOutputWriteFailure = 18,      // Disk full or output write failure during conversion.
    kValidationFailed = 19,        // PST validation failed.
    kStateDbIncompatible = 20,     // State database is incompatible with the PST or source manifest.
    kSourceChanged = 21,           // Source changed after the manifest was created.
    kResumeIntegrityError = 22,    // Integrity error during resume recovery.
    kProfileCleanupFailure = 23,   // Temporary profile creation or cleanup failed critically.
    kCancelled = 130,              // User cancelled with Ctrl+C.
};

// Short stable identifier for logs/reports (never localized).
const char* to_string(ExitCode code) noexcept;

}  // namespace wlm2pst
