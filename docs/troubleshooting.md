# Troubleshooting

Each scenario below lists the symptom, the underlying cause, the fix, and
the exit code WLM2PST returns so scripts and support notes can key off it
directly (see the exit-code table in README.md for the complete list).

## Only New Outlook is installed

**Symptom:** the launcher prints that only the new Outlook app was found and
exits immediately.

**Cause:** the Microsoft 365 "new Outlook" app does not implement Extended
MAPI, so there is nothing for WLM2PST to drive. The launcher's detector
specifically checks for a New Outlook (appx) alias
(`%LOCALAPPDATA%\Microsoft\WindowsApps\olk.exe`) when it cannot find a
classic Outlook install, and distinguishes this case from "no Outlook at
all" so the message is actionable.

**Fix:** install or switch back to classic (desktop) Outlook. In new
Outlook, use **View -> Try the new Outlook** to toggle it off, or install
classic Outlook via Microsoft 365 Apps / Office setup.

**Exit code:** 14 (`OUTLOOK_UNAVAILABLE`).

## Classic Outlook is not installed at all

**Symptom:** the launcher reports that classic Outlook could not be found.

**Cause:** no signal from any of the detector's layered checks (Click-to-Run
platform key, Office/MSI bitness keys, `App Paths`, `OUTLOOK.EXE` PE header)
located a classic Outlook installation.

**Fix:** install classic Outlook (desktop MSI or Click-to-Run) on the
machine that will run the conversion. WLM2PST needs no mail account
configured in it — it creates its own temporary profile.

**Exit code:** 14 (`OUTLOOK_UNAVAILABLE`).

## Bitness mismatch / matching worker missing

**Symptom:** the launcher reports that the matching worker is unavailable,
or that Outlook's architecture could not be confirmed.

**Cause:** either classic Outlook's bitness could not be determined
confidently, or the worker executable that matches the detected bitness
(`wlm2pst-worker-x86.exe` or `wlm2pst-worker-x64.exe`) is not present next
to `wlm2pst.exe` — for example, an incomplete extraction of the package, or
only one architecture was built.

**Fix:** re-extract/reinstall the full WLM2PST package so both worker
executables are present alongside the launcher. If Outlook's bitness
genuinely cannot be determined, repair the Outlook installation (a broken
Click-to-Run configuration can hide this signal).

**Exit code:** 15 (`BITNESS_MISMATCH`).

## MAPI initialization errors

**Symptom:** the worker fails during preflight with a `CoInitializeEx`,
`MAPIInitialize`, or `IProfAdmin`/`IMsgServiceAdmin` failure, or reports the
Unicode PST provider could not be created/configured.

**Cause:** classic Outlook's MAPI stack is present but not currently
loadable — a pending Outlook update, a repair-pending install, another
process holding an exclusive MAPI lock, or a corrupted Outlook profile
store. `IMapiEnvironment::preflight_check()` deliberately runs this whole
sequence (load MAPI, instantiate the MIME converter, create and delete a
throwaway profile, probe the Unicode PST service) before any output file is
touched, specifically to surface exactly this class of failure early.

**Fix:** close Outlook and any other MAPI-using process, let pending Office
updates finish, then retry. If the failure persists, run **Office ->
Repair** (Quick Repair, then Online Repair if needed) on the classic Outlook
installation.

**Exit codes:** 14 (`OUTLOOK_UNAVAILABLE`) for MAPI/COM init or converter
failures; 16 (`PST_PROVIDER_FAILURE`) specifically when the Unicode PST
service itself cannot be created or configured.

## Output already exists

**Symptom:** WLM2PST refuses to run, reporting the output PST already
exists.

**Cause:** WLM2PST never overwrites an existing PST unless explicitly told
to — this is deliberate, not a bug.

**Fix:** choose a new `--output` path, or add `--overwrite` if you
specifically want to discard a previous WLM2PST-owned output and start
over. `--overwrite` only deletes files it can prove it created itself (see
"Output already exists but --overwrite refuses to delete it" below) — it
will never be applied to `--resume` runs, since the two flags are mutually
exclusive.

**Exit code:** 12 (`OUTPUT_EXISTS`).

## Output already exists but `--overwrite` refuses to delete it

**Symptom:** `--overwrite` was passed, but WLM2PST still stops instead of
deleting the existing PST.

**Cause:** this is a deliberate safety check, not a false failure.
`--overwrite` may only delete files it can prove it owns: the matching
`<output>.wlm2pst-state.sqlite` must exist, identify the PST as
WLM2PST-created, record the same absolute output path, and carry a valid
run id. If a `.pst` file exists at the target path without a valid matching
state database — for example, an unrelated PST someone happened to save
under that name — WLM2PST stops and leaves it untouched rather than risk
deleting a file it did not create.

**Fix:** pick a different `--output` path, or manually confirm and remove
the existing file yourself if you are certain it is safe to discard.

**Exit code:** 12 (`OUTPUT_EXISTS`).

## PST is locked by another process

**Symptom:** WLM2PST fails to create or open the output PST because the
file is in use.

**Cause:** the output path is already open by Outlook, another WLM2PST run,
or some other process holding a lock on it.

**Fix:** close Outlook and any other program that might have the PST open,
confirm no other WLM2PST process is running against the same output path,
then retry.

**Exit code:** 16 (`PST_PROVIDER_FAILURE`) when the provider cannot open
the file at session-creation time; 18 (`OUTPUT_WRITE_FAILURE`) if the lock
appears mid-run during a write.

## Insufficient disk space

**Symptom:** preflight reports insufficient free disk space and stops
before creating anything.

**Cause:** the conservative space estimate
(`estimated_pst_bytes = total_eml_bytes * 1.30 + 1 GiB`,
`required_free_bytes = estimated_pst_bytes + 2 GiB`) exceeds the free space
available on the destination volume. The message reports both the required
and the available byte counts.

**Fix:** free up space on the destination volume, or choose an
`--output` path on a volume with more room. This check runs before any PST
is created, so nothing partial is left behind.

**Exit code:** 13 (`INSUFFICIENT_DISK_SPACE`). (A related but distinct
check — the fixed ~45 GiB safe single-PST ceiling — fails separately with
17, `PST_SIZE_CEILING_EXCEEDED`, and is not a disk-space problem: it exists
purely to avoid producing a PST near common Outlook size limits.)

## Source changed since the manifest was created

**Symptom:** a `--resume` run stops, reporting the source tree changed.

**Cause:** resume recomputes the full source manifest and its hash before
resuming any file, and refuses to proceed if files were added, removed, or
modified compared with the manifest stored at job creation — silently
resuming against a moved target could produce inconsistent or duplicate
results.

**Fix:** if the source folder is expected to be stable (which it should be
during a migration), investigate what changed — this can indicate Windows
Live Mail or another process wrote to the source tree during conversion,
which the tool explicitly checks for and should not happen. If the change
was intentional, start a fresh conversion (new `--output`, or `--overwrite`
the previous attempt) rather than resuming.

**Exit code:** 21 (`SOURCE_CHANGED`).

## Resume mismatch (incompatible state database)

**Symptom:** `--resume` stops, reporting the state database is incompatible.

**Cause:** `check_resume_compatible()` found a mismatch between the current
run's parameters and what is recorded in
`<output>.wlm2pst-state.sqlite` — a different `--source`, `--output`,
`--root-name`, worker architecture, or an incompatible schema version. This
also covers the case where `--resume` is passed but no matching state
database exists at all next to the target PST.

**Fix:** confirm the `--source`, `--output`, and `--root-name` used for
`--resume` exactly match the original run. If they genuinely need to
differ, this is not a resumable job — start a fresh conversion instead.

**Exit code:** 20 (`STATE_DB_INCOMPATIBLE`).

## Resume crash-window integrity error

**Symptom:** `--resume` stops, reporting an integrity error during crash
recovery.

**Cause:** while re-checking rows left `PENDING` from an interrupted run,
resume found *more than one* tool-owned message in the expected folder
matching this run's id and the row's source relative path. Exactly one
match is the only unambiguous case resume can safely adopt automatically;
more than one means something wrote a duplicate that resume cannot silently
resolve without risking either data loss or a wrong adoption.

**Fix:** this indicates the PST was modified by something other than this
WLM2PST run since the crash (including, in principle, a previous WLM2PST
run against the same output without a clean state database). Do not attempt
to resume further; start a fresh conversion to a new output, and treat the
partially-imported PST as needing manual inspection if it must be kept.

**Exit code:** 22 (`RESUME_INTEGRITY_ERROR`).

## Temporary profile cleanup problems

**Symptom:** the run fails during startup cleanup or final teardown with a
temporary-profile error.

**Cause:** profile creation, configuration, or deletion through
`IProfAdmin` failed critically — for example, the MAPI profile store itself
is corrupted, or deletion of a profile WLM2PST just created is refused by
the provider. Startup cleanup only ever targets profiles whose display name
begins with the `WLM2PST-` prefix, so this is never a sign that a real
Outlook profile was touched.

**Fix:** if this happens repeatedly, check for leftover `WLM2PST-*` or
`WLM2PST-VALIDATE-*` profiles in Outlook's account/profile manager
(Control Panel -> Mail -> Show Profiles) and remove them manually, then
retry. A corrupted profile store may need an Office repair.

**Exit code:** 23 (`PROFILE_CLEANUP_FAILURE`).

## PST validation failure

**Symptom:** conversion appears to finish importing files, but the run
still exits with a validation failure.

**Cause:** WLM2PST never reports success right after the last import — it
always reopens the finished PST through a brand-new, independent
`WLM2PST-VALIDATE-{GUID}` profile and re-checks it from scratch: every
expected folder exists, per-folder message counts match the state database,
every tracked message's properties are intact, every `IMPORTED` row has
exactly one matching message, every `PRESERVED_AS_ATTACHMENT` row has
exactly one fallback message with its EML attachment, and no unexpected
duplicate tool-owned message exists for the run. Any mismatch here means
the on-disk PST does not match what the state database believes was
written.

**Fix:** the PST, state database, and all logs are deliberately left
exactly as they were — nothing is deleted on validation failure — so first
check the JSON report's `validationStatus` and warnings for the specific
failing folder and relative source path. If the underlying cause is
transient PST provider corruption, a fresh `--overwrite` run is usually the
most reliable recovery; if it recurs, treat it as a signal to inspect the
PST provider / Outlook installation itself.

**Exit code:** 19 (`VALIDATION_FAILED`).
