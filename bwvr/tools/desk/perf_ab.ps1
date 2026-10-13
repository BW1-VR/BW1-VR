# Desktop runs of one ddraw.dll with some sets of ini keys, and a table of
# the numbers of each run (README.md, "A comparison of sets of keys").
#
#   perf_ab.ps1 -Name t1 -Dll <build>\src\mod\Release\ddraw.dll -Configs @(
#       @{ name = 'off'; keys = @{ render_thread = 0 } },
#       @{ name = 'on';  keys = @{ render_thread = 1 } }) -Repeat 2
#
# Each run gets the -Base keys, then the keys of its set. Each run goes
# through desk_run.ps1, with its lock, its wait and its guards. For each run
# the table has the median of the windows of 450 frames, with no first
# window (the load):
#   Frame     the frame time of the game, ms
#   Scene     the scene walks on the frame thread, ms (profile_sample)
#   Busy      the render thread busy, ms (render_thread)
#   Gpu       the scene walks on the GPU, ms (profile_gpu)
#   GpuDone   the time from the end of a walk until the GPU is done, ms
#             (profile_gpu)
#   Same      the calls of a frame that set the value that the device had
#   Draws     the draws of a frame in the queue of the render thread
#   BufDraws  the draws from the buffers of the render thread
#   Locks     the locks of those buffers in a frame
#   Failed    the lines of the log with calls that failed
#   Windows   the windows that the medians have
# -Pictures adds render_thread_test_frame=3000. Then only the windows before
# frame 3000 count, and the table also has the percent of the pixels that
# differ between frames 3000 and 3001 (one mode, PicSame) and between frames
# 3001 and 3002 (the mode of the ini and mode 0, PicModes), left eye.
# The table is also in desk-perf-<Name>\summary.txt.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [Parameter(Mandatory = $true)][object[]]$Configs,
    [hashtable]$Base = @{ one_walk = 1; fake_controller = 1; test_keys = 0; profile_sample = 250; profile_gpu = 1 },
    [int]$Seconds = 90,
    [int]$Repeat = 1,
    [switch]$Pictures
)
$ErrorActionPreference = 'Stop'
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not (Test-Path -LiteralPath $Dll)) { "NOT RUN: no DLL at $Dll"; exit 6 }
$outDir = Join-Path $sp "desk-perf-$Name"
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class PerfBmpDiff {
  // The percent of the pixels whose color differs.
  public static double Diff(string a, string b) {
    using (var x = new Bitmap(a)) using (var y = new Bitmap(b)) {
      if (x.Width != y.Width || x.Height != y.Height) return -1;
      var r = new Rectangle(0, 0, x.Width, x.Height);
      var dx = x.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
      var dy = y.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
      int n = dx.Stride * x.Height; var bx = new byte[n]; var by = new byte[n];
      Marshal.Copy(dx.Scan0, bx, 0, n); Marshal.Copy(dy.Scan0, by, 0, n);
      x.UnlockBits(dx); y.UnlockBits(dy);
      long diff = 0;
      for (int i = 0; i < n; i += 4) if (bx[i] != by[i] || bx[i+1] != by[i+1] || bx[i+2] != by[i+2]) diff++;
      return 100.0 * diff / (n / 4);
    }
  }
}
'@ -ErrorAction SilentlyContinue

function Get-Median([double[]]$v) {
    if ($v.Count -eq 0) { return [double]::NaN }
    $s = $v | Sort-Object
    $m = [int][Math]::Floor($s.Count / 2)
    if ($s.Count % 2) { return $s[$m] } else { return ($s[$m - 1] + $s[$m]) / 2 }
}

# The numbers of the windows of a log, with no first window.
function Read-PerfLog([string]$log) {
    $lines = [string[]](Get-Content -LiteralPath $log)
    # The test of the pictures turns the mode to 0 from its frame on.
    $cut = [Array]::FindIndex($lines, [Predicate[string]]{ param($l) $l -match 'render_thread_test_frame: frame' })
    if ($cut -gt 0) { $lines = $lines[0..($cut - 1)] }
    $pick = {
        param($pattern)
        $v = @()
        foreach ($l in $lines) { if ($l -match $pattern) { $v += [double]$matches[1] } }
        if ($v.Count -gt 1) { $v = $v[1..($v.Count - 1)] }
        return ,$v
    }
    [pscustomobject]@{
        Frame    = Get-Median (& $pick 'game had ([0-9.]+) ms as its frame time')
        Scene    = Get-Median (& $pick 'The scene walks were ([0-9.]+) ms in a frame')
        Busy     = Get-Median (& $pick 'render thread busy ([0-9.]+) ms')
        Gpu      = Get-Median (& $pick 'gpu time     : the scene walks took ([0-9.]+) ms')
        GpuDone  = Get-Median (& $pick 'the GPU was done ([0-9.]+) ms after')
        Same     = Get-Median (& $pick 'render_thread_filter=\d\. ([0-9]+) calls of a frame set')
        Draws    = Get-Median (& $pick 'records \(([0-9]+) draws\)')
        BufDraws = Get-Median (& $pick 'A frame: ([0-9]+) draws from the buffers')
        Locks    = Get-Median (& $pick 'DrawIndexedPrimitiveUP, ([0-9.]+) locks')
        Failed   = (($lines | Select-String -Pattern '([1-9][0-9]*) calls that failed').Count)
        Windows  = (& $pick 'game had ([0-9.]+) ms as its frame time').Count
    }
}

$hash = (Get-FileHash -LiteralPath $Dll).Hash.Substring(0, 12)
$rows = @()
for ($r = 1; $r -le $Repeat; ++$r) {
    foreach ($c in $Configs) {
        $keys = @{}
        foreach ($k in $Base.Keys) { $keys[$k] = $Base[$k] }
        foreach ($k in $c.keys.Keys) { $keys[$k] = $c.keys[$k] }
        if ($Pictures) { $keys['render_thread_test_frame'] = 3000 }
        $run = "perf-$Name-$($c.name)-$r"
        & (Join-Path $sp 'desk_run.ps1') -Name $run -Dll $Dll -Extra $keys -Seconds $Seconds | Out-Null
        $dir = Join-Path $sp "desk-$run"
        $log = Join-Path $dir 'bwvr.log'
        if (-not (Test-Path -LiteralPath $log)) { "NO LOG: $run"; continue }
        $row = Read-PerfLog $log
        $row | Add-Member -NotePropertyName Config -NotePropertyValue $c.name
        $row | Add-Member -NotePropertyName Run -NotePropertyValue $r
        $picSame = ''; $picModes = ''
        if ($Pictures -and (Test-Path -LiteralPath (Join-Path $dir 'bwvr-shared-left-03002.bmp'))) {
            $picSame = [Math]::Round([PerfBmpDiff]::Diff("$dir\bwvr-shared-left-03000.bmp", "$dir\bwvr-shared-left-03001.bmp"), 4)
            $picModes = [Math]::Round([PerfBmpDiff]::Diff("$dir\bwvr-shared-left-03001.bmp", "$dir\bwvr-shared-left-03002.bmp"), 4)
        }
        $row | Add-Member -NotePropertyName PicSame -NotePropertyValue $picSame
        $row | Add-Member -NotePropertyName PicModes -NotePropertyValue $picModes
        $rows += $row
    }
}
$table = $rows | Format-Table Config, Run, Frame, Scene, Busy, Gpu, GpuDone, Same, Draws, BufDraws, Locks, Failed, Windows, PicSame, PicModes -AutoSize | Out-String
$baseText = ($Base.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ' '
$text = "DLL $hash, $Seconds s a run, base keys: $baseText`n$table"
$text | Set-Content -LiteralPath (Join-Path $outDir 'summary.txt') -Encoding utf8
$text
