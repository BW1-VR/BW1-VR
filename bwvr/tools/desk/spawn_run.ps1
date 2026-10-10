# Waits for an idle PC (10 minutes, no game), then one desktop run of a new
# game with a choice of the start dialog, through desk_run.ps1. Gives up after
# -Minutes. -Choice keep gives a run with the Creature of the profile.
#
#   spawn_run.ps1 -Name x1 -Dll <dll> -Choice keep -Seconds 110
#   spawn_run.ps1 -Name x1 -Dll <dll> -Choice keep -Steps "shot@3000+1" -Set "teleport=0;god_spawn=0"
#
# -Choice chooses the row of the start dialog:
#   normal  "Start the game normally" (no click on a row, only OK)
#   select  "Skip straight to the Creature select"
#   skip    "Skip all of the Creature tutorial"
#   keep    "Keep your old Creature"
# -Steps is the script after the dialog (fake_controller_script words).
# -Points are the points after the two dialog points (cm from the head).
# -Set is a list of ini keys, "key=value;key=value".
#
# The run uses fake_controller_front=1: the fake controller acts while the
# game window is not in front. desk_run.ps1 prints the place of the real
# cursor and the window in front before and after the run.
#
# The last line of the output is RUN DONE (exit code 0) or GAVE UP (exit
# code 2).
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Choice = 'keep',
    [string]$Steps = '',
    [string]$Points = '',
    [string]$Set = '',
    [int]$Seconds = 110,
    [int]$Minutes = 115,
    [int]$WaitMinutes = 45
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
# The points of the start dialog depend on the render size. dialog.ps1
# measures a new size one time and gives the points. The
# first pose (yaw 8, pitch 1) points at a row, and the second pose (yaw 8,
# pitch -6) points at OK.
. (Join-Path $sp 'dialog.ps1')
if (-not $script:DialogRowY.ContainsKey($Choice)) { "no choice named $Choice"; exit 7 }
$deadline = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $deadline) {
    $idle = [int](& (Join-Path $sp 'idle_seconds.ps1'))
    $running = [bool](Get-Process runblack -ErrorAction SilentlyContinue)
    if ($idle -ge 620 -and -not $running) {
        if (-not (Confirm-DialogCalibration -Dll $Dll)) { Start-Sleep -Seconds 30; continue }
        $dp = Get-DialogPoints $Choice
        if ($Choice -eq 'normal') {
            $dialog = 'pose2@500+1 point@520+1 trigger@560+8'
            $first = $dp.Ok
        } else {
            $dialog = 'point@400+1 trigger@440+8 pose2@500+1 point@520+1 trigger@560+8'
            $first = "$($dp.Row) $($dp.Ok)"
        }
        $extra = @{
            play_mode = 4; fake_controller = 2; controller_hand = 1; intro_vr = 0
            intro_skip_test_frame = 5; fake_controller_front = 1
            fake_controller_yaw = 8; fake_controller_pitch = 1
            fake_controller_sweep_yaw = 8; fake_controller_sweep_pitch = -6
            test_keys = 1; depth24 = 1; help_test = 1
            fake_controller_points = ("$first $Points").Trim()
            fake_controller_script = ("$dialog $Steps").Trim()
        }
        foreach ($pair in $Set.Split(';')) {
            if ($pair -match '^\s*([^=\s]+)\s*=\s*(.*)$') { $extra[$Matches[1]] = $Matches[2] }
        }
        $o = & (Join-Path $sp 'desk_run.ps1') -Name $Name -Dll $Dll -Extra $extra -Seconds $Seconds -WaitMinutes $WaitMinutes
        $o
        if (-not (Test-BwvrVerdict $o 'NOT RUN')) { "RUN DONE"; exit 0 }
    }
    Start-Sleep -Seconds 30
}
"GAVE UP: the PC was not idle for 10 minutes in $Minutes minutes"
exit 2
