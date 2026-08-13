# WLM2PST implementation checklist (working document)

Removed or finalized before completion (spec section 33.6).

## Phase status

- [x] Phase 1: repository and build foundation (CMake presets, toolchains, flags, manifests, version resources, test framework)
- [x] Phase 2: launcher and environment detection (layered registry + PE detection, worker selection, exit-code forwarding; unit tested)
- [x] Phase 3: MAPI proof path (runtime loading, temp profile, Unicode PST service, IConverterSession; compile-checked for Win32+x64 via MinGW; runtime requires Outlook)
- [x] Phase 4: scanner and folder mapper (iterative traversal, reparse protection, deterministic manifest, collisions, preflight)
- [x] Phase 5: full message import (streaming, SHA-256, header inspector, MIME conversion, flags, date fallbacks, tracking props)
- [x] Phase 6: malformed-message fallback (conservative normalization retry, _Conversion Errors, original EML attachment)
- [x] Phase 7: crash-safe state and resume (SQLite WAL schema, manifest hash, Ctrl+C, crash-window recovery, source-change rejection)
- [x] Phase 8: validation (fresh profile, folder/count/property/fallback verification)
- [x] Phase 9: documentation and packaging (README, BUILDING, ARCHITECTURE, troubleshooting, PS scripts, notices)
- [ ] Phase 10: final verification (in progress)

## Environment constraints (documented, not fabricated)

This repository was implemented in a Linux container. Consequences:

- MSVC builds (`msvc-x86-release`, `msvc-x64-release`) are authored but not
  executed here; MinGW cross builds for Win32 and x64 serve as the
  compile-time check of all Windows code, including the MAPI layer. All three
  product executables (launcher + both workers) link successfully.
- Unit tests of the portable core run natively (`linux-tests` preset): 301+
  tests passing, including pipeline/validation logic against a fake PST session.
- Outlook-dependent integration tests and E2E runs require a Windows machine
  with classic Outlook; they are implemented but marked skipped off-Windows.
