# Runs a list of desktop runs of one DLL, each through aim_run.ps1 (which
# waits for an idle PC and no game). Stops at the first run that gives up.
#
#   aim_queue.ps1 -Dll <dll> -Runs aim2,move1b,cmd1b -Tag b
param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [string[]]$Runs = @('aim2', 'move1b', 'cmd1b'),
    [string]$Tag = '',
    [int]$Minutes = 170,
    [switch]$Now,
    [int]$WaitMinutes = 60
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
$defs = @{
    # THE PLAY FRAME. The first story of a new game ends at a time that
    # depends on the render size and on the frame rate, so the frame at which
    # the player gets the Hand changes. fake_controller_play_frame=2750 makes
    # the steps from 2750 on count from that frame. Each run has it.
    #
    # A fetch of a rock in its flight, then a throw of about 8.5 m/s into the
    # sea, a lock on the rock in its flight, and a pull 190 frames later,
    # when the rock is in the sea.
    move1b = @{
        Set = 'fake_controller_play_frame=2750;far_test_thrown=1'
        Steps = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 pull@3180+3 point@3200+1 aimfar@3230+1 grip@3233+188 pull@3250+3 shot@3262+1 pull@3300+3 point@3380+1 point@3420+1 shot@3450+1 aimfar@3510+1 grip@3513+193 shot@3530+1 pull@3700+3 shot@3720+1'
        Points = '-16,2,100 -17,-13,100 -10,-10,60 -10,-4,66 -10,-10,60 -25,8,70'
        Seconds = 120
    }
    # A Creature command on an object near the Hand and on the object at the
    # Hand, with the leash counted as on and no packet sent.
    cmd1b = @{
        Set = 'fake_controller_play_frame=2750;far_test_thrown=1;creature_command_test=1000;far_test_least_cm=5'
        Steps = 'point@2990+1 aimfar@3000+1 grip@3060+90 pull@3100+3 pull@3125+3 shot@3200+1 aimfar@3210+1 shot@3240+1 trigger@3250+6 seek@3310+1 shot@3350+1 trigger@3370+6'
        Points = ''
        Seconds = 112
    }
    # aim2: the fake hand is a Touch controller from frame 700, after the
    # start dialog. A far fetch and a teleport with the Alyx preset, then the
    # keys from frame 3260 of the script: 5, 5, 5, 8, 4, 4, 2, 2, 6.
    aim2 = @{
        Set = 'fake_controller_play_frame=2750;aim_test_touch=700;aim_test_keys=3260;aim_pitch_deg=-0.5;aim_yaw_deg=0.25'
        Steps = 'point@2990+1 aimfar@3000+1 shot@3040+1 grip@3060+90 pull@3100+3 pull@3170+3 point@3200+1 a@3215+30 shot@3230+1'
        Points = ''
        Seconds = 116
    }
    # ring1 and ring0: the teleport at three scales. A at 100 units a meter,
    # then two zoom steps in (44.4), then five zoom steps out (337). Each
    # aim logs the radius of the ring. ring1 has the size rule of the world
    # (the default), ring0 has the fixed size of the room. The zoom steps
    # keep the land under the head in its place (god_zoom_about=0).
    ring1 = @{
        Set = 'god_zoom_about=0;fake_controller_front=1;fake_controller_play_frame=2750;aim_test_touch=700;teleport_ring_mode=1'
        Steps = 'point@2990+1 aimfar@3000+1 grip@3060+90 pull@3100+3 pull@3170+3 point@3200+1 a@3215+30 shot@3230+1 stick_up@3260+10 stick_up@3290+10 point@3320+1 a@3340+30 shot@3355+1 stick_down@3390+10 stick_down@3420+10 stick_down@3450+10 stick_down@3480+10 stick_down@3510+10 point@3550+1 a@3570+30 shot@3585+1'
        Points = ''
        Seconds = 140
    }
    ring0 = @{
        Set = 'god_zoom_about=0;fake_controller_front=1;fake_controller_play_frame=2750;aim_test_touch=700;teleport_ring_mode=0'
        Steps = 'point@2990+1 aimfar@3000+1 grip@3060+90 pull@3100+3 pull@3170+3 point@3200+1 a@3215+30 shot@3230+1 stick_up@3260+10 stick_up@3290+10 point@3320+1 a@3340+30 shot@3355+1 stick_down@3390+10 stick_down@3420+10 stick_down@3450+10 stick_down@3480+10 stick_down@3510+10 point@3550+1 a@3570+30 shot@3585+1'
        Points = ''
        Seconds = 140
    }
    # move2: a throw of about 9.4 m/s toward the sea, then the lock and the
    # pull while the rock flies over the sea (88 to 106 frames after the
    # release).
    move2 = @{
        Set = 'fake_controller_play_frame=2750;far_test_thrown=1'
        Steps = 'point@2990+1 aimfar@3000+1 grip@3060+141 pull@3100+3 pull@3150+3 point@3200+1 aimfar@3292+1 grip@3295+100 pull@3307+3 shot@3320+1'
        Points = '-16,2,100 -17,-13,100 -10,-10,60 -25,8,70'
        Seconds = 112
    }
}
$Runs = @($Runs | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne '' })
foreach ($run in $Runs) {
    $d = $defs[$run]
    if ($null -eq $d) { "no run named $run"; continue }
    $name = "$run$Tag"
    "=== $name $(Get-Date -Format s)"
    if ($Now) {
        $o = & (Join-Path $sp 'aim_run.ps1') -Name $name -Dll $Dll -Set $d.Set -Steps $d.Steps -Points $d.Points -Seconds $d.Seconds -Now -WaitMinutes $WaitMinutes
    } else {
        $o = & (Join-Path $sp 'aim_run.ps1') -Name $name -Dll $Dll -Set $d.Set -Steps $d.Steps -Points $d.Points -Seconds $d.Seconds -Minutes $Minutes
    }
    $o
    if (-not (Test-BwvrVerdict $o 'RUN DONE')) { "QUEUE STOPPED at $name"; exit 2 }
}
"QUEUE DONE $(Get-Date -Format s)"
