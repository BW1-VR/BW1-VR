# The frame cost measure on the desktop: one recipe, six still scenes, and
# the numbers of each scene from the log of the mod. See bwvr/docs/backend.md,
# "Frame cost", and README.md, "The frame cost measure".
#
#   bench_run.ps1 -Name b -Dll <dll> -Runs 3
#   bench_run.ps1 -Name ab -Dll <dll> -Dll2 <other dll> -Runs 3
#   bench_run.ps1 -Name k -Dll <dll> -Set "audio_stop_wait=1" -Set2 "audio_stop_wait=0" -Runs 3
#   bench_run.ps1 -Read b1,b2,b3 -Read2 ab-b1,ab-b2,ab-b3
#
# THE RUN. Each run starts in the test save "temple" (README.md, "A fast
# start"). That is Land 1 with a town, and the Creature of the profile at the
# door of the Temple. The scripted controller and the fake head put the view
# at six places. At each place nothing of the player moves for -Frames frames
# (600). The word "bench" of the script makes these frames one scene of the
# measure (profile=1, frame_profile.cpp):
#   1  The door of the Temple at the scale of the start (100 units for each
#      meter), with a level head: the land to the horizon.
#   2  The same place, with the head 30 degrees to the left and 20 degrees
#      down (the word "look"). The head stays so for the rest of the run.
#   3  The Village Store, two zoom steps in (44.4 units for each meter):
#      villagers, trees and buildings.
#   4  The same place with a tree in the Hand.
#   5  Five zoom steps out (337.5 units for each meter).
#   6  The widest zoom (4000 units for each meter).
# The villagers and the Creature move by themselves, and a game turn comes
# each 100 ms of game time. So two runs are not the same frame for frame.
# -Runs 3 gives the spread of the same DLL. A difference between two builds
# counts only when it is larger than that spread.
#
# THE TABLES. For each run: one row for each scene. "frame" is the whole
# frame of the frame loop of the game. "work" is the frame less the copy to
# the monitor: a headset session has no such copy. With more than one run the
# script prints the spread. With -Dll2 or -Set2 the runs go A, B, A, B, and
# the script prints the comparison. -Read and -Read2 make no run. They take
# the logs of runs that are there (the names of runs, or log files).
#
# -Set and -Set2 are lists of ini keys, "key=value;key=value". -ProfileLevel 2
# gives the measure with no timer on the Direct3D draw calls.
#
# The last line is BENCH DONE (exit code 0) or BENCH STOPPED (exit code 2).
param(
    [string]$Name = 'bench',
    [string]$Dll = '',
    [int]$Runs = 1,
    [string]$Set = '',
    [string]$Dll2 = '',
    [string]$Set2 = '',
    [int]$Frames = 600,
    [int]$ProfileLevel = 1,
    [int]$Seconds = 0,
    [int]$Minutes = 115,
    [string]$Save = 'temple',
    [string[]]$Read = @(),
    [string[]]$Read2 = @()
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
$inv = [cultureinfo]::InvariantCulture
$sceneNames = @('', 'start, level head', 'start, head down', 'village', 'tree in the Hand', '337 units a meter', 'widest zoom')

# THE RECIPE. The frames count from 800 as the frame at which the controller
# first places the Hand in the loaded game (fake_controller_play_frame). Each
# scene starts 40 frames or more after the last step before it, so each step
# is over, and it is $Frames frames long.
function Get-BenchRecipe([int]$Hold) {
    $f = 840
    $steps = @()
    $steps += "bench@$f+$Hold"; $f += $Hold + 20
    $steps += "look@$f+60000"; $f += 40
    $steps += "bench@$f+$Hold"; $f += $Hold + 20
    $steps += "stick_up@$f+10"; $f += 30
    $steps += "stick_up@$f+10"; $f += 30
    $steps += "point@$f+1"; $f += 20
    $steps += "seek@$f+1"; $f += 80
    $steps += "bench@$f+$Hold"; $f += $Hold + 20
    $steps += "seek@$f+1"; $f += 40
    $steps += "seek@$f+1"; $f += 6
    $steps += "grip@$f+$($Hold + 140)"; $f += 94
    $steps += "bench@$f+$Hold"; $f += $Hold + 60
    foreach ($i in 1..5) { $steps += "stick_down@$f+10"; $f += 30 }
    $f += 50
    $steps += "bench@$f+$Hold"; $f += $Hold + 20
    foreach ($i in 1..7) { $steps += "stick_down@$f+10"; $f += 30 }
    $f += 50
    $steps += "bench@$f+$Hold"; $f += $Hold
    return @{ Steps = ($steps -join ' '); Last = $f }
}

function ConvertTo-BenchNumber([string]$Text) { return [double]::Parse($Text, $inv) }

# "name value, name value" of a line of the log, as a table.
function ConvertFrom-BenchList([string]$Text) {
    $out = [ordered]@{}
    foreach ($item in ($Text -split ', ')) {
        $m = [regex]::Match($item.Trim(), '^(.*?) (-?[0-9.]+)$')
        if ($m.Success) { $out[$m.Groups[1].Value] = ConvertTo-BenchNumber $m.Groups[2].Value }
    }
    return $out
}

# The scenes of one run log: the numbers of each line "frame cost   : scene N".
function Get-BenchScenes([string]$Log) {
    $scenes = @{}
    $num = '(-?[0-9.]+)'
    foreach ($line in (Select-String -LiteralPath $Log -Pattern '^frame cost   : scene (\d+), (.*)$')) {
        $n = [int]$line.Matches[0].Groups[1].Value
        $rest = $line.Matches[0].Groups[2].Value
        if (-not $scenes.ContainsKey($n)) {
            $scenes[$n] = @{ Scene = $n; Parts = @{}; Inside = @{}; Slow = ''; Worst = ''; Callers = @() }
        }
        $s = $scenes[$n]
        $m = [regex]::Match($rest, "^(\d+) frames in $num s \($num fps\), frames (\d+) to (\d+) of the controllers\. Frame ms: mean $num, median $num, 99th percentile $num, worst $num\. Less the desktop copy: mean $num, median $num, 99th percentile $num, worst $num")
        if ($m.Success) {
            $g = $m.Groups
            $s.Frames = [int]$g[1].Value; $s.Seconds = ConvertTo-BenchNumber $g[2].Value; $s.Fps = ConvertTo-BenchNumber $g[3].Value
            $s.First = [int]$g[4].Value; $s.Last = [int]$g[5].Value
            $s.Mean = ConvertTo-BenchNumber $g[6].Value; $s.Median = ConvertTo-BenchNumber $g[7].Value
            $s.P99 = ConvertTo-BenchNumber $g[8].Value; $s.WorstMs = ConvertTo-BenchNumber $g[9].Value
            $s.WorkMean = ConvertTo-BenchNumber $g[10].Value; $s.WorkMedian = ConvertTo-BenchNumber $g[11].Value
            $s.WorkP99 = ConvertTo-BenchNumber $g[12].Value; $s.WorkWorst = ConvertTo-BenchNumber $g[13].Value
            continue
        }
        if ($rest -match '^mean ms of a frame by part: (.*)$') { $s.Parts = ConvertFrom-BenchList $Matches[1]; continue }
        if ($rest -match '^inside those parts, mean ms of a frame: (.*)$') { $s.Inside = ConvertFrom-BenchList $Matches[1]; continue }
        # The log has "1 game turn", "1 frame" and "1 slow frame" for a count of 1.
        $m = [regex]::Match($rest, "^(\d+) game turns? in (\d+) frames?\. The turn of such a frame, ms: mean $num, median $num, 99th percentile $num, worst $num\. A frame has (\d+) draw calls and (\d+) vertices")
        if ($m.Success) {
            $g = $m.Groups
            $s.Turns = [int]$g[1].Value; $s.TurnMean = ConvertTo-BenchNumber $g[3].Value; $s.TurnMedian = ConvertTo-BenchNumber $g[4].Value
            $s.TurnWorst = ConvertTo-BenchNumber $g[6].Value; $s.Draws = [int]$g[7].Value; $s.Vertices = [int]$g[8].Value
            continue
        }
        $m = [regex]::Match($rest, "^(\d+) slow frames? of \d+ \(over $num ms less the desktop copy\), (\d+) of them with a game turn\. The part that is the most over its median: (.*)$")
        if ($m.Success) {
            $s.SlowCount = [int]$m.Groups[1].Value; $s.SlowLimit = ConvertTo-BenchNumber $m.Groups[2].Value
            $s.Slow = $m.Groups[4].Value
            continue
        }
        if ($rest -match '^the worst frame is ') { $s.Worst = $rest; continue }
        if ($rest -match '^the callers of ') { $s.Callers += $rest; continue }
    }
    return @($scenes.Keys | Sort-Object | ForEach-Object { $scenes[$_] } | Where-Object { $_.ContainsKey('Mean') })
}

# The log of a run: the name of a run (desk-<name>\bwvr.log beside the
# scripts), or a file.
function Resolve-BenchLog([string]$Run) {
    if (Test-Path -LiteralPath $Run -PathType Leaf) { return (Resolve-Path -LiteralPath $Run).Path }
    return (Join-Path $sp "desk-$Run\bwvr.log")
}

function Get-BenchPart($Scene, [string]$Part) {
    if ($Scene.Parts.Contains($Part)) { return $Scene.Parts[$Part] }
    return 0.0
}
function Get-BenchInside($Scene, [string]$Part) {
    if ($Scene.Inside.Contains($Part)) { return $Scene.Inside[$Part] }
    return 0.0
}

# The numbers of a scene that the spread and the comparison use.
$metrics = [ordered]@{
    'frame mean'  = { param($s) $s.Mean }
    'work mean'   = { param($s) $s.WorkMean }
    'work median' = { param($s) $s.WorkMedian }
    'work p99'    = { param($s) $s.WorkP99 }
    'work worst'  = { param($s) $s.WorkWorst }
    'eye passes'  = { param($s) (Get-BenchPart $s 'eye 0') + (Get-BenchPart $s 'eye 1') }
    'game turn'   = { param($s) (Get-BenchPart $s 'game turn') }
    'draw calls'  = { param($s) $s.Draws }
}

function Format-BenchTable($Rows) {
    return (($Rows | Format-Table -Property * -AutoSize | Out-String -Width 400).Trim("`r", "`n") -split "`r?`n" | ForEach-Object { $_.TrimEnd() })
}

# The table of one run, and the slow frames of each scene. The scenes of the
# run are then in $script:benchScenes.
$script:benchScenes = @()
function Show-BenchRun([string]$Run, [string]$Log) {
    $script:benchScenes = @()
    if (-not (Test-Path -LiteralPath $Log)) { "run ${Run}: no log at $Log"; return }
    $size = Select-String -LiteralPath $Log -Pattern '^vr           : game renders at (\S+)' | Select-Object -First 1
    $build = Get-Content -LiteralPath $Log -TotalCount 1
    $level = Select-String -LiteralPath $Log -Pattern '^frame cost   : profile=(\d)' | Select-Object -First 1
    "=== run ${Run}: $build, the game renders $(if ($size) { $size.Matches[0].Groups[1].Value } else { 'at a size that the log does not have' }), profile=$(if ($level) { $level.Matches[0].Groups[1].Value } else { '0: the log has no measure' })"
    $scenes = @(Get-BenchScenes $Log)
    if ($scenes.Count -eq 0) { "run ${Run}: the log has no scene of the measure ($Log)"; return }
    $script:benchScenes = $scenes
    $rows = foreach ($s in $scenes) {
        [pscustomobject][ordered]@{
            scene    = "$($s.Scene) $($sceneNames[$s.Scene])"
            fps      = '{0:F1}' -f $s.Fps
            frame    = '{0:F2}' -f $s.Mean
            'work mean' = '{0:F2}' -f $s.WorkMean
            median   = '{0:F2}' -f $s.WorkMedian
            p99      = '{0:F2}' -f $s.WorkP99
            worst    = '{0:F2}' -f $s.WorkWorst
            turn     = '{0:F2}' -f (Get-BenchPart $s 'game turn')
            'eye 0'  = '{0:F2}' -f (Get-BenchPart $s 'eye 0')
            'eye 1'  = '{0:F2}' -f (Get-BenchPart $s 'eye 1')
            mod      = '{0:F2}' -f ((Get-BenchPart $s 'mod frame') + (Get-BenchInside $s 'pass functions'))
            copy     = '{0:F2}' -f (Get-BenchPart $s 'desktop copy')
            rest     = '{0:F2}' -f ((Get-BenchPart $s 'input') + (Get-BenchPart $s 'before the scene') + (Get-BenchPart $s 'after the scene') + (Get-BenchPart $s 'other') + (Get-BenchPart $s 'wait') + (Get-BenchPart $s 'to the headset'))
            D3D      = '{0:F2}' -f (Get-BenchInside $s 'Direct3D draw calls')
            draws    = $s.Draws
            turns    = $s.Turns
            slow     = $s.SlowCount
        }
    }
    'ms of a frame. "frame" is the mean of the whole frame. "work" is the frame less the copy to the monitor: its mean, median, 99th percentile and worst frame. Then the mean of each part: the game turn, each eye pass, the frame and pass functions of the mod, the copy to the monitor, the rest. "D3D" is the time in Direct3D draw calls, which is inside the eye passes.'
    Format-BenchTable $rows
    foreach ($s in $scenes) {
        "  scene $($s.Scene): $($s.SlowCount) slow frame$(if ($s.SlowCount -ne 1) { 's' }) (work over $('{0:F1}' -f $s.SlowLimit) ms). The cause: $($s.Slow). A game turn: mean $('{0:F2}' -f $s.TurnMean) ms, worst $('{0:F2}' -f $s.TurnWorst)"
    }
    if ($scenes.Count -lt 6) { "run ${Run}: the log has $($scenes.Count) of 6 scenes. The run ended before the last scene: give more -Seconds" }
}

# One row for each scene and number: the value of each run, the mean and the
# spread (the largest less the smallest).
function Get-BenchSpread($Group) {
    $rows = @()
    foreach ($n in 1..6) {
        foreach ($metric in $metrics.Keys) {
            $values = @()
            foreach ($run in $Group) {
                $s = $run.Scenes | Where-Object { $_.Scene -eq $n }
                if ($s) { $values += [double](& $metrics[$metric] $s) }
            }
            if ($values.Count -eq 0) { continue }
            $mean = ($values | Measure-Object -Average).Average
            $spread = ($values | Measure-Object -Maximum).Maximum - ($values | Measure-Object -Minimum).Minimum
            $rows += [pscustomobject]@{ Scene = $n; Metric = $metric; Values = $values; Mean = $mean; Spread = $spread }
        }
    }
    return $rows
}

function Show-BenchSpread([string]$Title, $Group) {
    "=== the spread of $Title ($($Group.Count) runs: $(($Group | ForEach-Object { $_.Name }) -join ', '))"
    'The spread is the largest value less the smallest one. A difference between two builds counts only when it is larger.'
    $rows = foreach ($r in (Get-BenchSpread $Group)) {
        $row = [ordered]@{ scene = "$($r.Scene) $($sceneNames[$r.Scene])"; number = $r.Metric }
        for ($i = 0; $i -lt $r.Values.Count; $i++) { $row["run $($i + 1)"] = '{0:F2}' -f $r.Values[$i] }
        $row['mean'] = '{0:F2}' -f $r.Mean
        $row['spread'] = '{0:F2}' -f $r.Spread
        $row['percent'] = if ($r.Mean -ne 0) { '{0:F1}' -f (100.0 * $r.Spread / $r.Mean) } else { '' }
        [pscustomobject]$row
    }
    Format-BenchTable $rows
}

# The result of a comparison. Some runs give only a rough spread, so a
# difference counts as clear only when it is more than two times the spread.
function Show-BenchCompare($A, $B) {
    "=== the comparison: A ($(($A | ForEach-Object { $_.Name }) -join ', ')) and B ($(($B | ForEach-Object { $_.Name }) -join ', '))"
    'The mean of the runs of A and of B, the difference (B less A), and the larger spread of the two. "clear": the difference is more than two times that spread. "near the spread": it is more than the spread. "in the spread": it is not more.'
    $sa = Get-BenchSpread $A
    $sb = Get-BenchSpread $B
    $rows = foreach ($ra in $sa) {
        $rb = $sb | Where-Object { $_.Scene -eq $ra.Scene -and $_.Metric -eq $ra.Metric }
        if (-not $rb) { continue }
        $diff = $rb.Mean - $ra.Mean
        $noise = [Math]::Max($ra.Spread, $rb.Spread)
        $known = $ra.Values.Count -gt 1 -and $rb.Values.Count -gt 1
        [pscustomobject][ordered]@{
            scene   = "$($ra.Scene) $($sceneNames[$ra.Scene])"
            number  = $ra.Metric
            A       = '{0:F2}' -f $ra.Mean
            B       = '{0:F2}' -f $rb.Mean
            'B - A' = '{0:+0.00;-0.00;0.00}' -f $diff
            percent = if ($ra.Mean -ne 0) { '{0:+0.0;-0.0;0.0}' -f (100.0 * $diff / $ra.Mean) } else { '' }
            spread  = if ($known) { '{0:F2}' -f $noise } else { 'one run' }
            result  = if (-not $known) { '' } elseif ([Math]::Abs($diff) -gt 2 * $noise) { 'clear' } elseif ([Math]::Abs($diff) -gt $noise) { 'near the spread' } else { 'in the spread' }
        }
    }
    Format-BenchTable $rows
}

$groupA = @()
$groupB = @()
if ($Read.Count -gt 0) {
    $Read = @($Read | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne '' })
    $Read2 = @($Read2 | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne '' })
    foreach ($run in $Read) {
        Show-BenchRun $run (Resolve-BenchLog $run)
        $groupA += @{ Name = $run; Scenes = $script:benchScenes }
    }
    foreach ($run in $Read2) {
        Show-BenchRun $run (Resolve-BenchLog $run)
        $groupB += @{ Name = $run; Scenes = $script:benchScenes }
    }
} else {
    if ($Dll -eq '') { $Dll = Get-BwvrBuiltDll }
    if (-not (Test-Path -LiteralPath $Dll)) { "NOT RUN: no DLL at $Dll"; 'BENCH STOPPED'; exit 2 }
    $two = $Dll2 -ne '' -or $Set2 -ne ''
    if ($Dll2 -eq '') { $Dll2 = $Dll }
    if (-not (Test-Path -LiteralPath $Dll2)) { "NOT RUN: no DLL at $Dll2"; 'BENCH STOPPED'; exit 2 }
    $start = Confirm-BwvrTestSave $Save $Dll $Minutes
    $recipe = Get-BenchRecipe $Frames
    # The zoom steps keep the land under the head in its place
    # (god_zoom_about=0), as in village_run.ps1. The three words "seek" take
    # the places of seek_plan. The God view goes to the Village Store. Then
    # the Hand's point goes over the store, and then to the base of the
    # nearest tree.
    $keys = "profile=$ProfileLevel;god_zoom_about=0;far_test_influence=1;grab_hand=1;far_grab_max_m=0;seek_plan=gostore,store,treebase"
    if ($start) { $keys = "fake_controller_play_frame=800;$keys" }
    if ($Seconds -le 0) {
        # The start of the run, the frames of the recipe at 45 frames a
        # second, and 6 s.
        $Seconds = 14 + [int][Math]::Ceiling(($recipe.Last - 800) / 45.0) + 6
        if (-not $start) { $Seconds += 10 }
    }
    $plan = @()
    foreach ($i in 1..$Runs) {
        if ($two) {
            $plan += @{ Name = "$Name-a$i"; Dll = $Dll; Set = $Set; Group = 'A' }
            $plan += @{ Name = "$Name-b$i"; Dll = $Dll2; Set = (@($Set, $Set2) | Where-Object { $_ -ne '' }) -join ';'; Group = 'B' }
        } else {
            $plan += @{ Name = "$Name$i"; Dll = $Dll; Set = $Set; Group = 'A' }
        }
    }
    "the frame cost measure: $($plan.Count) runs of $Seconds s, $Frames frames for each of the 6 scenes, $(if ($start) { "the test save '$start'" } else { 'a new game' })"
    if ($two) {
        "A: $Dll$(if ($Set -ne '') { " with $Set" })"
        "B: $Dll2$(if ($Set2 -ne '') { " with $Set2" })"
    }
    foreach ($p in $plan) {
        $ini = $keys
        if ($p.Set -ne '') { $ini = "$keys;$($p.Set)" }
        $o = & (Join-Path $sp 'spawn_run.ps1') -Name $p.Name -Dll $p.Dll -Choice keep -Steps $recipe.Steps `
            -Points '0,-30,60' -Set $ini -Seconds $Seconds -Minutes $Minutes -Save $start
        if (-not (Test-BwvrVerdict $o 'RUN DONE')) {
            $o | Select-Object -Last 3
            "BENCH STOPPED at $($p.Name)"
            exit 2
        }
        # The lines of desk_run.ps1 that a measure needs: the window in front,
        # the real cursor, a person at the PC, and the files that the run put
        # back.
        $o | ForEach-Object { "$_" -split "`r?`n" } | Where-Object { $_ -match '^desk watch|^THE REAL CURSOR|^the real cursor|A person used the PC|^ini restored|^installed dll put back|^profile put back|^Creature mind put back|^WARNING' } | ForEach-Object { "  $_" }
        Show-BenchRun $p.Name (Join-Path $sp "desk-$($p.Name)\bwvr.log")
        $done = @{ Name = $p.Name; Scenes = $script:benchScenes }
        if ($p.Group -eq 'A') { $groupA += $done } else { $groupB += $done }
    }
}
if ($groupA.Count -gt 1) { Show-BenchSpread 'A' $groupA }
if ($groupB.Count -gt 1) { Show-BenchSpread 'B' $groupB }
if ($groupA.Count -gt 0 -and $groupB.Count -gt 0) { Show-BenchCompare $groupA $groupB }
'BENCH DONE'
