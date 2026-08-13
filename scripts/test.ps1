#requires -Version 5.1
<#
.SYNOPSIS
    Runs the WLM2PST unit test suites for whichever build trees exist, and
    optionally the end-to-end test.

.DESCRIPTION
    Runs `ctest --preset msvc-x86-release` and `ctest --preset
    msvc-x64-release` for each preset whose build tree
    (build\msvc-x86 / build\msvc-x64) has already been configured by
    scripts\build.ps1 or a manual `cmake --preset ...`. A preset whose build
    tree does not exist is skipped with a message rather than failing the
    whole run, so this script also works after building only one
    architecture.

.PARAMETER E2E
    Also run tests\e2e\run_e2e.ps1 if that script exists. This is off by
    default because the end-to-end test drives real classic Outlook and
    needs it installed and available.

.EXAMPLE
    .\scripts\test.ps1

.EXAMPLE
    .\scripts\test.ps1 -E2E
#>
[CmdletBinding()]
param(
    [switch]$E2E
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$Description
    )
    Write-Host ">> $Description" -ForegroundColor Cyan
    Write-Host "   $FilePath $($ArgumentList -join ' ')"
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

$presetTrees = @{
    "msvc-x86-release" = (Join-Path $RepoRoot "build\msvc-x86")
    "msvc-x64-release" = (Join-Path $RepoRoot "build\msvc-x64")
}

$ranAny = $false
foreach ($preset in $presetTrees.Keys) {
    $tree = $presetTrees[$preset]
    if (Test-Path -LiteralPath $tree) {
        Invoke-Native -FilePath "ctest" -ArgumentList @("--preset", $preset) `
            -Description "Test ($preset)"
        $ranAny = $true
    }
    else {
        Write-Warning "Skipping $preset: build tree not found at $tree (run .\scripts\build.ps1 or `cmake --preset $preset` first)."
    }
}

if (-not $ranAny) {
    throw "No build trees found under build\msvc-x86 or build\msvc-x64. Run .\scripts\build.ps1 first."
}

if ($E2E) {
    $e2eScript = Join-Path $RepoRoot "tests\e2e\run_e2e.ps1"
    if (Test-Path -LiteralPath $e2eScript) {
        Invoke-Native -FilePath "powershell.exe" -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $e2eScript) `
            -Description "End-to-end test"
    }
    else {
        Write-Warning "-E2E was requested but $e2eScript does not exist yet; skipping."
    }
}

Write-Host ""
Write-Host "Tests complete." -ForegroundColor Green
