#include "worker/app/worker_app.h"

#include "common/exit_codes.h"
#include "common/hashing/sha256.h"
#include "common/logging/logger.h"
#include "common/model.h"
#include "common/paths/path_utils.h"
#include "common/unicode/utf.h"
#include "common/version/version.h"
#include "worker/cancellation/cancel_token.h"
#include "worker/cli/cli_options.h"
#include "worker/folder_mapping/folder_mapper.h"
#include "worker/pipeline/import_pipeline.h"
#include "worker/reporting/errors_csv.h"
#include "worker/reporting/progress.h"
#include "worker/reporting/report.h"
#include "worker/resume/state_db.h"
#include "worker/scanner/scanner.h"
#include "worker/scanner/manifest_hash.h"
#include "worker/validation/validator.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>

namespace wlm2pst {

namespace {



FileTimeUtc now_filetime_utc() {
    auto now = std::chrono::system_clock::now();
    int64_t unix_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return filetime_from_unix_seconds(unix_seconds);
}

// Unique run identifier (GUID-shaped, cryptographically random enough for a
// job id; MAPI profile GUIDs come from CoCreateGuid separately).
std::string generate_run_id() {
    std::random_device rd;
    std::mt19937_64 gen(static_cast<uint64_t>(rd()) << 32 ^ rd());
    auto hex = [&gen](int chars) {
        static const char* digits = "0123456789abcdef";
        std::string s;
        for (int i = 0; i < chars; ++i) s += digits[gen() & 0xF];
        return s;
    };
    return hex(8) + "-" + hex(4) + "-" + hex(4) + "-" + hex(4) + "-" + hex(12);
}

int code(ExitCode c) { return static_cast<int>(c); }

int fail(ExitCode c, const std::string& message) {
    global_logger().error(message + " [" + to_string(c) + "]");
    std::cerr << "error: " << message << "\n";
    return code(c);
}

// Maps MAPI environment preflight/session errors onto spec exit codes.
ExitCode map_mapi_error(const Error& e) {
    if (e.operation.find("CreateMsgService") != std::string::npos ||
        e.operation.find("ConfigureMsgService") != std::string::npos) {
        return ExitCode::kPstProviderFailure;
    }
    if (e.operation.find("Profile") != std::string::npos) {
        return ExitCode::kProfileCleanupFailure;
    }
    return ExitCode::kOutlookUnavailable;
}

std::string describe(const Error& e) {
    std::string s = e.operation;
    if (e.hresult != 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<uint32_t>(e.hresult));
        s += " hr="; s += buf;
    }
    if (e.win32 != 0) s += " win32=" + std::to_string(e.win32);
    if (!e.message.empty()) s += ": " + e.message;
    return s;
}

std::wstring join_target(const std::vector<std::wstring>& segments) {
    std::wstring joined;
    for (const auto& s : segments) {
        if (!joined.empty()) joined += L'\\';
        joined += s;
    }
    return joined;
}

std::wstring parent_dir_of(const std::wstring& relative_file) {
    size_t pos = relative_file.find_last_of(L'\\');
    return pos == std::wstring::npos ? L"" : relative_file.substr(0, pos);
}

struct SidecarPaths {
    std::wstring state_db, report_json, log_file, errors_csv;
};

SidecarPaths sidecars_for(const std::wstring& output) {
    return {output + L".wlm2pst-state.sqlite", output + L".wlm2pst-report.json",
            output + L".wlm2pst.log", output + L".wlm2pst-errors.csv"};
}

// --overwrite may delete ONLY tool-owned outputs (spec section 18).
Result<bool> verify_tool_ownership(const std::wstring& pst_path, const std::wstring& db_path) {
    auto db = StateDb::open_existing(db_path);
    if (!db.ok()) return false;
    auto job = db.value().load_job();
    if (!job.ok()) return false;
    if (job.value().info.run_id.empty()) return false;
    // Recorded absolute output path must match the requested one.
    std::wstring recorded = normalize_backslashes(job.value().info.output_pst);
    std::wstring requested = normalize_backslashes(pst_path);
    return compare_ordinal_ignore_case(recorded, requested) == 0;
}

}  // namespace

int run_worker_app(const std::vector<std::wstring>& args,
                   IMapiEnvironment& mapi_env,
                   ISystemProbe& probe) {
    // ---- CLI ----
    CliParseResult parsed = parse_command_line(args);
    if (!parsed.ok) {
        std::cerr << "error: " << parsed.error_utf8 << "\n\n" << usage_text();
        return code(ExitCode::kInvalidArguments);
    }
    CliOptions& options = parsed.options;
    if (options.show_help) {
        std::cout << usage_text();
        return code(ExitCode::kSuccess);
    }
    if (options.show_version) {
        std::cout << "WLM2PST " << kToolVersion << " worker (" << kBuildArch << ")\n";
        return code(ExitCode::kSuccess);
    }
    options.source = normalize_backslashes(options.source);
    options.output = normalize_backslashes(options.output);
    if (auto err = validate_options_lexical(options)) {
        std::cerr << "error: " << err->message_utf8 << "\n";
        return code(err->exit_code);
    }

    SidecarPaths sidecars = sidecars_for(options.output);
    FileTimeUtc started_at = now_filetime_utc();

    // ---- Overwrite handling (before the logger appends to an old run's log) ----
    bool output_exists = probe.file_exists(options.output);
    if (options.overwrite && output_exists) {
        auto owned = verify_tool_ownership(options.output, sidecars.state_db);
        if (!owned.ok() || !owned.value()) {
            std::cerr << "error: refusing to overwrite: the existing PST is not verifiably "
                         "owned by WLM2PST (no matching state database)\n";
            return code(ExitCode::kOutputExists);
        }
        std::error_code ec;
        for (const auto& p : {options.output, sidecars.state_db, sidecars.report_json,
                              sidecars.log_file, sidecars.errors_csv}) {
            std::filesystem::remove(std::filesystem::path(p), ec);
            if (ec && p == options.output) {
                std::cerr << "error: existing PST could not be deleted (in use?)\n";
                return code(ExitCode::kOutputExists);
            }
        }
        output_exists = false;
    }

    // ---- Logger ----
    set_global_log_level(options.verbose ? LogLevel::kVerbose
                                         : options.quiet ? LogLevel::kWarn : LogLevel::kInfo);
    set_global_log_file(sidecars.log_file);
    global_logger().info(std::string("WLM2PST ") + kToolVersion + " worker (" + kBuildArch +
                         ") starting");

    // ---- Scan ----
    ScanOptions scan_options;
    scan_options.source_root = options.source;
    auto fs_probe = make_default_probe();
    auto scan = scan_source_tree(scan_options, *fs_probe);
    if (!scan.ok()) {
        return fail(ExitCode::kInvalidSource, "source scan failed: " + describe(scan.error()));
    }
    const ScanResult& scanned = scan.value();
    if (!scanned.issues.too_deep.empty()) {
        return fail(ExitCode::kInvalidSource,
                    "source contains paths deeper than the supported 100 levels (" +
                        std::to_string(scanned.issues.too_deep.size()) + " paths)");
    }
    for (const auto& skipped : scanned.issues.skipped_reparse_points) {
        global_logger().info("skipped reparse point: " + utf8_from_wide(skipped));
    }
    for (const auto& unreadable : scanned.issues.unreadable) {
        global_logger().warn("unreadable directory: " + utf8_from_wide(unreadable));
    }

    // ---- Preflight ----
    if (auto err = run_preflight_checks(options, scanned, probe)) {
        std::cerr << "error: " << err->message_utf8 << "\n";
        global_logger().error(err->message_utf8);
        return code(err->exit_code);
    }

    ProgressPrinter progress(options.quiet, options.verbose, std::cout);
    progress.print_preflight(options.source, scanned.entries.size(),
                             scanned.stats.folder_count, scanned.stats.total_bytes,
                             options.output,
                             std::string("worker ") + kBuildArch + " / classic Outlook MAPI");

    // ---- MAPI preflight (spec section 6) ----
    if (Status s = mapi_env.preflight_check(); !s.ok()) {
        return fail(map_mapi_error(s.error()), "MAPI preflight failed: " + describe(s.error()));
    }
    if (Status s = mapi_env.cleanup_stale_profiles(); !s.ok()) {
        global_logger().warn("stale profile cleanup: " + describe(s.error()));
    }

    // ---- Manifest hash ----
    Sha256 manifest_hasher;
    serialize_manifest_for_hash(scanned.entries, [&](const void* data, size_t len) {
        manifest_hasher.update(data, len);
    });
    auto manifest_digest = manifest_hasher.finish();
    std::string manifest_hash = to_hex_lower(manifest_digest.data(), manifest_digest.size());

    // ---- Folder plan ----
    std::set<std::wstring> dirs_set;
    for (const auto& entry : scanned.entries) dirs_set.insert(parent_dir_of(entry.relative_path));
    std::vector<std::wstring> dirs(dirs_set.begin(), dirs_set.end());
    auto plan = build_folder_plan(dirs);
    if (!plan.ok()) {
        return fail(ExitCode::kInvalidSource, "folder mapping failed: " + describe(plan.error()));
    }
    std::map<std::wstring, std::wstring> source_to_target;  // source rel dir -> target rel folder
    for (const auto& mapped : plan.value().folders) {
        source_to_target[mapped.source_relative] = join_target(mapped.target_segments);
    }

    // ---- Job setup: resume or new ----
    std::optional<StateDb> db;
    std::string run_id;
    bool resume_mode = false;

    if (options.resume) {
        if (!output_exists || !probe.file_exists(sidecars.state_db)) {
            return fail(ExitCode::kStateDbIncompatible,
                        "--resume requires both the PST and its state database to exist");
        }
        auto opened = StateDb::open_existing(sidecars.state_db);
        if (!opened.ok()) {
            return fail(ExitCode::kStateDbIncompatible,
                        "state database could not be opened: " + describe(opened.error()));
        }
        db.emplace(std::move(opened.value()));

        JobInfo expected;
        expected.source_root = options.source;
        expected.output_pst = options.output;
        expected.root_name = options.root_name;
        expected.outlook_bitness = kBuildArch;
        Status compat = db->check_resume_compatible(expected, manifest_hash, kStateDbSchemaVersion);
        if (!compat.ok()) {
            bool source_changed =
                compat.error().message.find("manifest_hash") != std::string::npos;
            return fail(source_changed ? ExitCode::kSourceChanged
                                       : ExitCode::kStateDbIncompatible,
                        "resume rejected: " + compat.error().message);
        }
        auto job = db->load_job();
        if (!job.ok()) {
            return fail(ExitCode::kStateDbIncompatible, "job row unreadable");
        }
        run_id = job.value().info.run_id;
        resume_mode = true;
        global_logger().info("resuming job " + run_id);
    } else {
        if (output_exists) {
            // Preflight already rejects this; double-check defensively.
            return fail(ExitCode::kOutputExists, "output PST already exists");
        }
        run_id = generate_run_id();
        JobInfo info;
        info.run_id = run_id;
        info.tool_version = kToolVersion;
        info.source_root = options.source;
        info.output_pst = options.output;
        info.root_name = options.root_name;
        info.outlook_bitness = kBuildArch;
        info.created_at_utc = started_at;
        info.manifest_hash = manifest_hash;

        std::vector<std::pair<std::wstring, std::wstring>> file_targets;
        file_targets.reserve(scanned.entries.size());
        for (const auto& entry : scanned.entries) {
            file_targets.emplace_back(entry.relative_path,
                                      source_to_target[parent_dir_of(entry.relative_path)]);
        }
        auto created = StateDb::create_new(sidecars.state_db, info, scanned.entries, file_targets);
        if (!created.ok()) {
            return fail(ExitCode::kStateDbIncompatible,
                        "state database could not be created: " + describe(created.error()));
        }
        db.emplace(std::move(created.value()));

        // Record the folder map (renames included) up front.
        std::map<std::wstring, std::string> rename_reasons;
        for (const auto& rename : plan.value().renames) {
            rename_reasons[rename.source_relative_folder] = rename.reason;
        }
        for (const auto& mapped : plan.value().folders) {
            std::string reason;
            if (auto it = rename_reasons.find(mapped.source_relative); it != rename_reasons.end()) {
                reason = it->second;
            }
            (void)db->upsert_folder(mapped.source_relative, join_target(mapped.target_segments),
                                    reason);
        }
        global_logger().info("new job " + run_id + ", " +
                             std::to_string(scanned.entries.size()) + " files");
    }

    // ---- Conversion session ----
    auto session = mapi_env.create_session(options.output, options.root_name,
                                           /*must_exist=*/resume_mode, "convert");
    if (!session.ok()) {
        (void)db->set_job_status("FAILED", now_filetime_utc());
        return fail(map_mapi_error(session.error()),
                    "PST session could not be opened: " + describe(session.error()));
    }
    (void)db->set_job_status("RUNNING", now_filetime_utc());

    // ---- Pipeline ----
    ErrorsCsvWriter errors_csv(sidecars.errors_csv);
    PipelineContext ctx;
    ctx.db = &db.value();
    ctx.session = session.value().get();
    ctx.cancel = &global_cancel_token();
    ctx.progress = &progress;
    ctx.errors_csv = &errors_csv;
    ctx.source_root = options.source;
    ctx.run_id = run_id;
    ctx.tool_version = kToolVersion;
    ctx.resume_mode = resume_mode;
    ctx.now_utc = now_filetime_utc;

    auto pipeline = run_import_pipeline(ctx);
    (void)errors_csv.close();
    if (!pipeline.ok()) {
        (void)db->set_job_status("FAILED", now_filetime_utc());
        (void)session.value()->close();
        return fail(ExitCode::kStateDbIncompatible,
                    "state database failure during conversion: " + describe(pipeline.error()));
    }
    PipelineResult& run = pipeline.value();

    // ---- Close the conversion session before validation (spec section 20) ----
    if (Status s = session.value()->close(); !s.ok()) {
        global_logger().warn("conversion profile cleanup: " + describe(s.error()));
    }
    session.value().reset();

    ReportData report;
    report.tool_version = kToolVersion;
    report.run_id = run_id;
    report.source = options.source;
    report.output = options.output;
    report.started_at_utc_iso = iso8601_from_filetime(started_at);
    report.counters = run.counters;
    report.counters.source_files = scanned.entries.size();
    report.counters.source_folders = scanned.stats.folder_count;
    report.counters.source_bytes = scanned.stats.total_bytes;
    report.renamed_folders = plan.value().renames;
    report.warnings = run.warnings;

    auto finish_report = [&](ValidationStatus status) {
        report.validation_status = to_string(status);
        report.completed_at_utc_iso = iso8601_from_filetime(now_filetime_utc());
        if (Status s = write_json_report(sidecars.report_json, report); !s.ok()) {
            global_logger().warn("JSON report could not be written: " + describe(s.error()));
        }
    };

    if (run.fatal) {
        (void)db->set_job_status("FAILED", now_filetime_utc());
        run.warnings.push_back("run aborted: " + run.fatal->message_utf8);
        report.warnings = run.warnings;
        finish_report(ValidationStatus::kValidationFailed);
        return fail(run.fatal->exit_code, run.fatal->message_utf8);
    }
    if (run.cancelled) {
        (void)db->set_job_status("CANCELLED", now_filetime_utc());
        run.warnings.push_back("cancelled by user; job is resumable with --resume");
        report.warnings = run.warnings;
        finish_report(ValidationStatus::kValidationFailed);
        std::cerr << "cancelled: conversion state saved; resume with --resume\n";
        return code(ExitCode::kCancelled);
    }

    // ---- Mandatory validation through a fresh profile (spec section 20) ----
    auto validation_session = mapi_env.create_session(options.output, options.root_name,
                                                      /*must_exist=*/true, "validate");
    if (!validation_session.ok()) {
        (void)db->set_job_status("FAILED", now_filetime_utc());
        finish_report(ValidationStatus::kValidationFailed);
        return fail(ExitCode::kValidationFailed,
                    "validation could not open the PST: " + describe(validation_session.error()));
    }
    auto validation = validate_pst(*validation_session.value(), db.value(), run_id);
    if (Status s = validation_session.value()->close(); !s.ok()) {
        global_logger().warn("validation profile cleanup: " + describe(s.error()));
    }
    if (!validation.ok()) {
        (void)db->set_job_status("FAILED", now_filetime_utc());
        finish_report(ValidationStatus::kValidationFailed);
        return fail(ExitCode::kValidationFailed,
                    "validation failed: " + describe(validation.error()));
    }
    const ValidationOutcome& outcome = validation.value();
    report.counters.output_folders = outcome.folders_checked;
    for (const auto& issue : outcome.issues) {
        std::string line = "validation: " + issue.detail;
        if (!issue.folder.empty()) line += " [folder: " + utf8_from_wide(issue.folder) + "]";
        if (!issue.relative_source_path.empty()) {
            line += " [source: " + utf8_from_wide(issue.relative_source_path) + "]";
        }
        global_logger().error(line);
        report.warnings.push_back(line);
    }

    bool validated = outcome.status != ValidationStatus::kValidationFailed;
    (void)db->set_job_status(validated ? "COMPLETED" : "FAILED", now_filetime_utc());
    finish_report(outcome.status);

    bool with_warnings = !report.warnings.empty() ||
                         run.counters.preserved_as_attachment > 0 ||
                         run.counters.failed_to_read > 0;
    progress.print_final_summary(report.counters, to_string(outcome.status), options.output,
                                 with_warnings);

    // ---- Exit code (spec section 22) ----
    if (!validated) return code(ExitCode::kValidationFailed);
    if (run.failed_mapi > 0) {
        // Even the fallback path could not persist some messages: output-side
        // write failures (spec section 23 systemic family).
        return code(ExitCode::kOutputWriteFailure);
    }
    if (run.counters.failed_to_read > 0) return code(ExitCode::kSuccessWithReadFailures);
    if (run.counters.preserved_as_attachment > 0) return code(ExitCode::kSuccessWithFallbacks);
    return code(ExitCode::kSuccess);
}

}  // namespace wlm2pst
