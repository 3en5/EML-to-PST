# WLM2PST

WLM2PST converts a recursive folder tree of Windows Live Mail `.eml` files
into **one** Unicode `.pst` file that opens locally in classic Microsoft
Outlook. That is the entire product.

```powershell
wlm2pst.exe --source "C:\Mail\Windows Live Mail" --output "D:\Migration\Rina-Mail.pst"
```

## What it does

- Recursively scans a source folder for `.eml` files.
- Recreates the source folder structure as ordinary PST folders under one
  root folder (default name `Windows Live Mail`, or `--root-name`).
- Imports every EML into the PST through classic Outlook's own MIME-to-MAPI
  converter (`IConverterSession::MIMEToMAPI`), so the parsing behavior is
  Outlook's, not a reimplementation.
- Writes a single Unicode PST via Outlook's `MSUPST MS` provider.
- Keeps a crash-safe SQLite resume database beside the PST so an interrupted
  job can continue with `--resume` instead of restarting.
- Validates the finished PST through a second, independent MAPI session
  before reporting success.

## What it does not do

WLM2PST has exactly one destination format and no cloud path. It does **not**
implement, and will not add:

- Microsoft 365, Exchange, or Exchange Online migration.
- Microsoft Graph API access.
- IMAP upload.
- Cloud storage of any kind.
- OST creation.
- MSG or MBOX export.
- Contact, calendar, task, account-settings, signature, or rule migration
  from Windows Live Mail.
- Automatic duplicate deletion.
- Automatic splitting into multiple PST files.
- Merging into an arbitrary pre-existing PST.
- A GUI or a web service.
- Telemetry or any other network dependency at runtime.
- HTML rendering, content sanitization, or antivirus scanning of imported
  mail — the tool preserves content, it does not vet it.
- Modification of any source `.eml` file, ever.

## Classic Outlook requirement

WLM2PST drives classic Outlook's installed Extended MAPI implementation and
Unicode PST provider. It has no PST-writing code of its own.

**New Outlook (the Microsoft 365 "one Outlook" app) does not support Extended
MAPI and cannot be used.** If only New Outlook is installed, the launcher
exits with `OUTLOOK_UNAVAILABLE` (exit code 14) and says so explicitly. A
classic Outlook installation (desktop MSI or Click-to-Run "Outlook" that
still exposes MAPI) is required on the machine that runs the conversion. No
mail account needs to be configured in it — WLM2PST creates and uses its own
temporary MAPI profile.

## Bitness behavior

Extended MAPI requires the calling process's bitness to match the installed
classic Outlook's bitness. The package therefore ships three executables:

```text
wlm2pst.exe                 32-bit launcher
wlm2pst-worker-x86.exe      Win32 worker
wlm2pst-worker-x64.exe      x64 worker
```

`wlm2pst.exe` is deliberately built 32-bit so it can run under WOW64 on any
64-bit Windows and inspect both the 32-bit and 64-bit registry views. At
startup it layers several detection signals (Click-to-Run configuration,
Office/MSI bitness keys, `App Paths`, and finally the `OUTLOOK.EXE` PE header
itself) to determine which Outlook is installed and how wide it is, then
launches the matching worker with the original command line, forwarding
Ctrl+C and the worker's exit code unchanged. If the matching worker binary is
missing from the install directory, the launcher exits with
`BITNESS_MISMATCH` (exit code 15) rather than guessing.

Both workers are built from identical source and behave identically; only
the target architecture differs.

## Command examples

Basic conversion:

```powershell
wlm2pst.exe --source "C:\OldMail" --output "D:\Migration\OldMail.pst"
```

Custom root folder name (Hebrew example):

```powershell
wlm2pst.exe --source "C:\OldMail" --output "D:\Migration\OldMail.pst" --root-name "דואר ישן"
```

Resuming an interrupted job (only valid when both the PST and its matching
state database already exist):

```powershell
wlm2pst.exe --source "C:\OldMail" --output "D:\Migration\OldMail.pst" --resume
```

### Full option reference

```text
--source <directory>       Required. Recursive source folder containing EML files.
--output <file.pst>        Required. Path to a new PST file.
--root-name <name>         Optional. Root folder name inside the PST.
                            Default: Windows Live Mail
--resume                   Resume a compatible interrupted job.
--overwrite                Delete a previous tool-owned output and start again.
--quiet                    Show only errors and final summary.
--verbose                  Show additional technical progress without exposing message content.
--help                     Show help.
--version                  Show application, worker, architecture, and build versions.
```

Rules worth knowing:

- `--source` and `--output` are mandatory for a conversion run.
- `--output` must end in `.pst` (case-insensitive), must be a local
  filesystem path (UNC and network drives are rejected), and must not be
  inside the source tree (or vice versa).
- WLM2PST never overwrites an existing PST unless `--overwrite` is given, and
  `--overwrite` only ever deletes files it can prove it created itself (see
  Output files, below).
- `--resume` and `--overwrite` are mutually exclusive.

## Output files

Every run writes its output beside the requested PST path, using the PST's
full path as a prefix:

| File | Created | Purpose |
|---|---|---|
| `<output>.pst` | Always | The Unicode PST itself — the only mail output. |
| `<output>.wlm2pst-state.sqlite` | Always | Crash-safe resume database: job identity, per-file import status, folder map. Required for `--resume` and for proving tool-ownership before `--overwrite`. |
| `<output>.wlm2pst-report.json` | Always, at end of run | Machine-readable summary: counts, validation status, renamed folders, warnings. |
| `<output>.wlm2pst.log` | Always | Human-readable run log (privacy-safe fields only; see docs/privacy.md). |
| `<output>.wlm2pst-errors.csv` | Only if any error or fallback occurred | One row per file that failed to read, failed to convert, or was preserved as a fallback attachment. UTF-8 with BOM so Hebrew paths open correctly in Excel. |

## Exit codes

Exit codes are centralized in `src/common/exit_codes.h` — this table matches
it exactly and is the single source of truth used by the tool.

| Code | Name | Meaning |
|---|---|---|
| 0 | `SUCCESS` | Conversion and validation completed with no warnings. |
| 2 | `SUCCESS_WITH_FALLBACKS` | Completed and validated; one or more EML files were preserved as fallback attachments instead of being converted. |
| 3 | `SUCCESS_WITH_READ_FAILURES` | Completed and validated; one or more source files could not be read at all. |
| 10 | `INVALID_ARGUMENTS` | Invalid command-line arguments. |
| 11 | `INVALID_SOURCE` | Invalid source directory, or no EML files found. |
| 12 | `OUTPUT_EXISTS` | Output already exists and cannot be safely overwritten. |
| 13 | `INSUFFICIENT_DISK_SPACE` | Insufficient free disk space for the estimated output. |
| 14 | `OUTLOOK_UNAVAILABLE` | Classic Outlook or Extended MAPI is unavailable. |
| 15 | `BITNESS_MISMATCH` | Worker and Outlook bitness mismatch (or the matching worker binary is missing). |
| 16 | `PST_PROVIDER_FAILURE` | The Unicode PST provider could not be created or configured. |
| 17 | `PST_SIZE_CEILING_EXCEEDED` | Estimated output exceeds the safe single-PST ceiling (45 GiB). |
| 18 | `OUTPUT_WRITE_FAILURE` | Disk full or output write failure during conversion. |
| 19 | `VALIDATION_FAILED` | Post-conversion PST validation failed. |
| 20 | `STATE_DB_INCOMPATIBLE` | State database is incompatible with the PST or the current source manifest. |
| 21 | `SOURCE_CHANGED` | Source tree changed after the manifest was created. |
| 22 | `RESUME_INTEGRITY_ERROR` | Integrity error during resume crash-window recovery (e.g. ambiguous duplicate tool-owned messages). |
| 23 | `PROFILE_CLEANUP_FAILURE` | Temporary MAPI profile creation or cleanup failed critically. |
| 130 | `CANCELLED` | User cancelled with Ctrl+C. |

## Privacy behavior

WLM2PST processes potentially sensitive personal and business mail. Logs,
the JSON report, and the errors CSV never contain message subjects, bodies,
HTML, sender/recipient addresses, raw MIME headers, or attachment content —
not even at `--verbose`. They contain only relative paths, target folder
paths, sizes, SHA-256 hashes, statuses, attempt counts, HRESULT/Win32 codes,
timings, counts, and bitness. See `docs/privacy.md` for the full contract,
including the one documented exception (the fallback message subject
intentionally contains the original filename, by design — see spec section
16 and `docs/privacy.md`).

## Known limitations

- Exactly one PST per run. WLM2PST does not split output across multiple
  files and will refuse a job whose estimated output exceeds an ~45 GiB
  ceiling (exit code 17) rather than silently produce an oversized file.
- Duplicates are preserved intentionally. Two distinct source files always
  become two PST items, even with identical content; the resume system only
  ever suppresses re-import of the *same* source file within the *same* job.
- Every imported message is marked read. EML does not reliably carry Windows
  Live Mail's per-user read/unread state, so WLM2PST does not guess from
  timestamps or folder location — it marks everything read on import.
- The filesystem folder tree is the source of truth; WLM2PST does not read
  `Mail.MSMessageStore` or any other Windows Live Mail database.
- Windows Live Mail's Inbox/Sent/Drafts are not special-cased into Outlook's
  built-in folders — they are recreated as ordinary folders under the tool
  root, matched only by name for sent/draft *flag* behavior.

## Opening the PST in classic Outlook

Once conversion finishes, open the PST directly — no import wizard is
needed:

```text
File -> Open & Export -> Open Outlook Data File
```

Then browse to the `.pst` file WLM2PST produced. Outlook attaches it as a
regular data file; the migrated folders appear under the root folder name
you chose (or `Windows Live Mail` by default).

## Building

See [BUILDING.md](BUILDING.md).

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md).

## Troubleshooting

See [docs/troubleshooting.md](docs/troubleshooting.md).
