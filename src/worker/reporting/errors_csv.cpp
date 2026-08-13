#include "worker/reporting/errors_csv.h"

#include <cstdio>
#include <exception>
#include <filesystem>

namespace wlm2pst {

namespace {

constexpr const char* kHeader =
    "relative_source_path,status,attempt_count,hresult,win32_error,target_folder,"
    "source_size,sha256,details";

// File-local, surrogate-correct wide -> UTF-8 conversion. Duplicated
// per-translation-unit on purpose (see docs/dev-conventions.md): this module
// must not depend on common/unicode, which is developed in parallel.
void append_utf8_codepoint(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string wide_to_utf8(const std::wstring& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp;
        if constexpr (sizeof(wchar_t) == 2) {
            uint32_t unit = static_cast<uint16_t>(s[i]);
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < s.size()) {
                uint32_t low = static_cast<uint16_t>(s[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                } else {
                    cp = 0xFFFD;
                    i += 1;
                }
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                cp = 0xFFFD;
                i += 1;
            } else {
                cp = unit;
                i += 1;
            }
        } else {
            cp = static_cast<uint32_t>(s[i]);
            i += 1;
        }
        append_utf8_codepoint(out, cp);
    }
    return out;
}

// RFC-4180: quote a field if it contains a comma, quote, CR, or LF; double
// any embedded quotes.
std::string csv_escape(const std::string& field) {
    const bool needs_quote = field.find_first_of(",\"\r\n") != std::string::npos;
    if (!needs_quote) return field;
    std::string out;
    out.reserve(field.size() + 2);
    out += '"';
    for (char c : field) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

std::string format_hresult(int32_t hresult) {
    if (hresult == 0) return {};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<uint32_t>(hresult));
    return std::string(buf);
}

std::string format_win32(uint32_t win32_error) {
    if (win32_error == 0) return {};
    return std::to_string(win32_error);
}

}  // namespace

Status ErrorsCsvWriter::ensure_created() {
    if (created_) return Status::success();
    try {
        std::filesystem::path fs_path(path_);
        ofs_.open(fs_path, std::ios::binary | std::ios::trunc);
        if (!ofs_) {
            return Status(make_error("ErrorsCsvWriter::ensure_created", "failed to open errors CSV for writing"));
        }
        static constexpr unsigned char kBom[3] = {0xEF, 0xBB, 0xBF};
        ofs_.write(reinterpret_cast<const char*>(kBom), sizeof(kBom));
        ofs_ << kHeader << "\r\n";
        if (!ofs_) {
            return Status(make_error("ErrorsCsvWriter::ensure_created", "failed to write errors CSV header"));
        }
        created_ = true;
        return Status::success();
    } catch (const std::exception& ex) {
        return Status(make_error("ErrorsCsvWriter::ensure_created", std::string("exception: ") + ex.what()));
    }
}

Status ErrorsCsvWriter::append(const ErrorsCsvRow& row) {
    Status created_status = ensure_created();
    if (!created_status) return created_status;

    try {
        const std::string fields[9] = {
            csv_escape(wide_to_utf8(row.relative_source_path)),
            csv_escape(row.status),
            csv_escape(std::to_string(row.attempt_count)),
            csv_escape(format_hresult(row.hresult)),
            csv_escape(format_win32(row.win32_error)),
            csv_escape(wide_to_utf8(row.target_folder)),
            csv_escape(std::to_string(row.source_size)),
            csv_escape(row.sha256),
            csv_escape(row.details),
        };
        for (size_t i = 0; i < 9; ++i) {
            if (i != 0) ofs_ << ',';
            ofs_ << fields[i];
        }
        ofs_ << "\r\n";
        if (!ofs_) {
            return Status(make_error("ErrorsCsvWriter::append", "failed to write errors CSV row"));
        }
        return Status::success();
    } catch (const std::exception& ex) {
        return Status(make_error("ErrorsCsvWriter::append", std::string("exception: ") + ex.what()));
    }
}

Status ErrorsCsvWriter::close() {
    if (!ofs_.is_open()) return Status::success();
    ofs_.flush();
    if (!ofs_) {
        return Status(make_error("ErrorsCsvWriter::close", "failed to flush errors CSV"));
    }
    ofs_.close();
    if (!ofs_) {
        return Status(make_error("ErrorsCsvWriter::close", "failed to close errors CSV"));
    }
    return Status::success();
}

}  // namespace wlm2pst
