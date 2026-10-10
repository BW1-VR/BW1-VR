# One desktop run in which the game ends by itself, and the check of its end.
#
#   exit_run.ps1 -Name exit1 -Dll <build>\src\mod\Release\ddraw.dll
#   exit_run.ps1 -Name exit0 -Dll <...> -Old        (exit_shutdown=0)
#   exit_run.ps1 -Name exit2 -Dll <...> -NoBridge   (no shared textures)
#
# The mod asks the game to close its window at -Frame (the test key
# exit_test_frame, docs/backend.md, "The end of the mod"). The game goes its
# usual exit path, and desk_test.ps1 stops it only at the time limit
# -Seconds. From that frame the mod drops each write of the game to the
# registry, and desk_run.ps1 puts the files of the profile back.
#
# The check has three parts: the game ended by itself, Windows recorded no
# fault of runblack.exe in the run, and the log of the mod ends with the
# line "the mod is shut down". The last line of the output is EXIT CLEAN or
# EXIT FAULT.
#
# By default the run has capture_every_seconds=10. The self test of the GPU
# bridge then makes the shared textures, as a headset session does. The
# fault of exit_shutdown=0 needs them.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [switch]$Old,
    [switch]$NoBridge,
    [int]$Frame = 3600,
    [int]$Seconds = 150,
    [int]$WaitMinutes = 30,
    [hashtable]$Extra = @{}
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
$keys = @{ exit_shutdown = $(if ($Old) { 0 } else { 1 }); exit_test_frame = $Frame }
if (-not $NoBridge) { $keys['capture_every_seconds'] = 10 }
foreach ($k in $Extra.Keys) { $keys[$k] = $Extra[$k] }

$started = Get-Date
$out = & (Join-Path $sp 'desk_run.ps1') -Name $Name -Dll $Dll -Extra $keys -Seconds $Seconds -WaitMinutes $WaitMinutes 2>&1
$out | Where-Object { "$_" -notmatch '^desk watch' }
if (Test-BwvrVerdict $out 'NOT RUN') { 'EXIT NOT RUN'; exit 3 }
$self = Test-BwvrVerdict $out 'game exited on its own: True'

# Windows writes the record of a fault some seconds after the fault.
Start-Sleep -Seconds 10
$faults = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; ProviderName = 'Application Error'; StartTime = $started } -ErrorAction SilentlyContinue |
    Where-Object { $_.Message -match 'runblack' } | Sort-Object TimeCreated)
foreach ($f in $faults) {
    $module = if ($f.Message -match 'Faulting module name:\s*([^,]+)') { $Matches[1] } else { '?' }
    $code = if ($f.Message -match 'Exception code:\s*(\S+)') { $Matches[1] } else { '?' }
    "fault of Windows: $($f.TimeCreated.ToString('HH:mm:ss')) $module $code"
}

$log = Join-Path $sp ("desk-" + $Name + "\bwvr.log")
$last = ''
if (Test-Path -LiteralPath $log) {
    Select-String -LiteralPath $log -Pattern '^(exit test|shutdown)    ' |
        ForEach-Object { $_.Line.Substring(0, [Math]::Min(200, $_.Line.Length)) }
    $last = "$(Get-Content -LiteralPath $log -Tail 1)"
}
$whole = $last -match 'the mod is shut down'
"the game ended by itself: $self. Faults of Windows: $($faults.Count). The log has its last line: $whole"
if ($self -and $faults.Count -eq 0 -and $whole) { 'EXIT CLEAN'; exit 0 }
'EXIT FAULT'
exit 1
