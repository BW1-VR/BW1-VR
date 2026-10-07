# The graphics backend, and removing the CPU readback

## The problem in one number

The mod renders the scene twice, once per eye, and sends both pictures to the
headset. To send a picture it must first get hold of it. It used to lock the
DirectDraw render target, which drags the whole frame out of graphics memory
into system memory. It then wrote that frame into a Direct3D 11 upload
texture, which pushed it back into graphics memory again.

That is two crossings of the bus per eye per frame, and the cost grows with
every pixel:

| Resolution | Lock, per eye |
| --- | --- |
| 1280x720 | 0.41 ms |
| 1600x900 | 0.58 ms |
| 1920x1080 | 0.83 ms |
| 2560x1440 | 1.34 ms |

The game's own drawing does not grow with resolution at all. It stayed between
4.33 ms and 6.52 ms at every resolution measured, because the engine transforms
vertices on the CPU and never touches a pixel. The variation tracks what is on
screen, not how large the screen is.

So the readback was the only thing between this mod and a headset native
resolution. A Quest 3 eye is 2064x2208. At that size the lock alone
extrapolates to about 1.65 ms per eye, and the upload costs about the same
again. Added to the game's 4.5 ms that fills the whole 11.1 ms budget of a
90 Hz frame and leaves nothing.

## What replaced it

The mod now asks the backend for the frame as a resource the GPU already
holds. The picture never leaves the graphics card:

```
game draws  ->  Direct3D 9 render target
                StretchRect        (graphics memory to graphics memory)
            ->  a shared texture
                CopyResource       (graphics memory to graphics memory)
            ->  the OpenXR swapchain image
```

System DirectDraw cannot do this. It keeps the frame where only the CPU can
reach it. That is not a fault in the implementation. It is what the 2001 API
promised. So the backend had to change.

`src/mod/gpu_bridge.cpp` owns this. It watches for the backend creating its
Direct3D 9 device, makes one shared texture per eye, opens both on the
Direct3D 11 device that the OpenXR session uses, and copies into the
swapchain. Everything in it fails safe. If the backend is not on Direct3D 9,
if the device is not 9Ex, if a shared texture cannot be made or opened, or if
the swapchain format does not match, the mod keeps using the readback.

### The backend can replace its device

dxwrapper makes a new Direct3D 9 device when the window mode or the window
changes. To free the old one, it calls `Release` until the count is zero
(`ReleaseD9Device` in `IDirectDrawX.cpp`). That also removes the bridge's own
references, so a reference does not keep the device alive.

The bridge used to keep the first device only. When the game started full
screen, the backend made a windowed device, destroyed it and made a full
screen one. The bridge then called `CreateTexture` on the freed device, and
the game crashed at `ddraw.dll+0xBF72`. A full screen start is what the game
does when `FullScreen` is missing from the registry.

The bridge now hooks the device's `Release`. When the count reaches zero, it
drops everything it made on that device and calls none of it. The creation
hooks give it the new device, and `GpuBridgeResume` starts the shared path
again on that device. Until then, the frame goes through the readback. The
log shows each device, its size, and whether it is in a window.

On the desktop rig, the self test with the game full screen crashed at that
address before the change. After the change, the shared frames match the
readback byte for byte, full screen and in a window. A device lost after the
bridge has started is not tested yet.

### Keeping the two devices in step

The two devices have separate command queues, and a shared texture carries no
automatic synchronization. Without a fence the Direct3D 11 copy could run
before the Direct3D 9 one finished. The bridge issues an event query after its
copy and waits for it, so the other device cannot read the texture early.

There are two shared textures, one per eye, not one shared texture reused.
The copy of the left eye is still queued when the right eye is captured, and
a single texture would be overwritten underneath it.

A keyed mutex would be the usual way to do this and is not available. Direct3D
9Ex cannot create or open a keyed mutex shared resource, so
`IDXGIKeyedMutex::AcquireSync` cannot be reached from the producer side. Do
not re-explore this.

Note what the wait actually measures. `D3DQUERYTYPE_EVENT` with
`D3DGETDATA_FLUSH` drains the whole Direct3D 9 command buffer to idle, so it
waits for everything the game queued for that eye, not for the copy alone. It
is a GPU drain, and it moves whenever anything else on the GPU moves. It fell
from about 0.9 ms to about 0.34 ms when the copy to the monitor stopped, and
nothing in the bridge changed.

### Measured cost

The blit itself never appears. It is a GPU to GPU copy of a few megabytes on a
card that is otherwise idle, and it measures 0.00 to 0.01 ms.

The flush wait is the real number, and it depends on what else the GPU has to
do first. Be careful which measurement you quote:

| Where | Flush wait, per frame |
| --- | --- |
| Headless self test, no headset, 720p | 0.01 ms |
| Headset, 1080p, monitor still being drawn | 0.74 to 1.14 ms |
| Headset, 1080p, monitor copy skipped | 0.34 ms |

The headless figure is not comparable to the others. With no headset there is
no compositor and no second eye, so the query has almost nothing to wait for.
The middle row is what the wait really costs when the GPU is also copying a
1920 pixel wide picture to a window every frame. Removing that copy cut the
flush wait by two thirds without touching the bridge at all, which is a good
reminder that this wait measures the queue, not the copy.

### Proof that it is correct

The bridge has a self test that needs no headset. It builds its own
Direct3D 11 device, runs the same capture, and copies the result back only so
it can be written to a file.

Both routes were captured on the same frames and compared:

| Frame | Result |
| --- | --- |
| 400 left | identical |
| 400 right | identical |
| 800 left | identical |
| 800 right | identical |

Byte for byte, not merely similar. Turn it on with `gpu_bridge=1` and a
capture frame in `bwvr-eye-frames.txt`. It writes `bwvr-shared-*.bmp` beside
`bwvr-eye-*.bmp`.

## The backends that were tested

Each backend ran the same scripted opening of Land 1 and saved the same frames
to disk, so the pictures could be compared directly.

| Backend | Runs the game | 720p, headless | Frame reachable on the GPU |
| --- | --- | --- | --- |
| System DirectDraw | Yes | 168 fps | No, and it never can be |
| **dxwrapper, Dd7to9** | **Yes** | **209 fps** | **Yes, and it is in use** |
| d7vk 2.2 | Yes | 82 fps | No, it hides its insides |

### dxwrapper

dxwrapper is the backend the mod now uses. It renders Black and White
correctly, and it is faster than system DirectDraw rather than slower.

Getting the mod to chain into it took one piece of understanding. dxwrapper
works out what it should wrap from the name of the stub that loaded it, and it
loads `dxwrapper.dll` from that stub's own folder. Loading `dxwrapper.dll`
directly skips all of that, and the game then crashes about 50 ms later with a
call through a null pointer.

So the stub keeps its name and moves out of the way:

```
Black & White Game/
  ddraw.dll                 the mod, which the game loads
  dxwrapper/
    ddraw.dll               dxwrapper's own stub
    ddraw.ini               the stub's settings
    dxwrapper.dll
    dxwrapper.ini           the real settings
```

`RealDllPath` is set to the system library by hand in both files. Left on
`AUTO` it finds the mod's own `ddraw.dll` and loads it again.

`D3d9to9Ex = 1` is what makes the shared texture possible. A plain Direct3D 9
device cannot share one.

### d7vk

d7vk also renders the game correctly, and the pictures look right: terrain,
water, alpha tested fences, lighting, subtitles and the interface widgets.

It is not used, for two reasons.

First, it turns on vertical sync by default and pinned the game to exactly
60.0 fps. `d3d9.presentInterval = 0` in `dxvk.conf` removes the limit and the
game reaches 82 fps, which is still well behind dxwrapper.

Second, and decisive, d7vk hides its insides. It is a proxy over DXVK's
Direct3D 9 backend, and DXVK publishes Vulkan interop interfaces on its
objects. `ID3D9VkInteropDevice` gives the VkDevice and the queue.
`ID3D9VkInteropTexture` gives the VkImage behind a texture. The mod asked the
Direct3D 7 device and its render target for all nine candidate interfaces and
every one was refused. Reaching the frame under d7vk means forking it.

d7vk stays interesting for later. OpenXR binds a Vulkan image with no sharing
step at all, so a fork is the better end state if dxwrapper ever falls short.
d7vk also offers `ddraw.emulateFSAA = Forced`, which is forced anti-aliasing,
and that is the reported jagged edge problem answered by one config line.

## What the backend change actually bought

At 1920x1080 in a headset, per frame:

| | Before | After |
| --- | --- | --- |
| Whole frame | 19.10 ms | 11.18 ms |
| Copy to the monitor | 7.90 ms | 0.02 ms |
| Outside the scene walk | 8.33 ms | 0.37 ms |
| Frame to the headset | 0.95 ms | 0.34 ms |
| Idle, waiting for the headset | 4.44 ms | 6.03 ms |

That is 89.4 fps against a 90 Hz headset, holding. Real work is about 5.2 ms
of the 11.11 ms a 90 Hz frame allows.

Two of those rows came from removing the readback. The large one came from
something else entirely, described next.

## The copy to the monitor

The game runs windowed, so DirectDraw presents by copying the back buffer onto
the primary surface. At 1920x1080 that copy cost 7.90 ms every frame, steady
across every measurement window whatever the scene was doing. With a headset
on, nobody sees the result.

`src/mod/present_watch.cpp` skips it while the mod is sending frames to a
headset. The copy has no result other than what appears on the monitor, and
nothing reads the primary surface back, so skipping it changes nothing else.
With VR off the monitor is the only output there is, so the copy stays. The
setting is `skip_desktop_present`, and it defaults to on.

The monitor stops updating while this is active. That is the intended
behavior, not a hang.

### Full screen is not an alternative

Running full screen would present by flipping buffers rather than copying
them, which would be cheap. It also crashes the game. The Windows event log
put one fault at offset `0xdededede`, which is a freed pointer, and an earlier
one inside `VirtualDesktop.LibOVRRT32_1.dll`. Exclusive full screen and a VR
compositor both want the display. Do not try this again.

## Two open findings

### The game locks the depth buffer every frame

dxwrapper logs `locking depth stencil surface is not implemented` several
times a frame, and returns an error each time. The picture is still correct,
so nothing visible depends on it.

Two things follow, and both need checking.

The first is a suspicion about speed. Under system DirectDraw that lock
succeeds, and a lock of a depth buffer is a readback like any other. It would
be counted as game code in the profile, because the mod does not time it. That
may be part of why dxwrapper is faster.

The second is a risk. Something in the game asks for that data. If it is
picking, which is how the Hand finds what it is over, then it matters a great
deal to this project and it has to work.

### Where the time goes now

With the monitor copy gone, the frame at 1080p is:

| Part | Cost |
| --- | --- |
| Scene walk, both eyes | 4.20 to 5.18 ms |
| Frame to the headset | 0.34 ms |
| xrEndFrame | 0.17 to 0.25 ms |
| Everything outside the scene walk | 0.37 ms |
| Idle, waiting for the headset | 5.04 to 6.03 ms |

The scene walk is now nearly all of it, and it does not scale with resolution.
So the next saving has to come from walking the scene more cheaply, not from
anything to do with pixels.

One correction worth keeping. `LH3DRender::FinishFrame` runs INSIDE
`GGame::Process3dEngine`, not after it. An attempt to split the frame using
FinishFrame as a midpoint reported zeros for every span, which is what led to
timing the present directly instead.

### The backend is faster per vertex, by a lot

At 720p, headless, both passes:

| Backend | Scene | Vertices per frame |
| --- | --- | --- |
| System DirectDraw | 4.97 ms | 9,505 |
| dxwrapper | 2.36 ms | 43,781 |

These are different moments in a scripted flight, so the comparison is not
controlled and the ratio should not be quoted. The direction is clear enough
to be worth a proper measurement at a fixed camera.

## The frame, as the game builds it

Recovered from the decompilation and the binary. Treat the addresses as a
starting point and confirm one before relying on it: they came from a search,
not from a breakpoint.

`GGame::Loop` at `0x0054CF20` runs one iteration per frame. Its only caller is
`GGame::StartGame` at `0x0054C190`. In call order:

| Step | Address |
| --- | --- |
| `ControlMap::ProcessActionsPerformed` | `0x00470AB0` |
| `GGame::ProcessBufferedKeys` | `0x0054C420` |
| `GGame::ProcessNetworkPackets` | `0x0054CC30` |
| `GGame::ProcessGraphicsEngine` | `0x0054D850` |
| `GGame::ProcessFrameInputs` | `0x0054C340` |
| `GGame::FlipScreen` | `0x0054D800` |

Inside `ProcessGraphicsEngine`: `LHMouse::UpdateDeltaPos` `0x007E4810`,
`GCamera::Update` `0x00441F80`, `GInterface::PreDrawProcess` `0x005CE9E0`,
`GGame::Process3dEngine` `0x0054DA80` (the hooked call),
`GInterface::PostDrawProcess` `0x005CEAB0`, `HelpSystem::PostDrawProcess`
`0x005C5EB0`.

### The present chain

`GGame::FlipScreen` `0x0054D800` calls `LHScreen::Flip` `0x007DE090`, which
calls `LHScreen::LHFlip` `0x007DE580`. That function branches on windowed
versus full screen:

- windowed, at `0x007DE58D`: `IDirectDrawSurface7::Blt` with `DDBLT_WAIT`,
  copying the whole back surface to the primary surface
- full screen, at `0x007DE5D0`: `IDirectDrawSurface7::Flip`, which copies
  nothing

This game is windowed, so it takes the copying branch. That is the 7.90 ms.
Across roughly 13,000 frames the mod counted zero `Flip` calls, which confirms
it: there is no flip path to optimize and `DDFLIP_NOVSYNC` is unreachable.

### The simulation runs at 10 Hz, not per frame

`GGame::ProcessNetworkPackets` `0x0054CC30` asks
`GGame::LocalTimerSaysDoATurn` `0x0054C4A0` whether a game turn is due, and a
single player game allows at most one turn per frame. The turn is
`GGame::ProcessOneGameTurn` `0x0054D620`, which runs `StartTurn` `0x0054E4F0`,
`ProcessTurn` `0x0054E5C0` and `EndTurn` `0x0054E960`. The script machine is
`GScript::Process` `0x006EB6B0`, called only from `ProcessTurn`. Audio is
`GAudio::ProcessAudioGameTurn` `0x00427080`, called only from `EndTurn`.

This matters for comfort more than for the average. At 90 fps about one frame
in nine carries a whole game turn and the other eight carry none, so the cost
arrives as a spike rather than as a flat charge. A spike is a dropped frame in
a headset. If it turns out to be large, the answer is to move the turn off the
render thread, not to make it faster. Measure the per-frame maximum, not the
mean.

## The mip chains cost more than the shimmer did

For weeks the camera drag was reported as heavy: it resisted, then overshot
when released. The feel was blamed, and several real bugs were found and fixed
while chasing it. The last one was not a feel problem at all. It was the frame
rate.

The log during a heavy episode, at 2560x1440:

```
79.2 fps   frame 12.63 ms = wait 3.57 + scene x2 6.93 + grab x2 1.58
89.8 fps   frame 11.13 ms = wait 3.43 + scene x2 5.73 + grab x2 1.50
82.0 fps   frame 12.20 ms = wait 2.29 + scene x2 7.57 + grab x2 1.73
70.8 fps   frame 14.12 ms = wait 2.20 + scene x2 9.32 + grab x2 1.74
82.3 fps   frame 12.16 ms = wait 0.73 + scene x2 9.89 + grab x2 1.15
```

The headset needs 11.1 ms. A frame that misses gets reprojected, and a
reprojected frame carries the old camera, so the world lags behind the drag.
That is what felt heavy.

The cost was `DdrawForceMipMapAutoGen = 1` in `dxwrapper.ini`. It rebuilds a
full mip chain every time a texture is updated, and the game updates its
landscape texture as the camera moves. So the cost arrived exactly when the
player dragged the world, and it was absent when the camera was still. That is
why the fault looked intermittent, and why no setting in the mod changed it.

With the setting off, in the same session:

```
89.9 fps   frame 11.12 ms = wait 3.32 + scene x2 6.72 + grab x2 0.77
89.7 fps   frame 11.15 ms = wait 2.86 + scene x2 7.09 + grab x2 0.88
```

The GPU drain fell with it, from 1.4 ms per frame to 0.7 ms. That number is
the event query the shared surface needs, so it measures how far behind the
GPU is. It is a good second opinion on any GPU cost.

The price is the shimmer on distant texture, which is what the mip chains
were for. `ForceMipMapUsage` is the next thing to test, because it builds a
chain once instead of on every update.

Two lessons worth keeping. A complaint about feel can be a complaint about
frame time, so measure the frame before debating the feel. And a cost that
rises with camera movement looks exactly like a camera bug.

## The rest of the frame, call by call

`frame_timers.cpp` times every call in the frame loop that the stereo report
did not cover. It prints a line only when a call went over 3 ms, because a
list of zeros hides the one number that matters.

The loop body, from `GGame::Loop` in the decompilation:

| Call | Address |
| --- | --- |
| `ControlMap::ProcessActionsPerformed` | `0x00470AB0` |
| `GGame::ProcessBufferedKeys` | `0x0054C420` |
| `GGame::ProcessNetworkPackets` | `0x0054CC30` |
| `GGame::fn_005525E0`, the network flush | `0x005525E0` |
| `GGame::ProcessGraphicsEngine` | `0x0054D850` |
| `GGame::ProcessFrameInputs` | `0x0054C340` |
| `GGame::FlipScreen` | `0x0054D800` |

`ProcessGraphicsEngine` takes two arguments and the rest take none. That
matters, because the callee cleans the stack: a stub with the wrong argument
count corrupts it.

### A keystroke costs 24 ms, and we press the keys ourselves

Two runs of the timers, each over about a minute of play:

```
GGame::ProcessGraphicsEngine   mean 10.84 ms, worst 15.36 ms over 450 calls
GGame::ProcessBufferedKeys     mean  0.05 ms, worst 24.15 ms over 488 calls

GGame::ProcessGraphicsEngine   mean 10.81 ms, worst 18.21 ms over 450 calls
GGame::ProcessBufferedKeys     mean  0.05 ms, worst 24.05 ms over 460 calls
```

Nothing else went over 3 ms in either run. `ProcessGraphicsEngine` holds the
wait and both scene walks, so its mean is the frame.

`ProcessBufferedKeys` averages 0.05 ms and hit 24 ms once per run, to within
0.1 ms both times. A number that repeatable is a fixed piece of work, not
contention.

Read the placement carefully before blaming the lock. `GGame::Loop` takes
`LHKeyboard::CriticalSection` BEFORE the call and releases it after, so a wait
on the lock happens outside the hook and is not in this number. The 24 ms is
spent inside the function, while it holds the lock.

The likely cause is the mod itself. Both stalls landed in a window where the
function keys were being pressed to switch modes, and the game binds those
keys too, so it reacts to every hotkey the mod defines. That makes it a self
inflicted cost during testing rather than the stutter to chase.

Note the call count: 460 to 488 calls against 450 frames, so it does not run
exactly once a frame.

### What still is not measured

The worst frames run 31 to 52 ms, in every configuration tested. None of the
timed calls explains them, and neither does the game turn. What remains is
short: `ScriptedScreenShot::Process`, `GPlayer::SavePlayerAlignment`, which
takes an argument, `CreatureRoom::ProcessScreenShot`, and the time between the
end of one frame loop and the start of the next, where the process can simply
be preempted.

Against that, the runtime counter stays at 28 skipped for the whole of a
12,154 frame run, and the mean frame time sits on 11.1 ms. So whatever this
is, it is not costing presented frames.

## ForceMipMapUsage costs nothing, unlike the autogen

`DdrawForceMipMapAutoGen` rebuilds a chain on every texture update.
`ForceMipMapUsage` in the `[d3d9]` section was tested as the cheaper way to
get the same picture. Gameplay windows, same scenes, same session:

| | fps | scene walk | GPU drain |
| --- | --- | --- | --- |
| no mip chains | 88.7 to 90.0 | 6.21 to 7.60 ms | 1.02 to 1.40 ms |
| `ForceMipMapUsage = 1` | 88.9 to 90.0 | 4.49 to 7.50 ms | 0.47 to 1.41 ms |
| `DdrawForceMipMapAutoGen = 1` | 70.8 to 82.3 | 9.32 to 9.89 ms | 1.13 to 1.71 ms |

So the first two are the same within noise and the third is the trap.

### Mip levels cannot be baked into the game's own files

The engine has no concept of a mip level:

- 228,748 `MINFILTER` and `MAGFILTER` calls in a session, and zero `MIPFILTER`.
- The strings `MIPMAP`, `MipMap`, `mipmap` and `MIPFILTER` do not appear
  anywhere in `runblack.exe`.
- The decompilation has no mip concept either.

The game creates single level surfaces and never asks for mip filtering, so
extra levels in a `.raw` would never be allocated and never uploaded. Mips
work at all only because the mod forces `D3DTSS_MIPFILTER` on beside every
`MINFILTER` call the game makes, and the backend supplies the chain. Building
the chain is therefore the backend's job, and the only question is how often
it rebuilds it.

## What the scene walk is made of

`profile=1` splits the walk. Measured in a heavy scene, a glowing portal with
particles, alpha tested foliage and trees moving under physics, and again in
an ordinary one:

| | draw calls | vertices | scene x2 | driver | game code |
| --- | --- | --- | --- | --- | --- |
| portal | 1839 | 126,565 | 9.79 ms | 3.76 ms | 6.03 ms |
| portal | 1764 | 126,717 | 9.92 ms | 3.66 ms | 6.26 ms |
| ordinary | 1079 | 85,379 | 6.15 ms | 2.30 ms | 3.85 ms |
| ordinary | 683 | 71,992 | 4.31 ms | 1.56 ms | 2.75 ms |

Both figures are for both passes together, so the portal is about 900 draw
calls per eye.

Two things hold across every window. The split is stable at about 37 percent
driver and 63 percent game code. And the cost tracks the draw call count far
more closely than the vertex count: 5.3 to 6.3 microseconds per call
throughout, while the vertex count barely moves between the two portal
windows and the two ordinary ones. This is a draw call bound workload, which
is what a 2001 engine with many small batches should be.

That sets the size of the prize for walking the scene once and deriving the
second eye from the first. It would save the game code half of the second
pass, about 3 ms of the 9.8, and nothing of the driver half, because each
batch still has to be submitted twice. Worth knowing before building it.

### The streaming layer can decide the outcome on its own

The same portal, in two sessions, with Virtual Desktop degraded in the first
and restarted in the second:

```
degraded    scene x2 10.05 ms  ->  60.9 fps
healthy     scene x2  9.79 ms  ->  86.9 fps
```

The game did the same work both times. A 26 fps difference for a 0.3 ms
difference in work is not the application.

The reason it showed up at the portal first is that the portal leaves almost
no room: those windows report a wait of 0.02 to 0.05 ms, against 3 to 6 ms in
an ordinary scene. At 9.8 ms of work against an 11.1 ms budget, anything
outside the game that wobbles will push it over there before anywhere else.

So a frame rate complaint needs the streaming session ruled out before the
game is blamed, the same way a feel complaint needs the frame time ruled out
before the feel is blamed.

## A wide view is nearly free, which clears Phase 3

Standing inside the world means submitting a projection layer instead of the
flat quad the mod submits today, and a projection layer is a promise: this
image was rendered from this pose, with this frustum. So the game has to
render the headset's view, about 100 degrees per eye, against the 70 degrees
it renders now.

That looked expensive. The workload is draw call bound, and the heaviest scene
already spends 9.79 ms of an 11.1 ms frame. A wider view means more of the
island in front of the camera.

`LH3DTech::ChangeFov` at `0x008195B0` is the lever, because the struct it
writes holds the culling cone as well as the projection: `InvHalfTanFovX` and
`InvHalfTanFovY` beside `CosHalfFov` and `CosHalfFovSqr`. Widening the view
widens the culling with it, so geometry is not thrown away before it can be
drawn. The project plan listed that pop-in as a certainty for any wrapper
based approach. It is not one here.

Measured: the game asks for **1.2217 radians, exactly 70.0 degrees**, once per
frame, 450 calls in 450 frames.

### The sweep

The first attempt was made by hand and failed. At one fixed scale the draw
calls ranged from 336 to 585, because the camera moved between windows and the
scene drowned the measurement. So the mod sweeps itself, one step per report
window, which is the same 450 frames the profile counts over. Each scale then
gets exactly one clean window, and the last step returns to the start so the
run can be checked rather than trusted.

| degrees | draw calls | vertices | scene x2 |
| --- | --- | --- | --- |
| 70 (start) | 1731 | 133,131 | 8.20 ms |
| 77 | 1701 | 132,993 | 8.11 ms |
| 84 | 1717 | 141,759 | 8.30 ms |
| 91 | 1747 | 146,126 | 8.46 ms |
| 98 | 1780 | 148,847 | 8.54 ms |
| 105 | 2025 | 156,448 | 9.55 ms |
| 70 (return) | 1653 to 1695 | 129,214 | 7.82 to 8.11 ms |

The return lands within 4 percent of the start, so the scene held still.

**70 to 98 degrees costs 0.34 ms and 49 draw calls, about 4 percent.** The
reason is the shape of the game: it looks down at an island, so a wider view
mostly adds sky, sea and empty distance. The plan guessed this when it said
the periphery is mostly empty for a top down diorama.

There is a step at 105 degrees, 245 more draw calls, worth remembering if the
view is ever pushed that far. A headset needs about 100, so the step is
outside what Phase 3 asks for.

Separately, the game was driven to 161 degrees by hand with no artifacts and
no instability, which is far past anything needed. The projection and the
culling really do move together.

### The trap: find the ret before hooking

`ChangeFov` is `__stdcall` and ends with `C2 04 00`. The decompilation header
calls it a static, which says nothing about who cleans the stack. Hooking it
with a `__cdecl` replacement left every caller 4 bytes out, and the game died
after the loading screen with an access violation at an address in no loaded
module.

So: before hooking anything that takes a stack argument, find its ret. `C3` is
caller cleans, `C2 n` is callee cleans. Member functions with no arguments are
safe either way, which is why every other hook in this mod survived without
the check.

## Phase 3: the world around you

The mod submitted a quad layer per eye: a flat screen at a fixed distance. The
world now goes in an `XrCompositionLayerProjection`, which is a promise, not a
picture. It says this image was rendered from this pose with this frustum, and
the runtime places every pixel accordingly. Three things had to be true
together, and none of them works alone.

| | |
| --- | --- |
| The layer | poses and frustums from `xrLocateViews`, in the same space the layer names |
| The view | the game renders the headset's angle, not its own 70 degrees |
| The camera | the game camera carries the player's head |

### What the headset asks for

```
eye 0   left -54.0  right  40.0   up 44.0  down -55.0 deg
eye 1   left -40.0  right  54.0   up 44.0  down -55.0 deg
separation 66.8 mm
```

Two things follow. The frustum is asymmetric, 54 degrees outward against 40
inward. And it is 99 degrees tall, against the 43 the game renders at 16:9.

The game renders a symmetric frustum and derives its vertical angle from the
render aspect, so covering that needs a roughly square render and a symmetric
superset of about 110 degrees. `ScreenW` and `ScreenH` are set equal for this.
A square render also costs a little over half the pixels of 2560x1440, and the
workload is draw call bound, so pixels are not what is paid here.

Rendering a symmetric superset throws away about a third of what it draws.
`InfoTransform` holds `HalfRes`, which is the projection centre and the reason
`screen_x = (1 + cx/cz) * half_res_x`. Moving that centre gives an off centre
projection, so an asymmetric frustum per eye looks reachable. Not attempted
yet, and not needed while the cost is this low.

### The eyes were being located in the wrong space

Phase 2 located the views in `XR_REFERENCE_SPACE_TYPE_VIEW`, the head's own
frame, because all it needed was the distance between the eyes. That was the
right call then. In that frame the orientation is identity and the positions
never move, whatever the player does, so head tracking read as no head
tracking at all.

The pose log said so plainly and was misread once as "it just recentred":

```
head : pose 1.000 0.000 -0.000 | 0.000 1.000 -0.000 | -0.000 -0.000 1.000  move 0.00 0.00 -0.00
head : pose 1.000 0.000 -0.000 | 0.000 1.000 -0.000 | -0.000 -0.000 1.000  move 0.50 0.00 -0.00
```

An identity matrix twice, and two positions exactly one eye separation apart.
Locating in `LOCAL` instead fixed it, and `LOCAL` is also the space the
projection layer names, so the two now agree by construction.

### Moving the camera, not just sliding it

The camera matrix is 4 rows of 3 floats in the row vector convention, and the
columns are the camera axes. Applying a head pose (H, d) in camera space:

    p_newcam = (p_camera - d) * H = p_world * (R * H) + (t - d) * H

so `R' = R * H` and `t' = (t - d) * H`. The old sideways only move is the same
thing with H as the identity, which is how it was checked.

The scaled matrix is rebuilt from the unscaled one column by column rather
than transformed directly, because its columns carry the field of view terms
and a rotation would mix them.

Handedness is done as a change of basis, `B * M * B` with `B = diag(1, 1, -1)`,
rather than by guessing signs on the quaternion. That is one attempt instead of
eight.

### What 0x00EA9E80 is not

The address map called it `kCameraToWorld`. Dumped from the running game it
holds:

```
869515.5  618082.06  -443068.28 | 0  -500000  500000 | -500000  0  0 | t 0 0 0
```

That is not a camera. `kCameraPos` at `0x00EA1DB8` is real, and reads exactly
the translation of the rigid inverse of the unscaled matrix, which also
confirms the matrix maths.

## The doubled tooltip, finally explained

Head tracking answered a question that months of pixel measuring did not. With
the head on the camera, the two copies separate for a reason that can be
described in one sentence:

**The game draws each hint twice: once projected at the Hand, and once pinned
to the mouse cursor's position on screen.**

On a monitor those are the same point, so it reads as one hint. Stereo pulls
them apart by the Hand's disparity. Head tracking pulls them much further
apart, because a screen position is fixed relative to the camera, so the pinned
copy rides the player's look direction while the Hand stays where it was put.

Which copy to keep therefore depends on the head:

| | Keep | Why |
| --- | --- | --- |
| No head tracking | the pinned copy | same place in both eyes, reads flat like a label |
| Head tracking | the copy at the Hand | the pinned one follows the head, which is worse than any depth problem |

So the default now follows `head_tracking`. `hint_mode` still overrides it, and
shift with F7 cycled all three until v1.0, see "Keys for v1.0".

The hint is also drawn at half size by default. The game sizes its interface in
pixels, which is a small part of a 60 degree view on a monitor and a large part
of a 110 degree one in a headset, so a hint kept its size while the Hand it
belongs to shrank into the distance. `hint_scale_pct` sets it, ctrl with F8 and
F9 changed it until v1.0, and it scales about the hint's own anchor so it shrinks in
place.

### Still open after the first night in the world

| What | What is known |
| --- | --- |
| The sea swings across the sky when the head turns | Guarding the four world steppers to one pass did not fix it, so something reads a camera the mod does not rewrite. `0x00EA9E80` is ruled out. The draw caller has not been identified yet |
| The interface is stuck to the head | Expected. The world compensates because the camera turns; screen space drawing does not. The fix is a separate layer for the interface |
| The Hand follows the head | The Hand sits where the cursor ray hits the world, and that ray is cast through the camera. `CHand::PrepareForDrawing` runs inside the scene walk, so the anchor camera can be restored just for that call |
| A wide view crashes on entering the world | At 110 degrees, an access violation at `0x00821AEB`, in the unnamed region after `LH3DCameraChecker::Create`. At 70 degrees it does not happen. Pinned at 70 until it is understood |

## A headset is not needed to test most of this

Most of what goes wrong with head tracking can be seen in a still picture: the
sea in the wrong place, the interface riding the view, geometry culled against
the wrong frustum. None of it needs a person wearing a headset, and asking one
to put it on for every experiment is slow.

So the mod can invent a head. Given a yaw and a pitch it builds the pose the
runtime would have reported, and the rest of the path is identical: the same
quaternion, the same change of basis, the same camera transform.

```ini
vr=0
stereo=1
stereo_headless=1     ; walk the scene twice with no runtime
head_tracking=1
fake_head=1
fake_head_yaw=40
fov_absolute_deg=110  ; the headset's angle, without a headset
capture_every_seconds=6
```

Both eye images are written to disk as BMP, which converts to PNG and can be
looked at. The game can also be driven: the options dialog takes clicks through
`SetCursorPos` and `mouse_event` at fractions of the client rect, which is how
the crash on entering the world was reproduced and then proved fixed.

Three traps, all of which cost a run each:

**An invented head must be absolute, not recentered.** Recentering takes the
first pose it sees as straight ahead, so a fixed fake yaw cancels itself out
exactly and every picture comes back looking like yaw zero.

**Stereo can be checked numerically.** Distant things should be identical
between the eyes and near things should not. Measured on a good frame: sky 0.01
levels of difference, horizon 2.96, sea 2.08. A single number that says the
stereo is real.

**The scene must hold still.** The options dialog before the tutorial is a
fixed camera and makes an excellent test scene.

## The sea, and what it really is

The sea is `LH3DWater`, an unnamed module at 0x00879000 to 0x0087A400, found by
its own Report3D strings ("LH3DWater::Open already open" at 0x00C39924).

It is **not world geometry**. `LH3DWater::Draw` at 0x00879930 draws a
screen-space band of horizontal scanline strips with pre-transformed XYZRHW
vertices, depth test forced to ALWAYS and depth writes off, so it is a
background fill that nothing occludes. Every strip is full width with x fixed
at 0 and res_x, and rhw is 1.0 on every vertex. The texture coordinates come
from unprojecting each screen row back onto the plane y=0, twice per row, at
screen x = 0 and x = res_x.

| Address | What |
| --- | --- |
| `0x00879930` | `LH3DWater::Draw`, `__fastcall`, material in ecx |
| `0x0087A090` | the simple fallback: one world space quad of +/-70000 at y=0 |
| `0x008792E0` | `LH3DWater::Open`, fills the strip buffer at 0x00FA9358 |
| `0x00C38228` | the water tiling value. Non-zero picks the scanline path |
| `0x005E4E8C` | where `GLandscape::Draw` calls the water |

### Why it broke, and why the fix works

Four things in that routine assume the camera has **no roll**: the strips are
horizontal, rhw is constant so the texture interpolates affinely along a row,
one depth per row drives the fade, and nothing rejects a negative ray-plane
intersection, so a row whose ray points upward samples the sea plane behind the
camera and the texture mirrors.

But roll was not the fault we hit. The fault was that the mod rewrote the
camera matrices INSIDE the scene walk, and `LH3DTech::UpdateCamera` runs before
the scene walk, from `camera->Update()` in `ProcessGraphicsEngine`. Everything
the engine derives once per frame, including this band, was therefore built
from a camera still looking the old way.

The fix is to apply the head at the source: hook `LH3DTech::UpdateCamera` and
move the position and focus it is given. Then nothing downstream needs
correcting. A camera described by a position and a focus has nowhere to put
roll, which is why this fix also cannot break the water.

**So roll is not free.** Adding head roll later means either switching the
water to the `0x0087A090` fallback or accepting a mirrored sea when the head
tilts. Note that setting `WaterTiling` in the registry does NOT do it: two
captures either side of that change differ by 0.007 levels, so the value must
be written to `0x00C38228` directly.

### Not the fault, but worth knowing

There is a cached camera forward at `0x00EA1DD4`, written by
`LH3DTech::UpdateWorldToCamera` at 0x00819773, and the water does read it, at
0x008799F6 and 0x008799FF, to push the sampled position along the view as a
fake swell. The amplitude is at most 1.0 world unit against a texture tile of
about 2000, so it is sub-pixel. `GScript::GetFacingCameraPosition` reads the
same global, which confirms what it is.

`LH3DSky::Draw` at 0x0086B010 is ordinary world geometry loaded from
`data\weathersystem\sky.l3d`, so the sky needs no work at all.

## 0x00EA9E80 is the light, in camera space

The address map called it `kCameraToWorld`. It is written by a light setter at
0x0081E1F0, which stores the light position in world space at 0x00EA9E90 and
the same point transformed by the SCALED camera matrix at 0x00EA9E80. The dump
that first flagged it as wrong read 869515, 618082, -443068 beside three
500000s, which are the light's world coordinates.

## Where the Hand comes from

With the head driving the camera, the Hand followed the head as well as the
mouse, because the Hand sits where the cursor's ray meets the world and the ray
is cast through the camera.

The chain, read out of the binary:

```
GGame::Process3dEngine 0x0054DA80
  0x0054DEAA  call [vtable+0x18]         CHand::PrepareForDrawing 0x0046C550
    0x0046C6C5  hand->screenXY <- the mouse globals 0x00E852C0 / 0x00E852C4
    0x0046C921  call [state vtable+0x0C] HandState::Update
      0x005B718F  ScreenPointToWorldAtDistance 0x0074CAF0
        0x0081B370  UnprojectScreenPoint, the reverse of ProjectPoint
```

`UnprojectScreenPoint` at `0x0081B370` reads exactly six things and nothing
else: the half resolution at 0x00E839F0, the field of view scalars at
0x00C3812C and 0x00C38130, the near plane at 0x00E839E0, the 3x3 rotation of
the UNSCALED matrix at 0x00EA1D28 applied transposed, and the camera position
at 0x00EA1DB8.

So making the Hand ignore the head needs 36 bytes at 0x00EA1D28 and 12 bytes at
0x00EA1DB8 restored around the call, and nothing else. The mod already hooks
`CHand::PrepareForDrawing` for the pass guard, so the bracket costs no new
hook, and the anchor rotation needs no second call into the engine: the columns
of that matrix are the camera axes, which the camera hook already computed from
its own arguments.

## The calling convention rule, corrected twice

Two crashes came from getting this wrong, in opposite directions.

`LH3DTech::ChangeFov` is `__stdcall` and ends `C2 04 00`. A `__cdecl`
replacement returned without popping the argument, so every caller was left
four bytes out.

`LH3DTech::UpdateCamera` is `__fastcall` and ends `C3`. Reading that as
`__cdecl` meant the hook took its arguments off the stack and handed the
original two garbage pointers.

The rule that survives both:

1. **Read the ret.** `C3` means the caller cleans. `C2 n` means the callee
   cleans n bytes.
2. **Read the first few instructions.** `mov esi, edx` and `mov edi, ecx` at
   the top mean the arguments arrive in registers, and then `C3` says nothing
   about `__cdecl`.

Both ends, every time. A member function with no arguments is safe either way,
which is why most of the mod's hooks survived without the check.

## Verifying the Hand fix, and three tests that measured nothing

The Hand anchor is a numerical claim: with the mouse still, the Hand's world
position must not change when the head turns. `0x00C5E680` holds that position,
so it can be read rather than looked at.

Getting a test that could actually fail took three attempts, and each failure
was worth more than the result.

**The creature select screen does not place the Hand from the cursor.** The
position was constant there whether the anchor was on or off, which looks like
success and is not.

**A cutscene has not settled.** The first gameplay sample pair differed by 28
units and it was the game's own camera still moving, not the head. Two
consecutive samples at the SAME head angle is what tells those apart.

**A parked mouse never recomputes.** This was the important one. With the mouse
perfectly still the Hand keeps its last position, so both arms of the
experiment gave identical numbers and the test could not fail. Jiggling the
mouse by one or two pixels between samples makes the Hand recompute while
staying at essentially the same screen position.

With that, the test separates cleanly:

| | yaw 35 | yaw 0 |
| --- | --- | --- |
| anchor off | 2229.6x, 17.14, 3150.02 | 2226.6x, 17.14, 3152.0x |
| anchor on | 2229.63, 17.14, 3150.02 | 2229.63, 17.14, 3150.02 |

With the anchor off the Hand alternates in lockstep with the head. With it on
the only variation left is the mouse jiggle itself, at two hundredths of a
unit. `hand_anchored=0` keeps the control available.

The game can be driven to this state without a headset: launch with no
arguments, click Continue Game at (0.508, 0.406) of the client rect, send
Escape five times, then park and jiggle the mouse.

## Head roll costs the pretty sea, and that is one choice

A camera described by a position and a focus point has no up vector, so the
engine derives up from world up and the result never rolls. Putting the head
on that camera therefore carries yaw, pitch and leaning, and leaves roll over.

The leftover is measured in the camera hook by comparing the head's own up,
carried into world space, against the up the engine is about to derive:

    roll = atan2(head_up . roll_free_right, head_up . roll_free_up)

Checked against an invented head given exactly 20 degrees: the hook reports
"roll left over 20.0 degrees" with the look direction unchanged. It is exact.

That roll then goes into the per eye matrix as a rotation about the camera's
forward axis, which is the one place roll can go.

### The head turns about world vertical, not about the camera's up

The first version applied the head rotation in the camera's own frame. This
game always looks DOWN at the island, so the camera's up axis is tilted, and
yawing about a tilted axis induces roll. Measured with an invented head given
a 40 degree turn and a 15 degree tilt:

```
in the camera's frame   looking -0.880 -0.170 0.444   roll left over 23.3 deg
in a levelled frame     looking -0.891 -0.215 0.399   roll left over 15.4 deg
the camera was          looking -0.373 -0.223 0.901
```

Two faults in one number. The roll should have been the 15 the head was given,
and the elevation should not have moved at all: the first version raised the
view by three degrees for a horizontal head turn.

The runtime reports the head in a gravity aligned space, so the rotation
belongs in a LEVELLED frame: world up, and the camera's heading flattened onto
the horizontal. The camera's own pitch is kept by applying the head as a
rotation OF the camera's forward rather than as a direction to look up in a
basis:

    L        columns are right, world up, levelled forward
    f_new    = L * H * transpose(L) * f_camera

At H = identity that returns the camera's own forward exactly, and a head turn
rotates it about true vertical.

### And it breaks the sea, which was predicted and then photographed

The scanline sea cannot tilt. At 20 degrees of roll, with the scanline path:

- the land rolled correctly, because it is ordinary geometry
- the sea kept a **horizontal** horizon, because its strips are full width and
  horizontal by construction
- the frame filled with diagonal streaks and a hard seam across the middle

With the plain world space sea at 0x0087A090, the same 20 degrees gives a
tilted horizon that matches the land, no streaks and no seam. The cost is
visible: that sea is a smooth gradient without the scanline sea's wave detail.

So roll and the sea are **one choice, not two**, and the mod treats them that
way. `HeadCameraRoll` returns zero unless the plain sea is selected, so nobody
can reach the state where the land rolls and the horizon does not, which looks
far worse than a level head.

| `water_simple` | The sea | Head roll |
| --- | --- | --- |
| 0, the default | scanline, wave detail | none, the head stays level |
| 1 | plain world space | works |

Shift with ctrl and F6 switched both at once until v1.0.

Note that the registry `WaterTiling` does NOT reach this. Two captures either
side of changing it differed by 0.007 levels out of 255. The value the renderer
reads is the float at `0x00C38228`, and the mod writes it directly, once a
frame, because a level load fills it again.

## The desktop window stops deciding anything

Two problems reported from the headset, with one root and one measurement.

```
pointer : game 576 795   windows client 576 795 of 1280 x 1280
```

The game's pointer IS the Windows cursor in client pixels, one to one and
unscaled. So the window is not merely a preview: it is the input device.

**A render bigger than the monitor loses part of the game.** The window is
created the size of the render, Windows clamps it to the screen, and the rest
of the game can never be pointed at. At 2048x2048 on a 1440 tall desktop the
bottom of the interface was simply unreachable.

**The cursor can leave the window.** A click outside brings another program to
the front and the game is lost behind it.

Both were fixed by taking the mapping over rather than living with it:

| | |
| --- | --- |
| Scale | the cursor is read in client pixels and scaled onto the render, per axis |
| Fit | the window is shrunk to the desktop work area and centred, once |
| Clip | the cursor is held inside the client area while the game has focus |

Measured afterwards: the cursor at client (1975, 1331), the far corner of a
1977x1333 window, reads as game (2046, 2045), the far corner of a 2048x2048
render. The whole game screen is reachable from a window smaller than it.

The clip is released the moment focus is lost, so alt and tab still work, and
again on shutdown, because a clipped cursor outlives the process that clipped
it.

`pointer_scale=0` and `pointer_clip=0` turn each off. `window_width` and
`window_height` force a size rather than letting it fit.

## The view fills the headset at any resolution

The game renders a symmetric frustum and takes its vertical angle from the
render aspect:

    tan(v) = tan(h) * height / width

So taking the headset's widest angle and using it as the horizontal one only
covers the view when the render happens to be square. At any other shape the
picture sits in a box with black above and below, and the box changes size
when the resolution changes. Solving for an h that covers both:

    tan(h) = max( tan(h_needed), tan(v_needed) * width / height )

The layer then declares exactly what was rendered. For a headset asking 108 by
110 degrees per eye:

| render | renders | covers | area drawn against area needed |
| --- | --- | --- | --- |
| 1600x1600 | 110.0 x 110.0 | yes | 1.04 |
| 2048x2048 | 110.0 x 110.0 | yes | 1.04 |
| 2560x1440 | 137.0 x 110.0 | yes | 1.84 |
| 1280x1024 | 121.5 x 110.0 | yes | 1.30 |

A render far from square still fills the view, by being wider than it needs.
It just draws more than it shows, and since this workload is draw call bound
rather than pixel bound, that costs less than it sounds.

## Settings

The backend is a line in `bwvr.ini`, so the routes can be compared without a
rebuild:

```ini
[bwvr]
backend=                        ; system DirectDraw, the default
backend=dxwrapper\ddraw.dll     ; through dxwrapper's own stub
backend=d7vk\ddraw.dll
gpu_bridge=1                    ; use the GPU path when the backend allows it
```

A name with no path is looked for beside the game first. Anything the chosen
backend does not export falls back to system DirectDraw, so a partial
implementation cannot leave a thunk pointing at nothing.

System DirectDraw is only loaded if the chosen backend leaves a gap. The order
matters: dxwrapper checks whether the system library is already in the process
and behaves differently when it is.

## One frame, one head

This is the fault that made the headset unwearable, and it hid for a long time
because it is invisible on a monitor.

`LH3DTech::UpdateCamera` is hooked to put the player's head on the camera the
engine builds. The hook also recorded three pieces of per frame state: the
anchor camera that places the Hand, the anchor position, and the roll the
position-and-focus camera cannot carry.

The engine does not call that function once a frame. The log counted it:

```
head camera  : 2250 camera updates, 2250 carried the head
```

2250 over 450 frames. One call places the main camera before the scene walk.
The other four happen **inside** the walk, and the mod runs that walk twice,
once per eye. So eye 0 read the roll, drew, and the walk then changed the roll
before eye 1 read it. Both eyes of one frame drew from different cameras:

```
eye 0 roll  3.27 degrees      eye 1 roll  136.54 degrees
```

The right eye's value climbed frame after frame and passed 90 degrees, which is
why looking up made the right eye invert and spin. The same state carries the
Hand, so the Hand landed in two places in one frame, which read as a doubled
Hand that followed the head in one eye only.

Three reports, one cause.

The fix is a bracket around the whole scene walk. Every call still moves its
camera, because the sub cameras need the head too. Only a call made **outside**
the walk records the frame's state. The bracket goes around the whole of
`GGame::Process3dEngine`, including the three paths where it returns early, so
a frame that draws mono counts exactly like a frame that draws two eyes.

After the fix, on the same scene:

```
head camera  : 2250 camera updates, 2250 carried the head, 450 recorded over 450 frames
```

The record count is the number to watch. It must equal the frame count.

## Roll about your nose, yaw about world vertical

The head rotation H arrives in a levelled frame, because the runtime reports
the head in a gravity aligned space. The game's own pitch P is whatever turns
that levelled frame into the game camera's frame.

The first version composed `L * H * P`, which puts the whole head rotation
outside the game's pitch. That keeps a level horizon and a correct yaw. It also
means a head roll is a rotation about the LEVEL forward axis, and the game
camera's forward is pitched down away from that axis, so rolling swept the look
direction sideways:

| Head tilt | Old `L * H * P` | Split |
| --- | --- | --- |
| 0 degrees | heading 0.00, elevation -13.00 | heading 0.00, elevation -13.00 |
| 10 degrees | heading 2.30, elevation -12.80 | heading 0.00, elevation -13.00 |
| 20 degrees | heading 4.51, elevation -12.20 | heading 0.00, elevation -13.00 |
| 30 degrees | heading 6.58, elevation -11.23 | heading 0.00, elevation -13.00 |

That 4.51 degrees is the world sliding sideways when the player tilts their
head, and it scales with how far down the game camera is looking.

Composing `L * P * H` instead fixes the roll and breaks the yaw, because a yaw
would then turn about the pitched up axis and lift the view. That was the
earlier complaint, measured at 23.3 degrees of induced roll for a 40 degree
turn. Neither order works on its own.

So split H. Its yaw goes outside the pitch, where yaw belongs, and its pitch
and roll go inside, where they act about the camera's own axes:

```
R = L * Yaw(H) * P * PitchRoll(H)
```

Measured against the things this has to do, at a 13 degree camera pitch:

| Head | Elevation | Heading | View roll |
| --- | --- | --- | --- |
| straight ahead | -13.00 | 0.00 | 0.00 |
| yaw 30 | -13.00 | 30.00 | 0.00 |
| roll 20 | -13.00 | 0.00 | -20.00 |
| pitch up 15 | 2.00 | 0.00 | 0.00 |
| yaw 40 and roll 15 | -13.00 | 40.00 | -15.00 |

A pure yaw keeps its elevation. A pure roll leaves the look direction alone. A
pure pitch adds. The combined case is exact as well.

### What this still cannot do

A pitched view and a perfectly static world cannot both be true. Turning the
head from A to B should turn the world by exactly the same rotation, and it
does for every case except the roll itself:

| The head moves | Split | Gravity lock |
| --- | --- | --- |
| yaw 0 to 40 | 0.00 | 0.00 |
| pitch 0 to 25 | 0.00 | 0.00 |
| roll 0 to 20 | 4.51 | 0.00 |
| yaw 40, then add pitch 25 | 0.00 | 0.00 |
| yaw 40 to 70, rolled 20 throughout | 0.00 | 0.00 |

The 4.51 degrees is the same number as before, but it has moved out of the look
direction and into the roll axis. A world that rolls slightly less than the
head does is far easier to take than a world that slides sideways, because
sliding reads as the world moving and a small roll error does not.

Gravity lock removes it completely by dropping P, so the stage carries only
position and heading. It was a setting called `gravity_lock` for one session,
and is now the only composition. See "Gravity lock is the only composition". The world then becomes perfectly rigid and the horizon is
always level. The cost is that the player's straight ahead view becomes the
horizon, and they look down at the island with their head.

### The zenith

The old code found the roll by crossing world up with the head turned forward
and normalising the result at a threshold of `1e-4`, which is 0.006 degrees
from vertical. Anywhere near the pole that cross product is dominated by
tracker noise, so the roll swept the whole range. That is what made looking at
the sky set the right eye spinning.

The roll is now computed from the rotation this file composed, against the
rotation the engine is about to build, and the forward is held half a degree
short of vertical so the engine's own basis stays well conditioned.

The roll number still flips by 180 degrees as the view crosses vertical,
because heading and roll about world up are not defined there. That does not
matter, and this is the check that proves it: the engine's basis flips at the
same instant, so the product of the two is continuous. Swept through the
crossing in 0.05 degree steps, the picture turns 0.0500 degrees for every
0.0500 degrees of head movement, with no jump anywhere.

## The eye offset comes after the roll

The per eye matrix wrote `t' = (t - d) * H`, which puts the eye offset on the
axis the camera had BEFORE it rolled. The eyes then separate along the level
axis while the picture is tilted. The error is `dx * (cos(roll) - 1,
sin(roll), 0)`, and the sin term is the one that hurts: the eyes gain
`separation * sin(roll)` of VERTICAL disparity, about 23 mm at a 20 degree
tilt, which no pair of eyes can fuse.

It is `t' = t * H - d`. With the eye at world position e and basis B, `t = -e *
B`. After the roll, `B1 = B0 * H`, and the eye moves by dx along B1's right
column, so `t1 = -(e0 + dx * B1_right) * B1 = t0 * H - d`.

## The declared pose, and the one place it lied

A projection layer says "this image is what a head at this pose sees", and the
runtime warps it from that pose to the pose at display time. The runtime's own
pose is the right thing to declare, because that is exactly what the render is
built to be.

The one place it lied is roll. With roll switched off the picture is drawn
level whatever the player's head is doing, while the runtime's pose still
carries the tilt. The layer then claimed a rolled camera for a level picture,
so the runtime showed a level picture inside a tilted frustum and the whole
world tilted with the player's head. The roll is now taken out of the claim
whenever the render leaves it out.

## The eye separation the headset measured

A fixed number in world units has to be right for one world scale and one pair
of eyes. The runtime already knows the player's own, so with
`eye_separation_milli=0` the mod takes the distance between the two located eye
positions and scales it into the world. The old default of 0.5 world units at
7.5 units per metre is 66.7 mm, which happens to be within 0.1 mm of the
headset this was built against, so the change is invisible here and right
everywhere else.

## Pair the hint copies by order, not by distance

The game draws each hint twice in each pass: the projected copy first, the
pinned copy second. The mod paired them by screen distance, keeping anything
within `120 * g_pixel_scale`, which is 75 px at a 1600 by 1600 render.

Head tracking pulls the pair much further apart than that, and by DIFFERENT
amounts in each eye, because the projected copy moves with the eye and the
pinned one does not. So one pass recognised the pair and dropped a copy while
the other did not, and the player saw one hint in the left eye and two in the
right. The picture alone is enough to pair them, and it gives both eyes the
same answer by construction.

Two flags were also doing one job. `g_in_widget_band` meant both "scale this"
and "leave this out of the interface shift", and it was only ever set in
`kHintKeepPinned`. Head tracking selects `kHintKeepProjected`, so in the mode
the player actually runs the hint size setting did nothing at all, and the
interface depth was stacked on top of the Hand's own.

The hint now keeps the copy at the Hand, and `hint_flat` records where it lands
in the first pass and moves it to the same place in the second. Both eyes then
draw it at one screen position, so it is flat and comfortable to read, and it
is still anchored to the Hand rather than riding the look direction.

## Subtitles

The subtitle bar sits at the bottom of the render. On a monitor that is a few
degrees below the middle of a 60 degree view. In a headset it is about 55
degrees below the middle of a 110 degree one, which is past the bottom of
comfortable vision.

The bar is drawn by one caller, `0x0081E3C0` to `0x0081E58C`. The LETTERS on it
come from `LH3DTech::Draw3DScreenTriangle` at `0x0081C5C0`, the generic text
submitter that menus and buttons also use, so the caller cannot tell them
apart. The band can: the letters sit in the same rows of screen as the bar they
are written on. That is the same trick the hand hint already uses, and it is
what lets `subtitle_lift_pct` move the bar and its words together, and what
lets `ui_world_lock` hang both in the world instead of only the bar.

`ui_world_lock` is parked. See "Hang subtitles in the world, parked" below.

`UiWorldLockPoint` was wrong twice over. It used the head's ABSOLUTE pose, and
it applied that pose in the camera's own frame. The head is applied to the
engine's camera in a levelled frame, and the engine's camera already carries it
by the time anything is drawn, so rotating a screen ray by the absolute head
pose applied the head a second time about the wrong axis. The rotation wanted
is simply the difference between the two cameras, `transpose(render) * anchor`,
which is the identity when the head is straight ahead.

## A window to look through

`immersive=0` keeps the picture on a quad at a fixed distance instead of
filling the view, which is the Phase 2 behaviour. The head still tracks, but
the screen stays put in front of the player and nothing fills the periphery.
It is the comfort setting for anyone the full view makes ill, and it costs
nothing to offer because the quad layer path never went away.

## Gravity lock is the only composition

Both compositions above were built and played side by side in the headset. The
split still felt like sliding, because the 4.51 degrees it leaves in the roll
axis is still a world that moves when the head tilts. Gravity lock removed it
completely and caused no sickness at all. So it is no longer a setting:

```
R = L * H
```

The cost is real and worth stating. The player's straight ahead view is the
horizon, whatever the game's camera is doing. The game's camera looks 12.9
degrees below the horizon in ordinary play, and 78.8 degrees below it during
the menu flyover, so the further the game zooms out the further down the
player has to look to see the land. That is what hovering above a real island
would be like.

## One camera for the Hand, and the cursor turns instead

To stop the Hand sweeping the world when the player turned their head, the
game's own camera used to be swapped in around `CHand::PrepareForDrawing`, so
the cursor's ray came from where the GAME was looking. That placed the Hand
through one camera while everything else the game does with the cursor used
another: what the Hand is over, whether a drop is allowed, where a held object
goes. `CHand::UpdateHeldObject` was never inside the swap at all.

Under the split composition the two cameras agreed when the head was centred,
so this only showed when the player looked around. Gravity lock made them
differ by the game's whole downward pitch, all the time, and it broke the Hand:

- a red cross beside the Hand, because the game's own ray pointed at sky while
  the Hand sat on the ground,
- ground that span under the Hand instead of dragging,
- objects that would only drop near the bottom edge of the screen, the one
  place where the player's own ray pointed low enough to reach the ground.

The swap is gone. Every part of the game now sees one camera, and
`pointer_map.cpp` turns the CURSOR by the inverse of the head each frame
instead. The view is `R = L * H`, so for a direction on screen to stay fixed in
the room while the head goes from `H_prev` to `H_now`:

```
r' = transpose(H_now) * H_prev * r
```

Checked in isolation, the world direction under the cursor holds to 0.000001
degrees for a turn, a tilt, a nod, and a turn made while already tilted.
Checked in the running game with an invented head stepping between 0 and 20
degrees and the mouse untouched:

| Head | Cursor, render pixels | Hand, world position |
| --- | --- | --- |
| 0 degrees | 2160, 1622 | 1547.77, 884.46 |
| 20 degrees | 1415, 1612 | 1547.93, 886.41 |

The cursor moved 745 pixels and the Hand stayed where it was. Over 13 steps
back and forth the cursor returned to exactly the same two positions, so the
float that carries the sub pixel remainder does not drift.

Two limits follow from doing it this way. A head turn far enough to take the
Hand off the side of the view pins the cursor at that edge, so turning back
finds the Hand at the edge rather than where it was. A turn of more than 30
degrees in one frame is treated as a recentre or a tracking glitch and ignored.

## A window the shape of the render

The game creates its window as wide as the render and only as tall as the
desktop allows, so a 2880 by 2880 render came up in a 2544 by 1217 window. The
cursor is scaled by the ratio of the two, which was 1.13 across and 2.37 down:
moving the mouse down moved the Hand more than twice as far as moving it
sideways. The window is now fitted to the render's own shape, allowing for the
game's Options menu bar, and the scale reads 2.16 across and 2.16 down.

## A separation of zero means ask, not zero

`eye_separation_milli=0` asks the headset for the player's own separation.
`StereoInstall` wrote the zero straight into the eye separation, so until a
headset had measured, both eyes drew from one position and the world had no
depth at all. The draw probe caught it, with every world batch classed as flat.
Zero now means "ask", with 0.5 world units, which is 66.7 mm at 7.5 units per
metre, until the answer arrives.

## The low cloud band rides the head (open)

Measured with the invented head stepping 20 degrees, by cross correlating each
band of the left eye picture between the two captures:

| Band | Shift | Match |
| --- | --- | --- |
| upper sky | -764 px | 0.894 |
| clouds near the horizon | 0 px | 0.588 |
| sea and land | -724 px | 0.505 |

A band fixed in the world shifts by the full turn, about 745 pixels. The sky
dome, its high cloud and the sun do. The soft grey cloud band just above the
horizon does not move at all.

Ruled out so far:

- The full screen quad `LH3DRender::FinishFrame` draws at `0x0082F617`. Its
  diffuse colour is 0, so it is a transparent fade, not cloud.
- `CloudInSky`. Its per frame update at `0x005E25C0` drifts the 70 clouds in
  world space and reads no camera at all.
- Missing depth. With a real separation every world caller in the probe is
  classed stereo, and only the fade quad is flat.
- `fn_0085AB10`, one batch a frame. It projects to the screen, clamps to the
  screen edges and uses a 90 pixel size, which reads as the sun's glare.

`LH3DSprite::Draw` builds each billboard's model view at `0x00EA9E10` as an
identity rotation in camera space, with the centre taken from the full per eye
matrix. So a billboard's centre follows the head, and its face stays square to
the screen. That second part explains the cloud that turns and rolls with the
head, and it is worth fixing once the band itself is found: a billboard for VR
wants world up, not the camera's up.

### Found: the stored camera carried the head

`LH3DTech::UpdateCamera` does two things. It builds everything the renderer
needs from the camera it is given, and it STORES that camera: the position at
`0x00EA1DB8` and the focus, the point the camera looks at, at `0x00EA1DC4`.
Thirty eight places read the stored focus afterwards, and most of them are game
logic, not drawing:

| Reader | What it does with the focus |
| --- | --- |
| `HandStateHolding::Update` | the vector from the held position to the focus, scaled by 0.3, decides the CameraHelp hint |
| `HandStateCamera::Update` | drags the land relative to the focus |
| `GCamera::Update` | in some modes takes the stored camera back as its own starting point |
| `GScript::StoreCameraDetails`, `GScript::GetCameraFocus` | scripts save and restore the camera |
| `fn_005E5CD0`, in the sky and cloud module | recomputes whenever the stored camera moves by more than 1 unit |
| `Tree::PreDraw`, `HelpSystem::SendFOVObject`, `PowerSpinRunner::Update` | various |

The hook passed the head camera in, so the head was stored too. Under gravity
lock the focus became a point in the air toward the horizon. That explains
both reports at once:

- A held villager could only be dropped while the player looked down about as
  far as the game's own camera does, which is the report exactly: "if I keep
  my head pointed downwards at about 45 degrees then I can grab things".
- The cloud field was laid out around where the head was looking, so it turned
  with the head.

It also fed the head into the game camera itself, through `GCamera::Update`,
which the hook then turned by the head again.

The fix is to write the camera the game asked for back into the stored
position and focus after every `UpdateCamera` call. The renderer has taken what
it needs by then, the matrices, the frustum and the culling cone, and those
keep the head. The arguments are copied before the call, because a caller may
pass the stored camera itself.

Measured again after the fix, same test:

| Band | Before | After |
| --- | --- | --- |
| upper sky | -764 px | -764 px |
| clouds near the horizon | 0 px | -772 px |
| sea and land | -724 px | -768 px |

Every band now moves with the world.

The stored camera FORWARD at `0x00EA1DD4` is left carrying the head. It is
written by `LH3DTech::UpdateWorldToCamera` and read by particles, trees and the
water, which are drawing. Its one getter, `fn_00442CE0`, has no caller and no
pointer to it anywhere in the binary.

Still open: the sun. `fn_0085AB10` draws something 90 pixels wide at the sun's
screen position and clamps it to the screen edges, which reads as the glare.
A glare clamped to the edge would slide along the edge of the view as the head
turns while the sun itself is out of view.

The headset report after this fix: the clouds and the sun now stay in the
right place in the world, but bob a little as the head moves. That is still
open.

## One pointer, not two

The report after the stored camera fix: a red cross beside the Hand whenever
the head was level, and objects that could only be picked up or dropped with
the head about 45 degrees down. The stored camera was not the cause.

### The interface was given window pixels

The game takes the pointer in more than one place. `pointer_map.cpp` rewrites
the pointer globals at `0x00E852C0` and `0x00E852C4` once a frame, in render
pixels, and the Hand is placed from them. But the window procedure also
writes them, on every `WM_MOUSEMOVE`, and something reads them there first:

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
stored point. So the land under the Hand was looked up at a point 2.16 times
nearer the top left corner than the Hand. Logged with an invented head:

| | Hand pointer | Interface pointer | Land found |
| --- | --- | --- | --- |
| before, head level | 2879, 1920 | 1285, 630 | none |
| after, head level | 1441, 2592 | 1441, 2592 | yes, 52.99 units away |
| after, head 40 degrees down | 1441, 2592 | 1441, 2592 | yes, 29.14 units away |

With the head level, the upper half of the view is sky, and the wrong point
was nearly always there. The game found no land under the Hand, drew a red
cross and would not drop anything. With the head down far enough, the wrong
point also landed on the island, which is why looking down helped.

The fix hooks `MouseMoveHandler` and changes the message position to render
pixels, with the same mapping the frame rewrite uses. The globals and the
interface then agree with the Hand from the moment the message arrives.

`LHMouse::SetPosition` at `0x007E4E40` had the same fault the other way. It is
how the game moves the cursor itself, and it gives render pixels to
`SetCursorPos`, which takes window pixels. It is now converted too, and the
Hand's own position starts from the exact point the game asked for.

This also corrects the section above. The stored focus fix was real, because
the hint for a held object and the land drag read the focus. But it did not
explain "only when looking down". The window pixel pointer did.

### Holding the ground and turning the head dragged the land

The camera code in `CameraModeNew3` builds its own world to camera matrix from
the game's camera, with `LH3DTech::UpdateWorldToCamera`, and reads the pointer
globals as a point on THAT screen. The head lock moves the pointer on the
rendered screen to keep the Hand still in the room. On the game's screen that
looks exactly like the player moving the mouse.

Measured with the invented head stepping 20 degrees, which is inside the
lock's 30 degree limit, and the left button held on the land:

| Head | Button | Game camera z |
| --- | --- | --- |
| level | up | 2696.22 |
| 20 degrees down | up | 2696.22 |
| 20 degrees down | held | 2748.06 |
| level | held | 2696.05 |

The camera moved 52 units back with the head down, and came back when the
head came up.

The fix gives the camera code its own screen. For the length of
`GCamera::Update` at `0x00441F80`, the pointer is where the Hand's ray falls on
the game's own screen:

```
game ray = transpose(E) * L * H * rendered ray
```

`E` is the game's camera with no head, recorded by the camera hook on the main
call. `L` is the room and `H` the head, as before. The point moves only when
the Hand's ray moves, which is when the player moves the mouse. A cursor move
that the camera code asks for is taken back to the rendered screen by the
inverse. Everything outside the bracket, the Hand, the land under it and the
drawing, still uses the rendered screen.

Measured again:

| Head | Button | Game camera z |
| --- | --- | --- |
| 20 degrees down | held | 2696.22 |
| level | held | 2696.22 |
| either | held, mouse moved 300 counts up | 2792.95 |

The head no longer moves the land. The mouse still drags it: the camera moved
101 units, and the Hand stayed on the ground it had grabbed, to within 1.6
units.

A test note. `SetCursorPos` does not reach the drag, because the drag takes
its motion from DirectInput. A drag test has to send relative mouse input,
`mouse_event` with `MOUSEEVENTF_MOVE`.

### Two smaller fixes found on the way

- The window fit counted the game's Options menu when the menu was not there,
  so the client came out 1353 by 1376, and the mouse moved the Hand 2 percent
  further across than down. The fit now checks the client it got and corrects
  it.
- `GetPrivateProfileIntA` returns an unsigned value, so `fake_head_pitch=-40`
  arrived as about four billion degrees. The invented head now reads negative
  angles, and the sweep alternates the whole pose with a level head, so a
  pitch can be tested like a turn.

## Moving around

The Hand works with the head level now. Two things were left, both about the
camera.

### After a middle mouse turn, the zoom stuck or the Hand stopped

`GCamera::Update` at `0x00441F80` runs the play mode's update, then makes the
main `LH3DTech::UpdateCamera` call at its END, at `0x00442622`. So while the
camera code runs, the engine still holds the previous frame's camera, and that
one carries the head. The camera code casts rays through the pointer with the
engine's matrices, for example the grab point at `0x0045BA22`. Since the
pointer became a point on the game's own screen (see "One pointer, not two"),
those rays went through a head camera with a game screen pointer, and landed in
the wrong place. A grab distance over 3000 stops the orbit and locks the
camera's input at `CameraModeNew3 + 0x2F0`.

`h_GCameraUpdate` now points the engine at the game's own camera from the last
main call, with no head, before the camera code runs. The main call at the end
puts the head back. Two guards:

- When the game draws from another camera (`[game + 0x205A28]` is not zero),
  the main call is skipped, so the rebuild is skipped too.
- If the main call still does not happen, the head is put back by hand, so a
  frame is never drawn without it.

On the desktop rig the rebuild ran on every frame and the head never had to be
put back by hand. Holding the ground through two head steps still left the
camera exactly where it was. `game_camera_rebuild=0` turns it off, for an A/B
test in the headset.

A second suspect was ruled out on the way. The game's "turn off mouse move"
flag at `0x00E8C0FA` is set while the middle mouse turns the camera. But
`GInterface::Process` sets it back from `IsPlayBack` on every game turn, so it
cannot stay set for long. The pointer map now holds the Hand still while that
flag is set and a mouse button is down, which is what the game intends.

### Snap turns, instant jumps and no glide

`comfort_camera.cpp` hooks `ControlMap::IsActionPerformed` at `0x00470AF0` and
`CameraModeNew3::Update` at `0x0045A960`.

- **Snap turns in place.** The game is told the middle mouse (action `0x10`)
  is up. The mod counts the mouse movement itself and turns once per 150 counts
  sideways. A turn key counts once per press, read from the key turn at
  `0x00C5B0C8` that `ProcessKeyMovement` fills after each camera update. A turn
  rotates the focus and the orbit pivot at `+0x12C` about the position, then
  puts the six GCamera zoomers straight at their values with
  `Zoomer::SetPosition` at `0x00441AC0`. The mode's copies at `+0xC`, `+0x18`
  and `+0x1D4`, and its heading at `+0x50`, come along, so the next zoom does
  not swing back. `CameraHelp::CameraHelpCallback` is called with `0x300` and
  a direction, so the tutorial still counts the turn.
- **No other smooth turn or tilt.** Bits 1 and 2 of
  `CameraHelp::EnabledFeatures` at `0x009CDD6C` are cleared for the length of
  the mode's update and put back after. Scripts write that word, but never
  during the call. A turn also needs the script's own bit 2, so the tutorial
  can still hold turning back.
- **Instant jumps.** A double click starts a near flight (150 units or less),
  which sets the elapsed time at `+0x78` to 0.75 s, or a far one, which sets
  it to 0 and flies through a point above the land with the target at
  `+0x110` and `+0x11C`. Either way the elapsed time goes down, and nothing
  else makes it go down. The mod then puts the zoomers at the target and sets
  the elapsed time to 1.5 s.
- **No glide.** The time the camera takes to reach where it is sent, 0.3 s at
  `0x009CE614`, has one reader and no writers. The mod sets it to 0. The glide
  also smoothed a held drag, and that hid a pointer fault. See "Holding the
  land shook the camera".

Measured on the desktop rig with real mouse and key input:

| Input | Result |
| --- | --- |
| Middle drag, 250 counts right | one turn, heading 3.687 to 4.472, exactly 45 degrees, position unchanged |
| Z key | one more turn of 45 degrees, no drift after it |
| Double click near | landed in one frame |
| Double click far | landed in one frame, about 97 units away, and stayed there |
| Wheel zoom after the turns | the camera moved in |
| Middle drag | the Hand's pointer stayed at the same x |

Still gliding, and not changed here: the camera that follows the Creature
after a double click on it, the 3 s pull back when the camera goes more than
3500 from the middle of the map, the arrow keys, and the automatic tilt. A
vignette during a land drag is the next step if the drag itself is still
uncomfortable.

### Holding the land shook the camera

The report from the headset: hold the land, do not move the mouse, and the
camera shakes in place. It keeps shaking when the mouse moves.

**Cause.** The camera code got its pointer through a round trip in whole
pixels.

1. `PointerMapApply` keeps the Hand as an exact float that is locked to the
   room. Then it writes the Hand to the pointer globals as a whole render
   pixel.
2. `h_GCameraUpdate` turned that whole pixel through the head onto the game's
   own screen, and cut it to a whole pixel again.

Both steps use the same head pose, so the head cancels in exact arithmetic.
The first cut does not cancel. Where the Hand falls inside its render pixel
changes as the head moves, so the game's pixel flipped between two neighbors.
A real head is never still, so this happened on many frames. Above 0.02
degrees of head motion a frame, it happened on most frames.

The land drag makes each flip visible. `UpdateClickParams` at `0x00459610`
fixes the grab point, the grab plane and the grab camera at the press. Each
frame, `CameraModeNew3::Update` casts the current pointer through that fixed
camera onto that fixed plane. Then it moves the camera by the difference
(`0x0045D365` to `0x0045D96E`). So the camera is a direct function of the
whole pixel, and each flip moves it. A flip moves the grabbed point by one
pixel on the screen. Ground nearer the camera moves 4 to 30 times as far.

**Why it showed up with the new controls.** The game's 0.3 s glide at
`0x009CE614` smoothed the hold too. The duration override at `0x00460229`
reads a stack slot that `0x0045E359` clears before the move switch. Only a
fight fills that slot. So the glide applied during a hold, and it hid the
flip. The "No glide" change removed that smoothing.

**Fix.** `h_GCameraUpdate` now turns the Hand's exact float, not its whole
pixel, and rounds the result. The pixel also stays where it was until the
exact point is 0.8 px away from it, so float noise cannot flip it. When the
camera code moves the cursor itself, the mod keeps the pixel it asked for, so
the next frame gives the same pixel back. Ctrl+Shift+F9 switched back to the
old conversion, for an A/B test in the headset. The measurements below settled
it, and that key now cycles the drag smoothing (see "Smoother drags far out").

Two new report lines count the frames where the land is held and the mouse
is still:

```
pointer      : land held with the mouse still on N frames. The game's pointer moved on P, the camera on C.
pointer      : the camera moved S units at most. Its pointer came from the exact point.
```

The second line names the conversion. It said "whole render pixel" after
Ctrl+Shift+F9, while that key switched the conversion. The hotkey printed the
counts so far before it switched, so a line never mixed the two conversions.

Measured:

| Test | Old conversion | New conversion |
| --- | --- | --- |
| Offline model, 2880 px at 110 degrees, 900 frames of small head noise (0.005 to 0.5 degrees a frame) | pointer changed on 186 to 649 frames | 0 |
| Desktop rig, land held through four head steps | 0 changes (only two head poses) | 0 changes |
| Desktop rig, a 300 count drag down a slope, frames with the mouse still | pointer changed on 64, camera moved on 243, up to 12.3 units | 0 and 0 |
| Desktop rig, land held on a slope with the head and the mouse still, switched live with Ctrl+Shift+F9 | pointer changed on 76 of 372 frames, camera moved on 357, up to 12.9 units a frame | 0 and 0 |

The last two rows show a second path the fix closes. On a slope, the drag changes
the game's pitch. That moves the game's pixel, which moves the camera again.
The 0.8 px stick stops that loop. During the drag, the Hand stayed within 1
to 3 units of the grabbed ground with either conversion.

### Zoom in steps

At the usual game pitch, one click of the wheel moved the camera about 6
percent of its distance. It took 11 clicks to halve the distance, and a fast
scroll felt like a slide.

How the game zooms:

- `CMouse::ProcessButtons` adds the DirectInput wheel total, 120 for a click,
  to the int at `0x00C5E8D8` (`0x0061A24D`, `0x0061A282`). The int keeps the
  sum until a reader clears it. Action 3 is the wheel down and action 4 the
  wheel up.
- `CameraModeNew3::Update` reads the int at `0x0045B704`, only while
  `[mode + 0x2EC]` is zero and the follow blend Zoomer at `0x00C5B100` is
  below 0.01. Then it clears the int at `0x0045B77E`, whether or not a wheel
  action is performed. `CameraModeFollow::ProcessKeyMovement` also reads and
  clears it.
- The zoom is `acc -= wheel * [0x009CE61C]` (0.5, one reader, no writers),
  then `delta = S * acc * 0.0015` at `0x0045C0CC`. S is three times the height
  of the camera above the focus, between 60 and 2000. When the camera is below
  the focus, S is between 60 and 240.
- `delta` goes to the distance at `+0x1A8`, with a floor of 3.1. That field
  can be stale until a zoom starts. Then `UpdateClickParams` sets it from the
  camera and the focus (`0x00459AC8`).

`comfort_camera.cpp` does not replace the game's zoom code. So the 3.1 floor,
the script switch (feature bit 4) and the tutorial's "zoomed" event all stay.
The mod does two things around that code:

- **A fixed ratio.** For the frame of a step, the mod sets the wheel scale at
  `0x009CE61C` so that the step takes the distance to `d * ratio^-clicks`. It
  measures the distance between the GCamera Zoomer destinations, the same
  points the game uses. A step is never smaller than the game's own step, so
  the camera does not crawl near the land. The scale goes back to 0.5 after
  the update.
- **Joined clicks.** The mod takes the wheel counts from the game and keeps
  them. The first click is used at once. After that, at most one step happens
  every 150 ms, with at most three clicks in a step. In between, the game is
  told the wheel did not move. The mod keeps at most two steps of clicks, and
  a turn of the wheel the other way drops them.

A key bound to a wheel action gives no counts, so it still works as the game
has it. The launcher setting `zoom_step` picks the ratio: the game's own step,
1.12, 1.20 (the default) or 1.30. Ctrl+Shift+F8 cycled them until v1.0.

Measured on the desktop rig at the medium step:

| Input | Distance |
| --- | --- |
| One click in, straight after loading | 151.0 to 125.9 |
| One click out, twice | 125.8 to 151.0, then 151.0 to 181.2 |
| Six fast clicks in | three steps: 151.6 to 126.4 (1 click), to 87.8 (2), to 50.8 (3) |
| A snap turn, then one click in and one out | 79.9 to 66.6, then back to 79.9 |

Each result is the target ratio to one decimal place.

## Toward v1.0

The headset report after the navigation work: the game is playable, with a
list of smaller faults. This round fixes six of them and trims the launcher.
See launcher.md for the settings.

### Tooltips drawn twice in each eye

The belief gained notice, the Storehouse readout and the Temple readout are
HelpSystem tooltips. The game draws its tooltip list twice in every eye pass:
once from `HelpSystem::Draw3D` (the list walker `fn_00447850` returns to
`0x005C5B36`) and once from the HelpText FinishFrame callback (return
`0x005CCE46`). On a monitor the two copies land on the same pixels.

The dedup for the hint beside the Hand builds its band around the mouse icon
quad. A tooltip has no icon (icon type 3), so no band was made and both copies
were drawn.

Each tooltip ends in one call to `CameraHelp::DrawKeyOrMouse` at `0x00447EA0`
(cdecl, 11 arguments, the text is a wide string). The mod hooks that call and
keys it by its text, its icon and its icon type, never by position. The first
copy in a pass draws, and a second with the same key is skipped. The icon
inside a speech bubble (return `0x005763D1`) is left alone. The kept copy is
drawn flat at the Hand, like the hint, and gets the interface size about its
own center. The log counts both, in every report window:

```
ui depth     : N tooltips drawn, M second copies skipped
```

### The help layer at a smaller size

The game sizes its help layer from the render height. A tooltip is a 25th of
the screen height and its text two thirds of that, so at a 2880 pixel render
the text is about 77 pixels tall, and a subtitle spans the whole view.

`ui_scale_pct` (default 30, 20 to 100) scales that layer toward the center of
the view,
which makes it smaller and brings it inward in one step. It applies by scope,
inside HelpText's FinishFrame callback `0x005CD020` (subtitles, help text, the
Creature panel at `0x005178D0`) and `HelpSystem::Draw3D` at `0x005C59A0`. The
caller ranges alone are not enough, because the text submitter and the widget
toolkit also draw loading screens, the front end and menus.

Menus and dialogs are not scaled. Their text is a fixed 20 to 35 pixels at any
render size, which is already small in a headset. They are also the only
interface anyone clicks, so moving them would move them away from their hit
test. The subtitle lift applies first, and the scale shrinks the lifted
position. The launcher sets the size. The default was 50 until the next headset round, see "The
subtitle box and the interface size".

### Hang subtitles in the world, parked

Tried in the headset, it read worse than subtitles on the view. The setting,
its hotkey and its step in the draw hooks are removed. `ui_worldlock.cpp`
stays in the build with a note, because a later interface in the world needs
the same camera difference. Removing the step also removed two 16384 float
arrays (128 KB) from the stack frame of every interface draw.

### Clouds that nodded with the head

A cloud is an `LH3DMist`: a dome, `mist.l3d`, with its base at y 0 and its
pole at y 20, scaled 13 to 50 times. `LH3DMist::Draw` at `0x007FA300` turns
each one by copying a basis from `0x00EA1C98` (at `0x007FA38C`). Its rows are
the camera right, the camera forward reversed and the camera up.
`LH3DTech::UpdateCamera` builds it from the camera it is given, which is the
head camera. The dome's body sits 130 to 500 units from its pivot along that
basis, so a nod swung each cloud up or down across the sky.

A hook on `LH3DMist::Draw` now gives each cloud its own basis first: facing
the game camera position at `0x00EA1DB8`, which carries no head, with world
up. With the head straight ahead this is the engine's own basis. The weather
clouds and the ground mist are the same class.

The same family: `UpdateCamera` also builds a heading turn at `0x00EA1D88`
from the stored camera before head_camera.cpp gives it back. The draw at
`0x0080DB30` turns billboard meshes at the lowest detail by it, so far trees
turned with the head's yaw. It is built again after each call from the camera
the game asked for, with the engine's own `fn_00841260` and the constant at
`0x008C78D8`.

Ctrl+Shift+F11 switched both back to the game's behavior until v1.0.

The desktop rig could not show the difference. A 30 degree nod moves the sky
out of the frame, and at 10 degrees the bob is about 1 degree, which is the
size of seven seconds of cloud drift between two captures. With the fix on,
cloud patches landed where a pure head rotation puts them (median miss 0 px).
The sun is a fixed quad at (-30000, h, -30000) and reads no camera
orientation. The headset still showed it bob, so the cause is elsewhere. See
"The sun glare" and "Clouds that reached the eye" below: the per cloud facing
here also had a fault.

### Models keep their detail further out

`LH3DObject::AddForDrawing` (`0x00815A70`, and its twin at `0x00816AD0`)
chooses a mesh by the depth of an object along the view axis, against four
multiples of a size figure: 23.3, 66.7, 86.7 and 173.3. Past the last, a
stand-in sprite is drawn, or nothing. The four live at `0x00C37E8C` to
`0x00C37E98` in writable data, with two readers each and no writer.
`detail_distance` multiplies them once, after checking that they hold the
values measured.

The registry's `MaxObjectDistance` does nothing. At `0x008238C4` its value is
read into a stack slot that the next read overwrites.

Measured on the desktop rig from one fixed view, both eye passes, a 70 degree
view:

| Detail distance | Both eye passes | Draw calls a frame |
| --- | --- | --- |
| Game | 3.6 to 3.8 ms | 864 to 933 |
| 3x | 4.1 to 4.3 ms | 1049 to 1117 |
| 5x | 4.3 to 4.5 ms | 1089 to 1155 |

A headset view is wider, so it holds more objects and costs more. Fog still
ends at about 800 to 900 units in clear weather, so the furthest gain is
hazy, not hidden.

Still open: the depth is measured along the view axis, not as a distance, so
an object at the edge of the view keeps more detail and can drop a level when
the player turns to it. Hooking `0x00868C80` to use the straight distance
would fix it.

### The story camera

This first design was replaced in the next round. See "The story camera
holds its shot" below for the design in use and why this one failed.

The campaign script moves the camera through `CameraModeScript` (vtable
`0x008C7D5C`). `comfort_camera.cpp` never saw any of it, because
`GCamera::Update` runs only the top mode.

| Source | Calls | How it moves |
| --- | --- | --- |
| `MOVE_CAMERA_POSITION`, `MOVE_CAMERA_FOCUS` and two that move both | 1,520 | `MoveCameraPosition 0x004616F0` and `MoveCameraFocus 0x00461430` ease the six GCamera Zoomers to a destination |
| `SET_CAMERA_POSITION`, `SET_CAMERA_FOCUS` | 597 | Already cuts |
| `RUN_CAMERA_PATH` | 10 tracks | `UpdatePath 0x00461AB0` writes a spline pose each frame. Every track turns 108 to 813 degrees |
| `PLAY_HAND_DEMO` | 18 | `fn_005DAEE0` writes recorded poses. `rotate.hnd` turns 719 degrees |
| `SHAKE_CAMERA` | 31 | `PSysGlobal::StartCameraShake 0x0068F400` |

`script_camera.cpp` does this, all at run time, with no game file changed:

1. **A move plays or cuts.** On the first `CameraModeScript::Update` after a
   move, the position and focus moves of one script step are both set up.
   The mod judges them as the player sees them under `R = L * H`: the change
   of the flattened heading, and the travel. Over 5 degrees, or faster than
   15 units a second at its peak, the move cuts.
2. **A cut keeps the pacing.** A Zoomer eases as `StartValue + StartSpeed * t`
   plus three curve terms (`Zoomer::Update 0x00442720`). The cut sets the
   start to the destination and the rest to zero, and leaves the clock
   running. `GCamera::Arrived` at `0x00443050` then answers no until that
   clock reaches the move's own duration. Scripts that wait with
   `HAS_CAMERA_ARRIVED` (493 calls) wait as long as they always did.
3. **Not too close.** Each frame in the script mode, the camera destination
   is kept 30 units (`script_camera_min_distance`) from its focus and 10
   above the ground (`LH3DIsland::GetAltitude 0x00803090`). A move under way
   is aimed again with `Zoomer::SetDestinationWithSpeedAndTime`, so it still
   eases.
4. **Paths and hand demos hold their shot.** `GCamera::SetPositionAndFocus`
   at `0x004438C0` is hooked for the two callers. The shown pose is held
   until the game's pose turns 45 degrees away or moves a quarter of the
   viewing distance, then it cuts. The demo also renders with a direct
   `UpdateCamera` call (return `0x005DB125`), which the head camera hook
   answers with the held pose.
5. **No shakes.**

Every hook checks the mode's vtable first, because Esc can remove the script
camera in the middle of a sequence. Ctrl+Shift+F7 switched it until v1.0, and `script_camera` picks the
variant. The log reports, per window:

```
script camera: P moves played, C cut, I already cuts. K kept back.
script camera: N path shots, D hand demo shots, S shakes skipped, W arrival waits.
```

The first 24 judgments are also logged one by one, with the travel, the turn
and the time.

Not tested in a scripted scene yet. On the desktop rig a new game stopped in
the front end, which the rig cannot see or click through. The hooks install,
a save loads and plays, and the intro flight is unchanged with the setting on
or off. Follow and dual cameras (49 calls), the Temple interior and the pull
back past 3500 units still glide.

## The second headset round

The next headset report listed ten faults. Six research agents traced them in
the binary first. Their reports are the source of the addresses below. None of
this ran in a headset before it shipped. Each change has a hotkey or an ini
key for an A/B, and a log line that proves it acted.

### Dragging the land when looking down or behind

A drag worked only while the player looked roughly ahead. Right click worked
in every direction.

The camera code decides what a press does from where the pointer sits on the
game camera's screen, in `CameraModeNew3::UpdateTricons` at `0x00459230`. A
point near the bottom edge means tilt, a point near a side edge means turn,
and the rest means drag the land. The mod shows that code the head free game
camera, and the Hand's ray only lands on that screen while the head looks
roughly ahead. Looking down put it in the bottom band, or past the edge where
it was clamped to the edge. So the press became a tilt, and the mod switches
tilt off. Nothing moved for the whole hold. Behind the game camera the ray had
no point on its screen at all.

The fix is in `pointer_map.cpp`:

- While no button is held, the point the camera code sees stays inside the
  area that means "drag the land". A Hand behind the game camera goes to a
  point low in the middle. This happens only while snap turns are on, which
  already switch off the turn and tilt zones.
- `UnprojectScreenPoint` at `0x0081B370` (fastcall, the pixel in ecx, the
  output in edx, one stack float, `ret 4`) is hooked. Inside the camera
  update, the two rays of a drag get the Hand's own world ray. The pointer at
  `mode+0xB0` gets the current one, and `mode+0x2D4` gets the one from the
  press. A ray is replaced only when its pixel is the pixel this file wrote.
  The grabbed land then stays on the Hand's ray at any head angle.
- `CameraModeNew3::UpdateClickParams` at `0x00459610` (thiscall, three stack
  arguments, `ret 0xC`) records the press ray. After it runs, a zero plane
  normal (the grab point straight under the camera) becomes a level plane
  through the grab point. The game stops a drag on a zero normal.
- The game caps a drag step by the pixel distance from the press, times the
  view distance, times 0.011. That distance must mean the same thing wherever
  the press was made. Behind the camera it would not grow, and after a press
  that the box moved it would start with the box offset in it. So during a
  land drag, the distance from the press pixel is set to the angle the Hand
  turned since the press. A press at the middle of the screen gives the same
  in the game.
- At the end of its update the camera code can put the cursor back on the
  pixel it was given. When that pixel is one the mod placed, the Hand stays
  where it is.

Shift+F10 switched between the exact ray with the safe box and the old
pointer, until v1.0.

The log counts it once a report window:

```
drag         : N presses, N land drags, N moved into the safe box (N from behind), N used the exact ray, N frames smoothed
```

### Smoother drags far out

The camera follows a drag exactly, as a function of one pointer pixel. Three
things make the steps large far out:

- A ray hits the land at range R from a camera h above it. Each radian of
  pointer motion then moves the hit by R squared over h. At R 400 and h 10,
  one window pixel moves the camera about 34 units.
- The last session ran a 1333 pixel window on a 2880 pixel render, so one
  mouse step is 2.16 render pixels. A slow drag moves 0 or 1 pixel a frame, so
  the camera moves on some frames and not on others.
- The game's own 0.3 second glide hid all of this on a monitor. The mod
  removes that glide, which is right for comfort.

The mouse part of the Hand's movement now goes into a pending amount. Each
frame the Hand takes a share of it, `1 - exp(-dt / 35 ms)`. This happens only
while the left button holds the land in a land drag. At release the rest
arrives at once, so nothing moves after the button comes up. The Hand and the
camera code read the same smoothed value, so the land stays under the Hand.
Head motion is not smoothed. The lag is capped at 300 pixels.

Smoothing the camera instead would put the Hand's ray through a camera that is
late, and the land would slide under the Hand. `drag_smoothing_ms` sets the
time, and Ctrl+Shift+F9 cycled off, 35 ms and 60 ms until v1.0.

### The story camera holds its shot

The first design (above) judged each scripted move once. In an 11 minute
headset session it judged only 7 moves. Most story motion never reached the
hooked moves. The follow natives, the dual camera (`CameraModeTwoObjects`) and
the creature follow (`CameraModeFollow`) aim the Zoomers again on every frame.
The clamp made a glide of its own. It aimed the Zoomers at the clamped point
with the time the move had left. Float rounding moved that point, so it aimed
again on about half of all frames for 20 seconds.

`script_camera.cpp` now changes only the camera that is drawn. The game camera
moves as the game moves it, so `HAS_CAMERA_ARRIVED`, script timing and save
games do not change.

1. **Who owns the camera.** `head_camera.cpp` asks at the top of its
   `UpdateCamera` hook. For the main call at the end of `GCamera::Update`
   (return `0x00442627`), the mode on top of the stack (`GCamera+0x28`, top
   index at `+0x58`) decides. The player's modes are not held. Every story
   mode is held, and the creature follow is held when
   `script_camera_follow=1`. The hand demo's own call (return `0x005DB125`) is
   always held.
2. **What the shot is.** "Cut at the start" (1) shows the Zoomer
   destinations, so the view cuts to where a move ends. "Cut at the end" (2)
   shows the camera where it is and cuts when it stops. It became the
   default after the headset tried both, see "Tuned in the headset".
3. **Not too close.** The shot was kept 60 units from its focus and 15 above
   the ground before any comparison. That was too far, see "Tuned in the
   headset".
4. **When it cuts.** A new owner cuts at once. A shot cuts when the target
   drifts 90 degrees or half the view distance from it. It also cuts when the
   target rests for 300 ms at least 5 degrees or 5 percent away.
5. **The game sees a still camera.** `GCamera::Update` sets its moving flag
   (`+0x74`, read by `GCamera::IsMoving`) when the stored camera is not the
   new pose. While a shot is held that is true on every frame, and the
   interface then clears what looks like the gesture trail. The flag is now
   set from the drawn camera.
6. **No shakes.**

The held pose becomes the camera the game asked for. So the stored camera at
`0x00EA1DB8`, the clouds, the billboard heading and the Hand's ray all agree
with the picture. Ctrl+Shift+F7 cycled the game's camera, a cut at the start
and a cut at the end, until v1.0. The log names each owner change and counts frames held,
cuts by reason and clamps.

### Clouds that reached the eye

The per cloud facing from the first round caused new gray shapes around the
head. The engine makes the dome's pole `20 * A` long. `A` is the scale
(`+0x88`), divided by a flatten term for sky clouds (flag value 2 at `+0x80`,
flatten at `+0x8C`). Seen from below, that is 260 to 1000 units. With the pole
pointed at the eye, any cloud nearer than its pole length pulled its dense
middle down to the eye. The dome also cut through the land on the way. A
simulation over the game's own cloud layout put dense smoke
within 20 m of the eye for 14 percent of camera positions.

The pole is now at most 25 percent of the distance to the cloud
(`cloud_pole_pct`). The pole lies along the line of sight, so this keeps the
outline the player sees and only keeps the geometry away from the eye. The
game camera basis was rejected: the dome is one sided, so the clouds behind
the player would vanish when they turn around.

### The sun glare

The sun disc is fixed in the world. The glare over it is not drawn through the
same camera. A particle effect in the sorted list (`fn_0068B480`) sets its own
camera, then puts the stored camera back with `UpdateCamera`. The glare
(`fn_0086BB60`, called at `0x0082F4C9`) is the next 3D draw after it. That
restore rebuilds the matrices without the per eye offset and roll. The glare
hook now calls `StereoReapplyEye` first, which makes the eye's own camera call
again. It is not proven that this is what the player saw bob. Shift+F12 cycled
the glare through the eye camera, as the game draws it, and hidden, so one
headset session can tell.

### The second eye pass doubled two clocks

The freeze on the second eye pass zeroed `0x00EA9EC0` and `0x00C38134`.
`GGame::Loop` also copies the frame time, in milliseconds, into the game
object twice: `+0x250540` (`0x0054D366`) and `+0x205D48` (`0x0054D380`). The
freeze did not cover them.

- `HelpSystem::Draw3D` runs in both passes and adds `+0x205D48` to the
  widescreen transition, so the bars slid in twice as fast.
- The walk cycle of animals (`sub_0051AF00`, from `Animal::Draw`, `Dove::Draw`
  and `SpellWolf::Draw`) steps the mesh by `+0x250540`. So animals walked at
  twice the speed, and the right eye drew them one step ahead.
- A 50 ms emitter reached from `GLandscape::Draw` adds `+0x250540` too.

Both are now zeroed on the second pass and put back after it. The game writes
zero to them itself on a paused frame, so zero is safe for their readers.

### No black bars in story scenes

`HelpSystem::SetWideScreen` sets the game's state, and the tick in
`HelpSystem::Draw3D` copies the bar share to `0x00EB9950`. Only four readers
use that copy. `LH3DRender::FinishFrame` draws two opaque bars from it at
`0x0082F6AE` and `0x0082F6DD`, and the screen fade draws them again. The mod
sets the copy to zero for the length of `FinishFrame` and puts it back after.
So no bar is drawn, and the logic that reads the state sees no change. The
HelpText region moves up by the bar height, so its bar height function
(`0x005C5780`) now returns zero while the bars are hidden. The subtitles then
stay where they are in play. The bars are hidden only once that hook is in, so
a subtitle never sits above a bar that is not drawn. `cinema_bars=1` in
`bwvr.ini` shows the bars again. There is no hotkey for it.

### The subtitle box and the interface size

`fn_005CCE60` queues the translucent box behind the subtitles inside the help
scope. The FinishFrame rect callback `0x0081E7D0` draws it later, outside that
scope. So the box got the lift and not the interface size, and the letters
left it. The interface size now also applies to the box's draw (`0x0081E3C0`
to `0x0081E58C`).

The size now goes down to 25 percent, and the default is 30. The glyph cache
keeps two copies of each letter, 40 rows and 20 rows. The game picks one from
the size it asks for, before the mod scales. At 30 percent a 77 pixel tooltip
letter is drawn from the 40 row copy at about 1.7 to 1. Whether the cache
pages get mip levels on this backend is not known. The launcher sets the size.

### Menus keep their size

The Options, Controls page shrank to small clusters. Its rows draw their key
and mouse icons with `fn_00447450`, the same function that draws the hint
beside the Hand. So each row made a hint band, and every batch near it got the
hint size of 20 percent. Every SetupBox dialog is drawn by one FinishFrame
callback, `0x006424E0`, which holds the only two calls to `SetupBox::Draw`.
Inside that callback the mod now gives interface batches the interface depth
and nothing else: no hint rule, no size and no subtitle lift.

### The advisors stay where you looked

The two spirits are real 3D meshes. `HelpDudeControl::Update` at `0x005C3A30`
places them in each eye pass at a point in camera space, with the camera's own
axes. `HelpDude::ScreenToWorld` (`0x005BD2A0`) unprojects a screen point
through `0x00EA1D28` and the stored camera. The head camera is in that matrix,
so the spirits rode the player's face, and they rolled with the head.

`spirit_anchor.cpp` gives the spirit code a room camera for the length of
`Update`, `ScreenToWorld` and `WorldToScreen`. It has the game camera
position, world up, and a forward that holds the head's yaw and pitch from
when the spirits came. It swaps `0x00EA1D28`, `0x00EA9E40` and `0x00EA1DB8` in
and puts them back exactly. The mesh is still drawn through the real eye
matrices, so the player can turn to look at the spirits or away from them. The
forward is decided once a frame, so both eyes place the spirits at one world
point. It takes the head's direction again after a jump, a snap turn, a story
camera cut or a recenter. It also does so when the spirits come back after
half a second away. A frame drawn in mono drops the anchor. A long frame does
not. A first version tested the time since the last stereo frame, and after
one hitch the spirits jumped to the gaze.

`HelpDude::Draw` places the spirits' light itself, with a direct call to
`UnprojectScreenPoint` at `0x005C0747`. That call now goes through the room
camera as well, so the shading does not change as the player looks around. The
call site is patched only when its five bytes are the expected ones.

Not done: when a spirit looks at the Hand, the game unprojects the pointer
through the room camera. But the pointer is a pixel of the live view. With the
head turned away, the spirit looks a little beside the Hand.

Ctrl+Shift+F12 switched it until v1.0. `spirit_anchor=0` turns it off.

### Tuned in the headset

The first headset session with these changes settled four values:

- **Interface size, 25 percent.** It is now the default.
- **No subtitle lift.** At 25 percent the interface size already brings the
  subtitles to about 16 degrees below the middle of the view, so they read
  well where the game puts them. The launcher row is gone, and
  `subtitle_lift_pct` defaults to 0.
- **Cut at the end.** It felt better than a cut at the start, so it is now
  the default for the story camera.
- **A little more room, not a fixed distance.** Most shots were too far at 60
  units, and the push back along the view line put some cameras in places
  that made no sense. Now a shot nearer than 60 units moves back 25 percent
  along its own line, but not past 60 units. A shot further out does not
  change, so each shot keeps its framing. The result grows with the distance
  and has no jump. The hard limits only keep the camera 5 units from its
  subject and 5 above the ground.

The ini keys are `script_camera_close_distance` (60),
`script_camera_close_zoom_pct` (125), `script_camera_min_distance` (5) and
`script_camera_min_height` (5).

### Subtitles lower, and out of the hint

Two subtitle faults from the next session:

- **Too high.** At 25 percent the interface size put the subtitles about 16
  degrees below the middle of the view, in the middle of the work area. Now,
  after the size, the box and its letters move down together until the middle
  of their band is 35 degrees below the middle of the view (`subtitle_deg`, 0
  leaves them where the size puts them). The move uses the vertical term of
  the projection, so it holds at any render size. Ctrl+Shift+F10 stepped it
  through 25, 30, 35, 40 and 45 degrees. That is now the Shift F12 test key,
  see "Keys for v1.0". The interface size is now set in the launcher only.
- **The box went away and the letters flickered when the player looked up.**
  With the head up, the Hand sits low in the picture, in the rows where the
  game draws its subtitle box before the mod moves it. The hint rule takes
  anything in the Hand hint's rows as part of the hint. So the box and its
  letters got the hint size about the Hand, and letters that looked like a
  copy of each other were dropped. Subtitle batches are now never part of a
  hint. The rows are tested before any transform, so the test is exact.

The report line counts both:

```
ui depth     : N subtitle batches placed at 35 degrees, M kept out of the Hand's hint
```

## Keys for v1.0

Before v1.0, most mod keys switched a choice live, so the person in the
headset could feel each choice without a rebuild. The headset sessions settled
those choices, and the values they found are now the defaults. So v1.0
removes those keys. Their ini keys stay, so a test can still set an old value
by hand. Three of them are the exception: the launcher deletes
`eye_separation_milli` and `freeze_time` on every save, and it writes
`stereo=1`. Set those three after the last save, or start the game without
the launcher.

These keys are left:

| Key | For | Effect |
| --- | --- | --- |
| F11 | Everyone | The direction of the head becomes straight ahead. |
| F12 | `test_keys=1` | Saves a picture of each eye beside `runblack.exe`, and one of the frame before either eye pass. |
| Shift F12 | `test_keys=1` | The subtitles step through 25, 30, 35, 40 and 45 degrees below the middle of the view. This key stays until the headset tests settle the subtitle height. |
| Ctrl F12 | `test_keys=1` | Who owns the pointer: the mouse and the controllers, the controllers only, or the mouse only. Added with step M2, see `docs/controllers.md`. |
| Ctrl Shift F11 | `test_keys=1` | The interface panel (Track 3, see `docs/ui-layer.md`) on and off, when `ui_panel` is not 0. The log names the state. |
| Shift F11 | `test_keys=1` | The grab of the world (step M7, see `docs/grab-world.md`): as set, one to one, or off. The log names the mode. Press it with the mouse at rest. |
| Ctrl Shift F12 | `test_keys=1` | The sound (Track 5, see `docs/audio.md`, 14.2) has five steps, and each one takes one more change away: all as set, the same distances at each zoom, QMixer's own panning, the game camera's rule for which sounds play, and the ears on the game camera (the old sound). The log names the step. Press it with the mouse at rest, because Ctrl and Shift together start the game's zoom to the Hand while the mouse moves. |
| Ctrl F11 | `play_mode=3` | The table of the tabletop mode goes off and on (see `docs/tabletop.md`, section 10). The log names the state. Press it with the mouse at rest. |

The launcher shows `test_keys` as "Test keys", in a group named "Testing". It
is off by default.

Three more changes came with this:

- **The keys work only while the game is in front.** `GetAsyncKeyState` reads
  the keyboard of the whole system. Before, an F key pressed in a browser
  changed the mod.
- **No mod key fires a game action.** In the game's default bindings
  (`ControlMap::LoadDefaults`, `0x0046F890`), F6 to F9 go to the Challenge
  Room, the Save Game Room, the Options Room and the Library. So the old
  plain F6 to F9 keys were game keys too. F10 reaches Windows as a system
  key, which can open the window menu. F11 and F12 have no game action.
  The game also reads a held Ctrl as its zoom and a held Shift as its tilt,
  and both together start its zoom to the Hand. Each of those needs a mouse
  move, so the recenter key holds none of them. Shift F12 holds one, and it
  is a test key pressed with the mouse at rest.
- **The field of view sweep has no key.** Ctrl F10 started it. In the
  immersive mode it left the render at the game's 70 degrees until the next
  level loaded.

The removed keys, and the ini key for the same choice:

| Old key | Choice | ini key |
| --- | --- | --- |
| F6 | draw probe | `draw_probe` |
| Shift F6 | world steps once a frame | none, always on |
| Ctrl F6 | head tracking | `play_mode` |
| Ctrl Shift F6 | plain sea | `water_simple` |
| F7 | stereo | `stereo` |
| Shift F7 | hint copy | `hint_mode` |
| Ctrl Shift F7 | story camera | `script_camera` |
| F8, F9, F10 | depth, and a reset | `eye_separation_milli` |
| Shift F8, Shift F9 | interface depth | `ui_shift_px` |
| Ctrl F8, Ctrl F9 | hint size | `hint_scale_pct` |
| Ctrl Shift F8 | zoom step | `zoom_step` |
| Ctrl Shift F9 | drag smoothing | `drag_smoothing_ms` |
| Shift F10 | the Hand's exact ray | none, always on |
| Ctrl F10 | field of view sweep | `fov_sweep_after` |
| Ctrl Shift F10 | subtitle height | `subtitle_deg`, and now Shift F12 |
| Ctrl F7 | recenter | none, and now F11 |
| F11 | freeze time on the second pass | `freeze_time` |
| Shift F11 | no Hand prepare on the second pass | `skip_hand_prepare` |
| Ctrl F11 | head position off (3DOF) | none, 6DOF always |
| Ctrl Shift F11 | clouds hold still | none, always on |
| Shift F12 | sun glare | `sun_glare` |
| Ctrl F12 | projection layer | `play_mode`, which uses the projection layer for the immersive mode only |
| Ctrl Shift F12 | advisors in the room | `spirit_anchor` |

The toggle and cycle functions that only a key called are gone too. The
functions that set a value stay.

## Ready to hand out: the v1.0 pass

The mod stopped being a thing on one PC. Four changes make it safe to install
somewhere else.

### The build of the game is checked, once per start

Every address in `bw_addresses.h` belongs to one file: the `runblack.exe` of
the unofficial v1.42 patch, SHA1 `9c9aea96f046911ab1b7e496fae511ed5ebbfefe`,
8,663,040 bytes. The old check only compared the image base, which is
`0x00400000` in every build of the game, so every build passed it.

What that cost, measured against the files in `Downloads`:

| Build | What `0x0082F460` holds | What used to happen |
| --- | --- | --- |
| v1.42 | `LH3DRender::FinishFrame` | Correct |
| 1.00 (retail, and the two changed copies) | `.rdata`, not executable | MinHook refuses, VR turns off, no hooks |
| 1.10, 1.20, 1.30 with the copy protection | encrypted `.text` | A hook writes into code that is decrypted later, so the game crashes |
| 1.10 or 1.30 with the protection removed | unrelated code | A jump lands in the middle of another function. If one frame gets through, about 40 more hooks go into unrelated code, and globals are written at v1.42 addresses. A crash is likely. Rubbish in a saved game is possible. |

`src/common/exe_check.h` hashes the file with a SHA-1 of its own, written out
in the header so no DLL has to load. That matters because it runs from
`DllMain`, under the loader lock. It reads the file in 64 KB pieces and takes
**19.7 ms** on the v1.42 file here, which is nothing against the seconds the
game takes to reach a menu.

A file size alone is not enough. v1.42 is the 1.20 build with the SafeDisc
sections removed and the patch bytes applied, so an unpatched 1.20 stripped
the same way would have the same size. The hash is the test.

On any other build, `DllMain` installs nothing: no gpu bridge, no
`FinishFrame` hook, and `ProxyForceSystemDDraw` makes the proxy load system
DirectDraw rather than dxwrapper. The game then runs as it would with no mod
at all, and `bwvr.log` names the build that was found. The launcher runs the
same check and says the same thing before the player presses Play.

### One set of defaults, and they are the tested ones

`ReadSettings` used to return early when there was no `bwvr.ini`, so the
static values applied and the file's own fallbacks never ran. The two sets
disagreed, so a first run without an ini had a fixed eye separation that
ignored the headset, and the pinned hint copy. The early return is gone, and
`GetPrivateProfileIntA` supplies every default.

Three defaults changed to what the headset settled on:

- `gpu_bridge` is 1. It falls back to the readback on its own when a device
  cannot share a texture, so it is safe everywhere.
- The backend is `dxwrapper\ddraw.dll` when that file is beside the game.
  The old default was system DirectDraw, which cannot go far above 720p.
- `hint_scale_pct` is 20, not 50.

So a fresh install needs no ini at all, and the installer ships none. The
launcher writes one when the player saves.

### The product has a name and a version

`src/common/version.h` holds the name, the version, the publisher and the
support link. The launcher, the mod, the OpenXR instance and both version
resources read it, so there is one place to change. The launcher is now
`Black & White VR Mod.exe`, and `ddraw.dll` carries a version resource, which
is what tells it apart from dxwrapper's stub and from a player's own
DirectDraw fix.

### The launcher stops guessing

It checks all five runtime files, hashes the game, reports a save that
Windows refused, finds the game through `GameDir` when it is not beside it,
and keeps the game's own `ScreenW`, `ScreenH`, `FullScreen` and `ScreenD` in
a `[game_before]` section so the uninstaller can put them back.

### The depth buffer

The game gets a 16 bit Z buffer. This is where it comes from, and how
`depth24=1` changes it (spike 1 of the overlay plan).

**Where dxwrapper makes it.** Dd7to9 does not use the auto depth stencil of
the present parameters (`EnableAutoDepthStencil` is FALSE unless
`DdrawOverrideStencilFormat` is set). The game makes a DirectDraw surface
with the Z buffer caps, and `m_IDirectDrawSurfaceX::CreateD9Surface` calls
`CreateDepthStencilSurface` on the Direct3D 9Ex device (`IDirectDrawSurfaceX.cpp`,
about line 4752). The format comes from the pixel format of the request. The
multisample type comes from `GetMultiSampleTypeQuality`, the same call that
sizes the render target. A failed create gets one retry with
`GetFailoverFormat`, which keeps D24X8 as D24X8.

dxwrapper makes the Direct3D 9 surface late. `CreateSurface` and
`AddAttachedSurface` only make the DirectDraw object. `CheckInterface` makes
the Direct3D 9 surface at the first use, in a later frame. So the game never
sees a failed create of the Direct3D 9 surface.

**Where the game picks the format.** `LH3DRender::OpenD3D` (`0x0082CD80`)
calls `EnumZBufferFormats` with the callback at `0x0082B370`. The callback
keeps the entries with `dwFlags` equal to `DDPF_ZBUFFER` (no stencil) in a
table at `0x00EC7E88`, 32 bytes each, with the count at `0x00ECA674`. The
count is never set back to 0, and the table holds at most 4 entries. The
loop after it asks for the entries in order. It stops at the first one that
makes a DirectDraw surface and attaches it. `0x00C386D8` is 1 when the entry
is 16 bit (and 0 when `0x00ECA5F0` is set). dxwrapper lists D16, D24S8,
D24X8, D32 in that order, each only when `CheckDeviceFormat` accepts it. So
the table is D16, D24X8, D32, and the game takes D16. The game uses no
stencil.

**What the hooks change.** With `depth24=1`, a hook on the callback moves
the D24X8 entry to the front of the table. D24X8 has `dwFlags`
`DDPF_ZBUFFER`, `dwZBufferBitDepth` 32 (offset `0x0C`) and `dwZBitMask`
`0x00FFFFFF` (offset `0x14`). The game then asks for 24 bit by itself.
dxwrapper records the real format, so its own copies and clears stay right.
The game's own code (`0x00C386D8` becomes 0) knows that the buffer is not
16 bit. This also holds after a resolution change, because OpenD3D lists the
formats again each time.

The first build did not work. It read the mask at offset `0x10`, which is
`dwStencilBitDepth` and is 0 for D24X8. So the entry never moved, the log had
no line about D24X8 first, and the backend made D16.

A second hook, on `IDirect3DDevice9::CreateDepthStencilSurface` (slot 29, in
`gpu_bridge.cpp`), sees every depth surface of the device. For a D24X8
request of the game it asks `CheckDeviceFormat`, `CheckDepthStencilMatch` and
`CheckDeviceMultiSampleType` with the render target format and the
multisample type of the request. The game cannot fall back at this point,
because its loop ended frames before. So if one check fails, the hook makes
D16 in place of D24X8 and logs the reason. dxwrapper then holds a D16 surface
that it calls D24X8. After the create, dxwrapper reads the format of a depth
surface only for a depth fill `Blt`, and the game clears with `Clear`. The
hook also keeps the depth of the game's Z buffer for the near plane
(`GpuBridgeDepthBits`). The interface panel makes its own depth surface, and
the hook lets those creates through as asked. This hook needs `gpu_bridge=1`
(the default), because the bridge captures the device. It is in with
`depth24=0` too.

The log lines start with `depth24      :`. One line says if the hook is on.
One line says that the game tries D24X8 first. One line says what the game
asked for and what the backend made. A new line of that kind comes only when
the format or the size changes:

```
depth24      : ON. The game asks for a 24 bit Z buffer (D24X8). If the adapter cannot make it, the mod makes a 16 bit one
depth24      : the game lists D24X8 as entry 1 of its Z formats. It now tries D24X8 first
depth24      : the game asked for D24X8, 2880x2880, multisample type 4. The Z buffer is D24X8, 24 bit
```

**What reads the depth buffer.** Nothing needs 16 bit:
- The sun glare test (`fn_0086BD00`) locks the Z buffer and reads 16 bit
  words (a sample is clear when it is below `0xFDE8`). It does this only when
  `0x00C386D8` is 1. dxwrapper refuses a lock on a video memory depth
  surface, so with 16 bit the lock fails and the test uses only its ray test
  (`fn_00802550`). With 24 bit the game skips the lock and uses the same ray
  test. The result is the same [V, the code at `0x0086BF2B`]. The failed lock
  of each frame also stops.
- The mod's interface panel makes its own depth surface (D24S8 first) and
  does not use the game's.
- `hand_glow.cpp` and `overlay.cpp` only use the depth test of the device,
  so any format works.
- The multisample type does not change.

**The depth steps.** The game writes `z = 1 - near / cz`, so a step of the
Z buffer is `cz^2 / (near * 2^bits)` units. The near plane is the one of
`near_plane.cpp` (see below, and `god-view.md`, section 8). The steps in the
room:

| Scale (units per meter) | Near, 24 bit | 24 bit at 1000 units | 24 bit at 6000 units | 16 bit, the game's 3.5, at 6000 units |
| --- | --- | --- | --- | --- |
| 7.5 (life size) | 0.375 (5 cm) | 21 mm | 0.76 m | 21 m |
| 22.5 | 1.125 (5 cm) | 2.4 mm | 85 mm | 7 m |
| 100 | 3.5 (the game's) | 0.17 mm | 6 mm | 1.6 m |
| 400 | 3.5 | 0.04 mm | 1.5 mm | 0.39 m |
| 4000 | 3.5 | 0.004 mm | 0.15 mm | 39 mm |

At every scale, the 24 bit buffer with a near plane of 5 cm is at least 27
times finer than the old 16 bit buffer with the game's 3.5 units. At the life
size scale the fog ends at 900 units, so the land at 6000 units is not seen.

**The near plane.** `near_plane.cpp` hooks `LandFeature::GetNearClipping`
(`0x005E2F30`) in play modes 0, 3 and 4, where the head moves the camera.
The near plane is at most `near_plane_cm` (5) of the room with a 24 bit
buffer. It is at most `near_plane_cm_16` (10) with a 16 bit buffer, or while
the depth is not known. The scale is the table scale while the table or the
God view owns the view. At other times it is the life size scale (play mode
0, and a story in play mode 3 or 4). The game's value stays when it is
nearer. 0 for a key keeps the game's near plane. Play modes 1 and 2 keep the
game's near plane, because the hands are not in the world there.

Why 10 cm at 16 bit: 0.75 units (10 cm at the life size scale) is the near
plane that the game itself uses with its camera 2.8 units over the land
(`0.3 + 0.16 * h`). At 900 units a 16 bit step is then 16 units. It is 3.5
units with the game's 3.5, and 33 units with 5 cm. So 10 cm keeps a hand at
10 cm and more, and costs half of what 5 cm costs.

What reads the near plane, and why each reader still works:
- The overlay hands (`overlay.cpp`), the glow (`hand_glow.cpp`) and the sea
  disc of the table (`tabletop.cpp`) read `0x00E839E0` when they draw, after
  the hook. They clip and write `z = 1 - near / cz` with the same value as
  the game.
- `GCamera::Update` sets `0x00C3812C` and `0x00C38130` from the value of the
  hook (`0x004424D0`), so `UnprojectScreenPoint` and the water stay in step.
- The adaptive interface depth (`draw_hooks.cpp`, `stereo.cpp`) used the raw
  rhw of the nearest world. With a near plane ten times smaller, all of the
  world fell in the lowest bucket of its histogram, and the interface shift
  went too near. Now the histogram counts `3.5 / cz`, and the shift uses
  `1 / cz`, so the near plane does not change the shift. Geometry nearer than
  3.5 units stays out of the histogram, as before: the real hand and the
  Hand must not pull the interface to the eyes. The log line is now
  `interface    : nearest world N units`.
- The flat interface has z 0 to 0.33. Where it computes `rhw = near / z`
  from its own z, a lower near plane gives a larger z, but never a z below
  0. The world and that interface both get `z = 1 - near / cz`, so their
  order at the same `cz` does not change. Where the interface has a fixed z,
  the world moves back, and the interface is in front more often. So the
  interface stays in front of the world. Only a hand that is nearer than the
  interface can cover it.
- `kNearDepth` (0.9) in `draw_hooks.cpp` is only a filter of a report.
- The game sets a near plane of 0.1 or 0.2 in three places (`0x00460F24`,
  `0x005F8FE1`, `0x0079693C`), outside `GetNearClipping`. The hook does not
  change those.

**Status.** Built, and the math checks pass. The offset of the fix agrees
with the game's own loop, which reads the bit depth at `0x0C` of each entry,
and with the pixel formats of dxwrapper. `depth24=0` was the default until version 1.1.0, which has `depth24=1`. No
desktop run yet, because the PC was in use. To test, use `depth24=1` in the
God view at its widest zoom and at the life size scale. Then read the
`depth24      :` and `near plane   :` lines of `bwvr.log`.
