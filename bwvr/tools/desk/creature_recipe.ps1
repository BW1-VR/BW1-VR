# A desktop run that reaches the Creature of the player, for the agent "hand"
# and for each agent that needs a Creature. See bwvr/docs/hand.md, "A
# Creature in a desktop run".
#
#   creature_recipe.ps1 -Name hkX -Dll <dll>
#   creature_recipe.ps1 -Name hkY -Dll <dll> -Set "creature_stroke_hand=3"
#   creature_recipe.ps1 -Name hkZ -Dll <dll> -Kind leash
#
# The start: the choice "Keep your old Creature" of the start dialog (through
# spawn_run.ps1 and desk_run.ps1). It gives a game with no story, and the
# Creature of the profile stands at the door of the Temple. From the build
# 8757f73 the God view goes to that door when the dialog closes (frame 571)
# and the game camera has come to rest (near frame 675). The steps start at
# frame 800. A zoom step at frame 700 came before the rest in run hkS, and
# then the view stayed at the shore: "the player moved the view, so the move
# of the game camera is dropped".
#
# -Kind state (the default):
#   Two zoom steps in (44 units a meter), so the Creature is in the view of
#   the desktop window. The head looks down. The Hand's point goes to the
#   body of the Creature, and the grip closes 6 frames later and stays closed
#   for 800 frames: the Creature state. Four strokes (steps of 2 cm), a turn
#   of the wrist out and back (the backhand: the grip is still, the Hand's
#   point moves) and a swing of the arm (a step of 12 cm out and back). Then
#   the hand aims at the Creature from a distance and holds the grip for 300
#   frames: the leash changes, and no Creature state starts.
# -Kind leash:
#   At 100 units a meter: the grip from a distance (the leash goes on), a
#   short press of the trigger with the aim on the Creature (the leash comes
#   off), the grip again (on), and a grip of 300 frames (off, and no state).
#
# What to read in bwvr.log of the run (the folder desk-<Name>):
#   hand creature: the body of the Creature ...     the body bone before a press
#   hand pick    : the target is ... (Creature)     the choice at the capsule
#   hand press   : the grip pressed on the target   the press
#   hand creature: the Creature state starts        the state
#   hand creature: a stroke / a slap / a hard slap  what reached the game
#   far grab    : the grip closed on your Creature the leash from a distance
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
$keys = 'seek_plan=creature;far_test_creature=1;far_test_influence=1;grab_hand=1;' +
        'far_grab_max_m=0;fake_controller_rots=-112,1,0 8,1,0'
if ($Set -ne '') { $keys = "$keys;$Set" }
if ($Kind -eq 'leash') {
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
           '^far grab    : the grip closed|^creature cmd : (the leash|a short press)|' +
           '^haptics      : (\d+ pulses|leash|slap|touch|activate)'
Get-Content $log | Where-Object { $_ -match $pattern } | ForEach-Object {
    $_.Substring(0, [Math]::Min(300, $_.Length))
} | Select-Object -First 60
