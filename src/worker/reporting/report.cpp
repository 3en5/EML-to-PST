#include "worker/reporting/report.h"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>

#include "worker/reporting/json_writer.h"

namespace wlm2pst {

namespace {

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

// Howard Hinnant's civil-from-days algorithm (public domain,
// http://howardhinnant.github.io/date_algorithms.html), used instead of
// gmtime()/gmtime_r()/gmtime_s() so behavior is identical and testable
// across MSVC, MinGW, and Linux without relying on platform-specific,
// thread-safety-varying calendar APIs.
struct CivilDate {
    int64_t year;
    unsigned month;
    unsigned day;
};

CivilDate civil_from_days(int64_t z) noexcept {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);              // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);  // [0, 365]
    const unsigned mp = (5 * doy + 2) / 153;                       // [0, 11]
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;               // [1, 31]
    const unsigned m = mp + (mp < 10 ? 3 : static_cast<unsigned>(-9));  // [1, 12]
    return CivilDate{y + (m <= 2 ? 1 : 0), m, d};
}

int64_t floor_div(int64_t a, int64_t b) noexcept {
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

}  // namespace

std::string iso8601_from_filetime(FileTimeUtc time) {
    const int64_t unix_seconds = unix_seconds_from_filetime(time);
    const int64_t days = floor_div(unix_seconds, 86400);
    const int64_t secs_of_day = unix_seconds - days * 86400;

    const CivilDate date = civil_from_days(days);
    const int hh = static_cast<int>(secs_of_day / 3600);
    const int mm = static_cast<int>((secs_of_day % 3600) / 60);
    const int ss = static_cast<int>(secs_of_day % 60);

    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02uT%02d:%02d:%02dZ",
                  static_cast<long long>(date.year), date.month, date.day, hh, mm, ss);
    return std::string(buf);
}

Status write_json_report(const std::wstring& path, const ReportData& data) {
    try {
        JsonValue root = JsonValue::object();
        root.set("toolVersion", data.tool_version);
        root.set("runId", data.run_id);
        root.set("source", wide_to_utf8(data.source));
        root.set("output", wide_to_utf8(data.output));
        root.set("startedAtUtc", data.started_at_utc_iso);
        root.set("completedAtUtc", data.completed_at_utc_iso);
        root.set("sourceFiles", data.counters.source_files);
        root.set("sourceFolders", data.counters.source_folders);
        root.set("sourceBytes", data.counters.source_bytes);
        root.set("importedNormally", data.counters.imported_normally);
        root.set("preservedAsAttachment", data.counters.preserved_as_attachment);
        root.set("failedToRead", data.counters.failed_to_read);
        root.set("outputFolders", data.counters.output_folders);
        root.set("validationStatus", data.validation_status);

        JsonValue renamed = JsonValue::array();
        for (const auto& folder : data.renamed_folders) {
            JsonValue item = JsonValue::object();
            item.set("sourceRelativeFolder", wide_to_utf8(folder.source_relative_folder));
            item.set("targetRelativeFolder", wide_to_utf8(folder.target_relative_folder));
            item.set("reason", folder.reason);
            renamed.push(std::move(item));
        }
        root.set("renamedFolders", std::move(renamed));

        JsonValue warnings = JsonValue::array();
        for (const auto& warning : data.warnings) {
            warnings.push(JsonValue(warning));
        }
        root.set("warnings", std::move(warnings));

        std::string text = to_json(root, 2);
        text += '\n';

        std::filesystem::path fs_path(path);
        std::ofstream ofs(fs_path, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            return Status(make_error("write_json_report", "failed to open report file for writing"));
        }
        ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!ofs) {
            return Status(make_error("write_json_report", "failed to write report file"));
        }
        ofs.close();
        if (!ofs) {
            return Status(make_error("write_json_report", "failed to close report file"));
        }
        return Status::success();
    } catch (const std::exception& ex) {
        return Status(make_error("write_json_report", std::string("exception: ") + ex.what()));
    }
}

}  // namespace wlm2pst
