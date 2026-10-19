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
| `dxwrapper\ddraw.dll`, `dxwrapper.dll`, `dxwrapper.ini`, `ddraw.ini` | The render backend of the renderer Direct3D 9: for a PC with no Vulkan 1.3, and for "Play, no VR". The renderer Vulkan does not load it. The stub keeps the name `ddraw.dll` because dxwrapper reads its own file name to pick its mode. |
| `Black & White VR Mod readme.txt`, `Black & White VR Mod notices.txt` | The player readme (`installer/README.txt`) and the third party licenses (`installer/Notices.txt`). |

No `bwvr.ini` ships. The defaults of the mod are the tested setup, and the
launcher writes the file when the player saves. Setup never changes an
existing `bwvr.ini`.

The game folder already holds a `readme.txt` from the game. Windows ignores the
case of a file name, so the readme of the mod installs under a name of its own.
A plain `README.txt` would replace a game file and remove it at uninstall.

## What a PC needs

The mod needs no runtime that the player installs. A fresh Windows 10 or 11
has each file that the game, dxwrapper, the mod and the OpenXR loader use.
So Setup and the launcher check for no runtime.

The DirectX End-User Runtime of Microsoft (June 2010) adds only libraries
that are not part of Windows: `D3DX*.dll`, `D3DCompiler_*.dll` up to number
43, XInput 1.1 to 1.3, XAudio2 up to 2.7, `X3DAudio*.dll`, `XACTEngine*.dll`
and Managed DirectX 1.1. It does not install `d3d9.dll`, `ddraw.dll`,
`dinput.dll` or `dsound.dll`. These are parts of Windows. No file that ships
and no file of the game imports or loads a library of the runtime.

| File | Its import table | What it loads by name |
| --- | --- | --- |
| `runblack.exe` | `DDRAW.dll` (the mod), `DINPUT.dll`, `DSETUP.dll` and the other DLLs of the game folder, Windows DLLs | no DirectX DLL |
| The DLLs of the game folder and of `Plug Ins` | Windows DLLs and DLLs of the game | `dsound.dll` (`binkw32.dll`), `dinput.dll` (`IFC22.dll`) |
| `ddraw.dll` (the mod) | `d3d11.dll`, `dxgi.dll`, `openxr_loader.dll`, Windows DLLs | `d3d9.dll`, `dxwrapper\ddraw.dll`, and the system `ddraw.dll` as a fallback |
| `openxr_loader.dll` | `advapi32.dll`, `kernel32.dll` | the OpenXR runtime that the registry names |
| `dxwrapper\ddraw.dll` | `kernel32.dll`, `user32.dll` | `dxwrapper.dll` in the same folder |
| `dxwrapper\dxwrapper.dll` | Windows DLLs | `d3d9.dll` of the system folder and other Windows DLLs, but no D3DX file (see below) |
| The launcher | Windows DLLs | no DirectX DLL |

The game calls `DirectXSetupGetVersion` of its own `DSETUP.dll` at
`0x00641CD8` (`pc_main`). Below version 4.07 (DirectX 7) it shows a message
and stops. The plain game makes the same check, so a PC that starts the
plain game passes it. The game holds the class IDs of DirectPlay, but no
code of the game uses them. Its multiplayer DLL uses Winsock. So the game
does not need the DirectPlay feature of Windows.

**D3DX is inside dxwrapper.** `dxwrapper.dll` holds two DLLs of Microsoft in
its own file: `d3dx9_43.dll` 9.29.952.3111 (the June 2010 file without its
signature) and `d3dcompiler_47.dll` 10.0.18362.1. At start, `LoadD3dx9` in
`Libraries/d3dx9.cpp` of the dxwrapper source loads both from memory with
MemoryModule. dxwrapper never looks for a D3DX file on the disk. So each PC
runs the same D3DX code, also for `D3DXLoadSurfaceFromSurface` (a copy
between two textures, `backend.md`). The two images import only
`msvcrt.dll`, `gdi32.dll`, `kernel32.dll`, `advapi32.dll`, `rpcrt4.dll` and
the API sets of the Universal C Runtime. Windows 10 and 11 have all of them.
If the load from memory fails, dxwrapper writes "failed to load d3dx9
module" in its log, and its first D3DX call goes to a null pointer.

**No Visual C++ runtime.** The mod and the launcher link the C runtime
statically (`CMAKE_MSVC_RUNTIME_LIBRARY` in the top `CMakeLists.txt` of
`bwvr`). The import tables of `openxr_loader.dll` and of dxwrapper name no
C runtime DLL. The game has its own MFC and C runtime DLLs in its folder.

A new version of dxwrapper or of the OpenXR loader can change this. For
each file in `out/stage`, `dumpbin /dependents` shows the import table. For
a new dxwrapper, also look in its source for a load of a D3DX file by name.

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
  uninstall, `bwvr.log`, the probe files, `bwvr-hint-counts.ini` (the
  counts of the hint cards, `controller-hints.md`, 9.10) and the folder
  `bwvr-host` of the Steam Frame add-on (`backend.md`) are deleted.
  `bwvr.ini` is kept
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
