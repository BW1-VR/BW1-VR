# Desktop runs of the ray pointer in the rooms of the Temple. See
# bwvr/docs/controllers.md, "The Temple".
#
#   temple_run.ps1 -Name tpl1 -Dll <dll> -Steps "<script>" -Set "<keys>" -Seconds 190
#
# The start is the choice "Keep your old Creature" (spawn_run.ps1). The ring
# item "enter" goes into the Temple at frame 850: the hall, and its guide
# until about frame 2710. The script waits for an idle PC (10 minutes, no
# game) and then makes one run through spawn_run.ps1 and desk_run.ps1. It
# reads the settings of the game in the registry before and after the run,
# and it says if a value changed. It writes nothing there.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Steps = '',
    [string]$Points = '20,-30,35',
    [string]$Set = '',
    [int]$Seconds = 190,
    [int]$Minutes = 170,
    [string]$Pattern = '',
    [switch]$NoEnter,
    [switch]$SaveTest
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
$enter = 'lstick_up@820+60 lstick_left@820+60 ltrigger@850+6'
# -NoEnter: the run stays on the land (the taps on the door and the posts).
if ($NoEnter) { $enter = '' }
$keys = 'grab_hand=1'
if ($Set -ne '') { $keys = "$keys;$Set" }
# -SaveTest: the run makes a save in the Save Game Room. The slots of a
# player are 0 to 13, and the test takes one that has no saved game. So the
# run starts only when the profile has none of those slots, and the list of
# the slots must be the same after the run (the profile guard of
# desk_run.ps1 puts the files back).
. (Join-Path $PSScriptRoot 'game.ps1')
# The first profile of the game folder that has saved games.
$saves = Get-ChildItem -LiteralPath (Join-Path (Get-BwvrGame) 'Profiles') -Directory |
    ForEach-Object { Join-Path $_.FullName 'Saved Games' } |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $saves) { 'NOT RUN: no profile with saved games in the game folder'; exit 7 }
function SlotList { (Get-ChildItem -LiteralPath $saves -Directory | ForEach-Object { $_.Name } | Sort-Object) -join ' ' }
function RegText { (& reg query 'HKCU\Software\Lionhead Studios Ltd\Black & White' /s 2>$null) -join "`n" }
$deadline = (Get-Date).AddMinutes($Minutes)
$done = $false
while ((Get-Date) -lt $deadline -and -not $done) {
    $idle = [int](& (Join-Path $sp 'idle_seconds.ps1'))
    $running = [bool](Get-Process runblack -ErrorAction SilentlyContinue)
    if ($idle -ge 620 -and -not $running) {
        $before = RegText
        if ($SaveTest) {
            $slotsBefore = SlotList
            "the slots of the profile before the run: $slotsBefore"
            $taken = @(0..13 | Where-Object { Test-Path -LiteralPath (Join-Path $saves "$_") })
            if ($taken.Count -gt 0) {
                "NOT RUN (the save test): the profile has the slots $($taken -join ' '), and the test saves only when the slots 0 to 13 are all empty"
                exit 8
            }
        }
        $o = & (Join-Path $sp 'spawn_run.ps1') -Name $Name -Dll $Dll -Choice keep `
            -Steps ("$enter $Steps").Trim() -Points $Points -Set $keys -Seconds $Seconds -WaitMinutes 45
        $o
        if (($o -join "`n") -notmatch 'NOT RUN') {
            $after = RegText
            if ($SaveTest) {
                # The profile guard takes the new files away, and the empty
                # folder of the new slot stays. Remove each folder that was
                # not there before the run, deepest first, and only when it
                # is empty (a delete that is not recursive).
                $had = $slotsBefore -split ' '
                foreach ($d in (Get-ChildItem -LiteralPath $saves -Directory | Where-Object { $had -notcontains $_.Name })) {
                    $inner = @(Get-ChildItem -LiteralPath $d.FullName -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending)
                    foreach ($x in ($inner + $d)) {
                        try { [System.IO.Directory]::Delete($x.FullName, $false); "removed the empty folder of the test: $($x.FullName.Substring($saves.Length))" }
                        catch { "WARNING: the folder $($x.FullName) of the test is not empty, so it stays" }
                    }
                }
                $slotsAfter = SlotList
                "the slots of the profile after the run: $slotsAfter"
                "the slots of the profile are the same after the run: $($slotsBefore -eq $slotsAfter)"
            }
            "the settings of the game in the registry are the same after the run: $($before -eq $after)"
            if ($before -ne $after) {
                Set-Content -Path (Join-Path $sp "desk-$Name\registry-before.txt") -Value $before
                Set-Content -Path (Join-Path $sp "desk-$Name\registry-after.txt") -Value $after
            }
            $done = $true
        }
    }
    if (-not $done) { Start-Sleep -Seconds 30 }
}
if (-not $done) { "GAVE UP: the PC was not idle for 10 minutes in $Minutes minutes"; exit 2 }
$log = Join-Path $sp "desk-$Name\bwvr.log"
if (-not (Test-Path $log)) { "no log at $log"; exit 1 }
if ($Pattern -eq '') {
    $Pattern = '^temple|^ring         : (the game|the player chose|the ring does not)|^hooks        : (Temple|InnerCamera)|' +
               '^controllers  : (the fake (hand|trigger|grip|y)|the (left|right) button|the controller lets go|a dialog box)|' +
               '^hand         : state|^god view     : the game draws'
}
Get-Content $log | Where-Object { $_ -match $Pattern } | ForEach-Object {
    $_.Substring(0, [Math]::Min(520, $_.Length))
} | Select-Object -First 260
"RUN DONE"
