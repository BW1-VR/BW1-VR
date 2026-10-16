# The God view (play mode 4)

The God view puts the land at the player's waist, in the manner of the VR
game "How to God". The player's hands are the Hand, one to one in place and
size, and the world moves and scales around the player so that the hands can
reach it. There is no reach gain and no Hand that grows. The reach comes from
the world (`hand.md`).

The God view is the main mode and the default. It works with motion
controllers and with the mouse. The launcher offers it first.

The God view is the camera of the tabletop mode (`tabletop.md`) with a
different room: the land is at the waist and there is no edge. The two modes
share the code, the table transform and the grab solve. `tabletop.md` has
that math. This file has everything that is specific to the God view.

## Play modes

| `play_mode` | View |
| --- | --- |
| 4 | The God view. The default |
| 3 | The tabletop: the island is a model on a table in the room (`tabletop.md`). The launcher calls it "Tabletop (test)" |
| 2 | A window that follows the head. Not in the launcher |
| 1 | The stereoscopic window: a 3D screen in front of the player. The head does not move the camera |
| 0 | Not a mode. A value of 0 in `bwvr.ini` gives the God view, and the mod logs it |

```
settings     : play_mode=0, the immersive mode, is now the God view, play mode 4
```

The immersive view (the world all around the player, with the game camera
moved by the head) stays in the code for two cases. A story shows in it while
the God view stands aside (section 7). Ctrl F11 turns the God view off and
on, and off is the immersive view. The kPlayModes table in `mod_main.cpp`
sets the flags of each mode.

## Summary

- **The map.** The land under the recenter pose sits `god_ground_down_cm`
  (60) below the head. There is no edge, no key color and no sea disc: the
  normal sky and sea go to the horizon.
- **The zoom** changes the scale about the land point that the player looks
  at (`god_zoom_about`, 1), so the thing in the middle of the view stays where
  it is and only its size changes. The ground keeps its height. It goes from
  the life size scale (7.5 units per meter) to 4000, in steps of 1.5 times.
  With `god_zoom_smooth=1` the stick zooms while it is held, with no steps.
- **The ground height** goes up and down with the left grip: the hand pulls
  the land up and down one to one. The mouse player has steps of 5 cm (the
  tilt keys). The view never follows the land up or down by itself.
- **The grab of the world** moves the land one to one with one hand, across
  and up and down, and two hands turn and scale it. Nothing moves after the
  release.
- **The walk with the stick.** The stick of the hand that does not point
  moves the player over the land while it is pushed, along the direction of
  the head or of the controller (`move_stick`). The height of the view
  follows the land under the head through a smooth filter
  (`move_stick_follow_land`). It stops when the stick comes back (section
  5.7).
- **The Hand** is always the real hand (`hand_real_cm`, 19 cm) at the scale
  now. Zoomed out, it is large against the villagers.
- **The game camera** stays over the player's place at 400 units, for the
  sound, the detail and the game's limits. It moves only when the player
  moves the view.
- **A story** shows as the game made it, at the life size scale. The God
  view stands aside and comes back where the story leaves the camera.
- **The mouse** drags the land, and the land point under the mouse stays
  under it. The wheel zooms, the tilt keys move the ground, and the turn keys
  and the middle mouse turn.
- **The menu.** The menu button of the left controller sends Esc, and a
  dialog box takes the pointing hand as a pointer (section 10).
- **The tutorial.** The zoom, the pan, the walk, the teleport and the turn
  keep to the game's camera switches, and they send the game's camera
  events. So a tutorial step that waits for a zoom or a drag can end. The
  pan, the walk and the teleport have one switch, so the player has all
  three from the first step of the tutorial.
- **A load** builds the view again at the camera of the loaded game.
- **The game moves its camera.** When the game puts its own camera at a new
  place (a new game, the end of a story, its jump to the Temple), the player
  goes there as one cut. The land point that the camera looks at is 50 cm in
  front of the head, and the player faces the way that the camera faces
  (section 15).
- **The teleport.** Hold A to aim an arc. A ring shows the target, cyan when it
  is valid and red when it is not. Let go of A to go there, as one cut. B
  cancels. A double click of the mouse goes to the land under the mouse
  (section 14).
- **The scripts see the player.** A script of the game that asks how far the
  camera is from a thing gets the distance from the head or the nearest
  hand. A script that asks if the camera sees a thing gets the view of the
  head. In a story the script gets the camera of the story (section 16).

## 1. The map

The table maps the room to the world with one similarity (`tabletop.md`,
"The table camera"):

```
world = G + s * Lt * (room - Tg)       Tg = (0, -down, 0)
```

| Symbol | Meaning |
| --- | --- |
| `G` | the waist point: the world point that sits right under the recenter pose, `down` meters below it |
| `s` | the scale, in units per meter |
| `Lt` | the basis: world up, and the heading `psi` of the room forward |
| `Tg` | the waist point in the room, `god_ground_down_cm` below the recenter pose |

On the table `C` is at sea level. In the God view `G` has a height of its
own: it is the world height at the waist level. At the start it is the height
of the land under the recenter pose, so that land is exactly at the waist.

`TabletopCamera` gives `head_camera.cpp` the camera at `P0` (the recenter
pose in the world) and the lean gain `s / 7.5`, as on the table. The render
bracket, the eye separation of the player's IPD times `s`, the room of the
controllers (`TabletopRoom`), the spirits and the ears all work as on the
table, with the God view's `P0`, `Lt` and `s`.

The land under the head is 60 cm below the head only where it has the height
of the waist point. A hill that the player pans to rises toward the head, and
a valley falls away. That is the rule: the view does not rise and fall with
the land. The player moves the ground with the left grip.

## 2. The zoom

One zoom step multiplies the scale by 1.5 (`god_zoom_step_pct`, 50), or
divides it by 1.5. A zoom keeps one point of the world at its place in the
room:

```
W  = the point of the zoom
s' = s * ratio,  kept between 7.5 and god_far_units_per_meter
G' = W + (s' / s) * (G - W)
```

`W` keeps its world point and its room point. A land point that is `d` from
`W` in the room is `d * s / s'` from it after the zoom, so the land comes
nearer to `W` when the view zooms out. The heading does not change. The game
camera stays over the land under the head (section 7), so the game logic, the
sound and the detail keep their distances to the player.

**The point of the zoom.** `god_zoom_about` chooses where `W` is across the
room. `GodZoomPoint` in `tabletop.cpp` is the one place that chooses the
point, for a step and for the smooth zoom. `PointAcross` in `zoom_math.h` has
the rule.

| `god_zoom_about` | The point | What the player sees |
| --- | --- | --- |
| 1, the default | The land point that the head looks at | The thing in the middle of the view stays where it is, and only its size changes |
| 0 | The land point straight under the head | The player keeps their place in the world. Each thing in front of the player comes 1.5 times nearer in a step out and goes 1.5 times away in a step in. That can look like a move forward or back |
| 2 | The Hand's point of the pointing hand | The land at the hand stays at the hand |

- **1, the look.** The line of the look starts at the middle of the head and
  goes along the forward direction of the head. The point is the first land
  on that line. The land test is the one of the mouse pan (`RayLand`), in
  steps of 1 cm of the room and of 2 units at least. The test stops
  `god_zoom_about_far_cm` (300) from the head. When the line meets no land
  before that (a look at the sky or at the horizon), the point is the point
  of the line at that distance. So the point is at most 3 m across from the
  head, and a look straight up gives the land under the head. The two cases
  meet at the limit, so a small turn of the head never makes a jump of the
  point. A head with no direction gives the land under the head.
- **2, the Hand.** The point is the Hand's point (`HandDirectRealPoint`) of
  the frame before. While no controller places the Hand (the mouse has the
  pointer, or a dialog box is up), the point is the land under the head.
- **The level.** The height of `W` is the same for each value: the height of
  the land under the head. Land of that height keeps its height in the room,
  so the ground stays where the player put it (section 3). Only the place of
  `W` across the room changes with the key. On a slope the land point that
  the player looks at is higher or lower than the level, and it goes up or
  down in a zoom by that height times the change of the scale.
- **The head moves over the land** with 1 and 2. In a step in, the land under
  the head goes away from `W` by half of its distance to `W`. In a step out it
  comes nearer by a third of that distance. A look at the horizon and a step
  in thus move the land 1.5 m under the player. The game camera follows the
  head (section 7).
- **A hill at the new place of the head.** When the land under the new place
  of the head is nearer than 10 cm below the head, all of the land goes down
  until it is 10 cm below (`ZoomClearHead`), as it does when the view goes to
  a place (section 15). The log line of the zoom says so.

**A hill over the head.** At the life size scale a hill of 100 units is 13 m
high. A pan can bring one over the head. So the level is first kept between
10 cm and 2.5 m below the head, the range of a ground step (section 3). With
`god_zoom_about=0`, each zoom step out then brings the hill top 1.5 times
nearer to that height. After 14 steps out at the life size scale, a hill top
12.73 m over the head is 5.6 cm under it.

**The limits.** 7.5 units per meter is the life size scale of
`head_units_per_meter_x10`. At 4000 an arm of 70 cm reaches 2800 units, which
is inside the Hand's reach of 5600 (section 8). `god_far_units_per_meter` can
go up to 8000. A step at a limit does nothing and is logged. From 7.5 to 4000
is 16 steps.

**The inputs.** The right stick forward zooms out, and back zooms in
(`god_zoom_forward_in=0`, "Stick forward" in the launcher). With 1 the stick
forward zooms in. The desktop test scripts set 1, so the word `stick_up` of a
recipe zooms in. With
`god_zoom_smooth=0`, the default, one push past 70 percent is one step. The
stick must come back below 30 percent before the next push counts. The mouse
wheel also steps the God view, one step a click, at most one each 150 ms, and
the game hears no wheel. The wheel zooms in steps with each value of
`god_zoom_smooth`.

A zoom step needs the game's camera switch for the zoom (section 7, "The
tutorial"). While the switch is off, a push or a click does nothing, the
controller gives no pulse, and the log says why. A step that the view takes
sends the game's zoom event, 0x304.

The log has two lines for a step, and one more for a check in the next
frame. They give the point of the zoom and why it is that point, how far the
point moved in the room, and how far the land under the head moved. The
lines of a zoom step and of a smooth zoom hold have their own count, 150
lines for a session (`kMaxZoomLogs`). The lines of a turn, a ground step, a
pan of the mouse and a zoom that the game does not allow share 60 lines
(`kMaxGodLogs`).

```
god view     : zoomed in, 100.0 to 66.7 units per meter, about the point at 2052.0 37.6 2489.7, 0.44 m below your head: the land that you look at, 0.77 m across from your head (god_zoom_about=1)
god view     : zoom check, the point of the zoom moved 0.00 cm in the room. The land under your head was at 1977.5 2468.3 and is at 2002.3 2475.4, 38.75 cm of the room apart. The recenter place moves 53.73 cm of the room, and the head is 30 cm right and 40 cm forward of it
god view     : zoom check, the drawn head moved 38.75 cm of the room over the land in the step (1977.5 2468.3 before, 2002.3 2475.4 after). The point of the zoom moved 0.00 cm from the drawn head in the room.
```

**The smooth zoom.** With `god_zoom_smooth=1` the stick changes the scale
while it is held, and the zoom stops in the frame in which the stick comes
back. Nothing glides after that, and a stick at rest moves nothing. The ratio
of the scale for one frame is

```
share = (|y| - 0.3) / (0.95 - 0.3), kept between 0 and 1
ratio = 1.5 ^ (-direction * share * dt / T)
```

`y` is the stick forward or back, and `direction` is 1 for forward. `dt` is
the real time of the frame, and at most 50 ms, so a frame that took long makes
no jump. `T` is `god_zoom_smooth_ms`, 500: a full push changes the scale by
one zoom step (`god_zoom_step_pct`) in that time, and a small push is slow.
The change is a ratio, so the zoom feels the same at each scale, and it
follows the time and not the frame rate. `zoom_math.h` holds these functions.

- **The point** is the point of a zoom step (`god_zoom_about`), with the
  rule for a hill over the head. With 1 and 2 the hold takes the point one
  time, at its start, and keeps that point of the world. So the point does
  not move while the head turns a little. With 0 each frame takes the land
  under the head again. The rule for a hill at the new place of the head acts
  on each frame.
- **The limits** are the limits of a step. At a limit the scale stops, the
  stick still counts as held, and the other direction zooms at once.
- **Once a frame.** The pump of the controllers asks the view again before
  each frame (`TabletopGodZoomHold`), and the main camera call changes the
  scale one time (`ApplyGodZoomHold`). A frame with no request changes
  nothing.
- **Which axis.** The stick zooms while it is past 30 percent forward or back
  and that axis is larger than the side axis. A snap turn needs the side axis
  past 70 percent and larger than the other one, as with the steps. So one
  frame never zooms and turns. A hold starts with a fresh push only: a hold
  that stopped because the stick went to the side, and a push that the view
  or a grab refused, must come back below 30 percent first.
- **The game's switch.** Where a zoom step is refused, the smooth zoom is
  refused too: a push with the zoom switch off does nothing, gives no pulse,
  is counted one time and must come back. A switch that goes off during a
  hold stops the hold. A grab of the world refuses the stick in the same way
  as for a step.
- **The zoom event.** Each frame that changes the scale sends 0x304 with the
  input bit of the wheel. The game does the same: it sends 0x304 on each
  update in which its own camera zooms (the test at `0x0045C38E`), and
  `CameraHelpAccumulator::Trigger` `0x00449040` drops a trigger while its
  flag at `+0x0A` is set. So a tutorial step that waits for a zoom hears the
  first frame of a hold. A frame at a limit sends no event.
- **The game camera.** With `god_zoom_about=0` the point of the zoom is the
  place of the game camera, so a hold does not move that place. The version
  of the view goes up on the first frame of a hold that changes the scale and
  one more time at its end, so `GodAfter` writes the game camera at most two
  times for a hold (section 7). With 1 and 2 the head moves over the land in
  each frame. The version then goes up on each frame that changes the scale,
  as for a grab, so the game camera follows the head.
- **No blink and no cue.** The blink of a step and the sound of a zoom step
  (`audio.md`) do not come with the smooth zoom. The controller gives one
  pulse at the start of a hold.

With `test_keys=1`, Ctrl, Shift and Insert change `god_zoom_smooth` between 0
and 1 while the game runs. The pointing controller gives a long pulse for 1
and a short pulse for 0. A stick that is pushed at that moment must come back
before it zooms. The test `god_zoom_test_toggle=N` does the same at frame `N`
of the controllers, for a desktop run.

With `test_keys=1`, Ctrl, Shift and Delete change `god_zoom_about` from 1 to
0, to 2 and back to 1 while the game runs. The pointing controller gives a
short pulse for 0, a medium pulse for 1 and a long pulse for 2. A hold of the
smooth zoom that runs stops, and a stick that is still pushed starts a new
hold with the new point. The test `god_zoom_about_test=N` does the same at
frame `N` of the controllers, for a desktop run.

The log has one line for the start of a hold, one for its end, and one for a
check 30 frames later:

```
god view     : the smooth zoom starts to zoom in at 100.00 units per meter, about the point at 1996.0 29.5 2520.3, 0.60 m below your head: the land that you look at, 1.10 m across from your head (god_zoom_about=1). The hold keeps this point of the world. A full push changes the scale 1.50 times in 500 ms.
god view     : the smooth zoom stopped (the stick asks for no zoom now). It zoomed in from 100.00 to 54.75 units per meter, which is 1.49 zoom steps. The stick was held for 0.743 s, and the scale changed on 45 of 45 frames. The point of the zoom at 1996.0 29.5 2520.3 moved 0.00 cm in the room. The land under your head was at 1890.4 2490.0 and is at 1938.2 2503.7, 90.79 cm of the room apart, and the drawn head moved 90.79 cm of the room.
god view     : smooth zoom check, 30 frames after the stop the scale is 54.75 units per meter, and it was 54.75 at the stop. The drawn head moved 0.00 cm of the room in those frames.
```

## 3. The ground height

The left grip moves the ground up and down (section 5). With the grip closed,
a hand that moves up raises the land by the same distance, and a hand that
moves down lowers it. A raise is `G.y -= s * d`, where `d` is the rise of the
hand in meters: every land point goes `d` up in the room, and the scale and
the heading stay. The left stick does not move the ground: it moves the
player over the land (section 5.7).

With the mouse, the game's two tilt keys move the ground in steps of 5 cm
(`god_ground_step_cm`), one step for each press (`GodTiltKeys` in
`comfort_camera.cpp`). The key that sets a tilt of more than 0 at
`0x00C5B0C4` raises the ground, and the other key lowers it. In the God view
the game camera does not tilt. A tilt key is refused while a grip holds the
world.

**The limits.** The land under the head stays between 10 cm and 2.5 m below
the head. A tilt step that would put it nearer or farther is refused and
logged. A step toward the range is always allowed. A grab stops the land at
the limit and does not refuse it: `GodGroundHeight` in `table_math.h` cuts the
height that the hand asks for to the range. The land under the head is found
in the pose that the solve made, so a pan over a hill is judged where the head
will be.

- At a limit the land stops, and it does not jump. The result is the request
  cut to a range, so it is continuous in the hand. When the hand comes back
  into the range, the land follows it again from the limit.
- A land that is already outside the range (a hill under the head) can only
  move toward the range. The cut never takes the height farther out than it
  is now.
- A head that moves while the land is held at a limit moves the limit with it.
  With the land inside the range, a head that moves and a hand that stays
  still move nothing.

The ground height belongs to the player. The view keeps it through a zoom, a
pan, a turn and a story. A recenter (F11) moves the room, so the waist point
goes under the new head position.

## 4. The turn

The right stick left or right turns the view by the snap turn step (45
degrees by default, the launcher's "Turn in steps"). The turn keys and the
middle mouse do the same. The request goes through `comfort_camera.cpp`, so
the tutorial's turn switch and `MayTurn` still apply, and the tutorial still
hears the turn. The view turns about the head: the world point of the head
stays, and the heading changes by the step. A 90 degree turn right puts the
land that was on the right in front.

With "Turn in steps" off, there is no turn in the God view.

## 5. Moving around: the grab and the mouse

### 5.1 The grab of the land

The grips move the land. The solve (`TableGrabSolve`, in `tabletop.md`, "Moving
the table by the grips") is the same as on the table, with the vertical grab
and the ground limits added.

- One hand (the other hand's grip, the left grip by default): the land moves
  one to one with the hand, across and up and down. A hand that moves up
  raises the land by the same distance, and a hand that moves down lowers it
  (`TableGrabSolve` with `vertical`, then `GodGroundHeight` for the limits of
  section 3). At a limit the land stops and does not jump. The height of the
  waist point is the height of the press plus the rise of the hand.
- Both grips: the land turns about up with the hands and scales with their
  spread, about the land point under the middle of the hands. The height of
  the press stays, so two hands move the land only across.
- The solve is from the press every time, so a head that moves with still
  hands moves nothing, and nothing moves after the release.
- The scale stays between 7.5 and `god_far_units_per_meter`. The mouse pan
  never lifts or drops the view: its hand is on the level plane of the press.
- The game's camera switches hold for the grab (section 7, "The tutorial").
  While the switch for the drag is off, the grab moves nothing, and it
  anchors again on each frame, so the land does not jump when the switch
  comes on. A two hand grab turns only while the turn is allowed, and zooms
  only while the zoom is allowed. Otherwise the turn or the zoom keeps its
  last value, so nothing jumps. A move of the grab sends the game's events:
  0x308 for a pan, 0x300 and the direction for a turn, and 0x304 for a zoom.

The sticks are refused while a grip holds the world.

The code is in `world_grab.cpp` (the state machine, `TableAnchor`,
`TableSolve`) and `controllers.cpp` (the grip points and the buttons).

### 5.2 The buttons of the grab

| Input | During no grab | During a grab |
| --- | --- | --- |
| Other hand's grip | Starts a grab on a press above `grab_press_pct` | Holds it. Below `grab_release_pct` the hand lets go |
| Pointing grip | The right button (pick up) | Joins the grab when the claim rule holds. Otherwise it stays the right button |
| Pointing trigger | The left button | Held back from the game. It must open before it presses again |
| Any new left press | Goes to the game | Held back, the same way |
| Stick | Snap turn and zoom steps | Refused and counted. A refused push must come back before it acts |
| Stick of the other hand | The walk with the stick (5.7) | Refused, with one log line. The push must come back before it walks |
| A button | The teleport | Refused |
| Mouse wheel, turn keys, middle mouse | As usual | Refused and counted in `comfort_camera.cpp` |

The other hand is the main way in, for four reasons:

1. It cannot take pick up from the pointing grip.
2. It works while the pointing hand holds an object. The player can carry a
   villager and pull the world.
3. It never asks whether the Hand is over land.
4. It needs no change to what the game sees. No game button is sent for it.

A grab starts only on a press, never on a grip that is already closed. A
grip that closes while the trigger is down, or while the game holds the left
button, does not grab. The next grab needs a new press after the trigger is
up. A trigger that is already down at the press of a grip blocks the grab
until it is up. The grab needs the controller to own the pointer, a scene (no
menu), and the player's camera mode with its input on
(`WorldGrabCameraReady`). When one of them goes away, the hands let go of the
world.

**The claim rule** for the pointing grip (`grab_hand=2`). The mod claims the
press, and the game gets no right button, only when all of these hold at the
press frame: the controller places the Hand, the Hand is in the Normal state
(`hand+0x4878` is 1) and holds nothing (`hand+0x4904` is 0), the mod's pick has no choice, the game has no object under
the Hand (`+0x3C8`), no game button is held, and the trigger is up. The rule
is decided at the press and kept until the release. With `grab_hand=1`, the
pointing grip joins only when the other grip holds first.

The pump never sends the game's both button mode (`0x00C5B0B0`) for a grab,
because the trigger is held back. `controller_swap_buttons` exchanges the
trigger and the grip of the pointing hand.

`khr/simple_controller` has no squeeze, so there the other hand's select (its
trigger) grabs. A left handed player (`left_hand`, `controllers.md`, 5.6)
has the mirror: the left hand points and the right hand grabs.

| Profile | Grip source |
| --- | --- |
| `oculus/touch_controller` (Quest) | `squeeze/value`, analog. Press above 70 percent, release below 50 |
| `valve/index_controller` | `squeeze/value`. A loose hand can rest at 30 to 50 percent [I] |
| `htc/vive_controller`, `microsoft/motion_controller` | `squeeze/click`, a boolean. The levels do not matter |

### 5.3 The mouse pan

A land drag of the mouse pans the view (`MousePan` in `tabletop.cpp`). At the
press, the ray of the pointer from the head finds the land, in steps of 2
units, up to the Hand's reach of 5600 units (`RayLand` in `table_math.h`).
That land point is the grabbed point, and its height in the room sets a level
plane. On each frame the ray meets that plane (`RayLevel`), and the pan of one
hand of the grab (`TableGrabSolve`) puts the grabbed point there. So the land
point under the mouse at the press stays under the mouse. It also stays under
the Hand, which the game puts on the same ray.

- The ray is the exact pointer through the head of the frame, `H * ray`
  (`PointerMapMouseRay` in `pointer_map.cpp`). The pointer is locked to the
  room, so the ray turns only when the mouse moves. The ray starts at the
  head of the press, so a head that moves with a still mouse moves no land.
- The pan follows the game's own land drag: the left button is down, and the
  hold status of the camera mode is 3 (`+0x8C` and `+0x1AC`). So a click on
  a villager picks it up as usual, and a drag starts only where the game
  starts one. The drag smoothing of 35 ms (`backend.md`) still eases the
  pointer. The land moves only on a frame of a land drag, so nothing moves
  after the release.
- The game's drag code still runs. So the game knows the drag, and the
  tutorial hears it (event 0x308). After each update with a land drag,
  `comfort_camera.cpp` puts the game camera back where it was before the
  update (`GodDragAfter`). So the drag does not move the game camera, which
  the God view does not show. `GodAfter` then puts the game camera over the
  player's place, as for each move of the view. This holds for a drag of the
  trigger too: it moves nothing, and the grip moves the land.
- A ray that meets the plane past the Hand's reach, or goes over it, holds
  the land where it is until the ray comes back.
- A drag with no land under the ray at the press, a drag of a controller, a
  drag while a grip holds the world, and a drag that the game's switch does
  not allow pan nothing until the release.
- A zoom or a turn during the drag anchors the pan again, with no move.

### 5.4 What the game's land drag does

This is the game's own drag with the mouse or with the trigger of the
controller. The mod keeps it, and the grab above does not use it.

1. The trigger is the left button. `controllers.cpp` `PumpButtons` sends it
   through `LHMouse::SetButtons`.
2. `CameraModeNew3::Update` starts a land drag when the left button goes down
   on land. `UpdateClickParams` `0x00459610` stores the grab point, the drag
   plane (mode `+0x14C`, `+0x158`) and the grab camera.
3. Each frame the camera code casts two rays through
   `UnprojectScreenPoint` `0x0081B370`: the press pointer at `0x0045D444` and
   the pointer now at `0x0045D456`. It cuts both rays with the plane and
   moves the camera by the difference (`0x0045D365` to `0x0045D96E`).
4. The mod gives both rays the Hand's world direction. Only the direction
   changes. The origin stays the game's own camera (`h_Unproject`,
   `pointer_map.cpp`).
5. The step is capped by the pixel distance from the press times the view
   distance times 0.011. The mod sets that distance to the angle the Hand
   turned since the press (`KeepStepLimit`).
6. The drag smoothing of 35 ms eases the mouse motion into the Hand
   (`DragShare`, `TakePending`). At the release the rest arrives at once, so
   nothing moves after the release. The glide time `0x009CE614` is 0.

The game's drag keeps the grabbed land on a ray from the game camera through
the Hand's direction. The land moves by an angle times the range, not by the
hand's motion. At the life size scale the world moves 10 to 130 times as far
as the hand, and it ignores a pull of the hand toward the eye. So it is not a
grab, and a true grab (5.1 and 5.5) is the tool for a large move.

### 5.5 The grab on the game camera

When the God view and the table are off (Ctrl F11), the same grips move the
game camera and not the table. This is the grab of the immersive view. It
needs no hook. All the writes go through code that exists.

**The rule.** While a grab holds, one point of the world stays attached to the
hand, and no other motion is made. A still hand gives a still camera,
whatever the head does. The release stops the camera at once, with no glide.

**The solve.** The grip points are in the room, in units. The virtual hand
point is `v = r0 + G * (a - r0)`, with `r0` the head center at the press and
`G = diag(g, g_v, g)`.

- `g = 1` is true 1:1.
- With `grab_gain_mode=1`, `g = Dcam / dhand * grab_gain_pct / 100`, kept
  between 1 and `grab_gain_max` and fixed for the whole grab, so the land
  looks glued to the hand. `Dcam` is the distance between the GCamera Zoomer destinations for
  position and focus. `dhand` is the mean distance of the held grips from the
  head center, kept between 0.25 and 0.70 m. A new anchor measures both
  again.
- `g_v = g * grab_vertical_pct / 100`.

One hand: `C(t) = C_a - L_a * (v(t) - v_a)`, and the focus follows by the same
offset. The solve is absolute from the press, so a second call in one frame
gives the same answer, and rounding cannot drift.

Two hands: with `m` the midpoint, `k` the spread ratio and `theta` the change
of the yaw of the line between the hands, the world turns with the hands
about up (`L(t) = Rot(up, -theta) * L_a`), and the camera moves toward the
midpoint by the zoom (`C(t) = A + (C_r - A) / k`). The zoom is a dolly, not a
change of the world's size. Below 8 cm of real spread, or when the turn is not
allowed (`MayTurn`), `k` and `theta` hold their last values. `k` stays
between 0.25 and 4.

**Where it writes.** `ComfortCameraGrabMove` runs in `h_ModeUpdate`, after
`RunGameUpdate` and `FinishFlight`, where the snap turn writes. It writes the
Zoomers of position (`+0x118`) and focus (`+0x88`) with `Zoomer::SetPosition`
`0x00441AC0`, which also zeroes the speeds. It writes the mode's copies at
`+0xC`, `+0x18` and `+0x1D4`, the pivot at `+0x12C`, and for a turn the
heading at `+0x50` by the change of `GetHeadingAndPitchFromPoints`
`0x004428D0`. A flight in progress is finished first (`FinishFlight` and the flags at
`+0x100` and `+0x101`). The mode fields `+0x78` and `+0x50` also have to stay
in step, or the next zoom or turn pulls the camera back. The game camera is the
sink because the sound, the picking, the Hand's ray, the save game and the
3500 unit pull back then all see one camera.

**Smoothing.** The grip points ease in by `1 - exp(-dt / grab_smoothing_ms)`,
once for each new sample. The solve stays absolute from the eased points. At
the release the rest is dropped, and the camera stays where it is. The grab
never starts the game's glide.

**Anchors.** A new anchor starts at a press, when the hands change, after a
grip jump of 50 cm, at a recenter, and from the camera that the grab wrote
last. Another move of the camera (a script, a fight, input off) ends the grab.
The grip must open and close again. A jump in flight makes the grab wait, and
it anchors where the jump lands.

**The limits.**

| Limit | Value |
| --- | --- |
| Pull back | `GCamera::Update` pulls any camera more than 3500 from (2560, 0, 2560) back over 3 s. The height counts. Test against 1.225e7 at `0x00442288`, code `0x0044224A` to `0x0044232F` [V] |
| Grab radius | `grab_limit`, 3400. It applies to the camera position and to the focus |
| Height above land | `grab_min_height`, 6 units above `GetAltitude` `0x00803090` at the camera x and z |
| Start outside a limit | The wall for a grab is the larger of `grab_limit` and the start distances of the camera and its focus. The floor is lowered by how far the start was below it. So a press never moves the camera |

At a limit the camera stops at the wall. The hand keeps moving, and the
world slides through it. The solve is absolute, so there is no dead zone. A
short pulse marks the wall. The focus and pivot follow the position by the
same offset.

**What the game logic sees.** The sound listener moves with the game camera.
`GCamera::IsMoving` (`+0x74`) is true while the camera changes, so the
interface clears the gesture trail and a miracle stroke cannot run during a
grab. A story mode is never touched, because the grab needs `CameraModeNew3`
on top and `+0x2EC == 0`.

### 5.6 Keys of the grab

All in `[bwvr]`.

| Key | Default | Meaning |
| --- | --- | --- |
| `grab_world` | 1 | Master switch. 0 gives no grab |
| `grab_hand` | 1 | 0 off, 1 the other hand's grip, 2 also the pointing grip alone. The launcher row "Grab the world" |
| `grab_two_hands` | 1 | Both grips turn and zoom |
| `grab_turn` | 1 | The two hand turn |
| `grab_zoom` | 1 | The two hand zoom |
| `grab_smoothing_ms` | 35 | The ease of the grip points. 0 is no smoothing |
| `grab_press_pct` | 70 | The squeeze that starts a grab |
| `grab_release_pct` | 50 | The squeeze that ends it |
| `grab_gain_mode` | 1 | Game camera grab only. 1 the land looks glued to the hand, 0 one to one |
| `grab_gain_pct` | 100 | Game camera grab only. Multiplies `g` |
| `grab_gain_max` | 60 | Game camera grab only. The most `g` can be |
| `grab_vertical_pct` | 100 | Game camera grab only. The gain up and down, as a share of `g` |
| `grab_max_speed` | 0 | Game camera grab only. Units a second. 0 is no cap |
| `grab_limit` | 3400 | Game camera grab only. Units from (2560, 0, 2560) |
| `grab_min_height` | 6 | Game camera grab only. Units above the land |
| `grab_probe` | 0 | A test. Each land drag logs the motion of the pointing grip, the head and the camera (tag `drag probe`). The launcher removes it |

With `test_keys=1`, Shift F11 cycles the grab: as set, one to one, and off.
The log names the mode. Press it with the mouse at rest.

Test aids: `fake_controller_left_points` (right, up, forward in cm, not
mirrored), the script words `lpoint`, `lgrip`, `lean` and `look`, and
`fake_head_right_cm`, `fake_head_up_cm` and `fake_head_forward_cm`. `lean`
puts the fake head at that lean while the word holds. Without a `lean` word
the lean holds for the whole run. `look` turns the fake head 30 degrees left
and 20 degrees down while it holds. All are steps. The launcher removes these
keys. After a run, the report has one line, `the held camera moved X units a
frame on average, Y at most`. A real hand is never quite still, so in the
headset this line shows the tracking noise times the gain.

### 5.7 The walk with the stick

The stick of the hand that does not point (the left stick, by default) moves
the player over the land while it is pushed. The move is the pan of a grab of
the world (5.1): the waist point goes to the land that was one step from it
in the room, and the heading and the scale stay. With
`move_stick_follow_land=1` the height of the view follows the land under the
head (see "The land under the head" below). With 0 the height of the ground
stays too.

```
G' = G + s * Lt * (step.x, 0, step.z)      step = speed * the time of the frame
G'.y = G.y + (F' - F)                      F = the filtered land under the head
```

The plain game has a move of its camera over the land by the keys. Its code
tests bit 8 of the camera switches (`0x0045C946`) and sends event 0x308
(`0x0045C981`) [V]. The walk keeps to the same switch and sends the same
event.

- **Forward** (`move_stick`). With 1 it is the forward direction of the head,
  flat on the land. It comes from the right axis of the head (`HeadYaw`), so
  a look down at the land does not change it. With 2 it is the direction in
  which the controller of the stick points (its aim ray, `controllers.md`,
  section 8), flat on the land. The mod takes the direction again in each
  frame, so a turn of the head or of the hand turns the walk. With 0 the
  stick does not move the player.
- **A controller that points up or down.** A controller that points less than
  11.5 degrees from straight up or from straight down has no direction on
  the land. The walk then keeps the direction of the frame before, and a walk
  with no such frame does not move.
- **The stick.** Forward on the stick moves the player forward, back moves
  back, and left and right move to the side. The push has the dead zone and
  the curve of the smooth zoom along its length (`Push` in `walk_math.h`): no
  move below 30 percent, the top speed from 95 percent, and a straight line
  between the two. A diagonal is not faster than a straight push.
- **The speed** is a speed of the room: `move_stick_speed_cm` (150) cm of the
  room for each second at a full push. So the walk feels the same at each
  scale. At 100 units for each meter that is 150 units a second over the
  land, and at the life size scale it is 11 units a second. The step of a
  frame is the speed times the real time of the frame, and a frame counts at
  most 50 ms.
- **It stops at once.** The pump of the controllers asks the view again
  before each frame (`PumpWalk`, `TabletopGodWalk`), and the main camera call
  moves the view one time (`ApplyGodWalk`). A frame with no request moves
  nothing, so the walk stops in the frame in which the stick comes back.
  There is no glide and no ease.
- **The land under the head** (`move_stick_follow_land`, 1). While the
  player walks, the height of the view follows the land under the head, so
  the land stays at the same height below the hands. The land height under
  the head at the new place goes through two stages of a first order filter
  (`FollowStep` in `walk_math.h`), and the view goes up or down by the change
  of its output. The filter counts the way of the head in the room, not the
  time:
  - A frame with no step changes nothing. So the view does not move while the
    stick is let go, and it does not drift after the walk.
  - Each stage has a length of 25 cm of the room (0.17 s at a full push), so
    the view is about 50 cm of the walk behind the land. A swell of the land
    with a wavelength of 40 cm of the room moves the view by 6 percent of its
    height, one of 1 m by 29 percent, and a hill of 3 m by 78 percent. So a
    long hill moves the view, and a small bump does not shake it. The second
    stage starts from rest, so the view does not start to rise with a jolt.
  - The stages take the exact answer of the filter for each step, so the
    heights do not change with the frame rate.
  - The height that the player set (the ground step, the grab, a zoom) is
    the height above the land. The filter keeps its state from one walk to
    the next, so a climb in short pushes follows the land as a long push
    does. When something else moved the view since the last frame of a walk
    (a teleport, a grab, a zoom, a ground step, a new build), the filter
    starts again at the land under the head, and the land keeps the height
    below the head that it has then.
  - The teleport puts the land under the head at the same height in the room
    as before the teleport (section 14.4), with each value of the key. So
    after a teleport the land is at the height below the hands that the
    player has.
  - With `move_stick_follow_land=0` the view keeps its height in the world.
    The launcher row "Walk follows the land" sets the key.
- **A hill at the new place.** When the land at the new place of the head is
  nearer than 10 cm below the head, all of the land goes down to that limit
  (`ZoomClearHead`), as in a zoom that moves the head over the land. The
  land does not come up again by itself. With the follow of the land, only a
  short sharp hill that the filter does not follow comes that near. With
  `move_stick_follow_land=0` the limit acts on each frame of a hill near the
  head, so the view then goes up with each bump at once.
- **The map and the game camera.** The waist point stays on the map, as for a
  grab. The version of the view goes up on each frame that moves, so the game
  camera follows the player (section 7).
- **The game's switch.** The walk needs the switch for the drag of the land
  (bit 8), as the grab and the teleport do. A push with the switch off does
  nothing, is counted one time and must come back. Each frame that moves
  sends event 0x308 with the input bit 2, as a move of the grab does.
- **The hint.** While a push of the stick moves the player, the card of the
  hand with that stick has the line "Stick Move" in the prompt of the land
  (`prompt_moves`, `controller-hints.md`, 9.4).
- **When the stick does not move the player.** While a grip holds the world.
  In a dialog box and in the game menu. While the God view does not own the
  view: a story, a cut scene, the Temple. While the camera is not the
  player's (`WorldGrabCameraReady`). While the controllers cannot act
  (`controllers.md`, section 9). While the stick drives the ring
  (`button_layout=0`, or a controller with no face buttons: `controllers.md`,
  5.1). A push that is refused, and a push that is there when the stick gets
  this job, must come back to the middle before it moves the player. The log
  has one line for a refused push.
- **The other stick** keeps its jobs. The stick of the pointing hand turns
  and zooms, also during a walk.
- **Comfort.** A walk with a stick makes some players ill. The grab and the
  teleport move the player with no such motion. `move_stick=0`, and the
  choice "Off" of the launcher, turn the walk off.

| Key | Default | Meaning |
| --- | --- | --- |
| `move_stick` | 1 | 0: the stick does not move the player. 1: forward is the direction of the head. 2: forward is the direction in which the controller of the stick points. The launcher row "Move with the left stick" |
| `move_stick_speed_cm` | 150 | The speed of a full push, in cm of the room for each second, 10 to 500 |
| `move_stick_follow_land` | 1 | 1: while the player walks, the height of the view follows the land under the head through the filter. 0: the view keeps its height. The launcher row "Walk follows the land" |
| `move_stick_trace` | 0 | A test for a desktop run: the log has one line for each of the first N frames of each walk (at most 2000 lines in a game) |

With `test_keys=1`, Shift and the Menu key of the keyboard change `move_stick`
from 1 to 2, to 0 and back to 1 while the game runs. The Menu key is the key
beside the right Ctrl key that opens a context menu. The pointing controller
gives a short pulse for 0, a medium pulse for 1 and a long pulse for 2. A
walk that runs stops, and a stick that is still pushed starts a new walk with
the new rule. The word `movekey` of the scripted controller does the same in
a desktop run.

The log has one line for the start of a walk, one for its end, and one for a
check 30 frames later:

```
god view     : the walk with the stick starts at 100.00 units per meter, with the land under your head at 1858.8 2528.7, 61.6 cm below your head. Forward is the direction of your head (move_stick=1), and it is 0.0 degrees to the right of straight ahead in the room. A full push moves you 150 cm of the room each second (move_stick_speed_cm). The height of the view follows the land under your head (move_stick_follow_land=1), frame 901.
god view     : the walk with the stick stopped (the stick is back). You went 789.67 units over the land, which is 789.67 cm of the room, 0.0 degrees to the right of straight ahead in the room. The land under your head was at 1858.8 2528.7 and is at 2625.1 2338.0. The stick was pushed for 5.264 s, which gives 150.0 cm of the room each second, and the view moved on 300 of 300 frames (move_stick=1, 100.00 to 100.00 units per meter), frame 1201. The land under your head was 61.6 cm below your head and is 63.8 cm below it. The view went 118.7 cm up and 130.4 cm down with the land (move_stick_follow_land=1). A frame took 16.2 to 27.4 ms, and a step was 2.43 to 4.11 cm of the room.
god view     : walk check, 30 frames after the stop the waist point is at 2625.12 2338.04, and it was at 2625.12 2338.04 at the stop: 0.000 units apart. In those frames the drawn head moved 0.00 cm of the room.
controllers  : the stick of the other hand does not move you: a grip holds the world, frame 1081
```

The line of the end also says how far the land went down for a hill, and on
how many frames the controller had no direction. The shortest and the
longest frame and step show whether the walk was even: at a steady push each
step is the speed times the time of its frame. In the headset the line also
counts the frames of the headset that came more than one refresh after the
frame before (see "An even walk" below):

```
Of N frames of the headset, M came more than one refresh of 8.3 ms after the frame before, and the headset showed the frame before again for them.
```

With `move_stick_trace=N` the log also has one line for each of the first N
frames of each walk:

```
god view     : walk trace, frame 61 of the walk: 18.05 ms, a step of 2.707 cm of the room, the waist point at 2023.553 42.130 2487.737, the land under your head at 49.517 (filtered 40.547) and 52.61 cm below your head, frame 961
```

**An even walk.** The step of each frame is the speed times the real time
of that frame, so the walk is even in the time of the game: each frame
moves the view at the speed of the push, and no limit, hill or switch of
the game stops it on some frames (section 12 has the run). In the headset
the walk can still look uneven. At about 97 frames each second on a display
of 120 Hz with Space Warp off, about one refresh in five has no new frame.
The headset then shows the frame before again, and with no Space Warp it
corrects only the turn of the head. So a move of the view stops for one
refresh and jumps at the next. A turn of the head hides this, but a walk
shows it on all of the land, and more where the frame rate is lower. A frame
rate at the refresh rate of the headset (for example 90 Hz in Virtual
Desktop), or Space Warp, gives an even walk. The count in the line of the
end shows how many frames came late. The report of a window has
the count of the walks, their time and the frames that moved the view.

## 6. The Hand

The Hand's size is the real hand at the scale now:

```
size = hand_real_cm * hand_scale_pct / 100 * s / 3.2
```

It is 19 cm long in the room at every scale: size 0.45 at 7.5 units per
meter, 5.9 at 100, and 237.5 at 4000. So zoomed out it is large against the
villagers, and a held villager takes the size of the Hand, as the game does.

The controllers place the Hand through the God view's room with no reach
curve, one to one (`controller_math.h`, `TableHand`). A hand pushed into the
land stops 5 mm of the room over it, so the point and the glow touch the land
and the villagers on it (`hand.md`). The grip takes, carries and lets go like
a real hand (`hand.md`). The pick fix of the table (`tabletop.md`, "The hand
on the table") is installed for play mode 4 too, so a villager of a few
pixels can be picked at every scale: the screen test offers the objects near
the Hand, and the interface step gets the land under the Hand. The throw and
the set down use the rule of the table, with the gain `s / 7.5`. At the life
size scale that is the throw of the immersive mode. Zoomed out, a swing
carries the Hand far over the land, so a throw that ends outside the player's
influence goes from the last point of the swing inside it (`tabletop.md`,
"The throw from inside the influence").

## 7. The game camera

The picture does not come from the game camera, but the game logic does: the
sound starts, the detail lists, the help triggers and the pull back of
`GCamera::Update` at 3500 units from the map center. So after the game's
update of `CameraModeNew3`, `comfort_camera.cpp` (`GodAfter`) puts the game
camera over the player's place, in the fields the snap turn and the grab
write:

- The focus is the land point under the head (`god_camera_at`, 1), kept
  within 3000 units of the map center. With `god_camera_at=0` it is the land
  under the waist point, the recenter place. The place is taken once for each
  move of the view. With `god_zoom_about=0` a zoom keeps the land point under
  the head, so it keeps the game camera in its place too. With 1 and 2 the
  head moves over the land in a zoom, and the game camera goes with it. The
  recenter place moves in a zoom when the head is off the recenter place.
- The camera is 400 units from it, 50 degrees above, along the heading of the
  view.

It moves only when the player moves the view: once for each step, on each
frame of a grab or of a walk with the stick, and in a hold of the smooth zoom
(section 2). A hold that
moves the head over the land writes it on each frame, and a hold that does
not writes it at its start and at its end. It does not follow the head when
the view is still, because `GCamera::Update` sets
its moving flag for a camera that changes, and the interface then clears the
gesture trail. It does not undo the game's own moves of its camera either. At
the build the game camera stays where the game put it, and the view goes to
it (section 15). No write happens while a script holds the input or the
pivot, while a fight holds the camera, or while a jump flies.

**A snap turn alone keeps the focus** (`god_turn_keeps_focus`, 1). When the
view only turned with snap turns since the last write
(`TabletopGodTurnedOnly`), the game camera turns about its own focus by the
same angle (`table_math::TurnAboutFocus`), and the focus stays. Over the land
under the head that is the place that the rule above gives, because a turn
about the head keeps that land point. It differs when a script put the
focus: the turn lesson of the tutorial (section 7, "The tutorial") points
the game camera at the Temple, and it tests each second that the focus is
less than 25 units from that point. With `god_turn_keeps_focus=0` the turn
puts the game camera over the land under the head, and the lesson says "No,
try rotating first." after three seconds and starts again.

**Stories.** While a story or a follow camera holds the view
(`script_camera.cpp`, `ScriptCameraStoryOwns`), the God view stands aside.
The story plays at the life size scale, as in the immersive mode. When the
player's camera is back, the God view is built again at the game camera, with
the last scale and the last ground height (section 15). Ctrl F11 and a new
land build it again the same way.

**Cut scenes.** A cut scene does not play in the full view. While the cinema
bars of the game are up, the view closes to a screen 2 m ahead, and the game
shows its cut scene there as play mode 1 does (`cutscene-window.md`). The God
view stands aside for those frames in the same way, and it is built again in
the same way when the cut scene ends. The rule above is for a story camera
with no bars, for example a camera that follows the Creature.
`cutscene_window=0` gives that rule to cut scenes too.

**A load.** A saved game of the land that is up has the same block table, so
the land does not look new. A post hook on `GGame::ClearMap` `0x00552BB0`
builds the view again at the next main camera call. `LoadAllGame`,
`StartPlaygroundGame` and `GGame::Close` call `ClearMap`. The hook assumes
that the load puts the game camera in place before the next main camera call.
The view also follows a camera that the game puts in place later (section 15).
The last scale lasts for the session only. The launcher owns `bwvr.ini`, so
the mod does not write the scale there.

**The tutorial.** Scripts and the tutorial turn the camera controls on one at
a time, with the switches at `CameraHelp::EnabledFeatures` `0x009CDD6C`: bit
1 the tilt, 2 the turn, 4 the zoom, 8 the drag of the land and 0x10 the
double click jump. The game's camera code tests each bit before it acts, for
example at `0x0045C0E7` for the zoom and at `0x0045D3A6` for the drag. The
God view keeps to the same bits:

- A zoom step and the smooth zoom need bit 4 (`TabletopGodStep`,
  `TabletopGodZoomHold`).
- The mouse pan, the grab, the walk with the stick and the teleport need bit
  8 (`MousePan`, `TabletopMove`, `TabletopGodWalk`, `TabletopMoveAllowed`).
  Each of them moves the player over the land, so one switch holds for all
  of them. With `teleport_switch=0` the teleport needs bit 0x10 (section
  14.1).
- A two hand grab turns only with bit 2 and zooms only with bit 4
  (`TabletopGrabAllows`, called by `world_grab.cpp`).
- A snap turn needs bit 2 (`MayTurn` in `comfort_camera.cpp`).
- The mouse double click needs bit 0x10, because it is the jump of the game.

**The interface levels** [V: the disassembly and
`Scripts\Quests\challenge.chl`]. A script sets all the bits at one time with
the script word "set interface interaction"
(`GScript::SetInterfaceInteraction` `0x0070B220`, function 63 of the script
machine). The word takes one of 16 levels (the jump table at `0x0070B7A8`),
and it calls `CameraHelp::EnableCameraFeatures` `0x00447430` with the bits of
the level and a mask of all bits. The table has each level that a script
uses:

| Level | Bits | The camera controls | The scripts |
| --- | --- | --- | --- |
| 0 | 0x1BF | All | Each script below, at its end |
| 1 | 8 | The drag | `FollowUs`, the walk to the village. `MoveAroundMountain`, at its start |
| 4 | 0x18 | The drag and the jump | `FollowUs`, the way from the village to the Temple |
| 8, 12 | 0 | None | `TheMissionaries`, `LearnGestures`, `PhysicalShieldChallenge`, `FireBallChallenge` |
| 10 | 0xA | The turn and the drag | `MoveAroundMountain` |
| 11 | 1 | The tilt | `MoveUpAndDownMountain` |
| 13 | 0x1A | The turn, the drag and the jump | `FollowUs`, the turn lesson |
| 14 | 0x1B | The tilt, the turn, the drag and the jump | `FollowUs`, the tilt lesson |
| 15 | 0x3F | The six low bits | `FollowUs`, the zoom lesson |

The levels 2 (bit 8), 3 and 6 (bit 2), 5 (0x24) and 7 (0x26) are in the code
of the game, and no script uses them. Level 9 is an error. The levels 1, 2
and 10 also call `fn_004473F0` `0x004473F0`, which turns on bit 0x40 and
writes `CameraHelp::AutoPitchParam1` and `AutoPitchParam2`: the automatic
pitch of the game camera. So the log has 0x48 for level 1.

**The walk to the village** [V: `challenge.chl`, the script `FollowUs`, source
file `followus.txt`]. At the start of a new game the first story ends with
level 1, and the villagers ask the player to follow them. Each game turn the
script measures the distance on the map from "camera position" to the end of
the path (`EndMarker`) and to the father. The step ends when the first
distance is less than 50 units. The script counts no event of the help for
this step. A distance from "camera position" is the distance from the player
(section 16), so the player comes there with the grab, with the walk with the
stick or with the teleport. The next step has level 4 and the script
`TunnelOfUltimateDoom`: it ends when the player is less than 75 units from
the Temple.

The tutorial counts the events of `CameraHelp::CameraHelpCallback`
`0x00449140`. Only the 0x300 events reach `HelpProfile::Trigger`. The 0x100
and 0x200 events feed the help hints only. They are errors and collisions:
0x102 is "Hand is too far away" and 0x200 is "Collide with ground". The
numbers below come from the calls in `CameraModeNew3::Update` and from the
game's own names of the events at `0x009CDD70`. That table has no names for
0x301 and 0x302, so its names from 0x303 on are two places early.

| Event | Meaning | The game pushes it at | The God view |
| --- | --- | --- | --- |
| 0x300 | The camera turned | `0x0045C745`, for a turn key | A snap turn, and a turn of a two hand grab |
| 0x301, 0x302 | Then to the left, or to the right | `0x0045C772`, `0x0045C79F` | The same. A snap turn sends 0x301 for the left and 0x302 for the right, in every play mode |
| 0x303 | The camera tilted | `0x0045C82B` | Never: the view does not tilt |
| 0x304 | The camera zoomed | `0x0045C3A8`, on each update in which the camera zooms | A zoom step, each frame of the smooth zoom that changes the scale, and a zoom of a two hand grab |
| 0x305, 0x306 | A double click jump to the land, or to an object | `0x0045DE85`, `0x0045DE1E` | The game's own jump, and each move of the teleport |
| 0x307 | The zoom to the Temple | `0x00457EDE` | Never |
| 0x308 | A drag of the land, or a move by the keys | `0x0045D682`, `0x0045C981` | The game's drag code, for the mouse pan, a pan of the grab, and each frame of the walk with the stick that moves |

The third argument names what made the move. The game's names for its bits
at `0x009CDDF0` are "keyboard" (1), "mouse button" (2), "both mouse
buttons" (4), "wheel spin" (8) and "wheel down" (0x10, the middle button).
The God view sends 8 with a zoom step and with the smooth zoom, and 2 with a
move of the grab and of the walk with the stick.

A tutorial step that waits for a tilt (0x303) could not end in the God view,
because the view does not tilt. The head looks up and down instead, so a
look of the head counts as the game's tilt (`tilt_help_degrees`, 3): one
tilt event for each 3 degrees that the head pitches at 20 degrees a second
or faster (`table_math::CountedPitch`), one in a game turn, while the
script lets the camera tilt (bit 1). A still head and the noise of the
tracking count nothing. The tilt lesson (`TeachPitch`, "Good, now let's
look up and down") wants its count of event 28 plus 25, and one look down
and back up by 40 degrees gives 26. 0 sends no tilt events.

**The turn lesson** [V: the disassembly and `Scripts\Quests\challenge.chl`,
LHVM version 7]. The script `TeachRotate` (source file `HandDemos.txt`) is
the step "Try rotating now to see the Temple as it's being built". It reads
the count of turn events with the script word "get total events"
(`GScript::GetTotalEvents` `0x0070B910`, function 237 of the script machine)
for event 25, which `CameraHelpCallback` makes of 0x300. At its start it
wants that count plus 60, and the step ends when the count is more than
that. Each second it also tests that the camera position is less than 100
units and the camera focus less than 25 units from its point at the Temple
(`centre_pointer`). The third second that fails says "No, try rotating
first." (`HELP_TEXT_FINAL_BEGINNING_46`), puts the camera back and plays the
demo again.

A count takes at most one event in a game turn: `CameraHelpAccumulator::
Trigger` `0x00449040` sets a flag that `HelpProfile::Process` `0x005C4660`
clears in each game turn, and that call also adds 100 to the clock of the
help (`HelpProfile::AccumulatedTime` `0x00C5AFD8`). The game's own turn sends
one event in each frame while it runs, so the lesson ends after about 6 s of
turning. A snap turn sent one event, so it took more than 60 snap turns, and
the turn moved the focus of the game camera (`GodAfter`), so the lesson
started again after three seconds. Two rules make the lesson end with one
full turn of snap turns:

- `turn_help_degrees` (5): a snap turn counts as one turn event for each 5
  degrees of its angle (`table_math::SnapTurnEvents`): 9 for 45 degrees. It
  sends the first at once, and `PayTurnEvents` sends one of the others in
  each new game turn. One full turn of snap turns is 72 events for each size
  of the step. 0 gives one event for a snap turn.
- `god_turn_keeps_focus` (1): the game camera keeps the focus that the script
  put (section 7, "A snap turn alone keeps the focus").

The report gives the events that the snap turns sent and the count of the
help, and a line for each change of `CameraHelp::EnabledFeatures`, which is
how the tutorial turns on the next control:

```
comfort      : the camera controls of the game are 0x1BF (1 tilt, 2 turn, 4 zoom, 8 drag, 0x10 jump). The help has counted 12 turn events
comfort      : the snap turns sent 64 more turn events to the help of the game (turn_help_degrees=5), and 8 updates turned the game camera about its own focus (god_turn_keeps_focus=1). The help has counted 84 turn events in all
```

A hook on the callback (`h_Help` in `comfort_camera.cpp`) logs each change of
the event, up to 80 lines, and the report counts each event:

```
comfort      : camera help event 0x304, inputs 0x08
comfort      : camera help events: 0x300 x2, 0x302 x2, 0x304 x3, 0x308 x41, 6 others
```

## 8. Fog, detail and the near plane

`TabletopApplyFrame` writes the values of `tabletop.md` ("What the engine does
at the table scale"), but they follow the scale. The gain is `s / 7.5`.

| Value | Address | In the God view |
| --- | --- | --- |
| Map objects stop | `0x00C37200` | the game's 700 times the gain, at most 8000 |
| Size cap of the model detail | `0x00C37EC8` | 5 times the gain, at most 30 |
| Land detail lines | `0x00E9C4FC`, `0x00E9C4D4` | 1600 and 2500 times the gain |
| Land texture line | `0x00E9C524` | the game's value times the gain, unless `table_textures=0` |
| The Hand's reach | `CHand+0x4838` | 1800 times the gain, at most 5600 |
| Fog | `0x00C37204` | the game's below a gain of 3, off above it |
| Reflection | `0x00E9CD8C` | the game's below a gain of 3, off above it |

At the life size scale every value is the game's own. A distance in the room
stays the same at every zoom. The fog ends at 900 units, which is 40 m of the
room at a gain of 3 (22.5 units per meter). Past that the fog would cover the
island, so it goes off, as on the table.

**The near plane.** `LandFeature::GetNearClipping` `0x005E2F30` gives 3.5
units for a camera 20 units or more over the land. At the life size scale that
is 47 cm, and it would cut away a real hand in front of the eyes. A hook on
it (`near_plane.cpp`) gives at most 5 cm of the room, `0.05 * s`, with a
24 bit Z buffer. With a 16 bit Z buffer it gives at most 10 cm, `0.10 * s`.
The game goes down to 0.3 units near the ground itself, and 0.375 at 7.5
units per meter is inside that range. Above 70 units per meter (35 at 16
bit) the game's 3.5 is nearer, and it stays.

| Scale (units per meter) | Near plane, 24 bit | Near plane, 16 bit |
| --- | --- | --- |
| 7.5 (life size) | 0.375 units, 5 cm | 0.75 units, 10 cm |
| 22.5 | 1.125 units, 5 cm | 2.25 units, 10 cm |
| 35 | 1.75 units, 5 cm | 3.5 units, the game's |
| 70 and more | 3.5 units, the game's | 3.5 units, the game's |

The 24 bit Z buffer (`depth24=1`, `backend.md`, "The depth buffer") is what
makes the near plane of 5 cm cheap. At every scale its depth steps are at
least 27 times finer than those of a 16 bit buffer with the game's 3.5
units. Zoomed out at 4000 units per meter, a step at the far edge of the
island goes from 39 mm of the room to 0.15 mm. A 16 bit step is the likely
cause of a far flicker.

When a story owns the view, the God view stands aside, and the camera is at
the life size scale. The same hook then gives 5 cm (10 cm at 16 bit) at the
life size scale. The table has it at the table scale, where the game's value
stays. The interface depth does not change with the near plane (`backend.md`,
"The depth buffer").

## 9. Sound and the spirits

The ears are the head (`audio.md`). The distances of the positional sounds
are room distances: QMixer gets each distance mapping of the game with min
and max times `s / audio_units_per_meter` (40). So one meter in the room
sounds the same at every zoom, and a sound at the hand is clear. A positional
sound with no object, which the game puts at its camera, is at the ears. A
bird or a wave of the atmosphere stays where it started in the room, and does
not turn with the head.

The mod mixes each sound with a place itself, for two ears and with its
height (`audio.md`). A sound on the land at the waist comes from below. The
bed of the atmosphere stays fixed in the world when the view or the head
turns. A zoom step changes how far a sound carries: far out, small sounds go
away and loud events stay. A turn step, a zoom step and a teleport have a soft
sound of their own.

The head also decides which positional sounds start and stop
(`audio_range=1`, `audio.md`). The game made these tests with its camera,
which is 400 units from the player's place. With `audio_range=1` a sound
plays while it is in range of the head, at each zoom, and a loop out of range
stays silent and ready.

With `audio_room=0` the ears go straight down from the head to at most 400
units over the land under it (`TabletopEars`), because the game's world
distances make every sound silent farther from the land.

The spirits float beside the player in the room, as on the table
(`tabletop.md`, "The spirits beside the table"). Their size and depth are
`s / 7.5` times the game's, so they float 1 to 1.5 m away at every zoom. A
step of the view moves `P0` in the world, so the spirits stay where they are
in the room.

A script can send a spirit to a place of the world, for example to the
scroll of a Creature lesson. The game makes the spirit 4 times as large
there, which is 73 cm in the room. With `spirit_world_size=1` (default) the
spirit at the place has the angle of size that it has at its near depth,
about 9 degrees (`tabletop.md`, "A spirit at a place of the world"). With
`test_keys=1`, Shift and Home change the key.

## 10. Buttons and keys

| Input | In the God view |
| --- | --- |
| Right stick forward or back | Zoom out or in one step (`god_zoom_forward_in=1` turns the direction), about the land that the head looks at (`god_zoom_about`). With `god_zoom_smooth=1`, zoom while the stick is held |
| Right stick left or right | Turn one step about the head |
| Left stick | Move over the land while it is pushed (`move_stick`, section 5.7) |
| Left grip | Grab the land: pull to move it across, and up or down |
| Y (left controller) | Hold it for the ring menu, and move the hand to an item (`controllers.md`, section 10) |
| X (left controller) | No job |
| The left menu button | Esc: open the game menu (see below) |
| Both grips | Turn and scale the land |
| Right grip, trigger, B | Pick up, click, draw a gesture. A trigger drag on the land moves nothing |
| A | Hold to aim an arc and a ring. Let go to go there. B while A is down cancels (section 14) |
| Mouse: double click | Go to the land under the mouse (section 14) |
| Mouse: left drag on the land | Pan: the land under the mouse stays under it (section 5.3) |
| Mouse wheel | One zoom step a click |
| Tilt keys | Ground up or down 5 cm, one step for each press |
| Turn keys, middle mouse | One turn step |
| Ctrl F11 | The God view off and on. Off is the immersive view |
| F11 | Recenter: the waist point goes under your head again |

For a left handed player (the left controller is the Hand, `controllers.md`,
5.6) the hands change sides:
the pointing hand's stick zooms and turns, the other hand's grip moves the
land, across and up and down, and the other hand's stick moves the player.
The right hand then has the ring on B, and the draw button of the left hand
is Y. The table has the layout of `button_layout=1`. `controllers.md`, 5.1,
has the layout of `button_layout=0`, with Y for Esc and X for the ring.

**The menu button.** The menu button of the left controller sends the game's
Esc key. The pump sends WM_KEYDOWN and WM_KEYUP to the game window (scan code
1), one press for one Esc. The game opens its menu. The menu does not close
with Esc again: `MainMenu::CanESCOut` returns false, so the player closes it
with "Continue Game". A Quest gives a program no menu button of the right
controller, so a left handed player has the same button, on the controller of
the pointing hand (`controllers.md`, 3.6).

**The menu ray.** While a dialog box is up (`SetupBox::GetCurrentActiveBox`
`0x00407ED0`, which reads `0x00C4CD20`), the direct hand steps aside. The
pointing hand points like a mouse: its aim ray picks the render pixel (the
pointer mode of `controller_hand=0`), and the trigger clicks. A thin ray (2 mm,
warm white) leaves the aim pose and shows where it points. It ends on the
cursor in both eyes (see "The depth of the menu" in `controllers.md`). When the
box closes, the direct hand comes back on the same frame.

### Ini keys

All in `[bwvr]`. The launcher has the play mode, the grab row, the two rows
of the zoom and the row of the walk with the stick. The keys of the teleport
are in section 14.6, the keys of the grab are in section 5.6, and the keys of
the walk with the stick are in section 5.7.

| Key | Default | Meaning |
| --- | --- | --- |
| `play_mode` | 4 | 4 is the God view. 0 gives the God view too. 1 is the stereoscopic window, and 3 is the table |
| `god_ground_down_cm` | 60 | The ground below the head at the last recenter, 10 to 200 |
| `god_units_per_meter` | 100 | The scale at the first start |
| `god_zoom_step_pct` | 50 | One zoom step changes the scale by this percent, 5 to 300 |
| `god_zoom_smooth` | 0 | The zoom of the stick (section 2). 0: one step for each push. 1: the smooth zoom, which changes the scale while the stick is held. The launcher row "Zoom with the stick". Ctrl, Shift and Insert change it with `test_keys=1` |
| `god_zoom_smooth_ms` | 500 | The top rate of the smooth zoom: the time in which a full push changes the scale by one zoom step, 100 to 5000 |
| `god_zoom_test_toggle` | 0 | A test with no key press: at this frame of the controllers the test key of `god_zoom_smooth` acts. 0 is never |
| `god_zoom_about` | 1 | The point that a zoom keeps at its place in the room (section 2). 1: the land point that the head looks at. 0: the land point under the head. 2: the Hand's point. The launcher row "Center of the zoom". Ctrl, Shift and Delete change it with `test_keys=1` |
| `god_zoom_about_far_cm` | 300 | With `god_zoom_about=1`, the point is at most this far from the head, 50 to 1000 |
| `god_zoom_about_test` | 0 | A test with no key press: at this frame of the controllers the test key of `god_zoom_about` acts. 0 is never |
| `god_ground_step_cm` | 5 | One ground step, 1 to 50 |
| `god_far_units_per_meter` | 4000 | The farthest zoom, 10 to 8000 |
| `god_spawn` | 1 | 1: the player goes where the game puts its own camera (section 15). 0: the view is built at the start, a load and the end of a story only, with the focus under the recenter pose |
| `god_spawn_ahead_cm` | 50 | How far the land point that the game camera looks at is in front of the head, 0 to 150 |
| `god_camera_at` | 1 | 1: the game camera is over the land point under the head. 0: over the recenter place (section 7). Ctrl and Numpad 1 change it with `test_keys=1` |
| `god_turn_keeps_focus` | 1 | 1: a snap turn alone turns the game camera about its own focus (section 7). 0: it puts the game camera over the land under the head |
| `tilt_help_degrees` | 3 | A look of the head up or down at 20 degrees a second or faster counts as the game's tilt: one tilt event of the help for each so many degrees, one in each game turn (section 7, "The tutorial"). 0: the head sends no tilt events |
| `turn_help_degrees` | 5 | A snap turn counts as one turn event of the help for each so many degrees, one in each game turn, so the turn lesson of the tutorial ends with one full turn (section 7, "The turn lesson"). 0: one event for a snap turn |
| `script_distance` | 1 | 1: a script that measures the distance from the camera gets the distance from the player (section 16). 0: the game's camera |
| `script_view` | 1 | 1: a script that asks if the camera sees a thing gets the view of the head (section 16). 0: the game's camera |
| `cutscene_window` | 1 | 1 plays a cut scene on a screen in the room (`cutscene-window.md`). 0 plays it in the full view with held shots |
| `depth24` | 1 | 1 asks for a 24 bit Z buffer (section 8) |
| `near_plane_cm` | 5 | The near plane at most this far in the room, with a 24 bit Z buffer. 0 keeps the game's |
| `near_plane_cm_16` | 10 | The same, with a 16 bit Z buffer |

The test words of the fake controller, with `fake_controller=2`: `x` and `y`,
the face buttons of the left hand, `lmenu`, its menu button, `lgrip` and
`lpoint`, the grip and the points of the left hand
(`fake_controller_left_points`), `lstick_left`, `lstick_right`, `lstick_up`
and `lstick_down`, the stick of the left hand, and `movekey`, the test key of
`move_stick`.

**The test mouse.** A desktop run must not move the real mouse. So
`fake_mouse_script` moves the pointer and presses the left button inside the
mod (`pointer_map.cpp`, `FakeMouseFrame`). Its words have the form
`word@frame+frames`, as in `fake_controller_script`. The frames count the
frames with a drawn scene, from 0.

| Word | What it does |
| --- | --- |
| `home` | The pointer goes to `fake_mouse_x_pct` and `fake_mouse_y_pct` of the render (50 and 50), on the first frame |
| `home2` | The same, to `fake_mouse_x2_pct` and `fake_mouse_y2_pct` (50 and 70) |
| `button` | The left button is down for the frames, through `LHMouse::SetButtons` |
| `double` | Down, up, double click and up, one step on each of 4 frames, through `LHMouse::SetButtons` |
| `move_right`, `move_left`, `move_up`, `move_down` | The pointer moves `fake_mouse_step_px` render pixels (8) on each frame, as a move of the real mouse |
| `wheel_up`, `wheel_down` | One click of the wheel, on the first frame |
| `tilt_up`, `tilt_down` | The tilt key, a tilt of 1 or -1 at `0x00C5B0C4`, for the frames |
| `turn_left`, `turn_right` | The turn key, a turn of 1 or -1 at `0x00C5B0C8`, for the frames |
| `god` | Ctrl F11, on the first frame |

The launcher removes these keys from `bwvr.ini` when it saves.

## 11. The code

| File | What it holds |
| --- | --- |
| `src/mod/table_math.h` | `GodRoom`, `PoseWorld`, `PoseRoom`, `GroundInRoom`, `ScaleAbout`, `GodZoomAbout`, `GodZoom`, `GodRaise`, `GodTurn`, `GameCameraOver`, `RayLevel`, `RayLand`, `GodSpawn`, `CameraLookPoint` and `HeadYaw`, as pure functions |
| `src/mod/zoom_math.h` | The smooth zoom: the share of the top rate for a push (`StickShare`), the time of a frame (`FrameSeconds`) and the ratio of a frame (`FrameRatio`). The point of a zoom: its place across the room for each value of `god_zoom_about` (`PointAcross`), the level (`ZoomLevel`), the point in the world (`PivotAt`) and the rule for a hill at the new place of the head (`ClearHead`). All are pure functions |
| `src/mod/walk_math.h` | The walk with the stick: the push (`Push`), the forward direction of the head and of the controller (`HeadForward`, `AimForward`), the speed in the room (`Speed`), the step of a frame (`Step`), the view after a step (`Walk`) and the filter of the land that the view follows (`FollowStart`, `FollowStep`). All are pure functions |
| `src/mod/tabletop.cpp`, `tabletop.h` | The God view in the table state: the place at a game camera (`PlaceGod`, `ApplyGodSpawn`), the steps (`ApplyGodSteps`), the point of a zoom (`GodZoomPoint`, `ZoomClearHead`), the smooth zoom (`TabletopGodZoomHold`, `ApplyGodZoomHold`), the walk with the stick (`TabletopGodWalk`, `ApplyGodWalk`), the mouse pan (`MousePan`), the camera switches and events, the hook on `GGame::ClearMap`, the values that follow the scale, the Hand's size, the ears and the game camera it wants |
| `src/mod/near_plane.cpp` | The near plane hook, for the God view, the table and the immersive view |
| `src/mod/teleport.cpp`, `teleport.h`, `aim_smooth.h` | The teleport: the states, the arc and the hit on the CPU, the refusals, the mouse double click, the aim filter, the log (section 14) |
| `src/mod/comfort_camera.cpp` | The game camera write (`GodAfter`), the watch of the game's own camera moves (`WatchBefore`, `WatchAfterGame`, `WatchEnd`), the turn, the wheel, the tilt keys (`GodTiltKeys`), the game camera after a land drag (`GodDragBefore`, `GodDragAfter`), the write of the grab on the game camera (`ComfortCameraGrabMove`) and the log of the camera events (`h_Help`) |
| `src/mod/pointer_map.cpp` | The mouse ray in the room (`PointerMapMouseRay`), the game's land drag hooks and the test mouse |
| `src/mod/world_grab.cpp` | The grab: the state machine, the table solve with its ground limits (`TableSolve`, `TabletopGodLimitHeight`), the camera switches for a turn and a zoom of the grab, and the solve on the game camera |
| `src/mod/controllers.cpp` | The sticks (`PumpStick`, and `PumpWalk` for the walk), the layout of the buttons (`ButtonLayout`), the menu button (`PumpEsc`), the pointer mode while a dialog box is up (`DirectHandNow`, `ControllersMenuRay`), the grip points and the fake left hand |
| `src/mod/script_camera.cpp` | `ScriptCameraStoryOwns`, and `ScriptCameraOwnerNow` for the scripts |
| `src/mod/hand_direct.cpp`, `near_math.h` | The player that a script sees: the hooks on the four script words, the latch of the player and the test of section 16.4 (`ScriptPlayerNow`, `PlayerDistance`, `PlayerIsCamera`, `HeadSees`) |
| `src/mod/cutscene_window.cpp`, `cutscene_window_math.h` | The cut scene window: the signal, the state of each frame, the screen and the walls (`cutscene-window.md`) |
| `src/mod/mod_main.cpp`, `src/launcher/launcher.cpp` | The play modes, the default, and the keys |

## 12. Tests

**The math test** (`bwvr/test/controller_math/`, run by `.\dev.ps1 build`)
checks the pure functions at 7.5, 100 and 2000 units per meter:

- The recenter pose is 60 cm of the room over the waist point.
- The Hand is 19 cm long at every scale.
- A real hand 60 cm under the recenter pose is at the waist height, and a head
  that leans and turns with a still hand moves it by nothing.
- A zoom step in and out keeps the land under the head at its room point and
  at its height in the room, keeps the heading, and does nothing at a limit.
  A hill over the head comes under it after 14 zoom steps out.
- The smooth zoom (`zoom_checks.h`): a stick at the dead zone asks for no
  zoom, a stick at full asks for the top rate, and a larger push never asks
  for less. A stick at rest and a frame of no time give a ratio of exactly 1.
  A full push for 0.5 s is one step, at 90 and at 30 frames a second. A frame
  of 1 s counts as 50 ms. On each frame of a hold the land point under the
  head keeps its room point, and the heading stays. After the stick is back
  the view does not change in 30 frames. A hold stops at each limit and never
  passes it, and a zoom step is the zoom about the land point under the head.
- The point of a zoom (`zoom_checks.h`), with a head 30 cm right and 40 cm
  forward of the recenter place: the direction of a head from the real
  `head_pose.cpp`, the level, and the rule for a hill at the new place of the
  head. `god_zoom_about=0` gives the point under the head. With 1, a look 20
  degrees down at flat land gives the land point on the line of the look,
  1.511 m across from the head. A hill in front of the flat land is found
  first. A look at the sky, a level look and a look straight up give the
  point of the line at 3 m. A line that meets the land at 2.99 m and a line
  that meets it at 3.01 m give points 1 cm apart. A head with no direction
  gives the point under the head. With 2, the Hand's point is the point, and
  with no Hand it is the point under the head. A step in and a step out about
  the looked-at land and about the Hand's point keep that point at its room
  point within 0.01 cm. The land of the level keeps its height, the land
  under the head moves a half and a third of the distance to the point, and
  the step back puts it under the head again. The point of a hold keeps its
  room point while the head turns 9 degrees.
- A ground step raises every land point 5 cm in the room and not across, and a
  step down puts it back.
- A snap turn keeps the head's world point, and a turn of 90 degrees right
  puts the land that was on the right in front.
- A pan keeps the grabbed land in the hand, moves the land as far as the hand,
  and keeps the waist height. Without the vertical grab, a hand that moves up
  moves no land.
- The vertical grab (`GodGroundHeight`, `TableGrabSolve` with `vertical`): a
  hand 20 cm up raises the land 20 cm in the room and moves no land across,
  a hand 20 cm down lowers it 20 cm, a still hand moves nothing, a move
  across and up keeps the same move across, and two hands keep the height of
  the press. For the limits: a request inside the range is used, a request
  past 10 cm or 2.5 m stops there, a sweep through both limits never leaves
  the range and never moves more than the request, a land outside the range
  only moves toward it, and a request that comes back into the range is used
  at once.
- Two hands that spread to twice the distance halve the scale, down to life
  size, and keep the land under their middle.
- The mouse pan (`GodPanChecks`): the ray of the press finds the land at the
  height of the waist point in the room, on the ray. After the pointer turns,
  the grabbed land is on the new ray, within 0.0003 cm, at the point where the
  ray meets the level plane, and the scale, the heading and the height of the
  waist point stay. A still mouse moves no land. A hill in front of flat land
  is found first. A ray that goes up, a ray that meets the plane past the
  Hand's reach, and a level ray find nothing.
- The game camera is 400 units from its ground point, 50 degrees over it,
  along the heading.
- The walk with the stick (`walk_checks.h`): the values of the key and the
  order of the test key. A stick in the dead zone gives no push, a full push
  gives the top speed in each direction, and a diagonal is not faster. The
  forward direction of a head that is level, turned, turned with a look down,
  and straight down, and no direction for a head on its side. The forward
  direction of a controller that points down by 30 and by 75 degrees, and no
  direction at 80 degrees and straight down. Forward, back and to the side
  on the stick with the head 30 degrees left and right. One second of a full
  push is 1.5 m at 90 and at 45 frames a second, a frame of 0.5 s counts as
  50 ms, and a stick that is back gives no step. A step puts the land that
  was ahead under the head, keeps the height, the heading and the scale, is
  the pan of a grab with one hand, and is 400 units at 4000 units for each
  meter and 0.75 units at life size. A hill at the new place goes 10 cm below
  the head. The follow of the land (`FollowStep`): a step over flat land and
  a frame with no step move nothing, also over a hill. A step of 10 cm onto
  higher land gives the exact answer of two stages, and ten steps of 1 cm
  give the same. On a long hill the view rises with the land, 50 cm of the
  walk behind it. A bump of 10 cm moves the view by less than 15 percent of
  its height, and 80 cm later the view is back within 5 percent.
- The teleport (14.8), the place at a game camera (15.9) and the player that a
  script sees (16.8) have their own checks.

**Desktop runs** use the rig of `backend.md` with `play_mode=4`,
`fake_controller=2`, `controller_hand=1`, `-NEWGAME` on Land 1, a level fake
head and the player's render size. Put the test keys under `[bwvr]`, and not
in the last section of `bwvr.ini`. Check the first lines of each log for the
render size, because the recipes depend on it.

- The start dialog needs a click on "Skip straight to the Creature select"
  and on OK. The dialog is a dialog box, so the fake hand points like a mouse
  there (the menu ray, section 10). Set the points `-14,2,100` and
  `-15,-11,100`, `fake_controller_yaw=8`, `fake_controller_pitch=1`,
  `fake_controller_sweep_yaw=8` and `fake_controller_sweep_pitch=-6`. The
  script is `point@400+1 trigger@440+8 pose2@500+1 point@520+1
  trigger@560+8`. At a render of 2880 by 2880 the aim gives the pixels 1298.6,
  1420.7 and 1296.2, 1547.9. The two eye pictures show the dialog about 48
  pixels apart, because of the interface depth, and the game's own place is
  the middle.
- The Creature select story holds the camera, and the God view stands aside
  until the pen.
- A zoom, ground and grab run at the pen: a log line shows each zoom step, the
  ground step ("the ground went 20.00 cm up in the room"), the limit ("the
  ground stops at its limit"), and a count of the frames in which the view
  moved with still hands (it must be 0).
- The menu button: the fake Y sends Esc ("the menu button sends Esc"), the box
  opens, and the game places the Hand while it is up. A second Y sends one
  more Esc and the menu stays, as `CanESCOut` says.
- The point of a zoom: `spawn_run.ps1 -Choice keep` with a head 30 cm right
  and 40 cm forward of the recenter place (`fake_head_right_cm`,
  `fake_head_forward_cm` and the word `lean@780+2400`),
  `god_zoom_about_test=1200` and the steps `stick_up@800+10 stick_down@860+10
  look@920+900 shot@960+1 stick_up@1000+10 shot@1040+1 stick_down@1080+10
  shot@1120+1 stick_up@1240+10 stick_down@1300+10`. The first step has a
  level head: the log must give the point of the line of the look at 3 m, and
  a move of the land under the head of 150 cm. The steps with the word `look`
  must give "the land that you look at", a point of the zoom that moved 0.00
  cm in the room and from the drawn head, and a move of the land under the
  head of half or a third of the distance to the point. The same thing is in
  the middle of the three eye pictures. After the test key the steps must
  give "the land under your head" and a move of 0.00 cm. For the Hand, set
  `god_zoom_about=2`, give a point of the hand with `-Points` and the word
  `point@900+1`, and step in and out: the log must give "the Hand's point"
  with the same world point for the two steps. For the smooth zoom, set
  `god_zoom_smooth=1` and hold `stick_up@1000+45 stick_down@1100+45` in a
  `look@920+520`, and `stick_up@1420+45` across the end of the look: each
  hold must give a point of the zoom that moved 0.00 cm in the room, also
  when the head turns back in the hold.
- The smooth zoom: `spawn_run.ps1 -Choice keep` with `god_zoom_smooth=1`,
  `god_zoom_about=0`,
  `god_zoom_test_toggle=1720`, a head 30 cm right and 40 cm forward of the
  recenter place (`fake_head_right_cm`, `fake_head_forward_cm` and the word
  `lean@780+1100`) and the steps `stick_up@800+45 stick_down@900+90
  stick_down@1040+10 stick_up@1100+40 stick_right@1110+10 stick_right@1180+10
  stick_left@1210+10 stick_up@1250+450 stick_down@1750+10 stick_down@1780+10`.
  Each hold must have its three log lines (section 2): the scale changes on
  each frame of the word, by 2 zoom steps a second, the land under the head
  and the drawn head move 0.00 cm, and the scale is the same 30 frames after
  the stop. The hold with `stick_right` stops when the two words are down
  together, and does not start again when `stick_right` is up. The two turns
  act. The long hold stops at 7.50 units per meter ("the zoom is at its
  limit"). After the test key the last two words are two zoom steps. The same
  steps with `god_zoom_smooth=0` give one zoom step for each word that
  starts with the stick at rest. The fake stick has full pushes only, so the
  rate of a small push has only the math test.

- The walk with the stick [R]: `spawn_run.ps1 -Save temple` with
  `fake_controller_play_frame=800`, `fake_controller_yaw=30`,
  `fake_controller_sweep_yaw=30`, `fake_controller_sweep_pitch=-30`,
  `god_zoom_about=0` and the steps `look@830+1300 lstick_up@900+60
  lstick_right@1020+60 movekey@1130+1 lstick_up@1150+60 movekey@1260+1
  lstick_up@1280+60 movekey@1360+1 stick_up@1380+10 lstick_up@1440+60
  movekey@1540+1 lstick_up@1560+60 lstick_down@1680+60 lstick_left@1680+60
  lgrip@1790+90 lstick_up@1810+40 layoutkey@1920+1 lstick_up@1940+50`. The
  word `look` turns the head 30 degrees to the left, and the left controller
  points 30 degrees to the right. Each walk must have its three log lines
  (section 5.7). With `move_stick=1` the player goes 30.0 degrees to the left
  of straight ahead, and with 2 goes 30.0 degrees to the right. With 0 the
  log has no walk, and the next walk starts at the place where the last one
  stopped. A push to the right with the head as the direction goes 60.0
  degrees to the right. A push back and to the left with the controller as
  the direction goes 105.0 degrees to the left. Each walk moves the view on
  60 of 60 frames at 150.0 cm of the room each second, at 100 and at 66.67
  units for each meter: the distance in units is the time of the 60 frames
  times 1.5 m times the scale. 30 frames after each stop the waist point is
  0.000 units from its place at the stop, and the drawn head moved 0.00 cm.
  The push during the grab is refused with one log line. After the word
  `layoutkey` the stick opens the ring and moves nothing. The scripted stick
  has full pushes only, so the speed of a small push has only the math test.
- The follow of the land [R]: `spawn_run.ps1 -Save temple` with
  `move_stick_trace=320` and the steps `lstick_up@900+300
  lstick_right@1300+300 lstick_down@1700+200`, once with
  `move_stick_follow_land=1` and once with 0. The first walk goes 7.9 m of
  the room over the hills at the Temple, where the land under the head goes
  from 0 to 112 units (at 100 units for each meter), and the third walk is
  over the sea. In each walk of both runs each frame moves the view at
  149.93 to 150.07 cm of the room each second, with frames of 14.8 to 46.2
  ms.
  - With 1: the land under the head is 61.6 cm below the head at the start
    of each walk, and 63.8, 61.6 and 61.6 cm at their ends. In the hills it
    is 19 to 115 cm below the head (the filter is behind the land on the
    steep slopes), and the land never comes nearer than 10 cm. The height of
    the view changes by at most 2.5 cm in a frame, and its speed up or down
    changes by at most 14 cm each second from one frame to the next. Over
    the sea the height does not change. After each stop the waist point
    keeps its height in the report of each window.
  - With 0: the view keeps its height. The land comes to 10 cm below the
    head, and the limit puts it 32.8 cm down in the room. While the limit
    acts, the view goes up with each bump of the land at once: up to 5.5 cm
    in a frame, and its speed up or down changes by up to 181 cm each second
    from one frame to the next. After the hills the land is 108.2 cm below
    the head, and 122.3 cm after the second walk.

**The mouse pan and the tutorial events** need a run with the test mouse. Use
`play_mode=0` (it must log the God view), `-NEWGAME` on Land 1,
`fake_mouse_step_px=4`, home at 45 and 49 percent, and the second home at 50
and 70 percent. The words click the start dialog (`home@380+1 move_right@382+1
move_down@383+2 button@440+4 move_left@450+3 move_down@455+34 button@600+4`).
After the first story they drag the land (`home2@3000+1 button@3010+150
move_right@3030+50`), zoom, move the ground and turn (`wheel_up@3300+1
tilt_up@3400+5 tilt_down@3500+5 turn_left@3600+5`), and do the same with the
God view off (`god@3700+1 wheel_up@3800+1 home2@3900+1 button@3910+100
move_right@3930+15 turn_left@4100+5 god@4200+1`). The log must show the pan
("the mouse takes the land", "stayed within ... cm of the mouse ray"), no
move of the view after the release, the game camera put back after the drag,
and the camera events (0x304 for a wheel click and 0x308 for a land drag).

## 13. Open items and what needs the headset

- **Shown on the desktop:** a pick up, a set down and a throw in the God
  view, at 66.7, 100 and 225 units per meter (`hand_run.ps1` and
  `throw_queue.ps1` in `bwvr/tools/desk`).
- **Not tested on the desktop:** the two hand zoom by the grips has only the
  math test. An advisor goes to a place of the world in a desktop run only
  with a test (`tabletop.md`, "A spirit at a place of the world"). No desktop
  run has the sound.
- **The mouse pan on the desktop** and the **tutorial events** (section 7)
  have not run in the game. The numbers and the bits come from the binary.
  The run is in section 12.
- **The mouse** pans, zooms, turns and moves the ground. Its Hand still has
  the game's own size, which grows with the distance, and its throw has the
  speeds of the life size scale. These are in `hand_direct.cpp`.
- **A load** on the same land is not tested. Save, pan far away, and load:
  the view must come back at the place of the save, with the same scale. A
  load at the start of the game has a run: in a run with a test save
  (`tools/desk/README.md`, "A fast start") the view is built at the game
  camera of the saved game.
- **The tilt keys.** Which key of the game sets a tilt of more than 0 is not
  checked. If the keys move the ground the wrong way, change the sign in
  `GodTiltKeys`.
- **The grab on the game camera** (5.5): the game's own lift of the camera
  above the land inside `GCamera::Update` is not proved, and what
  `GetAltitude` returns outside the map is not known. A recenter and a grip
  that jumps 50 cm start a new anchor, and no run has shown it. What a right
  press on bare land does in the game, in each Hand state, decides whether
  the claim rule of `grab_hand=2` changes anything. A step "drag the land" of
  the tutorial waits for event 0x308, which a grab sends.
- **Not built for the grab:** a vignette while the camera moves, a cut for the
  advisor anchor (`SpiritAnchorCameraCut`) on a fast grab, and a hint text
  "Grab the world" (see `controller-hints.md`).
- **The smooth zoom** (section 2) is not the default. A headset test must
  show how it feels: whether a scale that changes in each frame is
  comfortable with no blink, whether 500 ms for a step is a good top rate,
  and whether the dead zone of 30 percent fits a real stick. No run has shown
  a push with the zoom switch off, a small push, or the key Ctrl, Shift and
  Insert with a real key.
- **The point of the zoom** (section 2). A headset session compared the
  three points. The three stay as choices, and the land that the head looks
  at stays the default. The line of the look is the forward direction of the
  head, and the eyes of a player can look lower than that. No run has shown
  the Hand's point with no controller, a hill at the new place of the head,
  the mouse wheel with the look, or the key with a real key. These have the
  math test only.
- **The walk with the stick** (section 5.7). A headset test must show
  whether the walk is comfortable with the head as the direction and with
  the controller as the direction (Shift and the Menu key change the rule),
  whether 150 cm of the room for each second is a good top speed near the
  land and far out, and whether the dead zone of 30 percent fits a real
  stick. It must also show whether the follow of the land is calm on the
  hills and keeps the land near enough to the hands on a steep slope, and
  whether the walk is even (the count of the late frames in the line of the
  end of a walk, "An even walk" in section 5.7). No run has shown a small
  push, the edge of the map, a push with the switch for the drag off, a
  controller that points straight up or down, or the test key with a real
  key. These have the math test or no test.
- **The tutorial** (section 7, "The interface levels"). A desktop run shows
  the first step: the walk to the village with the teleport and with the
  stick (section 14.9). On that walk and on the way to the Temple the game
  has the turn and the zoom off, so the snap turn and the zoom do nothing
  there, and the player turns the head or the body. No run has the later
  steps: the way to the Temple (level 4), the three lessons of the camera
  (levels 13 to 15), the walk around the mountain (levels 1 and 10), the look
  up and down at the mountain (level 11: the game has each move off, and a
  look of the head gives the tilt events), and the lessons in which the game
  has each camera control off (levels 8 and 12). A headset session must show
  that a player comes through each step.
- **Needs the headset:** the ground height of 60 cm, the zoom step of 1.5
  times, the start scale of 100, the Hand at the life size scale near the
  eyes with the near plane of 5 cm, the look of the land and the clouds at
  each scale, the frame rate at 90 Hz, the sound, the spirits, whether a
  story that stands aside feels right, the mouse pan with a real mouse (the
  land must stay under the Hand, and must not move after the release), the
  tracking noise times the gain of the grab on the game camera, the squeeze
  levels of real Touch and Index controllers, and the frame age of the hands.

## 14. The teleport

Hold A to see where the teleport goes, and let go to go there. The code is in
`teleport.cpp`. The commands of the Creature are in `creature.md`.

**What the player does.** Hold A. An arc leaves the pointing hand and falls on
the land. A ring on the land shows the target. Let go of A to go there. The
arc and the ring are cyan when the target is valid. They are red when it is
not. B, while A is down, cancels the teleport and starts no gesture.

**What A does in each play mode.**

| View | Arc | The move on release |
| --- | --- | --- |
| God view (play mode 4) | A parabola | The head goes over the target. The land under the head keeps its height in the room. The scale and the heading stay |
| Table (play mode 3) | A straight ray | The target goes to the table center |
| Immersive view (Ctrl F11 off, with no story) | A parabola | The eye goes over the target at the same height over the land. The heading stays |
| The mouse, in the God view | None | A double click goes to the land under the mouse |

Each move is one cut. With `blink` above 0 a short black blink covers it
(`blink` is 0 by default). Nothing moves after it. The
teleport never glides. With `teleport=0` the A button sends the game's own
jump.

### 14.1 The press

A press of A is refused, with one buzz of 30 ms and no arc, when:

- a grip holds the world,
- a gesture is being drawn,
- a story or a follow camera owns the camera,
- a dialog box is up (`SetupBox::GetCurrentActiveBox` `0x00407ED0` reads
  `0x00C4CD20`),
- the camera input is off (`mode+0x2EC`), or there is a fight (`mode+0x44`),
- the game's switch for the drag of the land is off: bit 8 of
  `CameraHelp::EnabledFeatures` (section 7),
- the hand cannot act (a menu, or the game is not in front), or the aim pose
  is not tracked.

The log gives the reason. A reason that starts while the player aims cancels
the aim, with two ticks.

**The switch of the game** (`teleport_switch`, 1). The teleport moves the
player over the land, as the walk with the stick does (section 5.7). So the
two have one rule: the press of A and the push of the stick keep to the same
switch of the game, bit 8. When a script turns that bit off, the game holds
the player in place, and it refuses both. The first step of the tutorial has
bit 8 only (section 7, "The walk to the village"), and there the player goes
to the village with the teleport or with the stick. With `teleport_switch=0`
the press of A keeps to the switch of the double click jump, bit 0x10, which
the tutorial turns on after the village. `TeleportPossible` says whether a
press of A can move the player now, and the hint card shows the line of A
only then (`controller-hints.md`, 9.4).

### 14.2 The arc

The arc is a parabola in meters of the room, from the aim pose of the pointing
hand:

```
p(t) = o + v0 * d * t + 0.5 * (0, -g, 0) * t^2     v0 = sqrt(g R)
```

`g` is 9.8 and `R` is `teleport_reach_m` (10). `R` is the flat range of a 45
degree aim. There are 48 steps of equal time, so 49 points. The arc ends when
it falls 0.5 m under the height of the sea in the room, or after 3 s. An aim
more than 60 degrees up is the sky, and there is no arc.

The points go into the world with the map of the grip. It is the map that
`HandToWorld` uses: `world = base + map * room`. In the God view and on the
table that is `P0 + Lt * s * room`. In the immersive view it is the eye plus
the room basis. So the shape of the arc is the same at every zoom: the reach in
the world is `R * s`. It is 75 units at 7.5 units a meter, 1000 at 100, and
40000 at 4000. At 4000 the island is 1.3 m across, so a downward aim lands
within arm's reach.

The land hit is on the first segment of the polyline whose end is at or under
the land. Eight halvings of that segment find the point. So the hit is on the
tube that the player sees. The land height is `TeleportGround`: the height of
`GetAltitude` on the map, and 0 for the sea and for a point off the map. The
arc makes at most 56 lookups a frame, and the ring makes 72. A hand that is
already under the land has a hit at the hand. On the table the ray goes in
steps of 2 cm of the room (`RayLand`).

### 14.3 A valid target

A target is valid when all of these are true:

- It is on the map (0 to 5120).
- Its land height is more than 0. The sea is red.
- In the God view it is within 3000 units of the map center
  (`kGodGameRadius`). In the immersive view it is within 3400 units, which is
  inside the pull back of `GCamera::Update` at 3500.
- It is more than 30 cm of the room from the point under the head. A nearer
  target is red, because there is nothing to do.

Release on a red target, or with no hit, and nothing happens. The hand gives
two ticks of 0.2 for 30 ms. A move gives one pulse of 0.4 for 60 ms.

### 14.4 Going there

**The God view.** `TabletopGodTeleport` keeps the target. The next main camera
call (`ApplyGodSteps`) makes the move as one step: `GodTeleport`
(`table_math.h`), `ClampMap`, `Derive`, one more `g_god_version`, the blink,
and the help event. `GodAfter` then puts the game camera over the new place,
once. `GodTeleport` gives:

```
H   = the head in the world
r0  = the room height of the land under H, kept 0.10 m to 2.5 m under the head
G'  = (G.x + T.x - H.x, ground(T) - (r0 - Tg.y) * s, G.z + T.z - H.z)
```

The land under the head keeps its height in the room. That height is also
the level of a zoom (section 2). If the
waist point kept its height, a hill could come up through the head. The head
goes over `T` within 0.01 units.

**The table.** `TabletopTableTeleport` puts the target at the table center
(height 0). The scale and the heading stay.

**The immersive view.** `ComfortCameraTeleport` keeps the move
`(T.x - E.x, ground(T) - ground(E), T.z - E.z)` (`CameraTeleportDelta`). After
the game's update of the camera mode, the six Zoomers, the copies of the mode,
its fall back focus and its pivot get that move. The heading stays. The move
is dropped, with a log line, while a script holds the input or the pivot,
while a fight holds the camera, or while a jump flies.

**The help event.** Each move sends the game's own event for a jump:
`CameraHelpCallback` `0x305` (to the land) or `0x306` (when an object is under
the Hand, `+0x3C8` of the interface). So a tutorial step that waits for a jump
can end.

**The mouse.** In the God view, `h_ModeUpdate` calls
`TeleportMouseDoubleClick` before the game's update. It acts when the latch
`0x00E85350` has the double click bit `0x10`, the mouse owns the pointer, and
the game would allow its own jump: the reasons of the game for the press of
A, with the jump switch (bit 0x10) for each value of `teleport_switch`. The
mouse ray from the head finds the land (`RayLand`). The bit is cleared, so the
game does not fly its camera. Then the God view goes there as for A. The mouse
teleport works with `teleport=0` too, because the game's jump moves only a
camera that the God view does not show. A double click on a villager goes to
the land under the mouse ray, and not to the object.

### 14.5 The drawing

`overlay.cpp` has a second source for the arc, with its own switch, so the arc
draws with `overlay=0` too. A tube of 6 sides and 6 mm of the room across, with
no cap, 576 triangles at most. A ring of 24 points that follow the land, 2 cm
over it. The inner radius is 0.75 of the outer radius, so the band has a
width of a quarter of the radius at each size. A valid ring has a disc in the
middle. A red ring has a cross. The meshes are opaque, with the depth test and
the depth write on, as the hands. The tube of the arc and the lift of the ring
are in meters of the room, so they look the same at every scale.

**The size of the ring.** By default (`teleport_ring_mode=1`) the ring has one
size in the world: an outer radius of 11 units (`teleport_ring_units`). The
God view scale goes from 7.5 units a meter (life size) to 4000, and the start
scale is 100. At the start scale the ring is 11 cm of the room. It gets
larger in the room when the player zooms in and smaller when the player zooms
out, but never smaller than 4 cm (`teleport_ring_min_cm`) and never larger
than 40 cm (`teleport_ring_max_cm`). So the ring says how large the place is
from 27.5 to 275 units a meter (about 3 zoom steps in and 4 steps out from
the start scale). Outside this range it has the limit: 40 cm at life size
(0.3 units) and 4 cm at 4000 units a meter (160 units). The lift of 2 cm
stays in the room at each size. The rule is in `teleport_math.h`.
`teleport_ring_mode=0` gives a ring of one size in the room
(`teleport_ring_cm`, 11 by default). The overlay
clips them at the near plane of 5 cm (section 8). `kMaxTriangles` has 648 more
triangles for this.

**The smooth aim.** A hand in the air shakes a little, and the shake grows
with the length of the arc. So `TeleportFrame` filters the aim before it finds
the arc. `aim_smooth.h` has a one euro filter. It has a low cutoff when the
aim is still, so the ring does not shake. The cutoff grows with the speed of
the aim, so a real move shows at once. The aim direction (a unit vector, in
radians a second) has a cutoff of 0.9 Hz plus 6 Hz for each radian a second.
The aim point (meters of the room) has 1.5 Hz plus 8 Hz for each meter a
second. The filter starts again at each press of A. The release goes to the
target of the last drawn frame, so it goes to the smooth target. The report
line of the teleport shows the largest gap between the raw aim and the smooth
aim.

### 14.6 The keys

All in `[bwvr]`. They are ini only. The launcher has no row for them.

| Key | Default | Meaning |
| --- | --- | --- |
| `teleport` | 1 | 1 is the arc. 0 gives the game's own A jump |
| `teleport_reach_m` | 10 | The flat range of a 45 degree aim, 2 to 100 |
| `teleport_ring_mode` | 1 | 1 is a ring of one size in the world (`teleport_ring_units`), 0 is a ring of one size in the room (`teleport_ring_cm`) |
| `teleport_ring_units` | 11 | The outer radius of the ring in world units for mode 1, 1 to 1000 |
| `teleport_ring_min_cm` | 4 | The smallest outer radius in the room for mode 1, 1 or more |
| `teleport_ring_max_cm` | 40 | The largest outer radius in the room for mode 1 |
| `teleport_ring_cm` | 11 | The outer radius of the ring for mode 0, 5 to 60 |
| `teleport_switch` | 1 | The switch of the game that the press of A keeps to (section 14.1). 1: the drag of the land (bit 8), as the walk with the stick. 0: the double click jump (bit 0x10) |

With `test_keys=1`, Ctrl with Insert changes `teleport_ring_mode` between 1
and 0. The pointing controller gives a long pulse for 1 and a short pulse for
0. Ctrl and Shift with the comma key change `teleport_switch` between 1 and
0, with the same pulses. The word `teleportkey` of the scripted controller
does the same in a desktop run.

### 14.7 The log

The tag is `teleport`. The lines are:

```
teleport     : on. Hold A to aim an arc with a reach of 10 m, ...
teleport     : teleport_switch=1 (start). The press of A keeps to the game's switch for the drag of the land (bit 8), as the walk with the stick does
teleport     : A does not teleport now: a story owns the camera (teleport_switch=1), frame N
teleport     : the view and the game let A teleport now (teleport_switch=1), frame N
teleport     : frame N, God view, aim from x y z m in the room, 28.0 degrees up, 31 arc points, 100.0 units a meter
teleport     : hit land at x y z, the target is land. Valid (cyan), 56 land lookups this frame
teleport     : no hit: the aim is the sky, so there is no arc
teleport     : A is refused: a story owns the camera, frame N
teleport     : A is up, so go to x y z (God view), frame N. The eye is at ...
teleport     : the ring has a radius of 11.0 cm of the room and 11.0 units of the world, at 100.0 units a meter, rule 1
teleport     : the God view moved. The head went from ... to ... off. The land under the head was 0.600 m below it, and now it is 0.600 m. Scale 100.00 to 100.00, heading ..., version N.
teleport     : A is up on a red target: the target is the sea, so nothing happens, frame N
teleport     : the aim is cancelled: B was pressed, and it starts no gesture, frame N
teleport     : the mouse double click goes to x y z, the target is land
teleport     : N aims over N frames (N on a valid target, ...). N releases went, ...
teleport     : N land lookups, at most N in one frame. The arc was drawn in N eye passes with N triangles.
```

The ring line goes out once for each aim.
The aim line goes out on the first frame, on a change of the hit or of the
reason, and every 90 frames, 90 lines at most. The lines "A does not teleport
now" and "let A teleport now" go out on each change of what the view and the
game say to a press of A, with no reason of the hand, 40 lines at most.

### 14.8 Tests

The math test (`TeleportChecks`) covers:

- The arc: 49 points, the first point is the origin, and the first segment
  goes along the aim within 1.5 degrees. The steps in x and z are equal. A 45
  degree aim over flat ground lands at `R` within 1 percent, at 4, 10 and 20
  m. A level aim from 1.5 m lands at `v0 * sqrt(2h/g)`. The heading of the
  landing point is the heading of the aim. A wall gives a hit at the wall. No
  crossing gives no hit. A start under the land is a hit at the start. A 70
  degree aim is the sky, and 59 degrees is not.
- `CameraTeleportDelta`: the eye ends over the target at the same height over
  the land.
- `GodTeleport` at 7.5, 100 and 2000 units a meter: the head goes over the
  target within 0.01 units, the land under the head keeps its room height
  within 1 mm, `s` and `psi` do not change, a target at the head gives the same
  pose, and a hill over the head is kept 10 cm under it.
- `RingRadiusCm` (`teleport_checks.h`): mode 0 gives 11 cm at each scale. Mode 1
  gives 11 cm at 100 units a meter, 7.3 cm at 150, 16.5 cm at 66.7, 40 cm at
  7.5 and 4 cm at 4000. The size is always from 4 to 40 cm, never grows with a
  zoom step out, and is 11 units of the world between the limits.
- `BuildTube`: 24, 12 and 576 triangles, every corner at the radius from the
  line (a straight tube within 1e-6 m, the arc within 2 percent), and unit
  normals. `BuildLandRing`: 72 triangles (valid) or 52 (red), every point at the
  land height plus the lift, the band from the inner to the outer radius.
- The aim filter: 0.76 degrees of tremor leave 0.12 degrees. A step of 20
  degrees is within 0.4 degrees after 5 frames, and a sweep of 60 degrees a
  second is 0.7 degrees late.

### 14.9 The run on the desktop

Use the rig of section 12, with `fake_head_pitch=0`. The Creature select story
holds the view until the pen. A press of A in a story is refused, and the log
says so.

`aim_queue.ps1 -Runs ring1,ring0 -NewGame` in `bwvr/tools/desk` makes three
teleports at the pen: at 100 units a meter, after two zoom steps in (44.4) and
after five zoom steps out (337.5). The runs show for each teleport that the
head is over the target (0.000 units off), that the land under the head keeps
its room height, and that the scale and the heading stay. With
`teleport_ring_mode=1` the ring has a radius of 11.0 cm, 24.7 cm and 4.0 cm of
the room at the three scales. With `teleport_ring_mode=0` it has 11.0 cm at
each scale. In the test save `pen` the runs show the same ring sizes and two
teleports: the third aim is on the sea (`tools/desk/README.md`, "Known limits
of the recipes").

1. The switch of the game, at the start of the tutorial. `spawn_run.ps1
   -Choice normal` starts a new game with the row "Start the game normally":
   the whole first story, and then the walk to the village. Four texts of the
   story wait for a click, so the script has a press of the trigger at the
   frames 6750 and 7350, and then each 220 frames from 7950. The story ends
   at about frame 9700, and the log says "the camera controls of the game are
   0x48": the drag is on, and the jump is off. The run starts with
   `teleport_switch=0`, and the word `teleportkey` changes the switch.
   `fake_controller_turns=0,-40,0` and `aimturn@700+1` turn the aim to the
   land near the head. The log shows:
   - With `teleport_switch=0`, a press of A gives "A is refused: the game's
     jump switch is off (camera switch 0x10)".
   - After `teleportkey`, "the view and the game let A teleport now
     (teleport_switch=1)", and a press of A gives the aim, the hit and "A is
     up, so go to". The walk with the stick starts in the same state of the
     game.
   - The cards of the land (`prompts=2`): "[A] Teleport" at the right hand,
     and "[Grip] Move the land, [Stick] Move, [Y] Ring menu, [Menu] Game
     menu" at the left hand. With `far_grab=0` no far target takes the place
     of these cards.
   - One teleport of 140 units and four frames of the walk bring the player
     to the village: the story of the village plays, and then the log says
     "the camera controls of the game are 0x18".
2. With the switch on, two aims and a release: `a@N+90 pose2@N+30+1`. The
   log shows the aim, the hit, the reason, and "A is up, so go to". Then the
   line "the God view moved": the head is over the target within 0.01 units,
   the land under the head keeps its room height within 1 mm, `s` and `psi` do
   not change, and the version goes up by 1. The comfort report shows the game
   camera written once, and the help report shows 0x305.
3. Eye captures during the hold show the tube and the ring in both eyes, with
   a disparity of `fx * e / d * hx`.
4. A sweep toward the sea (`fake_controller_sweep_pitch`) shows a red arc, and
   the release does nothing.
5. An aim 70 degrees up has no arc ("the aim is the sky").
6. `b@N+5` during the hold cancels, and the log of the gesture stays silent.
7. The mouse: `double@N+4` after `home2@M+1`, with `fake_controller=0`. The
   log shows "the mouse double click goes to".
8. `teleport=0`: A sends the game's jump.

### 14.10 Not tested

- Not run on the desktop: the red arc on the sea, the aim at the sky, the
  cancel with B and the mouse double click (the steps 4 to 7 of 14.9).
- The look of the arc and the ring in the headset: the width of 6 mm, the
  colors, the limits of 4 and 40 cm of the ring, and whether the arc is easy to aim at 4000 units a
  meter.
- Whether the aim pose (the point of the controller) feels right as the start
  of the arc, or whether the arc must start at the index finger.
- The immersive move and the table ray have no desktop run. Ctrl F11 gives
  the immersive view.
- `teleport_switch`: no run has the key Ctrl, Shift and comma with a real
  key, a switch that goes off while the player aims, or the teleport on the
  walk around the mountain, where the game has the drag on and the jump off
  too. Only a headset session can show whether a player finds the way to the
  village with the teleport and the stick, with no snap turn there.

## 15. The place at a camera of the game

The player goes where the game puts its own camera. The code is in
`comfort_camera.cpp` (the watch), `tabletop.cpp` (`PlaceGod`) and
`table_math.h` (`GodSpawn`, `CameraLookPoint`, `HeadYaw`).

### 15.1 Why the view follows the game camera

The view is built from the game camera at the first main camera call of a new
game, and again at the end of a story, at a load and at Ctrl F11. But the game
camera can go to a new place at other times. At the start of a new game the
start dialog is up, and the game camera is at the shore of Land 1: at 1486 27
1974, with its focus at 1443 1 2079. After the row "Keep your old Creature"
the game plays no story. 55 frames after OK it makes a new `CameraModeNew3`
object, and that mode brings the camera to the door of the Temple in about 1.1
s. A view that is built one time only would stay at the shore.

A script moves the camera only with a story camera (section 15.2). The focus
of a story camera can be under the recenter pose, or under the land. So the
view takes the look point of the camera (15.4) and not the focus, and it
follows each new place of the game camera (15.3).

### 15.2 When the game puts its camera at a place

`[V]` is from the disassembly.

| Case | What the game does | The view |
| --- | --- | --- |
| The first frame of a new game | The start camera of the land, with the start dialog over it | Built there |
| The row "Keep your old Creature" | No story. A new `CameraModeNew3` object, which brings the camera to the Temple: at 1891 33 2521, looks at 1907 30 2517, heading 104 degrees, 17 units away | One cut when the camera is at rest |
| The other rows of the start dialog | A story with the cinema bars | The cut scene window, then the place of 15.4 |
| The end of a story | `END_CAMERA_CONTROL` takes `CameraModeScript` off. A script can put the camera back with `RESTORE_CAMERA_DETAILS`, which calls `GCamera::SetPositionAndFocus` `0x004438C0` [V] | The place of 15.4, or the view of before the story (15.5) |
| A script with no story camera | `SET_CAMERA_POSITION` `0x006EC8F0` and `MOVE_CAMERA_FOCUS` `0x006ECBA0` cast the top mode to `CameraModeScript`, and they do nothing with `CameraModeNew3` on top [V]. So a script needs a story camera to move the camera | The view stands aside |
| `SET_CAMERA_POS_FOC_LENS` `0x006EE3C0`, `RESTORE_CAMERA_DETAILS` `0x006EE390`, `TempleChallenge::StartScript` | They call `SetPositionAndFocus` with no test of the mode [V, the callers] | A set between two updates |
| The Temple key | `GGame::DoAction` calls `fn_0064A7A0(0)`, which calls `CameraModeNew3::ZoomToCitadel` `0x00457B60` with the flag 1 [V]. The first press pulls the camera back at its place. A second press in 500 ms flies to the Temple, 130 units away and 45 degrees down | The first press is not a new place. The flight is a jump: one cut |
| The key for the whole land | `fn_0064A7A0(1)`: `ZoomToCitadel` to a land point, 2200 units away, with the flag 0 [V] | A jump: one cut |
| A double click jump | The game's flight, events 0x305 and 0x306 | The mouse: the teleport takes the click. A with `teleport=0`: the view goes to the place of the jump |
| A load | `GGame::ClearMap` calls `ResetCameraModeNew3` `0x00460B20`, and `GCamera::Load` `0x004433A0` clears the mode stack [V, the calls] | Built again after `ClearMap`. A camera that the load sets later is a new place |
| The Temple rooms | The game draws from a different camera | The view stands aside and comes back where it was. A game camera that is at a new place then is a set between two updates |
| The Creature key, a bookmark, a fight | Not read in the binary | A flight is a jump, and a set is a set between two updates |

### 15.3 The watch

`comfort_camera.cpp` hooks the update of the player's camera mode. The watch
reads the destinations of the six Zoomers of the game camera at three points
of each update:

| Point | A change there is |
| --- | --- |
| Before the game's update | From outside the update: a script, a load, a key. A new mode object is a change too |
| After the game's update | The game's own: a jump, or a camera that goes to its place |
| After the writes of the mod | The player's move. These values are the base for the next update |

- **A move of the player is not counted.** A change in an update with a
  camera event of the player (0x300 to 0x304 and 0x308) is the player's. A
  write of the mod is the player's too: a step, the grab, a teleport, a snap
  turn. It ends a move of the game that is not at rest yet.
- **The game brings its camera to rest after a write.** After each write of
  the mod, and after a story, the game fits its camera to the land again. It
  puts the focus on the land along the camera line, and it pulls the camera
  in when a hill hides the focus. The focus can move 25 units and the
  position 190 units, in 250 to 1300 ms. That is the end of the player's
  move, and the watch does not count it. A change from outside the update, a
  new mode and a jump are always counted.
- **One move, one cut.** The changes of the game make one move. The move ends
  when the camera is at rest: no change for 250 ms and no flight. A jump that
  landed at once needs a rest of 100 ms only: the game puts the focus on the
  land in its next update, and the look point is read after that. Then the
  view goes there, with the blink of a teleport (only when `blink` is above 0). A camera that
  the game moves for many frames gives one cut at its end, never a glide.
- **A small move is not a new place.** A new place needs a turn of 5 degrees,
  or a look point (15.4) that moved 5 percent of the camera distance and
  5 units at the least. The first press of the Temple key moves the camera
  41.7 units back and the focus 2.6 units: the view stays.

### 15.4 The place

`PlaceGod` puts the view at a game camera. `GodSpawn` is the math:

```
L     = the land point that the camera looks at (the look point)
theta = the yaw of the head in the room
psi   = the heading of the camera - theta
r     = head + ahead * (sin theta, 0, cos theta)       across only
G     = L - s * Lt(psi) * (r - Tg)                     across only
G.y   = L.y - (h - Tg.y) * s
```

- **The look point** (`CameraLookPoint`) is the first land point on the line
  from the camera to its focus, when the line meets the land before the
  focus. Otherwise it is the focus, put on the land. The player's camera mode
  keeps its focus on the land, so there the look point is the focus. A script
  can put the focus under the land or in the air. Both poses of such a camera
  give one look point.
- **Where the camera goes.** The view is built from the destinations of the
  Zoomers of the game camera, and not from the camera of the main call. A
  camera that still glides then gives its place at once. At the first frame
  of a new game the main call has its focus at 1415 -16 2146, and the
  destination is 1443 1 2079.
- **In front of the head.** The look point is `god_spawn_ahead_cm` (50) in
  front of the head of that frame, along the flat forward of the head. The
  flat forward of the head gets the heading of the camera. So a player who
  stands off the recenter pose, or who turned in the room, still has the
  place in front. With no head pose the recenter pose is used.
- **The height.** `h` is the height in the room that the land under the head
  had at the last view, kept between 10 cm and 2.5 m below the head. The land at the
  look point gets it. So the ground steps of the player stay. At the first
  start the look point is at the waist. A hill under the head stays 10 cm
  below the head.
- **Why 50 cm.** With the land 60 cm below the head, a point 50 cm ahead is
  78 cm from the eyes and 50 degrees below eye level. A bent arm reaches it,
  and the player sees it with the head a little down. At the Temple start
  the Creature stands 4.4 units from the look point, so it is 46 cm in front
  of the head at 100 units per meter.
- **The turn at a jump.** The game keeps the heading of its camera at a jump
  to the Temple, and the camera that the mod wrote has the heading of the
  room forward. So the jump turns the world by the yaw of the head. The place
  is in front of the head in each case.

### 15.5 The view comes back

A script often puts the camera back where it was before its story. In the
God view that camera is the game camera of the view: the camera that
`GodAfter` wrote, as the game brought it to rest. `tabletop.cpp` keeps the
pair of that camera and the view (`g_pair`). When the game camera is at that
place again, and the player did not move the view after it, the view comes
back exactly where it was. The limits are 5 units for the position, and for
the focus 5 percent of the camera distance and 5 units at the least. The log
then says "the view comes back where it was".

### 15.6 The scale

The scale stays. A game camera far from its focus shows more land, and a
scale from the camera distance could show the same land: `s = distance / 1.2
m` gives 14 units per meter at the Temple start (17 units) and 108 at the jump
to the Temple (130 units). That is not built, for three reasons. The scale
is the player's own choice, as the ground height is. A change of the scale
is a second change that the player did not make, at each jump of the game.
And the distance of a story camera is the choice of a shot, not of a view:
the start at the Temple would be 7 times larger than the view of the player.
The player zooms with the stick after the cut.

### 15.7 The keys

| Key | Default | Meaning |
| --- | --- | --- |
| `god_spawn` | 1 | 1 is this section. 0 builds the view at the start, a load and the end of a story only, with the focus under the recenter pose. The log still names each new place of the game camera |
| `god_spawn_ahead_cm` | 50 | The look point, in front of the head, 0 to 150 |

With `test_keys=1`, Ctrl and Numpad 0 change `god_spawn` between 1 and 0.
The view then goes to the game camera of that moment by the rule that it has.
So the two places can be compared at one camera. Numpad 0 with no Ctrl is
the test key of the cut scene window.

Three keys are tests with no key press, for a desktop run:

| Key | Meaning |
| --- | --- |
| `spawn_test_temple=N` | At update N of the player's camera mode the mod calls `ZoomToCitadel`, as the Temple key does, to the land point `spawn_test_x` and `spawn_test_z` (2238 and 3156, the Creature pen) |
| `spawn_test_flag` | The flag of that call. 0 goes at once. 1 is the key: the first press pulls the camera back |
| `spawn_test_toggle=N` | At update N the test key acts, and 300 updates later it acts again |

### 15.8 The log

The tag of the place is `god view`, and the tag of the watch is `comfort`.

```
god view     : god_spawn=1. When the game puts its camera at a new place, you go there: the land point that it looks at is 50 cm in front of your head (god_spawn_ahead_cm). Ctrl and Numpad 0 change it
comfort      : the game moved its own camera (a new camera mode of the player). It was at 1486.3 26.7 1973.7 and looked at 1442.9 0.8 2078.5. Now it is at rest at 1891.0 32.5 2520.7 and looks at 1907.3 29.6 2516.7, update 690: the land point that it looks at went 638.5 units, from 1442.9 2078.5 to 1907.3 2516.7, and the camera turned 126.5 degrees. The move took 1390 ms and 80 updates. Changes: 63 in the update, 0 between updates, 0 jumps, 1 new modes. Camera events 0x000, the camera input was the player's. This is a new place
god view     : the view goes to the game camera (a new camera mode of the player). The game camera is at 1891.0 32.5 2520.7 and looks at 1907.3 29.6 2516.7, heading 104.0 degrees, 17.1 units from its focus. The waist point was at 1462.1 0.0 2032.3, heading -22.5 degrees. Now it is at 1858.8 29.5 2528.7, heading 134.0 degrees, 100.0 units per meter. The land at 1907 29 2517 is 50 cm in front of your head and 0.60 m below it, and the land under your head is 0.62 m below it (god_spawn=1).
god view     : the game camera went to a new place (a jump of the game camera): it is at ... and looks at .... The view stays where it is (god_spawn=0).
comfort      : the player moved the view, so the move of the game camera is dropped, update N
comfort      : the game put its camera at a new place 1 times. 0 of its moves were too small for a new place, and 0 ended when the player moved the view. 0 updates had a camera move of the player. In 45 updates the game brought its camera to rest after a write.
test key     : god_spawn=0. The view does not follow the game camera, and the land point that it looks at is under the recenter pose
```

- The causes of a move are: "a new camera mode of the player", "the game set
  its camera between two updates", "a jump of the game camera" and "the game
  moved its camera in its update". The causes of a build are: "the first view
  of this land", "a new land", "a load or a new game", "Ctrl F11", "the end of
  a story", "the end of a cut scene" and "the test key".
- Each place has one line with the cause, the game camera and its focus, and
  the waist point before and after. The line of the watch comes before it,
  with the counts of the changes and the camera events of the game.
- A small move has a line only the first 4 times. The report counts each
  one.
- The log has at most 80 lines of the place and 60 of the watch.

### 15.9 Tests

The math test (`spawn_checks.h`) covers:

- `HeadYaw`: a level head, a head that turned 30 degrees, a head that looks
  60 degrees down, a head that looks back, and a head on its side (no yaw).
- `CameraLookPoint`: a focus on the land, under the land and in the air, a
  hill before the focus, a camera under the land, and the two poses of a
  story camera on one line, which give one look point.
- `GodSpawn` at 7.5, 100 and 2000 units per meter: the look point is 50 cm
  in front of the head, at the height that was asked, for a head at the
  recenter pose and for a head that stands off it and turned 40 degrees. The
  forward of the head has the heading of the camera. The scale does not
  change. The same camera gives the same pose two times. A camera that looks
  the other way puts the player on the other side. A head at the recenter
  pose with 0 cm ahead gives the rule with no look ahead.

**Desktop runs** use a new game on Land 1 with a choice of the start dialog,
the fake controller with `fake_controller_front=1`, and a render of 2560 by
1440. The fake head looks 30 degrees left and 20 degrees down from frame 600
(the word `look`), so the captures show the land in front. The runs to make:

- "Keep your old Creature": the log must show one place 1.4 s after the new
  mode, with the Temple door in front of the head in the capture.
- The test key (`spawn_test_toggle`): `god_spawn=0` puts the waist point at
  the look point with the heading of the camera, and `god_spawn=1` puts it
  back in front of the head.
- `ZoomToCitadel` with the flag 0 (`spawn_test_temple`): the jump lands at
  once, and the view goes there 125 ms and 9 updates later.
- The same with the flag 1: one press is not a new place ("this is not a new
  place, so the view stays").
- The story to the pen: one place, and no second place in the updates in which
  the game brings its camera to rest after a write.
- A grab for 400 frames, a zoom step in and a zoom step out: 0 new places.

### 15.10 Not tested, and what needs the headset

- **The view comes back** (15.5) has no run. A story that puts the camera
  back is needed.
- **A load, a fight and the Creature key** have no run. The watch has no
  rule of its own for them.
- **The Temple key, the Temple rooms, a bookmark, the next town and the key
  for the whole land** are jumps of the game camera, and the view goes to
  each one. In the Temple the God view stands aside, and after the Temple it
  is at its place from before the Temple. The ring does not send the Creature key in the
  God view: it makes one teleport to the Creature (`controllers.md`).
- **Ctrl and Numpad 0** has no run with a real key. The test
  `spawn_test_toggle` calls the same function.
- **A move by the arrow keys** counts as a move of the player (event 0x308),
  so the view does not follow it. That was not seen in a run.
- **A grab that moves the land, and the mouse pan,** have no run with the
  watch. Each one writes the game camera, which is a move of the player by
  the rule of 15.3.
- **Only the headset can show** whether 50 cm ahead is the right distance,
  whether the height of the look point is right on a slope, whether the cut
  1 s after "Keep your old Creature" is good, and whether a quest puts
  the player at the right place.

## 16. The player that a script sees

A script of the game asks about the camera to learn what the player does, and
in the God view the game's own camera is not the player. So a script that
asks about the camera gets the player. The code is in `hand_direct.cpp`
(`h_ScriptDistance`, `h_ScriptPosView`, `h_ScriptThingView`,
`h_ScriptThingSees`, `LatchPlayer`, `ScriptPlayerNow`) and `near_math.h`
(`PlayerDistance`, `PlayerIsCamera`, `HeadSees`).

### 16.1 What the scripts ask

`[V]` is from the disassembly and from the compiled scripts
(`Scripts\Quests\challenge.chl`, LHVM version 7). Each script word is a
cdecl function with no arguments. It takes its values from the stack of the
script machine with `ScriptDLL::POP` `0x006F6BC0`, and it gives its answer
back with `ScriptDLL::PUSH` `0x006F6BA0`. The script machine is at
`0x00D95C10`.

| Script word | Function | What it does [V] |
| --- | --- | --- |
| "camera position" | `GScript::GetCameraPos` `0x006ECFF0` | Gives `LH3DTech::g_camera` `0x00EA1DB8`, the camera of the last `LH3DTech::UpdateCamera` |
| "get distance" | `GScript::GetDistance` `0x006F8CA0` | Takes two points and measures them with `GUtils::GetDistance` `0x0074CDE0`, which uses x and z only: the distance on the map. Below 0.5 units it gives 0 |
| "position in the field of view" | `GScript::IsPosFieldOfView` `0x006F8060` | Asks `fn_0081F1D0`: the point is in front of the camera, and its picture is on the screen. The test uses the matrix that the engine has at that time (`0x00EA9E40`) |
| "thing in the field of view" | `GScript::IsGameThingFieldOfView` `0x006F8130` | For an object with a mesh it asks `LH3DBoundingBox::CheckRegionOnScreen` `0x00868C80` for the ball of the mesh. For each other thing it asks `fn_0081F1D0` for its place |
| "thing can view the camera" | `GScript::GameThingCanViewCamera` `0x006FE4D0` | Asks if `LH3DTech::g_camera` is in front of the thing, in an angle that the script gives |

The scripts use these words at many places:

- "camera position" is at 561 places. At 111 of them, in 70 scripts, the
  next word is the distance to a thing, and the script compares it with a
  limit. 95 limits are constants from 1 to 250 units: 27 are 100, 20 are 150
  and 11 are 200. Each test means "the player came to look". 21 tests have
  NOT after them: "the player went away".
- The two field of view words are at 107 places (53 for a thing, 54 for a
  point). 78 of them are beside a distance test: "the player is near and
  looks at it". 42 have NOT after them: for example, a script deletes or
  moves a thing only while the player does not look.
- "thing can view the camera" is at 4 places: the guide of Land 1 and the
  monk.

Some of the tests:

| Script | Test | What it starts |
| --- | --- | --- |
| `PointOutScroll` | Less than 100 units from a place | The advisor points at the scroll of the missionaries |
| `ThrowingStonesChallengeHighlightNotify` | Less than 150, and in the view | The two advisors speak of the scroll |
| `LookAfterGuideWalkingAroundMountain` | Less than 10 and 25 | The guide of Land 1 walks faster |
| `MeetTheGuide` | Less than 90, and in the view | The cut scene of the guide |
| `ZoomInAndOut` | Two stones in the view, then the two not in the view | The steps of the zoom lesson |
| `LostBrotherDead` | Less than 20 | The dialogue at the body |
| `FollowUs`, `LostTheMother` | 50 to 100, with NOT | The mother waits, or the player lost her |
| `CreatureGuardian` | NOT less than 100, and NOT in the view | The script deletes the guardian |
| `HermitWardPact` | NOT less than 1, and NOT in the view | The player does not look at the hermit |

The scripts `ChallengeHighlightNotify` and its copies compare the distance
with the variable `NotifyDistance`. No script writes that variable. [I: its
value is 0, so the test is always false, in the plain game too.]

### 16.2 Why the game's camera is wrong for the tests

- **The distance.** After the player moves the view, `GodAfter` puts the
  game's own camera 400 units from the place of the player, 50 degrees above
  the land (section 7). That is 257 units on the map. No limit is more than
  250. So each distance test would be false for a thing at the place of the
  player, at each zoom.
- **The view.** `TabletopGraphicsDone` gives the engine the game's own camera
  again after each frame. The scripts run in the game turn, between two
  frames. So the view tests would use the camera 400 units behind the player,
  along the heading of the room. That camera sees almost all that is near
  the player, also behind the head. It does not turn with the head, and it
  does not change with the zoom. The zoom lesson asks if two stones are in
  the view, so a zoom of the God view would not end that step.

### 16.3 Which point is the player

- **For a distance,** the player is the head and each hand that is tracked:
  the Hand's point, and the grip of each controller. The distance of the
  player is the least of their distances on the map
  (`near_math::PlayerDistance`). A hand at a thing is "the player came
  close". The tests with a limit of 10 to 30 units need that: at 100 units a
  meter the head is 60 cm over the land. A hand counts only within 2.5 m of
  the room from the head.
- **For a view test,** the player is the head: its place and its three axes,
  as the frame was drawn. The view angle is the angle of the game for that
  frame, which the mod sets from the headset (110 degrees in a desktop run).
  A point is in the view when it is in front of the head and inside that
  angle (`near_math::HeadSees`). This is the game's test for the picture
  that the headset shows.
- **For "thing can view the camera",** the camera is the head.

The game measures a distance on the map, so the height of the head does not
count. At 100 units a meter a thing 50 cm in front of the head is 50 units
away, and at 29.6 units a meter 15 units.

### 16.4 When the player stands for the camera

The player is the answer only while the view of the player is at the place
of the game camera. The mod asks this at the moment of the script word
(`ScriptPlayerNow`, `near_math::PlayerIsCamera`), and not from a time limit.
In each other case the script gets the game's own answer, as in the plain
game.

| Case | What the mod reads | The script gets |
| --- | --- | --- |
| The menu, the intro, the first frames, a Temple room | The God view did not own the last frame | The game's camera |
| A load, Ctrl F11 | The God view does not own the view now | The game's camera |
| The first game turn of a story, from the instruction that starts it | The camera mode on top of the stack of `GCamera` is not a mode of the player (`ScriptCameraOwnerNow`) | The camera of the story |
| The cinema bars are up | `HelpSystem + 0x45E8` (`CutsceneWindowBarsUp`) | The camera of the story |
| A story or a cut scene plays | The God view stands aside (section 7), so it did not own the last frame | The camera of the story |
| The last frame of a story, and the frames until the window is open | The same | The game's camera, where the story left it |
| About 1 s after a story, and after a new game | The game moves its own camera to a new place, and the camera is not at rest (`ComfortCameraGameMoves`, section 15.3) | The game's camera |
| Each other frame of the God view | | The player |

**The place of now.** The player is latched once a frame, inside the draw
(`LatchPlayer`, from `HandDirectFrame`). The scripts run in the game turn
(`GGame::ProcessNetworkPackets`), before the next main camera call. Only
that call moves the view: a teleport, a zoom step, a turn, the place at a
game camera. So the latch is the view that the player has when a script
asks. A frame with no main camera call has no latch.

**Why not a time limit.** When the game makes a new camera mode, its camera
is at the new place at once, but the view goes there only when that camera is
at rest, about a second later (section 15.3). In that time the view of the
player is not at the game camera, so the script gets the game's camera.

**A story keeps its camera.** A script that keeps "camera position" to put
the camera back after a cut scene is not changed: only the three kinds of
test are. In a story each test gets the camera of the story. With
`cutscene_window=0` and a story camera with no bars, `script_camera.cpp`
holds the shot, and the script gets the held shot.

### 16.5 The tests that ask if the player went away

A test with NOT is true only when the head and each hand are away. So a
hand that stays at a thing keeps the player there.

- With the game's camera each such test would always be true in the God view:
  for the game the player would always be away. `FollowUs` would then say that
  the player lost the mother, and `CreatureGuardian` would delete the guardian
  while the player is beside it.
- The limit of 1 unit of `HermitWardPact` is true for the player as for the
  game's camera: no point of the player is that near in play.
- When the player stands for the camera in one game turn and not in the
  next, the answer can change in that turn. That is at the start of a story
  and for about 1 s after it. In that time the game's camera is the camera
  of the story, which is near the place of the story.
- A view test with NOT is true when the head does not face the thing. So a
  thing that a script deletes "while the player does not look" can go while
  the player is near with the head turned away. That is what the script
  asks for.

### 16.6 The keys

| Key | Default | Meaning |
| --- | --- | --- |
| `script_distance` | 1 | 1: a distance from the camera is the distance from the player. 0: the game's camera |
| `script_view` | 1 | 1: a view test uses the view of the head. 0: the game's camera |

With `test_keys=1`, Ctrl and the 7 key of the number pad change between the
player (the two keys as the ini has them) and the game's camera for all
tests. The pointing controller gives a long pulse for the player and a short
pulse for the game's camera.

When a function does not have the bytes of the disassembly, the mod does
not hook it, the key is 0, and a line with the tag `hooks` says so.

### 16.7 The log

```
scripts      : script_distance=1, script_view=1. In the God view a script of the game that asks about the camera gets the player. [...] Ctrl and the 7 key of the number pad change both
god view     : the waist point is at 1830.0 37.8 2720.0, [...] The scripts of the game got the player in the place of the camera 1851 times.
scripts      : Ctrl and the 7 key: a script that asks about the camera gets the game's camera, frame N
```

The log has no line for a test. The count in the report line of the God
view is the number of answers from the player in that report window. A
window with a story has no such line.

### 16.8 Tests

The math test covers `MapDistance` and `PlayerDistance` (the guide with the
head 20 units away and a hand 5 units away, a thing 300 units away, no point
of the player, a place that is not a number), the tests with NOT and the
limits of 1 and 250, each case of `PlayerIsCamera`, and `HeadSees` (ahead,
behind, 49 and 51 degrees to the side, the land at the feet, a head that
turned 90 degrees, 110 degrees).

**Desktop runs** use the start "Keep your old Creature", a render of 2880 by
2880, the fake head with the word `look`, and the test word `go` of
`seek_plan`, which teleports the head over a land point. After a teleport the
mod puts the game's camera 257 units on the map from the head. An advisor line
shows in the log as "audio adv : line N starts, sample S". The runs to make:

- `go` to 1830 2720, 80 units from the place of `PointOutScroll` (1755 2692,
  limit 100): the advisor comes out at once. With `script_distance=0` and
  `script_view=0` it does not.
- `go` to 2024.3 2382.3. The gold scroll of the throwing stones (1947.4 2360.2,
  limit 150 and in the view) is 80 units behind the head. With
  `script_view=1`, no line starts until a turn of 45 degrees brings the scroll
  into the view. With `script_view=0` the line starts at once, with the scroll
  behind the head.
- The start of the game and the story to the pen: each ask in a story gets the
  camera of the story, and each ask while the game brings its camera to rest
  gets the game's camera.

Check each log for input that was not from the test: no "the cursor moved"
line, and one "the mouse owns the pointer" line, at the start dialog.

### 16.9 Not changed, not tested, and what needs the headset

- **The first game turn of a story.** No run has a test in the game turn
  that starts a story, after the instruction that starts it. The two cases
  of 16.4 for that turn are from the code only.
- **A thing with a large mesh.** The game's test for an object counts the
  ball of its mesh. The mod tests the place of the thing. A house whose
  middle is out of the view, with a wall in the view, is "not in the view".
- **A marker at "camera position".** At about 114 places a script makes a
  marker thing at the camera, and at 150 it keeps the camera with its focus
  to put it back after a cut scene. These are not changed. [I: if a script
  uses such a marker as "where the player is", it is 257 units from the
  player on the map.]
- **`GetFacingCameraPosition`** `0x006EE710` (7 places) puts the Hand of
  another god in front of the camera. Each place is in a cut scene. Not
  changed.
- **The other readers of the engine camera** in the game logic (184
  instructions read `LH3DTech::g_camera`): the villagers, the Creature and
  the sounds. The sound has its own work (`audio.md`).
- **The Temple key, a load and a bookmark** have no run with a script test.
- **Only the headset can show** if the quests of a real session go on:
  the guide of Land 1, the mother of `FollowUs`, the zoom lesson. It can
  also show if the nearest hand is the right point for a real player, and if
  a script that deletes a thing behind the head is good in VR.
