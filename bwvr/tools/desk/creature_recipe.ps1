# A desktop run that reaches the Creature of the player. See
# bwvr/docs/creature.md, "A Creature in a desktop run".
#
#   creature_recipe.ps1 -Name x1 -Dll <dll>
#   creature_recipe.ps1 -Name x2 -Dll <dll> -Set "creature_stroke_hand=3"
#   creature_recipe.ps1 -Name x3 -Dll <dll> -Kind leash
#   creature_recipe.ps1 -Name x4 -Dll <dll> -Kind stay
#   creature_recipe.ps1 -Name x5 -Dll <dll> -Kind stay -Set "creature_hold_stay=0"
#
# The start: the choice "Keep your old Creature" of the start dialog (through
# spawn_run.ps1 and desk_run.ps1). It gives a game with no story, and the
# Creature of the profile stands at the door of the Temple. The God view goes
# to that door when the dialog closes (frame 571) and the game camera has come
# to rest (near frame 675). The steps start at frame 800. A zoom step before
# the rest keeps the view at the shore: "the player moved the view, so the
# move of the game camera is dropped".
#
# -Kind state (the default):
#   Two zoom steps in (44 units a meter), so the Creature is in the view of
#   the desktop window. The head looks down. The Hand's point goes to the
#   body of the Creature, and the grip closes 6 frames later and stays closed
#   for 800 frames: the Creature state. Four strokes (steps of 2 cm), a turn
#   of the wrist out and back (the backhand: the grip is still, the Hand's
#   point moves) and a swing of the arm (a step of 12 cm out and back). Then
#   the hand aims at the Creature from a distance and holds the grip for 300
#   frames: the Creature stays, and no Creature state starts.
# -Kind leash:
#   At 100 units a meter: the grip from a distance (the leash goes on), a
#   short press of the trigger with the aim on the Creature (the leash comes
#   off), the grip again (on), and a grip of 300 frames (the Creature stays,
#   the leash does not change, and no state starts).
# -Kind stay:
#   At 100 units a meter: the hand aims at the Creature from a distance and
#   holds the grip for 300 frames (the Creature stays at the door, and then
#   starts a walk). A second grip of 200 frames comes while the Creature
#   walks (it stops, stays and goes on). Then two short grips of 6 frames
#   (the leash goes on and comes off), and a grip of 40 frames (a stay that
#   ends soon after its start). With creature_hold_stay=0 each close of the
#   grip is a leash click, and no grip makes the Creature stay.
#
# What to read in bwvr.log of the run (the folder desk-<Name>):
#   hand creature: the body of the Creature ...     the body bone before a press
#   hand pick    : the target is ... (Creature)     the choice at the capsule
#   hand press   : the grip pressed on the target   the press
#   hand creature: the Creature state starts        the state
#   hand creature: a stroke / a slap / a hard slap  what reached the game
#   far grab    : the grip closed on your Creature the grip from a distance
#   far grab    : the grip opened after ...         a short grip: the leash click
#   creature stay: ...                              the stay, from start to end
#   creature cmd : the leash is on / off
#   haptics      : ...                              the pulses
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Kind = 'state',
    [string]$Set = '',
    [int]$Seconds = 65
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
# The zoom steps of this recipe keep the land under the head in its place
# (god_zoom_about=0), so the Creature stays in front of the head.
$keys = 'god_zoom_about=0;seek_plan=creature;far_test_creature=1;far_test_influence=1;grab_hand=1;' +
        'far_grab_max_m=0;fake_controller_rots=-112,1,0 8,1,0'
if ($Set -ne '') { $keys = "$keys;$Set" }
if ($Kind -eq 'stay') {
    # No picture in the hold: the frame of a picture can take 250 ms, and the
    # controller then lets go of its buttons.
    # The Creature starts its walk about 100 frames after the first grip
    # opens. The second grip closes in that walk.
    $steps = 'look@800+1700 point@860+1 aimfar@890+1 grip@910+300 ' +
             'aimfar@1335+1 grip@1345+200 ' +
             'aimfar@1700+1 grip@1720+6 aimfar@1820+1 grip@1840+6 ' +
             'aimfar@1950+1 grip@1970+40 shot@2200+1'
} elseif ($Kind -eq 'leash') {
    $steps = 'look@800+1200 point@860+1 aimfar@890+1 shot@900+1 grip@910+6 shot@950+1 ' +
             'trigger@980+6 aimfar@1060+1 grip@1080+6 aimfar@1160+1 grip@1180+300'
} else {
    # The Creature state starts 30 to 230 frames after the press, when the
    # Creature has ended its body action. So the strokes start at 1260.
    $steps = 'stick_up@800+10 stick_up@830+10 look@860+2000 point@900+1 seek@930+1 grip@936+800 ' +
             'shot@1220+1 nudge@1260+1 nudge@1310+1 nudge@1360+1 nudge@1410+1 ' +
             'rot@1460+1 rot@1530+1 slap@1580+1 slap@1660+1 ' +
             'point@1800+1 aimfar@1830+1 shot@1840+1 grip@1850+300'
}
& (Join-Path $sp 'spawn_run.ps1') -Name $Name -Dll $Dll -Choice keep -Steps $steps `
    -Points '0,-30,60' -Set $keys -Seconds $Seconds
$log = Join-Path $sp "desk-$Name\bwvr.log"
if (-not (Test-Path $log)) { "no log at $log"; exit 1 }
$pattern = '^hand creature: (the body of|a press|the Creature state|the hand of|a stroke|a slap|a hard slap|the press ends)|' +
           '^hand pick    : the target is|^hand press   : the grip pressed on|' +
           '^far grab    : (the grip closed|the grip opened|the hand cannot)|^creature stay: |' +
           '^creature cmd : (the leash|a short press)|' +
           '^haptics      : (\d+ pulses|leash|slap|touch|activate)'
Get-Content $log | Where-Object { $_ -match $pattern } | ForEach-Object {
    $_.Substring(0, [Math]::Min(300, $_.Length))
} | Select-Object -First 80
