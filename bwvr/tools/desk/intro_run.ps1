# Waits for an idle PC (10 minutes, no game), then one desktop run of the
# Lionhead logo in stereo (docs/intro.md, "The intro in stereo"), through
# desk_run.ps1. Gives up after -Minutes.
#
#   intro_run.ps1 -Name in1 -Dll <dll>
#   intro_run.ps1 -Name in0 -Dll <dll> -Motion 0
#   intro_run.ps1 -Name in2 -Dll <dll> -Motion 2 -Set "intro_speed_pct=50" -Seconds 110
#
# The run does not skip the logos (intro_skip_test_frame=0), and it starts no
# scripted controller. The logo screens come first and are flat: they take
# about 9 s. Then the Lionhead logo plays in stereo for 33 s
# (intro_speed_pct=100). It ends about 46 s after the start of the game, so
# the run is 54 s long. Give -Seconds for a slower clock.
#
# -Motion is intro_motion: 0 the still camera, 1 the camera on the path of
#    the game with no turn, 2 the model in front of the player, 3 the camera
#    of the game with its turns.
# -Every is capture_every_seconds. Each -Every seconds the mod saves a picture
#    of each eye: desk-<Name>\bwvr-eye-left-1NNNNN.bmp and
#    bwvr-eye-right-1NNNNN.bmp. NNNNN is the stereo frame of the intro. The
#    log line "eye capture" before each pair has the number, and the line
#    "intro" nearest to it has the clock of the effect.
# -Set is a list of ini keys, "key=value;key=value".
#
# The run has intro_probe=1: one line of the log for each second of the clock
# of the effect, with the camera of the game, the camera of the mod and its
# heading. After the run the script prints the lines of the log that have
# the tag "intro".
#
# The last line of the output is RUN DONE (exit code 0) or GAVE UP (exit
# code 2).
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [int]$Motion = 1,
    [int]$Every = 4,
    [string]$Set = '',
    [int]$Seconds = 54,
    [int]$Minutes = 115,
    [int]$WaitMinutes = 45
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
$deadline = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $deadline) {
    $idle = [int](& (Join-Path $sp 'idle_seconds.ps1'))
    $running = [bool](Get-Process runblack -ErrorAction SilentlyContinue)
    if ($idle -ge 620 -and -not $running) {
        $extra = @{
            play_mode = 4; intro_vr = 1; intro_skip_test_frame = 0
            intro_motion = $Motion; intro_probe = 1; test_keys = 1
            capture_every_seconds = $Every; depth24 = 1
        }
        foreach ($pair in $Set.Split(';')) {
            if ($pair -match '^\s*([^=\s]+)\s*=\s*(.*)$') { $extra[$Matches[1]] = $Matches[2] }
        }
        $o = & (Join-Path $sp 'desk_run.ps1') -Name $Name -Dll $Dll -Extra $extra -Seconds $Seconds -WaitMinutes $WaitMinutes
        $o
        if (-not (Test-BwvrVerdict $o 'NOT RUN')) {
            $log = Join-Path $sp "desk-$Name\bwvr.log"
            if (Test-Path -LiteralPath $log) {
                Select-String -LiteralPath $log -Pattern '^(intro   |eye capture  : armed)' | ForEach-Object { $_.Line }
            }
            "RUN DONE"
            exit 0
        }
    }
    Start-Sleep -Seconds 30
}
"GAVE UP: the PC was not idle for 10 minutes in $Minutes minutes"
exit 2
