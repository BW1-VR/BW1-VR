# Verifies a build of the mod with three desktop runs of the game. No headset
# is needed: a scripted controller plays, and the script reads the log.
#
#   verify_build.ps1 -Tag a          the build of this repo
#   verify_build.ps1 -Tag a -Dll <path to a ddraw.dll>
#
# Each run goes through desk_run.ps1: it waits until nobody has used the PC
# for 10 minutes and no game runs, puts the DLL in the game folder for the
# run, and puts the player's DLL, settings, profile and log back after it.
#
#   aim2<Tag>   a far fetch and a teleport with the aim preset, and the preset keys
#   tland<Tag>  a far fetch and a throw that lands
#   vc<Tag>     the Creature at the door of the Temple: the state, strokes, a slap
#
# The script ends with VERIFIED, or it names the run that did not show what
# it must. The logs and the pictures of a run are in the folder desk-<run>
# beside this script.
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$Dll
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
if (-not $Dll) { $Dll = Get-BwvrBuiltDll }
if (-not (Test-Path -LiteralPath $Dll)) { "no DLL at $Dll. Build first: .\dev.ps1 build"; exit 6 }

$aimSet = 'fake_controller_front=1;fake_controller_play_frame=2750;aim_test_touch=700;aim_test_keys=3260;aim_pitch_deg=-0.5;aim_yaw_deg=0.25'
$aimSteps = 'point@2990+1 aimfar@3000+1 shot@3040+1 grip@3060+90 pull@3100+3 pull@3170+3 point@3200+1 a@3215+30 shot@3230+1'
& (Join-Path $sp 'aim_run.ps1') -Name "aim2$Tag" -Dll $Dll -Set $aimSet -Steps $aimSteps -Seconds 116 -Minutes 150 | Select-Object -Last 3
& (Join-Path $sp 'throw_queue.ps1') -Dll $Dll -Runs land -Tag $Tag -Minutes 150 -More 'fake_controller_front=1' | Select-Object -Last 3
& (Join-Path $sp 'creature_recipe.ps1') -Name "vc$Tag" -Dll $Dll | Select-Object -Last 4

function Lines($run, $pattern, $first) {
    $log = Join-Path $sp "desk-$run\bwvr.log"
    "=== $run"
    if (-not (Test-Path $log)) { 'no log'; return }
    $l = @(Select-String -Path $log -Pattern $pattern | ForEach-Object { $_.Line.Substring(0, [Math]::Min(230, $_.Line.Length)) })
    $l | Select-Object -First $first
    $fps = Select-String -Path $log -Pattern ' fps ' | ForEach-Object { if ($_.Line -match '([0-9.]+) fps') { [double]$Matches[1] } } | Measure-Object -Minimum -Maximum
    "fps min $($fps.Minimum) max $($fps.Maximum)"
    $bad = @(Select-String -Path $log -Pattern 'REFUSING|could not hook|hook failed|failed to hook|MH_ERROR')
    if ($bad.Count -gt 0) { "HOOK PROBLEM: $($bad[0].Line)" }
}
$ok = $true
foreach ($check in @(
        @{ Run = "aim2$Tag"; Must = 'the pick up is over'; Show = 'the pick up is over|God view moved|numpad 5|fetches done' },
        @{ Run = "tland$Tag"; Must = 'the landing of'; Show = 'the pick up is over|the landing of|after the first contact|audio range  : the head' },
        @{ Run = "vc$Tag"; Must = 'creature'; Show = 'god spawn|spawn        :|hand press|creature     :|stroke|slap|leash' })) {
    Lines $check.Run $check.Show 9
    $log = Join-Path $sp "desk-$($check.Run)\bwvr.log"
    if (-not (Test-Path $log) -or -not (Select-String -Path $log -Pattern $check.Must -Quiet)) { $ok = $false; "MISSING in $($check.Run): $($check.Must)" }
}
if (-not $ok) { 'NOT VERIFIED: a run did not show what it must'; exit 2 }
'VERIFIED'
exit 0
