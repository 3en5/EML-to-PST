# Changelog

All notable changes to WLM2PST are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

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
