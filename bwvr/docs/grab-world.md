# M7: move by grabbing the world

This file is the design for roadmap step M7: hold a button and pull the
world with a motion controller, as in Google Earth VR. Sections 1 to 8 are
the design. Section 9 says what is built, what changed from the design, and
what the desktop runs showed.

## Summary

- The game's land drag is not a grab. It keeps the grabbed land on a ray from
  the game camera through the Hand's direction. The land moves by an angle
  times the range, not by the hand's motion. At life size the world moves 10
  to 130 times as far as the hand. It ignores pulling the hand toward you. In
  the direct hand mode, a head lean also moves the land.
- A true grab moves the world by the hand's motion in 3D. The design uses a
  gain that is fixed for the whole grab, so the land looks glued to the hand.
  True 1:1 is a setting.
- The camera moves through the game camera: the six GCamera Zoomers, exactly
  as the snap turn does. An offset in the mod's room basis L does not work at
  this scale, because the game logic would not follow it.
- The off hand's grip is the grab button. It takes nothing from pick up, the
  land drag, casting, the Creature or the menus. The dominant grip on empty
  land can also grab, as an option. Both grips give two hand pan, turn and
  zoom.
- The limits are the game's 3500 unit pull back, a floor above the land, and
  a stop at release with no glide.
- No new game hook is needed. All the writes go through code that exists.

## How to read this file

- **[V]** Verified. Read in the code, or measured in a doc that says so.
- **[C]** Computed here from [V] facts. The arithmetic is shown.
- **[I]** Inferred. Not read or measured. Each [I] has a test in section 6.

World units: 7.5 units per meter (`head_pose.cpp:101`, `hand.md` line 15).
File and line numbers are from the tree at commit `284e870` plus the working
copy. The other agents edit the tree, so lines can move.

## 1. What the game's land drag does

### 1.1 The path today [V]

1. The trigger is the left button. `controllers.cpp` `PumpButtons` (778 to
   835) sends it through `LHMouse::SetButtons`.
2. In the direct hand mode, the Hand's pixel is the reach point seen from the
   eye (`ControllerRenderPixel`, `controllers.cpp:983`, and `PointPixel` in
   `controller_math.h`). The reach point is on the line from the eye through
   the grip (`ReachPoint`, `controller_math.h:171`). So the pixel is the
   direction eye to grip. The Go-Go reach changes the distance only.
3. `CameraModeNew3::Update` starts a land drag when the left button goes down
   on land. `UpdateClickParams` `0x00459610` stores the grab point, the drag
   plane (mode `+0x14C`, `+0x158`) and the grab camera
   (`pointer_map.cpp:976`).
4. Each frame the camera code casts two rays through
   `UnprojectScreenPoint` `0x0081B370`: the press pointer at `0x0045D444` and
   the pointer now at `0x0045D456`. It cuts both rays with the plane and
   moves the camera by the difference (`0x0045D365` to `0x0045D96E`,
   `backend.md` 1733 to 1740).
5. The mod gives both rays the Hand's world direction `L * H * ray`. Only the
   direction changes. The origin stays the game's own camera
   (`h_Unproject`, `pointer_map.cpp:923` to `963`, and `HandRay`, 488 to 512).
6. The step is capped by the pixel distance from the press times the view
   distance times 0.011. The mod sets that distance to the angle the Hand
   turned since the press (`KeepStepLimit`, 654 to 676).
7. The player's drag smoothing, 35 ms, eases the mouse motion into the Hand
   (`DragShare`, 1077, and `TakePending`, 1089). At the release the rest
   arrives at once, so nothing moves after the release. The glide time
   `0x009CE614` is 0 (`comfort_camera.cpp:636`).

### 1.2 How far the world moves for 10 cm of hand [C]

The drag depends on the direction eye to hand. A sideways hand move `x` at the
real distance `d` turns the direction by `x / d`. The grabbed land at range
`R` from the game camera then moves about `R * x / d` along the plane. The
plane is the game's choice, so the exact factor for a steep plane is [I]. A
level pull is the base case.

The hand moves 10 cm, which is 0.75 units. One to one would move the world
0.75 units.

| Hand distance d | Turn | Land at R 50 | R 150 | R 400 | Factor at R 150 |
| --- | --- | --- | --- | --- | --- |
| 0.40 m (near, life size) | 0.250 rad | 12.5 units | 37.5 units | 100 units | 50 |
| 0.65 m (arm stretched, Go-Go 20 m) | 0.154 rad | 7.7 units | 23.1 units | 61.5 units | 31 |

The camera distance from its focus is 100 to 700 units in normal play
(`tabletop.md:318`). So the factor is 17 to 130 at 0.40 m [C].

The Go-Go reach does not change these numbers. The drawn Hand moves `x` times
`D / d` (a factor of 10.7 at 50 cm, 30.8 at 65 cm, from `ReachDistance`,
`controller_math.h:152`). At the land, that is the same distance as the land
moves. So the Hand looks glued to the land where the Hand and the land are at
the same range. Where they are not, the Hand floats in front of the land and
the land still moves by the angle [C].

### 1.3 What it lacks for a 1:1 grab

1. **Angle, not position.** The world follows the direction of the hand from
   the eye. The gain is `R / d`. It is 17 to 130 at life size, and it changes
   with the range of the grabbed point [C].
2. **No depth.** Pull the hand toward the eye along the ray and the angle
   does not change. Nothing moves [C from 1.1 step 2].
3. **Height only by accident.** Moving the hand up and down turns the ray up
   and down. On a level plane that moves the camera along the land. A steep
   plane moves it up and down (`LogPlane`, `pointer_map.cpp:732`, [V] that the
   game can make one). The player cannot choose height [I].
4. **The head moves the land.** The ray is eye to hand, and the eye moves with
   the head. Lean 10 cm with a still hand at 0.40 m and the direction turns
   0.25 rad. `HandRay` has no lean compensation. So a lean during a drag moves
   the land by the numbers in the table [C, and I that nothing else cancels
   it]. Test 8 in section 6 checks it. Turning the head is safe, because the
   Hand is locked to the room (`LockToRoom`, 357 to 420, measured in
   `backend.md` 1576 to 1611).
5. **The ray starts at the game camera, not the eye.** `h_Unproject` keeps the
   origin from the stored camera at `0x00EA1DB8`, which is the game camera
   with no lean (`pointer_map.cpp:952`). The direction comes from the eye. A
   lean of 30 cm (2.25 units) is an origin error of about 1.3 degrees at range
   100 [C].
6. **No rotation, no scale, one hand.** The drag is one hand and only pans.
7. **Grab point needed.** The drag needs land under the Hand at the press. A
   press over the sea or the sky becomes a tilt or a turn, or nothing
   (`PlaceGamePoint`, 702 to 726).

### 1.4 What is good and stays

The game's drag is exact and the author liked its smoothing. It is the tool
for a precise nudge of the land under the Hand. It stays on the trigger. The
grab is a second tool for large moves, height, turning and zoom.

## 2. A true grab of the world

### 2.1 The rule

While a grab is held, one point in the world stays attached to the hand, and
no other motion is made. A still hand gives a still camera, whatever the head
does. The release stops the camera at once.


### 2.2 The math

**Frames.** `HeadPoseToRoom` gives a hand point `a` in the room, in units
(`controllers.md` 2.7, `head_pose.cpp:309`). The world point is
`C + L * a`, with `C` the game camera position and `L` the level basis
(`HeadCameraStageBasis`, `head_camera.cpp:384`). The lean is part of the room
coordinates, so a head that moves with a still hand changes no `a` [C from
`controllers.md` 2.7]. The eye is `E = C + L * t_head`
(`head_camera.cpp:370` to `375`).

**Gain.** At the press, keep the head center `r0` in the room
(`HeadPoseHeadInRoom`). The virtual hand point is
`v = r0 + G * (a - r0)`, with `G = diag(g, g_v, g)`. Then `v` does not move
when the head moves and the hand does not.

- `g = 1` is true 1:1. The world point in the hand stays in the hand.
- The default `g` makes the land look glued to the hand. Use
  `g = clamp(Dcam / dhand, 1, gmax)`. `Dcam` is the distance between the
  GCamera Zoomer destinations for position and focus, measured as
  `SetWheelScale` does (`comfort_camera.cpp:415` to `421`). `dhand` is the
  real distance from the eye to the grip, kept between 0.25 and 0.70 m.
  For a land point at range `R`, the world moves `g * x`, which turns the view
  of that point by `g * x / R`. With `g = R / d` this is `x / d`, the angle the
  hand turns as seen from the eye. So the point stays under the hand in the
  picture [C]. This is the gain of the game's drag at the press, but it is
  fixed for the whole grab. An example: Dcam 150 units and a hand at 0.45 m
  (3.4 units) give `g = 44`. A hand move of 10 cm then moves the world 33
  units.
- `g_v = g * grab_vertical_pct / 100`. Height gets its own scale, because a
  fast height change is the least comfortable [I].

**One hand.** Keep `C_a`, `F_a` (the focus), `L_a` and `v_a` from the press.

```
C(t) = C_a - L_a * (v(t) - v_a)
F(t) = F_a + (C(t) - C_a)
```

This is an absolute solve from the press. It does not add up steps. So a
second call in the same frame gives the same answer, and rounding cannot
drift. This follows the rule in `docs/backend.md`, "One frame, one head".

**Two hands.** Keep both `v_i` at the press. Let `m` be their midpoint and `s`
the vector between them.

```
A     = C_a + L_a * m_a            the world point under the midpoint
k     = |s(t)| / |s_a|             the spread
theta = the change of the yaw of s, about up
L(t)  = Rot(up, -theta) * L_a      the world turns with the hands
C_r   = A - L(t) * m(t)            the rigid solution
C(t)  = A + (C_r - A) / k          the zoom, toward the midpoint
F(t)  = C(t) + Rot(up, -theta) * (F_a - C_a)
P(t)  = C(t) + Rot(up, -theta) * (P_a - C_a)     the pivot at mode+0x12C
```

With `k = 1` and `theta = 0` this is the one hand formula. The turn is about
the vertical axis only, so `L` keeps world up and the horizon stays level.
The zoom is a dolly, not a change of the world's size. The eye separation and
the lean keep their size. This matches the game's own wheel zoom, which is a
dolly too, and it needs no change of `HeadPoseSetScale` [I that it feels
right]. Below 8 cm of real spread, the turn and the zoom freeze. The spread
`k` stays between 0.25 and 4.

**Re-anchor.** Any change of the grip set (one hand to two, two to one)
starts a new anchor from the current camera. So the camera never jumps at a
change. A recenter (`HeadPoseRecenter`) or a hand jump of more than 0.5 m in
one frame also re-anchors, as `LockToRoom` skips a head jump
(`pointer_map.cpp:389`).

**Smoothing.** The player approved smoothing of their own drag. The applied
pose follows the solved pose with share `1 - exp(-dt / tau)` a frame, the
same form as `DragShare`. The default `tau` is 35 ms. At the release the
remainder is dropped, not applied. The camera stays where it is. This is
stricter than the drag, which applies the rest at once to keep the land under
the Hand. A grab has nothing to keep.

### 2.3 Where the camera moves

| | A: the game camera (Zoomers) | B: an offset in the mod's room basis |
| --- | --- | --- |
| How | Write the six Zoomers and the mode's copies, as `SnapTurn` does (`comfort_camera.cpp:324` to `366`) | Add an offset to `moved_position` in `h_UpdateCamera` (`head_camera.cpp:370`) |
| Sound listener | Follows [V: reads `0x00EA1DB8`, `audio.md` 3] | Does not follow |
| Picking, the Hand's ray | Follows. The stored camera is the game camera (`head_camera.cpp` 450 to 470) | The ray starts at the game camera, so it misses the land under the Hand by the whole offset |
| Detail distance, clouds, help triggers, script natives | Follow [V: `tabletop.md` 2.5] | Do not follow |
| 3500 pull back | Applies, and the grab keeps clear of it | Never sees the offset, so it grows without a limit |
| Save game, network packet | Carry the real camera | Carry a camera that is not the one drawn |
| Cost | The mode's own fields must stay in step | One vector, and everything else wrong |

**Use A.** B is right for the tabletop mode, where the table is a synthetic
camera and the game camera stays at a sensible place (`tabletop.md` 2.6). It
is wrong at 7.5 units per meter, where the player is in the world.

The writes go in `h_ModeUpdate` (`comfort_camera.cpp:552`), after
`RunGameUpdate` and `FinishFlight`, where the snap turn writes. For a move
the code sets:

- the Zoomers of position (`+0x118`) and focus (`+0x88`) with
  `Zoomer::SetPosition` `0x00441AC0`. It also zeroes the speeds, so the next
  `Zoomer::Update` keeps the value [V: the snap turn test, `backend.md` 1696
  to 1705].
- the mode's copies at `+0xC`, `+0x18` and `+0x1D4`, and the pivot at
  `+0x12C`.
- for a turn, the heading at `+0x50`, by the change of
  `GetHeadingAndPitchFromPoints` `0x004428D0`, as `SnapTurn` does.
- any flight in progress is finished first: `FinishFlight` and the flags at
  `+0x100` and `+0x101`.

The order inside a frame is [V from `head_camera.cpp` 1 to 20 and
`stereo.cpp:829`]: `GCamera::Update` runs the mode update, the Zoomer update,
the 3500 check and the main `UpdateCamera` call. Then the scene walk runs, and
`ControllersFrame` and `HandDirectFrame` run at its start. So the grab is in
the same frame as the pointer, the Hand's target and the picture. The Hand's
target uses this frame's eye [C from that order].

### 2.4 What the game logic sees

| Reader | Effect |
| --- | --- |
| Sound listener (`GAudio::ProcessAudioGameTurn`) | The listener moves with the game camera. Wanted. `audio_head` adds the head |
| The Hand, picking, `GInterface::CalculateOverObject` | See the moved camera in the same frame |
| Land drag state | Not started. The grab keeps the left button from the game while it is active (section 4) |
| `GCamera::IsMoving` (`+0x74`) | True while the stored camera changes. The interface then clears the gesture trail [V, `backend.md` 2135]. So a miracle stroke cannot run during a grab. Right |
| Tutorial and `CameraHelp` | The game hears no drag event, so a tutorial step "drag the land" may not complete by a grab. See open question 2 |
| Advisor anchor (`spirit_anchor.cpp`) | Latches on a game camera jump or turn between two frames. A fast grab can trip it. See risk 6 |
| Script and story cameras | The grab needs `CameraModeNew3` on top and `+0x2EC == 0`. A story mode is never touched |

### 2.5 No fight with the other camera code

- **`comfort_camera.cpp`.** One owner writes the camera fields. The grab
  math lives in a new file, and the write is a new function in
  `comfort_camera.cpp` that uses `ArriveNow`, `SetZoomer`, `TurnAbout` and the
  mode offsets it already has. `MayTurn` (293) also guards the grab's turn: the
  script's turn bit, the forced pivot and the fight flag.
- **Snap turns and the stick zoom** are refused while a grab is active. The
  stick turn request is dropped in `PumpStick` (`controllers.cpp:838`), and
  `MayTurn` says no. The middle mouse turn is refused the same way. Each
  refusal is counted in the log.
- **The wheel gate** (`PrepareWheel`, 446) answers "no wheel" while a grab is
  active, so a wheel step cannot add to the distance in the same frame.
- **`pointer_map.cpp`.** No game button is down during a grab, so `held` is
  false in `h_GCameraUpdate` and the safe box applies as for any hover. The
  land drag counters read zero.
- **`script_camera.cpp`.** It only holds shots for story modes. It is not
  active when `CameraModeNew3` is on top.
- **The game's own automatic motion.** The ground lift in `GCamera::Update`
  [I: a comment in `script_camera.cpp:404` says it calls `GetAltitude` before
  the main call], the automatic tilt, and the 3 s pull back past 3500 are
  motion the player did not make. The grab keeps clear of the last two by its
  own limits (section 3).
- **External moves.** Before the game update, the code compares the Zoomer
  destinations with what the grab wrote last frame. A difference means another
  system moved the camera. The grab ends at once and re-anchors on the next
  press.

## 3. The limits, and the release

| Limit | Value | Evidence |
| --- | --- | --- |
| Pull back | `GCamera::Update` pulls any camera more than 3500 from (2560, 0, 2560) back over 3 s. The height counts. Test against 1.225e7 at `0x00442288`, code `0x0044224A` to `0x0044232F` | [V] `tabletop.md:287` |
| Grab radius | `grab_limit`, 3400 by default. It applies to the camera position and to the focus | [I] that the game tests the focus too. Test 6 |
| The land | 512 by 512 cells of 10 units, so 5120 by 5120. Every land fits in a circle of 1928 around its center (`R far`), and the centers are within 2235 to 2715 in x and 2510 to 3030 in z | [V] `tabletop.md` 1.1 to 1.2 |
| Height above land | `grab_min_height`, 6 units. It is `GetAltitude` `0x00803090` at the camera x and z, plus 6. `script_camera.cpp:452` does the same with 5 | [V] the function, [I] the value. Test 7 |
| Height ceiling | The 3500 sphere is the ceiling. Add `grab_max_height` only if the headset says so | [C] |
| Distance to the focus | Not changed by a move. The zoom floor of 3.1 stays the game's | [V] `backend.md` 1802 |
| Zoom toward the midpoint | The camera stays 5 units or more from the anchor `A`. `k` stays in 0.25 to 4 | [I] the values |
| Off the map | `GetAltitude` rejects cells outside 0 to 511. The floor there must be 0, so the code checks the result | [V] the rejection, [I] the return value. Tests 6 and 7 |

**At a limit.** The camera stops at the wall. The hand keeps moving, and the
world slides through it. The solve is absolute, so there is no dead zone. When
the hand comes back inside, the camera leaves the wall at the point where the
solved pose crosses it. A short haptic tick marks the wall (`controller_haptics`).
The focus and pivot follow the position by the same offset.

**At the release.** The camera stops. The Zoomers hold their values, because
`Zoomer::SetPosition` zeroes the speeds and the duration. The remainder of the
smoothing is dropped. The grab never starts the game's glide. With
`camera_glide=1` the game's own glide stays for its own moves, and the grab
still stops at once. A glide after a grab needs its own key, and it is not in
the design [I that the player wants one].

## 4. Buttons

### 4.1 Facts [V]

- Only the pointing hand acts today. `PointingIndex` is the right hand, or the
  left with `controller_left_handed` (`controllers.cpp:565`). The other hand
  has no action. Its pose and its squeeze are read already
  (`g_hands[0]`, `g_hands[1]`, 931 and 932).
- The dominant trigger is the left button. The dominant grip is the right
  button (`RawDown`, 589). `controller_swap_buttons` exchanges them.
- The right button picks up, holds and throws. A left press on a holdable
  object starts a land drag (`hand.md` 696 to 702).
- Left and right together give the game's both button mode at `0x00C5B0B0`.
  Comfort mode already blocks the turn and tilt it makes
  (`comfort_camera.cpp:576`).
- The gesture ring has no button gate (`hand.md` 490 to 495).
- The stick turns and zooms. A, X, Y, B, the menu and the stick click of the
  dominant hand are read as pointer events only (`controllers.cpp:959`).

### 4.2 The mapping for the Quest Touch controllers

| Input | Action | Change |
| --- | --- | --- |
| Dominant trigger | Left button: click, land drag, drop, cast, stroke the Creature | None |
| Dominant grip | Right button: pick up, hold, throw | None. Option: on empty land it grabs |
| Dominant stick | Snap turn, zoom steps | None. Refused while a grab is active |
| Dominant A | The jump | None |
| **Off hand grip** | **Grab the world with the off hand** | New |
| **Both grips** | **Pan, turn and zoom with two hands** | New |
| Off hand trigger, stick, X, Y, menu | Nothing | None. Free for later |

The off hand is the main way in, for four reasons:

1. It cannot take pick up from the dominant grip.
2. It works while the dominant hand holds an object. The player can carry a
   villager and pull the world.
3. It never asks whether the Hand is over land. The Hand's pick is a frame
   old, so a grip claim on the dominant hand can take a pick up by mistake.
4. It needs no change to what the game sees. No game button is sent for it.

**The dominant grip on empty land** is the option `grab_hand=2`. The mod
claims the press, and the game gets no right button, only when all of these
hold at the press frame:

- the Hand state (`hand+0x4878`) is Normal (1) and the held object
  (`hand+0x4904`) is zero [V fields, `hand_direct.cpp:110`, `hand.md` 119].
- the direct hand has no candidate (`Choose`, `hand_direct.cpp:363`) and the
  interface pick (`+0x3C8`) is empty.
- no game button is held, and the trigger is up.

The claim is decided at the press and kept until the release. What a right
press on bare land does in the game is not known [I]. Test 11 checks that the
game sees no right button while the grab holds it.

**Two hands.** The off grip goes down first, or both within 250 ms. The
dominant grip then joins under the same claim rule. With an object under the
dominant hand, that grip stays the game's pick up, and the off hand keeps its
one hand grab.

**The trigger during a grab.** The dominant trigger is not sent to the game
while a grab is active. A drag would fight the camera writes. A trigger that
is already down at the press of a grip blocks the grab until it is up. One
thing at a time.

**Casting and the Creature.** A miracle is a seed in the hand and a gesture
on the pointer. The grab does not touch either. The gesture trail clears
while the camera moves, as it does for the game's drag [V]. In the Creature,
Totem and other states the claim rule refuses the dominant grip, and the off
hand still works.

**The menus.** `Usable()` is false for a menu, a loading screen or a video
(`controllers.cpp:570`, `StereoIdle`). A grab that is active ends at once, as
`ReleaseController` does for the buttons. A grab needs a new press.

### 4.3 The other profiles

| Profile | Grip source | Grab button |
| --- | --- | --- |
| `oculus/touch_controller` (Quest) | `squeeze/value`, analog | Off hand grip. Press above 70 percent, release below 50 |
| `valve/index_controller` | `squeeze/value`. A loose hand can rest at 30 to 50 percent [I] | Same. The higher press level keeps a relaxed hand from grabbing |
| `htc/vive_controller` | `squeeze/click`, the side button | Same. It is a boolean, so the levels do not matter |
| `microsoft/motion_controller` | `squeeze/click` | Same |
| `khr/simple_controller` | none bound (`controllers.md` 2.3) | The off hand's select (its trigger). The dominant hand cannot grab |
| Left handed play | `controller_left_handed=1` | The left hand is dominant. The off hand is the right. The mapping mirrors |

The stick on Vive needs its click for the turn and zoom (`StickLive`, 596).
The grab does not use the stick, so nothing changes.

## 5. The design

### 5.1 Pieces

- **`world_grab.cpp` (new).** The state machine and the solve of 2.2. It has
  no game hooks. Its inputs are the two grip positions in the room, the
  squeeze levels, the head center, the camera fields and a frame serial. Its
  output is a camera pose and a turn.
- **`controllers.cpp`.** Two additions. `ControllersGrabHands` gives, for each
  hand, `active`, the grip point in the room (the same `HeadPoseToRoom` call
  that `Compute` makes, line 413), and a squeeze edge with its own levels.
  The pump withholds the game's left button while a grab is active and the
  right button when the claim rule says so (`PumpButtons`, 816 to 834).
  `PumpStick` drops the turn and zoom while a grab is active.
- **`comfort_camera.cpp`.** One function, `ComfortCameraGrabMove`, called from
  `h_ModeUpdate` after `FinishFlight`. It writes the fields of 2.3 and
  reports the mode's state back: input off (`+0x2EC`), a fight (`+0x44`), a
  forced pivot (`+0x88`), the turn bit. The game's tutorial can then stop a
  grab.
- **`hand_direct.cpp`.** Nothing. The Hand's target is latched in the scene
  walk, after the camera, so it uses the moved eye [C].
- **`launcher.cpp`.** Add the keys of 5.2 to the launcher list and add the
  `fake_controller_left_*` keys to `kRetiredKeys` (`controllers.md:678`).
- **The hint texts** (`controller-hints.md`, H tasks). Add a "Grab the world"
  row: off hand grip.

No new MinHook hook. The game addresses used are the ones the mod already has:
`0x00441AC0`, `0x004428D0`, `0x00803090` and `0x004427B0`
(`MakeMapCoords`, `script_camera.cpp:104`).

### 5.2 Keys, all in `[bwvr]`

| Key | Default | Meaning |
| --- | --- | --- |
| `grab_world` | 1 | Master switch. 0 gives today's behavior |
| `grab_hand` | 1 | 0 off, 1 the off hand grip, 2 also the dominant grip on empty land |
| `grab_gain_mode` | 1 | 1 the land looks glued to the hand, 0 true 1:1 |
| `grab_gain_pct` | 100 | Multiplies `g` |
| `grab_gain_max` | 60 | The most `g` can be |
| `grab_vertical_pct` | 100 | Height gain, as a share of `g` |
| `grab_smoothing_ms` | 35 | The ease. 0 turns it off. Same form as `drag_smoothing_ms` |
| `grab_two_hands` | 1 | 0 keeps one hand only |
| `grab_turn` | 1 | Two hand turn. 0 keeps pan and zoom |
| `grab_zoom` | 1 | Two hand zoom |
| `grab_press_pct` | 70 | Squeeze level that starts a grab |
| `grab_release_pct` | 50 | Squeeze level that ends it |
| `grab_limit` | 3400 | Radius from the map center, in units |
| `grab_min_height` | 6 | Units above the land |
| `grab_max_speed` | 0 | Units a second the camera may move. 0 is no cap |
| `grab_probe` | 0 | Logs the game's land drag gain (stage G0) |

`fake_controller_left_points` and the script words `lgrip`, `lpoint` and
`lpose2` drive the left fake hand (section 6). A test hotkey for an A/B is
also needed, for example a free F12 combination, and Buck reads symptoms from
it (`test_keys=1`). `stereo.cpp:487` to `516` lists the keys in use. The game
reads a held Ctrl as its zoom, so the combination must avoid Ctrl.

### 5.3 Stages

| Stage | What it adds | Done when |
| --- | --- | --- |
| **G0** Measure | With `grab_probe=1`, log the hand's move and the camera's move for each land drag, and the factor. Log the drag plane's angle. No behavior change | The table in 1.2 is confirmed or corrected with numbers from the fake hand. This also checks the [I] items of 1.3 |
| **G1** One hand, off hand | `world_grab.cpp`, the controllers accessors, the camera write, limits, smoothing, the release. Translation only, `grab_hand=1` | Tests 1 to 8 and 13 to 15 pass |
| **G2** Dominant grip | The claim rule and the button withholding. `grab_hand=2` | Tests 11 and 12 pass |
| **G3** Two hands | Turn and zoom, the re-anchor | Tests 9 and 10 pass |
| **G4** Comfort and polish | The wall haptic tick, a vignette while the camera moves (the vignette is the "next step" in `backend.md` 1710), the tutorial events, the advisor anchor cut, the launcher and the hints | A headset session with no symptom that the player can tie to the grab |
| **G5** Tabletop | The same solve moves the table's `C`, `psi` and `s` (`tabletop.md` 6, "Roadmap step M7 ... is this same pan, turn and zoom") through a second sink | After track 2 T2 |

Each stage has an ini key and a log line that proves it acted. Buck tests
each in the headset with a hotkey A/B before the default changes.

## 6. Tests on the desktop

The desktop rig from `backend.md` ("A headset is not needed to test most of
this") with `fake_head=1` and `fake_controller=2`. The fake hands stand still
and change only in steps. No test moves anything continuously. A speed test
uses two poses one frame apart.

**New test aids.** The left fake hand needs its own points and button. Add
`fake_controller_left_points` (`right,up,forward` in cm, as
`fake_controller_points`), and the script words `lpoint@frame+n`,
`lgrip@frame+n` and `lpose2`. `FakeHand` (`controllers.cpp:216`) drives the
right hand only today [V]. Add `fake_head_right_cm`, `fake_head_up_cm` and
`fake_head_forward_cm` for a still head lean, because test 7 needs one. The
log tag is `grab`. A line prints the anchor error: the world point that was
in the hand at the press, minus the hand's world point now.

| # | Setup | Pass |
| --- | --- | --- |
| 1 | Grip held 60 frames, hand still | The camera changes by less than 0.001 units. The count of moving frames is 0 |
| 2 | One step: the left hand moves 10 cm right. `grab_gain_mode=0`, `grab_smoothing_ms=0` | The camera moves 0.75 units, opposite, along the room's right axis. The anchor error is under 0.01 units |
| 3 | Same with the gain on (`grab_gain_mode=1`) | The camera moves `g * 0.75` units. `g` matches the log line of `Dcam / dhand` |
| 4 | One step up, 10 cm, then a step toward the head | The camera moves down, then along the pull, by the same rule. Height and depth both move |
| 5 | Release after a step. Watch 60 frames, also with `camera_glide=1` | The camera stays at the released pose to 0.000. No glide |
| 6 | A far step so the target is beyond `grab_limit`, then a step back | The camera stops at the limit, distance from (2560, 0, 2560) equals `grab_limit` to 0.01. It leaves the wall only when the target is inside. The log counts the wall frames |
| 7 | A step down to below the land. Check the ground with `GetAltitude` in the log | The camera y equals the ground plus `grab_min_height` |
| 8 | Head only: the fake head steps its yaw, pitch and lean while the grip is held and the hand is still | The camera does not move. A lean does not move the world. This is the difference from the game's drag, which should move the land (G0 records it) |
| 9 | Two hands, spread 40 cm to 60 cm (`k = 1.5`) | The distance from the camera to `A` becomes 1/1.5 of the start, to 0.001. The heading is unchanged |
| 10 | Two hands, turn: the left hand back 10 cm and the right hand forward 10 cm, 40 cm apart | The heading changes by `theta` to 0.01 degrees, `A` stays fixed (anchor error under 0.01 units), the horizon stays level |
| 11 | `grab_hand=2`, the fake dominant grip on empty land. Then again with `seek` onto a villager | On land, the log shows a grab and no right button down. On the villager, the log shows the right button and the pick up, no grab |
| 12 | The off grip while the trigger is down. Then the trigger during a grab | No grab starts. No left button reaches the game during the grab |
| 13 | A snap turn request and a stick zoom during a grab | Both refused, and counted |
| 14 | A grab, a release, then a wheel step and a snap turn | No swing back. The camera moves from where it was released. This checks the mode's stale fields |
| 15 | A story scene (`+0x2EC` not zero) and a menu (`StereoIdle`) | No grab starts. An active grab ends |
| 16 | A land drag with the trigger only, and the mouse | The same drag counts and the same 0.0 unit hold as before (`ReportHold`) |
| 17 | The grab with a villager in the Hand (`seek`, grip on the dominant hand, off hand grab) | The villager stays in the Hand. The camera moves. The Hand target stays on the grip |

**Not testable on the desktop:** tracking noise at gain `g`, the frame age of
the hand pose against the display, squeeze levels on real Touch and Index
controllers, and comfort. These need the headset. The log records the
largest camera step in a still hold, so a headset run shows the noise.

## 7. Risks

1. **Comfort.** The camera moves by the player's hand, and it is fast at a
   gain of 44: a 1 m/s hand moves the world 330 units a second [C]. A turn
   about the vertical axis is the strongest trigger. Defaults are on. The
   answers are the gain keys, `grab_turn=0`, `grab_max_speed`, and the
   vignette in G4. Buck decides after a headset test.
2. **A false grab.** A loose Index hand, or a hand that closes around a
   controller, can hold above the press level. Any hand motion then moves the
   world. Higher levels, a haptic tick at the start, and a log of grab starts
   help. The rule "no motion the player did not make" is at stake here.
3. **Tracking loss.** A controller that loses tracking jumps. The 0.5 m rule
   re-anchors, and a lost hand ends the grab.
4. **Stale mode fields.** The Zoomers are not the only camera state. A field
   that the mod does not set can pull the camera back at the next zoom or
   turn. The snap turn and the jump list the fields that matter (`+0xC`,
   `+0x18`, `+0x1D4`, `+0x12C`, `+0x50`, `+0x78`, `+0x100`). Test 14.
5. **The game's automatic motion.** The ground lift, the automatic tilt and
   the pull back can move the camera during a grab [I for the lift and the
   tilt]. The grab overrides the camera each frame with its own solve, so it
   cancels them. Tests 6 and 7 catch a fight.
6. **The advisor anchor.** `spirit_anchor.cpp` lands the spirits again when the
   game camera jumps or turns between two frames. A fast grab might trip that
   test each frame, and the spirits would pop. Read the thresholds in G4.
7. **The tutorial.** A step that waits for a drag may not see a grab (open
   question 2).
8. **A claim by mistake.** The dominant grip claim uses a pick that is one
   frame old. A fast grip on an object might start a grab. This is the reason
   the off hand is the default.
9. **One frame of latency.** The hands are the ones from `BeginFrame` N, and
   the camera uses them in frame N+1. The runtime does not reproject the
   world for hand motion. At 330 units a second and 90 frames a second the
   world lags the hand by about 3.7 units, which is 1.4 degrees at range 150
   [C]. It is the same lag the pointer has (`controllers.md` 6).
10. **`GetAltitude` off the map.** Its result outside cells 0 to 511 is not
    known. Tests 6 and 7 and a guard on the result.
11. **The recenter.** The room origin changes. The re-anchor rule handles it.
12. **The tabletop mode.** It owns the game camera in the player's modes
    (`tabletop.md` 2.6). The grab must go through the tabletop sink then (G5),
    or the two will fight.
13. **The both button mode.** If the game ever gets both buttons through the
    grip and the trigger, it makes its own camera motion. The claim rule and
    the blocked features (`comfort_camera.cpp:578`) already stop it. Test 12.
14. **The gain is fixed for a grab.** With a big zoom, `Dcam` changes and `g`
    does not. The gain is right at the press and at each re-anchor only.

## 8. Open questions

1. What the game does with a right press on bare land, in each Hand state.
   This decides whether the dominant grip claim (G2) changes anything the
   player uses.
2. Which `CameraHelp` event a land drag sends, and which bit of
   `EnabledFeatures` gates dragging in the tutorial (`comfort_camera.cpp:72`
   lists only tilt, turn and the zoom bit 4). If a grab sends the event and
   honors the bit, the tutorial keeps working.
3. Whether `GCamera::Update` tests the focus against 3500, or only the
   position.
4. What `GetAltitude` returns outside the map.
5. Whether the game lifts the camera above the land inside `GCamera::Update`,
   and how fast.
6. Whether a real world scale (`HeadPoseSetScale`, the eye separation, the
   Hand's size) should follow the two hand spread, in place of the dolly. The
   dolly is simpler and keeps the stereo depth cues stable. A true scale is a
   track 2 topic (`tabletop.md` 6).
7. The frame age of the off hand's pose against the picture in the headset.

## 9. As built (first build, 2026-09-30)

The first build has stages G0 to G3, and a part of G4. G5 (the tabletop)
is not started. The code is in `src/mod/world_grab.cpp` (the state and the
log), `controller_math.h` (the solve, the gain and the limits, as pure
functions), `controllers.cpp` (the buttons and the grip points),
`comfort_camera.cpp` (the write) and `hand_direct.cpp` (the claim rule).

### 9.1 What each stage does

| Stage | Key | As built |
| --- | --- | --- |
| G0 | `grab_probe=1` | Each land drag logs the motion of the pointing grip, the head and the camera, from the press to the release, with the tag `drag probe` |
| G1 | `grab_hand=1` | The other hand's grip grabs. Pan and height, the gain, the smoothing, the wall, the floor, the release and the watch after it |
| G2 | `grab_hand=2` | The pointing grip also grabs alone, by the claim rule of 4.2 |
| G3 | `grab_two_hands=1` | Both grips turn about up and zoom. A change of the hands starts a new anchor |
| G4 | | Built: a pulse at the start of a grab and at the wall or the floor, the launcher row, the test key Shift F11. Not built: the vignette, the tutorial events, the advisor anchor cut, the hint texts |

### 9.2 The buttons as built

| Input | During no grab | During a grab |
| --- | --- | --- |
| Other hand's grip | Starts a grab on a press above `grab_press_pct` | Holds it. Below `grab_release_pct` the hand lets go |
| Pointing grip | The right button (pick up) | Joins the grab when the claim rule holds. Otherwise it stays the right button |
| Pointing trigger | The left button | Held back from the game. It must open before it presses again |
| Any new left press | Goes to the game | Held back, the same way |
| Stick | Snap turn and zoom steps | Refused and counted. A refused push must come back before it acts |
| A button | The jump | Refused |
| Mouse wheel, turn keys, middle mouse | As before | Refused and counted in `comfort_camera.cpp` |

A grab starts only on a press, never on a grip that is already closed. A
grip that closes while the trigger is down, or while the game holds the left
button, does not grab. The next grab needs a new press after the trigger is
up. The grab needs the controller to own the pointer, a scene (no menu), and
the player's camera mode with its input on (`WorldGrabCameraReady`). When one
of them goes away, the hands let go of the world.

The claim rule for the pointing grip: the controller places the Hand, the
Hand is in the Normal state and holds nothing, the mod's pick has no choice,
and the game has no object under the Hand (`+0x3C8`). No game button is held
and the trigger is up. The rule is decided at the press and kept until the
release. With `grab_hand=1`, the pointing grip joins only when the other grip
holds first. The design's "both within 250 ms" is not built: the game would
already have the press of the pointing grip.

`khr/simple_controller` has no squeeze, so there the other hand's select (its
trigger) grabs.

### 9.3 Changes from the design

- **The smoothing is on the grips, not on the camera.** The grip points in
  the room ease in by `1 - exp(-dt / grab_smoothing_ms)`, once for each new
  sample. The solve stays absolute from the press, with the eased points.
  This is the player's own motion. At the release the rest is dropped, and
  the camera stays where it is.
- **The gain** is `g = Dcam / dhand * grab_gain_pct / 100`, between 1 and
  `grab_gain_max`. `dhand` is the mean distance of the held grips from the
  head center, between 0.25 and 0.70 m. A new anchor measures it again.
- **A limit that the start already passes becomes the start.** The wall for
  a grab is the larger of `grab_limit` and the start distances of the camera
  and its focus. The floor is lowered by how far the start was below it. So
  a press never moves the camera.
- **The spread comes from the real grips.** The zoom `k` and the turn `theta`
  use the real spread, not the scaled points. Below 8 cm of spread, or when
  the turn is not allowed (`MayTurn`), they hold their last values.
- **A new anchor starts from the camera the grab wrote last**, not from the
  camera after the game's own update of that frame.
- **Another move of the camera ends the grab.** The grip must open and close
  again. A jump in flight makes the grab wait, and it anchors where the jump
  lands.

### 9.4 Keys

The player keys first, then the tuning, then the tests. All are in `[bwvr]`.

| Key | Default | Meaning |
| --- | --- | --- |
| `grab_world` | 1 | Master switch |
| `grab_hand` | 1 | 0 off, 1 the other hand's grip, 2 also the pointing grip alone. The launcher row "Grab the world" |
| `grab_two_hands` | 1 | Both grips turn and zoom |
| `grab_turn` | 1 | The two hand turn |
| `grab_zoom` | 1 | The two hand zoom |
| `grab_gain_mode` | 1 | 1 the land looks glued to the hand, 0 one to one |
| `grab_gain_pct` | 100 | Multiplies `g` |
| `grab_gain_max` | 60 | The most `g` can be |
| `grab_vertical_pct` | 100 | The gain up and down, as a share of `g` |
| `grab_smoothing_ms` | 35 | 0 is no smoothing |
| `grab_max_speed` | 0 | Units a second. 0 is no cap |
| `grab_press_pct` | 70 | The squeeze that starts a grab |
| `grab_release_pct` | 50 | The squeeze that ends it |
| `grab_limit` | 3400 | Units from (2560, 0, 2560) |
| `grab_min_height` | 6 | Units above the land |
| `grab_probe` | 0 | Stage G0. The launcher removes it |

Test keys: `fake_controller_left_points` (right, up, forward in cm, not
mirrored), the script words `lpoint`, `lgrip`, `lean` and `look`, and
`fake_head_right_cm`, `fake_head_up_cm` and `fake_head_forward_cm`. `lean`
puts the fake head at that lean while the word holds. Without a `lean` word
the lean holds for the whole run. `look` turns the fake head 30 degrees left
and 20 degrees down while it holds. All are steps. The launcher removes these
keys.

With `test_keys=1`, Shift F11 cycles the grab: as set, one to one, and off.

### 9.5 Desktop results

The rig of section 6, 60 fps, with the dialog recipe of `hand.md` and no
Creature chosen. From frame 3195 the camera is `CameraModeNew3` at the
Creature select, with its input on. There the camera is 12.7 units from its
focus and 676 units from the map center, and it sits on the floor: the land
plus 6 units.

**Run g1a**, `grab_gain_mode=0`, `grab_smoothing_ms=0`, `grab_probe=1`:

- Test 1: `With the hands still, the camera moved on 0 of 175 frames, 0.0000
  units at most.`
- Test 2, the hand 10 cm right: `The camera moved -0.7499 0.0000 0.0001
  units (right, up, forward of the anchor room) ... Anchor error 0.0000
  units.`
- Test 4: 10 cm up stops at the floor at once (`the camera stops at the
  floor ... height 20.195 (floor 20.195)`), because the camera already sat on
  it. That is also test 7. Then 10 cm toward the head: `-0.7498 0.0000
  0.7500`, so the pull moves the camera forward by 0.75.
- Test 8: the head leaned 12.2 cm, then turned 30 degrees and pitched 20,
  with the grip held: `the camera moved on 0 of 221 frames, 0.0000 units at
  most`.
- Test 13: `the stick turn is refused during the grab`, `the stick zoom is
  refused during the grab`.
- Test 12: the trigger during the grab: `the left button is held back from
  the game during the grab`. The grip with the trigger down: `the other
  hand's grip does not grab: the trigger is down`.
- Test 5: `60 camera updates after the release, the camera is 0.0000 units
  from where the grab left it, 0.0000 at most`.
- Test 14: after the release, a snap turn (`heading 4.121 to 4.907`) and a
  zoom step (`distance 12.7 to 7.3`) worked from the released camera.
- The game's own update never moved the held camera: `on 0 frames`.
- G0: a land drag with a grip step of 10 cm moved the camera 3.23 units,
  `4.3` camera units for each unit of the grip, with the land 12.7 units
  away. A land drag with a head lean of 12.2 cm and a still grip moved the
  camera `0.00 units`. So item 4 of 1.3 no longer holds: since the review fix
  (the drag ray from the game camera to the reach point), a lean does not move
  the land in the game's drag either.

**Run g1b**, the defaults (`grab_gain_mode=1`, `grab_smoothing_ms=35`), with
`grab_limit=100` and `camera_glide=1`. The small limit puts the wall at the
start, 676.5 units out (the focus was the farther point):

- Test 3: `gain 3.14 (the land glued to the hand)`, from 12.7 units to the
  focus over a hand 53.9 cm from the head. The hand 10 cm left of the press:
  `The camera moved 2.3517 0.0000 -0.0000 units ... Anchor error 0.0000
  units`. That is 0.75 times 3.14.
- Test 6: the hand 10 cm right pushed the camera out: `the camera stops at
  the wall ... Distance from the map center 675.605 (wall 676.473)`. The focus
  stood on the wall and the camera slid along it by 0.18 units. The next step
  took the camera off the wall, where the solve said.
- The hand 20 cm down: the camera rose `4.6993` units (0.75 times 2 times
  3.14 is 4.71), with 0.13 units taken by the wall.
- Test 7: the hand 20 cm up: `the camera stops at the floor ... height 20.195
  (floor 20.195)`.
- The hand back at the press point: `The camera moved 0.0000 0.0000 0.0000
  units ... Anchor error 0.0000 units`. The solve is absolute and it does not
  drift.
- Test 1 with the smoothing: `the camera moved on 0 of 209 frames, 0.0003
  units at most`. The 0.0003 was the last step of the ease, where the eased
  grip snaps onto the real one inside 0.0001 units. At a gain of 44 that step
  could pass 0.001, so the build now counts that frame as an eased frame, not
  a still one.
- Test 5 with the glide on: `60 camera updates after the release, the camera
  is 0.0000 units from where the grab left it`. A snap turn after it kept the
  position, and a zoom step moved the camera in one update. This grab ended
  where it started, so it cannot show a swing back. Run g1c does.

**Run g1c**, `grab_hand=2`, `grab_smoothing_ms=0`. The pointing hand was at a
reach of about 28 units, and a holdable object was inside its grab radius:

- Test 11, the object half: the pointing grip during the other hand's grab
  gave `the pointing grip stays the game's button (the Hand is over an object
  it can hold)`, the right button went down, and `the Hand holds 0F5246AC`
  10 frames later. The next press, with the object in the Hand, gave `the
  Hand is not in its Normal state`, and the game set the object down.
- Test 17: the other hand grabbed while the Hand held the object. The hand 10
  cm left: `The camera moved 3.1178 ... Gain 4.16. Anchor error 0.0000
  units` (0.75 times 4.16 is 3.12). The object stayed in the Hand until the
  next grip press. A second grab later, with the object in the Hand again,
  gave `Anchor error 0.0000 units` too.
- Test 14: the grab left the camera 3.94 units from its start. A zoom step
  after the release: `the camera moved 5.269 units in one update ... That is
  5.269 from where the grab left it and 2.186 from where the grab started`.
  The zoom started from the released camera and did not swing back. A snap
  turn after it: `heading 4.121 to 4.907`, the same heading as before the
  grab, because one hand does not turn.

**Run g1d**, the same keys, with the pointing hand raised into the sky, where
nothing is in its grab radius:

- The claim: `the pointing grip grabs the world, not a button (the Hand is
  empty and over nothing it can hold)`, and no right button reached the
  game. The new anchor for both hands started at the same camera
  (`2224.93 20.19 3146.31`), so the join did not move the view.
- Test 9, the spread from 20 to 30 cm: `zoom k 1.5000, turn 0.000 degrees,
  the game's heading turned 0.000 degrees, the camera is 8.059 units from
  the grabbed point (12.089 at the anchor, ratio 1.5000), its error 0.0000
  units`. The hands back at 20 cm put the camera back to 0.0000.
- Test 10, the hands turned 36.87 degrees about their middle, 20 cm apart:
  `zoom k 1.0000, turn 36.870 degrees, the game's heading turned -36.870
  degrees ... its error 0.0000 units, the camera height 20.195 to 20.195`.
  Both hands kept their world points: `The larger hand error ... 0.0000
  units`.
- The pointing grip opened first: the other hand went on alone from the same
  camera, then the release left it there for 60 updates.
- Test 11, the pointing grip alone with `grab_hand=2`: `an anchor for the
  pointing hand (a new press) ... gain 4.90`. A step of 5 cm right and 6 cm
  forward moved the camera `-1.8366 0.0000 -2.2038` units, `Anchor error
  0.0000 units`.
- Test 14 after a turn: a zoom step after the release moved along the new
  heading, 18 degrees, where the view before the turn zoomed along 56
  degrees (56 minus 36.87 is 19). The camera also rose 1.2 units over two
  updates at the new place. That is most likely the game's own lift above the
  land [I].
- The run also showed a fault in the log: a claimed grip whose Hand was off
  the view first logged `the right button press started off view, so it is
  dropped`. No press reached the game, but the claim is now tested first.

**Run g1e**, `grab_hand=2`, the default smoothing:

- Test 15: a grip during the story camera gave `the other hand's grip does
  not grab: the camera is not the player's now`. A grip held from the story
  camera into play did not grab. A grab needs a new press.
- The claim is tested first now: the pointing grip in the sky claimed with no
  "dropped" line.
- The pointing grip grabbed first, then the other grip joined. The spread
  from 20 to 30 cm with the smoothing on: `zoom k 1.5000 ... ratio 1.5000,
  its error 0.0000 units`.
- Test 8 with two hands: a head lean and a head turn during the grab gave
  `the camera moved on 0 of 159 frames, 0.0000 units at most`.
- The other hand opened first: the pointing hand went on alone from the same
  camera. After the release: `60 camera updates after the release, the camera
  is 0.0000 units from where the grab left it`.

After the runs, the report got one more line, for the headset: `the held
camera moved X units a frame on average, Y at most`. A real hand is never
quite still, so this line shows the tracking noise times the gain.

### 9.6 Not tested, and what needs the headset

- Test 16 with the mouse: a desktop run must not move the real mouse. The
  mouse path only changes while a grip holds the world, and with the mouse as
  the owner no grip can grab. With `controllers=0` no grab code runs.
- The menu half of test 15. The code ends a grab when the hand cannot act
  (`StereoIdle`), as it does for the buttons.
- A recenter and a grip that jumps 50 cm during a grab. Both start a new
  anchor from the camera the grab wrote last.
- The gain in open play. At the Creature select the camera was 12.7 units
  from its focus, so the glued gain was 3 to 5. In play the camera is 100 to
  700 units from its focus, and the gain will often stop at 60. Then 10 cm of
  hand moves the world 45 units.
- Tracking noise times that gain, the squeeze levels of real Touch and Index
  controllers, the frame age of the hands, and the comfort of the turn and
  the zoom. The new report line gives the noise in numbers.
- The rise of 1.2 units after a turn and a zoom (run g1d). It looks like the
  game's own lift above the land, but it is not proved.

### 9.7 Not built yet

- G4: the vignette while the camera moves, the tutorial events (open question
  2), a cut for the advisor anchor (risk 6: the grab does not call
  `SpiritAnchorCameraCut`), and the hint texts of `controller-hints.md`.
- G5: the tabletop sink.
- The script word `lpose2` of section 6. No test needed it.
