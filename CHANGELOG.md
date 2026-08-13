# Changelog

All notable changes to WLM2PST are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

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
