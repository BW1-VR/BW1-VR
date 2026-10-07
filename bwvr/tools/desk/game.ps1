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

# The mod DLL of the build of this repo.
function Get-BwvrBuiltDll {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
    return (Join-Path $repo 'bwvr\build\src\mod\Release\ddraw.dll')
}
