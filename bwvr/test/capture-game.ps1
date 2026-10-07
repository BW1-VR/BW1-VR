# Launch Black & White, wait for it to settle, capture its window, close it.
#
#   .\capture-game.ps1 -Out shot.png [-Wait 25] [-Offset "5 0 0"]
#
# -Offset writes bwvr-camera-offset.txt before launching, which arms the view
# matrix experiment in the probe. Omit it for a clean baseline.

param(
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Wait = 25,
    [string]$Offset = '',
    [string]$LogCopy = '',
    # Client-relative points to click before capturing, e.g. "648,291" for the
    # Continue Game button. Repeatable, separated by semicolons.
    [string]$Click = '',
    [int]$ClickWait = 12
)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(
        uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    public const uint LEFTDOWN = 0x0002, LEFTUP = 0x0004;
    public static void Click(int x, int y) {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(250);
        mouse_event(LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(80);
        mouse_event(LEFTUP, 0, 0, 0, UIntPtr.Zero);
    }
}
'@

# The game folder comes from local\game-path.txt, which dev.ps1 setup writes.
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$gameFile = Join-Path $repo 'local\game-path.txt'
if (-not (Test-Path -LiteralPath $gameFile)) {
    throw 'No game folder is set. Run dev.ps1 setup -Game <folder> first.'
}
$game = (Get-Content -LiteralPath $gameFile -TotalCount 1).Trim()
$built = Join-Path $repo 'bwvr\build\src\probe\Release\ddraw.dll'
$offsetFile = Join-Path $game 'bwvr-camera-offset.txt'
$logFile = Join-Path $game 'bwvr-probe.log'

Get-Process runblack -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item $offsetFile, $logFile -ErrorAction SilentlyContinue

# Always deploy the current build. Capturing against a stale DLL wastes a whole
# run, and the screenshot looks perfectly plausible either way.
if (-not (Test-Path $built)) { Write-Error "not built: $built"; exit 1 }
Copy-Item $built (Join-Path $game 'ddraw.dll') -Force
Write-Host ("deployed build from {0}" -f (Get-Item $built).LastWriteTime.ToString('HH:mm:ss'))

if ($Offset -ne '') {
    Set-Content -Path $offsetFile -Value $Offset -Encoding ASCII
    Write-Host "armed camera offset: $Offset"
} else {
    Write-Host 'baseline, no camera offset'
}

$proc = Start-Process -FilePath "$game\runblack.exe" -WorkingDirectory $game -PassThru
Start-Sleep -Seconds $Wait

$live = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
if (-not $live -or $live.MainWindowHandle -eq 0) {
    Write-Host 'no game window to capture'
    Get-Process runblack -ErrorAction SilentlyContinue | Stop-Process -Force
    exit 1
}

$handle = $live.MainWindowHandle
[Win]::ShowWindow($handle, 5) | Out-Null
[Win]::SetForegroundWindow($handle) | Out-Null
Start-Sleep -Seconds 2

$rect = New-Object Win+RECT
[Win]::GetWindowRect($handle, [ref]$rect) | Out-Null
$w = $rect.R - $rect.L
$h = $rect.B - $rect.T

# Drive past the menu so the capture shows real gameplay rather than whatever
# screen the game happened to be on.
if ($Click -ne '') {
    foreach ($point in $Click.Split(';')) {
        $xy = $point.Split(',')
        $sx = $rect.L + [int]$xy[0]
        $sy = $rect.T + [int]$xy[1]
        Write-Host "clicking client ($($xy[0]),$($xy[1])) = screen ($sx,$sy)"
        [Win]::Click($sx, $sy)
        Start-Sleep -Seconds 2
    }
    Write-Host "waiting ${ClickWait}s for the game to settle"
    Start-Sleep -Seconds $ClickWait
    [Win]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Seconds 1
}

$bmp = New-Object System.Drawing.Bitmap $w, $h
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($rect.L, $rect.T, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose()
$bmp.Dispose()
Write-Host "captured ${w}x${h} to $Out"

Get-Process runblack -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

if ($LogCopy -ne '' -and (Test-Path $logFile)) {
    Copy-Item $logFile $LogCopy -Force
    Write-Host "log copied to $LogCopy"
}
Remove-Item $offsetFile -ErrorAction SilentlyContinue
