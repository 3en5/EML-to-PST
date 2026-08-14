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

// How the converter is driven. The working combination differs between
// Outlook installations (field finding: on Click-to-Run, a converter used
// per Microsoft's "Importing MIME email" sample sequence including
// SetAdrBook converts correctly, while a bare object degraded to
// treat-stream-as-text). The preflight self-test CALIBRATES this: it tries
// the variants in a fixed order against a bundled fixture with full content
// checks, and the first one that provably converts is used for the run.
struct ConverterConfig {
    bool use_address_book = true;  // IMAPISession::OpenAddressBook + SetAdrBook
    ULONG flags = kCcsfSmtp | kCcsfIncludeBcc | kCcsfGlobalMessage;
    // Engine selection: Outlook's IConverterSession when it provably works;
    // otherwise Windows' own MimeOle parser (inetcomm.dll) - the engine
    // Windows Live Mail itself wrote these EML files with. Chosen by the
    // preflight calibration, never guessed (see mimeole_importer.h).
    bool use_mimeole = false;
};

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
    // of whatever is obtained is enforced separately by the preflight
    // self-test/calibration and the per-message integrity gate.
    static Result<MimeConverter> create(MapiRuntime& runtime);

    // Attaches the session's address book (Microsoft's documented import
    // sequence calls this before MIMEToMAPI; MFCMAPI does likewise).
    Status set_address_book(LPADRBOOK address_book);

    // MIMEToMAPI with the given CCSF flags. The exact HRESULT is logged when
    // it is not plain S_OK (field diagnosis: a converter can "succeed"
    // without converting; the integrity gate is the behavioral arbiter).
    Status convert(IStream& eml, IMessage& message, ULONG flags);

private:
    MapiPtr<IConverterSession> session_;
};

}  // namespace wlm2pst::mapi
