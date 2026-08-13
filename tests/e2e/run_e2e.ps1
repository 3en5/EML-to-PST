#Requires -Version 5.1
<#
.SYNOPSIS
    WLM2PST end-to-end test (spec section 28).

.DESCRIPTION
    Generates a synthetic Windows-Live-Mail-like EML folder tree under
    $env:TEMP (never real mail - see tests/fixtures/README.md), runs the
    actual launcher + matching worker against it, produces a PST, and
    validates the exit code, JSON report, and errors CSV.

    If classic Outlook / Extended MAPI is unavailable, wlm2pst.exe exits 14
    (kOutlookUnavailable). That is reported as SKIPPED with an explicit
    reason and the script exits 0 - never a false pass, never a hard
    failure for something this script cannot control.

.PARAMETER BinDir
    Directory containing wlm2pst.exe (the launcher) and its worker
    executable(s) (wlm2pst-worker-x86.exe / wlm2pst-worker-x64.exe) sitting
    alongside it. If omitted, a handful of conventional build/dist output
    locations are searched (see Resolve-BinDir below).

.PARAMETER RootName
    --root-name passed to wlm2pst.exe. Defaults to a synthetic test name.

.PARAMETER KeepTemp
    Do not delete the generated source tree / output PST / sidecar files on
    exit. Useful when a run fails and you want to inspect the artifacts.

.EXAMPLE
    .\run_e2e.ps1
    .\run_e2e.ps1 -BinDir C:\wlm2pst\dist
    .\run_e2e.ps1 -BinDir ..\..\build\msvc-x86\src\Release -KeepTemp
#>
[CmdletBinding()]
param(
    [string]$BinDir,
    [string]$RootName = "WLM2PST E2E Test",
    [switch]$KeepTemp
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Exit codes this script itself uses. The exit codes wlm2pst.exe RETURNS are
# a separate, larger table (spec section 22, common/exit_codes.h); this
# script's own exit code only ever means "did the E2E harness's checks pass"
# (0) or "the harness could not complete its checks" (1). A skip is reported
# as exit 0 with a clearly-labeled warning, per the spec's "never falsely
# passing" rule for a skip - it is not a pass, but it must not fail CI for a
# machine that simply has no Outlook installed.
# ---------------------------------------------------------------------------
$ScriptOk = 0
$ScriptFail = 1

# Subset of common/exit_codes.h relevant to this script's own decisions.
$ExitSuccess = 0
$ExitSuccessWithFallbacks = 2
$ExitSuccessWithReadFailures = 3
$ExitOutlookUnavailable = 14

$ExitCodeMeanings = @{
    0  = 'Conversion and validation completed with no warnings.'
    2  = 'Completed and validated; some EMLs preserved as fallback attachments.'
    3  = 'Completed and validated; some source files could not be read.'
    10 = 'Invalid command-line arguments.'
    11 = 'Invalid source directory or no EML files found.'
    12 = 'Output already exists or cannot be safely overwritten.'
    13 = 'Insufficient free disk space.'
    14 = 'Classic Outlook or Extended MAPI is unavailable.'
    15 = 'Worker and Outlook bitness mismatch.'
    16 = 'Unicode PST provider could not be created or configured.'
    17 = 'Estimated output exceeds the safe single-PST ceiling.'
    18 = 'Disk full or output write failure during conversion.'
    19 = 'PST validation failed.'
    20 = 'State database is incompatible with the PST or source manifest.'
    21 = 'Source changed after the manifest was created.'
    22 = 'Integrity error during resume recovery.'
    23 = 'Temporary profile creation or cleanup failed critically.'
    130 = 'User cancelled with Ctrl+C.'
}

function Write-Section([string]$Title) {
    Write-Host ""
    Write-Host "=== $Title ===" -ForegroundColor Cyan
}

function Get-ExitCodeMeaning([int]$Code) {
    if ($ExitCodeMeanings.ContainsKey($Code)) { return $ExitCodeMeanings[$Code] }
    return '(unknown exit code)'
}

# ---------------------------------------------------------------------------
# Locate wlm2pst.exe.
# ---------------------------------------------------------------------------
function Resolve-BinDir([string]$Explicit) {
    $repoRoot = Resolve-Path (Join-Path $PSScriptRoot '../..')
    if ($Explicit) {
        $candidate = Resolve-Path -ErrorAction SilentlyContinue $Explicit
        if (-not $candidate) {
            throw "BinDir '$Explicit' does not exist."
        }
        return $candidate.Path
    }

    # wlm2pst.exe (the launcher) is only ever produced as a 32-bit binary
    # (src/CMakeLists.txt builds it only for CMAKE_SIZEOF_VOID_P EQUAL 4),
    # with worker executables of one or both bitnesses copied alongside it
    # by the packaging step (spec section 29). Search the packaged dist/
    # output first, then the raw msvc-x86 build output directly.
    $candidates = @(
        (Join-Path $repoRoot 'dist'),
        (Join-Path $repoRoot 'build/msvc-x86/src/Release'),
        (Join-Path $repoRoot 'build/msvc-x86/src/Debug'),
        (Join-Path $repoRoot 'build/msvc-x86/Release'),
        (Join-Path $repoRoot 'build/msvc-x86/Debug')
    )
    foreach ($dir in $candidates) {
        if (Test-Path (Join-Path $dir 'wlm2pst.exe')) {
            return (Resolve-Path $dir).Path
        }
    }

    throw ("wlm2pst.exe not found. Build it first (msvc-x86-release preset), " +
           "or pass -BinDir explicitly. Looked in:`n  " + ($candidates -join "`n  "))
}

# ---------------------------------------------------------------------------
# Build a synthetic Windows-Live-Mail-like source tree under $env:TEMP,
# copying fixtures from tests/fixtures/eml/ (synthetic only - see
# tests/fixtures/README.md). Returns the source root path.
# ---------------------------------------------------------------------------
function New-SyntheticSourceTree([string]$SourceRoot, [string]$FixturesDir) {
    function Copy-Fixture([string]$Name, [string]$DestDir, [string]$DestName) {
        New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
        $src = Join-Path $FixturesDir $Name
        if (-not (Test-Path $src)) {
            throw "Fixture '$Name' not found under $FixturesDir"
        }
        $dest = Join-Path $DestDir $DestName
        Copy-Item -Path $src -Destination $dest -Force
    }

    $account = Join-Path $SourceRoot 'account@example.invalid'
    $inbox = Join-Path $account 'Inbox'
    $sent = Join-Path $account 'Sent Items'
    $drafts = Join-Path $account 'Drafts'
    $customersClientA = Join-Path $account 'Customers/Client A'
    $hebrewFolder = Join-Path $account 'תיקייה'
    $malformed = Join-Path $account 'Malformed'

    # Inbox: a representative slice of well-formed, differently-encoded mail.
    Copy-Fixture 'utf8_plain.eml' $inbox 'inbox-001.eml'
    Copy-Fixture 'utf8_html.eml' $inbox 'inbox-002.eml'
    Copy-Fixture 'multipart_alternative.eml' $inbox 'inbox-003.eml'
    Copy-Fixture 'inline_cid_image.eml' $inbox 'inbox-004.eml'
    Copy-Fixture 'quoted_printable.eml' $inbox 'inbox-005.eml'
    Copy-Fixture 'base64_body.eml' $inbox 'inbox-006.eml'
    Copy-Fixture 'utf8_bom.eml' $inbox 'inbox-007.eml'
    Copy-Fixture 'multiple_received.eml' $inbox 'inbox-008.eml'
    Copy-Fixture 'empty_body.eml' $inbox 'inbox-009.eml'
    Copy-Fixture 'missing_subject.eml' $inbox 'inbox-010.eml'
    Copy-Fixture 'missing_date.eml' $inbox 'inbox-011.eml'
    Copy-Fixture 'invalid_date.eml' $inbox 'inbox-012.eml'

    # Sent Items: sent-folder alias detection (spec section 12).
    Copy-Fixture 'utf8_plain.eml' $sent 'sent-001.eml'
    Copy-Fixture 'utf8_html.eml' $sent 'sent-002.eml'

    # Drafts: X-Unsent + drafts-folder alias detection.
    Copy-Fixture 'x_unsent.eml' $drafts 'draft-001.eml'
    Copy-Fixture 'utf8_plain.eml' $drafts 'draft-002.eml'

    # A nested customer sub-folder, with attachments.
    Copy-Fixture 'single_attachment.eml' $customersClientA 'client-a-001.eml'
    Copy-Fixture 'multiple_attachments.eml' $customersClientA 'client-a-002.eml'
    Copy-Fixture 'hebrew_attachment_name.eml' $customersClientA 'client-a-003.eml'
    Copy-Fixture 'nested_eml_attachment.eml' $customersClientA 'client-a-004.eml'

    # A Hebrew-named folder: Unicode folder-name handling (spec section 9).
    Copy-Fixture 'rfc2047_subject.eml' $hebrewFolder 'hebrew-001.eml'
    Copy-Fixture 'windows1255_hebrew.eml' $hebrewFolder 'hebrew-002.eml'
    Copy-Fixture 'iso8859_8_hebrew.eml' $hebrewFolder 'hebrew-003.eml'

    # Deliberately malformed mail, to exercise the normalize/fallback path
    # (spec section 16) and produce a realistic mix of exit codes 0/2/3.
    Copy-Fixture 'header_nul.eml' $malformed 'malformed-001.eml'
    Copy-Fixture 'missing_separator.eml' $malformed 'malformed-002.eml'
    Copy-Fixture 'broken_mime_boundary.eml' $malformed 'malformed-003.eml'
    Copy-Fixture 'malformed_line_endings.eml' $malformed 'malformed-004.eml'
    Copy-Fixture 'zero_byte.eml' $malformed 'malformed-005.eml'

    # A root-level EML directly under the selected source root (spec
    # section 9 rule 4: root-level EMLs map to "Root Messages").
    Copy-Fixture 'utf8_plain.eml' $SourceRoot 'root-level-note.eml'

    return $SourceRoot
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
$scratchRoot = $null
$exitCode = $ScriptFail
try {
    Write-Section 'Locating wlm2pst.exe'
    $binDir = Resolve-BinDir $BinDir
    $launcher = Join-Path $binDir 'wlm2pst.exe'
    if (-not (Test-Path $launcher)) {
        throw "wlm2pst.exe not found in resolved BinDir '$binDir'."
    }
    Write-Host "Using launcher: $launcher"

    $fixturesDir = Resolve-Path (Join-Path $PSScriptRoot '../fixtures/eml')
    Write-Host "Fixtures: $fixturesDir"

    Write-Section 'Building synthetic source tree'
    $runToken = [Guid]::NewGuid().ToString('N').Substring(0, 12)
    $scratchRoot = Join-Path $env:TEMP "wlm2pst-e2e-$runToken"
    $sourceDir = Join-Path $scratchRoot 'source'
    $outputDir = Join-Path $scratchRoot 'output'
    New-Item -ItemType Directory -Force -Path $sourceDir | Out-Null
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    New-SyntheticSourceTree -SourceRoot $sourceDir -FixturesDir $fixturesDir | Out-Null

    $sourceEmlCount = (Get-ChildItem -Path $sourceDir -Recurse -File -Filter '*.eml' |
        Measure-Object).Count
    Write-Host "Synthetic source tree: $sourceEmlCount .eml files under $sourceDir"
    if ($sourceEmlCount -eq 0) {
        throw 'Synthetic source tree generation produced zero .eml files (harness bug).'
    }

    $outputPst = Join-Path $outputDir 'E2E-Test.pst'
    $reportPath = "$outputPst.wlm2pst-report.json"
    $errorsCsvPath = "$outputPst.wlm2pst-errors.csv"
    $statePath = "$outputPst.wlm2pst-state.sqlite"
    $logPath = "$outputPst.wlm2pst.log"

    Write-Section 'Running wlm2pst.exe'
    $wlmArgs = @(
        '--source', $sourceDir,
        '--output', $outputPst,
        '--root-name', $RootName
    )
    Write-Host "$launcher $($wlmArgs -join ' ')"
    & $launcher @wlmArgs
    $processExitCode = $LASTEXITCODE
    Write-Host "wlm2pst.exe exit code: $processExitCode ($(Get-ExitCodeMeaning $processExitCode))"

    if ($processExitCode -eq $ExitOutlookUnavailable) {
        Write-Warning ("SKIPPED: classic Outlook / Extended MAPI is not available on this " +
                       "machine (exit code $ExitOutlookUnavailable). This is not a pass or a " +
                       "failure - the E2E scenario could not be exercised here.")
        Write-Host 'SKIPPED'
        exit $ScriptOk
    }

    if ($processExitCode -notin @($ExitSuccess, $ExitSuccessWithFallbacks, $ExitSuccessWithReadFailures)) {
        throw ("wlm2pst.exe exited $processExitCode " +
               "($(Get-ExitCodeMeaning $processExitCode)); expected 0, 2, or 3.")
    }

    Write-Section 'Validating outputs'

    if (-not (Test-Path $outputPst)) {
        throw "Expected output PST not found: $outputPst"
    }
    Write-Host "PST created: $outputPst"

    if (-not (Test-Path $reportPath)) {
        throw "Expected JSON report not found: $reportPath"
    }
    $report = Get-Content -Raw -Path $reportPath | ConvertFrom-Json
    Write-Host "JSON report: $reportPath"

    if ($null -eq $report.sourceFiles) {
        throw "JSON report is missing 'sourceFiles'."
    }
    if ([int]$report.sourceFiles -ne $sourceEmlCount) {
        throw ("JSON report sourceFiles ($($report.sourceFiles)) does not match the " +
               "synthetic tree's actual .eml count ($sourceEmlCount).")
    }
    Write-Host "sourceFiles matches: $($report.sourceFiles)"

    foreach ($field in @('toolVersion', 'runId', 'source', 'output', 'startedAtUtc',
                         'completedAtUtc', 'sourceFolders', 'sourceBytes', 'importedNormally',
                         'preservedAsAttachment', 'failedToRead', 'outputFolders',
                         'validationStatus')) {
        if (-not (Get-Member -InputObject $report -Name $field -MemberType NoteProperty)) {
            throw "JSON report is missing required field '$field'."
        }
    }
    Write-Host "validationStatus: $($report.validationStatus)"
    if ($report.validationStatus -notin @('VALIDATED', 'VALIDATED_WITH_FALLBACKS')) {
        throw "Unexpected validationStatus '$($report.validationStatus)' for a non-failing exit code."
    }

    $expectFallbacksOrFailures = ($processExitCode -ne $ExitSuccess)
    $csvExists = Test-Path $errorsCsvPath
    if ($expectFallbacksOrFailures -and -not $csvExists) {
        throw ("Exit code $processExitCode implies fallback/read-failure messages, but no " +
               "errors CSV was created at $errorsCsvPath.")
    }
    if (-not $expectFallbacksOrFailures -and $csvExists) {
        throw ("Exit code 0 (no warnings) but an errors CSV exists at $errorsCsvPath " +
               "(spec section 21: the errors CSV is created only when errors or fallback " +
               "messages exist).")
    }

    if ($csvExists) {
        Write-Host "Errors CSV: $errorsCsvPath"
        $csvBytes = [System.IO.File]::ReadAllBytes($errorsCsvPath)
        if ($csvBytes.Length -lt 3 -or $csvBytes[0] -ne 0xEF -or $csvBytes[1] -ne 0xBB -or
            $csvBytes[2] -ne 0xBF) {
            throw "Errors CSV must be UTF-8 with a BOM (spec section 21); BOM not found."
        }
        $csvText = [System.Text.Encoding]::UTF8.GetString($csvBytes, 3, $csvBytes.Length - 3)
        $firstLine = ($csvText -split "`r`n|`n")[0]
        $expectedHeader = 'relative_source_path,status,attempt_count,hresult,win32_error,' +
                          'target_folder,source_size,sha256,details'
        if ($firstLine.TrimEnd() -ne $expectedHeader) {
            throw "Errors CSV header row does not match spec section 21.`nExpected: $expectedHeader`nGot:      $($firstLine.TrimEnd())"
        }
        Write-Host 'Errors CSV header and BOM verified.'
    } else {
        Write-Host 'No errors CSV (exit code 0: nothing to report), as expected.'
    }

    if (Test-Path $statePath) {
        Write-Host "State database present: $statePath"
    }
    if (Test-Path $logPath) {
        Write-Host "Log file present: $logPath"
    }

    Write-Section 'PASSED'
    Write-Host ("wlm2pst E2E run completed: exit code $processExitCode, " +
               "$($report.importedNormally) imported normally, " +
               "$($report.preservedAsAttachment) preserved as attachment, " +
               "$($report.failedToRead) failed to read.")
    $exitCode = $ScriptOk
}
catch {
    Write-Host ""
    Write-Host "FAILED: $($_.Exception.Message)" -ForegroundColor Red
    $exitCode = $ScriptFail
}
finally {
    if ($scratchRoot -and (Test-Path $scratchRoot)) {
        if ($KeepTemp) {
            Write-Host ""
            Write-Host "-KeepTemp set: leaving artifacts at $scratchRoot"
        } else {
            Remove-Item -Path $scratchRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

exit $exitCode
