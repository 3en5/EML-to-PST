# Claude Code Build Prompt: WLM2PST

You are a senior Windows C++ engineer with deep experience in Extended MAPI, Outlook data stores, COM, MIME, crash-safe migration tooling, and production-grade command-line applications.

Your task is to build a complete, working, production-oriented Windows CLI application named `WLM2PST`.

The application has exactly one business purpose:

> Convert a recursive folder tree containing Windows Live Mail `.eml` files into one Unicode `.pst` file that can be opened locally in classic Microsoft Outlook.

This is not a cloud migration tool. Do not add Microsoft 365, Exchange Online, Graph API, IMAP upload, MSG export, MBOX export, EML cleanup as a separate product, or any other destination. The only mail output is one local Unicode PST file.

Do not stop after producing a design, proof of concept, partial skeleton, or pseudocode. Inspect the repository, implement the application, build it, run all tests that the environment supports, fix failures, and leave the repository in a usable state.

If the environment does not contain classic Outlook or the required Extended MAPI runtime, still complete all source code, build logic, unit tests, synthetic fixtures, and compile-time checks that can run. Clearly document which Outlook-dependent integration tests could not be executed. Never fabricate test results.

---

## 1. Non-negotiable product scope

### Input

A local Windows directory containing `.eml` files in an arbitrary recursive directory structure, typically copied from Windows Live Mail.

Example:

```text
C:\Mail\Windows Live Mail\
├── account@example.com\
│   ├── Inbox\
│   │   ├── 001.eml
│   │   └── 002.eml
│   ├── Sent Items\
│   │   └── 003.eml
│   └── Customers\
│       └── Client A\
│           └── 004.eml
└── Storage Folders\
    └── Archive\
        └── 005.eml
```

### Output

Exactly one new Unicode PST file, for example:

```text
D:\Migration\Rina-Mail.pst
```

The PST must preserve the relative directory structure under one root folder inside the PST.

Expected PST structure:

```text
Windows Live Mail
├── account@example.com
│   ├── Inbox
│   ├── Sent Items
│   └── Customers
│       └── Client A
└── Storage Folders
    └── Archive
```

### Explicitly out of scope

Do not implement any of the following:

- Microsoft 365 migration.
- Exchange or Exchange Online migration.
- Graph API.
- IMAP upload.
- Cloud storage.
- OST creation.
- MSG output.
- MBOX output.
- Contact migration.
- Calendar migration.
- Task migration.
- Windows Live Mail account settings migration.
- Signature migration.
- Rule migration.
- Automatic duplicate deletion.
- Automatic splitting into multiple PST files.
- Merging into an arbitrary existing PST.
- A GUI.
- A web service.
- Telemetry.
- Any network dependency at runtime.

---

## 2. Required technical strategy

Do not implement the PST file format directly.

Use classic Outlook's installed Extended MAPI implementation and the Microsoft PST provider to create and write the PST. Use Microsoft's MIME-to-MAPI conversion path to import each EML as an Outlook message.

Required conversion path:

```text
EML file
  -> IStream
  -> IConverterSession::MIMEToMAPI
  -> IMessage
  -> Microsoft Unicode PST provider
  -> PST file
```

Use the same general technical approach demonstrated by Microsoft's MFCMAPI project for:

- Loading Outlook MAPI safely.
- Creating a temporary MAPI profile.
- Adding and configuring a Unicode PST message service.
- Opening the PST message store.
- Importing EML content into an `IMessage` through `IConverterSession`.

Do not automate the Outlook user interface. Do not use Outlook Interop or the Outlook Object Model as the main conversion engine. Do not create `MailItem` objects one by one through COM automation.

The application must work without a configured mail account. It must use a tool-owned temporary MAPI profile rather than the user's normal Outlook profile.

---

## 3. Target platform and toolchain

Use the following implementation stack unless the existing repository already contains a clearly superior compatible setup:

- Language: C++20.
- Compiler: Microsoft Visual C++.
- Build system: CMake with Visual Studio generators and CMake presets.
- Operating systems: Windows 10 and Windows 11.
- Mail runtime: classic Outlook Extended MAPI.
- Database: SQLite for crash-safe resume state.
- Hashing: Windows CNG SHA-256.
- JSON report: a lightweight JSON library such as `nlohmann-json`, or a small internal writer if dependency policy requires it.
- Tests: Catch2, GoogleTest, or the repository's existing test framework.

Prefer official Microsoft headers, SDKs, and code where possible. For MAPI loading, use Microsoft's MAPI Stub Library approach rather than linking blindly to the system stub in a way that may load the wrong MAPI implementation.

Do not add an open-source license unless the repository already has one or the user explicitly requested one.

---

## 4. Process architecture and Outlook bitness

Extended MAPI requires the process bitness to match the installed classic Outlook bitness.

The packaged application must contain:

```text
wlm2pst.exe
wlm2pst-worker-x86.exe
wlm2pst-worker-x64.exe
```

Responsibilities:

### `wlm2pst.exe`

A small launcher that:

1. Parses enough of the command line to support `--help` and `--version`.
2. Detects whether classic Outlook is installed.
3. Detects Outlook's effective bitness reliably.
4. Selects the matching worker.
5. Starts the worker with the original command line.
6. Forwards Ctrl+C and process exit codes.
7. Produces a clear error if the matching worker is unavailable.

Build the launcher as a 32-bit executable so it can run on modern 64-bit Windows and inspect both 32-bit and 64-bit registry views. Use explicit registry view flags where required.

### Workers

- `wlm2pst-worker-x86.exe` must be built as Win32.
- `wlm2pst-worker-x64.exe` must be built as x64.
- Both workers must use the same source code and behavior.
- The worker performs all MAPI, PST, conversion, resume, and validation work.

Outlook detection must not rely on one registry key only. Implement a layered detector that can inspect relevant Click-to-Run and MSI locations, locate `OUTLOOK.EXE`, and confirm PE architecture when possible. Keep all detection logic covered by unit tests using abstractions or injectable registry/filesystem readers.

If only New Outlook is installed and classic Outlook MAPI is unavailable, exit with a specific error explaining that classic Outlook is required.

---

## 5. Required CLI

Minimum syntax:

```powershell
wlm2pst.exe --source "C:\Mail\Windows Live Mail" --output "D:\Migration\Rina-Mail.pst"
```

Required options:

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

Do not add destination modes or unrelated conversion switches.

Examples:

```powershell
wlm2pst.exe --source "C:\OldMail" --output "D:\Migration\OldMail.pst"
```

```powershell
wlm2pst.exe --source "C:\OldMail" --output "D:\Migration\OldMail.pst" --root-name "Rina old mail"
```

```powershell
wlm2pst.exe --source "C:\OldMail" --output "D:\Migration\OldMail.pst" --resume
```

Rules:

- `--source` and `--output` are mandatory for conversion.
- Output must end in `.pst` case-insensitively.
- Output must be a local filesystem path.
- Reject UNC paths and remote/network drives.
- Reject output inside the source tree.
- Reject an output file already open by another process.
- Never overwrite an existing PST unless `--overwrite` is explicitly provided.
- `--overwrite` may delete only files that are clearly owned by this tool and associated with the selected output path.
- `--resume` is valid only when both the PST and a compatible state database exist.
- `--resume` and `--overwrite` are mutually exclusive.

---

## 6. Preflight behavior

Before creating or modifying a PST, perform a complete preflight.

### Source validation

Verify that:

- The source directory exists.
- It is readable.
- At least one `.eml` file exists recursively.
- Extension matching is case-insensitive.
- Directory traversal does not follow reparse points, junctions, or symbolic links by default.
- The source is not inside the output directory and vice versa.
- `wlmail.exe` is not running.

If Windows Live Mail is running, stop before conversion and explain that the source must remain stable.

### Source inventory

Scan recursively and create a deterministic manifest containing at least:

- Normalized relative path.
- Absolute source path.
- File size.
- Last write time in UTC.
- Stable ordering key.

Sort deterministically by normalized relative path using case-insensitive Windows semantics with a stable ordinal fallback.

Display a preflight summary:

```text
Source:       C:\Mail\Windows Live Mail
EML files:   18,742
Folders:     137
Source size: 13.4 GiB
Output:      D:\Migration\Rina-Mail.pst
Outlook:     Classic Outlook 64-bit
```

### Free space check

Use a conservative estimate:

```text
estimated_pst_bytes = total_eml_bytes * 1.30 + 1 GiB
required_free_bytes = estimated_pst_bytes + 2 GiB
```

Use 64-bit arithmetic and show both required and available space on failure.

### Safe single-PST size ceiling

The tool must create only one PST. To avoid producing a file near common Outlook PST limits, reject a job when the estimated output exceeds 45 GiB.

Return a dedicated error explaining that automatic PST splitting is intentionally unsupported.

Do not modify Outlook PST size-related registry settings.

### Outlook and MAPI validation

Before creating the output:

- Confirm classic Outlook MAPI can be loaded.
- Confirm the worker bitness matches the Outlook MAPI bitness.
- Initialize COM and MAPI.
- Confirm the MIME converter can be instantiated.
- Confirm a temporary profile can be created and removed.
- Confirm the Unicode PST provider is available.

Do not leave a test profile behind after preflight.

---

## 7. Long path, Unicode, and filesystem requirements

The application must be Unicode-first.

Requirements:

- Use wide-character Windows APIs.
- Use `std::filesystem::path` carefully, preserving native wide paths.
- Add an application manifest declaring long-path awareness.
- Support paths longer than `MAX_PATH` when the operating system policy allows it.
- Do not convert paths through the active ANSI code page.
- Preserve Hebrew and other non-ASCII folder and file names.
- Stream EML data from disk. Do not load the entire archive or large files into memory.
- Open source EML files read-only and deny writes while each file is being processed.

Do not modify source files under any circumstances.

---

## 8. PST creation through a temporary MAPI profile

Create a unique temporary profile for each conversion run.

Example profile name:

```text
WLM2PST-{GUID}
```

High-level sequence:

```text
CoInitializeEx
MAPIInitialize
MAPIAdminProfiles
IProfAdmin::CreateProfile
IProfAdmin::AdminServices
IMsgServiceAdmin2::CreateMsgServiceEx
Configure the Unicode PST service with the requested output path
MAPILogonEx using the explicit temporary profile
Locate and open the newly created PST message store
Open the IPM subtree root
Create the WLM2PST root folder
Import folders and messages
Release all MAPI objects
MAPILogoff
Delete the temporary profile
MAPIUninitialize
CoUninitialize
```

Use Outlook's Unicode PST provider. Do not create an ANSI PST.

Keep PST provider service names, provider GUIDs, profile properties, and compatibility handling isolated in one well-documented module. Avoid scattering magic strings and property tags throughout the code.

Use RAII for:

- COM initialization.
- MAPI initialization.
- MAPI buffers.
- MAPI interfaces.
- Profiles.
- Sessions.
- Stores.
- Folders.
- Messages.
- Attachments.
- Streams.
- SQLite connections and transactions.
- File handles.

A normal exit, Ctrl+C path, or exception must attempt to release the store and remove temporary profiles.

At startup, clean up only stale temporary profiles that are provably owned by WLM2PST. Never remove user profiles.

---

## 9. Folder mapping rules

The filesystem folder tree is the source of truth in version 1.0.

Do not parse `Mail.MSMessageStore` and do not depend on the Windows Live Mail database.

Mapping rules:

1. Create one root folder inside the PST using `--root-name` or `Windows Live Mail`.
2. Recreate relative source folders beneath it.
3. Place each EML in the PST folder corresponding to its parent source directory.
4. EML files directly under the selected source root go into:

```text
Root Messages
```

5. Create a folder when it contains at least one EML directly or somewhere below it.
6. Empty filesystem folders with no EML descendants do not need to be recreated.
7. Do not merge similarly named folders.
8. Do not translate folder names.
9. Do not special-case folders into Outlook's default Inbox, Sent Items, or Drafts folders. Preserve them as ordinary folders beneath the tool root.

### Folder name normalization

Preserve the original name whenever possible.

For invalid or unsafe names:

- Trim trailing spaces and trailing periods.
- Replace NUL and control characters.
- Keep a deterministic mapping.
- Shorten extremely long names and append a short hash.
- Detect collisions after normalization.
- Resolve collisions as:

```text
Customers
Customers (2)
Customers (3)
```

Record every renamed folder in the report.

Maximum supported depth: 100 levels. Reject a deeper path as invalid input rather than risking recursion or provider failure.

Use iterative traversal where practical to avoid stack exhaustion.

---

## 10. EML to MAPI conversion

For every source EML:

1. Open the file read-only as an `IStream` without loading it all into RAM.
2. Calculate SHA-256 while streaming or in a separate buffered pass.
3. Create a new `IMessage` in the destination folder.
4. Instantiate or reuse a valid `IConverterSession` within the worker thread.
5. Call `IConverterSession::MIMEToMAPI`.
6. Apply required post-conversion metadata and state.
7. Add WLM2PST tracking properties.
8. Save the message.
9. Read and store its `PR_ENTRYID` when available.
10. Commit the success to SQLite.

Use MIME conversion flags appropriate for SMTP/MIME messages and preservation of BCC when present. Support global/international headers where the installed Outlook version supports the relevant converter flag.

Do not force HTML to RTF. Do not sanitize, render, or execute message content.

Expected content preservation:

- Subject.
- From.
- Sender.
- To.
- CC.
- BCC when present in the EML.
- Plain text body.
- HTML body.
- Multipart alternatives.
- MIME attachments.
- Inline CID images.
- Internet transport headers.
- Message-ID.
- In-Reply-To.
- References.
- International header text.
- Hebrew text and attachment names.

The Microsoft converter is the primary parser. Do not replace it with a hand-built MIME implementation.

---

## 11. Lightweight header inspection

Implement a small, defensive header inspector for metadata decisions and fallback behavior. It is not the primary MIME parser.

It should:

- Read only the RFC-style header section, with a configurable safety limit such as 1 MiB.
- Detect the header/body separator.
- Unfold continuation lines.
- Match header names case-insensitively.
- Extract raw values for `Date`, `Received`, `Message-ID`, and `X-Unsent`.
- Detect obvious malformed header conditions.
- Never decode or rewrite the message body.

Use it for:

- Draft detection.
- Date fallback.
- Diagnostics.
- Validation of converter output where useful.

Do not log header values containing personal content by default.

---

## 12. Message state rules

### Read state

Mark every imported archive message as read.

Reason: EML does not reliably contain the Windows Live Mail per-user read/unread state. Do not guess based on file timestamps, filesystem attributes, or folder location.

Set message flags before the first save where MAPI requires that behavior.

### Sent message detection

Treat a message as sent when any path component matches a configurable internal list such as:

```text
Sent
Sent Items
Sent Mail
Sent Messages
Items Sent
פריטים שנשלחו
דואר שנשלח
נשלח
```

The source code may include this list in Unicode. Keep it centralized and covered by tests.

For sent messages:

- Ensure `MSGFLAG_UNSENT` is not set.
- Set `MSGFLAG_FROMME` when valid and safe before first save.
- Preserve recipients.
- Preserve or set the client submit time.

Do not submit or send any message.

### Draft detection

Treat a message as a draft when:

- `X-Unsent: 1` is present, or
- a path component matches a known Drafts folder name.

For drafts:

- Set `MSGFLAG_UNSENT` before first save.
- Keep the message local.
- Never call `SubmitMessage`.

### Message class

Preserve the converter-produced message class where valid. Default to `IPM.Note` only when creating a fallback message manually.

---

## 13. Date handling

Do not use the file modification timestamp as the first choice.

After MIME conversion, inspect the MAPI time properties produced by Outlook.

### Received messages

Priority:

```text
1. Valid PR_MESSAGE_DELIVERY_TIME produced by the converter
2. First valid Received header timestamp according to the chosen documented rule
3. Valid Date header
4. Source file LastWriteTimeUtc
```

### Sent messages

Priority:

```text
1. Valid PR_CLIENT_SUBMIT_TIME produced by the converter
2. Valid Date header
3. Source file LastWriteTimeUtc
```

Implement a tolerant RFC 5322-style date parser for common real-world mail formats, including numeric timezone offsets and common obsolete formats. Keep it isolated and unit tested.

Rules:

- Convert parsed times to UTC for MAPI storage.
- Preserve original headers in the message.
- Never rewrite source content.
- Record a warning when fallback reaches the filesystem timestamp.
- Treat impossible or wildly invalid dates as missing.

Use explicit, documented bounds. For example, reject dates earlier than 1970 or more than one year in the future unless a clear business reason exists to preserve them. If you apply bounds, record them in the report and tests.

---

## 14. Duplicate policy

Preserve duplicates.

Do not delete or merge messages based on:

- Message-ID.
- Subject.
- Date.
- Hash.
- Sender and recipient combinations.

Two distinct source files must produce two PST items, even when their content is identical.

The resume system may suppress only accidental re-import of the exact same source file within the same conversion job.

---

## 15. Tracking properties inside imported messages

Create a fixed custom property set GUID owned by WLM2PST and named MAPI properties such as:

```text
Wlm2Pst.SourceRelativePath
Wlm2Pst.SourceSha256
Wlm2Pst.ImportVersion
Wlm2Pst.RunId
Wlm2Pst.SourceSize
Wlm2Pst.SourceLastWriteUtc
```

Requirements:

- Use `GetIDsFromNames` correctly.
- Use stable property names and one fixed GUID.
- Store the relative path as Unicode.
- Store SHA-256 as either binary or lowercase hexadecimal, consistently.
- Set these properties before final `SaveChanges`.
- Use them for resume recovery and final validation.

Do not expose these properties in normal Outlook views.

---

## 16. Malformed EML handling

A malformed EML must not cause the whole conversion to fail.

### Attempt 1: original file

Pass the original file stream to `MIMEToMAPI` without modification.

### Attempt 2: conservative normalized copy

If conversion fails, create a temporary normalized stream or file. Apply only conservative repairs:

- Remove a UTF-8 BOM at the very beginning.
- Normalize header-section line endings to CRLF.
- Remove NUL characters from the header section.
- Add a missing header/body separator only when the boundary can be identified safely.

Do not:

- Rewrite HTML.
- Re-encode the body globally.
- Guess MIME boundaries.
- Drop unknown MIME parts.
- Modify attachment bytes.
- Replace the original file.

### Final fallback: preserve the original EML inside the PST

If both conversion attempts fail, create a normal Outlook message in:

```text
<root-name>\_Conversion Errors
```

Fallback message requirements:

```text
Subject: [EML conversion failed] <original filename>
Message class: IPM.Note
Read state: Read
Body: concise diagnostic information
Attachment: the original EML bytes, unchanged
Attachment MIME type: message/rfc822 when practical
```

The body should include:

- Relative source path.
- Source size.
- SHA-256.
- First and second HRESULT or normalized error code.
- A statement that the original EML is attached unchanged.

Do not place message body content, sender, recipient, or subject from the failed source in the log.

State result:

```text
PRESERVED_AS_ATTACHMENT
```

If the source file cannot be opened at all, record:

```text
FAILED_SOURCE_READ
```

Continue with other files unless a systemic failure occurs.

---

## 17. Crash-safe resume database

Create a state database beside the output PST:

```text
Rina-Mail.pst.wlm2pst-state.sqlite
```

It is operational metadata, not another mail export.

Use SQLite with WAL mode where safe. Ensure database writes are durable enough for crash recovery without excessive per-message disk overhead. Batch transactions conservatively, but never report a message as imported before the PST save has succeeded.

Suggested schema:

### Job table

```text
run_id
schema_version
tool_version
source_root
output_pst
root_name
outlook_bitness
created_at_utc
last_updated_at_utc
manifest_hash
status
```

### Files table

```text
id
relative_source_path
source_size
source_last_write_utc
source_sha256
target_folder_path
target_entry_id
status
attempt_count
last_hresult
started_at_utc
completed_at_utc
```

Statuses:

```text
PENDING
IMPORTED
PRESERVED_AS_ATTACHMENT
FAILED_SOURCE_READ
FAILED_MAPI
```

### Folder map table

```text
source_relative_folder
target_relative_folder
target_entry_id
rename_reason
```

### Resume rules

On a new job:

1. Build the complete source manifest.
2. Store a manifest hash.
3. Create the PST and state database.
4. Mark each file as pending before processing.

On `--resume`:

1. Confirm the PST exists.
2. Confirm the state database exists.
3. Confirm source root, output path, root name, worker architecture, and schema are compatible.
4. Rebuild the manifest.
5. Reject resume if files were added, removed, or changed compared with the stored manifest.
6. Open the existing PST through a fresh temporary profile.
7. Verify the tool root and run ID.
8. Resume pending or recoverable rows only.

### Crash window recovery

A crash can occur after `IMessage::SaveChanges` succeeds but before SQLite records success.

For any row left in `PENDING` during resume:

- Search only the expected destination folder.
- Look for the WLM2PST run ID, source relative path, and SHA-256 properties.
- If exactly one matching message exists, mark it imported without creating another copy.
- If none exists, import it.
- If more than one exists, stop with a deterministic integrity error and document the duplicates.

Do not use a global full-PST search for every item. Build efficient folder-local indexes during resume.

---

## 18. Output ownership and overwrite safety

Beside the PST, create:

```text
<output>.wlm2pst-state.sqlite
<output>.wlm2pst-report.json
<output>.wlm2pst.log
<output>.wlm2pst-errors.csv
```

The errors CSV should be created only when errors or fallback messages exist.

Before `--overwrite` deletes anything:

- Verify the state database identifies the PST as created by WLM2PST.
- Verify the recorded absolute output path matches.
- Verify the run ID is valid.
- Refuse to delete an arbitrary PST without proof of tool ownership.

If a PST exists without a valid matching state database, stop and do not alter it.

---

## 19. Ctrl+C and shutdown behavior

Install a console control handler.

On the first Ctrl+C:

1. Set a cancellation flag.
2. Do not start another message.
3. Finish the current message operation if it is in a safe, bounded state.
4. Save the current message if conversion completed.
5. Commit SQLite state.
6. Release MAPI objects.
7. Log off.
8. Remove the temporary profile when possible.
9. Leave the PST and state database resumable.
10. Exit with code 130.

On a second Ctrl+C, allow a harder termination path but still avoid deliberately corrupting shared state.

Do not call unsafe complex code directly inside the Windows console control callback. Signal cancellation and let the worker thread perform orderly shutdown.

---

## 20. Mandatory PST validation

Do not report success immediately after importing the last file.

After conversion:

1. Close every message, attachment, stream, folder, and store.
2. Log off the MAPI session.
3. Delete the conversion profile.
4. Uninitialize MAPI and COM.
5. Start a fresh validation phase.
6. Create a new temporary profile named similar to:

```text
WLM2PST-VALIDATE-{GUID}
```

7. Attach the existing PST to the validation profile.
8. Open the store and tool root folder.
9. Verify every expected folder exists.
10. Verify per-folder message counts against the state database.
11. Enumerate all tool-owned messages and verify tracking properties.
12. Verify each `IMPORTED` row has one matching message.
13. Verify each `PRESERVED_AS_ATTACHMENT` row has one fallback message containing an EML attachment.
14. Verify no unexpected tool-owned duplicate exists for the same run ID and source path.
15. Close and remove the validation profile.

Validation result values:

```text
VALIDATED
VALIDATED_WITH_FALLBACKS
VALIDATION_FAILED
```

If validation fails:

- Do not delete the PST.
- Preserve all logs and state.
- Exit with a failure code.
- Include the failing folder, relative source path, and technical error in the report.

Do not run Inbox Repair Tool automatically. Do not shell out to Outlook UI.

---

## 21. Logging, privacy, and reports

The program processes potentially sensitive legal and business mail. Logging must be privacy-conscious.

### Never log by default

- Message body.
- HTML.
- Sender address.
- Recipient addresses.
- Message subject.
- Attachment contents.
- Raw MIME headers.

### Allowed operational log fields

- Relative source path.
- Target folder path.
- File size.
- SHA-256.
- Status.
- Attempt number.
- HRESULT.
- Win32 error code.
- Timing.
- Counts.
- Tool and Outlook bitness.

`--verbose` may add call names, provider details, timing, and sanitized paths, but it must still not log message content.

### Console progress

Example:

```text
WLM2PST 1.0.0
Outlook MAPI: 64-bit
Source files: 18,742
Source size: 13.4 GiB

[1,244 / 18,742] 6.6%
Folder: account@example.com\Inbox
Imported: 1,242
Fallback: 1
Failed: 1
Rate: 8.1 messages/sec
```

Do not show the subject or addresses.

### Final console summary

Example:

```text
Conversion completed with warnings.

Imported normally:       18,724
Preserved as attachment:     15
Failed to read:                3
Folders created:             137
PST validation:           PASSED

Output:
D:\Migration\Rina-Mail.pst
```

### JSON report

Create a machine-readable report containing at least:

```json
{
  "toolVersion": "1.0.0",
  "runId": "...",
  "source": "C:\\Mail\\Windows Live Mail",
  "output": "D:\\Migration\\Rina-Mail.pst",
  "startedAtUtc": "2026-08-13T08:12:44Z",
  "completedAtUtc": "2026-08-13T09:03:18Z",
  "sourceFiles": 18742,
  "sourceFolders": 137,
  "sourceBytes": 14388140442,
  "importedNormally": 18724,
  "preservedAsAttachment": 15,
  "failedToRead": 3,
  "outputFolders": 137,
  "validationStatus": "VALIDATED_WITH_FALLBACKS",
  "renamedFolders": [],
  "warnings": []
}
```

Use integer byte counts in JSON rather than localized strings.

### Errors CSV

Columns:

```text
relative_source_path,status,attempt_count,hresult,win32_error,target_folder,source_size,sha256,details
```

CSV must be UTF-8 with BOM so Hebrew paths open correctly in Excel.

---

## 22. Exit codes

Implement these stable process exit codes:

```text
0    Conversion and validation completed with no warnings.
2    Completed and validated, with one or more EML files preserved as fallback attachments.
3    Completed and validated, but one or more source files could not be read.
10   Invalid command-line arguments.
11   Invalid source directory or no EML files found.
12   Output already exists or cannot be safely overwritten.
13   Insufficient free disk space.
14   Classic Outlook or Extended MAPI is unavailable.
15   Worker and Outlook bitness mismatch.
16   Unicode PST provider could not be created or configured.
17   Estimated output exceeds the safe single-PST ceiling.
18   Disk full or output write failure during conversion.
19   PST validation failed.
20   State database is incompatible with the PST or source manifest.
21   Source changed after the manifest was created.
22   Integrity error during resume recovery.
23   Temporary profile creation or cleanup failed critically.
130  User cancelled with Ctrl+C.
```

Keep the exit-code definition centralized and document it in the README.

---

## 23. Fatal versus per-message failures

### Per-message, non-fatal

- MIME conversion failure for one EML.
- Invalid message date.
- Missing subject.
- Missing body.
- Bad attachment name.
- Unsupported MIME part.
- Fallback attachment creation for one message.

### Fatal

- MAPI cannot initialize.
- PST provider unavailable.
- Worker bitness mismatch.
- PST cannot be created or reopened.
- Output disk becomes full.
- SQLite state cannot be written reliably.
- Store-wide `SaveChanges` failures suggest a systemic problem.
- Source manifest changes during the run.
- Resume finds ambiguous duplicate tool-owned messages.
- Final validation cannot open the PST.

Implement a circuit breaker for repeated systemic MAPI failures. For example, if a configurable number of consecutive unrelated messages fail at the same store operation, stop instead of generating thousands of fallback messages for what is actually a broken PST session.

---

## 24. Security requirements

Runtime behavior must be local and offline.

Requirements:

- No HTTP calls.
- No DNS calls initiated by the application.
- No telemetry.
- No automatic crash upload.
- No account login.
- No password storage.
- No mailbox access.
- No shell execution of attachments.
- No HTML rendering.
- No preview generation.
- No antivirus bypass.
- No modification of source EML files.
- No registry changes except those transiently performed by standard MAPI profile APIs.
- Temporary files must use restricted ACLs for the current user.
- Temporary normalized EML copies must be deleted after use.
- Use secure temporary file creation to avoid predictable names.
- Signable release binaries with version resources and Authenticode-compatible build output.

Do not claim that imported messages or attachments are safe. The tool preserves content; it does not disinfect it.

---

## 25. Performance and resource requirements

PST writes must be serialized through one worker thread.

Allowed parallelism:

- Source inventory.
- SHA-256 calculation ahead of import.
- Non-MAPI metadata inspection.

However, do not allow background work to consume excessive memory or race with source stability checks.

Targets for acceptance testing:

- At least 5 small messages per second on a local SSD in a representative test environment.
- Working set below 512 MiB for a 100,000-message archive, excluding Outlook/MAPI runtime overhead that cannot be controlled.
- No whole-archive loading.
- No whole-file loading for large EML files.
- 64-bit byte counters everywhere.
- Progress updates no more frequently than necessary; avoid slowing conversion with excessive console output.
- Resume must not re-import already completed files.

These are engineering targets, not guaranteed throughput on every computer.

---

## 26. Recommended repository structure

Adapt to the existing repository where appropriate, but aim for a clean separation similar to:

```text
/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── README.md
├── BUILDING.md
├── ARCHITECTURE.md
├── CHANGELOG.md
├── cmake/
├── docs/
│   ├── troubleshooting.md
│   ├── mapi-notes.md
│   ├── privacy.md
│   └── validation.md
├── scripts/
│   ├── build.ps1
│   ├── test.ps1
│   └── package.ps1
├── src/
│   ├── common/
│   │   ├── errors/
│   │   ├── logging/
│   │   ├── paths/
│   │   ├── hashing/
│   │   ├── unicode/
│   │   └── version/
│   ├── launcher/
│   │   ├── main.cpp
│   │   ├── outlook_detector.cpp
│   │   └── worker_launcher.cpp
│   └── worker/
│       ├── main.cpp
│       ├── cli/
│       ├── preflight/
│       ├── scanner/
│       ├── folder_mapping/
│       ├── headers/
│       ├── dates/
│       ├── mapi/
│       │   ├── mapi_runtime.cpp
│       │   ├── temporary_profile.cpp
│       │   ├── pst_store.cpp
│       │   ├── mime_converter.cpp
│       │   ├── message_properties.cpp
│       │   └── mapi_raii.cpp
│       ├── resume/
│       ├── validation/
│       ├── reporting/
│       └── cancellation/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── e2e/
│   └── fixtures/
└── packaging/
    └── README.txt
```

Do not create giant source files. Keep MAPI-specific code isolated behind interfaces so non-MAPI logic can be tested without Outlook installed.

---

## 27. Coding standards

- Use C++20.
- Use RAII consistently.
- Avoid raw owning pointers.
- Avoid naked `new` and `delete`.
- Wrap HRESULT handling with context-rich error objects.
- Convert Win32 errors using `GetLastError` immediately where relevant.
- Keep user-facing messages separate from low-level diagnostics.
- Use `std::chrono` for time handling where possible.
- Use `std::filesystem` with native Windows paths.
- Use prepared SQLite statements.
- Use transactions.
- Validate every integer conversion.
- Avoid silent truncation between 64-bit sizes and MAPI types.
- Centralize property tags, GUIDs, provider identifiers, folder aliases, exit codes, and report schema versions.
- Add comments explaining MAPI-specific lifetime rules and non-obvious provider behavior.
- Treat compiler warnings as errors for project code.
- Enable `/W4`, `/permissive-`, Unicode, and suitable security hardening flags.
- Enable control-flow protection and modern linker protections where compatible.
- Do not disable exceptions or RTTI unless the existing project has a strong policy and the implementation remains clear.

Add an application manifest for:

- Long path awareness.
- Supported Windows versions.
- Requested execution level `asInvoker`.

The application must not require administrator privileges for normal operation.

---

## 28. Testing requirements

Create synthetic test fixtures. Do not commit real client email.

### Unit tests

Cover at least:

- CLI parsing.
- Output path validation.
- Local versus UNC path detection.
- Source/output containment checks.
- Outlook bitness detection abstraction.
- Deterministic source ordering.
- Reparse-point avoidance.
- Folder normalization.
- Folder collision handling.
- Root-level EML mapping.
- Sent folder alias detection.
- Draft folder alias detection.
- `X-Unsent` detection.
- Header unfolding.
- Header size limits.
- RFC-style date parsing.
- Timezone conversion.
- Invalid date fallback.
- SHA-256 calculation.
- Manifest hash generation.
- SQLite schema migration.
- State transition rules.
- Resume source-change detection.
- CSV escaping.
- JSON report schema.
- Exit-code mapping.
- Privacy redaction.

### Synthetic EML fixtures

Include fixtures for:

- UTF-8 plain text.
- UTF-8 HTML.
- UTF-8 with BOM.
- Windows-1255 Hebrew.
- ISO-8859-8 Hebrew.
- Quoted-printable body.
- Base64 body.
- RFC 2047 encoded subject.
- Hebrew attachment filename.
- Plain plus HTML multipart alternative.
- Inline CID image.
- One attachment.
- Multiple attachments.
- EML attached inside EML.
- Empty body.
- Missing subject.
- Missing Date.
- Invalid Date.
- Multiple Received headers.
- `X-Unsent: 1`.
- Malformed header line endings.
- Header NUL.
- Missing header/body separator.
- Broken MIME boundary.
- Zero-byte EML.
- Very large synthetic attachment generated during test execution rather than committed.

### Outlook-dependent integration tests

When classic Outlook is available:

1. Create a temporary Unicode PST.
2. Import one message.
3. Reopen and verify it.
4. Import a folder tree.
5. Verify Unicode folder names.
6. Verify Hebrew subject/body/attachment name.
7. Verify sent flags.
8. Verify draft flags.
9. Verify fallback EML attachment.
10. Verify tracking named properties.
11. Verify resume after simulated crash window.
12. Verify x86 worker with 32-bit Outlook.
13. Verify x64 worker with 64-bit Outlook.

Integration tests must use temporary directories and clean up only their own profiles and files.

### End-to-end tests

Create an automated E2E test that:

- Generates a synthetic Windows Live Mail-like folder tree.
- Runs the actual launcher and matching worker.
- Produces a PST.
- Runs mandatory validation.
- Checks report and exit code.

If Outlook is unavailable, mark the E2E test skipped with an explicit reason rather than passing it falsely.

---

## 29. Required build and packaging workflow

Create PowerShell scripts that build both architectures.

Expected commands:

```powershell
.\scripts\build.ps1
.\scripts\test.ps1
.\scripts\package.ps1
```

The build script should:

1. Verify prerequisites.
2. Configure x86 and x64 build trees.
3. Build the launcher and x86 worker in the x86 tree.
4. Build the x64 worker in the x64 tree.
5. Run unit tests for both architectures where practical.
6. Collect release artifacts.

The package output should resemble:

```text
dist\WLM2PST-1.0.0\
├── wlm2pst.exe
├── wlm2pst-worker-x86.exe
├── wlm2pst-worker-x64.exe
├── README.txt
├── LICENSES\
└── THIRD-PARTY-NOTICES.txt
```

Include only runtime DLLs that are legally redistributable and actually required. Do not redistribute Outlook or MAPI binaries.

Create version resources for all executables.

---

## 30. Documentation requirements

### README.md

Include:

- What WLM2PST does.
- What it does not do.
- Classic Outlook requirement.
- Bitness behavior.
- Basic command examples.
- Resume example.
- Output files.
- Exit codes.
- Privacy behavior.
- Known limitations.
- How to open the resulting PST in classic Outlook.

Use the correct user workflow:

```text
File -> Open & Export -> Open Outlook Data File
```

Do not imply that Outlook's Import/Export wizard is required merely to open the PST.

### BUILDING.md

Document:

- Visual Studio version.
- Windows SDK requirement.
- CMake requirement.
- vcpkg or dependency setup.
- MAPI headers and stub library setup.
- x86 build.
- x64 build.
- Test commands.
- Packaging commands.

### ARCHITECTURE.md

Document:

- Launcher and workers.
- Why bitness must match Outlook.
- Temporary profile lifecycle.
- PST provider lifecycle.
- MIME-to-MAPI path.
- Resume crash windows.
- Named property strategy.
- Validation strategy.
- Privacy boundaries.

### Troubleshooting

Cover:

- New Outlook only.
- Classic Outlook not installed.
- Bitness mismatch.
- MAPI initialization errors.
- Output already exists.
- PST locked.
- Insufficient disk space.
- Source changed.
- Resume mismatch.
- Temporary profile cleanup.
- Validation failure.

---

## 31. Acceptance criteria

The implementation is complete only when all applicable criteria pass.

1. The source tree builds successfully for Win32 and x64.
2. The package contains one launcher and two workers.
3. The launcher selects the worker matching classic Outlook bitness.
4. A Unicode PST can be created without using the user's Outlook profile.
5. The PST opens in classic Outlook without a repair prompt in tested environments.
6. All readable EML files become Outlook messages.
7. Relative folder structure is preserved under one root folder.
8. Hebrew and Unicode folder names are preserved.
9. Subject, sender, recipients, plain body, HTML body, and attachments are preserved when present.
10. Inline CID images remain associated with HTML messages when the converter supports them.
11. Sent messages retain recipients and sent-state behavior.
12. Drafts remain unsent.
13. All imported archive messages appear as read.
14. Date properties use converter values first and documented fallbacks only when necessary.
15. Duplicate source files are preserved as duplicate PST items.
16. Malformed EML files are retried conservatively.
17. An unconvertible but readable EML is preserved unchanged as an attachment in `_Conversion Errors`.
18. An unreadable source file is reported without aborting unrelated files.
19. Source files are never modified.
20. No network activity is required by the application.
21. Ctrl+C leaves a resumable job.
22. Resume does not duplicate a message saved before a crash.
23. Resume rejects a changed source manifest.
24. `--overwrite` cannot delete an arbitrary non-tool PST.
25. Final validation reopens the PST through a fresh temporary profile.
26. Final validation compares folder and message counts.
27. Final validation checks tracking properties and fallback attachments.
28. Logs do not contain message subjects, bodies, addresses, or attachment content.
29. JSON and CSV reports are produced correctly.
30. The documented exit code matches the actual process result.
31. Temporary profiles are removed after success and handled safely after failure.
32. The tool does not require administrator privileges.
33. Unit tests pass.
34. Outlook-dependent integration tests pass where the required environment is available.
35. No TODO placeholder remains in a core conversion, resume, validation, or packaging path.

---

## 32. Implementation sequence

Implement in this order and keep the repository buildable after each phase.

### Phase 1: repository and build foundation

- Inspect existing files.
- Establish CMake presets for Win32 and x64.
- Add warnings-as-errors and manifests.
- Create launcher and worker executables.
- Add version information.
- Add unit test framework.

### Phase 2: launcher and environment detection

- Implement Outlook installation and bitness detection.
- Implement worker selection.
- Implement exit-code forwarding.
- Unit test registry and PE detection logic.

### Phase 3: MAPI proof path

- Load Outlook MAPI through the supported stub approach.
- Initialize COM and MAPI.
- Create and delete a temporary profile.
- Add a Unicode PST service.
- Open the store.
- Create one folder.
- Convert one synthetic EML with `MIMEToMAPI`.
- Close and reopen the PST.

Do not proceed to large-scale import until this path is reliable.

### Phase 4: scanner and folder mapper

- Recursive EML inventory.
- Reparse-point protection.
- Deterministic manifest.
- Folder mapping and collision rules.
- Preflight summary and disk checks.

### Phase 5: full message import

- Streaming EML input.
- SHA-256.
- Header inspector.
- MIME conversion.
- Sent/draft/read flags.
- Date fallback.
- Tracking properties.
- Per-message save and progress.

### Phase 6: malformed-message fallback

- Conservative normalization retry.
- `_Conversion Errors` folder.
- Original EML attachment preservation.
- Per-message error reporting.

### Phase 7: crash-safe state and resume

- SQLite schema.
- Source manifest hash.
- State transitions.
- Ctrl+C.
- Crash-window recovery by named properties.
- Source-change rejection.

### Phase 8: validation

- Close conversion profile.
- Reopen through a fresh validation profile.
- Verify folders, counts, tracking properties, and fallback attachments.
- Produce validation status and failure details.

### Phase 9: documentation and packaging

- README.
- Building guide.
- Architecture guide.
- Troubleshooting.
- PowerShell scripts.
- Dist package.
- Third-party notices.

### Phase 10: final verification

- Clean build Win32.
- Clean build x64.
- Unit tests.
- Static analysis where available.
- Integration tests where Outlook is available.
- Package smoke test.
- Review logs for privacy leaks.
- Review repository for unfinished placeholders.

---

## 33. Working rules for Claude Code

Follow these instructions while implementing:

1. Start by inspecting the repository and existing build system.
2. Preserve useful existing code and conventions.
3. Do not rewrite unrelated files.
4. Do not merely describe what should be implemented; implement it.
5. Do not stop after generating interfaces or empty stubs.
6. Keep a concise implementation checklist in the repository while working, and remove or finalize it before completion.
7. Build after meaningful milestones.
8. Run unit tests after meaningful milestones.
9. Fix compile errors and test failures before proceeding.
10. Use real HRESULT and Win32 error propagation, not generic `false` returns.
11. Keep MAPI code behind testable abstractions.
12. Never claim an Outlook integration test passed unless it actually ran.
13. Do not weaken acceptance criteria merely because MAPI is difficult.
14. Do not add cloud features, a GUI, or alternate formats.
15. Do not use placeholder implementations in the final result.
16. If an exact MAPI provider property or service name differs across Outlook versions, implement compatibility detection based on official Microsoft behavior and document it.
17. Prefer official Microsoft documentation and the Microsoft MFCMAPI source when resolving MAPI details.
18. Do not copy large undocumented third-party codebases into the project.
19. Preserve all source EML bytes exactly.
20. Finish with a factual summary of files changed, commands run, tests executed, tests skipped, and any remaining environment-specific validation steps.

---

## 34. Final deliverable

The final repository must provide a buildable, testable Windows application whose normal use is:

```powershell
wlm2pst.exe --source "C:\Mail\Windows Live Mail" --output "D:\Migration\Rina-Mail.pst"
```

The resulting PST must be one Unicode PST that can be opened locally in classic Outlook, with the source folder tree and message content preserved as faithfully as the installed Outlook MIME converter and PST provider allow.

The final implementation must remain strictly focused on this one workflow:

```text
Recursive Windows Live Mail EML tree
  -> local WLM2PST CLI
  -> one validated Unicode PST
  -> open locally in classic Outlook
```

Nothing else is required, and no alternate migration destination should be introduced.
