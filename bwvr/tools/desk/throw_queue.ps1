# Desktop runs of what a thrown object does after it lands. Each run goes through aim_run.ps1, which
# waits for an idle PC and no game, and then through desk_run.ps1.
#
#   throw_queue.ps1 -Dll <dll> -Runs base,land -Tag 1
param(
    [Parameter(Mandatory = $true)][string]$Dll,
    [string[]]$Runs = @('base'),
    [string]$Tag = '',
    [int]$Minutes = 170,
    [string]$More = ''
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
$common = 'fake_controller_play_frame=2750;intro_skip_test_frame=5;throw_log=2'
# The first two points are the points of the start dialog. aim_run.ps1 puts
# them in for the current render size.
$points = '0,0,100 0,0,100 -10,-10,60 -10,-4,66 -10,-10,60 -10,-4,66'
# A far fetch of the rock at the pen and a throw of 2.6 m/s onto the slope.
# Then, after the rock is at rest, a second fetch and a second throw.
$two = 'point@2990+1 aimfar@3000+1 shot@3050+1 grip@3100+101 pull@3140+3 shot@3185+1 point@3200+1 point@4500+1 aimfar@4510+1 grip@4600+101 pull@4640+3 point@4700+1 shot@5900+1'
$defs = @{
    # The plain landing (throw_land=0).
    base = @{ Set = 'throw_land=0'; Steps = $two; Seconds = 112 }
    # The landing of the mod, with its defaults.
    land = @{ Set = 'throw_land=1'; Steps = $two; Seconds = 112 }
    # A throw right after a far fetch: the grip opens while the object still
    # comes to the hand.
    early = @{ Set = 'throw_land=1'; Steps = 'point@2990+1 aimfar@3000+1 grip@3100+60 pull@3140+3 point@3159+1 shot@3400+1'; Seconds = 96 }
    # A tree: the far fetch takes trees too.
    tree = @{ Set = 'throw_land=1;far_test_no_trees=0'; Steps = $two; Seconds = 112 }
}
$Runs = @($Runs | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne '' })
foreach ($run in $Runs) {
    $d = $defs[$run]
    if ($null -eq $d) { "no run named $run"; continue }
    $name = "t$run$Tag"
    $set = "$common;$($d.Set)"
    if ($More -ne '') { $set = "$set;$More" }
    "=== $name $(Get-Date -Format s)"
    $o = & (Join-Path $sp 'aim_run.ps1') -Name $name -Dll $Dll -Set $set -Steps $d.Steps -Points $points -Seconds $d.Seconds -Minutes $Minutes
    $o
    if (($o -join "`n") -notmatch 'RUN DONE') { "QUEUE STOPPED at $name"; exit 2 }
}
"QUEUE DONE $(Get-Date -Format s)"
