# Desktop test runs

These scripts play the game on the desktop with a scripted controller and
read the log of the mod. No headset is needed. They are the regression test
for each thing that needs the real game.

Two things first, one time:

1. Set the game folder: `.\dev.ps1 setup -Game "<folder>"` at the root of the
   repo, then `.\dev.ps1` to put the mod in it.
2. Start the mod with its launcher: open `Black & White VR Mod.exe` in the
   game folder, select "Play in VR", then close the game. The launcher
   writes `bwvr.ini` and gives the game its display size. A new install
   renders 800 by 600 with no settings file until then, and no recipe fits.
   The scripts stop with "NOT RUN" and say this when it was not done. For
   the run with the Creature, the profile of the game also needs a Creature
   ("Keep your old Creature" in the start dialog).

## The check before a merge

```powershell
.\dev.ps1 build
.\bwvr\tools\desk\verify_build.ps1 -Tag a
```

`verify_build.ps1` makes three runs: a far fetch and a teleport (30 s), a
throw that lands (112 s), and the Creature at the Temple (strokes and a slap,
42 s). The first run and the third run start in a test save. The second run
is a new game and plays the first story (see "A fast start"). The three runs
take about 190 s. The script ends with `VERIFIED`, or it names the run that
failed. The log and the pictures of each run are in a folder `desk-<run>`
beside the scripts. Those folders are not tracked.

## What a run does to your PC

`desk_run.ps1` is the only script that starts the game. Each other script
calls it. It keeps these rules, and a new script must keep them too:

- A run starts only when nobody has used the keyboard or the mouse for 10
  minutes, and only when no game runs. A game that runs is the player's own
  session. A test never stops it. Only the owner's "test now" flag turns the
  wait for an idle PC off (see "The shared folder").
- A run never moves or clicks the real mouse. The scripted controller acts
  inside the game process (`fake_controller=...`). `desk_test.ps1` gives each
  run the test key `pointer_leave_cursor=1`, so the mod does not hold or move
  the cursor of Windows. See "The real cursor and the window in front".
- The scripts and the mod never bring a window to the front. Windows can do
  it: it gives the front to the window of a new process when no person gave
  an input for longer than its foreground lock time (200 s by default). A run
  waits 10 minutes, so its game window is in front from the first frame.
  When the game ends, Windows gives the front to a different window.
  `fake_controller_front=1` lets the scripted controller act when the game
  window is behind (a run that starts soon after an input).
- A run takes a lock (`desk.lock`), so two runs do not happen at one time.
  The lock is in a folder that each git worktree of the repo on this PC
  shares (see "The shared folder").
- Before a run, the script keeps a copy of the mod DLL in the game folder,
  of `bwvr.ini`, of the `Profiles` folder (saved games, the Creature) and
  of the log. After the run it puts each of them back. A new game writes
  the save of slot 20, two save pictures, `helpstats.dat` and `vortex.txt`,
  all in the `Profiles` folder, so do not remove this guard.
- The script also keeps a copy of the folder `Scripts\CreatureMind` of the
  game folder and of `MemoryLeaks.txt`, and it puts each file back. The mind
  of the Creature of the profile is in that folder, and a game that ends by
  itself writes it (see "A run in which the game ends by itself"). In a run
  with the Creature of the profile, the game also writes it by itself in
  play, with `creature.lhp` of the profile. That write comes 65 s to 118 s
  after the start of the game, so a run of a minute or more can have it.
- The tools never write the registry. A run starts `runblack.exe` directly,
  not the launcher. The game writes its own values there. In its start it
  writes the values of the profile that it read, so each value stays the
  same. In play it writes the alignment of the player (`LoveBuckets`) each
  600 game turns, which is about one minute: that value stays the same while
  a test does not change the alignment. A run stops the game with
  `Stop-Process`, so the game never comes to its exit path, where it writes
  the play time and the counts of the profile. In a run in which the game
  ends by itself, and in a run with a test save, the mod drops each write of
  the game to its keys of the registry: see "A run in which the game ends by
  itself" and "A fast start".

## The scripts

| Script | What it is |
| --- | --- |
| `desk_run.ps1`, `desk_test.ps1` | One run of a DLL with extra ini keys. The base of each other script. `-Save <name>` starts the run in a test save |
| `make_test_save.ps1` | Makes a test save: a saved game that the game writes at the end of a test run. See "A fast start" |
| `idle_seconds.ps1` | The time since the last input. A large number while the owner's "test now" flag holds |
| `desk_state.ps1` | Reads the place of the real cursor, the window in front and the idle time |
| `game.ps1` | Gives the game folder (`local\game-path.txt`), and stops a run on a game folder where the launcher never ran. Gives the shared folder, reads the "test now" flag, and tests the output of a script for a verdict line. Gives the folder of the test saves, the test save that a script starts in, the length of a run in a test save, and the folder of the profile that the game uses. Reads the time of a frame from a run log. Gives the words and the places of the left hand for one choice of the ring |
| `dialog.ps1` | Measures the start dialog for a render size, and gives the dialog points. Gives the way through the Main Menu of a run in a test save |
| `verify_build.ps1` | The three runs before a merge |
| `exit_run.ps1` | A run in which the game ends by itself, and the check of its end |
| `launcher_shot.ps1` | A picture of the window of the launcher, for a change of its table or its layout. It does not start the game, and it puts no window on the screen, so it needs no idle PC. `-Rows <n>` gives the picture of a table with more columns. See `launcher.md`, "A picture of the window for a test" |
| `aim_run.ps1`, `aim_queue.ps1` | The far fetch, the aim presets, the teleport |
| `throw_queue.ps1` | Throws and landings |
| `spawn_run.ps1` | A start with "Keep your old Creature": no story, the player at the Temple |
| `creature_recipe.ps1` | The Creature state, strokes and a slap. With `-Kind leash`, `-Kind stay` or `-Kind tie`: the leash, the stay from a distance, and the leash tied to an object. With `-Kind click`: a text that waits for a click. With `-Kind walk`: the walk mode of a command |
| `hand_run.ps1`, `village_run.ps1`, `temple_run.ps1`, `miracle_run.ps1` | Runs for the near hand, the village, the Temple and miracles. See "The scripts of one area" |
| `intro_run.ps1` | The Lionhead logo in stereo, with a picture of each eye each few seconds. The run does not skip the logos. See "The logos" |
| `bench_run.ps1` | The frame cost measure: six still scenes, the numbers of each scene, the spread of some runs, and the comparison of two builds or of two sets of keys. See "The frame cost measure" |

The scripted controller is driven by ini keys: `fake_controller_script` (words
such as `point@400+1`, `grip@3100+101`, `trigger@440+8`: the word, the frame
at which it starts, and how many frames it lasts), `fake_controller_points`
and the test keys of each area. `docs/controllers.md` and `docs/hand.md` list
the words and the keys.

**The start dialog and the render size.** Each run of a new game must click
the start dialog. The dialog has one size in pixels, and the scripted hand
gets its places in whole cm, so the right places change with the render
size (the launcher takes that size from the headset). `dialog.ps1` solves
this: for a render size that it has not seen, it makes one run of 45 s in
which the hand points at six places, reads the pixel of each from the log,
and keeps the result in `local\dialog-calibration.json`. Each run script
takes its dialog points from there. You do nothing for it. It happens one
time for each new render size. A run in a test save has no start dialog, and
it needs no measure (see "A run with a test save").

`verify_build.ps1` and the scripts that it calls are the maintained set.

## The scripts of one area

Each of these scripts makes runs of one area, and prints the lines of the
log that show the result. The header comment of each script has its
parameters. The frames of a recipe fit a game that runs at about 60 frames a
second. `throw_queue.ps1 -Fps 90` makes a run at 90 frames a second, as a
headset session has: the mod does not copy the picture to the monitor and
gives each frame its time (the test key `test_frame_rate`), and the script
gives each step of the recipe 1.5 times as many frames, so it keeps its
time. The window of the game shows no new picture in such a run.

Each script starts its runs in a test save (see "A fast start"). `-NewGame`
gives the start of a new game, and `-Save <name>` a different test save with
the same state. The table has the test save, the row of the start dialog
that `-NewGame` uses, and the length of a run in the test save.

| Script | The start: the test save, and the row with `-NewGame` | The kinds and what they do | A run takes |
| --- | --- | --- | --- |
| `hand_run.ps1` | `pen`, "Skip straight to the Creature select" (`one`, `fixed`). `temple`, "Keep your old Creature" (`old`) | `one`: the grip in the air, at a scroll and at the crown of a tree, with `near_rule` 1 and 2. With `near_rule` 2 the grip at the scroll takes the rock beside it and sets it down. `old`: the grip on the Creature, strokes and slaps. `fixed`: places from `-Points` and steps from `-Steps` | 44 s for `one`, 142 s for `old` |
| `throw_queue.ps1 -Runs <names>` | `pen`, "Skip straight to the Creature select" | A far fetch at the pen and a throw (`land`, `base`, `early`, `tree`, `sea`), the same at a different scale (`landout`, and `landin`, `landin2`, `landin3` and `landlife` for 66.7, 44.4, 29.6 and 7.5 units for each meter), a release with a still hand (`fall`, `fallold`), the same with a tree that the hand holds upright or turns by 30 degrees (`treeset`, `treelean`), a new grip on an object that falls, flies or rolls (the names with `drop`, `fly` and `roll`), a grip on a rock in its fall, on a rock that the hand threw up, on a rock on the land that still moves, and a grip of 50 ms on a rock in its fall (`dropnear`, `tossup`, `landgrip`, `dropshort`: with `-More 'catch_at_once=0'` the rock moves on until the game takes it, and with `-More 'catch_at_once=0;grip_press_hold=0'` the game takes each of these presses as a click), a throw after a catch of a rock in its fall (`catchthrow`), eye pictures of the frame of a catch and of 3 frames later (`catchshot`, with no `-Fps`), a fetch from a distance of a tree that still moves on the land (`catchfar`), and a tree that comes to rest while the Hand takes it (`treedropend`, with the test key `catch_rest_test` for the runs in which the tree does not come to rest by itself in the take) | 31 to 63 s each |
| `aim_queue.ps1 -Runs <names>` | `pen`, "Skip straight to the Creature select" | The aim presets and the trim (`aim2`), a fetch of a thrown rock over the sea and from the sea (`move2`, `move1b`), a Creature command with the leash counted as on (`cmd1b`), the teleport at three scales (`ring1`, `ring0`), and the aim and the grip at a Creature that is not the Creature of the player (`other`) | 21 to 54 s each |
| `creature_recipe.ps1` | `temple`, "Keep your old Creature" | `state`: the Creature state, strokes and a slap. `leash`: the leash on and off. `stay`: the stay from a distance. `tie`: the leash tied to an object, and the walk of the Creature to it. `click`: a text of the game that waits for a click, with the leash on and with the Hand off the view (`text_click`). `walk`: a held press of the trigger on an object while the aim goes to the land and comes back, a tap and a press that ends on the object (`creature_command_rule`) | 42 to 62 s |
| `village_run.ps1` | `temple`, "Keep your old Creature" | `miracle`: a miracle from an icon of the Worship Site (`-Icon 3` is Food). `totem`, `charge`. `store`, `tree` and `job`: wood, a tree and a villager at the Village Store. `throw`: a thrown villager. `scaffold`, `build`, `site`, `probe`, `workshop`. `choice`: the aim steps from a villager to the tree nearest to him, past the tree, and back, and the log has each far target with its reason (`far_grab_choice`, `far_grab_stay`). `fartake`: a take at a distance from a pile of the Village Store with the grip closed, which the test key `far_test_take_gone` ends at its game turn 30 as the game ends the take of a pile that it deletes. The log shows who has the bundle then (`far_take_carry`), and with `audio_trace=1` the loop of the take (`audio_take_loop`). The lines `take stream` have the start and the end of the stream of the take (`take_stream`) | 31 to 63 s |
| `miracle_run.ps1` | `temple`, "Keep your old Creature" | `creature`, `throw`, `cancel`, `place`, `shield`: a miracle from a seed that the test key `miracle_test_seeds` makes. `-Seeds` gives other seed types, and `-Steps` other steps | 44 to 57 s |
| `temple_run.ps1` | `temple`, "Keep your old Creature" | The ring item `enter`, then the items of `-Rooms` on the ring of the Temple (`-Rooms "room_save@3400 leave@5600"`), and the steps of `-Steps`. Each choice of the ring holds Y of the left hand and steps the left hand to the item (`Get-BwvrRingChoice` in `game.ps1`). With `button_layout=0` in `-Set` it holds X. `-NoEnter` stays on the land. `-SaveTest` makes a save, and starts only when the profile has no saved game in the slots 0 to 13. It always starts in a new game | 180 s, or `-Seconds` |

What these scripts need:

- The profile of the game needs a Creature for the test save `temple`, and
  for each start with "Keep your old Creature".
- `dialog.ps1` gives the points of the start dialog to each run of a new
  game, also the point of the fourth box for `hand_run.ps1 -Kind old`.
- A miracle that pours (Water, Food, Wood and Lightning) stays in the Hand
  after the pour. A recipe with more than one seed must take it out of the
  Hand (a tap of B) before it takes the next seed.
- The word `pull` steps the hand 15 cm toward the head, and the next `pull`
  steps it back. A recipe with two fetches needs a `pull` with an empty hand
  between them.

Known limits of the recipes:

- No recipe chooses a Creature at the Creature select. A Creature in a pen
  has no player, so a grip on it starts no Creature state. A press of the
  trigger there starts a drag of the land, and the double click of A makes
  the jump of the game. The recipes with the Creature state start in the
  test save `temple` (`hand_run.ps1 -Kind old`, `creature_recipe.ps1`).
- `throw_queue.ps1 -Runs rollfar` does not take the rock in its roll. The
  word `aimfar` comes 1.6 s after the release. The rock is then 1.1 to 1.2 m
  of the room from the Hand, and the land hides it from the head (the log
  says "seen 0"), so it is no far target. `dropfarroll` takes a rock that
  rolls after a fall.
- `throw_queue.ps1 -Runs landgrip` does not close the grip on the rock on
  the land. With the head 25 degrees down, the word `aimfar` takes a
  tortoise, because a living thing comes first in the aim. With `-More
  'far_test_aim_class=3'` it takes the rock, but the word `seek` then leaves
  the hand 44 cm of the room from the rock on the land, and the grip takes a
  tree at the hand.
- `throw_queue.ps1 -Runs rollnear` is not run with the word `seek` of now.
  The word takes the place of the thrown object from its mesh when the draw
  does not offer the object. The rock is 1.1 to 1.2 m of the room from the
  Hand there, and the hand at it can be off the view.
- `throw_queue.ps1 -Runs droproll` and `-Runs treenear` do not close the grip
  on the object after its fall. The word `seek` puts the hand 50 cm below
  the head. That place is below the view of the desktop window, and the
  controller code drops a press that starts off view (the log says "the
  right button press started off view"). The two presses in the air of
  `hand_run.ps1 -Kind one` start off view too, and the code drops them.
- The word `aimfar` does not choose the same object in the test save `pen`
  and in a new game. The head is 1 cm from its place in a new game, and some
  objects are at the least distance of the word (50 cm for a rock, 17 cm for
  a tree). In a new game the first `aimfar` of a recipe that fetches a rock
  (`aim_queue.ps1`, `throw_queue.ps1`) aims at a small rock 0.50 m of the
  room from the Hand. In the test save it aims at the rock 3.86 m from the
  Hand. Two proofs need `-NewGame`:
  - `aim_queue.ps1 -Runs ring1,ring0`. In the test save the hand keeps the
    aim at the far rock, which is almost level. The third aim, at 337.5 units
    a meter, is then on the sea, and its release makes no teleport. The run
    shows the three ring sizes and two teleports. In a new game the hand aims
    61 degrees down, and each of the three teleports goes.
  - `throw_queue.ps1 -Runs treeset` and `-Runs treelean`. In the test save
    the second `aimfar` aims at a different tree, the hand turns to it, the
    Hand's point is then on a small rock, and the grip takes the rock. In a
    new game the grip takes the tree at the land.
- The speed of a throw is not the same in each run. The scripted hand makes
  its step of 15 cm in one frame, so the top speed that the mod measures
  depends on the time of the frames at the step. Four runs of
  `throw_queue.ps1 -Runs landout` gave 1.9 to 3.6 m/s for the same two
  throws, and the object then lands at a different place. Compare the place
  and the time of a landing in two runs only when the two lines `the flight
  of ... starts` have the same speed.
- In `throw_queue.ps1 -Runs early` the pick up of the game takes 2 or 3 game
  turns. It ends from 2 frames before the opening of the grip to 3 frames
  after it. So the release comes 17 to 64 ms after the opening, and the line
  `the pick up is over` is in the log of some runs only. The throw goes in
  each run.
- The Creature of the profile acts by its own plan, and the plan at one frame
  is not the same in each run. In `creature_recipe.ps1` and `hand_run.ps1
  -Kind old` the Creature state starts 8 to 170 frames after the press, and
  a state that starts late can have no stroke. The Creature stands still
  for a stay 0.2 to 2.7 s after its start. In one run of three of
  `hand_run.ps1 -Kind old` the Creature walked away before the third grip,
  and the third and the fourth grip had no target.
- `village_run.ps1 -Kind scaffold` shows the join of two scaffolds and the
  tap that takes them apart. Its last opening of the grip makes no building
  site: the game has no place for a building there, and the hand is 18 cm
  over the land, so the scaffold falls. `-Kind site` shows a building site.
- A run in which a person uses the PC does not count: the game window can
  lose the front, and the real mouse can take the pointer from the scripted
  controller. `desk_run.ps1` says when a person used the PC in a run.

## The frame cost measure

`bench_run.ps1` says what a frame costs, and whether a change makes a frame
cheaper. The numbers come from the log of the mod (`profile=1`,
`docs/backend.md`, "The frame cost measure").

```powershell
.\bwvr\tools\desk\bench_run.ps1 -Name b -Runs 3
.\bwvr\tools\desk\bench_run.ps1 -Name ab -Dll <build A> -Dll2 <build B> -Runs 3
.\bwvr\tools\desk\bench_run.ps1 -Name k -Set "audio_stop_wait=1" -Set2 "audio_stop_wait=0" -Runs 3
.\bwvr\tools\desk\bench_run.ps1 -Read b1,b2,b3
```

A run starts in the test save `temple` and takes 122 s. The scripted
controller and the fake head put the view at six places. At each place
nothing of the player moves for 600 frames (`-Frames`), and the word `bench`
of the script makes those frames one scene of the measure:

| Scene | The view |
| --- | --- |
| 1 | The door of the Temple at the scale of the start (100 units for each meter), with a level head: the land to the horizon. The Creature of the profile stands at that door |
| 2 | The same place with the head 30 degrees to the left and 20 degrees down (the word `look`). The head stays so for the rest of the run |
| 3 | The Village Store, two zoom steps in (44.4 units for each meter): villagers, trees and buildings |
| 4 | The same place with a tree in the Hand |
| 5 | Five zoom steps out (337.5 units for each meter) |
| 6 | The widest zoom (4000 units for each meter) |

The script prints these tables:

- **One table for each run**, with one row for each scene. `frame` is the
  mean of the whole frame. `work` is the frame less the copy to the monitor:
  its mean, its median, its 99th percentile and its worst frame. The next
  columns have the mean of each part of a frame. The parts are the game
  turn, each eye pass, the frame and pass functions of the mod, the copy to
  the monitor and the rest. `D3D` is the time in Direct3D draw calls, which
  is inside the eye passes. Below the table, one line for each scene has the
  count of the slow frames and the part that is their cause.
- **The spread**, with `-Runs` at 2 or more. For each number of each scene
  the table has the value of each run, the mean, and the largest value less
  the smallest one. The villagers and the Creature move by themselves, and a
  game turn comes each 100 ms of game time. So two runs are not the same
  frame for frame. A difference between two builds counts only when it is
  larger than the spread.
- **The comparison**, with `-Dll2` (a second build) or `-Set2` (a second set
  of ini keys, `key=value;key=value`). The runs go A, B, A, B. For each
  number the table has the mean of A, the mean of B, the difference and the
  larger spread of the two. Three runs give only a rough spread. So the
  result is `clear` only when the difference is more than two times that
  spread, and `near the spread` when it is more than the spread.

The runs of `-Name b` have the names `b1`, `b2` and `b3`. With a comparison
they have the names `ab-a1`, `ab-b1`, `ab-a2` and so on. `-Read` and `-Read2`
make no run. They take the logs of runs that are there, by the name of each
run or by the path of each log.

Rules for a measure:

- Read the first line of each run table: the build, the render size and the
  value of `profile`. Compare only runs with the same render size.
- A run in which a person used the PC does not count. The script prints the
  lines of `desk_run.ps1` that say so, and its lines about the files that
  the run put back.
- `profile=1` puts a timer on each Direct3D draw call. Those timers are in
  the numbers of each run: 0.2 to 0.4 ms of a frame with 2000 to 2900 draw
  calls. `-ProfileLevel 2` gives the measure with no such timer.
- A desktop run has no headset. `docs/backend.md`, "The measure on the
  desktop", says which costs such a run has and which it does not have.

The last line is `BENCH DONE`, or `BENCH STOPPED at <run>` when a run was not
made.

## A fast start

A run must not wait for a thing that it does not test. The table has what
each start of a run costs: the time from the start of the game process to the
first frame at which a recipe can act. The numbers are from runs with a
render of 2208 by 2208 at about 62 frames a second. The game needs 9 s of
each number for its own start and the load of the land.

| The start | What the game does first | A recipe can act | Seconds |
| --- | --- | --- | --- |
| A new game, the row `select` | The start dialog, then the first story: the doors and the walk to the Creature pens | At the frame at which the player gets the Hand (frame 3430 to 3750) | 63 |
| A new game, the row `keep` | The start dialog, then the game camera goes to the door of the Temple | At frame 800 | 22 |
| A test save, `-Save <name>` | The Main Menu over the loaded game, which the mod closes at frame 70 (a test key) | At the frame at which the controller places the Hand (frame 70) | 11 |

### The logos

The logos at the start of the game take 38 s: with them, the first frame
of the land comes 47 s after the start of the game process, and not 9 s.
`desk_test.ps1` gives each run the test key `intro_skip_test_frame=5`, and the
mod then skips them (`docs/intro.md`, "The skip"). So each number of the
table is without the logos. A run that tests the logos gives
`intro_skip_test_frame=0`, or a frame of its own, in its extra keys.

`intro_run.ps1` is such a run, for the Lionhead logo in stereo
(`docs/intro.md`, "The intro in stereo"):

```powershell
.\bwvr\tools\desk\intro_run.ps1 -Name in1 -Dll <build>\src\mod\Release\ddraw.dll -Motion 1
```

It gives `intro_vr=1`, `intro_skip_test_frame=0`, `intro_probe=1` and
`intro_motion` (`-Motion`), and it starts no scripted controller. The logo
screens take about 9 s and the Lionhead logo 33 s, so a run of 54 s (the
default) has the whole logo. Each `-Every` seconds (4 by default) the mod saves a picture of
each eye, `bwvr-eye-left-1NNNNN.bmp` and `bwvr-eye-right-1NNNNN.bmp`. NNNNN is
the stereo frame of the logo. Each picture is 14 MB at a render of 2208 by
2208. After the run the script prints the lines of the log with the tag
`intro`: the camera for each second of the effect clock, and for each pair of
pictures the pixels of some orbs. `-Set` gives other keys, for example the
test keys `intro_motion_test_seconds` and `intro_push_test_px`.

### A test save

A test save is a saved game that the game wrote at the end of a test run. It
is not a saved game of the player. It is a file that the game made, so it is
not in the repo, and it must not go there. The test saves are in
`local\test-saves\<name>` of the main checkout of the repo, so each git
worktree uses the same ones (`Get-BwvrTestSaves` in `game.ps1` gives the
folder). The folder `20` holds the files of the saved game. `save.txt` says
what the save is: the start, the land, the state, the frame, the render size
and the DLL.

```powershell
.\bwvr\tools\desk\make_test_save.ps1 -Name pen
.\bwvr\tools\desk\make_test_save.ps1 -Name temple
```

| Name | The run that makes it | The state of the game |
| --- | --- | --- |
| `pen` | A new game, the row `select`, 69 s | Land 1. The first story is over. The player has the Hand at the Creature pens, and no Creature is chosen. The save is 150 frames after the player got the Hand |
| `temple` | A new game, the row `keep`, 27 s | Land 1. No story. The Creature of the profile stands at the door of the Temple, and the game camera is at rest there. The village is there. The save is at frame 800 |

`make_test_save.ps1` makes a run of a new game. At the frame of the save, the
word `exit` of the scripted controller asks the game to end by itself. On its
exit path the game writes the saved game of slot 20, as it does each time a
player ends the game. That is a whole saved game of that moment. `desk_run.ps1`
copies the files of slot 20 that the game wrote in the run to the folder of
the test saves, and only then does it put the profile back. A file of the
slot that is older than the run is a file of the player, and the script does
not copy it. The guards of such a run are in "A run in which the game ends by
itself". The last line is `SAVE MADE` or `SAVE NOT MADE`.

A different name makes a save of your own: give `-Choice` (a row of the start
dialog), `-Frame` and, for script words before the save, `-Steps`.

`verify_build.ps1` and each script of one area make `pen` or `temple` when
the save that they start in is not there (`Confirm-BwvrTestSave` in
`game.ps1`). When that run does not make the save, the script starts in a new
game and says so. Make a test save again after a change of the game folder or
of the profile: the save `temple` has the Creature of the profile in it.

### A run with a test save

`desk_run.ps1`, `aim_run.ps1` and `spawn_run.ps1` have the parameter `-Save
<name>`, and with no `-Save` they start a new game. Each script of one area
starts in its test save by default. Such a run does this:

1. `desk_run.ps1` takes the lock and makes each copy of a run, as for each
   other run.
2. It takes the files of slot 20 out of the profile (the profile guard has
   them), and it puts the files of the test save in. It compares each file
   with the test save. When one is not the same, no game starts.
3. It starts the game with no `-NEWGAME`. The game then loads the saved game
   of slot 20 by itself. The game has no word of the command line that loads
   a saved game (`docs/backend.md`, "A test save").
4. The game opens its Main Menu over the loaded game, and it waits there for
   the button "Continue Game". At frame 70 the mod does what the button does
   (the test key `menu_skip_test_frame=70`, `docs/backend.md`, "A test
   save"). The run has no click, so it needs no place of a button and no
   measure of the render size. `Get-BwvrMenuStart` in `dialog.ps1` gives the
   keys and the script words.
5. The steps of the recipe start. With `fake_controller_play_frame=N`, the
   steps from frame N on count from the frame at which the controller first
   places the Hand after frame 60 (`fake_controller_play_after`). So a recipe
   of a new game keeps its steps and its points, and the frames in the log
   are less than the frames of the recipe. A test key of the recipe that acts
   at a frame counts the same way (`docs/controllers.md`, 4.3).
6. After the run, the profile guard takes each file of the test save out of
   the profile and puts each file of the player back.

The run has the test key `registry_test_guard=1`. In each start the game
writes the values of the profile to the registry, and it writes the key
bindings of the profile when the Main Menu closes. Each number is the number
that it read. In play the game also writes the
alignment of the player (`LoveBuckets`) each 600 game turns. With the key, the
mod drops each write of the game to its keys of the registry, and the log
names each one: 17 in the start, 99 key bindings, and each write in play.
The lines of the log that show a good start:

```
registry test: the game writes the registry value "timestarted" (type 4, 4 bytes, number 355) in the key Software\Lionhead Studios Ltd\Black & White\LHMultiplayer\Profiles\<profile>. The mod drops the write
game start   : the saved game is loaded, frame 0. The game loaded the saved game of slot 20, and it opens its Main Menu over it (game mode 0, land 1), 9.6 s after the start of the game process. In the start the mod dropped 17 writes of the game to its keys of the registry, and 41 writes of the process to other keys passed
game start   : test key menu_skip_test_frame: the mod does what the button "Continue Game" of the Main Menu does (PauseGame(0), DialogBoxBase::Hide, FrontEnd::SetCursorOff), so the loaded game plays with no click, frame 70, 11.3 s after the start of the game process
controllers  : the controller places the Hand after frame 60 at frame 70. The steps of the script from frame 2750 on are 2680 frames sooner (fake_controller_play_frame), 11.3 s after the start of the game process
```

When the load fails, the game starts a new game, and the line `game start`
says "a new game starts". `verify_build.ps1` tests for the line `game start`
and for the line `controllers`.

Two keys of the recipes are on from a frame of the game, and that frame is
before the play frame: `aim_test_touch` and `creature_command_test`. They do
not count from the play frame. `aim_queue.ps1` gives them the frame 110 in a
test save, which is after the Main Menu. In a new game it gives 700 and 1000,
which are after the start dialog. A `temple_test_*` key with a frame has the
frame of the game too.

**The length of a run.** A recipe of `aim_queue.ps1` or `throw_queue.ps1`
has its length in a new game, and a run in the test save `pen` is 49 s
shorter. For the other scripts `Get-BwvrSaveSeconds` in `game.ps1` gives the
length from the last step of the recipe: 12 s for the start, the frames from
the play frame to the last step at 40 frames a second, and 4 s. A kind of
`creature_recipe.ps1` that ends with a stay of the Creature (`state`, `leash`,
`stay`) runs 10 s more: the mod watches the Creature for 10 s after a stay,
and then the log says if it left its place. `-Seconds` gives a different
length. A recipe of your own must end late enough for its last line of the
log.

The rules of a test save:

- A test never loads a saved game of the player, and a test save has no
  file of the player in it. For the time of a run, the files of slot 20 of
  the player are in the copy of the profile guard only.
- The files of a test save go into the profile only after the profile guard
  made its copy.
- The tools write nothing to the registry, and the mod drops each write of
  the game to its keys there.
- A test save is not in the repo.

A DLL of a build that does not have the test key `menu_skip_test_frame` gets
a click on "Continue Game" in its place (`Get-BwvrMenuStart` reads the DLL
for the name of the key). The place of that button is measured at a render of
2208 by 2208 only. The measure of `dialog.ps1` gives the point for a
different render size, but no run has shown the click there.

### Which script uses which start

The table has the start of each script, and the length of a run in a new
game (`-NewGame`) and in the test save. The play frame is the frame of the
recipe that counts as the frame at which the controller first places the
Hand in the loaded game (`fake_controller_play_frame`).

| Script | The start | The play frame | A run in a new game | A run in the test save |
| --- | --- | --- | --- | --- |
| `verify_build.ps1`, the run `aim2` | The test save `pen` | 2750 | 79 s | 30 s |
| `verify_build.ps1`, the run `tland` | A new game, `select` | 2750 | 112 s | This run keeps the first story: the cut scene window, the story camera and the God view after a story need one run |
| `verify_build.ps1`, the run `vc` | The test save `temple` | 800 | 65 s | 42 s |
| `aim_queue.ps1` | The test save `pen` | 2750 | 70 to 103 s, 588 s for the seven runs | 21 to 54 s, 245 s for the seven runs |
| `throw_queue.ps1` | The test save `pen` | 2750 | 80 to 112 s, 2760 s for the 29 runs | 31 to 63 s, 1339 s for the 29 runs |
| `hand_run.ps1 -Kind one` | The test save `pen` | 4600 | 163 s | 44 s |
| `hand_run.ps1 -Kind fixed` | The test save `pen` | 4600 | 163 s | 16 s and the time of `-Steps` |
| `hand_run.ps1 -Kind old` | The test save `temple` | 700 | 163 s | 142 s |
| `creature_recipe.ps1` | The test save `temple` | 800 | 65 to 80 s, 350 s for the five kinds | 42 to 62 s, 257 s for the five kinds |
| `village_run.ps1` | The test save `temple` | 800 | 60 to 100 s, 809 s for the twelve kinds | 31 to 63 s, 457 s for the twelve kinds |
| `miracle_run.ps1` | The test save `temple` | 800 | 75 s, 375 s for the five kinds | 44 to 57 s, 247 s for the five kinds |
| `temple_run.ps1` | The test save `temple` | 800 | 190 s | 180 s |
| `temple_run.ps1 -SaveTest` | A new game, `keep` | | 190 s | This run keeps the new game: its check reads the slots of the profile, and a run in a test save has the files of the test save in slot 20 |
| `bench_run.ps1` | The test save `temple` | 800 | | 122 s for each run |
| `exit_run.ps1` | A new game, with the start dialog up | | 150 s | This run tests the end of the game, and it needs no state of the game |

The start in the test save `pen` is 49 s shorter than the start of a new game
with `select`, and the start in the test save `temple` is 10 s shorter than
the start with `keep`. A run of `hand_run.ps1 -Kind old` or `temple_run.ps1`
is long because its steps are long.

No recipe of `throw_queue.ps1` needs the first story. Each one needs the
Hand, the rock at the pen (or a tree) and the land, and the test save `pen`
has them. `verify_build.ps1` gives its run `tland` the new game
(`throw_queue.ps1 -NewGame`), because that one run of the check keeps the
story.

## The shared folder

The lock of a run and the owner's "test now" flag are in one folder for the
PC: `bwvr-desk` in the temp folder of the user (`%TEMP%\bwvr-desk`).
`Get-BwvrDeskShared` in `game.ps1` gives the folder and makes it. The folder
is not in the repo and not in the game folder, so each git worktree of the
repo on this PC uses the same two files. A shell that has a different `TEMP`
has a different folder, and then only the test for a game that runs keeps
its runs apart from the others.

- `desk.lock`. `desk_run.ps1` makes it before a run and removes it after the
  run. It holds the name of the run, the time, and the folder of the scripts
  that made it. A second run waits for it (`-WaitMinutes`, 30 by default) and
  then stops with `NOT RUN: another run held the lock ...`. A lock that is
  older than 20 minutes is from a run that died, and the next run takes it.
- `test-now-until.txt`. This is the owner's "test now". It holds one time,
  and while that time is in the future, a run does not wait for an idle PC.
  Each other guard stays: the lock, the test for a game that runs, and the
  copies. `desk_run.ps1` and `idle_seconds.ps1` read it (`Test-BwvrTestNow`
  in `game.ps1`). A file of the same name beside the scripts counts too, for
  the scripts of that worktree only. Only the owner makes this file:

  ```powershell
  . .\bwvr\tools\desk\game.ps1
  Set-Content (Join-Path (Get-BwvrDeskShared) 'test-now-until.txt') (Get-Date).AddMinutes(30).ToString('s')
  ```

## The wait and the verdict lines

`aim_run.ps1`, `spawn_run.ps1`, `hand_run.ps1` and `temple_run.ps1` wait for
an idle PC. Each 30 s they read the idle time and look for a game that runs.
They start the run when the PC is idle for 10 minutes and no game runs, and
they give up after `-Minutes`. `creature_recipe.ps1`, `miracle_run.ps1`,
`village_run.ps1` and `bench_run.ps1` wait through `spawn_run.ps1`. `verify_build.ps1`,
`aim_queue.ps1` and `throw_queue.ps1` wait through the scripts that they
call. `desk_run.ps1` and `exit_run.ps1` do not wait for an idle PC:
`desk_run.ps1` stops with `NOT RUN`, and `exit_run.ps1` with `EXIT NOT RUN`.

A script gives its result in a verdict line. A verdict line starts with fixed
words in capital letters:

| Verdict line | Who writes it | What it says |
| --- | --- | --- |
| `NOT RUN: <why>` | `desk_run.ps1`, and each script before it starts a run | No game started |
| `RUN DONE` | `aim_run.ps1`, `spawn_run.ps1`, `creature_recipe.ps1`, `hand_run.ps1`, `temple_run.ps1` | The script made one run. It is the last line, and the exit code is 0 |
| `GAVE UP: the PC was not idle for 10 minutes in N minutes` | The same scripts | No run started in `-Minutes`. It is the last line, and the exit code is 2 |
| `QUEUE DONE`, `QUEUE STOPPED at <run>` | `aim_queue.ps1`, `throw_queue.ps1` | Each run of the list was made, or the named run was not |
| `VERIFIED`, `NOT VERIFIED: <why>` | `verify_build.ps1` | The check before a merge |
| `SAVE MADE: <name>`, `SAVE NOT MADE: <why>` | `make_test_save.ps1` | A test save is in the folder of the test saves, or it is not. It is the last line |
| `EXIT CLEAN`, `EXIT FAULT`, `EXIT NOT RUN` | `exit_run.ps1` | See "A run in which the game ends by itself" |
| `BENCH DONE`, `BENCH STOPPED at <run>` | `bench_run.ps1` | Each run of the measure was made, or the named run was not |

`creature_recipe.ps1` prints the lines of the log only after a run, and then
`RUN DONE`. `miracle_run.ps1` and `village_run.ps1` print the verdict line of
`spawn_run.ps1` before their lines of the log, and they stop when it gave up.

A script that reads the output of a different script counts a verdict line
only when the line starts with the words, in capital letters
(`Test-BwvrVerdict` in `game.ps1`). The log of the mod and the lines of
`desk_run.ps1` can have the words "not run" in them, and such a line is not a
verdict. Do the same in a wait script of your own. In PowerShell, `-match`
ignores the case, so use `-cmatch '^NOT RUN'` on each line.

## A run in which the game ends by itself

`desk_test.ps1` waits until the game is gone or until `-Seconds` are over.
Only then does it stop the game. Its line `game exited on its own` says
which of the two happened. A stopped game never runs the shutdown of the
mod, so each other run tests nothing of the end of the game.

The test key `exit_test_frame=N` makes the game end by itself. At frame N the
mod posts the message of the close button to the game window. The word `exit`
of the scripted controller does the same at a frame of the script. The game
then goes its usual exit path (`docs/backend.md`, "The end of the mod"). On
that path the game writes these things:

- In the `Profiles` folder: the save of slot 20, two save pictures,
  `helpstats.dat` and `vortex.txt`.
- In the game folder, when the game has the Creature of the profile: the mind
  of that Creature, `Scripts\CreatureMind\<file>.erc` and
  `Scripts\CreatureMind\Physique<file>.erc`. The game folder also gets
  `MemoryLeaks.txt`.
- In the registry: the values of the profile and the sound volumes.

Two guards keep the player's state:

- `desk_run.ps1` puts each file of the `Profiles` folder back, as for each
  run. It does the same for each file of `Scripts\CreatureMind` and for
  `MemoryLeaks.txt`.
- From frame N to the end of the process, the mod drops each write of the
  game process to the registry. The log names each one (lines `exit test`),
  and each file that the process opens for a write. If a guard cannot be
  made, the mod does not post the message.

A new script that makes the game end by itself must read those lines of its
log: a file that is not in one of the folders above is not put back.

`exit_run.ps1` makes such a run and checks the end:

```powershell
.\bwvr\tools\desk\exit_run.ps1 -Name exit1 -Dll <build>\src\mod\Release\ddraw.dll
.\bwvr\tools\desk\exit_run.ps1 -Name exit0 -Dll <build>\src\mod\Release\ddraw.dll -Old
```

It ends with `EXIT CLEAN` when these three things are true:

- The game ended by itself.
- Windows recorded no fault of `runblack.exe` in the run (the Application
  log, the provider "Application Error").
- The log of the mod ends with `the mod is shut down`.

If not, it ends with `EXIT FAULT`. `-Old` gives `exit_shutdown=0`, the
shutdown in `DllMain`: that run ends with `EXIT FAULT`, and Windows records
one fault or two. `-NoBridge` gives a run with no shared textures: it ends
clean with each value of `exit_shutdown`. The run has no headset, so it does
not test the end of the OpenXR session.

The frame of `exit_test_frame` counts each call of `FinishFrame`: the frames
of the logos and of the load too, and two for each frame of the scene. It
must be in the game, after the load (3600 by default). The word `exit` has a
frame of the script. A different way to close the game window (a plain
`WM_CLOSE`) does not end the game.

## The real cursor and the window in front

`desk_run.ps1` reads the place of the real cursor and the class of the window
in front before it starts the game, and again when the game is gone. It
prints the two states, and then one of these lines:

- `the real cursor: at the same place before and after the run`.
- `THE REAL CURSOR MOVED in the run <name>, from A to B, and nobody used the
  PC ...`. The last input of a person is older than the run, so a program
  moved the cursor. For Windows, `SetCursorPos` of a program is not an input.
  The lines after it name the calls that moved the cursor, from the log of
  the mod. The line also goes out as a warning, so a script that shows only
  the last lines of a run does not hide it.
- `the real cursor is at a different place than before the run ... A person
  used the PC N s ago`. This does not count against the run.

The lines are also in `desk-<run>\desk-state.txt`, and `verify_build.ps1`
prints the third line for each of its runs. The check does not change the
result of a run.

While the game runs, `desk_test.ps1` prints one line `desk watch` for each
change of the place of the cursor or of the window in front, with the time
and the idle time of Windows.

`desk_test.ps1` gives each run two test keys of the mod (`docs/backend.md`,
"Who moves the cursor of Windows"):

- `pointer_leave_cursor=1`. With the game window in front, the mod holds the
  cursor in the window and puts it at the Hand: that is right for a player
  and wrong for a test. With the key the mod does neither.
- `cursor_trace=1`. The log of the mod names each caller in the game process
  of `SetCursorPos`, `ClipCursor`, `SetForegroundWindow` and
  `SetActiveWindow` (lines `cursor trace :`).

A run with the extra key `pointer_leave_cursor=0` has the behavior that a
player gets, and it moves the real cursor. The mod then puts the cursor at
the Hand each time the pointer moves in the game window, so the cursor ends
at a place that the recipe of the run decides.
