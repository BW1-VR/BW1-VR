<#
.SYNOPSIS
    The one script for work on the mod: build it, test it, and put it in
    your own game folder.

.DESCRIPTION
    One time:
      .\dev.ps1 setup -Game "D:\Games\Black & White"

    Then, after each change:
      .\dev.ps1              build, run the math tests, put the mod in the game
      .\dev.ps1 play         start the launcher of the mod

    Each step alone:
      .\dev.ps1 build        build and run the math tests
      .\dev.ps1 install      put the last build in the game folder
      .\dev.ps1 uninstall    take the mod out of the game folder
      .\dev.ps1 package      make the installer (needs Inno Setup 6)
      .\dev.ps1 status       say what is set up, and what is in the game folder

    The script never changes the registry and never touches a file of the
    game. It keeps the ddraw.dll that was in the game folder before, and
    "uninstall" puts it back.

.PARAMETER Game
    The folder that holds runblack.exe. It must be Black & White with the
    official patches 1.10 and 1.20 and the unofficial patch v1.42, and no
    other mod. The folder is kept in local\game-path.txt.

.PARAMETER Clean
    Builds each file again.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('all', 'setup', 'build', 'install', 'play', 'uninstall', 'package', 'status')]
    [string]$Command = 'all',
    [string]$Game,
    [switch]$Clean
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$Repo      = $PSScriptRoot
$Bwvr      = Join-Path $Repo 'bwvr'
$BuildDir  = Join-Path $Bwvr 'build'
$StageDir  = Join-Path $Bwvr 'out\stage'
$LocalDir  = Join-Path $Repo 'local'
$GameFile  = Join-Path $LocalDir 'game-path.txt'
$Package   = Join-Path $Bwvr 'tools\package.ps1'
$Product   = 'Black & White VR Mod'
$Launcher  = $Product + '.exe'
$Marker    = 'bwvr-dev-install.txt'
$Backup    = 'ddraw.dll.before-bwvr'

function Say([string]$Text) { Write-Host ('== ' + $Text) -ForegroundColor Cyan }
function Note([string]$Text) { Write-Host ('   ' + $Text) }
function Stop-Dev([string]$Text) {
    Write-Host ''
    foreach ($line in ($Text -split "`n")) { Write-Host ('   ' + $line) -ForegroundColor Red }
    Write-Host ''
    exit 1
}

# The SHA-1 of the one runblack.exe that the mod fits, from the source.
function Get-ExpectedSha1 {
    $header = Join-Path $Bwvr 'src\common\bw_addresses.h'
    $m = Select-String -LiteralPath $header -Pattern 'kExpectedSha1\[\]\s*=\s*"([0-9a-f]{40})"'
    if (-not $m) { Stop-Dev "Cannot read kExpectedSha1 from:`n$header" }
    return $m.Matches[0].Groups[1].Value
}

# Empty when the folder holds the right game. Else the reason.
function Test-GameFolder([string]$Dir) {
    $exe = Join-Path $Dir 'runblack.exe'
    if (-not (Test-Path -LiteralPath $exe)) { return "There is no runblack.exe in:`n$Dir" }
    $have = (Get-FileHash -Algorithm SHA1 -LiteralPath $exe).Hash.ToLowerInvariant()
    $want = Get-ExpectedSha1
    if ($have -ne $want) {
        return ("runblack.exe in this folder is not the file of the unofficial patch v1.42:`n$Dir`n" +
            "  its SHA-1 is  $have`n  the mod fits  $want`n" +
            "Install the game, then the official patches 1.10 and 1.20, then the`n" +
            "unofficial patch v1.42 (bwfiles.com), and no other mod.")
    }
    return ''
}

function Get-Game {
    if (-not (Test-Path -LiteralPath $GameFile)) {
        Stop-Dev ("No game folder is set. Run this first:`n" +
            '  .\dev.ps1 setup -Game "<the folder that holds runblack.exe>"')
    }
    $dir = (Get-Content -LiteralPath $GameFile -TotalCount 1).Trim()
    $why = Test-GameFolder $dir
    if ($why) { Stop-Dev $why }
    return $dir
}

function Assert-Tool([string]$Name, [string]$Help) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        Stop-Dev "$Name is not on the PATH. $Help"
    }
}

function Invoke-Configure {
    Assert-Tool 'cmake' 'Install CMake 3.20 or later, or use the "Developer PowerShell for VS 2022".'
    Assert-Tool 'git' 'Install Git for Windows.'
    Say 'Configure'
    & cmake -S $Bwvr -B $BuildDir -A Win32 | Select-Object -Last 3 | ForEach-Object { Note $_ }
    if ($LASTEXITCODE -ne 0) {
        Stop-Dev ("CMake could not configure the build.`n" +
            "The mod needs Visual Studio 2022 (or its Build Tools) with the`n" +
            "workload 'Desktop development with C++'. The build is 32 bit.")
    }
}

function Invoke-Build {
    Invoke-Configure
    Say 'Build'
    $buildArgs = @('--build', $BuildDir, '--config', 'Release')
    if ($Clean) { $buildArgs += '--clean-first' }
    $out = & cmake @buildArgs 2>&1
    $code = $LASTEXITCODE
    $out | Where-Object { "$_" -match ' error |warning C|FAILED|->' } | ForEach-Object { Note ("$_".Trim()) }
    if ($code -ne 0) { Stop-Dev 'The build failed.' }
    Say 'Math tests'
    $test = Join-Path $BuildDir 'test\controller_math\Release\bwvr_controller_math_test.exe'
    $result = & $test | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { Stop-Dev "The math tests failed: $result`nRun this for the list: $test" }
    Note $result
}

# True when a ddraw.dll is a build of the mod.
function Test-OurDll([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return ([System.Diagnostics.FileVersionInfo]::GetVersionInfo($Path).ProductName -eq $Product)
}

function Assert-GameNotRunning {
    if (Get-Process runblack -ErrorAction SilentlyContinue) {
        Stop-Dev 'The game runs. Close it first.'
    }
}

Add-Type -Namespace BwvrDev -Name Ini -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
'@

function Invoke-Install {
    $game = Get-Game
    Assert-GameNotRunning
    Say 'Stage the files that ship'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $Package -StageOnly -SkipBuild |
        Where-Object { "$_" -match 'bytes|matches|STOPPED|expected at|holds' } | ForEach-Object { Note ("$_".Trim()) }
    if ($LASTEXITCODE -ne 0) { Stop-Dev 'Staging failed. Run bwvr\tools\package.ps1 -StageOnly to see why.' }

    Say "Put the mod in $game"
    try {
        $live = Join-Path $game 'ddraw.dll'
        $kept = Join-Path $game $Backup
        $ours = (Test-OurDll $live) -or (Test-Path -LiteralPath (Join-Path $game $Marker))
        if ((Test-Path -LiteralPath $live) -and -not $ours -and -not (Test-Path -LiteralPath $kept)) {
            Copy-Item -LiteralPath $live -Destination $kept
            Note "kept the ddraw.dll that was there as $Backup"
        }
        # The same places as the installer (installer\bwvr.iss, [Files]).
        $names = @{ 'README.txt' = "$Product readme.txt"; 'Notices.txt' = "$Product notices.txt" }
        $done = @()
        foreach ($file in (Get-ChildItem -LiteralPath $StageDir -Recurse -File)) {
            $rel = $file.FullName.Substring($StageDir.Length).TrimStart('\')
            if ($names.ContainsKey($rel)) { $rel = $names[$rel] }
            $dest = Join-Path $game $rel
            [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest))
            Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
            $done += $rel
        }
        # dxwrapper must know where the real 32 bit ddraw.dll of Windows is.
        $system = Join-Path $env:WINDIR 'SysWOW64\ddraw.dll'
        if (-not (Test-Path -LiteralPath $system)) { $system = Join-Path $env:WINDIR 'System32\ddraw.dll' }
        foreach ($ini in @('dxwrapper\dxwrapper.ini', 'dxwrapper\ddraw.ini')) {
            [void][BwvrDev.Ini]::WritePrivateProfileString('General', 'RealDllPath', $system, (Join-Path $game $ini))
        }
        $id = (& git -C $Repo rev-parse --short=7 HEAD 2>$null)
        @("Put here by dev.ps1 of the Black & White VR Mod repo.", "build $id, $(Get-Date -Format s)", '') + $done |
            Set-Content -LiteralPath (Join-Path $game $Marker) -Encoding ascii
        Note "$($done.Count) files. Start the game with: .\dev.ps1 play"
    }
    catch [System.UnauthorizedAccessException] {
        Stop-Dev ("Windows did not permit a write to the game folder:`n$game`n" +
            "Run PowerShell as administrator, or keep the game in a folder that`n" +
            "your account can write to (not in Program Files).")
    }
}

function Invoke-Uninstall {
    $game = Get-Game
    Assert-GameNotRunning
    $markerPath = Join-Path $game $Marker
    if (-not (Test-Path -LiteralPath $markerPath)) {
        Stop-Dev ("dev.ps1 did not put the mod in this game folder (there is no $Marker).`n" +
            "If the installer put it there, remove it from the installed apps of Windows.")
    }
    Say "Take the mod out of $game"
    $files = @(Get-Content -LiteralPath $markerPath | Select-Object -Skip 3 | Where-Object { $_.Trim() -ne '' })
    foreach ($rel in $files) {
        $path = Join-Path $game $rel
        if (Test-Path -LiteralPath $path) { [System.IO.File]::Delete($path) }
    }
    $wrapper = Join-Path $game 'dxwrapper'
    if ((Test-Path -LiteralPath $wrapper) -and @(Get-ChildItem -LiteralPath $wrapper -Force).Count -eq 0) {
        [System.IO.Directory]::Delete($wrapper, $false)
    }
    $kept = Join-Path $game $Backup
    if (Test-Path -LiteralPath $kept) {
        Move-Item -LiteralPath $kept -Destination (Join-Path $game 'ddraw.dll') -Force
        Note 'put the ddraw.dll of before back'
    }
    [System.IO.File]::Delete($markerPath)
    Note "$($files.Count) files removed. bwvr.ini and the logs stay."
}

function Show-Status {
    Say 'Status'
    Note "repo:        $Repo"
    Note "build:       $(if (Test-Path -LiteralPath (Join-Path $BuildDir 'src\mod\Release\ddraw.dll')) { 'built' } else { 'not built yet' })"
    if (-not (Test-Path -LiteralPath $GameFile)) { Note 'game folder: not set (run .\dev.ps1 setup -Game "<folder>")'; return }
    $dir = (Get-Content -LiteralPath $GameFile -TotalCount 1).Trim()
    Note "game folder: $dir"
    $why = Test-GameFolder $dir
    Note "game:        $(if ($why) { 'NOT the v1.42 game' } else { 'the v1.42 game' })"
    $dll = Join-Path $dir 'ddraw.dll'
    if (Test-OurDll $dll) {
        $how = if (Test-Path -LiteralPath (Join-Path $dir $Marker)) { (Get-Content -LiteralPath (Join-Path $dir $Marker) -TotalCount 2)[1] } else { 'put there by the installer or by hand' }
        Note "mod in game: yes, $how"
    } else {
        Note 'mod in game: no'
    }
}

switch ($Command) {
    'setup' {
        if (-not $Game) { Stop-Dev 'Give the game folder: .\dev.ps1 setup -Game "<the folder that holds runblack.exe>"' }
        $dir = (Resolve-Path -LiteralPath $Game).Path
        $why = Test-GameFolder $dir
        if ($why) { Stop-Dev $why }
        [void](New-Item -ItemType Directory -Force -Path $LocalDir)
        Set-Content -LiteralPath $GameFile -Value $dir -Encoding ascii
        Say "The game folder is $dir"
        # The check that stops a commit with a file of the game.
        & git -C $Repo config core.hooksPath .githooks
        Invoke-Configure
        Note 'Ready. Run .\dev.ps1 to build the mod and put it in the game.'
    }
    'build'     { Invoke-Build }
    'install'   { Invoke-Install }
    'all'       { [void](Get-Game); Invoke-Build; Invoke-Install }
    'play'      {
        $game = Get-Game
        $exe = Join-Path $game $Launcher
        if (-not (Test-Path -LiteralPath $exe)) { Stop-Dev 'The mod is not in the game folder. Run .\dev.ps1 first.' }
        Start-Process -FilePath $exe -WorkingDirectory $game
    }
    'uninstall' { Invoke-Uninstall }
    'package'   {
        Invoke-Build
        & powershell -NoProfile -ExecutionPolicy Bypass -File $Package -SkipBuild
        exit $LASTEXITCODE
    }
    'status'    { Show-Status }
}
