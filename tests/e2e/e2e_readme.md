# WLM2PST end-to-end test

`run_e2e.ps1` is the automated end-to-end test required by spec section 28:
it generates a synthetic Windows-Live-Mail-like EML folder tree, runs the
**actual** launcher and matching worker (`wlm2pst.exe`) against it, produces
a real PST, and validates the exit code, JSON report, and errors CSV.

Everything the script generates is synthetic - copied from
`tests/fixtures/eml/` (see `tests/fixtures/README.md`); no real mail is ever
involved.

## Requirements

* Windows, with `wlm2pst.exe` and its worker executable(s) already built
  (`msvc-x86-release` and/or `msvc-x64-release` presets) or packaged into a
  `dist/` folder (spec section 29).
* PowerShell 5.1+ (Windows PowerShell) or PowerShell 7+ (`pwsh`).
* Classic Outlook + Extended MAPI installed, for a full PASS. Without it,
  `wlm2pst.exe` itself fails preflight and exits 14 - the script reports
  that as **SKIPPED**, with the reason printed, and still exits 0. A skip is
  not a pass: read the console output, don't just check the exit code, when
  triaging CI history.

## What it does

1. Builds a synthetic source tree under `$env:TEMP`:

   ```text
   <scratch>\source\
   ├── root-level-note.eml                          (-> "Root Messages")
   └── account@example.invalid\
       ├── Inbox\               (12 differently-encoded/malformed-adjacent EMLs)
       ├── Sent Items\          (2 EMLs - sent-folder alias detection)
       ├── Drafts\              (2 EMLs, one X-Unsent - draft detection)
       ├── Customers\Client A\  (4 EMLs - attachments, nested EML, Hebrew filename)
       ├── תיקייה\               (3 EMLs - Unicode/Hebrew folder name)
       └── Malformed\           (5 deliberately broken EMLs - fallback path)
   ```

2. Runs:

   ```powershell
   wlm2pst.exe --source <scratch>\source --output <scratch>\output\E2E-Test.pst --root-name "WLM2PST E2E Test"
   ```

3. Checks the process exit code is one of `0`, `2`, or `3` (spec section 22:
   the three non-fatal "completed" codes). Any other code fails the script,
   **except** `14` (Outlook/Extended MAPI unavailable), which is reported as
   SKIPPED.

4. Validates, next to the output PST (`<output>.wlm2pst-*`, spec section 18):
   * `<output>.wlm2pst-report.json` exists, is valid JSON, contains every
     field spec section 21 requires, and its `sourceFiles` count matches the
     actual number of `.eml` files the script generated.
   * `<output>.wlm2pst-errors.csv` exists **iff** the exit code was `2` or
     `3` (spec section 21: "created only when errors or fallback messages
     exist") - and when present, starts with a UTF-8 BOM and has exactly the
     documented header row.
   * `validationStatus` is `VALIDATED` or `VALIDATED_WITH_FALLBACKS` (a
     non-fatal exit code must not carry a failed validation status).

5. Cleans up its scratch directory (source tree, output PST, all sidecar
   files) unconditionally, unless `-KeepTemp` is passed.

## Running

```powershell
cd tests\e2e
.\run_e2e.ps1
```

If `wlm2pst.exe` isn't in one of the conventional build/dist locations the
script searches, point it at the right folder:

```powershell
.\run_e2e.ps1 -BinDir C:\path\to\dist
.\run_e2e.ps1 -BinDir ..\..\build\msvc-x86\src\Release -KeepTemp
```

`-KeepTemp` skips cleanup so you can inspect the generated tree, PST, and
report/CSV after a failed run.

## Exit codes (of the script itself)

| Script exit code | Meaning |
|---|---|
| `0` | The E2E scenario passed, **or** it was SKIPPED because Outlook/Extended MAPI is unavailable (check the console output for `SKIPPED` to tell which). |
| `1` | A real failure: `wlm2pst.exe` could not be found, exited with an unexpected code, or one of the report/CSV validations failed. The console output names exactly what failed. |

This mirrors the spec's requirement: "If Outlook is unavailable, mark the
E2E test skipped with an explicit reason rather than passing it falsely" -
the script never reports success without having actually validated a real
run, and a skip is always visibly labeled as a skip, not folded into a
silent pass.
