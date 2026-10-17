# Experimental game bridge for Steam Frame

`xr_bridge=1` sends OpenXR work from the 32 bit mod to an x64 Windows host.
`xr_bridge=0` keeps the local OpenXR path and is the default. Change this
setting with the game closed and restart. A live switch is not implemented
because the session and all shared resources belong to that backend.

The game runs on Steam Frame with Proton 11 ARM64 and SteamVR/OpenXR 2.18.2.
Stereo, controller input, shared GPU textures and session shutdown work.
The host submits hint cards, subtitles and cut scene layers. Full control
coverage, haptic feel, device loss and long sessions still need tests.
Performance can be below the headset refresh rate. This is experimental
support, not a promise that each Proton or SteamVR version works.

## Install the experimental artifact

Use your own game with patch v1.42 and the normal mod installer. Install the
mod in the same Proton prefix as the game. Download `steam-frame-<build>.zip`
from the same release as the installer. For a pull request, open its Build
run and download the installer and `steam-frame-addon` artifacts. Extract
the add-on artifact, then extract the ZIP inside it. Match the build IDs.
No Windows development machine is needed to download these builds.

The add-on needs a working Steam Frame game launcher with Proton 11 ARM64,
SteamVR and its OpenXR runtime. It does not install Proton, SteamVR or
FramePort. The tested launcher uses FramePort and its OpenXR compatibility
layer. Keep its prefix, display session and API layer settings. Other
launcher setups need testing.

With the game closed, keep a copy of the installed mod's `ddraw.dll` outside
the game folder. Copy the artifact's `ddraw.dll` beside `runblack.exe` and
copy the whole `bwvr-host` folder there. Keep the installed mod's other
files, including its 32 bit `openxr_loader.dll` and dxwrapper folder.
Do not copy the x64 loader out of `bwvr-host`.

The folder layout is:

```text
game/
  runblack.exe
  ddraw.dll                 experimental 32 bit mod
  openxr_loader.dll         existing 32 bit loader
  bwvr.ini
  dxwrapper/                existing backend
  bwvr-host/
    bwvr-xr-host.exe        64 bit OpenXR process
    openxr_loader.dll       64 bit loader
    LICENSE
```

In the existing `[bwvr]` section of `bwvr.ini`, set:

```ini
xr_bridge=1
```

Do not replace the whole ini. Use the FramePort launcher script with:

```bash
export WINEDLLOVERRIDES="ddraw=n,b;openxr_loader=n"
export PROTON_LOG=1
```

Start through your existing launcher and select Play in VR. The mod starts
the host itself in the same Proton prefix. Do not run the host by hand.
The bridge does not start the game, install a runtime or change registry
settings. The launcher and the game retain their existing behavior.

Return `bwvr.log`, `bwvr-host/bwvr-host.log` and the Proton log. First check
startup, the logos and menus. Report whether both eyes show an image and
whether controller pointing and buttons work. Then check the scene, head
motion, panels, subtitles, hint cards, the ring menu and haptics. Do not
use an existing saved game as a development test. Automated game tests
must use `bwvr/tools/desk/desk_run.ps1` and its profile safeguards.

To return to the local runtime, set `xr_bridge=0` with the game closed.
To return to the release, restore the mod DLL you kept. The add-on does not change saves or profiles. Remove its `bwvr-host` folder
when you no longer use it, with the game closed.

## Transport and limitations

The host owns the OpenXR instance, session, swapchains, input and layers.
The mod owns a local D3D11 device on the same adapter. Its existing GPU
bridge opens the D3D9Ex textures on that device. The host opens their legacy
shared handles and waits for its GPU copy before acknowledging each texture.
The mod keeps its existing waits for the producer GPU.

The named mapping has a version, size and request sequence. Events pass
ownership between processes. Full eye and hand state, display time, input
focus and recenter events return at BeginFrame. The game sends render poses,
projection options, panels, cards, cut scene windows and haptics. CPU image
fallback and card payloads have a 32 MiB limit per request. Card pixels
remain alive in the host until EndFrame.

Submission is synchronous in bridge mode, including when `frame_send=2`.
The native threaded submission remains available with `xr_bridge=0`.
There is no live backend switch and no automatic fallback to a local runtime
when the host fails. Failed or timed out requests stop VR submission. The
initialization timeout is 30 seconds, other requests have 5 seconds and
host GPU copies have 2 seconds. Shutdown signals the host. If it does not
exit in 3 seconds, the mod ends only the host process that it started.
The host also watches the parent process between requests. When process
jobs are available, closing the parent also ends its host. If Proton cannot
create that job, the mod logs the limitation.

CI builds the mod and host, runs the mod math checks and runs 120 synthetic
x86 to x64 host exchanges without OpenXR. This checks payloads, pose layout,
64 bit values, command sequencing and rejection of an incompatible version. It does not test rendering, layers,
input behavior, game compatibility, device loss or headset comfort.

## Graphics settings on Frame

Keep the render size near the runtime recommendation to limit GPU cost.
The launcher can produce a wider image than the selected square size.
Read `game renders at` in `bwvr.log` for the actual size.

To disable MSAA, change both existing entries in `dxwrapper/dxwrapper.ini`
with the game closed:

```ini
[Dd7to9]
DdrawAllowMultiSampling=0

[d3d9]
AntiAliasing=0
```

Keep the other settings. The next log must say `multisample type 0`.
`AntiAliasing=0` alone does not disable the Dd7to9 multisampling path.
These are optional player settings. The add-on does not overwrite them.

## Build and checks

The normal Build workflow calls `Steam Frame build` for pull
requests and main. The release workflow waits for those checks and attaches
the add-on ZIP beside the installer. The standalone workflow also accepts
manual runs. It builds the x64 loader from OpenXR-SDK release-1.1.43 and
includes its license. The host and the game DLL come from the same commit.
The ZIP contains no game files, player ini, probe executables or run logs.
