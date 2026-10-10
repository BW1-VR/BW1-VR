# Gives the game folder to the desktop test scripts. dev.ps1 setup writes it
# to local\game-path.txt at the root of the repo. The folder local is not
# tracked, so each contributor has his own.
function Get-BwvrGame {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
    $file = Join-Path $repo 'local\game-path.txt'
    if (-not (Test-Path -LiteralPath $file)) {
        throw 'No game folder is set. Run this first: .\dev.ps1 setup -Game "<your game folder>"'
    }
    $game = (Get-Content -LiteralPath $file -TotalCount 1).Trim()
    if (-not (Test-Path -LiteralPath (Join-Path $game 'runblack.exe'))) {
        throw "There is no runblack.exe in the game folder of local\game-path.txt: $game"
    }
    return $game
}

# Empty when the mod was started with its launcher in the game folder at
# least one time. Else the reason. The launcher writes bwvr.ini and gives the
# game its display size (a value of the game in the registry, which a test
# only reads). Before that the game renders 800 by 600 and has no settings
# file, and no recipe of the scripted controller fits.
function Test-BwvrFirstStart([string]$Game) {
    if (-not (Test-Path -LiteralPath (Join-Path $Game 'bwvr.ini'))) {
        return 'there is no bwvr.ini in the game folder'
    }
    $setup = Get-ItemProperty 'HKCU:\Software\Lionhead Studios Ltd\Black & White\BWSetup' -ErrorAction SilentlyContinue
    if (-not $setup -or -not $setup.ScreenW -or [int]$setup.ScreenW -lt 1024) {
        return 'the game has no display size from the launcher yet (it renders 800 by 600)'
    }
    return ''
}

# The folder that the desktop test scripts of each git worktree of this PC
# share. It holds the lock of a run (desk.lock) and the owner's "test now"
# flag (test-now-until.txt). It is in the temp folder of the user, so it is
# not in the repo and not in the game folder.
function Get-BwvrDeskShared {
    $dir = Join-Path ([System.IO.Path]::GetTempPath()) 'bwvr-desk'
    [void](New-Item -ItemType Directory -Force -Path $dir)
    return $dir
}

# True while the owner's "test now" holds: the time in test-now-until.txt is
# in the future. The file is in the shared folder. A file beside the scripts
# counts too, for the scripts of that worktree only.
function Test-BwvrTestNow {
    foreach ($dir in (Get-BwvrDeskShared), $PSScriptRoot) {
        $flag = Join-Path $dir 'test-now-until.txt'
        if (Test-Path -LiteralPath $flag) {
            try { if ((Get-Date) -lt [datetime]((Get-Content -LiteralPath $flag -Raw).Trim())) { return $true } } catch { }
        }
    }
    return $false
}

# True when a line of the output of a run script starts with the given
# verdict (NOT RUN, RUN DONE, GAVE UP). The test keeps the case and reads
# only the start of a line, so a line of a log that has the same words in it
# does not count.
function Test-BwvrVerdict($Output, [string]$Verdict) {
    return [bool](@($Output | ForEach-Object { "$_" -split "`r?`n" }) -cmatch ('^' + [regex]::Escape($Verdict)))
}

# The mod DLL of the build of this repo.
function Get-BwvrBuiltDll {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
    return (Join-Path $repo 'bwvr\build\src\mod\Release\ddraw.dll')
}
