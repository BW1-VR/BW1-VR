<#
.SYNOPSIS
    Stops a commit that holds a file of the game, a build output or a very
    large file.

.DESCRIPTION
    The repo must never hold a file of Black & White. This script checks the
    tracked files (or, with -Staged, the files of the commit that is being
    made) and ends with exit code 1 when it finds one of these:

      - a file with a name or a type that the game uses
      - an .exe, a .dll or a .lib outside the folder third_party
      - a file larger than 12 MB

    The git hook .githooks\pre-commit runs it with -Staged, and the build on
    GitHub runs it on each push.

.PARAMETER Staged
    Checks only the files that are staged for the next commit.
#>
[CmdletBinding()]
param([switch]$Staged)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$Repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$MaxBytes = 12MB

# Types and names of the files of the game. A document or a script of the
# mod never has one of these.
$GameTypes = @('.sad', '.l3d', '.hbn', '.sav', '.lhp', '.raw', '.wnd', '.ter', '.chl',
               '.exc', '.bwe', '.ani', '.lnd', '.sgl', '.xmc', '.map', '.tga_', '.vsq')
$GameNames = @('runblack.exe', 'runblack.exe.bak', 'creature.lhp', 'helpstats.dat')
$GameFolders = @('Black & White Game', 'Profiles/', 'Saved Games/', 'Data/CreatureMesh/', 'Audio/Sfx')

if ($Staged) {
    $paths = @(& git -C $Repo diff --cached --name-only --diff-filter=ACMR)
} else {
    $paths = @(& git -C $Repo ls-files)
}

$bad = @()
foreach ($rel in $paths) {
    if (-not $rel) { continue }
    $full = Join-Path $Repo $rel
    if (-not (Test-Path -LiteralPath $full)) { continue }
    $leaf = [System.IO.Path]::GetFileName($rel).ToLowerInvariant()
    $ext = [System.IO.Path]::GetExtension($rel).ToLowerInvariant()
    $inThirdParty = $rel -like 'third_party/*'

    if ($GameNames -contains $leaf) { $bad += "$rel  is a file of the game" }
    if ($GameTypes -contains $ext) { $bad += "$rel  has a file type of the game ($ext)" }
    foreach ($folder in $GameFolders) {
        if ($rel -like "*$folder*") { $bad += "$rel  is in a folder of the game ($folder)" }
    }
    if (@('.exe', '.dll', '.lib') -contains $ext -and -not $inThirdParty) {
        $bad += "$rel  is a binary outside third_party"
    }
    $size = (Get-Item -LiteralPath $full).Length
    if ($size -gt $MaxBytes) { $bad += ("$rel  is {0:N1} MB, more than 12 MB" -f ($size / 1MB)) }

}

if ($bad.Count -gt 0) {
    Write-Host ''
    Write-Host 'THE REPO CHECK FAILED' -ForegroundColor Red
    $bad | Sort-Object -Unique | ForEach-Object { Write-Host ('   ' + $_) -ForegroundColor Red }
    Write-Host ''
    Write-Host '   Take these out of the commit. A file of the game never goes in the repo.'
    exit 1
}
Write-Host ("Repo check: {0} files. No file of the game found." -f $paths.Count)
exit 0
