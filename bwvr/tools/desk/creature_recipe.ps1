# A desktop run that reaches the Creature of the player. See
# bwvr/docs/creature.md, "A Creature in a desktop run".
#
#   creature_recipe.ps1 -Name x1 -Dll <dll>
#   creature_recipe.ps1 -Name x2 -Dll <dll> -Set "creature_stroke_hand=3"
#   creature_recipe.ps1 -Name x3 -Dll <dll> -Kind leash
#   creature_recipe.ps1 -Name x4 -Dll <dll> -Kind stay
#   creature_recipe.ps1 -Name x5 -Dll <dll> -Kind stay -Set "creature_hold_stay=0"
#   creature_recipe.ps1 -Name x6 -Dll <dll> -Kind tie
#   creature_recipe.ps1 -Name x7 -Dll <dll> -Kind click
#   creature_recipe.ps1 -Name x8 -Dll <dll> -Kind click -Set "text_click=0"
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
# -Kind tie:
#   At 100 units a meter: the grip from a distance (the leash goes on), the
#   aim on an object that a command can act on (the word "aimcmd"), and two
#   short presses of the trigger: the Creature goes to the object, and the
#   leash is tied to it. Then six times the word "seek", 300 frames apart:
#   each one gives a log line with the place of the Creature, so the log
#   shows its walk to the object.
# -Kind stay:
#   At 100 units a meter: the hand aims at the Creature from a distance and
#   holds the grip for 300 frames (the Creature stays at the door, and then
#   starts a walk). A second grip of 200 frames comes while the Creature
#   walks (it stops, stays and goes on). Then two short grips of 6 frames
#   (the leash goes on and comes off), and a grip of 40 frames (a stay that
#   ends soon after its start). With creature_hold_stay=0 each close of the
#   grip is a leash click, and no grip makes the Creature stay.
# -Kind click:
#   A text that waits for a click (bwvr/docs/controller-hints.md, section
#   10). At 100 units a meter: the grip from a distance puts the leash on.
#   At frame 960 the test key text_click_test makes the game say a text with
#   interaction (text_click_test_text, 1275 by default here). A press of the
#   trigger comes at frame 1100, with the leash on and the aim on the
#   Creature, and a second press at frame 1300. At frame 1500 the game says
#   the text again, the hand goes behind the head (off the view), and the
#   trigger is pressed at frame 1700.
#   With text_click=1 the card says "[Trigger] Continue", the first press
#   ends the first wait and is no command, the second press takes the leash
#   off, and the third press ends the second wait. With text_click=0 the
#   card does not have that line, the first press takes the leash off, the
#   second press ends the first wait, the third press is dropped, and the
#   second wait stays to the end of the run.
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
#   text click   : ...                              a wait for a click, from start to end
#   hint card    : ... the game gives its prompt    the prompt of that wait on the card
#   haptics      : ...                              the pulses
#
# -Save <name>: the run starts in a test save and not in a new game
#   (README.md, "A fast start"). The save must have the state of the start
#   above: the test save "temple" has it. The steps and the points are the
#   same. The steps count from frame 800 as the frame at which the controller
#   first places the Hand in the loaded game (fake_controller_play_frame=800),
#   so each frame in the log is about 720 less than in a run of a new game.
#   -Kind click has no -Save: its test key text_click_test has frames of its
#   own.
#
# spawn_run.ps1 waits for an idle PC (10 minutes, no game) and gives up after
# -Minutes. The last line of the output is RUN DONE (exit code 0) or GAVE UP
# (exit code 2). The lines of the log come only after a run.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Kind = 'state',
    [string]$Set = '',
    [int]$Seconds = 65,
    [int]$Minutes = 115,
    [string]$Save = ''
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
if ($Save -ne '' -and $Kind -eq 'click') { 'NOT RUN: -Kind click has no -Save, because its test key text_click_test has frames of its own'; exit 7 }
# The zoom steps of this recipe keep the land under the head in its place
# (god_zoom_about=0), so the Creature stays in front of the head.
$keys = 'god_zoom_about=0;seek_plan=creature;far_test_creature=1;far_test_influence=1;grab_hand=1;' +
        'far_grab_max_m=0;fake_controller_rots=-112,1,0 8,1,0'
# In a test save the steps count from the frame at which the controller first
# places the Hand, as frame 800.
if ($Save -ne '') { $keys = "fake_controller_play_frame=800;$keys" }
if ($Set -ne '') { $keys = "$keys;$Set" }
# The points of the word "point", after the two points of the start dialog.
$points = '0,-30,60'
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
} elseif ($Kind -eq 'click') {
    if ($Seconds -lt 80) { $Seconds = 80 }
    # The keys of -Set come after these in the list, so -Set can give a
    # different text or different frames.
    $keys = "text_click_test=960,1500;text_click_test_text=1275;$keys"
    $points = '0,-30,60 0,-40,-40'
    $steps = 'look@800+1300 point@860+1 aimfar@890+1 grip@910+6 shot@1040+1 trigger@1100+6 ' +
             'aimfar@1250+1 trigger@1300+6 point@1560+1 shot@1600+1 trigger@1700+6 shot@1800+1'
} elseif ($Kind -eq 'tie') {
    # The second press of the trigger comes 17 frames after the first one: a
    # double press. The walk takes about 1500 frames, so the run is 75 s
    # long or more.
    if ($Seconds -lt 75) { $Seconds = 75 }
    $steps = 'look@800+2600 point@860+1 aimfar@890+1 grip@910+6 aimcmd@1000+1 shot@1010+1 ' +
             'trigger@1020+6 trigger@1037+6 seek@1100+1 seek@1400+1 seek@1700+1 seek@2000+1 ' +
             'seek@2300+1 seek@2600+1 shot@2610+1'
} else {
    # The Creature state starts 30 to 230 frames after the press, when the
    # Creature has ended its body action. So the strokes start at 1260.
    $steps = 'stick_up@800+10 stick_up@830+10 look@860+2000 point@900+1 seek@930+1 grip@936+800 ' +
             'shot@1220+1 nudge@1260+1 nudge@1310+1 nudge@1360+1 nudge@1410+1 ' +
             'rot@1460+1 rot@1530+1 slap@1580+1 slap@1660+1 ' +
             'point@1800+1 aimfar@1830+1 shot@1840+1 grip@1850+300'
}
$o = & (Join-Path $sp 'spawn_run.ps1') -Name $Name -Dll $Dll -Choice keep -Steps $steps `
    -Points $points -Set $keys -Seconds $Seconds -Minutes $Minutes -Save $Save
# The RUN DONE of spawn_run.ps1 is not shown: this script gives it as its
# last line, after the lines of the log.
$o | Where-Object { "$_" -cnotmatch '^RUN DONE' }
if (-not (Test-BwvrVerdict $o 'RUN DONE')) { exit 2 }
$log = Join-Path $sp "desk-$Name\bwvr.log"
if (-not (Test-Path $log)) { "no log at $log"; exit 1 }
$pattern = '^hand creature: (the body of|a press|the Creature state|the hand of|a stroke|a slap|a hard slap|the press ends)|' +
           '^hand pick    : the target is|^hand press   : the grip pressed on|' +
           '^far grab    : (the grip closed|the grip opened|the hand cannot)|^creature stay: |' +
           '^creature cmd : (the leash|a short press|a double press|the trigger opens|test: aimcmd)|' +
           '^text click   : |^hint card    : (frame \d+, the game gives its prompt|the prompt of a text)|' +
           '^controllers  : the left button press started off view|' +
           '^hand pick    : seek plan: the Creature|' +
           '^haptics      : (\d+ pulses|leash|slap|touch|activate)'
Get-Content $log | Where-Object { $_ -match $pattern } | ForEach-Object {
    $_.Substring(0, [Math]::Min(300, $_.Length))
} | Select-Object -First 80
"RUN DONE"
exit 0
