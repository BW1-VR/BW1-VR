# The start of the game: logos, intro, skip and internet check

This doc covers what the game shows when it starts, how the mod shows the
Lionhead logo in stereo, how a player skips the start, and the internet check
that the mod answers.

Marks: **[V]** means verified in the code or the files. **[I]** means inferred
from the code and not confirmed by a run.

## Summary

- The Lionhead particle logo is real time, not video [V]. It is one function in
  the engine, `RunAlexFX` at `0x005F89F0`. It has its own private frame loop
  and does not use `GGame::Process3dEngine`. Without the mod's stereo hook, the
  mod shows it flat.
- The other startup videos are Bink files. They are pre-rendered and stay flat.
- With `intro_vr=1`, the mod runs `RunAlexFX` one time for each eye, with a
  still camera at the final angle and the head tracked. A controller moves the
  mouse pointer that pushes the particles. `intro_vr` is off by default: in a
  headset some things show double, and nothing moves with the still camera.
- A press of a trigger or of A, B, X or Y skips the logos and the intro (the
  skip, `intro_skip`, on by default).
- The mod answers the game's check for an internet connection itself, so a PC
  whose Windows service `RasMan` does not answer can start the game
  (`net_check`).

Source files: `src/mod/intro_vr.cpp` (the stereo intro), `src/mod/intro_skip.cpp`
and `intro_skip_math.h` (the skip), `src/mod/net_check.cpp` (the internet
check), with small parts in `head_camera.cpp`, `stereo.cpp`, `mod_main.cpp` and
`controllers.cpp`.

## Section map

| Section | Subject |
| --- | --- |
| What runs at startup | The four steps of the start of the game |
| How the logo draws | `RunAlexFX` frame by frame |
| How the mouse pushes the particles | The reads and the maths of the push |
| Why the intro is flat without the hook | What the mod does when `intro_vr=0` |
| The intro in stereo | The design and the code that is built |
| The skip | The press that ends the logos and the intro |
| The internet check | The `RasMan` stop and the answer of the mod (`net_check`) |
| Open work | What is open |

## What runs at startup [V]

| Step | Code | Data | Kind |
| --- | --- | --- | --- |
| 1 | `PlayLogoScreens` `0x00642950`, from `pc_main` at `0x00641D5D` | `Data\logo.bik`, 2 frames, 768 x 512 | Bink video, faded by code |
| 2 | `PlayPreIntroVideo` `0x006426F0`, from `pc_main` at `0x00641D94`. It runs only with no profile, or with `-PREINTROVIDEO` | `Data\pre_intro.bik`, 2515 frames, 768 x 512, 25 fps, plus `audio\music\intro\trailer.sad` | Bink video |
| 3 | `DoLogo` `0x005FA070`, from `GGame::Loop` at `0x0054D060` (first loop, single player only) | `Data\lhlogo.raw` (12288 bytes), `Data\D_SKY.l3d`, `Data\textures\gatheringtext.raw` | **Real time, `RunAlexFX`** |
| 4 | `PSysGlobal::StartAVISequence`, string `data\intro.bik` | `Data\INTRO.bik`, 1601 frames, 640 x 360, 24 fps | Bink video (the story intro, not the logo) |

`RunAlexFX` gets its name from Alex Evans of Lionhead [I, the symbol name is
from the decomp project, and the source path in the binary is
`C:\dev\MP\Black\logo.cpp`]. The Bink files hold flat video, and the mod cannot
make them 3D.

`DoLogo` calls `RunAlexFX` in three modes [V]:

- Mode 0: build the effect. Load the texture and materials, build the sky
  object, fill the particle arrays.
- Mode 1: one frame. `DoLogo` repeats it until the effect sets its done flag
  (`0x00D37CC8`) or a mouse button bit is set in `0x00E85304` [I, the bit
  meaning of `0x00E85304`].
- Mode 2: free everything.

The step 4 movie `intro.bik` is not a part of the start. A script plays it
inside the first story, and the skip does not touch it.

## How the logo draws [V unless marked]

Each mode 1 call does all of this in one function:

1. It reads the frame time `LH3DTech::g_delta_time` (`0x00C38134`) and adds a
   scaled copy to an effect clock at `0x00D37CCC`. The clock sets the fade in
   and the fade out (constants 12, 15, 31 and 33 in the code) and the camera
   motion.
2. It works out a camera position and a focus point from the clock with `sin`
   and `cos`, and blends them.
3. It calls `LH3DRender::StartFrame` `0x0082F0E0`. `StartFrame` writes
   `g_delta_time` from the tick count, with a minimum of 1. It runs at
   `0x005F933E`, after the clock read at `0x005F9050` and before the spring and
   push read at `0x005F96F0`.
4. It calls `LH3DTech::UpdateCamera` `0x00819920` itself, at `0x005F934B`. The
   first call is in mode 0, at `0x005F8A84`, with the camera at `(0, 0, 100)`.
5. It draws the sky object (`D_SKY.l3d`) through the normal `LH3DObject` path.
6. It moves the particles, then draws 4096 of them, one call to
   `LH3DSprite::Draw` `0x00840530` each, in a loop at `0x005F9959`. Each
   particle is a 3D sprite record of `0x34` bytes. The engine projects it with
   the normal camera matrix at `0x00EA9E40` [V for the loop, I for the particle
   count from the `0xC000` byte bound divided by 12].
7. It draws 2D boxes and a text line (`SetupThing::DrawBox`,
   `GatheringText::DrawTextRaw`). The text and the boxes are the fades and a
   caption.
8. It calls `LH3DRender::FinishFrame` `0x0082F460` and `LHScreen::Flip`
   `0x007DE090`.

`DoLogo`, `PlayLogoScreens`, `PlayPreIntroVideo` and `RunAlexFX` are all
`FinishFrame` callers outside `Process3dEngine` (`ui-layer.md`, section 1.4).

The camera path of the game: the camera orbits the logo, then rests at
`(0, 0, 100)` looking at `(0, 0, 0)` from clock 26 to clock 30 (the same pose as
the build call). From clock 30 it flies through the logo to `(0, 0, -200)`. The
effect sets its done flag when the fade factor reaches -2 (clock about 34.7) or
when the window is minimized.

The logo is a flat image of about 75 units at 100 units of distance (110
degree render).

The logo shape comes from `lhlogo.raw`. It is 12288 bytes, which is 64 x 64 x 3,
so one grayscale image with three equal bytes per pixel [I]. The code reads the
file with `LHLoadData` and turns bright and dark pixels into particle home
positions [I]. Each particle then flies to its home like a spring, and the
sprite color is black or white [I from the color and the spring code].

At `0x005F8AF0` a tick count test makes the code read a file from the temp
folder for 1 launch in 4 [V]. It appears to load a replacement 64 x 64 image
[I]. The mod does not use it.

## How the mouse pushes the particles [V for the reads, I for the maths]

The push code sits inline in `RunAlexFX` mode 1, from `0x005F95DC` to
`0x005F993A`. It reads these `LHMouse` fields (see `controllers.md`):

| Address | Field | Use in the effect |
| --- | --- | --- |
| `0x00E852C0`, `0x00E852C4` | pointer x, y in window pixels (the mod maps them to render pixels) | Copied to the local pointer of the frame. |
| `0x00E852E4`, `0x00E852E8` | relative motion from DirectInput, summed | Smoothed into `0x00D37CB0` and `0x00D37CB4`, then cleared. A lagging cursor, clamped to +20 and -20. |
| `0x00E85300` | wheel delta | Clamped to plus or minus 240. It scales a strength value at `0x00D34A8C` by an exponent, kept between 0.1 and 1. |
| `0x00E85304` | button bits | Read by `DoLogo`, not by the push. |

`LHMouse::UpdateDeltaPos` `0x007E4810` is called at `0x005F95DC` in the same
frame, so the effect reads the raw DirectInput delta path as well as the window
pointer [V].

Then, for each of the 4096 particles [I]:

1. `LH3DTech::ProjectPoint` `0x00819390` (call at `0x005F9779`) gives the
   screen x, y and depth of the particle, against the matrix at `0x00EA9E40`.
2. If the particle is more than one tenth of the screen width from the pointer,
   the code skips it. The radius is `width / 10`, from `0x00E85058`.
3. Otherwise the weight is one minus distance over radius.
4. `fn_0081B370` (call at `0x005F987A`) turns the pointer x, y and the particle
   depth into a world point. This is an unproject [I from its arguments and the
   use of its result].
5. The code moves the particle toward that world point and adds a velocity
   term, both scaled by the frame time and the weight.

After the push, one loop applies damping and the spring pull to the home
positions, and the next loop draws. The push is a screen space disc that
reaches into depth at the depth of the particle. It is not a sphere in world
space.

## Why the intro is flat without the hook

- The mod renders two eyes only inside `h_Process3dEngine` (`stereo.cpp`).
  `RunAlexFX` never calls `Process3dEngine`, so no eye pass runs.
- After 150 ms with no stereo frame, `StereoIdle()` is true. `h_FinishFrame` in
  `mod_main.cpp` then sends the whole render target to both eyes as one flat
  picture (`ui-layer.md`, section 1.4).
- `h_UpdateCamera` in `head_camera.cpp` passes the call straight through while
  `g_world_is_up` is false. The game sets it inside `Process3dEngine`, which
  has not run yet at the logo. So the head does not move the camera.

## The intro in stereo

The key `intro_vr` turns it on. The default is 0, and with 0 nothing is hooked.
The core idea is to run the `RunAlexFX` of the game twice per frame, one time for
each eye, with the clock and the simulation frozen on the second call. It is the
same rule that `stereo.cpp` uses for `Process3dEngine`. The mod does not rebuild
the effect.

### How a frame runs

`h_RunAlexFx` passes modes 0 and 2 through. For mode 1 with a headset (or
`stereo_headless`) it runs `RunPair`:

1. `XrScreen::BeginFrame`. If it fails, or the runtime does not want a picture,
   the effect runs one time and flat.
2. First frame only: the head view setup of `stereo.cpp`
   (`StereoIntroPrepareView`), and the room scale. `intro_distance_cm` (default
   300) gives 33.3 units per meter, so the logo is about 2.2 m wide at 3 m. Mode
   2 puts the old scale back.
3. The head pose for the frame, and the declared render pose for both eyes.
4. `HeadCameraIntroFrame`: one head for both eyes.
5. Eye 0 with the real time, eye 1 with time 0. Each `UpdateCamera` call from
   `0x005F934B` goes through `IntroCameraPose` (the still camera) and the
   normal head code of `head_camera.cpp`. Then `IntroCameraBuilt` moves the
   matrices half an eye, as the scene does.
6. `h_FinishFrame` calls `IntroFinishFrame` after each eye, and it copies the
   picture to the headset. The mono path does not run.
7. `EndFrame`, then `StereoMarkPresented`, so `StereoIdle()` stays false.

Mode 2 calls `HeadPoseRecenter`, so the world starts as it did before.

### Why the freeze needs a hook on StartFrame

`StartFrame` writes `g_delta_time` after the clock read, so a frozen delta time
set before the call stops only the clock. The mod hooks `StartFrame` and writes
`g_delta_time` again for that one call site. The first eye gets the real frame
time, so the two calls together match real time.

The mouse smoothing (`0x00D37CB0`, `0x00D37CB4`), the push strength
(`0x00D34A8C`) and the C runtime random seed change on each call and do not
scale with time. The mod saves them and puts them back around the second eye.
Each sprite gets `rand` for its angle, so the seed matters. [V] (The random
jitter is `Random` at `0x0081D180`, per sprite, at `+0x14`.)

With `g_delta_time` at zero, the spring, the damping and the push scale to zero,
because each one multiplies by a value that comes from the delta time [I from
the constants at `0x005F8FE1`, `0x005F9054`, `0x005F96F0`]. If any other state
of `RunAlexFX` advances by something that is not `g_delta_time` (for example
`GetTickCount` or a frame counter), the second call moves it.

### The still camera

`intro_still=1` holds the camera at the rest pose `(0, 0, 100)` looking at
`(0, 0, 0)` for the whole intro (`kStillPosition`, `kStillFocus` in
`intro_vr.cpp`). The render frustum of about 110 degrees limits how far to
the side and behind the player particles show [I]. Particles behind the head
do not draw.

With the still camera nothing moves in the headset: the player sees the
formed logo from the start. The motion of the intro of the game is the
motion of its camera. The orbs stay where they are, the camera goes between
them, and then it goes away to the place from which the logo shows. This
was seen in a headset, and it is not read in the code yet. `intro_still=0`
gives the camera of the game, with its turns.

### The ways back to the flat intro

- `intro_vr=0`, no headset and no `stereo_headless`, stereo off.
- The bytes at `RunAlexFX` or at its three calls do not match. The mod makes no
  hook.
- 3 frames in a row without the camera call, 4 frames in a row over 250 ms, or
  3 frames in a row where the picture did not reach the headset. The mod logs
  the reason, puts the room scale back, and the rest of the intro is flat.
- The done flag is set. The mod skips the second eye.

If a click ends the logo in the middle of a stereo pair, the flag that says "in
the intro" is cleared when mode 2 runs.

### Ini keys

| Key | Default | Meaning |
| --- | --- | --- |
| `intro_vr` | 0 | The intro in stereo |
| `intro_still` | 1 | Hold the camera at the rest pose. 0 uses the game's orbit |
| `intro_distance_cm` | 300 | Distance of the logo plane from the head |
| `intro_controller` | 1 | Run the controller frame, so the aim moves the pointer |
| `intro_probe` | 0 | Log the camera of the game each second and the particle and pointer checks |

### The controller in the stereo intro

- `ControllersFrame` runs once for each pair. With the pointer owned by the
  controller, `pointer_map.cpp` writes the pixel to `0x00E852C0`, and the
  effect reads it as it reads the mouse.
- `Usable()` in `controllers.cpp` is false while the intro draws in stereo
  (`IntroBlocksButtons`). No controller button can reach `SetButtons`, so the
  trigger cannot click, and a click cannot end the intro.
- The menu button on either controller sets the done flag.
- The push is worked out in the view of the first eye (the left one).

The pointer is a shadow touch: a particle that overlaps the point of the
controller in the view moves. The push reaches every particle along the sight
line inside the pixel disc, whatever the depth.

### What a headset showed, and what is open

The intro in stereo is not finished, so `intro_vr` stays 0.

- Some things show double, here and there. The cause is not known. Suspects:
  the fade boxes and the name text at zero disparity, and the sprite depth.
  If the fade box looks like a wall that covers the whole view, turn it off
  in VR and fade with the sprite color alpha alone [I].
- Nothing moves with the still camera (see "The still camera"). Wanted:
  motion that shows the depth and does not make the player sick. One idea is
  a camera that moves along the path of the game and does not turn. That is
  motion that the player did not make, so it needs a decision first. A way
  that keeps the rule: the orbs are a small model in front of the player,
  the path of the game moves the model, and the room stays still.

Not known yet:

- The comfort of the scale (logo about 2.2 m wide at 3 m).
- The frame rate with two effect calls and the real `Flip` skip. On the desktop
  each call includes a `Flip` that waits for the display. With a headset
  `PresentWatchSkipDesktop` skips them.
- Whether the aim of a real controller pushes the particles as expected. The
  push uses the left eye view.
- The menu button skip.

## The skip

A player in the headset has no Esc key or mouse click for the logos. The skip
gives the controller a way to end the start of the game. The code is
`src/mod/intro_skip.cpp` and `intro_skip_math.h`, one call in `h_FinishFrame` of
`mod_main.cpp`, and `ControllersButtonBits` at the end of `controllers.cpp`.

### What the game accepts [V]

The start of the game has three parts that draw their own frames. Each one
tests the input itself:

| Part | Code | What ends it |
| --- | --- | --- |
| The logo screens: three screens of `Data\logo.bik` | `PlayLogoScreens` `0x00642950` | The test `0x00642930`: the last key is Esc, or a mouse button bit is set (`0x00E85350`, bits 0 and 1). A press while a screen fades in or shows starts its fade out. A press in the fade out ends the screen. `InputReset` `0x005FA000` clears the key and the bits after a press. So one press skips one screen at the most |
| The video before the intro, only with no profile or with `-PREINTROVIDEO` | `PlayPreIntroVideo` `0x006426F0` | The last key is Esc (`0x006428C5`). A mouse click does not end it |
| The Lionhead logo | `DoLogo` `0x005FA070` | A mouse button (`0x00E85304`, bits 0 and 1), each key (`0x005FA09F`: the last key is not 0 after a frame), or the done flag `0x00D37CC8`. `DoLogo` also clears `0x00E85474` before the loop and reads it after each frame |

**The last key** is one field. `LHKeyboard::ProcessKeyboard` `0x007DCAF0` writes
the scan code of each key that goes down to `this + 0x108` (`0x007DCBD3`). The
keyboard object is at `0x00E8536C`, so the field is `0x00E85474`. Esc has the
scan code 1. So `0x00E85474 == 1` is what an Esc press gives each of the three
parts.

### What the mod does

1. `h_FinishFrame` calls `IntroSkipFrame` after each `FinishFrame`, with the
   address that called `FinishFrame`. Each part calls `FinishFrame` from its own
   code, so that address says which part plays (`intro_skip::PartOf`). The mod
   hooks nothing more. After the intro, the call costs one test of an address
   for each frame.
2. The buttons are the two triggers and A, B, X and Y. The grip is not one of
   them, because a player holds it with no intent.
3. `intro_skip::Step` finds a new press: a button that is down now, was up in the
   frame before, and is on a hand that was awake in the frame before. So a
   button that is down when the intro starts, or when a controller wakes, does
   not skip.
4. A press writes 1 (Esc) to the last key. With `intro_skip=1`, the mod writes it
   again in each frame of a part until the intro is over. So one press skips all
   that is left: the logo screens end in a few frames each, and the Lionhead
   logo ends in its first frame. With `intro_skip=2` the mod writes it one time,
   so one press is one Esc, as on a monitor.
5. The press holds until a frame that is no part comes after the skip of the
   Lionhead logo, or for 120 s at the most. Between the parts the game loads,
   and those frames get nothing.

The mod does not send a key message and makes no call into the game. It writes
the one field that the three tests read. `InputReset` clears it after each part.

With `intro_vr=1` the Lionhead logo is in stereo. The trigger still does not
click there (`IntroBlocksButtons`), and the menu button still sets the done
flag. The trigger and the face buttons also end the logo through the skip.

### The controllers at the start

- `StartVrOnce` starts the session at the first `FinishFrame` of the game, which
  is a frame of the first logo screen.
- The mono path of `h_FinishFrame` sends each frame of the intro to the headset
  with `SubmitTexture`. That calls `XrScreen::BeginFrame`, and `BeginFrame` reads
  the controllers (`SyncInput`) for each frame [V]. So no controller code of
  `controllers.cpp` is needed, and `IntroSkipFrame` reads the hands of
  `XrScreen`.
- The runtime gives input only when the session has the focus. So the first
  moments of the first logo screen have no hands, and a press there does nothing
  [I, not measured in the headset].
- `ControllersButtonBits` gives the hands of `controllers.cpp` too. Those are the
  fake controller of a desktop run, and they change only where `ControllersFrame`
  runs: in the Lionhead logo with `intro_vr=1`.

### Keys

| Key | Default | Meaning |
| --- | --- | --- |
| `intro_skip` | 1 | 1: a press of a trigger or of A, B, X or Y skips all that is left of the logos at the start. 2: a press is one Esc, so it skips one part. 0: off |
| `intro_skip_test_frame` | 0 | A test: at this frame of the intro, a press comes that no hand made. The launcher removes the key from `bwvr.ini` on each save |

### The log

The tag is `intro skip`:

- The key at the first frame of the game.
- `the logo screens play now`, `the Lionhead logo plays now`, and `... over after
  N frames` when a part ends. So a log shows how long each part was.
- `a press of the right trigger skips the logo screens, at frame N of the
  intro`, one line for each press that skips.
- `the intro is over. N frames got Esc` when the press holds no more.

### Tests

The math test (`intro_skip_checks.h`, 26 checks) covers the part for each caller
of `FinishFrame`, a button that is down when the intro starts, a controller that
wakes with a button down, one press that holds to the end of the Lionhead logo
and not past it, the limit of 120 s, one Esc for one press with `intro_skip=2`,
and a press outside the intro.

For a desktop run, `intro_skip_test_frame=30` sends a press at frame 30. The log
then shows the logo screens and the Lionhead logo end early, and the game goes on
to the land and to its start dialog. Keep the mouse and the keyboard still
during a run, because the logo ends on each key and each mouse button.

Each run of the scripts in `bwvr/tools/desk` has `intro_skip_test_frame=5`, so
a run does not wait for the logos (`tools/desk/README.md`, "A fast start"). A
run that tests the logos gives the key the value 0, or a frame of its own.

**Needs the headset:** a press of a real controller. The hands come from
`XrScreen` there, and no desktop run has them. Also how early in the first logo
screen the runtime gives input.

## The internet check

**What a player sees.** The logo screens end, the window keeps the last picture,
and Windows shows the game as "not responding". Nothing more comes in
`bwvr.log`. This is a fault of the plain game on a PC where one service of
Windows does not answer.

**The cause [V].** `LHMultiplayerR.dll` has a function
`LHCheckForInternetConnection`. The game calls it on its main thread when the
logo screens are over. It loads `rasapi32.dll` with `LoadLibraryA`, gets
`RasEnumConnectionsA` and `RasEnumDevicesA` with `GetProcAddress`, and calls
them. They ask the Remote Access Connection Manager of Windows (the service
`RasMan`) for the dial-up and VPN connections and for the modems. Each call is an
RPC with no time limit. When the service does not answer, the stack of the main
thread is:

```
ntdll!NtAlpcSendWaitReceivePort
RPCRT4!NdrClientCall2
RASMAN!RasConnectionEnum
rasapi32!RasEnumConnectionsA
LHMultiplayerR!LHCheckForInternetConnection+0xb8
```

`rasdial.exe` of Windows waits in the same way, with no game. The last lines of
the log are then the sound start of the game, so the sound can look like the
cause. It is not.

**What the mod does (`net_check.cpp`).** The game needs the answer only for its
internet features of 2001 (the weather of the real world, its servers), and
those have no server now. So the mod gives the answers itself: no connection and
no modem. That is what Windows answers on a PC with no dial-up connection and no
VPN. The mod writes its own function in the import slot of `GetProcAddress` of
`LHMultiplayerR.dll`. The DLL asks for four names (`RasEnumConnections`,
`RasEnumConnectionsA`, `RasEnumDevices`, `RasEnumDevicesA`), and for each it gets
a function of the mod that gives 0 entries. Each other name goes to the real
`GetProcAddress`. The mod does this before the game starts (`EarlyHooks`), with
`vr=0` too. Both answers are needed: the game asks for the modems after the
connections.

| Key | Default | Meaning |
| --- | --- | --- |
| `net_check` | 0 | 0: the mod answers "no connection" at once. 1: the call of the game goes to Windows, as in the plain game |

**The log.** One line at the start says which way is on. The first time the game
asks, two more lines say "the game asked Windows for its dial-up and VPN
connections ... The mod answered: none" and the same for its modems.

**To get a PC out of this state** without the mod: start Windows again, or start
the service "Remote Access Connection Manager" again as an administrator.

## Open work

- A true 3D push (L3b). The pointer push of the stereo intro is a screen space
  disc. A sphere push needs two hooks keyed on the return address inside
  `RunAlexFX`: `ProjectPoint` `0x00819390` with return address `0x005F977E`
  (return "not visible", any value except 2, when the particle is farther than a
  radius from the hand point) and `fn_0081B370` with return address
  `0x005F987F` (return the 3D point of the hand instead of the unprojected mouse
  point). `ControllerDirectHand` in `hand_direct.cpp` gives the 3D point.
  `ProjectPoint` runs very often across the engine, so a hook must check the
  return address first, or the two call sites (5 bytes each) can be patched
  directly. A second way is to change the particle arrays (`0x00D34A98`,
  `0x00D34A94`, `0x00D34A90`) in place after the original returns. It needs a
  map of the strides first [I]. A short haptic pulse when a particle moves is an
  easy extra.
- The faults of the stereo intro in a headset (see "What a headset showed,
  and what is open").
- Not verified: the exact particle count and array strides (the `0xC000` bound
  and the strides `0x34` and 12 suggest 4096 particles [I]), the push maths in
  detail, and that `0x00E85304` holds the mouse button bits.
- The Bink videos (`PlayLogoScreens`, `PlayPreIntroVideo`) stay flat on the
  existing flat path. A small quad in the room is the right home for them.
- Do not ship `lhlogo.raw`, the Bink files or any Lionhead art. The mod reads
  them from the install of the player.
