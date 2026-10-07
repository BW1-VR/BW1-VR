# Desktop test runs

These scripts play the game on the desktop with a scripted controller and
read the log of the mod. No headset is needed. They are the regression test
for each thing that needs the real game.

Set the game folder first, one time: `.\dev.ps1 setup -Game "<folder>"` at
the root of the repo.

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
- A run never moves or clicks the real mouse, and it never brings a window
  to the front. The scripted controller acts inside the game process
  (`fake_controller=...`, and `fake_controller_front=1` lets it act while the
  game window is behind).
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
| `game.ps1` | Gives the game folder (`local\game-path.txt`) |
| `verify_build.ps1` | The three runs before a merge |
| `aim_run.ps1`, `aim_queue.ps1` | The far fetch, the aim presets, the teleport |
| `throw_queue.ps1` | Throws and landings |
| `spawn_run.ps1` | A start with "Keep your old Creature": no story, the player at the Temple |
| `creature_recipe.ps1` | The Creature state, strokes and a slap |
| `hand_run.ps1`, `village_run.ps1`, `temple_run.ps1`, `miracle_run.ps1` | Runs for the near hand, the village, the Temple and miracles |

The scripted controller is driven by ini keys: `fake_controller_script` (words
such as `point@400+1`, `grip@3100+101`, `trigger@440+8`: the word, the frame
at which it starts, and how many frames it lasts), `fake_controller_points`
and the test keys of each area. `docs/controllers.md` and `docs/hand.md` list
the words and the keys. The recipes depend on the render size of the game:
the scripts read it and choose the points of the start dialog for 2880 by
2880 or 2560 by 1440.

The scripts came from one developer's working folder in October 2026.
`verify_build.ps1` and the scripts that it calls are the maintained set. A
script for one area can need a correction before it runs on a different PC.
