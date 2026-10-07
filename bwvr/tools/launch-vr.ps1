# Start Black & White in VR.
#
# This does three small jobs that are easy to get wrong by hand:
#
# 1. It puts bwvr.ini back into VR mode if a measuring run left it in desktop
#    mode. The draw probe runs with vr=0 and stereo_headless=1, and forgetting
#    to change that back looks exactly like VR is broken.
# 2. It says which OpenXR runtime is registered for 32 bit programs. The game
#    is 32 bit, so a 64 bit only runtime cannot serve it.
# 3. It checks the two files the mod needs are actually beside the game.
#
# Settings you have edited by hand are kept. The file is only rewritten when
# it is missing or when it is still set up for measuring.

param(
  [switch]$NewGame,     # skip the front end and boot straight into land 1
  [switch]$Desktop      # run the two passes with no headset, for measuring
)

$ErrorActionPreference = "Stop"
$game = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "Black & White Game"
$exe  = Join-Path $game "runblack.exe"
$ini  = Join-Path $game "bwvr.ini"

if (-not (Test-Path $exe)) {
  Write-Host "Cannot find runblack.exe at $exe" -ForegroundColor Red
  exit 1
}

# --- the mod files ---
$missing = @()
foreach ($f in @("ddraw.dll", "openxr_loader.dll")) {
  if (-not (Test-Path (Join-Path $game $f))) { $missing += $f }
}
if ($missing.Count -gt 0) {
  Write-Host ("Missing from the game folder: " + ($missing -join ", ")) -ForegroundColor Red
  Write-Host "Build the mod first, then copy those two files beside runblack.exe."
  exit 1
}

# --- the runtime ---
$key = "HKLM:\SOFTWARE\WOW6432Node\Khronos\OpenXR\1"
if (Test-Path $key) {
  $runtime = (Get-ItemProperty -Path $key -ErrorAction SilentlyContinue).ActiveRuntime
  if ($runtime) {
    Write-Host "OpenXR runtime for 32 bit: $runtime" -ForegroundColor Gray
  } else {
    Write-Host "No 32 bit OpenXR runtime is registered. VR will not start." -ForegroundColor Yellow
  }
} else {
  Write-Host "No 32 bit OpenXR key at $key. VR will not start." -ForegroundColor Yellow
  Write-Host "Start Virtual Desktop and connect the headset, then try again."
}

# --- the settings ---
$wantsRewrite = $true
if (Test-Path $ini) {
  $text = Get-Content $ini -Raw
  $measuring = ($text -match "(?m)^\s*stereo_headless\s*=\s*1") -or
               ($text -match "(?m)^\s*vr\s*=\s*0") -or
               ($text -match "(?m)^\s*draw_probe\s*=\s*1")
  if (-not $measuring) { $wantsRewrite = $false }
  elseif (-not $Desktop) {
    Write-Host "bwvr.ini was left set up for measuring. Putting it back." -ForegroundColor Yellow
  }
}

if ($Desktop) {
  @"
[bwvr]
; dxwrapper puts Direct3D 7 onto Direct3D 9, so the finished frame reaches
; the headset without a copy through system memory. Empty means system
; DirectDraw, which needs that copy and cannot go above 720p.
backend=dxwrapper\ddraw.dll
gpu_bridge=1
vr=0
stereo=1
stereo_headless=1
draw_probe=1
draw_probe_delay=8000
draw_probe_frames=150
eye_separation_milli=500
screen_distance_cm=200
screen_width_cm=260
ui_shift_px=8
freeze_time=1
"@ | Set-Content -Path $ini -Encoding ascii
  Write-Host "Desktop measuring mode. The report lands in bwvr-draw-probe.txt." -ForegroundColor Cyan
} elseif ($wantsRewrite) {
  @"
[bwvr]
; dxwrapper puts Direct3D 7 onto Direct3D 9, so the finished frame reaches
; the headset without a copy through system memory. Empty means system
; DirectDraw, which needs that copy and cannot go above 720p.
backend=dxwrapper\ddraw.dll
gpu_bridge=1
vr=1
stereo=1
eye_separation_milli=500
screen_distance_cm=200
screen_width_cm=260
ui_shift_px=8
freeze_time=1
"@ | Set-Content -Path $ini -Encoding ascii
} else {
  Write-Host "Keeping your bwvr.ini." -ForegroundColor Gray
}

# --- go ---
Get-Process runblack -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item (Join-Path $game "bwvr.log") -ErrorAction SilentlyContinue

$args = @()
if ($NewGame) { $args += "-NEWGAME" }

Write-Host ""
Write-Host "Starting Black & White." -ForegroundColor Green
if (-not $NewGame) {
  Write-Host "It opens on the front end menu, so you can load a saved game."
}
Write-Host ""
Write-Host "While it runs:" -ForegroundColor Cyan
Write-Host "  F7         stereo off and on, to compare against flat"
Write-Host "  F8 / F9    less depth / more depth"
Write-Host "  Shift F8   the menus and subtitles further away"
Write-Host "  Shift F9   the menus and subtitles nearer"
Write-Host "  F10        back to the starting values"
Write-Host "  F6         measure this scene, then read bwvr-draw-probe.txt"
Write-Host ""
Write-Host "The mod writes bwvr.log beside the game." -ForegroundColor Gray

if ($args.Count -gt 0) {
  Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $game | Out-Null
} else {
  Start-Process -FilePath $exe -WorkingDirectory $game | Out-Null
}
