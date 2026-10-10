# Waits for an idle PC (10 minutes, no game), then one desktop run of the
# given DLL through desk_run.ps1, with the recipe of wait_and_run.ps1 and the
# keys of -Set on top. Gives up after -Minutes.
#
#   aim_run.ps1 -Name aim1 -Dll <dll> -Set "aim_test_keys=3600;aim_pitch_deg=-0.5" -Steps "<script words>" -Seconds 150
#
# -Save <name>: the run starts in a test save and not in a new game
#    (README.md, "A fast start"). The run has no start dialog. The game opens
#    its Main Menu over the loaded game, and the script clicks "Continue
#    Game" there (dialog.ps1 gives the point and the words). Then come
#    -Steps, and the points are the points of -Points after its first two.
#    With fake_controller_play_frame=N in -Set, the steps from frame N on
#    count from the frame at which the controller first places the Hand after
#    frame 60 (fake_controller_play_after). A key of -Set with a frame of its
#    own, such as aim_test_touch, does not count that way: give it a frame
#    that fits the run.
# -KeepSave <name>: see desk_run.ps1.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Set = '',
    [string]$Steps = '',
    [string]$Points = '',
    [int]$Seconds = 120,
    [int]$Minutes = 115,
    # -Now: no wait for an idle PC (only with the owner's word for these
    # runs). The lock and the test for a running game stay.
    [switch]$Now,
    [int]$WaitMinutes = 60,
    [string]$Save = '',
    [string]$KeepSave = ''
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
if ($Save -ne '' -and -not (Get-BwvrTestSave $Save)) {
    "NOT RUN: there is no test save named '$Save' in $(Get-BwvrTestSaves). Make it first: make_test_save.ps1 -Name $Save"
    exit 9
}
# The first two points close the start dialog: the row "Skip straight to
# the Creature select", then OK. They depend on the render size, so
# dialog.ps1 gives them (it measures a new size one time), and they take the
# place of the first two points of -Points.
. (Join-Path $sp 'dialog.ps1')
function Set-DialogPoints {
    $rest = @($Points.Split(' ') | Where-Object { $_ -ne '' } | Select-Object -Skip 2)
    if (-not (Confirm-DialogCalibration -Dll $Dll)) { return $false }
    # A test save has no start dialog. Its first point is the button
    # "Continue Game" of the Main Menu.
    if ($Save -ne '') { $extra.fake_controller_points = (@(Get-MenuContinuePoint) + $rest) -join ' '; return $true }
    $dp = Get-DialogPoints 'select'
    $extra.fake_controller_points = (@($dp.Row, $dp.Ok) + $rest) -join ' '
    return $true
}

$dialog = 'point@400+1 trigger@440+8 pose2@500+1 point@520+1 trigger@560+8'
# In a test save the script clicks "Continue Game" of the Main Menu.
if ($Save -ne '') { $dialog = $script:MenuContinueWords }
if ($Steps -eq '') {
    $Steps = 'shot@2900+1 point@2990+1 aimfar@3000+1 shot@3050+1 grip@3100+101 pull@3140+3 shot@3185+1 point@3200+1 shot@3215+1 point@3300+1 a@3320+50 shot@3350+1 aimfar@3400+1 grip@3460+101 pull@3500+3 shot@3545+1 point@3560+1'
}
if ($Points -eq '') {
    # The first two are the dialog points, which Set-DialogPoints puts in.
    $Points = '0,0,100 0,0,100 -10,-10,60 -10,-4,66 -10,-10,60 -10,-4,66'
}
$extra = @{
    play_mode = 4; fake_controller = 2; controller_hand = 1; intro_vr = 0
    fake_controller_yaw = 8; fake_controller_pitch = 1
    fake_controller_sweep_yaw = 8; fake_controller_sweep_pitch = -6
    test_keys = 1; far_test_influence = 1; far_test_no_trees = 1
    far_grab_max_m = 0; throw_log = 1; throw_law = 2; depth24 = 1; help_test = 1
    fake_controller_points = $Points
    fake_controller_script = "$dialog $Steps"
}
if ($Save -ne '') { $extra.fake_controller_play_after = 60 }
foreach ($pair in $Set.Split(';')) {
    if ($pair -match '^\s*([^=\s]+)\s*=\s*(.*)$') { $extra[$Matches[1]] = $Matches[2] }
}
# The test save of the run, and the test save that the run makes.
$saves = @{}
if ($Save -ne '') { $saves.Save = $Save }
if ($KeepSave -ne '') { $saves.KeepSave = $KeepSave }
if ($Now) {
    if (-not (Set-DialogPoints)) { 'NOT RUN NOW: the start dialog is not measured for this render size'; exit 3 }
    $o = & (Join-Path $sp 'desk_run.ps1') -Name $Name -Dll $Dll -Extra $extra -Seconds $Seconds -MinIdleMinutes 0 -WaitMinutes $WaitMinutes @saves
    $o
    if (-not (Test-BwvrVerdict $o 'NOT RUN')) { "RUN DONE"; exit 0 }
    "NOT RUN NOW"
    exit 3
}
$deadline = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $deadline) {
    $idle = [int](& (Join-Path $sp 'idle_seconds.ps1'))
    $running = [bool](Get-Process runblack -ErrorAction SilentlyContinue)
    if ($idle -ge 620 -and -not $running) {
        if (-not (Set-DialogPoints)) { Start-Sleep -Seconds 30; continue }
        $o = & (Join-Path $sp 'desk_run.ps1') -Name $Name -Dll $Dll -Extra $extra -Seconds $Seconds @saves
        $o
        if (-not (Test-BwvrVerdict $o 'NOT RUN')) { "RUN DONE"; exit 0 }
    }
    Start-Sleep -Seconds 30
}
"GAVE UP: the PC was not idle for 10 minutes in $Minutes minutes"
exit 2
