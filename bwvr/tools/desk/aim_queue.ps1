# Runs a list of desktop runs of one DLL, each through aim_run.ps1 (which
# waits for an idle PC and no game). Stops at the first run that gives up.
#
#   aim_queue.ps1 -Dll <dll> -Runs aim1,move1,cmd1 -Tag b
param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [string[]]$Runs = @('aim1', 'move1', 'cmd1'),
    [string]$Tag = '',
    [int]$Minutes = 170,
    [switch]$Now,
    [int]$WaitMinutes = 60
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
$base = 'shot@2900+1 point@2990+1 aimfar@3000+1 shot@3050+1 grip@3100+101 pull@3140+3 shot@3185+1 point@3200+1 shot@3215+1 point@3300+1 a@3320+50 shot@3350+1 aimfar@3400+1 grip@3460+101 pull@3500+3 shot@3545+1 point@3560+1'
$defs = @{
    # The trim from the ini, the far fetch with it, the teleport, the trim
    # keys from frame 3600, then an aim at a far object with the trim.
    aim1 = @{
        Set = 'aim_test_keys=3400;aim_pitch_deg=-0.5;aim_yaw_deg=0.25'
        Steps = 'shot@2900+1 point@2990+1 aimfar@3000+1 shot@3050+1 grip@3100+101 pull@3140+3 shot@3185+1 point@3200+1 point@3300+1 a@3320+50 shot@3350+1 point@3650+1 aimfar@3660+1 shot@3690+1'
        Points = ''
        Seconds = 132
    }
    # A fetch of a rock in flight, then a throw into the sea, a lock on the
    # rock in the sea, and a pull after 190 frames.
    move1 = @{
        Set = 'far_test_thrown=1'
        Steps = 'shot@2900+1 point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 pull@3180+3 point@3200+1 aimfar@3230+1 grip@3235+186 pull@3250+3 shot@3262+1 pull@3300+3 point@3380+1 point@3420+1 shot@3450+1 aimfar@3510+1 grip@3516+190 shot@3530+1 pull@3700+3 shot@3720+1'
        Points = '-16,2,100 -17,-13,100 -10,-10,60 -10,-4,66 -10,-10,60 -20,2,67'
        Seconds = 136
    }
    # A Creature command on an object near the Hand, with the leash counted
    # as on from frame 3250 and no packet sent.
    cmd1 = @{
        Set = 'far_test_thrown=1;creature_command_test=3250;far_test_least_cm=5'
        Steps = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 pull@3170+3 shot@3255+1 aimfar@3260+1 shot@3290+1 trigger@3300+6 seek@3360+1 shot@3400+1 trigger@3420+6'
        Points = ''
        Seconds = 120
    }
    # THE RUNS WITH A PLAY FRAME. The first story of a new game ends at a time
    # that depends on the render size, so the frame at which the player gets
    # the Hand changes. fake_controller_play_frame=2750 makes the steps from
    # 2750 on count from that frame.
    #
    # The fake hand counts as a Touch controller, so the preset acts.
    # The ini trim, a far fetch and a teleport with the Alyx preset, then the
    # keys: 5, 5, 5 (the three presets), 8, 4, 4, 2, 2, 6.
    aim1b = @{
        Set = 'fake_controller_play_frame=2750;aim_test_touch=1;aim_test_keys=3260;aim_pitch_deg=-0.5;aim_yaw_deg=0.25'
        Steps = 'point@2990+1 aimfar@3000+1 shot@3040+1 grip@3060+90 pull@3100+3 pull@3170+3 point@3200+1 a@3215+30 shot@3230+1'
        Points = ''
        Seconds = 116
    }
    # A fetch of a rock in its flight, then a throw of 5 m/s into the sea, a
    # lock on the rock in its flight, and a pull 190 frames later.
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
    # move2: a throw of 5.9 m/s toward the sea, then the lock and the pull
    # while the rock flies over the sea (88 to 106 frames after the release).
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
    if (($o -join "`n") -notmatch 'RUN DONE') { "QUEUE STOPPED at $name"; exit 2 }
}
"QUEUE DONE $(Get-Date -Format s)"
