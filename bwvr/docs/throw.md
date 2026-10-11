# The throw

How a thing leaves the Hand: the release speed, the flight, the landing of a
thrown object, and the release at once.

The game's own throw (the mouse, the packets, `InitialisePhysicsFromHand`) is
in `hand.md`, section 3, "Pick up, hold, drop, throw". The grip that holds an
object is in `grab.md`.

## Summary

- The throw speed is the top speed of the real hand in the 120 ms before the
  grip opens (`throw_law=2`). The mod does not use a gain or a square root.
- The flight is the real arc of a real throw at the scale of the room. The
  mod runs the game's own physics step more than one time in each substep to
  get it. A time factor k makes the flight slower than real (`throw_slow_x10`).
- From the first contact the rate of the game's steps goes down to 1 step in
  each substep. A low bounce and a brake make the object come to rest.
- An object that lies on the land does not turn in place, and it is at rest
  after 0.5 s with no move. On land that is steeper than the brake holds, the
  object goes down as in the game (`throw_land_rest`).
- A tree that falls comes to rest in about 1 s after its first contact
  (`throw_tree_settle`).
- A still hand near the land sets the object down. A still hand higher up
  than `release_fall_cm` lets the object fall (see "The height of a
  release").
- A release runs a game turn at once, so the object leaves the Hand on the
  frame after the release action (`release_at_once`). A fast fall of the grip
  value opens the grip at once (`grip_fast_open`).
- The math is in `bwvr/src/mod/throw_math.h` (the throw, the flight and the
  landing) and in `bwvr/src/mod/controller_math.h` (`GripLevelStep`). The
  checks are in `bwvr/test/controller_math/throw_checks.h` and
  `bwvr/test/controller_math/grip_checks.h`. The hooks are in
  `bwvr/src/mod/throw_physics.cpp`, `bwvr/src/mod/pass_guard.cpp` and
  `bwvr/src/mod/hand_direct.cpp`.

## The throw physics

### Why the square root law is not enough

The square root law sends `sqrt(s) v` units per second, with s the world
units in a meter of the room. In the room that is `v / sqrt(s)`. At 67 units
per meter the object leaves the hand 8 times slower than the hand moves. The
law gives the correct landing point, but the flight takes sqrt(s) times
longer than a real flight, so each throw looks slow. The game's physics is
also "floaty", because the game made it for a mouse.

The mean of the last 80 ms before the opening is also a poor speed. A swing
to 6 m/s in 150 ms that stops in 200 ms reads 5.76 m/s at an opening 44 ms
after the top speed, and 4.61 m/s at an opening 100 ms late. A short flick
loses more. The mod uses the top speed of the swing.

### What the game's physics does [V]

All of this is from the disassembly of `runblack.exe`.

- **The turn.** `GGame::ProcessTurn` calls `PhysicsObject::GameTurnUpdate`
  `0x00644FC0` once a game turn, 10 times a second (`0x0054E67E`). The call in
  `Process3dEngine` needs the byte at `0x00D46A74`, and no code writes it.
- **The objects.** An array of `0x1DC` bytes an entry at `*0x00D47814`, with
  the count at `0x00D47818`. The Object is at `+0x18`, the thrower's
  `GInterfaceStatus` at `+0x24`, and the PhysOb at `+0x28`. The game takes an
  entry out by a copy of the last entry over it, so an entry can move.
- **The substeps.** A turn is 20 substeps of 5 ms (`0x00646046`, the float at
  `0x008C7674`). A substep runs `fn_007FD200` and `fn_007FD4D0` for each
  object, then `fn_007FDA60` for each pair of objects whose spheres meet, then
  `fn_007FDE40` for each object, then `fn_007FE260` for each object.
- **`fn_007FD200`** puts the points of the object 0.06 s ahead along its
  velocity, and sets the force to the constant force of the object.
- **`fn_007FD4D0`** adds the drag `F -= k |v| v` (k at `PhysOb+0x14C`, the
  sixth value of a row of `PhysicsConstants.txt`, not times the mass) and the
  weight `m x 9.81` (the float at `0x008CF02C`). Then it tests each point
  against the land (`LH3DIsland::GetAltitude`), or floats the object when the
  land is under the sea.
- **`fn_007FDE40`** turns each contact into a spring force and a friction
  force (`+0x13C`, `+0x140`, `+0x144`). It adds 5 ms to the life of the
  object (`+0x170`).
- **`fn_007FE260`** adds the force to the velocity (`+0xDC`), cuts the speed
  to 124 (`0x007FE521`, the float at `0x00C371FC`) and adds the velocity to
  the position (`+0xA0`). It gives 1 for a step, 2 when the object came to
  rest (in contact and slower than 1 unit per second), and 4 when the object
  is 4 radii under the sea. After a 2 the game calls `EndPhysics`, which puts
  the Object back in the map, or deletes it when it is off the map.
- **The near objects.** At the start of a turn the game looks in the map
  cells around each moving object, out to the speed times 0.1 s and the
  radius (`0x0064528A`). It puts each object there that
  `InteractsWithPhysicsObjects` into the array for that turn.
- **The damage.** After each step with a contact the game adds the force to
  `PhysicsObject+0x0C` (`0x00645FEE`). After the 20 substeps `+0x08` is the
  length of that sum times 0.05, the mean force of the turn. Then it calls
  `ReactToPhysicsImpact` for each object. `Living::ReactToPhysicsImpact`
  `0x005ED3E0` takes `G = +0x08 / (m x 9.81)`. Above 2 the damage is
  `(G - 2) x 0.03`. A landing that stops in one turn gives a G near the
  landing speed in units per second, so about 35 units per second kills a
  villager with a life of 1 [I for the life]. `Abode::ReactToPhysicsImpact`
  `0x00406240` reads the speed times the mass of the object that hit it.
- **The draw.** `PhysicsObject::DrawAll` `0x00646DE0` draws each object with
  the mesh matrix that the last substep wrote (`fn_007FD140`, then
  `SetXYZAngles`). It does not blend between turns. So a thrown object moves
  10 times a second.
- **The release.** The release action `0x005D4DB0` sets the int at
  `0x00D47824` and the object at `0x00D47828` when it lets go. The drop
  action of a later turn calls `InitialisePhysicsFromHand` `0x00636F00` and
  `PhysicsObject::AddObject` `0x006443A0`, which cuts the speed to 124 again.

### The speed of a throw

With `throw_law=2` the speed is the top speed of the swing (`PeakVelocity` in
`throw_math.h`). The velocity at each sample of the grip is the mean from 24
ms before it to 24 ms after it (`kPeakHalfSpan`). The throw takes the largest
of them, from 120 ms before the opening of the grip (`kPeakWindow`) to the
release action. The direction comes from the same samples. The result does
not change with the sample rate.

**A set down stays a set down** (`SetDown`). The hand is still at the opening
when the mean of the last 80 ms, across the land, is 0.18 m/s or less. A still
hand sets the object down, unless the top speed of the window was 1 m/s or more
(`kSwingMps`). Then the hand stopped a moment before the opening, as in a
flick, and the object flies with the top speed.

### The real flight

A real throw at v m/s is `s v` units per second in the world, under a gravity
of `s g`. The game has one gravity. But a flight with `sqrt(s) v` under the
game's gravity is the same curve in a time that is sqrt(s) times longer. So
the mod runs the game's own step for the thrown object sqrt(s) times as often
(`throw_physics.cpp`). The object then flies in the room as a real object
flies, with the same arc and the same time, at each scale. Each rule of the
game sees the speeds of its own physics.

1. **The release.** `ThrowPhysicsPlan` works out the velocity. The release
   action sends `sqrt(s) v`, cut to 124, with 2.5 units per second or more
   across the land (`kSendFlat`), so the game throws the object. After the
   action `ThrowPhysicsReleased` reads the mark at `0x00D47824`. With the mark
   the flight starts, from the mesh position of that moment.
2. **The first frames.** The game holds the object in the Hand until the drop
   action of a game turn. In that time the mod draws the object on the real
   arc `p0 + s v t - s g t^2 / 2`, stopped at the land. So the object leaves
   the hand on the frame of the release action.
3. **The state.** A call before `GameTurnUpdate` (`pass_guard.cpp` has the
   hook) finds the entry of the object. On the first turn it moves the PhysOb
   to the place of the arc at that time and writes the velocity `sqrt(s) v`,
   with no cap. The center stays one radius or more over the land.
4. **The steps.** A hook on `fn_007FE260` runs the four parts of a substep
   again for the object: sqrt(s) times in each substep, with the part of a
   step that is left kept for the next substep (`MiniSteps`). At 67 units per
   meter that is 8 or 9 steps. This is the rate of the flight. After the
   first contact the rate goes to the rate of the land: see "The landing of
   a thrown object".
5. **The cap.** For the steps of the object the float at `0x00C371FC` is
   1e9, and 124 after them. At 100 units per meter a throw of 5 m/s is 50
   units per second in the game's time. The cap matters from about 600 units
   per meter.
6. **The drag** is 0 for the object until its first contact, so the arc is
   the real arc. From the first contact the game's drag is back, because the
   water needs it.
7. **Other objects.** In a substep where an object touched the thrown object
   (`fn_007FDA60` wrote `PhysOb+0x78`), the game's one step runs and no more.
   The mod does not change this, because the game finds the contact of two
   objects once a substep, and extra steps would lose the force of the pair.
   So a thrown object that rolls into a pile moves `sqrt(s) / k` times slower
   in those substeps. For the search of the near objects the velocity is that
   of the real flight, up to 300 units per second (`kSearchSpeed`), and a
   hook on `fn_007FD200` puts the velocity back before the first substep
   reads it.
8. **The damage.** A turn has more steps. The hook makes the game's sum for
   each 20 steps in a row and gives the game the largest. A landing hurts as
   it does in the game at the same speed of the game's time, and a slide does
   not hurt more. With the fall from the hand, a throw from 30 cm over the
   land at 67 units per meter lands at 20 units per second or more.
9. **The draw.** The mod keeps the matrix of the object after each substep
   with its time in the picture (`fn_007FD140` gives the matrix). A hook on
   `DrawAll` puts the object where the flight is at the time of the frame
   (`FlightAt`), once a frame for both eyes. See "The picture between the
   steps". On the first turn the mod compares its matrix of the last substep
   with the mesh matrix that the game wrote. When they agree it draws the
   axes of each substep. If not, it moves the object only, and the log gives
   the difference.
10. **The end.** The flight ends when the object comes to rest, falls under
    the sea, leaves the physics (a new pick up), or after 60 s. Up to 4
    objects fly at one time.

**What keeps the game's time.** A Miracle in the Hand gets the velocity of
the square root law. A tree flies as a rock does, unless `throw_trees=0`. The
disassembly shows that a tree is a physics object like a rock:
`Object::InitialisePhysicsFromHand` `0x00636F00` calls
`PhysicsObject::AddObject` for each object. The only differences are the
argument that `PhysOb::AdjustToGroundLevel` `0x007FCB80` gets (`0x00637075`,
`0x006370D5`: 1 for a thing that is not a tree) and that a tree is not a
gentle drop (`IsAnyKindOfTree`). A tree that the Hand sets down on dry land
is planted at once by `Tree::EndPhysics` `0x0074B830` (dust from
`SmokyStuff::Create`, a pulse of the hand and the search for a place). A tree
that falls does not get the flag of that landing (flag 8 of the physics
object, set only at the release), so it never leaves the physics as a `Tree`:
the game gives it the path of a dead tree. A desktop run showed both: a tree
set down at 2.4 cm was a planted `Tree` 25 frames later, and a tree let go at
45 cm stayed in the physics. See [grab.md](grab.md), "A tree in the Hand". That is the game's own code and
it comes at the end, so it does not matter for the flight. [I for the safety:
the hooks do not read the class. A flying tree has not been tested in the
headset.]

A multiplayer game uses the square root law for each throw, because all
players must run the same physics. The mouse player's throw does not pass
through the release hook. Each object that the player did not throw keeps the
game's physics.

**The game's state is ahead.** A turn works out the next 0.1 s of the flight,
and the mod draws it as the time comes. So the Object of the game is up to
0.1 s ahead of the picture, or 0.2 s after the extra turn of a release. The
sound of a landing can come that much before the picture.

### The picture between the steps

A game turn is due each 100 ms, and it gives 20 substeps. The clock of the
picture is the real time from the release. The mod draws the object at the
place that the substeps have for that time, on the line between two of them
(`FlightAt` in `throw_math.h`). Only the place of the picture is in this
part: the physics of the game, and where the object comes to rest, do not
change.

- **The time of a substep.** A substep of the flight runs `sqrt(s) / k`
  steps, and it is 5 ms of the flight. A substep in which another object
  touches the thrown object runs one step (see "Other objects" above), and
  that step is `5 ms / rate` of the flight: 1.5 ms at a rate of 3.33. The
  game runs it in 5 ms of the real time. With `throw_draw=1` such a substep
  is 5 ms of the picture (`PictureSpan`), so each turn gives 100 ms of the
  picture, and the object moves through a touch at the speed of the game's
  own time. With `throw_draw=0` the substep has its time of the flight. A
  turn of 20 such substeps is then 30 ms of the picture: the picture comes
  to the newest step, stands there, and goes on from a new place after the
  next turn.
- **After the newest step.** A turn comes one frame early or late. When the
  clock is past the newest step, the place is a guess: the object goes on
  from that step with its own speed, for 50 ms at most (`kFlightAhead`). A
  free flight falls with the gravity of the picture, and an object in contact
  goes on across the land. After 50 ms the clock of the flight waits.
- **The picture does not go back.** The steps that come after a guess can
  have the object behind the place of the guess, because a contact took a
  part of its speed. With `throw_draw=1` the picture stays at the place of
  the guess until the object comes to it (`GuessHolds`), for 50 ms at most.
  An object that turned in a contact does not come to that place, and the
  picture goes with it at once: a bounce is a real move back.
- **The clock falls back.** After a wait the clock is 50 ms in front of the
  newest step, and each second frame is a guess from then on. With
  `throw_draw=1` the clock runs 20 percent slower until it has given the 50
  ms back (`ClockFallBack`), which takes 0.25 s.

A flight keeps the rule of `throw_draw` that it started with.

### The time factor k

The real flight in a small world looks too fast and heavy. So the flight has
one more factor, k. The object flies the same arc (the landing point is the
real one), but k times slower than real. In the room the speed is `v / k` and
the gravity is `g / k^2`. In the world that is `s v / k` under `s g / k^2`.
The game does not change: its velocity is still `sqrt(s) v`, and its gravity
is still `g`. Only the number of game steps in a substep is `sqrt(s) / k` in
place of `sqrt(s)`. So k = 1 is the real flight, and k = sqrt(s) is the square
root law, with one step in a substep (`SlowFactor`). The game cannot run less
than one step, so k is cut to sqrt(s): at 7.5 units per meter k is 2.74 at
most. The log gives the k in use and the k that was asked for. The arc, the
draw, the first contact and the search of the near objects all use the slowed
flight. A flight keeps its k until it ends, and the plan of a release reads k
once.

The game's rules do not change with k: the speeds, the forces and the
distances are the game's own in its time, and only the clock is different. So
the roll after a landing ends in the same place for each k, and it takes k
times as long.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `throw_law` | 2 | 2 is the real flight, in the God view and on the table. 1 is the square root law with the game's physics. 0 is the fixed gain (`throw_gain_pct`) with the game's physics |
| `throw_log` | 0 | 1 logs each turn of a flight and each third frame of its draw, for the desktop runs. 2 also logs each substep and each frame after the first contact |
| `throw_gain_pct` | 150 | The gain of `throw_law=0`, and the gain in the set down rule |
| `throw_slow_x10` | 20 | The time factor k times 10, from 10 to 100. 20 is k = 2.0: the flight has the arc of the real throw, 2 times slower |
| `release_fall_cm` | 15 | The height of the Hand's point over the land, in cm of the room, above which a still hand lets the object fall. 0 sets it down at each height |
| `throw_trees` | 1 | 1 lets a thrown tree fly as a rock does. 0 gives the tree the game's physics |
| `throw_draw` | 1 | Where the mod draws a thrown object between the steps of the game (see "The picture between the steps"). 1: a substep with a touch of another object is 5 ms of the picture, the picture stays at a guess until the object comes to it, and the clock falls back after a wait. 0: each substep has its time of the flight |

Outside the God view and the table (play modes 0 to 2) the throw uses the
gain of `throw_law=0`.

**The test keys** (`test_keys=1`, in `throw_physics.cpp`):

- Scroll Lock goes between the real flight and the square root law while the
  game runs. A flight that is in the air goes on. The log names the law after
  each press.
- Shift with Scroll Lock goes between the height rule of a release
  (`release_fall_cm`) and the set down at each height (see "The height of a
  release"). Scroll Lock alone does not read while Shift or Ctrl is down.
  With Ctrl and Shift down, Scroll Lock is the key of `catch_rest_guard`
  ([grab.md](grab.md), "An object that comes to rest while the Hand takes
  it").
- Ctrl with Scroll Lock goes between the two rules of `throw_draw`. A long
  pulse of the pointing hand means 1, and a short pulse means 0. A flight
  that is in the air keeps its rule.
- Numpad Plus makes k larger by 0.25 (slower), and Numpad Divide makes it
  smaller by 0.25 (faster), from 1.0 to 10.0. Each press gives a short pulse
  of the pointing hand (weaker at the end of the range) and a log line with
  the new k.

### The log

Tag `hand throw`. The release: the top speed and its time before the release,
the mean of 80 ms, the speed of the flight in units per second, the speed in
the game's time, and what the release sent. The start: where the real arc
meets the land, and when. "The game's physics has" the object: the delay, the
place of the game and of the flight, the mass, the radius and the drag. The
first contact: the time, the place, and the distance from the real arc at that
time. The end: the time, the cause, the distance across the land, the count of
turns and steps, and the time that the steps took.

### Checks

`ThrowChecks` in `bwvr/test/controller_math/throw_checks.h` covers:

- The top speed: a swing with the opening early and late, at 72, 90 and 120
  samples a second, an opening at the top speed, a constant speed, one
  sample, and a sample after the window.
- The set down: a still hand, a hand that came to rest, a move that stopped
  200 ms before the opening, a slow hand that throws, and a flick that stopped
  60 ms before the opening and throws at its top speed.
- The plan: the flight is `s v` and the game's velocity is `sqrt(s) v` at 7.5,
  100 and 1000 units per meter, along the hand's direction through a turned
  room. The release sends the cap at most, and 2.5 units per second across the
  land for a toss straight up.
- The arc: the game's step (`GameFreeStep`), run sqrt(s) times a substep,
  flies a throw of 5 m/s at 40 degrees from 1.0 m within 8 mm, 2 mm and 1 mm
  of the real arc at 7.5, 100 and 1000 units per meter. The flight lasts
  0.885 s, the object lands 3.39 m away and the top is 1.53 m.
- The steps: 200 substeps run 200 sqrt(s) steps, at 7.5, 67, 100 and 4000
  units per meter.
- The draw: the place between two samples, 10 ms after the newest one, the
  stop after 50 ms, and an object in contact.
- The picture between the steps: a substep with a touch is 5 ms of the
  picture with the rule 1 and 1.5 ms with the rule 0, a turn of such substeps
  is 100 ms and 30 ms, the speed of the picture in the flight and in a touch,
  when the picture stays at a guess, and the time that the clock gives back.
  The counts of the log: a picture that stands and jumps, and a turn back in
  the frame after a late frame.
- The time factor: at 7.5, 100 and 1000 units per meter and k = 1, 2 and 4,
  the plan gives the velocity `sqrt(s) v` and `sqrt(s) / k` steps, the object
  lands where the real throw lands, and the flight takes k times the real
  time. At 7.5 units per meter k = 4 is cut to 2.74.

### Desktop runs

`throw_queue.ps1` and `village_run.ps1` in `bwvr/tools/desk` throw an object
with the scripted hand, and the log has the lines of each flight. The runs
show these flights. The distance is from the object to the real arc at the
first contact, in cm of the room. The speed of a scripted throw is not the
same in each run, and the times and the places change with it.

| Recipe | The object and the throw | The first contact | The end |
| --- | --- | --- | --- |
| `throw_queue.ps1 -Runs landout` | A rock, 2.5 m/s, at 225 units per meter | After 1.09 s, 0.3 cm from the arc | At rest |
| `throw_queue.ps1 -Runs landin` | Two small objects of the class `MobileObject`, 3.0 and 3.1 m/s, at 66.7 units per meter | After 0.83 s and 0.82 s, 0.3 cm and 1.2 cm from the arc, on the slope at the pen | At rest 5 to 9 s after the first contact, at the foot of the slope in 3 of 4 throws (see "The rest after a landing") |
| `throw_queue.ps1 -Runs tree` | A tree, 3.0 m/s, at 100 units per meter | After 1.03 s, 2.3 cm from the arc | The tree settles, and the game takes it out of the physics 2.6 s and 4.3 s after the release in two throws |
| `throw_queue.ps1 -Runs sea` | A rock, 9.5 and 9.7 m/s toward the sea in two runs, at 100 units per meter | After 2.94 s and 3.01 s on the sea, 9.8 m and 10.3 m from the release, 0.5 cm and 0.4 cm from the arc | The flight ends 0.6 s and 0.7 s later, when the object is under the sea |
| `village_run.ps1 -Kind throw` | A villager, 2.6 to 2.9 m/s, at 44.4 units per meter | After 0.59 to 0.60 s on the land in three runs. In the runs with the first contact on an object: after 0.51 to 0.55 s, 1.5 to 3.1 cm from the arc | At rest 2.1 to 2.5 s after the release, 0.81 to 0.87 m from it, in the three runs on the land. In the runs with a touch, 32 to 42 substeps have a touch of the object |

The same throw of the villager shows the picture between the steps. With
`throw_draw=1` the log counts 1 of 197 frames after the newest step, no turn
back, and no time that the picture stood and jumped. With `throw_draw=0` it
counts 86 of 178 frames after the newest step (70 ms at most), 3 turns back
(1.6 cm of the room at most, each one after a guess), and 5 times that the
picture stood and then jumped (4.4 cm at most). The place of rest is the same.
The two throws of the rock of `throw_queue.ps1 -Runs land` touch no other
object: with `throw_draw=1` they count 0 of 342 and 9 of 337 frames after the
newest step (8 ms at most), and no turn back. In `throw_queue.ps1 -Runs tree`
the second tree lands on the first one, and 186 substeps have a touch: with
`throw_draw=1` the log counts 2 of 192 frames after the newest step (6 ms at
most), no turn back, and no time that the picture stood and jumped.

A frame of about 150 ms at the first contact of the villager (the word `shot`
makes one) gives 4 of 120 frames after the newest step with `throw_draw=1`
(17 ms at most), and 19 of 93 with `throw_draw=0` (129 ms at most). In a
second run with `throw_draw=1` such a frame came 56 ms after the newest step,
so the clock waited. It then ran 20 percent slower for 13 frames (0.25 s),
and no later frame was after the newest step. No run had a picture that
stayed at the place of a guess: the math checks cover that rule.

### Open

- The last 0.1 to 0.2 s of a roll is not drawn: when the object comes to
  rest, the picture goes to the place of rest at once.
- A Miracle and the objects that a thrown object pushes keep the game's time.
- While another object touches a thrown object, the thrown object moves at
  the speed of the game's own time (`sqrt(s) / k` times slower than in the
  flight). Only a headset shows whether that looks right.
- A flying tree is tested in desktop runs only (a throw of 3.0 m/s, and a
  fall from the Hand, see "The landing of a tree"), and not in the headset.
- Only a headset shows: whether a throw feels like a real throw at 7.5, 67
  and 400 units per meter, whether the top speed of 120 ms gives the throw
  that the player meant, whether a quick put down throws by accident (the 1
  m/s rule), whether a villager lives through a soft throw and dies from a
  hard one, whether a rock still breaks a house, and the cost of the steps at
  1000 units per meter and more (the log gives it for each flight).

## The landing of a thrown object

This section is the time from the first contact to the rest. The flight is
in "The throw physics".

### The cause of the jitter

The game made its contact for one step in a substep. At the rate of the flight
(for example 5 steps in a substep at 100 units per meter) the rock turns at 5
times the game's cap of 1.5 turns a second, and its center goes up and down 11
to 15 times a second while the rock turns over its points on the soft
springs. The bounce and the draw are not the cause. So the object needs the
game's own rate after the first contact.

### The contact of the game [V]

All of this is from the disassembly of `runblack.exe`.

- **The points.** A PhysOb has points: an array of `0x50` bytes a point at
  `+0x15C`, with the count at `+0x154`. A rock has 14. `fn_007FD200`
  (`PhysOb::ZeroForces` in the symbol file) turns each point (`+0x10`) by the
  axes and adds the center. Then it adds 0.06 s of the velocity
  (`0x008CA27C`). So each point is 0.06 s ahead of the object, and a fast
  object touches the land before it is there: at 30 units per second, 1.8
  units before.
- **The depth.** `fn_007FD4D0` takes the altitude of the land minus the height
  of the point (`+0x24`). At 0 or more the point is in contact: the count at
  `+0x158` goes up, the byte at `+0x176` is set, and `+0x40` gets the normal
  of the land (`LH3DIsland::GetNormal` `0x00803630`, the call at
  `0x007FD93F`). `+0x4C` is null for the land and the other PhysOb for a
  contact of two objects (`fn_007FDA60`).
- **The land has stairs.** `LH3DIsland::GetAltitude` `0x00803090` takes the
  place in a map cell of 10 units with 8 bits for each axis (`0x008032F9`,
  `0x00803318`). So the land of the physics is flat in squares of 0.039
  units, and a slope is a stair: on a slope of 62 degrees each stair is 0.07
  units high. The height of a point over the land has a step at the edge of
  each square.
- **The force.** `fn_007FDE40` gives each point in contact a force along the
  normal. On the first step it is `spring x depth`, and the point keeps that
  first depth (`+0x00`). On each later step it is
  `spring x first + 200 x spring2 x (B / A) x (depth - first)`, and never
  below 0. B (`+0x20`) is the distance of the point from the center with the
  0.06 s, and A is `+0x1C` [I: the distance without it]. So `spring2` is a
  second spring, and not a damper. The only loss along the normal is the 0.06
  s: the force comes early on the way in and goes early on the way out. In
  the math test a ball that lands at 26.5 units per second leaves the land at
  4.2.
- **The friction.** The same function pulls the point back to the place where
  it came into contact (`+0x04`), with the constant `spring`. It cuts that
  force to `friction` times the force along the normal, and then it moves the
  place. So friction acts on a point that slides. An object that turns over
  its points loses little. With another object the function takes the smaller
  constant of the two objects, and 0.3 times the friction.
- **The constants.** `PhysOb::SetUpConstants` `0x007FB810` reads a row of 6
  values (`Object::SetUpPhysOb` `0x006376B0`, the table at `0x00CC63E0`,
  `PhysicsConstants.txt`). `+0x13C` is the mass times the second value
  (`spring`). `+0x140` is the mass times the third (`spring2`). `+0x144` is
  the fourth (`friction`). `+0x148` is the fifth to the power 0.005: the turn
  keeps that part of itself in each step. A rock has 21.56, 1.328, 1.31 and
  0.709 (the log line "the contact of").
- **The turn.** `fn_007FE260` adds the torque to the angular momentum
  (`+0x68`) and multiplies it by `+0x148`. It cuts the angular speed to 3 pi
  radians a second (`0x009A2BB0`), which is 1.5 turns a second. The cut is on
  the speed of that step only (`0x007FE3BF` to `0x007FE413`): the momentum
  keeps its size.
- **The rest.** The same function gives 2 when a point is in contact, the
  speed is below T, the angular speed is below T / 2, and the torque over
  `m r^2` is below T. The angular speed of this test is the one before the
  cut (`0x007FE3BB`, `0x007FE5C5`). T is 1. After 15 s of life (`+0x170`, 5
  ms a step) T is the life in seconds over 15. Before it gives 2, the
  function sets the velocity and the momentum to 0 (`0x007FE682`).
  `PhysicsObject::GameTurnUpdate` then calls `EndPhysics` of the object
  (table slot `+0x790`) with the flag 1, keeps the object that the call gives
  in the entry, and sets the rest byte (`PhysOb+0x174`).

### What the mod does

`throw_land=1` is the default. All of it starts at the first contact, and the
flight latches the keys at that moment. The flight to the first contact does
not change: the code of the landing reads `Flight::contact` first.

1. **The rate goes to the rate of the land.** The rate of the land is 1 step
   in a substep, the game's own time (`throw_land_rate_x10`). The rate
   changes only while the object touches, with a time constant of 0.10 s of
   the picture (`LandRate`, `kLandEase`). So the rate has no step, and the
   speed of the picture has no step. A hop keeps one rate from the land to the
   land, so its arc is a true arc. At 1 step in a substep the thrown object
   runs as each other physics object, and a touch of another object changes
   nothing in the rate.
2. **The low bounce.** A hook on `fn_007FDE40` (`h_Contact`) takes the force
   that the function added to the object. While that force pushes the object
   along its own motion, the hook cuts it to a part (`ReboundFactor`). That
   is the way back of the spring. `throw_bounce_pct=50` keeps a quarter of
   the force, so a bounce keeps half of the speed that the game gives it. A
   speed along the force below 0.5 units per second (`kReboundSoft`) gets a
   part of the cut in proportion, so an object that lies on the land does not
   sink. The ball of the math test leaves the land at 1.3 in place of 4.2.
3. **The brake of the land.** The hook adds a force against the velocity of
   the center while the object touches the land (`LandBrake`). Its size is a
   friction times the force of the contact, and never more than the force
   that stops the object in that step. The friction is
   `throw_land_brake_pct` percent of the friction that the game has for the
   object: 60 percent of 1.31 is 0.79 for a rock. The game keeps a part of
   the turn in each second, and for the thrown object that part is 0.25 at
   most (`kLandTurnKeep`, the game's value goes back at the end). Without the
   brake the rock rolls for a long time at the rate of the land.
4. **A contact with another object** keeps the game's force: the other object
   got the same force, and the two must agree.
5. **The damage** is the game's. The sum that the game reads for
   `ReactToPhysicsImpact` gets the force as the game made it, with no low
   bounce and no brake.
6. **The water.** An object that floats counts as in contact, so its rate
   goes to the rate of the land too. The float has no spring, so the hook
   changes no force there.
7. **The rest.** A turn in place wears off fast, the brake is off on steep
   land, and an object that lies still for 0.5 s is at rest
   (`throw_land_rest`). See "The rest after a landing".

`throw_land=2` keeps the rate of the flight and has the low bounce and the
brake. `throw_land=0` gives the game's landing, with no change.

**Why the brake is 60 percent.** That is a friction of 0.79 for a rock, near
the median friction of the materials of Half-Life: Alyx (0.8,
`research-hla.md`). The brake holds an object on a slope below the friction
angle, which is 38 degrees at 0.79. With the full friction of 1.31 an object
would stay on a slope of 52 degrees. On a steeper slope, such as the slope of
the pen, the rock slides until the land is flat.

### The rest after a landing

Three things keep a thrown object from its rest after its center is slow.
`throw_land_rest=1` corrects each of them. The flight latches the key at the
first contact, and the rule acts with `throw_land` 1 or 2.

**A turn in place.** The contact gives an object more turn than a roll at
its speed has. The game cuts the angular speed to 1.5 turns a second, but
the momentum keeps its size, and the rest test reads the turn before the cut
(see "The contact of the game"). So an object lies with its center still
and turns over its points: at 1.5 turns a second at first, and for 2 to 5 s
in all, until the turn factor has worn the momentum down to 0.5 radians a
second. This is the turn of a tree that falls ("The landing of a tree"), and
each object has it. The larger the world is in the room, the plainer the
turn is to see: at 7.5 units per meter a tortoise is 13 cm across.

The mod reads the turn of the object in each substep: the angle between its
axes before and after the substep (`TurnBetween`), over the time of the
steps. An object turns in place when it touches the land and the turn times
its radius is more than 2 times the speed of its center (`TurnsInPlace`,
`kSpinOverRoll`): a roll has the two equal. Such an object keeps only 0.001
of its turn in each second (`kSettleTurnKeep`), as a tree that settles does.
A roll keeps the turn of the brake, and so does an object in the air.

**Steep land.** The brake holds an object on a slope up to its friction
angle: 30.5 degrees for a brake with a friction of 0.59, and 38 degrees for
0.79 ("Why the brake is 60 percent"). On steeper land the object must go
down until the land is flat. But there the brake still acts against the
motion, on top of the friction of the game, and the turn keeps only a part
of itself. So the object goes down in small moves. On the slope at the pen
(42 to 75 degrees) a small object moves 0.6 to 1.3 units a second, with
stops, for 10 to 30 s, and the game calls it at rest only because its limit
T grows after 15 s. With the game's landing the same object is at the foot
of that slope after 10 s of the game's time.

So the brake is off on land that it can not hold the object on
(`SteepBrakeFactor`). The hook reads the normal of the land at the points in
contact (the mean of `+0x40` of each point). Up to the friction angle of the
brake, the brake has its full size. Above it the brake goes to 0, and it is
0 where the tangent of the slope is 1.15 times the friction of the brake
(`kSteepFade`): 34 degrees for a friction of 0.59. On such land the object
goes down with the friction of the game, as in the plain game, and the brake
stops it on the land below. Land below the friction angle of the brake is
not changed.

**An object that lies still.** The rest test of the game has three limits:
the speed, the turn, and the torque over `m r^2`. A small object has a small
`r^2`, so the springs of its points keep the torque over the limit while the
object lies still. In a desktop run an object with a radius of 0.5 units did
not move for 5 s and was not at rest, and then it moved again. The game makes
its limits wider only after 15 s of life.

The mod counts the time in which a point of the object is in contact, its
speed is below 0.1 units a second and its turn is below 0.5 radians a second
(`LiesStill`, `kStillSpeed`, `kStillTurn`). A substep with more speed or more
turn starts the count again. After 0.5 s (`kStillSeconds`) the hook on
`fn_007FE260` sets the velocity and the momentum to 0 and gives the game 2,
as the function does for its own rest. The game then ends the physics of the
object in its own way. The log has the torque of that moment: 1.0 to 6.0
over `m r^2` in four landings of the desktop runs, with the speed and the
turn far below their limits. An object that floats has no point in contact,
so it does not come to rest, as in the game.

`throw_land_rest=2` has the turn in place only, 3 has the steep land only, 4
has the object that lies still only, and 0 has none of the three.

**Desktop runs.** `throw_queue.ps1` in `bwvr/tools/desk` has a recipe for each
scale: two throws of about 3 m/s from about 50 cm over the land (2 m/s at 225
units per meter). The word `aimfar` takes a different object at each scale,
and the throw lands on different land, so compare the two values of the key
at one scale. A time is from the first contact to the rest of the game. The
number after it is the count of the changes of the vertical direction of the
center for each second (a move of 0.5 mm of the room or more counts).

| Units per meter | The recipe, and what the hand throws | `throw_land_rest=1` | `throw_land_rest=0` |
| --- | --- | --- | --- |
| 7.5 | `landlife`: a tortoise, two times | 1.4 s and 1.3 s (1.4 and 1.5) | 5.6 s and 5.2 s (0.7 and 1.5) |
| 29.6 | `landin3`: a tree, then a tortoise | 1.5 s and 1.5 s (0.7 and 0) | 3.0 s and 5.9 s (0.7 and 0.3) |
| 44.4 | `landin2`: two small objects of the class `MobileObject`, onto the slope at the pen | 5.6 s and 4.8 s (0 and 0.2) | 5.2 s and 12.4 s (0 and 0) |
| 66.7 | `landin`: the same two objects, onto the same slope | 8.6 s and 7.9 s, and 5.1 s and 9.4 s in a second run (0 to 0.3) | 5.5 s and 11.7 s (0 and 0) |
| 100 | `land`: a rock, then a small object | 2.8 s and 3.4 s (0 and 0) | 4.0 s and 5.8 s (0 and 0) |
| 225 | `landout`: two rocks | 1.2 s and 1.2 s (3.4 and 1.7) | 6.3 s and 2.7 s (0.3 and 1.5) |

With the value 1 an object comes to rest 1.2 to 3.4 s after its first
contact on land that the brake holds. On the slope at the pen it goes down
to the foot of the slope first, 41 units lower, at 3 to 9 units a second,
and it is at rest after 5 to 9 s. With the value 0 the tortoise lies with
its center still and turns in place for 4 s, and a landing on the slope at
the pen took 15 s and 30 s in two more runs.

On land that the brake holds, the place of rest is the same with each value,
within the difference between two throws. On the slope at the pen the place
is different. With the value 1 the object came to rest at the foot of the
slope in 3 of 4 throws at 66.7 units per meter (36 to 38 units from the
release, as with the game's landing), and one time on the slope (47 units).
With the value 0 it came to rest on the slope (46 to 50 units).

The object does not tremble with either value. The changes of direction are
the bounce of the landing and the hops: 0 to 4 in a landing with the value
1, and 0 to 8 with the value 0. A thrown tree and a thrown villager come to
rest with the value 1 too (`throw_queue.ps1 -Runs tree`, `village_run.ps1
-Kind throw`): see "Desktop runs" in "The throw physics".

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `throw_land` | 1 | 1 is the rate of the land with the low bounce and the brake. 2 is the low bounce and the brake at the rate of the flight. 0 is the game's landing |
| `throw_land_rest` | 1 | How a thrown object comes to rest after its landing (see "The rest after a landing"). 1: a turn in place wears off fast, the brake is off on land that is steeper than the brake holds, and an object that lies still for 0.5 s is at rest. 2: the turn in place only. 3: the steep land only. 4: the object that lies still only. 0: the turn of the brake, the brake on each land, and only the rest test of the game. It acts with `throw_land` 1 or 2 |
| `throw_land_rate_x10` | 10 | The rate of the land times 10, from 10 to 100. 10 is the game's own time. A rate above the rate of the flight changes nothing |
| `throw_bounce_pct` | 50 | The share of its speed that a bounce keeps, of the speed that the game gives it. 100 is the game's bounce |
| `throw_land_brake_pct` | 60 | The friction of the brake, in percent of the friction that the game has for the object. 0 is no brake |
| `throw_tree_settle` | 1 | 1 lets a tree that falls turn down fast while it touches the land and is slow (see "The landing of a tree"). 0 keeps the game's turn. It acts with `throw_land` 1 or 2 and `throw_trees=1` |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The test keys** (`test_keys=1`): the Pause key goes through the three
landings: 1 (a long pulse of the pointing hand), then 2 (a pulse of middle
length), then 0 (a short pulse), and 1 again. Shift with Pause turns the
settle of a tree on and off (see "The landing of a tree"). Ctrl and Shift
with Pause go between `throw_land_rest` 1 (a long pulse) and 0 (a short
pulse). With Ctrl down the Pause key is the Break key of Windows
(`VK_CANCEL`), and Ctrl with Pause and no Shift is a key of the audio
(`audio.md`). Ctrl with Numpad Plus makes the
rate of the land lower by 0.25 (slower), and Ctrl with Numpad Divide makes it
higher, from 1.0 to 10.0. Each press gives a log line. An object that touched
the land before a press keeps its landing. Whether the game uses the Pause
key is not known [G].

**The fallback.** When the hook on `fn_007FDE40` fails, a thrown object has
the game's bounce and no brake, and the log says so at the start and at each
first contact. When the list of calls after the Hand's draw is full, a
release in the glide starts at the point of the glide, and the log says so.

### The log

Tag `hand throw`. "The contact of": the points and the constants of the
object. "The landing of": the landing that the flight latched, the two rates,
the bounce, the friction of the brake and the rule of `throw_land_rest`.
"The land under ... has a slope of": the first step in which steep land
makes the brake less. "Turns in place": the first substep in which the turn
of the object is no roll. "Lay still for": the rest that the mod gives, with
the speed, the turn and the torque over `m r^2` of that moment. "After the
first contact": the time
and the way to the rest, the top speed, the hops (count, the highest, the
longest, the part of the time in the air), the changes of the vertical
direction of the center (a move of 0.5 mm of the room or more counts. The
height is that of the center in the world, and not its height over the land,
because the land of the physics has stairs), the
turns, the rate at the first contact and at the end, the steps with a cut
force, the substeps with a turn in place, the steps with less brake on steep
land, the frames that the mod drew after the newest step, and the times that
the picture turned back. "The picture of": the rule of `throw_draw`, the
turns back in the frame after a frame that the mod drew after the newest
step (those are from a guess, and each other one is a bounce), the times that
the picture stood for a frame and then moved 2 mm of the room or more, the
longest move of one frame, the frames that the picture stayed at a guess, and
the substeps with a touch of another object. With `throw_log=2` the lines `s`
(each substep: the time of the picture, the center, its height over the land,
the points in contact, the velocity, the turn, the rate) and `d` (each frame:
the drawn place, how late it was, and `h 1` when the picture stayed at a
guess) follow each first contact. The line of each turn has the time of the
flight and the time of the picture of the physics, and the real time.

### Checks

`ThrowChecks` in `throw_checks.h` also covers: the rate of the land (no touch,
one time constant, the end, no step larger than 5 percent of the change in a
substep), the factor of the low bounce, a ball on the contact of the game with
and without the low bounce (it comes to rest and does not lie deeper), the
brake (its size, the stop in one step, a slide that stops, a slope of 30
degrees that holds the block and one of 45 degrees that does not), the rest
after a landing (the values of the key, an object that lies still and the
count of its time, a roll and a turn in place, the turn
factor of each, the time of a turn at the cap, the part of the brake on
slopes of 0 to 62 degrees and at a wall for a brake friction of 0.59 and of
0.79, no step in that part, and a block
on a slope of 62 degrees with the brake on each land and with the brake off
on steep land), the turn
between two matrices with three scales, the numbers of the log line (the
stairs of the land under an object that goes down a slope are no change of
its vertical direction), and the
catch up of the picture (`kCatchUp`).

### Open

- Not tested: a throw into a pile of objects. A desktop run shows the landing
  of a villager, of a rock in the sea and of a rock at 7.5 to 225 units per
  meter ("Desktop runs" in "The throw physics", and "The rest after a
  landing"). The log does not say whether a thrown villager lives.
- The rest after a landing: only a headset shows whether a rock that stops
  its turn this fast looks right, and whether a rock on a steep slope goes
  down at a pace that looks right. On such a slope the object moves in the
  game's own time until the land is flatter (8 s on the slope at the pen,
  which is 41 units high).
  Ctrl and Shift with Pause give the landing with no rule for a comparison.
- Only a headset shows: whether the landing looks natural at the rate of the
  land, or too slow (on the slope of the pen the rock moves slowly for about
  2.5 s before it stops: try `throw_land=2` with the Pause key, or a higher
  rate with Ctrl and Numpad Divide), whether the change of the rate shows at
  the first contact, and whether a thrown villager lives through a soft throw
  and dies from a hard one.

## The landing of a tree

A tree is a physics object with 16 points, a spring, a friction and a turn
factor like a rock. A tree that falls lands on one corner of its body, and
the contact gives it an angular momentum that is many times the cap of 1.5
turns a second (`fn_007FE260` cuts the angular speed, not the momentum). So
the tree turns at the cap with its center still, for as long as the turn
factor of the game takes to wear the momentum down. A desktop run of a tree
let go 45 cm over the land showed this with the game's own landing
(`throw_land=0`), with the game's physics for the tree (`throw_trees=0`) and
with the landing of the mod: the center stays at 1.5 units over the land, but
the mesh point of the tree (its base) goes round and up and down by 2.6 units
for 4 s or more, and the tree needs 5 s to leave the physics. The draw and
the candidate place of the far grab follow the mesh point.

**What the mod does.** With `throw_tree_settle=1` a tree that has touched the
land, and whose center speed in the game's time is 6 units a second or less
(`kSettleSpeed`), keeps only 0.001 of its turn in each second
(`kSettleTurnKeep`, `TurnKeepAfterLanding` in `throw_math.h`). The mod sets the
factor at `PhysOb+0x148` in each substep, as it does for the brake of the turn,
and it gives the game's value back at the end of the flight. The speed test
leaves the first bounce and a real roll alone. The mod writes nothing else into
the physics object. In the desktop run the mesh point then moves 0.2 units or
less from 0.9 s into the flight (0.3 s after the first contact), and the tree
leaves the physics 1.2 to 1.7 s after the release, in place of 5.1 s. The log
has a line when the settle starts.

The settle acts on a flight of the mod. A tree with `throw_trees=0` or
`throw_law` other than 2 keeps the game's physics, and `throw_land=0` keeps the
game's landing. Rocks and other objects are not changed.

**Open.**

- The far target of a tree that was let go did not flicker in the desktop
  runs, with the settle on or off. The aim keeps the tree, and the gaps in the
  target were the near rule while the tree falls past the Hand. The tumble is
  gone with the settle, so it can not move the candidate.
- Only a headset shows whether a tree that stops this fast looks right, and
  whether a rolling tree on a slope still rolls.

## The height of a release

This section is the release of a still hand: a set down or a fall.

### What the game does [V]

`Object::InitialisePhysicsFromHand` `0x00636F00` decides at the drop action.
It adds the object to the physics at the place of the Hand, with the velocity
of the release. When the speed across the land squared is 4 or less (1 or
less with the gentle mark, see `hand.md`, section 3), it is a set down: the
function calls `PhysOb::AdjustToGroundLevel` `0x007FCB80` and
`PhysicsObject::RaiseUntilNotIntersecting`, so the object stands on the land
under the Hand at once and does not fall. With a faster speed it is a throw,
and the object falls under the game's physics. So a mouse Hand, which is
always near the land, loses nothing. A real hand in the God view can be far
over the land, and a set down from there was a jump to the land.

The classes that override the function: `Living` `0x005EFD80` and `Villager`
`0x005EFE90` (they call the base function and then set the state of the
creature), `Scaffold` `0x006EA8A0` (it destroys things in the way and then
calls the base function) and `Pot` `0x0066DF00`. A `Pot` (a pile of wood or
food in the Hand) with a velocity squared of 5 or less pours out at the place
(`StartMultiPutdown`). Above 5 it goes to the base function and is thrown as
any object. So the set down is the same for each kind of object that the
Hand can hold (a rock, a tree, a villager, an animal, a ball, a crop, a
scaffold). The mod found no kind that falls from the Hand's place with a
velocity of zero.

### What the mod does

`release_fall_cm` (default 15) is the height rule. The mod latches the height
of the Hand's point over the land (`g_target` and the altitude of the land
under it, which is 0 over the sea) at the opening of the grip, in meters of
the room at the scale now (`FallsFromHeight` in `throw_math.h`). A hand that is
still at the opening (`SetDown`) then does one of two things:

- At the limit or lower, the object is set down as the game does it, with a
  velocity of zero.
- Higher up, `RealThrowPlan` makes a fall. The object leaves with the mean
  velocity of the hand over the 80 ms before the opening (near zero across the
  land, and the vertical speed of the hand). The release sends 2.5 units per
  second across the land (`kSendFlat`), which is only the least that the game
  needs to throw the object and not set it down. The real flight then
  overwrites the velocity, so the object falls straight down from the place of
  the Hand under the game's physics, with the landing of "The landing of a
  thrown object". A throw with a swing is a throw at each height.

15 cm is the top of the reach values of `grab.md` (4 to 15 cm): a hand that
hovers within the reach of the land still puts the object down.

What each kind does:

| Object | At the limit or lower | Higher up |
| --- | --- | --- |
| A rock, a ball, a crop, a scaffold, an animal, a villager | Set down | Falls. The game's landing damage is the game's own (see "The damage") |
| A tree | Set down, and the game plants it | Falls, and the game does not plant it (see "A tree in the Hand" in grab.md). The card says so before the opening |
| A pot of wood or food | Pours out at the place | Falls as an object (the speed squared is 6.25, above 5), and does not pour |
| A Miracle | Set down | Set down. A Miracle keeps its own rules and the game's release |
| A thing that goes onto a thing (a store, a building site, the altar) | Its own rule, `grab.md`, "An object in the Hand that goes onto a thing" | The same. While a thing at the Hand takes the object (`VillageTakesObject` in `village.cpp`), the height rule does not act |

`release_fall_cm=0` gives a set down at each height. The rule
works with `throw_law=2` in the God view and on the table. The other laws and
the play modes 0 to 2 keep the set down.

### The test key

With `test_keys=1`, Shift with Scroll Lock goes between the height rule and the
set down at each height. A long pulse of the pointing hand means the rule is on,
a short pulse means it is off. The log has a line for each press.

### The log

Tag `hand throw`. A fall: "The hand was still, but its point was N cm of the
room over the land, and the limit is N cm", the velocity, and what the release
sent. After it the log has the lines of a flight (`throw_log=1`). A set down
names the height too.

### Checks

`ThrowChecks` covers the height rule: a set down at 5 cm and at the limit, a
fall at 30 cm, the limit of 0, and, at 7.5, 67 and 1000 units per meter, a
still hand that falls with its own velocity and sends more than the speed of a
set down. It covers a hand at rest that falls from rest, and a swing that is a
throw at each height.

### Open

- A desktop run (`throw_queue.ps1 -Runs fall` in `bwvr/tools/desk`) shows a
  rock that falls from 45.9 cm, and a tree that the Hand sets down at 2.0
  cm. A tree that falls is in "The landing of a tree".
- Not tested in a desktop run: a pot, a villager and an animal that fall
  from the Hand, and an opening over a store at a height.
- Only a headset shows whether 15 cm feels right.

## The release at once

This section is the time from the opening of the real hand to the moment the
object leaves the Hand. The throw speed and the flight are in "The throw
physics".

### The four moments of a release

| Moment | Frame | Cause |
| --- | --- | --- |
| The grip opens | F | The grip value comes below 35 percent |
| The click of the open grip | F and F + 1 | `GripStep` presses on the frame of the opening and releases when hand mode `0x17` shows |
| The release action `FUN_005D4DB0` | F + 2 | The game runs it one frame after the button goes up |
| The object leaves the Hand | F + 2 to F + 4 | The packets of the release wait for the next game turn |

Hand mode `0x17` shows one frame after each press, so the release action
comes 2 frames after the opening. That is the least that a click can take.
Without the mod's change the packets wait for the game's turn, up to 100 ms.
In that time the Hand is in the Holding state, so the game draws the object at
the Hand, and the Hand is on the real hand. A hand that swings at 3 m/s moves
26 cm in 87 ms. Then the object goes back to the release point of packet
`0x4D` and starts its flight. A late pick up (the grip opened during the pick
up) makes the click wait some frames for the object.

### The game turn [V]

The release action sends packet `0x4D`, and the drop sender sends the drop.
The game runs packets only in a game turn (`GGame::ProcessOneGameTurn`).
`GGame::ProcessNetworkPackets` `0x0054CC30` asks
`GGame::LocalTimerSaysDoATurn` `0x0054C4A0` at `0x0054CD45`, and a single
player game runs at most one turn in a frame (`0x00D01978` counts them). The
timer gives 1 when the game time in ms is at or past 100 times the turn count
(`GGame+0x205A40`). It gives 0 while a single player game is paused (bit 4 of
`GGame+0x14`). So a turn comes each 100 ms of game time, and a packet waits
0 to 100 ms for it.

### What the mod does

**A game turn at once.** `h_InterfaceStep` knows before the step when the
step will send such packets: the release bit (`+0x39` bit `0x40`) with
interface action 12 and an object in the Hand, or the release bit while a
bulk take runs. After that step it calls `TurnNow`. A hook on
`LocalTimerSaysDoATurn` (`h_DoATurn`) then gives 1 to the next ask of the
game, once, in the next 3 frames. The game runs that turn as each turn: the
turn count goes up by one. The turn after it is due at 100 ms times the new
count, so it comes later by the time that this turn came early. The game does
not run faster, and it runs no turn that it would not run. The hook gives no
turn while the game is paused, in a multiplayer game (all players must run
the same turns), or when the controller does not place the Hand. So the mouse
is the game's own. `release_at_once=0` in `bwvr.ini` leaves the function
unhooked.

**The grip opens on a fast fall.** A closed grip opens below 35 percent
(`kGripOpenBelow`). A hand that opens for a throw takes some frames to come
that far. `GripLevelStep` (`controller_math.h`) also opens a closed grip when
its value falls by 30 percent or more (`kGripFastFall`) in 3 frames
(`kGripFastFrames`), to a value below 80 percent (`kGripFastBelow`). A slow
change of the grip opens it only below 35 percent, so a hand that relaxes
during a carry does not let go. After a fast opening the grip closes again
only when its value rises by 10 percent (`kGripReclose`) from its lowest
value, or from below 55 percent (`kGripCloseAbove`). Only the control that is
the game's right button with the direct hand has this rule: the grip, or the
trigger with `controller_swap_buttons=1`. The other control and the pointer
keep the plain levels. `grip_fast_open=0` in `bwvr.ini` gives the plain levels
to both.

The fast fall does not act in the first 300 ms of a press while the grip
machine is in its Pass phase, before the Hand holds the object
(`grip_press_guard=1`, `GripPressGuarded`). A press that ends in less than
225 ms is a click for the game and splits a rock, and a dip of the squeeze
of a hand that jerks away must not end a press that the player still holds.
In that time only a value below 35 percent opens the grip. A carry has the
fast fall, so a throw is as fast as before.

The click of the open grip stays: 2 frames from the opening to the release
action. A release with no click would need the mod to put the interface in
action 12 by itself, and the game's press does more than that (the throw
spring of the Holding state). This is not built.

**The cost [V for the code].** `GGame::Loop` keeps the time since the last
turn (`LoopTimeRemainder`, `0x00D019BC`, at `0x0054D2E7`). It takes 100 ms
from it when the turn count changes, and it keeps it from 0 to 99
(`0x0054D325`). The game time at `GGame+0x25053C` is 100 times the turn count
plus this time. After a turn that came early, that game time goes forward by
up to 100 ms at once, and then it waits for the same time at 99. So a thing
that the game moves by this time jumps a part of a turn forward and then
stands still for that part. This comes once for each release. Which things
use this time is not traced.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `release_at_once` | 1 | 1 runs a game turn on the frame after a release of the Hand and after the opening of the grip in a bulk take. 0 waits for the game's own turn, up to 100 ms |
| `grip_fast_open` | 1 | 1 also opens a closed grip on a fast fall of its value. 0 opens it only below 35 percent |
| `grip_press_guard` | 1 | 1 keeps the fast fall from opening the grip in the first 300 ms of a press, before the Hand holds the object. 0 lets it open the grip at any time. The test key Ctrl Shift Home goes between the two (`test_keys=1`) |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The A/B key.** With `test_keys=1`, the star key of the number pad (`*`,
`VK_MULTIPLY`) turns the release at once off and on while the game runs: the
game turn after a release, and the fast opening of the grip. The pointing
controller gives a long pulse for on and a short pulse for off, and the log
has a line for each press. The mod reads the key only while a window of the
game is in front, and it does not take the key from the game. Ctrl must be
up: Ctrl and the star key is the key of the flight of a camera in the Temple
(`controllers.md`, 11.6). F11 and F12 have no free combination left
(`PollHotkeys` in `stereo.cpp`), and Scroll Lock is the A/B key of the throw
law. Whether the game itself uses the star key is not known [G].

### The log

Tag `hand throw`: "the object left the Hand N frames after the release
action" for each release, or "the Hand still holds the object" 90 frames
later when the game refused the drop. Each report window: the count of
releases, the mean and the largest count of frames, the turns that the mod
asked for and the turns that the game ran. Tag `grip`: for each opening of
the grip, whether it was a fast fall, and the values of the grip over the
last 12 frames. Each report window counts the two kinds of opening. The line
`hooks : GGame::LocalTimerSaysDoATurn` shows the hook at the start.

### Checks

`LevelChecks` in `bwvr/test/controller_math/grip_checks.h` covers the grip
level: the plain levels, the same answer as the plain rule with the fast
opening off, a release in 8 frames that opens the grip on frame 3 (the plain
rule: frame 6), a grip that relaxes by 2 percent a frame and opens only below
35 percent, a fall of 28 percent in one frame and a fall to 82 percent that
keep it closed, a fall of 31 percent in 3 frames that opens it, the rise that
closes it again, and the trace of 12 values.

### Open

- Not tested: a game turn at once while a story or a script runs, and in a
  saved game that loads.
- Not shown by a desktop run: the object leaves the Hand 1 or 2 frames after
  the release action with the defaults, and 1 to 6 frames with
  `release_at_once=0`. The run needs the start dialog of `god-view.md`,
  section 12, a far target that is no tree (`far_test_influence=1`,
  `far_grab_max_m=4.0`) and a pull, a grip open and a release.
- Only a headset shows: whether a throw leaves the hand when the hand opens,
  and whether the fast opening lets go of an object that the player wants to
  keep (the lines of the tag `grip` show the grip values before each opening).
