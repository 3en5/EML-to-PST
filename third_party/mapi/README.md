# Vendored Extended MAPI headers

`include/` holds the Extended MAPI header set (`mapidefs.h`, `mapix.h`,
`mapitags.h`, `mapiutil.h`, `mapiguid.h`, `mapiaux.h`, and friends).

## Why these are vendored

Older Windows SDKs shipped these headers under `Include/<ver>/um`, and
BUILDING.md originally assumed that is still true. It is not: the Windows SDK
**10.0.26100.0** (the one installed with current VS 2022) does not contain
`mapidefs.h` at all, so the MAPI layer could not compile.

They are vendored rather than fetched for the same reason SQLite is (see
`third_party/sqlite/README.md`): the build stays fully offline and
deterministic.

## Source

- Upstream: <https://github.com/microsoft/MAPIStubLibrary> (Microsoft)
- Commit: `a9505d73351554078431fc950a0bc34ada6fe39b` (2026-08-07)
- Path taken: `include/` (headers only — the stub library sources are not used;
  WLM2PST links MAPI through Outlook's own runtime, not the stub library)
- License: MIT, see `LICENSE` in this directory.

## Upgrading

Replace `include/` from a newer upstream commit and update the commit hash
above. No code changes should be required.

## How the build finds them

`cmake/wlm2pst_mapi_headers.cmake` first probes for `mapidefs.h` in the
platform SDK. If the SDK provides it, that is used and this directory is
ignored. Otherwise these vendored headers are used. Override either with
`-DWLM2PST_MAPI_HEADERS_DIR=<path>`.
