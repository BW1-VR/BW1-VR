# The Creature and the hands

This doc covers how the player touches, commands and leashes the Creature
with the VR hands. It has four parts: how the plain game does it, the
Creature by touch (the open hand, the stroke, the slap), the commands (the
trigger on the land and on a thing), and the leash (the tie, the leash from a
distance, the leash on an object).

Marks: [V] verified in the disassembly of `runblack.exe` 1.42, [I] inferred,
[G] a guess.

Other docs:

- `hand.md`: the state machine of the game's Hand, the depth hook, the
  direct hand.
- `grab.md`: the grab from a distance (cone, halo, "near objects win").
- `hand-model.md`: the model of the Hand, the poses, the glow, the haptics.
- `god-view.md`: the God view and the teleport (the arc that the land
  command uses).
- `ui-layer.md`: the Creature panel (`creature_panel`).
- `controllers.md`: the buttons and the pointer.

## Summary

- The plain game has one Creature state of the Hand (state 7). A press on the
  Creature starts it. In it, a fast move across the Creature is a slap and a
  slow move is a stroke. A short press is the leash click.
- The game also moves its camera to the Creature at the press. The mod does
  not (`creature_camera=2`), so the God view stays still.
- The Hand stays at the controller. A body capsule gives the contact. A hand
  that is not in the capsule touches nothing.
- The hand of a stroke is open: the index finger when the Creature is small
  in the player's view, the flat hand when it is large.
- With the leash on the Hand, the trigger commands the Creature: a tap or a
  hold on the land sends it there, a press on an object makes it act on that
  object, and a double press ties the leash to the object. A shake of the
  real hand takes the leash off.
- The grip from a distance puts the leash on and takes it off. It does not
  start the Creature state.

## 1. How the plain game does it

### 1.1 Stroke and slap: `HandStateCreature::Update` `0x005B17B0` [V]

Thiscall `(float dt, LHMatrix*)`. The Creature's `LH3DCreature` is
`hand+0x49B8`. Let `S = 15 * LH3DCreature+0x90` (the Creature's size).

**Inputs it computes each frame:**

- **Hand speed.** It unprojects the current and the previous screen point
  (`+0x485C`, `+0x4864`) at the Hand's distance `+0x4854` with
  `ScreenPointToWorldAtDistance` (`0x005B1B32`, `0x005B1B4E`). It divides the
  x and z difference by `dt` (the first stack argument, read at `0x005B1B79`).
  The y difference is not used, so a downward slap has no speed.
- **Contact.** It casts the camera ray through the Hand's screen point against
  the Creature mesh (`fn_00865020`, return address `0x005B19F1`). When that
  misses, it casts the ray of the pixel between the current and the last
  screen point (`0x005B1ABC`). A hit anywhere along the ray counts, also far
  behind the Hand.
- **Height.** `h = (hand.y - land altitude) / S`, from `hand+0x7C`.
- **Direction.** The sign of the screen x change (`0x005B1E15`). It chooses
  the side of the slap only. The slap test has no direction.

**Slap.** The slap branch (`0x005B1C0A`) runs when the ray hits and the state
field `+0x110` is 0 (the first frame of contact), or when the screen x
crossed the screen center this frame: `(x - W/2)(x_last - W/2) < 0`, with `W`
from `0x00E85058`. Then a slap needs all of these:

| Test | Value |
| --- | --- |
| height | `0 < h < 1.1`. Low below 0.4, middle below 0.7, high above |
| time in the state (`+0xDC`) | more than 1000 ms. `Update` only adds the frame time to it, and a stroke sets it to 0 (`0x005B226C`) |
| speed | more than `5 * S` per second |

A speed of `9 * S` or more counts a hard slap. After three hard slaps it
sends packet 99 with a strength from 0 to 7. Each slap forces a Creature
action, starts Immersion effect 1 or 2, sends packet `0x27`, and subtracts
0.1 from the total at `+0xD8` (0.2 when the total is above 0.25). A slap sets
`+0xCC` to 0 and `+0xD0` to 1000 (`0x005B1E25`, `0x005B1E2F`). A speed above
`5 * S` also sets the Hand animation `0x21` (`+0x114`).

**Stroke.** Otherwise, while the ray hits, it finds the nearest of nine body
bones to the hit point. It adds the frame time to `+0xD4`. A stroke plays
when all of these are true (`0x005B2154` to `0x005B2193`):

- `+0xD4` is above 1000 ms.
- The screen point moved this frame (`+0x4858`).
- `+0xD0` is above 2000 ms (the time since the last stroke animation).
- `+0xCC` is above `(1 - total) * 1250` ms.

It then plays the stroke of that bone, sends packet `0x27`, and adds 0.1 to
`+0xD8`. Immersion effect 0 plays while the ray touches.

**Size and place of the Hand.** The state calls `CHand::SetDistanceFromView`
twice (`0x005B196C` and `0x005B2044`) with a distance from a Zoomer in the
state (`+0x9C`) that follows the Creature's body. It multiplies the Hand's
scale `+0x4834` by `size^0.5` (`0x005B1975`, `0x005B204D`). Then
`ScreenPointToWorldAtDistance(+0x485C, +0x4854)` writes `+0x78`
(`0x005B23C4`). So the state sizes and places the Hand from the Creature.

**The first eye pass only.** `CHand::PrepareForDrawing` copies `+0x485C` to
`+0x4864` and then reads the pointer (`0x0046C6A7`). It runs once per eye
pass. In the second eye pass the previous point is this frame's point, so the
speed is zero and `+0x4858` is 0. A stroke or a slap can start only in the
first eye pass, with the mouse and with the mod.

**The lesson.** `+0xD8` is clamped to -1 and 1. When the Hand leaves the
Creature state, `fn_0046CF80` sends packet `0x59` with it [V]. This is the
reward or punishment that the Creature learns from [I].

### 1.2 How the state starts [V for the code, I for what the player does]

The hand mode comes from a table indexed by the interface style
(`GInterface+0x44`, table at `0x00D18278`, built at `0x005D7960`).

- Style 0 uses `fn_005D7F20`: mode 9 for an object the Hand can hold, `0x12`
  for an interactable object that it cannot hold (`0x005D8070`), 3 for
  nothing.
- Style 4 uses `fn_005D80C0`: for an object whose attribute block
  (`Object+0x28`) has `1` at `+0x10` it gives `0x13` (the Creature state), or
  `0xE` (the Totem) when the flag at `attribute+0x10AC` is set.

A trigger press on an object with bit `0x10` in the byte `Object+0x24` runs
the interface action at `0x005D4960`. It sets the style to 4 (`fn_005D2980`).
In style 4 the hand mode is `0x13` only while the next pick record (`+0x400`)
holds an object of type 1, the Creature. Another object gives `0xE` and no
object gives 3. Mode `0x13` puts the empty Hand in state 7.
`HandStateCreature::Enter` (`0x005B13F0`) sets the lesson total `+0xD8` and
the timers `+0xCC`, `+0xD0`, `+0xD4` and `+0xDC` to 0. So the state needs a
press on the Creature, not only a hover.

In the pen of the Creature select, the Creature does not have bit `0x10` of
`Object+0x24`, so a press does not start state 7 there.

### 1.3 The camera at the press [V]

For an empty Hand, `CHand::GetRequiredState` calls
`CHand::OperateInteractionCamera` (`0x0046CB60`, thiscall with the hand mode,
`ret 4`) at `0x0046CEA0`, on every call.

- For mode `0x13` it casts the object under the Hand (`GInterface+0x3C8`) to
  a Creature and writes it to `CHand+0x49B4`, and its `LH3DCreature`
  (`[[creature+0x160]+0x58]`) to `+0x49B8`.
- When the flag `+0x49C0` is clear, it makes a `CameraModeCtrInteract`
  (`0x0044A850`), which pushes itself over `CameraModeNew3`. It sets the flag
  and `+0x4900` to 0.
- For any other mode with the flag set, it pops the camera mode
  (`GCamera::PopViewMode` `0x00441C50`).
- Then, when `+0x4900` is below 450 and the Creature belongs to the local
  player, it sends packet `0x5F` (`GGame::SetPacket` `0x00550DD0`, thiscall,
  `ret 4`): the leash click. The local player is `GGame + 0x18 + 0xA60 *
  byte[GGame + 0x205A59]`. The Creature's player is its vtable slot `+0x1C`.

`CameraModeNew3::Update` drags the land only in hand mode `0x14`
(`0x0045AB8D`). So with no `CtrInteract` over it, the held trigger in mode
`0x13` does not drag the land.

`script_camera.cpp` counts `CameraModeCtrInteract` as a story mode. So when
the game pushes it, the God view stands aside to the game camera. This is the
reason for `creature_camera` (section 2.1).

### 1.4 The leash [V for the calls, I for the rest]

`GInterfaceStatus+0x12C` is the `GLeashStatus`.

| Field | Meaning |
| --- | --- |
| `+0x14` | 1 while the leash is on |
| `+0x1C` | the kind of the leash (1 to 3) |
| `+0x24` | the object that the leash is tied to (null while the Hand holds it) |
| `+0x38` | the `GInterfaceStatus` |

`GLeashStatus::SetOn` (`0x005E6F70`) turns the leash on and off.
`Creature::GetInterfaceStatusLeashOn` (`0x004CF060`) gives the status whose
leash is on a Creature. The player's Creature is `GPlayer+0xA4C`.
`GInterface` keeps the three drawn leashes (`Leash*`) at `+0x198` to `+0x1A0`.
The leash under the pointer comes from the screen test in
`UpdateInterfaceCollide`.

`GInterface::DoLeashSelectionSystem` (`0x005D01F0`) is the selection of the
kind of the leash by a gesture, not the pull. The Tug state (3, set by
`hand+0x4908`) handles the pull. It places the Hand without
`SetDistanceFromView`.

**The packets.** `GPacket::ProcessPacket` (`0x0063C420`) takes the packet
number minus 6 as the index of its table at `0x0063DDCC`.

| Packet | Sent by | What it runs |
| --- | --- | --- |
| `0x5B` | `0x00553F5C` | `SetOn(creature, 0)`: the leash goes off |
| `0x5C` | `fn_005D4210`, at `0x005D430A` | With an object: `fn_005E6BD0` ties the leash to it, and `ForceActOnObject` follows. With no object: the leash comes back to the Hand (`fn_005E6EA0`) |
| `0x5D` | `fn_005D4180`, at `0x005D41EA` | `Creature::ForceActOnObject` (`0x004C5FF0`): the Creature acts on the object |
| `0x5E` | `fn_005D3D10`, at `0x005D3D88`, and `0x005D5608` | `Creature::ForceActOnMapCoord` (`0x004C6540`): the Creature goes to the land point |
| `0x5F` | The Creature state, and the gestures | `fn_005D06E0`: the leash click, on or off |
| `0x65` | `DoLeashSelectionSystem`, at `0x005D03B4` | `fn_005E6AE0`: the kind of the leash |

**The leash click** (packet `0x5F`, handler `fn_005D06E0` on the
`GInterface`):

1. It takes the Creature of the player from `GPlayer+0xA4C`.
2. It stops when the Creature does not live (vtable slot `0x2C`) or when
   `Creature+0x1110` is 0. It also stops when the leash of a different player
   is on the Creature.
3. `fn_005E7140` on the `GLeashStatus` then does one of three things. A leash
   tied to an object (`+0x24`) comes back to the Hand. Else the leash goes on
   when it is off (`+0x14`), and comes off when it is on.

### 1.5 The commands with the mouse [V]

**The buttons.** The message handlers at `0x005D98E0` to `0x005D9960` set the
bits of `GInterface+0x39`: 1 for a left press, `0x20` for a left release, 2
for a right press, 8 and 2 for a right double click, and `0x40` for a right
release. The step of the default action (`fn_005D5190`) reads them: bit 8
runs `fn_005D4210` and nothing else, bit 1 `fn_005D1320` and bit 2
`fn_005D1330`.

**A click sends the Creature to a place.** The right button (the Action
button) does it. `fn_005D1330` is the right press with an empty Hand. With no
object in the next pick record, it calls `fn_005D3D10` with the land point
`GInterface+0x3F0` (`0x005D1548`). `fn_005D3D10` (thiscall, a `MapCoords*`,
`ret 4`) sends packet `0x5E` when all of these are true: the leash is on
(`+0x14`), the Hand holds it (`+0x24` is null), the player has a Creature,
and the action of the Creature is not `0x5D`. Else it does what a press on the
land does with no leash. The action is at `[Creature+0x164] + 0xFD8 + 0x60 *
index`, with the index at `+0xFB4`. What action `0x5D` is was not traced.

**A click on an object.** An object that the Hand can take is picked up
(`fn_005D1740`). For each other object, `fn_005D1740` calls `fn_005D3930`.
With the leash on the Hand, `ValidAsInterfaceTarget` (slot `0x6F0`) at 1 and
`ValidAsInterfaceLeashTarget` (slot `0x6F4`) not 0, it calls `fn_005D4180`
(thiscall on `GInterface`, the object, `ret 4`), which sends packet `0x5D`.
Else the object gets its own tap (packet `0x20`). Both slots give 1 for
`Object`, so almost each object is a target. A press of the grip on an object
outside the influence goes from `fn_005D1740` (`0x005D17A5`, the influence
`+0x48` is 0 and slot `0x714` gives 1) to `fn_005D3930`, which sends the
packet.

`Creature::ForceActOnObject` has no test of the water at its start. It tests
the leash (`Creature::IsLeashWorking`), a fight, and two values of the
Creature. So the game does not refuse an object in the sea.

**The double click.** With bit 8, `fn_005D4210` (thiscall on `GInterface`, no
arguments) takes the object of the next pick record (`+0x400`). The first
click of a double click is a click (packet `0x5D` on an object). The second
sends only the packet of the double click:

- When a leash of the player is tied to an object, it sends packet `0x5C`
  with 0 if the object is that object or the Creature (`0x005D42E0`).
- Else it needs: the object is not the Creature, slot `0x6F0` gives 1, and
  the leash of this status is on the Creature. A `OneOffSpellSeed` gets a tap
  and no leash (`0x005D42BB`). Then slot `0x6F4` must not give 0, and it
  sends packet `0x5C` with the index of the object (`Base::GetIndexOffset`
  `0x00436A70`, through `fn_00550C80`).

The handler of packet `0x5C` (`0x0063D416`) needs a Creature that can have a
leash (`+0x1110`) and a leash that is on. With an object it ties the leash
(`fn_005E6BD0`), ends the action of the Creature
(`Creature::FinishActionUnsuccessfully`) and calls
`Creature::ForceActOnObject`. With 0 it calls `GLeashStatus::SetOn(creature,
0)` and `fn_005E6EA0`, which clears `+0x24` and puts the leash on again: the
leash is back on the Hand. The leash click on a tied leash clears `+0x24`
too. While the leash is tied, the game sends no command: `fn_005D3D10` and
`fn_005D3930` need `+0x24` at null.

**No marker on the land.** The first 0x320 bytes of `ForceActOnMapCoord` make
the actions of the Creature and play a sound. They make no object and no
effect at the land point. The rest of the function (3296 bytes) was not read.
The game shows a tooltip beside the Hand (`GInterface::CalculateLeashToolTip`
`0x005D67F0`): `0xEA2` over bare land, `0xEA4` over an object that the leash
can go to, and `0xEA3` over the object that holds the leash. The symbol file
has no draw function of a footprint (`Creature::DrawLeashInfo` `0x004CEFB0` is
16 bytes), so the mod draws its own ring. [I: a search of the whole draw path
was not done.]

**The sea.** `Creature::ForceActOnMapCoord` asks `MapCoords::IsWater` at
`0x004C6C87` and then forces an activity of its own for the water (desire 3
with action `0x2A`, or desire `0xE` with action `0x37` or `0x2C`) [V]. What
the Creature then does was not traced.

**The shake.** The shake is the gesture of type 5, the scribble.
`GInterface::ProcessPowerUpSystem` (`0x005CF300`) tests the ring of pointer
samples against the templates of that type with `fn_005CF220(5)`. At
`0x005CF725`, with an empty Hand (`GInterfaceStatus+0x90` is 0), the leash on
and no object on the leash, a match clears the ring (`fn_005CE420(0)`) and
sends packet `0x5F` (`0x005CF76B`). The test reads the turns and the
directions of a path in pixels. It has no speed and no size, so there is no
threshold in it. Other branches use the same gesture: it drops a Miracle, it
stops a chain of gestures, and in the leash selection it closes the
selection (`DoLeashSelectionSystem`, `0x005D0283`). The gesture of type
`[0x00DA0590]` puts the leash on and opens the selection of its kind. See
`miracles.md` for the gestures.

### 1.6 When the Creature is ready for the Hand [V]

`HandStateCreature::Update` has a test of `LH3DCreature+0x526C`
(`0x005B17EA`). The flag does not say that the Creature has a body. It says
that the Creature is ready for the Hand:

- A press on the Creature sets `Creature+0x3CC` to 1.
- `Creature::ProcessState` then calls `fn_00477D00` (`0x00472FAB`). It waits
  while `LH3DCreature::IsPerformingBodyAction` is true. Then it sets `+0x3CC`
  to 0 and the flag to 1 (`fn_004806C0`).
- `Creature::GetReadyForNetworkUnfriendlyEndLockedSelect` clears the flag at
  the end of the press (`0x00476EF1`).

The image has four instructions with the offset `0x526C`: the start value,
these two, and the test in the Update. So the flag is 0 at each time before a
press. The time from the press to the Creature state is the time that the
Creature needs to end its body action. It was 30 to 223 frames in desktop
runs.

The body bone (`fn_004813D0`, thiscall on the `LH3DCreature`, plain `ret`:
bone `+0x51D8` through `LH3DCreature::GetSafeBuffer`) has no such test. The
game calls it for a Creature that the Hand does not have (`0x005D3E33`,
`0x005CFF29`).

`Creature::ValidForLockedSelectProcess` (`0x00476E10`) gives 1 for a Creature
that has a player, no spell on it and no state `0xD` [V], because a press on
the Creature starts its touch with the locked select. So the game's test for a
bulk take passes for the Creature (see `grab.md`, "The bulk take").

## 2. The Creature by touch

The code is in `hand_direct.cpp` (`TouchFrame`, `h_CreatureUpdate`,
`StrokeHand`) and `overlay.cpp` (`ChoosePose`, `BuildFrame`), with the pure
math in `controller_math.h` and `near_math.h`. The Creature panel is in
`ui-layer.md`.

### 2.1 No camera move (`creature_camera`)

A hook on `OperateInteractionCamera`:

- 0 never moves the camera.
- 1 is the game's camera move. The hook is not installed. For the mouse in
  play mode 0 the game's function runs and nothing else.
- 2 (the default) moves no camera while the controller placed the Hand on the
  last frame or while the table or the God view owns the view.

On the no camera path the hook sets the flag `+0x49C0` around the call for
mode `0x13`. The game then writes the Creature to `+0x49B4` and `+0x49B8` and
pushes nothing. With no mode pushed, `script_camera.cpp`, the God view, the
snap turns and the grab all run as usual.

When the object under the Hand is that Creature, the mod takes the press and
keeps its start time (`QueryPerformanceCounter`). It holds the press to the
end, even when the owner of the pointer changes. At the first other mode, the
press ends. A press shorter than 450 ms of real time on the player's own
Creature sends packet `0x5F` with `SetPacket`, as the game does. The game's
limit of 450 counts its frame time, which grows in both eye passes, so it is
about 225 ms in stereo.

### 2.2 The Hand stays at the controller

The depth hook on `SetDistanceFromView` also acts in state 7. It writes
`+0x4854 = |target - camera|` and `+0x4834 = real size / size^0.5`, so the
game's multiply gives the size of the real hand. The ray hook (the
`UnprojectScreenPoint` hook, `HandDirectUnproject`) aims `+0x485C` at the
target in state 7, and in the first Update of a frame it aims `+0x4864` at the
previous point. So the state puts `+0x78` on the target, and the Creature's
distance and the Hand's distance are the same. The hook has to write the
depth, because the state would otherwise slide the Hand along the line of
sight to the Creature's depth.

### 2.3 The body and the contact

**The body.** The first Creature Update of a frame, before the game's code,
works out a vertical capsule through the body bone. It goes from the land
under the bone up to `1.1 S`, the top of the game's slap test. Its radius is
the Creature's 2D radius (vtable `+0x64` of the Creature object) and
`creature_touch_cm` (5) of the room, at the scale now. The Hand's point is in
contact while it is in the capsule. The second Update of the frame (the second
eye) uses the same numbers.

**No touch at a distance.** A hook on the mesh ray test acts only inside a
Creature Update that the controller placed, and only for the two return
addresses of section 1.1. While the Hand's point is outside the capsule, it
returns 0 without a test. Inside, the game's own test runs, so the contact
point and the bone of a stroke stay the game's.

**The swept test.** A fast slap can go through the body between two frames
and never be in it at a frame. When the Hand was outside the capsule on the
last frame and its path to this frame meets the capsule (`CapsuleEntered`),
the mod forces the slap branch. It sets the last x to the other side of the
screen center (`2 * W/2 - x`). The ray of the middle pixel then goes through
the screen center, so the hook refuses its hit in that frame. When the speed
ray of the last point is cast, after the test of the center and before the
side is read, the mod puts the last x back one pixel behind the current one.

**No slap by a head turn.** In every other first Update the last x is one
pixel behind the current x, on the side the hand comes from. Two integers next
to each other never cross the center, so a head turn cannot slap, and the game
reads the side of the slap from the hand. The second Update of a frame keeps
the game's own last point, which the game copied from the current one.

**The pick keeps the Creature.** While the controller places the Hand in
state 7, after each eye pass, the next pick record gets the Creature again
when it holds anything else (`RecordPick` `0x005D5E40`). The Hand's pixel
leaves the Creature in a slap that goes through it, and in a Hand pulled back
to the body. Without this the hand mode would go to `0xE` or 3 and end the
state in the middle of a slap. So the state lasts until the trigger opens, and
the time of the press is the time that the trigger was held. The pick of
holdable objects (`Choose`, `Inject`) does nothing in state 7.

**The glow.** In state 7 the glow is green while the Hand's point is in the
body, and pale blue when it is not.

### 2.4 The speed of a slap (`creature_slap_mps`)

The mod aims the game's ray of the last point (`+0x4864`) at `target - u * s *
k * v * dt`.

- `u` is level, at right angles to the line from the eye to the target, and to
  the right as the eye sees it (`SlapAcross`).
- `s` is the sign of the hand's velocity along `u`.
- `v` is the real speed of the part that hits, in m/s over the last 50 ms, in
  three dimensions, so a slap down or forward counts too (see below).
- `dt` is the game's step.
- `k = 5 S / creature_slap_mps` (`SlapUnitsPerMps`).

Both points are on the sphere around the eye at the Hand's depth, so the game
reads `k * v`, within 0.2 percent at the depths of a slap. A slap then needs
`creature_slap_mps` of real speed and a hard slap 1.8 times that, at any size
of the Creature and any scale of the world. A still hand gives the target
itself, and speed zero. The second Update of a frame gets the target too, so
it reads zero, as with the mouse.

**The part that hits** (`near_math::SlapSpeed`): the larger of the speed of
the grip and the speed of the Hand's point, each over 50 ms. A slap with the
arm moves the grip. A slap with the wrist, as a backhand is, turns the hand
about the wrist, so the tip moves fast and the grip moves slowly (a backhand
of 420 degrees a second gives 1.25 m/s at the tip and 0.5 m/s at the grip).
Both count the same. A stroke of a real hand is 0.1 to 0.5 m/s, so it stays a
stroke. The plain game slaps at `5 S` world units a second of the mouse
pointer at the Creature's depth, which a small move of the mouse gives.

**The wait (`creature_slap_wait`).** At the first Update of a state, `+0xDC`
goes up to 1000, so the first slap counts at once. With `creature_slap_wait=1`
the game's wait of 1 s stays. After a stroke the game waits 1 s again before a
slap.

### 2.5 The open hand

The grip is the game's Action button, and the Creature state lasts while it
is down. So the mod must open the hand by itself in the state.

**The pose** (`near_math::StrokeFinger`). In the Creature state the pointing
hand takes one of two poses, with the grip closed too. The body is the
capsule of the touch, `1.1 S` high. The hand's length is the real hand's (19
cm) at the scale now. So the zoom decides.

| The body against the hand's length | Pose | Contact point |
| --- | --- | --- |
| less than 0.60 | the index finger | the tip of the index finger, the Hand's point |
| more than 0.90 | the flat hand | the middle of the palm (`HandExactPalm`) |
| between the two | the pose of the frame before | |

A stroke starts with the flat hand from 0.75 (`StrokeFingerAtStart`). The
limits are `creature_finger_below_pct` and `creature_flat_above_pct`.

| The body against the hand | A stroke starts with | In a stroke |
| --- | --- | --- |
| less than 0.60 | the index finger | the index finger |
| 0.60 to 0.75 | the index finger | the pose of the frame before |
| 0.75 to 0.90 | the flat hand | the pose of the frame before |
| more than 0.90 | the flat hand | the flat hand |

Example: a Creature of size 0.22 has a capsule of 3.63 units. At 13.2 units a
meter the body is 27.6 cm of the room (ratio 1.45), and the hand is flat. At
19.8 it is 18.4 cm (0.97), and the hand is flat. At 29.6 it is 12.3 cm
(0.64), and the hand is the index finger. At 100 units a meter a young
Creature is 3.6 cm high and gets the finger.

`creature_stroke_hand=1` gives the flat hand always, and 2 the index finger.

**The hand opens before the state starts** (`creature_open_wait=1`). A press
on the Creature that is older than the time of a leash click (450 ms) is a
stroke, so the hand takes the pose that the stroke starts with, and not a
fist. The game starts the state only when the Creature has ended its body
action (section 1.6).

**The capsule test and the swept test** use the contact point of the pose. A
change of the pose moves the contact point, and that frame is no slap.

**The point of the flat hand is the palm** (`creature_palm_point=1`,
`near_math::PalmPoint`). With the flat hand, the Hand's point of a frame is
the tip of the index finger of that frame plus the offset from the tip to the
palm of the frame before (`HandExactPalm`, `near_math::PalmOffset`). So the
point moves with the hand of this frame, with no lag. The glow shows at the
palm, the ray of the game's stroke goes through the palm, and the contact of
the mod is the same point. With the index finger the point stays at the tip.
An offset older than 8 frames is not used, and then the point is the tip.

Only the point of the game's Hand, whose mesh is hidden, goes to the palm: the
target (`g_target`). The mod's hand stands on the tip of the index finger
(`g_tip`, `HandDirectExactFacts`). This is a rule: if the mod's hand stood on
the target, the palm of the next frame would be one offset farther, and the
point would run away 11.4 cm of the room on each frame. When the land holds
the tip up (`ResolveLand`), the palm goes up as far as the tip.

**The tickle.** While the contact point is in the body, the fingers move a
little. Each 5 cm of path of the contact point is one wave, each finger is a
part of a wave behind the finger before it, and the wave closes a finger by
0.12 of its curl at most (`TicklePhase`, `TickleCurl`). The flat hand moves
its four fingers, and the finger pose moves the index finger only. The wave
goes on only with the path of the real hand. A hand that is still has still
fingers.

### 2.6 The stroke of the older build (`creature_stroke_hand=3`)

Value 3 of `creature_stroke_hand` gives the older stroke:

- The hand is a fist while the grip is closed, and the flat hand while it is
  open.
- The contact is at the Hand's point, and the fingers have no tickle.
- The speed of a slap is the speed of the grip only.
- A slap needs 1.5 m/s when `bwvr.ini` has no `creature_slap_mps`. With the
  key in the ini, its value counts.

### 2.7 Keys

| Key | Default | What it does |
| --- | --- | --- |
| `creature_camera` | 2 | 0 never moves the camera to the Creature. 1 moves it as the game does. 2 moves it only for the mouse outside the table and the God view |
| `creature_slap_mps` | 1.0 | The real speed of a slap, in m/s: the larger of the speed of the grip and of the Hand's point. A hard slap needs 1.8 times this. 0.2 to 10 |
| `creature_touch_cm` | 5 | How far the body reaches past the Creature's 2D radius, in cm of the room. 0 to 50 |
| `creature_slap_wait` | 0 | 1 keeps the game's wait of 1 s before the first slap |
| `creature_stroke_hand` | 0 | 0 chooses the hand of a stroke by the size of the Creature. 1 is the flat hand always, 2 the index finger always. 3 is the older stroke (section 2.6) |
| `creature_finger_below_pct` | 60 | The index finger while the body is below this percent of the hand's length. 10 to 500 |
| `creature_flat_above_pct` | 90 | The flat hand over this percent. Not less than the finger limit |
| `creature_open_wait` | 1 | The hand opens when a press on the Creature is 450 ms old. 0 gives a fist until the state starts |
| `creature_palm_point` | 1 | With the flat hand of a stroke the Hand's point is the palm. 0 gives the tip of the index finger |
| `creature_panel` | 2 | The Creature panel with `ui_panel=1`: 0 off, 1 fixed in the room, 2 near the Creature. See `ui-layer.md` |

**Test keys** (with `test_keys=1`):

- The 9 key of the number pad takes the next speed of a slap: 0.7, 1.0, 1.3
  and 1.6 m/s. The pointing controller gives a pulse that is longer for each
  next value.
- The 3 key of the number pad takes the next value of `creature_stroke_hand`:
  by the size, the flat hand, the index finger, and the older stroke. The
  pulse is longer for each next value.
- Ctrl and the 3 key change between the limits of the ini and the limits 80
  and 120. The pointing controller gives a long pulse for the limits of the
  ini and a short pulse for the others.
- Ctrl and the 9 key change `creature_palm_point` (a long pulse for the palm,
  a short pulse for the tip).

The word `touchc` of the fake controller steps the hand onto the body bone of
the nearest Creature (`HandDirectTouchCreaturePoint`).

### 2.8 The log

Tag `hand creature`. Examples of the format:

```
hand creature: a press on the Creature 0F3A2C10. The camera does not move (creature_camera=2), frame 5120
hand creature: the Creature state starts, frame 5121. A slap can count at once.
hand creature: 30 frames in the state. The body is 3.63 units high and 2.81 across (2D radius 1.22 and 5 cm of the room). The Hand's point is 0.40 units inside it. hand+0x78 is 0.0004 units from the target, and the size +0x4834 is 0.4750 for the real hand's 0.4750. Creature size 0.220, k 11.00 units/s for 1 m/s
hand creature: the hand of the stroke is the index finger, and its tip touches. The body is 3.6 cm of the room high, 0.19 of the hand's length ...
hand creature: a slap with the index finger, to the left as the eye sees it. The hand moved at 1.31 m/s (the grip 0.52, the Hand's point 1.31), which the game read as 21.6 units/s. A slap needs 1.00 m/s ...
hand creature: the hand went into the body at 0.74 m/s (the grip 0.31, the Hand's point 0.74), and that was no slap. A slap needs 1.00 m/s. The hand was too slow. The motion in the room: -0.93 right, 0.10 up, 0.35 forward ...
hand creature: the press ends after 2480 ms (hand mode now 0x12). Not a short press, so no leash click, frame 5344
```

Other lines:

- The keys at the start.
- "the point of the flat hand is the palm now ... The palm is N cm of the
  room from the tip of the index finger", and "the point of the Hand is the
  tip of the index finger again", one line for each change.
- "the press is N ms old and the Creature state has not started. The hand is
  open now".
- The line of a slap has the contact flag (`+0x110`), the animation
  (`+0x114`), the ms since a stroke, and the Update of the frame (the first
  or a later one).

Each slap goes to the log (200 lines), each stroke step (80 lines), and each
hand that went into the body and did not slap, with the reason: too slow, the
game's wait of 1 s after a stroke, or the game's own test (60 lines).

The report window adds the counts: presses with no camera move, leash clicks,
states with no wait, hits refused at a distance, slap branches forced by the
swept test, frames the pick kept the Creature, and frames with the point at
the palm.

## 3. The body of the Creature before a press

A mod that reads the body bone only when `LH3DCreature+0x526C` is not 0
sees no body before a press (section 1.6). The mod reads the body at each
time.

- `BodyPlace` gives the place of the body. It reads the body bone with no
  test of the flag. The bone counts when it is not more than `S + 2` units
  from the place of the Creature on the map, in x and z
  (`near_math::BoneSane`, with `S = 15 * size`). Else the place on the map
  counts, at half of `S` above the land.
- The capsule of the choice (`CreatureBody`) goes through that place, from
  the land up to `1.1 S`, with the 2D radius. It does not add
  `creature_touch_cm`. The reason: a Creature of size 0.22 with the 5 cm added
  has a radius of 6.02 units, 6 cm at 100 units a meter. By the rule of the
  choice such a Creature would take the press from a tree that the Hand's
  point is in.
- The touch in the Creature state (`TouchFrame`) keeps the test of the flag,
  as the game's Update has it, and `creature_touch_cm`.
- The Creature as a target of a command (`HandDirectCreatureBody`), the choice
  of the press, and the test words (`touchc`, `seek_plan=creature`) use
  `BodyPlace` too.

Without `BodyPlace`, the Creature is measured as each other object (a
cylinder at its place on the map), and a press of the trigger with the aim on
the Creature takes the land under it.

`creature_bone` (default 1): 0 gives that older behavior. Then the mod knows
the body only while the Creature is ready for the Hand. There is no test key
for it.

```
hand creature: creature_bone=1. The body of a Creature is known before a press: the body bone, or the place on the map when the bone is not sane. The capsule of the choice and the Creature as a target of a command use it
hand creature: the body of the Creature 29253674 is at its body bone. Its body bone is at 1896.49 31.30 2520.04, 0.02 units from its place on the map (1896.50 2520.06) and 1.82 above the land there. A sane bone is not more than 5.30 units from that place. Size 0.22, ready for the Hand 0, frame 802
hand creature: the mod asked 100 times where the body of a Creature is: 100 times it was the body bone, 0 times the place on the map, and 0 times there was no place, this window
```

The line of the body comes once for each change of the answer.

## 4. The commands

The code is in `src/mod/creature_command.cpp`, with the pure math in
`creature_command_math.h`. `controllers.cpp` gives it the trigger,
`hand_direct.cpp` the candidates of the cone, `hand_glow.cpp` draws the halo,
`overlay.cpp` the arc and the ring, and `hint_card.cpp` the prompts.

The command acts with `creature_command=1` (the default), while the leash is
on the player's Creature and the Hand holds it, the controller places the
Hand, and the Hand is empty in its Normal state. It is off with
`controller_swap_buttons=1`, where the trigger does the work of the grip.
With no leash, the trigger does what it does in the plain game.

### 4.1 What the trigger does

| The trigger, with the leash on the Hand | What it does |
| --- | --- |
| The aim is on an object | The halo of the object shows, green |
| The aim is on no object | The arc and the ring show the land, in the same frame |
| The aim comes onto an object while the arc shows | The object must stay in the aim for 100 ms (`kHeldCatchS`). Then its halo shows and the arc goes |
| The glow of the Hand is on an object: the object that the grip acts on, within `hand_pick_cm` | That object is the target. The aim does not count |
| A tap: a release less than `creature_command_hold_ms` (250 ms) after the press | It acts on what showed at the press: the object of the press (packet `0x5D`), or the land point of the press (packet `0x5E`). A tap on your Creature is the leash click |
| A hold: a later release | It acts on what shows at the release: the object with the halo (packet `0x5D`), or the land point of the ring (packet `0x5E`) |
| A hold that ends on your Creature | The leash click, only when the press started on the Creature and is shorter than 450 ms. A press that started on the land or on an object does not take the Creature as a target, so the ring can go past it |
| A press while the game's pick has the Creature under the Hand | Not a command. The press goes to the game: the stroke, the slap and the short press that changes the leash |
| A release with the aim off the target, on the sky, on the sea or off the map | Nothing |
| A drag of the land | None. The trigger of a command does not reach the game. The grip of the other hand, the stick and the teleport move the player |

The length of the press does not matter for an object that stays in the aim.
`HeldStep` is the rule of what shows, and `ReleaseLive` the rule of the
release (`creature_command_math.h`). The object at the Hand's point stays a
target.

**The grip keeps its jobs** (take, far grab, bulk take). One job is gone: a
grip on bare land does not send the Creature there. A hook on `fn_005D3D10`
leaves the packet out while the controller places the Hand, the leash is on
the Hand and `creature_command=1`. It clears the pick lock and gives 1, as the
game does after the packet. The mouse gets the game's function. The reason: a
grip from a distance that misses its target would send the Creature to the
land under the Hand.

### 4.2 The land aim and a tap

The land aim is the arc of the teleport (`teleport_reach_m`, see
`god-view.md`), a straight ray on the table. The aim goes through the same
one euro filter (`aim_smooth.h`). The target is valid when it is on the map
and not the sea. The ring has the size of the teleport ring
(`teleport_ring_cm`). The arc and the ring are green, the color of the
trigger, and red for a target that is not valid. The filter of the aim starts
again at each press, so the first aim has no lag.

`ReleaseLandAim` is the rule of the release on the land:

- The mod evaluates the land aim in the pump of the press, not one frame
  later. That first aim is the aim of the press.
- A tap, a press shorter than `creature_command_hold_ms`, sends the Creature
  to the aim of the press. The pull of the trigger and its release move the
  hand, so the aim of the release is the worse one for a tap.
- A hold sends the Creature to the aim of the release. The player moved the
  arc to that place.
- A tap whose first aim met no land uses the aim of the release, if that one
  is valid. A hold that ends on the sea sends nothing.
- After each send to the land, the ring stays on the place for 0.6 s
  (`kSentMarkS`), with no arc. So a tap shows where the Creature goes. The
  ring does not move, and a new press takes its place.

The pump of the buttons runs after the draw of the frame. [V]
`GGame::Loop` calls `GGame::ProcessGraphicsEngine` at `0x0054D3BE` and then
`GGame::FlipScreen` at `0x0054D3F5`, and `LHScreen::Flip` calls the pump. The
room of the God view is known only inside the draw of a frame (`TabletopRoom`,
from the main camera call to the end of `GGame::ProcessGraphicsEngine`).
Outside of that time the teleport function gives the map of the immersive
view, at 7.5 units a meter from the game camera, and the arc meets no land. So
`ControllersFrame` makes the aim once a frame, inside the draw
(`LatchFrameAim`), and each caller gets that aim until the next frame.

### 4.3 The target of an object

With the leash on, the land is the first target. A press with no object in the
aim sends the Creature to the land. So an object is the target only when the
player points at it. `AimRule` has three rules, and each has a band to stop
a flicker.

| Rule | Numbers | Reason |
| --- | --- | --- |
| The cone | A new target needs the aim within 3 degrees of its body (`creature_command_cone_deg`). The target stays to 5 degrees (`creature_command_keep_deg`). A different object takes the aim only with a score 1 degree lower | The land of the God view is 54 to 57 cm below the head, so the hand is 20 to 40 cm above the land. A ray from there to a point 5 m away goes down at 3 degrees. With a wide cone each tree of the far field would be in the aim. The grab from a distance keeps its cone of 8 and 14 degrees, because a grip with no target does nothing, and a trigger with no target has the land |
| The body | The angle is measured to the upright body of the object: the line from its base to its top, with its 2D radius (`AimAtBody`). The Creature is a ball around its body bone | With a cone of 3 degrees, the aim on the crown of a near tree is more than 3 degrees from the base of the tree. The cone starts at the side of the body, so the radius adds to it |
| The range | The object is at most 4 m of the room from the head, along the floor (`creature_command_max_m`), and 15 cm more for the target of the last frame | The arc for the land reaches 10 m, so the Creature can still go that far |
| The land in front wins | When the aim ray meets the land before the object, the object is out when it is more than 30 cm (`creature_command_behind_cm`) plus 20 percent of the distance of the land point behind that point. The target of the last frame stays to 1.5 times that | The player who points at the land in front of a tree wants the land. The share of the distance is there because a ray near the land moves its land point by 1 m for 1 degree |

The land point of the ray comes from a march along the ray in steps of 3 cm
of the room, with the sea as land at 0 (`RayLandDistance`). The march runs
once a frame, and only when an object in the cone needs it.

The tests of a valid object are the game's: slot `0x6F0` gives 1 and slot
`0x6F4` does not give 0. The player's own Creature is a target too: its body
bone, with its 2D radius. The cone needs `far_grab=1` and the God view or the
table. Without them, only the land command acts. The aim comes from the aim
pose of the pointing hand, with the angle rule of `FarConeScore` and
`FarPickTarget`, as the grab from a distance does (see `grab.md`).

**An object near the Hand.** A command has no least distance from the Hand.
The score takes 35 cm (`kFarMinHandM`) for a nearer object
(`FarScoreMeters`). The object at the Hand is a target: the object that a
press of the grip acts on, within `hand_pick_cm` of the Hand's point
(`HandDirectNearObject`, the choice of `Choose`). It wins over the aim. So
the trigger with the glow on a sailor in the water sends the Creature to him,
as the grip does.

**The list.** The list of the cone holds 128 objects. When it is full, a
better target takes the place of the worst one. Each object is a valid target
of the leash, and a forest has many.

**The target of a press** keeps the first word of the object. The release
sends nothing when the game deleted the object in that time
(`HandDirectObjectLives`).

**The two halos.** The target of the grab from a distance is the target of a
command too, when it is in the cone of the command. When the two targets are
different objects, each has its halo: gold for the grip and green for the
trigger (`DrawHalo` in `hand_glow.cpp`). A red far target that is the target
of a command has the green halo. A gold far target and a locked one keep their
halo.

**The land command and the sea.** The mod does not send a point in the sea.
The ring is red there. A sailor in the sea is a target as an object.

**Haptics.** A tick of 0.15 at a new target, 0.3 for 30 ms when a command is
sent, and 0.6 for 80 ms when the leash comes off.

**Rule 1.** `creature_command_rule=1` gives the older rule: the press fixes
its kind (a press with a target under the aim is a press on that target and
the target stays until the release, a press with no target is a press on the
land), and an object takes the aim in the cone of the grab from a distance
(8 and 14 degrees) at each distance. A press on an object then ignores a
move of the aim to the land. `ReleaseCommand` and `ReleaseLandAim` are its
rules.

### 4.4 The shake of the real hand

The mod does not feed the game's shape test. A real hand moves all the time,
and the same ring starts the Miracle gestures and changes the kind of the
leash. Also, while the controller owns the pointer, the mod drops the game's
own samples of the pointer (the hook on `fn_005CEAD0`), and samples go into
the ring only while the draw button is held. So `ShakeStep` in
`creature_command_math.h` finds the shake in the motion of the real grip in
the room, in meters and seconds:

- The anchor is the last turning point. The far point is the sample farthest
  from the anchor. When the hand comes back 2 cm from the far point, the
  stroke from the anchor to the far point is done.
- A stroke counts when it is 6 cm long or more, its mean speed is
  `leash_shake_mps` or more (0.6 m/s), and its length is 75 percent of its
  path or more. So a circle does not count.
- A stroke adds to the row only when it goes back along the stroke before it
  (the dot of the two directions is -0.5 or less).
- 4 strokes in a row, in 0.9 s or less, are a shake. The next shake needs 1 s.
- A stroke that takes more than 0.6 s, and a gap of 0.1 s in the samples, end
  the row. A hand that stays in a ball of 1.5 cm around the anchor has not
  moved.

A shake of 6 cm to each side, 4 times a second, has strokes of 12 cm at 0.96
m/s, and the detector fires after 0.6 s. Then the mod makes the game's own
tests (an empty Hand in its Normal state, the leash on, no object on the
leash) and does what the game does on a match: `fn_005CE420(0)` and packet
`0x5F`. No trigger, grab, gesture or teleport can run at that time. The
controller gives a pulse of 0.6 for 80 ms when the game turns the leash off.
With the mouse, the game's own test acts.

### 4.5 Keys

| Key | Default | What it does |
| --- | --- | --- |
| `creature_command` | 1 | 0: the trigger does not command the Creature, the grip on the land sends it as in the plain game, and no shake is read |
| `creature_command_hold_ms` | 250 | A press on the land shorter than this is a tap, and it uses the aim of the press. A longer press uses the aim of the release. 0 to 2000 |
| `leash_shake_mps` | 0.6 | The mean speed of each stroke of a shake, in m/s. 0 turns the shake off. 0.2 to 5 |
| `creature_command_rule` | 2 | 2 is the live target (section 4.3). 1 is the older rule |
| `creature_command_cone_deg` | 3 | A new target needs the aim this near to its body, in degrees. 0.5 to 20 |
| `creature_command_keep_deg` | the cone plus 2 | The target stays while the aim is this near |
| `creature_command_max_m` | 4 | The range from the head, in meters of the room along the floor. 0 is no limit |
| `creature_command_behind_cm` | 30 | The land in front wins past this many cm of the room, plus 20 percent of the distance. -1 turns the rule off |
| `creature_command_double_ms` | 500 | The time of a double press (section 5.2). 0 turns the double press off |
| `teleport_reach_m`, `teleport_ring_cm` | 10, 11 | The arc and the ring of the land aim. See `god-view.md` |

**The A/B key.** With `test_keys=1`, Ctrl with the 5 key of the number pad
changes `creature_command_rule` between 2 and 1 while the game runs. The
pointing controller gives a long pulse for 2 and a short pulse for 1, and the
log has a line. The keys of the aim trim (numpad 8, 2, 4, 6 and 5) do not act
while Ctrl is down.

**Test keys for the desktop runs only:**

- `creature_command_test=N`: from frame N of the mod the leash counts as on.
  The commands go to the log and no packet is sent.
- The script word `aimcmd` turns the ray of the fake hand to the middle of the
  body of the nearest object that a command can act on
  (`CreatureCommandTestPoint`), and `command_test_class` names the class of
  that object.
- The word `aimturn` turns the aim by the next step of
  `fake_controller_turns` (`yaw,pitch,0` in degrees, yaw to the left).

### 4.6 The prompts

| When | The card shows |
| --- | --- |
| The leash is on and the Hand is over the land | "Trigger Send the Creature" |
| The aim is on an object | "Trigger Send the Creature" (after "Grip Pull to fetch" for a far target) |
| The aim is on the player's own Creature | "Trigger Take the leash off" |
| The trigger is held and the arc shows | "Trigger Release to send" |
| The aim is on an object that can take the leash | A second line: "Trigger x2 Tie the leash" |
| The leash is tied, and the aim is on the object that holds it | "Trigger x2 Take the leash back" |
| The leash is tied, and the aim is on the player's own Creature | "Trigger Take the leash back" |

### 4.7 The log

The tag `creature cmd`: the keys at the start, each new target, each press
that the command takes and each press that goes to the game with the reason,
each release with what was sent, each shake with its strokes and speeds, each
change of the leash, and one line each report window. A new target in the aim
has its distance from the Hand, and a new target at the Hand has the line
"the Hand is on the". Examples of the format:

```
creature cmd : creature_command_rule: creature_command_rule=2. While the trigger is held, the target is what the hand points at: an object, or the land. A new object needs the aim within 3.0 degrees of its body and stays to 5.0 (creature_command_cone_deg, _keep_deg). It is at most 4.0 m of the room from the head (creature_command_max_m, 0 is no limit). The land wins when the ray meets it more than 30 cm, and 20 percent of the way to it, in front of the object (creature_command_behind_cm, -1 is off), frame 0
creature cmd : the aim is on the Tree 2AA30804, 0.0 degrees off the ray, 0.82 m of the room from the Hand and 1.44 m from the head, at 1999.8 52.4 2497.6. The land point of the ray is 1.39 m along it (less than 0: not known). A press of the trigger sends the Creature to it, and a second press ties the leash to it, frame 953
creature cmd : the trigger closes with the aim on the Tree 2AA30804. It does not go to the game, frame 1001
creature cmd : the trigger is held, and the aim is on no object now (it was on 2AA30804). The arc and the ring show the land, 476 ms after the press, frame 1022
creature cmd : the trigger is held, and the aim is on the Tree 2AA30804 now, 0.0 degrees off the ray. Its halo shows, and the arc is gone, 1788 ms after the press, frame 1067
creature cmd : the trigger opens after 5372 ms, so the aim of the release counts. Packet 0x5E: the Creature goes to 2010.0 54.6 2518.6, frame 1201
creature cmd : the trigger opens after 117 ms, a tap, so the target of the press counts, on the Tree 2A669804. The game sends packet 0x5D: the Creature acts on it, frame 1216
creature cmd : the trigger closes with the aim on no object. It does not go to the game. The arc of the press meets 2001.0 55.4 2512.3 (the target is land), frame 1301
creature cmd : the trigger opens after 119 ms, a tap, so the aim of the press counts. Packet 0x5E: the Creature goes to 2001.0 55.4 2512.3, frame 1306
creature cmd : the trigger opens after 2518 ms, a hold, so the target that shows now counts, on the Tree 2A669804. The game sends packet 0x5D: the Creature acts on it, frame 1461
creature cmd : a shake of the hand: 4 strokes in 510 ms, the slowest at 0.88 m/s, the last one 12 cm long. A stroke needs 0.60 m/s (leash_shake_mps). The leash click (packet 0x5F), frame 6010
creature cmd : the leash is off, 70 ms after the shake, frame 6016
creature cmd : creature_command_rule=2. While the trigger was held, what shows changed 5 times. The range left out 54490 objects, and the land in front left out 125 objects of the cone. 0 double presses tied the leash, and 0 took it back
```

## 5. The leash

### 5.1 The leash from a distance

Your Creature in the aim is a far target of its own kind. The code is in
`FarSelect` and `HandDirectLeashClick` (`hand_direct.cpp`), `FarGrabPumpGrip`
(`far_grab.cpp`) and `hint_card.cpp`. The pure rule is
`near_math::LeashClick`.

- `FarSelect` takes the Creature when `LeashKind` says that the click does
  something. The kinds are 1 (the leash goes on), 2 (it comes off) and 3 (it
  comes back from an object). The Creature of a different player is no
  target. A Creature that the game would not leash is no target. The halo is
  the halo of each far target.
- The card of the hand shows "Grip Put the leash on", "Grip Take the leash
  off" or "Grip Take the leash back".
- On the edge of the grip, `FarGrabPumpGrip` calls `HandDirectLeashClick`
  (packet `0x5F`) and takes the grip away from the game until it opens. No
  press goes to the game. So the Creature state does not start, for a short
  grip and for a long one. The game decides what the click does, as for the
  mouse. The mod sends only `0x5F`.
- Near the Creature the grip is a press. "Near objects win" turns the far
  target off while the Creature is 10 cm or less from the Hand's point. A
  short press is then the leash click, and a hold starts the Creature state.
- With the leash on, a short press of the trigger with the aim on your
  Creature is the leash click too.

The reason for the rule: the game's test of a bulk take passes for the
Creature (section 1.6). Without the rule, the halo and the hint say "collect"
for the Creature, and a grip from a distance starts the Creature state with
the hand far from the body. The Creature is touched with the hand, and the
trigger commands it.

| Key | Default | Meaning |
| --- | --- | --- |
| `far_leash` | 1 | Your Creature in the aim is a target of the leash. 0 takes the Creature out of the far targets |
| `far_test_creature` | 0 | A test key. 1: only your Creature is a far target, and the word `aimfar` aims at it |

```
far grab    : far_leash=1. Your Creature in the aim is a target of the leash: the grip sends the leash click, and no press
far grab    : the grip closed on your Creature 29258674, 0.75 m of the room from the Hand. The leash click (packet 0x5F): the leash goes on. No press goes to the game, so the Creature state does not start, frame 931
creature cmd : the leash is on, frame 936
```

The report of the far grab counts the leash clicks from a distance.

### 5.2 The leash on an object

| The trigger | What it does |
| --- | --- |
| A tap on an object, with the leash on the Hand | Packet `0x5D` at once, with no wait |
| A second press on the same object, 500 ms or less after the first press started (`creature_command_double_ms`) | `TieLeash`: the game's tests, then packet `0x5C` with the index of the object. The press does its work when it starts, and its release does nothing |
| A short press with the aim on your Creature, while the leash is tied | The leash click, packet `0x5F`: the leash comes back to the Hand |
| A tap and a second press on the object that holds the leash | `TakeLeashBack`: packet `0x5C` with 0, as the game's double click on that object. The first tap sends nothing |
| Each other press, while the leash is tied | It goes to the game |

The two presses do not fight. The first tap starts `ForceActOnObject`. The
handler of the second packet ends that action and starts it again with the
leash tied. The plain game sends the same two packets for a double click.

The grip on your Creature from a distance takes the leash back too (kind 3 of
`near_math::LeashClick`). It sends the leash click.

Each object that is a target of a command passes the tests of the double
click, except a `OneOffSpellSeed`. So the same objects can take the leash.

The card (`AddPromptTwice` in `hint_card.cpp`) has two lines for an object
that can take the leash: "Trigger Send the Creature" and "Trigger x2 Tie the
leash". While the leash is tied, the object that holds it has the green halo
and the line "Trigger x2 Take the leash back", and your Creature has "Trigger
Take the leash back".

What the Creature does on a tied leash is the game's own.

```
creature cmd : a double press on the Tree 2A67A804, 288 ms after the first press. Packet 0x5C: the game ties the leash to it, frame 1023
creature cmd : the leash is tied to the Tree 2A67A804 now (the game has the leash on, and the Creature has it as the leash of this player). The trigger sends no command until the leash is back on the Hand, frame 1025
creature cmd : the trigger opens after 115 ms, and nothing is sent: the leash is tied to this object. A second press takes the leash back, frame 1106
creature cmd : a double press on the Tree 2A67A804, 276 ms after the first press. The leash is tied to it. Packet 0x5C with no object: the leash comes back to the Hand, frame 1113
creature cmd : the leash is not tied to an object now (it was on 2A67A804). The leash is on the Hand, frame 1116
far grab    : the grip closed on your Creature 2A64C674, 0.36 m of the room from the Hand. The leash click (packet 0x5F): the leash comes back to the Hand. No press goes to the game, so the Creature state does not start, frame 1301
creature cmd : a short press of 118 ms with the aim on your Creature: the leash click (packet 0x5F). The leash comes back from the object to the Hand, frame 1526
```

## 6. Tests

### 6.1 The math tests

The math tests are in `bwvr/test/controller_math/` and run with
`.\dev.ps1 build`. They cover:

- The touch (`CreatureChecks`): the capsule gap, the distance of two
  segments, the swept test (through, stop inside, past, stay inside, come
  out, over the top, a graze), the across axis, the speed as the game reads it
  (1.4 m/s is no slap, 1.6 m/s a slap, 2.75 m/s a hard slap at `creature_slap_mps` 1.5,
  at size 0.22 and at size 1.8 with the eye 72 units away), the room to the
  world and back, the face of the panel and the place near the Creature.
- The stroke (`StrokeChecks` in `near_checks.h`): the hand of a stroke at 3.6
  cm and at 48 cm, the band, the same wave for a hand that is still, the
  limit of the wave, a backhand that is a slap by the tip and none by the
  grip, the limits and the start at 0.75, limits that are not sane, the point
  of the flat hand (the palm is the tip plus the offset, the finger and the
  key at 0 keep the tip, an old offset is not used, and the point does not run
  away).
- The leash (`LeashChecks` in `haptics_checks.h`): `LeashClick` (a click for
  no leash, for the leash on the Hand and for a tied leash, and none for the
  Creature of a different player, a Creature that can have no leash and a
  Creature on the leash of a different player) and `BoneSane`.
- The commands (`command_checks.h`): the shake (4 strokes at 4 Hz on each
  axis, a shake of 10 cm at 3 Hz, no shake for a wave, a tremor, short
  strokes, a still hand, a reach, a throw, slaps, a square, a circle, and a
  row that a gap cuts, and the threshold of 1.5 m/s), `ReleaseCommand`,
  `ReleaseLandAim`, the time of the ring after a send, `AimAtBody`,
  `RayLandDistance`, `AimLandWins` and `AimInRange`, `HeldStep` (the land at
  once, the object after 100 ms, a ring that passes 5 objects, an aim at the
  border for 200 frames), `ReleaseLive` and `DoublePress`.
- The near target is in `far_checks.h` (see `grab.md`).

### 6.2 A Creature in a desktop run

Two scripts in `bwvr/tools/desk/` reach the Creature of the player:

- `creature_recipe.ps1` runs the Creature state, strokes and a slap. It uses
  `spawn_run.ps1` and `desk_run.ps1`.
- `spawn_run.ps1` starts a game with the choice "Keep your old Creature" in
  the start dialog. That game has no story, and the Creature of the profile
  stands at the door of the Temple. The God view goes to that door when the
  dialog closes. The game camera comes to rest there, and the steps must
  start after that rest (frame 800 in the recipe). A zoom step before the rest
  leaves the view at the shore.

```
creature_recipe.ps1 -Name <run> -Dll <dll>
creature_recipe.ps1 -Name <run> -Dll <dll> -Kind leash
```

The kind `state` (the default) does these steps:

1. Two zoom steps in (44 units a meter), so the Creature is in the view of
   the desktop window. At 100 units a meter the Creature is at the lower edge
   of the window, and the controller code drops a press that starts off view.
2. `seek` puts the Hand's point on the body of the Creature. The Creature
   walks, so the grip comes 6 frames after the seek and stays closed for 800
   frames: the Creature state.
3. `nudge` steps the hand 2 cm to the side (a stroke), `slap` 12 cm (a hard
   slap), and `rot` turns the wrist (the backhand:
   `fake_controller_rots=-112,1,0 8,1,0` is a turn of 120 degrees).
4. `aimfar` aims the hand at the Creature, and the grip for 300 frames changes
   the leash from a distance. No Creature state starts.

The kind `leash` stays at 100 units a meter: the grip from a distance (on), a
short press of the trigger with the aim on the Creature (off), the grip (on),
and a grip of 300 frames (off, and no Creature state).

The settings of the `state` kind are `seek_plan=creature`,
`far_test_creature=1`, `far_test_influence=1`, `grab_hand=1`,
`far_grab_max_m=0` and `fake_controller_rots`. Lines to read in `bwvr.log` of
the run:

| Tag and text | Meaning |
| --- | --- |
| `hand creature: the body of the Creature ...` | the body bone before a press |
| `hand pick : the target is ... (Creature)` | the choice at the capsule |
| `hand press : the grip pressed on the target` | the press |
| `hand creature: the Creature state starts` | the state |
| `hand creature: a stroke / a slap / a hard slap` | what reached the game |
| `far grab : the grip closed on your Creature` | the leash from a distance |
| `creature cmd : the leash is on / off` | the leash |

For the commands, the desktop run uses `creature_command_test=N` (section
4.5). No desktop run can reach an owned Creature on a leash from a new game.
With the recipe and the word `aimcmd`, a tree near the Creature is the target.

The speed of a step of the fake hand depends on the time of its frame, and a
step is one frame long. A real stroke is not. So the desktop shows that the
path works. The headset shows how it feels. A capture of 2880 by 2880 can take
more than 250 ms. The controller then lets go of its buttons ("not usable
now"), and the Creature state ends.

### 6.3 What the desktop cannot show

- A stroke or a slap at the right feel at `creature_slap_mps` 1.0 and 5 cm of
  reach, and a backhand and a forehand, with the arm and with the wrist.
- That the camera stays still at a press on the Creature.
- That the size of the capsule fits the Creature's body.
- That the Creature tutorial goes on with no camera mode (it likely waits on
  the packets `0x27` and `0x59`, not on the camera [G]). Fallback:
  `creature_camera=1`.
- That the hand opens flat or as a finger at the right zoom, that 0.75 is a
  good change point, that the fingers of the tickle read as a stroke, that the
  glow at the palm reads well, and that a slow stroke with the flat hand is
  never a slap.
- That the halo of the Creature does not show when the player does not want
  it, and that the grip from a distance puts the leash on without the
  Creature turning to the hand.
- Whether a tap sends the Creature to the place that the player pointed at,
  whether a trigger that closes by accident sends it too often, and whether
  the Creature does what the player expects on each kind of object.
- Whether 0.6 m/s and 4 strokes fit a real shake, and whether a normal move
  of the hand never takes the leash off.
- Whether 3 degrees and 4 m are the right numbers for a real hand, whether
  the wait of 100 ms is right, and whether the halo on almost each object in
  the cone is too much while the leash is on.
- Whether 500 ms is the right time for a real finger, and whether it feels
  right that the first tap sends the Creature before the second press ties
  the leash.
- That each press on the land has its land point in the headset: the log line
  of the press must say "The arc of the press meets", not "has no land point:
  the arc meets no land".

## 7. Not built, open

- **The touch with no press** (`creature_touch`). The
  Hand within 25 cm of the body would press the trigger for the player, and
  the release past 35 cm. The mod must then hold the game's right button for
  the player (`controllers.cpp`, `PumpButtons`). The hand passes the Creature
  each time the player reaches for an object near it, and the Creature learns
  from each stroke and slap. So the touch needs a guard: an open hand (no
  finger on the trigger and the grip, which the Touch controllers report) and
  0.2 s in the body. A slap comes from outside the body in one or two frames,
  so it cannot wait 0.2 s, and it keeps the grip. It waits for a headset test
  of the touch above.
- **A `OneOffSpellSeed`** gets a tap and no leash, as in the game.
- **A stroke at 44 units a meter** with the fake hand gives the `stroke` ticks
  of the mod in some runs and no stroke of the game. The speed of a step
  depends on the time of its frame. The headset decides.
- **The size of the Creature** in the capsule changes in the game over a
  session (radius 1.02 to 1.44 for the same object). The mod reads the size
  of the game on each frame. Why the game changes it was not looked at.
- **One slap that was none.** In one desktop run a step of 4 cm in one frame at
  0.93 m/s counted as a slap, and a second slap came 25 frames later with a
  hand that was still. It did not come back in other runs. The log line of a
  slap has four more facts for the case (section 2.8).
- **The flat hand with the tip in the land** (the palm goes up with the tip)
  had no run.
- **A bone that is not sane** had no run. The place on the map as the answer
  has only the math test.
- **The tickle** of the flat hand moves the fingers while the palm is in the
  body. If the player reads it as a stroke with the fingers, make the tickle
  smaller.
- **What action `0x5D` is** and the rest of `ForceActOnMapCoord` were not
  traced. The Creature in the sea (desire and action values) was not traced.
- **A sailor in the sea, an object 15 to 35 cm from the Hand in the aim, a
  forest in the cone with the list of 128, and the green halo on a red far
  target** have no run with an owned Creature on a leash.
