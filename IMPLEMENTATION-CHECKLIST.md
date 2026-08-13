# WLM2PST implementation checklist (finalized)

All ten implementation phases (spec section 32) are complete. This document
is the finalized record required by spec section 33.6.

## Phase status

- [x] Phase 1: repository and build foundation
- [x] Phase 2: launcher and environment detection
- [x] Phase 3: MAPI proof path (compile-verified for Win32 + x64; runtime requires classic Outlook)
- [x] Phase 4: scanner and folder mapper
- [x] Phase 5: full message import
- [x] Phase 6: malformed-message fallback
- [x] Phase 7: crash-safe state and resume
- [x] Phase 8: validation
- [x] Phase 9: documentation and packaging
- [x] Phase 10: final verification (see below)

## Final verification record

Implemented and verified in a Linux container (no Windows/MSVC/Outlook
available). What ran here, factually:

- `linux-tests` preset: clean build, **339/339 unit tests passed** (CLI,
  paths, unicode, SHA-256, logger, header inspector, date parser, scanner,
  folder mapping, aliases, resume DB, reporting, launcher detection/PE,
  EML normalizer, fixture corpus, preflight, import pipeline and PST
  validation against a fake MAPI session).
- `mingw-x86` preset: full cross-compile of the 32-bit launcher
  (`wlm2pst.exe`) and `wlm2pst-worker-x86.exe`, zero warnings
  (warnings-as-errors), links successfully.
- `mingw-x64` preset: full cross-compile of `wlm2pst-worker-x64.exe`,
  zero warnings, links successfully. The complete Extended MAPI layer
  compiles in both bitnesses.
- Placeholder scan: no TODO/FIXME/stub in any conversion, resume,
  validation, or packaging path.
- Privacy review: every log call site audited; only relative paths, sizes,
  hashes, statuses, codes, timings, and counts are emitted.

## Not executed here (requires Windows + classic Outlook)

- MSVC builds via `msvc-x86-release` / `msvc-x64-release` presets
  (authored, not run).
- Outlook-dependent integration tests (`tests/integration`, 13 scenarios,
  compile-checked via MinGW, tagged `[.outlook]`, skip with an explicit
  reason when Outlook is missing).
- End-to-end run via `tests/e2e/run_e2e.ps1` (script logic verified against
  a stub binary; a real conversion needs Outlook).
- Opening the produced PST in classic Outlook.

No test results were fabricated; the items above are exactly the
environment-specific validation steps that remain.
