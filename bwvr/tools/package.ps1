<#
.SYNOPSIS
    Builds the Black & White VR Mod and makes the Windows installer.

.DESCRIPTION
    One command turns a clean working copy into the file that goes out to
    players. It does four things, and it stops at the first one that fails:

      1. Builds the mod and the launcher with CMake.
      2. Stages every file that ships into one folder.
      3. Checks each third party binary against a recorded SHA-256.
      4. Runs the Inno Setup compiler on installer\bwvr.iss.

    It never touches a game folder, a registry value or the game itself.

.PARAMETER TestBuild
    Also makes a second installer that writes no uninstall registry key and
    accepts /SKIPGAMECHECK=1, for install tests into a throwaway folder.

.PARAMETER SkipBuild
    Uses the binaries that are in the build folder already.

.PARAMETER StageOnly
    Stops after step 3. The staging folder then holds each file that ships,
    and no installer is made. dev.ps1 uses this to put the mod in a game
    folder. Inno Setup is not needed.

.PARAMETER Version
    The version to stamp on the installer. The default is the value of
    BWVR_VERSION_TEXT in src\common\version.h.

.PARAMETER OutDir
    Where the installer goes. The default is bwvr\out.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File bwvr\tools\package.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File bwvr\tools\package.ps1 -TestBuild
#>
[CmdletBinding()]
param(
    [switch]$TestBuild,
    [switch]$SkipBuild,
    [switch]$StageOnly,
    [string]$Version,
    [string]$OutDir
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------- the paths

$ToolsDir  = $PSScriptRoot
$BwvrRoot  = Split-Path -Parent $ToolsDir
$RepoRoot  = Split-Path -Parent $BwvrRoot
$BuildDir  = Join-Path $BwvrRoot 'build'
$InstDir   = Join-Path $BwvrRoot 'installer'
$IssFile   = Join-Path $InstDir 'bwvr.iss'
$VersionH  = Join-Path $BwvrRoot 'src\common\version.h'

if (-not $OutDir) { $OutDir = Join-Path $BwvrRoot 'out' }
$StageDir = Join-Path $OutDir 'stage'

$LauncherExe = 'Black & White VR Mod.exe'

# The SHA-256 of every binary that comes from somebody else. A change here is
# a new version of that project, so the value is updated by hand, on purpose,
# after the new build is tested.
#
#   dxwrapper 1.8.8600.25, from third_party\backends\dx7.games
#   OpenXR loader 1.1.43, Apache-2.0, built from third_party\OpenXR-SDK
$ThirdPartyHashes = @{
    'dxwrapper\ddraw.dll'     = '7C843006F81983617A37F57D7FB615D23BDA99860B71AB745F2B0CEA6AB00474'
    'dxwrapper\dxwrapper.dll' = 'EC42E51CBB4408518D6348706557B18EF50A87485C8BFA1839C895123FA3295F'
    'openxr_loader.dll'       = '1BCF618E5B37314AF8622AB3EFEABECEEB15A8AD702FCE37B9472A6EC68BBD97'
}

# What ships, and where each file comes from. The path on the left is the
# path inside the staging folder, which is what bwvr.iss reads.
#
# The mod's ddraw.dll comes from src\mod. The probe builds a file of the same
# name in src\probe, and that one must never ship.
$Payload = @(
    @{ Dest = 'ddraw.dll'
       Src  = Join-Path $BuildDir 'src\mod\Release\ddraw.dll'
       What = 'the mod' }
    @{ Dest = 'openxr_loader.dll'
       Src  = Join-Path $BuildDir 'src\mod\Release\openxr_loader.dll'
       What = 'the OpenXR loader' }
    @{ Dest = $LauncherExe
       Src  = Join-Path $BuildDir ('src\launcher\Release\' + $LauncherExe)
       What = 'the launcher' }
    @{ Dest = 'dxwrapper\ddraw.dll'
       Src  = Join-Path $RepoRoot 'third_party\backends\dx7.games\ddraw.dll'
       What = 'the dxwrapper stub' }
    @{ Dest = 'dxwrapper\dxwrapper.dll'
       Src  = Join-Path $RepoRoot 'third_party\backends\dx7.games\dxwrapper.dll'
       What = 'dxwrapper' }
    @{ Dest = 'dxwrapper\dxwrapper.ini'
       Src  = Join-Path $InstDir 'payload\dxwrapper.ini'
       What = 'the dxwrapper settings' }
    @{ Dest = 'dxwrapper\ddraw.ini'
       Src  = Join-Path $InstDir 'payload\ddraw.ini'
       What = 'the stub settings' }
    @{ Dest = 'README.txt'
       Src  = Join-Path $InstDir 'README.txt'
       What = 'the player readme' }
    @{ Dest = 'Notices.txt'
       Src  = Join-Path $InstDir 'Notices.txt'
       What = 'the third party notices' }
)

# --------------------------------------------------------------- the helpers

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host ('== ' + $Text) -ForegroundColor Cyan
}

function Write-Detail([string]$Text) {
    Write-Host ('   ' + $Text)
}

function Stop-WithMessage([string]$Text) {
    Write-Host ''
    Write-Host 'PACKAGING STOPPED' -ForegroundColor Red
    foreach ($line in ($Text -split "`n")) {
        Write-Host ('   ' + $line) -ForegroundColor Red
    }
    Write-Host ''
    exit 1
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToUpperInvariant()
}

function Get-VersionFromHeader([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        if ($line -match '^\s*#define\s+BWVR_VERSION_TEXT\s+"([^"]+)"') {
            return $Matches[1]
        }
    }
    return $null
}

function Find-Iscc() {
    # 32 bit Windows has no ProgramFiles(x86), so each root is checked before
    # it is used.
    $roots = @(
        (Join-Path $env:LOCALAPPDATA 'Programs'),
        ${env:ProgramFiles(x86)},
        $env:ProgramFiles
    )
    foreach ($root in $roots) {
        if (-not $root) { continue }
        $p = Join-Path $root 'Inno Setup 6\ISCC.exe'
        if (Test-Path -LiteralPath $p) { return $p }
    }
    $cmd = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# Reads a file as bytes and looks for Needle three ways: as plain bytes, and
# as UTF-16 at both alignments. Case does not matter. Binary files hold their
# text in all three shapes, so all three are searched.
function Test-FileHoldsText([string]$Path, [string]$Needle) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -eq 0) { return $false }

    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    $views = New-Object System.Collections.ArrayList
    [void]$views.Add($latin1.GetString($bytes))
    [void]$views.Add([System.Text.Encoding]::Unicode.GetString($bytes))
    if ($bytes.Length -gt 2) {
        [void]$views.Add([System.Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1))
    }
    foreach ($view in $views) {
        if ($view.IndexOf($Needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            return $true
        }
    }
    return $false
}

function Invoke-Iscc {
    param(
        [string]$Iscc,
        [string]$Iss,
        [string[]]$Defines
    )
    $line = @($Iss)
    foreach ($d in $Defines) { $line += ('/D' + $d) }
    $line += '/Q'
    # Out-Host keeps the compiler's own output on the screen and out of the
    # return value, so only the exit code comes back.
    & $Iscc @line | Out-Host
    return $LASTEXITCODE
}

# ------------------------------------------------------------------ the work

Write-Host ''
Write-Host 'Black & White VR Mod: packaging' -ForegroundColor White

if (-not (Test-Path -LiteralPath $IssFile)) {
    Stop-WithMessage ("The installer script is missing:`n" + $IssFile)
}

if (-not $Version) {
    $Version = Get-VersionFromHeader $VersionH
    if (-not $Version) {
        Stop-WithMessage ("Cannot read BWVR_VERSION_TEXT from:`n" + $VersionH +
            "`nGive the version with -Version instead.")
    }
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    Stop-WithMessage ("The version must look like 1.0.0. It is: " + $Version)
}
Write-Detail ('Version ' + $Version)

$Iscc = $null
if (-not $StageOnly) {
    $Iscc = Find-Iscc
    if (-not $Iscc) {
        Stop-WithMessage ("Cannot find ISCC.exe, the Inno Setup compiler.`n" +
            "Install Inno Setup 6, or put ISCC.exe on the PATH.")
    }
    Write-Detail ('Inno Setup compiler: ' + $Iscc)
}

# ---- 1. build

if ($SkipBuild) {
    Write-Step 'Build: skipped'
} else {
    Write-Step 'Build'
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt'))) {
        Stop-WithMessage ("There is no CMake build folder at:`n" + $BuildDir +
            "`nMake one first. The project is 32 bit:`n" +
            "  cmake -S bwvr -B bwvr\build -A Win32")
    }
    & cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        Stop-WithMessage 'The build failed. Fix it, then run this again.'
    }
}

# ---- 2. stage

Write-Step 'Staging'
if (Test-Path -LiteralPath $StageDir) {
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
[void](New-Item -ItemType Directory -Path $StageDir -Force)

$missing = @()
foreach ($item in $Payload) {
    if (-not (Test-Path -LiteralPath $item.Src)) {
        $missing += ('   ' + $item.Dest + '  (' + $item.What + ')' + "`n      expected at: " + $item.Src)
    }
}
if ($missing.Count -gt 0) {
    $hint = ''
    if (($missing -join '') -match [regex]::Escape($LauncherExe)) {
        $hint = "`n`nThe launcher must build as `"" + $LauncherExe + "`".`n" +
                "Check OUTPUT_NAME in bwvr\src\launcher\CMakeLists.txt."
    }
    Stop-WithMessage ("These files do not ship because they are not there:`n" +
        ($missing -join "`n") + $hint)
}

foreach ($item in $Payload) {
    $dest = Join-Path $StageDir $item.Dest
    $destDir = Split-Path -Parent $dest
    if (-not (Test-Path -LiteralPath $destDir)) {
        [void](New-Item -ItemType Directory -Path $destDir -Force)
    }
    Copy-Item -LiteralPath $item.Src -Destination $dest -Force
    $size = (Get-Item -LiteralPath $dest).Length
    Write-Detail ('{0,-26} {1,10:N0} bytes   {2}' -f $item.Dest, $size, $item.What)
}

# The probe builds a ddraw.dll of its own. It must never be the one that
# ships, so check that the staged file is not that one.
$probeDll = Join-Path $BuildDir 'src\probe\Release\ddraw.dll'
if (Test-Path -LiteralPath $probeDll) {
    if ((Get-Sha256 $probeDll) -eq (Get-Sha256 (Join-Path $StageDir 'ddraw.dll'))) {
        Stop-WithMessage ("The staged ddraw.dll is the probe DLL, not the mod.`n" +
            "The mod's own file is built into:`n" +
            "  " + (Join-Path $BuildDir 'src\mod\Release\ddraw.dll'))
    }
}

# No bwvr.ini ever ships. The mod's built-in defaults are the tested setup,
# and a player's own file must stay untouched.
if (Test-Path -LiteralPath (Join-Path $StageDir 'bwvr.ini')) {
    Stop-WithMessage 'bwvr.ini is in the staging folder. It must never ship.'
}

# ---- 3. the third party binaries

Write-Step 'Third party binaries'
foreach ($rel in ($ThirdPartyHashes.Keys | Sort-Object)) {
    $path = Join-Path $StageDir $rel
    $have = Get-Sha256 $path
    $want = $ThirdPartyHashes[$rel].ToUpperInvariant()
    if ($have -ne $want) {
        Stop-WithMessage ("A third party binary is not the one this release was tested with:`n" +
            "  file:     " + $rel + "`n" +
            "  expected: " + $want + "`n" +
            "  found:    " + $have + "`n`n" +
            "If the new file is meant to ship, test it, then put its SHA-256 in`n" +
            "ThirdPartyHashes in this script.")
    }
    Write-Detail ('{0,-26} SHA-256 matches' -f $rel)
}

if ($StageOnly) {
    Write-Host ''
    Write-Host 'Done (the staging folder only, no installer)' -ForegroundColor Green
    Write-Host ('   ' + $StageDir)
    exit 0
}

# ---- 4. compile

Write-Step 'Installer'
$defines = @(
    ('AppVersion=' + $Version),
    ('Payload=' + $StageDir),
    ('OutDir=' + $OutDir)
)
$code = Invoke-Iscc -Iscc $Iscc -Iss $IssFile -Defines $defines
if ($code -ne 0) {
    Stop-WithMessage ('The Inno Setup compiler failed with code ' + $code + '.')
}

$made = @()
$made += (Join-Path $OutDir ('Black & White VR Mod ' + $Version + ' Setup.exe'))

if ($TestBuild) {
    Write-Step 'Installer, test build'
    $code = Invoke-Iscc -Iscc $Iscc -Iss $IssFile -Defines ($defines + @('TestBuild=1'))
    if ($code -ne 0) {
        Stop-WithMessage ('The Inno Setup compiler failed on the test build with code ' + $code + '.')
    }
    $made += (Join-Path $OutDir ('Black & White VR Mod ' + $Version + ' Setup (test).exe'))
}

Write-Host ''
Write-Host 'Done' -ForegroundColor Green
foreach ($path in $made) {
    if (-not (Test-Path -LiteralPath $path)) {
        Stop-WithMessage ("The compiler reported success but this file is not there:`n" + $path)
    }
    $item = Get-Item -LiteralPath $path
    Write-Host ('   ' + $item.FullName)
    Write-Host ('      {0:N1} MB   SHA-256 {1}' -f ($item.Length / 1MB), (Get-Sha256 $path))
}
if ($TestBuild) {
    Write-Host ''
    Write-Host '   The test build writes no uninstall registry key, and it takes' -ForegroundColor Yellow
    Write-Host '   /SKIPGAMECHECK=1 to install into a folder with no game in it.' -ForegroundColor Yellow
}
Write-Host ''
exit 0
