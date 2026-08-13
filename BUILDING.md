# Building WLM2PST

## Prerequisites

- **Visual Studio 2022 (v17)** with the "Desktop development with C++"
  workload. The CMake presets use the `Visual Studio 17 2022` generator.
- **Windows 10 or Windows 11 SDK**, installed as part of the VS 2022
  workload above. Note: **current Windows SDKs do NOT ship the Extended MAPI
  headers** (verified missing in SDK 10.0.26100.0) — the build handles this
  automatically with a vendored copy; see "Extended MAPI headers" below. No
  separate MAPI SDK download is required either way.
- **CMake >= 3.21** (bundled with recent VS 2022 installs, or install
  separately and make sure it is on `PATH`).
- Production builds only target Windows; there is no Linux/macOS production
  build.

Everything else the build needs — SQLite, and Catch2 when tests are enabled
— is either vendored or fetched automatically as described below.

## Extended MAPI headers

Current Windows SDKs (e.g. 10.0.26100.0, installed with today's VS 2022) do
**not** contain the Extended MAPI headers (`mapidefs.h`, `mapix.h`,
`mapitags.h`, `mapiutil.h`, `mapiguid.h`); older SDKs did. The build resolves
this in [`cmake/wlm2pst_mapi_headers.cmake`](cmake/wlm2pst_mapi_headers.cmake):

1. If the active SDK/toolchain provides `mapidefs.h` (MinGW-w64 does, and
   older Windows SDKs did), it is used as-is.
2. Otherwise the build falls back to the vendored copy of Microsoft's own
   header set from the [MAPIStubLibrary](https://github.com/microsoft/MAPIStubLibrary)
   project (MIT), under `third_party/mapi/include/`.
3. `-DWLM2PST_MAPI_HEADERS_DIR=<path>` overrides both.

No action is needed for a normal build — the fallback is automatic.

## MAPI header version-specific gaps

Even where the headers exist, the set is not perfectly uniform across
versions — most notably, `IMsgServiceAdmin2` (needed for
`CreateMsgServiceEx`) is not guaranteed to be declared everywhere, and
`mapiaux.h` is not present in every header set. WLM2PST does not require a
newer or non-standard header set to work around this: everything the tool
needs beyond the guaranteed baseline is declared once, guarded, in
[`src/worker/mapi/mapi_compat.h`](src/worker/mapi/mapi_compat.h). When
`<mapiaux.h>` is available (MinGW-w64 ships it) it is used directly; when it
is not, the interface is declared inline per Microsoft's documented
`IMsgServiceAdmin2` layout (OLEGUID `0x20387`). The tree compiles and links
identically either way — you do not need to hunt for a different SDK or
patch headers yourself.

The `IConverterSession` interface (MIME <-> MAPI conversion) is not
published in any SDK header at all; its vtable layout lives in
[`src/worker/mapi/mapi_constants.h`](src/worker/mapi/mapi_constants.h),
sourced from Microsoft's MFCMAPI project and the documented "Importing MIME
email" sample.

## Vendored SQLite

The resume state database engine is the SQLite amalgamation (`sqlite3.c` /
`sqlite3.h`, version 3.38.2, public domain), vendored under
`third_party/sqlite/` and built as the static library `wlm2pst_sqlite` with
`SQLITE_OMIT_LOAD_EXTENSION` and `SQLITE_DQS=0`. It is vendored (not fetched)
so the build is fully offline and deterministic across MSVC, MinGW
cross-compilation, and the native Linux test build. See
`third_party/sqlite/README.md` for upgrade instructions.

## Tests: Catch2

Unit tests use Catch2 v3. `vcpkg.json` declares an optional `tests` feature
(on by default) that pulls in `catch2` when you configure through a vcpkg
toolchain file. vcpkg is optional: if a system/vcpkg `Catch2` package is
already discoverable via `find_package(Catch2)`, CMake uses it; if you are
not using vcpkg at all, point CMake at your own Catch2 install or rely on
`WLM2PST_BUILD_TESTS=OFF` to skip tests entirely. Either way, `tests/`
globs `tests/unit/test_*.cpp` automatically — adding a new test file needs
no CMake edits.

## Configuring and building

WLM2PST builds the launcher and the x86 worker together in the Win32 tree,
and the x64 worker separately in the x64 tree (bitness must match the build
architecture — see ARCHITECTURE.md).

x86 (produces `wlm2pst.exe` and `wlm2pst-worker-x86.exe`):

```powershell
cmake --preset msvc-x86-release
cmake --build --preset msvc-x86-release
```

x64 (produces `wlm2pst-worker-x64.exe`):

```powershell
cmake --preset msvc-x64-release
cmake --build --preset msvc-x64-release
```

Both presets build with `/W4 /WX /permissive- /utf-8 /EHsc /sdl /guard:cf`
and control-flow-guard/DEP linker hardening (see
`cmake/wlm2pst_flags.cmake`); warnings are errors for project code.

### Other presets (not production binaries)

Two additional preset families exist for development and CI, and are
documented here so their purpose is clear — neither produces the binaries
you ship:

- `mingw-x86` / `mingw-x64` — cross-compile-check every Windows-specific
  source file (launcher, worker, and the MAPI layer) using MinGW-w64 on
  Linux. This catches compile errors in Windows-only code without needing a
  Windows machine; it is not a substitute for an MSVC build and its output
  is not packaged.
- `linux-tests` — a native (non-Windows) build of the portable core only
  (everything except `src/worker/mapi/`, `*_win.cpp` files, and the two
  `main.cpp` entry points), used to run the Catch2 unit tests on Linux/macOS
  during development.

```powershell
cmake --preset mingw-x86 && cmake --build --preset mingw-x86
cmake --preset mingw-x64 && cmake --build --preset mingw-x64
cmake --preset linux-tests && cmake --build --preset linux-tests
```

## Running tests

```powershell
ctest --preset msvc-x86-release
ctest --preset msvc-x64-release
```

On a non-Windows development machine, run the portable-core unit tests
instead:

```powershell
ctest --preset linux-tests
```

`WLM2PST_BUILD_TESTS` (default `ON`) controls whether the `tests/`
subdirectory is configured at all; pass `-DWLM2PST_BUILD_TESTS=OFF` to a
`cmake --preset ...` invocation to skip it.

## Packaging

Once both architectures are built, produce the distributable package with:

```powershell
.\scripts\package.ps1
```

See [`scripts/package.ps1`](scripts/package.ps1) and
`packaging/README.txt` for what gets collected. `scripts/build.ps1` and
`scripts/test.ps1` wrap the configure/build/test steps above for both
architectures in one command; see the top of each script for usage.
