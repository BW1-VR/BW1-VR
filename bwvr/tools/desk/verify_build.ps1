# Verifies a build of the mod with three desktop runs of the game. No headset
# is needed: a scripted controller plays, and the script reads the log.
#
#   verify_build.ps1 -Tag a          the build of this repo
#   verify_build.ps1 -Tag a -Dll <path to a ddraw.dll>
#   verify_build.ps1 -Tag a -NewGame each run starts in a new game
#
# Each run goes through desk_run.ps1: it waits until nobody has used the PC
# for 10 minutes and no game runs, puts the DLL in the game folder for the
# run, and puts the player's DLL, settings, profile and log back after it.
#
#   aim2<Tag>   a far fetch and a teleport with the aim preset, and the preset keys
#   tland<Tag>  a far fetch and a throw that lands
#   vc<Tag>     the Creature at the door of the Temple: the state, strokes, a slap
#
# THE START OF EACH RUN (README.md, "A fast start").
#   aim2   The test save "pen": the game is at the Creature pens of Land 1,
#          after the first story. The recipe is the recipe of a new game.
#   tland  A new game with the row "Skip straight to the Creature select" and
#          the first story. One run keeps the story, because the story is a
#          part of what the mod does: the cut scene window, the story camera,
#          the end of the story and the God view that is built after it. A
#          test save starts after all of that.
#   vc     The test save "temple": the Creature of the profile at the door of
#          the Temple. The recipe is the recipe of a new game with the row
#          "Keep your old Creature".
# When a test save is not there, one run of make_test_save.ps1 makes it. When
# that does not work, the run starts in a new game, and the script says so.
# -NewGame gives each run the start of a new game.
#
# The script ends with VERIFIED, or it names the run that did not show what
# it must. The logs and the pictures of a run are in the folder desk-<run>
# beside this script.
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Dll,
    [switch]$NewGame
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
if (-not $Dll) { $Dll = Get-BwvrBuiltDll }
if (-not (Test-Path -LiteralPath $Dll)) { "no DLL at $Dll. Build first: .\dev.ps1 build"; exit 6 }
$why = Test-BwvrFirstStart (Get-BwvrGame)
if ($why) {
    "NOT RUN: $why. Open 'Black & White VR Mod.exe' in the game folder one time, select 'Play in VR', then close the game."
    exit 8
}

# The test saves of the runs. A save that is not there is made one time.
$save = @{ pen = ''; temple = '' }
if (-not $NewGame) {
    foreach ($name in 'pen', 'temple') {
        if (-not (Get-BwvrTestSave $name)) {
            "the test save '$name' is not there: one run makes it"
            & (Join-Path $sp 'make_test_save.ps1') -Name $name -Dll $Dll -Minutes 150 | Select-Object -Last 1
        }
        if (Get-BwvrTestSave $name) { $save[$name] = $name }
        else { "the test save '$name' was not made, so its run starts in a new game" }
    }
}

# aim2. The fake hand is a Touch controller from the frame of aim_test_touch.
# In a new game that frame is after the start dialog. In a test save it is
# after the Main Menu of the start.
$aimSet = 'fake_controller_front=1;fake_controller_play_frame=2750;aim_test_keys=3260;aim_pitch_deg=-0.5;aim_yaw_deg=0.25'
$aimSteps = 'point@2990+1 aimfar@3000+1 shot@3040+1 grip@3060+90 pull@3100+3 pull@3170+3 point@3200+1 a@3215+30 shot@3230+1'
if ($save.pen) {
    & (Join-Path $sp 'aim_run.ps1') -Name "aim2$Tag" -Dll $Dll -Set "$aimSet;aim_test_touch=110" -Steps $aimSteps -Seconds 30 -Minutes 150 -Save $save.pen | Select-Object -Last 3
} else {
    & (Join-Path $sp 'aim_run.ps1') -Name "aim2$Tag" -Dll $Dll -Set "$aimSet;aim_test_touch=700" -Steps $aimSteps -Seconds 79 -Minutes 150 | Select-Object -Last 3
}
# tland keeps the first story, so it starts in a new game (-NewGame).
& (Join-Path $sp 'throw_queue.ps1') -Dll $Dll -Runs land -Tag $Tag -Minutes 150 -More 'fake_controller_front=1' -NewGame | Select-Object -Last 3
if ($save.temple) {
    & (Join-Path $sp 'creature_recipe.ps1') -Name "vc$Tag" -Dll $Dll -Minutes 150 -Seconds 42 -Save $save.temple | Select-Object -Last 4
} else {
    & (Join-Path $sp 'creature_recipe.ps1') -Name "vc$Tag" -Dll $Dll -Minutes 150 -NewGame | Select-Object -Last 4
}

function Lines($run, $pattern, $first) {
    $log = Join-Path $sp "desk-$run\bwvr.log"
    "=== $run"
    if (-not (Test-Path $log)) { 'no log'; return }
    $l = @(Select-String -Path $log -Pattern $pattern | ForEach-Object { $_.Line.Substring(0, [Math]::Min(230, $_.Line.Length)) })
    $l | Select-Object -First $first
    $fps = Select-String -Path $log -Pattern ' fps ' | ForEach-Object { if ($_.Line -match '([0-9.]+) fps') { [double]$Matches[1] } } | Measure-Object -Minimum -Maximum
    "fps min $($fps.Minimum) max $($fps.Maximum)"
    # What desk_run.ps1 found about the real cursor. It does not decide the
    # result.
    $deskState = Join-Path $sp "desk-$run\desk-state.txt"
    if (Test-Path -LiteralPath $deskState) { Get-Content -LiteralPath $deskState | Select-Object -Skip 2 -First 1 }
    $bad = @(Select-String -Path $log -Pattern 'REFUSING|could not hook|hook failed|failed to hook|MH_ERROR')
    if ($bad.Count -gt 0) { "HOOK PROBLEM: $($bad[0].Line)" }
}
$ok = $true
# A run in a test save must also have two lines of the log: the game loaded
# the saved game (and not a new game), and the script came to its steps after
# the Main Menu of the start closed. For vc it must have the Creature state too:
# the pattern "creature" is in the log of each run.
$loaded = '^game start   : the saved game is loaded'
$played = '^controllers  : the controller places the Hand after frame \d+ at frame \d+'
foreach ($check in @(
        @{ Run = "aim2$Tag"; Must = 'the pick up is over'; Show = 'the pick up is over|God view moved|numpad 5|fetches done'; Save = $save.pen },
        @{ Run = "tland$Tag"; Must = 'the landing of'; Show = 'the pick up is over|the landing of|after the first contact|audio range  : the head'; Save = '' },
        @{ Run = "vc$Tag"; Must = 'creature'; Show = 'god spawn|spawn        :|hand press|creature     :|stroke|slap|leash'; Save = $save.temple; MustSave = '^hand creature: the Creature state starts' })) {
    Lines $check.Run $check.Show 9
    $log = Join-Path $sp "desk-$($check.Run)\bwvr.log"
    if (-not (Test-Path $log) -or -not (Select-String -Path $log -Pattern $check.Must -Quiet)) { $ok = $false; "MISSING in $($check.Run): $($check.Must)" }
    if ($check.Save -and (Test-Path $log)) {
        $start = Select-String -Path $log -Pattern $loaded | Select-Object -First 1
        $play = Select-String -Path $log -Pattern $played | Select-Object -First 1
        if ($start) { "the start: the test save '$($check.Save)'. $($start.Line.Substring(15, [Math]::Min(120, $start.Line.Length - 15)))" }
        else { $ok = $false; "MISSING in $($check.Run): the game did not load the test save '$($check.Save)'" }
        if ($play) { $play.Matches[0].Value.Substring(15) }
        else { $ok = $false; "MISSING in $($check.Run): the script did not come to its steps in the test save '$($check.Save)'. Did the Main Menu of the start stay up? See the line 'menu_skip_test_frame' of the log" }
        if ($check.MustSave -and -not (Select-String -Path $log -Pattern $check.MustSave -Quiet)) { $ok = $false; "MISSING in $($check.Run): $($check.MustSave)" }
    }
}
if (-not $ok) { 'NOT VERIFIED: a run did not show what it must'; exit 2 }
'VERIFIED'
exit 0
