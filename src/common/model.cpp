#include "common/model.h"

namespace wlm2pst {

const char* to_string(FileImportStatus status) noexcept {
    switch (status) {
        case FileImportStatus::kPending: return "PENDING";
        case FileImportStatus::kImported: return "IMPORTED";
        case FileImportStatus::kPreservedAsAttachment: return "PRESERVED_AS_ATTACHMENT";
        case FileImportStatus::kFailedSourceRead: return "FAILED_SOURCE_READ";
        case FileImportStatus::kFailedMapi: return "FAILED_MAPI";
    }
    return "UNKNOWN";
}

bool file_import_status_from_string(const std::string& text, FileImportStatus& out) noexcept {
    if (text == "PENDING") { out = FileImportStatus::kPending; return true; }
    if (text == "IMPORTED") { out = FileImportStatus::kImported; return true; }
    if (text == "PRESERVED_AS_ATTACHMENT") { out = FileImportStatus::kPreservedAsAttachment; return true; }
    if (text == "FAILED_SOURCE_READ") { out = FileImportStatus::kFailedSourceRead; return true; }
    if (text == "FAILED_MAPI") { out = FileImportStatus::kFailedMapi; return true; }
    return false;
}

const char* to_string(ValidationStatus status) noexcept {
    switch (status) {
        case ValidationStatus::kValidated: return "VALIDATED";
        case ValidationStatus::kValidatedWithFallbacks: return "VALIDATED_WITH_FALLBACKS";
        case ValidationStatus::kValidationFailed: return "VALIDATION_FAILED";
    }
    return "UNKNOWN";
}

}  // namespace wlm2pst
