# WLM2PST implementation checklist (working document)

Removed or finalized before completion (spec section 33.6).

## Phase status

- [x] Phase 1: repository and build foundation (CMake presets, toolchains, flags, manifests, version resources, test framework)
- [ ] Phase 2: launcher and environment detection
- [ ] Phase 3: MAPI proof path (compile-checked via MinGW; runtime requires Outlook)
- [ ] Phase 4: scanner and folder mapper
- [ ] Phase 5: full message import
- [ ] Phase 6: malformed-message fallback
- [ ] Phase 7: crash-safe state and resume
- [ ] Phase 8: validation
- [ ] Phase 9: documentation and packaging
- [ ] Phase 10: final verification

## Environment constraints (documented, not fabricated)

This repository was implemented in a Linux container. Consequences:

- MSVC builds (`msvc-x86-release`, `msvc-x64-release`) are authored but not
  executed here; MinGW cross builds for Win32 and x64 serve as the
  compile-time check of all Windows code, including the MAPI layer.
- Unit tests of the portable core run natively (`linux-tests` preset).
- Outlook-dependent integration tests and E2E runs require a Windows machine
  with classic Outlook; they are implemented but marked skipped off-Windows.
