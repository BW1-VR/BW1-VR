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

# The mod DLL of the build of this repo.
function Get-BwvrBuiltDll {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
    return (Join-Path $repo 'bwvr\build\src\mod\Release\ddraw.dll')
}
