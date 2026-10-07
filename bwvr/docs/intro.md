# The Lionhead intro in VR (Track 6, step L1)

Research and design only. No source file changed, no build, no game run.
Everything below comes from the code and the data files. Nothing was seen in a
headset or in a running game.

Marks: **[V]** means verified in the code or the files. **[I]** means
inferred from the code and not confirmed by a run.

## 1. The short answer

The particle logo is real time, not video [V]. It is one function in the
engine, `RunAlexFX` at `0x005F89F0`. It has its own private frame loop and
does NOT use `GGame::Process3dEngine`. So the mod shows it flat. It can go
into stereo with a small extra hook, and the mouse push maps well onto a
controller. The work is realistic, and it is smaller than Tracks 1 to 5.

The other startup videos are Bink files. They are pre-rendered and stay flat.

## 2. What runs at startup [V]

| Step | Code | Data | Kind |
| --- | --- | --- | --- |
| 1 | `PlayLogoScreens` `0x00642950`, from `pc_main` at `0x00641D5D` | `Data\logo.bik`, 2 frames, 768 x 512 | Bink video, faded by code |
| 2 | `PlayPreIntroVideo` `0x006426F0`, from `pc_main` at `0x00641D94`. It runs only with no profile, or with `-PREINTROVIDEO` | `Data\pre_intro.bik`, 2515 frames, 768 x 512, 25 fps, plus `audio\music\intro\trailer.sad` | Bink video |
| 3 | `DoLogo` `0x005FA070`, from `GGame::Loop` at `0x0054D060` (first loop, single player only) | `Data\lhlogo.raw` (12288 bytes), `Data\D_SKY.l3d`, `Data\textures\gatheringtext.raw` | **Real time, `RunAlexFX`** |
| 4 | `PSysGlobal::StartAVISequence`, string `data\intro.bik` | `Data\INTRO.bik`, 1601 frames, 640 x 360, 24 fps | Bink video (the story intro, not the logo) |

`RunAlexFX` gets its name from Alex Evans of Lionhead [I, the symbol name is
from the decomp project, and the source path in the binary is
`C:\dev\MP\Black\logo.cpp`]. The Bink header fields above were read from the
files. The Bink files hold flat video, and the mod cannot make them 3D.

`DoLogo` calls `RunAlexFX` in three modes [V]:

- Mode 0: build the effect. Load the texture and materials, build the sky
  object, fill the particle arrays.
- Mode 1: one frame. `DoLogo` repeats it until a mouse button bit is set in
  `0x00E85304` or the effect sets its own done flag at `0x00E85474`.
- Mode 2: free everything.

A click on either main mouse button therefore ends the logo [I, the bit
meaning of `0x00E85304`]. Keep this in mind for L3.

## 3. How the logo draws [V unless marked]

Each mode 1 call does all of this in one function:

1. It reads the frame time `LH3DTech::g_delta_time` (`0x00C38134`) and adds a
   scaled copy to an effect clock at `0x00D37CCC`. The clock sets the fade
   in and the fade out (constants 12, 15, 31 and 33 in the code) and the
   camera motion.
2. It works out a camera position and a focus point from the clock with `sin`
   and `cos`, and blends them.
3. It calls `LH3DRender::StartFrame` `0x0082F0E0`.
4. It calls `LH3DTech::UpdateCamera` `0x00819920` itself, at `0x005F934B`.
   The first call is in mode 0, at `0x005F8A84`, with the camera at
   `(0, 0, 100)`.
5. It draws the sky object (`D_SKY.l3d`) through the normal `LH3DObject` path.
6. It moves the particles (section 5), then draws 4096 of them, one call to
   `LH3DSprite::Draw` `0x00840530` each, in a loop at `0x005F9959`. Each
   particle is a 3D sprite record of `0x34` bytes. The engine projects it
   with the normal camera matrix at `0x00EA9E40` [V for the loop, I for the
   particle count from the `0xC000` byte bound divided by 12].
7. It draws 2D boxes and a text line (`SetupThing::DrawBox`,
   `GatheringText::DrawTextRaw`). The text and the boxes are the fades and a
   caption.
8. It calls `LH3DRender::FinishFrame` `0x0082F460` and `LHScreen::Flip`
   `0x007DE090`.

`disasm.py --callers 0x0082F460` and the decomp symbol list agree with
`ui-layer.md` section 1.4: `DoLogo`, `PlayLogoScreens`, `PlayPreIntroVideo`
and `RunAlexFX` are all FinishFrame callers outside `Process3dEngine`.

The logo shape comes from `lhlogo.raw`. It is 12288 bytes, which is 64 x 64 x
3, so one grayscale image with three equal bytes per pixel [I]. The code reads
the file with `LHLoadData` and turns bright and dark pixels into particle home
positions [I]. Each particle then flies to its home like a spring, and the
sprite color is black or white [I from the color and the spring code].

There is also a side effect worth a line: at `0x005F8AF0` a tick count test
makes the code read a file from the temp folder for 1 launch in 4 [V]. It
appears to load a replacement 64 x 64 image [I]. The mod need not care.

## 4. Why the mod shows it flat today [V for the calls, I for the picture]

- The mod renders two eyes only inside `h_Process3dEngine` (`stereo.cpp`).
  `RunAlexFX` never calls `Process3dEngine`. So no eye pass runs.
- After 150 ms with no stereo frame, `StereoIdle()` is true. `h_FinishFrame`
  in `mod_main.cpp` then sends the whole render target to both eyes as one
  flat picture (`ui-layer.md` 1.4).
- `h_UpdateCamera` in `head_camera.cpp` passes the call straight through
  because `g_world_is_up` is false. The game sets it inside `Process3dEngine`
  (`stereo.cpp:825`), which has not run yet at the logo. So the head does not
  move the camera either.

## 5. How the mouse pushes the particles [V for the reads, I for the maths]

The push code sits inline in `RunAlexFX` mode 1, from `0x005F95DC` to
`0x005F993A`. It reads these `LHMouse` fields (`controllers.md` table):

| Address | Field | Use in the effect |
| --- | --- | --- |
| `0x00E852C0`, `0x00E852C4` | pointer x, y in window pixels (the mod maps them to render pixels) | Copied to the frame's local pointer. The mouse position. |
| `0x00E852E4`, `0x00E852E8` | relative motion from DirectInput, summed | Smoothed into `0x00D37CB0` and `0x00D37CB4`, then cleared. A lagging cursor, clamped to +20 and -20. |
| `0x00E85300` | wheel delta | Clamped to plus or minus 240. It scales a strength value at `0x00D34A8C` by an exponent, kept between 0.1 and 1. |
| `0x00E85304` | button bits | Read by `DoLogo`, not by the push. |

`LHMouse::UpdateDeltaPos` `0x007E4810` is called at `0x005F95DC` in the same
frame. So the effect reads the raw DirectInput delta path as well as the
window pointer [V].

Then, for each of the 4096 particles [I]:

1. `LH3DTech::ProjectPoint` `0x00819390` (call at `0x005F9779`) gives the
   particle's screen x, y and depth, against the matrix at `0x00EA9E40`.
2. If the particle is more than one tenth of the screen width from the
   pointer, the code skips it. The radius is `width / 10`, from
   `0x00E85058`.
3. Otherwise the weight is one minus distance over radius.
4. `fn_0081B370` (call at `0x005F987A`) turns the pointer x, y and the
   particle depth into a world point. This is an unproject [I from its
   arguments and the use of its result].
5. The code moves the particle toward that world point and adds a velocity
   term, both scaled by the frame time and the weight.

After the push, one loop applies damping and the spring pull to the home
positions, and the next loop draws.

The push is therefore a screen space disc that reaches into depth at the
particle's own depth. It is not a sphere in world space.

## 6. Design

The goal from the author: a still camera at the final angle, the logo forming
around and in front of the player, and a push by the controller.

### 6.1 The core idea

Do not rebuild the effect. Run the game's own `RunAlexFX` twice per frame, once
per eye, with the clock and the simulation frozen on the second call. This is
the same rule that `stereo.cpp` uses for `Process3dEngine` (its comment 1 on
animations that advance per call). Then set the camera inside the hook on
`UpdateCamera`, using the return address to know the call comes from the logo.
The mod already does this kind of gate for the story camera
(`script_camera.cpp`, `ScriptCameraRenderPose`).

### 6.2 L2: stereo, head tracking, still camera

Four hooks or changes. Each one is small.

1. **Hook `RunAlexFX` `0x005F89F0`.** It is `cdecl` with one int argument.
   For mode 1 only, when a headset runs:
   - Set a flag "in the intro".
   - Call the original for eye 0.
   - Freeze `g_delta_time` and the two other time globals that
     `Process3dEngineBody` freezes, set them to 0, and call the original for
     eye 1.
   - Put the times back.
   Modes 0 and 2 pass through.
   With `g_delta_time` at zero the clock does not move, the spring and the
   damping do not change, and the push scales to zero, because each one
   multiplies by a value that comes from the delta time [I from the constants
   at `0x005F8FE1`, `0x005F9054`, `0x005F96F0`]. This is the main thing to
   confirm in the first test. If it is wrong, particles will run at 2x speed.
2. **Open the head gate for the intro.** `g_world_is_up` gates the head camera
   in `h_UpdateCamera`. Add an "intro" branch keyed on return addresses
   `0x005F934B + 5` (the per frame call) and `0x005F8A84 + 5` (the mode 0
   call). It builds the eye camera from a fixed pose and the head pose.
3. **The still camera.** Replace the position and focus that `RunAlexFX`
   passes with a fixed final pose. To find the final pose without decoding the
   `sin` and `cos` blend, log the two `UpdateCamera` arguments and the clock
   at `0x00D37CCC` once at the end of a normal run. The pose after the
   camera blend ends (the clock about 30 or more [I]) is the final angle.
   Then store it as two constants. Do this measurement as the first job of
   L2.
4. **Per eye offset.** The simplest method: move the position argument by half
   the eye separation along the camera's right vector. Then call the original
   `UpdateCamera`. This needs no matrix edits. The full family of matrices in
   `eye_camera.cpp` is only needed if a sprite or the sky reads a stale
   matrix. Check in a headset that the sky and the particles agree, because
   `eye_camera.cpp` records that shadows and sprites read other matrices.

Then pick the grab point. `FinishFrame` and `Flip` run inside `RunAlexFX`, so
`Process3dEngine` will not grab the picture. Use the `h_FinishFrame` path in
`mod_main.cpp`: when the flag "in the intro" is set, copy the frame to eye N
(`SetEyeImage`) instead of sending it flat. `FrameSourceBegin` and
`EndFrame` are the pieces to reuse. Set `g_last_present_tick` after each
pair so `StereoIdle` stays false.

Scale. The logo units are not meters. Choose the head pose scale so the logo
spans about 2.5 m at about 3 m from the player [I, a taste call]. Measure the
logo width in world units from the `lhlogo.raw` home positions during the
probe. Keep the eye separation in the same units. `HeadPoseScale()` and the
existing IPD path already convert meters.

"Around the player". The particles start scattered and fly in. With the
camera still, they arrive from all sides in the game's own fly in. The engine
clips to its own frustum, so the mod's roughly 110 degree render frustum
limits how far to the side and how far behind particles show [I]. Particles
behind the head do not draw. That is fine for L2. A wider "around" would need
more passes and is not worth it.

Also the extras:

- The 2D fade boxes and the caption text draw in screen space. Put them on
  the same eye images at first. Look at them in a headset. If the fade box
  looks like a wall that covers the whole view, turn it off in VR and fade
  with the sprite color alpha alone [I].
- The music is `intro.sad` and a sound effect `0xa0` in `GGame::Loop`. It is
  not affected.
- `LHScreen::Flip` runs twice per frame. It is a small cost on the desktop
  window. `PresentWatchSkipDesktop` may already skip it.
- The random jitter (`Random` at `0x0081D180` per sprite, `+0x14`) makes each
  eye's sprites turn a different amount. `eye_camera.cpp` already restores the
  game random seed per eye. Check that the same seed feeds `Random` here. If
  not, save and restore the seed around each call [I].

Test in the headset by hotkey (as in the memory notes): a hotkey to toggle
"intro in stereo" against flat, so the A and B are one press apart. Test with
a still head pose and with a step, not with a sway.

### 6.3 L3: push with a controller

Two levels. Do L3a first.

**L3a, the pointer already works [I, from `controllers.md`].** The effect
reads the same `0x00E852C0` pointer that the M2 controller pointer writes. So
the controller already moves the logo pointer when the pointer ray mode is on.
In stereo the meaning changes: the pointer is where a ray from the head, or
from the controller, meets the render plane. Set it to the projection of the
controller tip, using the cyclops camera (the mid eye) and
`LH3DTech::ProjectPoint`. Then a particle that overlaps the fingertip in the
view moves. This is a "shadow touch" and it is cheap.

Caveats for L3a:

- The push reaches every particle along the sight line inside the pixel disc,
  whatever the depth. The player sees particles behind the hand react.
- `DoLogo` ends the logo on a mouse button bit. The trigger must not set that
  bit during the logo, or the intro ends at the first push. Map the push to
  hand presence (or the grip) and keep the click bits clear. Add a way to skip
  on purpose: the menu button.
- The wheel scales the strength. A thumbstick could drive `0x00E85300`.

**L3b, a true 3D push.** Use `ControllerDirectHand` in `hand_direct.cpp`. It
returns a 3D point in world units and a reach factor. Two hooks keyed on the
return address, both inside `RunAlexFX`:

- `ProjectPoint` `0x00819390` with return address `0x005F977E`. Return "not
  visible" (any value except 2) when the particle is farther than a radius
  from the hand point. This limits the push to a real sphere.
- `fn_0081B370` with return address `0x005F987F`. Return the hand's 3D point
  instead of the unprojected mouse point. Then particles inside the sphere are
  pulled to the hand and carried by it.

Risk: `ProjectPoint` runs very often across the engine. A hook on it must
check the return address first and pass through at once. Or patch the two call
sites directly (5 bytes each) and skip the global hook.

Another way for L3b: apply the push in the mod. After the original returns,
read the particle arrays (`0x00D34A98`, `0x00D34A94`, `0x00D34A90`) and change
the positions and velocities in place. This needs a solid map of the arrays
first (which one is the sprite record, which is the home, which is the
velocity). I read three arrays in the code but did not fix their strides with
certainty [I]. Only do this if the two-hook method fails.

Haptics: a short pulse when a particle moves is easy and pleasant.

### 6.4 Stages

| Stage | Work | Test |
| --- | --- | --- |
| L2.0 | Probe: log `UpdateCamera` arguments and the clock during a normal logo run. Log `g_delta_time` and the array pointers. Find the final pose and the logo width. | Read the log. No visual change. |
| L2.1 | Hook `RunAlexFX`, run mode 1 twice with time frozen, grab per eye. No head, no camera change. | Logo in stereo. The speed must not double. |
| L2.2 | Fixed final pose, head tracking, scale to 2.5 m at 3 m. | Headset. Still head, then a step. |
| L2.3 | Fade boxes and caption check, sky depth check, random seed check. | Headset. |
| L3a | Controller tip to pointer, click bits kept clear, menu button to skip. | Headset. |
| L3b | The two return address hooks for a 3D sphere push. Haptics. | Headset. |

## 7. Risks

- Double time. If any state in `RunAlexFX` advances by something other than
  `g_delta_time` (for example `GetTickCount`, or a frame counter), the second
  call moves it. Check in L2.1 with a log of the particle positions between
  the two calls. This is the biggest risk.
- The mode 0 build and the mode 2 free must run once only. Do not double
  them. The hook passes them through.
- The sky and the sprites may read a matrix other than the one
  `ProjectPoint` reads. `eye_camera.cpp` already handles this for the game
  world. Check that it covers a call made outside `Process3dEngine`.
- The fade boxes are screen space, and they may look wrong in a headset.
- Skipping: if a click ends the logo in the middle of a stereo pair, the mod
  must not leave the flag set. Clear it when mode 2 runs.
- The wide-angle limit hides particles to the side or behind the player.
- `PlayLogoScreens` and `PlayPreIntroVideo` (Bink) stay flat. The mod shows
  them on the existing flat path. A small quad in the room (Track U4) is the
  right home for them.
- Do not ship `lhlogo.raw`, the Bink files or any Lionhead art. Read them from
  the player's install, as the rest of the mod does.

## 8. What I did not verify

- The exact final camera pose.
- The exact particle count and the array strides. The `0xC000` bound and the
  `0x34` and `12` strides suggest 4096 particles [I].
- The push maths in detail, and the unproject role of `fn_0081B370`.
- That `0x00E85304` holds the mouse button bits.
- Any behavior in a headset.

## 9. Sources

`runblack.exe` disassembly through `bwvr\tools\disasm.py` (`RunAlexFX`,
`DoLogo`, `PlayLogoScreens`, `PlayPreIntroVideo`, `pc_main`, `GGame::Loop`),
the decomp symbol list at `third_party\bw1-decomp\config\BW1W120\symbols.txt`,
the Bink headers of the four `.bik` files, and the mod files `stereo.cpp`,
`head_camera.cpp`, `eye_camera.cpp`, `mod_main.cpp`, `hand_direct.cpp`. Related
docs: `ui-layer.md` section 1.4, `controllers.md` sections on the pointer,
`hand.md`.

## 10. As built (L2 and L3a)

Code: `src/mod/intro_vr.cpp`, with small changes in `head_camera.cpp`,
`stereo.cpp`, `mod_main.cpp` and `controllers.cpp`. The ini key `intro_vr`
turns it on. The default is 0, and with 0 nothing is hooked.

### 10.1 What the disassembly changed in the design

Marks as in the top of this file. All of these are **[V]**.

- The done flag is `0x00D37CC8`, not `0x00E85474`. `RunAlexFX` sets it when
  the fade factor reaches -2 (clock about 34.7) or when the window is
  minimized. `DoLogo` ends the loop when this flag is set or when
  `0x00E85304` has bit 0 or bit 1 set. `DoLogo` clears `0x00E85474` before
  the loop. It also reads it after each frame, and a value that is not 0
  ends the loop (section 11.1). An earlier form of this note said that it
  never reads it.
- `LH3DRender::StartFrame` writes `g_delta_time` from the tick count, with a
  minimum of 1. It runs at `0x005F933E`, after the clock read at `0x005F9050`
  and before the spring and push read at `0x005F96F0`. So a frozen delta
  time set before the call only stops the clock. The mod hooks `StartFrame`
  and writes `g_delta_time` again for that one call site.
- With no freeze, each call would advance the clock by the time of the call
  before it, and the two calls together would not match real time. So the mod
  also sets the first eye to the real frame time.
- The mouse smoothing (`0x00D37CB0`, `0x00D37CB4`), the push strength
  (`0x00D34A8C`) and the C runtime random seed change on each call and do not
  scale with time. The mod saves them and puts them back around the second
  eye. Each sprite gets `rand` for its angle, so the seed matters.
- The camera path of the game: the camera orbits the logo, then rests at
  `(0, 0, 100)` looking at `(0, 0, 0)` from clock 26 to clock 30 (the same pose
  as the build call). From clock 30 it flies through the logo to
  `(0, 0, -200)`. The mod holds the rest pose for the whole intro. The
  particles fly in around it.
- The logo is a flat image of about 75 units at 100 units of distance
  (110 degree render), so at a scale of 33.3 units per meter it is about
  2.2 m wide at 3 m.

### 10.2 How a frame runs

`h_RunAlexFx` passes modes 0 and 2 through. For mode 1 with a headset (or
`stereo_headless`) it runs `RunPair`:

1. `XrScreen::BeginFrame`. If it fails, or the runtime does not want a
   picture, the effect runs once and flat.
2. First frame only: the head view setup of `stereo.cpp`
   (`StereoIntroPrepareView`), and the room scale (`intro_distance_cm`, default
   300, gives 33.3 units per meter). The old scale comes back at mode 2.
3. The head pose for the frame, and the declared render pose for both eyes.
4. `HeadCameraIntroFrame`: one head for both eyes.
5. Eye 0 with the real time, eye 1 with time 0. Each `UpdateCamera` call
   from `0x005F934B` goes through `IntroCameraPose` (the still camera) and the
   normal head code of `head_camera.cpp`, then `IntroCameraBuilt` moves the
   matrices half an eye, as the scene does.
6. `h_FinishFrame` calls `IntroFinishFrame` after each eye, and it copies the
   picture to the headset. The mono path does not run.
7. `EndFrame`, then `StereoMarkPresented`, so `StereoIdle()` stays false.

Mode 2 calls `HeadPoseRecenter`, so the world starts as it did before.

### 10.3 The ways back to the flat intro

- `intro_vr=0`, no headset and no `stereo_headless`, stereo off.
- The bytes at `RunAlexFX` or at its three calls do not match. No hook is
  made.
- 3 frames in a row without the camera call, 4 frames in a row over 250 ms,
  or 3 frames in a row where the picture did not reach the headset. The mod
  logs the reason, puts the room scale back, and the rest of the intro is
  flat.
- The done flag is set. The second eye is skipped.

### 10.4 Ini keys

| Key | Default | Meaning |
| --- | --- | --- |
| `intro_vr` | 0 | The intro in stereo |
| `intro_still` | 1 | Hold the camera at the rest pose. 0 uses the game's orbit |
| `intro_distance_cm` | 300 | Distance of the logo plane from the head |
| `intro_controller` | 1 | Run the controller frame, so the aim moves the pointer |
| `intro_probe` | 0 | Log the game's camera each second and the particle and pointer checks |

### 10.5 L3a: the controller

- `ControllersFrame` runs once for each pair. With the pointer owned by the
  controller, `pointer_map.cpp` writes the pixel to `0x00E852C0`, and the
  effect reads it as it reads the mouse.
- `Usable()` in `controllers.cpp` is false while the intro draws in stereo
  (`IntroBlocksButtons`). No controller button can reach `SetButtons`, so the
  trigger cannot end the intro.
- The menu button on either controller sets the done flag.
- The push is worked out in the view of the first eye (the left one).

### 10.6 What the desktop runs showed

Two runs with `stereo_headless` and a fake head (2560 by 1440 render, 110
degrees), each 90 s. No headset was used. Nothing was seen in a headset.

- Run l2a (`intro_still=0`, the game's orbit): 976 stereo frames, 0 flat
  frames, no fallback. The particle at index 100 and the clock were the same
  after the left and the right eye, so the second eye does not move the
  simulation. The clock ran at real time (36 clock units in about 33 s). The
  game reached its main menu after the intro.
- Run l2b (the default, still camera, fake controller with the trigger held
  for 120 frames): 971 stereo frames, no fallback. The trigger did not end the
  intro (buttons stayed 0x00). After the fake hand turned, the log said "the
  controller owns the pointer" and the pointer went from 1532 883 to
  1792 1135, which is where `RunAlexFX` reads it. The logo, the sky and the
  particles have disparity (about 29 px between the eyes for the logo plane at
  this scale) and the 2D name text has none. The game continued to the world
  after the intro.
- Cost: the two effect calls took 33.7 ms on average (worst about 70 ms). On
  the desktop each call includes a `Flip` that waits for the display, so most
  of this is two waits. With a headset `PresentWatchSkipDesktop` skips them.
  The frame cost with a headset is not measured.
- The push itself (particles moving under the pointer) was not checked
  in a picture. The pointer value reaches the effect and the buttons stay
  clear.

### 10.7 What needs the headset

- Comfort of the still camera and the scale (logo about 2.2 m wide at 3 m).
- The fade boxes and the name text at zero disparity, and the sprite depth.
- The frame rate with two effect calls and the real `Flip` skip.
- Whether the aim of a real controller pushes the particles as expected. The
  push uses the left eye view.
- The menu button skip.

## 11. The skip (2026-10-03)

Buck, in the headset test of the evening of 2026-10-03: "I only asked to
skip the intro when starting a game, you know, lionhead logos and stuff".
On a monitor Esc and a mouse click do that. A player in the headset had no
button for it: with `intro_vr=0`, which is his setting, the logos show flat
in the headset and no controller code runs.

Code: `src/mod/intro_skip.cpp` and `intro_skip_math.h`, one call in
`h_FinishFrame` of `mod_main.cpp`, and `ControllersButtonBits` at the end of
`controllers.cpp`.

### 11.1 What the game accepts [V]

The start of the game has three parts that draw their own frames. Each one
tests the input itself:

| Part | Code | What ends it |
| --- | --- | --- |
| The logo screens: three screens of `Data\logo.bik` | `PlayLogoScreens` `0x00642950` | The test `0x00642930`: the last key is Esc, or a mouse button bit is set (`0x00E85350`, bits 0 and 1). A press while a screen fades in or shows starts its fade out. A press in the fade out ends the screen. `InputReset` `0x005FA000` clears the key and the bits after a press. So one press skips one screen at the most |
| The video before the intro, only with no profile or with `-PREINTROVIDEO` | `PlayPreIntroVideo` `0x006426F0` | The last key is Esc (`0x006428C5`). A mouse click does not end it |
| The Lionhead logo | `DoLogo` `0x005FA070` | A mouse button (`0x00E85304`, bits 0 and 1), each key (`0x005FA09F`: the last key is not 0 after a frame), or the done flag `0x00D37CC8` |

**The last key** is one field. `LHKeyboard::ProcessKeyboard` `0x007DCAF0`
writes the scan code of each key that goes down to `this + 0x108`
(`0x007DCBD3`). The keyboard object is at `0x00E8536C`, so the field is
`0x00E85474`. Esc has the scan code 1. So `0x00E85474 == 1` is what an Esc
press gives each of the three parts.

Section 10.1 said that `DoLogo` never reads `0x00E85474`. That was wrong:
`DoLogo` clears it before the loop and reads it after each frame.

The story movie `intro.bik` (step 4 of section 2) is not a part of the
start. A script plays it inside the first story, and the skip does not
touch it.

### 11.2 What the mod does

1. `h_FinishFrame` calls `IntroSkipFrame` after each `FinishFrame`, with the
   address that called `FinishFrame`. Each part calls `FinishFrame` from its
   own code, so that address says which part plays (`intro_skip::PartOf`).
   The mod hooks nothing more. After the intro, the call costs one test of
   an address for each frame.
2. The buttons are the two triggers and A, B, X and Y. The grip is not one
   of them, because a player holds it with no intent.
3. `intro_skip::Step` finds a new press: a button that is down now, was up
   in the frame before, and is on a hand that was awake in the frame
   before. So a button that is down when the intro starts, or when a
   controller wakes, does not skip.
4. A press writes 1 (Esc) to the last key. With `intro_skip=1`, the default,
   the mod writes it again in each frame of a part until the intro is over.
   So one press skips all that is left: the logo screens end in a few
   frames each, and the Lionhead logo ends in its first frame. With
   `intro_skip=2` the mod writes it one time, so one press is one Esc, as on
   a monitor.
5. The press holds until a frame that is no part comes after the skip of
   the Lionhead logo, or for 120 s at the most. Between the parts the game
   loads, and those frames get nothing.

The mod does not send a key message and makes no call into the game. It
writes the one field that the three tests read. `InputReset` clears it
after each part.

With `intro_vr=1` the Lionhead logo is in stereo (section 10). The trigger
still does not click there (`IntroBlocksButtons`), and the menu button
still sets the done flag. The trigger and the face buttons now end the logo
through the skip.

### 11.3 The controllers at the start

- `StartVrOnce` starts the session at the first `FinishFrame` of the game,
  which is a frame of the first logo screen.
- The mono path of `h_FinishFrame` sends each frame of the intro to the
  headset with `SubmitTexture`. That calls `XrScreen::BeginFrame`, and
  `BeginFrame` reads the controllers (`SyncInput`) for each frame [V]. So no
  controller code of `controllers.cpp` is needed, and `IntroSkipFrame` reads
  the hands of `XrScreen`.
- The runtime gives input only when the session has the focus. So the first
  moments of the first logo screen have no hands, and a press there does
  nothing [I, not measured in the headset].
- `ControllersButtonBits` gives the hands of `controllers.cpp` too. Those
  are the fake controller of a desktop run, and they change only where
  `ControllersFrame` runs: in the Lionhead logo with `intro_vr=1`.

### 11.4 Keys

| Key | Default | Meaning |
| --- | --- | --- |
| `intro_skip` | 1 | 1: a press of a trigger or of A, B, X or Y skips all that is left of the logos at the start. 2: a press is one Esc, so it skips one part. 0: off |
| `intro_skip_test_frame` | 0 | A test: at this frame of the intro, a press comes that no hand made |

### 11.5 The log

The tag `intro skip`:

- The key at the first frame of the game.
- `the logo screens play now`, `the Lionhead logo plays now`, and `... over
  after N frames` when a part ends. So a log shows how long each part was.
- `a press of the right trigger skips the logo screens, at frame N of the
  intro`, one line for each press that skips.
- `the intro is over. N frames got Esc` when the press holds no more.

### 11.6 Tests

The math test has 26 checks of the skip (`intro_skip_checks.h`): the part
for each caller of `FinishFrame`, a button that is down when the intro
starts, a controller that wakes with a button down, one press that holds to
the end of the Lionhead logo and not past it, the limit of 120 s, one Esc
for one press with `intro_skip=2`, and a press outside the intro.

Desktop runs of 2026-10-03 (the rig of `backend.md`, a render of 2560 by
1440, no headset):

| Run | Keys | The logo screens | The Lionhead logo |
| --- | --- | --- | --- |
| cw1, cw2, cw4 | No press | 798 to 818 frames | 2921 to 2939 frames |
| cw6 | `intro_skip_test_frame=30` | Over after 36 frames | Over after 2 frames. "the intro is over. 9 frames got Esc" |
| cw7 | `intro_vr=1`, and the fake A button at frame 150 of the controllers | 819 frames (no press there) | "a press of the A button skips the Lionhead logo": it ended after 151 stereo pairs |
| cw8 | `intro_skip=2`, `intro_skip_test_frame=30` | Over after 589 frames: one screen was cut short | 1651 frames, with no press from the mod |

In each run the game went on to the land and to its start dialog. In run
cw8 the Lionhead logo ended early. Buck was at the PC, and the logo ends on
each key and each mouse button, so the cause can be his own input [I].

**Needs the headset:** a press of a real controller. The hands come from
`XrScreen` there, and no desktop run has them. Also how early in the first
logo screen the runtime gives input.

## 12. The game stops after the logos: the check for an internet connection (2026-10-07)

**What a player sees.** The logo screens end, the window keeps the last
picture, and Windows shows the game as "not responding". Nothing more comes
in `bwvr.log`. This is a fault of the plain game on a PC where one service of
Windows does not answer. It is not a fault of the mod, but the mod can
prevent it.

**The cause [V].** `LHMultiplayerR.dll` has a function
`LHCheckForInternetConnection`. The game calls it on its main thread when
the logo screens are over. It loads `rasapi32.dll` with `LoadLibraryA`, gets
`RasEnumConnectionsA` and `RasEnumDevicesA` with `GetProcAddress`, and calls
them. They ask the Remote Access Connection Manager of Windows (the service
`RasMan`) for the dial-up and VPN connections and for the modems. Each call
is an RPC with no time limit.
On 2026-10-07 the service of the owner's PC did not answer, and the stack of
the main thread of a hung game was:

```
ntdll!NtAlpcSendWaitReceivePort
RPCRT4!NdrClientCall2
RASMAN!RasConnectionEnum
rasapi32!RasEnumConnectionsA
LHMultiplayerR!LHCheckForInternetConnection+0xb8
```

`rasdial.exe` of Windows waited in the same way, with no game. Each start of
the game stopped there: in the headset, on the desktop, with the build of
that day and with a build that ran two days before. The last lines of the log
were the sound start of the game, so the sound looked like the cause. It was
not.

**What the mod does (`net_check.cpp`).** The game needs the answer only for
its internet features of 2001 (the weather of the real world, its servers),
and those have no server now. So the mod gives the answers itself: no
connection and no modem. That is what Windows answers on a PC with no
dial-up connection and no VPN. The mod writes its own function in the import
slot of `GetProcAddress` of `LHMultiplayerR.dll`. The DLL asks for four
names (`RasEnumConnections`, `RasEnumConnectionsA`, `RasEnumDevices`,
`RasEnumDevicesA`), and for each it gets a function of the mod that gives 0
entries. Each other name goes to the real `GetProcAddress`. The mod does
this before the game starts (`EarlyHooks`), with `vr=0` too.

| Key | Default | Meaning |
| --- | --- | --- |
| `net_check` | 0 | 0: the mod answers "no connection" at once. 1: the call of the game goes to Windows, as in the plain game |

**The log.** One line at the start says which way is on. The first time the
game asks, two more lines say "the game asked Windows for its dial-up and
VPN connections ... The mod answered: none" and the same for its modems.

**Proof [R, runs ras1 and ras2, 2026-10-07].** Both runs were on the PC with
the service that did not answer (`rasdial.exe` still waited). With an answer
for the connections only (ras1), the game stopped again: the log had the
first answer line and nothing after it, because the game asks for the modems
next. With both answers (ras2), the game went on: the two answer lines, then
the 3D device, the Lionhead logo, the start dialog and frames at 49 fps (444
log lines in 60 s, where each start before had about 207).

**To get a PC out of this state** without the mod: start Windows again, or
start the service "Remote Access Connection Manager" again as an
administrator.
