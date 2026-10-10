# One desktop run of a given ddraw.dll, safe when several agents test.
#
#   desk_run.ps1 -Name tp2 -Dll <build>\src\mod\Release\ddraw.dll -Extra @{ play_mode = 4 } -Seconds 120
#
# 1. Takes the lock desk.lock (waits up to -WaitMinutes for another run).
#    The lock is in the folder that Get-BwvrDeskShared of game.ps1 gives, so
#    the runs of each git worktree of this PC wait for one another.
# 2. Checks the idle time and that no game runs, as desk_test.ps1 does.
# 3. Puts -Dll in the game folder, runs desk_test.ps1, and always puts the
#    installed DLL back, then frees the lock.
# 4. Compares the place of the real cursor and the window in front before
#    and after the run, and says so. A run must not move the real cursor.
#    The result is also in desk-<Name>\desk-state.txt. It does not change
#    the result of the run.
# The output is in desk-<Name>, as for desk_test.ps1.
#
# -Save <name>: the run starts in a test save (README.md, "A fast start").
#    The game continues the saved game of slot 20 when it starts with no word
#    on its command line. So, after the profile guard made its copy, the files
#    of the test save take the place of the files of slot 20, and the game
#    starts with no -NEWGAME. The profile guard puts the slot back. The run
#    has the test key registry_test_guard=1: the mod drops each write of the
#    game to the registry, and the log names each one.
# -KeepSave <name>: when the game ended by itself in the run (the word "exit"
#    of the scripted controller, or exit_test_frame), slot 20 holds the saved
#    game of that moment. The script copies the files that the game wrote in
#    the run to the folder of the test saves, before the profile guard puts
#    the slot back. A file of the slot that the run did not write is a file
#    of the player, and the script does not copy it. make_test_save.ps1 uses
#    this.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [hashtable]$Extra = @{},
    [int]$Seconds = 90,
    [string]$Args = '-NEWGAME',
    [int]$MinIdleMinutes = 10,
    [int]$WaitMinutes = 30,
    [string]$Save = '',
    [string]$KeepSave = ''
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $PSScriptRoot 'game.ps1')
. (Join-Path $PSScriptRoot 'desk_state.ps1')
$game = Get-BwvrGame
$installed = Join-Path $game 'ddraw.dll'
$lock = Join-Path (Get-BwvrDeskShared) 'desk.lock'
if (-not (Test-Path $Dll)) { "NOT RUN: no DLL at $Dll"; exit 6 }
$saveFrom = $null
if ($Save -ne '') {
    $saveDir = Get-BwvrTestSave $Save
    if (-not $saveDir) { "NOT RUN: there is no test save named '$Save' in $(Get-BwvrTestSaves). Make it first: make_test_save.ps1 -Name $Save"; exit 9 }
    $saveFrom = Join-Path $saveDir '20'
}

# Puts each file of a folder back as its copy has it, and removes each file
# and each empty folder that the run made. Gives the counts. Content is the
# number of the files put back that were gone or had different bytes, and not
# only a different time.
function Restore-BwvrGuard([string]$Guard, [string]$Live) {
    $restored = 0; $content = 0; $removed = 0; $folders = 0
    foreach ($f in (Get-ChildItem -LiteralPath $Guard -Recurse -File)) {
        $file = $Live + $f.FullName.Substring($Guard.Length)
        $l = Get-Item -LiteralPath $file -ErrorAction SilentlyContinue
        if ($null -eq $l -or $l.Length -ne $f.Length -or $l.LastWriteTimeUtc -ne $f.LastWriteTimeUtc) {
            if ($null -eq $l -or (Get-FileHash -LiteralPath $file).Hash -ne (Get-FileHash -LiteralPath $f.FullName).Hash) { $content++ }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $file) | Out-Null
            Copy-Item -LiteralPath $f.FullName -Destination $file -Force
            $restored++
        }
    }
    foreach ($l in (Get-ChildItem -LiteralPath $Live -Recurse -File)) {
        if (-not (Test-Path -LiteralPath ($Guard + $l.FullName.Substring($Live.Length)))) {
            [System.IO.File]::Delete($l.FullName); $removed++
        }
    }
    # A folder that the run made (a new save slot) goes too, when it is empty
    # after its files went. The deepest folders first.
    foreach ($d in (Get-ChildItem -LiteralPath $Live -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending)) {
        if (-not (Test-Path -LiteralPath ($Guard + $d.FullName.Substring($Live.Length))) -and
            @(Get-ChildItem -LiteralPath $d.FullName -Force).Count -eq 0) {
            [System.IO.Directory]::Delete($d.FullName, $false); $folders++
        }
    }
    return @{ Restored = $restored; Content = $content; Removed = $removed; Folders = $folders }
}
if ($KeepSave -ne '' -and $KeepSave -notmatch '^[A-Za-z0-9_-]+$') { "NOT RUN: the name '$KeepSave' of a test save must have only letters, digits, '-' and '_'"; exit 9 }

# The lock. A lock older than 20 minutes is from a run that died.
$deadline = (Get-Date).AddMinutes($WaitMinutes)
$held = $false
while (-not $held) {
    try {
        $fs = [System.IO.File]::Open($lock, 'CreateNew', 'Write', 'None')
        $bytes = [System.Text.Encoding]::ASCII.GetBytes("$Name $(Get-Date -Format s) $sp")
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
    $testNow = Test-BwvrTestNow
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
    # THE PROFILE GUARD. In a run a new game writes the save of slot 20
    # (20.sav, Script_20.sav, SaveGame.inf), two save pictures (Saved
    # Games\Pictures), helpstats.dat and Saved Games\vortex.txt. A game that
    # ends by itself writes the save of slot 20, two save pictures and
    # helpstats.dat on its exit path too, and the values of the profile in
    # the registry. Each of those files is a file of the player, and each one
    # is in the profile folder. So the profile folder is copied before the
    # run, and after the run each file goes back as it was. The registry is
    # not a part of this guard: a run stops the game before its exit path, and
    # the mod drops the registry writes of a run with exit_test_frame.
    $player = Join-Path $game 'Profiles'
    $guard = Join-Path $sp 'profile-guard'
    $guardOk = $false
    if (Test-Path -LiteralPath $player) {
        if (Test-Path -LiteralPath $guard) { [System.IO.Directory]::Delete($guard, $true) }
        Copy-Item -LiteralPath $player -Destination $guard -Recurse -Force
        $guardOk = $true
    }
    # THE CREATURE GUARD. The mind of the Creature of the profile is not in
    # the profile folder. It is in the game folder, in Scripts\CreatureMind:
    # the .erc file that the value "file" of the profile names, and the file
    # "Physique" with the same name. A game that ends by itself writes both
    # on its exit path, and it writes MemoryLeaks.txt in the game folder. So
    # the folder Scripts\CreatureMind and that file are copied before the
    # run too, and after the run each file goes back as it was.
    $mind = Join-Path $game 'Scripts\CreatureMind'
    $mindGuard = Join-Path $sp 'creature-guard'
    $mindOk = $false
    if (Test-Path -LiteralPath $mind) {
        if (Test-Path -LiteralPath $mindGuard) { [System.IO.Directory]::Delete($mindGuard, $true) }
        Copy-Item -LiteralPath $mind -Destination $mindGuard -Recurse -Force
        $mindOk = $true
    }
    $leaks = Join-Path $game 'MemoryLeaks.txt'
    $leaksKeep = Join-Path $out 'MemoryLeaks.txt.before'
    Remove-Item -LiteralPath $leaksKeep -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $leaks) { Copy-Item -LiteralPath $leaks -Destination $leaksKeep -Force }
    # THE TEST SAVE. Slot 20 is the saved game that the game continues: the
    # folder Saved Games\20 of the profile that the game uses. A run touches
    # it only after the profile guard has its copy.
    $slot = $null
    if ($Save -ne '' -or $KeepSave -ne '') {
        $used = Get-BwvrProfileFolder $game
        if (-not $guardOk -or -not $used) {
            "NOT RUN: a run with a test save needs the folder of the profile that the game uses, and the copy of the profile guard. The folder of the profile: '$used'"
            exit 9
        }
        $slot = Join-Path $used 'Saved Games\20'
    }
    $deskBefore = Get-BwvrDeskState
    $runStart = Get-Date
    $testOut = @()
    try {
        $saveIn = $true
        if ($Save -ne '') {
            # Out of the slot goes each file that is there (the guard has
            # it). In go the files of the test save, and nothing else.
            New-Item -ItemType Directory -Force -Path $slot | Out-Null
            foreach ($f in (Get-ChildItem -LiteralPath $slot -Recurse -File -Force)) { [System.IO.File]::Delete($f.FullName) }
            foreach ($f in (Get-ChildItem -LiteralPath $saveFrom -Recurse -File -Force)) {
                $to = $slot + $f.FullName.Substring($saveFrom.Length)
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
                Copy-Item -LiteralPath $f.FullName -Destination $to -Force
            }
            $saveIn = (Get-BwvrFolderText $slot) -eq (Get-BwvrFolderText $saveFrom)
            if ($saveIn) {
                "the test save '$Save' is in slot 20 of the profile: $(@(Get-ChildItem -LiteralPath $slot -Recurse -File).Count) files, each one the same as in $saveFrom"
            } else {
                "NOT RUN: the files of the test save '$Save' are not whole in slot 20 of the profile, so the game does not start"
            }
            $Extra = $Extra.Clone()
            $Extra['registry_test_guard'] = 1
            $Args = ''
        }
        if ($saveIn) {
            Copy-Item $Dll $installed -Force
            & (Join-Path $sp 'desk_test.ps1') -Name $Name -Extra $Extra -Seconds $Seconds -Args $Args -MinIdleMinutes 0 |
                Tee-Object -Variable testOut
        }
    }
    finally {
        Start-Sleep -Milliseconds 500
        # THE REAL CURSOR AND THE WINDOW IN FRONT, before and after. A move of
        # the cursor counts against the run when the last input of a person is
        # older than the run: SetCursorPos of a program is not an input.
        $deskAfter = Get-BwvrDeskState
        $took = ($deskAfter.At - $deskBefore.At).TotalSeconds
        $state = @(
            "before the run: the real cursor was at $($deskBefore.Cursor), and the window in front had the class $($deskBefore.Front)"
            "after the run: the real cursor is at $($deskAfter.Cursor), and the window in front has the class $($deskAfter.Front)"
        )
        $moved = $false
        if ($deskAfter.Cursor -eq $deskBefore.Cursor) {
            $state += 'the real cursor: at the same place before and after the run'
        } elseif ($deskAfter.Idle -ge $took) {
            $moved = $true
            $state += "THE REAL CURSOR MOVED in the run $Name, from $($deskBefore.Cursor) to $($deskAfter.Cursor), and nobody used the PC (the last input was $([int]$deskAfter.Idle) s ago, the run took $([int]$took) s). A run must not move it"
            $runLog = Join-Path $out 'bwvr.log'
            $callers = @()
            if (Test-Path -LiteralPath $runLog) {
                $callers = @(Select-String -LiteralPath $runLog -Pattern 'cursor trace : .*(It moved the cursor|with no call of the game process)' |
                    Select-Object -First 8 | ForEach-Object { '  ' + $_.Line })
            }
            if ($callers.Count -gt 0) {
                $state += 'the log of the mod names these moves:'
                $state += $callers
            } else {
                $state += "the log of the mod names no call of the game process that moved it (lines 'cursor trace' of $runLog). So a different program moved it, or the run had cursor_trace=0"
            }
        } else {
            $state += "the real cursor is at a different place than before the run ($($deskBefore.Cursor), now $($deskAfter.Cursor)). A person used the PC $([int]$deskAfter.Idle) s ago, so this does not count against the run"
        }
        if ($deskAfter.Front -ne $deskBefore.Front) {
            $state += "the window in front has a different class than before the run ($($deskBefore.Front), now $($deskAfter.Front))"
        }
        $state
        Set-Content -LiteralPath (Join-Path $out 'desk-state.txt') -Value $state -Encoding ascii
        # A script that shows only the last lines of this output must not hide
        # the move, so it also goes out as a warning.
        if ($moved) { Write-Warning $state[2] }
        if (-not (Get-Process runblack -ErrorAction SilentlyContinue)) {
            Copy-Item $keep $installed -Force
            "installed dll put back: " + ((Get-FileHash $installed).Hash -eq (Get-FileHash $keep).Hash)
            if ($KeepSave -ne '' -and $slot) {
                # The game writes slot 20 at the end of its loop. So the
                # files are the saved game of the end of the run only when
                # the game ended by itself.
                $sav = Get-Item -LiteralPath (Join-Path $slot '20.sav') -ErrorAction SilentlyContinue
                if (-not (Test-BwvrVerdict $testOut 'game exited on its own: True')) {
                    "test save NOT kept: the game did not end by itself, so slot 20 does not hold the saved game of the end of the run"
                } elseif (-not $sav -or $sav.LastWriteTime -lt $runStart) {
                    "test save NOT kept: the game did not write 20.sav in slot 20 in this run"
                } else {
                    # Only the files that the game wrote in this run are the
                    # test save. A file of the slot that is older than the run
                    # is a file of the player: it is not copied.
                    $keepDir = Join-Path (Get-BwvrTestSaves) $KeepSave
                    $new = "$keepDir.new"
                    Remove-BwvrTestSaveFolder $new
                    New-Item -ItemType Directory -Force -Path (Join-Path $new '20') | Out-Null
                    $whole = $true; $copied = 0; $older = 0
                    foreach ($f in (Get-ChildItem -LiteralPath $slot -Recurse -File -Force)) {
                        if ($f.LastWriteTime -lt $runStart) { $older++; continue }
                        $to = (Join-Path $new '20') + $f.FullName.Substring($slot.Length)
                        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
                        Copy-Item -LiteralPath $f.FullName -Destination $to -Force
                        if ((Get-FileHash -LiteralPath $to).Hash -ne (Get-FileHash -LiteralPath $f.FullName).Hash) { $whole = $false }
                        $copied++
                    }
                    if ($whole) {
                        Remove-BwvrTestSaveFolder $keepDir
                        [System.IO.Directory]::Move($new, $keepDir)
                        "test save kept: '$KeepSave', the $copied files of slot 20 that the game wrote in this run, in $keepDir. $older files of the slot are older than the run and are not copied"
                    } else {
                        Remove-BwvrTestSaveFolder $new
                        "test save NOT kept: the copy of slot 20 was not whole"
                    }
                }
            }
            if ($guardOk) {
                $back = Restore-BwvrGuard $guard $player
                "profile put back: $($back.Restored) files restored, $($back.Removed) new files removed, $($back.Folders) new folders removed"
            }
            if ($mindOk) {
                $back = Restore-BwvrGuard $mindGuard $mind
                "Creature mind put back: $($back.Restored) files restored ($($back.Content) of them had different bytes, the others only a different time), $($back.Removed) new files removed"
            }
            if (Test-Path -LiteralPath $leaksKeep) {
                $was = Get-Item -LiteralPath $leaksKeep
                $now = Get-Item -LiteralPath $leaks -ErrorAction SilentlyContinue
                if ($null -eq $now -or $now.Length -ne $was.Length -or $now.LastWriteTimeUtc -ne $was.LastWriteTimeUtc) {
                    Copy-Item -LiteralPath $leaksKeep -Destination $leaks -Force
                    'MemoryLeaks.txt of the game folder put back'
                }
            } elseif (Test-Path -LiteralPath $leaks) {
                [System.IO.File]::Delete($leaks)
                'MemoryLeaks.txt of the game folder removed: the run made it'
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
