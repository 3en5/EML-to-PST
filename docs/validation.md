# PST validation

WLM2PST never reports success immediately after the last file is imported.
Every run ends with an independent validation phase that reopens the
finished PST from scratch and checks it against the resume state database,
rather than trusting the in-memory state accumulated during the import
loop. This document describes that sequence, its result values, and what
happens when it fails.

## Why a separate phase

The conversion session and the validation session are deliberately two
different MAPI sessions, with the first fully torn down before the second
begins. This means validation can only see what the PST provider actually
made durable on disk — it cannot be fooled by anything still cached in the
conversion session's memory. See ARCHITECTURE.md's "Validation strategy"
section for how this fits into the overall design.

## The validation sequence

1. Close every message, attachment, stream, and folder interface still open
   from the conversion session.
2. Log off the conversion MAPI session (`PstStore::close`).
3. Delete the conversion profile (`TemporaryProfile::remove`).
4. Uninitialize MAPI and COM for that session
   (`MapiEnvironment` destructor tears down `MapiRuntime` and `ComInit`).
5. Begin a fresh validation phase — nothing from the conversion session is
   reused.
6. Create a new temporary profile named `WLM2PST-VALIDATE-{GUID}`
   (`kValidateProfilePrefixW`), distinct from the `WLM2PST-{GUID}` prefix
   used during conversion, via
   `IMapiEnvironment::create_session(..., profile_purpose = "validate")`.
7. Attach the **existing** PST to the validation profile
   (`must_exist = true`; the file's presence is confirmed with
   `GetFileAttributesW` before the profile is even created, so a missing
   PST fails fast and clearly).
8. Open the store and the tool root folder (same `root_name` used for the
   conversion run; `ensure_root_folder` with `OPEN_IF_EXISTS` semantics).
9. Verify every folder expected from the state database's folder map
   actually exists in the PST (`list_tool_folders()` — an iterative,
   deterministic depth-first walk of everything under the tool root).
10. Verify per-folder message counts (`PR_CONTENT_COUNT`) against the
    counts recorded in the state database for that folder.
11. Enumerate every tool-owned message across all folders
    (`read_tracked_messages()`) and verify its six `Wlm2Pst.*` tracking
    properties are present and well-formed (see ARCHITECTURE.md's "Named
    property strategy").
12. Verify each state-database row with status `IMPORTED` has exactly one
    matching message (same run id + source relative path).
13. Verify each row with status `PRESERVED_AS_ATTACHMENT` has exactly one
    matching fallback message, and that the message actually carries an
    attachment (`PR_HASATTACH`) — a fallback row without an attached EML
    would mean the "preserve the original unchanged" guarantee was not
    honored.
14. Verify no unexpected tool-owned duplicate exists for the same run id
    and source relative path — i.e. that nothing beyond what the state
    database expects is sitting in the PST under this run's tracking
    properties.
15. Close the store, log off, and remove the validation profile
    (`WLM2PST-VALIDATE-{GUID}`), leaving the PST itself untouched by the
    validation session.

## Result values

Validation produces exactly one of these (`ValidationStatus` in
`src/common/model.h`):

| Value | Meaning |
|---|---|
| `VALIDATED` | Every check passed; no file needed fallback handling. |
| `VALIDATED_WITH_FALLBACKS` | Every check passed, but one or more source files were preserved as `_Conversion Errors` fallback attachments rather than converted normally. This is still a successful, validated PST — see exit code 2. |
| `VALIDATION_FAILED` | One or more checks (steps 9–14) failed. |

The result is recorded in the JSON report's `validationStatus` field and
drives the process exit code: a clean `VALIDATED` run with no read failures
exits 0; `VALIDATED_WITH_FALLBACKS` (or any read failures alongside a
successful validation) exits 2 or 3 as appropriate; `VALIDATION_FAILED`
exits 19 regardless of how the import loop itself went.

## Failure behavior

If validation fails:

- **The PST is not deleted.** Whatever was actually written is left exactly
  as validation found it.
- **All logs and state are preserved** — the `.wlm2pst.log`,
  `.wlm2pst-state.sqlite`, and `.wlm2pst-report.json` files are not removed
  or rolled back.
- **The process exits with `VALIDATION_FAILED` (19).**
- **The JSON report includes the failing folder, the relative source path
  involved, and the technical error** for whichever of steps 9–14 failed
  first, so the failure can be investigated without re-running the tool
  from scratch.

WLM2PST does not run the Inbox Repair Tool (`scanpst.exe`) automatically
and does not shell out to the Outlook UI at any point in validation — a
validation failure is reported for a human to investigate, not
auto-remediated.
