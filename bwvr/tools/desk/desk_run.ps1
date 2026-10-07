# One desktop run of a given ddraw.dll, safe when several agents test.
#
#   desk_run.ps1 -Name tp2 -Dll <build>\src\mod\Release\ddraw.dll -Extra @{ play_mode = 4 } -Seconds 120
#
# 1. Takes the lock desk.lock (waits up to -WaitMinutes for another run).
# 2. Checks the idle time and that no game runs, as desk_test.ps1 does.
# 3. Puts -Dll in the game folder, runs desk_test.ps1, and always puts the
#    installed DLL back, then frees the lock.
# The output is in desk-<Name>, as for desk_test.ps1.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [hashtable]$Extra = @{},
    [int]$Seconds = 90,
    [string]$Args = '-NEWGAME',
    [int]$MinIdleMinutes = 10,
    [int]$WaitMinutes = 30
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $PSScriptRoot 'game.ps1')
$game = Get-BwvrGame
$installed = Join-Path $game 'ddraw.dll'
$lock = Join-Path $sp 'desk.lock'
if (-not (Test-Path $Dll)) { "NOT RUN: no DLL at $Dll"; exit 6 }

# The lock. A lock older than 20 minutes is from a run that died.
$deadline = (Get-Date).AddMinutes($WaitMinutes)
$held = $false
while (-not $held) {
    try {
        $fs = [System.IO.File]::Open($lock, 'CreateNew', 'Write', 'None')
        $bytes = [System.Text.Encoding]::ASCII.GetBytes("$Name $(Get-Date -Format s)")
        $fs.Write($bytes, 0, $bytes.Length); $fs.Close()
        $held = $true
    } catch {
        $age = (Get-Date) - (Get-Item $lock -ErrorAction SilentlyContinue).LastWriteTime
        if ($age.TotalMinutes -gt 20) { Remove-Item $lock -Force -ErrorAction SilentlyContinue; continue }
        if ((Get-Date) -gt $deadline) { "NOT RUN: another run held the lock for $WaitMinutes minutes ($(Get-Content $lock))"; exit 5 }
        Start-Sleep -Seconds 15
    }
}

try {
    Add-Type @'
using System; using System.Runtime.InteropServices;
public static class IdleR {
  [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
  [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO i);
  public static double Seconds() {
    LASTINPUTINFO i = new LASTINPUTINFO(); i.cbSize = 8;
    GetLastInputInfo(ref i);
    return ((uint)Environment.TickCount - i.dwTime) / 1000.0;
  }
}
'@
    $idle = [IdleR]::Seconds()
    # The owner's "test now": while the time in test-now-until.txt is in the
    # future, the idle check is off. Each other guard stays.
    $flag = Join-Path $sp 'test-now-until.txt'
    $testNow = $false
    if (Test-Path $flag) { try { $testNow = (Get-Date) -lt [datetime]((Get-Content $flag -Raw).Trim()) } catch { } }
    if (-not $testNow -and $MinIdleMinutes -gt 0 -and $idle -lt $MinIdleMinutes * 60) {
        "NOT RUN: the PC was used $([int]$idle) s ago. A run needs $MinIdleMinutes minutes with no input."
        exit 3
    }
    $why = Test-BwvrFirstStart $game
    if ($why) {
        "NOT RUN: $why. Open 'Black & White VR Mod.exe' in the game folder one time, select 'Play in VR', then close the game."
        exit 8
    }
    if (Get-Process runblack -ErrorAction SilentlyContinue) {
        "NOT RUN: the game is already running. It may be the player's own session."
        exit 4
    }
    $out = Join-Path $sp ("desk-" + $Name)
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $keep = Join-Path $out 'ddraw.dll.installed'
    Copy-Item $installed $keep -Force
    # The old logs that are there before the run (the mod keeps the log of
    # each run before as bwvr-old-<date>-<time>.log).
    $before = @(Get-ChildItem $game -Filter 'bwvr-old-*.log' -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
    # THE PROFILE GUARD. A new game writes the save slot 20, the save pictures
    # and helpstats.dat, and a game that ends by itself writes creature.lhp.
    # Those are the player's files. So the profile folder is copied before the
    # run, and after the run each file goes back as it was.
    $player = Join-Path $game 'Profiles'
    $guard = Join-Path $sp 'profile-guard'
    $guardOk = $false
    if (Test-Path -LiteralPath $player) {
        if (Test-Path -LiteralPath $guard) { [System.IO.Directory]::Delete($guard, $true) }
        Copy-Item -LiteralPath $player -Destination $guard -Recurse -Force
        $guardOk = $true
    }
    try {
        Copy-Item $Dll $installed -Force
        & (Join-Path $sp 'desk_test.ps1') -Name $Name -Extra $Extra -Seconds $Seconds -Args $Args -MinIdleMinutes 0
    }
    finally {
        Start-Sleep -Milliseconds 500
        if (-not (Get-Process runblack -ErrorAction SilentlyContinue)) {
            Copy-Item $keep $installed -Force
            "installed dll put back: " + ((Get-FileHash $installed).Hash -eq (Get-FileHash $keep).Hash)
            if ($guardOk) {
                $restored = 0; $removed = 0
                foreach ($f in (Get-ChildItem -LiteralPath $guard -Recurse -File)) {
                    $live = $player + $f.FullName.Substring($guard.Length)
                    $l = Get-Item -LiteralPath $live -ErrorAction SilentlyContinue
                    if ($null -eq $l -or $l.Length -ne $f.Length -or $l.LastWriteTimeUtc -ne $f.LastWriteTimeUtc) {
                        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $live) | Out-Null
                        Copy-Item -LiteralPath $f.FullName -Destination $live -Force
                        $restored++
                    }
                }
                foreach ($l in (Get-ChildItem -LiteralPath $player -Recurse -File)) {
                    if (-not (Test-Path -LiteralPath ($guard + $l.FullName.Substring($player.Length)))) {
                        [System.IO.File]::Delete($l.FullName); $removed++
                    }
                }
                # A folder that the run made (a new save slot) goes too, when it
                # is empty after its files went. The deepest folders first.
                $folders = 0
                foreach ($d in (Get-ChildItem -LiteralPath $player -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending)) {
                    if (-not (Test-Path -LiteralPath ($guard + $d.FullName.Substring($player.Length))) -and
                        @(Get-ChildItem -LiteralPath $d.FullName -Force).Count -eq 0) {
                        [System.IO.Directory]::Delete($d.FullName, $false); $folders++
                    }
                }
                "profile put back: $restored files restored, $removed new files removed, $folders new folders removed"
            }
            # The log that was bwvr.log before this run goes back, so that
            # test runs do not push the player's logs out of the kept 20.
            # The log of the test itself is already in the run folder.
            $made = @(Get-ChildItem $game -Filter 'bwvr-old-*.log' -ErrorAction SilentlyContinue | Where-Object { $before -notcontains $_.Name })
            if ($made.Count -eq 1) {
                $live = Join-Path $game 'bwvr.log'
                try {
                    [System.IO.File]::Delete($live)
                    [System.IO.File]::Move($made[0].FullName, $live)
                    "log of the run before put back: $($made[0].Name)"
                } catch { "WARNING: the log of the run before was not put back: $_" }
            }
        } else {
            "WARNING: a game runs, so the installed dll was not put back. It is in $keep"
        }
    }
}
finally {
    Remove-Item $lock -Force -ErrorAction SilentlyContinue
}
