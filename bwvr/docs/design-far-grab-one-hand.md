# Design: grab from a distance, and one hand

Research of 2026-10-02 (evening), after Buck's headset test of that evening
(`roadmap.md`, items 2 and 3). Part 1 is not built yet. Part 2 is built, see
its note below. The agents that build each part move the result into the docs
of that part.

Marks: [V] verified in the binary with `disasm.py`, [D] read in our docs or
code, [G] a guess.

# Part 1: Grab from a distance

## What the game allows

- **The pick up has no distance test.** `fn_005D1740` (thiscall on
  GInterface, the object as its argument, `ret 4`, called at `0x005D1536`)
  starts every pick up [V]. It checks `+0x24` bit `0x20`,
  `ValidForPlaceInHand` (+0x6FC), `IsCannotBePickedUp` (+0x180), and the
  influence (`+0x48`, or vtable `+0x714` returns 0). Then it writes the object
  into the next record (`fn_005D5E40` on `+0x3E8`), sets the pick lock (`+0x38
  |= 0x10`), starts Immersion 4, and sets the Tug (`CHand+0x4908=1`) or calls
  `fn_0046DC30(obj,1)`. `PlaceObjectInMagicHand` `0x005DA6F0` (packet `0x13`)
  has no distance test either [V]. So the game's own pick can take a far
  object if the record holds it [V]. No other step of the press path tests
  the distance [G, test first].
- **Hand mode 9** (`fn_005D7F20`) needs `+0x400` holdable and in influence
  (`+0x48`, from the land point `+0x3F0`), or vtable `+0x714` returns 0 [V].
  If the mod writes the land under the far object to `+0x3F0`, the mouse rule
  stays: only what you could click in your influence.
- **The game cannot move a villager for the mod.** A pick makes a `Reaction`
  of type `0x10` (`0x005DA7DD`), so other villagers react [V]. `Object+0x14` is
  `MapCoords Pos` and `+0x20` the map cell link [D, decomp]. A write of only
  the mesh translation (`Object+0x40` to `+0x38`) moves only the picture
  [G]. Moves and physics of the mod send no packets, so they break
  multiplayer [G].
- **The game has no highlight for the object under the Hand.**
  `ScriptHighlight` belongs to scripts, and `HandGlow` is the hand effect [V
  symbols]. The overlay draws the glow.
- **The Tug state.** `HandStateTug::Enter` keeps the object's position in
  `CHand+0x492C` and the camera distances [V]. The mod does not place the Hand
  in state 3 [D], so in a far Tug the Hand can jump out to the object for
  about 10 frames [G].

## Selection

- **The ray:** the aim pose of the pointing hand, in the world, as in
  `teleport.cpp` (`ControllersTeleportInput`) [D].
- **The candidates:** from the draw (`h_SendObjectDrawCollision`, first eye
  pass). In play modes 3 and 4, `h_ScreenTest` already offers objects near
  the Hand. Also offer an object whose mesh point is inside the cone. This
  sets `0x00EA1AD0`, and the mesh test at distance 0 then gives no false game
  pick [D].
- **The filter:** `Holdable()`, the influence at the object
  (`InsideInfluence`) or vtable `+0x714` = 0, and a room distance from the
  Hand point of 0.35 m or more. A near object from `Choose()` always wins.
- **The score:** `te = max(0, t - atan(r/d))`, t the angle off the ray, r =
  Get2DRadius, d the distance. Select at `te <= 8` degrees and keep while `te
  <= 14` degrees. Add `1 degree x log2(1 + d_room)` so the nearer of two equal
  targets wins. Change the target only for one that is 2 degrees better.

## The glow and the hint

- **A rim halo:** the additive disc of `hand_glow.cpp` at the object center,
  radius `1.35 r` plus 1 cm of the room, depth tested at `c + r v` (the far
  side), so the object hides the core and the light shows around its outline
  [G look].
- **On the land:** a gold `BuildLandRing` under the object. After the lock, a
  3 mm tube from the index tip to the object.
- **The hint:** "Grip, pull", in the controller hint work (roadmap item 4).

## How to bring the object

| | (a) the mod flies it | (b) the game picks it at once | (c) a throw toward you, as HL:A |
| --- | --- | --- | --- |
| Game logic | the object stays at its old place | the game's own path: packet 0x13, reactions, tutorial events | needs a new PhysicsObject outside a packet |
| Rules | the mod must copy them | all kept | the influence is lost, a hit can kill a villager [G] |
| Risk | high | low | high: a catch of a flying object is not known [G] |

**Choose (b), with the look of (a) on top.** The game takes the object, and
the mod then draws it gliding to the Hand. In the Holding state the game
writes the held mesh translation to `0x00C5E660 + 0x00D13F20` at
`0x0046CA60` to `0x0046CA97` [V]. A call after `CHand::PrepareForDrawing`
`0x0046C550` writes over it. `pass_guard.cpp` already hooks that function, so
the glide adds its call with `HandExactAddPrepareHook` (`hand_exact.cpp`):

```
p(u) = p0 + (p1 - p0) e(u) + y h 4u(1 - u)
e(u) = 1 - (1 - u)^3
h    = 0.15 |p1 - p0|
T    = clamp(0.30 + 0.05 d_room, 0.30, 0.50) s
```

p1 is the game's value of each frame.

## The gesture and the haptics

States: Idle, Aim (a target), Lock, Pull, Fetch.

- **The grip claim.** In `controllers.cpp`, a grip that closes on a far
  target is held back from `StepGrip`. Without it, the press goes to the land
  or to the grab of the world.
- **The pull.** `p = normalize(head_room - grip_at_lock)`. The pull counts at
  `v_grip . p >= 0.8 m/s` (the mean of 60 ms) and an angle of 55 degrees or
  less between v and p, or a move of 12 cm along p since the lock, or a flick
  with `v_y >= 1.0` and `v . p >= 0.4`.
- **The press.** The mod puts in the target, writes `+0x3F0`, and frees the
  claim with one frame of the button up. `GripStep` then sends the press, and
  the grip carries the object as now [D]. A grip that opens before a pull
  ends the lock.
- **Haptics** (`ControllersGesturePulse`): 0.15 for 10 ms at a new target,
  0.3 for 30 ms at the lock, 0.2 up to 0.6 during the glide. The game's
  effect 5 marks the arrival [D].
- **Multiplayer:** off with `-MULTIPLAYER` (`0x00D46ABA`) [D].

## The throw physics: why it is floaty

- **Gravity.** `fn_007FD4D0` adds `F.y -= m x 9.81` (`0x007FD553`, the float
  `0x008CF02C`) [V]. In game units, at s units per meter, that is `9.81 / s`
  m/s^2 in the room: 1.31 at 7.5, 0.098 at 100.
- **Drag.** Quadratic, `F -= k |v| v` (`0x007FD4E9`). k is `PhysOb+0x14C`,
  column 5 of `PhysicsConstants.txt` (24 rows into `0x00CC63E0`, chosen by
  vtable `+0x788`) [V].
- **The cap of 124 units/s is for every physics object.** `fn_007FE260`
  cuts the speed of every physics object to 124 on each 5 ms substep
  (`0x007FE521`). `PhysicsObject::AddObject` (`0x006446BF`), the release
  (`0x005D4F75`) and the hold spring (`0x005B453F`) cut too [V]. A turn is
  20 substeps of 0.005 s (`0x00644F56`) [V].
- **The throw today.** The mod sends `v = 1.5 s v_room` [D]. That reaches the
  cap at `82.7 / s` m/s, 0.83 m/s at s = 100. So in the God view almost every
  throw is the same: the cap speed of 124, up to 1567 units with no drag, and
  up to about 18 s in the air.

**Step 1, small and safe: the square root law.** Send `v_game = sqrt(s)
v_room`, with the same direction. With the game's g, the landing point is the
landing point of the real throw in the room, times s, also with a release
height:

```
R_g = sqrt(s) v cos(a) (sqrt(s) v sin(a) + sqrt(s v^2 sin^2(a) + 2 g s h)) / g = s R_room
```

The cap comes only at `124 / sqrt(s)` m/s, 12.4 m/s at s = 100. The flight
is sqrt(s) times longer than a real throw, so this step fixes where the
object lands, not how long it flies. Drag shortens the throw: measure it, and
set `PhysicsObject+0x174` to 0 for the thrown object (the array at
`*0x00D47814`, stride `0x1DC`, the PhysOb at `+0x28`) [V stride, G meaning].

**Later: the real flight time.** While the player's throw flies, call
`PhysicsObject::GameTurnUpdate` `0x00644FC0` `w = clamp(round(sqrt(s)), 1, 8)`
times a game turn. Each call is a whole turn, with land tests, hits and
sounds. All physics objects go faster in that time [G]. First check whether
`PhysicsObject::DrawAll` `0x00646DE0` blends between turns. Another way:
give only the thrown PhysOb `g s`, a higher cap (around `fn_007FE260`) and
real speeds. The hit damage then reads the large speeds (the test at
`0x006462E4`) [G], which is riskier.

## Files

- New `far_grab.cpp` and `.h`: the state machine, the cone, the pull, the
  glide, the keys `far_grab`, `far_grab_cone_deg`, `far_grab_pull`, the log.
- `controller_math.h`: `FarConeScore`, `PullDetect`, `GlideArc`,
  `SqrtThrowVelocity` (in place of the gain in `TableThrowVelocity`, under
  `throw_law`).
- `hand_direct.cpp`: the cone offer in `h_ScreenTest`, the far choice in
  `Inject()`, the far land point in `h_InterfaceStep`. The glide goes in
  through `HandExactAddPrepareHook`.
- `controllers.cpp`: the grip claim before `StepGrip`, the fake words
  `aimfar` and `pull`.
- `overlay.cpp` and `hand_glow.cpp`: the ring, the tether, the halo.

## Checks

- The cone: an object on the ray or at its angular radius scores 0. An
  object behind the ray or nearer than the minimum is out. The 2 degree
  hysteresis holds.
- The pull: 0.9 m/s toward the head fires, 0.9 m/s across does not, a head
  step with a still hand does not.
- The glide: `p(0) = p0`, `p(1) = p1`, the peak is h at u = 0.5.
- The square root law: the range is `s R_room` to 1e-4 at s = 7.5, 100 and
  1000. The gentle drop rule does not change.

## Desktop test

The rig of `god-view.md` section 12, `play_mode=4`, `fake_controller=1`,
`controller_hand=1`, `intro_vr=0`. At the pen: `aimfar` (aim at the nearest
holdable object 2 to 6 m away in the room), `grip@+60`, `pull` (15 cm toward
the head over 3 frames). Expect "far target" with the angle and the distance,
the lock, a press with `+0x3C8` = the target, `+0x4904` = the target within
about 12 frames, and the glide line. Log the state, `+0x78` and the object's
mesh translation on each frame of the Tug (this answers the [G] above).
Outside the influence: a red ring and no press. The throw: two still poses
one frame apart at s = 100. Log the speed sent and the landing distance
against `s v^2 / 9.81`.

**First step for one night:** (b) with the cone, the ring and the halo, the
grip claim and the pull. Then the glide, and the square root law behind
`throw_law`.

# Part 2: One hand, the game's own model

Built on 2026-10-02 as option A. The research and the result are in
[hand.md](hand.md), "The exact Hand". Two facts changed from the first
research: the pose builder writes a root on the stack, and the pose arrays hold
the world matrix of each bone. So the mod gives its root to the compose of
the bones (`fn_00839F10`), and it moves the old pose of a blend to the new
root, not only the first matrix.
