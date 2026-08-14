# Changelog

All notable changes to WLM2PST are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added (fifth round - second conversion engine)

- **MimeOle import engine** (`src/worker/mapi/mimeole_importer.*`): a full
  fallback conversion path built on Windows' own MIME parser
  (`inetcomm.dll`) - the engine Windows Live Mail itself wrote these EML
  files with. MimeOle parses (subject, sender, To/Cc/Bcc, text and HTML
  bodies with full charset/RFC 2047/RFC 2231 handling, attachments including
  inline CID parts, Message-ID/In-Reply-To/References, transport headers);
  WLM2PST maps the parsed parts onto MAPI properties. Parsing remains a
  Microsoft engine end to end - this is not a hand-built MIME parser.
- The preflight calibration now ends with an `engine=mimeole` variant: when
  Outlook's `IConverterSession` fails content verification (the field-proven
  situation on current Click-to-Run), the MimeOle engine is verified with
  the same subject/body/attachment judges and selected only if it passes.
  `IConverterSession` remains primary wherever it actually works.
- Rationale and field evidence: `docs/verification/` (rounds 1-4);
  `IConverterSession` is not usable out-of-process on current C2R Office,
  and Microsoft's own MrMAPI fails the same way there.


### Changed (fourth verification round - diagnosis instrumentation)

- Field round 4 identified `MIMEToMAPI` returning `MAPI_W_PARTIAL_COMPLETION`
  (0x00040680) identically across every address-book/flag variant - the
  converter runs and partially completes rather than doing nothing. To turn
  "partial" into a concrete list:
  - The calibration ladder now logs a full post-mortem of every rejected
    variant (GetPropList tags, subject in both widths, body head, message
    class, recipient and attachment table counts) both before and after
    `SaveChanges`.
  - A minimal-ASCII canary message (no MIME structure, no encoded words) is
    converted first and described, separating structural failures from
    content-shaped ones.
  - Deferred-write hypothesis is tested automatically: each variant is
    re-judged after `SaveChanges`; if properties only materialize at save,
    calibration accepts the variant and says so.
- The per-message conversion-integrity gate moved to after `SaveChanges`
  (same hypothesis); on violation the just-saved message is deleted so
  nothing corrupt persists.

### Fixed (third verification round)

- **Converter driven per Microsoft's documented import sequence.** Field
  round 3 proved acquisition was never the variable (the C2R hive resolves
  to the same OUTLMIME.DLL): the object was alive but degraded to
  treat-stream-as-text. The missing piece: `SetAdrBook` — called by
  Microsoft's "Importing MIME email" sample and MFCMAPI before `MIMEToMAPI`,
  never called by WLM2PST. The session's address book is now attached
  (`IMAPISession::OpenAddressBook` + `SetAdrBook`) before converting.
- **Preflight self-test upgraded to calibration.** It now tries driving
  variants in a fixed order (address book on/off × three CCSF flag levels)
  against the bundled fixture with full content checks; the first variant
  that provably converts is locked in for the whole run and logged. If none
  converts, the run still refuses loudly — now with per-variant diagnostics
  in the error text.
- `MIMEToMAPI` results other than plain `S_OK` are now logged (`S_FALSE`
  cannot masquerade as silent success; the per-message integrity gate
  remains the behavioral arbiter).

### Fixed (second verification round)

- **Critical: silently corrupt output on Click-to-Run.** The previous C2R
  fallback guessed `OUTLMIME.DLL` by name; that dll hands out an
  `IConverterSession` which reports success without converting — a full run
  of 2,178 messages produced a PST whose messages had no subject, the raw
  EML as body, and no attachments, while validation PASSED (see
  `docs/verification/2026-08-13-c2r-converter-still-broken.md`). Three-layer
  fix:
  1. The converter is now obtained the way MFCMAPI does it: from the dll
     registered in the Click-to-Run virtual registry hive
     (`...\ClickToRun\REGISTRY\MACHINE\Software\Classes\CLSID\{...}\InprocServer32`),
     falling back to the loaded Outlook MAPI module — never a guessed dll.
  2. **Preflight converter self-test**: a bundled synthetic message (RFC 2047
     Hebrew subject, UTF-8 Hebrew body, one attachment) is converted into a
     throwaway PST before any real work; unless subject, decoded body, and
     exactly one attachment all come back, the run aborts with a clear error
     instead of writing an unusable archive.
  3. **Per-message conversion-integrity gate**: a source that declares a
     `Subject:` must yield a subject; a `multipart/mixed` source must yield
     at least one attachment. Violations route the message into the
     normalized-retry / preserve-as-attachment path, and a run of them trips
     the circuit breaker.

First verification round on real Windows (MSVC + classic Outlook,
Microsoft 365 Click-to-Run).

### Fixed

- **Click-to-Run Outlook: MIME converter unreachable.** C2R does not publish
  Outlook's COM classes in the ordinary registry, so
  `CoCreateInstance(CLSID_IConverterSession)` failed with
  `REGDB_E_CLASSNOTREG` and no message could be converted. The converter is
  now also obtainable directly from the implementing DLL (`OUTLMIME.DLL`,
  located next to `OUTLOOK.EXE`) via `DllGetClassObject`, the same fallback
  MFCMAPI uses. The acquisition path is logged at verbose level.
- **Extended MAPI headers are not in current Windows SDKs.** The build now
  probes the SDK and falls back to a vendored copy of Microsoft's
  MAPIStubLibrary header set (`third_party/mapi/`, MIT);
  `-DWLM2PST_MAPI_HEADERS_DIR` overrides both. BUILDING.md corrected.
- **Long paths without machine policy.** The scanner root and every per-file
  open now use extended-length (`\\?\`) paths, so source trees with paths
  beyond 260 characters convert even with `LongPathsEnabled=0`.
- MSVC `/W4 /WX` fixes: Catch2 C4324 suppressed for the test target only;
  `if constexpr` in the bitness integration tests; stale
  `kConversionErrorsFolder` identifier in integration tests.
- Integration tests read message content correctly (body via message
  `GetProps`/stream, never via contents-table columns, which providers do
  not serve for large properties) and are now self-describing: failures
  print exactly what each message contains, including per-property MAPI
  error codes and attachment tables.

### Added

- PST content dump diagnostic for operators (`wlm2pst_integration_tests.exe
  "[.dump]"` with `WLM2PST_DUMP_PST=<path>`): prints every folder and
  message (class/subject/flags/attachments/body head) of a PST, for
  verifying conversions on Click-to-Run machines where Outlook COM
  automation is unavailable.

## [1.0.0] - Unreleased

Initial release: converts a recursive Windows Live Mail `.eml` folder tree
into one Unicode PST openable in classic Outlook.

### Added

- Launcher/worker split (`wlm2pst.exe` + `wlm2pst-worker-x86.exe` +
  `wlm2pst-worker-x64.exe`) with layered classic-Outlook detection and
  bitness matching.
- Temporary, tool-owned MAPI profile lifecycle (`WLM2PST-{GUID}`) — no use
  of the user's real Outlook profile, and startup cleanup of stale
  `WLM2PST-*` profiles only.
- EML-to-MAPI import via classic Outlook's `IConverterSession::MIMEToMAPI`,
  writing to the Unicode PST provider (`MSUPST MS`) exclusively.
- Deterministic recursive source scanning and folder mapping, preserving
  the original folder tree under one configurable root folder, with
  collision-safe folder name normalization.
- Sent/draft detection by folder alias and `X-Unsent` header, including
  Hebrew alias names; read state is always set on import.
- Tolerant RFC 5322 date parsing with documented fallback priority and
  sanity bounds.
- Two-attempt malformed-EML handling (original bytes, then a conservative
  header-only normalized retry) with a final `_Conversion Errors` fallback
  that preserves the original EML unchanged as an attachment.
- Six `Wlm2Pst.*` named tracking properties under a fixed property-set GUID,
  used for crash-window recovery and validation.
- Crash-safe SQLite resume state database beside the PST, with `--resume`
  support and folder-local crash-window recovery.
- Independent post-conversion PST validation through a fresh
  `WLM2PST-VALIDATE-{GUID}` profile.
- Centralized, privacy-conscious logging, JSON report, and errors CSV.
- Centralized process exit codes (see README.md).
- PowerShell build, test, and packaging scripts; third-party notices for
  vendored SQLite and test-only Catch2.
