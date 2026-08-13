# WLM2PST development conventions

These rules keep six independently developed modules consistent. Follow them
exactly; the build treats warnings as errors in all three build modes.

## Build modes

| Preset | Purpose | Runs here |
|---|---|---|
| `msvc-x86-release`, `msvc-x64-release` | Production binaries | Windows only |
| `mingw-x86`, `mingw-x64` | Compile-check of ALL Windows code | any Linux with mingw-w64 |
| `linux-tests` | Unit tests of portable code (Catch2) | yes |

Every module must keep all three modes green.

## Portability rules

- Portable code (everything except `src/worker/mapi/`, `src/launcher/main.cpp`,
  `src/worker/main.cpp`, and `*_win.cpp` files) must compile without
  `<windows.h>` and must not include `common/win_compat.h`.
- Windows-only translation units use the **`_win.cpp` suffix**; they are
  excluded automatically from non-Windows builds. Their headers stay portable
  (declare interfaces, hide Windows types in the .cpp).
- `src/worker/mapi/**` is Windows-only wholesale (no suffix needed).
- Never assume `wchar_t` is 2 bytes. On Windows it is UTF-16; on Linux test
  builds it is UTF-32. Use `common/unicode` helpers for conversions.
- Timestamps use `wlm2pst::FileTimeUtc` (FILETIME ticks) from `common/model.h`.

## Naming and style

- Namespace `wlm2pst` (flat; no per-module sub-namespaces unless disambiguation
  is needed).
- Files `snake_case.{h,cpp}`, types `PascalCase`, functions and variables
  `lower_snake_case`, constants `kPascalCase`, members `trailing_underscore_`.
- Include paths are rooted at `src/`: `#include "worker/dates/date_parser.h"`.
- Errors: return `wlm2pst::Result<T>` / `wlm2pst::Status`
  (`common/errors/result.h`). No exceptions across module boundaries; local
  exceptions (e.g. from std::filesystem) must be caught inside the module.
- No `new`/`delete`, no raw owning pointers. RAII everywhere.
- 64-bit arithmetic for all sizes and counts. Validate narrowing conversions.

## Privacy (non-negotiable, spec section 21)

Log and report writers may output ONLY: relative paths, target folder paths,
sizes, SHA-256, statuses, attempt counts, HRESULT/Win32 codes, timings, counts,
bitness. NEVER message subject, body, addresses, raw MIME headers, or
attachment content — in any log level, including verbose.

## Tests

- Framework: Catch2 v3. Test files live in `tests/unit/` and are named
  `test_<module>_<topic>.cpp`. They are globbed automatically.
- Tag tests with the module name: `TEST_CASE("...", "[dates]")`.
- Fixture EMLs live in `tests/fixtures/`; access via the compile definition
  `WLM2PST_FIXTURES_DIR`. Fixtures are synthetic only - never real mail.
- Tests must pass with `ctest --preset linux-tests` (or equivalent).

## Build system

- Source files are discovered by glob; adding a `.cpp` under your module
  directory is enough. Do not edit `src/CMakeLists.txt` or `tests/CMakeLists.txt`.
- Centralized definitions live in: `common/exit_codes.h` (exit codes),
  `common/model.h` (statuses, manifest types), and for MAPI:
  `worker/mapi/mapi_constants.h` (property tags, GUIDs, provider names).
