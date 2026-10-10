# Desktop runs of the ray pointer in the rooms of the Temple. See
# bwvr/docs/controllers.md, "The Temple".
#
#   temple_run.ps1 -Name tpl1 -Dll <dll> -Steps "<script>" -Set "<keys>" -Seconds 190
#
#   temple_run.ps1 -Name tpl3 -Dll <dll> -NewGame
#
# THE START. The run starts in the test save "temple" (README.md, "A fast
# start"): no story, and the game camera at rest at the door of the Temple.
# When the save is not there, one run makes it. The steps count from frame
# 800 as the frame at which the controller first places the Hand in the
# loaded game (fake_controller_play_frame=800), so the frames of -Steps are
# the same for each start, and each frame in the log is about 730 less than
# the frame of the step. -NewGame gives a new game with the choice "Keep your
# old Creature" (spawn_run.ps1): the same state. -Save gives a different test
# save with that state. A run in a test save is 10 s shorter (180 s with no
# -Seconds). -SaveTest keeps the new game: its check reads the slots of the
# profile, and a run in a test save has the files of the test save in slot
# 20 of the profile.
#
# The ring item "enter" goes into the Temple at frame 850: the hall, and its
# guide. The guide has a length in seconds, so the frame of its end depends
# on the frame rate: between frame 2700 and frame 3250. The ring does not
# open while a guide plays, so put the first step of -Steps after frame 3350.
# The script waits for an idle PC (10 minutes, no game) and then makes one
# run through spawn_run.ps1 and desk_run.ps1. It reads the settings of the
# game in the registry before and after the run, and it says if a value
# changed. It writes nothing there.
#
# With no -Steps the run stays in the hall. The ring of the Temple goes to a
# room: the left stick in the direction of the item for 60 frames, and the
# left trigger 30 frames after its start. From the top and clockwise the
# items are hall, room_creature, room_challenge (lstick_right), room_save,
# leave (lstick_down), room_options, room_library (lstick_left). The first
# visit of a room plays a guide of 650 to 1500 frames. For example, the
# Challenge Room and then the Library, with a scan of the controls of each:
#
#   temple_run.ps1 -Name tpl2 -Dll <dll> -Set "temple_test_scan=16" -Steps `
#     "lstick_right@3400+60 ltrigger@3430+6 lstick_left@5800+60 ltrigger@5830+6"
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Steps = '',
    [string]$Points = '20,-30,35',
    [string]$Set = '',
    [int]$Seconds = 0,
    [int]$Minutes = 170,
    [string]$Pattern = '',
    [switch]$NoEnter,
    [switch]$SaveTest,
    [string]$Save = 'temple',
    [switch]$NewGame
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $PSScriptRoot 'game.ps1')
$start = ''
if (-not $NewGame -and -not $SaveTest) { $start = Confirm-BwvrTestSave $Save $Dll $Minutes }
$enter = 'lstick_up@820+60 lstick_left@820+60 ltrigger@850+6'
# -NoEnter: the run stays on the land (the taps on the door and the posts).
if ($NoEnter) { $enter = '' }
$keys = 'grab_hand=1'
# In a test save the steps count from the frame at which the controller first
# places the Hand, as frame 800. The start is 10 s shorter than the start of
# a new game.
if ($start) { $keys = "fake_controller_play_frame=800;$keys" }
if ($Seconds -le 0) { $Seconds = if ($start) { 180 } else { 190 } }
if ($Set -ne '') { $keys = "$keys;$Set" }
# -SaveTest: the run makes a save in the Save Game Room. The slots of a
# player are 0 to 13, and the test takes one that has no saved game. So the
# run starts only when the profile has none of those slots, and the list of
# the slots must be the same after the run (the profile guard of
# desk_run.ps1 puts the files back).
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
        # One try of spawn_run.ps1: the wait for an idle PC is the loop of
        # this script, which reads the registry right before each try. The
        # RUN DONE or GAVE UP of spawn_run.ps1 is not shown: this script
        # gives its own.
        $o = & (Join-Path $sp 'spawn_run.ps1') -Name $Name -Dll $Dll -Choice keep `
            -Steps ("$enter $Steps").Trim() -Points $Points -Set $keys -Seconds $Seconds -Minutes 1 -WaitMinutes 45 -Save $start
        $o | Where-Object { "$_" -cnotmatch '^(RUN DONE|GAVE UP)' }
        if (Test-BwvrVerdict $o 'RUN DONE') {
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
exit 0
