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

`verify_build.ps1` makes three runs of about two minutes each: a far fetch
and a teleport, a throw that lands, and the Creature at the Temple (strokes
and a slap). It ends with `VERIFIED`, or it names the run that failed. The
log and the pictures of each run are in a folder `desk-<run>` beside the
scripts. Those folders are not tracked.

## What a run does to your PC

`desk_run.ps1` is the only script that starts the game. Each other script
calls it. It keeps these rules, and a new script must keep them too:

- A run starts only when nobody has used the keyboard or the mouse for 10
  minutes, and only when no game runs. A game that runs is the player's own
  session. A test never stops it.
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
- Before a run, the script keeps a copy of the mod DLL in the game folder,
  of `bwvr.ini`, of the `Profiles` folder (saved games, the Creature) and
  of the log. After the run it puts each of them back. A new game writes
  save slot 20 and other files of the profile, so do not remove this guard.
- A run never changes the registry. It starts `runblack.exe` directly, not
  the launcher.

## The scripts

| Script | What it is |
| --- | --- |
| `desk_run.ps1`, `desk_test.ps1` | One run of a DLL with extra ini keys. The base of each other script |
| `idle_seconds.ps1` | The time since the last input |
| `desk_state.ps1` | Reads the place of the real cursor, the window in front and the idle time |
| `game.ps1` | Gives the game folder (`local\game-path.txt`), and stops a run on a game folder where the launcher never ran |
| `dialog.ps1` | Measures the start dialog for a render size, and gives the dialog points |
| `verify_build.ps1` | The three runs before a merge |
| `aim_run.ps1`, `aim_queue.ps1` | The far fetch, the aim presets, the teleport |
| `throw_queue.ps1` | Throws and landings |
| `spawn_run.ps1` | A start with "Keep your old Creature": no story, the player at the Temple |
| `creature_recipe.ps1` | The Creature state, strokes and a slap. With `-Kind leash` or `-Kind stay`: the leash and the stay from a distance |
| `hand_run.ps1`, `village_run.ps1`, `temple_run.ps1`, `miracle_run.ps1` | Runs for the near hand, the village, the Temple and miracles |

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

`verify_build.ps1` and the scripts that it calls are the maintained set. The
scripts for one area (`hand_run.ps1`, `village_run.ps1`, `temple_run.ps1`,
`miracle_run.ps1`) have run on one PC only, and can need a correction on a
different PC.

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
