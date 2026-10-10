# Makes a test save: a saved game that the game itself writes at the end of a
# test run. A run with -Save <name> then starts in that game (README.md, "A
# fast start").
#
#   make_test_save.ps1 -Name pen                    the build of this repo
#   make_test_save.ps1 -Name temple -Dll <path to a ddraw.dll>
#   make_test_save.ps1 -Name mine -Choice keep -Frame 1500 -Steps "<script words>"
#
# The two saves that the run scripts use have a recipe here:
#   pen     The row "Skip straight to the Creature select" of the start
#           dialog. The game plays the first story. The save is 150 frames
#           after the player gets the Hand, at the Creature pens of Land 1.
#   temple  The row "Keep your old Creature". No story. The Creature of the
#           profile stands at the door of the Temple of Land 1. The save is
#           at frame 800, after the game camera came to rest at that door.
# A different name needs -Choice (a row of the start dialog: normal, select,
# skip, keep) and -Frame. With a row that has a story, the frame counts from
# frame 2750 as the frame at which the player gets the Hand
# (fake_controller_play_frame). -Steps gives script words that come before
# the save.
#
# HOW. The run is a new game through spawn_run.ps1 and desk_run.ps1, with each
# guard of a run. At the frame, the word "exit" of the scripted controller
# asks the game to end by itself. The game then writes its saved game of slot
# 20, as it does each time a player ends the game, and the mod drops each
# write of the game to the registry. desk_run.ps1 copies the files of slot 20
# that the game wrote in the run to local\test-saves\<name>\20 of the main
# checkout, and then puts the profile back. This script writes save.txt
# beside them.
#
# A test save is a file that the game made. It is not in the repo, and it
# must not go there.
#
# The last line of the output is SAVE MADE or SAVE NOT MADE.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Dll,
    [string]$Choice = '',
    [int]$Frame = 0,
    [string]$Steps = '',
    [string]$Points = '',
    [string]$Set = '',
    [int]$Seconds = 0,
    [int]$Minutes = 115
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
. (Join-Path $sp 'dialog.ps1')
if (-not $Dll) { $Dll = Get-BwvrBuiltDll }
if (-not (Test-Path -LiteralPath $Dll)) { "SAVE NOT MADE: no DLL at $Dll. Build first: .\dev.ps1 build"; exit 6 }
if ($Name -notmatch '^[A-Za-z0-9_-]+$') { "SAVE NOT MADE: the name '$Name' must have only letters, digits, '-' and '_'"; exit 7 }

# The play frame of a start with a story: see controllers.md, "The script
# after the first story".
$playFrame = 2750
$recipes = @{
    pen    = @{ Choice = 'select'; Frame = $playFrame + 150
                State = 'Land 1. The first story is over (the doors, the walk to the Creature pens). The player has the Hand at the Creature pens. No Creature is chosen.' }
    temple = @{ Choice = 'keep'; Frame = 800
                State = 'Land 1. No story. The Creature of the profile stands at the door of the Temple, and the game camera is at rest at that door. The village is there.' }
}
$state = 'A state of this PC. See the start and the frame.'
if ($recipes.ContainsKey($Name)) {
    if ($Choice -eq '') { $Choice = $recipes[$Name].Choice }
    if ($Frame -eq 0) { $Frame = $recipes[$Name].Frame }
    if ($Choice -eq $recipes[$Name].Choice -and $Frame -eq $recipes[$Name].Frame -and $Steps -eq '') { $state = $recipes[$Name].State }
}
if ($Choice -eq '' -or $Frame -le 0) { "SAVE NOT MADE: the name '$Name' has no recipe, so give -Choice and -Frame"; exit 7 }
if (-not $script:DialogRowY.ContainsKey($Choice)) { "SAVE NOT MADE: no choice named $Choice"; exit 7 }
$rowText = @{
    normal = 'Start the game normally'; select = 'Skip straight to the Creature select'
    skip = 'Skip all of the Creature tutorial'; keep = 'Keep your old Creature'
}
$story = $Choice -ne 'keep'
$keys = $Set
if ($story) { $keys = ("fake_controller_play_frame=$playFrame;$Set").TrimEnd(';') }
if ($Seconds -le 0) {
    # The story of a start takes about 60 s. A frame is about 1/60 s.
    $after = if ($story) { $Frame - $playFrame } else { $Frame }
    $Seconds = $(if ($story) { 95 } else { 30 }) + [int]($after / 50)
}
$run = "save-$Name"
$started = Get-Date
$o = & (Join-Path $sp 'spawn_run.ps1') -Name $run -Dll $Dll -Choice $Choice -Steps ("$Steps exit@$Frame+1").Trim() `
    -Points $Points -Set $keys -Seconds $Seconds -Minutes $Minutes -KeepSave $Name
$o | Where-Object { "$_" -cnotmatch '^(RUN DONE|desk watch)' }
if (-not (Test-BwvrVerdict $o 'RUN DONE')) { 'SAVE NOT MADE: no run started'; exit 2 }
if (-not (Test-BwvrVerdict $o 'test save kept')) { 'SAVE NOT MADE: the run did not keep the files of slot 20 (see the lines above)'; exit 1 }
$dir = Get-BwvrTestSave $Name
if (-not $dir) { 'SAVE NOT MADE: the files of the save are not in the folder of the test saves'; exit 1 }

# What the log of the run says about the save.
$log = Join-Path $sp "desk-$run\bwvr.log"
$build = ''; $asked = ''; $start = ''; $wrote = 0
if (Test-Path -LiteralPath $log) {
    $build = "$(Get-Content -LiteralPath $log -TotalCount 1)"
    $asked = "$(Select-String -LiteralPath $log -Pattern '^exit test    : frame (\d+), the mod asks the game to close' | Select-Object -First 1 | ForEach-Object { $_.Matches[0].Groups[1].Value })"
    $start = "$(Select-String -LiteralPath $log -Pattern '^game start   : ' | Select-Object -First 1 | ForEach-Object { $_.Line })"
    $wrote = @(Select-String -LiteralPath $log -Pattern '^exit test    : the game opens a file for a write: .*\\20\\20\.sav').Count
    Select-String -LiteralPath $log -Pattern '^(game start|exit test)   ' | Select-Object -First 14 |
        ForEach-Object { $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
}
if ($asked -eq '' -or $wrote -eq 0) {
    "SAVE NOT MADE: the log does not show that the game wrote 20.sav after the word 'exit' (the frame: '$asked', the writes of 20.sav: $wrote). The files in $dir are from a different moment of the run"
    exit 1
}
$land = if ($start -match 'land (\d+)') { $Matches[1] } else { '?' }
$size = Get-BwvrRenderSize
$files = @(Get-ChildItem -LiteralPath (Join-Path $dir '20') -Recurse -File)
$text = @(
    "The test save '$Name' of the desktop tests of the Black & White VR Mod."
    'The game wrote these files in a test run. They are not in the repo, and they must not go there.'
    ''
    "Made: $($started.ToString('s')) by make_test_save.ps1, the run $run"
    "The start: -NEWGAME, the row `"$($rowText[$Choice])`" of the start dialog ($Choice)"
    "The land of the game: $land"
    "The state: $state"
    "The frame of the save: $asked$(if ($story) { " (frame $Frame of the script, with frame $playFrame as the frame at which the player got the Hand)" })"
    "The steps before the save: $(if ($Steps -eq '') { 'none' } else { $Steps })"
    "The render size: $(if ($size) { "$($size[0]) by $($size[1])" } else { '?' })"
    "The DLL: $build"
    "The files: the folder 20, $($files.Count) files, $([int](($files | Measure-Object Length -Sum).Sum / 1024)) KB (the files of Saved Games\20 of the profile that the game wrote in the run)"
)
Set-Content -LiteralPath (Join-Path $dir 'save.txt') -Value $text -Encoding ascii
$text | Select-Object -Skip 3
"SAVE MADE: $Name, in $dir"
exit 0
