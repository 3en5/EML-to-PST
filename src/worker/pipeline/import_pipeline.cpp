#include "worker/pipeline/import_pipeline.h"

#include "common/hashing/sha256.h"
#include "common/logging/logger.h"
#include "common/unicode/utf.h"
#include "worker/dates/date_parser.h"
#include "worker/folder_mapping/folder_aliases.h"
#include "worker/headers/header_inspector.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>

namespace wlm2pst {

namespace {

std::vector<std::wstring> split_segments(const std::wstring& path) {
    std::vector<std::wstring> segments;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(L'\\', start);
        if (end == std::wstring::npos) end = path.size();
        if (end > start) segments.emplace_back(path.substr(start, end - start));
        start = end + 1;
    }
    return segments;
}

// Joins the source root and a backslash-relative path into an openable
// absolute path. On POSIX test builds the separators are translated so the
// portable pipeline tests can exercise real files.
std::wstring join_absolute(const std::wstring& root, const std::wstring& relative) {
    std::wstring absolute = root + L"\\" + relative;
#ifndef _WIN32
    for (auto& c : absolute) {
        if (c == L'\\') c = L'/';
    }
#endif
    return absolute;
}

// Reads up to `cap` bytes from the start of the file (header inspection).
std::optional<std::string> read_prefix(const std::wstring& path, size_t cap) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return std::nullopt;
    std::string data(cap, '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (in.bad()) return std::nullopt;
    data.resize(static_cast<size_t>(in.gcount()));
    return data;
}

// Date fallback resolution (spec section 13). Returns the fallback timestamp
// and whether it came from the filesystem (which must produce a warning).
struct DateFallback {
    std::optional<FileTimeUtc> value;
    bool from_filesystem = false;
};

DateFallback resolve_received_fallback(const HeaderInspection& headers,
                                       const StateDb::FileRow& row, FileTimeUtc now) {
    // 2. first Received header timestamp; 3. Date header; 4. file time.
    if (auto received = headers.first_received_date_candidate()) {
        if (auto t = parse_rfc5322_date_bounded(*received, now)) return {t, false};
    }
    if (auto date = headers.date()) {
        if (auto t = parse_rfc5322_date_bounded(*date, now)) return {t, false};
    }
    return {row.source_last_write_utc, true};
}

DateFallback resolve_sent_fallback(const HeaderInspection& headers,
                                   const StateDb::FileRow& row, FileTimeUtc now) {
    // 2. Date header; 3. file time.
    if (auto date = headers.date()) {
        if (auto t = parse_rfc5322_date_bounded(*date, now)) return {t, false};
    }
    return {row.source_last_write_utc, true};
}

// Store-level operations whose repeated failure indicates a broken session
// rather than a broken message (spec section 23).
bool is_systemic_operation(const std::string& operation) {
    return operation == "IMessage::SaveChanges" || operation == "IMessage::SaveChanges(fallback)" ||
           operation == "IMAPIFolder::CreateMessage" || operation == "IMsgStore::OpenEntry(folder)" ||
           operation == "IMAPIFolder::CreateFolder";
}

class PipelineRun {
public:
    explicit PipelineRun(PipelineContext& ctx) : ctx_(ctx) {}

    Result<PipelineResult> run() {
        auto rows = ctx_.db->load_all_files();
        if (!rows.ok()) return fatal_db(rows.error());

        uint64_t total = rows.value().size();
        uint64_t processed_before = 0;
        for (const auto& row : rows.value()) {
            if (row.status != FileImportStatus::kPending) ++processed_before;
        }

        uint64_t processed = processed_before;
        size_t batch_open = 0;
        std::optional<sqlite_util::Transaction> batch;

        for (auto& row : rows.value()) {
            if (row.status != FileImportStatus::kPending) continue;

            if (ctx_.cancel->cancelled()) {
                result_.cancelled = true;
                break;
            }

            if (!batch) {
                auto tx = ctx_.db->begin_batch();
                if (!tx.ok()) return fatal_db(tx.error());
                batch.emplace(std::move(tx.value()));
                batch_open = 0;
            }

            if (auto fatal = process_row(row); fatal) {
                result_.fatal = std::move(fatal);
                break;
            }

            ++processed;
            ++batch_open;
            if (batch_open >= ctx_.db_batch_size) {
                if (Status s = batch->commit(); !s.ok()) return fatal_db(s.error());
                batch.reset();
            }

            ProgressSnapshot snap;
            snap.processed = processed;
            snap.total = total;
            snap.imported = counters_imported_;
            snap.fallback = counters_preserved_;
            snap.failed = counters_failed_read_ + counters_failed_mapi_;
            snap.current_folder = row.target_folder_path;
            ctx_.progress->update(snap);
        }

        if (batch) {
            if (Status s = batch->commit(); !s.ok()) return fatal_db(s.error());
        }

        // Authoritative counters from the database.
        auto counts = ctx_.db->count_by_status();
        if (!counts.ok()) return fatal_db(counts.error());
        result_.counters.imported_normally = counts.value().imported;
        result_.counters.preserved_as_attachment = counts.value().preserved;
        result_.counters.failed_to_read = counts.value().failed_read;
        result_.failed_mapi = counts.value().failed_mapi;
        return std::move(result_);
    }

private:
    Result<PipelineResult> fatal_db(const Error& e) {
        // State database writes failing is always fatal (spec section 23).
        return make_error(e.operation,
                          "state database failure: " + e.message +
                              " (sqlite code " + std::to_string(e.hresult) + ")");
    }

    // Returns a fatal error to abort the run, or nullopt to continue.
    std::optional<PipelineFatal> process_row(const StateDb::FileRow& row) {
        FileTimeUtc now = ctx_.now_utc();
        (void)ctx_.db->mark_started(row.id, now);

        std::wstring absolute = join_absolute(ctx_.source_root, row.relative_source_path);

        // Crash-window recovery (resume): adopt an already-saved message
        // instead of duplicating it (spec section 17).
        if (ctx_.resume_mode) {
            auto recovered = try_adopt_existing(row, now);
            if (recovered.has_value()) {
                if (*recovered) return std::nullopt;  // adopted
            } else {
                return PipelineFatal{ExitCode::kResumeIntegrityError,
                                     "duplicate tool-owned messages found during resume for: " +
                                         utf8_from_wide(row.relative_source_path)};
            }
        }

        // SHA-256 (streamed) - also the "can we read it" gate.
        auto digest = sha256_file(absolute);
        if (!digest.ok()) {
            record_read_failure(row, digest.error(), now);
            return std::nullopt;
        }
        std::string sha_hex = to_hex_lower(digest.value().data(), digest.value().size());
        (void)ctx_.db->set_sha256(row.id, sha_hex);

        auto prefix = read_prefix(absolute, kDefaultHeaderCap);
        if (!prefix) {
            record_read_failure(row, make_error("read_prefix", "header section unreadable"), now);
            return std::nullopt;
        }
        HeaderInspection headers = inspect_headers(*prefix);

        bool is_draft = headers.x_unsent() || path_has_drafts_component(row.relative_source_path);
        bool is_sent = !is_draft && path_has_sent_component(row.relative_source_path);

        MessageMetadata metadata;
        metadata.source_relative_path = row.relative_source_path;
        metadata.sha256_hex = sha_hex;
        metadata.run_id = ctx_.run_id;
        metadata.import_version = ctx_.tool_version;
        metadata.source_size = row.source_size;
        metadata.source_last_write_utc = row.source_last_write_utc;
        metadata.mark_sent = is_sent;
        metadata.mark_draft = is_draft;
        metadata.now_utc = now;

        bool fallback_from_fs = false;
        if (is_sent) {
            DateFallback fb = resolve_sent_fallback(headers, row, now);
            metadata.fallback_client_submit_time = fb.value;
            fallback_from_fs = fb.from_filesystem;
        } else {
            DateFallback fb = resolve_received_fallback(headers, row, now);
            metadata.fallback_message_delivery_time = fb.value;
            fallback_from_fs = fb.from_filesystem;
        }

        auto folder = ensure_target_folder(row.target_folder_path);
        if (!folder.ok()) {
            return PipelineFatal{ExitCode::kOutputWriteFailure,
                                 "destination folder could not be created: " + folder.error().message};
        }

        ImportFailure failure;
        auto outcome = ctx_.session->import_eml(folder.value(), absolute, metadata, &failure);
        if (outcome.ok()) {
            (void)ctx_.db->mark_imported(row.id, outcome.value().entry_id, ctx_.now_utc());
            ++counters_imported_;
            consecutive_systemic_ = 0;
            if (outcome.value().date_fallback_applied && fallback_from_fs) {
                result_.warnings.push_back("date fell back to file timestamp: " +
                                           utf8_from_wide(row.relative_source_path));
            }
            if (outcome.value().used_normalized_retry) {
                global_logger().verbose("imported after conservative normalization: " +
                                        utf8_from_wide(row.relative_source_path));
            }
            return std::nullopt;
        }

        // Stream-open failures are read failures, not conversion failures.
        if (outcome.error().operation == "SHCreateStreamOnFileEx") {
            record_read_failure(row, outcome.error(), now);
            return std::nullopt;
        }

        if (is_systemic_operation(outcome.error().operation)) {
            if (++consecutive_systemic_ >= ctx_.circuit_breaker_threshold) {
                return PipelineFatal{
                    ExitCode::kOutputWriteFailure,
                    "circuit breaker: " + std::to_string(consecutive_systemic_) +
                        " consecutive failures at " + outcome.error().operation};
            }
        }

        // Final fallback: preserve the original EML as an attachment.
        auto errors_folder = ensure_target_folder(kConversionErrorsFolderName);
        if (!errors_folder.ok()) {
            return PipelineFatal{ExitCode::kOutputWriteFailure,
                                 "_Conversion Errors folder could not be created"};
        }
        auto fallback = ctx_.session->import_fallback(errors_folder.value(), absolute, metadata,
                                                      failure.first_hresult,
                                                      failure.second_hresult);
        FileTimeUtc done = ctx_.now_utc();
        if (fallback.ok()) {
            (void)ctx_.db->mark_status(row.id, FileImportStatus::kPreservedAsAttachment,
                                       failure.first_hresult, done);
            ++counters_preserved_;
            consecutive_systemic_ = 0;
            append_csv(row, "PRESERVED_AS_ATTACHMENT", failure.first_hresult,
                       "MIME conversion failed; original EML attached unchanged");
            return std::nullopt;
        }

        (void)ctx_.db->mark_status(row.id, FileImportStatus::kFailedMapi,
                                   fallback.error().hresult, done);
        ++counters_failed_mapi_;
        append_csv(row, "FAILED_MAPI", fallback.error().hresult,
                   "fallback message creation failed at " + fallback.error().operation);
        if (is_systemic_operation(fallback.error().operation) &&
            ++consecutive_systemic_ >= ctx_.circuit_breaker_threshold) {
            return PipelineFatal{ExitCode::kOutputWriteFailure,
                                 "circuit breaker: repeated store failures while creating "
                                 "fallback messages"};
        }
        return std::nullopt;
    }

    // Returns: true = adopted, false = not found (import normally),
    // no value (nullopt) = integrity error (>1 match).
    std::optional<bool> try_adopt_existing(const StateDb::FileRow& row, FileTimeUtc now) {
        auto folder = ensure_target_folder(row.target_folder_path);
        if (!folder.ok()) return false;

        const std::wstring& key = row.target_folder_path;
        auto it = resume_index_.find(key);
        if (it == resume_index_.end()) {
            auto tracked = ctx_.session->read_tracked_messages(folder.value());
            if (!tracked.ok()) return false;
            it = resume_index_.emplace(key, std::move(tracked.value())).first;
        }

        std::vector<const TrackedMessage*> matches;
        for (const auto& m : it->second) {
            if (m.run_id == ctx_.run_id && m.source_relative_path == row.relative_source_path) {
                matches.push_back(&m);
            }
        }
        if (matches.empty()) return false;
        if (matches.size() > 1) return std::nullopt;  // deterministic integrity stop
        (void)ctx_.db->mark_imported(row.id, matches[0]->entry_id, now);
        ++counters_imported_;
        global_logger().info("resume: adopted message saved before crash: " +
                             utf8_from_wide(row.relative_source_path));
        return true;
    }

    Result<EntryId> ensure_target_folder(const std::wstring& target_relative) {
        if (auto it = folder_ids_.find(target_relative); it != folder_ids_.end()) {
            return it->second;
        }
        auto id = ctx_.session->ensure_folder(split_segments(target_relative));
        if (id.ok()) {
            folder_ids_.emplace(target_relative, id.value());
            (void)ctx_.db->set_folder_entry_id(target_relative, id.value());
        }
        return id;
    }

    void record_read_failure(const StateDb::FileRow& row, const Error& error, FileTimeUtc now) {
        (void)ctx_.db->mark_status(row.id, FileImportStatus::kFailedSourceRead,
                                   error.hresult, now);
        ++counters_failed_read_;
        append_csv(row, "FAILED_SOURCE_READ", error.hresult,
                   "source unreadable at " + error.operation);
        global_logger().warn("source file could not be read: " +
                             utf8_from_wide(row.relative_source_path));
    }

    void append_csv(const StateDb::FileRow& row, const char* status, int32_t hresult,
                    std::string details) {
        ErrorsCsvRow csv;
        csv.relative_source_path = row.relative_source_path;
        csv.status = status;
        csv.attempt_count = row.attempt_count + 1;
        csv.hresult = hresult;
        csv.win32_error = 0;
        csv.target_folder = row.target_folder_path;
        csv.source_size = row.source_size;
        csv.sha256 = row.source_sha256;
        csv.details = std::move(details);
        (void)ctx_.errors_csv->append(csv);
    }

    PipelineContext& ctx_;
    PipelineResult result_;
    std::map<std::wstring, EntryId> folder_ids_;
    std::map<std::wstring, std::vector<TrackedMessage>> resume_index_;
    uint64_t counters_imported_ = 0;
    uint64_t counters_preserved_ = 0;
    uint64_t counters_failed_read_ = 0;
    uint64_t counters_failed_mapi_ = 0;
    int consecutive_systemic_ = 0;
};

}  // namespace

Result<PipelineResult> run_import_pipeline(PipelineContext& ctx) {
    PipelineRun run(ctx);
    return run.run();
}

}  // namespace wlm2pst
