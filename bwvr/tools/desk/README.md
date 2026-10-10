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
  itself writes it (see "A run in which the game ends by itself").
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
| `game.ps1` | Gives the game folder (`local\game-path.txt`), and stops a run on a game folder where the launcher never ran. Gives the shared folder, reads the "test now" flag, and tests the output of a script for a verdict line. Gives the folder of the test saves and the folder of the profile that the game uses, and reads the time of a frame from a run log |
| `dialog.ps1` | Measures the start dialog for a render size, and gives the dialog points and the point of "Continue Game" in the Main Menu |
| `verify_build.ps1` | The three runs before a merge |
| `exit_run.ps1` | A run in which the game ends by itself, and the check of its end |
| `aim_run.ps1`, `aim_queue.ps1` | The far fetch, the aim presets, the teleport |
| `throw_queue.ps1` | Throws and landings |
| `spawn_run.ps1` | A start with "Keep your old Creature": no story, the player at the Temple |
| `creature_recipe.ps1` | The Creature state, strokes and a slap. With `-Kind leash`, `-Kind stay` or `-Kind tie`: the leash, the stay from a distance, and the leash tied to an object. With `-Kind click`: a text that waits for a click |
| `hand_run.ps1`, `village_run.ps1`, `temple_run.ps1`, `miracle_run.ps1` | Runs for the near hand, the village, the Temple and miracles. See "The scripts of one area" |

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
time for each new render size.

`verify_build.ps1` and the scripts that it calls are the maintained set.

## The scripts of one area

Each of these scripts makes runs of one area, and prints the lines of the
log that show the result. The header comment of each script has its
parameters. The frames of a recipe fit a game that runs at about 60 frames a
second.

| Script | The start of the game | The kinds and what they do | A run takes |
| --- | --- | --- | --- |
| `hand_run.ps1` | "Skip straight to the Creature select" (`one`, `creature`, `fixed`), "Keep your old Creature" (`old`) | `one`: the grip in the air, at a scroll and at the crown of a tree, with `near_rule` 1 and 2. With `near_rule` 2 the grip at the scroll takes the rock beside it and sets it down. `old`: the grip on the Creature, strokes and slaps. `creature`: the choice of a Creature at the Creature select. `fixed`: places from `-Points` and steps from `-Steps` | 163 s, 223 s for `creature` |
| `throw_queue.ps1 -Runs <names>` | "Skip straight to the Creature select" | A far fetch at the pen and a throw (`land`, `base`, `early`, `tree`, `sea`), the same at a different scale (`landout`, `landin`), a release with a still hand (`fall`, `fallold`), the same with a tree that the hand holds upright or turns by 30 degrees (`treeset`, `treelean`), and a new grip on an object that falls, flies or rolls (the names with `drop`, `fly` and `roll`) | 80 to 112 s each |
| `aim_queue.ps1 -Runs <names>` | "Skip straight to the Creature select" | The aim presets and the trim (`aim2`), a fetch of a thrown rock over the sea and from the sea (`move2`, `move1b`), a Creature command with the leash counted as on (`cmd1b`), and the teleport at three scales (`ring1`, `ring0`) | 75 to 103 s each |
| `creature_recipe.ps1` | "Keep your old Creature" | `state`: the Creature state, strokes and a slap. `leash`: the leash on and off. `stay`: the stay from a distance. `tie`: the leash tied to an object, and the walk of the Creature to it. `click`: a text of the game that waits for a click, with the leash on and with the Hand off the view (`text_click`) | 65 to 75 s |
| `village_run.ps1` | "Keep your old Creature" | `miracle`: a miracle from an icon of the Worship Site (`-Icon 3` is Food). `totem`, `charge`. `store`, `tree` and `job`: wood, a tree and a villager at the Village Store. `throw`: a thrown villager. `scaffold`, `build`, `site`, `probe`, `workshop` | 60 to 100 s |
| `miracle_run.ps1` | "Keep your old Creature" | `creature`, `throw`, `cancel`, `place`, `shield`: a miracle from a seed that the test key `miracle_test_seeds` makes. `-Seeds` gives other seed types, and `-Steps` other steps | 75 s |
| `temple_run.ps1` | "Keep your old Creature" | The ring item `enter`, and then the steps of `-Steps` in the Temple. `-NoEnter` stays on the land. `-SaveTest` makes a save, and starts only when the profile has no saved game in the slots 0 to 13 | 190 s |

What these scripts need:

- The profile of the game needs a Creature for each start with "Keep your
  old Creature".
- `dialog.ps1` gives the points of the start dialog to each script, also the
  point of the fourth box for `hand_run.ps1 -Kind old`.
- A miracle that pours (Water, Food, Wood and Lightning) stays in the Hand
  after the pour. A recipe with more than one seed must take it out of the
  Hand (a tap of B) before it takes the next seed.
- The word `pull` steps the hand 15 cm toward the head, and the next `pull`
  steps it back. A recipe with two fetches needs a `pull` with an empty hand
  between them.

Known limits of the recipes:

- `hand_run.ps1 -Kind creature` does not choose a Creature: the double click
  of A at the pen makes the jump of the game, and a grip on a Creature of the
  Creature select starts no Creature state.
- `throw_queue.ps1 -Runs rollnear` and `-Runs rollfar` do not take the rock
  in its roll. The word `seek` or `aimfar` comes 1.6 s after the release.
  The rock is then 1.1 to 1.2 m of the room from the Hand, and the draw of
  the game does not offer it (the log says "seen 0"), so it is no candidate.
  `dropfarroll` takes a rock that rolls after a fall.
- A run in which a person uses the PC does not count: the game window can
  lose the front, and the real mouse can take the pointer from the scripted
  controller. `desk_run.ps1` says when a person used the PC in a run.

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
| A test save, `-Save <name>` | The Main Menu over the loaded game, and one click on "Continue Game" | At the frame at which the controller places the Hand (about frame 81) | 11 |

### The logos

The logos at the start of the game take 38 s: with them, the first frame
of the land comes 47 s after the start of the game process, and not 9 s.
`desk_test.ps1` gives each run the test key `intro_skip_test_frame=5`, and the
mod then skips them (`docs/intro.md`, "The skip"). So each number of the
table is without the logos. A run that tests the logos gives
`intro_skip_test_frame=0`, or a frame of its own, in its extra keys.

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

`verify_build.ps1` makes `pen` and `temple` when they are not there. Make a
test save again after a change of the game folder or of the profile: the save
`temple` has the Creature of the profile in it.

### A run with a test save

`desk_run.ps1`, `aim_run.ps1`, `spawn_run.ps1` and `creature_recipe.ps1` have
the parameter `-Save <name>`. Such a run does this:

1. `desk_run.ps1` takes the lock and makes each copy of a run, as for each
   other run.
2. It takes the files of slot 20 out of the profile (the profile guard has
   them), and it puts the files of the test save in. It compares each file
   with the test save. When one is not the same, no game starts.
3. It starts the game with no `-NEWGAME`. The game then loads the saved game
   of slot 20 by itself. The game has no word of the command line that loads
   a saved game (`docs/backend.md`, "A test save").
4. The game opens its Main Menu over the loaded game. The scripted controller
   clicks "Continue Game" at frame 70 (`dialog.ps1` has the place of the
   button and the script words).
5. The steps of the recipe start. With `fake_controller_play_frame=N`, the
   steps from frame N on count from the frame at which the controller first
   places the Hand after frame 60 (`fake_controller_play_after`). So a recipe
   of a new game keeps its steps and its points, and the frames in the log
   are less than the frames of the recipe.
6. After the run, the profile guard takes each file of the test save out of
   the profile and puts each file of the player back.

The run has the test key `registry_test_guard=1`. In each start the game
writes the values of the profile to the registry, and it writes the key
bindings of the profile when "Continue Game" closes the Main Menu. Each
number is the number that it read. In play the game also writes the
alignment of the player (`LoveBuckets`) each 600 game turns. With the key, the
mod drops each write of the game to its keys of the registry, and the log
names each one: 17 in the start, 99 key bindings, and each write in play.
The lines of the log that show a good start:

```
registry test: the game writes the registry value "timestarted" (type 4, 4 bytes, number 355) in the key Software\Lionhead Studios Ltd\Black & White\LHMultiplayer\Profiles\<profile>. The mod drops the write
game start   : the saved game is loaded, frame 0. The game loaded the saved game of slot 20, and it opens its Main Menu over it (game mode 0, land 1), 9.6 s after the start of the game process. In the start the mod dropped 17 writes of the game to its keys of the registry, and 41 writes of the process to other keys passed
controllers  : the controller places the Hand after frame 60 at frame 81. The steps of the script from frame 2750 on are 2669 frames sooner (fake_controller_play_frame), 11.5 s after the start of the game process
```

When the load fails, the game starts a new game, and the line `game start`
says "a new game starts". `verify_build.ps1` tests for the line `game start`
and for the line `controllers`.

A key of a recipe with a frame of its own does not count from the play frame:
`aim_test_touch`, `text_click_test`, `miracle_test_seeds`, `village_test_site`
and others. Give such a key a frame that fits the run.

The rules of a test save:

- A test never loads a saved game of the player, and a test save has no
  file of the player in it. For the time of a run, the files of slot 20 of
  the player are in the copy of the profile guard only.
- The files of a test save go into the profile only after the profile guard
  made its copy.
- The tools write nothing to the registry, and the mod drops each write of
  the game to its keys there.
- A test save is not in the repo.

The place of "Continue Game" is measured at a render of 2208 by 2208 only.
The measure of `dialog.ps1` gives the point for a different render size, but
no run has shown the click there.

### Which script uses which start

| Script | The start | What a start in a test save needs |
| --- | --- | --- |
| `verify_build.ps1`, the run `aim2` | The test save `pen` | Nothing. The run is 30 s long |
| `verify_build.ps1`, the run `tland` | A new game, `select` | This run keeps the first story: the cut scene window, the story camera and the God view after a story need one run. The run is 112 s long |
| `verify_build.ps1`, the run `vc` | The test save `temple` | Nothing. The run is 42 s long |
| `creature_recipe.ps1` | A new game, `keep`. With `-Save temple`: that test save | `-Kind state` is shown by a run. `-Kind leash`, `stay` and `tie` have the parameter, and no run has shown them. `-Kind click` has no `-Save`: `text_click_test` has frames of its own |
| `aim_queue.ps1` | A new game, `select` | `-Save pen` for `aim_run.ps1`, `aim_test_touch=110` in place of 700 for `aim2`, `ring1` and `ring0`, and a length for each run. The steps count from the play frame |
| `throw_queue.ps1` | A new game, `select` | A parameter `-Save` that goes to `aim_run.ps1`, and a length for each run. The steps count from the play frame |
| `hand_run.ps1 -Kind one`, `creature`, `fixed` | A new game, `select` | The steps have fixed frames from 4600 on, and `near_test_flips` has frames of its own. They need `fake_controller_play_frame`, or new frames. `-Kind creature` is for the choice of a Creature at the pens: the save `pen` has that state |
| `hand_run.ps1 -Kind old` | A new game, `keep` | The script makes its own dialog words and calls `desk_run.ps1`. It needs the click on "Continue Game" in place of those words, `-Save temple`, and `fake_controller_play_frame=700` for its steps, which have fixed frames from 700 on |
| `village_run.ps1`, `miracle_run.ps1` | A new game, `keep` | The steps start at frame 800, and the test keys have frames of their own (`village_test_site=870`, `village_test_scaffolds=1@930`, `village_test_workshop=905`, `miracle_test_seeds=2@930`). Those keys must count from the play frame as the script does, or they need new frames |
| `temple_run.ps1` | A new game, `keep` | `-Save temple` for `spawn_run.ps1` and `fake_controller_play_frame=800`: the step into the Temple is at frame 820. The steps in a room come after the guide of the hall, which has a length in seconds, so they need new frames. `-SaveTest` writes a saved game of its own, so it stays with a new game |
| `exit_run.ps1` | A new game, with the start dialog up | Nothing: it tests the end of the game |

A new game with `keep` costs 22 s and a test save 11 s, so a run of
`village_run.ps1`, `miracle_run.ps1` or `temple_run.ps1` can be about 11 s
shorter. A run with `select` can be about 51 s shorter.

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
they give up after `-Minutes`. `creature_recipe.ps1`, `miracle_run.ps1` and
`village_run.ps1` wait through `spawn_run.ps1`. `verify_build.ps1`,
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
