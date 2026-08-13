# Outlook-dependent integration tests

`mapi_integration_tests.cpp` implements the 13 Outlook-dependent scenarios
from spec section 28. They drive the **real** Extended MAPI implementation
(`make_mapi_environment()`, `src/worker/mapi/mapi_environment.cpp`) against
an actual classic Outlook installation, so they:

* **Only run on Windows**, with classic Outlook (not "New Outlook") and
  Extended MAPI installed. The whole `.cpp` file is wrapped in `#ifdef
  _WIN32` and is a complete no-op elsewhere.
* **Never run automatically** in this repository's normal test flow. Every
  `TEST_CASE` carries Catch2's dot-tag, `[.outlook]`, which Catch2 excludes
  from a default run - matching the `linux-tests`/unit-test flow, which
  never touches these files at all (see "Why this isn't wired in" below).
* **Skip, never falsely pass**, when Outlook/Extended MAPI is unavailable:
  each test calls `IMapiEnvironment::preflight_check()` first and issues
  Catch2's `SKIP(...)` with the underlying error if it fails.
* **Clean up only their own temporary files**: each test creates its own
  scratch directory under `%TEMP%` (removed unconditionally at the end of
  the test) and its own `WLM2PST-*`-prefixed temporary MAPI profiles (never
  touches a real user profile or PST).

## What each scenario covers

| # | Scenario | `TEST_CASE` |
|---|---|---|
| 1 | Create a temporary Unicode PST | `1: create a temporary Unicode PST` |
| 2 | Import one message | `2-3: import one message, then reopen the PST and verify it` |
| 3 | Reopen and verify it | (same test as #2) |
| 4 | Import a folder tree | `4-5: import a folder tree and verify Unicode folder names` |
| 5 | Verify Unicode folder names | (same test as #4; asserts a Hebrew folder name round-trips exactly) |
| 6 | Verify Hebrew subject/body/attachment name | `6: Hebrew subject, body, and attachment filename survive conversion` |
| 7 | Verify sent flags | `7: sent-folder messages are marked sent (...)` |
| 8 | Verify draft flags | `8: X-Unsent messages are marked draft (...)` |
| 9 | Verify fallback EML attachment | `9: fallback preserves the original EML as an unchanged attachment` |
| 10 | Verify tracking named properties | `10: WLM2PST tracking named properties round-trip through the PST` |
| 11 | Verify resume after simulated crash window | `11: find_tracked_messages recovers an import across a simulated crash window` |
| 12 | Verify x86 worker with 32-bit Outlook | `12: x86 worker matches a 32-bit classic Outlook installation` |
| 13 | Verify x64 worker with 64-bit Outlook | `13: x64 worker matches a 64-bit classic Outlook installation` |

Items 12 and 13 share one test binary each build produces: a given compiled
test executable is only ever 32-bit or 64-bit, so exactly one of the two
`TEST_CASE`s can meaningfully run in a given build; the other SKIPs by
design (`sizeof(void*)` mismatch) rather than reporting a false pass. Run
both an x86 and an x64 build of `wlm2pst_integration_tests.exe` in CI,
against 32-bit and 64-bit classic Outlook respectively, to cover both.

`IPstSession` (`worker/import_engine.h`) is the portable orchestration
interface and deliberately does not expose message content such as Subject
or Body. Scenarios 6, 7, 8, and 9 need to inspect exactly that, so they open
a **second, independent** MAPI session directly against the PST file that
was just created/closed - a real "reopen from scratch" step, built from the
same pieces the production code uses (`worker/mapi/mapi_runtime.h`,
`temporary_profile.h`, `pst_store.h`) - and read the raw MAPI properties
(`PR_SUBJECT_W`, `PR_BODY_W`, `PR_MESSAGE_FLAGS`, attachment
`PR_ATTACH_LONG_FILENAME_W`, ...) through it.

## Why this isn't wired into the normal build

`tests/CMakeLists.txt` globs and builds everything in `tests/unit/*.cpp`
only (see `dev-conventions.md`); `tests/integration/` is deliberately
outside that glob because these tests need classic Outlook and cannot run
in CI containers, Linux, or on a machine without Outlook installed - forcing
them into the normal `wlm2pst_tests` target would either fail every build
that lacks Outlook or silently no-op in a way that's easy to mistake for
"tested."

`tests/integration/CMakeLists.txt` defines a separate target,
`wlm2pst_integration_tests`, guarded end-to-end by `if(WIN32)` (a no-op
`return()` on any other platform). There are two ways to actually build it:

### Option A (recommended): wire it into the top-level build

Add one line to the end of the top-level `CMakeLists.txt`, after
`add_subdirectory(src)`:

```cmake
if(WIN32)
    add_subdirectory(tests/integration)
endif()
```

With that in place, `wlm2pst_mapi` / `wlm2pst_worker_core` / `wlm2pst_common`
already exist as targets by the time `tests/integration/CMakeLists.txt`
runs, so it links against them directly - no source is compiled twice, and
the integration test binary always tracks the real build exactly. This
repository intentionally leaves that one line out of the top-level
`CMakeLists.txt` (maintainers may prefer it opt-in, since it changes what a
plain Windows configure produces) - add it locally, or ask the maintainer to
land it, whichever fits your workflow. **This file was not edited as part of
this change** because `CMakeLists.txt` is outside this change's scope.

### Option B: fully standalone configure

```powershell
cmake -S tests\integration -B build\integration -G "Visual Studio 17 2022" -A x64
cmake --build build\integration --config Release
```

No changes to the top-level build are required. `tests/integration/CMakeLists.txt`
detects that `wlm2pst_mapi` doesn't exist yet and compiles the small set of
source files it needs (`src/common/**`, `src/worker/**` except `worker/mapi/`
and `worker/main.cpp`, and `src/worker/mapi/**`) itself, with its own
correct include paths - deliberately **not** via
`add_subdirectory(../../)`, because the top-level `CMakeLists.txt` and
`cmake/wlm2pst_flags.cmake` key several include directories off
`CMAKE_SOURCE_DIR`, which points at whichever project is outermost for the
current configure. Re-entering the real root `CMakeLists.txt` from here
would make `CMAKE_SOURCE_DIR` resolve to `tests/integration`, not the
repository root, breaking `wlm2pst_common`'s include path (and, in a
production build, several `configure_file()`/`packaging` paths too) - hence
option B's independent compilation of the same real source files instead of
a nested `add_subdirectory` of the whole repository.

For either option, pick x86 or x64 generator/architecture to match the
Outlook installation under test (spec items 12/13).

## Running

```powershell
# Everything (all [.outlook]-tagged cases are hidden by default in Catch2;
# request them explicitly with the leading dot):
wlm2pst_integration_tests.exe "[.outlook]"

# One scenario:
wlm2pst_integration_tests.exe "6: Hebrew subject, body, and attachment filename survive conversion"

# Via ctest (after `include(Catch)` + `catch_discover_tests`, both already
# present in tests/integration/CMakeLists.txt):
ctest --test-dir build\integration -C Release
```

If no classic Outlook is registered, every test SKIPs (not fails) with the
`preflight_check()` error attached, and the process still exits 0.
