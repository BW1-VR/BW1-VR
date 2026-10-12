# How to build the mod and contribute

Thank you for your help. This file tells you how to build the mod, where
things are, and the rules of the project. The rules are short, and most of
them come from a lesson that cost time.

## The two roles

- **Testers** play the builds and write reports. A tester needs no tools:
  see the [README](README.md).
- **Contributors** change the code or the documents through pull requests.

## Hard rules

1. **No file of the game in the repo, ever.** No exe, no art, no sound, no
   saved game, no model or texture made from a file of the game. Keep your
   game outside the repo. The folders `local/` and `media/` are not tracked,
   and `bwvr/tools/check_repo.ps1` stops a commit that holds a game file. It
   runs as a git hook (after `.\dev.ps1 setup`) and on GitHub.
2. **Do not share builds or the repo** with a person outside the group.

## Build the mod

### What you need

- Windows 10 or 11.
- Visual Studio 2022, or the Build Tools for Visual Studio 2022, with the
  workload "Desktop development with C++". It brings the compiler and CMake.
- Git.
- The game, set up as in
  [step 1 of the README](README.md#1-set-up-a-clean-copy-of-the-game), in a
  folder that your account can write to (not in Program Files).
- Only to make an installer yourself: Inno Setup 6.

All other code that the mod needs is in the repo (`third_party/`). There is
nothing more to download.

### The first time

```powershell
git clone https://github.com/BW1-VR/BW1-VR.git
cd BW1-VR
.\dev.ps1 setup -Game "D:\Games\Black & White"
.\dev.ps1
.\dev.ps1 play
```

`setup` checks that your game is the v1.42 game and keeps its folder in
`local\game-path.txt`. `.\dev.ps1` with no word builds the mod, runs the math
tests, and puts the mod in your game folder.

If PowerShell does not permit scripts, run this one time:
`Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.

### How the mod gets into the game

The repo holds only the source of the mod. Your game stays in its own
folder, outside the repo, and no file of the game is ever changed. The mod
is nine files that go beside `runblack.exe` (`ddraw.dll`, the OpenXR loader,
the launcher and a `dxwrapper` folder). The game loads `ddraw.dll` from its
own folder when it starts, and that is how the mod gets in.

Two things put those files there: the installer, for players, and
`.\dev.ps1`, for you. `.\dev.ps1` keeps the `ddraw.dll` that was there
before, and `.\dev.ps1 uninstall` takes the nine files out and puts it back.
The script never changes the registry. A game in Program Files needs a
PowerShell that runs as administrator, so a copy of the game in a plain
folder is easier for development.

## How a change gets in

1. Make a branch from `main`. One subject for each branch.
2. Change the code. Then run `.\dev.ps1` and `.\dev.ps1 play`, and look at
   `bwvr.log` in your game folder. Its first line says which build runs.
3. If your change can affect the game, run the desktop check:
   `.\bwvr\tools\desk\verify_build.ps1 -Tag a`. It plays three short scenes
   with a scripted controller, with no headset, and says `VERIFIED`.
4. Open a pull request and fill in its template. GitHub builds it, runs the
   tests, and keeps an installer with the run, so a tester can try your
   change before the merge.
5. Buck merges. `main` must always build and pass the tests, because a
   release is a build of `main`.

Do not push to `main` directly. GitHub does not enforce this on a private
repo of this kind, so it is a rule between us.

To test one change before a release has it, open [Actions](../../actions),
select the build of that commit or pull request, and take the installer from
"Artifacts" at the bottom of the page.

A release is made by hand, when Buck wants one: on GitHub, Actions, "Release
for testers", "Run workflow". The workflow builds `main`, makes the installer
and names the release from the version, the date and the commit. Nobody makes
a tag. Buck makes a release when a set of changes is ready to test, not for
each change.

## How the mod works, in short

- The mod is a C++ DLL with the name `ddraw.dll`, so the game loads it. It
  hooks functions of `runblack.exe` (the camera, the Hand, the input, the
  draw) with MinHook. The logic of the game stays the game's own.
- The game is DirectX 7. dxwrapper turns that into Direct3D 9. The game walks
  its scene one time a frame, for the left eye, and the mod makes the right
  eye from the same batches. The mod gives the two pictures to the headset
  through OpenXR.
- The headset moves the game camera, and the controllers move the game's own
  Hand. So a pick up, a throw and a miracle go through the code of the game.
- A small renderer of the mod draws the hands, the highlights and the arcs
  in the frame of the game.

## Where things are

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

Start with [`bwvr/docs/roadmap.md`](bwvr/docs/roadmap.md) (the open work),
then the doc of your area in `bwvr/docs/`:

| Doc | Area |
| --- | --- |
| `backend.md` | The render path, the two eyes, OpenXR, the head, the hotkeys |
| `god-view.md`, `tabletop.md` | The God view (the main mode) and the tabletop mode |
| `controllers.md`, `controls-audit.md` | The controllers, and each control of the game in VR |
| `hand.md` | The Hand of the game, and the Hand at the real hand |
| `grab.md`, `throw.md` | Taking a thing, the far grab, forests, and the throw |
| `creature.md`, `miracles.md`, `village.md` | The Creature, miracles, and the village, scrolls and the Temple door |
| `hand-model.md` | How the hands look and feel: the model, the glow, haptics |
| `ui-layer.md`, `controller-hints.md` | The interface in the room, and the hint cards |
| `audio.md` | The sound |
| `cutscene-window.md`, `intro.md` | Stories in a window, and the start of the game |
| `launcher.md`, `installer.md` | The launcher and the installer |
| `address-map.md`, `software-tnl.md`, `stereo-artifacts.md`, `input-devices.md` | Facts about the game's code and its renderer |
| `research-hla.md` | Notes on Half-Life: Alyx as a guide for the hands |

Each doc holds the current state and the plans. The history is in git.

## Rules for the mod itself

- **No motion that the player did not make.** No automatic sway, glide or
  bob of the view. When the game moves its camera, the mod cuts or shows the
  scene on a screen. This is the first rule of comfort.
- **The Hand is the real hand, 1:1** in place and size. No extended reach.
  Reach comes from the scale of the world, and from the far grab.
- **A change of behavior has an ini key**, and the old behavior is one of
  its values. Give a test key for a live A/B when the change is about feel
  (`test_keys=1`). Check which keys are free first.
- **Use the game's own way** where it has one: its packets, its states, its
  tests. Find what the plain game does with the mouse, then give the game
  the same thing from the hand. Do not copy logic of the game into the mod.
- **Hooked functions run many times a frame.** Latch what belongs to a
  frame.
- **The log tells the story.** One line for each change of state, with the
  numbers, never a line for each frame. A log from a tester's session must
  show what happened in the case that he did not like.
- **Say what is proven.** In a pull request, say what a desktop run showed,
  what only a headset can show, and what did not run.

## Tests

| Test | Needs | How |
| --- | --- | --- |
| The math tests | Nothing | Part of `.\dev.ps1 build`. Put new math in a pure header and add checks in `bwvr/test/controller_math/` |
| Desktop runs | The game | `bwvr/tools/desk/`, see its README. A scripted controller plays, and the script reads the log |
| The headset | A headset | The only proof of feel and comfort. Say which headset and runtime |

The desktop runs never move the real mouse, never bring a window to the
front, and put your settings, your profile and your saved games back after
each run. Keep those rules in each new script.

## Style

- **C++**: follow the code around your change: its names, its comment
  density, its idiom. C++17, 32 bit, static CRT, warnings at `/W4`.
- **Text** (comments, log lines, strings that the player sees, docs, commit
  messages): short, plain, active sentences. One name for one thing. The
  docs use Simplified Technical English, because readers of many languages
  use them. No em dashes.
- **Docs**: each area has one doc in `bwvr/docs/`. When the code changes,
  the doc says what the code does now. `[V]` marks a fact that was verified
  in the disassembly.
- **The current state only**: the roadmap, the docs and the comments say
  what is true now and what is planned. The git history is the history.
  When a task is done or a fault is corrected, remove it from the roadmap
  and the docs. Do not keep dates, names of test runs, a fault that is
  gone, or a design that was dropped. The reason and the proof of a change
  go in the commit message and the pull request.
- **Commits**: an imperative subject line that says what changes ("Keep the
  Hand at the real hand in the Totem state"), then why, if it is not plain.

## Reverse engineering

The notes are in `bwvr/docs/` (start with `hand.md`, "How to read this
file" and "Tools used"). The addresses of the game are in
`bwvr/src/common/bw_addresses.h`. The mod fits one exe only, the v1.42 file,
and it checks the SHA-1 before it hooks anything.

Two tools read the code of the game. Both use the function names of the
public project `bw1-decomp` (see `third_party/README.md`).

- `python bwvr\tools\disasm.py 0x005B3C70` gives the disassembly of one
  function in less than a second, with the names. It also finds a name
  (`--find`), the callers of a function (`--callers`) and the uses of an
  address (`--xref`). It needs Python 3 and `pip install capstone`.
- `.\bwvr\tools\re.ps1 decompile 0x005B3C70` gives the C of a function from
  Ghidra, in `local\ghidra\out\`. It needs Ghidra 12 and a JDK 21.

Set them up one time with `.\bwvr\tools\re.ps1 setup`. It gets the names,
and it makes a Ghidra project of your `runblack.exe` in `local\ghidra`
(about 3 minutes). `.\bwvr\tools\re.ps1 status` says what is there. The
project is large and holds the game, so it stays on your machine: git does
not track `local`. You can open it in Ghidra too
(`local\ghidra\project\runblack.gpr`). Close it there before you run
`re.ps1 decompile`, because a Ghidra project takes one user at a time.

A name or a signature from the symbol file is a claim, not a proof. The exe
has no symbols: a person or a tool wrote each Windows name, and Ghidra makes
the parameter types of a function from it. Check a type in the code before
you use it. When a name is wrong, put the right one in
`bwvr/ghidra/name-corrections.txt` with the proof. Both tools read that
file. `python bwvr\tools\check_names.py` compares the names with the Mac
symbols of the same project, which are from the compiler.

## AI tools

Most of this project is written with AI coding tools, and you are welcome to
use them too. [`CLAUDE.md`](CLAUDE.md) holds the rules for them. You are
responsible for what you submit: build it, run it, and read it first.

## License of your work

The project has the MIT license. When you open a pull request, you agree
that your contribution has the same license.
