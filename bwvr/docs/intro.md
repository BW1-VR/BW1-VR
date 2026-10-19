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
- With `intro_vr=1`, the mod runs `RunAlexFX` one time for each eye, with the
  head tracked. The view goes along the camera path of the game and does not
  turn (`intro_motion=1`). Each eye has its own trails, and the mark beside the
  logo has the depth of the logo. `intro_vr` is off by default: no headset
  session has checked this state.
- In the logo in stereo, each hand takes the orbs in its reach with its own
  motion, one to one, in 3D (`intro_push_3d`). The hand models of the game
  show while a hand moves (`intro_hand_show`).
- A press of a trigger or of A, B, X or Y skips the logos and the intro (the
  skip, `intro_skip`, on by default).
- The mod answers the game's check for an internet connection itself, so a PC
  whose Windows service `RasMan` does not answer can start the game
  (`net_check`).
- The mod answers the game's check for DirectX 7 itself, so the game starts
  with no administrator rights (`dx_check`).

Source files: `src/mod/intro_vr.cpp` and `intro_motion_math.h` (the stereo
intro), `src/mod/intro_skip.cpp` and `intro_skip_math.h` (the skip),
`src/mod/net_check.cpp` (the internet check), `src/mod/dx_check.cpp` (the
DirectX check), with small parts in
`head_camera.cpp`, `input_watch.cpp`, `draw_hooks.cpp`, `gpu_bridge.cpp`,
`stereo.cpp`, `mod_main.cpp`, `controllers.cpp`, `overlay.cpp` and
`hand_model.cpp`.

## Section map

| Section | Subject |
| --- | --- |
| What runs at startup | The four steps of the start of the game |
| How the logo draws | `RunAlexFX` frame by frame: the camera path, the clock, the sky, the trails |
| How the mouse pushes the particles | The reads and the maths of the push |
| Why the intro is flat without the hook | What the mod does when `intro_vr=0` |
| The intro in stereo | The design and the code that is built |
| The skip | The press that ends the logos and the intro |
| The internet check | The `RasMan` stop and the answer of the mod (`net_check`) |
| The DirectX check | The check that stops the game with no administrator rights, and the answer of the mod (`dx_check`) |
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
inside the first story, and the skip does not touch it. In the God view it
plays on the screen of the cut scene window (`cutscene-window.md`, section 7).

`GGame::Loop` starts one sound before it calls `DoLogo` (sound effect `0xA0`,
`0x0054D046`) and stops it after the call [V]. So the sound of the logo has its
own length. It does not follow the clock of the effect.

## How the logo draws [V unless marked]

Each mode 1 call does all of this in one function:

1. It adds the frame time `LH3DTech::g_delta_time` (`0x00C38134`) to an effect
   clock at `0x00D37CCC`, times 0.0011 and times a speed factor at
   `0x00D34A8C` (`0x005F904E`). The effect sets the factor to 1 when it
   builds, so the clock gets 1.1 for each second. The mouse wheel changes the
   factor, between 0.1 and 1 (`0x005F968C`). No other code reads it. The clock
   sets the camera, the fades and the size of the orbs.
2. It works out a camera position and a focus point from the clock (see "The
   camera path").
3. It sets `LH3DRender::g_mode_cleaning` (`0x00C3871C`) to 2, and it calls
   `LH3DRender::StartFrame` `0x0082F0E0`. `StartFrame` writes `g_delta_time`
   from the tick count, with a minimum of 1. It runs at `0x005F933E`, after the
   clock read at `0x005F9050` and before the spring read at `0x005F96F0`.
4. It calls `LH3DTech::UpdateCamera` `0x00819920` itself, at `0x005F934B`. The
   first call is in mode 0, at `0x005F8A84`, with the camera at `(0, 0, 100)`.
5. It draws the sky object (`D_SKY.l3d`) through the normal `LH3DObject` path,
   with a color that has an alpha of 160 of 255 (`0x005F9531`).
6. It calls `LHMouse::UpdateDeltaPos` at `0x005F95DC`. Then it moves the orbs
   (see "How the mouse pushes the particles"), and it draws 4096 of them, one
   call to `LH3DSprite::Draw` `0x00840530` each, in a loop at `0x005F9959`.
   Each orb is a 3D sprite record of `0x34` bytes. `LH3DSprite::Draw` reads the
   camera matrix at `0x00EA1D28` and draws through `LH3DRender::DrawTriangle`
   [V for the loop, I for the count from the `0xC000` byte bound divided by
   12].
7. It draws a text and boxes at a place of the picture (see "The flat text and
   the boxes of the effect").
8. It calls `LH3DRender::FinishFrame` `0x0082F460` and `LHScreen::Flip`
   `0x007DE090`. `FinishFrame` draws the fade of the effect: a black picture
   with an alpha (`fn_0086FEE0`, the color at `0x00FA51D8`).

`DoLogo`, `PlayLogoScreens`, `PlayPreIntroVideo` and `RunAlexFX` are all
`FinishFrame` callers outside `Process3dEngine` (`ui-layer.md`, section 1.4).

### The camera path

The logo is at the origin, on the plane z = 0. The orbs stay at their places:
each orb starts at its home, and only the push moves it. So the motion that a
player sees in the intro is the motion of the camera. The effect works out
the camera from its clock (`0x005F90E7` to `0x005F933A`):

| Clock | The camera of the game |
| --- | --- |
| 0 to 15 | It goes around the origin, between the orbs, at `r * (sin(0.352532 t + 2), cos(3 - 0.26793 t), sin(0.15161 t + 7))`. The distance `r` goes from 25 units to 10. It looks at the origin |
| 15 to 26 | It leaves the orbit and goes to the rest pose. The blend is `(1 - cos(pi a)) / 2` with `a = (t - 15) / 11` (`fn_005F8960`), so this move starts and ends with no speed. It looks at the origin |
| 26 to 30 | The rest pose: `(0, 0, 100)`, looking at the origin. It is the same pose as the build call |
| 30 to 36 | It goes through the logo along -z, 50 units for each unit of the clock, to `(0, 0, -200)`. This move starts at once, with no ease. It looks along -z |

The effect sets its done flag at clock 36 (`0x005F95AE`), or when the window
is minimized. With no skip the logo plays for 33 s. `intro_motion_math.h` has
the path (`GameCamera`), and the math test compares it with the camera that
the game asked for in a run.

### The clock

| Clock | What changes |
| --- | --- |
| 0 to 12 | The fade from black: its alpha goes from 255 to 0 |
| 15 to 26 | The orbs grow to 2.54 times their size (`1 + 1.54 s^8`, `s` is the blend of the camera) |
| 22 to 23 | The text and the boxes fade in. The alpha of the sky goes from 160 to 178, and the sky gets darker |
| 31 to 33 | The fade to black |
| 32 to 34 | The orbs fade out |
| 33 to 34 | The text and the boxes fade out |

### The picture of the logo

The logo shape comes from `lhlogo.raw`. It is 12288 bytes, which is 64 x 64 x 3,
so one grayscale image with three equal bytes per pixel [I]. The code reads the
file with `LHLoadData` and gives each pixel one orb: the place of the pixel in
the picture of the build camera, at a distance of 70 to 130 units from that
camera (`Random(70, 130)`, `LH3DTech::Get3DPointFromScreen`). A bright pixel
gets a white orb and a dark pixel a black orb. So the orbs make the logo only
for a camera at the rest pose, and the cloud is 60 units deep. On its plane the
logo is 74.9 units wide with a view of 110 degrees (the log has the number at
the first stereo frame).

At `0x005F8AF0` a tick count test makes the effect read `userlogo.raw` from the
temp folder of the user in 1 start of 4, when the file is there: 64 x 64 bytes
and a name [V]. The desktop runs on one PC showed a second picture in some
runs [I, it is the symbol of the player]. With that picture the effect draws
the name under the logo, and it does not draw the mark "TM".

### The sky and the trails

The sky is a sphere around the logo with a radius of about 160 units [measured
in a desktop run: the difference between the two eye pictures puts each part
of the sky 157 to 163 units from the origin]. The camera is inside it until
clock 35.

The effect keeps the picture of the frame before in the render target (the
clean mode 2), and it draws the sky with an alpha over that picture [I for the
blend, from the pictures]. So 37 percent of the frame before shows through
the sky, and each thing that moves in the picture has a trail.

### The flat text and the boxes of the effect

From clock 22 the effect draws these things at a fixed place of the picture,
through `GatheringText::DrawTextRaw` (which draws with
`LH3DTech::Draw3DScreenTriangle`) and `SetupThing::DrawBox`:

- A name under the logo (`0x005F9B9D`). With the logo of Lionhead the name is
  two spaces, so nothing shows.
- The mark "TM" above the right corner of the logo (`0x005F9FBA`), only with
  the logo of Lionhead.
- The logo of a maker of hardware, in two boxes, only on a PC that has that
  hardware: a Pentium 4 (`LH3DP3::IsPentium4`, `0x005F9BB8`), and a mouse with
  force feedback of Immersion (`0x005F9DA2`).

## How the mouse pushes the particles [V]

The push code sits inline in `RunAlexFX` mode 1, from `0x005F95DC` to
`0x005F993A`. It reads these `LHMouse` fields (see `controllers.md`):

| Address | Field | Use in the effect |
| --- | --- | --- |
| `0x00E852C0`, `0x00E852C4` | pointer x, y in window pixels (the mod maps them to render pixels) | The middle of the push. |
| `0x00E852E4`, `0x00E852E8` | relative motion of the mouse, summed | Smoothed into `0x00D37CB0` and `0x00D37CB4`, then cleared. |
| `0x00E85300` | wheel delta | Clamped to plus or minus 240. It changes the speed factor of the clock at `0x00D34A8C`. |
| `0x00E85304` | button bits | Read by `DoLogo`, not by the push. |

`LHMouse::UpdateDeltaPos` `0x007E4810` is called at `0x005F95DC` in the same
frame. It reads the mouse device (DirectInput) and adds its motion to the two
sums.

Each call smooths the motion: `lag = lag + (motion - lag) * 0.1`, as a whole
number. This step has no frame time in it. While the smoothed motion is not
zero, the effect does this for each of the 4096 orbs:

1. `LH3DTech::ProjectPoint` `0x00819390` (call at `0x005F9779`) gives the
   pixel and the depth of the orb, against the matrix at `0x00EA9E40`.
2. If the orb is one tenth of the picture width or more from the pointer,
   across or up and down, the code skips it.
3. The weight is one minus distance over that radius.
4. The new pixel of the orb is its pixel plus the smoothed motion (at most 20
   each way) times 4 times the weight. `LH3DTech::Get3DPointFromScreen`
   `0x0081B370` (call at `0x005F987A`) gives the point of the scene for that
   pixel at the depth of the orb.
5. The effect puts the orb at that point (`0x005F991A`), and it adds the move
   to the speed of the orb, times the frame time and 100.

After the push, one loop pulls each orb to its home like a spring, damps its
speed and moves it by its speed, all with the frame time. The next loop draws.

So the push is a disc in the picture that goes into depth at the depth of each
orb. It is not a sphere in the scene. And the push needs a motion of the mouse
device: the place of the pointer alone moves no orb.

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
   (`StereoIntroPrepareView`), and the speed of the effect clock
   (`intro_speed_pct`).
3. The test keys, and the scale of the room for the value of `intro_motion`.
   The call with mode 2, at the end of the effect, puts the old scale back.
4. The head pose for the frame, and the declared render pose for both eyes.
5. `HeadCameraIntroFrame`: one head for both eyes.
6. Eye 0 with the real time, eye 1 with time 0. Before each eye pass the keep
   of that eye goes into the render target (see "The trails"). The
   `UpdateCamera` call from `0x005F934B` goes through `IntroCameraPose`, which
   gives the camera for `intro_motion`, and through the normal head code of
   `head_camera.cpp`. Then `IntroCameraBuilt` moves the matrices half an eye,
   as the scene does. The first eye pass works out the cameras of the frame,
   and the second eye pass uses the same ones.
7. `h_FinishFrame` calls `IntroFinishFrame` after each eye. It takes the
   picture into the keep of the eye, and it copies the picture to the headset.
   The mono path does not run.
8. `EndFrame`, then `StereoMarkPresented`, so `StereoIdle()` stays false.

The call with mode 2 also frees the keeps and calls `HeadPoseRecenter`, so the
world starts as it did before.

### Why the freeze needs a hook on StartFrame

`StartFrame` writes `g_delta_time` after the clock read, so a frozen delta time
set before the call stops only the clock. The mod hooks `StartFrame` and writes
`g_delta_time` again for that one call site. The first eye gets the real frame
time, so the two calls together match real time.

With `g_delta_time` at zero the clock, the spring and the damping stop, because
each one multiplies by the frame time. The push does not: see "The second eye
and the push". The smoothed mouse motion (`0x00D37CB0`, `0x00D37CB4`), the
speed factor of the clock (`0x00D34A8C`) and the C runtime random seed change
on each call and do not scale with time. The mod saves them and puts them back
around the second eye. Each sprite gets `rand` for its angle, so the seed
matters. [V] (The random jitter is `Random` at `0x0081D180`, per sprite, at
`+0x14`.)

### The motion

`intro_motion` chooses what the view does. In each value the head of the
player turns and moves the view, as in the scene. `intro_motion_math.h` has
the cameras (`Cameras`), and the math test checks them.

| `intro_motion` | The view |
| --- | --- |
| 0 | The camera stays at the rest pose. The player sees the formed logo from the start, and nothing moves |
| 1 (default) | The camera goes along the path of the game and does not turn. Each frame the mod takes the place that the game asks for, and gives it one fixed heading. The player goes between the orbs, then back to the rest pose, then through the logo |
| 2 | The orbs are a model in front of the player, and the room stays still. See "The model" |
| 3 | The camera of the game, with its turns: its place and its heading. This turns the view of the player with no turn of the head. `head_camera.cpp` keeps the view level, so the tilt of the camera of the game is not in the view |

**The fixed heading.** It is the direction of the rest pose: from `(0, 0, 100)`
to the logo, along -z. `head_camera.cpp` builds the room of the head from the
heading of the camera that it gets, so the straight ahead of the player (the
recenter direction) is that heading for the whole intro. At the rest pose the
logo is then in front of the player. In the orbit the logo is to the side or
behind: the log gives the angle.

**The speed.** The path has the speed of the game: 0.15 to 0.3 m for each
second in the orbit (5 to 10 units, at 33.3 units for a meter), 0.6 m for each
second at the most on the way to the rest pose, and 1.65 m for each second
through the logo (55 units). The game eases the start and the end of the way
to the rest pose. The move through the logo starts at once, 3.6 s after the
camera came to rest, and the fade to black starts 0.9 s later. The mod adds no
ease.
`intro_speed_pct` (10 to 100) makes the clock of the effect slower: the mod
writes it to the speed factor of the effect (`0x00D34A8C`), which is the way of
the game for a slower clock. The path, the fades and the end then take longer.
The spring of the orbs and the sound of the logo keep their speed.

**The scale.** `intro_distance_cm` (default 300) is the distance from the head
to the plane of the logo at the rest pose. The rest pose is 100 units from
that plane, so the room has 33.3 units for each meter: the logo is 2.25 m wide
at 3 m, the cloud of orbs is 1.8 m deep, and in the orbit the head is 0.3 to
0.75 m from the middle of the cloud. Orbs then pass near the eyes: the effect
has its near plane at 0.1 units (3 mm).

**This is motion that the player did not make.** The design rule of the mod is
"no motion that the player did not make". `intro_motion=1` and 3 are an
exception that the player chooses with `intro_vr=1`. 0 and 2 keep the rule.

### The model

With `intro_motion=2` a frame has two cameras:

- **The camera of the room** stays at the rest pose. It draws the sky, so the
  room does not move.
- **The camera of the orbs** starts from the rest pose and makes half of each
  move of the camera of the game, with the fixed heading (`ModelPlace`,
  `kModelGain`). So the cloud of orbs moves in front of the player as the path
  of the game moves it, and the head does not go between the orbs: in the
  orbit this camera is 50 units from the plane of the logo at the least, and
  the nearest orbs are 30 units in front of that plane. The model passes the
  head only in the move through the logo, after clock 32.8, when the fade is
  90 percent black.

`intro_model_distance_cm` (default 200) is the distance from the head to the
plane of the logo at the rest pose, as `intro_distance_cm` is for the other
values. With 200 the room has 50 units for each meter: the logo is 1.5 m wide
at 2 m, and in the orbit the middle of the cloud is 1 m to 1.2 m from the head.

The call at `0x005F934B` gets the camera of the room. The effect calls
`LHMouse::UpdateDeltaPos` at `0x005F95DC`, after it drew the sky and before it
moves and draws the orbs. The hook of that function (`input_watch.cpp`) calls
`IntroMouseCall`, and that places the camera of the orbs
(`HeadCameraIntroPlace`) and moves it half an eye. The push, the orbs, the text
and the boxes then use the camera of the orbs.

### The trails

The effect draws each frame over the frame before (see "The sky and the
trails"). The two eye passes share one render target. With no help, the pass
of the left eye draws over the picture of the right eye and the reverse. Each
eye then sees each thing two times: at its own place, and weaker at the place
that the thing has for the other eye. This is the doubling of the stereo
intro.

`gpu_bridge.cpp` has a keep for each eye (`GpuBridgeKeepTake`,
`GpuBridgeKeepGive`): a surface with the size, the format and the
multisampling of the render target of the game. After an eye pass the mod
copies the picture to the keep of that eye. Before the next pass of that eye
it copies the keep back to the render target. So each eye has its own trails,
and a trail has the eye difference of the thing that made it.
`intro_eye_trails=0` gives one render target for both eyes with no keep. The
keeps need the Direct3D 9 device of the backend (`gpu_bridge=1`, the default).
With no keep the log has one line that says so.

With `backend=vulkan` the Vulkan renderer keeps the trails: the walk of eye 0
draws into target 0 and the walk of eye 1 into target 1, over the last
picture of that eye (`IntroPassEye`, `vk_keep_picture=1`, `backend.md`, "The
picture of the walk before"). The depth starts at the far plane in each walk.
The game also plays this effect while it loads a saved game. A desktop run of
the logo with `backend=vulkan` shows the trails in both eyes. Only a headset
can show that no eye flickers.

The scene has the same rule for each frame of play that keeps the picture of
the frame before (`eye_clean`, `stereo-artifacts.md`, "The frame before of
each eye"). It uses the same keeps. The logo frees them when it ends.

### The flat text and the boxes in stereo

The effect draws its text and its boxes at a pixel, with no place in the scene
(see "The flat text and the boxes of the effect"). With no help each eye gets
them at the same pixel. They then have no depth, they show double when the
eyes look at the logo, and they go with the head.

The place in the scene of such a thing is the point of the plane of the logo
that the rest camera shows at that pixel. `draw_hooks.cpp` gives each corner
of a flat batch of an eye pass of the intro to `IntroFlatVertex`, and that
gives the pixel at which the camera of the eye pass sees the point
(`intro_motion::PlaceFlat`). So the mark "TM" has the depth of the logo, it
stays at the corner of the logo when the head or the camera moves, and it has
the right size for its distance. A batch with a corner that is not in front of
the camera is not drawn. `intro_text_place=0` gives the same pixel in each
eye.

### The second eye and the push

The push puts each orb near the pointer at a new place, with no frame time in
it (see "How the mouse pushes the particles"). So a second eye pass with a
smoothed motion of the mouse moves those orbs again, after the first eye drew
them. For the second eye pass the mod sets the smoothed motion to zero, and
`IntroMouseCall` makes its call of `LHMouse::UpdateDeltaPos` return with no
read of the mouse. The push code then does not run, and the motion that the
mouse made in that time stays in the device for the next frame.
`intro_push_once=0` lets each eye pass push.

A desktop run with the test key `intro_push_test_px=15` shows it. With
`intro_push_once=0` the second eye pass moves an orb of the disc 0.6 units
after the first eye drew it: the two pictures have it -9.9 px apart, and its
depth asks for -13.7 px. With `intro_push_once=1` no orb has a different
place for the two eye passes.

### The push in 3D

With `intro_push_3d=1` (the default) each hand takes the orbs in its reach
with its own motion, one to one, in place and in turn. The push of the game
is a disc of one tenth of the picture around one pointer, so it cannot be
one to one and it has one hand only.

1. `HandsFrame` in `intro_vr.cpp` runs once for each pair, after
   `ControllersFrame`. `ControllersOverlayHands` gives the grip point and the
   grip axes of each hand in the world. The mod keeps each pose against the
   camera that placed it (`HeadCameraGameCamera`), for this pair and the
   pair before. So the motion of the camera along the path of the game is
   not a motion of the hand.
2. In the first eye pass, after the sky and before the effect moves and
   draws the orbs, the effect calls `LHMouse::UpdateDeltaPos`. There
   `Push3d` puts both poses on the camera of the orbs of this pair. Each orb
   within `intro_push_radius_cm` of the grip point of the pose before keeps
   its place in the hand frame, so it goes where the hand takes that place
   now (`intro_motion::DragPoint`). The weight is 1 to half the reach and
   falls to 0 at the reach.
3. The spring of the effect then pulls each orb back to its home, and the
   orbs draw. The second eye pass moves no orb.

A hand reaches an orb only when the orb is in the reach of the arm. With
`intro_distance_cm=300` the formed logo is 2.1 m to 3.9 m from the head, and
only the orbit at the start of `intro_motion=1` goes between the orbs. With
`intro_distance_cm=60` the logo is 45 cm wide and its orbs are 42 cm to
78 cm in front of the head, so both hands go through it.

With `intro_distance_cm=300` the room has 33.3 units for each meter and the
orbs are about 1.2 units apart, so a reach of 10 cm holds about 2 orbs and a
reach of 30 cm (the default) about 50.

The hands give the mouse no motion, so the push of the game acts on a motion
of the mouse only. `intro_push_3d=0` gives the push of the pointing hand
through the pointer (see "The push of the hand").

### The hand models in the intro

The overlay draws the hand models of the game at both hands, as in play
(`hand-model.md`). `OverlayIntroFrame` builds only the hands: the world and
the Hand of the game are not there yet, so the hands have no context of the
game, and no finger bends on the land or on an object. `OverlayPassEye`
gives the overlay each eye pass.

**The depth.** The effect draws the sky with the depth test and the depth
write, and then the orbs with the depth test and no depth write [V, with
`intro_probe=1` the log has the states of each kind of draw]. With
`intro_hand_depth=1` (the default) the hands draw in the first eye pass and
in the second, after the sky and before the orbs (`IntroMouseCall`), with
the depth write. So an orb behind a hand fails the depth test, and an orb in
front of a hand draws over it: the hands go into the cloud of orbs. The draw
of the `FinishFrame` of that pass then draws nothing, because the overlay
draws one time in each eye pass. A hand under 90 percent of its opacity
writes no depth (`HandModelDraw`), so a hand that fades does not hide the
orbs behind it. With 0 the hands draw in the `FinishFrame`, over each orb.
With `intro_motion=2` the hands draw with the camera of the room and the
orbs with the camera of the orbs, so the depth of the hands against the
orbs is not exact there.

With `intro_hand_show=1` (the default) a hand shows while it moves and fades
when it rests (`intro_motion::HandShowStep`). The speed is the motion of the
grip point in the room, smoothed over 80 ms, and a hand moves above
0.12 m/s. A hand that moves comes in 0.15 s. A hand that rests stays for
0.4 s and then goes in 0.5 s. A hand that is not tracked goes at once. The
opacity multiplies the alpha of the texture of the hand model
(`HandModelSetOpacity`). The low poly hand of a game folder with no hand
files has no alpha, so it shows only from half the opacity. With 2 a hand
shows while it is tracked, and with 0 no hand shows.

### The push of the hand

With `intro_push_3d=0` the push of the effect does the work. It needs a
motion of the mouse device (see "How the mouse pushes the particles"). The
place of the pointer alone moves no orb. So the mod gives the effect a
motion of the mouse from the motion of the pointing hand. This is the way of
the game: the push code, its disc and its spring are those of the game.

1. In the first eye pass, the effect calls `LHMouse::UpdateDeltaPos`. After
   the call, `pointer_map.cpp` puts the pixel of the controller in the
   pointer. The value of the active box (`0x00C4CD20`) is not 0 while the
   logo plays, so the direct hand steps aside there (`controllers.md`, "A
   dialog box takes the pointer"), and that pixel is the pixel of the aim
   ray. A step of the hand moves an aim ray that points far away by a few
   pixels only.
2. With `intro_hand_point=1`, `HandPush` in `intro_vr.cpp` puts the pixel of
   the Hand's point in the pointer (`ControllerHandPixel`), so the push is
   at the hand. With 0 the pointer stays at the pixel of the aim ray.
3. Then `HandPush` takes the motion of that pixel since
   the frame before, in render pixels, times `intro_hand_push_pct`, and adds
   it to the two motion sums of `LHMouse` (`0x00E852E4`, `0x00E852E8`).
   Only whole counts go. The fraction stays for the next frame, so a slow
   hand also pushes (`intro_motion::HandPushStep`).
4. The effect reads the sums, smooths them, and pushes the orbs in the disc
   around the pointer, as for a mouse.

A frame with no pixel of the hand gives no motion, and the next pixel starts
again: the pointer is the mouse's, the hand is off the view, or the frame
before was not a stereo frame. So a hand that comes back into the view does
not push as a jump. The second eye pass gets no motion (see "The second eye
and the push").

With 100 percent a motion of the hand of one render pixel is one count of the
mouse. The disc of the push is one tenth of the picture width, so a hand
that moves a tenth of the picture pushes as a mouse that moves a tenth of
the picture. The effect clamps the smoothed motion to 20 counts, so at a
large render size a fast hand pushes no farther than a hand at 20 pixels a
frame.

Only the pointing hand pushes in this way, because the game has one pointer.

### The ways back to the flat intro

- `intro_vr=0`, no headset and no `stereo_headless`, stereo off.
- The bytes at `RunAlexFX` or at its four calls do not match. The mod makes no
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
| `intro_motion` | 1 | 0: the camera stays at the rest pose. 1: the camera goes along the path of the game and does not turn. 2: the orbs are a model in front of the player, and the room stays still. 3: the camera of the game, with its turns |
| `intro_speed_pct` | 100 | The speed of the effect clock in percent, 10 to 100 |
| `intro_distance_cm` | 300 | The distance from the head to the plane of the logo at the rest pose, for `intro_motion` 0, 1 and 3 |
| `intro_model_distance_cm` | 200 | The same distance for the model of `intro_motion=2` |
| `intro_eye_trails` | 1 | 1: each eye pass draws over the last picture of the same eye. 0: each eye pass draws over the last picture in the render target, which is the picture of the other eye |
| `intro_text_place` | 1 | 1: the text and the boxes of the effect are on the plane of the logo. 0: they have the same pixel in each eye |
| `intro_push_once` | 1 | 1: only the first eye pass pushes the orbs. 0: each eye pass pushes them |
| `intro_controller` | 1 | Run the controller frame, so the aim moves the pointer |
| `intro_push_3d` | 1 | 1: each hand takes the orbs in its reach in 3D, and the hands give the mouse no motion. 0: the pointing hand pushes through the pointer, with the two keys below |
| `intro_push_radius_cm` | 30 | The reach of a hand for the push in 3D, in cm of the room, 2 to 100 |
| `intro_hand_depth` | 1 | 1: the hands draw after the sky and before the orbs, so an orb in front of a hand draws over it. 0: the hands draw after the orbs, over each orb |
| `intro_hand_show` | 1 | 0: no hand models. 1: a hand shows while it moves. 2: a hand shows while it is tracked |
| `intro_hand_push_pct` | 100 | With `intro_push_3d=0`: the motion of the pixel of the hand, in percent, that goes to the motion of the mouse, so the hand pushes the orbs. 0 to 400. 0: only a mouse pushes the orbs |
| `intro_hand_point` | 1 | With `intro_push_3d=0`: 1, with the direct hand, the pointer is at the pixel of the Hand's point. 0: the pointer is at the pixel of the aim ray |
| `intro_probe` | 0 | Log the cameras for each second of the effect clock, and for a pair of eye pictures the pixel of some orbs |
| `intro_motion_test_seconds` | 0 | A test key of the desktop runs: each N seconds of the logo in stereo the mod steps `intro_motion`, as its test key does |
| `intro_push_test_px` | 0 | A test key of the desktop runs: each frame of the logo in stereo gets that motion of the mouse to the right, with the pointer in the middle of the picture |

The launcher removes the two test keys of the desktop runs from `bwvr.ini` on
each save.

### The test keys

With `test_keys=1` these keys act while the logo plays in stereo (`backend.md`,
"Keys for v1.0"). Ctrl must be up.

| Key | Effect | Pulse of the pointing controller |
| --- | --- | --- |
| Shift and Page Up | `intro_motion` steps: 1, 2, 3, 0 and 1 again | One short pulse for each unit of the new value, one long pulse for 0 |
| Shift and Page Down | `intro_eye_trails` goes between 1 and 0 | Long for 1, short for 0 |
| Shift and Delete | `intro_text_place` goes between 1 and 0 | Long for 1, short for 0 |
| Shift and Home | The push of the hand goes off and on: `intro_hand_push_pct` goes between 0 and its value in the ini (100 when that is 0) | Long for on, short for off |
| Shift and End | `intro_hand_point` goes between 1 and 0 | Long for 1, short for 0 |
| Shift and Insert | `intro_push_3d` goes between 1 and 0 | Long for 1, short for 0 |
| Shift and Backspace | `intro_hand_show` steps: 1, 2, 0 and 1 again | One short pulse for each unit of the new value, one long pulse for 0 |
| Shift and Pause | `intro_hand_depth` goes between 1 and 0 | Long for 1, short for 0 |

`DoLogo` ends the logo on each key that goes down. While Shift, Page Up, Page
Down, Delete, Home, End, Insert, Backspace or Pause is down, the mod clears
the last key of the game after each frame (`0x00E85474`), so a press of a
test key does not end the logo.

In play, Shift and Page Down change `eye_clean`, Shift and Delete change
`bubble_card`, Shift and Home change `spirit_world_size`, Shift and End
change `creature_hold_stay`, Shift and Insert change `text_click`, and Shift
and Backspace change `far_take_carry` (`backend.md`, "Keys for v1.0").

### The controller in the stereo intro

- `ControllersFrame` runs once for each pair. With the pointer owned by the
  controller, `pointer_map.cpp` writes the pixel to `0x00E852C0`, and the
  effect reads it as the middle of the push. That pixel is the pixel of the
  aim ray, and with `intro_hand_point=1` the mod puts the pixel of the Hand's
  point there (see "The push of the hand"). The controller gets the pointer
  when the hand moves 2 cm or the aim turns 1 degree. The mouse takes it
  back when it moves.
- Both hands push the orbs in 3D (see "The push in 3D"). With
  `intro_push_3d=0` the pointing hand pushes through the pointer (see "The
  push of the hand").
- The overlay draws both hand models while they move (see "The hand models
  in the intro"). Like the hands in play, they draw only while the
  controller owns the pointer.
- `Usable()` in `controllers.cpp` is false while the intro draws in stereo
  (`IntroBlocksButtons`). No controller button can reach `SetButtons`, so the
  trigger cannot click, and a click cannot end the intro.
- The menu button on either controller sets the done flag.
- The push of a mouse is worked out in the view of the first eye (the left
  one). With the model it uses the camera of the orbs.

### The log

The tag is `intro`:

- One line at the start with each key, and one line for the scale of the room.
- At the first stereo frame: the width of the logo on its plane, in units and
  in meters.
- One line when a new part of the path starts (the orbit, the way to the rest
  pose, the rest pose, the move through the logo). The line has the camera
  that the game asks for, the camera that the mod draws from and its
  direction, and the angle and the distance of the logo from there.
- With `intro_probe=1`: the same line for each second of the effect clock,
  and for each pair of eye pictures the pixel of 8 orbs in each eye.
- `test key`: one line for each press of a test key, with the clock.
- The first flat batch that gets a place, and at the end the counts.
- The first frame in which a hand moves orbs in 3D, with the reach in units.
  At the end: the frames in which the hands moved orbs, the orbs in all, the
  most in one frame, and the frames in which a hand showed.
- With `intro_probe=1`, each 100 pairs: the pointer, the pointer that the
  push of the game read, and the count of orbs more than 1 unit from their
  home. Also the place of the right hand against the camera, its speed and
  its opacity.
- The first eye pass in which the hands draw before the orbs, with the
  triangles.
- With `intro_probe=1`: one line for each kind of draw of the first eye pass
  (the code that draws, and before or after the sky), with the depth states
  of the device and the depth of its first corner.

### The math test

`intro_motion_checks.h` (89 checks) covers the camera path against the camera
that the game asked for in a run, the ease, the fixed heading, the cameras of
each value of `intro_motion` (the place of the game with no turn, the room
that stays still, the model that stays in front of the head), the step of the
test key, the scale and the speed, the pixel of a flat corner on the plane of
the logo for each eye, the motion of the mouse from the motion of the hand
(the percent, the fraction of a slow hand, and no motion after a gap), the
push in 3D (one to one, the soft edge, the reach, a turn of the hand, a hand
that does not move), and the show of the hands (the shake of the tracking
of a hand at rest, the times to come and to go, a hand that is not
tracked).

### A desktop run

`tools/desk/intro_run.ps1` makes a run of the logo in stereo with no skip, with
`intro_probe=1` and a picture of each eye each few seconds
(`tools/desk/README.md`, "The logos"). A run renders 2208 by 2208 with a view
of 110 degrees, and its invented head has 66.8 mm between the eyes. The
difference of a thing between the two pictures is then 1721 pixels over its
distance in units (at 33.3 units for a meter). Measured at the rest pose:

| Thing | Distance | Difference that the distance asks for | In the pictures |
| --- | --- | --- | --- |
| An orb (the log has the pixel of 8 orbs for each eye) | 70 to 130 units, for example 84.1 | -20.4 px for 84.1 units | -20.4 px. An orb with no other orb over it is less than 1 px from its pixel in the log |
| The logo as one picture | 70 to 130 units | -24.6 to -13.2 px, -17.8 px in the mean | -17.7 px (-15.6 px with `intro_eye_trails=0`: the second picture of each orb pulls it to 0) |
| The mark "TM" | 100 units, the plane of the logo | -17.2 px | -17.0 px (0.0 px with `intro_text_place=0`) |
| The sky | A sphere of about 160 units around the logo | Each part gives a point on one sphere | 157 to 163 units from the origin |
| The fade | A black picture with an alpha, the same in each eye | none | none |
| A second picture of an orb at its place for the other eye | It must not be there | 0 percent | 0 to 5 percent of the orb at the edge of the logo (22 to 33 percent with `intro_eye_trails=0`) |
| A second picture of the mark at its place for the other eye | It must not be there | 0 percent | 0 percent (10 percent with `intro_eye_trails=0`) |

With `intro_motion=2` the room has 50 units for each meter, and each
difference is 1.5 times as large: the mark is -25.3 px apart (its distance
asks for -25.8 px), and the sky is on the same sphere. In the orbit the orbs
are at the pixels of the camera of the orbs, and the sky is the sky of the
rest pose.

### What only a headset can show

No headset session has seen this state, so `intro_vr` stays 0.

- Whether the doubling is gone for the eye. The orbs are 3.6 cm apart on a
  grid, and the cloud is 1.8 m deep at 3 m. Two orbs that are neighbors in the
  picture can be far apart in depth, and the eye can then see one of them
  double. That is the geometry of the scene, not a fault of a draw. A larger
  `intro_distance_cm` makes each eye difference smaller.
- Whether the motion of `intro_motion=1` is comfortable, and if not, whether
  `intro_motion=2` is. Which value is the default.
- Orbs that pass near the eyes with `intro_motion=1`: an orb a few cm from one
  eye shows large in that eye only.
- The size of the logo. The effect builds the logo in mode 0, before the mod
  sets the view of the headset at the first stereo frame. So in a headset the
  logo can be smaller than in a desktop run, which has its view from the
  start [I]. The log has the width.
- The frame rate with two effect calls and four copies of the picture for the
  keeps. On the desktop each call includes a `Flip` that waits for the
  display. With a headset `PresentWatchSkipDesktop` skips them.
- The menu button skip, and the pulses of the test keys.
- The push in 3D: whether the orbs move with the real hands, and whether the
  reach is right (`intro_push_radius_cm`). The scripted hands of a desktop
  run move in steps, and a real hand moves a little in each frame.
- The show of the hands: whether the shake of the tracking of a hand at rest
  stays below the speed of a motion, and whether the times to come and to go
  feel right.
- Which `intro_distance_cm` puts the logo in the reach of the arms and still
  shows it well.

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

## The DirectX check

At its start the game asks `DSETUP.dll` of its folder for the version of
DirectX (`DirectXSetupGetVersion`, ordinal 11). When the answer is below
4.07, it stops with "You need DirectX 7 or greater" (`runblack.exe`
`0x00641CD8`) [V]. `DSETUP.dll` opens the key
`HKLM\SOFTWARE\Microsoft\DirectX` with all access to read the version there.
With no administrator rights Windows refuses that access, also with the
virtual store of UAC. `DSETUP.dll` then takes the version of the file
`ddraw.dll` of Windows, which is 4.06. So with no mod the game starts only as
administrator [V].

Each Windows that runs the mod has DirectX 9, because dxwrapper needs it. So
the mod gives the answer that Windows gives with administrator rights:
4.09.00.0904. It puts its own function in the import slot of
`runblack.exe` for that ordinal, when the mod loads (`dx_check.cpp`). The log
names the answer:

```
dx check     : the game asked DSETUP.dll for the version of DirectX. The mod answered 4.09.00.0904 (dx_check=0)
```

| Key | Default | Effect |
| --- | --- | --- |
| `dx_check` | 0 | 0: the mod answers the check. 1: the check goes to `DSETUP.dll`, as in the plain game |

## Open work

- A push of the hand in the flat intro (`intro_vr=0`). There the controller
  frame does not run, and the controller does not move the pointer.
- A speed for the orbs that a hand moved. The push in 3D moves the place of
  an orb only, so an orb stops when the hand stops. The push of the game also
  adds the move to the speed of the orb. That needs the offset of the speed in
  the record of `0x34` bytes [I]. A short haptic pulse when a hand moves orbs
  is an easy extra.
- With `intro_motion=3` the camera of the game turns, and the room of the
  head turns with it. The push in 3D takes that turn as a turn of the hand.
- The headset check of the stereo intro (see "What only a headset can
  show").
- Not verified: the exact particle count and array strides (the `0xC000` bound
  and the strides `0x34` and 12 suggest 4096 particles [I]), and that
  `0x00E85304` holds the mouse button bits.
- The Bink videos (`PlayLogoScreens`, `PlayPreIntroVideo`) stay flat on the
  existing flat path. A small quad in the room is the right home for them.
- Do not ship `lhlogo.raw`, the Bink files or any Lionhead art. The mod reads
  them from the install of the player.
