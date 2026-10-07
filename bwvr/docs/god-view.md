# The God view (play mode 4)

The God view puts the land at the player's waist, in the manner of the VR
game "How to God". The player's hands are the Hand, one to one in place and
size, and the world moves and scales around the player so that the hands can
reach it. This is Buck's rule after the first headset test of the direct
hand (`hand.md`, "The real hand"): no reach gain, no Hand that grows, and
reach that comes from the world.

The God view is the camera of the tabletop mode (`tabletop.md`) with a
different room. The code is shared. Play mode 3, the table, works as before.

**The main mode.** Since 2026-10-02 the God view is the main mode and the
default. It replaced the immersive mode, play mode 0, after Buck's first
headset test: "There was no need to add a new mode, it should replace the
"immersive" mode, there's no need for both. It's much better in God mode."
The launcher offers the God view first, and no longer offers "Immersive". An
old `bwvr.ini` with `play_mode=0` gives the God view, in the launcher and in
the mod, and the mod logs it:

```
settings     : play_mode=0, the immersive mode, is now the God view, play mode 4
```

The immersive view stays in the code for two cases: a story, while the God
view stands aside (section 7), and Ctrl F11, which turns the God view off and
on as a test. The God view works with motion controllers and with the mouse.

This file is the design as built on 2026-09-30 and 2026-10-02, with the
desktop results.

## Summary

- **The map.** The land under the recenter pose sits `god_ground_down_cm`
  (60) below the head. There is no edge, no key color and no sea disc: the
  normal sky and sea go to the horizon.
- **The zoom** changes the scale about the land point under the head, so the
  player keeps their place in the world, and the ground under them keeps its
  height. It goes from the life size scale (7.5 units per meter) to 4000, in
  steps of 1.5 times.
- **The ground height** goes up and down with the left grip: the hand pulls
  the land up and down one to one. The mouse player has steps of 5 cm (the
  tilt keys). The view never follows the land up or down by itself.
- **The grab of the world** moves the land one to one with one hand, across
  and up and down, and two hands turn and scale it. Nothing moves after the
  release.
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
- **The menu.** Y on the left controller sends Esc, and a dialog box takes the
  pointing hand as a pointer (section 10).
- **The tutorial.** The zoom, the pan and the turn keep to the game's camera
  switches, and they send the game's camera events. So a tutorial step that
  waits for a zoom or a drag can end.
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

The table maps the room to the world with one similarity
(`tabletop.md`, 2.2):

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
divides it by 1.5. It turns about the land point straight under the head:

```
W  = the land point under the head (the head's x and z, the land height)
s' = s * ratio,  kept between 7.5 and god_far_units_per_meter
G' = W + (s' / s) * (G - W)
```

`W` keeps its world point and its room point, so the player keeps their place
in the world, and the land under them keeps its height in the room. A land
point off to the side comes nearer when the view zooms out. The heading does
not change.

**A hill over the head.** At the life size scale a hill of 100 units is 13 m
high. A pan can bring one over the head. So the height of `W` in the room is
first kept between 10 cm and 2.5 m below the head, the range of a ground step
(section 3). Each zoom step out then brings the hill top 1.5 times nearer to
that height. The math test zooms out 14 times at the life size scale, and a
hill top 12.73 m over the head comes down to 5.6 cm under it.

**The limits.** 7.5 units per meter is the life size scale of
`head_units_per_meter_x10`. At 4000 an arm of 70 cm reaches 2800 units, which
is inside the Hand's reach of 5600 (section 8). `god_far_units_per_meter` can
go up to 8000. A step at a limit does nothing and is logged. From 7.5 to 4000
is 16 steps.

**The inputs.** The right stick forward zooms in, and back zooms out. One push
is one step. The stick must come back below 30 percent before the next push
counts. The mouse wheel also steps the God view, one step a click, at most
one each 150 ms, and the game hears no wheel.

A zoom step needs the game's camera switch for the zoom (section 7, "The
tutorial"). While the switch is off, a push or a click does nothing, the
controller gives no pulse, and the log says why. A step that the view takes
sends the game's zoom event, 0x304.

## 3. The ground height

The left grip moves the ground up and down (section 5). With the grip closed,
a hand that moves up raises the land by the same distance, and a hand that
moves down lowers it. A raise is `G.y -= s * d`, where `d` is the rise of the
hand in meters: every land point goes `d` up in the room, and the scale and
the heading stay. The left stick has no job.

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

The grab of the table (`tabletop.md`, 10.7) works as it is:

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
  only while the zoom is allowed. Otherwise the turn or the zoom keeps its last value, so nothing
  jumps. A move of the grab sends the game's events: 0x308 for a pan, 0x300
  and the direction for a turn, and 0x304 for a zoom.

The sticks are refused while a grip holds the world, as before.

**The mouse pan.** A land drag of the mouse pans the view (`MousePan` in
`tabletop.cpp`). At the press, the ray of the pointer from the head finds the
land, in steps of 2 units, up to the Hand's reach of 5600 units (`RayLand`
in `table_math.h`). That land point is the grabbed point, and its height in
the room sets a level plane. On each frame the ray meets that plane
(`RayLevel`), and the pan of one hand of the grab (`TableGrabSolve`) puts
the grabbed point there. So the land point under the mouse at the press stays
under the mouse. It also stays under the Hand, which the game puts on the
same ray.

- The ray is the exact pointer through the head of the frame, `H * ray`
  (`PointerMapMouseRay` in `pointer_map.cpp`). The pointer is locked to the
  room, so the ray turns only when the mouse moves. The ray starts at the
  head of the press, so a head that moves with a still mouse moves no land.
- The pan follows the game's own land drag: the left button is down, and the
  hold status of the camera mode is 3 (`+0x8C` and `+0x1AC`). So a click on
  a villager picks it up as before, and a drag starts only where the game
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
and the villagers on it. Before 2026-10-02 the point stopped 9.5 cm over the
land (`hand.md`, "The point touches the land"). The grip takes, carries and
lets go like a real hand (`hand.md`, "The grip holds"). The pick fix of the
table (`tabletop.md`, 10.6) is installed for play mode 4 too, so a villager of
a few pixels can be picked at every scale: the screen test offers the objects
near the Hand, and the interface step gets the land under the Hand. The throw
and the set down use the rule of the table, with the gain `s / 7.5`. At the
life size scale that is the throw of the immersive mode. Zoomed out, a swing
carries the Hand far over the land, so a throw that ends outside the player's
influence goes from the last point of the swing inside it
(`tabletop.md`, 10.11).

## 7. The game camera

The picture does not come from the game camera, but the game logic does: the
sound starts, the detail lists, the help triggers and the pull back of
`GCamera::Update` at 3500 units from the map center. So after the game's
update of `CameraModeNew3`, `comfort_camera.cpp` (`GodAfter`) puts the game
camera over the player's place, in the fields the snap turn and the grab
write:

- The focus is the land under the waist point, kept within 3000 units of the
  map center.
- The camera is 400 units from it, 50 degrees above, along the heading of the
  view.

It moves only when the player moves the view: once for each step, and on each
frame of a grab. It does not follow the head, because `GCamera::Update` sets
its moving flag for a camera that changes, and the interface then clears the
gesture trail. It does not undo the game's own moves of its camera either. At
the build the game camera stays where the game put it, and the view goes to
it (section 15). No write happens while a script holds the input or the
pivot, while a fight holds the camera, or while a jump flies.

**Stories.** While a story or a follow camera holds the view
(`script_camera.cpp`, `ScriptCameraStoryOwns`), the God view stands aside.
The story plays at the life size scale, as in the immersive mode. When the
player's camera is back, the God view is built again at the game camera, with
the last scale and the last ground height (section 15). Ctrl F11 and a new
land build it again the same way.

**Cut scenes.** Since 2026-10-03 a cut scene does not play in the full view.
While the cinema bars of the game are up, the view closes to a screen 2 m
ahead, and the game shows its cut scene there as play mode 1 does
(`cutscene-window.md`). The God view stands aside for those frames in the
same way, and it is built again in the same way when the cut scene ends. The
paragraph above is still the rule for a story camera with no bars, for
example a camera that follows the Creature. `cutscene_window=0` gives that
rule to cut scenes too.

**A load.** A saved game of the land that is up has the same block table, so
the land does not look new, and the view stayed where it was. A post hook on
`GGame::ClearMap` `0x00552BB0` now builds the view again at the next main
camera call, in the same way. `LoadAllGame`, `StartPlaygroundGame` and
`GGame::Close` call `ClearMap`. The hook assumes that the load puts the game
camera in place before the next main camera call. A desktop load must
confirm that (section 13). Since 2026-10-04 the view also follows a camera
that the game puts in place later (section 15). The last scale lasts for the
session only. The launcher owns `bwvr.ini`, so the mod does not write the
scale there.

**The tutorial.** Scripts and the tutorial turn the camera controls on one at
a time, with the switches at `CameraHelp::EnabledFeatures` `0x009CDD6C`: bit
1 the tilt, 2 the turn, 4 the zoom, 8 the drag of the land and 0x10 the
double click jump. The game's camera code tests each bit before it acts, for
example at `0x0045C0E7` for the zoom and at `0x0045D3A6` for the drag. The
God view keeps to the same bits:

- A zoom step needs bit 4 (`TabletopGodStep`).
- The mouse pan and the grab need bit 8 (`MousePan`, `TabletopMove`).
- A two hand grab turns only with bit 2 and zooms only with bit 4
  (`TabletopGrabAllows`, called by `world_grab.cpp`).
- A snap turn already needed bit 2 (`MayTurn` in `comfort_camera.cpp`).

The tutorial counts the events of `CameraHelp::CameraHelpCallback`
`0x00449140`. Only the 0x300 events reach `HelpProfile::Trigger`. The 0x100
and 0x200 events feed the help hints only. They are errors and collisions:
0x102 is "Hand is too far away" and 0x200 is "Collide with ground". So the
two candidates of the design were not the zoom and the drag. The numbers
below come from the calls in `CameraModeNew3::Update` and from the game's own
names of the events at `0x009CDD70`. That table has no names for 0x301 and
0x302, so its names from 0x303 on are two places early.

| Event | Meaning | The game pushes it at | The God view |
| --- | --- | --- | --- |
| 0x300 | The camera turned | `0x0045C745`, for a turn key | A snap turn, and a turn of a two hand grab |
| 0x301, 0x302 | Then to the left, or to the right | `0x0045C772`, `0x0045C79F` | The same |
| 0x303 | The camera tilted | `0x0045C82B` | Never: the view does not tilt |
| 0x304 | The camera zoomed | `0x0045C3A8` | A zoom step, and a zoom of a two hand grab |
| 0x305, 0x306 | A double click jump to the land, or to an object | `0x0045DE85`, `0x0045DE1E` | The game's own jump |
| 0x307 | The zoom to the Temple | `0x00457EDE` | Never |
| 0x308 | A drag of the land, or a move by the keys | `0x0045D682`, `0x0045C981` | The game's drag code, for the mouse pan, and a pan of the grab |

The third argument names what made the move. The game's names for its bits
at `0x009CDDF0` are "keyboard" (1), "mouse button" (2), "both mouse
buttons" (4), "wheel spin" (8) and "wheel down" (0x10, the middle button).
The God view sends 8 with a zoom step and 2 with a move of the grab.

Before 2026-10-02 a snap turn to the right sent 0x301. The game sends 0x301
for a key turn of more than 0, which `comfort_camera.cpp` reads as a turn to
the left. So a snap turn now sends 0x301 for the left and 0x302 for the
right, in every play mode.

A hook on the callback (`h_Help` in `comfort_camera.cpp`) logs each change of
the event, up to 80 lines, and the report counts each event:

```
comfort      : camera help event 0x304, inputs 0x08
comfort      : camera help events: 0x300 x2, 0x302 x2, 0x304 x3, 0x308 x41, 6 others
```

## 8. Fog, detail and the near plane

`TabletopApplyFrame` writes the values of `tabletop.md`, 3.2, but they follow
the scale. The gain is `s / 7.5`.

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
least 27 times finer than those of the old 16 bit buffer with the game's 3.5
units. Zoomed out at 4000 units per meter, a step at the far edge of the
island goes from 39 mm of the room to 0.15 mm. That step is the likely
cause of the far flicker.

When a story owns the view, the God view stands aside, and the camera is at
the life size scale. The same hook then gives 5 cm (10 cm at 16 bit) at the
life size scale. Play mode 0 has the same rule, and play mode 3 has it at the
table scale, where the game's value stays. The interface depth does not
change with the near plane (`backend.md`, "The depth buffer").

## 9. Sound and the spirits

The ears are the head (`audio.md`, section 11). The distances of the
positional sounds are room distances: QMixer gets each distance mapping of
the game with min and max times `s / audio_units_per_meter` (40). So one
meter in the room sounds the same at every zoom, and a sound at the hand is
clear. A positional sound with no object, which the game puts at its camera,
is at the ears. A bird or a wave of the atmosphere stays where it started in
the room, and does not turn with the head.

Since 2026-10-04 the mod mixes each sound with a place itself, for two ears
and with its height (`audio.md`, sections 14 to 19). A sound on the land at
the waist comes from below. The bed of the atmosphere stays fixed in the
world when the view or the head turns. A zoom step changes how far a sound
carries: far out, small sounds go away and loud events stay. A turn step, a
zoom step and a teleport have a soft sound of their own.

The head also decides which positional sounds start and stop
(`audio_range=1`, `audio.md`, section 12). The game made these tests with its
camera, which is 400 units from the player's place. So a loop near the player
stopped, and a turn or a teleport brought it back. Now a sound plays while it
is in range of the head, at each zoom, and a loop out of range stays silent
and ready.

Before this, the ears went straight down from the head to at most 400 units
over the land under it (`TabletopEars`), because the game's world distances
made every sound silent farther from the land. That rule still applies with
`audio_room=0`.

The spirits float beside the player in the room, as on the table
(`tabletop.md`, 10.9). Their size and depth are `s / 7.5` times the game's, so
they float 1 to 1.5 m away at every zoom. A step of the view moves `P0` in the
world, so the spirits stay where they are in the room.

## 10. Buttons and keys

| Input | In the God view |
| --- | --- |
| Right stick forward or back | Zoom in or out one step |
| Right stick left or right | Turn one step about the head |
| Left stick | No job |
| Left grip | Grab the land: pull to move it across, and up or down |
| Y (left controller), or the left menu button | Esc: open the game menu (see below) |
| Both grips | Turn and scale the land |
| Right grip, trigger, B | As before: pick up, click, draw a gesture. A trigger drag on the land moves nothing |
| A | Hold to aim an arc and a ring. Let go to go there. B while A is down cancels (section 14) |
| Mouse: double click | Go to the land under the mouse (section 14) |
| Mouse: left drag on the land | Pan: the land under the mouse stays under it (section 5) |
| Mouse wheel | One zoom step a click |
| Tilt keys | Ground up or down 5 cm, one step for each press |
| Turn keys, middle mouse | One turn step |
| Ctrl F11 | The God view off and on, as a test. Off is the old immersive view |
| F11 | Recenter: the waist point goes under your head again |

For a left handed player (`controller_left_handed=1`) the hands change sides:
the pointing hand's stick zooms and turns, and the other hand's grip moves the
land, across and up and down. The right hand then has the Esc button, B, and
the draw button of the left hand is Y.

**The menu button.** Y on the left controller sends the game's Esc key, and so
does the left menu button when the runtime gives it to the game. The
pump sends WM_KEYDOWN and WM_KEYUP to the game window (scan code 1), one press
for one Esc. The game opens its menu. The menu does not close with Esc again:
`MainMenu::CanESCOut` returns false, so the player closes it with "Continue
Game". For a left handed player the pointing hand is the left one and its Y
draws gestures, so the second face button of the right hand (B) is Esc. It is
the free hand's button, so it never clashes with the draw button or with the B
of the teleport.

**The menu ray.** While a dialog box is up (`SetupBox::GetCurrentActiveBox`
`0x00407ED0`, which reads `0x00C4CD20`), the direct hand steps aside. The
pointing hand points like a mouse: its aim ray picks the render pixel (the
pointer mode of `controller_hand=0`), and the trigger clicks. A thin ray (2 mm,
warm white) leaves the aim pose and shows where it points. It ends on the
cursor in both eyes (see "The depth of the menu" in `controllers.md`). When the box
closes, the direct hand comes back on the same frame.

The ini keys, all in `[bwvr]`. Only the play mode is in the launcher ("God
view", the first choice and the default). The keys of the teleport are in
section 14.6.

| Key | Default | Meaning |
| --- | --- | --- |
| `play_mode` | 4 | 4 is the God view. 0, the old immersive mode, gives the God view too. 1 is the stereoscopic window, and 3 is the table |
| `god_ground_down_cm` | 60 | The ground below the head at the last recenter, 10 to 200 |
| `god_units_per_meter` | 100 | The scale at the first start |
| `god_zoom_step_pct` | 50 | One zoom step changes the scale by this percent, 5 to 300 |
| `god_ground_step_cm` | 5 | One ground step, 1 to 50 |
| `god_far_units_per_meter` | 4000 | The farthest zoom, 10 to 8000 |
| `god_spawn` | 1 | 1: the player goes where the game puts its own camera (section 15). 0: the view as before, built at the start, a load and the end of a story only, with the focus under the recenter pose |
| `god_spawn_ahead_cm` | 50 | How far the land point that the game camera looks at is in front of the head, 0 to 150 |
| `script_distance` | 1 | 1: a script that measures the distance from the camera gets the distance from the player (section 16). 0: the game's camera |
| `script_view` | 1 | 1: a script that asks if the camera sees a thing gets the view of the head (section 16). 0: the game's camera |
| `cutscene_window` | 1 | 1 plays a cut scene on a screen in the room (`cutscene-window.md`). 0 plays it in the full view with held shots |
| `depth24` | 0 | 1 asks for a 24 bit Z buffer (section 8) |
| `near_plane_cm` | 5 | The near plane at most this far in the room, with a 24 bit Z buffer. 0 keeps the game's |
| `near_plane_cm_16` | 10 | The same, with a 16 bit Z buffer |

The test words of the fake controller, with `fake_controller=2`: `x` and `y`,
the face buttons of the left hand, `lgrip` and `lpoint`, the grip and the
points of the left hand (`fake_controller_left_points`). The words `lstick_up`
and `lstick_down` no longer exist.

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
| `move_right`, `move_left`, `move_up`, `move_down` | The pointer moves `fake_mouse_step_px` render pixels (8) on each frame, as a move of the real mouse |
| `wheel_up`, `wheel_down` | One click of the wheel, on the first frame |
| `tilt_up`, `tilt_down` | The tilt key, a tilt of 1 or -1 at `0x00C5B0C4`, for the frames |
| `turn_left`, `turn_right` | The turn key, a turn of 1 or -1 at `0x00C5B0C8`, for the frames |
| `god` | Ctrl F11, on the first frame |

The launcher removes these keys from `bwvr.ini` when it saves.

## 11. The code

| File | What it holds |
| --- | --- |
| `src/mod/table_math.h` | `GodRoom`, `PoseWorld`, `PoseRoom`, `GroundInRoom`, `ScaleAbout`, `GodZoom`, `GodRaise`, `GodTurn`, `GameCameraOver`, `RayLevel`, `RayLand`, `GodSpawn`, `CameraLookPoint` and `HeadYaw`, as pure functions |
| `src/mod/tabletop.cpp`, `tabletop.h` | The God view in the table state: the place at a game camera (`PlaceGod`, `ApplyGodSpawn`), the steps (`ApplyGodSteps`), the mouse pan (`MousePan`), the camera switches and events, the hook on `GGame::ClearMap`, the values that follow the scale, the Hand's size, the ears and the game camera it wants |
| `src/mod/near_plane.cpp` | The near plane hook, for the God view, the table and play mode 0 |
| `src/mod/teleport.cpp`, `teleport.h` | The teleport: the states, the arc and the hit on the CPU, the refusals, the mouse double click, the log (section 14) |
| `src/mod/comfort_camera.cpp` | The game camera write (`GodAfter`), the watch of the game's own camera moves (`WatchBefore`, `WatchAfterGame`, `WatchEnd`), the turn, the wheel, the tilt keys (`GodTiltKeys`), the game camera after a land drag (`GodDragBefore`, `GodDragAfter`) and the log of the camera events (`h_Help`) |
| `src/mod/pointer_map.cpp` | The mouse ray in the room (`PointerMapMouseRay`) and the test mouse |
| `src/mod/world_grab.cpp` | The camera switches for a turn and a zoom of the grab, and the vertical grab with its limits (`TableSolve`, `TabletopGodLimitHeight`) |
| `src/mod/controllers.cpp` | The sticks, the menu button (`PumpEsc`), the pointer mode while a dialog box is up (`DirectHandNow`, `ControllersMenuRay`) and the fake left hand |
| `src/mod/script_camera.cpp` | `ScriptCameraStoryOwns`, and `ScriptCameraOwnerNow` for the scripts |
| `src/mod/hand_direct.cpp`, `near_math.h` | The player that a script sees: the hooks on the four script words, the latch of the player and the test of section 16.4 (`ScriptPlayerNow`, `PlayerDistance`, `PlayerIsCamera`, `HeadSees`) |
| `src/mod/cutscene_window.cpp`, `cutscene_window_math.h` | The cut scene window: the signal, the state of each frame, the screen and the walls (`cutscene-window.md`) |
| `src/mod/mod_main.cpp`, `src/launcher/launcher.cpp` | Play mode 4, the default, play mode 0 as the God view, and the keys |

## 12. Tests

**The math test** had 460 checks on 2026-09-30, and all passed. The 104 new
ones of that day run at 7.5, 100 and 2000 units per meter:

- The recenter pose is 60 cm of the room over the waist point.
- The Hand is 19 cm long at every scale.
- A real hand 60 cm under the recenter pose is at the waist height, and a head
  that leans and turns with a still hand moves it by nothing.
- A zoom step in and out keeps the land under the head at its room point and
  at its height in the room, keeps the heading, and does nothing at a limit.
- A ground step raises every land point 5 cm in the room and not across, and a
  step down puts it back.
- A snap turn keeps the head's world point, and a turn of 90 degrees right
  puts the land that was on the right in front.
- A pan keeps the grabbed land in the hand, moves the land as far as the hand,
  and keeps the waist height. Without the vertical grab, a hand that moves up
  moves no land.
- Two hands that spread to twice the distance halve the scale, down to life
  size, and keep the land under their middle.
- A hill over the head comes under it after 14 zoom steps out.
- The game camera is 400 units from its ground point, 50 degrees over it,
  along the heading.

**The vertical grab** (2026-10-02, 36 checks of `GodGroundHeight` and
`TableGrabSolve` with `vertical`): a hand 20 cm up raises the land 20 cm in the
room and moves no land across, a hand 20 cm down lowers it 20 cm, a still hand
moves nothing, a move across and up keeps the same move across, two hands keep
the height of the press, and the mouse pan (a hand on the level plane) gets the
same height as before. For the limits: a request inside the range is used, a
request past 10 cm or 2.5 m stops there, a sweep through both limits never
leaves the range and never moves more than the request, a land outside the
range only moves toward it, and a request that comes back into the range is
used at once.

The 37 checks of the mouse pan (`GodPanChecks`, 2026-10-02) also run at 7.5,
100 and 2000 units per meter. With them the test had 684 checks on
2026-10-02, and all passed:

- The ray of the press finds the land at the height of the waist point in
  the room, on the ray.
- After the pointer turns, the grabbed land is on the new ray, within
  0.0003 cm, at the point where the ray meets the level plane. The scale, the
  heading and the height of the waist point stay.
- A still mouse moves no land.
- A hill in front of flat land is found first, and the point is on the hill.
- A ray that goes up, a ray that meets the plane past the Hand's reach, and a
  level ray find nothing.

**Desktop runs** used the rig of `backend.md` with `play_mode=4`,
`fake_controller=2`, `controller_hand=1`, `-NEWGAME` on Land 1, a level fake
head and the player's 2880 by 2880 render. The start dialog needs a click on
"Skip straight to the Creature select" at render pixel 1301, 1420 and on OK at
1290, 1555: the fake hand points `-14,2,100` and `-15,-11,100`. The two eye
pictures show the dialog about 48 pixels apart, because of the interface
depth, and the game's own place is the middle.

**Since the menu ray (2026-10-02)** the start dialog is a dialog box, so the
fake hand points like a mouse there. The hand points `-14,2,100` and
`-15,-11,100` still set the ray origin. The ray must also hit the buttons, so
the run sets `fake_controller_yaw=8`, `fake_controller_pitch=1`,
`fake_controller_sweep_yaw=8` and `fake_controller_sweep_pitch=-6`. The script
is `point@400+1 trigger@440+8 pose2@500+1 point@520+1 trigger@560+8`. The aim
gives the pixels 1298.6, 1420.7 and 1296.2, 1547.9, and the box closes at
frame 571.

| Run | What it showed |
| --- | --- |
| gv1 | The test ini keys went into the last section of `bwvr.ini`, `[game_before]`, so the run was the player's own settings. The test script now puts them under `[bwvr]` |
| gv2 | The player's stereo intro counted frames, so the script came early. From 100 units per meter: zoom steps to 66.7, 44.4, 29.6, down to 7.5, then refused at the limit, and out to 4000. Ground steps: the land under the head went from 0.60 to 0.55 and 0.50 m, and back. At 29.6 units per meter the map objects stopped at 2765 units and the fog was off. The near plane went down at the small scales. Both scene walks took 2.8 ms while the view zoomed from 7.5 to 128 toward the sea, 7.7 ms at 128 after a turn toward the island, and 10.0 ms at 3284 |
| gv3 to gv6 | The click on the check box missed by about 28 pixels, so the game started normally, and its first story held the camera to the end of each run. Two changes came from these runs: the game camera is written only when the player moves the view, and the God view stands aside for a story |
| gv7 | The Creature select. The God view stood aside from frame 608 to 2721 and came back at the pen at 100 units per meter, with the land 0.60 m under the head. A snap turn took the heading from 54.8 to 99.8 degrees, and the game camera was written once. The left grip: a hand step of 10 cm right and 10 cm forward moved the land 14.142 units, and the land point of the anchor was 0.0000 units from the land under the hand. A step of 20 cm up moved no land. With the hand still the view moved on 0 of 210 frames, and after the release it stayed. Two zoom steps out (100, 150, 225) and a ground step down (0.60 to 0.65 m) worked after the grab. Both scene walks took 3.7 to 3.9 ms at the pen |
| gv8 (run menu6) | The menu button and the menu ray. The start dialog is a dialog box (log: "a dialog box is up now, frame 0"), the fake hand points like a mouse, and the two clicks close it ("a dialog box is closed now, frame 571"). The direct hand comes back at frame 572. At frame 3001 the fake Y sends Esc ("the menu button sends Esc"), the box opens at frame 3002, and the game places the Hand while it is up. The picture of frame 3063 shows the game menu, with the pointer on "Options" at the pixel of the aim (1292, 1549). A second Y at frame 3301 sends one more Esc and the menu stays, as `CanESCOut` says. The log shows "the ray of the menu is drawn, 12 triangles" |
| gv9 (run grab2) | The vertical grab, from the left grip at 100 units per meter. A hand step of 20 cm up: "the ground went 20.00 cm up in the room". A step of 70 cm up: "the ground stops at its limit", and the ground went 50.00 cm up (the land 10 cm under the head). A step back to 20 cm: "the ground moves again" and it went 20.00 cm up. With the hands still the camera moved on 0 of 264 frames |
| gv10 (run grab3) | The far limit, with `god_ground_down_cm=200`. A step of 30 cm down: the ground went 30.00 cm down. A step of 70 cm down: "the ground stops at its limit", and it went 50.00 cm down (the land 2.5 m under the head). A step back to 30 cm: the ground went 30.00 cm down again, with no jump. With the hands still the camera moved on 0 of 206 frames |

The pictures of gv7 show the Hand the size of a real hand over the hills at
the waist at 100 units per meter, and the coast as a model under it at 225.

## 13. Not built, not tested, and what needs the headset

- **Not tested on the desktop:** a pick up, a set down and a throw in the God
  view. In gv7 the hand mode went to 9 (pick up) over an object at 150 units
  per meter, but no grip was pressed. The throw on the table did not let go
  when it ended outside the player's influence, and the God view uses the
  same path. The fix is in `tabletop.md`, 10.11. The two hand
  zoom by the grips has only the math test. No advisor appeared, so the
  spirits and the sound are not seen.
- **The mouse** pans, zooms, turns and moves the ground. Its Hand still has
  the game's own size, which grows with the distance, and its throw has the
  speeds of the life size scale. These are items 2 and 10 of Part 3 of
  `design-creature-teleport.md`, in `hand_direct.cpp`. Another change builds
  them.
- **The mouse pan on the desktop.** The runs of 2026-10-02 were refused,
  because the PC was in use. The run to do is in the next item.
- **A jump.** Built on 2026-10-02: A and a double click of the mouse teleport
  the view (section 14). The game's own jump stays for `teleport=0` and for
  the other views.
- **The tutorial.** The event numbers and the bits come from the binary
  (section 7). A desktop run must confirm them in play: the log of `h_Help`
  must show 0x304 for a wheel click and 0x308 for a land drag with the God
  view off (Ctrl F11), and the same events from the God view. A tutorial step
  that waits for a tilt (0x303) cannot end in the God view, because the view
  does not tilt. The snap turns of the old immersive mode had the same limit,
  because the middle mouse does not tilt there either.
- **A load** on the same land is not tested. Save, pan far away, and load:
  the view must come back at the place of the save, with the same scale.
- **The tilt keys.** Which key of the game sets a tilt of more than 0 is not
  checked. If the keys move the ground the wrong way, change the sign in
  `GodTiltKeys`.
- **The run to do** (`run_gm.ps1` in the scratchpad of 2026-10-02): the rig of
  `backend.md`, `play_mode=0` (it must log the God view), `-NEWGAME` on Land
  1, and the test mouse with `fake_mouse_step_px=4`, home at 45 and 49
  percent, the second home at 50 and 70 percent. The words click the start
  dialog (`home@380+1 move_right@382+1 move_down@383+2 button@440+4
  move_left@450+3 move_down@455+34 button@600+4`), then after the first story
  drag the land (`home2@3000+1 button@3010+150 move_right@3030+50`), zoom,
  move the ground, turn (`wheel_up@3300+1 tilt_up@3400+5 tilt_down@3500+5
  turn_left@3600+5`), and do the same with the God view off (`god@3700+1
  wheel_up@3800+1 home2@3900+1 button@3910+100 move_right@3930+15
  turn_left@4100+5 god@4200+1`). The log must show the pan ("the mouse takes
  the land", "stayed within ... cm of the mouse ray"), no move of the view
  after the release, the game camera put back after the drag, and the events.
- **Needs the headset:** the ground height of 60 cm, the zoom step of 1.5
  times, the start scale of 100, the Hand at the life size scale near the
  eyes with the near plane of 5 cm, the look of the land and the clouds at
  each scale, the frame rate at 90 Hz, the sound, the spirits, whether a
  story that stands aside feels right, and the mouse pan with a real mouse:
  the land must stay under the Hand, and it must not move after the
  release.

## 14. The teleport

Buck's headset test of 2026-10-02: "Teleport, we need to render a teleport
path/target and teleport on button release so we can actually see where we
will teleport." The A button now works this way. The code is in
`teleport.cpp`. The design is in `design-creature-teleport.md`, part 2.

**What the player does.** Hold A. An arc leaves the pointing hand and falls on
the land. A ring on the land shows the target. Let go of A to go there. The
arc and the ring are cyan when the target is valid. They are red when it is
not. B, while A is down, cancels the teleport and starts no gesture.

**What A does in each play mode.**

| View | Arc | The move on release |
| --- | --- | --- |
| God view (play mode 4) | A parabola | The head goes over the target. The land under the head keeps its height in the room. The scale and the heading stay |
| Table (play mode 3) | A straight ray | The target goes to the table center |
| Immersive view (play mode 0, Ctrl F11 off, with no story) | A parabola | The eye goes over the target at the same height over the land. The heading stays |
| The mouse, in the God view | None | A double click goes to the land under the mouse |

Each move is one cut with a short black blink. Nothing moves after it. The
teleport never glides.

### 14.1 The press

A press of A is refused, with one buzz of 30 ms and no arc, when:

- a grip holds the world,
- a gesture is being drawn,
- a story or a follow camera owns the camera,
- a dialog box is up (`SetupBox::GetCurrentActiveBox` `0x00407ED0` reads
  `0x00C4CD20`),
- the camera input is off (`mode+0x2EC`), or there is a fight (`mode+0x44`),
- the game's jump switch is off: bit `0x10` of `CameraHelp::EnabledFeatures`
  (section 7). The tutorial turns it on at its own step,
- the hand cannot act (a menu, or the game is not in front), or the aim pose
  is not tracked.

The log gives the reason. A reason that starts while the player aims cancels
the aim, with two ticks.

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

The land under the head keeps its height in the room, as the zoom does. If the
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
the game would allow its own jump (the same switches as the press of A). The
mouse ray from the head finds the land (`RayLand`). The bit is cleared, so the
game does not fly its camera. Then the God view goes there as for A. The mouse
teleport works with `teleport=0` too, because the game's jump moves only a
camera that the God view does not show.

### 14.5 The drawing

`overlay.cpp` has a second source for the arc, with its own switch, so the arc
draws with `overlay=0` too. A tube of 6 sides and 6 mm of the room across, with
no cap, 576 triangles at most. A ring of 24 points that follow the land, 2 cm
over it, from 8 to 11 cm of the room (`teleport_ring_cm` sets the outer
radius, and the inner radius is 0.75 of it). A valid ring has a disc in the
middle. A red ring has a cross. The
meshes are opaque, with the depth test and the depth write on, as the hands.
The arc keeps its shape at every scale, because the tube, the ring and the lift
are in meters of the room. The overlay clips them at the near plane of 5 cm
(section 8). `kMaxTriangles` has 648 more triangles for this.

**The smooth aim** (2026-10-03). A hand in the air shakes a little, and the
shake grows with the length of the arc. So `TeleportFrame` filters the aim
before it finds the arc. `aim_smooth.h` has a one euro filter. It has a low
cutoff when the aim is still, so the ring does not shake. The cutoff grows
with the speed of the aim, so a real move shows at once. The aim direction
(a unit vector, in radians a second) has a cutoff of 0.9 Hz plus 6 Hz for each
radian a second. The aim point (meters of the room) has 1.5 Hz plus 8 Hz for
each meter a second. The filter starts again at each press of A. The release
goes to the target of the last drawn frame, so it goes to the smooth target.
In the math test, 0.76 degrees of tremor leave 0.12 degrees. A step of 20
degrees is within 0.4 degrees after 5 frames, and a sweep of 60 degrees a
second is 0.7 degrees late. The report line of the teleport shows the largest
gap between the raw aim and the smooth aim.

### 14.6 The keys

All in `[bwvr]`. They are ini only, as the other God view keys. The launcher
has no row for them.

| Key | Default | Meaning |
| --- | --- | --- |
| `teleport` | 1 | 1 is the arc. 0 gives the old A jump |
| `teleport_reach_m` | 10 | The flat range of a 45 degree aim, 2 to 100 |
| `teleport_ring_cm` | 11 | The outer radius of the ring, 5 to 60. It was 20 until 2026-10-03 |

The test mouse has a new word, `double`: down, up, double click and up, one
step on each of 4 frames, through `LHMouse::SetButtons`.

### 14.7 The log

The tag is `teleport`. The lines are:

```
teleport     : on. Hold A to aim an arc with a reach of 10 m, ...
teleport     : frame N, God view, aim from x y z m in the room, 28.0 degrees up, 31 arc points, 100.0 units a meter
teleport     : hit land at x y z, the target is land. Valid (cyan), 56 land lookups this frame
teleport     : no hit: the aim is the sky, so there is no arc
teleport     : A is refused: a story owns the camera, frame N
teleport     : A is up, so go to x y z (God view), frame N. The eye is at ...
teleport     : the God view moved. The head went from ... to ... off. The land under the head was 0.600 m below it, and now it is 0.600 m. Scale 100.00 to 100.00, heading ..., version N.
teleport     : A is up on a red target: the target is the sea, so nothing happens, frame N
teleport     : the aim is cancelled: B was pressed, and it starts no gesture, frame N
teleport     : the mouse double click goes to x y z, the target is land
teleport     : N aims over N frames (N on a valid target, ...). N releases went, ...
teleport     : N land lookups, at most N in one frame. The arc was drawn in N eye passes with N triangles.
```

The aim line goes out on the first frame, on a change of the hit or of the
reason, and every 90 frames, 90 lines at most.

### 14.8 Tests

The math test has 82 new checks (`TeleportChecks`), 766 in all, and all pass:

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
- `BuildTube`: 24, 12 and 576 triangles, every corner at the radius from the
  line (a straight tube within 1e-6 m, the arc within 2 percent), and unit
  normals. `BuildLandRing`: 72 triangles (valid) or 52 (red), every point at the
  land height plus the lift, the band from the inner to the outer radius.

### 14.9 The run to do on the desktop

The rig of `backend.md`, with `play_mode=4`, `fake_controller=2`,
`controller_hand=1`, `-NEWGAME` on Land 1, and `fake_head_pitch=0`. The start
dialog needs the fake hand: points `-14,2,100` then `-15,-11,100`, with
`point@400+1 trigger@440+8 point@520+1 trigger@560+8`. The Creature select
story then holds the view to about frame 2721. If camera switch `0x10` is off
at the pen, the press of A is refused and the log says so. Then use a game
start that turns it on.

1. A at the pen: `a@3000+10`. With the switch off, the log shows "A is
   refused: the game's jump switch is off (camera switch 0x10)".
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
8. `teleport=0`: A sends the game's jump again.

### 14.10 Not tested

- Nothing of the above has run in the game yet. Only the math has.
- The look of the arc and the ring in the headset: the width of 6 mm, the
  colors, the ring size, and whether the arc is easy to aim at 4000 units a
  meter.
- Whether the aim pose (the point of the controller) feels right as the start
  of the arc, or whether the arc must start at the index finger.
- The immersive move and the table ray have no desktop run yet. Ctrl F11 gives
  the immersive view.
- The mouse double click on a villager: the game would jump to the object, and
  the God view goes to the land under the mouse ray.

## 15. The place at a camera of the game

Buck's words of 2026-10-03: "Test recipe for creatures should work if you
start a new game and select use existing creature [...] the game normally
places the camera at the door of the temple with creature right in front of
you, at least when not in VR. For some reason we spawn at the edge of the
island in VR which might be a bug as I noticed I don't get spawned to the
right location during quests either."

Since 2026-10-04 the player goes where the game puts its own camera. The
code is in `comfort_camera.cpp` (the watch), `tabletop.cpp` (`PlaceGod`) and
`table_math.h` (`GodSpawn`, `CameraLookPoint`, `HeadYaw`).

### 15.1 The cause

The view did go to the focus of the game camera, and not to the camera
itself [V, `BuildGod` of the builds before]. The fault was that the view was
built one time only.

- **The start.** The view was built at the first main camera call of a new
  game. The start dialog is up then, and the game camera is at the shore of
  Land 1: at 1486 27 1974, with its focus at 1443 1 2079. After the row
  "Keep your old Creature" the game plays no story. 55 frames after OK it
  makes a new `CameraModeNew3` object, and that mode brings the camera to the
  door of the Temple in 42 to 79 updates (runs sp4 to sp6). Nothing built the
  view again, so the player stayed at the shore. Buck's log shows the same:
  the new mode at frame 3174, and the waist point at 1414 0 2148 after it.
- **A quest.** A script moves the camera only with a story camera
  (section 15.2), and the view was built again at the end of each story. But
  the focus of the story camera went under the recenter pose. So the place
  that the game showed was under the feet of the player, 60 cm below the
  head, and not in front. A focus that a script put under the land also gave
  a place 22 units from the place that the camera showed (run sp7). Which of
  the two Buck saw is not known.

### 15.2 When the game puts its camera at a place

`[V]` is from the disassembly of this change.

| Case | What the game does | The view before | The view now |
| --- | --- | --- | --- |
| The first frame of a new game | The start camera of the land, with the start dialog over it | Built there | Built there |
| The row "Keep your old Creature" | No story. A new `CameraModeNew3` object, which brings the camera to the Temple: at 1891 33 2521, looks at 1907 30 2517, heading 104 degrees, 17 units away | Stayed at the shore | One cut when the camera is at rest |
| The other rows of the start dialog | A story with the cinema bars | The cut scene window, then built at the focus | The cut scene window, then the place of 15.4 |
| The end of a story | `END_CAMERA_CONTROL` takes `CameraModeScript` off. A script can put the camera back with `RESTORE_CAMERA_DETAILS`, which calls `GCamera::SetPositionAndFocus` `0x004438C0` [V] | Built at the focus | The place of 15.4, or the view of before the story (15.5) |
| A script with no story camera | `SET_CAMERA_POSITION` `0x006EC8F0` and `MOVE_CAMERA_FOCUS` `0x006ECBA0` cast the top mode to `CameraModeScript`, and they do nothing with `CameraModeNew3` on top [V]. So a script needs a story camera to move the camera | The view stood aside | The same |
| `SET_CAMERA_POS_FOC_LENS` `0x006EE3C0`, `RESTORE_CAMERA_DETAILS` `0x006EE390`, `TempleChallenge::StartScript` | They call `SetPositionAndFocus` with no test of the mode [V, the callers] | Stayed | A set between two updates. Not seen in a run |
| The Temple key | `GGame::DoAction` calls `fn_0064A7A0(0)`, which calls `CameraModeNew3::ZoomToCitadel` `0x00457B60` with the flag 1 [V]. The first press pulls the camera back at its place. A second press in 500 ms flies to the Temple, 130 units away and 45 degrees down | Stayed | The first press is not a new place. The flight is a jump: one cut |
| The key for the whole land | `fn_0064A7A0(1)`: `ZoomToCitadel` to a land point, 2200 units away, with the flag 0 [V] | Stayed | A jump: one cut. Not seen in a run |
| A double click jump | The game's flight, events 0x305 and 0x306 | The mouse: the teleport takes the click. A with `teleport=0`: the game camera went, the view stayed | With `teleport=0` the view goes to the place of the jump |
| A load | `GGame::ClearMap` calls `ResetCameraModeNew3` `0x00460B20`, and `GCamera::Load` `0x004433A0` clears the mode stack [V, the calls] | Built again after `ClearMap` | The same, and a camera that the load sets later is a new place. Not tested |
| The Temple rooms | The game draws from a different camera | The view stood aside and came back where it was | The same. A game camera that is at a new place then is a set between two updates. Not tested |
| The Creature key, a bookmark, a fight | Not read in the binary | Not known | A flight is a jump, and a set is a set between two updates |

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
  in when a hill hides the focus. In runs sp4 to sp7 the focus moved 1.0 to
  25.7 units and the position 0 to 188.6 units, in 250 to 1312 ms. That is
  the end of the player's move, and the watch does not count it. A change
  from outside the update, a new mode and a jump are always counted.
- **One move, one cut.** The changes of the game make one move. The move ends
  when the camera is at rest: no change for 250 ms and no flight. A jump that
  landed at once needs a rest of 100 ms only: the game puts the focus on the
  land in its next update, and the look point is read after that. Then the
  view goes there, with the short black blink of a teleport. A camera that
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
  had before, kept between 10 cm and 2.5 m below the head. The land at the
  look point gets it. So the ground steps of the player stay. At the first
  start the look point is at the waist. A hill under the head stays 10 cm
  below the head.
- **Why 50 cm.** With the land 60 cm below the head, a point 50 cm ahead is
  78 cm from the eyes and 50 degrees below eye level. A bent arm reaches it,
  and the player sees it with the head a little down. At the Temple start
  the Creature stands 4.4 units from the look point (run sp11), so it is
  46 cm in front of the head at 100 units per meter.

### 15.5 The view comes back

A script often puts the camera back where it was before its story. In the
God view that camera is the game camera of the view: the camera that
`GodAfter` wrote, as the game brought it to rest. `tabletop.cpp` keeps the
pair of that camera and the view (`g_pair`). When the game camera is at that
place again, and the player did not move the view after it, the view comes
back exactly where it was. The limits are 5 units for the position, and for
the focus 5 percent of the camera distance and 5 units at the least. The log
then says "the view comes back where it was". No run showed this case: the
two stories of the desktop runs end at a new place.

### 15.6 The scale

The scale stays. A game camera far from its focus shows more land, and a
scale from the camera distance can show the same land: `s = distance / 1.2 m`
gives 14 units per meter at the Temple start (17 units) and 108 at the jump
to the Temple (130 units). That is not built, for three reasons. The scale
is the player's own choice, as the ground height is. A change of the scale
is a second change that the player did not make, at each jump of the game.
And the distance of a story camera is the choice of a shot, not of a view:
the start at the Temple would be 7 times larger than the view of before.
The player zooms with the stick after the cut.

### 15.7 The keys

| Key | Default | Meaning |
| --- | --- | --- |
| `god_spawn` | 1 | 1 is this section. 0 is the view as before: built at the start, a load and the end of a story only, with the focus under the recenter pose. The log still names each new place of the game camera |
| `god_spawn_ahead_cm` | 50 | The look point, in front of the head, 0 to 150 |

With `test_keys=1`, Ctrl and Numpad 0 change `god_spawn` between 1 and 0.
The view then goes to the game camera of that moment by the rule that it has.
So the two places can be compared at one camera. Numpad 0 with no Ctrl is
still the test key of the cut scene window.

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
comfort      : the game moved its own camera (a new camera mode of the player). It was at 1486.3 26.7 1973.7 and looked at 1442.9 0.8 2078.5. Now it is at rest at 1891.0 32.5 2520.7 and looks at 1907.3 29.6 2516.7, update 690: the land point that it looks at went 638.5 units, from 1442.9 2078.5 to 1907.3 2516.7, and the camera turned 126.5 degrees. The move took 750 ms and 68 updates. Changes: 44 in the update, 0 between updates, 0 jumps, 1 new modes. Camera events 0x000, the camera input was the player's. This is a new place
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

**The math test** has 82 new checks (`spawn_checks.h`), 1983 in all, and all
pass:

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
  pose with 0 cm ahead gives the rule of before.

**Desktop runs** (2026-10-04, 01:50 to 02:40) used `spawn_run.ps1` in the
scratchpad: a new game on Land 1 with a choice of the start dialog, the fake
controller with `fake_controller_front=1`, and a render of 2560 by 1440. The
fake head looks 30 degrees left and 20 degrees down from frame 600 (the word
`look`), so the captures show the land in front.

| Run | What it showed |
| --- | --- |
| sp3 | The view of before, with "Keep your old Creature". The dialog closes at frame 571, a new `CameraModeNew3` comes at frame 626, and the waist point stays at 1414.3 0.0 2147.7: the fault |
| sp4 | The first watch. The first view is at the start camera (the destinations: 1486 27 1974, focus 1443 1 2079). After the new mode the camera is at rest at 1891 33 2521 and looks at 1907 30 2517, and the view goes there in one cut: the waist point 1858.8 29.5 2528.7, heading 134.0 degrees (the camera's 104.0 less the head's -30.0). The capture of frame 903 shows the door of the Temple in front, between its two torches, with the Creature at the door. A teleport, a snap turn and a zoom step then moved the focus 1.3 to 3.3 units: not a new place |
| sp5 | The test key. `god_spawn=0` at update 900: the waist point goes to 1907.3 31.1 2516.7, heading 104.0, and the Temple is not in the view (the capture of frame 1003). `god_spawn=1` at update 1200: back to 1858.8 31.1 2528.7, heading 134.0. The Temple key with the flag 1: the camera pulls back 41.7 units and the focus moves 2.6 units |
| sp6 | `ZoomToCitadel` to 2238 3156 with the flag 0. The log has the event 0x307 and "a far jump landed at once". The camera is at 2148.8 111.0 3178.2 and looks at 2240.2 16.8 3155.4, 133.2 units away. The view goes there: "the land at 2240 16 3155 is 50 cm in front of your head" |
| sp7 | The fault that the look point and the rest after a write correct. After the story to the pen the game moved the focus 22.5 units along the camera line in 82 updates, and the view jumped a second time, 1.3 s after the story. After a teleport on a hill the game pulled its camera in 188.6 units and moved the focus 25.7 units, and the view jumped |
| sp8 | The story to the pen with the corrected watch. One place at frame 4198, the first full view frame: the game camera at 2224.9 20.2 3146.3 with its focus at 2252.7 9.6 3165.0 (under the land), and the look point at 2236 16 3154. The walls open at frame 4238. No second place: "in 45 updates the game brought its camera to rest after a write". Two teleports and a snap turn after it: 0 new places, 74 updates of that kind |
| sp9 | "Keep your old Creature" again. The view goes to the Temple 750 ms after the new mode (the waist point 1858.8 29.5 2528.7). `ZoomToCitadel` with the flag 0 at update 1300: the jump lands at once, and the view goes there. The game then moves the focus 2.3 units: not a new place. A teleport after it: no new place |
| sp12 | The story to the pen again. One place at frame 3876, and the walls open at frame 3913. After the story the game has a new `CameraModeNew3` object, and it moves the focus along the camera line for 875 ms. The look point moves 1.0 units: "this is not a new place, so the view stays" |
| sp13 | A fault. The look point of a jump was read in the update of the landing, before the game put the focus on the land. One press of the Temple key (the flag 1) then was a new place 6.9 units away. So a jump now needs a rest of 100 ms |
| sp14 | The build of this change. One press of the Temple key (the flag 1) at update 1300: the jump lands at once, and 125 ms later the look point is 2.6 units from the look point of before: "this is not a new place, so the view stays" |
| sp15 | The build of this change. `ZoomToCitadel` with the flag 0: the camera is at rest at 2148.8 111.0 3178.2 and looks at 2240.2 16.8 3155.4, and the view goes there 125 ms and 9 updates after the jump (the waist point 2191.7 17.7 3167.5) |
| sp16 | The build of commit `8757f73` (this change on the mixer of the same night). The view goes to the Temple. A teleport, then `ZoomToCitadel` with the flag 0 from the camera that the mod wrote: the view goes to 2203.7 17.7 3189.1, and the heading of the view is 164.0 degrees (the camera's 134.0 less the head's -30.0) |
| sp17 | The same build. The left grip holds the land for 400 frames, and the mod writes the game camera on each of them. Then a zoom step in and a zoom step out. 0 new places: "in 400 updates the game brought its camera to rest after a write". The steps of the fake left hand moved no land in this run, so the waist point ends where it started |
| sp11 | The Creature at the Temple start (`seek_plan=creature`). The log has "the Creature [...] at 1904.1 29.5 2519.7". The look point is 1907.3 29.6 2516.7 and the head is over 1858.8 2528.7. So the Creature is 4.4 units from the look point and 46 cm in front of the head |
| sp10 | `god_spawn=0`. The first view is at the camera of the main call, with its focus under the recenter pose: the waist point 1414.9 0.0 2146.2, the place of Buck's log. After the new mode and after the jump the log says "the game camera went to a new place [...] The view stays where it is (god_spawn=0)" |

### 15.10 Not tested, and what needs the headset

- **The view comes back** (15.5) has no run. A story that puts the camera
  back is needed.
- **A load, a fight and the Creature key** have no run. The watch has no
  rule of its own for them.
- **The Temple key, the Temple rooms, a bookmark, the next town and the key
  for the whole land** have runs since 2026-10-04, with the keys of the ring
  (`controllers.md`, 10.7). Two presses of the Temple key in one frame: the
  jump lands at once, and the view goes to the Temple. In the Temple the
  God view stands aside, and after the Temple it is at its place of before.
  A bookmark (Ctrl and Tab, Backspace), the next town (Tab) and the whole
  land (F3) are jumps of the game camera, and the view goes to each one.
  The ring does not send the Creature key in the God view: it makes one
  teleport to the Creature.
- **Ctrl and Numpad 0** has no run with a real key. The test
  `spawn_test_toggle` calls the same function.
- **A move by the arrow keys** counts as a move of the player (event 0x308),
  so the view does not follow it. That was not seen in a run.
- **A grab that moves the land, and the mouse pan,** have no run with the
  watch. Each one writes the game camera, which is a move of the player by
  the rule of 15.3.
- **The turn at a jump.** The game keeps the heading of its camera at a jump
  to the Temple, and the camera that the mod wrote has the heading of the
  room forward. So the jump turns the world by the yaw of the head. In run
  sp16 the head looked 30 degrees to the left, and after the jump it looked
  along a heading 30 degrees more to the right. The place is in front of the
  head in each case.
- **Only the headset can show** whether 50 cm ahead is the right distance,
  whether the height of the look point is right on a slope, whether the cut
  1 s after "Keep your old Creature" is good, and whether a quest now puts
  the player at the right place.

## 16. The player that a script sees

Buck said that quests do not behave right in VR. One cause is here: a
script of the game asks about the camera to learn what the player does, and
in the God view the game's own camera is not the player.

Since 2026-10-04 a script that asks about the camera gets the player. The
code is in `hand_direct.cpp` (`h_ScriptDistance`, `h_ScriptPosView`,
`h_ScriptThingView`, `h_ScriptThingSees`, `LatchPlayer`, `ScriptPlayerNow`)
and `near_math.h` (`PlayerDistance`, `PlayerIsCamera`, `HeadSees`).

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
with the variable `NotifyDistance`. No script writes that variable, and no
run showed one of these scripts speak. [I: its value is 0, so the test is
always false, in the plain game too.]

### 16.2 Why the tests were wrong in the God view

- **The distance.** After the player moves the view, `GodAfter` puts the
  game's own camera 400 units from the place of the player, 50 degrees above
  the land (section 7). That is 257 units on the map. No limit is more than
  250. So each distance test was false for a thing at the place of the
  player, at each zoom.
- **The view.** `TabletopGraphicsDone` gives the engine the game's own camera
  again after each frame. The scripts run in the game turn, between two
  frames. So the view tests used the camera 400 units behind the player,
  along the heading of the room. That camera sees almost all that is near
  the player, also behind the head. It does not turn with the head, and it
  does not change with the zoom. The zoom lesson asks if two stones are in
  the view, so a zoom of the God view did not end that step.

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

**The fault of run hd1.** The build of commit `a373b7b` kept the latch for
300 ms and had no test of the game camera. At frame 601 a script got a
player 585.8 units away while the game's camera was 170.0 units away. The
latch was not old. The player was at the shore, where the view was. The
game had made a new camera mode at frame 600 and its camera was at the
Temple, but the view goes to a new place of the game camera only when that
camera is at rest, 1266 ms later (section 15.3). In that time the view of
the player is not at the game camera, so the script now gets the game's
camera. Run sc-d1 shows it: 100 asks in the frames 601 to 653 got the game's
camera, and each ask after them got the player.

**A story keeps its camera.** A script that keeps "camera position" to put
the camera back after a cut scene is not changed: only the three kinds of
test are. In a story each test gets the camera of the story. With
`cutscene_window=0` and a story camera with no bars, `script_camera.cpp`
holds the shot, and the script gets the held shot, as before.

### 16.5 The tests that ask if the player went away

A test with NOT is true only when the head and each hand are away. So a
hand that stays at a thing keeps the player there.

- With the game's camera each such test was always true in the God view:
  for the game the player was always away. `FollowUs` then says that the
  player lost the mother, and `CreatureGuardian` deletes the guardian while
  the player is beside it.
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
| `script_distance` | 1 | 1: a distance from the camera is the distance from the player. 0: the game's camera, as in the builds before |
| `script_view` | 1 | 1: a view test uses the view of the head. 0: the game's camera, as in the builds before |

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
scripts      : Ctrl and the 7 key: a script that asks about the camera gets the game's camera, as in the builds before, frame N
```

The log has no line for a test. The count in the report line of the God
view is the number of answers from the player in that report window. A
window with a story has no such line.

### 16.8 Tests

**The math test** has 45 checks for this section (2220 in all, 0 failed):
`MapDistance` and `PlayerDistance` (the guide with the head 20 units away
and a hand 5 units away, a thing 300 units away, no point of the player, a
place that is not a number), the tests with NOT and the limits of 1 and 250,
each case of `PlayerIsCamera`, and `HeadSees` (ahead, behind, 49 and 51
degrees to the side, the land at the feet, a head that turned 90 degrees,
110 degrees).

**Desktop runs** (2026-10-04, 16:23 to 16:36) used `spawn_run.ps1` with the
start "Keep your old Creature", a render of 2880 by 2880, the fake head with
the word `look`, and the test word `go` of `seek_plan`, which teleports the
head over a land point. After a teleport the mod puts the game's camera 257
units on the map from the head. The build is commit `3528044`. An advisor
line shows in the log as "audio adv : line N starts, sample S".

| Run | Keys | What it showed |
| --- | --- | --- |
| sc-s1 | distance 1, view 1 | `go` at frame 902 to 1830 2720, 80 units from the place of `PointOutScroll` (1755 2692, limit 100). The advisor comes out at once, and line 2 starts (sample 1043) before frame 971. The report windows count 790, 1851, 1870 and 1853 answers from the player |
| sc-s0 | distance 0, view 0 | The same steps. No advisor and no line after the teleport, to the end of the run (the last line of the log is at frame 2084). The count is 0 in each window |
| sc-x1 | distance 1, view 1 | `go` at frame 902 to 2024.3 2382.3. The gold scroll of the throwing stones (1947.4 2360.2, limit 150 and in the view) is 80 units behind the head. No line for 459 frames. Four turns of 45 degrees at the frames 1301, 1331, 1361 and 1391: nothing after the first two (the scroll 135 and 90 degrees from the head's forward), and the advisors come out at frame 1361, with the scroll 45 degrees from it. Lines 2 and 3 start (samples 1922 and 1923) |
| sc-x0 | distance 1, view 0 | The same steps. The advisors come out and line 2 starts at once after the teleport, with the scroll behind the head: the game's camera sees it |
| sc-v1, sc-v0 | distance 1 | A turn at the Temple brings a hand within 150 units of the gold scroll, and the same two lines start. After `go` to 1830 2720 the line of `PointOutScroll` starts (sample 1043), in the two runs |
| sc-d1 | a test build with counts of the cases of 16.4 | The start of the game. 847 asks to frame 900: 100 got the game's camera because the game moved its camera (frames 601 to 653), and 747 got the player |
| sc-d2 | the same test build | The story to the pen, from frame 601 to frame 2744. 1332 asks in the story: each one got the camera of the story, because the God view did not own the last frame. After the story 44 asks got the game's camera while the game brought its camera to rest (frames 2747 to 2796). Then each ask got the player |

Each log was checked for input that was not from the test: no "the cursor
moved" line, and one "the mouse owns the pointer" line, at the start dialog.

### 16.9 Not changed, not tested, and what needs the headset

- **The first game turn of a story.** No run had a test in the game turn
  that starts a story, after the instruction that starts it. The two cases
  of 16.4 for that turn counted 0 in run sc-d2. They are from the code only.
- **A thing with a large mesh.** The game's test for an object counts the
  ball of its mesh. The mod tests the place of the thing. A house whose
  middle is out of the view, with a wall in the view, is "not in the view"
  now.
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
- **The Temple key, a load and a bookmark** had no run with a script test.
- **Only the headset can show** if the quests of a real session now go on:
  the guide of Land 1, the mother of `FollowUs`, the zoom lesson. It can
  also show if the nearest hand is the right point for a real player, and if
  a script that deletes a thing behind the head is good in VR.
