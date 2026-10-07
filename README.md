# Black & White VR Mod

The original Black & White (2001) in VR. This is a mod for the real game, not
a remake: the villagers, the Creature, the quests and the advisors are the
game's own. Your hands are the Hand. The land is at the height of your waist,
and you reach in.

**Status: alpha.** This repo is private and by invitation. Please do not give
a build or a link to a person outside the group.

- [For testers](#for-testers): get a build, play, report.
- [For contributors](#for-contributors): build the mod and put it in your game
  with one script.

---

## For testers

### What you need

- A Windows PC and a PC VR headset with controllers. The mod is tested with a
  Quest 3 through Virtual Desktop. Other headsets can work, and we want to
  know.
- Your own copy of Black & White (the first game).

### 1. Set up a clean copy of the game

The mod fits one exact version of the game, so do this in order:

1. Install Black & White.
2. Install the official patch 1.10.
3. Install the official patch 1.20.
4. Install the unofficial patch v1.42 (from bwfiles.com).
5. Install nothing else: no Creature Isle, no other mod, no widescreen or
   resolution fix, no other `ddraw.dll` or graphics wrapper, no changed exe.
6. Start the game one time without the mod. Make sure that it runs, then quit.

Your game folder now holds a file `readme-patch-v142.txt`.

### 2. Get the newest build

Open [Releases](../../releases) and take the installer of the newest
release. Buck makes a release when a set of changes is ready to test, not
for each change.

1. Run the installer. Windows warns about a file with no signature: select
   "More info", then "Run anyway".
2. If the installer does not find your game folder, select it. The installer
   stops if `runblack.exe` is not the v1.42 file.
3. To update, run a newer installer over the old build. You do not need to
   remove the old build first.

To test one change before a release has it, open [Actions](../../actions),
select the build of that commit or pull request, and take the installer from
"Artifacts" at the bottom of the page.

### 3. Play

1. Start your VR software and connect the headset. With Virtual Desktop, use
   its own OpenXR runtime (VDXR, in the options of the Streamer). With
   SteamVR, use version 2.17 or later as the OpenXR runtime.
2. Open the shortcut "Black & White VR Mod". The launcher names the runtime
   that it found.
3. Select "Play in VR".
4. In the headset, press a trigger or A to skip the logos. Press F11 on the
   keyboard if the view is not in front of you.

| Control | What it does |
| --- | --- |
| Right grip | Close it on a thing to take it. Open it to let go. Swing and open to throw |
| Point, grip, pull | Brings a thing that is out of reach to your hand |
| Left grip | Grab the land and pull to move. Both grips: turn and zoom |
| Right stick | Up and down: zoom. Left and right: turn |
| A | Hold to aim a teleport, let go to go. B cancels |
| B | Hold and draw a gesture in the air to cast a miracle |
| X (left) | Hold for a ring menu: Temple, Creature, town, leashes, save |
| Y (left) | The game menu. In a menu, point with the line and click with the trigger |
| Trigger, leash on | Point and press to send your Creature there |

The full list is in "Black & White VR Mod readme.txt" in your game folder.

### 4. Report a problem

Open [Issues](../../issues), select "New issue", and fill in the form. Three
things make a report useful:

- **The build.** It is the first line of `bwvr.log` in your game folder, for
  example `Black & White VR Mod 1.1.0 (build abc1234)`.
- **The logs.** Attach `bwvr.log` and each `bwvr-old-*.log` of that day. The
  mod keeps the logs of your last 20 sessions.
- **What you did**, what you expected and what happened. A short video helps.

Save the game before you start a quest. The known problems are in the section
"KNOWN ISSUES" of the readme in your game folder, and the work list is at the
top of [`bwvr/docs/roadmap.md`](bwvr/docs/roadmap.md).

### To remove the mod

Remove "Black & White VR Mod" in the list of installed apps of Windows.

---

## For contributors

### What you need

- Windows 10 or 11.
- Visual Studio 2022, or the Build Tools for Visual Studio 2022, with the
  workload "Desktop development with C++". It brings the compiler and CMake.
- Git.
- The game, set up as in [step 1 for testers](#1-set-up-a-clean-copy-of-the-game),
  in a folder that your account can write to (not in Program Files).
- Only to make an installer yourself: Inno Setup 6.

All other code that the mod needs is in the repo (`third_party/`). There is
nothing more to download.

### The first time

```powershell
git clone https://github.com/Buckzor/BW1-VR.git
cd BW1-VR
.\dev.ps1 setup -Game "D:\Games\Black & White"
.\dev.ps1
.\dev.ps1 play
```

`setup` checks that your game is the v1.42 game and keeps its folder in
`local\game-path.txt`. `.\dev.ps1` with no word builds the mod, runs the math
tests, and puts the mod in your game folder.

**How the mod gets into the game.** The repo holds only the source of the
mod. Your game stays in its own folder, outside the repo, and no file of the
game is ever changed. The mod is nine files that go beside `runblack.exe`
(`ddraw.dll`, the OpenXR loader, the launcher and a `dxwrapper` folder). The
game loads `ddraw.dll` from its own folder when it starts, and that is how
the mod gets in. Two things put those files there: the installer, for
testers, and `.\dev.ps1`, for you. `.\dev.ps1` keeps the `ddraw.dll` that was
there before, and `.\dev.ps1 uninstall` takes the nine files out and puts it
back. The script never changes the registry. A game in Program Files needs
a PowerShell that runs as administrator, so a copy of the game in a plain
folder is easier for development.

If PowerShell does not permit scripts, run this one time:
`Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.

### The work loop

1. Make a branch, change the code.
2. `.\dev.ps1`, then `.\dev.ps1 play`, and look at `bwvr.log` in your game
   folder. Its first line says which build runs.
3. If your change can affect the game, run the desktop check:
   `.\bwvr\tools\desk\verify_build.ps1 -Tag a`. It plays three short scenes
   with a scripted controller, with no headset, and says `VERIFIED`.
4. Open a pull request. GitHub builds it, runs the tests, and keeps an
   installer with the run, so a tester can try your change.
5. A release for the testers is made by hand, when Buck wants one: on
   GitHub, Actions, "Release for testers", "Run workflow". The workflow
   builds `main`, makes the installer and names the release from the
   version, the date and the commit. Nobody makes a tag.

[`CONTRIBUTING.md`](CONTRIBUTING.md) has the rules of the project. Read it
before your first pull request.

### How the mod works, in short

- The mod is a C++ DLL with the name `ddraw.dll`, so the game loads it. It
  hooks functions of `runblack.exe` (the camera, the Hand, the input, the
  draw) with MinHook. The logic of the game stays the game's own.
- The game is DirectX 7. dxwrapper turns that into Direct3D 9. The mod makes
  the game draw the scene one time for each eye and gives the two pictures to
  the headset through OpenXR.
- The headset moves the game camera, and the controllers move the game's own
  Hand. So a pick up, a throw and a miracle go through the code of the game.
- A small renderer of the mod draws the hands, the highlights and the arcs
  in the frame of the game.

### Where things are

| Path | What |
| --- | --- |
| `bwvr/src/mod/` | The mod (`ddraw.dll`): the hooks, the hands, the God view, the sound |
| `bwvr/src/vr/` | The OpenXR session and the layers |
| `bwvr/src/launcher/` | The launcher with the settings |
| `bwvr/src/common/` | The addresses in the game, the version |
| `bwvr/test/controller_math/` | The math tests (no game needed) |
| `bwvr/tools/desk/` | The desktop test runs with a scripted controller |
| `bwvr/tools/package.ps1` | Makes the installer |
| `bwvr/installer/` | The installer script, the player readme, the notices |
| `bwvr/docs/` | The design and the reverse engineering notes of each area |
| `third_party/` | MinHook, the OpenXR loader, dxwrapper, cgltf |

Start with [`bwvr/docs/roadmap.md`](bwvr/docs/roadmap.md) (the work list and
the history), then the doc of your area: `hand.md` (the Hand, grab, throw,
the Creature, miracles), `god-view.md`, `controllers.md`, `backend.md` (the
render path), `audio.md`, `cutscene-window.md`.

---

## How this project is made

Most of the code and the documents in this repo are written with AI coding
tools (Claude Code), under the direction of Buck, who plays each build in a
headset and decides what is right. We say this openly. [`CLAUDE.md`](CLAUDE.md)
holds the rules that the tools follow here. They are good rules for a person
too.

## License and the game

The code and the documents of the mod have the MIT license, see
[`LICENSE`](LICENSE). The code of other projects in `third_party/` keeps its
own license, see [`third_party/README.md`](third_party/README.md).

Black & White belongs to its owners. This repo holds no file of the game and
never will. You need your own copy.
