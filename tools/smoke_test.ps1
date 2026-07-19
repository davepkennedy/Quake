<#
.SYNOPSIS
    Automated regression smoke test for the Quake modernization project.

.DESCRIPTION
    Replaces the manual "-condebug + a hand-built wait;wait;... argv chain +
    eyeball qconsole.log" ritual used throughout this project's verification
    history with a scripted, repeatable, pass/fail harness: builds the game
    (optional), chains it through a fixed sequence of levels, and checks the
    resulting qconsole.log for wpoly regressions and engine errors.

    Known quirk this script deliberately works around: glquake.exe's own
    command-line parser (WinMain in sys_win.cpp) splits argv purely on
    whitespace bytes with zero quote-awareness. Any argv token containing an
    embedded space gets corrupted if the launcher quotes it (stray literal
    quote characters end up glued onto the parsed command text). This script
    avoids that entirely by never constructing a token with an embedded
    space -- "+map" and "e1m1" are passed as separate array elements, and
    Quake's own Cmd_StuffCmds_f rejoins them with a space internally.

    Also note: Start-Process -ArgumentList was found to silently drop
    arguments to this particular binary during development of this script --
    even a bare "+quit" never reached the process. The call operator (&)
    does not have this problem, so that's what this script uses.

.PARAMETER Configuration
    Build configuration to test (Debug or Release). Default: Release.

.PARAMETER SkipBuild
    Skip the MSBuild step and test whatever binary is already in bin\<Configuration>.

.PARAMETER WaitFrames
    Frames to let each level settle before moving to the next (and before
    sampling wpoly). Default: 60.

.PARAMETER Tolerance
    Allowed absolute difference between a level's last-sampled wpoly and its
    recorded baseline before it's flagged as a regression. Not zero: even a
    static camera can see a few polys' worth of frame-to-frame variance from
    animating world geometry (doors, water, torches) at the moment sampled.
    Default: 10.

.PARAMETER TimeoutSeconds
    Hard ceiling on the whole run; the process is force-killed and the run
    is failed if this elapses. Default: 120.

.EXAMPLE
    .\tools\smoke_test.ps1
    Rebuilds Release and runs the full 6-level baseline chain.

.EXAMPLE
    .\tools\smoke_test.ps1 -SkipBuild -Configuration Debug
    Tests the already-built Debug binary without rebuilding.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipBuild,
    [int]$WaitFrames = 60,
    [int]$Tolerance = 10,
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

# The baseline level chain. Display titles are each level's worldspawn
# "message" field, exactly as printed to the console on load -- used to
# find each level's segment in qconsole.log. wpoly values are the
# established baselines carried through this project's verification
# history (see project_quake_modernisation.md).
$Levels = @(
    [pscustomobject]@{ Id = 'e1m1'; Title = 'the Slipgate Complex';  ExpectedWpoly = 247 }
    [pscustomobject]@{ Id = 'e1m2'; Title = 'Castle of the Damned';  ExpectedWpoly = 364 }
    [pscustomobject]@{ Id = 'e1m3'; Title = 'the Necropolis';        ExpectedWpoly = 197 }
    [pscustomobject]@{ Id = 'e1m4'; Title = 'the Grisly Grotto';     ExpectedWpoly = 233 }
    [pscustomobject]@{ Id = 'e1m7'; Title = 'The House of Chthon';   ExpectedWpoly = 519 }
    [pscustomobject]@{ Id = 'e1m8'; Title = 'Ziggurat Vertigo';      ExpectedWpoly = 559 }
)

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

function Invoke-Build {
    param([string]$Configuration)

    $msbuild = Find-MSBuild
    Write-Host "Building $Configuration|x64 ..." -ForegroundColor Cyan
    $slnPath = Join-Path $RepoRoot 'Quake.sln'
    & $msbuild $slnPath /p:Configuration=$Configuration /p:Platform=x64 /m:2 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }
}

function Invoke-SmokeRun {
    param([string]$ExePath, [int]$WaitFrames, [int]$TimeoutSeconds)

    $logPath = Join-Path $RepoRoot 'id1\qconsole.log'
    Remove-Item $logPath -ErrorAction SilentlyContinue

    # Every level segment is one self-contained token: a leading ';' keeps
    # it from being glued onto the preceding map/changelevel's argument as
    # a bogus extra parameter, and the repeated 'wait;'s make Cbuf_Execute
    # defer one frame at a time until the chain moves on.
    $waitChain = ';' + ('wait;' * $WaitFrames)

    $gameArgs = @('-basedir', $RepoRoot, '-condebug', '-nosound', '+r_speeds', '1')
    foreach ($level in $Levels) {
        $verb = if ($level -eq $Levels[0]) { '+map' } else { '+changelevel' }
        $gameArgs += @($verb, $level.Id, $waitChain)
    }
    # Host_Quit_f only exits directly when key_dest == key_console --
    # otherwise it opens the "Really quit?" confirmation menu and waits for
    # a keypress that a stuffcmd can never deliver, hanging forever.
    # +toggleconsole sets key_dest directly, letting +quit bypass it.
    $gameArgs += @('+toggleconsole', '+quit')

    Write-Host "Running $($Levels.Count)-level chain (this takes well under a minute) ..." -ForegroundColor Cyan

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $ExePath
    $psi.WorkingDirectory = Split-Path $ExePath -Parent
    $psi.UseShellExecute = $false
    foreach ($a in $gameArgs) { $psi.ArgumentList.Add($a) }

    $proc = [System.Diagnostics.Process]::Start($psi)
    $exited = $proc.WaitForExit($TimeoutSeconds * 1000)

    if (-not $exited) {
        $proc.Kill()
        throw "Smoke run did not exit within ${TimeoutSeconds}s -- force-killed. Check '$logPath' for where it got stuck."
    }

    if (-not (Test-Path $logPath)) {
        throw "No qconsole.log was produced at '$logPath' -- the process may not have started correctly."
    }
    return Get-Content $logPath
}

function Test-SmokeResults {
    param([string[]]$LogLines, [int]$Tolerance)

    $results = [System.Collections.Generic.List[pscustomobject]]::new()

    # Hard failures: anything that looks like an engine error, anywhere in
    # the log, independent of which level segment it happened in.
    $errorLines = $LogLines | Where-Object { $_ -match 'Sys_Error|Host_Error|PR_RunError|PROG_TO_EDICT: bad|EDICT_TO_PROG: bad|NEXT_EDICT: walked|PR_FieldAddress: bad' }

    # Find each level's title line, in order, to split the log into segments.
    $titleLineIndex = @{}
    for ($i = 0; $i -lt $LogLines.Count; $i++) {
        foreach ($level in $Levels) {
            if (-not $titleLineIndex.ContainsKey($level.Id) -and $LogLines[$i].Trim() -eq $level.Title) {
                $titleLineIndex[$level.Id] = $i
            }
        }
    }

    for ($li = 0; $li -lt $Levels.Count; $li++) {
        $level = $Levels[$li]
        if (-not $titleLineIndex.ContainsKey($level.Id)) {
            $results.Add([pscustomobject]@{
                Level = $level.Id; Pass = $false; Detail = 'level title never appeared in qconsole.log (level did not load)'
            })
            continue
        }
        $start = $titleLineIndex[$level.Id]
        $end = if ($li -lt $Levels.Count - 1 -and $titleLineIndex.ContainsKey($Levels[$li + 1].Id)) {
            $titleLineIndex[$Levels[$li + 1].Id]
        } else {
            $LogLines.Count
        }

        $lastWpoly = $null
        for ($j = $start; $j -lt $end; $j++) {
            if ($LogLines[$j] -match '(\d+)\s+wpoly') {
                $lastWpoly = [int]$Matches[1]
            }
        }

        if ($null -eq $lastWpoly) {
            $results.Add([pscustomobject]@{
                Level = $level.Id; Pass = $false; Detail = 'no wpoly reading captured for this level (r_speeds not printing?)'
            })
            continue
        }

        $diff = [Math]::Abs($lastWpoly - $level.ExpectedWpoly)
        $pass = $diff -le $Tolerance
        $results.Add([pscustomobject]@{
            Level = $level.Id
            Pass  = $pass
            Detail = "wpoly=$lastWpoly baseline=$($level.ExpectedWpoly) diff=$diff (tolerance=$Tolerance)"
        })
    }

    return [pscustomobject]@{
        LevelResults = $results
        ErrorLines   = $errorLines
    }
}

# ---------------------------------------------------------------------------

if (-not $SkipBuild) {
    Invoke-Build -Configuration $Configuration
} else {
    Write-Host "Skipping build (-SkipBuild)." -ForegroundColor Yellow
}

$exePath = Join-Path $RepoRoot "bin\$Configuration\glquake.exe"
if (-not (Test-Path $exePath)) {
    throw "glquake.exe not found at '$exePath'. Build it first or omit -SkipBuild."
}

$logLines = Invoke-SmokeRun -ExePath $exePath -WaitFrames $WaitFrames -TimeoutSeconds $TimeoutSeconds
$results = Test-SmokeResults -LogLines $logLines -Tolerance $Tolerance

Write-Host ""
Write-Host "=== Smoke Test Results ($Configuration) ===" -ForegroundColor Cyan
$allPass = $true
foreach ($r in $results.LevelResults) {
    if ($r.Pass) {
        Write-Host ("  PASS  {0,-6} {1}" -f $r.Level, $r.Detail) -ForegroundColor Green
    } else {
        Write-Host ("  FAIL  {0,-6} {1}" -f $r.Level, $r.Detail) -ForegroundColor Red
        $allPass = $false
    }
}

if ($results.ErrorLines.Count -gt 0) {
    $allPass = $false
    Write-Host ""
    Write-Host "  Engine errors found in qconsole.log:" -ForegroundColor Red
    foreach ($e in $results.ErrorLines) {
        Write-Host "    $e" -ForegroundColor Red
    }
}

Write-Host ""
if ($allPass) {
    Write-Host "ALL CHECKS PASSED" -ForegroundColor Green
    exit 0
} else {
    Write-Host "SMOKE TEST FAILED" -ForegroundColor Red
    exit 1
}
