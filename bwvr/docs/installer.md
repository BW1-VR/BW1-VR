# The installer

One file goes out to players: `Black & White VR Mod 1.0.0 Setup.exe`, about
5 MB. It is built with Inno Setup 6.7.3 from `installer/bwvr.iss`.

Build it with:

```
powershell -ExecutionPolicy Bypass -File bwvr/tools/package.ps1
```

That builds the mod and the launcher, stages every file that ships, checks
each third party binary against a recorded SHA-256, refuses to go on if a
shipped file holds the author's own name, and runs the compiler. Add
`-TestBuild` for a second installer that writes nothing to the registry and
takes `/SKIPGAMECHECK=1`.

## What it installs

| File | What it is |
| --- | --- |
| `ddraw.dll` | The mod. The game imports DirectDraw by name, and the folder of the exe comes first in the search order. |
| `openxr_loader.dll` | The OpenXR loader. The mod imports it, so without it the game does not start at all. |
| `Black & White VR Mod.exe` | The launcher. |
| `dxwrapper\ddraw.dll`, `dxwrapper.dll`, `dxwrapper.ini`, `ddraw.ini` | The render backend. The stub keeps the name `ddraw.dll` because dxwrapper reads its own file name to pick its mode. |
| `Black & White VR Mod readme.txt`, `... notices.txt` | The player readme, and the third party licenses. |

No `bwvr.ini` ships. The mod's own defaults are the tested setup, and the
launcher writes the file when the player saves. An existing `bwvr.ini` is
never touched.

The game folder already holds a `readme.txt` from 2001. Windows ignores the
case of a file name, so the mod's readme installs under a name of its own. A
plain `README.txt` would have replaced a game file, and taken it away again
at uninstall.

## What it refuses

The wizard hashes `runblack.exe` in the chosen folder and compares it with
the v1.42 file, exactly as `src/common/exe_check.h` does inside the mod. Any
other build is refused, by name where the build is known:

```
This copy of Black & White is version 1.10.
...
2. Get the unofficial v1.42 patch from bwfiles.com.
```

Nothing is written in that case. The mod's addresses fit one file, and on a
1.10 or 1.30 file with the copy protection removed the hooks would land in
unrelated code.

## What it is careful about

- **A `ddraw.dll` that is already there** belongs to the player: it can be
  DDrawCompat, dgVoodoo or cnc-ddraw. Setup tells the files apart three ways,
  cheapest first: the SHA-256 it recorded at the last install, the text
  `bwvr.ini` inside the file, and the product name in the version resource.
  A file that is none of ours is kept as `ddraw.dll.before-bwvr`, and the
  uninstaller puts it back. A backup that turns out to be the mod's own file
  is thrown away instead, so a game cannot end up loading the mod with
  nothing behind it.
- **Write rights.** The game is often under Program Files, where the launcher
  cannot save. Setup grants the Users group modify rights on the game folder
  itself, with no inheritance, so the game's own files keep their rights.
  `dxwrapper\dxwrapper.ini` gets the same on its own.
- **VirtualStore.** `runblack.exe` has no manifest, so under Program Files
  Windows redirects its writes into `AppData\Local\VirtualStore`. A
  `bwvr.ini` left there hides the real file from the game. Setup moves those
  copies aside, under every profile, because an elevated Setup can hold a
  different account's hive. It moves them, and never deletes them, and it
  touches only the three file names the mod writes.
- **The game's own registry values.** Setup writes none of them. An elevated
  Setup can hold another user's hive. The launcher runs as the player, so it
  writes them, and before its first write it copies `ScreenW`, `ScreenH`,
  `FullScreen` and `ScreenD` into a `[game_before]` section of `bwvr.ini`.
  The uninstaller puts them back, but only when the hive it can reach still
  holds the square size the launcher wrote.
- **Saved games.** Nothing in the installer reads or writes `Profiles\` or
  `Scripts\`.
- **The uninstaller** lives in `%APPDATA%\Black & White VR Mod`, not in the
  game folder, where an `unins000.exe` would look like the game's own.

## Tested

A silent install and uninstall into a throwaway folder, with a foreign
`ddraw.dll` in it and a `readme.txt` from the game:

- Every file lands, and `RealDllPath` in both ini files is rewritten from
  `{syswow64}`.
- The foreign `ddraw.dll` is kept, and is back in place after the uninstall.
- `readme.txt` and `runblack.exe` are untouched.
- `bwvr.log` and the probe files go at uninstall. `bwvr.ini` is kept, after a
  question.
- A 1.10 game is refused, and nothing is written.
- The test build leaves the registry exactly as it was.

Not tested yet: an elevated install into Program Files, and the wizard pages
themselves, which no silent run ever draws.
