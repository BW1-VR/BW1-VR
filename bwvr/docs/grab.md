# Taking things with the Hand

This doc says how the player takes things with the Hand: the grip that holds,
the one point and the one target of each press, the grab from a distance, the
take from a pile or a forest, and an object in the Hand that goes onto a thing.
The game's own pick up, hold, drop and throw are in [hand.md](hand.md),
section 3. The other docs of this subject:

- [hand.md](hand.md): the game's Hand and the mod's direct hand.
- [throw.md](throw.md): the throw, the landing and the release.
- [creature.md](creature.md): the Creature and its leash.
- [miracles.md](miracles.md): a miracle in the Hand.
- [village.md](village.md): clicks on things, scrolls, the village, the Temple
  door.
- [hand-model.md](hand-model.md): the model of the Hand, the glow, the
  overlay renderer, haptics.
- [controllers.md](controllers.md): the aim ray and its presets (section 8).

Marks: [V] verified in the disassembly, [I] inferred.

## Summary

- **The grip holds like a real hand.** Close the grip on a target to take it.
  Keep it closed to carry. Open it to drop or to throw. The game's right
  button is the grip in the other cases (a take from a pile, a pull of a tree,
  a leash).
- **One point.** Each press acts from the Hand's point: the tip of the index
  finger, where the glow is. No press acts from the palm.
- **One target.** At each moment the grip has one target or none: the choice
  of the mod at the Hand's point, or the far target of the aim ray. The
  target has a halo. A press acts on that object and on no other.
- **The grab from a distance.** Point at an object, close the grip, pull the
  hand toward the head. The object glides to the Hand. A pile, a field and a
  scroll need no pull.
- **Bulk.** A pile of a store, a field and a fish farm give a take that lasts
  while the grip is closed. A forest gives one tree for each grip. With
  `forest_take=1` it gives many trees in one bundle.
- **An object in the Hand goes onto a thing** at the Hand: a store, a
  scaffold, an altar, a Creature. The mod puts that thing in the pick record
  so the game's own press acts on it.

Source files: `hand_direct.cpp` (the pick, the target, the record, the grip
facts), `controller_math.h` (`GripStep`, the cone, the pull, the glide),
`near_math.h` (the body, the rule of the target), `far_grab.cpp` and
`far_grab.h` (the states of the far grab), `forest_take.cpp` and
`forest_take_math.h`, `village.cpp` and `village_math.h`, `controllers.cpp`
(the pump, the fake words), `hand_glow.cpp` (the halo), `overlay.cpp` (the
tether). Checks are in `bwvr/test/controller_math/` (`grip_checks.h`,
`far_checks.h`, `near_checks.h`, `forest_checks.h`, `village_checks.h`).

## The grip holds

The game takes an object with one click and lets go with a second click
([hand.md](hand.md), section 3). The second press starts the throw spring and
its release runs the release action. A player would need to close and open the
grip twice. So with the direct hand (`controller_hand=1`) the grip holds like
a real hand, in every play mode:

1. Close the grip on a target (the glow is gold). The game's pick up starts at
   once. An object at rest is in the Hand one or two game turns later. An
   object that moves is in the hand at once ("A catch at once").
2. Keep the grip closed. The Hand carries the object. The game's right button
   is up during the carry, so the trigger still drags the land.
3. Open the grip. The Hand lets go. A fast hand throws with the speed of the
   swing at the opening. A still hand sets the object down at the Hand.

`GripStep` in `controller_math.h` is the machine, with no game state.
`controllers.cpp` runs it once a pump for the right button, and
`hand_direct.cpp` gives it the facts of the Hand. With
`controller_swap_buttons=1` the trigger does all of this, because it is then
the right button. With `controller_hand=0` the grip is the right button, one
to one.

| Phase | When | The game's right button |
| --- | --- | --- |
| Pass | The Hand holds nothing, or the game does not hold the object in the Holding state | The grip, one to one |
| Carry | An object came into the Hand while the grip is closed, and the pick up is over: the game has the object in its Hand (`GInterfaceStatus+0x90`) and the pick lock `GInterface+0x38` is clear | Up |
| OpenHeld | An object is in the Hand and the grip is open, for example after the game refused a drop | Up. A close carries it again |
| LetGo | The grip opened while it carried | The mod's click: down, then up when the hand mode is `0x17` on a later frame, when the object left the Hand, or after 130 ms |

Each time of the machine is in milliseconds of real time, so the grip does
the same thing at each frame rate. A count of frames is only the order of
two events: the game must read a release in one frame and a press in a later
frame.

**The press of the game.** While the grip is closed in the Pass phase, the
button of the game stays down until the pick up of the game is over. The
game has two ways to take an object [V]:

- The right press on an object that the Hand can hold is `fn_005D1740`. It
  keeps the clock and the game turn of the press (`GInterface+0x454` and
  `+0x458`), sets bit `0x10` of the pick lock and starts the interface
  action 13. For an object that it can put in the Hand of the picture at
  once it calls `fn_0046DC30`, which writes `CHand+0x4904`.
- The step of action 13 is `fn_005D5250`. It takes the age of the press: the
  smaller of the real time since the press (the clock that
  `LH3DRender::StartFrame` writes at `0x00EA9EB0`) and 100 ms for each game
  turn since the press and one more. So a press is 225 ms old only when the
  second game turn after it has started.
- **An object that the physics of the game has** (bit `0x40` of `Object+0x24`
  is set and bit `0x10` of `Object+0x0A` is clear: it falls, flies or rolls).
  While the age is less than 225 ms, the step only waits. A button that goes
  up in that time is a click: the step calls `CHand::ThrowObject`, so the
  Hand lets the object go, and then `fn_005D3930`, which taps the object.
  The tap of a rock splits it (`Rock::InterfaceTap` `0x006E7480` calls
  `Rock::SplitInTwo` `0x006E7560`). From 225 ms on the step calls
  `fn_005D2800`, which sends the pick up (packet `0x13`) and starts action 7.
  The game has the object in its Hand one game turn later
  (`GInterface::PlaceObjectInMagicHand`, which takes the object out of the
  physics). The step of action 7 (`fn_005D4A90`) clears the pick lock and
  resets the action when the object of the record has bit `0x04` of
  `Object+0x24`, with the button down or up [I: the bit is the mark of an
  object in a Hand]. With no object in the record that step gives 0, and
  the interface step then ends the pick up (`fn_005D1260`).
- **An object at rest.** The step waits while the Tug flag `CHand+0x4908` is
  set, and then it sends the pick up in the same way. A button that goes up
  before that is a click when the press is 225 ms old or less.
- `fn_005D1330` tells a press on an object from the press of a release by
  the byte `GInterfaceStatus+0x90`: it is not 0 while the game has an object
  in its Hand.

With `catch_at_once=1` the mod gives the step of action 13 a press that is
225 ms old for an object that the physics of the game has, so the step sends
the pick up on the first frame after the press ("A catch at once"). The
rules below are for each press, and for `catch_at_once=0` they are the only
guard against a click.

So a time or a count of frames must not end the press of a closed grip. With
`grip_press_hold=1` the machine reads the facts of the game on each pump
(`HandDirectGripPress`):

1. The carry starts when the game has the object in its Hand and the pick
   lock is clear. While a release is a click for the game, no time ends the
   press. A pick up that the game does not end in 1 s is a carry from then
   on.
2. **A short press on an object that moves.** When the grip opens while the
   physics of the game has the object and a release is a click for the game,
   the button stays down until the press is 240 ms old on the clock of the
   game (`controller_math::GamePressAge`), and for 1.5 s at most. The game
   then takes the object, and the machine lets it go with its click when the
   game has it in its Hand. So a press on an object in the air never splits
   a rock: the object comes into the Hand, and the open Hand lets it fall.
   With `catch_at_once=1` the game sent its pick up before the grip opened,
   and the button stays down in the same way until the game has the object.
3. **A short press on an object at rest** is a click, as in the plain game:
   the button goes up with the grip, and a rock at rest splits.

`grip_press_hold=0` gives a carry when the pick lock is clear, or 167 ms
after the object came into the Hand of the picture, and no press that the
mod holds. That time is 15 frames at 90 frames a second, which is less than
225 ms. With `catch_at_once=0` the press on an object that moves is then a
click: the Hand lets the object go one frame after the line `the pick up is
over`, and a rock splits. With `catch_at_once=1` the game sent its pick up
on the first step, so that press is no click.
With `test_keys=1`, Shift and the plus key of the number pad change the key
while the game runs (Ctrl must be up). The pointing controller gives a long
pulse for 1 and a short pulse for 0.

**The click.** The press comes on the frame the grip opens. The game sees the
throw spring 2 frames after a press and the release action 1 frame after the
release, so the game lets go about 3 to 4 frames after the opening. The press
of the click always comes after a frame with the button up, so the game sees a
new press. The click is not a press on something, so it needs no pixel in view
and no fresh press, and a grab of the world does not hold it back. The click
gives no haptic pulse. The game's effect 46 (the release) gives one.

**The throw speed.** `HandDirectGripOpened` latches the motion at the moment
the grip opens: the direction of the reach point and the real speed of the
grip, as the mean over the 80 ms before the opening (`kThrowWindow`). The
release action of the next 0.3 s uses the latch once, through `PrepareThrow`
(see [throw.md](throw.md)). A release with no latch, for example from the
trigger, takes the 80 ms before the release.

**The cases.**

- **A grip on nothing** is the game's right press (Pass). It keeps the camera
  mode of both buttons. With `grab_hand=2` it grabs the world.
- **A take from a pile** (the food or the wood of a store, a field, a fish
  farm): the grip is the button while the take runs (the fact `take` of
  `GripStep`). A closed grip collects. The opening stops the take, latches no
  throw and sends no click. See "The bulk take".
- **The Tug** (a tree or a rock): the button stays down while the grip is
  closed, as with the mouse, so the pull works. An opening ends the pull. When
  the object comes free into the Hand, the grip carries it.
- **The Creature's leash** puts the Hand in the Tug state, so the grip is the
  button: closed holds the leash, open lets go of it.
- **A miracle in the Hand** (the Grain state) is not "holding". The grip is
  the button, and what a close and an opening do depends on the kind of the
  miracle (see [miracles.md](miracles.md)). A one-off seed that the Hand
  carries is an object, so the grip carries it.
- **The Creature** (state 7) and every other state: the grip is the button.
- **A villager over the Creature or over a building:** the opening is the
  game's own drop click at that point, so the game does there what a mouse
  drop does.
- **A drop outside the player's influence:** the game keeps the object, as for
  the mouse, and the phase is OpenHeld. The table and the God view still let a
  throw go from the last point inside the influence (`tabletop.md`). Close the
  grip to carry the object again, and open it inside the influence.
- **A land drag while the grip opens:** the click waits until the trigger is
  up, because both buttons together are the game's camera mode and not a drop.
  A close while it waits carries the object again.
- **A dip of the squeeze in the first 300 ms** does not open the grip
  (`grip_press_guard=1`). In the plain game a press of the Action button
  that ends in less than 225 ms is a click: the game taps the object in the
  pick record, and a rock splits (`Rock::InterfaceTap` `0x006E7480` calls
  `Rock::SplitInTwo` `0x006E7560`) [V]. A real hand that jerks away squeezes
  less for a frame or two, for example 1.00, 0.92, 0.59, and the fast fall of
  `grip_fast_open` read that as an opening. The press then ended early and
  the rock broke, with the grip still closed. From the close of the grip, and
  while the machine is in the Pass phase (the pick up is not over), only a
  value below 35 percent opens the grip (`GripPressGuarded` in
  `controller_math.h`). The guard lasts at most 300 ms. A carry has the fast
  fall again, for the throw. The same guard acts on a tree in the ground, a
  villager and a pile: they all take their press in the Pass phase. A close
  and an opening below 35 percent in less than 225 ms is still a click, as
  with the mouse. `grip_press_guard=0` gives the fast opening at each time. With
  `test_keys=1`, Ctrl Shift Home turns the guard off and on while the game
  runs.
- **The grip opens before the object comes** (during the pick up of an
  object at rest): the take press ends at once, and the game stops the pick
  up, as for a short mouse click. If the object still comes within 350 ms,
  the machine lets it go with a click. The click waits until the game has
  the object in its Hand, because before that it is a press on the object.
  An object that moves is in "The press of the game".
- **The mouse takes the Hand** during a carry: the grip is the button again,
  and a closed grip must open before it presses. A carry counts as a held
  controller button for the pointer owner rule (`ControllersGripCarries`, read by
  `ChooseOwner` in `pointer_map.cpp`), so a bump of the mouse does not take
  the pointer then. While the controller holds no button, a real mouse event
  reaches the game unchanged (`h_SetButtons`).
- **A second object while holding:** not possible with the grip, because the
  grip carries with no button. The game may allow more than one object in the
  Hand (`IsSpaceInHands`). Not tested.

**The log.** Tag `grip`: each change of the machine, with the facts of the
Hand (the Holding state, the pick lock, whether the game has the object in
its Hand, whether the physics of the game has it, and the throw spring). The
line `the pick up is over` and the two lines of a press that the mod held
say how long the button was down for the game, and how old the press is on
the clock of the game. A button that goes up while a release is a click has
its own line ("so it is a click"). Tag `hand press`: a press on an object
that the physics of the game has says so. Tag `rock split`: each rock that
splits, with the cause (the tap of the game, a hard impact, or a different
call) and the age of the last press. Tag `hand throw` adds the time between
the opening and the release. Each report window counts the carries, the
openings, the clicks, the waits, the presses that the mod held and the
presses that were a click on an object that moves.

**Tests.** The math checks (`PressHoldChecks` in `grip_checks.h`) run a model
of the game's rule with the machine: the same press at 60, 72, 90 and 120
frames a second, at 20 times in a game turn. A closed grip and a press of 50
ms on an object that moves are never a click with `grip_press_hold=1`, and
each one is a click with 0. A game turn of 180 ms and a press on an object at
rest are there too. Desktop runs: `throw_queue.ps1 -Runs
dropnear,tossup,dropshort` in `bwvr/tools/desk` closes the grip on
a rock in its fall, on a rock that the hand threw up, and for 50 ms on a
rock in its fall. `-More 'catch_at_once=0;grip_press_hold=0'` gives the
click of the game, and `-Fps 90` a run at 90 frames a second. The recipe
`landgrip`, for a rock on the land 0.15 s after its first contact, does not
reach its state (`bwvr/tools/desk/README.md`, "Known limits of the
recipes").

**Needs the headset.** Whether 3 to 4 frames from the opening to the release
feel at once. Whether the window of 80 ms before the opening gives the throw
the player meant. Whether a still hand sets the object down where it is.
Whether a catch with a real hand takes the object each time: a real grip
closes over some frames, and a real hand moves with the object.

### A catch at once

A press of the grip on an object that moves puts the object in the hand at
once (`catch_at_once=1`, the default). "Moves" is the test of the game: its
physics has the object (bit `0x40` of `Object+0x24` is set and bit `0x10` of
`Object+0x0A` is clear), so the object falls, flies or rolls. An object at
rest keeps the press of the game, and a short press on it is a click.

**What the game does [V].** The press puts the object in the Hand of the
picture (`fn_0046DC30` writes `CHand+0x4904`), and the Hand is in the Holding
state on the next frame. The step of action 13 (`fn_005D5250`) then only
waits until the press is 225 ms old ("The press of the game"). In that time
it changes nothing: not the hand mode, and not the pick lock. Then
`fn_005D2800` asks the object three things (the table slots `+0x6FC` with
the player and `+0x180`, and the slot `+0x714` when the land under the Hand
is outside the influence), sends packet `0x13`, starts action 7 and sets the
pick lock. When that function refuses, the step clears the pick lock, resets
the action and lets the object go (`CHand::ThrowObject`).

The game takes the object out of its physics in a game turn after the
packet. Until then the physics moves the object. `PhysicsObject::DrawAll`
`0x00646DE0` draws each object of the physics with the matrix of its mesh as
it is then. For an object that the Hand threw or let fall, the flight of the
mod writes that matrix on each frame ([throw.md](throw.md), "The picture
between the steps"). So the object moves on for 0.25 to 0.35 s, and then it
is in the Hand in one frame.

**What the mod does.** The catch starts at the press, when the game put an
object that its physics has in the Hand of the picture
(`ThrowPhysicsCatchPress` in `throw_physics.cpp`). It has three parts:

1. **The game sends its pick up at once.** A hook on the step of action 13
   (`h_PickStep` in `hand_direct.cpp`) gives the step a press that is old
   enough. For that one call the clock and the game turn of the press
   (`GInterface+0x454` and `+0x458`) are 225 ms and 3 game turns back
   (`catch_math::OldPress`), and then they have their values again. So the
   step of the game calls `fn_005D2800` on the first frame after the press,
   with each test of that function, and the game sends its own packet. The
   step then does not read a release as a click, so no press on an object
   that moves taps it, and no rock splits.
2. **The body stays where it is.** While the Hand of the picture has the
   object and the physics of the game has it too, the hook on `fn_007FE260`
   does not run the step of that object. It gives the game 1, the answer of
   a step that moved (`CatchStep`). So the body does not fall on, does not
   hit the land or a different object, and does not come to rest in that
   time. The contact force of a step that did not run does not go to the sum
   of the turn (`PhysicsObject+0x0C`), so the object gets no impact from it.
3. **The mod draws the object in the Hand.** The flight of the mod does not
   draw the object (`DrawFlights`). `PlaceHeld` in `hand_exact.cpp` places
   it as it places each object in the Hand. For a catch it starts the settle
   of 0.12 s at the place and with the axes that the picture had before the
   press (`ThrowPhysicsCatchFrom`), and not at the place that the game
   writes at the Hand's point. So the object does not jump: it stops, and it
   goes to its place in the Hand in 0.12 s, in the frame of the hand. While
   the physics of the game has the object, the mesh has the place of the
   body again before `DrawAll`, on each frame of the desktop runs [I: which
   code of the game writes it there]. So the mod writes the matrix that the
   draw of the Hand left (`HandExactHeldMatrix`) again before `DrawAll` in
   each eye pass (`CatchFrame`), and `PlaceHeld` does not take the axes of
   the body as new axes of the object.

The catch ends when the game took the object out of its physics. From then
on the object is a held object as each other: the grip carries it, and the
opening lets it go with a throw or a drop.

- **The Hand lets the object go before that** (the game refused the pick
  up): the catch ends, the picture is at its place of the press again, the
  body moves again from there, and the log says so.
- **The game does not take the object in 3 s:** the catch ends, and the body
  moves again.
- **A fetch from a distance** of an object that moves is a catch too. Parts 1
  and 2 act. The glide of the fetch draws the object from the frame of the
  press ("The glide"): the matrix that the mod writes before `DrawAll` has
  the point of the glide.
- The mouse keeps the pick up of the game: the mod acts only while the
  controller places the Hand. A multiplayer game keeps it too.
- The catch needs the hooks of the real flight (`throw_law=2` at the start of
  the game).
- With `hand_hold_palm=0` the mod does not place an object in the Hand. The
  game then sends its pick up at once and the body stays where it is, but the
  mesh has the place of the body until the game takes the object [I: from the
  code, not run].

`catch_at_once=0` in `bwvr.ini` gives the pick up of the game: the packet
goes out 225 ms after the press, and the object moves on until the game
takes it. With `test_keys=1`, Ctrl and Shift with Backspace go between 1 and
0 while the game runs. The pointing controller gives a long pulse for 1 and
a short pulse for 0, and the log has a line. A catch that runs keeps its
value.

**The log.** Tag `hand catch`: the value at the start, one line at each
press on an object that moves, with the place of its picture, and one line
when the catch is over. That line has the numbers of the picture
(`catch_math::Track`): the time from the press to the first frame in which
the mod drew the object in the Hand, how far the picture went and came down
before that by the motion of the object, and the largest move of the picture
from one frame to the next. It also has the age of the press when the game
sent its pick up, the time and the game turn in which the game took the
object, the count of the steps that the mod held back, and the count of the
frames in which the mod wrote the matrix of the Hand again. The numbers are
there with each value of the key. With `throw_log=2` each frame of a catch
has a line `c` with the place of the picture.

**Tests.** The math checks (`CatchChecks` in `catch_checks.h`) make the
press old for the rule of the game with a clock and a game turn that wrap,
and run one catch at 60, 72, 90 and 120 frames a second, at 20 times in a
game turn. With the catch the object is in the Hand from the frame of the
press, it does not fall, the largest move of a frame is the settle, the
game sends its pick up on the first step, and a press of 50 ms is no click.
With `catch_at_once=0` the same model falls 47 cm or more and then jumps.

**Desktop runs.** `throw_queue.ps1 -Runs dropnear,tossup,dropshort,catchthrow`
in `bwvr/tools/desk` closes the grip on a rock in its fall, on a rock that
the hand threw up, for 50 ms on a rock in its fall, and on a rock in its
fall with a step of the hand at the opening. `-More 'catch_at_once=0'` gives
the pick up of the game, and `-Fps 90` a run at 90 frames a second. `-Runs
catchshot` takes eye pictures of the frame of the press and of 3 frames
later. The numbers are from the line of the log of each catch, at 90 frames a
second and at the frame rate of the rig (53 frames a second):

| | `catch_at_once=1` | `catch_at_once=0` |
| --- | --- | --- |
| The first frame with the object in the Hand | The first frame after the press: 7 to 9 ms after it at 90 frames a second, 14 to 15 ms at 53 | 242 to 353 ms after the press |
| The way of the picture before that, by the motion of the object | 0.0 cm | 11 to 34 cm. A rock in its fall comes down 28 to 33 cm |
| The largest move of the picture in one frame | 0.6 to 4.8 cm, in the settle. For the rock in its fall, with a way of 26 cm to its place in the Hand, it is 3 cm at 90 frames a second and 4.8 cm at 53 | 45 to 49 cm for a rock in its fall, in the frame in which the game takes it |
| The game sends its pick up | On the first step, with a real press of 0 to 16 ms | 225 ms after the press or later |
| The game takes the object out of its physics | 31 to 131 ms after the press, in game turn 1 or 2 | 242 to 353 ms after the press, in game turn 3 or 4 |
| A rock that splits | None | None, with `grip_press_hold=1` |

With `catch_at_once=1` the mod held the 20 steps of one game turn back in
each run in which a game turn came between the press and the take, and the
body of the rock did not move in that turn. It wrote the matrix of the Hand
again on each frame in which the physics had the object, 2 to 8 frames a
catch. After the catch an open grip with a still hand lets the rock fall
from its place in the Hand, and a hand that steps 15 cm in 3 frames throws
it from there at 3.4 m/s of the room. In the run with the grip of 50 ms the
game has the rock 50 to 97 ms after the press, and the open Hand lets it go
then. A tree in its fall is in the Hand in the same way (`-Runs treedrop`).

`-Runs catchfar` fetches a tree from a distance 1.0 s after its release,
while the physics of the game still has it. With `catch_at_once=1` the glide
draws the tree from the first frame after the press. The largest move of a
frame is 5.5 cm, which is the first frame of a glide of 0.5 m in 0.32 s, and
the game takes the tree 42 to 54 ms after the press. With `catch_at_once=0`
the tree stays at its place for 320 ms. The game takes it then, and the tree
is 52 to 54 cm nearer in one frame, at the end of the glide.

Not shown on the desktop: a catch of a rock that touches the land. The
recipe `landgrip` does not reach that state (`bwvr/tools/desk/README.md`,
"Known limits of the recipes"). For such a catch the step of the body does
not run, as for each catch, and the game adds no contact force of that step
to the sum of the turn (`0x00645FEE` adds the force of a step with a contact
after the call of `fn_007FE260`, and the hook takes that force off before).


**Needs the headset.** Whether the object is in the hand at the press with a
real hand, which closes over some frames and moves with the object. Whether
the way of 0.12 s to the place in the Hand looks right when the object was
15 to 23 cm from the finger tip at the press. Whether the throw after a
catch is the throw that the player meant.

## One point for each interaction

Each press acts from the Hand's point, where the glow is: the tip of the real
index finger at rest. The tip is the right point for a real hand. A person
sees the tip and aims with it, and the hand hides its own palm. A touch is a
move of the finger tip. The land stops the point, so a point at the palm would
keep the fingers 9 cm in the land before the palm comes to a villager. A
villager in the God view is 1 cm wide, and that needs a fine point.

| Interaction | Where it takes its place from |
| --- | --- |
| The glow | The Hand's point (`g_target`) |
| The pick up of an object, the take from a pile, a scroll, a house (`Choose`) | The nearest body within `hand_pick_cm` (4 cm of the room) of the Hand's point. For an object that the Hand can hold, the reach is `hand_wide_pick_cm` (15 cm) when no object that a press acts on is nearer ("The wide pick"), and up to 8 cm more while the object moves (`catch_moving`) |
| The Creature | The capsule of its body about the Hand's point (see [creature.md](creature.md)) |
| The grab from a distance | The aim ray of the pointing controller. It is off while an object is near the Hand's point |
| A held object | The palm of the mod's hand (`overlay_math::HoldCenter`) |
| The end of the leash at the Hand | The palm of the mod's hand |

No interaction uses the wrist or the grip pose. Pure math is in `near_math.h`.

### The pick at the glow

After the first eye pass, `Choose` measures each candidate that a press of the
grip acts on (`PressKind`):

| Kind | The test | What the grip does |
| --- | --- | --- |
| hold | `Holdable`: the tests of `PlaceObjectInMagicHand` | The pick up |
| take | `TakeValid`: slot `0x6CC` gives 1 | The bulk take |
| tap | `Tappable`: slot `0x740` (`InterfaceValidToTap`) gives 1, slot `0x180` gives 0, and the object is interactable | The game's tap |
| Creature | slot `0x34` (`IsCreature`) | Not a choice of `Choose`. See "One target" |

**The body** of an object is an upright cylinder (`BodyGap` in
`near_math.h`). Its base is the object's point, the translation of its mesh.
Its radius is the game's 2D radius (slot `0x64`, `Object::Get2DRadius`
`0x00638180`: the larger of the two half sizes of the box across the land
times the scale [V]). Its height is `Object::GetHeight` `0x00638120` (slot
`0x42C`): the height of the mesh times the scale. `Object::GetBoundingSphere`
`0x00637730` puts the middle of an object half of that over its point [V]. An
object with no height gets two radii.

**The gap** is the distance from the Hand's point to the body. It is 0 inside
the body. The pick reaches `hand_pick_cm` of the room (4 cm).

### A scroll, a house [V]

A scroll is a `ScriptHighlight` (vtable `0x0094228C`). Village rules for
scrolls are in [village.md](village.md). The facts that the pick needs:

- The button is the Action button: the right mouse button, the grip. The right
  press with an empty Hand is `FUN_005D1330`. For the object of the next pick
  record it tests `ValidForPlaceInHand` (slot `0x6FC`), and then
  `InterfaceValidToTap` (slot `0x740`, at `0x005D150F`). Each of the two leads
  to `fn_005D1740`.
- `fn_005D1740` calls `fn_005D3930` for an object that the Hand cannot hold
  (`0x005D1833`). With no leash on the Hand that calls `fn_005D38A0`, which
  sends packet `0x20` with the object when the land point of the record is in
  the influence (`GInterface+0x48`) or the object does not need it (slot
  `0x714`), slot `0x740` gives 1 and slot `0x180` gives 0 (`0x005D3904`).
- The handler of the packet, `fn_005DA650`, calls `InterfaceTap` (slot
  `0x744`, at `0x005DA6D8`). `ScriptHighlight::InterfaceTap` `0x0070AC70` sets
  the scroll active and starts its script.
- `ScriptHighlight::InterfaceValidToTap` `0x0070ADD0` gives 1 while the scroll
  has a script (`+0x78`). A scroll does not need the influence (`0x00709840`
  gives 0), and the Creature cannot be sent to it (`ValidAsInterfaceTarget`
  `0x00709800` gives 0), so the leash does not change the tap of a scroll.
- The trigger does not activate a scroll. The left press is `fn_005D1320`, a
  jump to `fn_005D1AB0`, which grabs the land.
- The hand mode chooser `fn_005D7F20` gives `0x12` for a scroll: the mode of an
  object that is interactable and that the Hand cannot take.
- A house is an `Abode`. `Abode::InterfaceValidToTap` `0x00406820` gives 1
  always. `Abode::InterfaceTap` `0x00406830` knocks on the house
  (`HowManyPeople::KnockKnock`).
- The scroll offers itself to the game's pick with
  `GInterface::SendInvisibleDrawCollision` (`0x0070A2D7`): a circle on the
  screen about its point. `ScriptHighlight::Draw` takes the middle of the box
  of the mesh through the matrix of the mesh (`0x00709ED7`) and gives it as the
  place of the scroll [V].

The game shows the tooltip of the tap ("Activate", then "Reminder" for a
scroll that is active), and the hint card shows it with the word of the grip.

## One target

At each moment the grip has one target or none. A press acts on the object
with the halo, and on no other object. `near_rule=2` gives this behavior.

### Why the game's own paths must not act [V]

Three paths of the game act on an object that is not at the Hand's point. The
mod closes each of them.

1. **The game's search at the palm.** The right press `FUN_005D1330` reads the
   object of the next pick record (`+0x400`). When the record holds no object,
   it calls `fn_005D39E0` (thiscall on GInterface, a `MapCoords*`, `ret 4`) at
   `0x005D13A1` and at `0x005D14A6`. `GLandscape::Draw` makes the box of the
   bones of the Hand's mesh (`0x005E46A5` to `0x005E47CD`) and casts a ray from
   the camera through the middle of that box (`fn_00802550`, `0x005E4848`). It
   keeps the land point that the ray meets in `0x00D1A3A0` (x) and
   `0x00D1A3A8` (z), and a flag in `0x00D2017C`. `fn_005D39E0` looks at each
   map cell within 5 units of that land point and keeps the nearest `Object`
   that is no `Fragment` and is less than 5 units away (`0x005D3C41`). It then
   takes the distance from that land point to the land point of the record
   (`+0x3F0`). When the object is not farther than that, it puts the object in
   the record (`0x005D3CE9`) and gives it to the press. With the mouse the two
   land points are a few units apart. With the real hand the land point of the
   record is straight under the Hand's point (`h_InterfaceStep`) and the other
   one is the land that the eye sees through the palm. With the hand 20 cm over
   the land they are far apart, so a press with an empty record would take the
   nearest object to the land behind the palm.
2. **The game takes the choice out of the record.** After the draw,
   `GInterface::PostDrawProcess` calls `GInterface::UpdateInterfaceCollide`
   `0x005D5A70` (`0x005CEABA`). It keeps the object of the next record only
   when its distance (`+0x404`) is less than the distance of the land under the
   pointer (`+0x3FC`), or when that land point is inside the circle of the
   object's box. A tree on lower land can be farther from the camera than the
   land under the pointer, and then the game clears the record.
3. **The game's pick at the pixel.** An object behind the pixel of the Hand's
   point (a scroll, a house, a pile) stays in the record at each depth, and a
   press would act on it.

### What the mod does

- **The record is the choice.** `Inject` gives each choice a distance that the
  game keeps (`near_math::RecordDistance`: at most half of the land
  distance). With no choice it takes each object out of the record, not only a
  holdable one. Before each interface step and before each right press,
  `AssertRecord` makes the record agree with the choice again. It acts only for
  the empty Hand in its Normal state, with no pick up, no take and no far fetch
  in work. It uses the choice only while the first word of the object is still
  the table of its class.
- **The search at the palm is off.** A MinHook hook on `fn_005D39E0` runs the
  game's function, so the log can say what it found. Then it takes the object
  out of the record again and gives the press no object. `palm_search=1` leaves
  the game's answer. The mouse, and each `near_rule` below 2, keep the game's
  answer.
- **The choice is made before the picture.** `HandGlowDraw` asks for the choice
  at the end of the world of the first eye pass (`HandDirectChooseNow`), when
  the draw has offered its objects. So the halo and the glow of a frame show
  the choice of that frame, and the press that the game reads next acts on that
  object.
- **The Creature is a choice too.** Its body is a capsule through its body bone.
- **The pick up is the game's.** The choice goes in the next pick record
  (`Inject`). The game then gives its own hand mode, its own tooltip and its
  own action.

### The rule of the choice

`near_math::BeatsTarget` and `TargetRule` order the bodies within
`hand_pick_cm` of the Hand's point. The point touches a body when the gap is
`hand_touch_cm` (1 cm) or less. A loose body is a tree, a forest, a building
or a bulk source: it is large, and its body is wider than what the player
sees.

1. A body that the point touches and that is not loose wins: the Creature, a
   villager, a rock, a scroll.
2. Then a scroll in the reach wins. A scroll is small, it floats, and the
   player goes to it on purpose. Of two scrolls the nearer one wins.
3. Then a loose body that the point touches wins.
4. Then the nearest body in the reach wins.
5. Then a scroll that is past the reach of the pick and inside the reach of a
   scroll (`scroll_reach_cm`, 10 cm, see [village.md](village.md)). It wins
   only when no other body is in the reach of the pick.
6. Last, a signpost that the Hand's point is not in (`hand_sign_last=1`,
   `near_math::SignOut`). A signpost is a scroll of the kind "did you know"
   (see "The cone and the choice"). The point is in it when the gap to its
   ball is `hand_touch_cm` or less, and then it comes under rule 1 as each
   other scroll. A signpost of rule 6 is the target only when no other body is
   in its reach, no object is in the wide reach, and the aim has no target at
   a distance:
   - Each body of the rules 1 to 5 comes before it: a miracle, the Creature,
     a house, a tree, a scroll of a quest.
   - It is no near object. It does not stop the wide pick of an object that
     is farther from the point, and it does not turn the grab from a distance
     off (see "Near objects win").
   - While the aim has a far target, it is no target
     (`near_math::SignGivesWay`). `Choose` reads the far target of the last
     frame. `FarSelect` runs with such a signpost as the choice, and it takes
     the choice away in the frame in which the aim gets its target. The halo
     at the Hand of that one frame can still be on the signpost.

   With `hand_sign_last=0` a signpost is a scroll of the rules 2 and 5.

In rule 1 and in rule 3 the smaller body wins: a villager in a forest, a rock
at a house. The size is the larger of the radius and half the height, in steps
of a factor of 2 (`SizeClass`). In one step the nearer middle wins, so two
trees with crowns that meet share the room between them. When the middles
agree too, the order is hold, take, tap. Each rule gives a full order, so the
choice does not depend on the order of the candidates.

### The bodies (`Measure`)

| Object | Body |
| --- | --- |
| A scroll or a sign (`IsScriptHighlight`, slot `0x48C`) | A ball: the middle of the box of its mesh in the world, and the half diagonal of the box, through the matrix of the mesh of this frame. The game draws a scroll smaller when the camera is less than 30 units from it (`0x00709E60` to `0x00709EA6`), and the matrix holds that scale. Each scroll that the game draws is a candidate |
| A tree (`IsAnyKindOfTree`, slot `0x478`, and no forest) | A trunk and a crown. The crown is the upright spheroid in the box: the game's 2D radius, and half the game's height. The trunk is a cylinder of a quarter of that radius (`kTrunkShare`), from the base to the middle |
| The Creature | A capsule through the body bone, from the land up to `1.1 S`, with the 2D radius. The touch in the Creature state adds `creature_touch_cm`, and the choice does not |
| A house and each other object that a press taps | Two cylinders, and the nearer one counts. The first is the cylinder from the point of the object, which also goes half its height below the point. The second stands about the middle of the box of its mesh in the world (`BoxMiddle`: slot `0xF8` of the `LH3DObject` gives the box, its middle at `+0x18` and its half sizes at `+0x24`) |
| Each other object | The cylinder from its point |

### The halo and the glow

The target at the Hand has the halo of a far target (`DrawNearHalo` in
`hand_glow.cpp`): gold, or red when the land under the Hand is outside the
influence and the object needs the influence. It is as large as the body, 35
percent and 1 cm more. For a tall body it is an ellipse along the image of the
upright axis (`near_math::HaloEllipse`). Its depth test is one radius behind
the middle of the body, so the object hides the middle of the disc. By
default the mesh of the target glows in that color, and the disc shows only
for a target whose mesh does not glow (`target_glow=1`, `hand-model.md`, "The
glow on the mesh of a target").

The fingertip glow shows only while a press of the grip has a target at the
Hand's point (`hand_glow_idle=0`). Its look comes from the choice, and not from
the game's hand mode, which is one frame late:

| What a press does | Look |
| --- | --- |
| The grip picks up, or takes from a pile | gold, bright, 1.7 cm |
| The grip activates: a scroll, a house, the Creature, a bubble, a leash | a paler gold, 1.5 cm |
| No target, a target that the influence stops, or an object in the Hand | no glow |

The game gives hand mode `0x12` also for the Temple and for an object outside
the influence that needs the influence, where a press does nothing. The glow
is off there. For hand mode `0x12` the object of the pick record decides
(`PressKindAtPoint`). The hint card shows the grip in gold and the trigger in
green. The trigger acts on nothing at the Hand's point, so the glow has no
green look. See [hand-model.md](hand-model.md) for the model of the glow.

### Near objects win

The grab from a distance is off while an object that a press acts on is near
the Hand's point. The objects are the four kinds of the table above, and the
Creature. A signpost that the Hand's point is not in is not such an object
(`hand_sign_last=1`, rule 6 of the choice): the aim keeps its far target with
the Hand's point beside a signpost. `Choose` keeps the smallest gap of the
others (`NearBlocks`):

- The far grab goes off when the gap is `far_grab_near_cm` or less (10 cm of
  the room).
- It comes back when the gap is more than `far_grab_near_out_cm` (15 cm).
- Between the two, the answer of the last frame stays, so a hand that rests at
  the border does not turn the halo on and off.

While it is off, there is no far target, no halo and no claim of the grip. The
grip goes to the game. The grip of the pointing hand is then no grab of the
world either (`HandDirectClaimFree`): the pick reaches only 4 cm, and a grip
that closes 7 cm from a tree must not move the world. The table hook offers
each object in view within the larger of the two distances and 10 units more to
the pick, so a near object is a candidate before the pixel is on it.

**The wide pick.** An object that the Hand can hold, past `hand_pick_cm` and
inside `hand_wide_pick_cm` (15 cm of the room), is the target of the grip when
no object that a press acts on is nearer to the Hand's point
(`near_math::WidePick`, the Creature counts as such an object). An object
inside the reach of `hand_pick_cm` is chosen by the plain rules and wins over a
wide one. A wide target shows as any near target does (the glow and the halo),
and its closing of the grip is the plain pick up. The log has one line for each
new wide target ("inside the wide reach"). A house, a scroll and a pile keep
their reach. The far grab stays off while the object is within 10 cm, so each
gap from 0 to 15 cm of a holdable object has a target, and past 15 cm the
object is a far target when the aim is on it. The wide reach is the same
from every side, so a hand at an object has the nearest object, and a far thing
behind it is not taken. An object that moves has a longer wide reach, by its
speed (see "A target that moves, floats or dies"). With `hand_wide_pick_cm=0` an object from
`hand_pick_cm` to 15 cm is no target. The test key Ctrl Shift Page Up goes
between 0 and the value (`test_keys=1`).

**The Creature has the wide reach too** (`hand_wide_creature=1`). The far grab
is off while the Hand's point is 10 cm or less from the Creature, so with a
reach of `hand_pick_cm` only, the Creature is no target from 4 cm to that gap.
With the wide reach the Creature is the target of the grip to 15 cm, when no
object that a press acts on is nearer. The press is the press at the
Creature: a short grip is the leash click, and a grip that stays closed
starts the Creature state. With `hand_wide_creature=0` the reach of the
Creature is `hand_pick_cm`.

**No band with no target.** The cone leaves out an object whose point is less
than 35 cm of the room from the Hand's point (`kFarMinHandM`). With the rule
on, the least distance of a far target is `far_grab_near_out_cm`
(`FarLeastMeters` in `near_math.h`), and never more than 35 cm. The score takes
35 cm for a nearer object. So each object is a target of the Hand, or it turns
the far grab off, or it is a target of the aim.

### What still acts from the pixel of the Hand's point

- **A bubble and a leash.** The game finds them at the pixel (`GInterface+0x408`
  and `+0x410`), and the right press acts on them before it reads the object.
  The glow shows the activate look for them.
- **While the Hand holds an object**, the mod does not change the record for
  the pick. A drop on a store or a villager goes to the thing in the record
  (see "An object in the Hand that goes onto a thing").
- **The far target** of the aim ray. It has its own halo.
- **The trigger** grabs the land under the pixel (`fn_005D1AB0`).

The held object, and the contact of the flat hand on the Creature, are at the
palm on purpose.

### The order of the targets

1. An object within 4 cm of the Hand's point: the grip acts on it.
2. An object that the Hand can hold, within 15 cm of the Hand's point, with no
   nearer object that a press acts on: the grip takes it (the wide pick). An
   object that moves has up to 8 cm more.
   A house, a scroll or a pile from 4 to 15 cm: no far target. The player
   moves the Hand to the object.
3. A far target in the cone: the grip takes it, collects from it or activates
   it.
4. The game's own pick at the pixel of the Hand's point, for what the mod does
   not choose (a bubble, a leash).

### The leash at the mod's hand

`GInterface::UpdateAllLeashes` `0x005D9130` runs once a frame, from
`GInterface::PostDrawProcess` (`0x005CEAB3`). For each leash that is on it
calls `GInterface::GetLeashStartPoint` `0x005D8EA0` (thiscall, an `LHPoint*`
and a bool, `ret 8`) with 0, and gives the point to the rope (`fn_00848970`, 40
nodes with their own physics) as its end. The other end is a bone of the
Creature. `GInterface::DrawAllLeashes` `0x005D9310` draws the rope through its
nodes (`fn_008491B0`) [V]. With the leash on the Hand (`GLeashStatus+0x24` is
null) and the local interface, `GetLeashStartPoint` gives the translation of
the first matrix of the Hand's pose array, `*(CHand+0x47F0) + 0x24`
(`0x005D90B9`): the wrist bone of the game's Hand. With the bool set it gives
`GInterfaceStatus+0xC8`.

The mesh of the game's Hand is hidden, and the player holds the rope in the
mod's hand. The wrist joint is about 7 cm behind the middle of the palm, and
with the fingers up it is below the hand. So a hook on `GetLeashStartPoint`
runs the game's function first, then writes the middle of the palm of the mod's
hand: the point where a held object touches the palm
(`overlay_math::HoldCenter`, 17 percent of the hand's length behind the
knuckles). `HandExactPalm` in `hand_exact.cpp` gives that point from the exact
root of the last compose, once a frame. The hook writes the point only when
each of these is true:

- The interface is the local one, the leash is on the Hand, and the bool is 0.
- The controller placed the Hand on the last frame.
- The Hand is in an exact state (Normal, Camera, Tug, Holding, Creature, Grain
  or PlayAnim), and the exact root is from this frame or the one before.
- The overlay drew the pointing hand, and the mesh of the game's Hand is
  hidden.

In each other case the game's point stays, and the log says why (8 lines at
most). The hook goes in only when the first 13 bytes of the function are the
bytes of the disassembly. `leash_at_palm=0` leaves the hook out. Tag `leash`
gives one line each time the leash comes on, with the palm, the distance from
the game's point to it, and how far that point is below it. No desktop run can
put the leash on from a new game.

### Keys of the pick and the target

| Key | Default | What it does |
| --- | --- | --- |
| `near_rule` | 2 | 2 is one target. 1 is the nearest body within the reach, with the game's pick at the pixel for an object that is not near, and the search at the palm. 0 gives a pick with a reach of 60 percent of the Hand's length, no tap choice, no rule of the near objects and no far touch |
| `hand_pick_cm` | 4 | The reach of the pick from the Hand's point, in cm of the room. 1 to 20 |
| `hand_wide_pick_cm` | 15 | The reach of the pick for an object that the Hand can hold, when no object that a press acts on is nearer, in cm of the room. 0 turns it off. 0 to 30. The test key Ctrl Shift Page Up goes between 0 and the value (`test_keys=1`) |
| `hand_touch_cm` | 1 | The point touches a body when the gap is this or less, in cm of the room. 0 to `hand_pick_cm` |
| `hand_sign_last` | 1 | 1: at the Hand a signpost that the point is not in comes last (rule 6). Each other body in its reach and each object in the wide reach comes before it, it does not turn the grab from a distance off, and it is no target while the aim has a far target. 0: a signpost is a scroll as each other scroll |
| `hand_wide_creature` | 1 | 1: the Creature has the wide reach of `hand_wide_pick_cm`, when no object that a press acts on is nearer. 0: the reach of the Creature is `hand_pick_cm` |
| `catch_moving` | 3 | The reach of the pick for an object that moves (see "A target that moves, floats or dies"). 3 puts the extra reach on the reach that the object has at rest, the wide reach too. Only a throw has the guard of `catch_grace_ms`, and that guard ends when the grip is fully open. 2 is the same, but the guard lasts for the whole of `catch_grace_ms`. 1 puts the extra reach on `hand_pick_cm` only, and each object that the Hand let go has the guard. 0 gives each object the same reach, moving or still. The test key Ctrl Page Up goes through 3, 2, 1 and 0, with a pulse that is shorter for each lower value (`test_keys=1`) |
| `catch_lag_ms` | 100 | The time that the speed of the object counts for, in ms. 0 to 500 |
| `catch_cap_cm` | 8 | The most extra reach, in cm of the room. 0 to 20 |
| `catch_grace_ms` | 600 | The object that the Hand threw has no extra reach for this time at the most, in ms. 0 to 3000 |
| `catch_rest_guard` | 1 | 1 holds the end of the physics of an object back while the Hand takes it, so a tree that comes to rest in that time stays a tree and goes into the Hand (see "An object that comes to rest while the Hand takes it"). 0 lets the game end the physics: the tree is then a dead tree on the land, and the Hand is empty. The test key Ctrl Shift Scroll Lock turns it off and on (`test_keys=1`) |
| `catch_at_once` | 1 | 1 puts an object that moves in the hand at the press of the grip: the game sends its pick up at once, the body of the object stays where it is, and the mod draws the object in the Hand (see "A catch at once"). 0 is the pick up of the game: it goes out 225 ms after the press, and the object moves on until the game takes it out of its physics. The test key Ctrl Shift Backspace goes between the two (`test_keys=1`) |
| `far_grab_near_cm` | 10 | The far grab goes off when an object that a press acts on is this near the Hand's point |
| `far_grab_near_out_cm` | 15 | The far grab comes back past this distance |
| `palm_search` | 0 | 1 leaves the game's search at the palm on |
| `hand_glow_idle` | 0 | 1 draws the glow also with no target and while the Hand holds an object |
| `leash_at_palm` | 1 | 1 ends the rope of the leash at the palm of the mod's hand |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The A/B keys**, with `test_keys=1`: the point key of the number pad (`.`,
`VK_DECIMAL`) changes `near_rule` between 2 and 1. The 7 key of the number pad
changes `hand_glow_idle`. The pointing controller gives a long pulse for the
new rule and a short pulse for the old one.

Test keys for the desktop runs only:

- `seek_forest=1`, `seek_tap=1`, `seek_scroll=1`: the word `seek` goes to the
  nearest forest, to the nearest object that a press taps and that the Hand
  cannot hold, or to the nearest scroll that a press taps. The log has a census
  of the candidates by class.
- `seek_plan`: a list of words with commas, one for each `seek` of the script.
  `scroll` puts the Hand's point in the middle of the ball of a scroll that a
  press taps. `air` puts the palm on the line from the camera to the base of
  the nearest tree, 25 cm of the room from that base, where a press has no
  target. `crown` puts the Hand's point on the axis of the nearest tree at 80
  percent of its height. `palm` moves the hand across the view, so that the land
  behind the palm goes onto the base of the tree nearest to it.
- `near_test_flips`: a list of frames with commas. `near_rule` starts at 1 and
  changes between 1 and 2 at each frame of the list, so one run shows both
  rules at the same places.
- `far_test_forest=1`, `far_test_scroll=1`, `far_test_tree=1`: only a forest, a
  scroll or a tree is a far target, and `aimfar` aims at one. The word takes a
  target from 17 cm of the room, because the tests run with the view zoomed
  out.

### The log of the pick

- Tag `hand press`: one line for each press of the grip that the game reads.
  It gives the target with its class, what the grip does to it and its
  distance, or "no target" with the distance of the nearest object that a press
  acts on (600 lines). One line when the game's search at the palm found an
  object: the object, its distance from the Hand's point, and whether the mod
  gave it to the press (60 lines).
- Tag `hand pick`: "the target is P (class): what the grip does", with the gap,
  whether the point touches the body, and the body. "no target". One line for
  each change (400 lines). "before the interface step the pick record held
  ..." when the game or the second eye pass changed the record (40 lines).
- Tag `far grab`: "off. The Hand's point is N cm of the room from P (class)"
  and "on again" (80 lines).
- Tag `hand glow`: "no glow now" with the reason, and "a halo of the target at
  the Hand is drawn" with the place and the two half axes in pixels (30 lines).
- Tag `hand`: each change of the Hand's state and of the hand mode (300 lines).

### Tests and open questions

The math tests are `OneTargetChecks` and the checks of `near_math.h` in
`near_checks.h`: the body of a scroll, a tree and a Creature, the gap to an
ellipse, the rule in each case (a scroll over a tree that the point is in, a
villager over the tree about him, a tree that the point touches over a villager
3 cm away, a rock at a house), all 720 orders of 6 bodies giving the same
target, the distance of the record, the band of 10 and 15 cm, the least
distance of a far target, and the halo. For rule 6: a signpost 3 cm from the
point wins over a miracle and over the Creature in the reach as a scroll, and
each of them wins over it with the rule. A tree that the point is in and a
scroll of a quest 8 cm away come before it. A signpost that the point is in
comes under rule 1. A miracle 6 cm away is the wide target with a signpost 9
cm away. A signpost of rule 6 does not turn the far grab off, and it gives
way while the aim has a target. Each order of five bodies gives the same
target.

Desktop runs (`hand_run.ps1 -Kind fixed -Save temple`) show the two keys:

- A signpost stands at the door of the Temple. The Hand's point is 8 cm from
  it, and the aim is on the Creature 30 cm away. With `hand_sign_last=0` the
  far grab is off, the signpost is the target of the grip, and the Creature
  has no halo. With the default the signpost is the target only until the
  aim is on the Creature. Then the Creature is the far target, and the log
  says "the signpost ... is no target now".
- The Hand's point is 6 cm over a signpost and 2 cm from a house
  (`seek_plan=go,scrolls,up`). With `hand_sign_last=0` the signpost is the
  target, and with the default the house is.
- The Hand's point is 11 cm from the Creature (`seek_plan=creature` and the
  word `slap`). With `hand_wide_creature=0` there is no target, and a grip
  does nothing. With the default the Creature is the target in the wide
  reach, and a grip of 6 frames puts the leash on.

**Not proven on the desktop:** a miracle in the wide reach beside a signpost
(the test miracle floats at the height of the Hand, so the point is in it), a
grip that stays closed on the Creature in the wide reach, the body of a tree and the game's height in the
game, the red halo, the Creature as a choice, the rule of the near objects in a
village, a scroll that wins over a tree that the point is in, and the hook of
the leash past its install. A place at 66 to 100 units a meter, the scale of
the headset, is not run.

**Needs the headset.** Whether 4 cm is the right reach: an object must be easy
to take, and an object at the palm must not be taken. Whether a scroll starts
with the glow on it. Whether the body of a scroll is where the scroll shows
(the line of the tag `hand pick` gives the gap). Whether 10 and 15 cm keep the
far grab out of the way near an object and do not turn it off too often in a
village. Whether 1 cm is the right touch distance. Whether the halo is easy to
see and not too strong, and whether the Hand with no glow is still easy to
aim. Whether the rope of the leash now comes out of the hand. Whether the paler
gold of the activate look is easy to tell from the gold of the take look.

## The bulk take

### What the game does [V]

- **The sources.** A store keeps its food and its wood in piles beside it:
  `PileFood` and `PileWood` objects (class `PileResource`). A `Field` and a
  `FishFarm` are sources too. For a source, vtable slot `0x6CC`
  (`ValidForLockedSelectProcess`, thiscall with the GInterfaceStatus, `ret 4`)
  gives 1. A `StoragePit`, a `Tree` and a `BigForest` give 0.
- **The hover.** Over a source, the hand mode chooser `fn_005D7F20` gives
  `0xD`. That needs the influence flag `GInterface+0x48`, or a source that does
  not ask for the influence (slot `0x714`). Outside the player's influence it
  gives `0x12`, and the press does nothing.
- **The start.** A right press runs `FUN_005D1330`. It reads the object of the
  next record (`GInterface+0x400`), tests slot `0x6CC`, `IsCannotBePickedUp`
  and bit `0x10` of `Object+0x24`, and asks for the take (`FUN_005D1A00`). The
  start packet runs `FUN_005DA950`. There slot `0x6D0`
  (`NetworkFriendlyStartLockedSelect`, `0x0066E710` for a pile) takes the first
  amount into a new `Pot`, puts the Pot in the Hand, and starts the stream of
  pieces (`PSysGlobal::StartMultiPickup` `0x0068F8C0`, see "The stream and the
  loop of a take"). Then `FUN_005DC110` writes the source to
  `GInterfaceStatus+0x3C` and 0 to `+0x40`.
- **Each game turn.** `GInterfaceStatus::Process` `0x005DC4E0` adds 1 to `+0x40`
  and calls slot `0x808` (`ProcessInInteract`, `0x0066E520` for a pile). The
  amount of a turn grows with the turns (`fn_0066CD30` and
  `GPotInfo::GetTicksToChangeOver`). The call asks for the loop of the take
  and sets its pitch (`fn_0068F930`), moves the amount from the pile to the
  Pot, and
  shows the count with `ToolTips::ForceToolTips(0xEEA, Pot+0x70)`. Text 3818
  is "Amount: %3.0f". The game shows no count when the help options of the
  profile are "No ToolTips" (see [ui-layer.md](ui-layer.md)). For a source that
  is not a Creature, a turn also ends the take when the Hand's own position
  (`GInterfaceStatus+0xC8`, set from the Hand at `+0xA4` in `fn_005DBC60`) is
  over land outside the influence (`0x005DC540`).
- **The end.** The release of the right button (`GInterface+0x39` bit `0x40`)
  runs `FUN_005D4830`, and `FUN_005D2210` sends packet `0x1C`. `FUN_005DAA10`
  then calls slot `0x6EC` (`StopMultiPickup` stops the stream and the loop) and
  writes 0 to `+0x3C`. The Pot stays in the Hand, in the Holding state. A click
  drops it as any object, and `Pot::InitialisePhysicsFromHand` pours it out
  (`StartMultiPutdown`).
- **The Hand.** During a take the Holding state asks slot `0x6A0`
  (`IsLockedInInteract`) of the source (`0x005B3EB3`) and holds the Hand over
  it: at the height of the land under the Hand plus slot `0x64C` of the source
  (`GetHeightForHandAboveInteractObject`, the call at `0x005B3F49`). For a
  pile and a field that slot is `Object::GetHeight` `0x00638120` (the height
  of the box of the mesh times the scale), and for a `FishFarm` it is 5 units
  (`0x0052C840`). The direct hand moves the Hand to the controller, as for any
  held object.
- **The forest.** A `Tree` gives no take. A press on it starts the Tug. A
  `BigForest` puts one new tree in the Hand
  (`BigForest::InterfaceSetInMagicHand` `0x004393C0`). The game has no bulk take
  from a forest. See "A forest gives a tree" and "Many trees from a forest".

### How a take ends in the game [V]

The game ends a take through three interface actions (`GInterface+0x44`, the
table of steps at `0x00D17DA8`, `0x30` bytes an action):

| Action | Its step | What the step does |
| --- | --- | --- |
| 4, the take runs | `FUN_005D4830` | On the release bit (`+0x39` bit `0x40`) it reads the object of the next pick record (`+0x400`, at `0x005D483A`), sets the pick lock (`+0x38 = 0x10`) and calls `FUN_005D49B0` |
| 5, the wait | `FUN_005D49B0` | When the object of the record has bit `0x10` of `+0x24` (a take holds it) and slot `0x6E4` gives 1, it sends the end packet `0x1C` (`FUN_005D2210`) and sets action 6. Else it sets action 5 and sends nothing |
| 6, the end | `FUN_005D4A10` | When the object of the record has no bit `0x10`, it clears the pick lock and calls `GInterface::ResetActionState` `0x005D29C0` |

Slot `0x6E4` is `Object::IsReadyForNetworkUnfriendlyEndLockedSelect`
(`0x00402810`) and gives 1 for each bulk source. The Creature has its own
function there (see [creature.md](creature.md), section 1.7). So a release ends the take only when
the next pick record holds the source of the take at that moment. The pick lock
then keeps the record as it is: `GInterface::PreDrawProcess` (`0x005CEA57`) and
`UpdateInterfaceCollide` (`0x005D5A9C`) do not change a locked record. With
another object in the record, the interface stays in action 5 with the pick
locked. The take goes on, no button does anything, and the Hand picks nothing.
`GInterfaceStatus::Process` ends this only when slot `0x808` of the source says
stop (the source is empty, or the Pot is full) or when the Hand is outside the
influence. It then resets the interface (`fn_005D1260`).

With the mouse the game holds the Hand over the source during a take, so the
record holds the source. With the controller the Hand is at the real hand, and
the pick of the mod does not fill the record while the Hand holds the Pot
(`Choose` and `Inject` need an empty Hand). The record then holds what the
game's own pick finds under the pixel of the Hand: a different pile of the
store (the piles of a store are less than 10 units apart), a villager, or
nothing. With no object in the record, `FUN_005D4830` calls `fn_005D1260` and
then reads through the null pointer at `0x005D4854` [V for the code, never seen
in a run].

**A source that is used up.** When a turn of the take leaves the source with
the deleted flag (bit 0 of the byte at `+0xA`, the flag that
`GameThing::IsAvailable` `0x00401810` reads), `ProcessInInteract` stops the
stream and the loop at once (`0x0066E6A1`). In the next game turn
`GInterfaceStatus::Process` finds that the source is not available, writes 0 to
`+0x3C` and resets the interface (`0x005DC522` to `0x005DC5A5`). The button is
still down then. The game deletes the pile of a miracle (`MagicWood`,
`MagicFood`) in this way when the Hand took all of it: the log of a headset
session has the deleted source in the frame in which the take ended.

### The stream and the loop of a take [V]

**The stream of pieces.** `PSysGlobal::StartMultiPickup(status, place, kind)`
acts only for the interface of the player. It starts the force feedback
effect 11 (food) or 12 (wood) of the game (`GInterface::StartImmersion`), and
`fn_006715F0` makes one particle system (`GJPSysInterface::Create` `0x0068F2F0`) and keeps it
in the object at `0x00D4E0E8`, field `+0x28`. The kind gives the type:

| Kind | Type | File of the game |
| --- | --- | --- |
| 0, food | `0x6B` | `Data\Spells\ZSpellFiles\SF_MultiPickUpFood.txt` |
| 1, poisoned food (from the name of the file) | `0x6C` | `SF_MultiPickUpFoodPoisoned.txt` |
| 3, fish (from the name of the file) | `0x6D` | `SF_MultiPickUpFoodFish.txt` |
| 2, wood | `0x6E` | `SF_MultiPickUpWood.txt` |

The call does not use the place of the pile. Each file has one emit rule of
the class `ER_MultiPickup` with `EmitRate` 8 (pieces a second) and `RaiseTime`
1 (second), and one maker of a piece: the mesh `MSH_I_OFFERING_WOOD` at scale
0.35 with a tumble rule, or a grain sprite of `S_SpriteSheet1.raw` at scale
0.5.

On each draw of the 3D engine, `PSysGlobal::DrawLoop` `0x0068F5E0` (from
`GGame::Process3dEngine`) calls `fn_006714F0`. That function gives the
particle system the place of the Hand of the picture (`CHand+0x78`) as its
gesture position, and then processes and draws it. The time step of the
process is `LH3DTech::g_game_time_inc`, and 1 ms at the least (`0x006715AB`).
So the rule runs one time in each walk of the scene: one time a frame with
one walk for both eyes, and two times with two walks, where the second walk
has a time step of 1 ms. The rule
`ER_MultiPickup::ModifyAtomCollection` `0x006A77C0` (thiscall on the rule, the
`AtomCollection`, `ret 4`) does all the work for the pieces, in world units:

1. It reads the gesture position (`PSysManager::GetCurrentGesturePosn`
   `0x00673600`).
2. It makes new pieces at the emit rate (`AtomCore::Create` `0x006737F0`). The
   start of a piece is the gesture position of that frame with the height of
   the land there (`LH3DIsland::GetAltitude`). The rule keeps the start in its
   own data of the piece.
3. For each piece of the collection (the first one at `AtomCollection+0x40`,
   the next one at `AtomCore+0x18`) it takes the age (`AtomCore::GetAtomAge`
   `0x00673CC0`). A piece older than `RaiseTime` (the rule, `+0x2C`) is
   deleted. For each other piece, `t` is the age divided by `RaiseTime`, and
   the place is `start + (gesture position now - start) * t`. The rule writes
   the place to `AtomCore+0x80` and the velocity (the change of the place
   divided by the frame time) to `AtomCore+0x34`.

So a piece has no path of its own. It is on the straight line from the land
under the place where the Hand was at its start to the Hand of this frame, and
the rule writes its place on each frame. About 8 pieces live at one time. The
stream comes out of the land under the Hand, and not out of the pile, also in
the plain game: there the game holds the Hand over the pile.
`PSysGlobal::StopMultiPickup` `0x0068FA50` deletes the particle system, with
its pieces.

The tumble rule of the piece of wood reads the velocity
(`AppearanceRuleTumble::ModifyAtomCore` `0x006A6200`, with `TumbleSpeed` 0.849
and `MaxTumbleSpeed` 6.2 in the file). The speed times `TumbleSpeed` is the
rate of the turn in radians a second, 6.2 at the most, and the larger one of
the x and the z of the velocity gives the axis. The grain is a sprite, and no
rule of it reads the velocity.

In a take at a distance the mod puts the pieces on a curve from the source to
the Hand. See "The stream of a take at a distance".

**The loop.** `PileResource::ProcessInInteract` calls `fn_0068F930(status,
kind, t)` on each game turn of the take (`0x0066E5F9`). For the interface of
the player it asks `GAudio::PlaySoundEffect` (the call at `0x0068F9E8`) for a
positional sound with no object and with no update of its place, at the Hand's
own position (`GInterfaceStatus+0xC8`): sample 44 of the in game bank for food
(`G_PickUpFood.wav`), and sample 98 for wood (`G_PickUpWood.wav`). The bank
gives both a loop with no end, play mode 2 (one copy at a time) and a range of
60 to 180 units. So the first ask starts the loop, and each later ask finds it.
Then `LHSampleSetPitch` sets the pitch in percent: `60 + 180 * t * t`, with
`t` the turns of the take divided by `GPotInfo::GetTicksToChangeOver`, 1 at
the most (`fn_0066CDE0`). The loop becomes higher while the take becomes
faster. `StopMultiPickup` stops both samples (`GAudio::StopPlayingSoundEffect`
`0x0042A210`). The start test of the game measures from the game camera
(`0x00429F16`): when the game camera is more than 180 units from the Hand, the
take has no sound. See [audio.md](audio.md), 6.4, for the God view.

### What the mod does

1. **The grip is the button during a take.** `HandDirectGripFacts` says that a
   take runs (`take`): `GInterfaceStatus+0x3C` holds an object, and its slot
   `0x6A0` gives 1. `GripStep` then makes the grip the game's button, one to
   one, as for a held mouse button. The opening lets the button go up, and that
   ends the take. That opening latches no throw and sends no click. After the
   take the Pot is in the Hand with the grip open (OpenHeld). The next close
   carries it, and the next opening lets go. A pile that runs out while the grip
   is closed gives a carry.
2. **The source is a choice.** `Choose` counts a bulk source (`TakeValid`, the
   tests of `FUN_005D1330`), and a holdable object wins a tie. So the pile goes
   in the next record, and the game gives `0xD`. The glow is gold over a source.
3. **The source stays in the record.** `KeepTakeSource` (`hand_direct.cpp`) puts
   the source of the take (`GInterfaceStatus+0x3C`) in the next pick record with
   `fn_005D5E40`, while a take runs and the controller places the Hand. It runs
   before each interface step (`h_InterfaceStep`) and after each eye pass. So
   the steps of actions 4, 5 and 6 find the source and end the take, as they do
   for the mouse. A take that is in action 5 ends on the next step.
4. **Nothing collects after the grip opens.** While a take runs, the grip is the
   button. When the button was up for `kGripTakeEndMs` (200 ms, two game
   turns) and the take still runs, `GripStep` sets
   `end_take` on each frame. `EndTakeNow` then acts before the next interface
   step. In action 4 it sets the release bit for that one step. In actions 5 and
   6 the record is enough. In any other action it sets the pick lock and calls
   `FUN_005D49B0`. If the take still runs 200 ms later, it calls the handler
   of the end packet (`FUN_005DAA10`), clears the pick lock and calls
   `ResetActionState`. The handler never runs in a multiplayer game.
5. **The take ends at once.** After the step that reads the release, the mod
   asks for a game turn at once, so the end packet runs on the next frame and
   not up to 100 ms later.
6. **The loop of the take sounds in the God view.** The game camera of the God
   view is 400 units from the player's place, so the start test of the game
   refuses the loop on each game turn. The ears decide that start
   (`audio_take_loop=1`, [audio.md](audio.md), 6.4).

For the player:

1. Put the Hand on the food or the wood of a store, or on a field. The glow is
   gold. At a distance, point at it (see "A bulk source at a distance").
2. Close the grip and keep it closed. The amount grows, and the count shows
   beside the hand.
3. Open the grip. The take stops, and the Hand holds the bundle.
4. Close and open the grip again. The Hand puts the bundle down, on the land or
   on the store (or onto another thing, see "An object in the Hand that goes
   onto a thing").

The mouse is as before: no change runs while no controller button is held.

### The log, tests and keys of the take

Tag `hand bulk`: the start of a take with the class of the source and the first
amount, the amount of each of the first 5 turns and then of each tenth turn,
the end with the amount in the Hand. During a take, each change of the next
pick record (the object, and whether it is the source), of the pick lock and of
the interface action. "The next pick record held X, not the source" each time
the mod puts the source back. "The take still runs with the grip open, so the
mod ends it", with the method. Each report window counts the takes, the frames
of a take, the pumps where the grip was the game's button, the steps where the
record held the source, the steps where the mod put it back, and the takes that
the mod ended. Tag `grip`: "the grip opens during a bulk take" and "the bulk
take still runs after the grip opened". Tag `hand pick` says when the choice is
a bulk source, and whether it is inside the player's influence.

The math tests are `BulkTakeChecks` and `TakeRuleChecks`
(`test/controller_math/grip_checks.h`): the button is down for all frames of a
take with no carry, the opening lets the button go up with no throw and no
click, the bundle stays (OpenHeld), the next close and opening drop it, a pile
that runs out with the grip closed, a short take, a take that still runs 18
frames after the opening gets `end_take` on each frame, a close during such a
take collects again, a take during a carry gets no button, and the mouse's take
is never ended.

Test keys of `bwvr.ini`, for the desktop runs only:

- `seek_bulk=1`: the word `seek` goes to the nearest bulk source within 2 m of
  the room, and the log lists the sources and the classes near the Hand. With
  `aimfar`, the word aims at the nearest bulk source in place of a holdable
  object.
- `bulk_test_influence=1`: the player's influence is 1 while the pick has a bulk
  source, while a take runs and while the Hand holds an object. The store of
  Land 1 is outside the influence for minutes after a new game.
- `bulk_test_no_keep=1`: the source does not go back in the record, and the mod
  ends no take. This shows the take that does not end.

**Not tested:** a field and a fish farm, a store inside the real influence,
and a take with the hand pulled away from the store. A desktop run has the
loop of a take of wood ([audio.md](audio.md), section 11).

**Needs the headset.** Whether the count reads at the hand. Whether the loop
of the take sounds, and how loud it is beside the other sounds. Whether the
stream of pieces reads well. Whether the Hand stays at the controller during a
take. A short squeeze on a pile must stop when the grip opens.

## The grab from a distance

The code is in `far_grab.cpp` (the states, the claim, the pull and the glide),
`hand_direct.cpp` (the cone, the choice, the pick and the throw),
`controllers.cpp` (the ray, the claim and the fake words), `overlay.cpp` (the
tether) and `hand_glow.cpp` (the halo). The pure math is in
`controller_math.h`.

### What the player does

1. Point the hand at an object that is out of reach, and at most 2 m of the room
   from the head. A gold halo shows around the object, and the controller gives
   a short tick. The halo is red when the land under the object is outside the
   influence. Then the Hand cannot take the object.
2. Close the grip. A thin gold line goes from the Hand to the object (the lock),
   and the controller gives a firmer tick.
3. Pull the hand toward the head. The object glides to the Hand in 0.3 to 0.5 s,
   and the grip carries it as it carries an object that the Hand touched. Open
   the grip to let go or to throw.

A grip that opens before the pull ends the lock and does nothing else. The
player never moves. Only the object moves. The Hand stays on the real hand. The
feature acts in the God view and on the table (play modes 3 and 4). It acts only
while the controller places the Hand, the Hand is empty in its Normal state, and
no grab of the world, gesture, teleport or trigger runs. It is off in a
multiplayer game (the flag at `0x00D46ABA` and the game mode at
`GGame+0x25017C`, 3 for multiplayer).

A pile of a store, a field and a fish farm are targets too, and they need no
pull. A scroll needs no pull either. A forest needs the pull.

### What the game allows [V]

- **The pick up has no distance test.** `fn_005D1740` (thiscall on GInterface,
  the object as its argument, `ret 4`, called at `0x005D1536`) checks `+0x24`
  bit `0x20`, `ValidForPlaceInHand` (+0x6FC), `IsCannotBePickedUp` (+0x180) and
  the influence (`+0x48`, or vtable `+0x714` returns 0). Then it puts the object
  in the next record (`0x005D17BD`), sets the pick lock (`+0x38 |= 0x10`),
  starts Immersion 4, and sets the Tug (`CHand+0x4908 = 1`) or calls
  `fn_0046DC30(obj, 1)`. `GInterface::PlaceObjectInMagicHand` `0x005DA6F0`
  (packet `0x13`) tests the same and `IsSpaceInHands`. It takes an object that
  is in the physics (bit `0x40` of `+0x24`): at `0x005DA7F4` it has a help line
  for that catch. No test of the water and no test of the motion stops a pick
  up. A pick makes a `Reaction` of type `0x10` (`0x005DA7DD`), so other
  villagers react.
- **Hand mode 9** (`fn_005D7F20`) needs the object of `+0x400` and the influence
  at the land point `+0x3F0`, or vtable `+0x714` returns 0 (`0x005D7FA2`).
  Vtable `+0x714` is `InterfaceMustBeInInfluenceForInteraction`.
- **The object of the record must be nearer than the land.**
  `UpdateInterfaceCollide` (`0x005D5A70`) keeps the object of the next record
  only when its distance (`+0x404`) is less than the land distance (`+0x3FC`).
  Else it tests whether the object stands over the land, and it clears the
  record when that fails. The land distance is where the ray of the Hand's pixel
  meets the land. A far object is behind that land. So the mod cuts the
  distance of the record to half of the land distance (`Inject()`), and the game
  keeps the object. It also writes the map point of the object of the next
  record to the land point `+0x3F0` (`0x005D5D09` to `0x005D5D2B`), so the
  game's own land point follows the object.
- **In the Holding state** the game writes the held mesh translation to the
  Hand's position plus the hang offset (`0x0046CA60` to `0x0046CA97`, the floats
  at `0x00D13F20`). In the Tug state it writes nothing. The Tug lasts 4 to 5
  frames for a far fetch, then the Hand is in the Holding state.
- **The influence does not read the land.** `Influence::CalculatePlayerInfluence`
  (`0x005CD170`) does not read the land. `MapCoords::IsWater` (`0x006035B0`) has
  two callers in the interface: `0x005D3A2F` (a search for an object near a land
  point when the record is empty) and `0x005D6A83` (a tooltip).
- **The physics floats an object over the sea** (`fn_007FD4D0`, from
  `0x007FD5A6`): the land under it is lower than 0.0001 and its center is lower
  than one radius. An object that floats does not come to rest, so it stays a
  physics object, and `PhysicsObject::DrawAll` draws it. The draw offers it to
  the pick as each other object. An object 4 radii under the sea leaves the
  physics, and the game deletes it.
- **The game has no highlight for the object under the Hand.**
  `ScriptHighlight` belongs to scripts, and `HandGlow` is the hand effect. The
  overlay draws the halo.

### The aim ray

The ray is the aim pose of the pointing hand in the world, as the teleport takes
it (`ControllersFarRay`, from `ControllersTeleportInput`). It has the turn and
the point of the aim preset (`aim_preset`) and the trim. See
[controllers.md](controllers.md), section 8, for the presets and the keys
`aim_preset`, `aim_pitch_deg` and `aim_yaw_deg`. A small object has a small
angle of its own, so a wrong aim leaves it at the edge of the cone.

### The cone and the choice

The draw offers only the objects whose circle holds the pick pixel. So the
screen test hook of the table (`h_ScreenTest`) also offers an object whose mesh
point is inside the cone, 4 degrees wider than the selection (12 degrees,
`g_far_offer_deg`). The distance 0 of that offer asks the mesh about the pick
pixel, so the game's own pick gets no false hit. The mesh of the locked target
is offered on each frame (`g_far_keep_model`).

After the first eye pass, `FarSelect()` scores the candidates. The pure rules
are in `far_choice_math.h` (the body, the balls, the ranks and the target that
stays) and `controller_math.h` (`FarConeScore`, `FarPickTarget`). Two keys pick
the rule: `far_grab_choice` (default 2) is how the angle is measured and which
class comes first, and `far_grab_stay` (default 1) is how the target of now
keeps the halo. With ranks, each candidate has a class (`ClassOf`): the first
class, no class, or the last class. `PickClassed` gives the target. The choice
of `PickRanked` serves the object target of a Creature command too (see
[creature.md](creature.md), section 4.3).

**What `te` is.** Each candidate has an angle `te` in degrees: 0 when the ray
is on it, and more when the ray is beside it. A new target needs `te <= 8`
degrees (`far_grab_cone_deg`), and the target stays while `te <= 14` degrees.
The plain score of a candidate is `te + 1 degree x log2(1 + meters from the
Hand)`, so of two equal targets the nearer one wins.

**The values of `far_grab_choice`.**

- **0, one point.** `te = max(0, t - atan(r / d))`, with t the angle off the
  ray to the base of the object (the mesh translation), r the `Get2DRadius` of
  the object and d its distance from the ray's origin. The height of the
  object does not count, and no class counts.
- **1, the body with a bias.** The angle is measured to the nearest point of
  the upright body of the object: the line from the base up to `GetHeight` (two
  radii when the game gives none), with the radius around it (`BodyScore`,
  which uses `AimAtBody`). A body covers at least 2 degrees of the view
  (`kMinCoverDeg`). A forest and a scroll keep the point. Your Creature has
  the body of its capsule (see "Your Creature" below). When
  the ray goes through two bodies, a small one comes first: the score adds 0.5
  degrees for each log2(1 + size), with size the larger of the radius and half
  the height in world units (`kSizeDeg`). A rock of 0.5 comes 1.2 degrees
  before a tree of 7.5. A Creature, an animal (slot `0x454` `IsAnimal` [V]) and
  an object of the class `Villager` (by the class name) score 3 degrees better
  than their angle (`kLivingBiasDeg`, `RankedScore`). So the score of the other
  object decides how far from a villager the aim can be: about 4 degrees
  against a tree on the ray, and about 3 degrees against a rock. This test of
  the class does not know a quest character, which is a `SpecialVillager`: he
  gets no bias, and a tree whose body the ray crosses wins when the aim is 1
  degree from him.
- **2, the body with ranks (default).** The angle is measured as with 1, with
  the least cover and the size term, and with no bias. A living thing in its
  cone is in the first rank, and so are a scroll of a quest and a miracle (see
  below): it comes before each thing that is not living,
  also before a tree, a rock or a pile whose body the ray crosses. The cone of
  a living thing is `te <= 4` degrees (`far_grab_living_deg`), which is 6
  degrees from the line of its body with the least cover. Of two things in the
  first rank the one nearest to the ray wins, by the angle to the line of the
  body with no cover. When no living thing is in its cone, the plain score
  decides among all candidates, so a tree and a rock are as easy to take as
  with 1. A living thing past its cone is a candidate of that plain order.
- **3, balls.** Each candidate is a ball with a size in degrees that comes from
  its class and not from its mesh (`BallScore`). The ball of a living thing is
  at the middle of its body and has a radius of 3 degrees
  (`far_grab_ball_living_deg`). The ball of each other thing is at its origin
  (the base of a tree, of a rock and of a building) and has 1.5 degrees
  (`far_grab_ball_other_deg`). `te` is the angle from the ray to the middle,
  less the size. No size term, no bias and no rank.
- **4, balls with ranks.** The balls of 3 and the ranks of 2.

**A living thing** for the values 2 to 4 is each object of a class that has
`Living` as a base: a villager of each kind and age, a quest character, an
animal and a Creature. The test is the game's own `IsLiving`, slot `0x3C4`
[V]: `GameThingWithPos::IsLiving` `0x00402350` gives 0, and `Living::IsLiving`
`0x00417460` gives 1. A scroll of a quest is in the first rank too. The
character of a quest stands under his scroll, and the nearest of the two to
the ray wins.

**A miracle** that the Hand can take is in the first rank too
(`far_grab_miracle=1`): a one-off seed on the land, class `OneOffSpellSeed`
(`IsMiracleSeed`, by the class name). It is a bubble with a radius of 2.3
units: 7 cm of the room across at 64 units a meter. In its cone it comes
before a signpost and before each thing that is not living, and it keeps the
halo as a living thing does. With `far_grab_miracle=0` it has no rank, as a
rock. Quests of Land 1 and of Land 2 put such miracles on the land with a
signpost beside them.

**A signpost** is in the last rank (`far_grab_sign=1`). A signpost is a
scroll of the kind "did you know": the wooden sign with a text of the help
system. The test is the game's own, `ScriptHighlight::IsDidYouKnow`
`0x0070AC20`: the info of the scroll is entry 1 of the table at `0x00D96390`
[V]. The log names that type "silver". A signpost takes the halo in two cases
only:

- No other candidate is a target.
- The aim is on the signpost and on no other candidate. The aim is on a
  candidate when its `te` is 0: the ray is in its body, with the least cover.

In each other case the other candidates decide, as if the signpost was not
there. So a signpost has no rank in its cone, it does not keep the halo past
its body, it has no bias in the score, and a tree whose body the ray crosses
comes before it. For the target of now the aim stays on it to 1 degree
(`kLastStayDeg`), so a hand at the edge of a body does not move the halo to
and fro. With `far_grab_sign=0` a signpost is a scroll of the first rank: it
wins over a miracle and over a tree in its cone, and it keeps the halo to 7
degrees. The two keys act only with ranks (`far_grab_choice` 2 and 4).

**Your Creature** is a living thing, and the aim measures it by its body
(`far_grab_creature=1`). The body is the capsule of the choice at the Hand
(`CreatureBody`): the line from the land under its body bone up to `1.1 S`,
with its 2D radius, and the least cover of 2 degrees. `FarSelect` takes the
Creature from the game (`GPlayer+0xA4C` of the local player), so it is a
candidate also when the draw did not offer it. With `far_grab_creature=0` the
measure is the point of its mesh less the angle of its 2D radius, and the
Creature is a candidate only when the draw offered it. The point of the mesh
is at the feet of the Creature, and the draw offers an object by that point.
So the aim on the head of a Creature of size 0.22, 44 cm from the Hand, is 4.3
degrees from it by the point: that is past the cone of a living thing, and a
tree behind the Creature has the halo. For a Creature of size 0.5 at 70 cm
the same aim is 6.8 degrees away. By the body both are 0. The Creature needs
`far_leash=1`, and a leash click that the game takes now (`LeashKind`).

**Why the default is the body with ranks.** A bias is a part of the score. So
the score of the other object decides how far from a living thing the aim can
be, and a living thing of a class that the bias does not know loses to each
tree whose body the ray crosses. A rank does not compare scores, so the score
of the tree does not count. A ball at the origin of an object does not follow
what the player sees. At 30 units a
meter a tree is 44 cm tall in the room, and from 1 m its crown is more than 20
degrees from its base, so the ball holds only the foot of the tree. Balls with
no ranks also give a tie when the ray is in the ball of a tree and in the ball
of a villager, and then the one nearer to the Hand wins. The size of a ball is
an angle from the ray, because a length in the world is 4 cm of the room at 7.5
units a meter and 0.01 cm at 4000, and the shake of a hand is an angle.

**The target that stays, `far_grab_stay`** (`PickRanked`).

- **1 (default).** A living thing that has the halo keeps its rank while `te`
  is at or below its cone plus `far_grab_stay_deg` (4 + 3 = 7 degrees). In that
  time no thing that is not living takes the halo, whatever its score. So a new
  target needs the aim in the tight cone, and the target of now stays in the
  wide cone: a tree behind or beside a villager does not take the halo back
  when the hand shakes. A living thing in its cone takes the halo from a tree
  at once. A different living thing takes it when it is 1 degree nearer to the
  ray (`kFirstSwitchDeg`). With the values 1 and 3, which have no ranks, the
  hold acts on a living target too.
- **0.** The margins of `FarPickTarget` only: the target stays while `te <= 14`
  degrees, unless a different object has a score 2 degrees lower. With ranks, a
  living thing has its rank only in its cone, so an aim at the edge of the cone
  moves the halo between a villager and a tree.
- Among things that are not living, the rule is the same with each value: a
  different object takes over only when its score is 2 degrees lower.

`far_grab_choice=1` with `far_grab_stay=0` gives the choice with the bias and
no hold.

The log line of each new target says why it won and names the second best
("Why: a living thing in its cone comes before each thing that is not living.
The second one is ... (Tree) at 0.0 degrees, score 2.42 against 2.92"). A
target that keeps the halo past its cone has one line ("the target ...
(Villager) keeps the halo (far_grab_stay=1)"). The reasons of the classes are
"a miracle in its cone comes before a signpost and before each thing that is
not living", "the aim is on the signpost and on no other target" and "a
signpost comes after each other target". The last one shows only when the
signpost has the halo with `far_grab_sign=0`. The line names a signpost ("a
signpost: the grip activates it"), and the second one too. Each time your
Creature becomes the target by its body, one line has the body, the angle to
it, and the angle that the point of its mesh gives ("your Creature ... is the
target by its body", 60 lines).

**The rules of each value.**

- An object behind the ray is out. An object less than 0.35 m of the room from
  the Hand's point is a near object, and it is out too (`kFarMinHandM`, or the
  `FarLeastMeters` of "Near objects win"). A near object of `Choose()` always
  wins, and then there is no far target.
- The object must be holdable (`Holdable()`), and the land must not hide it. The
  draw offers every object in the frustum, so the winner is tested against the
  land: 24 heights between the eye and a point half a radius over the base, with
  the last 5 percent left out (`FarVisible`). A hidden winner is out for that
  frame, and the next best is tested (12 tries at most). Only land higher than 0
  hides an object (`FarLandHides`): the sea does not, and an object that floats
  can have its point under the sea plane.
- The target is inside when the influence at its land point is positive, or when
  vtable `+0x714` returns 0.

**The range.** A far target is at most `far_grab_max_m` meters of the room from
the head (2.0). The distance is from the head, not from the chest anchor and not
from the Hand. It is measured along the floor: `FarFloorMeters` takes x and z of
the world and divides by the units in a meter. So the height of the land does
not count, and a player who leans toward an object brings it into range. The
head is the eye point of the aim ray (`ControllersFarRay`). The target of the
last frame stays to 15 cm more (`FarInRange`, `kFarRangeKeepM`), so a small move
of the head does not turn the target off and on. `far_grab_max_m=0` gives no
limit. An object past the range gets no halo and no lock.

**Classes of far targets.**

- A holdable object, including a single `Tree`. The fetch gives that object.
- A pile of a store (`PileFood`, `PileWood`), a `Field` and a `FishFarm`
  (`TakeValid`, the tests of `FUN_005D1330`). These start the game's bulk take
  at a distance, with no pull (see below).
- A forest (`BigForest`). The pull gives one new tree, and the glide draws it
  from the forest's point.
- A scroll in the cone (`IsScroll`, by the class name, and `Tappable`): the far
  touch (see below). A signpost is such a scroll, in the last rank.
- A miracle on the land (`OneOffSpellSeed`): it is a holdable object, in the
  first rank.
- The player's own Creature, for the leash and the stay: a short grip changes
  the leash, and a grip that stays closed makes the Creature stay, with no
  pull and no press of the game (`near_math::LeashClick`, `stay_math::Step`,
  the keys `far_leash` and `creature_hold_stay`, see
  [creature.md](creature.md)).
- No other object that a press taps. Else the aim would knock on each house that
  it crosses, and a press on the entrance of the Temple from 2 m would take the
  player into the Temple [I].

### The claim, the pull and the fetch

The states are Idle, Aim, Lock, Pull, Fetch, Take and Hold (`far_grab.cpp`).
The Hold state is the grip on the player's own Creature (see
[creature.md](creature.md), section 5.3).

- **The claim.** A grip that closes while the target is selected and inside the
  influence is held back from the grip machine and from the grab of the world
  (`FarGrabPumpGrip`, called by the pump before `UpdateGrab`). The machine sees
  an open grip. A grip that closes on a red target is not claimed, and the log
  says that the game would refuse it. A claimed grip that is still closed when a
  lock ends stays held back until it opens, so it never reaches the machine as a
  new press on the land.
- **The pull.** `p` is the unit direction from the grip at the lock to the head,
  in the room. After 60 ms of lock (`kLockSeconds`), `PullDetect` fires when the
  mean velocity of the grip over 60 ms has `v . p >= 0.8 m/s` and an angle of 55
  degrees or less to `p` (`far_grab_pull` sets the speed). It also fires when
  the grip moved 12 cm along `p`, and for a flick up with `v.y >= 1.0` and
  `v . p >= 0.4`.
- **Pull state.** For 2 frames (`kPullFrames`) the mod puts the object in the
  next record (`fn_005D5E40`, through `Inject()`, and it wins over `Choose()`
  for these frames). It writes the land under the object to `+0x3F0` in the
  interface step (`h_InterfaceStep`).
- **Fetch state.** The claim goes, and the grip machine presses the button. The
  press needs no pixel in view (`FarGrabPressing`), because after a pull the
  hand is near the head. The game then picks the object up as for any object.
  When `CHand+0x4904` is the object, the fetch is done. After 700 ms
  (`kFetchMs`) without it, the fetch ends and the grip stays held back until
  it opens. For a forest the fetch waits 1.5 s (`kForestFetchMs`),
  because the game makes the tree after 225 ms. The waits are real time, so
  a fetch does the same thing at each frame rate.
- After the Hand holds the object, the grip machine carries it as before, and
  the throw is the throw of [throw.md](throw.md).

**The timeline of the game for a fetch [V].** The press is at frame F. The pick
lock is set at F+2, and `+0x4904` is the object. The Hand is in the Tug state
(3) for 4 frames and in the Holding state (4) from F+7. The grip machine says
"the pick up is over" then. The exact Hand keeps the drawn Hand on the
controller in the Tug state, so the Hand does not jump to the object. The
position `+0x78` in the Tug state is not measured.

### A bulk source at a distance

A grip that closes on a bulk target goes to Pull at once. For 2 frames the
source is in the next pick record and the land under it is at `+0x3F0`, so the
game gives hand mode `0xD`. Then Fetch sends the grip as a press, and the game
starts the take (`FUN_005D1330` reads the record). When `GInterfaceStatus+0x3C`
is the source, the state is Take: the grip is not held back, and `GripStep`
makes it the button. The tether and the halo stay until the game ends the take.
A press that the game does not answer in 700 ms ends as a fetch does. The
rule of "The bulk take" applies: hold the grip to collect, open it to stop and
keep the bundle, close and open it again to drop.

On each game turn the game ends a take when the Hand's own position is over land
outside the influence. So the halo of a bulk source is gold only when the land
under the Hand is inside the influence too.

**A source that goes away while the grip is closed** (`far_take_carry=1`). The
game ends a take by itself when the source is used up, and it deletes the pile
of a miracle in that game turn ("How a take ends in the game"). The test that
the target lives (`FarTargetLives`) then tells the far grab that its source is
gone, before the far grab reads that the take is over. In the Take state the
grip machine has the grip, so the far grab does not hold the grip back then
(`controller_math::FarHoldsBack`). The machine sees the closed grip, and the
grip carries the bundle, as after a take with the Hand at the pile. The next
opening of the grip lets the bundle go. With `far_take_carry=0` the far grab
holds the grip back until it opens. The machine then sees an open grip in the
frame after the take, reads the bundle as an object that came after the
opening, and lets it go with its click while the real grip is still closed.
The same rule applies when a test key turns `far_grab` off during a take.

### The stream of a take at a distance

The plain game makes each piece of the stream at the land under the Hand
("The stream and the loop of a take"). In a take at a distance the Hand is
not over the source. So the mod puts the pieces on a curve from the top of
the source to the Hand (`take_stream=1`). A piece goes straight up out of the
source, then over to the Hand, and it comes down into the Hand at the end of
its life. The game makes and deletes the pieces as before: 8 pieces a second,
and each one lives 1 s. A take with the Hand at the source has the line of
the game. With `take_stream=0` each take has the line of the game, from the
land under the Hand.

**The hook** (`take_stream.cpp`) is on the rule
`ER_MultiPickup::ModifyAtomCollection`. After the rule of the game it writes
the place (`AtomCore+0x80`) and the velocity (`+0x34`) of each piece again,
from the age of the piece (`AtomCore::GetAtomAge`) over `RaiseTime`.
`FarGrabTakeSource` says that the far grab has the source of a take: from the
press on a bulk target to the end of the take. The game starts the stream in
the game turn of the press, before the far grab reads the take.

**The curve** (`take_stream_math.h`) is a cubic Bezier curve in world units:

| Point | Where |
| --- | --- |
| P0 | The top of the source |
| P1 | Straight over P0, at the top level |
| P2 | Half of the way to the Hand along the floor, at the top level |
| P3 | The Hand: the gesture position of the particle system (`PSysManager::GetCurrentGesturePosn` `0x00673600`), which is the Hand of the picture |

The top level is 0.25 of the distance along the floor over the higher one of
the two ends. Each measure is a share of the way, so the shape is the same
for a short way and for a long way. With the two ends at one height, the top
of the curve is 0.19 of the way over them. No point of the curve is lower
than the lower end, so the pieces do not go through flat land between the
source and the Hand. With the Hand straight over the source the curve is the
straight line up.

**The top of a source** is the point of its mesh plus the height at which
the game holds the Hand over the source in a take (slot `0x64C`, see "What
the game does"). The point is the translation of the mesh of the source (the
`LH3DObject` at `Object+0x40`, `+0x38`), and it is the base of the mesh. The
hook reads the point and the height on each frame.

| Source | Its top |
| --- | --- |
| `PileWood`, `PileFood` (also the pile of a miracle) | The point plus `Object::GetHeight`. The game draws a pile that is not full lower in the land (`PileWood::Draw` `0x0051BC40` adds the field `+0x84` to the height of the land) |
| `Field` | The point plus `Object::GetHeight` |
| `FishFarm` | The point plus 5 units |
| A store | It has no take. Its piles are the sources |
| A tree, a forest | No take and no stream ("A forest gives a tree") |

The hook reads the source only while it lives (its first word is the table
of its class, as at the selection). For a source that the game deleted it
keeps the last point and the last height.

**The velocity** that the mod writes is the velocity of the curve: the change
of the place in a second. The tumble rule of the piece of wood reads it. On a
way of some tens of units the speed is past the maximum of that rule, so the
piece turns 6.2 radians a second, in the direction of its way.

**Once a frame.** The rule runs one time in each walk of the scene. The hook
latches the top of the source and the Hand in its first call of a frame, and
each call of that frame uses the same curve. The place of a piece then
depends only on its age. In the second walk of a frame with two walks a piece
is 1 ms older, which is 0.001 of the way. One frame of a take has one more
call of the rule.

**The life of a piece** is 1 s for each length of the way (`RaiseTime`, the
rule, `+0x2C`), and 8 pieces start in a second (`EmitRate`, `+0x30`). So
about 8 pieces are on the way at one time, and they go faster on a long way:
a way of 0.6 m of the room has a mean speed of 0.6 m/s, and a way of 2.5 m
has 2.5 m/s. The mod does not change the two values.

**The log.** Tag `take stream`: one line when the stream of a take at a
distance starts, with the source, its point and its top, the Hand, the length
of the way in units and in meters of the room, and the life of a piece. One
line at its end, with the count of the pieces that the mod placed, the calls
of the rule, how many of them were a second call in a frame, and the top of
the source at the start and at the end. With `take_stream=0` the two lines
say that the pieces are on the line of the game.

### A forest gives a tree

A press on a `BigForest` does not put the forest in the Hand.
`BigForest::IsTuggable` (slot `0x718`, `0x00438DC0`) gives 0. So `fn_005D1740`
sets only the Tug flag `CHand+0x4908` and `GInterface+0x450` (`0x005D17F4`), the
pick lock and the interface action 13. The step of action 13 is `fn_005D5250`
(the table at `0x00D17DA8` is made by `0x005D0890`, and its entry for 13 is
written at `0x005D0C1C`) [V]. On each frame that step reads the object of the
next pick record (`+0x400`). When there is none it gives 0 (`0x005D5295`), and
the interface step then cancels the action (`fn_005D1260`, called at
`0x005D1246`). After 225 ms (`0xE1`) with the Hand empty it clears the Tug flag
(`0x005D52D6`), and it then sends the pick up packet `0x13` through
`fn_005D2800`. The handler calls `BigForest::InterfaceSetInMagicHand`
`0x004393C0`, which makes a `Tree` at the forest's point (`Tree::Create`) and
puts it in the Hand. A single `Tree` does not wait: `Object::IsTuggable` gives
1, so `fn_0046DC30` puts the tree in the Hand at the press.

While the pick is locked the draw offers no candidates. So `Inject` leaves the
record alone while the pick is locked. Else the step would find no object, and
the game would cancel the pick up on the frame after the press.

- **The Hand in the forest.** Close the grip with the Hand's point in the forest,
  and keep it closed. After the game's 225 ms a tree is in the Hand, and the
  grip carries it. A grip that opens before that gives no tree.
- **At a distance.** A forest in the cone and in the range is a far target.
  Close the grip and pull. The fetch is done when an object is in the Hand, and
  the glide draws the new tree from the forest's point.
- A forest needs the pull, and a pile does not. A forest is large, so the aim is
  on a forest often. A grip that closes for a grab of the world must not take a
  tree each time.

### A tree in the Hand

**The roots pile [V].** When the Tug of the game pulls a tree free
(`HandStateTug::Update`, the call at `0x005B847E` of `fn_0074BD20`), the game
makes a roots pile: a mesh (`0x251` in the mesh pack) at the mesh translation of
the tree, with dust. It is a decoration with a life of 15 s, and it fades out
(`fn_00825240` makes it, `fn_00825350` ages it, the list head is at
`0x00EB9A00`). It does not follow the tree. With the Hand at the tree the pile
stands at the roots, on the land where the tree stood. At a distance the Tug
breaks after the glide has begun to move the tree, so the game put the pile on
the way of the glide, in the air.

With `far_roots_at_tree=1` the mod moves the new pile to the place of the tree at
the lock (`FarGrabGlideOrigin`), so a tree from a distance leaves the same pile
at the roots as a tree that the Hand took. The pile is the game's own object:
the mod only gives it the place. A tree in the Hand has no piece of the pile
with it in the game's code, and the game has no test of a pile for the planting.

**What decides that a release plants the tree [V].** The release is
`Object::InitialisePhysicsFromHand` `0x00636F00`. The game sets the tree down
and plants it on the spot (`Tree::EndPhysics` `0x0074B830`, with the flag 8 of
the physics object) when all of these are true. Else the tree goes into the
physics and falls.

- The Hand is not fast (its speed across the land is at most 2 units a turn).
- No other body is in the way: the game raises the object until it does not
  intersect, and a raised object falls (see "The test of a body in the way").
- The land under the tree is dry land, or shallow water of the land block.
- The mesh of the tree leans at most 0.2 rad about x and about z
  (`LHMatrix::GetYXZ` `0x007FAB30`).
- The tree is not on fire (`Tree+0x44`).

A tree that falls has no flag 8, because only the release sets it. When it
comes to rest, `Tree::EndPhysics` makes a `DeadTree` (`fn_00510B70`) at its
place and deletes the tree (table slot `+0xC`) [V]. The dead tree is a
different object, and the entry of the physics keeps it as an object at rest.
A desktop run showed the two ends: a tree set down at 2.4 cm of the room
was a `Tree` in a forest 25 frames later, and a tree let go at 45 cm never
left the physics as a `Tree` (the dead tree path does not clear its flag).
The dead tree of a tree that fell was a far target in a later frame of a run
(the log line of the target names the class `DeadTree`). A tree that the Hand
takes while it comes to rest is in "An object that comes to rest while the
Hand takes it".

**The card.** The text "Plant" of the game is the same for each tree in the
Hand. With `tree_plant_hint=1` the card tests the same things in each frame
(`tree_plant_math.h`, `HandDirectTreeVerdict`): the height rule of
`release_fall_cm` (throw.md), the lean, dry land (`MapCoords::IsDryLand`) and
fire. When the release plants the tree, the card keeps the text of the game.
Else it says what the opening does: "Lower to plant. Open to drop", "Hold
upright to plant. Open to drop", "Needs dry land. Open to drop" or "On fire.
Open to drop". The test of a body in the way is not in the card.

**The test of a body in the way [V].** The release makes the physics object of
the tree (`PhysicsObject::AddObject`), puts it on the land
(`PhysOb::AdjustToGroundLevel` `0x007FCB80`) and calls
`PhysicsObject::RaiseUntilNotIntersecting` `0x00644800`. When that call moved
the tree up, the tree falls. The call has two parts:

1. It reads each object in the map cells of a square about the tree, as wide
   as the radius of the physics object (`PhysOb+0x150`). It asks each one
   `Object::ShouldPhysicsRaiseObjectUntilNotIntersectingThis` `0x006377D0`
   (the table slot `+0x7A4`), which gives the answer of
   `InteractsWithPhysicsObjects` (`+0x79C`). An object that says yes and is not
   in the physics goes into the physics now (`fn_00644DF0`, with
   `PhysOb::Initialise` from its mesh).
2. For each other object in the physics whose center is nearer than the sum
   of the two radii, `fn_007FDD60` gives how deep the points of the one body
   are in the other, along up. More than 0.001 units raises the tree by that
   depth, and the call starts again.

`InteractsWithPhysicsObjects` is yes for an object with a mesh
(`Object`, `0x006377B0`), and for a rock, a scaffold, a Creature and a dead
tree. A house or other building says yes by two of its own values
(`MultiMapFixed`, `0x0052F3D0`). It is no for a tree that stands
(`Tree`, `0x0074B6A0`), a forest, a field, a fish farm, a Worship Site, a
graveyard and a bonfire. So the trees of a forest are never in the way of a
planted tree.

No call of the game gives this answer before the release with no change of
state. The first part puts each near object into the physics of the game. The
second part compares two physics bodies, and the tree has a physics body only
from the release on. A test with the same answer needs a body of its own for
the tree and for each near object (`PhysOb::Initialise` on memory of the mod,
then `AdjustToGroundLevel` and `fn_007FDD60`). That is not built.

**The log.** Tag `hand tree`: the settings, each roots pile that the game makes
(with the place), each change of the answer for the tree in the Hand (the
height, the lean, dry land, fire), the release with the lean of the last frame
in the Hand, the first frame of the tree in the physics (the height of its
mesh over the land, its lean, the count of the objects in the physics of the
game that are nearer to the tree than the sum of the two radii, the nearest
one, and the flag 8 of the tree), and what the tree is when it leaves the
physics (its class and its forest).

**The lean in a desktop run.** The tree in the Hand turns with the hand
(`hand_hold_turn`, [hand.md](hand.md)). `throw_queue.ps1 -Runs treelean
-NewGame` turns the hand 30 degrees with a tree in it, 1.6 cm of the room
over dry land. The tree then leans 0.26 and -0.46 rad, and the card says "Hold upright to plant.
Open to drop". After the opening the tree is in the physics with the same
lean and with no forest: the game did not plant it. The game does not plant
an upright tree at that place either (the next paragraph). So the run shows
the lean that the card and the game read, and it does not show that the lean
alone stops the planting.

**A tree that the card calls "plant" and the game does not plant.**
`throw_queue.ps1 -Runs treeset -NewGame` is the same recipe with a hand that
does not turn. The hand takes a tree at the land and sets it down upright
where it stood, 1.3 cm of the room over dry land. A rock lies 0.8 units from
that place. The card says "Plant". The game does not plant the tree: it is in
the physics with no forest, its mesh is 0.7 units over the land in its first
frame there, and the flag 8 of its physics object is not set. The physics of
the game then has 6 other objects with their center nearer to the tree than
the sum of the two radii, and the nearest one is a rock. After a set down
with a still hand, with no lean, on dry land and with no fire, the release
leaves the flag 8 out only when it raised the tree. So a body was in the way.
[I: which of the 6 objects raised the tree.]

In the test save `pen` the two recipes take a small rock beside the tree and
not the tree, so they need `-NewGame` (`tools/desk/README.md`, "Known limits
of the recipes").

**Open.** A body in the way is not predicted, and the card says "Plant" for a
tree that the game then lets fall (the run above). Whether the pile at the
roots is what the player means by the "pit" needs the headset.

### The far touch

A scroll in the cone of the aim ray is a far target, with the range of
`far_grab_max_m`. It has the gold halo. It needs no pull:

1. The grip closes on the scroll.
2. For 2 frames the scroll is in the next pick record (the Pull state, as for a
   bulk source).
3. The grip goes to the game as a press (the Fetch state). The game taps the
   scroll with its own action, as in "A scroll, a house".
4. After 130 ms (`kTouchMs`), and two frames at the least, the far grab lets
   the press go. The grip
   stays held back until it opens.

A scroll scores 3 degrees better than its angle (`kTouchBiasDeg`,
`TouchBias` in `far_choice_math.h`), because trees stand all around a scroll.
A signpost of the last rank does not (see "The cone and the choice").
`far_touch=0` turns the far touch off.

`FarDraw` (`far_grab.h`) has the fields `touch` (the target is a scroll, and the
grip activates it) and `bulk` for the hint card, so the card can say "Activate"
in place of "Pull to fetch". `HandDirectPressKind()` gives what the grip does at
the Hand's point (nothing, hold, take, tap or the Creature).

### A target that moves, floats or dies

The game takes the object of the record by its pointer, where it is, so the
fetch needs no change for an object that moves. The game also takes an object
that is in its physics: a rock that falls or flies after a release is a far
target, and the fetch puts it in the Hand while it moves. The press of that
fetch is a catch: the game sends its pick up at once, and the body of the
object stays where it is while the glide draws it ("A catch at once").

- **The pick reaches farther for an object that moves** (`catch_moving`). The
  player closes the hand on the place where the object was a moment ago: the
  eye, the hand and the closing of the grip take time, and the pick of a frame
  is one frame old. So an object that the Hand can hold and that moves has an
  extra reach (`near_math::MovingExtra`): the speed of the object less 15 cm a
  second (`kCatchStillCmps`), times `catch_lag_ms`, and at most
  `catch_cap_cm`. The speed comes from the place of the object in the draw of
  this frame and of an earlier frame (`ObjectSpeedNow`, once a frame, for an
  object that the draw offered in the last 3 frames). The first cut of
  `Choose` is as long as the cap. The log has one line for each object that
  only this reach chose ("moves at"). Only the pick up has the extra reach: a
  scroll, a house and a pile keep theirs.

  With `catch_moving=2` or 3 the extra reach is on the reach that the object
  has at rest: on `hand_pick_cm`, and on the wide reach
  (`near_math::WideMoving`). So
  a rock that falls at 95 cm a second or more is the target up to 23 cm from
  the Hand's point, when no object that a press acts on is nearer. While such
  an object is the target, there is no far target. With `catch_moving=1` the
  extra reach is on `hand_pick_cm` only, so an object that moves is the target
  up to 15 cm, as an object at rest is.

  **The guard of a throw.** The object that the Hand threw has no extra reach
  for `catch_grace_ms` after the release (`near_math::GuardHolds`), so a throw
  does not come back to the hand in the same closing of the grip. It keeps
  the reach that it has at rest. With `catch_moving=2` or 3 the guard is for a
  throw only. An object that fell from a hand that was still (the height rule
  of `release_fall_cm`) is no throw: it starts with no speed, and the player
  who closes the grip on it in its fall wants it back. A fall from 46 cm of
  the room lasts 0.63 s, so a guard of 600 ms is almost all of that time.
  With `catch_moving=1` each object that the Hand let go has the guard.

  **The guard ends when the grip is fully open** (`catch_moving=3`, the
  default). The guard is for a grip that closes again by a dip of the squeeze:
  a fast fall opens the grip at a value that is still high, and a small rise
  closes it again. A grip that was fully open (its value came to 35 percent or
  less after the opening that let the object go) closes only when the player
  closes the hand, and that is a catch. So the object that the player threw
  up has its extra reach from that moment, and a new close of the grip
  catches it. With `catch_moving=2` the guard lasts for the whole of
  `catch_grace_ms`.

  **The frames after a release.** The game holds the object in the Hand from
  the release action to the drop action of a game turn: 3 to 16 frames in the
  desktop runs, 0.17 s at most. The mod draws the object on its arc in that
  time. `Choose` has no target while the Hand of the game holds an object, so
  the thrown object is no target in those frames. This is the state of the
  game, and the mod does not change it. From the frame after that game turn
  the object is a candidate again.

- **The lock follows the object.** `FarGrabTrack` takes the place of the locked
  object, the land under it and the influence there on each frame. The tether,
  the halo and the start of the glide follow the object. An object that drifts
  out of the influence while the lock holds ends the lock at the pull: "the
  object left your influence".
- **The lock keeps a pointer that can die.** The game deletes a rock 4 radii
  under the sea, and a villager that dies. A pull after that would put a dead
  pointer in the pick record, and `fn_005D5E40` calls a slot of its table. So
  the selection keeps the first word of the object (its table). `FarTargetLives`
  tests once a frame, before each write of the record and before each read of
  the object, that the memory can be read and that the first word is the same.
  If not, `FarGrabGone` ends the lock, and no code reads the object again. The
  destructor of an object writes the table of its base class there, so a
  deleted object fails the test. [I: an object that the game makes at the same
  address with the same class passes. The fetch then takes that object, which is
  a live object.]

### An object that comes to rest while the Hand takes it

**What the game does [V].** A press on an object that the physics of the game
has puts the object in the Hand of the picture at once: `fn_0046DC30` writes
`CHand+0x4904`, and the Hand goes to the Holding state with no Tug. The game
puts the object in the Hand in a later game turn.
`GInterface::PlaceObjectInMagicHand` `0x005DA6F0` calls `fn_005DC330`, and
that function takes the object out of the physics with
`PhysicsObject::RemoveObject` `0x00646A00` (`0x005DC346`). Its two flags are
0, so the call makes no end of the physics, and the object stays what it is.
With the pick up of the game (`catch_at_once=0`) the physics ran 2 or 3 game
turns between the press and that turn in the desktop runs.

In each of those turns the object can come to rest. Then the step of the
object (`fn_007FE260`) gives 2, and `PhysicsObject::GameTurnUpdate` calls
`EndPhysics` of the object (table slot `+0x790`) with the flag 1. For a tree
with no flag 8 that is the dead tree of "A tree in the Hand": the game
deletes the tree that the Hand has. The Hand is then empty with the grip
closed, and a dead tree lies on the land. A thrown tree comes to rest by the
same call, so a tree that lies after a throw is a dead tree too. The mouse
has the same race.

**What the mod does.** With `catch_rest_guard=1` an object does not come to
rest while the Hand takes it. Once a game turn, before the physics, the mod
latches the object of the take (`TakeTurnBegin` in `throw_physics.cpp`): the
object that the far grab has locked, pulls or fetches, or the object that the
Hand of the game holds, while the physics still has it. A bulk source, a
scroll and the Creature of a leash click are no take. When the step of that
object gives 2, the hook on `fn_007FE260` gives the game 1, the answer of a
step that moved (`TakeStep`). The step has set the velocity and the turn of
the object to 0, so the object lies still. The game then takes the object
out of the physics in its own turn, as it takes each object that moves. A
tree stays a tree, and it is in the Hand until the grip opens.

With `catch_at_once=1` the step of the object does not run from the press
on ("A catch at once"), so the object cannot come to rest after the press.
The guard then acts in the lock and the pull of a far grab. With
`catch_at_once=0` it also acts in the game turns between the press and the
take.

The limits:

- The guard of one take ends after 30 game turns (`kTakeTurns`), which is 3
  s. Then the object comes to rest as each other object does, and the log
  says so. So a lock that the player holds for longer on a tree that lies
  still ends: the tree is a dead tree then, and the dead tree is a far
  target of its own.
- The guard acts only when the controller placed the Hand in one of the last
  3 frames. The mouse keeps the take of the game.
- A multiplayer game keeps the physics of the game: all players must run the
  same physics.
- The guard uses the hook of the real flight, so it needs `throw_law=2` at
  the start of the game.
- For an object whose center is lower than half of its radius,
  `GameTurnUpdate` can make the 2 by itself (the table slot `+0x7B8` of the
  object decides). The hook does not see that answer [I: which objects say
  yes there].

`catch_rest_guard=0` in `bwvr.ini` gives the game's own end of the physics.
With `test_keys=1`, Ctrl and Shift with Scroll Lock turn the guard off and
on. The pointing controller gives a long pulse for on and a short pulse for
off, and the log has a line.

**The log.** Tag `hand take`: the setting at the start, "came to rest in the
physics of the game while the Hand takes it" with the game turn of the take
and what the mod does, and the end of the take: "the Hand holds" or "the Hand
does not hold" the object, with the count of the game turns and of the steps
that the mod held back.

**Desktop runs.** `throw_queue.ps1 -Runs treedropend` in `bwvr/tools/desk`
lets a tree fall from the Hand and locks it from a distance 1.3 s later. The
pull comes 10 frames after the lock, and the press 2 frames after the pull.
A tree that falls comes to rest by itself 1.2 to more than 2.3 s after the
release, so the rest of the game itself comes in the take in some runs only.
For the other runs the recipe has the test key `catch_rest_test=1`: it makes
one step of the object give 2 in the first game turn in which the Hand of
the game has the object, with the state that the step of the game leaves
then (no velocity and no turn). That step runs only with `catch_at_once=0`,
so the runs of the table have `-More 'catch_at_once=0'`.

| `catch_rest_guard` | What the runs show |
| --- | --- |
| 1 | In 3 of 4 runs the mod holds the rest back (1, 11 and 30 steps), and the tree leaves the Hand when the grip opens, 46 frames after the Hand took it. In one of the three the rest of the game itself came in the lock, before the pull. In the fourth run the tree came to rest before the aim, so there was no take |
| 0 | In 3 of 3 runs the game ends the physics of the tree in a turn of the take: by the test key in 2 runs, and by itself in 1 run. The Hand holds nothing 1 or 2 frames after it took the tree, and the grip stays closed on an empty Hand for 44 frames more |

With `catch_rest_guard=1` the two recipes with no test key keep their
results. In `throw_queue.ps1 -Runs treedrop` a near grip on a tree in its
fall keeps the tree until the grip opens. In `-Runs treerest` the aim is on
a tree that falls with no lock, the tree comes to rest 1.5 to 1.6 s after the
release, and the target is then the dead tree.

A rock that comes to rest in a take is in the Hand with each value
(`throw_queue.ps1 -Runs dropfarroll`, one run with each value). With
`catch_rest_guard=1` the rock came to rest in game turn 4 of the take, the mod
held 15 steps back, and the game put the rock in the Hand in that turn. With
`catch_rest_guard=0` the game ended the physics of the rock at its rest, and
the Hand kept the rock until the grip opened.

**Open.** Not run: a villager that comes to rest in a take, and
the mouse. Only a headset shows whether a tree that the player takes at the
moment of its rest comes to the hand each time.

### Show

- **The halo** (`hand_glow.cpp`, `DrawHalo`) is the disc of the glow, gold, or
  red outside the influence. It is centered one radius over the base of the
  object. Its radius is `1.35 r + 1 cm` of the room, at least 2 cm and 1.2
  percent of the distance. The depth test uses a point one radius behind the
  center (`GlowProject` takes a negative pull). So the object hides the middle
  of the disc, and the light shows around its outline. Nothing is drawn on the
  land under the target. By default the mesh of the target glows in the color
  of the halo, and the disc shows only for a target whose mesh does not glow
  (`target_glow=1`, `hand-model.md`, "The glow on the mesh of a target").
- **The tether** (`overlay.cpp`, `BuildFar`). After the lock, a gold tube of 3 mm
  goes from the Hand's point to the object. It also shows while the game takes
  from a far bulk source.
- **The tick:** 0.15 for 10 ms at a new target, 0.3 for 30 ms at the lock, and
  0.2 up to 0.6 every 60 ms during the glide. The game's effect 4 starts at the
  pick up.

### The glide

`GlidePrepare` is a call after `CHand::PrepareForDrawing` (`0x0046C550`).
`pass_guard.cpp` hooks that function, and `hand_exact.cpp` gives it a list of
calls (`HandExactAddPrepareHook`), so no second hook is needed. The glide runs
from the take (`+0x4904` is the object), not from the Holding state, because the
Tug lasts 4 to 5 frames:

```
p(u) = p0 + (p1 - p0) e(u) + y h 4u(1 - u)
e(u) = 1 - (1 - u)^3
h    = 0.15 |p1 - p0|
T    = clamp(0.30 + 0.05 d_room, 0.30, 0.50) s
```

p0 is the mesh translation of the object at the pull. p1 is the mesh translation
that the game wrote in the Holding state, and before that the Hand's point.
`GlideArc` and `GlideSeconds` are in `controller_math.h`. The clock is the time
of `FarGrabFrame`, so both eyes draw the same point. Only the drawn mesh moves.
The object itself is where the game puts it.

**A release in the glide.** The glide writes only the drawn mesh, so the throw
must not start from it. `throw_physics.cpp` has a call that runs before the
glide (`ThrowCapture`, put in the list by `ThrowPhysicsInstallEarly`). On each
frame it keeps the place of the held object in the Hand: the mesh position that
the game and `hand_exact.cpp` wrote in the Holding state, or the Hand's point
before that state. At the release the flight starts at that place, when the drawn
mesh is more than 1 cm of the room from it. The picture starts at the point of
the glide and comes to the flight in 0.15 s (`CatchUpLeft`), so the object does
not jump.

### Keys of the far grab

| Key | Default | What it does |
| --- | --- | --- |
| `far_grab` | 1 | 0 turns the whole feature off |
| `far_grab_cone_deg` | 8 | The `te` that a new target needs. The target stays to this plus 6 |
| `far_grab_choice` | 2 | How the angle to a candidate is measured, and which class comes first (see "The cone and the choice"). 2: the body of the object, and a living thing in its cone comes before each thing that is not living. 1: the body, and a villager, an animal and your Creature score 3 degrees better. 3: a ball for each object, with a size from its class. 4: the balls of 3 with the ranks of 2. 0: one point of the object. The values 1 to 4 give a Creature command 10 m of range, and 0 gives it 4 m |
| `far_grab_living_deg` | 4 | The cone of a living thing for the ranks (`far_grab_choice` 2 and 4): it is in the first rank while its `te` is at or below this. 0.5 to 20 |
| `far_grab_ball_living_deg`, `far_grab_ball_other_deg` | 3, 1.5 | The radius of the ball of a living thing, and of each other thing, in degrees from the ray (`far_grab_choice` 3 and 4). 0 to 20 |
| `far_grab_stay` | 1 | 1: a living thing that has the halo keeps it against each thing that is not living while the aim is within its cone plus `far_grab_stay_deg`. 0: the margin of 2 degrees between two scores only |
| `far_grab_stay_deg` | 3 | How far past its cone a living target keeps the halo, in degrees. 0 to 20 |
| `far_grab_creature` | 1 | 1: the aim measures your Creature by its body, as each living thing, and takes it from the game. 0: by the point of its mesh, at its feet, and only when the draw offered it |
| `far_grab_miracle` | 1 | 1: a miracle that the Hand can take is in the first rank, as a living thing (`far_grab_choice` 2 and 4). 0: it has no rank |
| `far_grab_sign` | 1 | 1: a signpost is in the last rank: it takes the halo only when the aim is on it and on no other target, or when no other target is in the cone (`far_grab_choice` 2 and 4). 0: a signpost is a scroll of the first rank |
| `far_grab_pull` | 80 | The speed of the pull, in cm/s |
| `far_grab_max_m` | 2.0 | The range: a target is at most this far from the head, in meters of the room along the floor. 0 gives no limit |
| `far_grab_near_cm`, `far_grab_near_out_cm` | 10, 15 | See "Near objects win" |
| `far_touch` | 1 | 1 makes a scroll in the aim a far target that the grip activates |
| `far_leash` | 1 | The leash click on the player's own Creature at a distance (see [creature.md](creature.md)) |
| `far_take_carry` | 1 | 1: when the source of a take at a distance goes away while the grip is closed, the grip machine keeps the grip and the grip carries the bundle. 0: the far grab holds the grip back until it opens, and the machine lets the bundle go at once (see "A bulk source at a distance"). Test key: Shift and Backspace |
| `take_stream` | 1 | 1: the pieces of the stream of a take at a distance go on a curve from the top of the source to the Hand. 0: the line of the game, from the land under the Hand (see "The stream of a take at a distance"). Test key: Ctrl, Shift and the minus key |
| `creature_hold_stay`, `creature_hold_stay_ms`, `creature_stay_look` | 1, 350, 1 | A grip that stays closed on the far Creature makes it stay (see [creature.md](creature.md), section 5.3). Test key: Shift and End |
| `far_roots_at_tree` | 1 | 1 puts the roots pile of a tree that comes from a distance at the roots, on the land where the tree stood. 0 leaves it where the game puts it, on the way of the glide. Test key: Ctrl, Shift and Page Down |
| `tree_plant_hint` | 1 | 1 makes the card say what the opening of the grip does with a tree in the Hand. 0 shows the text of the game ("Plant") each time. Test key: Ctrl and Page Down |
| `throw_law` | 2 | The law of the throw (see [throw.md](throw.md)) |

With `test_keys=1`, Ctrl with the Delete key steps `far_grab_choice` from 2 to
3 to 4 to 0 to 1 and back to 2 while the game runs. The pointing controller
gives one short pulse for each unit of the value and one long pulse for 0, and
the log has a line. Ctrl with the Backspace key changes `far_grab_stay` between
1 and 0, with a long pulse for 1 and a short pulse for 0. Shift must be up for
both keys. Shift with the Backspace key, with Ctrl up, changes `far_take_carry`
between 1 and 0, with the same pulses. Ctrl and Shift with the minus key of
the row of digits change `take_stream` between 1 and 0, with the same pulses.
The change applies from the next frame, also in a take that runs.

Test keys for the desktop runs only:

- `far_test_influence=1`: every land point is inside the influence.
- `far_test_no_trees=1`: no tree is a far target.
- `far_test_thrown=1`: the word `aimfar` aims at the object that the Hand threw
  last, where the draw saw it, so a run can fetch an object that flies, rolls or
  is in the sea. When the draw did not offer the object, the place is from its
  mesh. `seek_thrown=1` does the same for the word `seek`: the hand steps onto
  the thrown object.
- `far_test_least_cm=N`: the word takes an object N cm or more from the Hand,
  and not 50 cm.
- `far_test_aim_class=N`: the word `aimfar` aims at the middle of the body of the
  nearest villager (1), living thing (2), rock (3), your Creature (4), tree
  (5), Creature that is not your Creature (6), quest character (7, a
  `SpecialVillager`) or miracle that the Hand can take (8). The body of your
  Creature is its capsule. `far_test_aim_up_pct=N` gives a different height
  on the body, in percent of its height: 100 is the top.
  `far_test_aim_toward=1` (a tree), 2 (a rock) or 3 (a signpost) then turns the aim toward
  the nearest such object, 20 units or less from the target, by
  `far_test_aim_blend_pct` percent of the way. The same aim then runs with
  each value of `far_grab_choice`, and the log of each new target names the
  winner and the second best. The start of Land 1 has no villager in view, so
  the runs there aim at rocks and trees.
- `far_test_aim_pair=1`: the word takes the object of the class that has such
  a tree (or rock) nearest to it, 2 to 20 units away, and not the object
  nearest to the Hand.
- `far_test_aim_blends=0,30,60`: a list of percents. Each word `aimfar` takes
  the next one in place of `far_test_aim_blend_pct`, the last one stays, and
  each later word keeps the object and the tree of the first word. So one run
  steps the aim along the line from a villager to a tree, past the tree, and
  back (`village_run.ps1 -Kind choice`).
- `far_test_quest=1`: the class test of `far_grab_choice=1` answers no for
  each villager, as it does for a quest character. A run then shows value 1
  with a living thing that its bias does not know.
- `far_test_take_gone=N`: at game turn N of a take at a distance, the test
  asks for the end of the take on each frame, as the grip machine does for a
  take that still runs with the button up. The grip stays closed. In the
  frame in which the take is over, the far grab gets the answer that its
  source is gone. A run then shows who has the grip after a take whose source
  the game deletes (`village_run.ps1 -Kind fartake`).

The fake controller has two words. `aimfar@F+n` turns the aim of the right hand
to the nearest far target: a holdable object that is no tree, more than 0.5 m of
the room from the Hand, in the range of `far_grab_max_m` from the head, inside
the influence and seen, in the God view or on the table. With `seek_bulk=1` it
aims at the nearest bulk source. The hand stays where it is. `pull@F+n` steps the
right hand 15 cm toward the head over n frames, and the next `pull` steps it
back.

### The log of the far grab

Tag `far grab`: each new target with its class, the angle and the distance, the
reason and the second best candidate, one line for each target that keeps the
halo by the stay rule, the
lock, the pull (the speed along `p` and the move in cm), the press, "the Hand
holds" with the frames after the press, each frame of the glide, each end of a
lock or a fetch with the reason, and one line each report window. The tag has
room for 40 lines in each report window of 450 frames (`kMaxLogs` in
`far_grab.cpp`), so a long session has the lines of its last targets too. A
window that had more lines says how many are not in the log. Also: how far
the object moved while the lock held, a locked target that goes in or out of the
influence, a target that the game deleted ("the target is gone", with what the
far grab does with a closed grip then), and how often
the locked target went to the pick. "the grip closed on the scroll", "the far
touch is over", "the forest P gave the new object Q". Tag `take stream`: the
start and the end of the stream of a take at a distance ("The stream of a take
at a distance"). Tag `hand throw`: the
speed sent and the landing of the real throw. Tag `hand glow` and tag `overlay`:
the first draw of a halo, with its pixels.

### Tests and open questions

The math test (`far_checks.h`, `RangeChecks` in `grip_checks.h`):

- The cone: an object on the ray or at its angular radius scores 0. An object
  behind the ray, square to it, or 20 cm from the Hand is out. The nearer of two
  equal targets scores lower. The hysteresis holds at 1.5, 2 and 2.5 degrees.
  The target is kept at 13 and 14 degrees and lost at 14.5. An object 20 cm from
  the Hand is no target with its own distance and is one with the distance of the
  score (`FarScoreMeters`).
- The choice by the body (`far_choice_checks.h`): a tree 4 units from a villager
  wins over the villager when the hand is 1.2 degrees off the villager with
  `far_grab_choice=0` and loses with 1. A ray through the crown of a tree takes
  the tree. A villager 2.4 degrees from a tree on the ray comes first, and a
  villager 8 degrees from it does not. Two villagers: the one under the ray
  wins, and a shake of 2 degrees keeps the halo. A rock before a tree wins in
  both choices, and a rock beside a tree that is nearer to the Hand wins with 1
  and loses with 0. A tree that has the halo keeps it against a villager that
  scores less than 2 degrees better. A villager 5 m away covers 2 degrees at the
  least. The range of a command: 4 m, 10 m, and an ini value as it is.
- The ranks, the balls and the target that stays (`FarRankChecks` in
  `far_choice_checks.h`). A quest character stands 4 units from a tree, and the
  ray goes through the crown of the tree, 3.6 degrees from him: value 1 gives
  the tree (score 2.42 against 2.92), and the values 2 and 4 give the man. A
  villager in his place wins with value 1 by the bias. The man takes the halo
  from a tree that has it, and he keeps it. Balls with no ranks give the tree
  at its foot when the ball of the man holds the ray too. A ball at the origin
  of a tree is 5.6 degrees from its crown, and the body holds the crown. A ball
  has the same `te` at 7.5 and at 4000 units a meter. A hand that shakes 1
  degree at the edge of the cone of the man, with the ray in a tree beside
  him: with `far_grab_stay=0` the halo changes 45 times in 200 frames, and with
  1 it does not change. 10.5 degrees from the man the tree takes the halo. With
  value 1 and `far_grab_stay=1`, a rock on the ray does not take the halo from
  a villager 5.5 degrees past his cover. Of two villagers whose bodies hold the
  ray, value 1 gives the one nearer to the Hand and the ranks give the one
  nearest to the ray. The aim on the chest of a villager 1.7 degrees beside
  the target takes the halo, and a villager less than 1 degree nearer does
  not. The crown of a tree with a villager 20 degrees away is the tree.
- The classes of the ranks (`FarClassChecks` in `far_choice_checks.h`). The
  class of a signpost, of a scroll of a quest, of a miracle, of a living thing
  and of a tree, with each value of `far_grab_sign` and `far_grab_miracle`,
  and with a choice that has no ranks. A miracle 1 m away with a signpost 12
  cm beside it: the aim on the miracle gives the miracle. With the aim 2.5
  degrees toward the signpost (0.4 degrees past the cover of the miracle, 2.7
  degrees from the signpost), the keys at 0 give the signpost and the keys at
  1 give the miracle, and each key alone gives the miracle too. A signpost
  that has the halo keeps it with the aim on the miracle when the keys are 0,
  and the miracle takes it when they are 1. The aim on the signpost and on no
  other target gives the signpost. With a tree behind the signpost whose body
  the ray crosses, the tree has the halo. A signpost alone in the cone has
  the halo with the aim beside it. At the edge of a body: a signpost keeps
  the halo to 1 degree past its body, a new signpost needs the ray in its
  body, and a rock keeps the halo to 1 degree past its body with the ray in a
  signpost. The ray in a signpost and in the body of a villager gives the
  villager. With no signpost among the candidates, `PickClassed` gives the
  target and the reason of `PickRanked` in 210 cases of the man beside a
  tree. The Creature: by the point of its mesh, the aim on the head of a
  Creature of size 0.22, 44 cm from the Hand, is 4.3 degrees from it, and a
  tree behind it has the halo. By its body the angle is 0, and the Creature
  has the halo. The same for a Creature of size 0.5 at 70 cm (6.8 degrees by
  the point). The Creature keeps the halo in its stay margin, and a tree 12
  units beside it is the target when the aim is on its crown.
- A miracle beside a signpost in a desktop run (`hand_run.ps1 -Kind fixed
  -Save temple`, 100 units a meter). The God view goes to a signpost
  (`seek_plan=go,scrolls`), the test key `miracle_test_seeds` makes a miracle
  11.7 units from it, and the aim steps from the miracle to the signpost, past
  it, and back (`far_test_aim_class=8`, `far_test_aim_toward=3`,
  `far_test_aim_blends`). The hand is 66 cm from both. With the default keys
  the miracle has the halo at 0, 25, 50 and 75 percent of the way. At 75
  percent the aim is 5.4 degrees from the miracle and 1.6 degrees from the
  signpost, and the miracle keeps the halo by the stay rule. At 100 percent
  the signpost takes it ("the aim is on the signpost and on no other
  target"). On the way back the miracle takes it at 75 percent ("a signpost
  comes after each other target"). With `far_grab_sign=0` and
  `far_grab_miracle=0` the signpost takes the halo at 50 percent, with the aim
  2.9 degrees from the miracle and 4.0 degrees from the signpost (score 1.75
  against 4.50). It keeps the halo until the aim is on the miracle again.
- The Creature in a desktop run (the same script, 72 units a meter, a
  Creature of size 0.22, `far_test_aim_class=4` and
  `far_test_aim_up_pct=100`). The Hand's point is 34 cm from the Creature,
  the controller is 17 cm over the land, and the aim is on the top of the
  body of the Creature. By its body the angle
  is 0.0 degrees. By the point of its mesh it is 5.3 degrees, past the cone
  of 4 degrees. That point is 1.8 units below the middle of the body: at the
  feet. With a thing that has no rank 1.7 units beside the Creature (a
  miracle of the test key, with `far_grab_miracle=0`), `far_grab_creature=0`
  gives the halo to that thing (score 1.29 against 6.06 for the Creature,
  5.6 degrees away). The default gives it to the Creature, by its rank. With
  the hand 60 cm in front of the head, the aim comes from above and both
  measures give 0 degrees.
- The choice in a desktop run (`village_run.ps1 -Kind choice`, the test save
  `temple`, 44.4 units a meter). The aim steps from a villager at the Village
  Store toward a tree 9 units from him, and past the tree. A rock is on the
  same line. With `far_touch=0` no scroll is a far target, and the runs differ
  only in the keys:
  - `far_grab_choice=1`, `far_grab_stay=0` and `far_test_quest=1` (the bias
    does not know his class): with the ray on the body of the villager, the
    rock takes the halo (score 2.27 against 2.34). Past the tree a tree takes
    it. The villager never has the halo.
  - The default: the villager takes the halo, and he keeps it while the ray is
    on the rock and on the trees. 4.6 degrees past his cover the log says that
    he keeps the halo by the stay rule.
  - `far_grab_stay=0`: at that aim an object beside the tree takes the halo.
    When the aim comes back to 3.4 degrees from the villager, he takes it from
    a tree with `te` 0, by his rank (score 5.76 against 3.44).
  - `far_grab_choice=1` and `far_grab_stay=0`, with the class that the bias
    knows: the villager has the halo at each of these aims.
  With the scrolls of that village as far targets, the same aim on the
  villager gives a scroll 2.3 degrees away with the first set of keys, and the
  villager with the default. A villager walks, so the last steps of a run can
  have a different villager. No quest character of that save stands 20 units
  or less from a tree.
- The stream of a take at a distance (`take_stream_checks.h`). The start of
  the curve is the top of the source, and its end is the Hand. A piece starts
  straight up, and it comes down into the Hand from the side of the source.
  With the two ends at one height, no point is lower than the ends, and the
  top is 0.19 of the way over them, for a way of 13, 60 and 200 units. A way
  that is 10 times as long has 10 times each offset. With the Hand lower than
  the source the piece goes up first. With the Hand straight over the source
  the way is the straight line up. No piece goes back along the floor. The
  velocity is the change of the place in a second. A height of the source
  that is no sane number gives the point of the source. `take_stream=0` and
  a take with the Hand at the source keep the line of the game.
- The stream in a desktop run (`village_run.ps1 -Kind fartake`, the test save
  `temple`, 44.4 units a meter, a take of 9 s that the grip ends). The source
  is a `PileWood` of the Village Store. Its point is at a height of 28.1 and
  the game gives a height of 2.98, so its top is at 31.1. The Hand is 24
  units higher and 12 units to the side. The way is 30.6 units long (0.69 m
  of the room) against 24.5 units straight, and its top is 1.4 units over
  the Hand. The eye pictures show 6 or 7 pieces at one time: they go straight
  up out of the pile that the tether points at, bend at the height of the
  Hand, and end at the Hand's point. The two eyes show the same chain of
  pieces. The mod placed 73 pieces in 400 frames, and the rule ran 401
  times. The top of the pile did not change while the Hand took 3846 of
  30000 units of wood. With `take_stream=0` the pictures show the pieces on
  a straight line up to the Hand from the land under it, which is under the
  roof of the store, and no piece at the pile. With `one_walk=0` the rule
  ran 611 times in 305 frames, and the two eyes show the same chain.
- The pull: 0.9 m/s toward the head fires. 0.9 m/s across, a head step with a
  still hand, 0.7 m/s with 5 cm, and an angle of 57.8 degrees do not fire. A
  move of 12 cm fires and 11 cm does not. A flick up fires, and one away from
  the head does not.
- The glide: `p(0) = p0`, `p(1) = p1`, the peak of a flat glide is at u = 0.5 and
  is `0.15 |p1 - p0|`, the ease never goes back, and the time is 0.30, 0.40 and
  0.50 s at 0, 2 and 4 m.
- The range: 150 units across at 100 units a meter are 1.5 m, the height does
  not count, 1.90 m and 2.00 m are in a range of 2 m, 2.05 m is out for a new
  target and keeps the target of the last frame, 2.20 m loses it, a range of 0 is
  no limit, and an object with no known distance is out.
- `FarLandHides`: a hill hides, a hill lower than the slack does not, the sea
  does not hide a line over it or under it, and land just over the sea level
  does.
- The aim of the fake hand: the angles of `FakeAimAngles` aim the hand along the
  wanted direction.
- A take at a distance whose source goes away (`FarTakeChecks` in
  `grip_checks.h`): `FarHoldsBack` holds a closed grip back after a lock, a
  pull and a fetch with each value of the key, and after the Take state only
  with `far_take_carry=0`. With the closed grip in the frame after the take,
  the machine carries the bundle for 15 s with no button and no click, and the
  opening lets it go. With an open grip in that frame, the machine reads a late
  object and clicks to let it go.

Desktop runs have shown the fetch of an object at rest, a fetch of an object in
flight (the lock follows it), the same for a rock that flies over the sea, a
fetch of a rock from the sea 1.8 s after its first contact with the water
(its point is then 0.5 units under the sea plane), and the end of a lock when
the game deletes the object. They have shown a take at a distance from a pile
of wood of the Village Store, 0.61 m of the room from the Hand
(`village_run.ps1 -Kind fartake`, the test save `temple`). The test key
`far_test_take_gone=30` ends that take at its game turn 31 with the grip
closed and tells the far grab that the source is gone. With `far_take_carry=1`
the grip then carries the bundle of 437 for the 28 s until the grip of the
script opens. With 0 the machine reads a late object, clicks, and the bundle
is out of the Hand 9 frames after the take, with the grip still closed. When
the grip ends the take, the bundle stays in the Hand with the grip open for
the 26 s to the end of the run, with each value of the key. Desktop runs (God view, `fake_controller=2`,
Land 1) start at the pen, in the test save `pen`. With `-NewGame` they start
in a new game, with the dialog recipe of [god-view.md](god-view.md), section
12, and the first story. The steps count from the frame at which the player
gets the Hand (`fake_controller_play_frame=2750`, `aim_queue.ps1` and
`throw_queue.ps1` in `bwvr/tools/desk`).

**Not shown in a run:** a villager beside a tree (the start of Land 1 has no villager in view, so the runs use a rock and a tree), a tree or a signpost beside the Creature (the test save `temple` has no tree 20 units or less from the Creature), a large Creature that the draw does not offer, a miracle of a quest beside its signpost, a locked target that drifts out of the
influence, a villager in the sea, the tether and the halo during a take at a
distance, a take at a distance from a pile that the game deletes (the runs
make that case with the test key and a pile of the store), a `Field` and a
`FishFarm` as a far target, the stream of a take at a distance from a source
that is not a `PileWood`, on a way that is long along the floor, with a pile
that goes down, and with a hand that moves, a take with the Hand over
land outside the influence, a multiplayer game, a fetch of a villager, a
Creature or a building, the haptics on a real controller, and the look of the
halo against open land (the pen is a slope).

**Needs the headset.** Whether 2 m is the right range. Whether the body, the size term and the bias of 3 degrees pick the villager that the hand points at in a crowd, in the quest to save the children. Whether the Creature, a miracle and a signpost are easy to choose with the real aim, and whether a signpost in the last rank is still easy to choose with the aim on it. Whether the cone of 8
degrees feels right with the real aim pose. Whether the pull at 0.8 m/s or 12 cm
is easy to do and does not fire by accident. Whether the glide time reads well.
Whether a take at a distance reads well: the halo, the tether and the count at
the Hand. Whether the curve of the stream of that take reads well in depth, how
it looks with a hand that moves, and whether a life of 1 s and 8 pieces a
second read well on a way of 2 m. Whether the halo alone shows the target well against the land. Whether
a ball on the sea gets the halo, the lock and the fetch.

## Many trees from a forest

### What the game does [V]

The plain game has no take of many trees.

- **The Hand holds one object.** `GMagicHand` (`0x1C` bytes, the vtable at
  `0x0092BFC0`) has a count at `+0x14` and one object at `+0x18`. Its add
  (`0x005FAFC0`) refuses when the count is 1. `GInterfaceStatus` has one hand in
  use at most (`+0x90`, `0x005DC2A0`), and `GInterfaceStatus::IsSpaceInHands`
  `0x005DC2F0` says no when that hand has its object.
  `GInterface::PlaceObjectInMagicHand` `0x005DA6F0` tests it first, and a refusal
  resets the interface (`fn_005D1260`).
- **A tree and a forest are no bulk sources.** `ValidForLockedSelectProcess`
  (slot `0x6CC`) is 0 for a `Tree` and for a `BigForest`, and
  `GameThing::RemoveResource` (slot `0xA0`) of a `Tree` gives 0. A press on a
  `Tree` starts the Tug and puts that tree in the Hand. A press on a `BigForest`
  makes one new `Tree` (see "A forest gives a tree").
- **A press with a tree in the Hand takes no second tree.** The press goes to
  `fn_005D1560`. It applies the held object to the object under the Hand only
  when `Tree::ValidToApplyThisToObject` `0x0074BD50` says yes. That is for a
  store of wood (`IsResourceStore(1)`, slot `0x680`) and for one more class (the
  cast at `0x0074BD64`). A second tree is neither, so the press is the drop or
  the throw.
- **The hand state MultiPickUp is never the state of the Hand.** The state
  object exists (`CHand+0x4894`, made at `0x0046BD8C`, vtable `0x00900B6C`).
  `CHand::GetRequiredState` `0x0046CD10` has no path that gives 6. The filter
  `fn_0046CF80` gives 6 for no input, and no other code writes the state
  (`+0x4878`). "Multi pickup" is the game's word for the bulk take of a pile
  (`PSysGlobal::StartMultiPickup`,
  `GPotInfo::GetAmountToPickUpDuringMultiPickup`). During that take the Hand is
  in the Holding state (`0x0046CD5D` to `0x0046CD87`).

The wood of a tree is `Tree::GetWoodValue` `0x0074B7B0`: the scale times the
life times the wood of its `GTreeInfo` (`+0x6C`). A store gets that amount when
the player drops the tree on it (`Tree::ApplyThisToObject` `0x0074BDA0` calls
slot `0x684`, `MultiMapFixed::DeleteObjectAndTakeResource`, and that calls
`Object::DoDeleteObjectAndTakeResource` `0x0063A940`). The trees near the Temple
of Land 1 hold 346 to 599 wood each, and a bush holds 24 to 27.

### The default and the mode `forest_take=1`

The default is `forest_take=0`: one tree for each grip, as in the plain game.
The take of many trees is new behavior and uses only objects and functions of
the game. With `forest_take=1`:

1. Close the grip on a tree. The tree is in the Hand.
2. Keep the grip closed. The take starts after 600 ms, when a second tree is
   within 10 cm of the room of the Hand's point. The tree in the Hand and that
   tree become one bundle of wood. Then one more tree goes in the bundle each 400
   ms, while a tree is in reach. Move the Hand through the forest to get more.
   The card shows the amount ("Amount: N", the game's own text) and "Open to stop
   (N trees)".
3. Open the grip. The take stops, and the bundle stays in the Hand.
4. Close and open the grip again. The Hand lets the bundle go, as it does for a
   bundle from a store. The game gives the wood to a store or to a pile of wood
   near that place. With no store and no pile there, it makes a pile of wood on
   the land.

A grip that opens before the take starts lets the one tree go. A tree that the
player lifts out of the forest stays one whole tree: when no second tree is in
reach for 300 ms, the take is off for that tree. So a tree that the player
carries to a different forest, to plant it there, starts no take. A player who
wants one whole tree must lift it away from the other trees in the first 600 ms,
or leave `forest_take=0`.

**At a distance.** A tree that came with a fetch starts the same wait. The second
tree is then a tree in the cone of the aim (`far_grab_cone_deg`, with the radius
of the tree counted), in the range of the far grab and not behind a hill. Keep
the grip closed and the aim on the forest to collect. Turn the aim away for 300
ms to keep the one tree.

**A `BigForest`** gives the wood of one of its trees at each step
(`BigForest::RemoveResource`, slot `0xA0`, with the amount that
`BigForest::InterfaceSetInMagicHand` takes for the tree that it makes,
`0x00DA3F04`). The game deletes the forest when its wood is gone.

**How** (`forest_take.cpp`, the rule in `forest_take_math.h`):

- The bundle is the game's own object for wood in the Hand: the `PileWood` that a
  take from the wood of a store makes. `Pot::Create` `0x0066CF10` makes it with
  pot info 11 (`PileWood`, `0x0066EC40`), at the place of the Hand
  (`GInterfaceStatus+0x14`), for the nearest town (`MapCoords::GetNearestTown`
  `0x006020E0`), as the Creature's own pick up does (`0x004FFF97`).
- The start changes the object in the Hand. The mod makes the bundle with the
  wood of the tree in the Hand. The tree leaves the hand list (`0x005DC470`) and
  the `CHand` (`CHand::ThrowObject` `0x0046DDD0`), and the game deletes it as a
  store does (`GoolooGooloo` `0x005E6540`, `ToBeDeleted`). Then
  `GInterface::PlaceObjectInMagicHand` puts the bundle in the Hand. The Hand
  stays in the Holding state through the start.
- A tree goes into the bundle with `Object::DoDeleteObjectAndTakeResource`, with
  the bundle as the taker. So the bundle gets the wood that a store gets, the
  game plays its sound of wood, and the tree is deleted.
- The let go is the game's own [V]. `Pot::InitialisePhysicsFromHand` `0x0066DF00`
  calls `fn_0066F270`. That function looks at the 9 map cells around the place
  (`GUtils::Spiral`). An object there that is a store of wood (slot `0x680`), or
  a pot of wood (slots `0x4B4` and `0x690`), gets the wood with `AddResource`
  when the place is in its radius. For the wood that is left, and not on water,
  `fn_005FA8B0` makes a `MagicWood` (the vtable at `0x0092F7A4`): the game's pile
  of wood outside a store. Then the game deletes the bundle of the Hand.
- All of this runs before the interface step of the game (`h_InterfaceStep`),
  where the game itself puts objects in the Hand. The next tree is chosen at the
  end of the first eye pass, from the objects that the draw offered, and the step
  tests that it still lives. Never in a multiplayer game: the calls are not
  packets.
- The grip machine gets the fact `mod_take` (`GripStep`). While the take runs,
  the closed grip presses no button of the game, the opening latches no throw and
  sends no click, and the bundle is then in the Hand with the grip open
  (OpenHeld). The next close carries it, and the next opening lets it go.

The mouse is as before: the take runs only while the controller places the Hand.

### Keys of the forest take

| Key | Default | Meaning |
| --- | --- | --- |
| `forest_take` | 0 | 1 lets the closed grip collect the trees of a forest in one bundle. 0 gives one tree for each grip, as in the plain game |
| `forest_take_wait_ms` | 600 | How long the grip stays closed on the first tree before the take starts (100 to 5000) |
| `forest_take_ms` | 400 | The time from one tree to the next (50 to 5000) |
| `forest_take_cm` | 10 | The reach of the take from the Hand's point to the body of a tree, in cm of the room (1 to 100) |

With `test_keys=1`, Ctrl with the 9 key of the number pad turns the take off and
on while the game runs. The pointing controller gives a long pulse for on and a
short pulse for off. The keys are not in the launcher. The test key
`far_test_tree=1` makes only a tree a far target, and the word `aimfar` then aims
at the nearest tree. The word `seek` with `seek_plan=crown` puts the Hand's point
in the crown of the nearest tree.

### The log and tests of the forest take

Tag `forest take`: the rule at the start. "a tree is in the closed grip", with
how it came (at the Hand, or with a fetch). "no second tree was in reach for 300
ms, so the tree in the Hand stays one tree". "the take starts", with the tree, its
wood and the bundle. "tree N goes in" for the first six trees and then each
tenth, with the distance or the angle and the wood. "the take ends", with the
reason, the count and the wood. "the bundle left the Hand", with what the game did
with it, and the nearest pile of wood on the land. Each report window: the takes,
the trees, the starts that the game refused. Tag `grip`: "the grip opens during a
take from a forest: the take stops, and the bundle stays in the Hand".

The math tests (`forest_checks.h`): the wait, the start with two trees, the rate,
a take with no tree in reach, the opening that ends the take, no new take on the
next close, a grip that opens before the wait is over, a tree that leaves the
forest, a gap in the reach, a tree with no forest, an object that is no tree, a
bundle that leaves the Hand, a start that the game refused, the gap from the
Hand's point to the body of a tree, the angle from the aim, and the grip machine
during the take.

Desktop runs have shown the whole cycle at the Temple of Land 1: a tree in the
closed grip, the start of the take, the trees going in, the opening that keeps
the bundle, and the let go that leaves one pile of the game's `MagicWood` on the
land with all the wood. They also showed a `BigForest` giving a tree at each
step, a take from a fetch at a distance, and `forest_take=0` giving one tree.

**Not proven on the desktop:** a throw of the bundle with a swing, a bundle that
the Hand lets go over a store or from high in the air (where that wood goes is
not proven), the sound, a forest outside the influence, and a game that the
player saves with the bundle in the Hand.

**Known limits.** The hint "Hold to collect" of the wait almost never shows: the
game has its own hint for a tree in the Hand ("[Grip] Plant"), and the card shows
that one. A `BigForest` gives the wood of one tree at each step with no tree that
leaves the picture, until the game deletes the forest. The take changes the tree
in the Hand into wood.

**Needs the headset.** Whether 600 ms and 400 ms feel right, and whether 10 cm is
the right reach at the scales of the headset. Whether one whole tree is still easy
to take from a forest. Whether the take at a distance starts when the player does
not want it, after a fetch of one tree.

## An object in the Hand that goes onto a thing

The core loop of a village is to carry a thing to a place: wood and food to the
Village Store, wood to a building site, a scaffold to a second scaffold. With an
object in the Hand the pick record would hold the object behind the pixel of the
Hand's point, and not the thing at the Hand. The code is `VillageSelect`,
`VillageKeepRecord` and `VillageHalo` in `src/mod/village.cpp`, with the pure
rules in `village_math.h`.

### What the game does [V]

**The press.** A press of the Action button with an object in the Hand goes to
`FUN_005D1560`. It does these tests in this order:

1. The thing is `CHand::GetCreatureToGiveTo` `0x0046E4B0`, or else the object of
   the next pick record (`GInterface+0x400`). With no thing the press sets the
   object down on the land point of the record.
2. The thing must be a target (`ValidAsInterfaceTarget`, slot `0x6F0`).
3. A Creature that takes the object (`InterfaceValidToGiveObject`, slot `0x748`,
   only `Creature` at `0x0047A330`): `fn_005D3080` sends packet `0x1F`, and the
   Creature takes the object.
4. A thing that needs the influence (slot `0x714`) with the land point of the
   record outside the influence: the press sets the object down.
5. The object in the Hand says yes to the thing (`ValidToApplyThisToObject`, slot
   `0x71C`, the status and the thing): `fn_005D30D0` sends packets `0x4D` and
   `0x11`, and the object goes onto the thing. Else the press sets the object
   down.

**The Creature that takes an object.** `HandStateHolding::Update` reads the
object of the pick record of now (`GInterface+0x3C8`). When it is a Creature that
says yes to the object in the Hand, the Hand keeps it at `CHand+0x49B4` for 1100
ms (the timer at `0x00D13F3C`). So the record must hold the Creature before the
press. What the game does after the press, and how it ends a gift, is in
`creature.md`, section 1.8.

**What each object goes onto** (`ValidToApplyThisToObject` of its class):

| In the Hand | Function | It goes onto |
| --- | --- | --- |
| A pot of food or wood (`PileFood`, `PileWood`, `MagicFood`, `MagicWood`) | `Pot` `0x0066DD50` | A store of its resource. A pot of the same resource (a pile) |
| A tree, a felled tree | `Tree` `0x0074BD50`, `DeadTree` `0x00510E90` | The altar of a Worship Site (a `WorshipTotem`). A store of wood |
| A crop of a field | `FieldCrop` `0x00608360` | A store of food |
| An animal | `Animal` `0x0041B320` | The Village Store (a `StoragePit`). The altar |
| A villager | `Villager` `0x00752BD0` | The altar. A `MagicTeleport` that takes him |
| A scaffold | `Scaffold` `0x006E9900` | A second scaffold. A building that stores wood |
| A rock, a part of a building | `MobileStatic` `0x00608BB0` | A store of wood, when the object passes a test of its own (slot `0x3CC`, not traced) |
| A ball, a one-off miracle seed, a reward | `MobileObject` `0x00607B70` | A store, only for three kinds of object that are a resource. No thing takes a one-off seed |
| A miracle | `SpellSeed` `0x007286D0` | See [miracles.md](miracles.md) |
| Each other object | `Object` `0x004028B0` | Nothing |

**The stores of a resource** (`IsResourceStore`, slot `0x680`, with 0 for food and
1 for wood):

| Thing | Function | It stores |
| --- | --- | --- |
| The Village Store (`StoragePit`) | `0x0055CD20` | Food and wood |
| Each building (`MultiMapFixed`) | `0x0052F1F0` | Wood, while it has a building site (`+0x74`): it is not complete, or it needs a repair |
| The Workshop | `0x0077A650` | Wood, always |
| The Worship Site | `0x0077DEC0` | Food, always (the food of the worshippers) |
| A scaffold | `0x006EAEA0` | Wood, when it is planted (it has a building site) |
| A tree, a rock, a pot | | Nothing |

Food has no villager as its thing: a villager is no store. A tree and a rock that
go onto no thing go on the land.

**A villager and a job.** The job does not use the press. While the Hand carries
a villager, `Villager::InHand` `0x0076ADE0` asks each object near the place of the
villager for a job (`GetDiscipleStateIfInteractedWith`, slot `0x644`), once a game
turn:

| Thing | Job |
| --- | --- |
| A tree of the player (`Tree` `0x0074C150`) | 2, the forester |
| A field (`Field` `0x00529FB0`) | 1, the farmer. 4 while the field is not complete |
| A building that is not complete (`MultiMapFixed` `0x0052F0D0`) | 4, the builder |
| The Workshop (`0x0077A340`) | 8, the craftsman |
| The Worship Site of his town (`0x0077DE70`) | 11, the worshipper |
| A villager (`0x007560E0`) | 5, the breeder [I] |
| The complete Village Store of his own town (`0x00733A20`) | None |

The numbers are the values that the functions give. The names are those of the
game's list of disciples. A thing counts when it is within 6 units of the villager
(`Villager::FindCloseObjectsForInteract` `0x00756E80`). So the Hand must have the
villager low at the thing: 2 units above the base of a tree works. The map point
of the villager is the place of the Hand. The game gives a job other than 11 only
to a villager that passes its tests (`0x0076AE6D` to `0x0076AEBC`: two flags and
four slots of the villager, not traced). A villager who does not pass gets no job.
That is the rule of the plain game too.

### What the mod does (`hold_target=1`)

`VillageSelect` runs at the end of the first eye pass while the Hand carries an
object (state 4). It does not run for a miracle, which has its own target, and not
while the game takes from a bulk source, which must stay in the record. For each
object that the draw offered it does the tests of the press: the object is a
target, and it is a Creature that takes the object in the Hand, or the object in
the Hand says yes to it.

**The thing at the Hand.** The body of a thing is the body that the choice of the
grip uses (`Measure` in `hand_direct.cpp`): a cylinder of its radius and its
height, or the capsule of a Creature. The gap is 0 in the body. The mod takes the
smaller of two gaps: from the Hand's point, and from the place of the object in
the Hand. The target is the thing with the smallest gap within `hold_target_cm` (6
cm of the room, `village_math::HoldTakes`).

**No reach for a sacrifice and for a Creature** (`HoldReach`). The altar of a
Worship Site takes a villager, an animal and a tree as a sacrifice, and a Creature
takes most objects. For these two the Hand's point, or the object in the Hand, must
be in the body. So a villager that the player sets down beside the altar is no
sacrifice, and a Creature that stands near the Hand does not take the wood.

**A Creature that is the target stays the target** (`hold_creature_stay=1`,
`HoldReachNow`). A Creature that was the target in the last frame has the reach of
each other thing, `hold_target_cm`. Its body is small and it moves, so with no stay
the halo comes and goes while the hand is almost still. The gift itself is in
`creature.md`, section 2.9.

**The record, the halo and the card.** `VillageKeepRecord` puts the target in the
next pick record after each eye pass, before each interface step and before each
press. The target has the gold halo of a target of the grip (`VillageHalo`,
`kHaloHold`). The halo is red when the thing needs the influence and the land
under the Hand is outside it. The card has one line for the grip:

| In the Hand | At | Line |
| --- | --- | --- |
| Wood | The Village Store | "[Grip] Open to store the wood" |
| Food, a crop | The Village Store | "[Grip] Open to store the food" |
| Wood | The Workshop | "[Grip] Open to give the wood to the Workshop" |
| Wood | A building that needs wood, a planted scaffold | "[Grip] Open to give the wood to the builders" |
| Food | The Worship Site | "[Grip] Open to give the food to the worshippers" |
| Wood or food | A pile of the same resource | "[Grip] Open to add to the pile" |
| A tree | A store of wood | "[Grip] Open to give the tree as wood" |
| An animal | The Village Store | "[Grip] Open to store it as food" |
| A villager, an animal, a tree | The altar | "[Grip] Open to sacrifice it here" |
| A scaffold | A scaffold | "[Grip] Open to join the scaffolds" |
| A scaffold | An other thing | "[Grip] Open to put the scaffold here" |
| A villager | A teleport | "[Grip] Open to send the villager through" |
| A villager | A thing that gives a job | "[Grip] Open to make a forester", with the job of the game (`village_math::JobName`) |
| Each object | A Creature that takes it | "[Grip] Open to give it to the Creature" |
| Each other pair | | "[Grip] Open to put it here" |

The line of a job needs no target: the game sets the job, and the mod reads it from
the villager (`Villager+0xF2`). Outside the influence the card adds "This place is
outside your influence". With no target the record, the hint of the game and the
card are the game's own. A bundle that the Hand holds with the grip open goes onto
the target at the next close and opening of the grip.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hold_target` | 1 | 0: the pick record of an object in the Hand is the object behind the pixel of the Hand's point |
| `hold_target_cm` | 6 | The reach of a thing, from the Hand's point or from the object in the Hand to its body, in cm of the room. 1 to 50 |
| `hold_creature_stay` | 1 | 1: a Creature that is the target stays the target to `hold_target_cm` from its body. 0: a Creature is the target only with the Hand's point or the object in its body |

`scaffold_join` and `scaffold_join_cm` are second names of the two keys. The mod
reads them when `hold_target` and `hold_target_cm` are not in the file. The keys
are not in the launcher.

**The A/B key**, with `test_keys=1`: Ctrl with the 2 key of the number pad changes
`hold_target`. The pointing controller gives a long pulse for 1 and a short pulse
for 0.

Test keys: the word `seek` has these places in `seek_plan`: `gostore` (the God view
teleports to 60 cm of the room before the Village Store), `store` (the Hand's point
goes 3 units above the point of the store), `pile` (the nearest bulk source),
`villager` (the nearest plain villager that the Hand can take and that passes the
tests of the game for a job), `treebase` (2 units above the base of the nearest
tree), and `goshop` and `shop` for the Workshop. The groups of `seek_go` can have a
bar between them, because a list of keys in a script has the semicolon between its
keys. The test key `village_test_workshop` is a frame: the first town of the player
gets a complete Workshop on the land under the Hand (`Abode::Create` `0x00402E20`
with the info of a Workshop of its tribe).

### The log and tests

The tag `village`: the key at the start, and each press of the A/B key. "the ... in
the Hand can go on the ...", with the gap, the reach and the line of the card.
"... has no thing to go on now". "the game puts the ... in the Hand on the ...
(packets 0x4D and 0x11)", from the hook of `fn_005D30D0`, with whether the thing
had the halo. "the villager ... in the Hand has the job N now", for each change, and
"the villager ... left the Hand with the job N". "the game has a place for a
building for the scaffold ... in the Hand", and "... no place ...". Each report
window: the frames with a target, the times the mod put it in the record, and the
times the game put an object on a thing. The tag `hand press` has "the grip pressed
with ... in the Hand. The pick record holds ...".

The math test (`village_checks.h`): the choice with the two gaps, the reach, no
reach for the altar and for a Creature, the reach of a Creature that was the target,
and the line of the card for each pair of the tables above.

Desktop runs (the God view, `village_run.ps1`) have shown wood at the Village Store,
a tree at the Village Store (the card says "Open to give the tree as wood", and the
store has more wood after the opening), two scaffolds, wood at the Workshop, wood at
a building site and a scaffold, and a villager at a tree and at the Village Store.
Desktop runs of `creature_recipe.ps1 -Kind gift` have shown an animal given to a
Creature (`creature.md`, section 7.2).

**Not run on the desktop:** food, an animal and a crop at a store (the same
code with a different class), the altar and a sacrifice, a teleport, a thing outside
the influence (the red halo and its line), a villager at a field, at a building site
and at the Workshop, and a scaffold that the Workshop makes.

**Needs the headset.** Whether 6 cm is the right reach for a store and a scaffold.
Whether the halo of a large thing (the Village Store has a body of 15 units, 35 cm of
the room) reads as "the wood goes here". Whether a player gives an object to the
Creature with no effort: the Creature becomes the target in its body, and it stays
the target to 6 cm.

**Limits.** The draw offers a thing only when the point of its mesh is near the Hand.
A very large building with its point far from the Hand's point can be no target, and
then the game's pick at the pixel acts. The mod does not change where an object lands
when no thing takes it.
