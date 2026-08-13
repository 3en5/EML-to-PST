#requires -Version 5.1
<#
.SYNOPSIS
    Configures and builds WLM2PST for both Win32 (launcher + x86 worker) and
    x64 (x64 worker) using the CMake presets defined in CMakePresets.json.

.PARAMETER Configuration
    Build configuration. The msvc-x86-release / msvc-x64-release presets
    currently only produce a Release configuration; this parameter is
    accepted for forward compatibility and is validated against that.

.PARAMETER SkipTests
    Skip running ctest after building each architecture.

.EXAMPLE
    .\scripts\build.ps1

.EXAMPLE
    .\scripts\build.ps1 -SkipTests
#>
[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [switch]$SkipTests
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

if ($Configuration -ne "Release") {
    Write-Warning "CMakePresets.json currently only defines a Release configuration for msvc-x86-release/msvc-x64-release. Continuing with -Configuration $Configuration, but the presets themselves will still build Release."
}

# --- Prerequisite checks (warn, do not fail, so a partially-configured dev
#     machine still gets a clear message from cmake itself below) ---------

Write-Host "Checking prerequisites..." -ForegroundColor Cyan

$cmakeFound = $true
try {
    $cmakeVersionOutput = & cmake --version 2>&1
    if ($LASTEXITCODE -ne 0) {
        $cmakeFound = $false
    }
    else {
        Write-Host "  cmake: $($cmakeVersionOutput | Select-Object -First 1)"
    }
}
catch {
    $cmakeFound = $false
}
if (-not $cmakeFound) {
    throw "cmake was not found on PATH. Install CMake >= 3.21 and/or open a Visual Studio Developer PowerShell."
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
    $vsInfo = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName 2>&1
    if ([string]::IsNullOrWhiteSpace($vsInfo)) {
        Write-Warning "vswhere did not report a Visual Studio install with the C++ desktop workload. The cmake configure step below will fail if VS 2022 with C++ tools is not actually installed."
    }
    else {
        Write-Host "  Visual Studio: $vsInfo"
    }
}
else {
    Write-Warning "vswhere.exe not found; skipping Visual Studio detection (this is only a diagnostic, not a hard requirement)."
}

# --- Configure + build both architectures ---------------------------------

$presets = @("msvc-x86-release", "msvc-x64-release")

foreach ($preset in $presets) {
    Invoke-Native -FilePath "cmake" -ArgumentList @("--preset", $preset) `
        -Description "Configure ($preset)"
    Invoke-Native -FilePath "cmake" -ArgumentList @("--build", "--preset", $preset) `
        -Description "Build ($preset)"
}

# --- Tests ------------------------------------------------------------------

if ($SkipTests) {
    Write-Host "Skipping tests (-SkipTests)." -ForegroundColor Yellow
}
else {
    foreach ($preset in $presets) {
        Invoke-Native -FilePath "ctest" -ArgumentList @("--preset", $preset) `
            -Description "Test ($preset)"
    }
}

# --- Summary of collected artifacts -----------------------------------------

Write-Host ""
Write-Host "Build complete." -ForegroundColor Green

$expected = @(
    (Join-Path $RepoRoot "build\msvc-x86\src\$Configuration\wlm2pst.exe"),
    (Join-Path $RepoRoot "build\msvc-x86\src\$Configuration\wlm2pst-worker-x86.exe"),
    (Join-Path $RepoRoot "build\msvc-x64\src\$Configuration\wlm2pst-worker-x64.exe")
)
foreach ($path in $expected) {
    if (Test-Path -LiteralPath $path) {
        Write-Host "  OK    $path"
    }
    else {
        Write-Warning "  MISSING  $path"
    }
}

Write-Host ""
Write-Host "Next: .\scripts\package.ps1"
