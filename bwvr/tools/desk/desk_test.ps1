# One desktop run of the game with a test ini, then everything put back.
#
#   desk_test.ps1 -Name m1 -Extra @{ fake_controller = 1 } -Seconds 90
#
# Backs up the player's bwvr.ini, writes the rig keys and the extra keys on
# top, starts runblack.exe directly (never the launcher, so nothing writes the
# registry), waits, stops the game, keeps the log, and restores the ini.
#
# Each run skips the logos at the start of the game (intro_skip_test_frame=5
# in the rig keys). A run that tests the logos gives intro_skip_test_frame=0,
# or its own frame, in -Extra.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [hashtable]$Extra = @{},
    [int]$Seconds = 90,
    [string]$Args = '-NEWGAME',
    [int]$MinIdleMinutes = 10
)

# The player may be at this PC. Windows gives the front to the game window of
# a run, so a run only starts when nobody has touched the keyboard or the
# mouse for a while. -MinIdleMinutes 0 skips the check.
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Idle {
  [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO i);
  public static double Seconds() {
    LASTINPUTINFO i = new LASTINPUTINFO(); i.cbSize = 8;
    GetLastInputInfo(ref i);
    return ((uint)Environment.TickCount - i.dwTime) / 1000.0;
  }
}
'@
$idle = [Idle]::Seconds()
if ($MinIdleMinutes -gt 0 -and $idle -lt $MinIdleMinutes * 60) {
    "NOT RUN: the PC was used $([int]$idle) s ago. A desktop run needs $MinIdleMinutes minutes with no input, so it does not take the screen from the player."
    exit 3
}

# The player may be playing in the headset with controllers only, which does
# not count as input above. A running game is always the player's: never stop
# it, never start a second one.
if (Get-Process runblack -ErrorAction SilentlyContinue) {
    "NOT RUN: the game is already running. It may be the player's own session, so a test never stops it."
    exit 4
}

. (Join-Path $PSScriptRoot 'game.ps1')
. (Join-Path $PSScriptRoot 'desk_state.ps1')
$game = Get-BwvrGame
$ini = Join-Path $game 'bwvr.ini'
$log = Join-Path $game 'bwvr.log'
# A game folder in which the launcher of the mod has never run has no
# settings file to keep and to put back, and the game has no display size.
$why = Test-BwvrFirstStart $game
if ($why) {
    "NOT RUN: $why. Open 'Black & White VR Mod.exe' in the game folder one time, select 'Play in VR', then close the game."
    exit 8
}
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path $sp ("desk-" + $Name)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$backup = Join-Path $out 'bwvr.ini.player'

$p = $null
Copy-Item $ini $backup -Force

try {
    # The rig from docs/backend.md, "Testing without a headset", then the
    # extra keys of this test.
    # Two test keys keep the rule of the real cursor. pointer_leave_cursor=1:
    # the mod does not hold or move the cursor of Windows, also when Windows
    # gives the front to the game window. cursor_trace=1: the log of the mod
    # names each caller in the game process that touches the cursor or takes
    # the front.
    # intro_skip_test_frame=5: at frame 5 of the logos a press comes that no
    # hand made, and the mod skips the logos, which take 38 s.
    # registry_test_guard=1: the mod drops each write of the game to its
    # keys of the registry, and the log names it. The game writes values of
    # the player's profile there by itself (the key bindings, and the
    # alignment each 600 game turns), and a test must not change them.
    # god_zoom_forward_in=1: the word stick_up of a recipe zooms in. Each
    # recipe counts on that, and the default of the mod zooms out with the
    # stick forward.
    $keys = [ordered]@{
        vr = 0; stereo = 1; stereo_headless = 1; fake_head = 1;
        fake_head_yaw = 0; fake_head_pitch = 0; fov_absolute_deg = 110
        pointer_leave_cursor = 1; cursor_trace = 1
        intro_skip_test_frame = 5
        registry_test_guard = 1
        god_zoom_forward_in = 1
    }
    foreach ($k in $Extra.Keys) { $keys[$k] = $Extra[$k] }

    $lines = Get-Content $backup | Where-Object {
        $m = [regex]::Match($_, '^\s*([^=;\s]+)\s*=')
        -not ($m.Success -and $keys.Contains($m.Groups[1].Value))
    }
    # The keys go right after the [bwvr] header. The launcher can add other
    # sections after it (for example [game_before]), and a key appended at
    # the end of the file would land in the last section.
    $written = @()
    $inserted = $false
    foreach ($line in $lines) {
        $written += $line
        if (-not $inserted -and $line -match '^\s*\[bwvr\]\s*$') {
            foreach ($k in $keys.Keys) { $written += "$k=$($keys[$k])" }
            $inserted = $true
        }
    }
    if (-not $inserted) {
        $written += '[bwvr]'
        foreach ($k in $keys.Keys) { $written += "$k=$($keys[$k])" }
    }
    Set-Content -Path $ini -Value $written -Encoding ascii

    $startArgs = @{ FilePath = (Join-Path $game 'runblack.exe'); WorkingDirectory = $game; PassThru = $true }
    if ($Args -and $Args.Trim().Length -gt 0) { $startArgs['ArgumentList'] = $Args }
    # THE WATCH. While the game runs, one line for each change of the place
    # of the real cursor or of the window in front, with the time and the idle
    # time of Windows. It only reads.
    $watch = Get-BwvrDeskState
    $started = $watch.At
    $watchLines = 0
    function Watch-Desk([string]$When) {
        $now = Get-BwvrDeskState
        $cursorMoved = $now.Cursor -ne $watch.Cursor
        $frontChanged = $now.Front -ne $watch.Front
        if (($cursorMoved -or $frontChanged) -and $script:watchLines -lt 16) {
            $script:watchLines++
            $whose = if ($p -and $now.FrontPid -eq $p.Id) { ' (the game window)' } else { '' }
            $cursor = if ($cursorMoved) { "the real cursor went from $($watch.Cursor) to $($now.Cursor)" } else { "the real cursor stays at $($now.Cursor)" }
            $front = if ($frontChanged) { "The window in front changed from the class $($watch.Front) to the class $($now.Front)$whose" } else { "The window in front stays the class $($now.Front)$whose" }
            "desk watch, ${When}: $cursor. $front. The last input was $([int]$now.Idle) s ago"
        }
        $script:watch = $now
    }
    $p = Start-Process @startArgs
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
        Start-Sleep -Seconds 2
        Watch-Desk "$([int]((Get-Date) - $started).TotalSeconds) s after the start"
    }
    $exited = $p.HasExited
    if (-not $exited) { Stop-Process -Id $p.Id -Force }
    Start-Sleep -Milliseconds 800
    Watch-Desk 'after the end of the game'
    Copy-Item $log (Join-Path $out 'bwvr.log') -Force
    "game exited on its own: $exited"
    Get-ChildItem $game -Filter 'bwvr-*.bmp' -ErrorAction SilentlyContinue |
        Move-Item -Destination $out -Force
}
finally {
    # Only the game this test started. A game the player starts during the
    # test is theirs.
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    Start-Sleep -Milliseconds 500
    Copy-Item $backup $ini -Force
    "ini restored: " + ((Get-FileHash $ini).Hash -eq (Get-FileHash $backup).Hash)
}
