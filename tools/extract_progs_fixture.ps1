<#
.SYNOPSIS
    One-time extraction of progs.dat from id1/PAK0.PAK into tests/fixtures/,
    for the QuakeC VM test harness (see tests/test_pr_exec.cpp).

.DESCRIPTION
    The .pak format is a flat, uncompressed archive: a 12-byte header
    (4-byte "PACK" id, int dirofs, int dirlen) followed by file data, with
    a directory of 64-byte entries (56-byte name, int filepos, int filelen)
    living at dirofs. This script reads the directory, finds "progs.dat",
    and copies its filelen bytes verbatim out to tests/fixtures/progs.dat.

    This is a one-time asset-extraction step, not part of the regular build
    or test run -- the extracted fixture is checked into git (same as
    id1/PAK0.PAK itself) so tests never need to re-run this. Kept here,
    rather than run-once-and-discarded, purely to document how to regenerate
    the fixture if it's ever lost or needs to come from a different pak.

.EXAMPLE
    .\tools\extract_progs_fixture.ps1
#>
[CmdletBinding()]
param(
    [string]$PakPath = (Join-Path $PSScriptRoot '..\id1\PAK0.PAK'),
    [string]$OutPath = (Join-Path $PSScriptRoot '..\tests\fixtures\progs.dat')
)

$ErrorActionPreference = 'Stop'

$PakPath = (Resolve-Path $PakPath).Path
Write-Host "Reading $PakPath ..." -ForegroundColor Cyan

$fs = [System.IO.File]::OpenRead($PakPath)
try {
    $br = New-Object System.IO.BinaryReader($fs)

    $id = [System.Text.Encoding]::ASCII.GetString($br.ReadBytes(4))
    if ($id -ne 'PACK') {
        throw "Not a PACK file (id='$id')"
    }
    $dirofs = $br.ReadInt32()
    $dirlen = $br.ReadInt32()

    # dpackfile_t: char name[56]; int filepos; int filelen;  -- 64 bytes, no padding
    $entrySize = 64
    $numEntries = $dirlen / $entrySize
    if ($dirlen % $entrySize -ne 0) {
        throw "dirlen ($dirlen) is not a multiple of the 64-byte directory entry size"
    }

    $fs.Seek($dirofs, [System.IO.SeekOrigin]::Begin) | Out-Null

    $found = $false
    for ($i = 0; $i -lt $numEntries; $i++) {
        $nameBytes = $br.ReadBytes(56)
        # Name is a null-terminated C string in a 56-byte field; some pak
        # tools (as with this one) don't zero-fill the tail past the
        # terminator, leaving stale buffer bytes after it -- split on the
        # first null rather than trimming trailing nulls.
        $name = [System.Text.Encoding]::ASCII.GetString($nameBytes).Split([char]0)[0]
        $filepos = $br.ReadInt32()
        $filelen = $br.ReadInt32()

        if ($name -eq 'progs.dat') {
            Write-Host "Found progs.dat: filepos=$filepos filelen=$filelen" -ForegroundColor Green
            $fs.Seek($filepos, [System.IO.SeekOrigin]::Begin) | Out-Null
            $data = $br.ReadBytes($filelen)

            $outDir = Split-Path -Parent $OutPath
            if (-not (Test-Path $outDir)) {
                New-Item -ItemType Directory -Path $outDir -Force | Out-Null
            }
            [System.IO.File]::WriteAllBytes($OutPath, $data)
            Write-Host "Wrote $OutPath ($($data.Length) bytes)" -ForegroundColor Green
            $found = $true
            break
        }
    }

    if (-not $found) {
        throw "progs.dat not found in $PakPath's directory ($numEntries entries scanned)"
    }
}
finally {
    $fs.Dispose()
}
