# Black & White VR Mod

The original Black & White (2001) in VR. This is a mod for the real game, not
a remake: the villagers, the Creature, the quests and the advisors are the
game's own. Your hands are the Hand. The land is at the height of your waist,
and you reach in.

[![Support the mod on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/buckzor)

The mod is free. If you like it, you can support the work on
[Ko-fi](https://ko-fi.com/buckzor).

**Status: alpha.** This repo is private and by invitation. Please do not give
a build or a link to a person outside the group.

To build the mod or to change its code, see
[`CONTRIBUTING.md`](CONTRIBUTING.md).

## What you need

- A Windows PC and a PC VR headset with controllers. The mod is tested with a
  Quest 3 through Virtual Desktop. Other headsets can work, and we want to
  know.
- Your own copy of Black & White (the first game, not Black & White 2).

## 1. Set up a clean copy of the game

The mod does not contain the game. No store sells Black & White today, so you
must find a copy yourself, for example the original CD.

The mod fits one exact version of the game, so do this in order:

1. Install Black & White. A plain folder is best, for example
   `C:\Black & White`.
2. Install the official
   [patch 1.10](https://www.bwgame.net/downloads/v1-1-patch.129/).
3. Install the official
   [patch 1.20](https://www.bwgame.net/downloads/v1-2-patch.130/).
4. Install the
   [unofficial patch v1.42](https://www.bwgame.net/downloads/black-white-unofficial-patch-v1-42.1418/).
5. Install nothing else: no patch 1.30, no Creature Isle, no other mod, no
   widescreen or resolution fix, no other `ddraw.dll` or graphics wrapper, no
   changed exe.
6. Start the game one time without the mod. Make sure that it runs, then quit.

Your game folder now holds a file `readme-patch-v142.txt`.

If the game itself does not start on your PC, the community has a guide:
[Running Black & White on Windows 10](https://www.bwgame.net/threads/running-black-white-on-windows-10.7916/).

## 2. Install the mod

Open [Releases](../../releases) and take the installer of the newest
release.

1. Run the installer. Windows warns about a file with no signature: select
   "More info", then "Run anyway".
2. If the installer does not find your game folder, select it. The installer
   stops if `runblack.exe` is not the v1.42 file.
3. To update, run a newer installer over the old build. You do not need to
   remove the old build first.

The mod adds its own files beside the game. It changes no file of the game,
and it does not touch your saved games.

## 3. Set up your headset

The game is a 32 bit program, so it needs an OpenXR runtime with a 32 bit
entry. Virtual Desktop and SteamVR have one.

- **Virtual Desktop.** In the options of the Streamer on the PC, set the
  OpenXR runtime to VDXR. In the headset, open the Streaming tab of Virtual
  Desktop and set Synchronous Spacewarp (SSW) to Disabled. The mod is tested
  with SSW off.
- **SteamVR.** Use version 2.17 or later, and make SteamVR the OpenXR
  runtime.

## 4. Play

1. Start your VR software and connect the headset.
2. Open the shortcut "Black & White VR Mod". The launcher names the runtime
   that it found.
3. Select "Play in VR". "Play, new game" starts a new game.
4. In the headset, press a trigger or A to skip the logos. Press F11 on the
   keyboard if the view is not in front of you.

**Skip the tutorial.** You can play the tutorial of the game in VR, but it
is made for the mouse and the keyboard of the original game. We recommend
that you skip it. When you start a new game, select "Skip straight to the
Creature select" in the start dialog.

### The right controller

| Control | What it does |
| --- | --- |
| Grip | Close it on a thing to take it. Open it to let go. Swing and open it to throw |
| Point, grip, pull | Brings a thing that is out of reach to your hand |
| Trigger | Click on a thing or in a menu. With the leash on, point and press to send your Creature there |
| Stick | Up and down: zoom. Left and right: turn |
| A | Hold to aim a teleport, let go to go. B cancels |
| B | Hold and draw a gesture in the air to cast a miracle |

### The left controller

| Control | What it does |
| --- | --- |
| Grip | Grab the land and pull to move. With both grips: turn and zoom |
| Stick | Move over the land, in the direction of your head |
| Y | Hold for a ring menu: Temple, Creature, town, leashes, save |
| Menu button | The game menu. In a menu, point with the line and click with the trigger |

The full list is in "Black & White VR Mod readme.txt" in your game folder.

### Good to know

- F11 makes the direction of your head straight ahead. Press it when you sit
  down, or when the world is not where you expect it.
- A thing that your Hand can act on glows gold. A red glow means that the
  thing is outside your influence.
- Save the game before you start a quest. Quick save is in the ring menu,
  under "More".
- If the frame rate is low, lower "Resolution scale" in the launcher.
- A walk with the left stick makes some people ill. If it makes you ill, turn
  off "Move with the left stick" in the launcher. Then move with the left
  grip and with A.
- If you see no hints and no subtitles, open the options of the game, then
  "Help Options". Set "Intelligent ToolTips", "All Text" and "Medium Help" or
  more.
- Each setting in the launcher has a tooltip. If you are not sure, leave a
  setting as it is.
- Use headphones. A sound comes from its place in the world.

## Report a problem

Open [Issues](../../issues), select "New issue", and fill in the form. Three
things make a report useful:

- **The build.** It is the first line of `bwvr.log` in your game folder, for
  example `Black & White VR Mod 1.1.0 (build abc1234)`.
- **The logs.** Attach `bwvr.log` and each `bwvr-old-*.log` of that day. The
  mod keeps the logs of your last 20 sessions.
- **What you did**, what you expected and what happened. A short video helps.

The known problems are in the section "KNOWN ISSUES" of the readme in your
game folder, and the work list is at the top of
[`bwvr/docs/roadmap.md`](bwvr/docs/roadmap.md).

## To remove the mod

Remove "Black & White VR Mod" in the list of installed apps of Windows. Your
saved games and the files of the game stay as they are.

## Support the mod

Thank you for playing. If you like the mod, you can support the work here:

[![Support the mod on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/buckzor)

## License and the game

The code and the documents of the mod have the MIT license, see
[`LICENSE`](LICENSE). The code of other projects in `third_party/` keeps its
own license, see [`third_party/README.md`](third_party/README.md).

This is an unofficial mod, made by a fan. Electronic Arts and Lionhead
Studios did not make it, do not endorse it, and have no connection with it.
Black & White belongs to its owners. This repo holds no file of the game and
never will. You need your own copy.
