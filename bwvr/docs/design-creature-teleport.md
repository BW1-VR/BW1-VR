# Design: the Creature by touch, the teleport, and the God view as the main mode

Research of 2026-10-02, after Buck's headset test of that day (see
`roadmap.md`, "Headset test, 2026-10-02"). Nothing here is built yet. The
agents that build each part move the result into the docs of that part.

Marks: [V] verified in the binary with `disasm.py`, [D] read in our docs or
code, [G] a guess.

# Part 1: The Creature without camera moves

Built on 2026-10-02 (items A to D). The research and the design as built moved
to `hand.md`, "The Creature by touch", and to `ui-layer.md`, section 13 (the
Creature panel). Item E, the touch with no press (`creature_touch`), is not
built. It waits for a headset test of A to D.

# Part 2: A teleport with a visible arc

## 2.1 Today

A sends down, up, double click and up over four pump frames [D].
`CameraModeNew3::Update` reads bit `0x10` of the latch `0x00E85350` at
`0x0045DDAE` [V]. It jumps only when `GetCameraFeatures` `0x00456260` has bit
`0x10` [V]: `CameraHelp::EnabledFeatures` `0x009CDD6C`, minus bit `0x10` in a
fight, and the tutorial turns this bit on. The jump goes to the object under
the Hand (`+0x3C8`) or to the land, then calls `CameraHelpCallback(0x306,
point, 0)` [V, G that `0x306` means "jumped"]. In mode 4 the jump moves only
the game camera, which the God view does not show [D].

## 2.2 Behaviour (`teleport=1`)

- **A down:** aim, and draw the arc each frame.
- **A up:** go to the target drawn on the last frame, if it is valid. Pulse
  0.4 for 60 ms.
- **Cancel:** release on an invalid target (two ticks at 0.2), or press B
  while A is down. B is claimed while A is down, so no gesture starts.
- **The A press is refused** (no arc, one 30 ms buzz) when a grip holds the
  world, a story owns the camera, a dialog is up (`SetupBox::GetCurrentActiveBox`
  `0x00407ED0` [V]), the camera input is off (`mode+0x2EC`), there is a fight
  (`mode+0x44`) [D], or feature bit `0x10` is off [V].
- **`teleport=0`:** the old A jump.

## 2.3 The arc

- A parabola in room meters from the aim pose of the pointing hand, put into
  the world with the map of the grip: `PoseWorld` in modes 3 and 4, and `E + L
  x (p - head) x 7.5` in mode 0.
- `p(t) = o + v0 d t + 0.5 (0, -g, 0) t^2`, `v0 = sqrt(g R)`, g = 9.8 m/s^2,
  R = `teleport_reach_m` (10): the flat range at 45 degrees. 48 equal time
  steps, until the arc falls 0.5 m below the room height of sea level, or 3 s.
  An aim more than 60 degrees up is the sky: no arc.
- The reach in the world is R x s, and the shape in the room is the same at
  every scale: 75 units at 7.5, 1000 at 100, the whole map (5120) at 500 and
  more. At 4000 the island is 1.3 m across, so a downward aim lands in arm's
  reach.
- The land hit: on the first segment that crosses `Ground(x, z)`, 8
  bisection steps. `Ground` as in `world_grab.cpp`, off the map is 0 [D]. At
  most 56 `GetAltitude` calls a frame, on the game thread, before the eye
  passes.

## 2.4 What is a valid target

- Valid: land (`Ground > 0`, the land test of `FitLand`) [D], on the map (0 to
  5120), in mode 4 within 3000 units of the map center (`kGodGameRadius`)
  [D], in mode 0 the new camera within 3400 units of the map center [D].
- Red (the arc, and a ring with a cross): the sea, off the map, outside a
  limit.
- Nothing to do: a target within 30 cm (room) of the point under the head.

## 2.5 Going there

**Mode 4.** `TabletopGodTeleport(target)` holds the request, and
`ApplyGodSteps` applies it at the next main camera call as one step:
`ClampMap`, `Derive`, `++g_god_version` (so `GodAfter` puts the game camera
over the new place), `ComfortCameraBlink`, then `CameraHelpCallback(0x306)`.
`table_math::GodTeleport`:

- H = `PoseWorld(P, Tg, head_room)`
- r0 = `GroundInRoom(P, Tg, ground(H))`, kept between head - 2.5 m and head -
  0.10 m
- `G'.x = G.x + T.x - H.x`, `G'.z = G.z + T.z - H.z`
- `G'.y = ground(T) - (r0 - Tg.y) x s`, with s and psi kept

The land under the head keeps its height in the room, the same rule as the
zoom. Keeping `G.y` instead would let a hill come up through the head.

**Mode 0.** `ComfortCameraTeleport(target)`, inside `h_ModeUpdate` after
`RunGameUpdate`, only when `GodAfter` would be allowed: delta = `(T.x - E.x,
ground(T) - ground(E), T.z - E.z)` added to the six Zoomers (`SetZoomer`), the
copies, the last focus and the pivot, heading kept. Then `StartBlink`,
`SpiritAnchorCameraCut` and help `0x306`. The head arrives over T at the same
height above the land, as a cut.

**Mode 3.** A straight ray. On release, `TabletopMove` puts T at the table
center (y 0).

**A mouse double click in mode 4.** In `h_ModeUpdate`, before the game's
update, when the God view and the mouse own the pointer, the latch has `0x10`
and the feature bit is on: clear the latch bit so the game does not fly, send
the mouse ray from the render eye to the land (`pointer_map` `HandRay`), call
`TabletopGodTeleport(hit)` and help `0x306`.

## 2.6 Drawing (`overlay.cpp`)

A second source with its own switch, so the arc draws with `overlay=0`. A tube
of 6 sides, radius 6 mm of the room, no inner caps, about 372 triangles, cyan
when valid and red when invalid. A ring of 24 points that follow the land, 2
cm above it, from 15 to 20 cm of the room, with a center disc when valid and a
cross when invalid. Opaque, depth test and depth write on, as the hand. Raise
`kMaxTriangles` by 600.

## 2.7 Math and checks

- `controller_math.h`: `ArcLaunchSpeed`, `ArcPoints`, `ArcLandHit<Ground>`,
  `ArcIsSky`, `CameraTeleportDelta`. Checks: the first point is o and the
  first segment goes along d. A 45 degree aim over flat ground at the launch
  height lands at R within 1 percent. A level aim from h lands at `v0 x
  sqrt(2h/g)`. The heading of the landing point is the heading of the aim. A
  wall gives a hit at the wall, no crossing gives false. A 70 degree aim is
  the sky.
- `table_math.h`: `GodTeleport` at 7.5, 100 and 2000 units per meter. The new
  head point is over T.xz within 0.01, the land under the head keeps its room
  height within 1 mm, s and psi do not change, T equal to H gives the same
  pose.
- `overlay_math.h`: `BuildTube`, `BuildLandRing`. Triangle counts, every tube
  corner at the radius from the line, ring points at the land height plus the
  lift.

## 2.8 Files

New `teleport.cpp` and `teleport.h` (the states, `TeleportFrame` called in
`stereo.cpp` after `HandDirectFrame`, the checks, the log). `controllers.cpp`
(A and B, `ControllersRoomToWorld`), `overlay.cpp`, `overlay_math.h`,
`controller_math.h`, `table_math.h`, `tabletop.cpp` and `tabletop.h`
(`TabletopGodTeleport`), `comfort_camera.cpp` (the mode 0 move, the double
click in the God view), `mod_main.cpp` and `launcher.cpp` (the keys
`teleport`, `teleport_reach_m`, `teleport_ring_cm`), `test/controller_math`.

## 2.9 Desktop test

Mode 4, the script `a@N+90 pose2@N+30+1` (two aims, then the release).

1. The log shows the aim, the hit, the land height and why it is valid.
2. After the release: the new head point is over T within 0.01, the room
   height of the land under the head is the same within 1 mm, s and psi do
   not change, one blink, the version goes up by 1, and the game camera is
   written once.
3. Captures during the hold show the tube and the ring in both eyes, with a
   disparity of `fx x e / d x hx`.
4. A sweep toward the sea shows red, and the release does nothing.
5. A 70 degree aim draws no arc.
6. B during the hold cancels, and the gesture log stays silent.
7. Before the tutorial gives bit `0x10`, the press is refused.
8. In mode 0 the camera moves by delta and help `0x306` is logged.
9. The double click in mode 4: use `teleport=0`, so A sends the double click.

# Part 3: What the God view still lacks as the only main mode

1. **The mouse land drag moves only the game camera** [D]. Fix: a mouse pan in
   the God view. The land point under the mouse ray at the press stays under
   the ray (`TableGrabSolve` with one "hand"), through `TabletopMove`. Files:
   `pointer_map.cpp`, `tabletop.cpp`, `comfort_camera.cpp`.
2. **The mouse Hand has the game's size** (it grows with distance) [D]. Fix: a
   branch in the depth hook for the mouse in the God view that writes only
   `+0x4834 = TabletopHandSize()`. File: `hand_direct.cpp`.
3. **The jump.** Part 2.
4. **The tutorial.** God view zooms, pans and teleports send no CameraHelp
   events, the zoom ignores feature bit 4, and the jump ignores bit `0x10` [D,
   V]. Fix: gate on those bits and call `CameraHelpCallback` `0x00449140` with
   the game's events. The turn sends `0x300` to `0x302` [D] and the jump
   `0x306` [V]. Find the zoom and drag events with a hook that logs this
   callback. `0x102` and `0x200` are the candidates [G]. Files: `tabletop.cpp`,
   `comfort_camera.cpp`.
5. **A load on the same land keeps the old view** (the land signature does
   not change) [D]. Fix: a post hook on `GGame::ClearMap` `0x00552BB0`
   (thiscall, plain `ret`, called from `LoadAllGame` and
   `StartPlaygroundGame` [V]) that sets `g_have_table = false`, so the view is
   built again at the game camera after the load [G for the timing]. File:
   `tabletop.cpp`.
6. **The God view does not follow script moves inside `CameraModeNew3`** [D].
   Fix: a jump of the game camera destination with no change of version and
   no player input counts as a script cut, and `BuildGod` builds the view
   there.
7. **The Creature makes the view stand aside, and its panel is not shown with
   `ui_panel=1`.** Part 1.
8. **A follow camera and fights.** `CameraModeFollow` makes the view stand
   aside at life size [D]. Fights move the game camera, and `GodAfter` does
   not write during a fight [D]. Decide after a test: stand aside, or one God
   teleport to the subject [G].
9. **The ground height for the mouse.** The left stick is the only input [D].
   Map the game's tilt keys, which `h_ModeUpdate` already reads
   (`0x00C5B0C4`), to `TabletopGodStep(0, +-1)`.
10. **The mouse throw** uses life size speeds, so it is slow in the room.
    Scale it by s/7.5 in the release hook. File: `hand_direct.cpp`.
11. **The start.** Keep the last scale in the ini (today it lasts only for the
    session) [D]. Make play mode 4 the launcher default, and keep mode 0 for
    stories and the stereoscopic window. Files: `mod_main.cpp`,
    `launcher.cpp`.
