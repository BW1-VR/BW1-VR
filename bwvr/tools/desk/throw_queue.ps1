# Desktop runs of what a thrown object does after it lands. Each run goes through aim_run.ps1, which
# waits for an idle PC and no game, and then through desk_run.ps1.
#
#   throw_queue.ps1 -Dll <dll> -Runs base,land -Tag 1
#   throw_queue.ps1 -Dll <dll> -Runs land -NewGame
#   throw_queue.ps1 -Dll <dll> -Runs dropnear,tossup,landgrip,dropshort -Fps 90
#
# -Fps N: the run has N frames a second, as a headset session has (the test
# key test_frame_rate of the mod: no copy to the monitor, and each frame
# lasts 1/N s at the least). The frames of a recipe fit 60 frames a second,
# so each step from the play frame on gets N/60 times as many frames, and
# keeps its time. After each run the script prints the frame rate of the run
# and the lines of the grip from its log.
#
# THE START. Each run starts in the test save "pen" (README.md, "A fast
# start"): the Creature pens of Land 1 after the first story. Each recipe
# needs only the Hand, the rock at the pen (or a tree) and the land, and the
# save has them. No recipe tests the story. When the save is not there, one
# run makes it. -NewGame gives each run a new game with the row "Skip straight
# to the Creature select" and the first story: verify_build.ps1 uses it for
# its run "tland", which is the one run that keeps the story. -Save gives a
# different test save with that state.
#
# Seconds is the length of a run in a new game, in which the player gets the
# Hand about 63 s after the start of the run. In a test save the controller
# places the Hand about 12 s after the start, so the run is 49 s shorter.
param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [string[]]$Runs = @('base'),
    [string]$Tag = '',
    [int]$Minutes = 170,
    [string]$More = '',
    [string]$Save = 'pen',
    [switch]$NewGame,
    [int]$Fps = 0
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
$start = ''
if (-not $NewGame) { $start = Confirm-BwvrTestSave $Save $Dll $Minutes }
$play = 2750
$common = "fake_controller_play_frame=$play;intro_skip_test_frame=5;throw_log=2"
if ($Fps -gt 0) { $common = "$common;test_frame_rate=$Fps" }

# The steps of a recipe at -Fps frames a second: a step from the play frame
# on starts N/60 times as many frames after the play frame, and lasts N/60
# times as many frames. A step of one frame stays one frame.
function Convert-BwvrSteps([string]$Steps) {
    if ($Fps -le 0 -or $Fps -eq 60) { return $Steps }
    $k = $Fps / 60.0
    return (@($Steps.Split(' ') | Where-Object { $_ -ne '' } | ForEach-Object {
        if ($_ -match '^([a-z_0-9]+)@(\d+)\+(\d+)$') {
            $at = [int]$Matches[2]; $frames = [int]$Matches[3]
            if ($at -ge $play) { $at = $play + [int][Math]::Round(($at - $play) * $k) }
            if ($frames -gt 1) { $frames = [int][Math]::Round($frames * $k) }
            "$($Matches[1])@$at+$frames"
        } else { $_ }
    }) -join ' ')
}

# The frame rate of a run and the lines of the grip, from the log of the run.
function Show-BwvrGripLines([string]$Name) {
    $log = Join-Path $sp "desk-$Name\bwvr.log"
    if (-not (Test-Path -LiteralPath $log)) { "no log at $log"; return }
    $rates = @(Select-String -LiteralPath $log -Pattern '^stereo       : ([0-9.]+) fps' | ForEach-Object { [double]::Parse($_.Matches[0].Groups[1].Value, [cultureinfo]::InvariantCulture) })
    if ($rates.Count -gt 0) {
        $sorted = @($rates | Sort-Object)
        "frame rate of ${Name}: median $($sorted[[int]($sorted.Count / 2)]) frames a second, $($sorted[0]) to $($sorted[-1]) over $($rates.Count) windows"
    }
    $taps = @(Select-String -LiteralPath $log -Pattern '^rock split   : .*the game taps it').Count
    $splits = @(Select-String -LiteralPath $log -Pattern '^rock split   : ').Count
    "rocks that split in ${Name}: $taps by a tap of the game (a click of the Action button), $($splits - $taps) by a different cause"
    Select-String -LiteralPath $log -Pattern '^grip         : grip_press_hold=|^hand pick    : catch_moving=|^hand pick    : seek |^hand press   : the grip pressed|^grip         : (the pick up is over|the object left the Hand|the grip opened on an object|the press that the mod held|the button of the game goes up|the object came after|an object is in the Hand with)|^rock split   : |^hand throw   : (release at|first contact of|the flight of .* is over)|^far grab    : the Hand holds|^hand catch   : (the|catch_at_once)|^controllers  : the right button press started off' |
        ForEach-Object { $t = $_.Line; if ($t.Length -gt 330) { $t = $t.Substring(0, 330) + ' ...' }; "  $t" }
}
# The first two points are the points of the start dialog. aim_run.ps1 puts
# them in for the current render size. In a test save it puts the point of
# "Continue Game" in their place.
$points = '0,0,100 0,0,100 -10,-10,60 -10,-4,66 -10,-10,60 -10,-4,66'
# A far fetch of the rock at the pen and a throw of about 3 m/s onto the slope.
# Then, after the rock is at rest, a second fetch and a second throw. The
# word "pull" steps the hand 15 cm toward the head, and the next "pull" steps
# it back. So the pull at 3260, with an empty hand, makes the pull of the
# second fetch a step toward the head again.
$two = 'point@2990+1 aimfar@3000+1 shot@3050+1 grip@3100+101 pull@3140+3 shot@3185+1 point@3200+1 pull@3260+3 point@4500+1 aimfar@4510+1 grip@4600+101 pull@4640+3 point@4700+1 shot@5900+1'
# Two far fetches and two releases with a hand that does not move (the height
# rule of the release, docs/throw.md, "The height of a release"). The first
# release is high over the land: the object falls. The second place of the
# hand is at the land, beside a tree. There the near hand wins over the far
# fetch: the grip takes the tree, and the release sets it down. The third and
# the fourth point of the list (counting the dialog points) are the two
# places of the hand, in cm from the head: right, up, forward.
$still = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 pull@3260+3 point@4500+1 aimfar@4510+1 grip@4600+101 pull@4640+3 pull@4670+3 shot@5900+1'
$stillPoints = '0,0,100 0,0,100 -10,-10,60 -10,-50,60'
# The same with a tree, and with a hand that turns before the second release:
# the word "aimturn" turns the hand 30 degrees up (fake_controller_turns), so
# the tree in the Hand leans more than 0.2 rad, and the game does not plant it
# (docs/grab.md, "A tree in the Hand"). The word "rot" does not turn the hand
# after the word "aimfar". The lines "hand tree" of the log have the answer
# of the card and what the tree is after the release.
$stillLean = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 pull@3260+3 point@4500+1 aimfar@4510+1 grip@4600+101 pull@4640+3 pull@4670+3 aimturn@4682+1 shot@5900+1'
# The same with a tree, and with a step of the hand to a fifth point before
# the second release: the hand sets the tree down 6 cm to the left or to the
# right of its place, about 5 units of the land. One of the two places is away
# from the rocks beside the place of the tree. The lines "tree body" of
# the log have the bodies that the release compares with the tree.
$stillAway = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 pull@3260+3 point@4500+1 aimfar@4510+1 grip@4600+101 pull@4640+3 pull@4670+3 point@4685+1 shot@4695+1 shot@5900+1'
$leftPoints = '0,0,100 0,0,100 -10,-10,60 -10,-50,60 -16,-50,60'
$rightPoints = '0,0,100 0,0,100 -10,-10,60 -10,-50,60 -4,-50,60'
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
# THE GRIP ON AN OBJECT THAT MOVES (docs/grab.md, "The grip holds",
# grip_press_hold). Each recipe is for the new rule and, with
# -More 'grip_press_hold=0', for the rule before.
#
# A short press on the rock in its fall: the grip is closed for 50 ms. With
# the new rule the rock comes into the Hand and the Hand lets it go, and no
# rock splits.
$dropShort = "$dropBase seek@3217+1 grip@3221+3 shot@3300+1 shot@3500+1"
# A grip on the rock on the land, 0.15 s after its first contact, while it
# still moves. The head looks 25 degrees down, so the hand at the land is in
# the view (the controller code drops a press that starts off view).
$landGrip = "$dropBase seek@3246+1 grip@3250+60 shot@3330+1 shot@3500+1"
# A catch of a rock that the hand threw up. The hand is 15 cm lower for the
# fetch. Then it steps 15 cm up in one frame, and the grip opens: a throw
# straight up. The hand goes to the rock 0.58 s later and the grip closes, a
# new press after a full opening.
$tossUp = 'point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 pull@3170+3 point@3200+1 seek@3236+1 grip@3240+60 shot@3320+1 shot@3500+1'
$tossPoints = '0,0,100 0,0,100 -10,-25,60 -10,-10,60'
# A CATCH AT ONCE (docs/grab.md, "A catch at once", catch_at_once). The
# recipes dropnear, tossup, landgrip and dropshort are the catches, and each
# is also for -More 'catch_at_once=0'. Two more recipes:
#
# A throw after a catch of the rock in its fall: the hand steps 15 cm over 3
# frames, and the grip opens in that step.
$catchThrow = "$dropBase seek@3217+1 grip@3221+59 pull@3277+3 shot@3330+1 shot@3500+1"
# Eye pictures of a catch: one near the frame of the press, and one 3 frames
# later. Run it with no -Fps, so the frames of the recipe are the frames of
# the run.
$catchShot = "$dropBase seek@3217+1 shot@3220+1 grip@3221+60 shot@3223+1 shot@3300+1 shot@3500+1"
# A fetch from a distance of a tree 1.0 s after its release, while it still
# moves on the land: the press of the fetch is a catch.
$catchFar = "$dropBase pull@3207+3 aimfar@3262+1 grip@3266+60 pull@3276+3 shot@3360+1 shot@3500+1"
# THE HEAD THAT LOOKS DOWN. The draw offers an object to the pick only when
# the render shows it, and the far grab and the word aimfar take only an
# object that the draw offers. The view has a fixed horizontal angle of 110
# degrees (fov_absolute_deg), so a render of 3600 by 2880 shows 48.8 degrees
# below its middle, and a render of 2208 by 2208 shows 55 degrees. After the
# fall the tree lies on the land about 50 degrees below a level view. With
# the head 25 degrees down, the tree on the land and the Hand are in the view
# at each of the two sizes.
$lookDown = 'fake_head_pitch=-25'
$defs = @{
    dropshort = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1'; Steps = $dropShort; Seconds = 80; Points = $stillPoints }
    landgrip = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1;fake_head_pitch=-25'; Steps = $landGrip; Seconds = 80; Points = $stillPoints }
    tossup = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1'; Steps = $tossUp; Seconds = 80; Points = $tossPoints }
    treedrop = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_thrown=1;far_test_no_trees=0;far_test_tree=1'; Steps = $dropFar; Seconds = 80; Points = $stillPoints }
    treedroproll = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_thrown=1;far_test_no_trees=0;far_test_tree=1'; Steps = $dropFarRoll; Seconds = 80; Points = $stillPoints }
    treerest = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_thrown=1;far_test_no_trees=0;far_test_tree=1'; Steps = "$dropBase aimfar@3290+1 shot@3600+1 shot@5000+1"; Seconds = 80; Points = $stillPoints }
    # A tree that comes to rest while the Hand takes it. The test key
    # catch_rest_test=1 makes one step of the tree answer "at rest" in the
    # first game turn in which the Hand has the tree. With catch_rest_guard=1
    # the tree stays in the Hand. With -More 'catch_rest_guard=0' the game
    # makes a dead tree, and the Hand is empty (docs/grab.md, "An object that
    # comes to rest while the Hand takes it"). The step of the tree runs
    # after the press only with -More 'catch_at_once=0'. The steps are the
    # steps of catchfar: the lock comes 65 frames after the opening of the
    # grip, when the tree is on the land and the physics of the game still
    # has it. The tree of the first aimfar falls straight down and comes to
    # rest about 1.6 s after the opening, so the lock must come before that.
    # At 53 frames a second, 65 frames are 1.2 s.
    treedropend = @{ Set = "throw_land=1;release_fall_cm=15;far_test_thrown=1;far_test_no_trees=0;far_test_tree=1;catch_rest_test=1;$lookDown"; Steps = $catchFar; Seconds = 80; Points = $stillPoints }
    treenear = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1;far_test_no_trees=0;far_test_tree=1'; Steps = $dropRoll; Seconds = 100; Points = $stillPoints }
    dropnear = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1'; Steps = $dropNear; Seconds = 80; Points = $stillPoints }
    catchthrow = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1'; Steps = $catchThrow; Seconds = 80; Points = $stillPoints }
    catchfar = @{ Set = "throw_land=1;release_fall_cm=15;far_test_thrown=1;far_test_no_trees=0;far_test_tree=1;$lookDown"; Steps = $catchFar; Seconds = 80; Points = $stillPoints }
    catchshot = @{ Set = 'throw_land=1;release_fall_cm=15;seek_thrown=1'; Steps = $catchShot; Seconds = 80; Points = $stillPoints }
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
    # A tree that the hand sets down upright, and a tree that leans at the
    # release. A rock lies 0.8 units from the place of the tree, and the game
    # plants neither of the two trees (docs/grab.md, "A tree in the Hand").
    treeset = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_no_trees=0;far_test_tree=1'; Steps = $still; Seconds = 112; Points = $stillPoints }
    treelean = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_no_trees=0;far_test_tree=1;fake_controller_turns=0,30,0'; Steps = $stillLean; Seconds = 112; Points = $stillPoints }
    # A tree that the hand sets down upright 6 cm to the left or to the right
    # of its place (docs/grab.md, "The test of a body in the way").
    treeleft = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_no_trees=0;far_test_tree=1'; Steps = $stillAway; Seconds = 112; Points = $leftPoints }
    treeright = @{ Set = 'throw_land=1;release_fall_cm=15;far_test_no_trees=0;far_test_tree=1'; Steps = $stillAway; Seconds = 112; Points = $rightPoints }
    # A tree: the far fetch takes only a tree (far_test_tree=1), and the hand
    # throws it.
    tree = @{ Set = 'throw_land=1;far_test_no_trees=0;far_test_tree=1'; Steps = $two; Seconds = 112 }
    # The two throws of "land" at a different scale of the God view: two zoom
    # steps out (225 units for each meter), and one zoom step in (66.7). The
    # zoom steps keep the land under the head in its place (god_zoom_about=0).
    landout = @{ Set = 'throw_land=1;god_zoom_about=0'; Steps = "stick_down@2800+10 stick_down@2830+10 $two"; Seconds = 112 }
    landin = @{ Set = 'throw_land=1;god_zoom_about=0'; Steps = "stick_up@2800+10 $two"; Seconds = 112 }
    # The same with two, three and seven zoom steps in: 44.4, 29.6 and 7.5
    # units for each meter (the seventh step stops at the life size scale).
    landin2 = @{ Set = 'throw_land=1;god_zoom_about=0'; Steps = "stick_up@2800+10 stick_up@2830+10 $two"; Seconds = 112 }
    landin3 = @{ Set = 'throw_land=1;god_zoom_about=0'; Steps = "stick_up@2800+10 stick_up@2830+10 stick_up@2860+10 $two"; Seconds = 112 }
    landlife = @{ Set = 'throw_land=1;god_zoom_about=0'; Steps = "stick_up@2800+10 stick_up@2830+10 stick_up@2860+10 stick_up@2890+10 stick_up@2920+10 stick_up@2950+10 stick_up@2980+10 $two"; Seconds = 112 }
    # A throw of about 9.5 m/s toward the sea, with no fetch after it. The
    # rock comes down on the sea about 10 m of the room from the release.
    sea = @{ Set = 'throw_land=1'; Steps = 'point@2990+1 aimfar@3000+1 grip@3060+141 pull@3100+3 pull@3150+3 point@3200+1 shot@3260+1 shot@3500+1'; Seconds = 100; Points = '0,0,100 0,0,100 -10,-10,60 -25,8,70' }
}
$Runs = @($Runs | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne '' })
foreach ($run in $Runs) {
    $d = $defs[$run]
    if ($null -eq $d) { "no run named $run"; continue }
    $name = "t$run$Tag"
    $set = "$common;$($d.Set)"
    if ($More -ne '') { $set = "$set;$More" }
    # The start of a run in a test save is 49 s shorter than the start of a
    # new game with the first story.
    $seconds = $d.Seconds
    $from = @{}
    if ($start) {
        $seconds = $d.Seconds - 49
        $from.Save = $start
    }
    "=== $name $(Get-Date -Format s), $(if ($start) { "the test save '$start'" } else { 'a new game' }), $seconds s"
    $o = & (Join-Path $sp 'aim_run.ps1') -Name $name -Dll $Dll -Set $set -Steps (Convert-BwvrSteps $d.Steps) -Points $(if ($d.Points) { $d.Points } else { $points }) -Seconds $seconds -Minutes $Minutes @from
    $o
    if (-not (Test-BwvrVerdict $o 'RUN DONE')) { "QUEUE STOPPED at $name"; exit 2 }
    Show-BwvrGripLines $name
}
"QUEUE DONE $(Get-Date -Format s)"
