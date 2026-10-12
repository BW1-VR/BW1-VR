# The render and VR backend

This document covers the render side of the mod: the `ddraw.dll` proxy, the
Direct3D 9 backend, the hooks in the game's renderer, the two eye passes, the
bridge to Direct3D 11, OpenXR, the head pose, the pointer map, the hotkeys and
the ini keys of this area.

Summary:

- The game (`runblack.exe`, unofficial patch v1.42 only) draws with
  DirectDraw 7 and Direct3D 7. The mod is a `ddraw.dll` proxy. It loads
  dxwrapper (`Dd7to9`) as the backend, which turns Direct3D 7 into Direct3D 9Ex.
- The game transforms every vertex on the CPU and submits screen space
  vertices. The mod walks the scene twice per frame, once per eye, and changes
  the camera between the two walks.
- The two eye pictures stay on the GPU. `gpu_bridge.cpp` copies them through
  shared textures into the OpenXR swapchain, which uses a Direct3D 11 device.
- The head pose from OpenXR moves the game camera in a levelled frame. The
  pointer map keeps the Hand and the interface in step with that camera.
- The game is draw call bound, not pixel bound. The game code and the driver
  share the cost of the scene walk.

Other docs: `software-tnl.md` (the CPU transform and the camera matrix),
`stereo-artifacts.md`, `address-map.md`, `god-view.md`, `controllers.md`,
`hand.md`, `ui-layer.md`, `launcher.md`.

## Source files of this area

| File | Job |
| --- | --- |
| `src/proxy/ddraw_proxy.cpp` | The `ddraw.dll` stub. Loads the backend named by `backend` |
| `src/mod/mod_main.cpp` | Settings (`ReadSettings`), hook installation, the log, the shutdown |
| `src/mod/gpu_bridge.cpp` | Shared textures, Direct3D 9 to Direct3D 11 copy, depth format hook |
| `src/mod/frame_send.cpp` | The way of the eye pictures to the headset: when the mod waits for the GPU, and on which thread (`frame_send`) |
| `src/mod/draw_merge.cpp` | One draw call for a row of draw calls of the game with one state (`draw_merge`) |
| `src/mod/present_watch.cpp` | Skips the copy to the monitor |
| `src/mod/stereo.cpp` | The scene walk twice, the hotkeys, the view setup |
| `src/mod/head_camera.cpp`, `head_pose.cpp`, `eye_camera.cpp` | The head on the camera, the per eye matrix |
| `src/mod/fov.cpp` | `LH3DTech::ChangeFov` hook |
| `src/mod/pointer_map.cpp` | The pointer in render pixels, the head lock of the cursor |
| `src/mod/cursor_trace.cpp` | A test aid: the log names each caller that touches the cursor of Windows or takes the front |
| `src/mod/exit_test.cpp` | A test aid: the game ends by itself at one frame, and its writes to the registry are dropped. The same guard from the start of the mod, for a run that loads a test save |
| `src/mod/game_start.cpp` | A test aid: one line of the log says how the game started (a new game or a saved game) and how long the start took |
| `src/mod/comfort_camera.cpp` | Snap turns, jumps, zoom steps |
| `src/mod/script_camera.cpp` | The camera of the story |
| `src/mod/draw_hooks.cpp` | Hints, tooltips, subtitles, interface depth and size |
| `src/mod/eye_clean.cpp` | The frame before of each eye: the clean of each eye pass, and the keeps of the eye pictures |
| `src/mod/bubble_card.cpp` | A speech bubble of the game as one card at the depth of its sign |
| `src/mod/sky_detail.cpp`, `water_mode.cpp` | Clouds, sun glare, detail distance, the sea |
| `src/mod/spirit_anchor.cpp` | The advisors |
| `src/mod/near_plane.cpp` | The near plane |
| `src/mod/frame_timers.cpp`, `eye_capture.cpp`, `draw_probe.cpp` | Timing, eye pictures, draw probe |
| `src/common/exe_check.h`, `version.h` | The build check and the product name |

## How the game draws

### The draw functions

The game transforms and clips on the CPU. Every draw path submits FVF
`0x01C4` (XYZRHW, diffuse, specular, one texture). `Vertex3D` is 32 bytes:
x, y, z, rhw, diffuse, specular, u, v. It is a screen space vertex, in spite of
the name. `LH3DRender::DrawTriangle` sets FVF `0x1C4` in its
`DrawIndexedPrimitive` call. See `software-tnl.md`.

| Function | Address | Convention | Role |
| --- | --- | --- | --- |
| `LH3DRender::OpenD3D` | `0x0082CD80` | | Creates the device. Starts `81 EC 9C 01 00 00`. Returns 0 on success |
| `LH3DRender::FinishFrame` | `0x0082F460` | | End of the frame. It runs inside `GGame::Process3dEngine` |
| `LH3DRender::DrawTriangle` | `0x0082F810` | `__fastcall` | Main 3D path |
| `DrawAndClip` | `0x0082A500` | `__cdecl` | Software clipped 3D geometry at the frustum edge |
| `DrawAndClip2D` | `0x0082A5B0` | `__cdecl` | The 2D and interface route |
| `LH3DRender::StartFrame` | `0x0082F0E0` | | Start of the frame. `g_started_frame` is at `0x00ECA644` |
| `LH3DRender::RegisterFinishFrameCallback` | `0x0082F2C0` | | End of frame callbacks. `RemoveFinishFrameCallback` is at `0x0082F3B0`. `g_enable_callbacks` is at `0x00C386D0` |
| `LH3DRender::Direct3DDevice7` | `0x00ECA638` | | A global with the `IDirect3DDevice7` |
| `LH3DRender::SetProjMatrix` | `0x0082B2D0` | | Near plane, far plane and field of view as floats |
| `GCamera::SetCameraFov` | `0x00443680` | | The high level field of view entry |

`DrawAndClip2D` is a named function for the interface, so the mod needs no
vertex format heuristic to find it. `0x0082B3C0` and `0x0082B570` are the
`Open` and `Close` of the renderer, not the frame bracket. The image loads at
`0x00400000`.

The device is `IID_IDirect3DHALDevice`, never `IID_IDirect3DTnLHalDevice`. It reports hardware transform and
lighting, and the registry value `HardwareTnL` does not change the device GUID.
The game does not need hardware transform.

### The matrices

`g_d3d_world2view` (`0x00EC8010`) and `g_d3d_view2proj` (`0x00EC7FD0`) are
Direct3D copies. `LH3DTech::UpdateCamera` writes them through
`LH3DRender::SetD3DMatrix`. The scene does not read them. The engine sets
`D3DTRANSFORMSTATE_VIEW` about once a frame and never sets `WORLD` or
`PROJECTION`.

The matrix that the scene uses is at `0x00EA9E40`: 4 rows of 3 floats, row
vector convention, the camera axes in the columns. `LH3DTech::UpdateCamera`
(`0x00819920`) builds it. A change to this matrix from a hook on `UpdateCamera`
changes the picture. A change to a Direct3D copy does not.

The game's field of view is 1.2217 radians (70.0 degrees), asked once per
frame through `LH3DTech::ChangeFov` (`0x008195B0`). The vertical angle follows
the render aspect: `tan(v) = tan(h) * height / width`.

`0x00EA9E80` is not a camera. It is the light in camera space. A light setter
at `0x0081E1F0` stores the world position at `0x00EA9E90` and the same point
transformed by the scaled camera matrix at `0x00EA9E80`.

### The frame

`GGame::Loop` (`0x0054CF20`) runs once per frame. Its only caller is
`GGame::StartGame` (`0x0054C190`). In call order:

| Step | Address |
| --- | --- |
| `ControlMap::ProcessActionsPerformed` | `0x00470AB0` |
| `GGame::ProcessBufferedKeys` | `0x0054C420` |
| `GGame::ProcessNetworkPackets` | `0x0054CC30` |
| `GGame::fn_005525E0`, the network flush | `0x005525E0` |
| `GGame::ProcessGraphicsEngine` | `0x0054D850` |
| `GGame::ProcessFrameInputs` | `0x0054C340` |
| `GGame::FlipScreen` | `0x0054D800` |

`ProcessGraphicsEngine` takes two arguments and the others take none. The
callee cleans the stack, so a stub with the wrong argument count corrupts it.

Inside `ProcessGraphicsEngine`: `LHMouse::UpdateDeltaPos` (`0x007E4810`),
`GCamera::Update` (`0x00441F80`), `GInterface::PreDrawProcess` (`0x005CE9E0`),
`GGame::Process3dEngine` (`0x0054DA80`, the hooked call),
`GInterface::PostDrawProcess` (`0x005CEAB0`) and `HelpSystem::PostDrawProcess`
(`0x005C5EB0`).

These addresses came from a search of the decompilation and the binary. Confirm
one before you rely on it.

### The present chain

`GGame::FlipScreen` calls `LHScreen::Flip` (`0x007DE090`), which calls
`LHScreen::LHFlip` (`0x007DE580`). That function branches on the window mode:

- windowed, at `0x007DE58D`: `IDirectDrawSurface7::Blt` with `DDBLT_WAIT`, a
  copy of the back surface to the primary surface
- full screen, at `0x007DE5D0`: `IDirectDrawSurface7::Flip`

The game runs windowed, so it takes the copying branch. The mod counted zero
`Flip` calls in about 13,000 frames. See "The copy to the monitor".

### The simulation runs at 10 Hz

`GGame::ProcessNetworkPackets` asks `GGame::LocalTimerSaysDoATurn`
(`0x0054C4A0`) whether a game turn is due. A single player game allows at most
one turn per frame. The turn is `GGame::ProcessOneGameTurn` (`0x0054D620`). It
runs `StartTurn` (`0x0054E4F0`), `ProcessTurn` (`0x0054E5C0`) and `EndTurn`
(`0x0054E960`). The script machine is `GScript::Process` (`0x006EB6B0`), called
only from `ProcessTurn`. Audio is `GAudio::ProcessAudioGameTurn` (`0x00427080`),
called only from `EndTurn`.

At 90 fps about one frame in nine carries a whole game turn. The cost arrives as
a spike, and a spike is a dropped frame in a headset. Measure the per frame
maximum, not the mean. If the spike is large, the answer is to move the turn off
the render thread.

### The calling convention rule

Wrong conventions in a hook crashed the game in both directions:

- `LH3DTech::ChangeFov` is `__stdcall` and ends `C2 04 00`. A `__cdecl`
  replacement leaves every caller 4 bytes out.
- `LH3DTech::UpdateCamera` is `__fastcall` and ends `C3`. Read as `__cdecl`, the
  hook passes garbage pointers to the original.

Rule for each new hook:

1. Read the ret. `C3` means the caller cleans the stack. `C2 n` means the callee
   cleans n bytes.
2. Read the first instructions. `mov esi, edx` and `mov edi, ecx` at the top mean
   the arguments arrive in registers. Then `C3` says nothing about `__cdecl`.

A member function with no arguments is safe either way.

### Mip levels

The engine has no concept of a mip level. It makes single level surfaces and
makes 228,748 `MINFILTER` and `MAGFILTER` calls in a session with zero
`MIPFILTER` calls. The strings `MIPMAP`, `MipMap`, `mipmap` and `MIPFILTER` do
not appear in `runblack.exe`. So extra levels in a game file would never be
allocated. Mips work because the mod forces `D3DTSS_MIPFILTER` on beside every
`MINFILTER` call (`force_mips=1`) and the backend supplies the chain.

### The sea

The sea is `LH3DWater`, an unnamed module at `0x00879000` to `0x0087A400`, found
by its own Report3D strings ("LH3DWater::Open already open" at `0x00C39924`). It is
not world geometry. `LH3DWater::Draw` (`0x00879930`, `__fastcall`, material in
ecx) draws a screen space band of horizontal scanline strips with
pre-transformed XYZRHW vertices. Depth test is ALWAYS and depth writes are off,
so nothing occludes it. Every strip is full width, x is 0 and `res_x`, and rhw
is 1.0. The texture coordinates come from unprojecting each screen row onto the
plane y=0.

| Address | What |
| --- | --- |
| `0x00879930` | `LH3DWater::Draw` |
| `0x0087A090` | The plain fallback: one world space quad of +/-70000 at y=0 |
| `0x008792E0` | `LH3DWater::Open`, fills the strip buffer at `0x00FA9358` |
| `0x00C38228` | The water tiling float. Non-zero picks the scanline path |
| `0x005E4E8C` | Where `GLandscape::Draw` calls the water |

The scanline path assumes the camera has no roll, in four ways. The strips are
horizontal. Rhw is constant, so the texture interpolates affinely along a row.
One depth per row drives the fade. Nothing rejects a negative ray-plane
intersection, so a row whose ray points up samples the sea behind the camera and
the texture mirrors. With roll the scanline sea keeps a horizontal horizon, the
frame fills with diagonal streaks and a seam crosses the middle. The plain
world space sea tilts with the land, with no streaks and no seam. It has no wave
detail.

So head roll and the scanline sea are one choice. `water_mode.cpp` writes the
float at `0x00C38228` once a frame, because a level load fills it again. The
registry value `WaterTiling` does not reach it. See "Roll and the sea".

The cached camera forward at `0x00EA1DD4` (written by
`LH3DTech::UpdateWorldToCamera` at `0x00819773`) is read by the water at
`0x008799F6` and `0x008799FF`, only as a sub pixel swell. `LH3DSky::Draw`
(`0x0086B010`) is ordinary world geometry from `data\weathersystem\sky.l3d`.

### Where the Hand comes from

The Hand sits where the cursor ray meets the world. The ray is cast through the
camera:

```
GGame::Process3dEngine 0x0054DA80
  0x0054DEAA  call [vtable+0x18]         CHand::PrepareForDrawing 0x0046C550
    0x0046C6C5  hand->screenXY <- the mouse globals 0x00E852C0 / 0x00E852C4
    0x0046C921  call [state vtable+0x0C] HandState::Update
      0x005B718F  ScreenPointToWorldAtDistance 0x0074CAF0
        0x0081B370  UnprojectScreenPoint, the reverse of ProjectPoint
```

`UnprojectScreenPoint` reads six things: the half resolution at `0x00E839F0`, the
field of view scalars at `0x00C3812C` and `0x00C38130`, the near plane at
`0x00E839E0`, the 3x3 rotation of the unscaled matrix at `0x00EA1D28` (applied
transposed) and the camera position at `0x00EA1DB8`. `0x00C5E680` holds the
Hand's world position.

### The camera cannot express roll

`GCamera` works with distance, heading and pitch
(`SetPointFromPointDistanceHeadingAndPitch` `0x00442810`,
`GetHeadingAndPitchFromPoints` `0x004428D0`, `GCamera::Update` `0x00441F80`).
`CameraMode` holds `GCamera* camera` at offset `0x4`. Only the first three of
its 18 virtual slots are recovered: the destructor `0x0044A3C0`,
`CanPlayerGestureWhenCameraMoving` `0x0044A290` and `Update` `0x0044A2A0`.
`CAMERA_MODE_HAND_STATUS` has `NORMAL`, `ZOOMING`, `TILT_ON`, `GRABBING_LAND`,
`PANNING`, `TILTING` and three unnamed values. `CAMERA_MODE_MOUSE_STATUS` is
`NONE`, `LEFT`, `MIDDLE`.

## The backend

### Backends compared

| Backend | Runs the game | 720p, headless | Frame reachable on the GPU |
| --- | --- | --- | --- |
| System DirectDraw | Yes | 168 fps | No, and it never can be |
| **dxwrapper, Dd7to9** | **Yes** | **209 fps** | **Yes, and it is in use** |
| d7vk 2.2 | Yes | 82 fps | No, it hides its internals |

System DirectDraw keeps the frame where only the CPU can reach it.

d7vk renders the game correctly. It is not used for two reasons. It turns on
vertical sync by default (`d3d9.presentInterval = 0` in `dxvk.conf` removes the
limit). And it hides its internals. It is a proxy over the DXVK Direct3D 9
backend, and the mod asked the Direct3D 7 device and its render target for all
nine DXVK interop interfaces (`ID3D9VkInteropDevice` and
`ID3D9VkInteropTexture` among them). Every one was refused. Reaching the frame
needs a fork. A fork stays an option for later, because OpenXR binds a Vulkan
image with no sharing step. d7vk also offers `ddraw.emulateFSAA = Forced`.

### dxwrapper

dxwrapper works out what to wrap from the name of the stub that loaded it, and
it loads `dxwrapper.dll` from the stub's folder. Loading `dxwrapper.dll`
directly skips that, and the game crashes about 50 ms later with a call through
a null pointer. So the stub keeps its name and moves into a folder:

```
Black & White Game/
  ddraw.dll                 the mod, which the game loads
  dxwrapper/
    ddraw.dll               dxwrapper's own stub
    ddraw.ini               the stub's settings
    dxwrapper.dll
    dxwrapper.ini           the real settings
```

Set `RealDllPath` to the system library by hand in both ini files. `AUTO`
finds the mod's own `ddraw.dll` and loads it again. `bwvr/config/dxwrapper.reference.ini`
holds the shipped values. The important ones:

| Key | Value | Why |
| --- | --- | --- |
| `Dd7to9` | 1 | Direct3D 7 to Direct3D 9 |
| `D3d9to9Ex` | 1 | A plain Direct3D 9 device cannot share a texture |
| `DdrawForceMipMapAutoGen` | 0 | See "Mip chains cost frame time" |
| `ForceMipMapUsage` | 1 | In `[d3d9]`. Builds a chain once |
| `AnisotropicFiltering` | 16 | |

### Settings

```ini
[bwvr]
backend=                        ; the default: dxwrapper\ddraw.dll when it is beside the game
backend=dxwrapper\ddraw.dll
backend=system                  ; system DirectDraw
gpu_bridge=1                    ; use the GPU path when the backend allows it
```

A name with no path is searched beside the game first. If the chosen backend
does not export a function, the proxy falls back to system DirectDraw for it.
System DirectDraw loads only when a gap needs it, because dxwrapper behaves
differently when the system library is already in the process. A first install
needs no ini: the keys read by `GetPrivateProfileIntA` have their defaults in the
code.

On a build of the game other than v1.42, `DllMain` installs no hook, and
`ProxyForceSystemDDraw` makes the proxy load system DirectDraw. See "The build of
the game is checked".

## The GPU bridge

The bridge gives the eye pictures to OpenXR without a CPU readback.

```
game draws  ->  Direct3D 9 render target
                StretchRect        (graphics memory to graphics memory)
            ->  a shared texture
                CopyResource       (graphics memory to graphics memory)
            ->  the OpenXR swapchain image
```

A readback (lock the DirectDraw target, write it into a Direct3D 11 upload
texture) crosses the bus twice for each eye, and its cost grows with the
resolution: 0.41 ms at 1280x720, 0.83 ms at 1920x1080 and 1.34 ms at 2560x1440
for the lock, for each eye. The game's own drawing does not grow with
resolution (4.33 to 6.52 ms at every size measured). A Quest 3 eye is 2064x2208,
so the readback would fill the 11.1 ms budget of a 90 Hz frame.

`gpu_bridge.cpp` watches the backend create its Direct3D 9 device, makes one
shared texture for each eye, opens both on the Direct3D 11 device of the OpenXR
session and copies into the swapchain. It fails safe to the readback if the
backend is not Direct3D 9, the device is not 9Ex, a shared texture cannot be
made or opened, or the swapchain format does not match.

### The backend can replace its device

dxwrapper makes a new Direct3D 9 device when the window mode or the window
changes. To free the old one it calls `Release` until the count is zero
(`ReleaseD9Device` in `IDirectDrawX.cpp`), which also removes the bridge's own
references. A bridge that keeps only the first device calls `CreateTexture` on a
freed device and the game crashes at `ddraw.dll+0xBF72`. A full screen start
does this: the backend makes a windowed device, destroys it and makes a full
screen one. The game starts full screen when `FullScreen` is missing from the
registry.

So the bridge hooks the device's `Release`. When the count reaches zero, it drops
everything it made on that device and calls none of it. The creation hooks give
it the new device, and `GpuBridgeResume` starts the shared path again. Until
then, the frame goes through the readback. The log shows each device, its size
and whether it is in a window. A device lost after the bridge has started is not
tested.

### Keeping the two devices in step

The two devices have separate command queues, and a shared texture has no
automatic synchronization. The bridge issues an event query after its copy and
waits for it, so the Direct3D 11 device cannot read the texture early.

There are two shared textures, one for each eye. The copy of the left eye is
still queued when the right eye is captured, and one texture would be
overwritten.

A keyed mutex is not available. Direct3D 9Ex cannot create or open a keyed mutex
shared resource, so `IDXGIKeyedMutex::AcquireSync` cannot be reached from the
producer side.

The wait measures the queue, not the copy. `D3DQUERYTYPE_EVENT` with
`D3DGETDATA_FLUSH` drains the whole Direct3D 9 command buffer, so it waits for
all the work that the game queued for that eye. The blit itself measures 0.00 to
0.01 ms. The flush wait at 1080p with a headset is 0.34 ms, and 0.74 to 1.14 ms
while the monitor copy is also running. A headless self test shows 0.01 ms
because it has almost nothing to wait for. Do not compare it to the others.

### One wait a frame, and a second thread

`frame_send` sets when the mod waits for the GPU, and which thread waits
(`frame_send.cpp`). The pixels of a frame are the same with each value.

| Value | The wait | The frame thread |
| --- | --- | --- |
| 0 | After each eye pass. Then the picture of that eye goes to its swapchain image | Does no work in the two waits |
| 1 | One time, after the second eye pass. Then both pictures go to their swapchain images | Walks the scene for the second eye while the GPU draws the first eye |
| 2, the default | As 1, on a second thread. That thread also makes the two copies and calls `xrEndFrame` | Goes on with the game. It calls `xrWaitFrame` for the next frame, and then it waits until the runtime has the frame |

One wait is enough because each eye has its own shared texture, and a device
does its work in the order of its queue. The copy of the interface panel is in
the same queue.

With the value 2, `XrScreen::EndFrameLate` makes the layers and sends the cards
on the frame thread. The second thread gets only the wait, the copies and
`xrEndFrame`. OpenXR permits `xrWaitFrame` for a frame while a different thread
ends the frame before. It does not permit `xrBeginFrame` then, so `BeginFrame`
waits for the second thread after `xrWaitFrame`. While the second thread has a
frame, the frame thread does not use the Direct3D 11 context. Each code path
that writes or frees a shared texture waits for the second thread first
(`FrameSendJoin`): the capture of a menu frame, the end of the bridge, and the
last count of a device that the backend destroys.

The second thread reads the event query while the frame thread can draw. So
the value 2 needs a Direct3D 9 device that permits calls from two threads
(`D3DCREATE_MULTITHREADED`). The log says which device the backend made
(`gpu bridge   : the device permits calls from two threads`). With
`frame_send=2` or `test_keys=1` in the ini, the mod adds that flag when the
backend does not ask for it. On a device for one thread the value 2 acts as 1.
The second thread asks the query about 20,000 times a second, and not without
a stop: the device takes a lock for each call. Each call sends the queue to
the GPU, as the wait on the frame thread does. After 100 ms with no answer the
frame goes out as it is.

For each 450 frames the log has the line `frame send   :`. It gives the mean
time of the wait for the GPU, of the copies and of `xrEndFrame`, and how long
the frame thread waited for the second thread. With the values 1 and 2 the
line `gpu bridge   :` has one wait for two captures. The number `grab x2` of
the line `stereo       :` is the time of the frame thread for the eye
pictures, so it does not have a wait of the second thread.

With each value the log also has the line `frame age    :`: the time from the
end of `xrWaitFrame` to the end of `xrEndFrame`. The runtime shows a frame that
it gets later at a later time. So compare the values by the frame rate and by
this number.

Sessions in a headset with the value 2 showed these points:

- The runtime accepts each frame, and it gives `xrWaitFrame` back for each
  display frame while the second thread ends the frame before.
- The frame thread has 2 to 3 ms less work in a frame.
- The second thread takes 1.0 to 3.3 ms for a frame: 0.4 to 1.3 ms in the wait
  for the GPU and 0.6 to 1.9 ms in `xrEndFrame`. The frame thread waits 0.3 to
  0.9 ms for it before the next frame. On the frame thread `xrEndFrame` takes
  0.4 to 0.7 ms.
- The age of a frame is 8.9 ms when the two scene walks take less than 5 ms,
  and 12.6 to 14.4 ms when they take 8 ms or more.
- The player saw no fault of the picture.

The frame rate against a session with the value 0, by the cost of the two
scene walks (the mean of the windows of 450 frames, at 90 Hz):

| The two scene walks | Value 2 | Value 0 |
| --- | --- | --- |
| Less than 5 ms | 89.2 | 89.5 |
| 5 to 7 ms | 86.1 | 85.4 |
| 7 to 8 ms | 82.1 | 76.0 |
| 8 to 9 ms | 77.7 | 66.8 |
| 9 to 10 ms | 79.0 | 57.4 |
| 10 ms and more | 69.4 | 50.0 |

The two sessions were not in the same scenes, and a row has 3 to 40 windows.
So the gain is small: no gain in a light scene, which holds 90 with each
value, and 6 to 22 frames a second in a scene that does not. When the two
scene walks take 9 ms or more, the frame thread alone is over the 11.1 ms of
a frame at 90 Hz.

Not known: why `xrEndFrame` is slower on the second thread, and the age of a
frame with the values 0 and 1. Not run: the value 1 in a headset (its code
also runs when the device is for one thread, or when the second thread does
not start), and the value 2 with a headset or a runtime other than a Quest 3
through Virtual Desktop. A desktop run sends no frame to a runtime. With
`test_keys=1`, Ctrl, Shift and the slash key of the number pad step the value
while the game runs.

### The self test

The bridge has a self test that needs no headset. It builds its own Direct3D 11
device, runs the same capture and copies the result back only to write a file.
Turn it on with `gpu_bridge=1` and a capture frame in `bwvr-eye-frames.txt`. It
writes `bwvr-shared-*.bmp` beside `bwvr-eye-*.bmp`. The two routes must give the
same bytes.

## The end of the mod

### How the game ends

The close button of the window and Alt F4 give `WM_SYSCOMMAND` with `SC_CLOSE`.
`GameWindowProc` (`0x007DBF30`) answers it with `LHSystem::SetTerminate`
(`0x007DB8A0`), which sets a flag. `GGame::Loop` reads the flag at `0x0054D424`
and sets the state 6 in `GGame+0x250180`. The Quit of the options dialog sets
the same state (`0x005553F0`). A plain `WM_CLOSE` is not this way: the window
goes, the backend makes a new device, and the game runs on with no window.

The exit path, in its order:

1. The game loop ends. The game writes the save of slot 20 (`20.sav.tmp`,
   `Script_20.sav.tmp`, `SaveGame.inf`, two save pictures) and `helpstats.dat`.
2. `pc_main` (`0x00641C60`) calls `GGame::Close` (`0x0054EC80`) at
   `0x00641EFF`. It clears the map. Then it writes the numbers of the profile
   to the registry (`PlayerProfile::WriteBackToRegistry`, `0x0066BCD0`, called
   at `0x0054ECCD`): the play time (`timestarted`), the counts of saves and
   loads, and the options of the profile.
3. The game writes `helpstats.dat` again, and the two sound volumes of
   `BWSetup` in the registry. Then `pc_main` calls `free_system`
   (`0x00643420`) as its last call.
4. `WinMain` returns, and `WinMainCRTStartup` (`0x007CA49F`) calls `exit`
   (`0x007C9EE1`) of the C runtime.
5. `exit` runs the exit list of the C runtime (`_doexit`, `0x007C9F21`). The
   destructor of the static `LHScreen` (`0x007DD850`) is in that list. It calls
   `IDirectDraw7::RestoreDisplayMode` at `0x007DD8B0`, and dxwrapper destroys
   its Direct3D 9 device there (`Release` until the count is zero).
6. `exit` calls `ExitProcess`. Windows stops each thread but this one. Then it
   calls `DllMain` of each DLL, in an order that the mod does not control.

A fault in step 5 does not end the process at once. The exception handler of
`WinMainCRTStartup` (`0x007CA59C`) calls `_exit` (`0x007C9EF2`), which calls
`ExitProcess`. So step 6 can run after the fault, and a second fault can
follow.

Each write of the profile is in steps 1 to 3, so a fault in step 5 or 6 does
not cut a write of the profile. It cuts the log of the mod.

In steps 1 to 3 the game also writes files that are not in the profile folder
[R]. When the game has the Creature of the profile, it writes the mind of
that Creature in the game folder: `Scripts\CreatureMind\<file>` and
`Scripts\CreatureMind\Physique<file>`. `<file>` is the value `file` of the
profile in the registry, a name that ends in `.erc`. A game with no Creature
of the profile, for example a game at the Creature pens, does not write
them. The game also writes `MemoryLeaks.txt` in the game folder.

### What must be true at step 5 and at step 6

- The device of the backend must not be destroyed while the shared textures
  of the bridge are open on a Direct3D 11 device. The last `Release` then
  stops the process with an access violation (`0xc0000005`). Windows names
  the module `unknown` or `nvd3dum.dll_unloaded`.
- No call into OpenXR or Direct3D can come from `DllMain` at the end of the
  process. The threads of the runtime are stopped, and the device is gone. A
  release of the bridge there can stop the process a second time, in
  `d3d9.dll`.
- The OpenXR session and the instance must be destroyed before step 6. A
  runtime that has a session at the end of the process is the likely cause of
  the fault `0xc0000409` in `VirtualDesktop.LibOVRRT32_1.dll`. No run has
  shown this: only a headset session can.

### What the mod does

`mod_main.cpp` hooks `free_system` and `exit`. The first of the two that the
game calls runs the shutdown, one time. `exit` is there for an end that does
not pass `free_system` (the game calls `exit` at 27 places, most of them for
an error). At that time each thread runs, each DLL is loaded, and the device
of the backend lives.

The parts of the shutdown, in their order:

1. The cursor is free again (`ClipCursor`). The glide time, the wheel scale
   and the detail distance of the game get their own values back.
2. With a headset session: the reports, and then `XrScreen::Stop`. Each file
   that holds the screen lets it go first.
3. `GpuBridgeShutdown`: the shared textures on the two devices, and the
   references of the bridge to the device of the backend.
4. `FrameSourceShutdown`.

The hooks stay until the process ends. A thread of the game can be in a hook,
and `FinishFrame` calls the game's function directly after the shutdown.

`XrScreen::Stop` ends a session that runs as OpenXR asks. `xrEndSession` is
valid only in the state `STOPPING`. So the mod calls `xrRequestExitSession`
and reads the events for at most 500 ms until that state comes. Then it calls
`xrEndSession`. Then it destroys the swapchains, the spaces and the session,
releases its Direct3D 11 objects, and destroys the instance. If the state
does not come in time, the destroy calls run all the same.

At the end of the process the third parameter of `DllMain` is not null. The
mod then only makes the cursor free, writes its last line and closes the
log. When the game did not pass `free_system` or `exit` (it called
`ExitProcess` directly), the log says that the shutdown did not run.

### The key and the log

`exit_shutdown=1` (the default) is the behavior above. With `exit_shutdown=0`
the mod has no hook on the exit path, and the whole shutdown runs in
`DllMain`, with a removal of the hooks at its end. With the bridge on, a game
that ends by itself then has the first fault of "What must be true", and in
some runs the second one too.

The log of an end:

```
shutdown     : the shutdown starts, the game frees its system (free_system), frame N
shutdown     : the cursor, the camera values and the detail distance, T ms
final: N frames, N presented, N skipped
  openxr: the end of the session. xrRequestExitSession gave 0, the state STOPPING came, xrEndSession gave 0 (0 is success), T ms
  openxr: the swapchains, the spaces and the session are destroyed, T ms
  openxr: the Direct3D 11 objects of the session are released, T ms
  openxr: the instance is destroyed, T ms
shutdown     : the OpenXR session and its Direct3D 11 device, T ms
shutdown     : the GPU bridge, T ms
shutdown     : the frame source, T ms
shutdown     : all parts are done, T ms
shutdown     : the mod is shut down
```

The lines `final` and `openxr` are there only with a headset session. The
line of a part comes after the part, so a log that stops in the shutdown
names the part before the one that did not come back. `the mod is shut down`
is the last line of a whole log. A log without it is cut: Windows stopped the
process (a desktop run, the Task Manager), or a fault did.

### The test of the end

A desktop run stops the game with `Stop-Process`, and a stopped process never
comes to the exit path. `exit_test_frame=N` makes the game end by itself: at
frame N of `FinishFrame` the mod posts the message of the close button to the
game window (`exit_test.cpp`). The word `exit` of the scripted controller does
the same at a frame of the script (`controllers.md`, 4.2). From that frame to
the end of the process, the mod drops each write of the process to the
registry: `RegSetValueEx`, `RegDeleteValue` and `RegDeleteKey`. The log names
each one, with its key. The log also names each file that the process opens
for a write. `desk_run.ps1` puts those files back: the profile folder, the
folder `Scripts\CreatureMind` and `MemoryLeaks.txt`.
`bwvr/tools/desk/exit_run.ps1` makes the run and reads the faults that Windows
recorded. The launcher removes the key from `bwvr.ini` on each save.

A desktop run has no OpenXR session. With `capture_every_seconds`, the self
test of the bridge makes the shared textures on a Direct3D 11 device of its
own. So a run shows the fault of the device with `exit_shutdown=0`, and a
clean end with 1. Only a headset session shows the end of the OpenXR session
and the record of `VirtualDesktop.LibOVRRT32_1.dll`.

### A test save

A desktop run can start in a saved game that a test run made
(`tools/desk/README.md`, "A fast start"). The mod has five test aids for it.
`[V]` is from the disassembly, `[R]` from a run.

**How the game starts [V].** `ScanParameters` (`0x00641AA0`) knows no word
that loads a saved game. `pc_main` (`0x00641C60`) gives `GGame` a mode at
`GGame+0x25017C` before `GGame::StartGame` (`0x0054C190`), and `GGame::Init`
reads it in a switch at `0x0054F6EF`:

| Mode | When | What `GGame::Init` does |
| --- | --- | --- |
| 0 | No word on the command line | `GameOSFile::AutoLoad` (`0x00563FF0`) loads `Saved Games\20\20.sav` of the profile. When the load fails, the mode becomes 1 |
| 1 | `-NEWGAME`, or no saved game in slot 20 | A new game: `GSetup::LoadMapScript` and `GGame::OnNewGame` (`0x005538E0`) |
| 2 | After a load in play (the Temple, or the quick load, which calls `fn_00793380(19)`) | `GameOSFile::LoadAllGame` (`0x005587B0`) with the file name at `0x00D01838` |

Slot 20 is the saved game that the game writes each time its loop ends
(`SaveGameRoom::InstantSaveGame(20)` at `0x0054D5DD`). It is a whole saved
game of that moment. So the game loads a saved game with no menu only at its
start, and only from slot 20. With the mode 0, `GGame::Loop` opens the Main
Menu over the loaded game before its first frame (`0x0054CF58`, `0x0054D086`).
The game waits there, and "Continue Game" starts the play. A load in play is
not a way for a test: the game then runs `GGame::Close`, which writes the
profile to the registry.

**The Main Menu of the start [V].** `GGame::Loop` sets
`FrontEnd::StartupMenuActive` (`0x00CD0B03`) and shows
`FrontEnd::MainMenuDialog` (`0x00CD065C`). The procedure of that dialog
(`0x00540041`) pauses the game when the menu opens (`PauseGame(1)`). For the
button "Continue Game" it does four things, at `0x005400D1`: `PauseGame(0)`
(`0x0054AE20`), `DialogBoxBase::Hide()` (`0x00513640`), the flag
`StartupMenuActive` to 0, and `FrontEnd::SetCursorOff()` (`0x0053C470`). Each
of the three functions is static and takes no object.

**`menu_skip_test_frame=N`.** At frame N of the scene, `game_start.cpp` does
those four things itself, when the game mode is 0 and the flag of the menu is
set. The loaded game then plays with no click, so a run in a test save needs
no place of a button on the screen, and it does not depend on the render
size. The log has one line `game start   : test key menu_skip_test_frame`.
When the menu is not up at that frame, the mod does nothing and the line says
so. The key is 0 by default, and a player gets the menu. The run scripts give
70 (`tools/desk/dialog.ps1`).

**The registry in the start [R].** In each start, with a new game or with a
load, the game writes the values of the profile to the registry before its
first frame: `Profile` in `LHMultiplayer`, and in the key of the profile
`creature name`, `login name`, `login password`, `stuff`, `player symbol`,
`timestarted`, `numberofsaves`, `numberofloads`, `pitchaboutcentre`,
`pushscroll`, `IMMERSION_KEY` and `HAND_ORIENTATION`. These are the values of
`PlayerProfile::WriteBackToRegistry` (`0x0066BCD0`). When "Continue Game"
closes the Main Menu, the game also writes the key bindings of the profile:
`Key 0 Primary`, `Key 0 Primary Mod`, `Key 0 Mouse Button` and so on to `Key
32`, 99 values. Each number is the number that the game read, so the registry
is the same after such a write. In play the game writes `LoveBuckets`, the
alignment of the player, each 600 game turns (`GPlayer::SavePlayerAlignment`,
`0x0064D280`, called from `GGame::Loop` at `0x0054D419`) [V]. So a test that
changes the alignment, in a run with no guard that is longer than those
turns, changes that value of the profile. Windows writes to other keys in
the game process
too: DirectInput keeps a value for each input device under
`MediaProperties\PrivateProperties\DirectInput`.

**`registry_test_guard=1`.** The guard of `exit_test.cpp` from the start of
the mod. The mod drops each write to a key under `Lionhead Studios Ltd`, and
the log names each one with its key (lines `registry test`). A write to a
different key passes, as in each other run. The log names the first 6 and
counts each one. The log also names each file that the process opens for a
write. `desk_run.ps1 -Save` gives a run this key.

**The line of the start.** `game_start.cpp` writes one line at the first
frame of the scene:

```
game start   : a new game starts, frame 0 (game mode 1, land 1), 8.9 s after the start of the game process
game start   : the saved game is loaded, frame 0. The game loaded the saved game of slot 20, and it opens its Main Menu over it (game mode 0, land 1), 9.6 s after the start of the game process. In the start the mod dropped 17 writes of the game to its keys of the registry, and 41 writes of the process to other keys passed
```

The land is `GGame+0x205A08`. A run script reads from this line that the
load was good: a load that fails gives a new game.

**The clock of the log.** The log has the date of the start only. Three
kinds of lines have a frame of the scripted controller and the seconds from
the start of the game process: the line of the start, each line `stereo` with
the frame rate (one for each 450 frames), and the line of
`fake_controller_play_frame`. `Get-BwvrFrameSeconds` in
`tools/desk/game.ps1` gives the time of a frame from them.

**The word `exit`.** The game writes slot 20 on its exit path. So a run
that ends with the word `exit` leaves a saved game of that frame in the
profile folder, and `make_test_save.ps1` keeps a copy of it before the
profile goes back.

## The depth buffer

The game gets a 16 bit Z buffer unless `depth24=1`. The default is 1.

### Where dxwrapper makes it

Dd7to9 does not use the auto depth stencil of the present parameters
(`EnableAutoDepthStencil` is FALSE unless `DdrawOverrideStencilFormat` is set).
The game makes a DirectDraw surface with the Z buffer caps, and
`m_IDirectDrawSurfaceX::CreateD9Surface` calls `CreateDepthStencilSurface` on
the Direct3D 9Ex device (`IDirectDrawSurfaceX.cpp`, about line 4752). The format
comes from the pixel format of the request. The multisample type comes from
`GetMultiSampleTypeQuality`. A failed create gets one retry with
`GetFailoverFormat`, which keeps D24X8 as D24X8.

dxwrapper makes the Direct3D 9 surface late. `CreateSurface` and
`AddAttachedSurface` only make the DirectDraw object. `CheckInterface` makes the
Direct3D 9 surface at the first use, in a later frame. So the game never sees a
failed create of the Direct3D 9 surface.

### Where the game picks the format

`LH3DRender::OpenD3D` (`0x0082CD80`) calls `EnumZBufferFormats` with the
callback at `0x0082B370`. The callback keeps the entries with `dwFlags` equal to
`DDPF_ZBUFFER` (no stencil) in a table at `0x00EC7E88`, 32 bytes each, with the
count at `0x00ECA674`. The count is never set back to 0. The table holds at most
4 entries. The loop after it takes the entries in order and stops at the first
one that makes a DirectDraw surface. `0x00C386D8` is 1 when the entry is 16 bit
(and 0 when `0x00ECA5F0` is set). dxwrapper lists D16, D24S8, D24X8, D32, each
only when `CheckDeviceFormat` accepts it. So the table is D16, D24X8, D32, and
the game takes D16. The game uses no stencil.

### What the hooks change

With `depth24=1`, a hook on the callback moves the D24X8 entry to the front of
the table. D24X8 has `dwFlags` `DDPF_ZBUFFER`, `dwZBufferBitDepth` 32 (offset
`0x0C`) and `dwZBitMask` `0x00FFFFFF` (offset `0x14`). Offset `0x10` is
`dwStencilBitDepth`, which is 0 for D24X8. The game then asks for 24 bit by
itself, and `0x00C386D8` becomes 0. This holds after a resolution change,
because `OpenD3D` lists the formats again each time.

A second hook, on `IDirect3DDevice9::CreateDepthStencilSurface` (slot 29, in
`gpu_bridge.cpp`), sees every depth surface of the device. For a D24X8 request
of the game it asks `CheckDeviceFormat`, `CheckDepthStencilMatch` and
`CheckDeviceMultiSampleType` with the render target format and the multisample
type of the request. The game cannot fall back at this point, because its loop
ended frames before. So if a check fails, the hook makes D16 in place of D24X8
and logs the reason. dxwrapper then holds a D16 surface that it calls D24X8.
After the create, dxwrapper reads the format of a depth surface only for a depth
fill `Blt`, and the game clears with `Clear`. The hook also keeps the depth of
the game's Z buffer for the near plane (`GpuBridgeDepthBits`). The interface
panel makes its own depth surface, and the hook lets those creates through. The
hook needs `gpu_bridge=1`, because the bridge captures the device. It is active
with `depth24=0` too.

Log lines start with `depth24      :`. One says if the hook is on. One says that the
game tries D24X8 first. One says what the game asked for and what the backend
made. A new line comes only when the format or the size changes:

```
depth24      : ON. The game asks for a 24 bit Z buffer (D24X8). If the adapter cannot make it, the mod makes a 16 bit one
depth24      : the game lists D24X8 as entry 1 of its Z formats. It now tries D24X8 first
depth24      : the game asked for D24X8, 2880x2880, multisample type 4. The Z buffer is D24X8, 24 bit
```

### What reads the depth buffer

Nothing needs 16 bit:

- The sun glare test (`fn_0086BD00`) locks the Z buffer and reads 16 bit words (a
  sample is clear below `0xFDE8`). It does this only when `0x00C386D8` is 1.
  dxwrapper refuses a lock on a video memory depth surface
  (`locking depth stencil surface is not implemented`), so with 16 bit the lock
  fails each frame and the test uses only its ray test (`fn_00802550`). With 24
  bit the game skips the lock and uses the same ray test. The result is the same
  [V, the code at `0x0086BF2B`].
- The interface panel makes its own depth surface (D24S8 first).
- `hand_glow.cpp` and `overlay.cpp` use only the depth test of the device.

### The depth steps

The game writes `z = 1 - near / cz`, so a step of the Z buffer is
`cz^2 / (near * 2^bits)` units. The steps in the room:

| Scale (units per meter) | Near, 24 bit | 24 bit at 1000 units | 24 bit at 6000 units | 16 bit, the game's 3.5, at 6000 units |
| --- | --- | --- | --- | --- |
| 7.5 (life size) | 0.375 (5 cm) | 21 mm | 0.76 m | 21 m |
| 22.5 | 1.125 (5 cm) | 2.4 mm | 85 mm | 7 m |
| 100 | 3.5 (the game's) | 0.17 mm | 6 mm | 1.6 m |
| 400 | 3.5 | 0.04 mm | 1.5 mm | 0.39 m |
| 4000 | 3.5 | 0.004 mm | 0.15 mm | 39 mm |

At every scale, the 24 bit buffer with a near plane of 5 cm is at least 27 times
finer than a 16 bit buffer with the game's 3.5 units. At the life size scale the
fog ends at 900 units, so the land at 6000 units is not seen.

### The near plane

`near_plane.cpp` hooks `LandFeature::GetNearClipping` (`0x005E2F30`) in play
modes 0, 3 and 4, where the head moves the camera. The near plane is at most
`near_plane_cm` (default 5) of the room with a 24 bit buffer. It is at most
`near_plane_cm_16` (default 10) with a 16 bit buffer, or while the depth is not
known. The scale is the table scale while the table or the God view owns the
view. At other times it is the life size scale. The game's value stays when it is
nearer. 0 for a key keeps the game's near plane. Play modes 1 and 2 keep the
game's near plane, because the hands are not in the world there.

Why 10 cm at 16 bit: 0.75 units (10 cm at the life size scale) is the near plane
that the game uses with its camera 2.8 units over the land (`0.3 + 0.16 * h`).
At 900 units a 16 bit step is then 16 units. It is 3.5 units with the game's 3.5
and 33 units with 5 cm.

What reads the near plane:

- The overlay hands (`overlay.cpp`), the glow (`hand_glow.cpp`) and the sea disc
  of the table (`tabletop.cpp`) read `0x00E839E0` when they draw. They clip and
  write `z = 1 - near / cz` with the same value as the game.
- `GCamera::Update` sets `0x00C3812C` and `0x00C38130` from the value of the hook
  (`0x004424D0`), so `UnprojectScreenPoint` and the water stay in step.
- The adaptive interface depth (`draw_hooks.cpp`, `stereo.cpp`) counts `3.5 / cz`
  in its histogram and shifts by `1 / cz`, so the near plane does not change the
  shift. Geometry nearer than 3.5 units stays out of the histogram: the real hand
  and the Hand must not pull the interface to the eyes. The log line is
  `interface    : nearest world N units`.
- The flat interface has z 0 to 0.33. The world and the interface both get
  `z = 1 - near / cz`, so the interface stays in front of the world. Only a hand
  nearer than the interface can cover it.
- `kNearDepth` (0.9) in `draw_hooks.cpp` is only a filter of a report.
- The game sets a near plane of 0.1 or 0.2 in three places (`0x00460F24`,
  `0x005F8FE1`, `0x0079693C`) outside `GetNearClipping`. The hook does not change
  those.

To test, use the God view at its widest zoom and at the life size scale, then read
the `depth24      :` and `near plane   :` lines of `bwvr.log`.

## Frame cost

At 1920x1080 in a headset (89.4 fps against a 90 Hz headset, holding), with the
copy to the monitor skipped:

| Part | Cost |
| --- | --- |
| Scene walk, both eyes | 4.20 to 5.18 ms |
| Frame to the headset | 0.34 ms |
| `xrEndFrame` | 0.17 to 0.25 ms |
| Everything outside the scene walk | 0.37 ms |
| Idle, waiting for the headset | 5.04 to 6.03 ms |

The scene walk is nearly all of it, and it does not scale with resolution. A
saving has to come from a cheaper walk, not from pixels. The budget of a 90 Hz
frame is 11.11 ms.

`LH3DRender::FinishFrame` runs inside `GGame::Process3dEngine`, not after it.
Do not use it as a midpoint to split the frame.

### The copy to the monitor

The game runs windowed, so DirectDraw presents by copying the back buffer onto
the primary surface. At 1920x1080 that copy costs 7.90 ms every frame. With a
headset on, nobody sees the result. `present_watch.cpp` skips it while the mod
sends frames to a headset. Nothing reads the primary surface back. With VR off,
the copy stays. The key is `skip_desktop_present` (default 1). The monitor stops
updating while it is active. This is intended.

**A desktop run at the frame rate of a headset.** A desktop run has no
headset, and the copy is about 8 ms of its frame, so a run has 55 to 80
frames a second. A headset session has about 90. The test key
`test_frame_rate=N` (20 to 240, 0 is off) gives a desktop run the frame rate
N: from frame 150 of the controllers the mod does not copy the picture to the
monitor, and the call of the copy waits until the frame is 1/N s long. The
window of the game then shows no new picture. The log has one line for each
report window: the frames that waited, and the frames that were longer than
1/N s. `throw_queue.ps1 -Fps 90` in `bwvr/tools/desk` uses the key.

**A copy between two textures.** For a `Blt` to or from a texture,
`m_IDirectDrawSurfaceX::CopySurface` of dxwrapper calls
`D3DXLoadSurfaceFromSurface`. The game makes such a copy when it makes the
two pictures of a save (`0x007923D0`, the second `Blt` at `0x0079260C`, the
rect `0 128 128 192`), between two DXT textures of 256 by 256 pixels in
system memory. The rect is inside both surfaces. On some PCs D3DX then
writes to memory that is not there, and the game stops with an access
violation in `msvcrt.dll`. The first save of a new game is such a save, so
the game can stop when a new game starts. The cause in dxwrapper is not
known.

With `blt_texture_copy=1` (the default) the `Blt` hook of
`present_watch.cpp` makes such a copy itself. It does this for a copy with
no stretch and no effect, when one of the two surfaces is a texture and none
is the primary surface. It locks the two surfaces and copies the pixels, also
between two RGB formats, or the blocks of 4 by 4 pixels of two textures with
one DXT format (`blt_copy_math.h`, checks in
`test/controller_math/blt_copy_checks.h`). Each other copy goes to dxwrapper.
The log has the first two copies of each caller with the sizes, the formats,
the rects and the caller, and the report counts the copies:

```
present      : the mod copies with a texture. Destination 256 x 256, ...
present      : the mod made N copies between two textures, and M failed
```

`blt_texture_copy=0` gives each copy to dxwrapper. Not known: whether a save
picture looks right in the Save Game Room.

Full screen is not an alternative. It would flip instead of copy, but it crashes
the game (one fault at offset `0xdededede`, a freed pointer, and one inside the
Virtual Desktop streaming library). Exclusive full screen and a VR compositor both
want the display.

### What the scene walk is made of

`profile=1` splits the walk. Both passes together:

| Scene | Draw calls | Vertices | Scene x2 | Driver | Game code |
| --- | --- | --- | --- | --- | --- |
| Heavy (a portal, particles, foliage, trees under physics) | 1764 to 1839 | 126,565 to 126,717 | 9.79 to 9.92 ms | 3.66 to 3.76 ms | 6.03 to 6.26 ms |
| Ordinary | 683 to 1079 | 71,992 to 85,379 | 4.31 to 6.15 ms | 1.56 to 2.30 ms | 2.75 to 3.85 ms |

The split is stable: about 37 percent driver and 63 percent game code. The cost
follows the draw call count (5.3 to 6.3 microseconds per call), not the vertex
count. The workload is draw call bound. Walking the scene once and deriving the
second eye would save about half of the game code share (about 3 ms of 9.8 ms)
and nothing of the driver share, because each batch is submitted twice. These
numbers are from sessions in a headset. "The measure on the desktop" has the
numbers of six fixed scenes on the desktop.

The streaming session can decide the outcome. The same heavy scene gave 60.9 fps
with Virtual Desktop degraded and 86.9 fps with it healthy, for 0.3 ms of
difference in the game's work. The heavy scene leaves almost no wait (0.02 to
0.05 ms, against 3 to 6 ms in an ordinary scene). Rule out the streaming session
before you blame the game for a frame rate problem. Rule out the frame time
before you blame the feel of a control.

### Mip chains cost frame time

`DdrawForceMipMapAutoGen = 1` in `dxwrapper.ini` rebuilds a full mip chain at
every texture update, and the game updates the landscape texture as the camera
moves. So the cost arrives when the player drags the world, and a missed frame is
reprojected with the old camera. `ForceMipMapUsage` in `[d3d9]` builds a chain
once and costs nothing:

| | fps | Scene walk | GPU drain |
| --- | --- | --- | --- |
| No mip chains | 88.7 to 90.0 | 6.21 to 7.60 ms | 1.02 to 1.40 ms |
| `ForceMipMapUsage = 1` | 88.9 to 90.0 | 4.49 to 7.50 ms | 0.47 to 1.41 ms |
| `DdrawForceMipMapAutoGen = 1` | 70.8 to 82.3 | 9.32 to 9.89 ms | 1.13 to 1.71 ms |

The GPU drain is the event query of the bridge. It shows how far the GPU is
behind and is a good second check for any GPU cost.

### Timing the rest of the frame

`frame_timers.cpp` times every call of the frame loop that the stereo report does
not cover. It prints a line only when a call goes over 3 ms.
`ProcessBufferedKeys` averages 0.05 ms and reaches about 24 ms once in a minute of
play. The cause is the mod: the hotkeys are also game keys, and the game reacts to
each of them. The call runs 460 to 488 times for 450 frames, so it does not run
exactly once a frame. `GGame::Loop` takes `LHKeyboard::CriticalSection` before the
call, so a lock wait is not in that number.

### The frame cost measure

`profile=1` (a test key) turns the frame cost measure on (`frame_profile.cpp`).
The measure keeps the time of each frame and of each part of it, and the log
gets a summary for each group of frames. With `profile=0` nothing of it runs:
its hooks are not in, and each caller tests one flag.

A frame is one turn of the frame loop of `GGame::Loop` (`0x0054D262` to
`0x0054D460`), from one call of `ControlMap::ProcessActionsPerformed` to the
next. The parts of a frame do not overlap, and their sum is the frame:

| Part | What it is |
| --- | --- |
| input | `ProcessActionsPerformed`, `ProcessBufferedKeys` and `ProcessFrameInputs` |
| game turn | `GGame::ProcessNetworkPackets`. It runs one game turn when 100 ms of game time are over (`GGame::LocalTimerSaysDoATurn`, `0x0054C4A0`). A single player game runs one turn in a call at most (`0x0054CD52`) |
| before the scene | `GGame::ProcessGraphicsEngine` before the stereo hook: the mouse and the camera of the game |
| mod frame | The stereo hook less the next four parts: the head, the frame functions of the mod (the controllers, the Hand, the teleport, the overlay), the frame to the runtime, and the report of each 450 frames |
| wait | The wait for the runtime of the headset |
| eye 0, eye 1 | The scene walk of each eye pass, with the pass functions of the mod around it |
| to the headset | The copy of each eye picture to the bridge, or the read back |
| after the scene | The rest of `GGame::ProcessGraphicsEngine` |
| desktop copy | `GGame::FlipScreen`: the copy to the monitor |
| other | The rest of the loop: `ScriptedScreenShot::Process`, `GPlayer::SavePlayerAlignment`, `CreatureRoom::ProcessScreenShot` and the network flush |

Timers inside those parts say what a part is made of:

- the frame functions and the pass functions of the mod, the frame to the
  runtime, and the report,
- the Direct3D draw calls,
- the lines of the log,
- the surface calls: `Lock`, `Unlock` and `BltFast`, and each `Blt` but the
  copy to the monitor,
- `CreateFile`, `ReadFile` and `Sleep`.

The hooks of the surface calls, of `ReadFile` and of `Sleep` are in only with
the measure. A call counts only on the frame thread.

A group is a scene or a window. The word `bench` of the scripted controller
(`controllers.md`, 4.2) makes the frames of the word one scene. A run with no
such word has one window for each 450 frames, so a session in a headset gets
the same lines. A frame with no eye passes (a menu, a load) is in no group.
The lines of a group start with `frame cost   : scene N` or with `frame cost
: window`:

```
frame cost   : scene 3, 600 frames in 10.80 s (55.6 fps), frames 1552 to 2151 of the controllers. Frame ms: mean 18.00, median 17.75, 99th percentile 20.57, worst 21.76. Less the desktop copy: mean 9.68, median 9.63, 99th percentile 10.79, worst 12.23
frame cost   : scene 3, mean ms of a frame by part: input 0.010, game turn 0.078, before the scene 0.029, mod frame 0.061, wait 0.000, eye 0 4.813, eye 1 4.680, to the headset 0.000, after the scene 0.001, desktop copy 8.321, other 0.003
frame cost   : scene 3, inside those parts, mean ms of a frame: frame functions 0.041, pass functions 0.002, frame to the runtime 0.000, report 0.001, Direct3D draw calls 4.721, log 0.002, surface locks 0.015, surface copies 0.023, file opens 0.000, file reads 0.001, sleep 0.006
frame cost   : scene 3, the frame functions of the mod, mean ms of a frame (and the worst call): controllers 0.012 (0.135), hand 0.009 (0.126), teleport 0.001 (0.037), Creature command 0.002 (0.039), miracle 0.001 (0.017), village 0.002 (0.011), overlay 0.014 (0.040), water 0.000 (0.000), table 0.000 (0.001)
frame cost   : scene 3, 108 game turns in 108 frames. The turn of such a frame, ms: mean 0.43, median 0.40, 99th percentile 0.72, worst 0.83. A frame has 2828 draw calls and 106345 vertices in both passes. In the 600 frames: 1200 surface locks, 1200 surface copies, 0 file opens, 14 file reads, 98 lines of the log, 7822 calls of Sleep
frame cost   : scene 3, 0 slow frames of 600 (over 14.45 ms less the desktop copy), 0 of them with a game turn. The part that is the most over its median: none
frame cost   : scene 3, the worst frame is frame 1566 of the controllers, 12.23 ms less the desktop copy, with 1 game turn and 2778 draw calls. Its parts, ms: ...
frame cost   : scene 3, the callers of the surface locks with the most time: 0x008711AA (runblack.exe+0x4711AA) a surface of 32 by 32, 1200 times, 5.88 ms in all, worst 0.03. ...
```

- **The frame less the desktop copy.** A headset session has no copy to the
  monitor (see "The copy to the monitor"). So each summary also has the frame
  less that copy. The slow frames and the worst frame use that number.
- **A slow frame** is over 1.5 times the median, and 4 ms over the median at
  least (`frame_stats_math.h`). The cause of a slow frame is the part that is
  the most over its own median.
- **The callers.** For `Sleep`, the surface calls and `ReadFile` the summary
  names the code that made the calls with the most time, by the address that
  the call returns to. For the longest call it also lists the code of the game
  that the stack has above the call. The game has no frame pointers, so that
  list comes from a read of the stack. It has each value that is an address
  in the code of `runblack.exe` with a call instruction before it. A value of
  an old call can be in the list. `python bwvr\tools\disasm.py <address>`
  shows each one.
- **A frame with 2 ms or more in file calls** gets a line of its own, with
  its longest part and the last file that it opened (40 lines at most).

`profile=1` also puts a timer on each Direct3D draw call and on each call of
`DrawTriangle`. The report of each 450 frames then has the lines of "What the
scene walk is made of". `profile=2` is the measure with no such timer. Its
numbers of the Direct3D draw calls (the time, the draw calls and the vertices)
are 0. Those timers cost 0.2 to 0.4 ms of a frame with 2000 to 2900 draw calls
(2.3 to 3.8 percent). A frame of `profile=2` is that much shorter.

`profile=3` is the measure of `profile=1` with the state count of the draw
probe (`draw_probe.cpp`, "THE STATE COUNT"). It counts the runs of a frame: a
run is a row of draw calls with one state of the device, which the mod could
give to Direct3D as one draw call. Each summary then has one more line: the
draw calls and the runs of a frame, the causes that start a run, and the calls
of the game that set a state. The count has hooks on 13 more calls of the
device, so the times of a frame are a little longer than with `profile=1`.

In a headset session, set `profile=1` in `bwvr.ini` after the last save of the
launcher, because the launcher removes the key (see "Testing without a
headset"). The windows then give the parts that only a headset has: the wait,
the copy to the bridge and the frame to the runtime.

### The measure on the desktop

`bwvr/tools/desk/bench_run.ps1` makes desktop runs of one recipe and prints
the numbers (`tools/desk/README.md`, "The frame cost measure"). A run starts
in the test save `temple`. The scripted controller and the fake head put the
view at six places, and at each place nothing of the player moves for 600
frames. The script prints a table for each run, the spread of some runs, and
the comparison of two builds or of two sets of ini keys.

**What a desktop run has, and what it does not have.** A desktop run draws
both eye passes (`stereo_headless=1`). So it has the game turn, the calls of
the frame loop, and both scene walks with the calls into the backend. It has
the frame functions and the hooks of the mod, the surface calls and the file
calls. It also has the copy to the monitor, which a headset session does not
have. A desktop run does not have these costs of a headset session:

- the wait for the runtime, and the frame to the runtime (`xrEndFrame`),
- the copy of each eye picture to the bridge, and the wait for the GPU there
  (the GPU drain),
- the compositor and the encoder of the headset link, which take time of the
  CPU and of the GPU from the game,
- the poses and the buttons of real controllers.

In a desktop run the frame waits for the GPU only in the copy to the monitor.
So the eye passes of a desktop run are time of the CPU, and the time that the
GPU needs for the scene is not in them.

The logs of three sessions in a headset give the size of those costs. The
sessions had a Quest 3 through Virtual Desktop, a render of 2880 by 2880 and
175,500 frames, with `profile=0`. A frame took 13.0 ms as a mean. Both scene
walks were 6.5 ms of it, and the wait was 3.9 ms. The two copies to the
bridge were 1.55 ms, and 1.5 ms of that was the GPU drain. The rest was
1.0 ms. The median window of 450 frames had 88.7 frames a second, and one
window in ten had less than 56.

**The numbers.** The PC has an Intel Core Ultra 7 265KF, a GeForce RTX 3090
and Windows 11. The render is 2208 by 2208 for each eye at 110 degrees, with
`profile=1` and each other key at its default. The table has the mean of
four runs, in ms of a frame, less the copy to the monitor (8.0 to 8.8 ms at
this size). The worst frame is the range of the four runs:

| Scene | Mean | Median | 99th percentile | Worst frame | Both eye passes | In Direct3D draw calls | Draw calls |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1. The door of the Temple, level head | 8.69 | 8.62 | 9.94 | 14.7 to 18.4 | 8.48 | 3.32 | 1989 |
| 2. The same, head down | 9.99 | 9.91 | 11.06 | 15.0 to 19.4 | 9.80 | 3.96 | 2471 |
| 3. The village | 9.59 | 9.55 | 10.68 | 10.7 to 12.2 | 9.41 | 4.69 | 2827 |
| 4. A tree in the Hand | 9.35 | 9.31 | 10.26 | 10.3 to 11.4 | 9.16 | 4.65 | 2850 |
| 5. 337.5 units for each meter | 9.74 | 9.70 | 10.88 | 13.2 to 14.3 | 9.54 | 4.94 | 2932 |
| 6. The widest zoom | 2.08 | 2.02 | 2.66 | 2.7 to 4.4 | 1.91 | 0.77 | 306 |

- The two eye passes are 92 to 98 percent of a frame. In scenes 1 to 5 one
  eye pass costs as much as the other, within 4 percent.
- The Direct3D draw calls are 39 to 52 percent of the eye passes. In scenes 1
  to 5 a draw call costs 3.2 to 4.3 microseconds, and 1.6 to 1.7 of them are
  in Direct3D.
- A game turn takes 0.4 ms (the median), in one frame of six at 56 frames a
  second. Its mean for a frame is 0.05 to 0.11 ms.
- The frame work of the mod is 0.06 to 0.07 ms. The frame functions are
  0.04 ms of it. The pass functions add 0.06 ms in scenes 1 and 2, and 0.002
  to 0.006 ms in the other scenes. The report of each 450 frames takes under
  1 ms, in one frame. A line of the log takes 0.01 to 0.02 ms. The hooks of
  the mod inside the scene walk are in the eye passes, and they have no
  number of their own.
- At the widest zoom a frame has 306 draw calls and takes 2.1 ms.

**The spread.** The spread of a number is its largest value in some runs of
one build, less its smallest value. For three runs, the spread of the mean of
a frame is 0.02 to 0.21 ms (0.5 to 2.4 percent). The spread of the median is
0.02 to 0.16 ms, and the spread of the 99th percentile is 0.01 to 0.80 ms.
The worst frame of a scene changes by 0.2 to 4.4 ms. A difference between two
builds counts only when it is larger than the spread of each build. Three
runs give only a rough spread. So `bench_run.ps1` calls a difference clear
only when it is more than two times the spread.

**The texture of 32 by 32.** A scene walk writes a texture of 32 by 32 for an
item of the land draw. The code is `LH3DIsland::Draw` (`0x007FF610`) and
`fn_00878350`, and the texture write is in `fn_00837DF0` and `fn_008704C0`.
Scenes 3 to 5 have one write in each scene walk, and its `Lock` takes
0.005 ms. Scenes 1 and 2 have two writes in each scene walk, and the two
calls of `Lock` then take 0.6 ms together. That is 1.2 to 1.5 ms of a frame.
The cause is not known (`roadmap.md`, N1).

### Draw calls with one state

The cost of a frame follows the count of the draw calls. The mod can give two
draw calls that follow each other to Direct3D as one when the device has the
same state at both. `profile=3` counts such rows of draw calls, the runs
(`bench_run.ps1 -ProfileLevel 3`). The numbers of the six scenes, for both eye
passes of a frame:

| Scene | Draw calls | Runs | Fewer draw calls | Runs that start at a change of a texture only | Runs that start at a change of a render state or of a stage state | State calls of the game |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 1991 | 1224 | 38.5 percent | 220 | 952 | 3247 |
| 2 | 2485 | 1542 | 37.9 percent | 232 | 1267 | 3991 |
| 3 | 2868 | 1774 | 38.1 percent | 247 | 1475 | 4843 |
| 4 | 2916 | 1795 | 38.4 percent | 246 | 1492 | 4905 |
| 5 | 2789 | 1966 | 29.5 percent | 273 | 1639 | 5351 |
| 6 | 306 | 272 | 11.2 percent | 92 | 137 | 630 |

- Two runs gave the same counts, within 3 draw calls and 5 state calls.
- Each draw call is a triangle list, so a merge needs only new index values.
- Each state call of the game changes a value: the game makes no call that
  sets a state to the value that it has. So a filter of such calls saves
  nothing.
- A draw call takes 1.6 to 1.7 microseconds in Direct3D, so the draw calls
  that a merge can save are 1.3 to 1.9 ms of a frame in scenes 1 to 5. The
  merge of the mod gets 0.4 to 0.8 ms of that (see the next section). The
  Direct3D draw calls are 3.3 to 4.8 ms of a frame, so the merge does not take
  the place of a render thread.
- The count does not see a texture that the game writes between two draw
  calls with the same texture.

### One draw call for a row of draw calls

`draw_merge=1`, the default, gives a run to Direct3D as one draw call
(`draw_merge.cpp`). In an eye pass of the scene, the hook of a draw call of
the game copies the vertices and the indices into a batch and returns. The
batch goes to Direct3D as one draw call before each call that changes what a
draw call does, or that needs the picture so far:

- a call of the device that sets a state, clears, or starts or ends a scene,
  and a draw call that the merge does not take,
- a call of a DirectDraw surface that reads or writes pixels (`Lock`, `Blt`,
  `BltFast`, `GetDC`), or that changes a color key or a palette,
- a call of the mod on the Direct3D 9 device of the backend (`gpu_bridge.cpp`),
- the end of the eye pass.

So each triangle has the state and the place in the order that it has with no
merge. The merge takes a triangle list with vertices that have their place on
the screen, from the code of the game. A draw call of the mod itself goes to
Direct3D as it comes, and so does each draw call outside an eye pass of the
scene (a menu, the logo). With `draw_merge=0` the mod puts in no hook for the
merge.

Two desktop runs with the merge and two with no merge (`bench_run.ps1 -Set2
"draw_merge=0"`, 2880 by 2880, `profile=1`):

| Scene | Draw calls with no merge | With the merge | Both eye passes with no merge | With the merge | Gain |
| --- | --- | --- | --- | --- | --- |
| 1 | 1992 | 1319 | 9.18 ms | 8.50 ms | 0.68 ms |
| 2 | 2485 | 1620 | 10.51 ms | 9.72 ms | 0.79 ms |
| 3 | 2868 | 1875 | 11.56 ms | 11.12 ms | 0.44 ms |
| 4 | 2917 | 1886 | 11.75 ms | 11.00 ms | 0.75 ms |
| 5 | 2789 | 2062 | 11.82 ms | 11.06 ms | 0.75 ms |
| 6 | 307 | 270 | 1.95 ms | 2.03 ms | none |

- The gain of scenes 1 to 5 is more than two times the spread of the runs
  (0.03 to 0.17 ms). The difference of scene 6 is in the spread.
- The merge sends 5 to 8 percent more draw calls than the count of the runs:
  it also ends a batch at a state that the game changes and puts back.
- The gain is less than the count of the saved draw calls times the time of
  one draw call: a merged draw call has more vertices, and the merge copies
  each vertex.
- The table of "The measure on the desktop" is from runs with no merge.

The proof that the picture is the same, from a desktop run of the six scenes:

- `draw_merge_test=1` makes a checksum of the triangles that the game gave in
  an eye pass, and of the triangles that went out. In more than 7000 eye
  passes with about 250 million triangles, no eye pass had a different
  checksum.
- `draw_merge_test_frame=N` makes four frames with a picture of each eye: two
  with the merge, then two with no merge. Between the frame with the merge and
  the frame with no merge, 0.6 to 0.7 percent of the pixels differ. Between
  two frames of one kind, 0.5 to 1.2 percent differ: the villagers, the smoke
  and the clouds move.

Not run: the merge in a headset, and the check of the triangles in the Temple,
in a story, in a fight of Creatures and with the effect of a miracle. The
check runs in each eye pass of the scene, so one session with
`draw_merge_test=1` in the ini gives that proof: the line `draw merge   : the
check of the triangles` of the log has the count of the eye passes with a
different checksum. The check takes time of each frame.

### The slow frames

A session at 90 Hz has frames of 11 ms. A frame of 31 or 52 ms there has a
game turn that waits 20 ms, one time or two times. In the logs of sessions in a
headset in which each stop had its wait, 71 percent of the windows with a
worst frame of 31 to 52 ms have a worst game turn of 18 to 24 ms or of 38 to
45 ms. The other windows have that time inside `GGame::ProcessGraphicsEngine`,
and no desktop run shows it. The measure gives these causes of a slow frame, in the
order of their cost:

| Cause | How long | How often | The part |
| --- | --- | --- | --- |
| The audio DLL calls `Sleep(20)` on the frame thread when it stops a sound that plays (`audio.md`, 7.6). `GAudio::ProcessAudioGameTurn` stops each positional sound that is out of range, in the game turn | 20.5 to 21 ms for each stop. Two stops in one turn are 41 ms, and three are 62 ms | With `audio_stop_wait=1`: 1 stop in 10 s at the door of the Temple with the head down, 4 to 8 at the village, and none at the far zooms. With `audio_stop_wait=0`, the default, only a sound that QMixer plays keeps the wait: 2 stops in a run of 110 s, and none in a scene of the measure | game turn |
| The Creature reads an animation from the disk at its first use (`Creature::ProcessSubAction`, `Morphable::GetSetAnim` `0x00619690`, `CAnim::ReadBinary` `0x00860860`). One animation is 2400 to 5600 calls of `ReadFile` | The game turn takes 4.5 to 9.5 ms | 3 times in the first 20 s of play, and 3 times in the 60 s after them. The same animations load in each run, at about the same frames | game turn |
| The first use of a texture or of an effect file in a scene walk: 2 to 5 calls of `CreateFile`, and the load | The eye pass takes 8 to 15 ms more when Windows does not have the file in memory. When it has, the calls take 0.03 ms each | 8 times in the first minute of the first run of a row of runs, and no time in the runs after it | eye 0 |
| Each 600 game turns the game writes the mind of the Creature and two more files to the disk, and the alignment of the player to the registry | The game turn takes 3 to 4 ms, with 2 ms in 10 calls of `CreateFile`. In the first run of a row of runs those calls took 28 ms | One time a minute | game turn |

No frame of the measure has two game turns. With `audio_stop_wait=0` the six
scenes have 3 slow frames in 3600 frames. Each of them is an animation of the
Creature, and the worst frame less the desktop copy is 18 to 19 ms. With
`audio_stop_wait=1` the scenes have 13 slow frames, and the worst frame is 51
to 72 ms.

By the measure, these are not a cause: the lines of the log, the report of
each 450 frames and the frame functions of the mod. A read back of a surface
and `Sleep` from other code are not a cause too. The backend calls `Sleep(0)`
about 12 times a frame, which is 0.005 ms in all.

The runtime does not count a slow frame as a skipped frame. In a session in a
headset its counter stayed at 28 skipped frames for 12,154 frames.

Only a headset session can show what the wait, the copy to the bridge and the
frame to the runtime cost in a slow frame. Only a person can say if a sound
that stops sounds the same with `audio_stop_wait` 0 and 1.

### The clock of the game and the hooks of dxwrapper

With `Dd7to9 = 1` dxwrapper runs the Win32 hooks of its copy of DDrawCompat
(v0.3.2), and its ini cannot turn them off: `DDrawCompat = 0` does not stop
them (`Dllmain.cpp`, `InstallDd7to9Hooks`). The hooks are in front of
`GetTickCount`, `timeGetTime`, `PeekMessage`, `GetMessage` and some wait
functions. Each call asks Windows for the cycle time of the thread
(`QueryThreadCycleTime`, a call into the kernel), and after 3 million
cycles of the thread it calls `Sleep(0)` (`WaitFunctions.cpp`,
`mitigateBusyWaiting`). The current DDrawCompat of its author has no such
hooks.

The game calls `GetTickCount` in the draw (`fn_00837DF0`, the call at
`0x00838302`): 1000 to 1250 times in a frame in the six scenes. A sampling
profile of the frame thread in a run of `bench_run.ps1` with `one_walk=1`
had 8.3 percent of the samples of the scenes 1 to 5 in that check, nearly
all of it in the call into the kernel. The same profile, for the parts of
the frame thread: the game code 23 percent, the NVIDIA driver, `d3d9.dll`
and the calls into the kernel under them about 45 percent, dxwrapper 7.7
percent and the mod 7.3 percent.

`tick_direct=1` gives the import slot of `GetTickCount` in `runblack.exe`
(`0x008A91C4`) a function of the mod (`tick_direct.cpp`). It gives the low
32 bits of `GetTickCount64`, which is the number of `GetTickCount`, and it
makes no call into the kernel. The game does not wait for its clock in a
loop, because the headset paces the frame. The other hooks of dxwrapper
stay. The log has one line at the start and the count of the calls of each
report window.

`bench_run.ps1 -Runs 3 -ProfileLevel 2 -Set "one_walk=1;test_frame_rate=240"
-Set2 "tick_direct=0"`, the mean ms of the work of a frame:

| Scene | `tick_direct=1` | `tick_direct=0` | Less | Spread |
| --- | --- | --- | --- | --- |
| 1 | 5.88 | 6.33 | 0.45 (7.1 percent) | 0.04 |
| 2 | 8.83 | 9.39 | 0.57 (6.0 percent) | 0.09 |
| 3 | 7.47 | 8.14 | 0.68 (8.3 percent) | 0.13 |
| 4 | 6.77 | 7.57 | 0.79 (10.4 percent) | 0.04 |
| 5 | 6.96 | 7.68 | 0.72 (9.4 percent) | 0.09 |
| 6 | 0.91 | 0.96 | 0.05 | 0.03 |

Each difference of the scenes 1 to 5 is more than two times the spread of
the runs.

In a headset (Quest 3 through Virtual Desktop, 90 Hz) the five runs of 90 s
at the door of the Temple (the test save `temple`, a still headset,
`profile=1`) held 90 frames a second with each value, because that view
fits in the frame before the change too. The scene walk of the eye pass,
the median of the windows: 5.15 and 5.55 ms with `tick_direct=1` (and 5.5
ms in the first half of one more run, whose view changed in its second
half), and 5.71 and 5.73 ms with `tick_direct=0`. The time went to the wait
for the headset, and the age of a frame was 6.29 ms against 6.82 and 6.88
ms. One run with `tick_direct=1` had 81 frames a second, with the frame to
the runtime at 2.94 ms against 0.09 ms in the others: that is the state of
the streaming session ("What the scene walk is made of"), and not the
change.

## One walk for both eyes

`one_walk=1` walks the scene one time a frame, for the left eye, and makes
the right eye from the same batches (`one_walk.cpp`). The key is 0 by
default. The launcher shows it as "One scene walk for both eyes (prototype)".
With `test_keys=1`, the menu key turns it on and off in play.

### Why it is exact

The two eyes differ in the camera space x translation alone (see
`stereo-artifacts.md`, "How the mod makes the two eyes"). With head roll the
offset is on the rolled right axis, so this stays true. The engine writes
`rhw = near / cz`, so for each vertex:

    x(right) = x(left) - scale * separation * half_res_x * rhw / near

y, z, rhw, the colors and the texture coordinates are the same in both eyes.
`one_walk_math.h` has the rule, and `bwvr/test/controller_math/` checks it
against a true right eye camera at three rolls.

### How it draws

| Step | What |
| --- | --- |
| The pass starts | The render target is a target of the mod that is two eyes wide, with its own depth, in the format and the multisampling of the game's. It keeps the picture of each eye in its half from one frame to the next, as the keeps of `eye_clean` do. The clean of the game after the last present acted on the game's own target, so `eye_clean.cpp` makes it again on the wide target (`EyeCleanOneWalkBefore`) |
| A draw | dxwrapper gives each batch to `DrawIndexedPrimitiveUP` or `DrawPrimitiveUP`. A hook draws it into the left half, then a copy into the right half: the viewport moves by one eye width, and each x moves by the eye width and by its disparity. The disparity per rhw comes at each draw from the field of view, the size and the near plane that the engine has then, because a room of the Temple builds its camera again inside the pass. With `draw_merge=1` a batch of the merge goes out later than the draw calls of the game in it, so the merge keeps the disparity per rhw of its draw calls, sends the batch when that number changes, and one walk uses the kept number while the batch goes out |
| A clear | The same clear in the right half |
| A batch with no depth | Every rhw exactly 1 (screen space) or every z 1 (the sky): the copy moves by the eye width only |
| The interface | `draw_hooks.cpp` runs its chain a second time with the right eye, and the batch goes to the right half only. A tooltip at the Hand has an anchor for each eye. `hand_glow.cpp` gives the right eye its Hand's point by the same rule |
| The pass ends | The game's target is bound again. The two halves go to the shared textures: with `frame_send=0` the first copy has no wait and the second has the wait for the GPU, and with `frame_send` 1 and 2 both are copies alone, and `frame_send.cpp` waits one time as for two eye passes |

Direct3D 9 takes XYZRHW vertices as pixels of the whole target and clips them
to the viewport, so a triangle at the edge of one eye does not reach the
other. A change of the viewport costs much less than a change of the render
target. dxwrapper binds the game's target again when its own record changes,
and the interface panel puts back the target it found. The hook on
`SetRenderTarget` turns the game's target into the wide one in the pass.

### The edge of the right eye

The left eye has nothing past its own right edge. The engine clamps each
projected x into 0 to `g_MaxScreen.x` (`0x00C2AB00`). In the pass the clamp
limit is half an eye width further. Other paths cull at the edge of the left
eye too. So the launcher makes the render a quarter wider when one walk is on
(`one_walk_guard_pct`, 25). `PrepareHeadView` then gives a wider horizontal
angle at the same pixels per degree. Measured on the desktop at 110 degrees:
the band with no picture was at most 160 px, a tangent of 0.21. With a
render of 1.25 to 1 it starts at 57.6 degrees, outside the 54 degrees of the
right eye of a Quest 3. A thing nearer than about 15 cm of the room at the
outer edge of the right eye can still show the band.

### What it saves

One desktop run at 2208 by 2208, a new game to the island, a story and the
Temple, against the same run with two walks:

| Part of the run | Scene, two walks | Scene, one walk |
| --- | --- | --- |
| The island at the start | 9.73 ms | 7.24 ms |
| The story | 4.79 to 8.78 ms | 3.26 to 5.93 ms |
| After the story | 5.75 to 5.89 ms | 3.15 to 3.20 ms |

The three runs of `verify_build.ps1` (median scene time):

| Run | Two walks | One walk |
| --- | --- | --- |
| aim2 (far fetch, teleport) | 5.84 ms | 3.68 ms |
| tland (a throw that lands) | 5.72 ms | 3.42 ms |
| vc (the Creature at the Temple) | 12.69 ms | 8.19 ms |

In a headset (Quest 3, VDXR, the start dialog over the island, the headset
on a desk), both hold 90 Hz: the scene goes from 8.50 to 6.18 ms and the wait
for the headset from 1.72 to 4.17 ms.

The scene time of one walk holds the copies for the right eye, 0.29 to
0.97 ms on the desktop and about 1.4 ms in the headset run. The right eye of one walk matched the right eye of its own walk
(`one_walk_check=1`) except for the band at the edge.

### Known limits

- The right eye uses the culling of the left eye. A thing that only the right
  eye sees, at its outer edge, is missing. See "The edge of the right eye".
- Lighting that follows the camera (specular) and the particle jitter are
  those of the left eye in both eyes.
- A draw from a vertex buffer gets no right eye. The report counts them. None
  was seen.
- Not with a pose for each eye (`head_camera=0` with head tracking). Then the
  frame has two walks.
- Only a headset can show the feel of it.

`one_walk_check=1`: on a frame that saves pictures, the right eye also gets
its own walk, and its picture is saved as `walk2` beside the right eye of one
walk.

## The world around you

The mod submits the world as an `XrCompositionLayerProjection`. It is a promise:
this image was rendered from this pose with this frustum, and the runtime places
every pixel accordingly. Three things must be true together:

| | |
| --- | --- |
| The layer | Poses and frustums from `xrLocateViews`, in the space that the layer names |
| The view | The game renders the angle of the headset, not its own 70 degrees |
| The camera | The game camera carries the head of the player |

The eyes are located in `LOCAL` space, which is also the space the layer names.
The `VIEW` space gives identity orientation and fixed positions, which reads as no
head tracking.

### A wide view is nearly free

`LH3DTech::ChangeFov` (`0x008195B0`) writes the culling cone as well as the
projection: `InvHalfTanFovX` and `InvHalfTanFovY` beside `CosHalfFov` and
`CosHalfFovSqr`. So a wider view widens the culling with it, and nothing pops in
before it is drawn. `fov.cpp` hooks it. A sweep of the view at a fixed scene:

| Degrees | Draw calls | Vertices | Scene x2 |
| --- | --- | --- | --- |
| 70 | 1731 | 133,131 | 8.20 ms |
| 77 | 1701 | 132,993 | 8.11 ms |
| 84 | 1717 | 141,759 | 8.30 ms |
| 91 | 1747 | 146,126 | 8.46 ms |
| 98 | 1780 | 148,847 | 8.54 ms |
| 105 | 2025 | 156,448 | 9.55 ms |

From 70 to 98 degrees the cost is 0.34 ms and 49 draw calls (about 4 percent). The
game looks down at an island, so a wider view adds sky, sea and empty distance.
There is a step at 105 degrees (245 more draw calls). A headset needs about 100.
The game also ran at 161 degrees with no artifact, so the projection and the
culling move together.

### What the view must cover

A headset asked for this:

```
eye 0   left -54.0  right  40.0   up 44.0  down -55.0 deg
eye 1   left -40.0  right  54.0   up 44.0  down -55.0 deg
separation 66.8 mm
```

The frustum is asymmetric (54 degrees outward, 40 inward) and 99 degrees tall. The
game renders a symmetric frustum and takes the vertical angle from the render
aspect. `PrepareHeadView` (`stereo.cpp`) takes the widest half angle of either eye
in each direction and solves for a horizontal angle that covers both:

    tan(h) = max( tan(h_needed), tan(v_needed) * width / height )

The layer declares exactly what was rendered. For a headset that asks 108 by 110
degrees per eye:

| Render | Renders | Area drawn against area needed |
| --- | --- | --- |
| 1600x1600 | 110.0 x 110.0 | 1.04 |
| 2048x2048 | 110.0 x 110.0 | 1.04 |
| 2560x1440 | 137.0 x 110.0 | 1.84 |
| 1280x1024 | 121.5 x 110.0 | 1.30 |

A render far from square still fills the view, by being wider than it needs.
Because the workload is draw call bound, the extra area costs little. A square
render needs a symmetric superset of about 110 degrees, and it throws away about a
third of what it draws. `InfoTransform` holds `HalfRes`, the projection centre
(`screen_x = (1 + cx/cz) * half_res_x`). Moving that centre would give an off
centre projection for each eye. This is not done and is not needed while the cost
is low. `head_fov_deg` forces a view angle, and `fov_absolute_deg` does the same
without a headset.

### Eye separation

`eye_separation_milli=0` asks the headset: the mod takes the distance between the
two located eye positions and scales it into the world. A separation of zero is not
a value. Until the headset has measured, the mod uses 0.5 world units, which is
66.7 mm at 7.5 units per metre (`head_units_per_meter_x10`, default 75). Writing the
zero straight into the eye separation would draw both eyes from one position, with
no depth.

## A separation of zero means ask, not zero

`eye_separation_milli=0` asks the headset for the player's own separation, as the
section above says. `StereoInstall` keeps 0.5 world units until the answer
arrives. A non-zero value is a fixed separation in thousandths of a world unit.
The draw probe shows the fault of a real zero: every world batch is classed as
flat.

## One frame, one head

`LH3DTech::UpdateCamera` is hooked to put the head of the player on the camera
the engine builds. The engine calls it about five times per frame (2250 calls in
450 frames). One call places the main camera before the scene walk. The other four
happen inside the walk, and the mod runs the walk twice. The hook also records
three pieces of state for each frame: the anchor camera that places the Hand, the
anchor position and the roll that a position and focus camera cannot carry.

If every call records, eye 0 reads one value and the walk changes it before eye 1
reads it. The two eyes then draw from different cameras, the right eye spins when
the player looks up, and the Hand lands in two places. So `h_Process3dEngine`
brackets the whole scene walk (`HeadCameraBeginScene`, `HeadCameraEndScene`),
including the three paths that return early. Every call still moves its camera,
because sub cameras need the head too. Only a call made outside the walk records
the state of the frame. The check line is:

```
head camera  : 2250 camera updates, 2250 carried the head, 450 recorded over 450 frames
```

The record count must equal the frame count.

Rule for any hooked function: it runs many times a frame, and game logic reads
back what it stores. Latch the state for each frame.

### The stored camera

`LH3DTech::UpdateCamera` builds everything the renderer needs from the camera it
gets, and it also stores that camera: the position at `0x00EA1DB8` and the focus at
`0x00EA1DC4`. Thirty eight places read the stored focus, and most are game logic:

| Reader | What it does with the focus |
| --- | --- |
| `HandStateHolding::Update` | The vector from the held position to the focus, scaled by 0.3, decides the CameraHelp hint |
| `HandStateCamera::Update` | Drags the land relative to the focus |
| `GCamera::Update` | In some modes takes the stored camera back as its start point |
| `GScript::StoreCameraDetails`, `GScript::GetCameraFocus` | Scripts save and restore the camera |
| `fn_005E5CD0` (sky and cloud module) | Recomputes whenever the stored camera moves by more than 1 unit |
| `Tree::PreDraw`, `HelpSystem::SendFOVObject`, `PowerSpinRunner::Update` | Various |

So after every `UpdateCamera` call, `head_camera.cpp` writes the camera that the
game asked for back into the stored position and focus. The renderer has taken the
matrices, the frustum and the culling cone by then, and those keep the head. The
arguments are copied before the call, because a caller can pass the stored camera
itself. The stored camera forward at `0x00EA1DD4` carries the head. It is read by
particles, trees and the water, which draw. Its one getter, `fn_00442CE0`, has no
caller.

## The head on the camera

### Composition

The runtime reports the head in a gravity aligned space. The rotation of the view
is

    R = L * H

`L` has the columns right, world up and levelled forward (the camera heading
flattened onto the horizontal). `H` is the head rotation. The game's own pitch is
dropped. The world is rigid and the horizon is level. The straight ahead view of
the player is the horizon, whatever the game camera does. The game camera looks
12.9 degrees below the horizon in ordinary play and 78.8 degrees below it during
the menu flyover. So the further the game zooms out, the further down the player
looks to see the land.

A turn of the head about the world vertical, not about the camera's up, is the
reason for the levelled frame: the camera looks down at the island, so its up axis
is tilted, and a yaw about that axis induces roll.

### The per eye matrix

The camera matrix is 4 rows of 3 floats in the row vector convention, with the
camera axes in the columns. Applying a head pose (H, d) in camera space:

    R' = R * H
    t' = t * H - d

The eye offset comes after the roll. With the eye at world position e and basis B,
`t = -e * B`. After the roll `B1 = B0 * H`, and the eye moves by dx along the
right column of B1, so `t1 = -(e0 + dx * B1_right) * B1 = t0 * H - d`. The form
`(t - d) * H` puts the offset on the axis before the roll, and the eyes then gain
`separation * sin(roll)` of vertical disparity (about 23 mm at a 20 degree tilt).

The scaled matrix is rebuilt from the unscaled one column by column, because its
columns carry the field of view terms and a rotation would mix them. Handedness is
a change of basis, `B * M * B` with `B = diag(1, 1, -1)`.

### The zenith

The roll is the part of the head that a camera of position and focus cannot carry:

    roll = atan2(head_up . roll_free_right, head_up . roll_free_up)

It is computed from the rotation that the mod composed, against the rotation that
the engine is about to build. The forward is held half a degree short of vertical,
so the basis of the engine stays well conditioned. The roll number flips by 180
degrees as the view crosses vertical, because heading and roll about world up are
not defined there. The basis of the engine flips at the same instant, so the
product of the two is continuous.

### Roll and the sea

`head_roll=1` (default) applies the roll as a rotation about the camera's forward
axis in the per eye matrix. Roll forces the plain sea (`water_simple`, default 1).
The table and the God view also use the plain sea, because the scanline sea
cannot be cut at an edge or follow the scale. With `head_roll=0` and
`water_simple=0` the scanline sea stays and the head stays level.
`HeadCameraRoll` returns zero unless the plain sea is selected, so the land cannot
roll while the horizon does not.

### The declared pose

The layer declares the pose of the runtime, because the render is built to match
it. When the render leaves out the roll, the mod takes the roll out of the
declared pose too. A level picture in a tilted frustum would tilt the world with
the head.

### Recenter

F11 makes the way the head faces now the straight ahead direction. See "Keys for
v1.0".

## The pointer

### The window is the input device

The pointer of the game is the Windows cursor in client pixels, one to one:

```
pointer : game 576 795   windows client 576 795 of 1280 x 1280
```

So the mod keeps the window usable at any render size:

| | |
| --- | --- |
| Scale | The cursor is read in client pixels and scaled onto the render, for each axis |
| Fit | The window is shrunk to the work area of the desktop and centred, once. It has the shape of the render and allows for the Options menu bar. The fit checks the client it got and corrects it |
| Clip | The cursor is held inside the client area while the game has focus |

The clip is released the moment focus is lost, and on shutdown, because a clipped
cursor outlives its process. The keys are `pointer_scale=0`, `pointer_clip=0`,
`window_width` and `window_height` (a forced size).

### Who moves the cursor of Windows

Three calls of the mod can move the real cursor, and all are in
`pointer_map.cpp`. `PointerMapApply` puts the cursor at the Hand in each frame
in which the two differ (`SetCursorPos`), so the next frame measures the mouse
from there. `h_MouseSetPosition` puts it where the game asks
(`LHMouse::SetPosition` `0x007E4E40`). `ClipCursor` in `PointerMapApply` moves a
cursor that is outside the window into it. Each of the three asks `HoldsCursor`:
the game window is the window in front, and the test key `pointer_leave_cursor`
is 0.

The game has one more path to `SetCursorPos`, in `LHMouse::SetPosition` at
`0x007E4EC2`. It runs when the mod gives the call to the game: with
`pointer_scale=0`, or while the game has no window or no render size. It can
also run before the first frame with a render target, when the mod puts its
hooks in. The two other references to the import slot `0x008A97E4` sit after a
`ret`. The game does not import `ClipCursor`. dxwrapper imports `ClipCursor`,
`SetForegroundWindow` and `SetActiveWindow`. In the desktop runs the game and
dxwrapper called none of these functions [R].

The game and the mod do not take the front (`controllers.md`, 9.1). Windows gives
the front to the window of a new process when no person gave an input for longer
than the foreground lock time (`ForegroundLockTimeout`, 200 s by default). So a
game that starts on a PC that nobody uses has the real focus from its first
frame, with no call of the process [R].

Two test keys are for the desktop runs (`tools/desk/README.md`). A run must not
move the real cursor, and it starts only on a PC that nobody uses, so its game
window is in front. Neither key is in the launcher, and the launcher removes
both when it saves.

| Key | Default | Meaning |
| --- | --- | --- |
| `pointer_leave_cursor` | 0 | 1: the mod does not hold or move the cursor of Windows, also while the game window is in front. The point that `pointer_map.cpp` wrote last is then the cursor itself, as it is with the window behind. A real mouse that moves still moves the Hand. The controllers, the fake controller and the fake mouse work as with 0, because they write the pointer of the game and not the cursor |
| `cursor_trace` | 0 | 1: `cursor_trace.cpp` hooks `SetCursorPos`, `ClipCursor`, `SetForegroundWindow` and `SetActiveWindow` of user32 for the whole process, and the log names each caller. Each call goes on to Windows with no change |

The log of `cursor_trace=1`, each line with the frame:

- "cursor_trace=1 (a test) ... At the start the cursor is at X Y, and in front is
  ...", once.
- "SetCursorPos(X, Y) from MODULE+0xOFFSET, call N of this caller. It moved the
  cursor from A to B. The game window was in front" (or "The cursor stayed at
  ..."). A caller is one function and the address that the call returns to. The
  module `DDRAW.dll` is the mod, `runblack.exe` is the game. The log has the
  first 3 calls of a caller, and then a call only when it differs from the call
  before it: it moved the cursor or not, the game window was in front or not.
- "in front is the game window" (or the class of a different window), for each
  change of the window in front, with the place of the cursor.
- "the cursor went from A to B with no call of the game process": a person or a
  different program moved it.
- In the report each 900 frames: "N calls of ClipCursor from ... since the
  report before, and M of them moved the cursor".

### One pointer, not two

The game takes the pointer in more than one place:

- `pointer_map.cpp` rewrites the pointer globals at `0x00E852C0` and `0x00E852C4`
  once a frame, in render pixels, and the Hand is placed from them.
- The window procedure also writes them on every `WM_MOUSEMOVE`, in window pixels:

```
GameWindowProc
  MouseMoveHandler 0x007DC920               writes the globals from the message,
                                            in window pixels
GGame::MouseHandler 0x0054FFE0              on a move, at most every 28 ms
  CMouse::ProcessPosition 0x0061A110        sends the globals to the interface
    handler 0x005D99E0                      stores them at GInterface + 0x428
      fn_005D5980 -> fn_005E5740            asks the landscape what is under them
```

`GInterface::PreDrawProcess` asks the landscape again every frame, at the same
stored point. If the interface gets window pixels, it looks for land at a point
2.16 times nearer the top left corner than the Hand. With the head level the
upper half of the view is sky, the game finds no land, draws a red cross and
refuses a drop.

So the mod hooks `MouseMoveHandler` and changes the message position to render
pixels, with the same mapping as the frame rewrite. `LHMouse::SetPosition`
(`0x007E4E40`) is how the game moves the cursor. It gives render pixels to
`SetCursorPos`, which takes window pixels, so it is converted too, and the Hand
starts from the exact point the game asked for.

The pointer has several readers: the window pixels, the render pixels and the
screen of the game camera. Each reader gets the right one.

### One camera for the Hand, and the cursor turns instead

Every part of the game sees one camera, the camera with the head. `pointer_map.cpp`
turns the cursor by the inverse of the head each frame, so that the Hand keeps its
place in the room. The view is `R = L * H`, so for a direction on screen to stay
fixed while the head goes from `H_prev` to `H_now`:

    r' = transpose(H_now) * H_prev * r

The Hand's position is kept as an exact float, with the sub pixel remainder. A
head turn that takes the Hand off the side of the view pins the cursor at that
edge. A turn of more than 30 degrees in one frame is a recenter or a tracking
glitch, and the lock ignores it. The mod holds the Hand still while the game's
"turn off mouse move" flag (`0x00E8C0FA`, set while the middle mouse turns the
camera) is set and a mouse button is down. `GInterface::Process` sets the flag back
from `IsPlayBack` on every game turn.

### The game camera's own screen

`CameraModeNew3` builds its own world to camera matrix from the game camera
(`LH3DTech::UpdateWorldToCamera`) and reads the pointer globals as a point on that
screen. If the head lock moved the pointer on the rendered screen, the camera code
would see mouse movement. So for the length of `GCamera::Update` (`0x00441F80`)
the pointer is where the ray of the Hand falls on the game's own screen:

    game ray = transpose(E) * L * H * rendered ray

`E` is the game camera with no head, recorded by the camera hook on the main call.
The point moves only when the Hand's ray moves. A cursor move that the camera code
asks for is converted back to the rendered screen. Everything outside the bracket
(the Hand, the land under it, the drawing) uses the rendered screen.

`GCamera::Update` runs the play mode's update, then makes the main
`LH3DTech::UpdateCamera` call at its end (`0x00442622`). So while the camera code
runs, the engine holds the camera of the previous frame, which carries the head.
The camera code casts rays through the pointer with the engine's matrices (for
example the grab point at `0x0045BA22`). A grab distance over 3000 stops the orbit
and locks the input of the camera at `CameraModeNew3 + 0x2F0`. So `h_GCameraUpdate`
points the engine at the game camera from the last main call, with no head, before
the camera code runs. The main call at the end puts the head back. Two guards:

- When the game draws from another camera (`[game + 0x205A28]` is not zero), the
  main call is skipped, so the rebuild is skipped too.
- If the main call does not happen, the head is put back by hand, so a frame never
  draws without it.

`game_camera_rebuild=0` turns the rebuild off.

### Holding the land shook the camera

A land drag is a direct function of one pointer pixel. `UpdateClickParams`
(`0x00459610`) fixes the grab point, the grab plane and the grab camera at the
press. Each frame, `CameraModeNew3::Update` casts the current pointer through that
fixed camera onto that fixed plane and moves the camera by the difference
(`0x0045D365` to `0x0045D96E`). A flip of the pointer by one pixel moves the
grabbed point by one pixel on the screen, and ground nearer the camera moves 4 to
30 times as far.

So `h_GCameraUpdate` turns the exact float of the Hand, not its whole pixel, and
rounds the result once. The pixel stays where it was until the exact point is 0.8
pixel away from it. This stops float noise from flipping the pixel, and stops a
loop on slopes where the drag changes the pitch of the game, which moves the
pixel, which moves the camera. When the camera code moves the cursor itself, the
mod keeps the pixel that it asked for.

The report lines count the frames where the land is held and the mouse is still:

```
pointer      : land held with the mouse still on N frames. The game's pointer moved on P, the camera on C.
pointer      : the camera moved S units at most. Its pointer came from the exact point.
```

Both counts must be 0.

The game also has a 0.3 s camera glide at `0x009CE614` (one reader, no writers),
which would smooth a hold. Its duration override at `0x00460229` reads a stack slot
that `0x0045E359` clears before the move switch, and only a fight fills that slot. The mod sets the glide to 0 (`camera_glide=0`), so a
pointer fault shows at once.

Test note: the drag takes its motion from DirectInput, as relative mouse
motion. A change of the place of the cursor (`SetCursorPos`) does not reach the
drag. A drag test uses the test mouse (`fake_mouse_script`, `god-view.md`,
section 10). Its words `button` and `move_right` act inside the mod, and each
move also goes into the mouse motion that the camera code reads
(`ControlMap::MouseDelta`). The real mouse stays where it is.

### Dragging the land when looking down or behind

The camera code decides what a press does from where the pointer sits on the game
camera's screen, in `CameraModeNew3::UpdateTricons` (`0x00459230`). A point near
the bottom edge means tilt, near a side edge means turn, and the rest means drag
the land. The mod shows that code the head free game camera, and the ray of the
Hand lands on that screen only while the head looks roughly ahead. Looking down
puts it in the tilt band, and behind the camera it has no point on the screen at
all. `pointer_map.cpp` handles this:

- While no button is held, the point that the camera code sees stays inside the
  area that means "drag the land". A Hand behind the game camera goes to a point
  low in the middle. This happens only while snap turns are on, which switch off
  the turn and tilt zones.
- `UnprojectScreenPoint` (`0x0081B370`, fastcall, the pixel in ecx, the output in
  edx, one stack float, `ret 4`) is hooked. Inside the camera update, the two rays
  of a drag get the Hand's own world ray. The pointer at `mode+0xB0` gets the
  current one, and `mode+0x2D4` gets the one from the press. A ray is replaced only
  when its pixel is the pixel this file wrote. The grabbed land stays on the ray of
  the Hand at any head angle.
- `CameraModeNew3::UpdateClickParams` (`0x00459610`, thiscall, three stack
  arguments, `ret 0xC`) records the press ray. After it runs, a zero plane normal
  (the grab point straight under the camera) becomes a level plane through the grab
  point, because the game stops a drag on a zero normal.
- The game caps a drag step by the pixel distance from the press, times the view
  distance, times 0.011. During a land drag the mod sets that distance to the angle
  that the Hand turned since the press, so it means the same wherever the press was
  made.
- At the end of its update the camera code can put the cursor back on the pixel it
  was given. When that pixel is one the mod placed, the Hand stays where it is.

The report line:

```
drag         : N presses, N land drags, N moved into the safe box (N from behind), N used the exact ray, N frames smoothed
```

### Smoother drags far out

A ray hits the land at range R from a camera h above it, so each radian of pointer
motion moves the hit by R squared over h. At R 400 and h 10, one window pixel moves
the camera about 34 units. When the window is smaller than the render (a 1333
pixel window on a 2880 pixel render), one mouse step is 2.16 render pixels, and a
slow drag moves 0 or 1 pixel a frame.

So the mouse part of the Hand's movement goes into a pending amount. Each frame
the Hand takes a share of it, `1 - exp(-dt / 35 ms)`. This happens only while the
left button holds the land in a land drag. At release the rest arrives at once. The
Hand and the camera code read the same smoothed value. Head motion is not smoothed.
The lag is capped at 300 pixels. `drag_smoothing_ms` sets the time (default 35, 0
is off). Smoothing the camera instead would put the Hand's ray through a late
camera, and the land would slide under the Hand.

### Testing the pointer

`0x00C5E680` holds the Hand's world position, so the claim "the Hand does not move
when the head turns" can be read as numbers.

A test of the pointer never moves or clicks the real mouse. Two tools move the
pointer inside the game process:

- The test mouse (`fake_mouse_script`, `god-view.md`, section 10) moves the
  pointer in render pixels and presses the left button.
- The scripted controller (`fake_controller_script`, `controllers.md`, section
  4) places the hands, and the mod puts the pointer where the pointing hand
  points.

With `fake_head=1` the head has a fixed pose, and `fake_head_sweep=1` alternates
that pose with a level head ("Testing without a headset"). The log of the mod
gives the numbers.

Three traps:

- The creature select screen does not place the Hand from the cursor.
- A cutscene has not settled. Take two samples at the same head angle to tell the
  game's own camera from the head.
- A pointer that does not move never recomputes the Hand. Move the pointer by
  one or two pixels between samples: a word `move_right` of the test mouse for
  one frame, with `fake_mouse_step_px=1`.

To reach gameplay without a headset, use a script of `bwvr/tools/desk/`. The
scripts of one area start in a test save, which a test run made ("A test
save"): the game is at the Creature pens after the first story, or at the
door of the Temple with no story. With `-NewGame` a run starts the game with
`-NEWGAME`, and the scripted controller closes the start dialog.
`spawn_run.ps1 -Choice keep` gives a new game with no story. A test never
loads a saved game of the player.

## Moving around

`comfort_camera.cpp` hooks `ControlMap::IsActionPerformed` (`0x00470AF0`) and
`CameraModeNew3::Update` (`0x0045A960`). The camera moves only when the player
moves it.

- **Snap turns in place** (`snap_turn`, default 2 for 45 degrees, steps 0, 30, 45
  and 90). The game is told the middle mouse (action `0x10`) is up. The mod counts
  the mouse movement itself and turns once per 150 counts sideways. A turn key
  counts once per press, read from the key turn at `0x00C5B0C8` that
  `ProcessKeyMovement` fills after each camera update. A turn rotates the focus and
  the orbit pivot at `+0x12C` about the position, then puts the six GCamera zoomers
  at their values with `Zoomer::SetPosition` (`0x00441AC0`). The copies of the mode
  at `+0xC`, `+0x18` and `+0x1D4`, and its heading at `+0x50`, follow, so the next
  zoom does not swing back. `CameraHelp::CameraHelpCallback` is called with `0x300`
  and a direction, so the tutorial counts the turn, and with `turn_help_degrees` (5)
  as many more events as the game's own turn makes for that angle, one in each
  later game turn (`god-view.md`, "The turn lesson"). `blink` (default 0) can add a
  blink of 100 ms or 200 ms.
- **No other smooth turn or tilt.** Bits 1 and 2 of `CameraHelp::EnabledFeatures`
  (`0x009CDD6C`) are cleared for the length of the update of the mode and put back
  after. Scripts write that word, but never during the call. A turn also needs the
  script's own bit 2, so the tutorial can hold turning back.
- **Instant jumps** (`instant_jumps=1`). A double click starts a near flight (150
  units or less), which sets the elapsed time at `+0x78` to 0.75 s, or a far one,
  which sets it to 0 and flies through a point above the land with the target at
  `+0x110` and `+0x11C`. Either way the elapsed time goes down, and nothing else
  makes it go down. The mod then puts the zoomers at the target and sets the
  elapsed time to 1.5 s.
- **No glide** (`camera_glide=0`). See "Holding the land shook the camera".

Still gliding: the camera that follows the Creature after a double click on it, the
3 s pull back when the camera goes more than 3500 from the middle of the map, the
arrow keys and the automatic tilt. A vignette during a land drag is an open idea if
the drag is still uncomfortable.

### Zoom in steps

How the game zooms:

- `CMouse::ProcessButtons` adds the DirectInput wheel total (120 for a click) to
  the int at `0x00C5E8D8` (`0x0061A24D`, `0x0061A282`). The int keeps the sum until
  a reader clears it. Action 3 is wheel down and action 4 is wheel up.
- `CameraModeNew3::Update` reads the int at `0x0045B704`, only while `[mode + 0x2EC]`
  is zero and the follow blend Zoomer at `0x00C5B100` is below 0.01. It clears the
  int at `0x0045B77E` whether or not a wheel action is performed.
  `CameraModeFollow::ProcessKeyMovement` also reads and clears it.
- The zoom is `acc -= wheel * [0x009CE61C]` (0.5, one reader, no writers), then
  `delta = S * acc * 0.0015` at `0x0045C0CC`. S is three times the height of the
  camera above the focus, between 60 and 2000. When the camera is below the focus, S
  is between 60 and 240.
- `delta` goes to the distance at `+0x1A8`, with a floor of 3.1. That field can be
  stale until a zoom starts. Then `UpdateClickParams` sets it from the camera and
  the focus (`0x00459AC8`).

At the usual pitch, one wheel click of the game moves the camera about 6 percent of
its distance. The mod does not replace the zoom code, so the 3.1 floor, the script
switch (feature bit 4) and the "zoomed" event of the tutorial stay. It does two
things:

- **A fixed ratio.** For the frame of a step, the mod sets the wheel scale at
  `0x009CE61C` so that the step takes the distance to `d * ratio^-clicks`. It
  measures the distance between the GCamera Zoomer destinations. A step is never
  smaller than the game's own, and the scale goes back to 0.5 after the update.
- **Joined clicks.** The mod takes the wheel counts from the game and keeps them.
  The first click is used at once. After that, at most one step happens every 150
  ms, with at most three clicks in a step. In between, the game is told the wheel
  did not move. The mod keeps at most two steps of clicks, and a turn of the wheel
  the other way drops them.

A key bound to a wheel action gives no counts, so it works as in the game. The key
`zoom_step` picks the ratio: 0 the game's own step, 1 is 1.12, 2 is 1.20 (default),
3 is 1.30.

## The story camera

The campaign script moves the camera through `CameraModeScript` (vtable
`0x008C7D5C`). `GCamera::Update` runs only the top mode.

| Source | Calls | How it moves |
| --- | --- | --- |
| `MOVE_CAMERA_POSITION`, `MOVE_CAMERA_FOCUS` and two that move both | 1,520 | `MoveCameraPosition` (`0x004616F0`) and `MoveCameraFocus` (`0x00461430`) ease the six GCamera Zoomers to a destination |
| `SET_CAMERA_POSITION`, `SET_CAMERA_FOCUS` | 597 | Cuts already |
| `RUN_CAMERA_PATH` | 10 tracks | `UpdatePath` (`0x00461AB0`) writes a spline pose each frame. Every track turns 108 to 813 degrees |
| `PLAY_HAND_DEMO` | 18 | `fn_005DAEE0` writes recorded poses. `rotate.hnd` turns 719 degrees |
| `SHAKE_CAMERA` | 31 | `PSysGlobal::StartCameraShake` (`0x0068F400`) |

### The story camera holds its shot

Most story motion does not use the hooked moves. The follow natives, the dual camera
(`CameraModeTwoObjects`) and the creature follow (`CameraModeFollow`) aim the
Zoomers again on every frame. So `script_camera.cpp` changes only the camera that
is drawn. The game camera moves as the game moves it, so `HAS_CAMERA_ARRIVED`,
script timing and save games do not change.

1. **Who owns the camera.** `head_camera.cpp` asks at the top of its `UpdateCamera`
   hook. For the main call at the end of `GCamera::Update` (return `0x00442627`), the
   mode on top of the stack (`GCamera+0x28`, top index at `+0x58`) decides. The
   modes of the player are not held. Every story mode is held, and the creature
   follow is held when `script_camera_follow=1` (default). The own call of the hand
   demo (return `0x005DB125`) is always held. Every hook checks the vtable of the
   mode first, because Esc can remove the script camera in the middle of a sequence.
2. **What the shot is.** `script_camera` picks: 0 is the game's camera, 1 "cut at the
   start" shows the Zoomer destinations, 2 "cut at the end" (default) shows the
   camera where it is and cuts when it stops.
3. **Not too close.** A shot nearer than `script_camera_close_distance` (60) to its
   focus moves back by `script_camera_close_zoom_pct` (125) percent along its own
   line, but not past 60 units. A shot further out does not change. The hard limits
   are `script_camera_min_distance` (5) from the subject and
   `script_camera_min_height` (5) above the ground
   (`LH3DIsland::GetAltitude`, `0x00803090`). The result grows with the distance
   and has no jump.
4. **When it cuts.** A new owner cuts at once. A shot cuts when the target drifts
   `script_camera_cut_degrees` (90) degrees or `script_camera_cut_percent` (50)
   percent of the view distance from it. It also cuts when the target rests for 300
   ms at least 5 degrees or 5 percent away. A value below 0 keeps the default.
5. **The game sees a still camera.** `GCamera::Update` sets its moving flag (`+0x74`,
   read by `GCamera::IsMoving`) when the stored camera is not the new pose. While a
   shot is held that is true on every frame, and the interface then clears what
   looks like the gesture trail. The flag is set from the drawn camera.
6. **No shakes.**

Paths and hand demos hold their shot: `GCamera::SetPositionAndFocus` (`0x004438C0`)
is hooked for the two callers. The hand demo also renders with a direct
`UpdateCamera` call (return `0x005DB125`).

The held pose becomes the camera the game asked for. So the stored camera at
`0x00EA1DB8`, the clouds, the billboard heading and the Hand's ray agree with the
picture. A "cut" keeps the pacing of the game: the Zoomer eases as
`StartValue + StartSpeed * t` plus three curve terms (`Zoomer::Update`,
`0x00442720`), and `GCamera::Arrived` (`0x00443050`) answers no until the move's
own duration ends, so scripts that wait with `HAS_CAMERA_ARRIVED` (493 calls) wait
as long as before. The log names each owner change and counts frames held, cuts by
reason and clamps. Follow and dual cameras (49 calls), the Temple interior and the
pull back past 3500 units still glide.

## Interface, hints and subtitles

### The hint beside the Hand

The game draws each hint twice in each pass: once projected at the Hand (first) and
once pinned to the cursor's screen position (second). On a monitor the two copies
land on one point. In stereo they separate by the disparity of the Hand. With head
tracking they separate much further, and by different amounts in each eye, because
the projected copy moves with the eye and the pinned one does not.

| | Keep | Why |
| --- | --- | --- |
| No head tracking | The pinned copy | Same place in both eyes, reads flat like a label |
| Head tracking | The copy at the Hand | The pinned one follows the head |

`hint_mode` picks: 0 leaves the hint alone, 1 keeps the copy at the Hand, 2 keeps
the copy at the pointer. The default is 1 with head tracking (every play mode but
play mode 1) and 2 without. The mod pairs the copies by order (projected first,
pinned second), not by distance, so both passes give the same answer. `hint_flat=1`
(default) records where the kept copy lands in the first pass and moves it to the
same place in the second, so both eyes draw it at one screen position. It stays
anchored to the Hand. `hint_scale_pct` (default 20) sets its size about its own
anchor, because the game sizes its interface in pixels, which is small on a monitor
and large in a view of 110 degrees.

### Tooltips

The belief gained notice, the Storehouse readout and the Temple readout are
HelpSystem tooltips. The game draws its tooltip list twice in every eye pass: from
`HelpSystem::Draw3D` (the list walker `fn_00447850` returns to `0x005C5B36`) and
from the HelpText FinishFrame callback (return `0x005CCE46`). A tooltip has no icon
(icon type 3), so the band of the hint dedup is not made for it.

Each tooltip ends in one call to `CameraHelp::DrawKeyOrMouse` (`0x00447EA0`, cdecl,
11 arguments, the text is a wide string). The mod hooks that call and keys it by
its text, its icon and its icon type, never by position. The first copy in a pass
draws and a second with the same key is skipped. The icon inside a speech bubble
(return `0x005763D1`) is left alone. The kept copy is drawn flat at the Hand and
gets the interface size about its own center. The log counts both:

```
ui depth     : N tooltips drawn, M second copies skipped
```

### Interface size

The game sizes its help layer from the render height. A tooltip is a 25th of the
screen height and its text two thirds of that, so at a 2880 pixel render the text
is about 77 pixels tall. `ui_scale_pct` (default 25, 25 to 100, not in the
launcher) scales that layer toward the center of the view. It applies by scope,
inside the HelpText FinishFrame callback `0x005CD020` (subtitles, help text, the
Creature panel at `0x005178D0`) and `HelpSystem::Draw3D` (`0x005C59A0`). The caller
ranges alone are not enough, because the text submitter and the widget toolkit also
draw loading screens, the front end and menus.

The box behind the subtitles is queued by `fn_005CCE60` inside the help scope but
drawn later by the FinishFrame rect callback `0x0081E7D0`, outside it. So the size
also applies to the box's draw (`0x0081E3C0` to `0x0081E58C`), and the letters stay
in it. The glyph cache keeps two copies of each letter (40 rows and 20 rows). The
game picks one from the size it asks for, before the mod scales. At 30 percent a 77
pixel letter is drawn from the 40 row copy at about 1.7 to 1. Whether the cache
pages get mip levels on this backend is not known.

Menus and dialogs keep their size. Their text is a fixed 20 to 35 pixels at any
render size, and they are the only interface that the player clicks, so moving
them would move them away from their hit test. Every SetupBox dialog is drawn by one
FinishFrame callback, `0x006424E0`, which holds the only two calls to
`SetupBox::Draw`. Inside it the mod gives interface batches the interface depth and
nothing else (no hint rule, no size, no subtitle lift). This is needed because the
rows of the Controls page draw their icons with `fn_00447450`, the same function
that draws the hint beside the Hand.

### Subtitles

The subtitle bar sits at the bottom of the render. On a monitor that is a few
degrees below the middle of a 60 degree view. In a headset it is about 55 degrees
below the middle of a 110 degree view. One caller draws the bar,
`0x0081E3C0` to `0x0081E58C`. The letters come from
`LH3DTech::Draw3DScreenTriangle` (`0x0081C5C0`), the generic text submitter that
menus and buttons also use, so the caller cannot tell them apart. The band can: the
letters sit in the same rows as the bar they are written on.

- `subtitle_lift_pct` (default 0) moves the bar and its words together. At the
  interface size of 25 percent the subtitles are about 16 degrees below the middle.
- `subtitle_deg` (default 35) moves the box and its letters down together, after the
  size, until the middle of their band is that many degrees below the middle of the
  view. 0 leaves them where the size puts them. The move uses the vertical term of
  the projection, so it holds at any render size.
- Subtitle batches are never part of a hint. With the head up, the Hand sits low in
  the picture, in the rows of the subtitle box. The hint rule would take the box as
  part of the hint, resize it and drop letters that look like a copy of each other.
  The rows are tested before any transform, so the test is exact.

```
ui depth     : N subtitle batches placed at 35 degrees, M kept out of the Hand's hint
```

The Shift F12 test key steps `subtitle_deg` through 25, 30, 35, 40 and 45.

### Subtitles in the world (parked)

`ui_worldlock.cpp` stays in the build but nothing calls it. Subtitles that hang in
the world read worse than subtitles on the view, so the setting (`ui_world_lock`),
its hotkey and its draw hook step do not exist. The math is kept for a later
interface in the world: the rotation that is needed is the difference between the
two cameras, `transpose(render) * anchor`, which is the identity when the head is
straight ahead. The head applied twice about the wrong axis is the fault to avoid.
See `ui-layer.md`.

### Cinema bars

`HelpSystem::SetWideScreen` sets the game's state, and the tick in
`HelpSystem::Draw3D` copies the bar share to `0x00EB9950`. Only four readers use
that copy. `LH3DRender::FinishFrame` draws two opaque bars from it at `0x0082F6AE`
and `0x0082F6DD`, and the screen fade draws them again. The mod sets the copy to
zero for the length of `FinishFrame` and puts it back after. So no bar is drawn and
the logic that reads the state sees no change. The HelpText region moves up by the
bar height, so its bar height function (`0x005C5780`) returns zero while the bars
are hidden, and the subtitles stay where they are in play. The bars are hidden only
once that hook is in. `cinema_bars=1` shows the bars. There is no hotkey.

### The advisors

The two spirits are real 3D meshes. `HelpDudeControl::Update` (`0x005C3A30`) places
them in each eye pass at a point in camera space, with the camera's own axes.
`HelpDude::ScreenToWorld` (`0x005BD2A0`) unprojects a screen point through
`0x00EA1D28` and the stored camera. The head camera is in that matrix, so the
spirits would ride the face of the player.

`spirit_anchor.cpp` (`spirit_anchor=1`, default) gives the spirit code a room camera
for the length of `Update`, `ScreenToWorld` and `WorldToScreen`: the game camera
position, world up, and a forward that holds the yaw and pitch of the head from
when the spirits came. It swaps `0x00EA1D28`, `0x00EA9E40` and `0x00EA1DB8` in and
puts them back exactly. The mesh is drawn through the real eye matrices, so the
player can look at the spirits or away from them. The forward is decided once a
frame, so both eyes place the spirits at one world point. The mod takes the
direction of the head again after a jump, a snap turn, a story camera cut or a
recenter, and when the spirits come back after half a second away. A mono frame
drops the anchor. A long frame does not.

`HelpDude::Draw` places the light of the spirits with a direct call to
`UnprojectScreenPoint` at `0x005C0747`. That call goes through the room camera too.
The call site is patched only when its five bytes are the expected ones.

A script can send a spirit to a place of the world. `HelpDude::Update1`
(`0x005BDDA0`) then makes the mesh 4 times as large as at the usual place. On the
table and in the God view the mod gives the spirit the angle of size of its usual
place there (`spirit_world_size=1`, default). It changes the address in one `fmul`
(`0x005BE947`), only when the bytes are the expected ones. See `tabletop.md`, "A
spirit at a place of the world".

Open: when a spirit looks at the Hand, the game unprojects the pointer through the
room camera, but the pointer is a pixel of the live view. With the head turned away,
the spirit looks a little beside the Hand.

## Sky, clouds and detail

### Clouds

A cloud is an `LH3DMist`: a dome (`mist.l3d`) with its base at y 0 and its pole at y
20, scaled 13 to 50 times. `LH3DMist::Draw` (`0x007FA300`) turns each one by copying
a basis from `0x00EA1C98` (at `0x007FA38C`). Its rows are the camera right, the
camera forward reversed and the camera up. `LH3DTech::UpdateCamera` builds it from
the camera it gets, which is the head camera. The body of the dome sits 130 to 500
units from its pivot along that basis, so a nod would swing each cloud across the
sky.

A hook on `LH3DMist::Draw` gives each cloud its own basis: facing the game camera
position at `0x00EA1DB8` (no head) with world up. With the head straight ahead this
is the engine's own basis. Weather clouds and ground mist are the same class. The
engine makes the pole `20 * A` long (`A` is the scale at `+0x88`, divided by a
flatten term for sky clouds: flag value 2 at `+0x80`, flatten at `+0x8C`), which is
260 to 1000 units seen from below. With the pole pointed at the eye, a cloud nearer
than its pole length pulls its dense middle down to the eye. So the pole is at most
`cloud_pole_pct` (default 25) percent of the distance to the cloud. It lies along the
line of sight, so the outline stays and the geometry stays away from the eye. The
basis of the game camera does not work, because the dome is one sided, and the
clouds behind the player would vanish.

`UpdateCamera` also builds a heading turn at `0x00EA1D88` from the stored camera. The
draw at `0x0080DB30` turns billboard meshes at the lowest detail by it, so far trees
would turn with the head. It is built again after each call from the camera the game
asked for, with the engine's `fn_00841260` and the constant at `0x008C78D8`.

`LH3DSprite::Draw` builds the model view of each billboard at `0x00EA9E10` as an
identity rotation in camera space, with the centre from the full per eye matrix. A
billboard for VR wants world up, not the camera's up.

### The sun glare

The sun is a fixed quad at (-30000, h, -30000) and reads no camera orientation. The
glare over it (`fn_0086BB60`, called at `0x0082F4C9`) is the next 3D draw after a
particle effect in the sorted list (`fn_0068B480`), which sets its own camera and
puts the stored camera back with `UpdateCamera` (`fn_0068B7D0`, called at
`0x0068BF2F`). That restore rebuilds the matrices without the per eye offset and
roll. `sun_glare` picks: 0 (default) calls `StereoReapplyEye` first and draws the
glare, 1 draws it as the game does, 2 does not draw it. The `fn_0085AB10` batch (one
a frame, 90 pixels wide, clamped to the screen edges) reads as the glare.

Ruled out as the cause: the full screen quad that `LH3DRender::FinishFrame` draws at
`0x0082F617` (diffuse 0, a transparent fade) and `CloudInSky` (its per frame update at
`0x005E25C0` drifts the 70 clouds in world space and reads no camera).

Open: the clouds and the sun bob a little as the head moves in the headset. The sun
glare is the best candidate, and it is not proven.

### Models keep their detail further out

`LH3DObject::AddForDrawing` (`0x00815A70`, and its twin at `0x00816AD0`) chooses a
mesh by the depth of an object along the view axis, against four multiples of a size
figure: 23.3 (full detail), 66.7 (middle), 86.7 (lowest) and 173.3 (lowest, fading
out). Past the last, a stand-in sprite is drawn, or nothing. The four live at
`0x00C37E8C` to `0x00C37E98` in writable data, with two readers each and no writer.
`detail_distance` multiplies them once, after the mod checks that they hold the
expected values. The key is 0 for the game's own, 1 for 2x, 2 for 3x and 3 (default)
for 5x. The registry value `MaxObjectDistance` does nothing: at `0x008238C4` its
value is read into a stack slot that the next read overwrites.

From one fixed view with both eye passes at 70 degrees: the game's values give 3.6 to
3.8 ms and 864 to 933 draw calls, 3x gives 4.1 to 4.3 ms and 1049 to 1117, 5x gives
4.3 to 4.5 ms and 1089 to 1155. A headset view is wider and costs more. Fog ends at
about 800 to 900 units in clear weather.

Open: the depth is measured along the view axis, not as a distance. An object at the
edge of the view keeps more detail and can drop a level when the player turns to it.
A hook on `0x00868C80` that uses the straight distance would fix it.

## Time, bars and the second eye pass

The game has two kinds of frame time, in milliseconds.

- **The time step of the game.** `GGame::Loop` writes `LH3DTech::g_game_time_inc`
  (`0x00EA9EC0`) one time for each frame, before the scene walk (`0x0054D374`). It
  copies the same value into the game object twice: `+0x250540` (`0x0054D366`) and
  `+0x205D48` (`0x0054D380`). On a paused frame it writes zero to all three.
- **The delta time.** `LH3DRender::StartFrame` (`0x0082F0E0`) writes
  `LH3DTech::g_delta_time` (`0x00C38134`): the sample of its clock less the sample
  that it kept at its call before (`0x00EA9EAC`), and 1 when that is not more than 0
  (`0x0082F184` to `0x0082F195`). The number has no upper limit. The clock comes
  from `GetTickCount`, which steps 15 or 16 ms. `GGame::Process3dEngine` calls
  `StartFrame` at its start (`0x0054DAB5`), and in play no other code writes the
  number.

### The time step in the second eye pass

The second eye pass has zero in the time step and in its two copies in the game
object. The three numbers go back after the pass. The reasons:

- `HelpSystem::Draw3D` runs in both passes and adds `+0x205D48` to the widescreen
  transition.
- The walk cycle of animals (`sub_0051AF00`, from `Animal::Draw`, `Dove::Draw` and
  `SpellWolf::Draw`) steps the mesh by `+0x250540`.
- A 50 ms emitter reached from `GLandscape::Draw` adds `+0x250540` too.

The game writes zero to them itself on a paused frame, so zero is safe for their
readers. The key `freeze_time` (default 1) covers the time step. The keys
`skip_hand_pass2`, `skip_physics_pass2` and `skip_hand_prepare` guard other work
of the second pass (defaults 1, 1 and 0).

### The delta time in a pass

The mod runs the scene walk two times, so `StartFrame` runs two times in a frame.
The first eye pass gets the time since the start of the second pass of the frame
before. The second eye pass gets the time of the first pass, and 1 ms at the
least. `freeze_time` also writes zero to the delta time before the second pass,
but that zero lasts only to the call of `StartFrame` at the start of the pass.

So a reader of the delta time in the scene walk counts in both passes. The hand
states, `HelpSystem::Draw3D`, `Temple::Update`, `TempleRoom::Update` and the draw of
a room of the Temple are such readers [R]. `Temple::UpdateFade` counts in the first
pass only: the world steps run one time a frame (`pass_guard.cpp`). In the second
pass it runs with a frame time of 0, so it gives its fade to that eye and moves
nothing (`temple_fade_eyes`, `controllers.md`, 11.4). For one frame
the two numbers have the real time as their sum, and 1 ms more for each pass that
got the limit. In desktop runs at 50 to 80 frames a second the first pass got 11 to
15 ms and the second pass 2 to 6 ms as a mean, and their sum was 4 to 9 percent
more than the real time [R]. This is a known limit (roadmap B14).

### The delta time after the passes

The frame loop of the game reads the delta time outside the scene walk too. In the
plain game it finds the time of one whole frame there.

| Reader | Where in a frame | What counts with the number |
| --- | --- | --- |
| `GCamera::Update` `0x00441F80`, through `GGame::GetCameraTimeInc` `0x00555820` | Before the scene walk | The update of the camera mode of the player and the 6 Zoomers of the game camera. So each move of the game camera that takes time: a flight, the fit to the land after a move of the view, the follow of the Creature, the pull back past 3500 units, and the moves by the keys (`CameraModeNew3::ProcessKeyMovement`). While a cut scene with the cinema bars has the dialogue (`HelpSystem::IsDialogueControlled`), the function gives the time step of the game and not the delta time |
| `CameraModeNew3::Update`, at `0x0045BC87` | The same | 1 s before the camera of a Creature fight starts (`StartFight`). No run reached it |
| `CameraModeNew3::UpdateTricons`, at `0x0045924F` | The same | A wait of the camera mode that the interface starts (`0x00C5E178`) |
| `CameraModeFree::Update` at `0x0044E306`, and `ProcessKeyMovement` at `0x0044EA61` | The same, by the listing | The free camera. No run reached it |
| `TempleRoom::UpdateMouse`, at `0x0079A507` | After the scene walk, from `CMouse::ProcessButtons` | The seconds of each update of the camera of a room of the Temple (`controllers.md`, 11.1) |

After the two passes the mod gives the game the time of the whole frame
(`frame_time_real=1`, `frame_time.cpp`): the sample that `StartFrame` keeps after
the passes less its sample from before the passes, with the limit of 1
(`frame_time_math.h`). Both passes are between the two samples, and `StartFrame`
makes the same number in a frame with one scene walk. The number that each pass
gets does not change with the key.

With `frame_time_real=0` the mod writes the number from before the passes. No code
changes that number outside the passes. So the game then reads, for the whole
session, the number that the last `StartFrame` before the first stereo frame left.
After the load of a saved game ("Continue Game", and a run in a test save) that
number is not the same in each run: it was 1 ms in 14 runs, and 15 or 16 ms in 3
runs [R]. In a new game it was 31 or 32 ms from the end of its start dialog [R].
At 60 frames a second a move that counts 1 ms for each frame takes 16 times its
own time, a move that counts 15 ms takes about its own time, and a move that
counts 31 ms takes half of it. The mod follows that number with each value of the
key (`Kept` in `frame_time_math.h`): a `StartFrame` between two frames with eye
passes gives a new one. So the test key gives the number of the session at each
moment, and not the last real time.

Seen in desktop runs with the value 1 and with the value 0 [R]:

| The move | 1, a test save | 0, a test save | 1, a new game | 0, a new game |
| --- | --- | --- | --- | --- |
| The way through a door of the Temple, 5 s | 5.0 s | 85.9 s | 5.0 s | 2.8 s |
| The look at a picture of the Save Game Room | 1.4 s | 17.2 s | 1.4 s | 0.6 s |
| The flight of the hall with `temple_camera_flight=1`, 5.5 s | 5.5 s | | | |
| The flight of the game camera to the Creature pen, with `instant_jumps=0` | 2.3 s | 22.4 s, and the camera then moves in small steps for 4 s more | | |
| The move of the game camera to the door of the Temple at the start | | | 1.4 s | 1.0 s |

The two rows of the game camera have the time of the log line of the watch
(`god-view.md`, 15.3), which has 0.25 s of rest of the camera in it.

In the same runs these things had the same speed with each value [R]:

- The smooth zoom of the stick: 2.00 zoom steps for each second of a hold of 45
  frames.
- A throw: the first contact came 1.0 to 1.2 s after the release, the object was
  at rest 3.6 to 4.7 s after the release, and the game ran 9.5 to 9.8 turns for
  each second of the flight.
- The Creature state: the state started at the same frame, and the strokes and
  the three slaps of the recipe came at the same frames.
- The number that each eye pass gets, and the frame cost (`bench_run.ps1`: the
  work mean of each of the six scenes changed by 0.2 ms or less).

They take their time from the scene walk, from the game turn or from the clock of
the mod. The fades of the hint card use that clock too (`QueryPerformanceCounter`).

One thing was different, and the cause is not known. In the recipe of the stay
(`creature_recipe.ps1 -Kind stay`), the first stay comes 1 s after the start of
play, and the plan of the Creature is then desire 9 (to idle around with the
player) [R]. It was at step 1 of 2 with an action of the body with the value 1
(three runs). It was at step 0 of 2 with no such action with the value 0 and 1 ms
as the number (three runs). A Creature with an action of its body stood still for
that stay 2.6 s after the start, and not 0.2 s after it. The game camera is the
one reader of the number on the land, and with the value 1 it comes to rest
sooner.

The plan of the Creature later in a run does not depend on the key [R]. In the
recipe of the Creature state, 2 s after the state, the plan was one of two: desire
3 (to play) and action 172 with no action of the body, or desire 18 (to show its
state) and action 23 with an action of the body. With the value 1 it was desire 3
in 4 runs and desire 18 in 2 runs. With the value 0 it was desire 18 in 3 runs and
desire 3 in 2 runs, and each plan came with 1 ms and with 15 or 16 ms as the
number. A Creature with an action of its body stood still for a stay 0.9 to 2.7 s
after the start. The names of the desires are from the headers of the
decompilation.

**The other readers.** The code of the game has 80 references to the number. A
count with a data breakpoint on the number (`frame_time_test_readers`) found no
other reader outside the scene walk in three runs: in the hall and the Save Game
Room of the Temple, at the door of the Temple with the Creature state, and in a
fetch and a throw at the Creature pens [R]. One block copy of the game
(`0x00837B41`) goes over the address and does not use the number. By the listing,
the other references are in the scene walk (the hand states, the help system, the
draws of the rooms of the Temple, the Creature, the glow of the Hand, the charge of
a Worship Site, a miracle that falls from the sky, the boxes of the setup kind), or
in code that a single player game does not run in play: the front end, the logos,
the Temple of a multiplayer game, the text messages of players and a debug
display.

**The keys.** With `test_keys=1`, Ctrl, Shift and the star key of the number pad
change `frame_time_real` while the game runs: a long pulse for the real time and a
short pulse for the number from before the passes. The log has one line at the
start, one line for each 450 frames, and one line for each press of the test key:

```
frame time   : frame_time_real=1. After the two eye passes the game has the real time of the frame as its frame time (LH3DTech::g_delta_time), so the cameras of the game and each other reader outside the passes move at the speed of the game
frame time   : after the two eye passes the game had 16.4 ms as its frame time (1 to 32 ms, frame_time_real=1). The real time of a frame was 16.4 ms on the clock of the game, so a move of the game that counts this number took 1.00 times its own time. Eye pass 0 got 13.4 ms and eye pass 1 got 4.0 ms, 450 frames
frame time   : after the two eye passes the game had 1.0 ms as its frame time (1 to 1 ms, frame_time_real=0). The real time of a frame was 16.4 ms on the clock of the game, so a move of the game that counts this number took 16.39 times its own time. Eye pass 0 got 13.4 ms and eye pass 1 got 3.9 ms, 450 frames
frame time   : Ctrl, Shift and the star key of the number pad: frame_time_real is 0 now. After the two eye passes the mod puts the frame time of the game back to the number from before the passes, so the code of the game outside the passes reads one number for the session. The number from before the passes is 1 ms in this session, frame 3670
```

In a run in a test save the test key acted after the way through a door (5.0 s
with the real time). The look at a picture then took 17.1 s, as it does with
`frame_time_real=0` from the start [R].

Two test keys are for desktop runs. The launcher removes them from `bwvr.ini` on
each save.

- `frame_time_test_frame=N`: the test key acts at frame N of the game. With
  `fake_controller_play_frame` the frame counts as a step of the script counts it.
- `frame_time_test_readers=N`: from frame N a data breakpoint of the processor is on
  the delta time for the thread of the game, for `frame_time_test_readers_frames`
  frames (600). At the end the log has one line for each instruction of the game
  that read or wrote the number. The line has the count and the least and the most
  number for each part of a frame: outside the hook of the scene walk, in the hook
  and not in a pass, eye pass 0 and eye pass 1. The address of a line is the
  address after the instruction, which is 4 more than the address of the reference
  in the listing. The traps make each frame longer, so do not read a time from
  such a run.

### The lights on the land in the second eye pass

The game has a list of the lights on the land of one scene walk: 200 lights of
`0x34` bytes at `0x00FA2920`, and their count at `0x00FA51C0`. `fn_0086CFF0` adds
one light: a place, a map of values, the size of the map in points of 10 units, a
strength and a kind. `GLandscape::Draw` uses each light of the list (`fn_005E5830`
at `0x005E488E`, `fn_0086D360`). `ClearLight` (`0x005E57B0`, `fn_0086D460`) empties
the list at the end of a scene walk that drew the land (`0x0054E2FA`).

| The code that adds a light | Where in a frame | The light |
| --- | --- | --- |
| `fn_00837200`, from `LH3DAtmos::Update3D` (`0x008357C9`) | In the scene walk, before the draw of the land (`0x0054DEC7`). It is a step of the world | The white light of a lightning of a storm. Its map is white, 64 by 64 points (`0x00ED92F0`, `fn_00837110`): the land is white for 320 units round the lightning. The strength starts at 1 or at 0.5 (`fn_00837290`, from `GWeather::Update`), and it falls for 0.8 s (`fn_008372D0`) |
| `PSysLightMaps::AddDrawing` (`0x006CA6E0`, `fn_006CA280`) | In each scene walk (`0x0054DEAD`) | The light maps of the particles. A particle gives its map to an array when the game draws it (`ParticleLightMap::DrawAt` `0x0067B220`, `RenderParticleMist::DrawAt`, `fn_00731560`). The function adds each map of the array, and then it empties the array (`fn_006CA660`) |
| `DanceLight::Process` (`0x0050F919`), from `GroupBehaviour::Process` and `Dance::Process` | Not in the scene walk, by the listing | The lights of a dance |
| `fn_005E25C0` | In each scene walk, from `GLandscape::Draw` | Not known |
| `GWeather::DrawClouds` (`0x008400C6`) | Not known | A map of kind 2 at a storm cloud |

The first eye pass uses the lights of the list and empties it. So a light that
comes one time in a frame shows in the first eye pass only, and the land is
white in the left eye and not in the right eye:

- The second eye pass skips `LH3DAtmos::Update3D`, because it is a step of the
  world. So it has no light of a lightning of a storm.
- A light from code before the scene walks is in the list of the first pass
  only.
- The call of `PSysLightMaps::AddDrawing` of the first pass empties the array of
  the particles. The second pass has only the maps that came to the array after
  that call.

**The lights show in both eyes** (`land_light_eyes=1`, `land_light.cpp`,
`land_light_math.h`). The mod holds a copy of three groups of lights of the first
eye pass, and it adds the copies to the list again in the second eye pass:

| The lights of the first eye pass | Where the second eye pass gets the copies |
| --- | --- |
| The lights that came to the list after the last eye pass of the frame before | At the start of the pass |
| The lights that `PSysLightMaps::AddDrawing` adds | After that call, and only when the call of the second pass added no light |
| The lights that `LH3DAtmos::Update3D` adds | Where the pass skips that step |

No step runs a second time, so nothing moves two times. The second pass draws
from the list and empties it as the game does. A scene walk with no draw of the
land does not empty the list. The mod adds nothing while the list still has the
lights. With `land_light_eyes=0` the second eye pass does not get those lights.

Ctrl, Shift and the Menu key change `land_light_eyes` (`test_keys=1`): a long
pulse for both eyes and a short pulse for the left eye only. The test key
`land_light_test=1` adds the light of a lightning at its start, as `fn_00837200`
adds it, at the place of the Hand, for 150 frames of each 500 frames. The log has
one line for each run of frames with such lights, and their counts for each
window. A second line of a window has the count of the light maps that the
particles added in first eye passes and in second eye passes. In a desktop run
with the test light and `eye_test_luma=1`, the mean brightness of the eye
pictures was 162 for the left eye and 73 for the right eye with
`land_light_eyes=0`, and 162 and 163 with `land_light_eyes=1` [R]. The recipe,
with each value of the key:

```powershell
.\bwvr\tools\desk\hand_run.ps1 -Name light0 -Dll <dll> -Kind fixed -Seconds 75 `
  -Set "eye_test_luma=1;land_light_test=1;land_light_eyes=0;one_walk=0"
```

The brightness test reads no picture in one walk for both eyes, so the recipe
has `one_walk=0`.

Open: no desktop run had a real lightning, of a storm or of a miracle. Only the
test light is shown in a run, and no run had a light map of a particle. Not
known: which effects have a particle with a light map, and whether the second
eye pass has such a map by itself. The line of the window in the log of a
session with such an effect shows it.

### The color over the picture at the end of a frame

`LH3DRender::FinishFrame` draws the color at `0x00FA51D8` over the picture when its
alpha is not 0, and then it clears the color (`fn_0086FEE0`, called at
`0x0082F753`, the clear at `0x008701DB`). Each eye pass ends with `FinishFrame`. So
a color shows in both eyes only when its writer writes it in each eye pass.

| The writer | Where in a frame | In both eyes |
| --- | --- | --- |
| `GScript::ProcessFade` (`0x006EB9D0`): the fade of a script (`GScript::SetFade`, `GScript::SetFadeIn`), of the skip of a story (`GGame::ProcessKey`), of a video (`PSysGlobal::StartAVISequence`) and of a new game | `GGame::Process3dEngine` calls it with 1 in each scene walk (`0x0054E2D7`): it writes the color and steps nothing. `GScript::Process` calls it with 0 in the game turn (`0x006EB6E4`, from `GGame::ProcessTurn`): that call steps the alpha | Yes, by the listing |
| `Temple::UpdateFade` (`0x00794280`, the write at `0x00794361`) | In each scene walk in place of the fade of a script (`0x0054E2DE`), while the numbers at `0x00E06020` and `0x00C2A150` are not the same, or the state at `+0x205A28` of the game is 1 | Yes, with `temple_fade_eyes=1` (`controllers.md`, 11.4) |
| The front end (`0x0053CD9B`, `FrontEnd::JustDoABox`) and the logo (`RunAlexFX`) | Their own frame loops | They have no eye passes |

With the state 3 at `+0x205A28` the scene walk calls none of the two
(`0x0054E2AA`). Not known: what that state is. A fade of a script in that state
has only the write of `GScript::Process` in the game turn. So by the listing it
shows in the first eye pass only, and only in a frame with a game turn. No run
reached that state.

`FinishFrame` also draws a white wash of the atmosphere before that color
(`fn_00836200`, called at `0x0082F74E`). Its strength is the number at `0x00EDC374`,
0 to 90 of 255. `LH3DAtmos::Update3D` makes it from the number at `0x00EDD388`
above 0.6, which `GLandAlignement::DrawSky` writes. The number stays between the
passes, and `LH3DAtmos::Render3D` sets the flag of the draw (`0x00EDD3A4`) in each
scene walk. So both eyes get it, by the listing. No lightning writes it.

## Testing without a headset

Most of what goes wrong with head tracking can be seen in a still picture: the sea
in the wrong place, the interface riding the view, geometry culled against the wrong
frustum. The mod can invent a head. Given a yaw and a pitch it builds the pose that
the runtime would report, and the rest of the path is the same.

```ini
vr=0
stereo=1
stereo_headless=1     ; walk the scene twice with no runtime
head_tracking=1
fake_head=1
fake_head_yaw=40
fov_absolute_deg=110  ; the angle of the headset, without a headset
capture_every_seconds=6
test_keys=1
```

Both eye images are written as BMP, which converts to PNG. `fake_head_pitch` and
`fake_head_roll` take negative values. `fake_head_sweep=1` alternates the whole
pose with a level head, so a pitch can be tested like a turn.

A test drives the game from inside the game process. It never moves or clicks
the real mouse, and it never brings a window to the front. The tools:

- The scripted controller (`fake_controller`, `fake_controller_script`) holds the
  buttons of a controller and steps its hands (`controllers.md`, section 4). It
  also closes the start dialog of a new game: `tools/desk/dialog.ps1` gives
  the points for the render size. In a run with a test save the test key
  `menu_skip_test_frame` closes the Main Menu ("A test save").
- The test mouse (`fake_mouse_script`) moves the pointer and presses the left
  button inside the mod (`god-view.md`, section 10).
- The scripts of `bwvr/tools/desk/` start the game with these keys, put the
  player's files back, and read the log of the mod. `desk_run.ps1` is the only
  script that starts the game, and `tools/desk/README.md` has the rules of a
  run. Check the first lines of each run log.

Three traps:

- **An invented head must be absolute, not recentered.** Recentering takes the first
  pose as straight ahead, so a fixed fake yaw cancels itself out.
- **Stereo can be checked as a number.** Distant things must be identical between the
  eyes and near things must not. On a good frame: sky 0.01 levels of difference,
  horizon 2.96, sea 2.08.
- **The scene must hold still.** The start dialog of a new game, before the
  tutorial, is a fixed camera and a good test scene.

**The brightness of the two eye pictures** (`eye_test_luma=1`, `eye_luma.cpp`).
A fade that only one eye pass draws, and a frame with no picture, do not show
in a still picture of one moment. With the key, the mod reads the mean
brightness of each eye picture after its pass, from 0 to 255, on a grid of 24
by 24 points (`eye_luma_math.h`). The log then has:

- one line for each run of frames in which one eye is much darker than the
  other eye (a difference of more than 10, and of more than a quarter of the
  brighter eye), with the two numbers at the frame with the largest
  difference,
- one line for each run of frames in which both eyes are black (below 4). A
  run of 8 frames or less between two frames with a picture is a black flash,
- one line for each window of 450 frames with the counts, the largest
  difference between the eyes, and the brightest frame with the number of
  each eye. A flash of light in one eye is a run of the first kind: the other
  eye is the darker one. A flash that both eye passes draw is no such run,
  and the brightest frame has it in each eye.

Each line has the fade of the Temple at the start of the run and after it. The
read locks the render target, so the key costs frame time: about 1 ms for each
eye at 1440 lines, and more at the size of a headset. It works in a headset
session too.

The launcher removes the retired keys from `bwvr.ini` on every save (the list is
`kRetiredKeys` in `launcher.cpp`), and it writes `stereo=1`. Set test keys such as
`eye_separation_milli`, `freeze_time` and `stereo_headless` after the last save, or
start the game without the launcher.

## Keys for v1.0

Hotkeys read `GetAsyncKeyState` and work only while a window of the game process is
in front. The mod reads the keys and never takes them from the game.

| Key | For | Effect |
| --- | --- | --- |
| F11 | Everyone | The direction of the head becomes straight ahead |
| Ctrl F11 | `play_mode` 3 or 4 | The table or the God view goes off and on (`tabletop.md`, `god-view.md`). Press it with the mouse at rest |
| F12 | `test_keys=1` | Saves a picture of each eye beside `runblack.exe`, and one of the frame before either eye pass |
| Shift F12 | `test_keys=1` | The subtitles step through 25, 30, 35, 40 and 45 degrees below the middle of the view |
| Ctrl F12 | `test_keys=1` | Who owns the pointer: automatic, the controllers only, or the mouse only (`controllers.md`) |
| Ctrl Shift F12 | `test_keys=1` | The ears: the head, or the game camera (`audio.md`). Press it with the mouse at rest |
| Shift F11 | `test_keys=1` | The grab of the world: as set, one to one, or off (`god-view.md`). Press it with the mouse at rest |
| Ctrl Shift F11 | `test_keys=1` | The interface panel on and off, when `ui_panel` is not 0 (`ui-layer.md`) |
| Numpad 0 | `test_keys=1` | The cut scene window goes off and on (`cutscene-window.md`) |
| Numpad 1 | `test_keys=1` | The depth limit of the cut scene window goes off and on |
| Ctrl Numpad 1 | `test_keys=1` | The game camera stands over the land under the head or over the recenter place (`god-view.md`, section 7) |
| Ctrl Numpad 0 | `test_keys=1` | The God view follows the game camera or not (`god-view.md`) |
| Ctrl Delete | `test_keys=1` | The choice of a far target steps from 2 to 3 to 4 to 0 to 1 and back: the body with ranks, balls, balls with ranks, one point, the body with a bias. One short pulse for each unit of the value and one long pulse for 0 (`far_grab_choice`, `grab.md`, "The cone and the choice"). Shift must be up |
| Ctrl Backspace | `test_keys=1` | A living thing that has the halo of a far target keeps it against a tree or a rock while the aim is near it, or the margin between two scores is the only rule. A long pulse and a short pulse (`far_grab_stay`, `grab.md`, "The cone and the choice"). Shift must be up |
| Ctrl Page Up | `test_keys=1` | The reach of the pick for an object that moves: the reach at rest and the extra reach, with a guard of a throw that ends when the grip is fully open (3), the same with a guard for the whole grace time (2), the extra reach on `hand_pick_cm` only (1), or the same reach as a still object (0). A pulse that is shorter for each lower value (`catch_moving`, `grab.md`) |
| Ctrl Shift Page Up | `test_keys=1` | A holdable object up to 15 cm from the Hand's point is the target when nothing is nearer, or the pick reaches only `hand_pick_cm` (`hand_wide_pick_cm`, `grab.md`) |
| Ctrl Insert | `test_keys=1` | The ring of the teleport: one size in the world, or one size in the room (`teleport_ring_mode`, `god-view.md`) |
| Ctrl Shift Insert | `test_keys=1` | The stick of the God view zooms in steps, or while it is held (`god_zoom_smooth`, `god-view.md`, section 2) |
| Ctrl Shift Delete | `test_keys=1` | The point that a zoom of the God view keeps at its place in the room: the land that the head looks at, the land under the head, or the Hand's point (`god_zoom_about`, `god-view.md`, section 2) |
| Shift Scroll Lock | `test_keys=1` | A still hand high over the land lets the object fall, or sets it down at each height (`release_fall_cm`, `throw.md`). Ctrl must be up |
| Ctrl Shift Scroll Lock | `test_keys=1` | An object that comes to rest while the Hand takes it stays in the physics of the game until the game puts it in the Hand, or the game ends its physics and a tree is then a dead tree (`catch_rest_guard`, `grab.md`, "An object that comes to rest while the Hand takes it"). A long pulse and a short pulse |
| Ctrl Shift Backspace | `test_keys=1` | An object that moves is in the hand at the press of the grip, or it moves on until the game takes it, which is 225 ms and some game turns after the press. A long pulse and a short pulse (`catch_at_once`, `grab.md`, "A catch at once"). Backspace with Ctrl and no Shift is the key of `far_grab_stay` |
| Ctrl Scroll Lock | `test_keys=1` | The picture of a thrown object between the steps of the game: a touch of another object has the real time and the picture does not go back behind a guess, or each substep has its time of the flight (`throw_draw`, `throw.md`, "The picture between the steps"). Shift must be up |
| Shift Pause | `test_keys=1` | A tree that falls turns down fast on the land, or keeps the game's turn (`throw_tree_settle`, `throw.md`). Ctrl must be up |
| Ctrl Shift Pause | `test_keys=1` | After its landing a thrown object loses a turn in place fast, the brake of the land is off on steep land and an object that lies still for 0.5 s is at rest, or the object keeps the turn of the brake, the brake acts on each land and only the rest test of the game ends the landing (`throw_land_rest`, `throw.md`, "The rest after a landing"). A long pulse and a short pulse |
| Ctrl End | `test_keys=1` | The prompt of a target shows at once, or after its dwell (`prompt_timing`, `controller-hints.md`) |
| Ctrl Home | `test_keys=1` | The words of a target on the hint card: 3, 2, 1 or 0, with one short pulse for each unit of the value and one long pulse for 0. With 3 the prompt "Pull to fetch" stays from the end of the far target until the Hand holds the object, and a hint of the game that a prompt owned does not show for 0.25 s after the prompt went. With 2 only the prompt "Take" stays, from the close of the grip until the Hand holds the object. With 1 the hint of the game shows in those frames. With 0 the hint of the game takes the place of each prompt (`hint_words`, `controller-hints.md`) |
| Ctrl Numpad 5 | `test_keys=1` | The rule of a Creature command steps from 4 to 3 to 2 to 1 and back. One short pulse for each unit of the value (`creature_command_rule`, `creature.md`, section 4) |
| Ctrl Numpad Star | `test_keys=1` | In the Temple: the camera of a room is at its rest pose at once when the player comes into the room, or it flies there as in the plain game. A long pulse and a short pulse (`temple_camera_flight`, `controllers.md`, 11.4). Shift must be up. The star key alone is the key of the release at once (`throw.md`) |
| Shift Numpad Slash | `test_keys=1` | In the Temple: three ways of the view of a room. Each eye pass draws from the head pose of the frame with its roll and its eye, and the head does not turn the camera of the room. Or the same view, and the head turns the camera of the hall when it looks at the pool. Or each eye pass draws from the camera that the game builds in the pass, with no roll and no depth. A long pulse, a pulse of middle length and a short pulse (`temple_view`, `temple_camera_screen`, `controllers.md`, 11.4). Ctrl must be up |
| Ctrl Shift Numpad Plus | `test_keys=1` | The fade of the Temple shows in both eyes, or in the left eye only. A long pulse and a short pulse (`temple_fade_eyes`, `controllers.md`, 11.4). Ctrl must be up. The plus key and the slash key with no Shift are keys of the throw (`throw.md`) |
| Ctrl Shift Menu key | `test_keys=1` | The white light of a lightning on the land, and each other light on the land that comes one time in a frame, shows in both eyes, or in the left eye only. A long pulse and a short pulse (`land_light_eyes`, "The lights on the land in the second eye pass") |
| Ctrl Shift Numpad Star | `test_keys=1` | After the two eye passes the game has the real time of the frame as its frame time, or the number from before the passes. The cameras of the rooms of the Temple and the game camera of the land then move at a different speed. A long pulse and a short pulse (`frame_time_real`, "The delta time after the passes"). Press it with the mouse at rest |
| Ctrl Page Down | `test_keys=1` | The card says what the opening of the grip does with a tree, or shows the text of the game (`tree_plant_hint`, `grab.md`) |
| Ctrl Shift Page Down | `test_keys=1` | The roots pile of a tree from a distance stands at the roots, or on the way of the glide (`far_roots_at_tree`, `grab.md`) |
| Ctrl Shift Home | `test_keys=1` | A dip of the squeeze in the first 300 ms of a press does not open the grip, or the fast fall opens it at any time (`grip_press_guard`, `grab.md`) |
| Shift Numpad Plus | `test_keys=1` | The button of the game stays down until the game has the object in its Hand, and a press on an object that moves is no click for the game, or the button goes up 167 ms after the object came into the Hand of the picture: with `catch_at_once=0` a grip on an object that moves is then a click, the Hand lets the object go, and a rock splits. A long pulse and a short pulse (`grip_press_hold`, `grab.md`, "The press of the game"). Ctrl must be up |
| Ctrl Shift End | `test_keys=1` | A small object in the Hand is held in a pinch between the thumb and the index finger, or in the fist (`hand_pinch`, `hand-model.md`) |
| Shift Home | `test_keys=1` | An advisor at a place of the world has the size for the eye that it has at its usual place, or the size of the game (`spirit_world_size`, `tabletop.md`) |
| Shift End | `test_keys=1` | A grip that stays closed on the far Creature makes it stay and a short grip changes the leash when it opens, or each grip changes the leash when it closes (`creature_hold_stay`, `creature.md`, section 5.3). Ctrl must be up |
| Shift Insert | `test_keys=1` | The prompt of a text that waits for a click shows in each view and the trigger is that click, or the prompt has the rule of each other hint of the left button (`text_click`, `controller-hints.md`, section 10). Ctrl must be up |
| Shift Page Up | `test_keys=1` | While the Lionhead logo plays in stereo: the motion of the view steps through 1, 2, 3 and 0. With 1 the camera goes along the path of the game and does not turn, with 2 the orbs are a model in a room that stays still, with 3 the camera of the game turns the view, with 0 the camera is still. One short pulse for each unit of the value and one long pulse for 0 (`intro_motion`, `intro.md`). Ctrl must be up |
| Shift Page Down | `test_keys=1` | While the Lionhead logo plays in stereo: each eye has its own trails, or each eye pass draws over the picture of the other eye and each orb shows two times (`intro_eye_trails`, `intro.md`). In play: each eye pass of the scene starts from its own frame before, or the two eye passes share the render target (`eye_clean`, `stereo-artifacts.md`, "The frame before of each eye"). A long pulse and a short pulse. Ctrl must be up |
| Shift Delete | `test_keys=1` | While the Lionhead logo plays in stereo: the mark beside the logo has the depth of the logo, or the same pixel in each eye (`intro_text_place`, `intro.md`). In play: a speech bubble of the game (a signpost, a villager that speaks, the Creature) is one flat card at the depth of its sign, or it is drawn as the game and the interface depth make it in each eye pass (`bubble_card`, `ui-layer.md`, section 18). A long pulse and a short pulse. Ctrl must be up |
| Ctrl Pause | `test_keys=1` | The mod skips the wait of 20 ms of the audio DLL at the stop of a sound that the mixer of the mod plays, or each stop has the wait. A long pulse and a short pulse (`audio_stop_wait`, `audio.md`, 7.6). Shift must be up |
| Ctrl Shift Numpad Slash | `test_keys=1` | The mod waits for the GPU after each eye pass, one time a frame, or one time a frame on a second thread that also gives the frame to the runtime. A long pulse, a pulse of middle length and a short pulse (`frame_send`, "One wait a frame, and a second thread"). The slash key with Shift and no Ctrl is a key of the Temple, and with no Shift it is a key of the throw |
| Menu key | `test_keys=1` | One walk for both eyes on and off (see "One walk for both eyes"). The key beside the right Ctrl key that opens a context menu. Ctrl and Shift must be up |
| Ctrl Menu key | `test_keys=1` | The layout of the buttons of the hand that does not point. The menu button of the left controller opens the game menu, Y opens the ring and the stick does not drive the ring, or Y opens the game menu and X and the stick open the ring. A long pulse and a short pulse on that hand (`button_layout`, `controllers.md`, 5.1). Shift must be up |
| Shift Menu key | `test_keys=1` | The walk with the stick of the hand that does not point steps from 1 to 2 to 0 and back: forward is the direction of the head, forward is the direction of that controller, or the stick does not move the player. A medium pulse, a long pulse and a short pulse (`move_stick`, `god-view.md`, section 5.7). Ctrl must be up |

The launcher shows `test_keys` as "Test keys", in a group named "Testing". It is off
by default.

Why these keys. The game binds F1 to F9: F6 to F9 go to the Challenge Room, the Save
Game Room, the Options Room and the Library (`ControlMap::LoadDefaults`,
`0x0046F890`). F10 reaches Windows as a system key and can open the window menu. F11
and F12 have no game action. The default control map has no entry with the code
of Backspace (`0x0E`) either. The game also reads a held Ctrl as its zoom and a held
Shift as its tilt, and both together start its zoom to the Hand. Each of those acts
on a mouse move, so the recenter key holds none of them. A test key that holds Shift
or Ctrl is pressed with the mouse at rest.

The game ends the Lionhead logo on each key that goes down. The three test keys of
the logo do not end it: while one of their keys is down, the mod clears the last key
of the game after each frame (`intro.md`, "The test keys").

The ini keys of the settled choices stay, so a test can set a value by hand:

| Choice | ini key |
| --- | --- |
| Draw probe | `draw_probe` |
| Head tracking and the play mode | `play_mode` (4 God view, default. 1 stereoscopic window. 3 tabletop. 0 gives 4) |
| Plain sea | `water_simple` |
| Stereo | `stereo` |
| Hint copy | `hint_mode` |
| Story camera | `script_camera` |
| Depth | `eye_separation_milli` |
| Interface depth | `ui_shift_px` (default 8), `ui_adaptive` (1), `ui_shift_max_px` (24) |
| Hint size | `hint_scale_pct` |
| Zoom step | `zoom_step` |
| Drag smoothing | `drag_smoothing_ms` |
| Field of view sweep | `fov_sweep_after` |
| Subtitle height | `subtitle_deg` |
| Freeze time on the second pass | `freeze_time` |
| No Hand prepare on the second pass | `skip_hand_prepare` |
| Sun glare | `sun_glare` |
| Advisors in the room | `spirit_anchor` |

Head position is always 6DOF, the world steps run once a frame, the Hand uses its
exact ray and the clouds hold still. These have no key.

## Ini keys of this area

All keys are in `[bwvr]`. Defaults are from `mod_main.cpp`. `launcher.md` lists the
keys that the launcher sets.

| Key | Default | Meaning |
| --- | --- | --- |
| `backend` | dxwrapper when installed | See "Settings" |
| `gpu_bridge` | 1 | The GPU path |
| `frame_send` | 0 | 0: the mod waits for the GPU after each eye pass. 1: one wait a frame. 2: one wait a frame on a second thread, which also gives the frame to the runtime. See "One wait a frame, and a second thread" |
| `draw_merge` | 1 | 1: in an eye pass of the scene, a row of draw calls of the game with one state goes to Direct3D as one draw call. 0: each draw call goes as it comes. See "One draw call for a row of draw calls" |
| `tick_direct` | 1 | 1: the `GetTickCount` of the game goes past the check of busy waiting of dxwrapper. 0: the game calls it through the hooks of dxwrapper. See "The clock of the game and the hooks of dxwrapper" |
| `draw_merge_test`, `draw_merge_test_frame` | 0, 0 | Test keys of the desktop runs. See the same section |
| `depth24` | 1 | A 24 bit Z buffer |
| `near_plane_cm`, `near_plane_cm_16` | 5, 10 | The near plane in the room. 0 keeps the game's |
| `vr`, `stereo` | 1, 1 | VR on, two eye passes |
| `play_mode` | 4 | See "Keys for v1.0" |
| `screen_distance_cm`, `screen_width_cm` | 200, 260 | The quad in the window modes |
| `eye_separation_milli` | 0 | 0 asks the headset |
| `head_units_per_meter_x10` | 75 | World units per meter, in tenths |
| `head_roll`, `water_simple` | 1, 1 | See "Roll and the sea" |
| `head_camera` | 1 | The head on the camera |
| `hand_anchored` | 1 | The Hand keeps its place in the room |
| `game_camera_rebuild` | 1 | See "The game camera's own screen" |
| `pointer_scale`, `pointer_clip` | 1, 1 | See "The window is the input device" |
| `pointer_leave_cursor`, `cursor_trace` | 0, 0 | Test keys of the desktop runs. See "Who moves the cursor of Windows" |
| `exit_shutdown` | 1 | 1: the mod shuts down on the exit path of the game. 0: in `DllMain`. See "The end of the mod" |
| `exit_test_frame` | 0 | A test key of the desktop runs: the frame at which the game ends by itself. See "The test of the end" |
| `registry_test_guard` | 0 | A test key of the desktop runs. 1: from the start of the mod, the mod drops each write of the game to its keys of the registry, and the log names each one. See "A test save" |
| `menu_skip_test_frame` | 0 | A test key of the desktop runs: the frame at which the mod closes the Main Menu that the game opens over a saved game that it loads at its start, as the button "Continue Game" does. 0 is off. The launcher removes the key from `bwvr.ini` on each save. See "A test save" |
| `window_width`, `window_height` | 0 | A forced window size |
| `snap_turn`, `blink`, `instant_jumps`, `camera_glide` | 2, 0, 1, 0 | See "Moving around" |
| `turn_help_degrees`, `god_turn_keeps_focus`, `tilt_help_degrees` | 5, 1, 3 | See `god-view.md`, "The turn lesson" |
| `zoom_step` | 2 | See "Zoom in steps" |
| `move_stick`, `move_stick_speed_cm` | 1, 150 | The walk with the stick of the hand that does not point, in the God view. See `god-view.md`, section 5.7 |
| `button_layout` | 1 | The layout of the buttons of the hand that does not point. See `controllers.md`, 5.1 |
| `drag_smoothing_ms` | 35 | See "Smoother drags far out" |
| `script_camera` and its keys | 2 | See "The story camera" |
| `hint_mode`, `hint_flat`, `hint_scale_pct` | by head, 1, 20 | See "The hint beside the Hand" |
| `ui_scale_pct` | 25 | See "Interface size" |
| `subtitle_lift_pct`, `subtitle_deg` | 0, 35 | See "Subtitles" |
| `cinema_bars` | 0 | 1 shows the story bars |
| `spirit_anchor` | 1 | See "The advisors" |
| `spirit_world_size` | 1 | The size of an advisor at a place of the world. See "The advisors" and `tabletop.md` |
| `cloud_pole_pct`, `sun_glare`, `detail_distance` | 25, 0, 3 | See "Sky, clouds and detail" |
| `freeze_time` | 1 | See "Time, bars and the second eye pass" |
| `frame_time_real` | 1 | 1: after the two eye passes the game has the real time of the frame as its frame time. 0: it has the number from before the passes. See "The delta time after the passes" |
| `frame_time_test_frame`, `frame_time_test_readers`, `frame_time_test_readers_frames` | 0, 0, 600 | Test keys of the desktop runs. See "The delta time after the passes" |
| `eye_clean` | 1 | 1: each eye pass of the scene starts from its own frame before. 0: the two eye passes share the render target. See `stereo-artifacts.md`, "The frame before of each eye" |
| `eye_clean_test`, `eye_clean_test_frame` | 0, 0 | Test keys of the desktop runs. See the same section |
| `bubble_card` | 1 | 1: a speech bubble of the game is one flat card at the depth of its sign. 0: it is drawn as the game and the interface depth make it in each eye pass. See `ui-layer.md`, section 18 |
| `bubble_card_test`, `bubble_card_test_frame` | 0, 0 | Test keys of the desktop runs. See the same section |
| `one_walk`, `one_walk_check` | 1, 0 | See "One walk for both eyes" |
| `skip_desktop_present` | 1 | See "The copy to the monitor" |
| `blt_texture_copy` | 1 | 1: the mod makes a copy with a texture and no stretch itself. 0: each copy goes to dxwrapper. See "The copy to the monitor" |
| `test_frame_rate` | 0 | A test key of the desktop runs: the frame rate of a run with no copy to the monitor. See "The copy to the monitor" |
| `anisotropy`, `force_mips` | 16, 1 | Texture filter. `force_mips` forces the mip filter |
| `skip_snapshot` | 1 | |
| `test_keys` | 0 | The test keys |
| `profile` | 0 | A test key. 1 is the frame cost measure with a timer on each Direct3D draw call, 2 is the measure with no such timer. See "The frame cost measure" |
| `draw_probe`, `capture_every_seconds` | 0 | Reports and eye pictures |
| `eye_test_luma` | 0 | A test key. 1: the log names each run of frames with one dark eye and each run of black frames. See "Testing without a headset" |
| `land_light_eyes` | 1 | 1: the white light of a lightning on the land, and each other light on the land that comes one time in a frame, shows in both eyes. 0: it shows in the left eye only. See "The lights on the land in the second eye pass" |
| `land_light_test` | 0 | A test key. 1: the step of the atmosphere adds the light of a lightning at the place of the Hand, for 150 frames of each 500 frames |
| `fake_head`, `fake_head_yaw`, `fake_head_pitch`, `fake_head_roll`, `fake_head_sweep`, `stereo_headless`, `fov_absolute_deg`, `head_fov_deg` | 0 | See "Testing without a headset" |

## Ready to hand out

### The build of the game is checked, once per start

Every address in `bw_addresses.h` belongs to one file: the `runblack.exe` of the
unofficial v1.42 patch, SHA1 `9c9aea96f046911ab1b7e496fae511ed5ebbfefe`, 8,663,040
bytes. The image base is `0x00400000` in every build of the game, so it is no test.
Other builds fail in different ways:

| Build | What `0x0082F460` holds | Result with the hooks |
| --- | --- | --- |
| v1.42 | `LH3DRender::FinishFrame` | Correct |
| 1.00 (retail) | `.rdata`, not executable | MinHook refuses, VR turns off, no hooks |
| 1.10, 1.20, 1.30 with the copy protection | Encrypted `.text` | A hook writes into code that is decrypted later, and the game crashes |
| 1.10 or 1.30 with the protection removed | Unrelated code | A jump lands in the middle of another function. About 40 more hooks go into unrelated code and globals are written at v1.42 addresses |

`src/common/exe_check.h` hashes the file with a SHA-1 of its own, so no DLL has to
load. It runs from `DllMain`, under the loader lock. It reads the file in 64 KB
pieces and takes about 19.7 ms on the v1.42 file. A file size alone is not a test:
v1.42 is the 1.20 build with the SafeDisc sections removed and the patch bytes
applied.

On any other build, `DllMain` installs nothing (no gpu bridge, no `FinishFrame`
hook), and `ProxyForceSystemDDraw` makes the proxy load system DirectDraw. The game
runs as with no mod, and `bwvr.log` names the build that it found. The launcher runs
the same check.

### Defaults

`ReadSettings` has no early return when `bwvr.ini` is missing. `GetPrivateProfileIntA`
supplies every default, so a fresh install needs no ini, and the installer ships
none. The launcher writes one when the player saves.

### The product name and version

`src/common/version.h` holds the name, the version, the publisher and the support
link. The launcher, the mod, the OpenXR instance and both version resources read it.
The launcher is `Black & White VR Mod.exe`. `ddraw.dll` carries a version resource,
which tells it apart from the dxwrapper stub and from a DirectDraw fix of the player.

### The launcher

It checks all five runtime files, hashes the game, reports a save that Windows
refused, finds the game through `GameDir` when it is not beside it, and keeps the
game's own `ScreenW`, `ScreenH`, `FullScreen` and `ScreenD` in a `[game_before]`
section, so the uninstaller can put them back.

## Open items

- The end of the OpenXR session at the exit of the game (`XrScreen::Stop` on
  the exit path) has run in no headset session. See "The end of the mod".
- A device lost after the bridge has started is not tested.
- The worst frames of 31 to 52 ms have no explanation.
- The clouds and the sun bob a little in the headset.
- The advisors look a little beside the Hand when the head is turned away.
- The detail distance is measured along the view axis, not as a distance.
- A vignette during a land drag, and a single scene walk with a derived second eye
  (about 3 ms in a heavy scene), are open ideas.
- An off centre projection for each eye would draw about a third less.
- The game turn could move off the render thread if its spike is large.
