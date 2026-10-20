# Desktop runs of the village: the totem of the Village Centre, the miracle
# icons of the Worship Site, the Village Store, a villager, scaffolds and the
# Workshop. See bwvr/docs/village.md, "The village with the real hand", and
# bwvr/docs/grab.md.
#
#   village_run.ps1 -Name vilD -Dll <dll> -Kind miracle
#   village_run.ps1 -Name vilE -Dll <dll> -Kind totem -Set "totem_hand=0"
#   village_run.ps1 -Name vilF -Dll <dll> -Kind miracle -Icon 3
#
#   village_run.ps1 -Name vilG -Dll <dll> -Kind store -NewGame
#
# THE START. The run starts in the test save "temple" (README.md, "A fast
# start"): no story, and a town of the player with no Worship Site. When the
# save is not there, one run makes it. The steps and the frames of the test
# keys count from frame 800 as the frame at which the controller first places
# the Hand in the loaded game (fake_controller_play_frame=800), so each frame
# in the log is about 730 less than the frame of the step. -NewGame gives a
# new game with the choice "Keep your old Creature" (spawn_run.ps1): the same
# state. -Save gives a different test save with that state. With no -Seconds,
# a run in a test save ends 4 s after its last step or census.
#
# The test keys of village.cpp make the rest with the game's own functions:
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
#                shake. -Icon gives the seed type of a different icon (3 is
#                Food, which pours as Water does).
# The other kinds (charge, store, job, tree, throw, probe, workshop, site,
# build, scaffold, fartake, fartotem, choice) have a comment at their steps
# below.
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
    [int]$Lines = 160,
    [string]$Save = 'temple',
    [switch]$NewGame
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $sp 'game.ps1')
$start = ''
if (-not $NewGame) { $start = Confirm-BwvrTestSave $Save $Dll }
# With no -Seconds, each kind gives the length of its run in a new game.
$given = $Seconds -gt 0
# The zoom steps of these recipes keep the land under the head in its place
# (god_zoom_about=0).
$keys = 'god_zoom_about=0;far_test_influence=1;grab_hand=1;far_grab_max_m=0'
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
} elseif ($Kind -eq 'fartotem') {
    # The totem from a distance (bwvr/docs/village.md, "The totem from a
    # distance"). The God view goes 60 cm of the room before the totem, the
    # hand goes near the body, and the word "aimfar" aims at the totem, the
    # nearest bulk source (seek_bulk=1). The grip stays closed for 400 frames.
    # The hand goes 3 steps of 4 cm up and 1 step down, and the grip opens.
    # The lines "far grab" and "village" show who has the totem and when the
    # Hand lets it go. With -Set "totem_take=0" the far grab lets the totem go
    # 700 ms after the press, with the grip still closed. The eye pictures
    # show the ghost of the pole while the grip holds it and after the
    # release (totem_ghost), and the height of the pole in the second after
    # the release (totem_hold).
    if ($Plan -eq '') { $Plan = 'gototem,up,up,up,down' }
    if ($Census -eq '') { $Census = '890' }
    if ($Seconds -eq 0) { $Seconds = 70 }
    $points = '0,-30,60 0,-10,20'
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 point@1000+1 aimfar@1040+1 grip@1080+400 ' +
                 'seek@1140+1 shot@1150+1 seek@1170+1 seek@1200+1 shot@1230+1 seek@1260+1 shot@1300+1 ' +
                 'shot@1500+1 shot@1530+1 shot@1560+1'
    }
    $keys = "$keys;seek_bulk=1;far_test_least_cm=30;far_grab_choice=0;far_touch=0"
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
} elseif ($Kind -eq 'tree') {
    # A tree near the Village Store: the grip takes it, the hand goes to the
    # point of the store and the grip opens. The store takes the tree as
    # wood.
    if ($Plan -eq '') { $Plan = 'gostore,store,treebase,store' }
    if ($Census -eq '') { $Census = '890' }
    if ($Seconds -eq 0) { $Seconds = 62 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 seek@1000+1 seek@1040+1 grip@1046+260 seek@1180+1 shot@1420+1'
    }
} elseif ($Kind -eq 'throw') {
    # A villager near the Village Store: the grip takes him, the hand steps
    # 15 cm toward the head, and the grip opens in that step: a throw of
    # about 2.6 m/s at 44 units for each meter.
    if ($Plan -eq '') { $Plan = 'gostore,store,villager' }
    if ($Census -eq '') { $Census = '890' }
    if ($Seconds -eq 0) { $Seconds = 62 }
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 seek@1000+1 seek@1040+1 grip@1046+100 pull@1143+3 shot@1200+1 shot@1420+1'
    }
    $keys = "$keys;throw_log=1"
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
} elseif ($Kind -eq 'fartake') {
    # A take at a distance from a pile of the Village Store (bwvr/docs/grab.md,
    # "A bulk source at a distance"). The God view goes to the store, and the
    # hand goes to the height of the head: the Hand's point on the store turns
    # the far grab off. The word "aimfar" aims at the nearest bulk source
    # (seek_bulk=1), and the grip stays closed for 1500 frames. The test key
    # far_test_take_gone=30 ends the take at its game turn 30 with the grip
    # closed, and tells the far grab that the source is gone, as for a pile
    # that the game deletes when it is used up. The lines "grip" and "hand
    # pick" then show who has the bundle: the grip carries it to the end of
    # the run, or with -Set "far_take_carry=0" the Hand lets it go at once.
    #
    # A take that the grip ends: -Set "far_test_take_gone=0" with
    #   -Steps "<the steps below with grip@1080+180 and shot@2400+1>"
    # The bundle then stays in the Hand with the grip open.
    #
    # The sound of the take: the lines "audio take" have the start and the
    # stop of the loop (sample 44 of the in game bank for food, 98 for wood),
    # or the refusal of the game with -Set "audio_take_loop=0". With
    # audio_trace=1 the lines "audio play" and "audio chan" have what the DLL
    # sent to QMixer. With audio_capture=1 the mod writes the stream to
    # bwvr-audio.wav in the game folder, and each line "audio take" ends with
    # the frame of that file. Take the file out of the game folder after the
    # run.
    #
    # The stream of the take: the lines "take stream" have its start (the
    # source, its top, the Hand, the length of the way) and its end (the
    # count of the pieces that the mod placed), or with -Set "take_stream=0"
    # that the pieces are on the line of the game. Eye pictures of a take
    # that the grip ends, with pictures while it runs:
    #   -Set "far_test_take_gone=0" -Steps "<the steps below with
    #   grip@1080+400 and shot@1140+1 shot@1180+1 shot@1220+1 shot@1300+1>"
    #
    # far_grab_choice=0 and far_touch=0: a villager or a scroll near the line
    # of the aim does not take the halo from the pile.
    if ($Plan -eq '') { $Plan = 'gostore' }
    if ($Census -eq '') { $Census = '890' }
    if ($Seconds -eq 0) { $Seconds = 80 }
    $points = '0,-30,60 0,0,45'
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 point@1000+1 aimfar@1040+1 shot@1070+1 grip@1080+1500 ' +
                 'shot@1200+1 shot@2500+1'
    }
    $keys = "$keys;seek_bulk=1;far_test_take_gone=30;far_grab_choice=0;far_touch=0"
} elseif ($Kind -eq 'choice') {
    # The choice of a far target (bwvr/docs/grab.md, "The cone and the
    # choice"). The God view goes to the Village Store. The word "aimfar"
    # takes the villager that has a tree nearest to him (far_test_aim_pair),
    # and each later word turns the aim one step along the line from his body
    # to the base of that tree, past the tree, and back
    # (far_test_aim_blends, in percent of the way). The log has each new
    # target with its reason, and the line of a target that keeps the halo.
    # Compare runs with -Set "far_grab_choice=1;far_grab_stay=0" (the bias),
    # with -Set "far_grab_stay=0" and with no -Set (the default). Add
    # far_test_quest=1 to the first one: the bias then does not know the
    # class of a villager, as it does not know a quest character. Add
    # far_touch=0 to each -Set: the village has scrolls near that line, and a
    # scroll is then no far target. far_test_aim_class=7 takes a quest
    # character in the place of a villager. In the test save "temple" no
    # quest character stands 20 units or less from a tree.
    #
    # The second point of the hand is at the height of the head. The Hand's
    # point on the Village Store turns the far grab off (near objects win).
    if ($Plan -eq '') { $Plan = 'gostore' }
    if ($Census -eq '') { $Census = '890' }
    if ($Seconds -eq 0) { $Seconds = 70 }
    $points = '0,-30,60 0,0,45'
    # A villager walks, so a step of the aim is 20 frames long.
    $blends = '0,30,60,90,120,160,200,240,200,160,120,90,60,30,0'
    if ($Steps -eq '') {
        $Steps = $pre + 'seek@920+1 point@1000+1 '
        $at = 1040
        foreach ($b in $blends.Split(',')) { $Steps += "aimfar@$at+1 "; $at += 20 }
        $Steps += "shot@$at+1"
    }
    $keys = "$keys;far_test_aim_class=1;far_test_aim_toward=1;far_test_aim_pair=1;far_test_aim_blends=$blends"
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
if ($start) {
    # In a test save the steps and the frames of the test keys count from the
    # frame at which the controller first places the Hand, as frame 800. The
    # start is 10 s shorter than the start of a new game.
    $keys = "fake_controller_play_frame=800;$keys"
    if (-not $given) {
        $after = @($Census.Split(',') | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
        $Seconds = Get-BwvrSaveSeconds $Steps 800 ($Seconds - 10) $after
    }
}
if ($Set -ne '') { $keys = "$keys;$Set" }
& (Join-Path $sp 'spawn_run.ps1') -Name $Name -Dll $Dll -Choice keep -Steps $Steps `
    -Points $points -Set $keys -Seconds $Seconds -Save $start
# spawn_run.ps1 gave up: no run, so no log of this run to read.
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$log = Join-Path $sp "desk-$Name\bwvr.log"
if (-not (Test-Path $log)) { "no log at $log"; exit 1 }
$pattern = '^village      :|^miracle      : (?!miracle_)|^hand pick    : seek plan|^hand bulk    :|^hand press   : the grip pressed|' +
           '^haptics      : (effect (3|14|15|18|19|20|21|23|24|25|26|27|28)|cast|refused)|' +
           '^hand         : state|^gesture      : (the draw|the game)|^controllers  : the right button press started off|^grip         :|^hand (throw|drop|hold|take) *:'
if ($Kind -eq 'choice') {
    $pattern = '^far grab    : (far_grab_choice|far_grab_stay|the cone of a living|target|no target|the target \S+ \(\w+\) keeps|test: (aimfar|the tree))|^hand pick    : seek plan'
}
if ($Kind -eq 'fartotem') {
    $pattern = '^village      :|^far grab    : (test|target|the grip closed|the grip goes|the game (takes|moves)|the take|the \w+ ends|aimfar|the word aimfar)|' +
               '^hand bulk    : the next pick record|^hand press   : the grip pressed|^grip         : (?!\d)'
}
if ($Kind -eq 'fartake') {
    $pattern = '^far grab    : (far_take_carry|test|target|the grip closed|the grip goes|the game takes|the take|the target \S+ is gone|the \w+ ends|\d+ targets were deleted)|' +
               '^hand bulk    : (the game|the bulk take ends|the amount in the Hand is \d+ after (10|20|30|50|100) game|test key|the take|the test key)|' +
               '^hand pick    : (seek plan|the Hand holds)|^hand press   : the grip pressed|^grip         : (?!\d)|^hand throw   : (release|the object left)|' +
               '^audio play   : .* in game (44|98),|^audio chan   : .* in game (44|98) |^audio take   :|^take stream  :'
}
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
