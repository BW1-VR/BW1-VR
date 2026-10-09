# The points of the scripted controller for the start dialog of a new game,
# for the current render size. Dot-source this file.
#
# The start dialog has one size in pixels and sits at the middle of the
# render. The scripted controller gets its places in whole cm from the head.
# How many pixels one cm is depends on the render size, and the render size
# is the size that the launcher took from the headset. So fixed points fit
# one render size only.
#
# So one short run measures each new render size: the hand points at three
# places in each of its two poses, the log of the mod gives the pixel of each
# place, and that gives the pixels for each cm. The result is kept in
# local\dialog-calibration.json at the root of the repo. A run reads the
# render size (a value of the game in the registry, which a test only reads)
# and takes its points from that file.

# Where the controls of the dialog are, in pixels from the middle of the
# render. Measured at 2560 by 1440 and at 2880 by 2880: the same in both.
$script:DialogRowY = @{ normal = -64; select = -20; skip = 23; keep = 67 }
$script:DialogRowX = -143
$script:DialogOkX = -147
$script:DialogOkY = 113

# The six places of the measure run, in cm: three for the first pose (it
# points at a row), then three for the second pose (it points at OK).
$script:DialogSamples = @(
    @(-16, 2), @(-22, 2), @(-16, -10),
    @(-17, -13), @(-23, -13), @(-17, -1)
)
$script:DialogSampleFrames = @(400, 490, 580, 680, 770, 860)

function Get-BwvrRenderSize {
    $setup = Get-ItemProperty 'HKCU:\Software\Lionhead Studios Ltd\Black & White\BWSetup' -ErrorAction SilentlyContinue
    if (-not $setup -or -not $setup.ScreenW -or -not $setup.ScreenH) { return $null }
    return @([int]$setup.ScreenW, [int]$setup.ScreenH)
}

function Get-DialogCalibrationFile {
    $repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
    return (Join-Path $repo 'local\dialog-calibration.json')
}

# The measure of the render size of now, or null when there is none yet.
function Get-DialogCalibration {
    $size = Get-BwvrRenderSize
    if (-not $size) { return $null }
    $file = Get-DialogCalibrationFile
    if (-not (Test-Path -LiteralPath $file)) { return $null }
    $all = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    $entry = $all.PSObject.Properties["$($size[0])x$($size[1])"]
    if (-not $entry) { return $null }
    return $entry.Value
}

# The place in whole cm at which a pose puts the pointer on a pixel.
function ConvertTo-DialogPoint($Pose, [double]$PixelX, [double]$PixelY) {
    $dx = $PixelX - $Pose.apx
    $dy = $PixelY - $Pose.apy
    $det = $Pose.cxx * $Pose.cyy - $Pose.cyx * $Pose.cxy
    $x = $Pose.ax + ($dx * $Pose.cyy - $Pose.cyx * $dy) / $det
    $y = $Pose.ay + ($Pose.cxx * $dy - $dx * $Pose.cxy) / $det
    return ('{0},{1},100' -f [int][Math]::Round($x), [int][Math]::Round($y))
}

# The two points that close the start dialog with one choice: the check box
# of the row (empty for "normal", which needs no click on a row) and OK.
# Null when the render size is not measured, or the choice is not known.
function Get-DialogPoints([string]$Choice) {
    if (-not $script:DialogRowY.ContainsKey($Choice)) { return $null }
    $cal = Get-DialogCalibration
    $size = Get-BwvrRenderSize
    if (-not $cal -or -not $size) { return $null }
    $mx = $size[0] / 2.0
    $my = $size[1] / 2.0
    $row = ''
    if ($Choice -ne 'normal') {
        $row = ConvertTo-DialogPoint $cal.pose1 ($mx + $script:DialogRowX) ($my + $script:DialogRowY[$Choice])
    }
    $ok = ConvertTo-DialogPoint $cal.pose2 ($mx + $script:DialogOkX) ($my + $script:DialogOkY)
    return @{ Row = $row; Ok = $ok }
}

# One short run that measures the current render size. It goes through
# desk_run.ps1, so the idle wait, the lock and the guards are the same as for
# each other run. True when the measure is in the file after it.
function Invoke-DialogCalibration([string]$Dll) {
    $size = Get-BwvrRenderSize
    if (-not $size) { 'the game has no display size yet'; return $false }
    $points = ($script:DialogSamples | ForEach-Object { "$($_[0]),$($_[1]),100" }) -join ' '
    $f = $script:DialogSampleFrames
    $words = "point@$($f[0])+1 point@$($f[1])+1 point@$($f[2])+1 pose2@$($f[3] - 10)+1 point@$($f[3])+1 point@$($f[4])+1 point@$($f[5])+1"
    $extra = @{
        play_mode = 4; fake_controller = 2; controller_hand = 1; intro_vr = 0
        intro_skip_test_frame = 5; fake_controller_front = 1
        fake_controller_yaw = 8; fake_controller_pitch = 1
        fake_controller_sweep_yaw = 8; fake_controller_sweep_pitch = -6
        test_keys = 1; depth24 = 1
        fake_controller_points = $points
        fake_controller_script = $words
    }
    "the start dialog is not measured for a render of $($size[0]) by $($size[1]) yet: one run of 45 s measures it"
    $o = & (Join-Path $PSScriptRoot 'desk_run.ps1') -Name 'dialogcal' -Dll $Dll -Extra $extra -Seconds 45 -WaitMinutes 30
    if (($o -join "`n") -match 'NOT RUN') { $o | Select-Object -Last 1; return $false }
    $log = Join-Path $PSScriptRoot 'desk-dialogcal\bwvr.log'
    if (-not (Test-Path -LiteralPath $log)) { 'the measure run left no log'; return $false }
    $seen = @(Select-String -LiteralPath $log -Pattern 'the pixel is ([0-9.]+) ([0-9.]+) \(in the view\), frame (\d+)' | ForEach-Object {
        @{ X = [double]$_.Matches[0].Groups[1].Value; Y = [double]$_.Matches[0].Groups[2].Value; Frame = [int]$_.Matches[0].Groups[3].Value }
    })
    $px = @()
    for ($i = 0; $i -lt 6; $i++) {
        $hit = $seen | Where-Object { $_.Frame -ge $f[$i] + 10 -and $_.Frame -le $f[$i] + 85 } | Select-Object -Last 1
        if (-not $hit) { "the measure run has no pointer line for the place $($i + 1) (frames $($f[$i] + 10) to $($f[$i] + 85)). Its log is $log"; return $false }
        $px += , @($hit.X, $hit.Y)
    }
    $poses = @{}
    foreach ($p in 0, 1) {
        $a = $script:DialogSamples[$p * 3]; $b = $script:DialogSamples[$p * 3 + 1]; $c = $script:DialogSamples[$p * 3 + 2]
        $pa = $px[$p * 3]; $pb = $px[$p * 3 + 1]; $pc = $px[$p * 3 + 2]
        $pose = [ordered]@{
            ax = $a[0]; ay = $a[1]; apx = $pa[0]; apy = $pa[1]
            cxx = ($pb[0] - $pa[0]) / ($b[0] - $a[0]); cxy = ($pb[1] - $pa[1]) / ($b[0] - $a[0])
            cyx = ($pc[0] - $pa[0]) / ($c[1] - $a[1]); cyy = ($pc[1] - $pa[1]) / ($c[1] - $a[1])
        }
        if ([Math]::Abs($pose.cxx) -lt 1 -or [Math]::Abs($pose.cyy) -lt 1) {
            "the measure run gave no clear motion of the pointer in pose $($p + 1) ($([Math]::Round($pose.cxx, 2)) and $([Math]::Round($pose.cyy, 2)) pixels for each cm). Its log is $log"
            return $false
        }
        $poses["pose$($p + 1)"] = $pose
    }
    $file = Get-DialogCalibrationFile
    $all = [ordered]@{}
    if (Test-Path -LiteralPath $file) {
        (Get-Content -LiteralPath $file -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $all[$_.Name] = $_.Value }
    }
    $all["$($size[0])x$($size[1])"] = [ordered]@{ pose1 = $poses.pose1; pose2 = $poses.pose2; made = (Get-Date -Format s) }
    [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $file))
    $all | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $file -Encoding ascii
    "measured $($size[0]) by $($size[1]): one cm of the hand is $([Math]::Round($poses.pose1.cxx, 2)) pixels across and $([Math]::Round(-$poses.pose1.cyy, 2)) pixels up"
    return $true
}

# True when the current render size is measured. Makes the measure run when it
# is not. The lines that the run prints go to the host, not to the result.
function Confirm-DialogCalibration([string]$Dll) {
    if (Get-DialogCalibration) { return $true }
    $out = @(Invoke-DialogCalibration $Dll)
    $out | Where-Object { $_ -is [string] } | ForEach-Object { Write-Host $_ }
    return [bool]($out | Where-Object { $_ -is [bool] } | Select-Object -Last 1)
}
