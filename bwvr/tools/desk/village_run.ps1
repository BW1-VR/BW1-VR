# Desktop runs of the agent "village": the totem of the Village Centre and the
# miracle icons of the Worship Site. See bwvr/docs/hand.md, "The village with
# the real hand".
#
#   village_run.ps1 -Name vilD -Dll <dll> -Kind miracle
#   village_run.ps1 -Name vilE -Dll <dll> -Kind totem -Set "totem_hand=0"
#
# The start is the choice "Keep your old Creature" (spawn_run.ps1): no story,
# and a town of the player with no Worship Site. The test keys of village.cpp
# make the rest with the game's own functions:
#   village_test_site       the frame at which the town gets its Worship Site
#   village_test_miracles   the magic types that the town knows (22 Water, 23
#                           its power up, 14 Food, 15 its power up, 10 Heal)
#   village_test_chants     amount@frame: the pool of chants of the site
#   village_test_census     frames at which the log gets the towns and icons
#   village_test_icon       the seed type of the icon for the place "icon"
# The test key seek_plan gives the places of the word "seek": gosite, icon,
# gototem, totem, up, down (hand_direct.cpp).
#
# -Kind totem:   the grip closes on the totem, the hand goes 3 steps of 4 cm
#                up and 5 steps down, the grip opens.
# -Kind miracle: a tap on the icon of Water, the pour, the power up with the
#                spiral (B and the word "trace"), a tap of B (the cancel), the
#                choice with the gestures (the spiral and the W shape), and a
#                shake.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dll,
    [string]$Kind = 'miracle',
    [string]$Set = '',
    [string]$Steps = '',
    [string]$Plan = '',
    [string]$Chants = '20000@950',
    [string]$Census = '',
    [string]$Miracles = '22,23,14,15,10',
    [int]$Icon = 9,
    [string]$Gestures = '64 64 48',
    [int]$Seconds = 0,
    [string]$Go = '1860,2580|1840,2600|1880,2610|1850,2670|1790,2650|1900,2660',
    [int]$Lines = 160
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
$keys = 'far_test_influence=1;grab_hand=1;far_grab_max_m=0'
$points = '0,-30,60 14,-32,56 -12,-31,58 10,-30,62 -6,-32,54 4,-31,64 0,-30,60 12,-31,58'
$pre = 'stick_up@800+10 stick_up@830+10 look@860+5000 point@900+1 '
if ($Kind -eq 'totem') {
    if ($Plan -eq '') { $Plan = 'gototem,totem,up,up,up,down,down,down,down,down' }
    if ($Census -eq '') { $Census = '890,1400' }
    if ($Seconds -eq 0) { $Seconds = 60 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 seek@1000+1 grip@1040+280 seek@1090+1 seek@1120+1 seek@1150+1 ' +
                 'seek@1180+1 seek@1210+1 seek@1240+1 seek@1270+1 seek@1300+1 shot@1390+1'
    }
} elseif ($Kind -eq 'charge') {
    # An icon that charges for more than one game turn (3000 of 5000 chants),
    # the tap that stops it, a full pool, a tap, and a press with the miracle
    # at its icon (the miracle goes back).
    if ($Plan -eq '') { $Plan = 'gosite,icon' }
    if ($Census -eq '') { $Census = '890,1100,1260,1500' }
    if ($Seconds -eq 0) { $Seconds = 65 }
    if ($Chants -eq '20000@950') { $Chants = '3000@950 20000@1300' }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 seek@1000+1 grip@1040+3 grip@1200+3 grip@1340+3 grip@1440+3 shot@1560+1'
    }
    $keys = "$keys;village_test_icon=$Icon"
} elseif ($Kind -eq 'store') {
    # The Village Store: the grip collects from a pile, opens (the bundle
    # stays), the hand goes 8 cm up and then over the point of the store, and
    # the grip closes and opens (the bundle goes into the store).
    if ($Plan -eq '') { $Plan = 'gostore,store,pile,up,up,store' }
    if ($Census -eq '') { $Census = '890' }
    if ($Seconds -eq 0) { $Seconds = 62 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 seek@1000+1 seek@1040+1 grip@1080+120 seek@1230+1 seek@1250+1 ' +
                 'seek@1280+1 grip@1330+40 shot@1450+1'
    }
} elseif ($Kind -eq 'job') {
    # A villager near the Village Store: the grip takes him, the hand goes to
    # the point of the store and the grip opens. The log has his job.
    if ($Plan -eq '') { $Plan = 'gostore,store,villager,store' }
    if ($Census -eq '') { $Census = '890' }
    if ($Seconds -eq 0) { $Seconds = 62 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 seek@1000+1 seek@1040+1 grip@1046+260 seek@1180+1 shot@1420+1'
    }
} elseif ($Kind -eq 'probe') {
    # One scaffold in the Hand over six places of the test key seek_go: the
    # log says where the game has a place for a building.
    if ($Plan -eq '') { $Plan = 'scaffold,go,go,go,go,go,go' }
    if ($Census -eq '') { $Census = '960,1680' }
    if ($Seconds -eq 0) { $Seconds = 68 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@1000+1 grip@1040+580 seek@1100+1 seek@1180+1 seek@1260+1 seek@1340+1 ' +
                 'seek@1420+1 seek@1500+1 shot@1720+1'
    }
    $keys = "$keys;village_test_scaffolds=1@930;seek_go=$Go"
} elseif ($Kind -eq 'workshop') {
    # A Workshop on the land under the Hand, and then wood from the pile of
    # the Village Store that the hand carries to the Workshop.
    if ($Plan -eq '') { $Plan = 'gostore,store,pile,goshop,shop' }
    if ($Census -eq '') { $Census = '915,1600' }
    if ($Seconds -eq 0) { $Seconds = 66 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 seek@1000+1 seek@1040+1 grip@1080+120 seek@1240+1 seek@1320+1 ' +
                 'grip@1400+40 shot@1640+1'
    }
    $keys = "$keys;village_test_workshop=905"
} elseif ($Kind -eq 'site') {
    # One scaffold: the grip takes it, the God view goes to a place where the
    # game builds (seek_go), and the grip opens: a building site. Then wood
    # from the pile of the Village Store goes to the planted scaffold.
    if ($Plan -eq '') { $Plan = 'scaffold,go,gostore,store,pile,go,scaffold' }
    if ($Census -eq '') { $Census = '960,1240,1800' }
    if ($Seconds -eq 0) { $Seconds = 72 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@1000+1 grip@1040+160 seek@1100+1 seek@1260+1 seek@1320+1 seek@1360+1 ' +
                 'grip@1400+120 seek@1560+1 seek@1640+1 grip@1700+40 shot@1840+1'
    }
    $keys = "$keys;village_test_scaffolds=1@930;seek_go=1860,2580|1860,2580"
} elseif ($Kind -eq 'build') {
    # One scaffold on the land under the Hand: the grip takes it and opens at
    # the same place.
    if ($Plan -eq '') { $Plan = 'scaffold' }
    if ($Census -eq '') { $Census = '960,1300,1500' }
    if ($Seconds -eq 0) { $Seconds = 60 }
    if ($Steps -eq '') { $Steps = $pre + 'seek@1000+1 grip@1040+160 shot@1420+1' }
    $keys = "$keys;village_test_scaffolds=1@930"
} elseif ($Kind -eq 'scaffold') {
    # Two scaffolds on the land under the Hand. The grip takes the first and
    # opens over the second (they join), a tap takes the result apart, and
    # the grip takes one and opens over the land (the building site).
    if ($Plan -eq '') { $Plan = 'scaffold,scaffold,scaffold,scaffold' }
    if ($Census -eq '') { $Census = '960,1320,1460,1800' }
    if ($Seconds -eq 0) { $Seconds = 70 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@1000+1 grip@1040+220 seek@1150+1 seek@1360+1 grip@1400+3 ' +
                 'seek@1500+1 grip@1540+200 point@1620+1 shot@1850+1'
    }
    $keys = "$keys;village_test_scaffolds=2@930"
} else {
    if ($Plan -eq '') { $Plan = 'gosite,icon' }
    if ($Census -eq '') { $Census = '890,1010,1500,1600,1700,2460,2600' }
    if ($Seconds -eq 0) { $Seconds = 100 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 seek@1000+1 grip@1040+3 point@1100+1 ' +
                 'trace@1150+320 b@1156+300 point@1505+1 grip@1520+60 b@1650+5 ' +
                 'trace@1750+320 b@1756+300 trace@2100+320 b@2106+300 ' +
                 'pull@2500+1 pull@2506+1 pull@2512+1 pull@2518+1 pull@2524+1 pull@2530+1 shot@2650+1'
    }
    $keys = "$keys;fake_controller_gestures=$Gestures;village_test_icon=$Icon"
}
$keys = "$keys;seek_plan=$Plan;village_test_chants=$Chants;village_test_census=$Census;village_test_site=870;village_test_miracles=$Miracles"
if ($Set -ne '') { $keys = "$keys;$Set" }
& (Join-Path $sp 'spawn_run.ps1') -Name $Name -Dll $Dll -Choice keep -Steps $Steps `
    -Points $points -Set $keys -Seconds $Seconds
$log = Join-Path $sp "desk-$Name\bwvr.log"
if (-not (Test-Path $log)) { "no log at $log"; exit 1 }
$pattern = '^village      :|^miracle      : (?!miracle_)|^hand pick    : seek plan|^hand bulk    :|^hand press   : the grip pressed|' +
           '^haptics      : (effect (3|14|15|18|19|20|21|23|24|25|26|27|28)|cast|refused)|' +
           '^hand         : state|^gesture      : (the draw|the game)|^controllers  : the right button press started off|^grip         :|^hand (throw|drop|hold|take) *:'
$cards = @{}
Get-Content $log | Where-Object { $_ -match $pattern -or $_ -match '^hint card    : frame \d+, the card of the right hand: (.*?)\. \d' } | ForEach-Object {
    if ($_ -match '^hint card    : frame (\d+), the card of the right hand: (.*?)\. \d') {
        # One line for each new text of the card.
        $text = $Matches[2] -replace '87\d\d Belief Left', 'N Belief Left'
        if (-not $cards.ContainsKey($text)) { $cards[$text] = 1; "card @$($Matches[1]): $text" }
    } else {
        $_.Substring(0, [Math]::Min(440, $_.Length))
    }
} | Select-Object -First $Lines
