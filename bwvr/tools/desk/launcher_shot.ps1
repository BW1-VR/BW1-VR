# A picture of the window of the launcher, with no window on the screen.
#
#   launcher_shot.ps1 -Name a
#   launcher_shot.ps1 -Name b -Rows 12     (a column has 12 rows at most)
#   launcher_shot.ps1 -Name c -Exe "<game folder>\Black & White VR Mod.exe"
#   launcher_shot.ps1 -Name d -Game "C:\Black & White"
#
# The launcher writes its window to a file and ends (the word "-shot" of its
# command line, docs/launcher.md, "A picture of the window for a test"). The
# script makes a PNG from that file in local\launcher-shot. It does not
# start the game, and the launcher writes no setting.
#
# By default the launcher is the one of the build folder. The picture shows
# the settings of the game folder that the launcher finds. When it finds
# none, the picture shows the default of each row, and the status line names
# a file that is missing. -Game names the game folder (the word "--game" of
# the launcher), so the status line shows the checks of that game.
#
# The last line of the output is SHOT <file>, or SHOT FAILED with the cause.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$Rows = 0,
    [string]$Exe,
    [string]$Game
)

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
if (-not $Exe) {
    $Exe = Join-Path $repo 'bwvr\build\src\launcher\Release\Black & White VR Mod.exe'
}
if (-not (Test-Path -LiteralPath $Exe)) {
    "SHOT FAILED: no launcher at $Exe. Build it first: .\dev.ps1 build"
    exit 1
}

$outDir = Join-Path $repo 'local\launcher-shot'
New-Item -ItemType Directory -Force $outDir | Out-Null
$bmp = Join-Path $outDir "$Name.bmp"
$png = Join-Path $outDir "$Name.png"
foreach ($old in $bmp, $png) {
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old }
}

# "-shot" takes the rest of the line as the file, so it is the last word.
$words = "-shot $bmp"
if ($Rows -gt 0) { $words = "-rows $Rows $words" }
if ($Game) { $words = "--game `"$Game`" $words" }
$run = Start-Process -FilePath $Exe -ArgumentList $words `
    -WorkingDirectory (Split-Path $Exe) -PassThru
if (-not $run.WaitForExit(30000)) {
    $run.Kill()
    'SHOT FAILED: the launcher did not end in 30 s'
    exit 1
}
if ($run.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $bmp)) {
    "SHOT FAILED: the launcher wrote no picture (exit code $($run.ExitCode))"
    exit 1
}

Add-Type -AssemblyName System.Drawing
$image = [Drawing.Image]::FromFile($bmp)
$size = "$($image.Width) x $($image.Height)"
$image.Save($png, [Drawing.Imaging.ImageFormat]::Png)
$image.Dispose()
Remove-Item -LiteralPath $bmp
"SHOT $png ($size)"
