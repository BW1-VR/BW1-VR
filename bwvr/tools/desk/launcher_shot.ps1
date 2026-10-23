# A picture of the window of the launcher, with no window on the screen.
#
#   launcher_shot.ps1 -Name a
#   launcher_shot.ps1 -Name b -Rows 12     (a column has 12 rows at most)
#   launcher_shot.ps1 -Name c -Exe "<game folder>\Black & White VR Mod.exe"
#   launcher_shot.ps1 -Name d -Game "C:\Black & White"
#   launcher_shot.ps1 -Name e -Game "C:\Black & White" -UpdateTag v9.9.9-test
#   launcher_shot.ps1 -Name f -UpdateUrl https://127.0.0.1:9/
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
# The launcher asks GitHub for the latest version only with -UpdateTag or
# -UpdateUrl, so that a picture does not change with the releases on GitHub
# (docs/launcher.md, "The update check"). -UpdateTag takes the place of the
# answer: the picture shows the line of a release with that tag. -UpdateUrl
# takes the place of the address of the ask, for example an address with no
# server for a PC with no network. The launcher waits for the answer before
# the picture.
#
# The launcher also writes the tooltip of the status line (each check and
# its result). The script prints it, with TIP before each line, and keeps it
# as <name>.txt beside the PNG.
#
# The last line of the output is SHOT <file>, or SHOT FAILED with the cause.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$Rows = 0,
    [string]$Exe,
    [string]$Game,
    [string]$UpdateTag,
    [string]$UpdateUrl
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
$tip = Join-Path $outDir "$Name.txt"
foreach ($old in $bmp, $png, $tip, "$bmp.tip.txt") {
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old }
}

# "-shot" takes the rest of the line as the file, so it is the last word.
$words = "-shot $bmp"
if ($Rows -gt 0) { $words = "-rows $Rows $words" }
if ($Game) { $words = "--game `"$Game`" $words" }
if ($UpdateTag) { $words = "-update-tag `"$UpdateTag`" $words" }
if ($UpdateUrl) { $words = "-update-url `"$UpdateUrl`" $words" }
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
if (Test-Path -LiteralPath "$bmp.tip.txt") {
    Move-Item -LiteralPath "$bmp.tip.txt" $tip
    Get-Content -LiteralPath $tip | ForEach-Object { "TIP $_" }
}
"SHOT $png ($size)"
