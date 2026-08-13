// WLM2PST - IConverterSession wrapper and EML stream plumbing (spec section 10).
#pragma once

#include "common/errors/result.h"
#include "worker/mapi/mapi_constants.h"
#include "worker/mapi/mapi_raii.h"
#include "worker/mapi/mapi_runtime.h"

#include <string>

namespace wlm2pst::mapi {

// Opens a read-only, deny-write IStream over a file (spec section 7).
Result<MapiPtr<IStream>> open_read_only_stream(const std::wstring& path);

// Self-deleting temporary file used for the attempt-2 normalized copy
// (spec sections 16, 24: secure name, restricted to the user's temp dir,
// removed after use).
class TempFile {
public:
    TempFile() = default;
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&& other) noexcept : path_(std::move(other.path_)) { other.path_.clear(); }
    ~TempFile();

    static Result<TempFile> create();
    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

private:
    std::wstring path_;
};

// Writes the attempt-2 normalized copy (repaired header + verbatim body) of
// `source_path` into a fresh temp file. Returns an unset optional-like error
// when normalization has nothing to change (caller then skips attempt 2).
Result<TempFile> write_normalized_copy(const std::wstring& source_path);

class MimeConverter {
public:
    MimeConverter() = default;
    MimeConverter(const MimeConverter&) = delete;
    MimeConverter& operator=(const MimeConverter&) = delete;
    MimeConverter(MimeConverter&&) = default;
    MimeConverter& operator=(MimeConverter&&) = default;

    // Obtains Outlook's converter (requires COM + MAPI up):
    // CoCreateInstance first; on Click-to-Run installs (class not in the
    // ordinary registry) the dll registered in the C2R virtual hive via
    // DllGetClassObject; finally the loaded MAPI module itself. Correctness
    // of whatever is obtained is enforced separately by
    // run_converter_self_test() during preflight.
    static Result<MimeConverter> create(MapiRuntime& runtime);

    // MIMEToMAPI with CCSF_SMTP | CCSF_INCLUDE_BCC | CCSF_GLOBAL_MESSAGE,
    // retrying without CCSF_GLOBAL_MESSAGE for Outlook versions that reject
    // the flag (spec sections 10, 33.16).
    Status convert(IStream& eml, IMessage& message);

private:
    MapiPtr<IConverterSession> session_;
};

}  // namespace wlm2pst::mapi
