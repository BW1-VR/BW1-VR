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
   once. The object is in the Hand about 10 frames later.
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
| Carry | An object came into the Hand while the grip is closed, and the pick up is over: the pick lock `GInterface+0x38` is clear, or 15 frames passed | Up |
| OpenHeld | An object is in the Hand and the grip is open, for example after the game refused a drop | Up. A close carries it again |
| LetGo | The grip opened while it carried | The mod's click: down, then up when the hand mode is `0x17` on a later frame, when the object left the Hand, or after 8 frames |

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
- **The grip opens before the object comes** (during the 10 frames of the pick
  up): the take press ends at once, and the game stops the pick up, as for a
  short mouse click. If the object still comes within 20 frames, the machine
  lets it go with a click.
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
Hand. Tag `hand throw` adds the time between the opening and the release.
Each report window counts the carries, the openings, the clicks and the waits.

**Needs the headset.** Whether 3 to 4 frames from the opening to the release
feel at once. Whether the window of 80 ms before the opening gives the throw
the player meant. Whether a still hand sets the object down where it is.

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
| The pick up of an object, the take from a pile, a scroll, a house (`Choose`) | The nearest body within `hand_pick_cm` (4 cm of the room) of the Hand's point |
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
5. Last, a scroll that is past the reach of the pick and inside the reach of a
   scroll (`scroll_reach_cm`, 10 cm, see [village.md](village.md)). It wins
   only when no other body is in the reach of the pick.

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
the middle of the body, so the object hides the middle of the disc.

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
Creature. `Choose` keeps the smallest gap of them (`NearBlocks`):

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
2. An object within 10 to 15 cm of the Hand's point: no far target. The player
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
| `near_rule` | 2 | 2 is one target. 1 is the nearest body within the reach, with the game's pick at the pixel for an object that is not near, and the search at the palm. 0 gives the pick of the first builds: a reach of 60 percent of the Hand's length, no tap choice, no rule of the near objects and no far touch |
| `hand_pick_cm` | 4 | The reach of the pick from the Hand's point, in cm of the room. 1 to 20 |
| `hand_touch_cm` | 1 | The point touches a body when the gap is this or less, in cm of the room. 0 to `hand_pick_cm` |
| `catch_moving` | 1 | 1 gives an object that moves a longer reach of the pick (see "A target that moves, floats or dies"). 0 gives each object the reach of `hand_pick_cm`. The test key Ctrl Page Up goes between the two (`test_keys=1`) |
| `catch_lag_ms` | 100 | The time that the speed of the object counts for, in ms. 0 to 500 |
| `catch_cap_cm` | 8 | The most extra reach, in cm of the room. 0 to 20 |
| `catch_grace_ms` | 600 | The object that the Hand let go has the plain reach for this time, in ms. 0 to 3000 |
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
distance of a far target, and the halo.

**Not proven on the desktop:** the body of a tree and the game's height in the
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
  amount into a new `Pot`, puts the Pot in the Hand, and starts the effect and
  the sound (`PSysGlobal::StartMultiPickup` `0x0068F8C0`). Then `FUN_005DC110`
  writes the source to `GInterfaceStatus+0x3C` and 0 to `+0x40`.
- **Each game turn.** `GInterfaceStatus::Process` `0x005DC4E0` adds 1 to `+0x40`
  and calls slot `0x808` (`ProcessInInteract`, `0x0066E520` for a pile). The
  amount of a turn grows with the turns (`fn_0066CD30` and
  `GPotInfo::GetTicksToChangeOver`). The call plays a tick (`fn_0068F930`,
  `GAudio::PlaySoundEffect`), moves the amount from the pile to the Pot, and
  shows the count with `ToolTips::ForceToolTips(0xEEA, Pot+0x70)`. Text 3818
  is "Amount: %3.0f". The game shows no count when the help options of the
  profile are "No ToolTips" (see [ui-layer.md](ui-layer.md)). For a source that
  is not a Creature, a turn also ends the take when the Hand's own position
  (`GInterfaceStatus+0xC8`, set from the Hand at `+0xA4` in `fn_005DBC60`) is
  over land outside the influence (`0x005DC540`).
- **The end.** The release of the right button (`GInterface+0x39` bit `0x40`)
  runs `FUN_005D4830`, and `FUN_005D2210` sends packet `0x1C`. `FUN_005DAA10`
  then calls slot `0x6EC` (`StopMultiPickup` stops the effect and the sound) and
  writes 0 to `+0x3C`. The Pot stays in the Hand, in the Holding state. A click
  drops it as any object, and `Pot::InitialisePhysicsFromHand` pours it out
  (`StartMultiPutdown`).
- **The Hand.** During a take the Holding state asks slot `0x6A0`
  (`IsLockedInInteract`) of the source (`0x005B3EB3`) and holds the Hand over
  it. The direct hand moves the Hand to the controller, as for any held object.
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
(`0x00402810`) and gives 1 for each class. So a release ends the take only when
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
   button. When the button was up for `kGripTakeEndFrames` (18 frames, two game
   turns at 90 frames a second) and the take still runs, `GripStep` sets
   `end_take` on each frame. `EndTakeNow` then acts before the next interface
   step. In action 4 it sets the release bit for that one step. In actions 5 and
   6 the record is enough. In any other action it sets the pick lock and calls
   `FUN_005D49B0`. If the take still runs 18 frames later, it calls the handler
   of the end packet (`FUN_005DAA10`), clears the pick lock and calls
   `ResetActionState`. The handler never runs in a multiplayer game.
5. **The take ends at once.** After the step that reads the release, the mod
   asks for a game turn at once, so the end packet runs on the next frame and
   not up to 100 ms later.

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

**Not tested:** the sound of the tick, a field and a fish farm, a store inside
the real influence, and a take with the hand pulled away from the store.

**Needs the headset.** Whether the count reads at the hand, the tick and the
effect. Whether the Hand stays at the controller during a take. A short squeeze
on a pile must stop when the grip opens.

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
are in `far_choice_math.h` (the choice by the body) and `controller_math.h`
(`FarConeScore`, `FarPickTarget`). The key `far_grab_choice` (default 1) picks
the rule:

- **`far_grab_choice=0`, one point.** `te = max(0, t - atan(r / d))`, with t the
  angle off the ray to the base of the object (the mesh translation), r the
  `Get2DRadius` of the object and d its distance from the ray's origin. The
  height of the object does not count.
- **`far_grab_choice=1`, the body (default).** The angle is measured to the
  nearest point of the upright body of the object: the line from the base up to
  `GetHeight` (two radii when the game gives none), with the radius around it
  (`BodyScore`, which uses `AimAtBody`). A body covers at least 2 degrees of the
  view (`kMinCoverDeg`). A forest, a scroll and the Creature keep the point.
  When the ray goes through two bodies, a small one comes first: the score adds
  0.5 degrees for each log2(1 + size), with size the larger of the radius and
  half the height in world units (`kSizeDeg`). A rock of 0.5 comes 1.2 degrees
  before a tree of 7.5. A living thing (a villager, found by the class name, an
  animal, found by slot `0x454` `IsAnimal` [V], and your Creature) scores 3
  degrees better than its angle (`kLivingBiasDeg`, `RankedScore`). With the size
  term, a villager wins over a tree on the ray when it is about 4 degrees
  further from the ray than the tree, and over a rock when it is about 3 degrees
  further. With no living thing near the ray the order is the plain score. The
  bias is in the score, so the hysteresis below keeps a living target against a
  tree. The log line of each new target says why it won and names the second
  best ("Why: a living thing comes first. The second one is ... (Tree) at 0.0
  degrees, score 2.50 against -1.66").
- An object behind the ray is out. An object less than 0.35 m of the room from
  the Hand's point is a near object, and it is out too (`kFarMinHandM`, or the
  `FarLeastMeters` of "Near objects win"). A near object of `Choose()` always
  wins, and then there is no far target.
- The score is `te + 1 degree x log2(1 + meters from the Hand)`, less the bias.
- A new target needs `te <= 8` degrees (`far_grab_cone_deg`). The target stays
  while `te <= 14` degrees. Another object takes over only when its score is 2
  degrees lower (`FarPickTarget`).
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
  touch (see below).
- The player's own Creature, for the leash: the grip changes the leash with no
  pull and no press of the game (`near_math::LeashClick`, key `far_leash`, see
  [creature.md](creature.md)).
- No other object that a press taps. Else the aim would knock on each house that
  it crosses, and a press on the entrance of the Temple from 2 m would take the
  player into the Temple [I].

### The claim, the pull and the fetch

The states are Idle, Aim, Lock, Pull, Fetch and Take (`far_grab.cpp`).

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
  When `CHand+0x4904` is the object, the fetch is done. After 40 frames
  (`kFetchFrames`) without it, the fetch ends and the grip stays held back until
  it opens. For a forest the fetch waits 90 frames (`kForestFetchFrames`),
  because the game makes the tree after 225 ms.
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
A press that the game does not answer in 40 frames ends as a fetch does. The
rule of "The bulk take" applies: hold the grip to collect, open it to stop and
keep the bundle, close and open it again to drop.

On each game turn the game ends a take when the Hand's own position is over land
outside the influence. So the halo of a bulk source is gold only when the land
under the Hand is inside the influence too.

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
  intersect, and a raised object falls.
- The land under the tree is dry land, or shallow water of the land block.
- The mesh of the tree leans at most 0.2 rad about x and about z
  (`LHMatrix::GetYXZ` `0x007FAB30`).
- The tree is not on fire (`Tree+0x44`).

A tree that falls has no flag 8, because only the release sets it. When it
comes to rest, `EndPhysics` makes a `DeadTree` (`fn_00510B70`) in its place
[I]. A desktop run showed the two ends: a tree set down at 2.4 cm of the room
was a `Tree` in a forest 25 frames later, and a tree let go at 45 cm never
left the physics as a `Tree` (the dead tree path does not clear its flag).

**The card.** The text "Plant" of the game is the same for each tree in the
Hand. With `tree_plant_hint=1` the card tests the same things in each frame
(`tree_plant_math.h`, `HandDirectTreeVerdict`): the height rule of
`release_fall_cm` (throw.md), the lean, dry land (`MapCoords::IsDryLand`) and
fire. When the release plants the tree, the card keeps the text of the game.
Else it says what the opening does: "Lower to plant. Open to drop", "Hold
upright to plant. Open to drop", "Needs dry land. Open to drop" or "On fire.
Open to drop". The test of a body in the way is not in the card: the game works
it out in the physics at the release.

**The log.** Tag `hand tree`: the settings, each roots pile that the game makes
(with the place), each change of the answer for the tree in the Hand (the
height, the lean, dry land, fire), the release, and what the tree is when it
leaves the physics (its class and its forest).

**Open.** A body in the way is not predicted. A tree held by the Hand at an angle
of more than 0.2 rad is not seen in a desktop run (the hand of the script keeps
the tree upright). Whether the pile at the roots is what the player means by the
"pit" needs the headset.

### The far touch

A scroll in the cone of the aim ray is a far target, with the range of
`far_grab_max_m`. It has the gold halo. It needs no pull:

1. The grip closes on the scroll.
2. For 2 frames the scroll is in the next pick record (the Pull state, as for a
   bulk source).
3. The grip goes to the game as a press (the Fetch state). The game taps the
   scroll with its own action, as in "A scroll, a house".
4. After 8 frames (`kTouchFrames`) the far grab lets the press go. The grip
   stays held back until it opens.

A scroll scores 3 degrees better than its angle (`kFarTouchBiasDeg`), because
trees stand all around a scroll. `far_touch=0` turns the far touch off.

`FarDraw` (`far_grab.h`) has the fields `touch` (the target is a scroll, and the
grip activates it) and `bulk` for the hint card, so the card can say "Activate"
in place of "Pull to fetch". `HandDirectPressKind()` gives what the grip does at
the Hand's point (nothing, hold, take, tap or the Creature).

### A target that moves, floats or dies

The game takes the object of the record by its pointer, where it is, so the
fetch needs no change for an object that moves. The game also takes an object
that is in its physics: a rock that falls or flies after a release is a far
target, and the fetch puts it in the Hand while it moves.

- **The pick reaches farther for an object that moves** (`catch_moving`). The
  player closes the hand on the place where the object was a moment ago: the
  eye, the hand and the closing of the grip take time, and the pick of a frame
  is one frame old. So the reach of the pick for an object that the Hand can
  hold is `hand_pick_cm` plus `near_math::MovingExtra`: the speed of the object
  less 15 cm a second (`kCatchStillCmps`), times `catch_lag_ms`, and at most
  `catch_cap_cm`. The speed comes from the place of the object in the draw of
  this frame and of an earlier frame (`ObjectSpeedNow`, once a frame, for an
  object that the draw offered in the last 3 frames). The first cut of
  `Choose` is as long as the cap. The log has one line for each object that
  only this reach chose ("moves at"). The object that the Hand let go has the
  plain reach for `catch_grace_ms` after the release (`JustLetGo`), so a throw
  does not come back to the hand in the same closing of the grip. Only the
  pick up has the extra reach: a scroll, a house and a pile keep theirs. The
  gap between this reach and `far_grab_near_out_cm` (15 cm) is still a gap
  with no target for a moving object, as for a still one.

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

### Show

- **The halo** (`hand_glow.cpp`, `DrawHalo`) is the disc of the glow, gold, or
  red outside the influence. It is centered one radius over the base of the
  object. Its radius is `1.35 r + 1 cm` of the room, at least 2 cm and 1.2
  percent of the distance. The depth test uses a point one radius behind the
  center (`GlowProject` takes a negative pull). So the object hides the middle
  of the disc, and the light shows around its outline. Nothing is drawn on the
  land under the target.
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
| `far_grab_choice` | 1 | 1 measures the angle to the body of the object, puts a small body before a large one and a living thing before a tree or a rock (see "The cone and the choice"), and gives a Creature command 10 m of range. 0 is the choice by one point of the object, and 4 m for the command |
| `far_grab_pull` | 80 | The speed of the pull, in cm/s |
| `far_grab_max_m` | 2.0 | The range: a target is at most this far from the head, in meters of the room along the floor. 0 gives no limit |
| `far_grab_near_cm`, `far_grab_near_out_cm` | 10, 15 | See "Near objects win" |
| `far_touch` | 1 | 1 makes a scroll in the aim a far target that the grip activates |
| `far_leash` | 1 | The leash click on the player's own Creature at a distance (see [creature.md](creature.md)) |
| `far_roots_at_tree` | 1 | 1 puts the roots pile of a tree that comes from a distance at the roots, on the land where the tree stood. 0 leaves it where the game puts it, on the way of the glide. Test key: Ctrl, Shift and Page Down |
| `tree_plant_hint` | 1 | 1 makes the card say what the opening of the grip does with a tree in the Hand. 0 shows the text of the game ("Plant") each time. Test key: Ctrl and Page Down |
| `throw_law` | 2 | The law of the throw (see [throw.md](throw.md)) |

With `test_keys=1`, Ctrl with the Delete key changes `far_grab_choice` between 1
and 0 while the game runs. The pointing controller gives a long pulse for 1 and
a short pulse for 0, and the log has a line.

Test keys for the desktop runs only:

- `far_test_influence=1`: every land point is inside the influence.
- `far_test_no_trees=1`: no tree is a far target.
- `far_test_thrown=1`: the word `aimfar` aims at the object that the Hand threw
  last, where the draw saw it, so a run can fetch an object that flies, rolls or
  is in the sea.
- `far_test_least_cm=N`: the word takes an object N cm or more from the Hand,
  and not 50 cm.
- `far_test_aim_class=N`: the word `aimfar` aims at the middle of the body of the
  nearest villager (1), living thing (2), rock (3), your Creature (4) or tree
  (5). `far_test_aim_toward=1` (a tree) or 2 (a rock) then turns the aim toward
  the nearest such object, 20 units or less from the target, by
  `far_test_aim_blend_pct` percent of the way. The same aim then runs with
  `far_grab_choice=0` and `1`, and the log of each new target names the winner
  and the second best. The start of Land 1 has no villager in view, so the
  runs aim at rocks and trees.

The fake controller has two words. `aimfar@F+n` turns the aim of the right hand
to the nearest far target: a holdable object that is no tree, more than 0.5 m of
the room from the Hand, in the range of `far_grab_max_m` from the head, inside
the influence and seen, in the God view or on the table. With `seek_bulk=1` it
aims at the nearest bulk source. The hand stays where it is. `pull@F+n` steps the
right hand 15 cm toward the head over n frames, and the next `pull` steps it
back.

### The log of the far grab

Tag `far grab`: each new target with its class, the angle and the distance, the
lock, the pull (the speed along `p` and the move in cm), the press, "the Hand
holds" with the frames after the press, each frame of the glide, each end of a
lock or a fetch with the reason, and one line each report window. Also: how far
the object moved while the lock held, a locked target that goes in or out of the
influence, a target that the game deleted ("the target is gone"), and how often
the locked target went to the pick. "the grip closed on the scroll", "the far
touch is over", "the forest P gave the new object Q". Tag `hand throw`: the
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

Desktop runs have shown the fetch of an object at rest, a fetch of an object in
flight (the lock follows it), and the end of a lock when the game deletes the
object. Desktop runs (God view, `fake_controller=2`, Land 1, `-NEWGAME`) use the
dialog recipe of [god-view.md](god-view.md), section 12, then the pen from about
frame 2750.

**Not shown in a run:** a villager beside a tree (the start of Land 1 has no villager in view, so the runs use a rock and a tree), a fetch of an object over the sea or in it, an object
with its point under the sea plane, a locked target that drifts out of the
influence, a villager in the sea, the tether and the halo during a take at a
distance, a `Field` and a `FishFarm` as a far target, a take with the Hand over
land outside the influence, a multiplayer game, a fetch of a villager, a
Creature or a building, the haptics on a real controller, and the look of the
halo against open land (the pen is a slope).

**Needs the headset.** Whether 2 m is the right range. Whether the body, the size term and the bias of 3 degrees pick the villager that the hand points at in a crowd, in the quest to save the children. Whether the cone of 8
degrees feels right with the real aim pose. Whether the pull at 0.8 m/s or 12 cm
is easy to do and does not fire by accident. Whether the glide time reads well.
Whether a take at a distance reads well: the halo, the tether and the count at
the Hand. Whether the halo alone shows the target well against the land. Whether
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
press.

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

`scaffold_join` and `scaffold_join_cm` are older names. They still count when the
new keys are not in the file. The keys are not in the launcher.

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
reach for the altar and for a Creature, and the line of the card for each pair of
the tables above.

Desktop runs (the God view, `village_run.ps1`) have shown wood at the Village Store,
two scaffolds, wood at the Workshop, wood at a building site and a scaffold, and a
villager at a tree and at the Village Store.

**Not run on the desktop:** food, a tree, an animal and a crop at a store (the same
code with a different class), the altar and a sacrifice, a teleport, an object given
to a Creature, a thing outside the influence (the red halo and its line), a villager
at a field, at a building site and at the Workshop, and a scaffold that the Workshop
makes.

**Needs the headset.** Whether 6 cm is the right reach for a store and a scaffold.
Whether the halo of a large thing (the Village Store has a body of 15 units, 35 cm of
the room) reads as "the wood goes here". Whether a player gives an object to the
Creature with no effort when the object must be in its body.

**Limits.** The draw offers a thing only when the point of its mesh is near the Hand.
A very large building with its point far from the Hand's point can be no target, and
then the game's pick at the pixel acts. The mod does not change where an object lands
when no thing takes it.
