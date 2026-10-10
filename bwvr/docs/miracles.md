# Miracles and gestures

This doc has two parts. The first part says how the plain game does gestures
and miracles. The second part says what the mod does so that a player can
make a miracle active, cast it and cancel it with the real hand.

Marks: [V] is verified in the disassembly. [I] is inferred. The facts come
from `disasm.py` and the Ghidra project on `runblack.exe` 1.42, and from
`Scripts\info.dat` and `Data\Gestures.jty`.

Related docs: `grab.md` (the grip), `throw.md` (the throw), `creature.md`
(the Creature), `village.md` (the Worship Site, the icons and the totem),
`hand-model.md` (the halo and the haptics).

## Summary

- A gesture is a path of the pointer on the land. The game records it, finds
  the corners and matches it to a template of `Gestures.jty`. A miracle is a
  chain of one to three gestures. No button starts a gesture.
- A miracle is active when a `SpellSeed` is in the Hand (hand state 8,
  Grain). It comes from a one-off bubble (a tap) or from a Worship Site (the
  chain of gestures or a tap on the icon).
- The right button of the mouse casts. The cast kind (pour, release, press)
  is a field of the seed info.
- A shake of the pointer (the scribble gesture) cancels.
- The mod puts the Hand at the real hand in the Grain state. The grip casts.
  A shake of the real hand or a short press of the second face button
  cancels. The trigger makes a carried seed active. The mod gives a target
  with a halo to a Creature miracle.

## Gestures in the plain game [V unless marked]

### Storage

| Where | What |
| --- | --- |
| `GGame+0x250064` | `GestureSystemDataList*`, the 81 templates of `Data\Gestures.jty` (loaded by `GGame::LoadFiles` at `0x005503BF`). The templates at `+0x08`, the count at `+0x0C` |
| `GGame+0x250068` | `GestureSystemData*`, a scratch path. Each match builds it again from the ring |
| `GGame+0x25006C` | `GestureSystem*`, the ring |
| `GGame+0x250070` | `GestureSystemResult*`: the type at `+0x00`, the start and end corners at `+0x08` and `+0x09`, the template number at `+0x0A` |
| `GestureSystem+0x08` | a ring of 80 samples, `0x28` bytes each |
| `+0xC88` | byte, the sample count |
| `+0xC8C` | the count of repeated samples |
| `+0xC90` | byte, the ring head |

The constructor of `GGame` (`0x0054B6D5` to `0x0054B772`) makes the list,
the path (`0x65C` bytes), the ring (`0xC98` bytes) and the result in that
order, at `+0x250064` to `+0x250070`.

One sample: `+0x00` screen x, `+0x08` screen y (floats from int pixels),
`+0x0C` the turn at this corner, `+0x10` the direction of the segment that
starts here, `+0x14..+0x1C` the land point under the pixel, `+0x20` the flags
and `+0x24` the angle of the segment. The flags are 1 for the oldest sample
(the start), 2 for a corner, 4 for the newest after a corner and 8 for the
newest. The direction is the angle in eight steps: 0 up, 2 right, 4 down, 6
left, with the screen's y down (`fn_00578730`).

A template (`0x65C` bytes, 1628 in the file after a 4 byte count) holds up to
80 corners of five floats (x, a pad, y, the turn, the direction), then the
corner count (`+0x648`), the gesture type (`+0x649`, `GESTURE_TYPE`), a kind
(`+0x64A`, 2 in every template), the aspect of its box (`+0x64C`), and three
switches: test the start direction (`+0x650`), accept the path backward
(`+0x654`) and test the aspect (`+0x658`). The corners are on a square of side
1. The game's own names of the 23 types are at `0x00BEF308`: spiral, inverse
spiral, S shape, circle, scribble, three, vertical, star, the four forks,
heart, R shape, square spiral, Cyrillic L, E shape, reverse S, infinity, W
shape, house, inverse square spiral and square wave. Several templates share
a type.

### Recording

- No button starts or ends a gesture. Each pointer move reaches the interface
  as message 0 (handler `0x005D99E0`). The handler copies the pointer to
  `GInterface+0x428` (`0x005CECF0`) and calls `fn_005CEAD0(pointer)`.
- `fn_005CEAD0` is thiscall on `GInterface`, one argument (the `int[2]`
  pixel), `ret 4`. It skips while the game is paused (`GGame+0x14` bit 2) or
  while the cooldown `GInterface+0x378` is above 0. It finds the land under
  the pixel with `fn_005E5620` (edx = the pixel, the land out and 0 on the
  stack, `ret 8`). With land it adds a sample with `fn_0057BBC0(&land,
  pixel)` (thiscall on the ring, `ret 8`). Without land it adds the pixel with
  the last land point (`fn_0057C8F0`), unless the ring is empty.
- A sample at the same pixel as the last one still goes in, and adds to
  `+0xC8C`. After 70 of them the ring clears. So a still pointer clears the
  gesture.
- `fn_0057C3F0` marks the corners after each sample. It looks back to the
  last corner for the sample with the largest turn, at least 0.2945 rad
  (`0x00D064FC`). Samples closer than 4 px do not count as a segment
  (`fn_0057C5C0`). Two corners closer than 12 px, or in a box under 50 px,
  merge into one (`fn_0057C630`). These are the only sizes in pixels.
- The ring clears on a pick up (`fn_005DC330`), a drop (`fn_005DC1E0`), a
  match (`fn_005CE420`), in fights (`0x005D4AC0`), when a leash goes on
  (`fn_005E6B00`) and when a Miracle leaves the Hand (`0x005D3340`).
  `ProcessPowerUpSystem` also clears it at its start in the Temple
  (`GGame+0x205A28 == 1`), while the interface is not active
  (`GInterface::IsActive`, `+0x28` bit 0), and while `GCamera::IsMoving`
  (`+0x74`). The camera mode can allow gestures during a move
  (`CanPlayerGestureWhenCameraMoving`). `CameraModeNew3` allows it only in a
  fight (`+0x44` and `+0x48`). So a land drag clears the ring, and a mouse
  gesture is drawn with no button held.

### Recognition

`GInterface::ProcessPowerUpSystem` `0x005CF300` (thiscall, no arguments)
runs once a frame from `fn_005D1120`. It makes a "looking for" list of the
gesture types that fit the moment, and it tests each of them with
`fn_005CF220(type)`:

| Moment | Types | On a match |
| --- | --- | --- |
| A Miracle in the Hand is ready to power up (`GInterface+0x39` bit 1) | its own gesture (`SpellSeedInfo+0x10C`) | `fn_0057A5E0` places the power up, then `fn_005CE420(1)` |
| A Miracle in the Hand (`fn_005CEF50`) | the scribble (5), and up to three types at `GInterface+0x16C` | the scribble drops the Miracle. The others power it up (`fn_005D0000`) |
| A chain of a miracle has started (`GInterface+0x364`, `fn_005CF1C0`) | the next gesture of each candidate miracle, and the scribble | `fn_005CFAE0`, see below |
| The Creature's leash is on (`fn_005CEFB0`) | the leash gestures | `GInterface::DoLeashSelectionSystem` |
| Other states of the interface status (`+0x90`, `+0x12C`), not traced | the scribble (5) | packet `0x1E` or `0x5F`, or `fn_005D4350` |
| An empty Hand (`IsHandReadyForObject`) | the two start types of `GSpellSystemInfo` (`+0x14` and `+0x18`, at `0x00DA0574` and `0x00DA0578`). Each only when a Worship Site of the player has a miracle icon that may be asked for (`WorshipSpellIcon::ValidForRequestSpell`) and whose first gesture is that type | `fn_005CF010`: the chain starts |
| An empty Hand | the "again" type (`GSpellSystemInfo+0x10`, `0x00DA0570`), for the last miracle (`GInterfaceStatus+0x34`) | the same miracle again |

**A miracle is a chain of two or three gestures.** Each miracle icon has three
gesture types (`+0x100`, `+0x104` and `+0x108` of its `GSpellSeedInfo`). The first
is one of the two start types. A match of a start type (`fn_005CF040`) lists
the miracles that start with it, sets step 1 (`GInterface+0x360`) and turns
the chain on. Each next match (`fn_005CFAE0`) keeps the miracles whose gesture
at this step is the one drawn. When a miracle has no next gesture, or at step
2, the game sends packet `0x25` with its spell seed type (`fn_00550C20`), and
the Miracle comes into the Hand (hand state 8, Grain). The chain ends after
`GSpellSystemInfo+0x1C` seconds (`0x00DA057C`) with no match, and the
scribble ends it at once. Each piece is a match of its own, so each piece
clears the ring and starts the 0.4 s cooldown. [V for the code. I for how a
player sees it: as one gesture with a common start]

`fn_005CF220(type)` builds the path from the ring and calls the match.
`fn_00578C20` (thiscall on the path, the ring as the argument, `ret 4`) copies
the corner samples (flags `& 0xB`) and the aspect of their box.
`fn_00579F10` (thiscall on the list, `(type, path, result)`, `ret 0xC`) then
tests each template of the type with `fn_0057A050`:

- It tries each corner of the path as the start, so a gesture can end a
  longer path.
- The turns of the path and of the template must agree to 0.589 rad
  (`0x00D064BC`) at each corner. A turn under 0.515 rad (`0x00D064C0`) can
  merge with the next one.
- With `+0x650` the first direction must be the template's (out of eight).
  With `+0x654` the path may also run backward (`fn_0057A3E0`).
- With `+0x658` the aspect of the box must be in the same class as the
  template's: under 0.15, 0.15 to 4, or over 4 (`fn_00579FA0`). The aspect
  is the width over the height, divided by the width over the height of the
  game's screen (`0x00E85058`).

So the match reads angles and directions, not the size. The size only has to
be well above the corner rules (4, 12 and 50 px). The land points take no
part in it. `0x00579F70` (thiscall on the list, `(index, path, result)`,
`ret 0xC`) tests one template. An unused debug function (`0x0057B980`) runs
it on every template and prints the names.

**On a match** `fn_005CE420(1)` clears the ring, sets the cooldown
`GInterface+0x378` to 0.4 s (`0x00BF1AF8`) and starts Immersion effect 3. Its
first call (`0x00689790`, which the symbol file names
`GJMesh::GetBoundingBox`) makes a `PSysGesture` from the land points of the
ring: the game draws the matched gesture on the land. No trail shows while
the gesture is drawn [I: no other code reads the ring's land points to
draw]. `GInterface::Get3DSoundPos` puts a sound at the newest land point.

**How the game teaches it.** At the end of each run, `ProcessPowerUpSystem`
gives the "looking for" list to `fn_0068ABA0`. That makes a `DisplayGesture`
for each type the game is looking for, only when the list changes [V]. This
is the picture of the gestures that the player can draw now [I]. The matches
also start help events (`HelpProfile::Trigger`): `0xE` (again), `0xF` (a
chain starts), `0x10` (a next piece), `0x11` (the Miracle), `0x14` to `0x16`
(a scribble) and `0x17`. The `SpellDispenser` class (`0x00722730`) was not
traced.

### Feeding a 3D path

The mod draws a gesture with the real hand through the game's own ring.

- While the draw button is held, the grip point goes on a plane that faces
  the head at the start of the stroke, with the head's right and up axes.
- The plane becomes pixels at `gesture_size_cm` (30) for a third of the
  render height, around the controller's pixel at the start.
- A pre hook on `ProcessPowerUpSystem` adds a sample when the pixel has
  moved a fortieth of the render height, and first a corner between the two
  when there is one. It adds the samples through the game's own
  `fn_005CEAD0`, so the pause, the cooldown and the land under the pixel work
  as they do for the mouse. A still hand adds nothing, so the 70 repeat rule
  never clears a stroke.
- A pre hook on `fn_005CEAD0` drops the game's own samples of the pointer
  while the controller owns it.

## A miracle in the Hand in the plain game [V]

### Two classes

A miracle is in one of two objects:

- `OneOffSpellSeed` (vtable `0x008F3774`, a `MobileObject`): the bubble of a
  reward, of a spell dispenser or of a script. It is an object on the land.
  `OneOffSpellSeed::Create` `0x0072A2F0` makes it (cdecl: a `MapCoords*`, the
  seed type, the power up, a float, and the caller cleans `0x10` bytes).
  `Reward::InterfaceTap`, `SpellDispenser::CreateOneOffSpellSeed`
  `0x00722B80` and the scripts call it. The seed type is at `+0x68`.
- `SpellSeed` (vtable `0x00981FC0`, an `Object`): the miracle that is active
  in the Hand. Its info is at `+0x58`, the icon of its Worship Site at
  `+0x5C` (null for a seed from a bubble), the spell that runs at `+0x60`,
  the power up at `+0x6C`, and the byte "ready" at `+0x90`.

**A one-off seed becomes active with a tap.** A right press on the seed
starts interface action 13 (`FUN_005D1740`, with the time of the press at
`GInterface+0x454` and the game turn at `+0x458`). Its step `FUN_005D5250`
takes the time since the press: the real time, or 100 ms for each game turn,
the smaller of the two.

- The button goes up before 226 ms: `fn_005D3930` (the click) runs, and
  `fn_005D38A0` sends the tap, packet `0x20`. The tap needs
  `InterfaceValidToTap` and the influence at the land point of the record
  (`+0x48`).
- The button stays down for more than 224 ms: the game picks the seed up as
  an object. The Hand carries it in the Holding state.

Packet `0x20` runs `FUN_005DA650` and then `OneOffSpellSeed::InterfaceTap`
`0x0072A640`. That calls `CreateSpellIntoHand` `0x0072A730`: it needs
`IsHandReadyForObject`, makes a `SpellSeed`, puts it in the Hand
(`PlaceObjectInMagicHand`) and makes it ready. Then it starts Immersion
effect 14, plays sample `0x6D` and deletes the one-off seed.

**A seed from a Worship Site** comes from the chain of gestures above:
packet `0x25`, `FUN_005DABA0`, `FUN_0064BDB0`. It has the icon link at
`+0x5C`. `SpellSeed::ProcessInHand` `0x00729930` makes it ready after the
time at `0x00DA0584`. The icons, the chants and the tap on an icon are in
`village.md`.

### The states

With a `SpellSeed` in the Hand, `CHand::GetRequiredState` gives state 8,
Grain, and the hand mode chooser `fn_005D7F20` gives hand mode 5 (`0x18` for
each other object in the Hand). `HandStateGrain::Update` `0x005B3310` calls
`HandStateHolding::Update` at `0x005B3361`. So the Grain state places the
Hand as the Holding state does, with the ray, the distance and the pose
builder. Then it keeps the place of the Hand at `+0x48E0`, and a filter of
the speed of the Hand at `+0x48C8`: the throw velocity. The rate of the
filter is `-10 ln(1 - 0.8)` a second (`0x00BF014C`), a time constant of
62 ms. A one-off seed that the Hand carries is in state 4, Holding.

### The seed info

`GSpellSeedInfo`: 30 records of `0x190` bytes at `0x00D9D678`
(`load_variables`, `0x0042C28E`, the block `DETAIL_SPELL_SEEDS`). In
`Scripts\info.dat` the records are `0x180` bytes from offset `0x3D28`, and a
field at `+X` in memory is at `+X - 0x10` in the file.

| Field | Meaning |
| --- | --- |
| `+0x100` | The first gesture of the chain that asks for the miracle: 1 (the spiral) for a miracle of the god, 2 (the inverse spiral) for a Creature miracle |
| `+0x10C` | The gesture that the cast needs, or 0 |
| `+0x110` | How the miracle is cast: 0, 1 or 2 (below) |
| `+0x114` | 1: the seed stays in the Hand while its spell runs (`IsSpellKeptInHand`) |
| `+0x118` | 1: the game can cast it on an object (`SpellSeed::CanCast` `0x00729190`) |
| `+0x124` | The magic type of each power up, 4 of them |
| `+0x134` | The gesture of each power up |
| `+0x168` | 1: the seed is deleted after the cast (`IsThisSeedDeletedOnceCast`) |
| `+0x184` | The help text of the tooltip |

| Type | Seed | Cast | Kept | On an object | Its gesture | Power up gestures | Deleted |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Storm | release | no | no | circle | inverse spiral, spiral | no |
| 1 | Forest | press | no | no | none | none | no |
| 2 | Fireball | release | no | no | none | inverse spiral, spiral | yes |
| 3 | Food | pour | yes | no | none | spiral | no |
| 4 | Spiritual Shield | release | no | no | circle | none | no |
| 5 | Physical Shield | release | no | no | circle | none | no |
| 6 | Lightning | pour | yes | no | none | inverse spiral, spiral | no |
| 7 | Heal | press | no | no | none | spiral | yes |
| 8 | Wood | pour | yes | no | none | none | no |
| 9 | Water | pour | yes | no | none | spiral | no |
| 10, 11 | The two flocks | release | no | no | none | none | yes |
| 12 to 27 | The Creature miracles (16 is Strong) | press | yes | yes | none | none | yes |
| 28 | Teleport | press | no | no | none | none | yes |
| 29 | Explosion | press | no | no | none | inverse spiral, spiral | yes |

### The buttons and the cast

The left button drags the land, as with an empty Hand. The right button, the
Action button, casts. `FUN_005D1330` (the right press) goes to `FUN_005D1560`
when the Hand holds an object (`GInterfaceStatus+0x90`):

1. The object is `CHand::GetCreatureToGiveTo`, or else the object of the next
   pick record (`+0x400`). When `ValidToApplyThisToObject` (slot `0x71C`,
   `0x007286D0`) gives 1 for it, the miracle is cast on it: `fn_005D30D0`
   sends packets `0x4D` and `0x11`. A miracle of cast kind 1 first waits for
   the release (action 9).
2. With no such object, and the land point of the record (`+0x3F0`) inside
   the influence (`+0x48`), the land point is the target. Cast kind 1 starts
   action 8 (`FUN_005D2730`). The other kinds go to `FUN_005D1850`, and that
   calls `FUN_005D3340`: packets `0x4D`, `0x16` and `0x12`.
3. With no such object and the land point outside the influence, the press
   does nothing: no cross and no sound.

`ValidToApplyThisToObject` needs the seed ready, an object that is no scroll,
and `SpellSeed::CanCast`. That passes for four kinds of object:

- The place of a Worship Site of the player that takes the miracle back
  (`IsSpellSeedReturnPoint`, slot `0x670`).
- A spell dispenser (slot `0x1F4`).
- A magic fireball (slot `0x218`).
- With `+0x118`: an object where the cast rule passes at its place, and that
  passes the test of the magic info (vtable `+0x2C`).

Only the Creature miracles have `+0x118`. So no miracle of the game is cast
on a villager: Heal is cast at a place.

**How each kind is cast** (`+0x110`):

- **0, the pour** (`IsSpellCastInHand`): food, wood, water, lightning. The
  press starts action 10 on the land (or 11 on an object). While the button
  is held, the step `FUN_005D4C10` sends the cast again at the land point of
  the record, once a game turn. The release sends packet `0x1A`
  (`ApplyUnlockProcess`). The seed stays in the Hand.
- **1, the release** (`ApplyOnlyAfterRecSystem`): the press starts action 8
  and clears the ring of the gestures. The game records the path of the
  pointer while the button is held. The release (`FUN_005D48D0`) calls
  `FUN_005D3340`, which sends the throw velocity `CHand+0x48C8` in packet
  `0x4D`. A fireball and a flock fly with that velocity. A storm and the two
  shields need their own gesture, the circle: `ProcessPowerUpSystem` matches
  it while the button is held (`0x005CF4E0`, `GInterface+0x17C`), and the
  circle gives the place and the size (`fn_0057A5E0`).
- **2, the press**: the cast is at once, on the object or at the land point.
  A Creature miracle stays in the Hand while its spell runs (`+0x114`), and
  `ProcessInHand` deletes the seed when the spell ends.

### Power ups

Only a seed from a Worship Site can power up. `fn_005CEE80` lists the
gestures of the power ups that the player has and that the site can pay
(`fn_0077FBF0`), at `GInterface+0x16C`. `fn_005D0000` matches them, and a
match sends packet `0x6A` (`FUN_005DAC30`, `SpellSeed::SetPowerUp`
`0x00729B30`, which takes the chants from the site). The gestures are the
spiral and the inverse spiral. A shake of a seed with a power up takes the
power up off first.

### The cancel

The shake is the scribble gesture, type 5. For a seed from a Worship Site
`fn_005D0000` matches it. For each other object in the Hand
`ProcessPowerUpSystem` matches it at `0x005CF6ED`, when the land point is
inside the influence and `ValidToShakeFromHand` (slot `0x710`) gives 1. A
match calls `PHandFX::DoRemoveFromHandVisual` (`0x0068CE90`),
`fn_005CE420(0)` and `fn_005D4350`. That sends packet `0x4D` with no velocity
and packet `0x1D`. The handler `FUN_005DA8F0` calls
`SpellSeed::ThrowObjectFromHand(status, 1)` `0x0072ACD0`: a seed from a
Worship Site gives its chants back to the site, and each seed is deleted. So
a one-off miracle that is shaken out of the Hand is lost.

### The refusal

`fn_005D18F0` (the held object and a `MapCoords*`, `ret 8`) plays sample
`0x25` and sends packet `0x2B`, which makes the yellow cross at the land
point. Its callers for a miracle:

| Caller | The cause |
| --- | --- |
| `FUN_005D1560` (`0x005D16B4`) | Cast kind 1, and the land point is not valid |
| `FUN_005D1850` (`0x005D18D2`) | Cast kind 0 or 2 with no object, and the land point is not valid |
| `fn_005D30D0` (`0x005D32E4`) | The gesture at `+0x1A4` is not the seed's own gesture (`fn_00729AF0`) |
| `FUN_005D3340` (`0x005D364E`, `0x005D3663`) | The same gesture test, a land point off the map, a land point that is not valid, or no influence at it |

A land point is valid (`ValidToApplyThisToMapCoord` `0x00728720`) when the
seed is ready, the cast rule of the magic info (`GMagicInfo+0x34`,
`fn_005FB5D0`: 0 anywhere, 1 on land, 2 in the influence, 3 on land in the
influence) passes, and the test of the magic info (vtable `+0x30`) passes. A
Creature miracle fails that last test at each land point. No test of the
power is in these functions: the game takes the power when it makes the seed
and at a power up.

### The Immersion effects

The names are in the game's own enum (`IMMERSION_EFFECT_TYPE` of `Enum.h`).
Those of a miracle: 3 a gesture is known, 8 the trail of a gesture, 9 a
miracle crackles, 14 a one-off seed pops, 18 to 20 a fireball, 21 lightning,
23 a miracle is cast, 24 a shield, 25 water, 26 and 27 a flock, 28 a
Creature miracle. `DoPostCastThings` `0x00729260` starts the effect of the
magic info (`+0x14`) when the spell starts.

## What the mod does (`miracle_hand=1`)

The code is in `src/mod/miracle.cpp`, with the pure math in
`miracle_math.h`. `hand_direct.cpp` places the Hand and holds the pick
record, `controllers.cpp` gives the buttons, `hand_glow.cpp` draws the halo
and `hint_card.cpp` the lines of the card.

### The Hand is the real hand

`DirectState` of `hand_direct.cpp` has the Grain state. The Grain state runs
the update of the Holding state, so the hooks of the ray, the distance and
the pose builder place it with no other change. The land point of the pick
record is the land under the Hand, as in the Holding state. So the
influence, the pour and a cast at a place follow the real hand. With
`miracle_hand=0` the game places the Hand with a miracle, and the Hand of
the game is then away from the real hand.

### The target

`MiracleSelect` runs at the end of the first eye pass. A candidate is an
object that the game casts this miracle on: `ValidAsInterfaceTarget` and
`ValidToApplyThisToObject`. The body is a ball of the 2D radius of the
object, about the body bone of a Creature. The target is
(`miracle_math::AimAt`, `Better`):

1. The body within `miracle_touch_cm` (3 cm of the room) of the Hand's
   point. Of two, the nearer one.
2. Else the body within `miracle_cone_deg` (6 degrees) of the aim ray, and
   `miracle_max_m` (4 m of the room) from its start. Of two, the one nearer
   to the ray.

**The candidates.** The Creature of each player comes from the game
(`GPlayer+0xA4C`, 8 players), with its body from `BodyPlace`. Each other
candidate is an object that the draw offered: near the Hand, or in the aim
cone, which the draw offers while a miracle is in the Hand
(`MiracleWantsCone`). The draw alone is not enough for a Creature. It offers
an object in the aim only when the point of its mesh, the feet of a
Creature, is in the cone of the far grab, and the body of a Creature is 7
units and more above its feet. So the mod takes the Creature from the game.

**The target stays** (`miracle_math::Takes`). The target of the last frame
has wider limits: `miracle_keep_deg` (10 degrees), twice the touch and a
quarter more range. A new body takes its place only when it is at the Hand
and the target is in the aim, when it is nearer at the Hand, or when it is 2
degrees nearer to the ray. So a Creature that walks keeps the halo.

The target has the gold halo of a target of the grip (`MiracleHalo`,
`kHaloMiracle`). A Creature that the game does not let the miracle act on
has the red halo, and the card says why.

**The pick record holds the target.** `MiracleKeepRecord` runs after each
eye pass, before each interface step and before each right press. It puts
the target in the next record with a distance that the game keeps
(`near_math::RecordDistance`). With no target it takes each object out of
the record. So a press casts on the object with the halo, and on no object
that is behind the pixel of the Hand.

### The grip casts

The grip is the game's Action button one to one while a miracle is in the
Hand (see `grab.md`, "The grip holds").

| Kind of miracle | What the player does | What the game does |
| --- | --- | --- |
| A Creature miracle | Close the grip with the halo on a Creature | `fn_005D30D0`: the cast on the object |
| Heal, Forest, Teleport, Explosion | Close the grip with the Hand over the place | `FUN_005D3340`: the cast at the land under the Hand |
| Food, Wood, Water, Lightning | Keep the grip closed over the place | Action 10: a cast each game turn. The opening ends the pour |
| Fireball, the flocks | Close the grip, swing the hand, open the grip | Action 8: the release sends the speed of the Hand |
| Storm, the two shields | Close the grip, draw a circle with the hand, open the grip | The game matches the circle, and the release casts at its place |

**The throw** uses the game's own speed of the Hand: the Grain state filters
the motion of the Hand, which is the motion of the real hand. The mod does
not change it.

**The circle.** The mod feeds the ring of the gestures while the grip is
closed with a miracle that needs a circle, and the interface action is 8 or
9. The path of the hand goes to the ring as it does for the draw button
(`MiracleDrawsCircle`, `ControllersGestureInput`, "Feeding a 3D path").

### The way out

Two inputs take a miracle out of the Hand:

- A shake of the real hand: the detector of the leash
  (`command_math::ShakeStep`, 4 strokes of 6 cm at `miracle_shake_mps`,
  0.6 m/s).
- A short press of the second face button of the pointing hand (B, or Y for
  a left handed player): at most `miracle_cancel_ms` (400 ms) and 5 cm of
  hand motion. A longer press, or a press with motion, is the draw of a
  gesture.

The cancel runs before the next interface step (`MiracleInterfaceStep`). It
does what the game does after a match of the scribble: the visual of the
Hand, `fn_005CE420(0)` and `fn_005D4350`. The mod makes no test of the
influence, so the way out works at each place. The game's rule stays: a
one-off miracle that is cancelled is lost, and a miracle of a Worship Site
gives its power back.

### A seed that the Hand carries

The grip picks a one-off seed up as an object, so a player can move a seed.
A press shorter than 225 ms makes it active at once, by the rule of the
game. While the Hand carries the seed, the trigger makes it active
(`miracle_seed_use=1`):

1. The trigger does not reach the game. The grip looks open to the grip
   machine, so the Hand lets the seed go (the click of "The grip holds" in
   `grab.md`).
2. One frame after the Hand is empty, the mod calls the game's tap
   (`fn_005D38A0`, packet `0x20`).
3. The miracle is in the Hand. The grip stays up for the game until the real
   grip opens, so a closed grip does not cast by accident.

The game needs the land under the Hand inside the influence for the let go
and for the tap. `miracle_math::UseStep` ends the use after 120 frames with
no miracle, and the card says "Use it inside your influence".

### The card

`MiracleHint` has the context "a miracle in the Hand". Its lines stay while
the miracle or the seed is in the Hand. The hints of the game for a button
go, and one line of its text stays (the name of the miracle).

| State | Lines |
| --- | --- |
| The Hand carries a one-off seed | "[Trigger] Use the miracle", "[Grip] Open to drop" |
| A miracle with a target | "[Grip] Cast on the Creature" (or "Give the miracle back", "Put it in the dispenser", "Cast on the target") |
| A Creature miracle with no target | "Touch a Creature, or point at one" |
| A miracle for a place | "[Grip] Cast here" |
| A miracle to pour | "[Grip] Hold to pour", and "[Grip] Open to stop" while it pours |
| A miracle to throw | "[Grip] Hold, swing, open to throw", and "[Grip] Open to throw" while the grip is closed |
| A miracle that needs a circle | "[Grip] Hold and draw a circle", and "[Grip] Draw a circle, then open" |
| The spell of a Creature miracle runs | "The miracle acts now" |
| Each state of a miracle | "[B] Tap to cancel, or shake" |

After a refusal the card has the reason for 4 s (`miracle_math::RefusalOf`,
the tests of the game in its order): "The miracle is not ready yet", "The
Hand is not over the land", "This miracle needs land here", "This place is
outside your influence", "The Creature is outside your influence", "It needs
a Creature as its target", "Draw a circle on the land first" or "The game
refused the cast here".

### Haptics

The kind `cast` (0.45 for 30 ms) plays on the game's effects of a cast (18 to
21 and 23 to 28). `activate` plays when a one-off seed pops (effect 14).
`refused` plays once for each press that the game refuses. `let go` plays at
a cancel, and `target` for a new target.

### A sound with no Base

`PHandFX::DoRemoveFromHandVisual` plays sample `0x77` with the `PHandFX` of
the Hand as the object of the sound. The audio code asks each object of a
sound for its place through slot `0x10` of its vtable,
`Base::Get3DSoundPos` (one argument, `ret 4`). `PHandFX` derives from
`PSysHandFX` only, and its slot `0x10` is `PHandFX::DrawHandFX`, with no
argument and a plain `ret`. A call through that slot leaves 4 bytes on the
stack, and the stack check of the caller ends the game (`0xC0000409`,
`___report_gsfailure`). The game's own shake of a miracle has the same
fault.

`audio_object.h` (`AudioVtableIsBase`) reads the RTTI of the vtable: the
locator at `vt - 4`, the hierarchy at `+0x10`, and the names of the base
classes. `audio_sources.cpp` and `audio_trace.cpp` call slot `0x10` only for
a class that is `Base` or derives from it. On the exe 553 of the 949 classes
with RTTI derive from `Base`.

## Keys

| Key | Default | What it does |
| --- | --- | --- |
| `miracle_hand` | 1 | 0: the game places the Hand with a miracle, and the mod gives no target, no cancel, no use of a seed and no lines |
| `miracle_shake_mps` | 0.6 | The mean speed of each stroke of a shake, in m/s. 0 turns the shake off. 0.2 to 5 |
| `miracle_cancel_button` | 1 | 0: the second face button does not cancel |
| `miracle_cancel_ms` | 400 | The longest press of that button that is a cancel. 50 to 2000 |
| `miracle_seed_use` | 1 | 0: the trigger does not make a carried seed active. Then only a short press of the grip does |
| `miracle_touch_cm` | 3 | A body this near to the Hand's point is the target at the Hand, in cm of the room |
| `miracle_cone_deg` | 6 | The cone of the aim for a new target at a distance |
| `miracle_keep_deg` | the cone and 4 | The target of the last frame stays to this angle |
| `miracle_max_m` | 4 | The most distance of a target in the aim, in m of the room. 0 is no limit |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The A/B key**, with `test_keys=1`: Ctrl with the 8 key of the number pad
changes `miracle_hand`. The pointing controller gives a long pulse for 1 and
a short pulse for 0.

Test keys for the desktop runs only (the launcher removes them):

- `miracle_test_seeds`: a list `type@frame` with spaces between. At that
  frame the game makes a one-off seed of that type on the land under the
  Hand (`OneOffSpellSeed::Create`).
- `miracle_test_cancel`: a list of frames. At each one the mod asks for a
  cancel.

## The log

The tag `miracle` has one line for each change:

- The keys at the start, and each press of the A/B key.
- "the Hand holds the one-off seed ... as an object".
- "... is active in the Hand", with the seed, the power up, where it comes
  from, how it is cast, and the Hand state.
- "the Hand of the game with the miracle is at ..., and the point of the
  real hand is at ...: N cm of the room apart", 4 times after a miracle
  comes into the Hand, also with `miracle_hand=0`.
- "the target of ... is the ...", with the gap and the angle. "has no
  target now", with the cause: the gap and the angle of the old target
  against the keep limits, "the game does not let the miracle act on" it, or
  "the draw did not offer it". "the ... is at the Hand, but the game does
  not let ... act on it", with the reason.
- "the game casts ... on the ..." and "the game casts ... at the land
  point ...", with the speed of the Hand. For a pour: the first cast, and
  "the pour ends: the game sent N casts in M frames".
- "the game refused the cast of ... at ...", with the reason, the object of
  the pick record and the influence. Also with `miracle_hand=0`.
- "a shake of the hand: ...", "... the mod takes ... out of the Hand with
  the game's own drop".
- "the trigger closes with the one-off seed ...", "the Hand is empty, so the
  mod taps the seed ...", "the use of the seed ... ended with no miracle".
- "the spell ... runs", "... has power up N now", and "... left the Hand
  (the cause)", with whether the Hand is free.
- One line each report window: the frames with a miracle and with a seed,
  the casts, the refusals, the cancels, the seeds used, and the writes of
  the pick record.

## Tests

The math test (`miracle_checks.h`) covers the lines of the card for each kind
of miracle, each reason of a refusal in the order of the game, the target at
the Hand and in the aim (the gap, the angle, the range, no ray, and the
order), the target that stays, the cancel button, the steps of the use of a
seed, and the shake of the desktop runs (5 steps of 15 cm).

A desktop run (`bwvr/tools/desk/`) in the God view with the start "Keep your
old Creature" can test a miracle. `miracle_test_seeds` makes the seeds. A
press of 3 frames on a seed is the tap of the game. Desktop runs have shown
a cast of Strong and of Freeze on a Creature, the cancel by a shake and by
the button, a throw (Fireball and the two flocks), a pour (Water, Food, Wood
and Lightning), a circle (Spiritual Shield and Storm), a place (Heal, Forest
and Explosion), a carried seed with the trigger, and a seed from a Worship
Site (Water and Food) with a power up and the power that goes back at a
cancel. A Physical Shield with no circle gives the refusal "this miracle
needs a circle on the land first".

A miracle that pours stays in the Hand after the pour, until its chants are
used or the player takes it out. So a recipe with more than one seed must
tap B after a pour (`miracle_run.ps1 -Seeds` gives the seed types, and
`-Steps` the steps).

**Not run on the desktop:**

- Teleport (the cast of Heal), and the Creature miracles other than Strong
  and Freeze.
- A Creature outside the influence (the red halo and its line), each reason
  of a refusal other than "needs a Creature" and "needs a circle on the
  land", a Worship Site or a dispenser as the target, and a magic fireball.
- The line of the card with `miracle_seed_use=0`, and each key at a value
  that is not its default, other than `miracle_hand`.
- A left handed player, `controller_swap_buttons=1`, and a multiplayer game.

**Needs the headset:**

- Whether a real shake of 0.6 m/s takes the miracle out and a normal move
  does not.
- Whether 400 ms is the right limit of the tap of B.
- Whether the speed of a real swing throws a fireball as far as the player
  means. The desktop runs had only a step of the hand.
- Whether a circle of the real hand over the land fits the circle of the
  game.
- Whether 3 cm and 6 degrees find the Creature with no effort.
- Whether the miracle looks right in the real hand.

## Limits

- A cancel takes a power up off together with the miracle: the game takes
  the power up off first.
- The trigger does not make a carried seed active outside the influence.
- The aim finds a target only in the God view and on the table, where the
  cone of the far grab is installed.
