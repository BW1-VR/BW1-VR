# bwvr: the source of the Black & White VR Mod

The [README at the root](../README.md) says what the mod is, how a tester
gets a build, and how a contributor starts. This file is the short map of
this folder.

## Build by hand

`..\dev.ps1` does these steps for you. To do them yourself:

```
cmake -S bwvr -B bwvr/build -A Win32
cmake --build bwvr/build --config Release
bwvr\build\test\controller_math\Release\bwvr_controller_math_test.exe
```

Everything is 32 bit, because the game is. The code of other projects comes
from `..\third_party` (MinHook, the OpenXR headers and the 32 bit loader,
dxwrapper, cgltf). It is in the repo, so there is nothing to clone.

The build makes:

| File | What |
| --- | --- |
| `build\src\mod\Release\ddraw.dll` | The mod |
| `build\src\mod\Release\openxr_loader.dll` | The 32 bit OpenXR loader, copied beside the mod |
| `build\src\launcher\Release\Black & White VR Mod.exe` | The launcher |
| `build\test\controller_math\Release\bwvr_controller_math_test.exe` | The math tests |

## The files in a game folder

The installer (`tools\package.ps1`, `installer\bwvr.iss`) and `..\dev.ps1
install` put the same files beside `runblack.exe`:

```
ddraw.dll                      the mod
openxr_loader.dll              the OpenXR loader
Black & White VR Mod.exe       the launcher
dxwrapper\ddraw.dll            the dxwrapper stub
dxwrapper\dxwrapper.dll        dxwrapper
dxwrapper\dxwrapper.ini        its settings
dxwrapper\ddraw.ini            the settings of the stub
```

The mod writes `bwvr.ini` (the settings, made by the launcher), `bwvr.log`
and up to 20 files `bwvr-old-<date>-<time>.log` in the same folder. It
checks the SHA-1 of `runblack.exe` before it hooks anything: only the file
of the unofficial patch v1.42 is accepted.

## The folders

| Folder | What |
| --- | --- |
| `src/mod/` | The mod: hooks, the hands, the God view, the sound, the cut scene window |
| `src/vr/` | The OpenXR session, the swapchains and the layers |
| `src/launcher/` | The launcher |
| `src/proxy/`, `src/probe/`, `src/xrtest/` | The ddraw proxy, an early probe, a test of the OpenXR path with no game |
| `src/common/` | The addresses in the game, the exe check, the version |
| `test/controller_math/` | The math tests |
| `tools/` | `package.ps1` (the installer), `check_repo.ps1`, `desk/` (the desktop test runs), `hand_export/`, `re.ps1` and `disasm.py` (they read the code of the game) |
| `ghidra/` | The two Ghidra scripts that `re.ps1` runs |
| `installer/` | The Inno Setup script, the player readme, the notices |
| `docs/` | One document for each area, and `roadmap.md` |
