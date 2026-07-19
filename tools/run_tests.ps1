<#
.SYNOPSIS
    Runs the QuakeTests unit-test suite (tests\QuakeTests.vcxproj).

.DESCRIPTION
    Companion to smoke_test.ps1, but far simpler: QuakeTests.exe is a plain
    console app testing pure logic (currently mathlib.cpp) against a
    test-only Sys_Error stub (see tests\test_stubs.cpp) -- no window, no GL
    context, no game assets, none of smoke_test.ps1's argv/quit-menu
    workarounds are needed. Builds (optional), runs QuakeTests.exe, and
    forwards its exit code -- doctest already prints a clear pass/fail
    report on its own.

.PARAMETER Configuration
    Build configuration to test (Debug or Release). Default: Release.

.PARAMETER SkipBuild
    Skip the MSBuild step and run whatever binary is already in
    bin\<Configuration>\Tests.

.EXAMPLE
    .\tools\run_tests.ps1
    Rebuilds Release and runs the unit tests.

.EXAMPLE
    .\tools\run_tests.ps1 -SkipBuild -Configuration Debug
    Runs the already-built Debug test binary without rebuilding.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Find-MSBuild {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at '$vswhere' -- cannot locate MSBuild."
    }
    $installPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $installPath) {
        throw "vswhere found no Visual Studio installation with MSBuild."
    }
    $msbuild = Join-Path $installPath 'MSBuild\Current\Bin\amd64\MSBuild.exe'
    if (-not (Test-Path $msbuild)) {
        throw "Expected MSBuild.exe at '$msbuild' but it doesn't exist."
    }
    return $msbuild
}

if (-not $SkipBuild) {
    $msbuild = Find-MSBuild
    Write-Host "Building $Configuration|x64 (QuakeTests) ..." -ForegroundColor Cyan
    # Build via the .sln, not the .vcxproj directly -- QuakeTests.vcxproj's
    # include paths depend on $(SolutionDir), which is only set in solution
    # context; building the .vcxproj standalone silently fails to find
    # external\glm and external\doctest.
    $slnPath = Join-Path $RepoRoot 'Quake.sln'
    & $msbuild $slnPath /t:QuakeTests /p:Configuration=$Configuration /p:Platform=x64 /m:2 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Host "Skipping build (-SkipBuild)." -ForegroundColor Yellow
}

$exePath = Join-Path $RepoRoot "bin\$Configuration\Tests\QuakeTests.exe"
if (-not (Test-Path $exePath)) {
    throw "QuakeTests.exe not found at '$exePath'. Build it first or omit -SkipBuild."
}

& $exePath
exit $LASTEXITCODE
