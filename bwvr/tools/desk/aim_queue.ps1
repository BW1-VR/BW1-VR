# Runs a list of desktop runs of one DLL, each through aim_run.ps1 (which
# waits for an idle PC and no game). Stops at the first run that gives up.
#
#   aim_queue.ps1 -Dll <dll> -Runs aim2,move1b,cmd1b -Tag b
#   aim_queue.ps1 -Dll <dll> -Runs aim2 -NewGame
#
# THE START. Each run starts in the test save "pen" (README.md, "A fast
# start"): the Creature pens of Land 1 after the first story, which is the
# state that each recipe starts in. When the save is not there, one run makes
# it. -NewGame gives each run a new game with the row "Skip straight to the
# Creature select" and the first story. -Save gives a different test save
# with that state.
param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [string[]]$Runs = @('aim2', 'move1b', 'cmd1b'),
    [string]$Tag = '',
    [int]$Minutes = 170,
    [switch]$Now,
    [int]$WaitMinutes = 60,
    [string]$Save = 'pen',
    [switch]$NewGame
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
$start = ''
if (-not $NewGame) { $start = Confirm-BwvrTestSave $Save $Dll $Minutes }
# Two keys are on from a frame, and not at a frame: the fake hand is a Touch
# controller (aim_test_touch), and the leash counts as on
# (creature_command_test). In a new game the frame is after the start dialog.
# In a test save it is after the Main Menu of the start.
$from = if ($start) { 110 } else { 700 }
$leash = if ($start) { 110 } else { 1000 }
$defs = @{
    # THE LENGTH OF A RUN. desk_test.ps1 skips the logos at the start of the
    # game, so the game is at its start dialog about 9 s after the start of
    # a run. In a new game the player gets the Hand about 63 s after the
    # start of a run, and Seconds is the length of such a run. In a test save
    # the controller places the Hand about 12 s after the start of a run, so
    # the run is 49 s shorter.
    #
    # THE PLAY FRAME. The first story of a new game ends at a time that
    # depends on the render size and on the frame rate, so the frame at which
    # the player gets the Hand changes. fake_controller_play_frame=2750 makes
    # the steps from 2750 on count from that frame. Each run has it. In a
    # test save that frame is the frame at which the controller first places
    # the Hand.
    #
    # A fetch of a rock in its flight, then a throw of about 8.5 m/s into the
    # sea, a lock on the rock in its flight, and a pull 190 frames later,
    # when the rock is in the sea.
    move1b = @{
        Set = 'fake_controller_play_frame=2750;far_test_thrown=1'
        Steps = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 pull@3180+3 point@3200+1 aimfar@3230+1 grip@3233+188 pull@3250+3 shot@3262+1 pull@3300+3 point@3380+1 point@3420+1 shot@3450+1 aimfar@3510+1 grip@3513+193 shot@3530+1 pull@3700+3 shot@3720+1'
        Points = '-16,2,100 -17,-13,100 -10,-10,60 -10,-4,66 -10,-10,60 -25,8,70'
        Seconds = 83
    }
    # A Creature command on an object near the Hand and on the object at the
    # Hand, with the leash counted as on and no packet sent.
    cmd1b = @{
        Set = "fake_controller_play_frame=2750;far_test_thrown=1;creature_command_test=$leash;far_test_least_cm=5"
        Steps = 'point@2990+1 aimfar@3000+1 grip@3060+90 pull@3100+3 pull@3125+3 shot@3200+1 aimfar@3210+1 shot@3240+1 trigger@3250+6 seek@3310+1 shot@3350+1 trigger@3370+6'
        Points = ''
        Seconds = 75
    }
    # aim2: the fake hand is a Touch controller from the frame of $from. A
    # far fetch and a teleport with the Alyx preset, then the keys from frame
    # 3260 of the script: 5, 5, 5, 8, 4, 4, 2, 2, 6.
    aim2 = @{
        Set = "fake_controller_play_frame=2750;aim_test_touch=$from;aim_test_keys=3260;aim_pitch_deg=-0.5;aim_yaw_deg=0.25"
        Steps = 'point@2990+1 aimfar@3000+1 shot@3040+1 grip@3060+90 pull@3100+3 pull@3170+3 point@3200+1 a@3215+30 shot@3230+1'
        Points = ''
        Seconds = 79
    }
    # ring1 and ring0: the teleport at three scales. A at 100 units a meter,
    # then two zoom steps in (44.4), then five zoom steps out (337). Each
    # aim logs the radius of the ring. ring1 has the size rule of the world
    # (the default), ring0 has the fixed size of the room. The zoom steps
    # keep the land under the head in its place (god_zoom_about=0).
    ring1 = @{
        Set = "god_zoom_about=0;fake_controller_front=1;fake_controller_play_frame=2750;aim_test_touch=$from;teleport_ring_mode=1"
        Steps = 'point@2990+1 aimfar@3000+1 grip@3060+90 pull@3100+3 pull@3170+3 point@3200+1 a@3215+30 shot@3230+1 stick_up@3260+10 stick_up@3290+10 point@3320+1 a@3340+30 shot@3355+1 stick_down@3390+10 stick_down@3420+10 stick_down@3450+10 stick_down@3480+10 stick_down@3510+10 point@3550+1 a@3570+30 shot@3585+1'
        Points = ''
        Seconds = 103
    }
    ring0 = @{
        Set = "god_zoom_about=0;fake_controller_front=1;fake_controller_play_frame=2750;aim_test_touch=$from;teleport_ring_mode=0"
        Steps = 'point@2990+1 aimfar@3000+1 grip@3060+90 pull@3100+3 pull@3170+3 point@3200+1 a@3215+30 shot@3230+1 stick_up@3260+10 stick_up@3290+10 point@3320+1 a@3340+30 shot@3355+1 stick_down@3390+10 stick_down@3420+10 stick_down@3450+10 stick_down@3480+10 stick_down@3510+10 point@3550+1 a@3570+30 shot@3585+1'
        Points = ''
        Seconds = 103
    }
    # other: the aim at a Creature that is not the Creature of the player
    # (far_test_aim_class=6). In the test save "pen" the Creatures of the pens
    # have no player. A grip that stays closed for 1 s and a short grip come
    # after the aim. The log names the Creature and says that it is no far
    # target, and no leash click and no stay start (docs/creature.md, "The
    # leash from a distance").
    other = @{
        Set = 'fake_controller_play_frame=2750;far_test_aim_class=6;far_test_least_cm=20'
        Steps = 'point@2990+1 aimfar@3000+1 shot@3040+1 grip@3060+60 grip@3160+8 shot@3200+1'
        Points = ''
        Seconds = 70
    }
    # move2: a throw of about 9.4 m/s toward the sea, then the lock and the
    # pull while the rock flies over the sea (88 to 106 frames after the
    # release).
    move2 = @{
        Set = 'fake_controller_play_frame=2750;far_test_thrown=1'
        Steps = 'point@2990+1 aimfar@3000+1 grip@3060+141 pull@3100+3 pull@3150+3 point@3200+1 aimfar@3292+1 grip@3295+100 pull@3307+3 shot@3320+1'
        Points = '-16,2,100 -17,-13,100 -10,-10,60 -25,8,70'
        Seconds = 75
    }
}
$Runs = @($Runs | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne '' })
foreach ($run in $Runs) {
    $d = $defs[$run]
    if ($null -eq $d) { "no run named $run"; continue }
    $name = "$run$Tag"
    # The start of a run in a test save is 49 s shorter than the start of a
    # new game with the first story.
    $seconds = $d.Seconds
    $more = @{}
    if ($start) {
        $seconds = $d.Seconds - 49
        $more.Save = $start
    }
    "=== $name $(Get-Date -Format s), $(if ($start) { "the test save '$start'" } else { 'a new game' }), $seconds s"
    if ($Now) {
        $o = & (Join-Path $sp 'aim_run.ps1') -Name $name -Dll $Dll -Set $d.Set -Steps $d.Steps -Points $d.Points -Seconds $seconds -Now -WaitMinutes $WaitMinutes @more
    } else {
        $o = & (Join-Path $sp 'aim_run.ps1') -Name $name -Dll $Dll -Set $d.Set -Steps $d.Steps -Points $d.Points -Seconds $seconds -Minutes $Minutes @more
    }
    $o
    if (-not (Test-BwvrVerdict $o 'RUN DONE')) { "QUEUE STOPPED at $name"; exit 2 }
}
"QUEUE DONE $(Get-Date -Format s)"
