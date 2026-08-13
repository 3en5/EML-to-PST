#requires -Version 5.1
<#
.SYNOPSIS
    Collects the built WLM2PST binaries into a distributable
    dist\WLM2PST-<version>\ folder and zips it.

.DESCRIPTION
    Requires that .\scripts\build.ps1 (or the equivalent manual cmake
    --preset msvc-x86-release / msvc-x64-release build) has already run, so
    that build\msvc-x86\src\Release\wlm2pst.exe,
    build\msvc-x86\src\Release\wlm2pst-worker-x86.exe, and
    build\msvc-x64\src\Release\wlm2pst-worker-x64.exe exist.

    Does NOT copy any Outlook or MAPI DLLs into the package - classic
    Outlook is a runtime requirement the end user must already have
    installed, and its binaries are not WLM2PST's to redistribute.

.PARAMETER Version
    Package version. Defaults to the VERSION declared in the top-level
    CMakeLists.txt `project(WLM2PST VERSION x.y.z ...)` call.

.EXAMPLE
    .\scripts\package.ps1

.EXAMPLE
    .\scripts\package.ps1 -Version 1.0.1
#>
[CmdletBinding()]
param(
    [string]$Version
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot

function Get-ProjectVersion {
    param([string]$CMakeListsPath)
    if (-not (Test-Path -LiteralPath $CMakeListsPath)) {
        throw "CMakeLists.txt not found at $CMakeListsPath"
    }
    $text = Get-Content -LiteralPath $CMakeListsPath -Raw
    $match = [regex]::Match($text, 'project\s*\(\s*WLM2PST\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', "IgnoreCase")
    if (-not $match.Success) {
        throw "Could not parse project VERSION out of $CMakeListsPath"
    }
    return $match.Groups[1].Value
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = Get-ProjectVersion -CMakeListsPath (Join-Path $RepoRoot "CMakeLists.txt")
}
Write-Host "Packaging WLM2PST $Version" -ForegroundColor Cyan

# --- Locate required binaries -----------------------------------------------

$binaries = @(
    @{ Source = Join-Path $RepoRoot "build\msvc-x86\src\Release\wlm2pst.exe"; Name = "wlm2pst.exe" },
    @{ Source = Join-Path $RepoRoot "build\msvc-x86\src\Release\wlm2pst-worker-x86.exe"; Name = "wlm2pst-worker-x86.exe" },
    @{ Source = Join-Path $RepoRoot "build\msvc-x64\src\Release\wlm2pst-worker-x64.exe"; Name = "wlm2pst-worker-x64.exe" }
)

$missing = @()
foreach ($binary in $binaries) {
    if (-not (Test-Path -LiteralPath $binary.Source)) {
        $missing += $binary.Source
    }
}
if ($missing.Count -gt 0) {
    Write-Host "Missing required build outputs:" -ForegroundColor Red
    foreach ($path in $missing) {
        Write-Host "  $path"
    }
    throw "Build outputs are missing. Run .\scripts\build.ps1 first (both msvc-x86-release and msvc-x64-release presets must build successfully)."
}

# --- Assemble dist\WLM2PST-<version>\ ---------------------------------------

$distRoot = Join-Path $RepoRoot "dist"
$packageDir = Join-Path $distRoot "WLM2PST-$Version"
$licensesDir = Join-Path $packageDir "LICENSES"

if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
New-Item -ItemType Directory -Path $licensesDir -Force | Out-Null

foreach ($binary in $binaries) {
    Copy-Item -LiteralPath $binary.Source -Destination (Join-Path $packageDir $binary.Name) -Force
    Write-Host "  copied $($binary.Name)"
}

$readmeSource = Join-Path $RepoRoot "packaging\README.txt"
$noticesSource = Join-Path $RepoRoot "THIRD-PARTY-NOTICES.txt"
if (-not (Test-Path -LiteralPath $readmeSource)) {
    throw "packaging\README.txt not found at $readmeSource"
}
if (-not (Test-Path -LiteralPath $noticesSource)) {
    throw "THIRD-PARTY-NOTICES.txt not found at $noticesSource"
}
Copy-Item -LiteralPath $readmeSource -Destination (Join-Path $packageDir "README.txt") -Force
Copy-Item -LiteralPath $noticesSource -Destination (Join-Path $packageDir "THIRD-PARTY-NOTICES.txt") -Force

# SQLite (public domain) blessing text - see third_party/sqlite/README.md.
$sqliteLicenseText = @"
SQLite is in the Public Domain
===============================

WLM2PST vendors the SQLite amalgamation (sqlite3.c / sqlite3.h), version
3.38.2, used only for the local crash-safe resume state database
(<output>.wlm2pst-state.sqlite). SQLite is not licensed software; the
authors disclaim copyright interest in the SQLite source code so that it
may be used freely for any purpose, commercial or non-commercial.

See https://sqlite.org/copyright.html for the full public-domain statement,
and third_party/sqlite/README.md in the WLM2PST source tree for vendoring
details.
"@
Set-Content -LiteralPath (Join-Path $licensesDir "SQLITE.txt") -Value $sqliteLicenseText -Encoding UTF8

Write-Host "  wrote README.txt, THIRD-PARTY-NOTICES.txt, LICENSES\SQLITE.txt"

# --- Zip ---------------------------------------------------------------------

$zipPath = Join-Path $distRoot "WLM2PST-$Version.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $zipPath -Force

Write-Host ""
Write-Host "Package ready:" -ForegroundColor Green
Write-Host "  $packageDir"
Write-Host "  $zipPath"
Write-Host ""
Write-Host "Note: the msvc-x86-release/msvc-x64-release presets do not set /MT;" -ForegroundColor Yellow
Write-Host "binaries link the default dynamic MSVC runtime (/MD). End users need" -ForegroundColor Yellow
Write-Host "the matching Visual C++ Redistributable installed (see README.txt)." -ForegroundColor Yellow
