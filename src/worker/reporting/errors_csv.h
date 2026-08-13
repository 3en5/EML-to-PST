// WLM2PST - per-file errors CSV report (spec section 21).
//
// Portable: no Windows headers. The file is created lazily on the first
// appended row, so a job with zero failures produces no errors CSV at all.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "common/errors/result.h"

namespace wlm2pst {

// One row of the errors CSV. All fields must be privacy-safe (see
// docs/dev-conventions.md): no subjects, bodies, or addresses in `details`.
struct ErrorsCsvRow {
    std::wstring relative_source_path;
    std::string status;
    int attempt_count = 0;
    int32_t hresult = 0;       // 0 when not applicable.
    uint32_t win32_error = 0;  // 0 when not applicable.
    std::wstring target_folder;
    uint64_t source_size = 0;
    std::string sha256;
    std::string details;  // privacy-safe
};

// Writes RFC-4180 CSV rows, UTF-8 with a BOM (so Hebrew paths open correctly
// in Excel). The file is created only when the first row is appended; if no
// rows are ever appended, no file is written.
class ErrorsCsvWriter {
public:
    explicit ErrorsCsvWriter(std::wstring path) : path_(std::move(path)) {}
    ~ErrorsCsvWriter() { close(); }

    ErrorsCsvWriter(const ErrorsCsvWriter&) = delete;
    ErrorsCsvWriter& operator=(const ErrorsCsvWriter&) = delete;
    ErrorsCsvWriter(ErrorsCsvWriter&&) = default;
    ErrorsCsvWriter& operator=(ErrorsCsvWriter&&) = default;

    Status append(const ErrorsCsvRow& row);

    // True once the file has been created (i.e. at least one row appended).
    [[nodiscard]] bool created() const noexcept { return created_; }

    Status close();

private:
    Status ensure_created();

    std::wstring path_;
    std::ofstream ofs_;
    bool created_ = false;
};

}  // namespace wlm2pst
