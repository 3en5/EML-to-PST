#include "worker/mapi/mime_converter.h"

#include "worker/normalize/eml_normalizer.h"

#include <shlwapi.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace wlm2pst::mapi {

Result<MapiPtr<IStream>> open_read_only_stream(const std::wstring& path) {
    MapiPtr<IStream> stream;
    // Deny writers while the file is being converted (spec section 7); the
    // stream reads incrementally, never loading the file into memory.
    HRESULT hr = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
                                        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, stream.put());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "SHCreateStreamOnFileEx");
    }
    return stream;
}

TempFile::~TempFile() {
    if (!path_.empty()) {
        DeleteFileW(path_.c_str());
    }
}

Result<TempFile> TempFile::create() {
    wchar_t temp_dir[MAX_PATH + 1] = {};
    DWORD n = GetTempPathW(MAX_PATH + 1, temp_dir);
    if (n == 0 || n > MAX_PATH) {
        return make_win32_error(GetLastError(), "GetTempPathW");
    }
    // GetTempFileNameW creates the file atomically with a non-predictable-
    // enough unique name inside the per-user temp directory, whose default
    // ACL restricts access to the current user (spec section 24).
    wchar_t temp_path[MAX_PATH + 1] = {};
    if (GetTempFileNameW(temp_dir, L"wlm", 0, temp_path) == 0) {
        return make_win32_error(GetLastError(), "GetTempFileNameW");
    }
    TempFile file;
    file.path_ = temp_path;
    return file;
}

Result<TempFile> write_normalized_copy(const std::wstring& source_path) {
    // Read up to the header cap plus the remainder streamed in chunks.
    std::ifstream in(std::filesystem::path(source_path), std::ios::binary);
    if (!in) {
        return make_error("write_normalized_copy", "source EML could not be reopened");
    }
    std::string prefix(1024 * 1024, '\0');
    in.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    prefix.resize(static_cast<size_t>(in.gcount()));

    EmlNormalization norm = normalize_eml_header(prefix);
    if (!norm.changed) {
        return make_error("write_normalized_copy", "no conservative repair applicable");
    }

    auto temp = TempFile::create();
    if (!temp.ok()) return temp.error();

    std::ofstream out(std::filesystem::path(temp.value().path()),
                      std::ios::binary | std::ios::trunc);
    if (!out) {
        return make_error("write_normalized_copy", "temporary file could not be opened");
    }
    out.write(norm.normalized_header.data(),
              static_cast<std::streamsize>(norm.normalized_header.size()));

    // Body bytes are preserved exactly (spec section 16): copy verbatim from
    // the original boundary onward.
    if (norm.original_header_bytes < prefix.size()) {
        out.write(prefix.data() + norm.original_header_bytes,
                  static_cast<std::streamsize>(prefix.size() - norm.original_header_bytes));
    }
    std::vector<char> chunk(256 * 1024);
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        std::streamsize got = in.gcount();
        if (got <= 0) break;
        out.write(chunk.data(), got);
    }
    out.flush();
    if (!out) {
        return make_error("write_normalized_copy", "temporary file write failed");
    }
    return std::move(temp.value());
}

Result<MimeConverter> MimeConverter::create() {
    MimeConverter converter;
    HRESULT hr = CoCreateInstance(kClsidIConverterSession, nullptr, CLSCTX_INPROC_SERVER,
                                  kIidIConverterSession, converter.session_.put_void());
    if (FAILED(hr)) {
        return make_hresult_error(static_cast<int32_t>(hr), "CoCreateInstance(IConverterSession)",
                                  "Outlook MIME converter unavailable");
    }
    return converter;
}

Status MimeConverter::convert(IStream& eml, IMessage& message) {
    // CCSF_GLOBAL_MESSAGE enables international (EAI) header handling on
    // Outlook 2010+; older converters reject unknown flags, so retry without
    // it (spec sections 10, 33.16). Rewind the stream before each attempt.
    const ULONG flag_sets[] = {
        kCcsfSmtp | kCcsfIncludeBcc | kCcsfGlobalMessage,
        kCcsfSmtp | kCcsfIncludeBcc,
    };
    HRESULT last_hr = E_FAIL;
    for (ULONG flags : flag_sets) {
        LARGE_INTEGER zero{};
        HRESULT hr = eml.Seek(zero, STREAM_SEEK_SET, nullptr);
        if (FAILED(hr)) {
            return make_hresult_error(static_cast<int32_t>(hr), "IStream::Seek");
        }
        hr = session_->MIMEToMAPI(&eml, &message, nullptr, flags);
        if (SUCCEEDED(hr)) {
            return Status::success();
        }
        last_hr = hr;
        if (hr != E_INVALIDARG && hr != MAPI_E_UNKNOWN_FLAGS) {
            break;  // real conversion failure; dropping flags will not help
        }
    }
    return make_hresult_error(static_cast<int32_t>(last_hr), "IConverterSession::MIMEToMAPI");
}

}  // namespace wlm2pst::mapi
