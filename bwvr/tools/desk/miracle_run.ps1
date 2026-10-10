# Desktop runs of a miracle in the Hand. See bwvr/docs/miracles.md.
#
#   miracle_run.ps1 -Name x1 -Dll <dll> -Kind creature -Set "miracle_hand=0"
#
#   miracle_run.ps1 -Name x2 -Dll <dll> -Kind throw -NewGame
#
# THE START. The run starts in the test save "temple" (README.md, "A fast
# start"): no story, and the Creature of the profile at the door of the
# Temple. When the save is not there, one run makes it. The steps and the
# frames of miracle_test_seeds count from frame 800 as the frame at which the
# controller first places the Hand in the loaded game
# (fake_controller_play_frame=800), so each frame in the log is about 730
# less than the frame of the step. -NewGame gives a new game with the choice
# "Keep your old Creature" (spawn_run.ps1): the same state. -Save gives a
# different test save with that state. With no -Seconds, a new game runs 75 s,
# and a run in a test save ends 4 s after its last step.
#
# The test key miracle_test_seeds makes one-off seeds on the land under the
# Hand.
#
# -Kind creature: a seed of Creature Strong (16). A short press of the grip
#   makes it active. A press over bare land, a press with the aim on the
#   Creature from a distance, a shake (5 steps of the word "pull"), a second
#   seed, a press with the Hand on the Creature, and a short press of B.
# -Kind throw: a Fireball (2): hold the grip, a step of the hand, open. Then
#   Water (9): hold the grip for 90 frames, and a tap of B. Water stays in
#   the Hand after a pour, and the tap takes it out. Then Heal (7): a press.
#   Then a Creature seed that the Hand carries, and the trigger.
# The other kinds (cancel, place, shield) have a comment at their steps
# below. -Seeds gives other seed types to the steps of a kind (the types are
# in bwvr/docs/miracles.md), and -Steps gives other steps.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Kind = 'creature',
    [string]$Set = '',
    [string]$Steps = '',
    [string]$Seeds = '',
    [int]$Seconds = 0,
    [string]$Save = 'temple',
    [switch]$NewGame
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
$start = ''
if (-not $NewGame) { $start = Confirm-BwvrTestSave $Save $Dll }
# The zoom steps of these recipes keep the land under the head in its place
# (god_zoom_about=0).
$keys = 'god_zoom_about=0;far_test_creature=1;far_test_influence=1;grab_hand=1;far_grab_max_m=0'
$points = '0,-30,60 14,-32,56 -12,-31,58 10,-30,62 -6,-32,54 4,-31,64 0,-30,60 12,-31,58'
if ($Kind -eq 'throw') {
    if ($Seeds -eq '') { $Seeds = '2@930 9@1330 7@1730 16@2030' }
    if ($Steps -eq '') {
        $Steps = 'stick_up@800+10 stick_up@830+10 look@860+5000 point@900+1 ' +
                 'seek@960+1 grip@1000+3 point@1080+1 grip@1120+40 pull@1156+3 ' +
                 'pull@1230+1 point@1300+1 seek@1360+1 grip@1400+3 point@1480+1 grip@1520+90 b@1640+5 ' +
                 'point@1700+1 seek@1760+1 grip@1800+3 point@1880+1 grip@1920+10 ' +
                 'point@2000+1 seek@2060+1 grip@2100+260 trigger@2200+6 shot@2420+1'
    }
} elseif ($Kind -eq 'cancel') {
    # A shake of a new miracle, the button on a new miracle, and a shake while
    # the spell of a cast miracle runs.
    if ($Seeds -eq '') { $Seeds = '16@930 16@1250 16@1560' }
    if ($Steps -eq '') {
        $Steps = 'stick_up@800+10 stick_up@830+10 look@860+5000 point@900+1 ' +
                 'seek@960+1 grip@1000+3 ' +
                 'pull@1100+1 pull@1106+1 pull@1112+1 pull@1118+1 pull@1124+1 pull@1130+1 ' +
                 'seek@1300+1 grip@1340+3 b@1440+5 ' +
                 'seek@1610+1 grip@1650+3 aimfar@1720+1 grip@1760+10 ' +
                 'pull@1800+1 pull@1806+1 pull@1812+1 pull@1818+1 pull@1824+1 pull@1830+1 ' +
                 'shot@1980+1'
    }
} elseif ($Kind -eq 'place') {
    # Heal (7): a press casts it at the place of the Hand. Then a Creature
    # seed that the grip carries: the trigger makes it active, and a press
    # with the Hand on the Creature casts it.
    if ($Seeds -eq '') { $Seeds = '7@930 16@1330' }
    if ($Steps -eq '') {
        $Steps = 'stick_up@800+10 stick_up@830+10 look@860+5000 point@900+1 ' +
                 'seek@960+1 grip@1000+3 point@1080+1 grip@1120+10 ' +
                 'point@1300+1 seek@1360+1 grip@1400+260 trigger@1500+6 ' +
                 'touchc@1700+1 grip@1740+10 shot@1900+1'
    }
} elseif ($Kind -eq 'shield') {
    # A shield (4): the grip stays closed while the hand steps through the
    # circle of Gestures.jty (template 5), and opens. Then a storm (0).
    if ($Seeds -eq '') { $Seeds = '4@930 0@1500' }
    $keys = "$keys;fake_controller_gestures=5,5"
    if ($Steps -eq '') {
        $Steps = 'stick_up@800+10 stick_up@830+10 look@860+5000 point@900+1 ' +
                 'seek@960+1 grip@1000+3 trace@1100+200 grip@1106+150 ' +
                 'point@1470+1 seek@1540+1 grip@1580+3 trace@1680+200 grip@1686+150 ' +
                 'b@1980+5 shot@2060+1'
    }
} else {
    if ($Seeds -eq '') { $Seeds = '16@930 16@1700' }
    if ($Steps -eq '') {
        $Steps = 'stick_up@800+10 stick_up@830+10 look@860+5000 point@900+1 ' +
                 'seek@960+1 grip@1000+3 point@1080+1 grip@1120+10 ' +
                 'aimfar@1200+1 grip@1240+10 ' +
                 'pull@1500+1 pull@1506+1 pull@1512+1 pull@1518+1 pull@1524+1 pull@1530+1 ' +
                 'point@1660+1 seek@1740+1 grip@1780+3 touchc@1860+1 grip@1900+10 ' +
                 'b@2100+5 shot@2200+1'
    }
}
$keys = "$keys;miracle_test_seeds=$Seeds"
if ($start) {
    # In a test save the steps and the frames of the seeds count from the
    # frame at which the controller first places the Hand, as frame 800. The
    # start is 10 s shorter than the start of a new game.
    $keys = "fake_controller_play_frame=800;$keys"
    if ($Seconds -le 0) { $Seconds = Get-BwvrSaveSeconds $Steps 800 65 }
} elseif ($Seconds -le 0) {
    $Seconds = 75
}
if ($Set -ne '') { $keys = "$keys;$Set" }
& (Join-Path $sp 'spawn_run.ps1') -Name $Name -Dll $Dll -Choice keep -Steps $Steps `
    -Points $points -Set $keys -Seconds $Seconds -Save $start
# spawn_run.ps1 gave up: no run, so no log of this run to read.
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$log = Join-Path $sp "desk-$Name\bwvr.log"
if (-not (Test-Path $log)) { "no log at $log"; exit 1 }
$pattern = '^miracle      :|^hand press   : the grip pressed|^haptics      : (effect (14|18|19|20|21|23|24|25|26|27|28)|cast|refused)|' +
           '^hint card    : frame .*(iracle|ancel|Cast|pour|throw|Use)|^hand         : state'
Get-Content $log | Where-Object { $_ -match $pattern } | ForEach-Object {
    $_.Substring(0, [Math]::Min(420, $_.Length))
} | Select-Object -First 220
