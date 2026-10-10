# Waits for an idle PC (10 minutes, no game), then one desktop run of the
# given DLL through desk_run.ps1. Gives up after -Minutes.
#
#   hand_run.ps1 -Name hs1 -Dll <dll> -Kind one -Seconds 200
#   hand_run.ps1 -Name hs2 -Dll <dll> -Kind old -Seconds 200
#   hand_run.ps1 -Name hs3 -Dll <dll> -Kind creature -Seconds 260
#
# -Kind chooses the script:
#   one       One target: the air, the scroll and the crown, each with
#             near_rule 1 and then 2 (near_test_flips), at the store of Land
#             1. With near_rule 2 the grip at the scroll takes the rock beside
#             it: the Tug state, the Holding state and a set down.
#   old       The start "Keep your old Creature". Four times: the grip on the
#             Creature (the Creature state), four strokes and two slaps.
#   creature  The start "Skip straight to the Creature select". The steps for
#             the choice of a Creature at the Creature select, and then the
#             steps of "old". The choice does not work now: no Creature is
#             chosen, and no Creature state starts (README.md, "Known limits
#             of the recipes").
#   fixed     The places from -Points (cm from the head), with no seek, so a
#             DLL of an older build runs the same test. -Steps gives the
#             script after the start dialog. With no -Points and no -Steps
#             the run only closes the start dialog.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Kind = 'one',
    [string]$Set = '',
    [string]$Steps = '',
    [string]$Points = '',
    [string]$Box = '',
    [int]$Seconds = 200,
    [int]$Minutes = 600
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
# The steps that close the start dialog.
$dialog = 'point@400+1 trigger@440+8 pose2@500+1 point@520+1 trigger@560+8'
# The two dialog points depend on the render size. dialog.ps1 measures a
# new size one time and gives them. For -Kind old it also gives the point of
# the fourth box. -Box gives a different point for that box.
. (Join-Path $sp 'dialog.ps1')
if (-not (Confirm-DialogCalibration -Dll $Dll)) {
    'NOT RUN: the start dialog is not measured for this render size'
    exit 9
}
$dp = Get-DialogPoints 'select'
$d1 = $dp.Row; $d2 = $dp.Ok
$extra = @{
    play_mode = 4; fake_controller = 2; controller_hand = 1; intro_vr = 0
    fake_controller_yaw = 8; fake_controller_pitch = 1
    fake_controller_sweep_yaw = 8; fake_controller_sweep_pitch = -6
    test_keys = 1; far_test_influence = 1; far_grab = 0; grab_hand = 1
    throw_log = 1; throw_law = 2; depth24 = 1; help_test = 1; fake_controller_front = 1
    # The zoom steps of these recipes keep the land under the head in its
    # place.
    god_zoom_about = 0
}
if ($Kind -eq 'one') {
    # The first story ends before frame 4600, so the script starts there.
    # First to the store of Land 1: four turns
    # right and six zoom steps out.
    $s = '-16,-50,55'
    $extra.fake_controller_points = "$d1 $d2 $s $s $s $s $s $s $s"
    $extra.seek_plan = 'air,air,scroll,scroll,crown,crown'
    $extra.near_test_flips = '5055,5190,5315,5440,5575'
    $store = 'stick_right@4600+8 stick_right@4630+8 stick_right@4660+8 stick_right@4690+8 ' +
             'stick_down@4720+10 stick_down@4750+10 stick_down@4780+10 stick_down@4810+10 stick_down@4840+10 stick_down@4870+10'
    $Steps = $store + ' point@4900+1 ' +
             'seek@4930+1 grip@4980+40 shot@5005+1 point@5050+1 ' +
             'seek@5065+1 grip@5115+40 shot@5140+1 point@5185+1 ' +
             'seek@5200+1 shot@5240+1 grip@5250+30 point@5310+1 ' +
             'seek@5325+1 shot@5365+1 grip@5375+30 point@5435+1 ' +
             'seek@5450+1 grip@5500+40 shot@5525+1 point@5570+1 ' +
             'seek@5585+1 grip@5635+40 shot@5660+1 point@5710+1'
} elseif ($Kind -eq 'creature') {
    # The Creature. teleport=0 makes the A button the double click of the
    # game, which chooses a Creature at the Creature select. The head looks
    # down, two zoom steps in, and seek_plan=creature puts the hand on the
    # body bone of the nearest Creature in view. First the choice, at four
    # headings. Then three times, at three headings: the grip held with four
    # strokes (nudge), a slap of the wrist (rot out and in: the grip is still
    # and the tip moves) and a slap of the arm.
    $p = '0,-40,60'
    $extra.fake_controller_points = "$d1 $d2 $p"
    $extra.fake_controller_rots = '-27,1,0 8,1,0'
    $extra.seek_plan = 'creature'
    $extra.teleport = 0
    $Steps = 'look@4600+12000 point@4620+1 stick_up@4650+10 stick_up@4680+10'
    $b = 4720
    foreach ($k in 0..3) {
        $Steps += " seek@$($b)+1 trigger@$($b + 45)+6 a@$($b + 75)+4"
        if ($k -lt 3) { $Steps += " stick_right@$($b + 110)+8 stick_right@$($b + 140)+8" }
        $b += 180
    }
    foreach ($b in 7400, 9300, 11200) {
        $Steps += " seek@$($b)+1 grip@$($b + 50)+700 nudge@$($b + 150)+1 nudge@$($b + 190)+1" +
                  " nudge@$($b + 230)+1 nudge@$($b + 270)+1 seek@$($b + 300)+1" +
                  " rot@$($b + 340)+1 rot@$($b + 490)+1 slap@$($b + 560)+1 slap@$($b + 700)+1" +
                  " stick_right@$($b + 800)+8 stick_right@$($b + 830)+8"
    }
} elseif ($Kind -eq 'old') {
    # A new game with "keep your old Creature" (the fourth box of the start
    # dialog, -Box gives its point). The head looks down, one zoom step in,
    # and seek_plan=creature puts the hand on the body bone of the nearest
    # Creature in view. Four times: the grip held with four strokes (nudge),
    # a slap of the wrist (rot out and in) and a slap of the arm.
    if ($Box -eq '') { $Box = (Get-DialogPoints 'keep').Row }
    $extra.fake_controller_points = "$Box $d2 0,-30,60"
    $extra.fake_controller_rots = '-27,1,0 8,1,0'
    $extra.seek_plan = 'creature'
    $dialog = 'shot@300+1 point@400+1 trigger@440+8 shot@480+1 pose2@500+1 point@520+1 trigger@560+8'
    $Steps = 'look@700+30000 stick_up@720+10 point@1100+1'
    foreach ($b in 1200, 2400, 3600, 5000) {
        $Steps += " seek@$($b)+1 grip@$($b + 50)+700 nudge@$($b + 150)+1 nudge@$($b + 190)+1" +
                  " nudge@$($b + 230)+1 nudge@$($b + 270)+1 seek@$($b + 300)+1" +
                  " rot@$($b + 340)+1 rot@$($b + 490)+1 slap@$($b + 560)+1 slap@$($b + 700)+1"
    }
} else {
    $extra.fake_controller_points = "$d1 $d2 $Points"
}
$extra.fake_controller_script = "$dialog $Steps"
foreach ($pair in $Set.Split(';')) {
    if ($pair -match '^\s*([^=\s]+)\s*=\s*(.*)$') { $extra[$Matches[1]] = $Matches[2] }
}
"script: $($extra.fake_controller_script.Length) characters"
$deadline = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $deadline) {
    $idle = [int](& (Join-Path $sp 'idle_seconds.ps1'))
    $running = [bool](Get-Process runblack -ErrorAction SilentlyContinue)
    if ($idle -ge 620 -and -not $running) {
        $o = & (Join-Path $sp 'desk_run.ps1') -Name $Name -Dll $Dll -Extra $extra -Seconds $Seconds
        $o
        if (-not (Test-BwvrVerdict $o 'NOT RUN')) { "RUN DONE $(Get-Date -Format s)"; exit 0 }
    }
    Start-Sleep -Seconds 30
}
"GAVE UP: the PC was not idle for 10 minutes in $Minutes minutes"
exit 2
