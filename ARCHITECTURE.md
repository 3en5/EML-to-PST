# WLM2PST architecture

## Launcher and workers

WLM2PST ships three binaries:

```text
wlm2pst.exe                 32-bit launcher
wlm2pst-worker-x86.exe      Win32 worker
wlm2pst-worker-x64.exe      x64 worker
```

`src/launcher/main.cpp` is intentionally thin: it handles `--help` and
`--version` directly, then hands off everything else. All detection and
selection logic lives in the portable `wlm2pst_launcher_core` static
library (`src/launcher/outlook_detector.*`, `pe_arch.*`,
`registry_reader.*`, `worker_launcher.*`), built behind the
`IRegistryReader` / `IFileProbe` abstractions so it is unit-testable without
a real Windows registry or filesystem.

Detection (`detect_outlook()`) does not trust a single registry key. It
layers several independent signals — Click-to-Run platform configuration,
Office/MSI bitness keys, `App Paths`, and finally the `OUTLOOK.EXE` PE
header itself — and reconciles them, preferring the PE header when it
disagrees with a registry hint. It also probes for a New Outlook (appx)
install via `%LOCALAPPDATA%\Microsoft\WindowsApps\olk.exe`; if that is the
*only* thing found, the launcher reports `new_outlook_only` and exits with
`OUTLOOK_UNAVAILABLE` (14) rather than trying to run a worker against
something that has no Extended MAPI at all.

Once a classic Outlook bitness is confirmed, the launcher resolves
`wlm2pst-worker-x86.exe` or `wlm2pst-worker-x64.exe` next to its own
executable, starts it with the untouched original command line
(`command_tail(GetCommandLineW())`), and forwards Ctrl+C and the worker's
process exit code unchanged. If the expected worker file is missing, the
launcher exits `BITNESS_MISMATCH` (15) instead of guessing at a substitute.

### Why bitness must match Outlook

Extended MAPI is loaded in-process: the worker calls into Outlook's MAPI
provider DLLs directly (not over RPC), so the worker process and the
installed classic Outlook must be the same bitness — a 32-bit worker cannot
load a 64-bit Outlook's MAPI stack and vice versa. This is also why the
launcher itself is built 32-bit: a 32-bit process runs unmodified under
WOW64 on any 64-bit Windows and can query both the 32-bit and 64-bit
registry views to determine what is actually installed, whereas a 64-bit
launcher would need extra machinery to see the 32-bit view at all.

Both workers are compiled from the same `src/worker/` sources; the only
difference is `CMAKE_SIZEOF_VOID_P`, which selects the output name
(`wlm2pst-worker-x86` vs. `wlm2pst-worker-x64`) in
`wlm2pst_configure_windows_exe()`.

## Temporary MAPI profile lifecycle

WLM2PST never touches the user's real Outlook profile. Every session —
conversion or validation — creates its own throwaway profile
(`src/worker/mapi/temporary_profile.cpp`, `TemporaryProfile`):

```text
CoInitializeEx                                   (ComInit RAII)
MAPIInitialize                                   (MapiRuntime::initialize)
IProfAdmin::CreateProfile("WLM2PST-{GUID}")       (TemporaryProfile::create)
IProfAdmin::AdminServices -> IMsgServiceAdmin
IMsgServiceAdmin2::CreateMsgServiceEx("MSUPST MS")   (falls back to
    IMsgServiceAdmin::CreateMsgService + a GetMsgServiceTable scan when
    IMsgServiceAdmin2 is unavailable on the installed MAPI stack)
IMsgServiceAdmin::ConfigureMsgService(PR_PST_PATH_W = <output path>)
MAPILogonEx(profile name, MAPI_EXTENDED | MAPI_NEW_SESSION |
            MAPI_EXPLICIT_PROFILE | MAPI_NO_MAIL)      (PstStore::logon_and_open)
open IPM subtree -> ensure/open the tool root folder
... import or validate ...
release all MAPI interfaces (RAII, see below)
MAPILogoff                                        (PstStore::close)
IProfAdmin::DeleteProfile                          (TemporaryProfile::remove)
MAPIUninitialize / CoUninitialize                  (destructors of MapiEnvironment)
```

The profile name always starts with the fixed prefix `WLM2PST-`
(`kProfilePrefixW`/`A` in `mapi_constants.h`); the validation session uses
`WLM2PST-VALIDATE-` specifically so a crash during validation is
distinguishable from a crash during conversion. `TemporaryProfile`'s
destructor calls `remove()` on every exit path — normal return, early
`Result` propagation, or an exception unwinding through it — so a profile is
never intentionally left behind. At startup,
`cleanup_stale_wlm2pst_profiles()` scans `IProfAdmin::GetProfileTable` and
deletes only profiles whose display name starts with the `WLM2PST-` prefix;
that prefix match is the entire ownership proof, and no profile lacking it
is ever touched (spec section 8 forbids removing user profiles).

`MapiPstSession::close()` releases the store, logs off, and removes the
profile in that order, both when called explicitly and from the session's
destructor — so a normal exit, an early return through `Result<...>`
propagation, or an exception unwinding all converge on the same orderly
teardown.

## PST provider lifecycle

WLM2PST always creates/opens the **Unicode** PST provider, service name
`MSUPST MS` (`kUnicodePstServiceName`). The ANSI provider (`MSPST MS`) is
never used. The provider is configured with the profile-section property
`PR_PST_PATH_W` — `PROP_TAG(PT_UNICODE, 0x6700)` — which is not published in
public SDK headers; the tag value is taken from MFCMAPI's own PST provider
usage. Passing a path that does not yet exist, with `ulUIParam = 0` and no
`SERVICE_UI_ALWAYS` flag, makes the provider create a new Unicode PST
silently with no UI prompt.

During preflight, `TemporaryProfile::probe_unicode_pst_service()` adds the
`MSUPST MS` service to a throwaway profile *without* configuring a path,
which proves the provider is registered and loadable while creating no
file on disk; the whole profile is then deleted immediately.

## MIME-to-MAPI conversion path

```text
EML file
  -> SHCreateStreamOnFileEx(STGM_READ | STGM_SHARE_DENY_WRITE)   (open_read_only_stream)
  -> IConverterSession::MIMEToMAPI(stream, message, nullptr, flags)
  -> IMessage (unsaved)
  -> flags/state, date policy, tracking properties applied
  -> IMessage::SaveChanges(KEEP_OPEN_READONLY)
  -> Microsoft Unicode PST provider (MSUPST MS)
```

The file is opened with `STGM_SHARE_DENY_WRITE` so nothing can modify it
while it is being read, and the stream is read incrementally by the
converter rather than loaded whole into memory (spec section 7).

`IConverterSession` (CLSID `{4E3A7680-B77A-11D0-9DA5-00C04FD65685}`) is not
published in SDK headers; its vtable layout in `mapi_constants.h` matches
MFCMAPI and the documented Microsoft MIME-import sample. `MimeConverter::
convert()` calls `MIMEToMAPI` with `CCSF_SMTP | CCSF_INCLUDE_BCC |
CCSF_GLOBAL_MESSAGE` first — `CCSF_GLOBAL_MESSAGE` enables international
(EAI) header handling on Outlook 2010+ — and, only if that specific call
fails with `E_INVALIDARG` or `MAPI_E_UNKNOWN_FLAGS` (i.e. an older converter
that does not recognize the flag), retries once with `CCSF_SMTP |
CCSF_INCLUDE_BCC` alone, rewinding the stream first. Any other failure HRESULT
is treated as a real conversion failure and is not retried at this layer —
it instead becomes attempt 1 of the two-attempt malformed-EML pipeline
described next.

## Malformed EML handling (two attempts + fallback)

`MapiPstSession::import_eml()` implements the spec's two-attempt pipeline:

1. **Attempt 1** — the original file bytes, unmodified, through
   `MIMEToMAPI` directly.
2. **Attempt 2** — if attempt 1 fails, `write_normalized_copy()`
   (`src/worker/mapi/mime_converter.cpp`) reads up to a 1 MiB header prefix,
   asks the portable, Windows-independent `normalize_eml_header()`
   (`src/worker/normalize/eml_normalizer.*`) for conservative header-only
   repairs — strip a leading UTF-8 BOM, normalize header line endings to
   CRLF, drop NUL bytes in the header section, insert a missing
   header/body separator only when the boundary is safely identifiable —
   writes the result to a securely-created temp file
   (`GetTempFileNameW` in the per-user temp directory), and retries
   `MIMEToMAPI` against that copy. Body bytes past the header are always
   copied verbatim; nothing beyond the header section is ever rewritten.
   The temp file is deleted by `TempFile`'s destructor.

If both attempts fail, the caller (the import loop, not shown in this
header-only walkthrough) invokes `import_fallback()`, which creates a plain
`IPM.Note` in `<root-name>\_Conversion Errors` containing a
privacy-safe diagnostic body (relative path, size, SHA-256, both attempts'
HRESULTs — never subject/body/addresses from the failed source) and the
original EML attached byte-for-byte unchanged
(`PR_ATTACH_DATA_BIN`, MIME type `message/rfc822`, streamed via
`IStream::CopyTo` rather than loaded into memory).

## Resume and crash windows

The resume database (`<output>.wlm2pst-state.sqlite`,
`src/worker/resume/state_db.*`, built on the vendored SQLite in WAL mode)
tracks three things: the job identity/compatibility row, one row per
manifest file with its `FileImportStatus`
(`PENDING` / `IMPORTED` / `PRESERVED_AS_ATTACHMENT` / `FAILED_SOURCE_READ` /
`FAILED_MAPI`), and the source-folder-to-PST-folder map.

The crash window this exists to close: a process crash can happen after
`IMessage::SaveChanges` has already durably written a message into the PST
but before SQLite records that success. `StateDb::check_resume_compatible()`
first rejects resume outright if the source root, output path, root name,
outlook bitness, schema version, or the recomputed manifest hash differ from
what was stored (exit codes `STATE_DB_INCOMPATIBLE` / `SOURCE_CHANGED`).
Once compatibility is confirmed, every row still `PENDING` is recovered
individually and *locally*, not with a global full-PST search:

1. Open only the row's expected destination folder
   (`IPstSession::find_tracked_messages`).
2. Search that folder's contents table for messages whose tracking
   properties match this run's `run_id` **and** the row's
   `source_relative_path` (see Named property strategy below — the search
   also implicitly confirms via SHA-256 when needed for extra certainty).
3. **Exactly one match** → adopt it: mark the row `IMPORTED` with that
   message's entry id, no new copy is created.
4. **No match** → the save never completed; import the file normally.
5. **More than one match** → this is not supposed to be possible for a
   single run id + relative path pair, so resume stops with a deterministic
   integrity error (`RESUME_INTEGRITY_ERROR`, exit code 22) rather than
   guessing which copy is authoritative.

## Named property strategy

Every message WLM2PST imports (including fallback messages) carries six
custom named properties under one fixed, tool-owned property-set GUID
(`kWlm2PstPropertySetGuid`, `{D8E3C2A6-51B4-4E0D-9A67-3F2B8C1D4A90}`,
generated once and never changed):

| Property (`Wlm2Pst.*`, `MNID_STRING`) | Type | Purpose |
|---|---|---|
| `SourceRelativePath` | `PT_UNICODE` | Identifies the source file for resume/validation. |
| `SourceSha256` | `PT_UNICODE` (lowercase hex) | Content identity, independent of path. |
| `ImportVersion` | `PT_UNICODE` | Tool version that performed the import. |
| `RunId` | `PT_UNICODE` | Job GUID; scopes crash-window and validation lookups to one run. |
| `SourceSize` | `PT_I8` | Source file size in bytes. |
| `SourceLastWriteUtc` | `PT_SYSTIME` | Source file's last-write time. |

`resolve_tracking_tags()` resolves all six IDs once per session against the
open store via `GetIDsFromNames` (named-property IDs are store-wide) and
reuses the resulting `TrackingTags` for every message; `set_tracking_properties()`
writes all six before the message's first `SaveChanges`, as MAPI requires
for flags that must be correct before the initial save. Because the
properties live under a private GUID rather than a well-known property set,
they do not appear in normal Outlook views.

## Validation strategy

WLM2PST never reports success immediately after the last import. After the
conversion session is fully torn down (store closed, logged off, profile
deleted, MAPI/COM uninitialized), a **second, independent** MAPI session is
opened — a fresh `WLM2PST-VALIDATE-{GUID}` profile pointed at the
now-existing PST (`must_exist = true` in `IMapiEnvironment::create_session`,
`profile_purpose = "validate"`) — specifically so validation cannot be
fooled by any in-memory state left over from conversion; it can only see
what actually landed durably in the file.

Validation then, via the same `IPstSession` interface used for conversion:

- Walks every folder under the tool root with an iterative depth-first
  traversal (`list_tool_folders()`), recording each folder's
  `PR_CONTENT_COUNT` and comparing message counts against the state
  database's per-folder counters.
- Reads every tool-owned message's tracking properties in each folder
  (`read_tracked_messages()`) and cross-checks them against the state
  database: every `IMPORTED` row must have exactly one matching message;
  every `PRESERVED_AS_ATTACHMENT` row must have exactly one fallback
  message that actually carries an attachment; no unexpected duplicate may
  exist for the same run id and source path.
- Produces one of `VALIDATED`, `VALIDATED_WITH_FALLBACKS`, or
  `VALIDATION_FAILED` (`ValidationStatus` in `common/model.h`).

On `VALIDATION_FAILED` the PST and all logs/state are left exactly as they
are — nothing is deleted — and the process exits `VALIDATION_FAILED` (19)
with the failing folder, relative source path, and technical error recorded
in the JSON report.

## Privacy boundaries

The privacy contract is centralized, not scattered: `Logger`
(`src/common/logging/logger.h`) documents in its class comment exactly which
fields are safe to log (relative paths, target folder paths, sizes,
SHA-256, statuses, attempt counts, HRESULT/Win32 codes, timings, counts,
bitness) and states that it performs no redaction of its own — every call
site is responsible for upholding the contract, including at `--verbose`.
The state database's header comment repeats the same boundary for what may
be persisted to SQLite. See `docs/privacy.md` for the full list, including
the one deliberate, documented exception: the fallback message's *subject*
contains the original filename by design (spec section 16), while every log
line and report field uses only the relative path.

## The portable orchestration seam

Everything above the real MAPI implementation — preflight sequencing, the
import loop, resume decisions, and validation logic — depends only on two
interfaces declared in `src/worker/import_engine.h`:

- `IMapiEnvironment` — preflight check, session creation, stale-profile
  cleanup.
- `IPstSession` — one open conversion or validation session: folder
  creation, EML import (both attempts), fallback import, crash-window
  lookup, folder/message enumeration for validation, orderly close.

The single real implementation of both (`MapiEnvironment` /
`MapiPstSession` in `src/worker/mapi/` via `import_engine.h`'s
`make_mapi_environment()`) lives entirely in the Windows-only `wlm2pst_mapi`
static library. Because the orchestration layer never references MAPI types
directly, it can be — and is — exercised by unit tests with fake
implementations of these two interfaces on any platform, without Outlook or
even Windows present, matching the project's portability rule that only
`src/worker/mapi/**` and `*_win.cpp` files are allowed to depend on
`<windows.h>` or real MAPI headers.
