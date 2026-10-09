# One desktop run of a new game with a choice of the start dialog, through
# desk_run.ps1. -Choice keep gives a run with the Creature of the profile.
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
# game window is not in front. The script prints the place of the real cursor
# and the window in front before and after the run. They must not change.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Choice = 'keep',
    [string]$Steps = '',
    [string]$Points = '',
    [string]$Set = '',
    [int]$Seconds = 110,
    [int]$WaitMinutes = 45
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
# The points of the start dialog depend on the render size. dialog.ps1
# measures a new size one time and gives the points. The
# first pose (yaw 8, pitch 1) points at a row, and the second pose (yaw 8,
# pitch -6) points at OK.
. (Join-Path $sp 'dialog.ps1')
if (-not $script:DialogRowY.ContainsKey($Choice)) { "no choice named $Choice"; exit 7 }
if (-not (Confirm-DialogCalibration -Dll $Dll)) {
    'NOT RUN: the start dialog is not measured for this render size'
    exit 9
}
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
Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class SpawnWatch {
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
  [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  public static string State() {
    POINT p; GetCursorPos(out p);
    StringBuilder s = new StringBuilder(256);
    GetClassName(GetForegroundWindow(), s, 256);
    return "the real cursor is at " + p.X + " " + p.Y + ", and the window in front has the class " + s.ToString();
  }
}
'@
"before the run: $([SpawnWatch]::State())"
& (Join-Path $sp 'desk_run.ps1') -Name $Name -Dll $Dll -Extra $extra -Seconds $Seconds -WaitMinutes $WaitMinutes
"after the run: $([SpawnWatch]::State())"
