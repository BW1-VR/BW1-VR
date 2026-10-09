# The installer

One file goes out to players: `Black & White VR Mod <version> Setup.exe`,
about 5 MB. The version is `BWVR_VERSION_TEXT` in `src/common/version.h`.
Inno Setup 6 builds the file from `installer/bwvr.iss`.

## How it is built

| Way | What it does |
| --- | --- |
| `.\dev.ps1 package` | Builds the installer on your PC. It needs Inno Setup 6. |
| `powershell -ExecutionPolicy Bypass -File bwvr/tools/package.ps1` | The script that `dev.ps1 package` runs. |
| GitHub, workflow `Build` (`.github/workflows/build.yml`) | Runs for each push to `main` and each pull request. It makes the installer and keeps it with the run for 30 days ("Artifacts"). It makes no release. |
| GitHub, workflow `Release for testers` (`.github/workflows/release.yml`) | A person starts it by hand: Actions, "Release for testers", "Run workflow". It builds `main`, makes the installer and puts it in a new release. It names the release from the version, the date and the commit. Nobody makes a tag. |

Both workflows use the action in `.github/actions/build`. The action does
these steps:

1. Run `bwvr/tools/check_repo.ps1`, which makes sure that the repo holds no
   file of the game.
2. Configure and build the mod for 32 bit.
3. Run the math tests.
4. Install Inno Setup 6 if it is missing.
5. Run `package.ps1 -SkipBuild`, then add `build <commit>` to the name of the
   installer.

`package.ps1` does four things and stops at the first one that fails:

1. Builds the mod and the launcher (not with `-SkipBuild`).
2. Stages every file that ships in `bwvr/out/stage`.
3. Checks each third party binary against a recorded SHA-256. A new version of
   a third party project needs a new hash in the script, by hand.
4. Runs the Inno Setup compiler.

It refuses to ship the probe `ddraw.dll` (the mod and the probe have the same
file name) and refuses a staging folder that holds `bwvr.ini`. It never
touches a game folder or a registry value.

| Parameter | Effect |
| --- | --- |
| `-TestBuild` | Also makes a second installer that writes no uninstall registry key and takes `/SKIPGAMECHECK=1`. |
| `-SkipBuild` | Uses the binaries in the build folder. |
| `-StageOnly` | Stops after the hash check. `dev.ps1` uses it to put the mod in a game folder. Inno Setup is not needed. |
| `-Version` | The version for the installer. The default is `BWVR_VERSION_TEXT`. |
| `-OutDir` | Where the installer goes. The default is `bwvr/out`. |

## What it installs

| File | What it is |
| --- | --- |
| `ddraw.dll` | The mod. The game imports DirectDraw by name, and the folder of the exe comes first in the search order. |
| `openxr_loader.dll` | The OpenXR loader. The mod imports it, so without it the game does not start. |
| `Black & White VR Mod.exe` | The launcher. |
| `dxwrapper\ddraw.dll`, `dxwrapper.dll`, `dxwrapper.ini`, `ddraw.ini` | The render backend. The stub keeps the name `ddraw.dll` because dxwrapper reads its own file name to pick its mode. |
| `Black & White VR Mod readme.txt`, `Black & White VR Mod notices.txt` | The player readme (`installer/README.txt`) and the third party licenses (`installer/Notices.txt`). |

No `bwvr.ini` ships. The defaults of the mod are the tested setup, and the
launcher writes the file when the player saves. Setup never changes an
existing `bwvr.ini`.

The game folder already holds a `readme.txt` from the game. Windows ignores the
case of a file name, so the readme of the mod installs under a name of its own.
A plain `README.txt` would replace a game file and remove it at uninstall.

## What it refuses

The wizard hashes `runblack.exe` in the chosen folder and compares it with the
v1.42 file, as `src/common/exe_check.h` does inside the mod. It refuses any
other build, by name where the build is known:

```
This copy of Black & White is version 1.10.
...
2. Get the unofficial v1.42 patch from bwfiles.com.
```

Setup writes nothing in that case. The addresses of the mod fit one file. On
a 1.10 or 1.30 file with the copy protection removed, the hooks would land in
unrelated code.

## What it takes care of

- **A `ddraw.dll` that is already there** belongs to the player. It can be
  DDrawCompat, dgVoodoo or cnc-ddraw. Setup tells the files apart three ways,
  cheapest first: the SHA-256 that it recorded at the last install, the text
  `bwvr.ini` inside the file, and the product name in the version resource.
  Setup keeps a file that is not the mod as `ddraw.dll.before-bwvr`, and the
  uninstaller puts it back. A backup that is the file of the mod is deleted,
  so the game cannot load the mod with nothing behind it.
- **Write rights.** The game is often in Program Files, where the launcher
  cannot save. Setup gives the Users group modify rights on the game folder
  itself, with no inheritance, so the files of the game keep their rights.
  `dxwrapper\dxwrapper.ini` gets the same on its own.
- **VirtualStore.** `runblack.exe` has no manifest, so in Program Files Windows
  redirects its writes to `AppData\Local\VirtualStore`. A `bwvr.ini` there
  hides the real file from the game. Setup moves those copies aside, under
  every profile, because an elevated Setup can hold the hive of a different
  account. It moves them, never deletes them, and touches only the three file
  names that the mod writes.
- **The registry values of the game.** Setup writes none of them, because an
  elevated Setup can hold the hive of another user. The launcher runs as the
  player and writes them. Before its first write it copies `ScreenW`,
  `ScreenH`, `FullScreen` and `ScreenD` into a `[game_before]` section of
  `bwvr.ini`. The uninstaller puts them back, but only when the hive that it
  can reach still holds the square size that the launcher wrote.
- **Saved games.** The installer never reads or writes `Profiles\` or
  `Scripts\`.
- **The uninstaller** is in `%APPDATA%\Black & White VR Mod`, not in the game
  folder, where an `unins000.exe` would look like a file of the game. At
  uninstall, `bwvr.log` and the probe files are deleted. `bwvr.ini` is kept
  unless the player agrees to delete it, after a question.

## Tests

A silent install and uninstall into a throwaway folder with a foreign
`ddraw.dll` and a game `readme.txt` must give these results:

- Every file lands, and `RealDllPath` in both ini files points to
  `{syswow64}`.
- The foreign `ddraw.dll` is kept, and is back in place after the uninstall.
- `readme.txt` and `runblack.exe` are not changed.
- A 1.10 game is refused, and Setup writes nothing.
- The test build leaves the registry as it was.

Not tested yet: an elevated install into Program Files, and the wizard pages
themselves, which no silent run draws.
