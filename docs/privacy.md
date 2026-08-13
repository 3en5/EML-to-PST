# Privacy

WLM2PST processes potentially sensitive legal and business mail. This
document is the authoritative statement of what the tool is and is not
allowed to write to logs, the JSON report, or the errors CSV — matching
`docs/dev-conventions.md`'s privacy rule and the class-level contract
documented on `Logger` (`src/common/logging/logger.h`) and on the resume
state database (`src/worker/resume/state_db.h`).

This applies to **every** log level, including `--verbose`. `--verbose` may
add call names, provider/HRESULT details, timing, and sanitized paths — it
must never add message content.

## Never logged, by default or at any verbosity

- Message body (plain text or HTML).
- Sender address.
- Recipient addresses (To/Cc/Bcc).
- Message subject — **of the source message being converted**. (See the one
  documented exception below, which is a different, tool-authored subject.)
- Attachment contents.
- Raw MIME headers.

None of the above may appear in the console, the `.wlm2pst.log` file, the
JSON report, the errors CSV, or the SQLite resume database. `Logger`
performs no content redaction of its own — it trusts every call site to
uphold this contract, so any new logging call is reviewed against this list
before it ships.

## Allowed operational fields

These are the only categories of information WLM2PST's logs, report, and
CSV may contain:

- Relative source path (e.g. `account@example.com\Inbox\001.eml`).
- Target folder path inside the PST.
- File size (bytes).
- SHA-256 hash (lowercase hex).
- Status (`PENDING` / `IMPORTED` / `PRESERVED_AS_ATTACHMENT` /
  `FAILED_SOURCE_READ` / `FAILED_MAPI`, and validation statuses).
- Attempt number.
- HRESULT and Win32 error codes.
- Timing (durations, timestamps).
- Counts (files, folders, messages).
- Tool version and Outlook/worker bitness.

## JSON report contents

`<output>.wlm2pst-report.json` (`ReportData` in
`src/worker/reporting/report.h`) contains: tool version, run id, source and
output paths, start/completion timestamps (ISO 8601 UTC), the aggregate
`JobCounters` (source files/folders/bytes, imported/preserved/failed
counts, output folder count), the validation status, the list of renamed
folders (source and target relative paths plus a machine-readable rename
reason such as `"collision"` or `"too-long"` — folder *names*, not message
content), and a `warnings` array that must contain only privacy-safe
strings under the same rules as everything else in this document. Byte
counts are always integers, never localized strings.

## Errors CSV contents

`<output>.wlm2pst-errors.csv` (`ErrorsCsvRow` in
`src/worker/reporting/errors_csv.h`) is created lazily — only once the
first row is appended, so a job with zero failures or fallbacks produces no
CSV file at all. Its columns are:

```text
relative_source_path,status,attempt_count,hresult,win32_error,target_folder,source_size,sha256,details
```

`details` is explicitly documented as privacy-safe only — diagnostic text
such as "conversion failed, see HRESULT", never subject/body/address
content from the source file. The file is written UTF-8 with a BOM
specifically so Hebrew (and other non-ASCII) relative paths display
correctly when opened in Excel.

## The one documented exception: fallback message subjects

When both conversion attempts fail for a file, WLM2PST creates a fallback
message in `<root-name>\_Conversion Errors` whose **subject line is set by
the tool** to:

```text
[EML conversion failed] <original filename>
```

(see `import_fallback()` in `src/worker/import_engine.h`'s implementation,
and spec section 16). This is a deliberate, documented exception to "never
log the subject": it is not the *original* message's subject (which may
never have been readable, and is never surfaced), it is a tool-authored
subject line containing only the original **filename** — the same
information already present in the relative source path — so the failure
is identifiable inside Outlook without exposing anything about the
message's actual content. The fallback message's body is similarly limited
to relative path, size, SHA-256, and both attempts' HRESULTs; it never
contains sender, recipient, or original subject/body text.

This exception applies **only** to that one tool-authored subject line
inside the PST itself. It does not extend to logs, the JSON report, or the
errors CSV — those continue to reference the file only by its relative
path, exactly as everywhere else in the tool.
