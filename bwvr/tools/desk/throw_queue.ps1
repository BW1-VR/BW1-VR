# Desktop runs of what a thrown object does after it lands. Each run goes through aim_run.ps1, which
# waits for an idle PC and no game, and then through desk_run.ps1.
#
#   throw_queue.ps1 -Dll <dll> -Runs base,land -Tag 1
param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [string[]]$Runs = @('base'),
    [string]$Tag = '',
    [int]$Minutes = 170,
    [string]$More = ''
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
$common = 'fake_controller_play_frame=2750;intro_skip_test_frame=5;throw_log=2'
# The first two points are the points of the start dialog. aim_run.ps1 puts
# them in for the current render size.
$points = '0,0,100 0,0,100 -10,-10,60 -10,-4,66 -10,-10,60 -10,-4,66'
# A far fetch of the rock at the pen and a throw of 2.6 m/s onto the slope.
# Then, after the rock is at rest, a second fetch and a second throw.
$two = 'point@2990+1 aimfar@3000+1 shot@3050+1 grip@3100+101 pull@3140+3 shot@3185+1 point@3200+1 point@4500+1 aimfar@4510+1 grip@4600+101 pull@4640+3 point@4700+1 shot@5900+1'
# Two far fetches and two releases with a hand that does not move (the height
# rule of the release, docs/throw.md, "The height of a release"). The first
# release is high over the land, the second one is near it. The third and the
# fourth point of the list (counting the dialog points) are the two places of
# the hand, in cm from the head: right, up, forward.
$still = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 point@4500+1 aimfar@4510+1 grip@4600+101 pull@4640+3 shot@5900+1'
$stillPoints = '0,0,100 0,0,100 -10,-10,60 -10,-40,60'
# A catch of the thrown rock while it moves. The first pull of the recipe steps
# the hand back, the second one forward. A far fetch, a release, then at once
# the aim at the thrown object (far_test_thrown=1), a new grip and a pull.
$flyFar = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 point@3200+1 pull@3205+3 aimfar@3225+1 grip@3228+60 pull@3238+3 shot@3290+1 shot@3500+1'
# The same in the roll after the first contact.
$rollFar = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 point@3200+1 pull@3205+3 aimfar@3300+1 grip@3310+60 pull@3330+3 shot@3380+1 shot@3500+1'
# The hand at the rock in the roll: a step onto it, then the grip.
$rollNear = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 point@3200+1 seek@3300+1 grip@3305+60 shot@3380+1 shot@3500+1'
# A drop: the hand is still, high over the land, and the grip opens. The rock falls
# and then may roll. The recipes close the grip on it while it falls or rolls.
$dropBase = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3'
$dropNear = "$dropBase seek@3217+1 grip@3221+60 shot@3300+1 shot@3500+1"
$dropRoll = "$dropBase seek@3290+1 grip@3296+60 shot@3360+1 shot@3500+1"
$dropFar = "$dropBase pull@3207+3 aimfar@3220+1 grip@3224+60 pull@3234+3 shot@3300+1 shot@3500+1"
$dropFarRoll = "$dropBase pull@3207+3 aimfar@3290+1 grip@3294+60 pull@3304+3 shot@3360+1 shot@3500+1"
$defs = @{
    treedrop = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_thrown=1;far_test_no_trees=0;far_test_tree=1'; Steps = $dropFar; Seconds = 80; Points = $stillPoints }
    treedroproll = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_thrown=1;far_test_no_trees=0;far_test_tree=1'; Steps = $dropFarRoll; Seconds = 80; Points = $stillPoints }
    treenear = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1;far_test_no_trees=0;far_test_tree=1'; Steps = $dropRoll; Seconds = 100; Points = $stillPoints }
    dropnear = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1'; Steps = $dropNear; Seconds = 80; Points = $stillPoints }
    droproll = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1'; Steps = $dropRoll; Seconds = 80; Points = $stillPoints }
    dropfar = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_thrown=1'; Steps = $dropFar; Seconds = 80; Points = $stillPoints }
    dropfarroll = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_thrown=1'; Steps = $dropFarRoll; Seconds = 80; Points = $stillPoints }
    flyfar = @{ Set = 'throw_land=1;far_test_thrown=1'; Steps = $flyFar; Seconds = 80 }
    rollfar = @{ Set = 'throw_land=1;far_test_thrown=1'; Steps = $rollFar; Seconds = 80 }
    rollnear = @{ Set = 'throw_land=1;seek_thrown=1'; Steps = $rollNear; Seconds = 80 }
    # The plain landing (throw_land=0).
    base = @{ Set = 'throw_land=0'; Steps = $two; Seconds = 112 }
    # The landing of the mod, with its defaults.
    land = @{ Set = 'throw_land=1'; Steps = $two; Seconds = 112 }
    # A throw right after a far fetch: the grip opens while the object still
    # comes to the hand.
    early = @{ Set = 'throw_land=1'; Steps = 'point@2990+1 aimfar@3000+1 grip@3100+60 pull@3140+3 point@3159+1 shot@3400+1'; Seconds = 96 }
    # A still hand over the limit of release_fall_cm, then a still hand near
    # the land. The rule is on.
    fall = @{ Set = 'throw_land=1;release_fall_cm=15'; Steps = $still; Seconds = 112; Points = $stillPoints }
    # The same recipe with release_fall_cm=0: a set down at each height.
    fallold = @{ Set = 'throw_land=1;release_fall_cm=0'; Steps = $still; Seconds = 112; Points = $stillPoints }
    # A tree: the far fetch takes trees too.
    tree = @{ Set = 'throw_land=1;far_test_no_trees=0'; Steps = $two; Seconds = 112 }
}
$Runs = @($Runs | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne '' })
foreach ($run in $Runs) {
    $d = $defs[$run]
    if ($null -eq $d) { "no run named $run"; continue }
    $name = "t$run$Tag"
    $set = "$common;$($d.Set)"
    if ($More -ne '') { $set = "$set;$More" }
    "=== $name $(Get-Date -Format s)"
    $o = & (Join-Path $sp 'aim_run.ps1') -Name $name -Dll $Dll -Set $set -Steps $d.Steps -Points $(if ($d.Points) { $d.Points } else { $points }) -Seconds $d.Seconds -Minutes $Minutes
    $o
    if (($o -join "`n") -notmatch 'RUN DONE') { "QUEUE STOPPED at $name"; exit 2 }
}
"QUEUE DONE $(Get-Date -Format s)"
