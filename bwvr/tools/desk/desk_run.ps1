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
# -VulkanPending <text>: the run starts with the file bwvr-vulkan-pending.txt
#    in the game folder, with this text. With backend=vulkan the game then
#    uses dxwrapper (docs/backend.md, "The fall back of the renderer
#    Vulkan"). Without it, each run starts with no such file.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [hashtable]$Extra = @{},
    [int]$Seconds = 90,
    [string]$Args = '-NEWGAME',
    [int]$MinIdleMinutes = 10,
    [int]$WaitMinutes = 30,
    [string]$Save = '',
    [string]$KeepSave = '',
    [string]$VulkanPending = ''
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $PSScriptRoot 'game.ps1')
. (Join-Path $PSScriptRoot 'desk_state.ps1')
$game = Get-BwvrGame
$installed = Join-Path $game 'ddraw.dll'
$pending = Join-Path $game 'bwvr-vulkan-pending.txt'
$pendingKeep = $null
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
    # THE CREATURE GUARD. The mind of the Creature of the profile is not in
    # the profile folder. It is in Scripts\CreatureMind: the .erc file that
    # the value "file" of the profile names, and the file "Physique" with the
    # same name. A game that ends by itself writes both on its exit path, and
    # it writes MemoryLeaks.txt. So the folder Scripts\CreatureMind and that
    # file are copied before the run too, and after the run each file goes
    # back as it was.
    # THE FOLDERS OF THE GUARDS. The game writes these files in the folder of
    # the value GameDir in the registry, and that folder can be a different
    # install or its VirtualStore (Get-BwvrPlayerFolders in game.ps1). Each
    # guard keeps the files of that folder and of the game folder. A folder
    # that is not there before the run gets an empty copy, so each file that
    # the run makes in it is removed after the run.
    $playerFolders = Get-BwvrPlayerFolders $game
    "the files of the player: $($playerFolders.Why)"
    $guards = @()
    $leakKeeps = @()
    $n = 0
    foreach ($root in $playerFolders.Roots) {
        foreach ($part in @(@{ Sub = 'Profiles'; Copy = 'profile-guard'; What = 'profile' }, @{ Sub = 'Scripts\CreatureMind'; Copy = 'creature-guard'; What = 'Creature mind' })) {
            $live = Join-Path $root $part.Sub
            $copy = Join-Path $sp ($part.Copy + $(if ($n -gt 0) { "-$n" } else { '' }))
            if (Test-Path -LiteralPath $copy) { [System.IO.Directory]::Delete($copy, $true) }
            if (Test-Path -LiteralPath $live) {
                Copy-Item -LiteralPath $live -Destination $copy -Recurse -Force
            } else {
                New-Item -ItemType Directory -Force -Path $copy | Out-Null
            }
            $guards += @{ Live = $live; Copy = $copy; What = $part.What }
        }
        $leaks = Join-Path $root 'MemoryLeaks.txt'
        $leaksKeep = Join-Path $out ('MemoryLeaks.txt.before' + $(if ($n -gt 0) { "-$n" } else { '' }))
        Remove-Item -LiteralPath $leaksKeep -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $leaks) { Copy-Item -LiteralPath $leaks -Destination $leaksKeep -Force }
        $leakKeeps += @{ Live = $leaks; Keep = $leaksKeep }
        $n++
    }
    $guardOk = $guards.Count -gt 0
    # THE TEST SAVE. Slot 20 is the saved game that the game continues: the
    # folder Saved Games\20 of the profile that the game uses. In its start
    # the game reads 20.sav in the game folder. Then it changes its current
    # folder, reads the other files of the slot, and on its exit path writes
    # the slot in the folder of Get-BwvrPlayerFolders ($keepSlot). On most
    # PCs the two are one folder. A test save goes into the slot of each of
    # the two ($slots). A run touches them only after the profile guard has
    # its copy.
    $slots = @()
    $keepSlot = $null
    if ($Save -ne '') {
        $used = Get-BwvrProfileFolder $game
        $later = Get-BwvrProfileFolder $playerFolders.Folder -Make
        if (-not $guardOk -or -not $used -or -not $later) {
            "NOT RUN: a run with a test save needs the folder of the profile that the game uses, in the game folder and in the folder that gets the writes of the game, and the copy of the profile guard. The folders of the profile: '$used', '$later'"
            exit 9
        }
        $slots = @(@($used, $later) | Select-Object -Unique | ForEach-Object { Join-Path $_ 'Saved Games\20' })
    }
    if ($KeepSave -ne '') {
        $used = Get-BwvrProfileFolder $playerFolders.Folder -Make
        if (-not $guardOk -or -not $used) {
            "NOT RUN: a run that keeps a test save needs the folder of the profile that the game writes, and the copy of the profile guard. The folder of the profile: '$used'"
            exit 9
        }
        $keepSlot = Join-Path $used 'Saved Games\20'
    }
    $deskBefore = Get-BwvrDeskState
    $runStart = Get-Date
    $testOut = @()
    try {
        $saveIn = $true
        if ($Save -ne '') {
            # Out of each slot goes each file that is there (the guard has
            # it). In go the files of the test save, and nothing else.
            foreach ($slot in $slots) {
                New-Item -ItemType Directory -Force -Path $slot | Out-Null
                foreach ($f in (Get-ChildItem -LiteralPath $slot -Recurse -File -Force)) { [System.IO.File]::Delete($f.FullName) }
                foreach ($f in (Get-ChildItem -LiteralPath $saveFrom -Recurse -File -Force)) {
                    $to = $slot + $f.FullName.Substring($saveFrom.Length)
                    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
                    Copy-Item -LiteralPath $f.FullName -Destination $to -Force
                }
                if ((Get-BwvrFolderText $slot) -eq (Get-BwvrFolderText $saveFrom)) {
                    "the test save '$Save' is in slot 20 of the profile: $(@(Get-ChildItem -LiteralPath $slot -Recurse -File).Count) files, each one the same as in $saveFrom, in $slot"
                } else {
                    $saveIn = $false
                    "NOT RUN: the files of the test save '$Save' are not whole in slot 20 of the profile ($slot), so the game does not start"
                }
            }
            $Extra = $Extra.Clone()
            $Extra['registry_test_guard'] = 1
            $Args = ''
        }
        # THE GUARD OF THE FALL BACK OF VULKAN. With backend=vulkan the mod
        # writes bwvr-vulkan-pending.txt in the game folder, and the first
        # picture deletes it. A start with the file there uses dxwrapper. So
        # a file that a different run left goes before the run, and after the
        # run the file of the player comes back.
        $pendingKeep = Join-Path $out 'bwvr-vulkan-pending.txt.before'
        Remove-Item -LiteralPath $pendingKeep -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $pending) {
            Move-Item -LiteralPath $pending -Destination $pendingKeep -Force
            "bwvr-vulkan-pending.txt was in the game folder. It is out for the run, and it comes back after it"
        }
        if ($VulkanPending -ne '') {
            Set-Content -LiteralPath $pending -Value $VulkanPending -Encoding ascii
            "the run starts with bwvr-vulkan-pending.txt in the game folder: $VulkanPending"
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
            if ($pendingKeep) {
                if (Test-Path -LiteralPath $pending) {
                    Copy-Item -LiteralPath $pending -Destination (Join-Path $out 'bwvr-vulkan-pending.txt.after') -Force
                    "bwvr-vulkan-pending.txt after the run: there. $((Get-Content -LiteralPath $pending -Raw).Trim())"
                    [System.IO.File]::Delete($pending)
                } else {
                    "bwvr-vulkan-pending.txt after the run: not there"
                }
                if (Test-Path -LiteralPath $pendingKeep) {
                    Move-Item -LiteralPath $pendingKeep -Destination $pending -Force
                    "bwvr-vulkan-pending.txt of the player put back"
                }
            }
            Copy-Item $keep $installed -Force
            "installed dll put back: " + ((Get-FileHash $installed).Hash -eq (Get-FileHash $keep).Hash)
            if ($KeepSave -ne '' -and $keepSlot) {
                # The game writes slot 20 at the end of its loop. So the
                # files are the saved game of the end of the run only when
                # the game ended by itself.
                $sav = Get-Item -LiteralPath (Join-Path $keepSlot '20.sav') -ErrorAction SilentlyContinue
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
                    foreach ($f in (Get-ChildItem -LiteralPath $keepSlot -Recurse -File -Force)) {
                        if ($f.LastWriteTime -lt $runStart) { $older++; continue }
                        $to = (Join-Path $new '20') + $f.FullName.Substring($keepSlot.Length)
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
            foreach ($g in $guards) {
                if (-not (Test-Path -LiteralPath $g.Live)) { continue }
                $back = Restore-BwvrGuard $g.Copy $g.Live
                "$($g.What) put back: $($back.Restored) files restored ($($back.Content) of them had different bytes, the others only a different time), $($back.Removed) new files removed, $($back.Folders) new folders removed, in $($g.Live)"
            }
            foreach ($k in $leakKeeps) {
                if (Test-Path -LiteralPath $k.Keep) {
                    $was = Get-Item -LiteralPath $k.Keep
                    $now = Get-Item -LiteralPath $k.Live -ErrorAction SilentlyContinue
                    if ($null -eq $now -or $now.Length -ne $was.Length -or $now.LastWriteTimeUtc -ne $was.LastWriteTimeUtc) {
                        Copy-Item -LiteralPath $k.Keep -Destination $k.Live -Force
                        "MemoryLeaks.txt put back: $($k.Live)"
                    }
                } elseif (Test-Path -LiteralPath $k.Live) {
                    [System.IO.File]::Delete($k.Live)
                    "MemoryLeaks.txt removed, the run made it: $($k.Live)"
                }
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
