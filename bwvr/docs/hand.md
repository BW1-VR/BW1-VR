# The Hand: data flow, and a design for direct hand control

This file records how the game places the Hand, what the Hand points at, how
it throws, how it strokes and slaps the Creature, and how it reads gestures.
Then it gives a staged design for roadmap steps M3 to M6. All four are built
(see "M3 as built", "M4 as built", "M5 as built" and "M6 as built" in section
7).

## How to read this file

- **[V]** means verified. The claim comes from the disassembly (capstone,
  `bwvr/tools/disasm.py`) or from a Ghidra 12.1.3 decompile of our exact
  binary, and the text quotes the address.
- **[I]** means inferred. The claim fits the code but a live test or a
  deeper read must confirm it.
- World units: the mod uses 7.5 units per metre (see
  [backend.md](backend.md), "A separation of zero means ask").
- "The pointer" means the pointer globals `0x00E852C0` / `0x00E852C4`, in
  render pixels after the mod's pointer map (see [backend.md](backend.md),
  "One pointer, not two").

### Two symbol names are swapped

The bw1-decomp symbol file swaps two names. `disasm.py` prints the wrong
name for both. [V]

| Address | Symbol file says | It really is | Proof |
| --- | --- | --- | --- |
| `0x005B3C70` | `ObtainRequiredHandPosition` | `HandStateHolding::Update` | slot 3 (+0x0C) of the `HandStateHolding` vtable `0x00900B40`. It starts `mov esi, ecx` and reads the CHand at `[esi+4]` |
| `0x005B5E70` | `HandStateHolding::Update` | `ObtainRequiredHandPosition` | `HandStateNormal::Update` calls it at `0x005B71CB` with nine stack arguments and cleans `0x24` bytes. That matches the mangled signature `(CHand*, float, float, float&, LHPoint&, LHPoint&, int&, bool, bool)` |

This file always uses the real names.

### Tools used

- A Ghidra project of the exe with full auto analysis and the names of
  bw1-decomp is in `local\ghidra`. `bwvr/tools/re.ps1 setup` makes it in
  about 3 minutes, and `re.ps1 decompile` gives the C of a list of
  addresses in `local\ghidra\out\`.
- The research for this file used an older project of the same exe, in
  which the names came after the analysis. There the decompiler lost the
  register arguments of `__thiscall` and `__fastcall` functions. So every
  calling convention in this file was read from the disassembly (the `ret`
  and the first instructions).
- In the project of `re.ps1` the names come before the analysis. Ghidra
  then makes the class, the signature and `this` from each decorated name.
  A signature that comes from a name is a claim of the symbol file, not a
  proof. Check it in the disassembly before you mark a fact [V].

## 1. Where the Hand's position comes from

### The frame chain [V]

```
GGame::Process3dEngine 0x0054DA80
  CHand::PrepareForDrawing 0x0046C550          vtable slot 6, once a frame (pass guard)
    screen point      +0x485C/+0x4860 <- pointer 0x00E852C0/C4     0x0046C6C5
    yaw               +0x84 <- GUtils::SetUnitDirectionVectorFromScreenPoint 0x007FAA50
    state             GetRequiredState 0x0046CD10 -> fn_0046CF80 -> Exit/Enter
    screen spring     +0x48B8.. (the lean of the Hand)
    HandState::Update [state vtable +0x0C](dt seconds, *(+0x47F0))   0x0046C921
      Normal, Holding: ObtainRequiredHandPosition 0x005B5E70
        distance       fn_0046DF60, Zoomers, CHand::SetDistanceFromView 0x0046C0D0
        position       ScreenPointToWorldAtDistance 0x0074CAF0
                         UnprojectScreenPoint 0x0081B370
      pose             fn_0046E160 (the pose builder) -> 0x00C5E680, root matrix
    blend             0x00C5E660 = lerp(0x00C5E670, 0x00C5E680) for 0.13 s
    held object       translation = 0x00C5E660 + (0x00D13F20..28)
    mesh              12 floats of *(+0x47F0) -> (+0x482C)+0x14, fn_00814FD0
```

### CHand fields

`CHand` derives from `Morphable`. Offsets are from the CHand pointer
(`GInterface+0x3A0`, `MyInterface()->hand`). `GInterface+0x3A0` holds the
`CHand*` itself: `Process3dEngine` loads it at `0x0054DE9C` and calls vtable
slot 6 on it [V].

| Offset | Meaning | Source |
| --- | --- | --- |
| `+0x78` | the Hand's world position (`Morphable::position`, LHPoint) | [V] output `param_5` of ObtainRequiredHandPosition |
| `+0x84` | the Hand's yaw, from the screen ray | [V] `0x0046C725`, read by the pose builder |
| `+0x90` | `Size1` of the Hand | [V] header, used in fn_0046DF60 |
| `+0x47F0` | `LHMatrix*`, the root pose (12 floats, translation at +0x24) | [V] copied to the mesh at `0x0046CAE5` |
| `+0x482C` | the Hand mesh object | [V] |
| `+0x4834` | the Hand's scale factor from distance | [V] SetDistanceFromView |
| `+0x4838` | `MaxDistanceFromUser` | [V] clamp in SetDistanceFromView |
| `+0x4850` | `GInterfaceStatus*` | [V] header |
| `+0x4854` | the Hand's distance from the camera, this frame | [V] SetDistanceFromView |
| `+0x4858` | 1 when the screen x changed this frame | [V] `0x0046C6E1` |
| `+0x485C`, `+0x4860` | the screen point this frame (ints) | [V] |
| `+0x4864`, `+0x4868` | the screen point last frame | [V] |
| `+0x486C`, `+0x4870` | a saved screen point | [V] |
| `+0x4874` | byte: use the saved point instead of the pointer | [V] |
| `+0x4878` | current hand state index (0 to 10). Written at `0x0046C755`, before the state's Update runs | [V] |
| `+0x487C` | the 11 `HandState*` in enum order | [V] |
| `+0x48B8`, `+0x48BC` | smoothed screen point | [V] |
| `+0x48C0`, `+0x48C4` | its velocity. A spring with `0.03^dt` decay, clamped to 80 px from the point | [V] |
| `+0x48C8..+0x48D0` | **throw velocity**, world units per second | [V] section 3 |
| `+0x48D4..+0x48DC` | a point on the held object, in its local space | [V] used by the release, meaning [I] |
| `+0x48F8`, `+0x48FC` | a timer and the last thrown object | [V] |
| `+0x4904` | the held `Object*` | [V] ThrowObject, GetRequiredState |
| `+0x4908` | nonzero: tug state | [V] GetRequiredState |
| `+0x490C` | hold mode (switch in HandStateHolding::Update) | [V] |
| `+0x491C` | byte: play an animation state | [V] |
| `+0x492C..+0x4934` | held object target point | [V] |
| `+0x4944` | held object hang height | [V] |
| `+0x49AC`, `+0x49B0` | the old pose and the 0.13 s blend timer | [V] fn_0046C500 |
| `+0x49B4`, `+0x49B8` | the Creature, and its `LH3DCreature` | [V] |

Globals written with the Hand [V]:

| Address | Meaning |
| --- | --- |
| `0x00C5E680` | the Hand's world position (written by the pose builder only) |
| `0x00C5E670` | the position before the last state change |
| `0x00C5E660` | the drawn position (blend of the two) |
| `0x00C5E640`, `0x00C5E650` | the land point under the pointer, now and last drop |
| `0x00C5E698` | byte: the Hand is hidden because the pointer left the view |
| `0x00D13F20..28` | the offset from the Hand to the held object |

### The state machine [V]

`CHand::GetRequiredState` `0x0046CD10` (thiscall, no arguments) returns the
state index from the interface. It reads `GInterface+0x3AC`, the interface
hand mode, and the held object at `+0x4904`:

| Condition | State |
| --- | --- |
| `GGame+0x205A28 == 1` (drawing from another camera, the Temple) | 10 `Citadel` |
| holding, and hand mode `0x19` | 0 `Invisible` |
| holding, and the held object `IsSpellSeed` (vtable +0x4C4) | 8 `Grain`, the state of a miracle in the Hand [V, see "A miracle in the Hand"] |
| holding, and `+0x4908` set | 3 `Tug` |
| holding otherwise | 4 `Holding` |
| empty, and byte `+0x491C` set | 9 `PlayAnim` |
| empty, hand mode 1, 2 or `0xC` | 0 `Invisible` |
| empty, hand mode `0x19` (the story scenes) | 0 `Invisible`, seen in the M3 desktop runs [V] |
| empty, hand mode `0xE` | 5 `Totem` |
| empty, hand mode `0x13` | 7 `Creature` if `+0x49B8` is set, else 1 |
| empty otherwise | 2 `Camera` if `CameraModeNew3+0x8C` bit 0, else 1 `Normal` |

`fn_0046CF80` filters the result. On the way out of the Creature state it
sends packet `0x59` with the stroke and slap total (section 4).
`GetRequiredState` never returns 6 (`MultiPickUp`), and no other code sets
that state: see "Many trees from a forest".

### ObtainRequiredHandPosition `0x005B5E70` [V]

`__cdecl`, nine arguments: `(CHand*, float dt, float, float& out,
LHPoint& position, LHPoint& up, int& hint, bool, bool)`. The callers are
`HandStateNormal::Update` (`0x005B71CB`, position = `hand+0x78`) and
`HandStateHolding::Update` (`0x005B3FA6`).

1. **Target distance.** `fn_0046DF60` casts the pointer ray at the land
   (`fn_00800C30`) and takes the camera distance to that point. With no land
   it uses `hand+0x78`. It subtracts the Hand's half size
   (`3.2 * +0x4834 * +0x90`) and clamps to 2 and `+0x4838`.
2. **Object under the pointer.** It reads `GInterface+0x3C8` (section 2).
   For an object it intersects the ray with the object's mesh
   (`fn_00865020`) so the Hand rests on the surface.
3. **Smoothing.** A Zoomer glides the distance. The glide times are
   0.28 s, 2.0 s and 0.1 s (`0x00BF01D0`, `0x00BF01D4`, `0x00BF01D8`). A
   cubic path in `0x00D13F80..0x00D13FAC` also runs.
4. **Final position.** `SetDistanceFromView(min(glide, target))`, then
   `ScreenPointToWorldAtDistance(+0x4854)` writes the position (the call at
   `0x005B718F`: ecx = `hand+0x485C`, edx = `position`) [V]. Between the
   two, the function asks `LH3DIsland::GetAltitude` `0x00803090` for the
   land under the old position and compares it with half of
   `3.2 * +0x4834`. With a low Hand it calls `fn_004427B0` and
   `fn_00803630`. The decompile shows no write to the position after this
   test, so the land clearance comes from step 1, which ends the ray half
   the Hand's size before the land [I, a correction of the first pass].
5. **Up vector and hint.** Three Zoomers (0.4 s) glide the up vector. The
   hint code goes to `*hint` (`0x0C`, `0x18`, `0x2D`, `0x30`).

### The primitives [V]

- **`CHand::SetDistanceFromView` `0x0046C0D0`**: thiscall, one float,
  `ret 4`. It clamps the distance to 2 and `+0x4838` and stores it at
  `+0x4854`. Then it sets the Hand scale `+0x4834`: 1.0 up to 10 units,
  `(d/10)^0.8` above that, and a further `(d/150)(1 - 0.3(d-150)/1650)`
  above 150 units. So the game grows the Hand with distance to keep its size
  on the screen. Twelve call sites, in every state that places the Hand.
- **`ScreenPointToWorldAtDistance` `0x0074CAF0`**: `__fastcall`, ecx = the
  screen point (`int[2]`), edx = the output `LHPoint*`, the distance on the
  stack, `ret 4`. It returns `camera + normalize(Unproject(point) - camera)
  * distance`. The camera is `0x00EA1DB8`. Eighteen call sites.
- **`fn_0046C040`** returns 3.2, the Hand's length at size 1 [V]. The
  exponent of the scale curve is the double at `0x008CBF28`, 0.8 [V].
- **`LH3DIsland::GetAltitude` `0x00803090`**: `__fastcall`, ecx = an
  `LH3DMapCoords` of three ints `{x * 6553.6, z * 6553.6, 0}`, the height in
  st0, a plain `ret` [V, `0x005B6D73` to `0x005B6DB9` builds the argument
  with the constants 65536 and 0.1].
- **The yaw.** `PrepareForDrawing` calls `fn_0074CBD0` at `0x0046C6F0` with
  ecx = `hand+0x485C`. That function calls `UnprojectScreenPoint` too, so a
  hook there turns the yaw with the ray [V].
- **The pose builder `fn_0046E160`**: thiscall, `(LHMatrix* out, const
  LHPoint* position, const LHPoint* up)`, `ret 0xC`. It writes the
  position to `0x00C5E680`, builds a basis from the yaw `+0x84 + pi` and the
  up vector, and puts the position in the translation. Callers: the Camera,
  Citadel, Normal, PlayAnim, Tug and Holding states. The Creature and
  MultiPickUp states do not call it.

### Who else reads the Hand's position

- `hand+0x78`: `fn_0046DF60` next frame, the power up effect in
  `GInterface::InterfaceActionHandObjectApplyToPos` `0x00671DA0`, the far
  click fly in `fn_005D1AB0`, and the network packets below. [V]
- `0x00C5E660`: the held object's position in `PrepareForDrawing`, unless the
  release physics owns it (`0x00D47824`, `0x00D47828`). [V]
- **The network copy.** `GInterface::Process` `0x005CEC10` calls
  `fn_005D2250`. It sends packet `0x15` or `0x16` with the land point under
  the pointer (`GInterface+0x3F0`), `hand+0x78`, the camera and its focus,
  but only when they change. `GPacket::ProcessPacket` `0x0063C420` stores
  them in the player's `GInterfaceStatus` through `0x005DC060`: the Hand at
  `+0xA4` and the land point at `+0x14`. [V]
- **The per turn hand velocity.** `GInterfaceStatus::Process` `0x005DC4E0`
  calls `fn_005DBC60` once a game turn. It sets `+0xE8` (the Hand's move this
  turn), `+0x10C` `HandVelocity` (units per second, smoothed by 0.6 a turn),
  `+0x100` the speed, `+0x104` the heading, `+0x108` the turn rate, and
  `+0xA0` a count of still turns. [V] Its readers were not traced.

### The single place to override

The game builds the Hand from two inputs, a screen ray and a distance. Then
one function commits the pose.

- **Best single hook: the pose builder `fn_0046E160`.** It is the one place
  where six states commit the final world position and rotation. A hook
  there can write the controller point through its `position` argument (for
  Normal that argument is `hand+0x78` itself), and replace the 3x3 of the
  output matrix with the controller rotation. It also writes `0x00C5E680`,
  so the held object and the drawing follow. [V for the callers and the
  argument, I for the write through in the Holding call]
- **Consistency layer: the ray and the distance.** The game's own logic
  (the pick, the drop test, the hint, the held object target) runs before
  the pose builder, from the ray and the distance. Feed both, so the logic
  sees the same point:
  1. The Hand's screen point is the projection of the controller point
     through the render camera.
  2. The mod's `h_Unproject` hook on `0x0081B370` (pointer_map.cpp, "the
     exact Hand ray") returns the exact direction to the controller point.
  3. A post hook on `SetDistanceFromView` writes `+0x4854 =
     |controller - camera|` and a fixed VR scale to `+0x4834`.

  Normal and Holding end with `ScreenPointToWorldAtDistance(+0x4854)`, so
  these three inputs alone put the Hand on the controller point. [V for the
  final call, I for the result without the pose hook]

## 2. What the Hand points at

### A render pick, one frame late [V]

1. `fn_00809E50` starts a scene render. It copies the pointer to the LH3D
   pick point `0x00EA1AC8` / `0x00EA1ACC` (`0x00809E73`, `0x00809EAB`).
2. While objects draw, each one calls `GInterface::SendObjectDrawCollision`
   `0x005D56C0` (thiscall, `(Object*, float distance, LH3DObject*)`,
   `ret 0xC`). Callers: `Game3DObject::AddForDrawing`,
   `Game3DObject::AddJustForCollide`, `Abode::Draw`,
   `MultiMapFixed::DrawBuilding`, `CitadelHeart::DrawNow`, `Creature::Draw`,
   `Reward::ProcessDrawing`, `ChessPion::Draw` and
   `SendInvisibleDrawCollision`.
3. It skips objects that fail `IsInteractable` (vtable +0x190) or have flag
   `+0x24` bit 2. With a zero distance it asks the mesh whether the pick
   point hits it (`LH3DObject` vtable +0x1E0 or +0x1E4). It keeps the
   nearest hit through `fn_005D5E40` (thiscall on the collide record,
   `(Object*, float)`, `ret 8`).
4. `SendInvisibleDrawCollision` `0x00519960` handles objects without a mesh.
   It projects the object's point and tests a screen circle of the object's
   radius around the pick point.
5. Next frame, `GInterface::PreDrawProcess` `0x005CE9E0` calls
   `UpdateHandRenderCollide` `0x005D0610`. That copies the "next" record into
   the "current" record. Then it clears the next record and asks the land
   under `GInterface+0x428` (`fn_005D5980` -> `fn_005E5740` ->
   `fn_005E5620`), with 2.3 added to the distance.
6. `UpdateInterfaceCollide` `0x005D5A70` chooses between the object, a
   bubble and a leash. The leash test is `fn_00848F90(pointer, 2.0)`, a
   screen distance test [I].

The pick is a screen space test at the pointer pixel. It is not a 3D search.

### The records in GInterface [V]

The decomp header puts the second record at `+0x3E0`. The code shows it
starts at `+0x3E8`. `+0x3E0` is the time over the same object and `+0x3E4`
that object.

| Field | Current (this frame) | Next (filled by the draw) |
| --- | --- | --- |
| land point (`MapCoords`) | `+0x3B8` | `+0x3F0` |
| land distance | `+0x3C4` | `+0x3FC` |
| **object** | **`+0x3C8`** | `+0x400` |
| object distance | `+0x3CC` | `+0x404` |
| bubble, distance | `+0x3D0`, `+0x3D4` | `+0x408`, `+0x40C` |
| leash, distance | `+0x3D8`, `+0x3DC` | `+0x410`, `+0x414` |

Readers: `ObtainRequiredHandPosition` (`+0x3C8`, `+0x3D0`, `+0x3D8`),
`HandStateCreature::Enter` (`+0x3C8`), `GInterface::GetInteractObject`
`0x005CED10` (`+0x400`), and the interface actions. The pointer the
interface asked about is at `+0x428`.

### Replacing it with "the nearest holdable object to a 3D point"

- **Candidates.** Hook `SendObjectDrawCollision`. The draw offers it every
  interactable object it draws [I]. (Not so, found for the table on
  2026-09-30: the draw offers only an object whose circle on the screen holds
  the pick pixel. See `tabletop.md`, 10.6.) Record each `Object*` in the first eye
  pass only. The object's matrix is at `Object+0x14` (a 3x4, translation at
  `+0x38`) [V from `ThrowObjectFromHand`].
- **Filter.** Use the tests of the pick up path in
  `GInterface::PlaceObjectInMagicHand` `0x005DA6F0` [V]: `IsInteractable`
  (+0x190), `GInterfaceStatus::IsSpaceInHands`, the byte `+0x24` bits
  `0x04` and `0x20` clear, the byte `+0x0A` bit `0x10` clear
  (`0x005DA766`), `ValidForPlaceInHand(status)` (+0x6FC) equal to 1 with
  the status at `GInterface+0x39C`, and `IsCannotBePickedUp` (+0x180) equal
  to 0.
- **The record function.** `fn_005D5E40` is thiscall on a record
  (`GInterface+0x3E8` for the next one), `(Object*, float)`, `ret 8`. When
  `IsInteractable` is 1 it writes the object at `+0x18` and the distance at
  `+0x1C`, else it writes 0 and `FLT_MAX`. The nearest test is in
  `SendObjectDrawCollision`, not here [V]. `SendObjectDrawCollision` skips
  everything while the byte `GInterface+0x38` is set [V].
- **Choice.** Take the nearest candidate to the controller point inside a
  grab radius. Use `Get2DRadius` (+0x64) to measure to the surface, not the
  center.
- **Injection.** At the end of the first eye's draw, call `fn_005D5E40` on
  the next record (`GInterface+0x3E8`) with the choice and its distance. The
  game then treats it as its own pick. For the same frame, a post hook on
  `UpdateHandRenderCollide` can also write `+0x3C8` and `+0x3CC`.

## 3. Pick up, hold, drop, throw

### Pick up [V]

The pick up is a network packet even in single player. `GPacket::ProcessPacket`
case `0x13` calls `GInterface::PlaceObjectInMagicHand(Object*)` `0x005DA6F0`.
For the local player it calls `fn_0046E590(object)`, which gives the object
to the CHand [I]. `fn_005DC330` then clears the gesture samples. The
interface action that sends the packet was not traced.

### Hold [V]

`HandStateHolding::Update` `0x005B3C70` calls `ObtainRequiredHandPosition`
for the target. It has two modes, chosen by the state field `+0x104`:

- **Direct** (`+0x104 == 0`): `hand+0x78` is the target. The target is
  the point of `ObtainRequiredHandPosition` (through the state's vtable
  slot `+0x14`), lifted by the hang height of the object, so the object
  hangs at the pointer and the Hand is above it [V]. With an object, the
  pose builder gets a copy of `hand+0x78` on the stack [V].
- **Throw spring** (`+0x104 != 0`): the interface hand mode `0x17` starts it
  and clears the spring velocity. Then the Hand follows the target in 10 ms
  steps: `v += ((target - pos) * 260 - 40 * v) * 0.01`, `|v|` capped at 124,
  `pos += v * 0.01`. The constants are `0x00BF0198` (260), `0x00BF019C` (40)
  and `0x00C371FC` (124). The spring velocity (state `+0x108..+0x110`) is
  copied to the throw velocity `hand+0x48C8..+0x48D0`. The unit is world
  units per second.

So the game's throw velocity is the velocity of a spring that chases the
pointer target. It is not a buffer of old positions.

### Release and throw [V]

1. The release action `FUN_005D4DB0` (thiscall on GInterface, no
   arguments, `ret`) runs when `GInterface+0x39` bit 6 is set. It is slot
   `+0x20` of interface action 11 in the action table at `0x00D17DB8` [V for
   the table write at `0x005D0C01`, I for the slot meaning].
2. It checks the land point under the pointer (`+0x3F0`) with
   `MapCoords::InBounds` and `ValidToApplyThisToMapCoord` (+0x724), and the
   player's influence there (`+0x48`). If one fails, the object stays in the
   Hand (see "The influence gate" below).
3. It builds a local physics object at `0x00D47088` from the held object
   and sets `0x00D47824` / `0x00D47828`.
4. It reads `hand+0x48C8..+0x48D0` (`0x005D4E46`, `add esi, 0x48C8`) and
   caps it at 124 units per second.
5. It marks a gentle drop when the object is not a tree
   (`IsAnyKindOfTree`, +0x478) and `|v|^2 <= 1`.
6. It sends packet `0x4D` with 48 bytes: the velocity, a release point
   (`hand+0x48D4` through the physics matrix), a translation and three
   angles (`fn_007FD140`, `fn_007FAB30`).
7. `GPacket::ProcessPacket` case `0x4D` copies the 48 bytes to
   `GInterfaceStatus+0x44..+0x73`.
8. The drop action `fn_005DA400` calls `ApplyThisToMapCoord` (+0x728). For a
   `Mobile` or `MobileStatic` that calls `ThrowObjectFromHand` (+0x758),
   `Object::ThrowObjectFromHand` `0x006385E0`.
9. `ThrowObjectFromHand` puts `status+0x5C` in the object's matrix,
   rotates it by the angles at `+0x68`, and scales it. Then it calls
   `InitialisePhysicsFromHand(status+0x44, status+0x50, status, 0, int)`
   (+0x780, `Object` `0x00636F00`).
10. `InitialisePhysicsFromHand` treats a horizontal speed squared above 4 (2
    units per second) as a throw. Slower objects are set down and raised
    until they do not intersect.

`CHand::ThrowObject` `0x0046DDD0` is not the physics throw. It only lets go
of the object on the CHand side (`+0x4904` to `+0x48FC`). `fn_005D1260` calls
it when the interface resets. `CHand::GetMaxForce` `0x0046BFB0` returns
`(this+0xAC + 1.0) * 600000`. No caller in the throw path was found.

The spell seed path `FUN_005D3340` sends the same packet from the same CHand
fields (`0x005D3340`, reads `+0x48C8`, `+0x48D4`, `+0x48E0`, `+0x48EC`).

**Verified again for M4** (`disasm.py 0x005D4E24 0x360`) [V]:

- The release action reads the three floats at `hand+0x48C8` (`esi` at
  `0x005D4E46`, read at `0x005D4F34` to `0x005D4F59`) and copies them
  unchanged, in world space, to `0x00D47164` (the first vector of packet
  `0x4D`). No rotation and no scale.
- The cap is on the length, not on each part. `0x005D4F75` compares `|v|^2`
  with `124^2` (the float at `0x00C371FC`). Above it, `0x005D4FF6` to
  `0x005D501C` divide by `|v|` and multiply by 124. So the direction stays.
- The gentle drop (`|v|^2 <= 1` and not a tree) is decided after the cut, from
  the length before it.
- A right press while the Hand holds an object gives hand mode `0x17` (the
  throw spring) 2 frames later. The release action runs 1 frame after the
  right button goes up [V, run m4d: down at frame 3731, `0x17` at 3733, up at
  3737, release at 3738, and the same at 4031 to 4038]. The first pass said
  "8 frames after the press". That was a press of 6 frames. So the mouse lets
  go of an object with a second click: the press starts the throw spring, and
  the release of the button lets go. See "The grip holds" for the controller.

**The influence gate** [V]. The release lets go only when the land point
`+0x3F0` of the release is inside the player's influence:

- The interface step `fn_005D1120` sets `GInterface+0x48` from
  `Influence::CalculatePlayerInfluence` (`0x005CD170`, cdecl, the land point,
  the player, then 0, 1, 1, the value in st0) at `+0x3F0`. Then it runs the
  step of the interface action `+0x44`. The table of steps is at `0x00D17DA8`,
  `0x30` bytes an action, and the step of action 12 is the release action.
- With `+0x48` clear, the release action goes to `0x005D5150`: it turns the
  throw spring off and sends no packet `0x4D`. The drop sender `FUN_005D3340`
  tests `+0x48` again and calls `fn_005D18F0`, which refuses the drop with a
  sound and packet `0x2B`. No packet `0x12` goes out, so the drop action
  `fn_005DA400` does not run and the object stays in the Hand.
- The drop sender also needs `+0x444` clear. It is the game turn of a drop
  that waits for its packet, and `fn_005D2250` clears it once the drop ran.

With the mouse this is the game's rule: no drop and no throw outside the
influence. At a large world scale it stopped every throw of the direct hand
that ended outside, because a swing carries the Hand far over the land. See
[tabletop.md](tabletop.md), section 10.11, for the cause in runs th5 and th6
and the fix: a throw that ends outside goes from the last point of the swing
inside the influence.

### Feeding a controller velocity

Pre hook the release action `FUN_005D4DB0`. Write the controller's linear
velocity to `hand+0x48C8..+0x48D0`, in world units per second:

- OpenXR gives metres per second in the room. Multiply by 7.5, and turn it
  into the world with the room and head transform that the camera hook
  already uses.
- Use the mean over the last 50 to 100 ms before the release, not one
  sample. A release makes the pose noisy. With the grip that holds, the
  "release" is the moment the grip opens, not the release action that the
  game runs some frames later (see "The grip holds").
- The 124 units per second cap (`0x00C371FC`, a `.data` float) is 16.5 m/s
  at this scale. A player's real throw is slower than a flick of a mouse on a
  far camera [I], so a throw gain setting is likely needed. Raise the cap only
  after a test.

## 4. The Creature

### Stroke and slap: `HandStateCreature::Update` `0x005B17B0` [V]

Thiscall `(float dt, LHMatrix*)`. The Creature's `LH3DCreature` is
`hand+0x49B8`. Let `S = 15 * LH3DCreature+0x90` (the Creature's size).

**Inputs it computes each frame:**

- **Hand speed.** It unprojects the current and the previous screen point
  (`+0x485C`, `+0x4864`) at the Hand's distance `+0x4854`. It divides the
  x and z difference by `dt`. The y difference is not used, so a downward
  slap has no speed.
- **Contact.** It casts the camera ray through the Hand's screen point
  against the Creature mesh (`fn_00865020`).
- **Height.** `h = (hand.y - land altitude) / S`, from `hand+0x7C`.
- **Direction.** The sign of the screen x change.

**Slap.** The slap branch runs when the ray hits and the state field
`+0x110` is 0, or when the screen x crossed the screen center this frame.
Then a slap needs all of these:

| Test | Value |
| --- | --- |
| height | `0 < h < 1.1`. Low below 0.4, middle below 0.7, high above |
| time in the state (`+0xDC`) | more than 1000 ms. `Update` only adds the frame time to it, and no reset was found in `Update` [V, was "time since the last slap"] |
| speed | more than `5 * S` per second |

A speed of `9 * S` or more counts a hard slap. After three hard slaps it
sends packet 99 with a strength from 0 to 7. Each slap forces a Creature
action, starts Immersion effect 1 or 2, sends packet `0x27`, and subtracts
0.1 from the total at `+0xD8` (0.2 when the total is above 0.25).

**Stroke.** Otherwise, while the ray hits, it finds the nearest of nine body
bones to the hit point. It adds the frame time to `+0xD4`. After 1000 ms, with
the screen point moving (`+0x4858`) and 2000 ms since the last stroke
animation (`+0xD0`), it plays that bone's stroke, sends packet `0x27`, and
adds 0.1 to `+0xD8`. Immersion effect 0 plays while the ray touches.

**What else the state does** [V, from `disasm.py 0x005B17B0`, added for M5]:

- It calls `CHand::SetDistanceFromView` twice (`0x005B196C` and `0x005B2044`)
  with a distance from a Zoomer in the state (`+0x9C`) that follows the Creature's body,
  and then multiplies the Hand's scale `+0x4834` by `size^0.5` (`0x005B1975`).
  So the Creature state does size and place the Hand from the Creature.
- It reads `dt` (seconds, the first stack argument) at `0x005B1B79` and
  divides the x and z difference of the two unprojected points by it. The
  y difference is not read. The points are `ScreenPointToWorldAtDistance` of
  `+0x485C` and `+0x4864` at `+0x4854` (`0x005B1B32`, `0x005B1B4E`).
- Speed above `5 * S` also sets the Hand animation `0x21` (`+0x114`).
- A slap sets `+0xCC` to 0 and `+0xD0` to 1000 (`0x005B1E25`, `0x005B1E2F`),
  and a stroke needs `+0xD4` above 1000 ms, `+0x4858` set (the pointer moved
  this frame), `+0xD0` above 2000 ms and `+0xCC` above `(1 - total) * 1250` ms
  (`0x005B2154` to `0x005B2193`).
- `CHand::PrepareForDrawing` copies `+0x485C` to `+0x4864` and then reads the
  pointer (`0x0046C6A7`), and it runs once per eye pass. So in the second eye
  pass the previous point is this frame's point, the speed is zero and
  `+0x4858` is 0. A stroke or a slap can only start in the first eye pass,
  with the mouse and with the mod.

**How the state starts** [V for the code, I for what the player does]. The
hand mode comes from a table indexed by the interface style (`GInterface+0x44`,
table at `0x00D18278`, built at `0x005D7960`). Style 0 uses `fn_005D7F20`: mode
9 for an object the Hand can hold, `0x12` for an interactable object that it
cannot hold (`0x005D8070`), 3 for nothing. Style 4 uses `fn_005D80C0`: for an
object whose attribute block (`Object+0x28`) has `1` at `+0x10` it gives
`0x13` (the Creature state), or `0xE` (the Totem) when the flag at
`attribute+0x10AC` is set. Style 4 is set by an interface action at
`0x005D4960` (press on an object with bit `0x10` of the byte at `Object+0x24`).
So the Creature state needs a press on the Creature, not only a hover.

**The lesson.** `+0xD8` is clamped to -1 and 1. When the Hand leaves the
Creature state, `fn_0046CF80` sends packet `0x59` with it [V]. This is the
reward or punishment the Creature learns from [I].

### The leash [V for the calls, I for the rest]

`GInterface` keeps three `Leash*` at `+0x198..+0x1A0`. The leash under the
pointer comes from the screen test in `UpdateInterfaceCollide`. The hand
mode `0x13` puts the empty Hand in the Creature state when `+0x49B8` is set.
`GInterface::DoLeashSelectionSystem` `0x005D01F0` and the Tug state (3, set
by `hand+0x4908`) handle the pull. The Tug state places the Hand without
`SetDistanceFromView`, so it will need its own hook.

The leash status, the packets of the leash, the click that sends the
Creature and the shake were read on 2026-10-03: see "The Creature commands".
`DoLeashSelectionSystem` is the selection of the kind of the leash by a
gesture, not the pull.

## 5. Miracles and gestures [V unless marked]

This section was read again for M6 (Ghidra and `disasm.py`). The first pass
had the list and the path swapped, and it did not find the chain of gestures
that casts a miracle. The text below is the corrected one.

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

The first pass put the list at `+0x250068`. The constructor of `GGame`
(`0x0054B6D5` to `0x0054B772`) makes the list, the path (`0x65C` bytes), the
ring (`0xC98` bytes) and the result in that order, at `+0x250064` to
`+0x250070`.

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

### Feeding a 3D path (as built in M6)

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

## 6. Clicks on things in the world

Signposts, scrolls, the Temple and buildings go through the same render pick
[V for the draw callers: `Abode::Draw`, `MultiMapFixed::DrawBuilding`,
`CitadelHeart::DrawNow`, `Reward::ProcessDrawing`,
`Game3DObject::AddForDrawing`]. Signposts and scrolls are `Game3DObject`s
[I]. The interface actions read the same records (`+0x3C8`, `+0x400`).
`GInterface::CalculateOverObject` `0x005C9FC0` picks the tooltip from them.
Inside the Temple the game draws from another camera (`GGame+0x205A28`), and
the Hand runs the Citadel state, which calls `SetDistanceFromView` and
`ScreenPointToWorldAtDistance` itself [V].

So a direct Hand that points the render pick through the controller point
keeps every click working. A 3D nearest search (section 2) should apply to
holdable objects only. Buildings, signposts and scrolls should keep the ray.

Since 2026-10-03 the nearest search also takes each object that a press taps,
a scroll and a house too, and the game's pick at the pixel stays for an
object that is not near the Hand's point. The button of a tap is the Action
button, the grip. See "One point for each interaction".

## 7. Design

The steps build on M1 (OpenXR input and a fake controller) and M2 (the
controller ray as the pointer). The mouse must keep working at every step.
Each hook latches its state once a frame, because the game calls some of
these functions once per eye.

### Common test method

The M1 fake controller gives still poses. A hotkey steps it to the next pose.
No test moves the Hand by itself. A speed test uses two still poses one
frame apart, which is a step with a known speed. Each test reads numbers
from memory and logs them, like the Hand anchor test in
[backend.md](backend.md). The game boots to play with `-NEWGAME`.

**The start dialog.** With `-NEWGAME` the game first shows a dialog: "Please
choose from one of the following options", with four check boxes (start
the game normally, skip straight to the Creature select, skip all of the
Creature tutorial, keep your old Creature) and OK. Until it closes, the
interface hand mode is `0xC` and the Hand is Invisible, so no Hand test can
run. Earlier desktop runs waited in this dialog. The fake controller can
click it: with the fake head straight ahead and a 2560 x 1440 render at 110
degrees, the pixel of a point `r, u, f` in cm from the eye is
`x = (1 + 0.7003 r / f) * 1280` and `y = (1 - 1.2449 u / f) * 720`. The
pixel of the direct hand is the pixel of its reach point. That point is on
the line from the reach anchor (20 cm below the head center) through the
hand point, so a far reach points along `(r, u + 20, f)` of the hand point.
The points `-15,-18,94` (the second check box), `-16,-31,94` (OK) and then
`0,-20,41` with the A button (the ape) take the game to the Creature select
and then to play in the pen, about 70 s after the world first shows. The
trigger makes no double click with the direct hand, so the ape needs the A
button. Run m3fix2 used the script `point@400+1 trigger@440+8 point@520+1
trigger@560+8 point@3400+1 a@3440+4`: the dialog closed at frame 572, and
the A button sent the double click on the ape at frame 3443. Before the
reach anchor, the points were `-16,2,100`, `-17,-12,100` and `0,-7,40` with
a trigger double click. "Start the game normally" gives play only after
about 210 s, and in that run no holdable object came near the Hand.

### M3: direct hand

**Hooks.**

1. The pointer map: for the Hand, the pointer is the projection of the
   controller point through the render camera (the head). The render pick
   and the land lookup then see what is under the Hand from the eye.
2. `h_Unproject` on `0x0081B370` (exists): the exact ray to the controller
   point, for the Hand's own point.
3. New: a post hook on `CHand::SetDistanceFromView` `0x0046C0D0` (thiscall,
   `ret 4`). Write `+0x4854 = |controller - camera|` and a fixed VR scale to
   `+0x4834`. The scale makes the Hand life size. Tune it by eye.
4. New: a hook on the pose builder `fn_0046E160` (thiscall, `ret 0xC`).
   Write the controller point through `position` and to `hand+0x78`. After
   the call, put the controller rotation in the 3x3 of `out`.
5. New: the pick of section 2 (`SendObjectDrawCollision` for candidates,
   `fn_005D5E40` to inject). Holdable objects only, inside a grab radius.
6. New: set `+0x48B8` / `+0x48BC` to the screen point each frame, so the
   mouse lean animation does not tilt a tracked Hand.

Start with the Normal and Holding states. Leave the Camera state (the land
drag) on the ray until M7.

**Desktop tests.**

- Step the fake pose through five points 2 to 6 units in front of the
  camera. Log `hand+0x78`, `0x00C5E680` and the target. They must agree to
  0.01 units.
- Step the fake pose onto a villager's position (from the candidate list).
  `+0x3C8` must be that villager next frame. Press the fake grip (the right
  button, which picks up). `hand+0x4904` must become it.
- Step the pose under the land surface. The Hand must stop on the land.

**Risks.**

- The game clamps the distance to 2 units (27 cm) and keeps the Hand above
  the land. A controller close to the face clamps.
- `+0x4834` also sizes the land clearance and the power up effect. A fixed
  scale changes both.
- The render pick runs in both eye passes and keeps the nearest hit. The
  second eye can pick an object only that eye sees [I]. Take the candidates
  from the first pass.
- The pick is one frame late. A fast hand grabs what was under it last frame.
- The Creature, Tug, Totem and MultiPickUp states place the Hand their own
  way and need their own checks.

### M3 as built

The code is in `src/mod/hand_direct.cpp`, with the grip point, the reach and
the pixel in `controllers.cpp` and the pure math in `controller_math.h`.

**When it acts.** `controller_hand=1` (the default) and the M2 owner rule
gives the controller the pointer this frame. `HandDirectFrame` latches the
target, the size and the CHand once a frame, before the eye passes. The
hooks only act in the Normal and Holding states, and only inside the two eye
passes. With the mouse as the owner, with `controller_hand=0` or with
`controllers=0`, no hook changes anything. With `controller_hand=0` the three
hooks are not installed.

**The target.** The grip pose goes into the room. The reach curve moves it
out along the line from the reach anchor. The anchor is a fixed point in the
room: the head center at the last recenter, `hand_reach_anchor_down_cm` (20)
lower, at about the chest. Then the anchor and the target go into the world
with the same step as the aim pose. The eye, the head matrix and the head
center come from one head sample (`EyeFromLean` in `controller_math.h`:
`E = game_position + L * lean`). So with full six degrees of freedom the
target is `game_position + L * reach_room`, and a head that moves with a
still hand moves neither the Hand nor a held object. Then the land test lifts
the target straight up to the land, from `GetAltitude`, once a frame, from
inside a Hand state. The first build lifted it to half the Hand's length
above the land. Now the point stops 5 mm of the room over the land (see "The
point touches the land").

The first build measured the reach from the render eye. The eye moves with
the head, and the render position of the frame drawn now has the lean of the
frame before. A still hand at 60 cm and a head sway of 1 cm moved the Hand
about 8 units in depth. `test/controller_math` now checks that a head move of
1 cm and of 5 cm, with the hand still, moves neither the target nor its reach
factor. In rotation only mode the Hand keeps its place to the eye, as the
aim ray does, so it moves with the head by the same distance. The reach
factor does not change.

**Tracking.** When the runtime reports the grip as not tracked (inferred, for
example a controller outside the headset's cameras), the target stays at the
last tracked grip point in the room. The owner rule does not count an
untracked pose as motion. The log tag `controllers` says each change of the
tracked state. Until the runtime has reported the grip as tracked once, a
valid grip counts as tracked, so a runtime that never sets the tracked bits
still moves the Hand.

**The hooks.**

| Hook | What it writes |
| --- | --- |
| `UnprojectScreenPoint` `0x0081B370` (the existing hook in `pointer_map.cpp`) | For the screen point `hand+0x485C` only, outside the camera code: the ray to the target. The origin and the length stay |
| `CHand::SetDistanceFromView` `0x0046C0D0`, post | `+0x4854` = the distance from the camera `0x00EA1DB8` to the target, past the game's clamp of 2 units. `+0x4834` = the Hand's size |
| The pose builder `fn_0046E160`, pre | The target to `*position` and to `hand+0x78`. The screen spring `+0x48B8..+0x48C4` to the screen point with no speed, so the mouse lean does not tilt the Hand |
| `GInterface::SendObjectDrawCollision` `0x005D56C0`, post | Nothing. It keeps each object of the first eye pass and its mesh translation (`LH3DObject+0x38`) |

**The pointer.** The pixel is the target seen from the eye
(`PointPixel`). So the render pick, the land lookup, the yaw and the gesture
path see the point where the Hand is. The Camera state (the land drag) keeps
the game's own placement of the Hand, on the ray through this pixel. The
camera code gets a different ray: the one from the game camera to the target
(`ControllerCameraRay`, used in `h_GCameraUpdate` and `h_Unproject`). The
target is fixed in the room, so a lean of the head does not turn this ray and
the land does not slide. The ray from the eye through the pixel turns with
every lean. The press ray and the drag rays all use it, so the grab point
does not jump at the press. The cost is a small parallax: the camera code
grabs the land on the game camera's ray through the target, and the game
draws the Hand on the eye's ray. The two rays meet at the target.

**The buttons.** The trigger and the grip send no double clicks. The aim
direction says nothing about where the Hand is, so a ratchet of short land
drags with a still wrist read as a double click, and a double click on the
land is a jump. The A button makes the jump.

**The pick.** After the first eye pass, `Choose` takes the nearest holdable
candidate within `0.6 * 3.2 * size` of the target. The gap is measured
across to the object's 2D radius (+0x64) and up and down to its base and a
top two radii above it. `Inject` calls `fn_005D5E40` on the next record with
the choice after each eye pass. With no choice, a holdable object in the
next record is taken out, because it is far behind the Hand on the ray.
Buildings, signposts and scrolls stay. The game copies the record to
`+0x3C8` the next frame, as it does with its own pick. The pick only runs
while the Hand is empty.

Since 2026-10-03 the reach is `hand_pick_cm` (4 cm of the room), the body of
an object has its real height, and an object that a press taps is a candidate
too. See "One point for each interaction".

**The reach.** `hand_reach_start_cm` (35) and `hand_reach_gain` (215 per
meter): `D = d + gain * (d - start)^2` past the start, with d the distance
from the reach anchor in meters. A hand 65 cm from the anchor reaches 20 m,
which is 150 units. The reach stops at the CHand far limit `+0x4838`, the
upper clamp of `SetDistanceFromView` (`0x0046C0E4`). The logs read 1800
units there. The limit never pulls the target nearer than the grip, and the
depth hook writes no more than the limit to `+0x4854`.

**The size.** `hand_scale_pct` (50) of the game's size 1. The game's Hand at
size 1 is 3.2 units long (`fn_0046C040`), 43 cm at 7.5 units per meter. A
hand from the wrist to the finger tips is about 19 to 21 cm, so 50% gives 21
cm. Past the reach start the size grows with the reach factor, so a far Hand
keeps the size on the screen of the real hand at the real distance. The land
clearance and the grab radius grow with it. The growth stops at a factor of
80, which a real arm does not reach: a hand 80 cm from the anchor gives about
55. Without the stop, a controller 1.2 m away (for example on the desk) gave
a Hand of size 65 and a grab radius of 17 m.

**Not done.** The controller's rotation is not put in the pose. The pose
builder takes a yaw and an up vector, and the axes of the Hand mesh need a
look in the headset first. The Tug, Grain, Creature, Totem and PlayAnim
states place the Hand as the game does. `CHand::PrepareForDrawing` runs in
both eye passes (`skip_hand_prepare=0`), so the hooks run twice a frame and
write the same values twice.

**Desktop results** (runs m3a to m3h, fake head straight ahead, render 2560
x 1440, before the review fixes of 2026-09-30, so the reach was measured from
the eye):

- Placement: in every settled check of the Normal and Holding states the
  target, `hand+0x78` and `0x00C5E680` agree to 0.0000 units. The checks
  cover a near point one to one (depth 2.02, size 0.50), mid points (depth
  8.1 to 48, reach factor 2.7 to 12.3), far points (depth 84 to 731) and
  points under the land ("held up by the land").
- Hooks per frame in the Normal state: 4 depth writes, 2 pose writes and 8
  rays, because `PrepareForDrawing` runs in both eye passes. In the
  Invisible state (the story scenes) no hook writes anything.
- The pick: the choice is in `+0x3C8` one frame later. Up to about 700 far
  holdable picks a report window are taken out of the next record.
- The pick up is the RIGHT button, the grip in the M2 mapping. A left press
  on a holdable object (hand mode 9) starts a land drag (mode `0x14`, state
  2). A grip press on the chosen object gives the Tug state (3), then
  `+0x4904` = the object and the Holding state (4), 10 frames later
  (`the Hand holds 0F56C1D4 (+0x4904), frame 7931`). A second grip press
  gives hand mode `0x17` (the throw spring) and lets go.
- The Camera state (a left press on the land) starts and ends as before.

**Desktop results after the review fixes** (runs m3fix and m3fix2, the same
rig):

- The default fake pose (20, -30, 35 cm) is 41.5 cm from the anchor, with a
  reach factor of 3.21, as the math gives.
- The target, `hand+0x78` and `0x00C5E680` still agree to 0.0000 units, at
  depth 9.0 (factor 2.89) and at depth 605 (factor 84.3). At the far point
  the size stops at 40.000, which is 50% times 80.
- The camera code gets the ray from the game camera to the target on every
  frame while the controller owns the pointer. The press of the A button
  jump started a land drag with that ray ("1 used the exact ray").
- Not reached in 110 s: the points in the pen, `seek` and the grip pick up.

**Test aids.** `fake_controller_points` is a list of `right,up,forward` in
cm. The script word `point@frame+frames` steps the hand to the next point,
and after the last point the list starts again.
The word `seek@frame+frames` steps it once onto the nearest holdable object
of the last frame, through the reach curve backward from the reach anchor
(`ReachInverse`). The
log tag `hand` prints the target, `hand+0x78` and `0x00C5E680` 20 frames
after each step. The tag `hand pick` prints the choice, `+0x3C8` and
`+0x4904`.

### M4: throwing with the real hand

**Hooks.**

1. A pre hook on the release action `FUN_005D4DB0`: write the controller
   velocity (section 3) to `hand+0x48C8..+0x48D0`.
2. A hook on `GInterface::StartImmersion` `0x005D8A10`: route the effect
   index to controller haptics. Seen in this pass: 0 (touch the Creature), 1
   and 2 (soft and hard slap), 3 (gesture success), 8 and 9 (power up) [V
   for the calls, I for the names].

**Desktop tests.**

- Grab a villager with the fake grip. Set a fake velocity of 20 units per
  second forward. Release. Log `hand+0x48C8` at the release and
  `GInterfaceStatus+0x44` after packet `0x4D`. They must match.
- Repeat at 1 unit per second. The villager must be set down, not thrown.
- Log the landing point for three speeds. The distance must grow with the
  speed.

**Risks.**

- The throw is a packet. It lands one packet cycle later [I].
- The spring mode only runs in hand mode `0x17`. The override does not need
  it, but the held object hangs differently in each mode.
- Trees are planted, not thrown.
- A real arm is slower than a mouse flick. A gain is likely, and the 124
  cap may need a change.

### M5: the Creature

**Hooks.**

The game's speed test reads the current and the previous screen point at the
current distance. With M3 in place both points come from the controller, so
the test works if the depth does not change much [I]. Two changes make it
exact:

1. A pre hook on `HandStateCreature::Update` `0x005B17B0` (thiscall,
   `ret 8`): set `+0x4864` / `+0x4868` to the projection of the last frame's
   controller point.
2. A gain on the Creature speed. With `S = 15 * size`, a slap needs `5 * S`
   units per second. Scale the previous point away from the current one to
   apply the gain.

The slap ignores vertical speed. A downward slap will not count. A mod side
test on the 3D speed can force the slap branch if the player wants that.

**Desktop tests.**

- Step the fake Hand onto the Creature's head and hold it still. Immersion
  0 must start. Step it across the body in 20 small steps, one a frame. After
  1 s a stroke must play and `+0xD8` must go up by 0.1.
- Two poses one frame apart at `6 * S` units per second at shoulder height:
  one slap, `+0xD8` down by 0.1, Immersion 1.
- Leave the Creature state and count packet `0x59`.

**Risks.**

- `S` depends on the Creature's size, which grows through the game. The gain
  must follow it.
- The slap also fires when the screen x crosses the screen center. Head
  motion moves the Hand's screen point, so a head turn during contact can
  slap [I]. Test this in the headset.
- The leash still uses a screen test. Keep it on the M2 ray.

### M4 as built

The code is in `src/mod/hand_direct.cpp` (the throw) and `controllers.cpp`
(the haptics), and the pure math is in `controller_math.h`
(`MeanVelocity`, `ThrowVelocity`).

**The history.** `HandDirectFrame` keeps up to 64 samples of the reach point
and of the grip, in the room, with a `QueryPerformanceCounter` time. It keeps
them only while the controller places the Hand. A gap of more than 250 ms
clears them. Room space makes a snap turn or a camera fly during a swing
harmless.

**The throw.** A pre hook on the release action `FUN_005D4DB0` runs only when
the controller placed the Hand on the last frame, bit 6 of `GInterface+0x39`
is set and the Hand holds an object. Then it writes `hand+0x48C8..+0x48D0`:

- The direction is the mean velocity of the reach point over the last 80 ms,
  turned into the world by the room basis L.
- The speed is the real speed of the grip over the same 80 ms, in units per
  second (7.5 per meter), times `throw_gain_pct`. The reach curve is not in
  it. Then the mod cuts the length to the game's cap (it reads the float at
  `0x00C371FC`, 124) and keeps the direction. The game makes the same cut, so
  the mod can log what it sent.
- With no motion (fewer than two samples, or a newest sample older than
  250 ms) it writes zero, and the game sets the object down.
- The mouse path never passes through the hook.

**Why the speed is the grip's.** The first build sent the Hand's own velocity.
The Go-Go reach multiplies it by 8 to 40, so every throw in the desktop runs
was cut to 124 and only the direction was left. Examples from run m4c: a real
hand speed of 1.83 m/s gave a Hand speed of 167 units/s, 4.37 m/s gave 372
and 5.38 m/s at a reach of x38.6 gave 2594. The real speed does not change
with the reach, so the throw scales with the arm.

**The default gain, 150.** A real arm is slower than a mouse flick. The cap is
124 units/s, which is 16.5 m/s at 7.5 units per meter. A hard overhand throw
with a controller is about 6 to 12 m/s. With a gain of 150, 8 m/s gives 90
units/s and 11 m/s reaches the cap. A lob at 3 m/s gives 34. The value needs
a headset test.

**The timing.** The release action runs 1 frame after the right button goes
up, not 8 frames after the press (see section 3). In the first headset test
(2026-10-02) the grip took with one click and let go with a second click, so
a throw did not work well. Now the grip lets go when it opens, and the
velocity is latched at the opening. See "The grip holds".

**The log.** Each throw logs one line with the tag `hand throw`:

```
hand throw   : release at frame 4038: the real hand moved at 1.80 m/s (13.5 units/s), the Hand at 161.6 units/s (reach x11.5). Gain 150%. Sent 15.9 -1.6 -12.4, 20.3 units/s. The game caps at 124. Holding 0F7219FC
```

After 40, 100 and 200 frames a second line gives the distance of the object
from the Hand at the release, from the draw's candidates. Thrown objects often
leave the view, so it often says "not drawn".

**The haptics.** `GInterface::StartImmersion` `0x005D8A10` is thiscall
`(int effect, unsigned int)`, `ret 8`. The hook sits at the entry, calls the
original first, and then sends one pulse to the pointing hand when
`controller_haptics` is on and the controller owns the pointer. Each effect
has its own strength and length and a least gap between pulses:

| Effect | Name | Pulse |
| --- | --- | --- |
| 0 | touch the Creature | 0.15, 30 ms, gap 250 ms |
| 1 | soft slap | 0.60, 60 ms |
| 2 | hard slap | 1.00, 100 ms |
| 3 | gesture success | 0.70, 120 ms |
| 4 | a pick up starts | 0.35, 50 ms |
| 5 | the object is in the Hand | 0.60, 80 ms |
| 6 | the Hand is over an object | 0.15, 20 ms, gap 200 ms |
| 8, 9 | power up | 0.50, 150 ms, gap 150 ms |
| 44 | a menu click | 0.30, 40 ms |
| 46 | the release | 0.40, 70 ms |
| the rest | not named yet | 0.30, 40 ms |

The game calls `StartImmersion` with no Immersion device present, so the
entry hook sees every effect. Every call in the desktop runs had the
argument `0x80000000`. The first start of each index is logged with the tag
`haptics`. Indices 4, 5, 6, 44 and 46 were named from when they started, not
from the callers: 4 came 2 frames after a grip press on an object, 5 one frame
before the Holding state, 46 5 frames after the release, 6 when the Hand
came over an object, and 44 after a click on the start dialog. Effects 0 to
3 and 8 to 9 are from the callers (section 7, M4). The Creature state and
the spell code did not run in the desktop tests, so those pulses are not
seen in a run.

**Desktop results** (runs m4a to m4f, fake head straight ahead, 60 fps):

- The hooks install: `the release action (the throw)`, `HandStateCreature::
  Update` and `GInterface::StartImmersion` all say ok.
- The recipe of "M3 as built" reaches the pen. The `seek` word puts the Hand
  on a holdable object, and a grip press of 20 frames picks it up (a press of
  6 frames does not: the Tug state ends before the Holding state, which came
  8 to 10 frames after the press). The second grip press starts the release.
- Three throws with the final math (run m4d), one step of the fake hand
  before each release: a step of 2 cm gave 0.30 m/s and sent 3.4 units/s. A
  step of 12 cm gave 1.80 m/s and sent 20.3 units/s, twice, in opposite
  directions (15.9 -1.6 -12.4 and -18.4 1.9 8.3). The speed is `1.8 m/s * 7.5
  * 1.5 = 20.3`. The direction follows the step.
- With no step in the last 80 ms the line says 0.00 m/s and sends zero (runs
  m4b, 3 throws). The object was set down.
- Not proved: that the distance grows with the speed. The tracked objects were
  out of view or walking, so the distances (7.8 units at 3.4 units/s after 100
  frames, 15.5 at 20.3 after 200) prove nothing. Also not read: the packet
  `0x4D` copy in `GInterfaceStatus+0x44`. The value sent is what the hook
  wrote to `+0x48C8`, and the release reads it from there [V, disassembly].

Needs the headset: the feel of the gain and the window, and whether the
release 8 frames after the press feels late.

### M5 as built

The code is in `hand_direct.cpp`. The section "The Creature by touch"
(2026-10-02) at the end of this file replaces the speed gain and the depth
rule below.

**Hooks.**

| Hook | What it does |
| --- | --- |
| `HandStateCreature::Update` `0x005B17B0`, pre and post | Pre: in the first eye pass only, works out the previous point. Post: reads the total at `state+0xD8`. A fall of more than 0.05 is a slap, a rise of more than 0.05 is a stroke. Logs it with the tag `hand creature` |
| `UnprojectScreenPoint` hook (`HandDirectUnproject`) | For `+0x485C` in the Normal, Holding and Creature states: the exact ray to the target. In the Creature Update, for `+0x4864` in the first eye pass: the ray to the previous point |
| `CHand::SetDistanceFromView`, pose builder | Not changed in the Creature state. The state sizes and places the Hand from the Creature |

**The previous point.** The game unprojects the current and the previous
screen point at the same depth and divides by `dt`. A previous pixel from the
last frame would move with a head turn and would carry the rounding of a
pixel. So the mod does not use it. It takes the mean velocity of the reach
point over the last 50 ms (`v`, in the world) and the game's `dt` (the first
argument of `Update`), and aims the ray of `+0x4864` at
`target - v * gain * dt`. The speed that the game sees is then `|v| * gain`
(only the part across the view, as with the mouse). A still hand gives a
previous point equal to the current one and a speed of exactly zero. The
second eye pass keeps the game's own previous point, so it sees speed zero,
as with the mouse. A stroke or a slap starts once a frame.

**The gain, `creature_speed_gain_pct`, default 100.** A slap needs `5 * S`
units per second and a hard slap `9 * S`, with `S = 15 * size`. The game
measures the speed at the Creature's distance, so it is the angular speed of
the Hand seen from the eye times that distance. The Hand's screen point is
the reach point seen from the eye. For a grip 40 to 50 cm from the anchor, a
hand moving at `v` m/s sweeps about `2 * v` rad/s. A Creature 40 units away
then sees about `80 * v` units/s. So a slap of 1 m/s is over `75 * size` for a
Creature of size 1, and a stroke at 0.3 m/s is 24, below it. The angular speed
of a real arm is as fast as a mouse flick, so the gain is 100. The Creature's
size changes and the distance changes, so this needs a headset test. The
Creature in the desktop runs was 30 to 45 units from the camera with a 2D
radius of 1.22.

**The log.**

```
hand creature: a slap at 96.0 units/s in the game's test (the Hand at 96.0, real hand 1.20 m/s, gain 100%). A slap needs 75.0 and a hard slap 135.0, Creature size 5.00. Total 0.30 to 0.20, dt 0.0167, frame 5000
```

(An example of the format. No slap or stroke was reached in a desktop run.)

**The test aids.** The script word `seekc` steps the fake hand to a point on
the line from the camera to the nearest Creature, a share of the way (0.5,
then 0.35, then 0.65 on each call). The Hand keeps half its length above the
land, so a point on the Creature itself is too high and its pixel misses it.
The game marks a Creature by the value 1 at `+0x10` of the attribute block
at `Object+0x28`. The runs showed 1 for the Creature, 6 for villagers and
28 for a building. Bit `0x10` of the byte at `Object+0x24` was not set on the
Creature. `nudge` steps the hand 2 cm to the side and back, and `slap` steps
it 12 cm, one frame each.

**Desktop results.** Not reached. The Creature is in view for a part of the
play in the pen (runs m4d, m4e and m4f found it at type 1, 30 to 45 units
from the camera, radius 1.22, from about frame 4530 to 5000). `seekc` put
the Hand over it: the game had it under the Hand (`+0x3C8`) and gave hand
mode `0x12` (interactable, not holdable), so the hover works. The Creature
state (state 7, hand mode `0x13`) did not start. A trigger press with the
Hand over the Creature was not tried: the presses came when the nudges had
moved the Hand off it, and they started a land drag (hand mode `0x14`). So
none of the `Update` hook, the ray for `+0x4864`, the stroke, the slap, the
effects 0 to 2 and the packet `0x59` ran in a test. The math is tested
(`MeanVelocity`, `PreviousPoint`, `test/controller_math`). Needs a next run:
`seekc`, then a trigger press without any nudge before it, then a trigger
hold with `nudge` and `slap` steps. Or the headset.

### M6: gestures in the air

**Hooks.**

1. A pre hook on `fn_005CEAD0` (thiscall, `ret 4`): skip it while the
   gesture button is held.
2. Calls to `fn_0057BBC0` with the flattened path (section 5).
3. A hook on the return of `fn_00579F10`, to log which template matched.

**Desktop tests.**

- Read one template from `Gestures.jty`. Step the fake controller along its
  points, one pose per sample. `fn_005CF220` must return 1 and the Miracle
  must start.
- Hold the fake controller still for 70 samples. The ring must clear.

**Risks.**

- The ring holds 80 samples. A slow path loses its start.
- The match likely depends on the orientation and the size of the path [I].
  The plane must use the head's axes, and the scale must be fixed.
- The ring clears when the game camera moves. Head tracking must not look
  like camera movement to `GCamera` [I].
- No button gate was found. With the mouse, every pointer move records. The
  mod must decide when a path starts and ends.

### M6 as built

The code is in `src/mod/gesture_air.cpp` (the hooks, the stroke and the
log), `controllers.cpp` (the button, and the fake hand's `trace`) and
`controller_math.h` (the plane and the corners, as pure functions).

**What the design got wrong.** The design expected one path for each miracle
and a match at the release. The game has no release. It matches the ring
every frame while the pointer moves, and a miracle is a chain of two or three
gestures (section 5). So the mod feeds the ring while the button is held,
and the game matches each piece at once, as it does for the mouse. The
design's test "hold still for 70 samples and the ring clears" does not
apply: the draw adds no sample for a still hand.

**The button.** `gesture_button` (1) is the second face button of the
pointing hand: B on the right hand, Y on the left. 2 is the stick click. 0
turns the draw off, and the hooks are then not installed. The button is no
mouse button. A press needs the hand to act as a mouse (not in a menu) and no
grab of the world. The press gives the controller the pointer (the owner
rule of M2), and the draw starts on the first frame that the controller owns
it. A draw that stops while the button stays down, for example when the
mouse takes the pointer, needs a new press.

**The plane.** On the first frame of a stroke the mod keeps the grip point in
the room and the head's right and up axes (the columns of the head matrix
`H`, `GesturePlaneFromHead`). Each later grip point goes on that plane
(`GestureFlatten`): its offset along the kept right axis and the kept up
axis. A motion toward the face adds nothing. The head of later frames has no
part in it, so a head turn during the stroke does not bend the shape. A snap
turn or a camera move changes the room basis L, not the room, so it does not
bend the shape either.

**The scale.** `gesture_size_cm` (30) of hand motion is a third of the render
height (`GesturePixelsPerUnit`). A mouse gesture is about that size on the
screen. The match does not read the size, so the value only has to keep the
path well above the corner rules (4, 12 and 50 px) and under the 80 samples
of the ring. At a render 1440 high a cm of hand is 16 px. The path starts at
the controller's own pixel at the press (the Hand's pixel with the direct
hand), so the land points and the game's picture of a match are near the
Hand. The screen's y goes down, so up on the plane is a smaller y.

**The samples.** A pre hook on `ProcessPowerUpSystem` runs the stroke once a
controllers frame, just before the game matches. A sample goes in when the
pixel has moved a fortieth of the render height (36 px at 1440, 2.25 cm of
hand at the default size). A 30 cm circle is about 27 samples, a spiral about
41. The sample goes in through the game's own `fn_005CEAD0`, so the pause,
the 0.4 s cooldown after a match, the land under the pixel and the corner
marks are the game's. At the press the mod clears the ring, so nothing from
before the press takes part. At the release it clears the ring again, so an
old path cannot match later, when a miracle becomes possible.

**The corners.** The first build cut sharp corners. With a sample every 36
px, the sample before a corner can be up to 36 px short of it, and the game
then read two half turns (for example -1.06 and -0.49 rad) where a template
has one (-1.55). The fork up and one house template did not fit (run m6b). The
mouse does not cut corners, because it samples by time and a hand slows down
at a corner. So the mod keeps the pixels of the frames since the last sample.
When the next sample is due, the pixel farthest from the line between the
last sample and now goes in first, if it is more than a quarter of the
spacing (9 px) off that line (`GestureCornerIndex`). A curve stays within
that, a corner does not. An offline port of the game's corner and match code
(`fn_0057BBC0`, `fn_0057C3F0`, `fn_0057A1A0`) agreed with the game on the
four templates of run m6b, and it gave 76 of the 81 templates a fit to their
own type with the corners kept, against 59 without.

**No land.** The game drops a sample with no land under its pixel while the
ring is empty, for example over the sky. A stroke that started over the sky
then lost its start, and the "three" of run m6c did not fit. Now, when the
game drops a sample that way (not paused, no cooldown), the mod adds it with
`fn_0057BBC0` and the Hand's world position as the land point. The land
point only places the game's picture of a match and a sound.

**The mouse.** A pre hook on `fn_005CEAD0` drops the game's own samples of
the pointer while the controller owns it (`ControllersOwnPointer`). The
Hand's pixel moves with the head and with the reach, and a wave of the hand
must not cast a miracle. With the mouse as the owner the game samples the
pointer as before. With `controllers=0` or `gesture_button=0` the hooks are
not installed.

**The feedback.** A match starts Immersion effect 3, and the `StartImmersion`
hook of M4 sends a pulse of 0.70 for 120 ms to the pointing hand. The press
sends a tick of 0.15 for 15 ms. The game draws a matched gesture on the land
from the land points of the ring (section 5). Each sample of a stroke has
the land under its own pixel, so the picture has the shape that was drawn.
The game draws no trail while a gesture is drawn, and the mod adds none.

**The log.** The tag `gesture` logs the start of a stroke (the pixel, the
plane, the scale, and whether the camera moves) and its end. The end gives
the samples fed, the corners kept, the samples with no land, the samples
refused, the length of the path in pixels and in cm of hand, the corners
that the game marked, the types the game asked for, its matches, and how
often it cleared the ring during the stroke. A post hook on `fn_00579F10`
logs each match of the game with its template. Then the mod tests the path
against every template with the game's own test (`0x00579F70`), on its own
copy of the path, and logs the corners and the types that fit. So the log
says what the path looks like to the game, even when no miracle can be cast.

**Test aids.** `fake_controller_gestures` is a list of template numbers of
`Data\Gestures.jty`, read from the file (up to 8). The script word
`trace@frame+frames` steps the fake hand through the next one: the corners on
a square of `fake_controller_gesture_cm` (30) in front of the head, centered
at 0, -10, 45 cm, with a point every 8.5% of the side between them. It holds
the first pose for 10 frames, then takes the next still pose every 2 frames.
Each pose is a step, and no pose moves by itself. The launcher removes both
keys.

**Desktop results** (runs m6a to m6e, fake head straight ahead, render 2560
x 1440, 60 fps). The dialog recipe of "M3 as built" takes the game to play in
the pen. From frame 3700 the script held B over one `trace` of a template at
a time. Each stroke is still for its last frames, and B then goes up.

- The hooks install: `the gesture sampler fn_005CEAD0`,
  `GInterface::ProcessPowerUpSystem` and `the gesture match fn_00579F10` all
  say ok. While the controller owns the pointer, the game's own samples of
  the pointer are dropped, 205 to 227 of them a report window of 450 frames.
- Every stroke starts with "16.0 pixels per cm of hand, a sample every 36
  pixels" and the plane of the straight head, right 1 0 0 and up 0 1 0.
- m6a: the file reader used a record of 1632 bytes, not 1628, and read no
  path. Fixed.
- m6b, a sample every 36 pixels: the circle and the vertical fit their own
  type. The fork up (8 corners for the template's 6) and one house (11 for 7)
  did not: the corners were cut.
- m6c, with the corners kept: the house, spiral, heart, fork right and star
  fit. The three did not: it started over the sky, and the game refused 17
  samples. One stroke at the Creature select (frame 3206) found the camera
  moving: the game cleared the ring 38 times, 37 of them with
  `GCamera::IsMoving` set, and the path fit nothing.
- m6d, with the land point of the Hand: the three, fork up, scribble, square
  spiral, S shape and E shape fit, each its own type and only that one. For
  example: `the path (6 corners) fits 1 of the 81 templates: fork up. The
  best is template 21, fork up.`, with the corners `981,865 0.00 0 | 986,416
  3.11 4 | 993,865 -1.56 2 | 1353,865 -1.57 0 | 1355,416 3.12 4 | 1363,836
  0.00 0` against the template's 0, 3.107, -1.551, -1.566, 3.113.
- m6e, steps of the head during a stroke: the look (30 degrees left and 20
  down) during the circle, a lean of 10 cm right and 5 cm forward during the
  fork up, and the look on and off during the square spiral. Each path had
  the same samples, length and corners as with a still head (for example 26
  samples and 1207 pixels for the circle, as in m6b), and each fit its type.
  The game cleared the ring 0 times: a head step does not make
  `GCamera::IsMoving` true in play.

**Tested later (2026-10-04, run mirF of "A miracle in the Hand").** With a
shield and a storm in the Hand the game asked for the circle, matched the
stroke of the fake hand ("the game matched a circle (template 5)"), started
Immersion effect 3 and cast the miracle at the place of the circle. The chain
of gestures of a Worship Site is still not tested.

**Not tested.** The game asked for no gesture in any stroke (`the game asked
for: none`): at this point of Land 1 no Worship Site has a miracle that can
be asked for, and no Miracle is in the Hand. So these did not run: the
game's own match of a stroke, the post hook's log of it, the chain of a
miracle, the Miracle in the Hand, the pulse of effect 3 and the game's
picture of the gesture on the land. A later save with a Worship Site, or the
headset, is needed. Also not tested: the mouse (no run moved it), a real
hand's speed, tremor and slow corners, and continuous head motion, which the
fake head does not make.

**Needs the headset.** The feel of B, the default size of 30 cm, whether a
real hand fits as well as the steps, whether the real head sets
`GCamera::IsMoving` in play, a real miracle, and Y for a left handed player.

## Open questions

- Which interface action sends the pick up packet `0x13`, and which code sets
  hand mode `0x17` (the throw spring) and `GInterface+0x39` bit 6 (the
  release). The input is known from the runs: a right press while the Hand
  holds an object gives `0x17`, and the release of that press lets go
  (section 3).
- What a right press on bare land does with an empty Hand.
- Answered on 2026-10-04: the Hand of the game holds one object
  (`GInterfaceStatus::IsSpaceInHands`), and the mouse cannot pick up a second
  one. See "Many trees from a forest".
- Who reads `GInterfaceStatus+0x10C` (`HandVelocity`).
- What `HandStateCreature+0x110` gates. (No code enters
  `HandStateMultiPickUp`: see "Many trees from a forest".)
- What starts the Creature state (hand mode `0x13`) in play. A press on the
  Creature is the likely input (section 4), and no desktop run tried it.
- The names of the Immersion slots are known now: the game's enum `IMMERSION_EFFECT_TYPE` (`Enum.h` of the decomp) has all 50. See "A miracle in the Hand" for those of a miracle.
- The exact meaning of the second and third vectors of packet `0x4D`.
- Which gesture types `GSpellSystemInfo` holds at `+0x10` to `+0x40`
  (`0x00DA0570` and on). The info file fills them at run time, and no run
  logged them. The start types of the miracle chains are `+0x14` and `+0x18`.
- A `SpellDispenser` makes a one-off seed with `SpellDispenser::CreateOneOffSpellSeed` `0x00722B80`. A tap makes that seed active, with no gesture (see "A miracle in the Hand"). When a dispenser makes its seed was not traced.

## The real hand (2026-09-30, after the first headset test)

The first test with real controllers found the direct hand unplayable. The
Hand was the size of a couch and moved more than the real hand. Both came
from the Go-Go reach of M3: past 35 cm from the chest anchor the reach
multiplied the motion, and the size followed the reach factor up to 80.

The rule now (Buck): the Hand is the real hand, one to one in place and
size, always. So:

- `hand_reach_gain` is 0 by default. The reach curve is then one to one,
  and the anchor no longer matters.
- The size is the real hand's length (`hand_real_cm`, 19 by default) at the
  world scale, divided by the Hand's length at size 1 (3.2 units), times
  `hand_scale_pct` (100 by default). At 7.5 units a meter that is size
  0.445. It never grows with the distance.

Reach comes from the world instead. The next step is a God view in the
manner of the game "How to God": the ground at about waist height, a zoom
that scales the world about the ground below the player, and a stick for
the height of the ground.

**The throw at a large scale.** In the God view and on the table a swing
carries the Hand far over the land, so a throw can end outside the player's
influence, and the game then keeps the object (see "The influence gate" in
section 3). Such a throw now goes from the last point of the swing inside the
influence ([tabletop.md](tabletop.md), section 10.11).

## The glow at the Hand's point (2026-09-30)

After the first headset test Buck asked for "some kind of glow or something
on the hand just to highlight the actual point that will be used for
interacting, like clicking scrolls, picking up objects, etc. It must feel
frictionless."

**The point.** The Hand interacts from one point: the target of "M3 as
built", after the land test. The render pick looks through the pixel of that
point (scrolls, signposts, buildings), and the pick of holdable objects
measures its grab radius from it. The Hand's mesh is drawn around the point,
so the player cannot see where it is. The glow sits on it.

**What it shows.** The look comes from the game's interface hand mode
(`GInterface+0x3AC`), which is what the next press does. The modes are the
returns of the hand mode chooser `fn_005D7F20` [V], and `0x13` is the
Creature (section 1):

| Hand mode | Look | Radius in the room |
| --- | --- | --- |
| 9: the grip picks it up | gold, bright | 1.7 cm |
| `0xD`: the grip takes from a pile, a field or a fish farm (see "The bulk take") | gold, bright | 1.7 cm |
| `0xF`, `0x10`, `0x13`, `0x1C`, `0x20`, and `0x12` over the Creature or over an object that the game taps: the grip acts on something that the Hand cannot take | a paler gold (green until 2026-10-03, see "The colors of the glow") | 1.5 cm |
| any other, for example 3 (the land) | pale blue | 1.0 cm |
| the Hand holds an object | pale blue, faint | 0.8 cm |

The chooser gives `0xD` when the object's vtable slot `+0x6CC` returns 1,
`0xF` or `0x10` when `+0x72C` returns 1, `0x1C` for a bubble (`+0x408`),
`0x20` for a leash (`+0x410`), and `0x12` for any other interactable object.
Which objects give `0xF` and `0x10` is not known [I]. The piles of a store, a
field and a fish farm give `0xD` [V, run bk7]. The mod's pick
puts its choice in the game's record, so the mode is 9 one frame after the
Hand reaches a holdable object. Outside the player's influence the game says
`0x12`, so the glow is green and not gold there: the grip does not pick up.

**How it is drawn** (`src/mod/hand_glow.cpp`):

1. `sky_detail.cpp` calls `HandGlowDraw` after the sun glare, at
   `0x0082F4C9` in `LH3DRender::FinishFrame`. There the world and the sorted
   list are drawn, the depth buffer holds them, and the interface is not
   drawn yet ([tabletop.md](tabletop.md), 4.4). The call runs in every eye
   pass, for each value of `sun_glare`.
2. `HandDirectGlowFacts` (`hand_direct.cpp`) gives the point, the Hand's
   length, the hand mode and whether the Hand holds an object. The first ask
   of a frame latches them, so both eyes get the same values. It gives
   nothing unless the controller places the Hand in the Normal or the Holding
   state and the hooks placed it in this frame.
3. An item of the sorted list can leave the camera without the eye offset
   (`sky_detail.cpp`). So the draw calls `StereoReapplyEye`, reads
   `0x00EA9E40`, and puts the four camera matrices back as they were.
4. `GlowProject` (`controller_math.h`) projects the point as `ProjectPoint`
   does. The radius in pixels comes from the lengths of the matrix columns,
   so the disc is round. Its size is in the room: centimeters times the units
   per meter of the scale now (`TabletopViewScale` on the table and in the
   God view, `HeadPoseScale` at the life size scale).
5. Two layers, each a disc of 20 segments with a middle, a ring at 45% of
   its radius and an edge with an alpha of 0 (60 triangles). First the disc
   in the color of the look, with the game's usual alpha blend, so it shows
   on bright and on dark ground. Then a core of half the radius, in that
   color half way to white, that adds its light (source alpha and one), so
   the middle glows. Each layer is one `DrawPrimitive` on the game's
   Direct3D 7 device, with the game's vertex format `0x1C4`, so dxwrapper
   sees it as it sees the game's own draws. The first desktop build had only
   the first layer, and on the brown hills it read as a gray patch, not as a
   light.
6. The draw reads each state before it sets it, and puts back only the states
   that it changed, in the reverse order. The game keeps its own copy of some
   states and skips a call when its copy agrees, so a state left changed would
   stay wrong. Render states: `ZENABLE` (only when it is off), `ZWRITEENABLE`
   off, `ZFUNC` less or equal, the alpha blend with source alpha and inverse
   source alpha, no alpha test, no fog, no cull, Gouraud, solid, no stencil,
   no color key. Stage 0 takes the diffuse color and alpha, and stage 1 is
   off. The texture stays bound. The core then sets the destination blend
   to one. A state that the device does not give is not changed, and the log
   says so once.
7. The depth: `rhw = near / depth` and `z = 1 - rhw`, as the game writes them.
   The depth test uses a point half the Hand's length nearer to the eye, 9.5
   cm of the room for a hand of 19 cm. So the Hand's own mesh does not hide
   the glow, and land or an object further in front does. The disc writes no
   depth. Its place on the screen stays at the true point, so the two eyes
   fuse it at the Hand's point.

The glow is not drawn when the point is not in front of the near plane, when
the disc is wholly outside the render, or when the disc would be wider than
half the render (a hand at the face).

**Keys.** `hand_glow` (1). 0 turns it off. The launcher row is "Glow at the
Hand's point", in the Controllers group. With the mouse, with
`controller_hand=0` or with `hand_glow=0` nothing is drawn and no state
changes.

**The log.** The tag is `hand glow`. One line at the first draw. One line for
each change of the look and every 900 frames, 40 lines at most, with the
center in each eye, the distance between the two, the radius and the depth.
Each report window counts the draws of each look and the passes with no draw.

**Tests.** The math test has 29 new checks, 498 in all (`GlowChecks`): the
middle and a point off the middle agree with `ProjectPoint`, the disc is round
on a 16:9 render, rhw and z are the game's, the pull moves only the depth test
and stops at the near plane, a point behind the near plane or outside the view
gives no draw, a turned camera gives the same numbers, and two eyes 6.68 units
apart put the two discs `fx * e / d * hx` pixels apart, level, each on its own
eye's line to the point.

**Desktop runs** (glow1 to glow3, the rig of `backend.md`, `play_mode=4`,
`fake_controller=1`, a level fake head, the 2880 by 2880 render at 110
degrees, `-NEWGAME` to the Creature select as in `god-view.md`, section 12,
and `intro_vr=0`: the stereo intro counts frames, so the script's clicks
came during the intro in glow1):

- The device gave every state, and no draw failed. The rest of the frame
  looks as before. The frame rate at the pen was 54.9 fps with the glow on,
  on this desktop rig.
- Both eyes put the disc on the same point of the Hand, the tip of the index
  finger, so that is where the game's Hand keeps its point. At 1 m the discs
  were 67.4 px apart and at 45 cm 149.7 px apart, which is
  `fx * e / d * hx` with fx 0.700 (110 degrees), e 6.68 units and hx 1440.
  The radius of 1 cm at 45 cm was 22.4 px, as the math gives. Both discs were
  level to 0.1 px.
- The word `seek` put the Hand on a villager: the mod's pick chose it and
  the hand mode was 9 four frames later. The disc turned gold and larger.
  That point was at the edge of the view, so only the right eye drew it: 294
  draws, and 298 passes with the disc outside the view. A step away gave
  one frame of `0x12` (green), then 3.
- A pale blue disc over the land in both eyes, and a gold one with a white
  middle over the villager. The first build with one layer was a dull gray
  patch on the hill (run glow2), which is why the core was added.

Not seen on the desktop: the green look for more than one frame, the look
while holding, and a disc that land or an object in front hides. The grip
press on the villager was dropped, because the Hand's pixel was off the
render ("the right button press started off view"). So at the edge of the
view one eye can show gold while a press does nothing.

**Needs the headset.** The sizes and the colors. Whether the glow is easy to
see on bright land, on the sea and on the Hand. Whether the depth pull of
half a Hand is right: the disc must not vanish into the finger tip, and a
hill in front must still hide it. Whether a one frame change of the look (9,
`0x12`, 3 when the Hand leaves an object) shows as a flicker. Which objects
give the touch look: scrolls, signposts and buildings.

## Overlay renderer (2026-10-02)

The plan for the renderer's limits (spike 2) grows the glow into a small
renderer for the mod's own meshes. Real hand models, the controller models
of the hints, rays and the rim of the table can then draw inside the game's
frame, at the right depth in both eyes. This first version draws one test
mesh: a low poly hand at each controller.

**The test hand** (`src/mod/overlay_math.h`, `BuildHand`). A palm, four
fingers of three segments and a thumb of three segments, 330 triangles. Each
segment is a prism with six sides that gets thinner at each joint, and each
finger ends in a round tip. The hand uses the frame of the grip pose after
the flip of z in `head_pose.cpp`:

- +x is out of the back of the right hand, so the palm faces -x.
- -y is along the open fingers, from the wrist to the finger tips.
- +z is across the knuckles, from the little finger to the index finger.

The grip point is the middle of the controller's handle, and a closed hand
wraps around it. The left hand is the mirror image in x. The wrist to the
tip of the middle finger is the real hand's length in the room
(`hand_real_cm` times `hand_scale_pct`, 19 cm by default) at the scale now:
the units per meter of `head_pose.cpp` times the gain of the table or the
God view. So the hand is one to one in place and in size, also on the table,
where the game's Hand has `table_hand_cm`.

**The fingers.** The index finger closes with the trigger. The middle, ring
and little fingers close with the grip. The curl is 0.12 at a value of 0, so
a hand at rest is a little bent, and 1 at a value of 1. A full curl turns the
three joints by 75, 95 and 55 degrees about +z, toward the palm side. The
thumb did not bend in this first version. Since 2026-10-03 the hands have
poses and a thumb that moves ("One pair of hands").

**How it is drawn** (`src/mod/overlay.cpp`):

1. `ControllersOverlayHands` (`controllers.cpp`) gives the grip pose of each
   hand in the world, with its trigger and its squeeze. It uses the head
   sample of the frame and the steps of the direct hand, on the table and
   off it. With no reach gain, the pointing grip is the point where the
   direct hand puts the game's Hand.
2. Once a frame, after `HandDirectFrame`, `OverlayFrame` builds the meshes,
   places them in the world and lights each corner: a fixed light from
   straight above and an ambient of 0.35, on a plain skin color. Both eye
   passes draw this one list.
3. `sky_detail.cpp` calls `OverlayDraw` after the sun glare and before the
   glow, at `0x0082F4C9` in `LH3DRender::FinishFrame`. There the world and
   its depth are drawn, and the interface is not. Later a screen quad resets
   the depth before the interface draws ([tabletop.md](tabletop.md), 4.4),
   so the depth of the hand does not reach the interface. The glow comes
   after the hand, so it also shows on the hand.
4. `OverlayEyeMatrix` gives the world to camera matrix of this eye and puts
   the four camera matrices back. The glow now uses it too.
5. Each corner goes into camera space on the CPU. Each triangle is cut at
   the near plane, and a cut point takes the color at the same share of the
   edge. Then each corner is projected as `ProjectPoint` does, with
   `rhw = near / depth` and `z = 1 - rhw`. The vertices have the game's
   format `0x1C4`, so the draw needs no shader and no transform state. A
   triangle with all its corners past one edge of the render is not sent.
6. One `DrawPrimitive` for each eye, on the game's Direct3D 7 device. As the
   glow does, the draw reads each state before it changes it, and puts back
   only the states that it changed, in the reverse order. Render states:
   `ZENABLE` (only when it is off), `ZWRITEENABLE` on, `ZFUNC` less or
   equal, no alpha blend, no alpha test, no fog, no cull, Gouraud, solid, no
   stencil, no color key. Stage 0 takes the diffuse color and alpha, and
   stage 1 is off. The texture stays bound. The hand writes depth, so its
   fingers hide its palm, and land in front hides the hand.

The overlay draws only while the controller owns the pointer, and each hand
only while its pose is tracked. The game's own Hand still draws, and the log
puts the two side by side.

**Keys.** `overlay` (0). 1 turns the test hand on. With 0 the overlay draws
nothing and changes no state. There is no launcher row yet.

**The log.** The tag is `overlay`. One line at the first draw, and one at
each change of the owner of the pointer. Then, on the first draw, on a
change of the pointing hand's trigger or grip, when its index tip moves on
the screen, and every 900 frames, 40 times at most: for each hand the
trigger, the grip and the index tip in each eye. For the pointing hand also
the grip point, the index tip and the game's Hand position (`CHand+0x78`),
with the distance between the two in cm of the room. After 300 frames with
a draw in both eyes, one line gives the triangles a frame and the time on the
CPU, with the build and the draws apart. Each report window counts the draws,
the triangles, the triangles that crossed the near plane and the triangles
off the screen.

**Tests.** The math test has 49 new checks, 547 in all (`OverlayChecks`):

- The curl from the controller values, and 330 triangles.
- The index tip at half curl against an independent chain of bends, and as
  a corner of the mesh.
- The tip at a turned grip pose, for the right hand and for the mirrored left
  hand, and the back of each hand.
- A length of 19 cm and a width of 6 to 12 cm across the knuckles, at 7.5, 100
  and 2000 units per meter.
- The trigger closes only the index, and the grip closes only the other
  three. The thumb does not bend.
- The light from above and the ambient.
- The cut at the near plane: one triangle in front, nothing behind, two
  triangles for one corner behind (the cut on the near plane, the color at
  the same share), and one triangle for two corners behind. The test for a
  triangle off the screen.
- Two eyes put a corner `fx * e / d * hx` pixels apart, level, at the same
  depth.

An offline check sent the fake controller's default pose for both hands
through the real `head_pose.cpp` and the overlay math, for two eyes at 110
degrees and 100 units per meter, and drew the triangles. Both hands showed in
both eyes, the left hand was the mirror image of the right, and the index
tips were 100.8 px apart at 33 cm, as `fx * e / d * hx` gives. The index
closed with the trigger and the other three fingers with the grip.

**Desktop runs.** None yet. Both tries of run ov1 were refused, because the
PC was in use. The run to do: `play_mode=4`, `overlay=1`,
`fake_controller=2`, `controller_hand=1`, `intro_vr=0`, eye captures, and the
script of the glow runs to the Creature select. At the pen, the right hand
steps to `20,-25,45`, the trigger and then the grip close for 30 frames each
with a `shot` in each hold, and the hand steps down to `20,-62,45` and
`20,-85,45` under the land, then near the eyes to `5,-20,30`. Check that the
hand is at the same point in both eyes, that the index closes, that land in
front hides the hand, that every state is the same after the draw, and the
cost line.

**Needs the headset.** Whether the hand sits on the real hand: each runtime
and controller puts the grip pose at its own place on the handle. The size
and the color. The near plane: in play mode 0 it is 3.5 units, 47 cm of the
room, so a hand nearer to the eye than that is cut away. The God view brings
it in to 5 cm. Whether the two hands, the game's and the overlay's, confuse
the player before the game's Hand is hidden. The frame cost at the
headset's render size.

## The grip holds (2026-10-02)

Buck, after the headset test of 2026-10-02: "Throwing things doesn't really
work, we need to pick objects up with the grip button, and release them when
we release the grip button, now we need to press the grip button to release
something, so throwing simply doesn't work that well."

**Why.** The grip was the game's right button, one to one. The game takes an
object with one click and lets go with a second click (section 3): the second
press starts the throw spring, and its release runs the release action. So a
player had to close and open the grip twice, and the throw speed came from
the second opening, after the swing.

**What the grip does now.** With the direct hand (`controller_hand=1`) the
grip holds like a real hand, in every play mode:

1. Close the grip on an object (the glow is gold): the game's pick up starts
   at once, as before. The object is in the Hand about 10 frames later.
2. Keep the grip closed: the Hand carries the object. The game's right button
   is up during the carry, so the trigger still drags the land.
3. Open the grip: the Hand lets go. A fast hand throws with the speed of the
   swing at the opening. A still hand sets the object down at the Hand.

`GripStep` in `controller_math.h` is the machine, with no game state.
`controllers.cpp` runs it once a pump for the right button, and
`hand_direct.cpp` gives it the facts of the Hand. With
`controller_swap_buttons=1` the trigger does all of this, because it is then
the right button. With `controller_hand=0` (the pointer of M2) the grip is the
right button, one to one, as before.

| Phase | When | The game's right button |
| --- | --- | --- |
| Pass | The Hand holds nothing, or the game does not hold the object in the Holding state | The grip, one to one, as before |
| Carry | An object came into the Hand while the grip is closed, and the pick up is over: the pick lock `GInterface+0x38` is clear, or 15 frames passed | Up |
| OpenHeld | An object is in the Hand and the grip is open, for example after the game refused a drop | Up. A close carries it again |
| LetGo | The grip opened while it carried | The mod's click: down, then up when the hand mode is `0x17` on a later frame, when the object left the Hand, or after 8 frames |

**The click.** The press comes on the frame the grip opens. The desktop runs
of M4 saw the throw spring 2 frames after a press and the release action 1
frame after the release, so the game lets go about 3 to 4 frames after the
opening. The press of the click always comes after a frame with the button
up, so the game sees a new press. The click is not a press on something, so
it needs no pixel in view and no fresh press, and a grab of the world does not
hold it back. A throw often ends with the hand out of view. The click gives no
haptic pulse. The game's own effect 46 (the release) gives one.

**The throw speed.** `HandDirectGripOpened` latches the motion at the moment
the grip opens: the direction of the reach point and the real speed of the
grip, as the mean over the 80 ms before the opening (`kThrowWindow`). The
history's newest sample is from the frame of the opening, as is the grip. The
release action of the next 0.3 s uses the latch once, through the same
`PrepareThrow` as before, so the gain, the cap of 124, the gentle drop, the
rule of the table and the throw from inside the influence all stay. A release
with no latch, for example from the trigger, takes the 80 ms before the
release as before. The math test shows why: a swing at 4 m/s that stops at
the opening gives 30 units/s at the opening, and only 12.9 units/s over the
80 ms before a release 4 frames later.

**The choices.**

- **A grip on nothing** is the game's right press, as before (Pass). It keeps
  the camera mode of both buttons. With `grab_hand=2` it grabs the world, as
  before. What a right press on bare land does in the game is not known.
- **A bulk take** (the food or the wood of a store, a field, a fish farm):
  the grip is the button while the take runs (the fact `take` of `GripStep`).
  A closed grip collects. The opening stops the take, latches no throw and
  sends no click. See "The bulk take" and "The take that did not end".
- **The Tug** (a tree or a rock): the button stays down while the grip is
  closed, as with the mouse, so the pull works. An opening ends the pull. When
  the object comes free into the Hand, the grip carries it.
- **The Creature's leash** puts the Hand in the Tug state, so the grip is the
  button: closed holds the leash, open lets go of it.
- **A Miracle in the Hand** (the Grain state) is not "holding". The grip is
  the button, and what a close and an opening do depends on the kind of the
  miracle. See "A miracle in the Hand" (2026-10-04). A one-off seed that the
  Hand carries is an object, so the grip carries it.
- **The Creature** (state 7) and every other state: the grip is the button.
- **A villager over the Creature or over a building**: the opening is the
  game's own drop click at that point, so the game does there what a mouse
  drop does.
- **A drop outside the player's influence**: the game keeps the object, as for
  the mouse, and the phase is OpenHeld. The table and the God view still let a
  throw go from the last point inside the influence (`tabletop.md`, 10.11).
  Close the grip to carry the object again, and open it inside the influence.
- **A land drag while the grip opens**: the click waits until the trigger is
  up, because both buttons together are the game's camera mode and not a
  drop. A close while it waits carries the object again.
- **The grip opens before the object comes** (during the 10 frames of the
  pick up): the take press ends at once, and the game stops the pick up, as
  for a short mouse click. If the object still comes within 20 frames, the
  machine lets it go with a click.
- **The mouse takes the Hand** during a carry: the grip is the button again,
  and a closed grip must open before it presses. A carry counts as a held
  controller button for the owner rule (`ControllersGripCarries`, read by
  `ChooseOwner` in `pointer_map.cpp`), so a bump of the mouse does not take
  the pointer then. The real mouse buttons reach the game as before.
- **A second object while holding**: not possible with the grip, because the
  grip carries with no button. The game may allow more than one object in the
  Hand (`IsSpaceInHands`). Not tested.

**The mouse.** The machine runs only with the direct hand, and only the
controller's own presses go through it. While the controller holds no button,
a real mouse event reaches the game exactly as before (`h_SetButtons`).

**The log.** The tag `grip` gives each change: "the pick up is over, the grip
carries the object", "the grip opens: let go", "the click to let go presses",
"the click to let go releases", and the rest, with the facts of the Hand. The
press of the click logs as "the right button is down (the click of the open
grip)". The tag `hand throw` adds "the grip opened N ms (N frames) before this
release, and the motion is from the 80 ms before the opening". Each report
window counts the carries, the openings, the clicks and the waits.

**Tests.** The math test has 50 new checks, 597 in all (`GripChecks`): the
take, the carry and the let go in the frames of run m4d, a second step in one
frame, the click with no spring, a press that gives the object away, a
refused drop and a close to carry again,
the wait for the trigger, a Miracle, the Tug, a pick lock that does not clear,
an object that comes after the opening (soon and late), an opening on the
frame the pick up ends, the mouse during a carry and during the click, an
object that leaves another way, the throw latched at the opening, and the
land floor below.

**Desktop runs.** None yet: every try of run gr1 was refused, because the PC
was in use. The run to do: the rig of `backend.md` with `play_mode=4`,
`fake_controller=1`, `controller_hand=1`, `intro_vr=0` and
`fake_head_pitch=-30`, so the land at the waist is in view. The head looks
down, so the clicks on the start dialog move to the points `-14,-48,88` and
`-15,-60,81` (the same render pixels as `-14,2,100` and `-15,-11,100` for a
level head). Then `15,-50,40`, and `0,-80,45` at the pen, which pushes the
hand into the land. The script:

```
point@400+1 trigger@440+8 point@520+1 trigger@560+8 point@640+1
point@2800+1 shot@2830+1 seek@2860+1 grip@2900+60 shot@2930+1
seek@3020+1 grip@3060+40 slap@3098+1 seek@3200+1 grip@3240+40
trigger@3270+20 shot@3300+1
```

Look for: the point 0.50 cm over the land at frame 2820, a take at 2900, "the
grip carries" about 15 frames later, a let go at 2960 with a set down, a
throw at 3100 with the 12 cm step of 3098 in the latched motion, and a click
that waits for the trigger at 3280 and presses at 3290.

**Needs the headset.** Whether 3 to 4 frames from the opening to the release
feel at once. Whether the window of 80 ms before the opening gives the throw
the player meant. Whether a still hand sets the object down where it is.
Whether the trigger drag while carrying feels right.

## The point touches the land (2026-10-02)

Buck tried the God view on 2026-10-02: "it's still a bit hard to pick
objects up, it seems my hands/glowing interact orb can't get close enough to
the ground so picking things up and interacting is difficult."

**The cause.** `ResolveLand` in `hand_direct.cpp` lifted the target, the point
of the Hand and of the glow, to half the Hand's length over the land, as the
game keeps its own Hand. The Hand's length is the real hand in the room at the
scale now, so the point stayed 9.5 cm of the room over the land at every
scale for a hand of 19 cm: at 7.5, at 100 and at 1000 units per meter (5 cm on
the table, with `table_hand_cm` 10). The math test checks this number for the
three scales. A real hand pushed into the land stopped 9.5 cm over it.

The game itself does not hold the point up. Its distance step ends the ray
half a Hand before the land, but the depth hook and the pose hook replace
both. Its test of a low Hand in `ObtainRequiredHandPosition` (`0x005B6F8C`)
only turns the Hand's up vector to the land's normal (`fn_004427B0` makes map
coordinates, and `fn_00803630` reads the normal there) [V]. The God view's far
limit follows the scale, and its room has no offset in height.

**The fix.** The target now stops `kLandClearanceM`, 5 mm of the room, over
the land, at the scale now (`HandLandFloor` in `controller_math.h`). The scale
is the table's in play modes 3 and 4 and `HeadPoseScale` in play mode 0. So
the point touches the land and the objects on it, one to one, and a hand
pushed further down slides on the land. Nothing moves that the player did not
move. The glow follows the point. The grab radius of the pick still measures
from the point. On a real table the controller's grip is about 5 cm over the
top when the fingers touch it, so the table looks as before.

The tag `hand` logs, 20 frames after the target stops, the point's height
over the land and how far the controller is under it, in cm of the room.

**Needs the headset.** Whether the glow and the game's Hand now reach the
land and a villager in the God view. Whether the Hand's mesh in the land
looks wrong.

## The bulk take (2026-10-02)

Buck, after the headset test of 2026-10-02 (roadmap item 8): "I couldn't
really do any bulk interactions, like collecting grain or wood from the
granary or from the forests, sometimes the animation got stuck, no sound was
playing, no visible counter hint was showing."

### What the game does [V]

- **The sources.** A store keeps its food and its wood in piles beside it:
  `PileFood` and `PileWood` objects (class `PileResource`). Run bk6 found five
  `PileWood` and one `PileFood` within 10 units of the store of Land 1
  (`NORSE_ABODE_STORAGE_PIT` at 1826.79, 2641.36 in `Scripts\Land1.txt`). A
  `Field` and a `FishFarm` are sources too. For a source, vtable slot `0x6CC`
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
  the sound (`PSysGlobal::StartMultiPickup` `0x0068F8C0`). Then
  `FUN_005DC110` writes the source to `GInterfaceStatus+0x3C` and 0 to `+0x40`.
- **Each game turn.** `GInterfaceStatus::Process` `0x005DC4E0` adds 1 to
  `+0x40` and calls slot `0x808` (`ProcessInInteract`, `0x0066E520` for a
  pile). The amount of a turn grows with the turns (`fn_0066CD30` and
  `GPotInfo::GetTicksToChangeOver`). The call plays a tick (`fn_0068F930`,
  `GAudio::PlaySoundEffect`), moves the amount from the pile to the Pot, and
  shows the count with `ToolTips::ForceToolTips(0xEEA, Pot+0x70)`. Text 3818
  is "Amount: %3.0f". For a source that is not a Creature, a turn also ends
  the take when the point at `GInterfaceStatus+0xC8` is outside the
  influence.
- **The end.** The release of the right button (`GInterface+0x39` bit `0x40`)
  runs `FUN_005D4830`, and `FUN_005D2210` sends packet `0x1C`. `FUN_005DAA10`
  then calls slot `0x6EC` (`StopMultiPickup` stops the effect and the sound)
  and writes 0 to `+0x3C`. The Pot stays in the Hand, in the Holding state. A
  click drops it as any object, and `Pot::InitialisePhysicsFromHand` pours it
  out (`StartMultiPutdown`).
- **The Hand.** During a take the Holding state asks slot `0x6A0`
  (`IsLockedInInteract`) of the source (`0x005B3EB3`) and holds the Hand over
  it. The direct hand moves the Hand to the controller, as for any held
  object.
- **The forest.** A `Tree` gives no take. A press on it starts the Tug (see
  "The grip holds"). A `BigForest` puts one new tree in the Hand
  (`BigForest::InterfaceSetInMagicHand` `0x004393C0`). So the wood of a forest
  comes one tree at a time, and wood in bulk comes only from the piles of a
  store. Since 2026-10-04 the mod adds a take of its own: see "Many trees
  from a forest".

### Why it failed

1. **The grip carried.** The Pot is in the Hand from the first turn, so
   `GripStep` saw an object in the Hand. After the pick lock, or 15 frames, it
   went to Carry and let the right button go up. The game then ended the take
   with the first amount (33 wood in run bk7). The tick and the count ran for
   a few turns only. That is also the likely cause of the Hand's animation
   that stopped [I].
2. **The mod's pick.** In the God view and on the table the pick puts the
   nearest holdable object within the grab radius in the next record, and the
   screen test hook keeps every other object out of the game's pick. A pile of
   a store cannot be held, so a villager or a barrel near the store went in
   the record. In run bk4 the Hand was on a `PileWood`, and the game had no
   object under the Hand.
3. **No count.** The game's help options of the profile were "No ToolTips",
   so the game showed no "Amount" (see `ui-layer.md`, section 15).

### What the mod does

1. `HandDirectGripFacts` says that a take runs (`take`): `GInterfaceStatus+0x3C`
   holds an object, and its slot `0x6A0` gives 1. `GripStep` then makes the
   grip the game's button, one to one, as for a held mouse button. The
   opening lets the button go up, and that ends the take. That opening latches
   no throw and sends no click. After the take the Pot is in the Hand with the
   grip open (OpenHeld). The next close carries it, and the next opening lets
   go. A pile that runs out while the grip is closed gives a carry. Until
   2026-10-03 the function returned false during a take, and the take did not
   always end (see "The take that did not end").
2. `Choose` counts a bulk source too (`TakeValid`, the tests of
   `FUN_005D1330`), and a holdable object wins a tie. So the pile goes in the
   next record, and the game gives `0xD`.
3. The glow is gold over a source (`hand_glow.cpp`).

For the player:

1. Put the Hand on the food or the wood of a store, or on a field. The glow is
   gold.
2. Close the grip and keep it closed. The amount grows, and the count shows
   beside the hand.
3. Open the grip. The take stops, and the Hand holds the bundle.
4. Close and open the grip again. The Hand puts the bundle down, on the land
   or on the store.

The mouse is as before: no change runs while no controller button is held.

### The log

The tag `hand bulk` gives the start of a take with the class of the source and
the first amount, the amount after each change, and the end with the amount
in the Hand. Each report window counts the takes, the frames of a take and
the pumps where the grip was the game's button. The tag `hand pick` says when
the choice is a bulk source, and whether it is inside the player's influence.

### Tests

The math test has 12 new checks (`BulkTakeChecks`): the carry of the old
facts, 200 frames with the button down, an opening with no throw and no click,
the end of the take with the Pot kept (OpenHeld, no late let go), a close and
an opening that let go, a pile that runs out with the grip closed, and a short
take.

Two test keys of `bwvr.ini`, for the desktop runs only:

- `seek_bulk=1`: the word `seek` goes to the nearest bulk source within 2 m of
  the room, and the log lists the sources and the classes near the Hand.
- `bulk_test_influence=1`: the player's influence is 1 while the pick has a
  bulk source, while a take runs and while the Hand holds an object. The store
  of Land 1 is outside the influence for minutes after a new game (runs bk5
  and bk6, to frame 7000).

Desktop runs, God view, `fake_controller=1`, `controller_hand=1`, Land 1 with
`-NEWGAME`. The start dialog now needs the points `-21,33,100` and
`-23,13,100`, because the aim ray clicks a dialog box. Four turns right and six
zoom steps out (1139 units a meter) put the store at the waist, and the point
`-16,-50,55` with `seek` puts the Hand on a `PileWood`:

- bk7: the hand mode was `0xD`. The press at frame 3281 started the take at
  3286 with 33 wood. The amount grew each turn: 41, 49, 57 and on, 1088 after
  49 turns. The grip was the game's button on 201 pumps of the 200 frames.
  The opening at 3481 ended the take at 3487 with the wood in the Hand, and
  the grip machine said OpenHeld. The "Amount" tooltip showed at the Hand.
- bk8 (the influence also lifted while holding): 488 wood after 33 turns.
  The close and opening at 3521 and 3551 set the bundle down on the store
  pile at 3553, and the Hand was empty at 3558. A second take gave 118 after
  11 turns.

Not tested: the sound (the play trace of bk8 shows no start of the tick
sample), a field and a fish farm, a store inside the real influence, and the
Tug of a tree in a forest.

**Needs the headset.** Whether the count reads at the hand, the tick and the
effect, and whether the Hand stays at the controller during a take.

### The take that did not end (2026-10-03)

Buck, after the headset test of 2026-10-03: "Grabbing things in bulk still
doesn't really work well. At times it seems to get stuck in the bulk
collection mode and releasing the grip or pressing any other button just
doesn't stop, it only stops itself once it reaches the 20,000 gather limit
... Maybe we need to change how bulk collection works by making it so holding
the grip collects it (even over distance) and letting go of the grip stops
the collection, then pressing the grip again drops it? If this worked
consistently it would be good."

**His log.** The first take (a `PileFood`, frame 11998) ended 111 game turns
later, 6 frames after the grip opened. The second take (a `PileWood`, frame
13241) ran for 2381 frames, which is 26 s. In that time the controller sent 6
releases and 5 presses of the right button to the game, and the take went on.
It ended at frame 15622, when the game itself stopped it. The tag `hand bulk`
shows only 8 turns of it, because the first take had used the 120 lines of
the tag.

**The cause [V].** The game ends a take through three interface actions
(`GInterface+0x44`, the table of steps at `0x00D17DA8`, `0x30` bytes an
action):

| Action | Its step | What the step does |
| --- | --- | --- |
| 4, the take runs | `FUN_005D4830` | On the release bit (`+0x39` bit `0x40`) it reads the object of the next pick record (`+0x400`, at `0x005D483A`), sets the pick lock (`+0x38 = 0x10`) and calls `FUN_005D49B0` |
| 5, the wait | `FUN_005D49B0` | When the object of the record has bit `0x10` of `+0x24` (a take holds it) and slot `0x6E4` gives 1, it sends the end packet `0x1C` (`FUN_005D2210`) and sets action 6. Else it sets action 5 and sends nothing |
| 6, the end | `FUN_005D4A10` | When the object of the record has no bit `0x10`, it clears the pick lock and calls `GInterface::ResetActionState` `0x005D29C0` |

Slot `0x6E4` is `Object::IsReadyForNetworkUnfriendlyEndLockedSelect`
(`0x00402810`) and gives 1 for each class. So a release ends the take only
when the next pick record holds the source of the take at that moment. The
pick lock then keeps the record as it is: `GInterface::PreDrawProcess`
(`0x005CEA57`) and `UpdateInterfaceCollide` (`0x005D5A9C`) do not change a
locked record. With another object in the record, the interface stays in
action 5 with the pick locked. The take goes on, no button does anything, and
the Hand picks nothing. `GInterfaceStatus::Process` ends this only when slot
`0x808` of the source says stop (the source is empty, or the Pot is full) or
when the Hand is outside the influence. It then resets the interface
(`fn_005D1260`). That is what he saw.

With the mouse the game holds the Hand over the source during a take, so the
record holds the source. With the controller the Hand is at the real hand.
While the Hand holds the Pot, the mod's pick does not fill the record
(`Choose` and `Inject` need an empty Hand). So the record holds what the
game's own pick finds under the pixel of the Hand: a different pile of the
store, a villager, or nothing. The piles of a store are less than 10 units
apart, so this can go wrong with a still hand too.

With no object in the record, `FUN_005D4830` calls `fn_005D1260` and then
reads through the null pointer at `0x005D4854` [V for the code]. No run and
no log showed this.

**The fix.**

1. **The source stays in the record.** `KeepTakeSource` (`hand_direct.cpp`)
   puts the source of the take (`GInterfaceStatus+0x3C`) in the next pick
   record with `fn_005D5E40`, while a take runs and the controller places the
   Hand. It runs before each interface step (`h_InterfaceStep`) and after
   each eye pass. So the steps of actions 4, 5 and 6 find the source and end
   the take, as they do for the mouse. A take that is in action 5 ends on the
   next step.
2. **Nothing collects after the grip opens.** `GripStep` has the fact `take`.
   While a take runs, the grip is the button. When the button was up for
   `kGripTakeEndFrames` (18 frames, two game turns at 90 frames a second) and
   the take still runs, the step sets `end_take` on each frame. `EndTakeNow`
   then acts before the next interface step. In action 4 it sets the release
   bit for that one step. In actions 5 and 6 the record is enough. In any
   other action it sets the pick lock and calls `FUN_005D49B0`. If the take
   still runs 18 frames later, it calls the handler of the end packet
   (`FUN_005DAA10`), clears the pick lock and calls `ResetActionState`. The
   handler never runs in a multiplayer game.
3. **The take ends at once.** After the step that reads the release, the mod
   asks for a game turn at once (see "The release at once"). So the end
   packet runs on the next frame, and not up to 100 ms later.

**The rule for the player.**

1. Hold the grip on a bulk source. The game collects while the grip is
   closed.
2. Open the grip. The take stops, and the bundle stays in the Hand.
3. Close and open the grip again. The Hand drops the bundle.

The rule is the same with the Hand on the source and at a distance (see "A
bulk source at a distance").

**The log.** Tag `hand bulk`: the amount of each of the first 5 turns and
then of each tenth turn, so one take does not fill the log. During a take,
each change of the next pick record (the object, and whether it is the
source), of the pick lock and of the interface action. "The next pick record
held X, not the source" each time the mod puts the source back. "The take
still runs with the grip open, so the mod ends it" with the method. Each
report window: the steps where the record held the source, the steps where
the mod put it back, and the takes that the mod ended. Tag `grip`: "the grip
opens during a bulk take" and "the bulk take still runs after the grip
opened".

**Tests.** The math test has 28 checks of the rule (`TakeRuleChecks` in
`test/controller_math/grip_checks.h`): the button is down for all 300 frames
of a take with no carry, the opening lets the button go up with no throw and
no click, the game has a game turn to end the take, the bundle stays
(OpenHeld), the next close and opening drop it, a take that still runs 18
frames after the opening gets `end_take` on each frame, a close during such a
take collects again, a take that starts after the grip opened is ended, a
take during a carry gets no button, and the mouse's take is never ended.

**Desktop runs.** None yet. Each try of 2026-10-03 was refused, because the
PC was in use. The runs to do use the rig of run bk8 with the start dialog of
`god-view.md`, section 12: `play_mode=4`, `fake_controller=2`,
`controller_hand=1`, `intro_vr=0`, `fake_controller_yaw=8`,
`fake_controller_pitch=1`, `fake_controller_sweep_yaw=8`,
`fake_controller_sweep_pitch=-6`, `seek_bulk=1`, `bulk_test_influence=1`, the
points `-14,2,100 -15,-11,100 -16,-50,55 25,-25,40` and the script

```
point@400+1 trigger@440+8 pose2@500+1 point@520+1 trigger@560+8
stick_right@2880+8 stick_right@2910+8 stick_right@2940+8 stick_right@2970+8
stick_down@3000+10 stick_down@3030+10 stick_down@3060+10
stick_down@3090+10 stick_down@3120+10 stick_down@3150+10
point@3190+1 seek@3220+1 grip@3280+120 point@3340+1 grip@3560+30
```

The hand goes to a pile of the store, the grip closes at frame 3280, the hand
steps 50 cm away at 3340, and the grip opens at 3400.

- **gt1**, with the test key `bulk_test_no_keep=1` (the source does not go
  back in the record, and the mod ends no take). This is the build of
  2026-10-03. Look for "during the take the next pick record holds X (not the
  source)" after frame 3340, for interface action 5 with the pick locked
  after frame 3400, and for amounts that grow after that.
- **gt2**, with no test key. Look for "the next pick record held X, not the
  source", for "the bulk take ends" a few frames after 3400, for OpenHeld,
  and for the drop of the bundle after the opening at 3590.

**Needs the headset.** A take with the hand pulled away from the store, and
a short squeeze on a pile. Both must stop when the grip opens.

## The Creature by touch (2026-10-02)

Buck tried the Creature in the headset on 2026-10-02: "Interacting with the
creature seems to be broken too, it keeps teleporting the camera around and
you can't really touch the creature, ideally we would render the creature
panel HUD in space but not move the camera at all so we can slap or stroke
the creature more naturally without seeming like we're entering this
different mode."

This section replaces the speed gain of "M5 as built". The research was
`design-creature-teleport.md`, Part 1. The code is in `hand_direct.cpp`, with
the pure math in `controller_math.h`. The Creature panel is in
`ui-layer.md`, section 13.

### What the game does [V]

- **The press.** A trigger press on an object with bit `0x10` in byte
  `Object+0x24` runs the interface action at `0x005D4960`. It sets the
  interface style `GInterface+0x44` to 4 (`fn_005D2980`). In style 4 the hand
  mode is `0x13` only while the next pick record (`+0x400`) holds an object of
  type 1, the Creature (`fn_005D80C0`). Another object gives `0xE` and no
  object gives 3. Mode `0x13` puts the empty Hand in state 7.
  `HandStateCreature::Enter` (`0x005B13F0`) sets the lesson total `+0xD8` and
  the timers `+0xCC`, `+0xD0`, `+0xD4` and `+0xDC` to 0.
- **The camera.** For an empty Hand, `CHand::GetRequiredState` calls
  `CHand::OperateInteractionCamera` (`0x0046CB60`, thiscall with the hand mode,
  `ret 4`) at `0x0046CEA0`, on every call. For mode `0x13` it casts the object
  under the Hand (`GInterface+0x3C8`) to a Creature and writes it to
  `CHand+0x49B4`, and its `LH3DCreature` (`[[creature+0x160]+0x58]`) to
  `+0x49B8`. When the flag `+0x49C0` is clear, it makes a
  `CameraModeCtrInteract` (`0x0044A850`), which pushes itself over
  `CameraModeNew3`, and it sets the flag and `+0x4900` to 0. For any other
  mode with the flag set, it pops the camera mode (`GCamera::PopViewMode`
  `0x00441C50`). Then, when `+0x4900` is below 450 and the Creature belongs to
  the local player, it sends packet `0x5F` (`GGame::SetPacket` `0x00550DD0`,
  thiscall, `ret 4`): the leash click. The local player is `GGame + 0x18 +
  0xA60 * byte[GGame + 0x205A59]`, and the Creature's player is its vtable
  slot `+0x1C`.
- **Why the camera jumped.** `script_camera.cpp` counts
  `CameraModeCtrInteract` as a story mode. So the God view stood aside to the
  game camera at the press, and built the view again at the release, at the
  focus of a camera that was still in its glide back. In play mode 0 the held
  shot cut each time the camera's ease came to rest.
- **The Hand in state 7.** The state calls `SetDistanceFromView` at
  `0x005B196C` and `0x005B2044` with the Creature's distance, and multiplies
  the size `+0x4834` by `size^0.5` at `0x005B1975` and `0x005B204D`. Then
  `ScreenPointToWorldAtDistance(+0x485C, +0x4854)` writes `+0x78` at
  `0x005B23C4`. The depth hook skipped state 7, so the Hand left the controller
  and slid along the line of sight to the Creature's depth.
- **Contact.** The mesh ray test `fn_00865020` (thiscall on a hit record, 8
  stack arguments, `ret 0x20`) casts the ray from the eye through the Hand's
  point (return address `0x005B19F1`). When that misses, it casts the ray of
  the pixel between the current and the last screen point (`0x005B1ABC`). A
  hit anywhere along the ray counts, also far behind the Hand.
- **The slap.** The slap branch runs on a hit with `+0x110` at 0 (the first
  frame of contact), or when the screen x crossed the center since the last
  point: `(x - W/2)(x_last - W/2) < 0`, with `W` from `0x00E85058`. It then
  needs a height `0 < h < 1.1 S` over the land, `+0xDC` over 1000 ms, and a
  speed over `5 S`, with `S = 15 * size` (`LH3DCreature+0x90`). A speed of
  `9 S` or more is a hard slap. The speed is the difference across the land
  (x and z only) of the current and the last screen point, both unprojected
  at the depth `+0x4854`, over the first argument `dt`. The sign of
  `x - x_last` gives the side of the slap (`0x005B1E15`). A stroke sets `+0xDC`
  to 0 (`0x005B226C`).
- **The camera mode under the Hand.** `CameraModeNew3::Update` drags the land
  only in hand mode `0x14` (`0x0045AB8D`). So with no `CtrInteract` over it,
  the held trigger in mode `0x13` does not drag the land.

### What the mod does

**No camera (`creature_camera`, default 2).** A hook on
`OperateInteractionCamera`. 0 never moves the camera, 1 is the game's camera
move, and 2 moves no camera while the controller placed the Hand on the last
frame or while the table or the God view owns the view. On the no camera path
the hook sets the flag `+0x49C0` around the call for mode `0x13`. The game
then writes the Creature to `+0x49B4` and `+0x49B8` and pushes nothing. When
the object under the Hand is that Creature, the mod takes the press and keeps
its start time (`QueryPerformanceCounter`). It holds the press to the end, even
when the owner of the pointer changes. At the first other mode, the press
ends. A press shorter than 450 ms of real time on the player's own Creature
sends packet `0x5F` with `SetPacket`, as the game does. The game's limit of
450 counts its frame time, which grows in both eye passes, so it was about
225 ms in stereo. With no mode pushed, `script_camera.cpp`, the God view, the
snap turns and the grab all run as usual. With `creature_camera=1` the hook
is not installed. For the mouse in play mode 0 it calls the game's function
and does nothing else.

**The Hand stays at the controller.** The depth hook on `SetDistanceFromView`
also acts in state 7. It writes `+0x4854 = |target - camera|` and `+0x4834 =
real size / size^0.5`, so the game's multiply gives the size of the real hand.
The ray hook already aimed `+0x485C` at the target in state 7. So the state
puts `+0x78` on the target, and the Creature's distance and the Hand's
distance are the same.

**The body.** The first Creature Update of a frame, before the game's code,
works out a vertical capsule through the body bone (`fn_004813D0`, thiscall
on the `LH3DCreature`, plain `ret`: bone `+0x51D8` through `GetSafeBuffer`).
It goes from the land under the bone up to `1.1 S`, the top of the game's slap
test. Its radius is the Creature's 2D radius (vtable `+0x64` of the Creature
object) and `creature_touch_cm` (5) of the room, at the scale now. The
Hand's point is in contact while it is in the capsule. The later Update of
the frame (the second eye) uses the same numbers.

**No touch at a distance.** A hook on the mesh ray test acts only inside a
Creature Update that the controller placed, and only for the two return
addresses above. While the Hand's point is outside the capsule, it returns 0
without a test. Inside, the game's own test runs, so the contact point and the
bone of a stroke stay the game's.

**The swept test.** When the Hand was outside the capsule on the last frame
and its path to this frame meets the capsule (`CapsuleEntered`), it went into
the body since the last frame. A fast slap can go through the body between two
frames and never be in it at a frame. The mod then forces the slap branch:
it sets the last x to the other side of the screen center (`2 * W/2 - x`).
The ray of the middle pixel then goes through the screen center, so the hook
refuses its hit in that frame. When the speed ray of the last point is cast,
after the test of the center and before the side is read, the mod puts the
last x back one pixel behind the current one.

**No slap by a head turn.** In every other first Update the last x is one
pixel behind the current x, on the side the hand comes from. Two integers next
to each other never cross the center, so a head turn cannot slap, and the
game reads the side of the slap from the hand. The second Update of a frame
keeps the game's own last point, which the game copied from the current one.

**The speed (`creature_slap_mps`, default 1.5).** The mod aims the game's ray
of the last point (`+0x4864`) at `target - u * s * k * v * dt`. `u` is level,
at right angles to the line from the eye to the target, and to the right as
the eye sees it (`SlapAcross`). `s` is the sign of the hand's velocity along
`u`. `v` is the real speed of the grip in m/s over the last 50 ms, in three
dimensions, so a slap down or forward counts too. `dt` is the game's step.
`k = 5 S / creature_slap_mps` (`SlapUnitsPerMps`). Both points are on the
sphere around the eye at the Hand's depth, so the game reads `k * v`, within
0.2 percent at the depths of a slap. A slap then needs `creature_slap_mps` of
real speed and a hard slap 1.8 times that, at any size of the Creature and
any scale of the world. A still hand gives the target itself, and speed
zero. The second Update of a frame gets the target too, so it reads zero, as
with the mouse.

**The wait (`creature_slap_wait`, default 0).** At the first Update of a
state, `+0xDC` goes up to 1000, so the first slap counts at once. With
`creature_slap_wait=1` the game's wait of 1 s stays. After a stroke the game
waits 1 s again before a slap, as before. [G for side effects.]

**The pick keeps the Creature.** While the controller places the Hand in
state 7, after each eye pass, the next pick record gets the Creature again
when it holds anything else (`RecordPick` `0x005D5E40`). The Hand's pixel
leaves the Creature in a slap that goes through it, and in a Hand pulled back
to the body. Without this the hand mode would go to `0xE` or 3 and end the
state in the middle of a slap. So the state lasts until the trigger opens, and
the time of the press is the time the trigger was held. The pick of holdable
objects (`Choose`, `Inject`) does nothing in state 7.

**The glow.** In state 7 the glow is green while the Hand's point is in the
body, and pale blue when it is not.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `creature_camera` | 2 | 0 never moves the camera to the Creature. 1 moves it as the game does. 2 moves it only for the mouse outside the table and the God view |
| `creature_slap_mps` | 1.0 | The real speed of a slap, in m/s: the larger of the speed of the grip and of the Hand's point (see "The open hand on the Creature"). 1.5 before 2026-10-03. A hard slap needs 1.8 times this. 0.2 to 10 |
| `creature_touch_cm` | 5 | How far the body reaches past the Creature's 2D radius, in cm of the room. 0 to 50 |
| `creature_slap_wait` | 0 | 1 keeps the game's wait of 1 s before the first slap |
| `creature_panel` | 2 | The Creature panel with `ui_panel=1`: 0 off, 1 fixed in the room, 2 near the Creature. See `ui-layer.md`, section 13 |

`creature_speed_gain_pct` is gone.

### The log

The tag `hand creature`:

```
hand creature: a press on the Creature 0F3A2C10. The camera does not move (creature_camera=2), frame 5120
hand creature: the Creature state starts, frame 5121. A slap can count at once.
hand creature: 30 frames in the state. The body is 3.63 units high and 2.81 across (2D radius 1.22 and 5 cm of the room). The Hand's point is 0.40 units inside it. hand+0x78 is 0.0004 units from the target, and the size +0x4834 is 0.4750 for the real hand's 0.4750. Creature size 0.220, k 11.00 units/s for 1 m/s
hand creature: a slap. The real hand moved at 1.84 m/s, which the game read as 20.2 units/s. A slap needs 1.50 m/s (16.5 units/s) and a hard slap 2.70 m/s (29.7). The Hand's point is 0.31 units inside the body, and it went into the body since the last frame. Total 0.30 to 0.20, dt 0.0111, frame 5190
hand creature: the press ends after 2480 ms (hand mode now 0x12). Not a short press, so no leash click, frame 5344
```

(Examples of the format. No run reached the Creature state yet.) The report
window adds the counts: presses with no camera move, leash clicks, states with
no wait, hits refused at a distance, slap branches forced by the swept test,
and frames the pick kept the Creature.

### Tests

**The math test** has 50 new checks (`CreatureChecks`): the capsule gap, the
distance of two segments, the swept test (through, stop inside, past at 0.8,
stay inside, come out, over the top, a graze at 1.19 of 1.2), the across axis
(level, at right angles, a unit, kept for a line straight down), and the
speed as the game reads it. That last one unprojects both points as the game
does: 1.4 m/s is no slap, 1.6 m/s a slap, 2.75 m/s a hard slap, at size 0.22
and at size 1.8 with the eye 72 units away. It also checks the room to the
world and back, the room to LOCAL against `head_pose.cpp`, the face of the
panel and the place near the Creature.

**Desktop runs: not run.** `desk_test.ps1` refused, because the PC was in use.
A run also needs a game with the player's own Creature: in the pen of the
Creature select the Creature does not have bit `0x10` of `Object+0x24`, so a
press does not start state 7 (see "M5 as built"). The fake controller has the
word `touchc` for these runs. It steps the hand onto the body bone of the
nearest Creature (`HandDirectTouchCreaturePoint`). The runs to do, with the
God view rig of `god-view.md` section 12 (`play_mode=4`, `fake_controller=1`,
`controller_hand=1`, `intro_vr=0`, `ui_panel=2`) and a game where the player
owns a Creature:

1. `touchc`, then the trigger held for 60 frames. Expect state 7, `+0x49C0`
   at 0, no "a story holds the camera" in the log, and no new God view.
   Expect "30 frames in the state" with `hand+0x78` within 0.01 of the target
   and `+0x4834` within 1 percent of the real hand.
2. At 60 frames a second, `touchc`, the trigger held, then `nudge` (2 cm in a
   frame, 1.2 m/s): a stroke after 1 s. A step of 3 cm (1.8 m/s): a slap.
   `slap` (12 cm): a hard slap. Check `+0x114 == 0x21` and `+0xD8`.
3. The hand 20 cm in front of the body on the line of sight: no contact, no
   stroke, and "hits refused at a distance" in the report.
4. A trigger press of 8 frames on the Creature: "the leash click (packet
   0x5F)", and the leash changes. A press of 60 frames: no click, and packet
   `0x59` on the end of the state.
5. With the mouse in play mode 0 and `creature_camera=1` (Ctrl F12): the
   owner log still shows `CameraModeCtrInteract`.

**Needs the headset.** That the camera stays still at a press on the
Creature. That a stroke and a slap feel right at 1.5 m/s and with 5 cm of
reach. That the size of the capsule fits the Creature's body. That the
Creature tutorial still goes on with no camera mode (it likely waits on the
packets `0x27` and `0x59`, not on the camera [G]). Fallback:
`creature_camera=1`.

**Not built.** Stage 2 of the design, the touch with no press: the Hand within
25 cm of the body would press the trigger for the player, and the release
past 35 cm. It waits for a headset test of the parts above.

## The exact Hand (2026-10-02)

Buck, after the headset test of the evening of 2026-10-02: "The hands you
added work great, but the in-game hand kind of follows the right hand around
but not exactly. I wonder if you can combine the 2? ... If we could match the
animations for different interactions it would be great, then we wouldn't
even have to render the in-game hand at all."

The research was Part 2 of `design-far-grab-one-hand.md`. This section
replaces it. The code is in `src/mod/hand_exact.cpp`, with the pure math in
`src/mod/hand_exact_math.h`.

### Why the Hand followed the controller only roughly [V]

1. **The origin.** The pose builder puts the matrix translation at the
   target (`0x0046E2CC`). The mesh's origin is the tip of the index finger
   (the glow runs, and the file below). The target was the grip point, so the
   whole Hand sat about one hand length in front of the real hand.
2. **The rotation.** The pose builder uses only the yaw `+0x84` (from the
   screen ray, `0x0046C725`) and an up vector that glides over 0.4 s:
   `A = normalize(d x u)`, `B = A x u`, `C = -u`, with
   `d = (sin(yaw + pi), 0, -cos(yaw + pi))` (`0x0046E193` to `0x0046E2C7`).
   Then `fn_0046D9D0` multiplies the scale matrix of `fn_0046D080` by these
   rows. The controller's rotation was never used.
3. **The blend.** After each change of state, `fn_0046C500` swaps the pose
   arrays `+0x47F0` and `+0x49AC`, sets the timer `+0x49B0` to 0 and copies
   `0x00C5E680` to `0x00C5E670`. For 0.13 s `PrepareForDrawing` then mixes
   each float of the old and the new pose (`0x0046C932` to `0x0046CA28`), and
   the drawn position `0x00C5E660` too. The pose arrays hold the world matrix
   of each bone, so a Hand that moved glides from its old place.
4. **The size.** The game's mesh scale is
   `k = 3.2 / (+0x8C) * (+0x4834) * (+0x90)`, written to `+0x94`
   (`fn_0046D080`).

### How the game draws the Hand [V]

- **The files.** `Data\CTR\hh.HBN` names `hand_boned_base2`, `evil2` and
  `good2`, which are `Data\CreatureMesh\*.l3d`. Each has one submesh of 141
  vertices, 169 triangles and 22 bones, and a texture. They load through
  `CHand::LoadBinary` `0x0046D2C0` and `Morphable::ReadBinary`.
- **The bones.** Bone 0 is the wrist. Bones 4, 17, 18 and 19 are the index
  finger, 7, 8, 9 and 10 the middle finger, 5, 14, 15 and 16 the ring finger,
  6, 11, 12 and 13 the little finger, and 3, 20 and 21 the thumb. Bone 1 has
  no vertices. It is 130 units out of the palm, so it is likely the point of
  the spell in the Hand [G].
- **The stored mesh is a left hand.** +y goes from the wrist to the finger
  tips, +z out of the palm (the thumb and bone 1 are on that side) and +x
  toward the thumb. `CHand::LoadBinary` calls `ToggleLeftRight` once
  (`0x0046D56F`), which turns the winding of the triangles and sets `+0x484C`.
  `fn_0046D080` then mirrors x, so the Hand that the game draws is a right
  hand. The game's own left handed option toggles it back.
- **The pose.** Each Hand state builds a root matrix, then samples its
  animation into the pose array `*(+0x47F0)` (`fn_00860E00` or
  `fn_00861EE0`), then composes the bones with the root: `fn_00839F10`, cdecl
  `(LHMatrix* pose, anim, const LHMatrix* root)`, plain `ret`. The symbol
  file calls it `LH3DTech::GetCameraPosition`. Each bone becomes
  `bone * parent`, and bone 0 takes the root. The states that call it: Camera
  `0x005B0B42`, Citadel `0x005B131B`, Creature `0x005B2A49`, Holding
  `0x005B480E` and `0x005B565C`, MultiPickUp `0x005B5B6C`, Normal
  `0x005B7443`, PlayAnim `0x005B75E9`, Totem `0x005B7BFE` and Tug
  `0x005B8DEE`. The pose builder writes a root on the stack of the Normal
  state (`0x005B725A`), not the pose array. The Creature state does not call
  the pose builder.
- **The draw.** `PrepareForDrawing` copies bone 0's world matrix to the mesh
  at `+0x14` (`0x0046CAE5`), `+0x94` to the mesh at `+0x44` and the pose array
  pointer to the mesh at `+0x80`. `CHand::Draw` calls the mesh's vtable
  `+0x108` (`0x0046D27C`).
- **The spell in the Hand.** `CHand::DrawSpellInHand` `0x0046E680` takes its
  place from a bone of the pose array (`fn_0083A0E0`) when the byte `+0x4954`
  is set, and from `+0x78` when it is not. Its size is `+0x4834`.
- **`+0x8C`.** `Morphable::LoadBase` writes the return value of
  `LH3DAnim::SetTransform` (`0x00618602`). That is the span in y of the bone
  joints, with 0 in it. For the stored bones it is 555.29: the wrist joint to
  the index tip along y. It is not the length of the hand: the wrist joint to
  the end of the middle finger is 623.6 units, 12% more. So with the game's
  `k` a Hand of `hand_real_cm` 19 was about 21.3 cm long.

### What the mod does

**The seam.** A hook on `fn_00839F10` acts only on the Hand's own pose
array (`*(CHand+0x47F0)`). While the controller places the Hand, it gives the
compose the exact root in place of the game's root. The state's own root and
the game logic that reads it stay as the game made them. Only the drawn
bones and a spell on a bone follow the exact root. The
seam reaches every state, the Creature state too, so the pose builder hook of
`hand_direct.cpp` needs no change.

**The root** (`ExactRoot` in `hand_exact_math.h`). From the pointing grip's
axes in the world `R = L * M_grip`, of the frame of `controllers.cpp`:

```
f = R (-y_grip)                   the open fingers
b = R (+x_grip), -x for the left hand     the back of the hand
F = [b x f, f, -b]                columns
M = diag(s, 1, 1)                 s = -1 while +0x484C is set
row j = k F M Q e_j,  row 3 = the point
k = 3.2 * size / 623.6            mesh units to world units
```

`Q` is the rest turn of the mesh (below). The game's relations `B = A x u`
and `C = -u` stay, with `u = b`, and so does the sign of the determinant.
`size` is the Hand's size of `hand_direct.cpp`: the real hand at the scale
now, or `table_hand_cm` on the table. So the Hand from the wrist joint to the
end of the middle finger is `hand_real_cm` long, not 12% more.

**The point.** With `hand_exact=1`, `controllers.cpp` moves the Hand's point
from the grip to the tip of the real index finger: the overlay's index tip at
the rest curl of 0.12, 8.8 cm from the grip for a hand of 19 cm. The trigger
does not move the point, so a press does not move the Hand. The pick, the
glow, the land test, the render pixel and the held object all use the point,
so the glow and a touch are at the real index finger. The throw keeps the
motion of the grip. The fake word `seek` puts the tip, not the grip, on the
object.

**No lag.** The hook keeps the root of the last compose of each pose array.
While a blend runs, an exact compose moves the old pose to the new root
(`Rebase`: each bone matrix `m' = m * inverse(old root) * new root`). The
bones keep their place in the Hand's own frame, so the blend mixes only the
fingers, as the game does, and the Hand does not glide. In the Normal and the
Holding states the hook also sets `0x00C5E670` to `0x00C5E680`, so the held
object does not lag either. With the mouse nothing is moved, and the game's
blend stays.

**The states.**

| State | The drawn Hand | The game's position `+0x78` |
| --- | --- | --- |
| 1 Normal, 4 Holding | exact | the point (the hooks of "M3 as built") |
| 7 Creature | exact | the point (the depth hook of "The Creature by touch") |
| 2 Camera (the land drag), 3 Tug, 8 Grain (a Miracle in the Hand), 9 PlayAnim | exact | the game's. Its logic (the land under the drag, the pull of the Tug, the cast of the Miracle) keeps it |
| 0 Invisible, 5 Totem, 6 MultiPickUp, 10 Citadel | the game's | the game's |

In the states of the third row only the drawn Hand moves to the controller.
In the Tug the game pulls the object toward a point on the line of sight
through the Hand, so the drawn Hand and that point stay on one line from the
eye. A spell of the Grain state on a bone follows the drawn Hand. A spell
placed at `+0x78` does not [G]. The Totem state was not read, and the Citadel
state draws from another camera.

**The overlay.** With `hand_exact=1` and `overlay=0` the overlay draws only
the other hand: the left hand, or the right hand for a left handed player
(`controller_left_handed=1`). With `overlay=1` it draws both, so the game's
Hand and the test hand can be compared.

Since 2026-10-03 the mesh of the game's Hand is hidden by default, and the
overlay draws both hands. See "One pair of hands" below. This paragraph is
the case of `hand_hide_game=0`.

**The left handed player.** The game's Hand goes to the pointing controller,
which is the left one with `controller_left_handed=1`. While `+0x484C` is set
the game's Hand is a right hand, so on the left controller its palm and
fingers fit and its thumb is on the other side. The log says so once. The
game's own left handed option makes the Hand a left hand.

### The rest turn of the mesh

The rest turn `Q` turns the stored mesh about its index tip before the
mirror, so the mirrored Hand keeps it. In the stored frame:

- `hand_mesh_yaw_deg`: about the palm normal. Plus turns the fingers toward
  the little finger.
- `hand_mesh_pitch_deg`: about the thumb axis. Plus turns the fingers toward
  the palm.
- `hand_mesh_roll_deg`: about the fingers. Plus turns the palm toward the
  thumb.

`Q = Ry(roll) * Rx(pitch) * Rz(yaw)`, so the yaw comes first, and the pitch
and the roll turn the line that the yaw put on +y.

**Measured in the file.** The line from the wrist joint (bone 0,
`-229.27 -555.29 -4.86`) to the end of the middle finger (`-110.34 56.79
1.45`) leans 11.0 degrees from +y toward the thumb and 0.58 degrees toward
the palm. So the defaults are a yaw of 11.0 and a pitch of -0.58. Then that
line goes along the controller's finger axis. The palm and the knuckle line
lie flat in the stored mesh, so the roll is 0.

**Measured in the game** (run ex2). The log takes the drawn bones back into
the stored mesh frame through the inverse of the exact root, 30 passes after
a blend. In the Normal state the wrist is at `-229.3 -555.3 -5.0` and the
index finger's last joint at `-29.5 -61.0 -18.2`: the bones of the file. Only
the middle, ring and little fingers bend toward the palm (the middle finger's
last joint is at `-124.8 -28.5 21.7`, against `-123.7 -18.6 -19.0` in the
file). The line from the wrist to that joint leans 11.2 degrees toward the
thumb (11.1 in the file), and the knuckle line tilts 0.1 degrees (0.1 in the
file). So the Normal animation keeps the hand of the file, and the defaults
fit it. In the Camera state (the land drag) the game's grip animation curls
the fingers, and the wrist moves to `-193.7 -487.6 -136.0` from the index tip.

**Against the overlay's hand** at the same point: the drawn wrist is 2.5 cm
from the overlay's wrist, the index finger's last joint 0.7 cm from the
overlay's, and the line from the wrist to the middle finger is 4.5 degrees
off, in every steady check of runs ex1 and ex2. Most of the 4.5 degrees is
the bent middle finger of the game's animation.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hand_exact` | 1 | 1 puts the game's Hand on the real hand: its index tip at the real index tip, turned with the controller, the real size. 0 turns it with the screen ray as before, and the point is the grip |
| `hand_mesh_yaw_deg` | 11.0 | The rest turn of the mesh about its palm normal, in degrees |
| `hand_mesh_pitch_deg` | -0.58 | The rest turn about its thumb axis |
| `hand_mesh_roll_deg` | 0 | The rest turn about its fingers |
| `hand_left_mesh` | 0 | Does nothing. The hand model draws the left hand ("The game's hand on both real hands") |
| `fake_controller_rots` | | Test key. `yaw,pitch,roll` in degrees, separated by spaces. The script word `rot` turns the fake right hand to the next one |

The keys are read from `bwvr.ini` at the start. They are not in the
launcher.

### The log

The tag `hand exact`. At the first exact Hand: the game's `k` and the exact
`k`, `+0x8C`, `+0x90`, `+0x4834`, the mirror and the bone count. Once: the
drawn bones in the stored mesh frame, against the bones of the file, and the
leans of the hand's line and of the knuckle line. At each change of state,
for 3 passes, and every 600 frames (60 lines at most): the drawn wrist and
the index finger's last joint against the overlay's hand, in cm of the room,
the angle between the two lines from the wrist to the middle finger, and how
far the wrist moved in the Hand's own frame since the last pass. A Hand that
lags moves there, a Hand on the exact root does not. The report window counts
the exact composes, the composes as the game made them, the old poses moved
to the new root, and the frames with the wrist moved more than 0.5 cm.

### Tests

**The math test** has 26 new checks (`ExactChecks` in
`test/controller_math/exact_checks.h`), 898 in all. At six grip turns and
for both hands: `(A, B, C) / k` is orthonormal, the determinant is below 0
for the right Hand (`+0x484C` set) and above 0 for the left one, and it has
the sign of the game's own root for each mirror. The mesh's line from the
wrist to the middle finger is along `f` within 0.02 degrees and as long as
the real hand. The palm faces away from the back of the hand. The thumb is on
the thumb side of each hand. The origin is the point. Each rest turn moves
the side it says, and 8 more degrees of pitch put the line 8.0 degrees off
`f`. The point is the overlay's index tip of each hand, 8.8 cm from the grip,
and the trigger does not move it. An old bone moved to the new root is the
new bone, so the blend does not move it, and without the move it starts 7
units away.

**Desktop runs** (ex1 to ex3: the God view rig of `god-view.md` section 12,
`play_mode=4`, `fake_controller=2`, `controller_hand=1`, `intro_vr=0`,
`overlay=1`, the start dialog as there, 100 units per meter at the pen. The
word `rot` turned the right hand through `90,0,-90` (palm down, fingers
forward), `0,0,0` (fingers down, palm to the left), `0,90,0` (fingers
forward, thumb up), `-90,0,90` (palm up) and `30,45,-60`, with an eye
capture at each):

- ex1: `+0x8C` is 555.29, as the stored bones give. The game's `k` was
  0.034216 and the exact `k` 0.030468 (12% less), with `+0x90` 1 and
  `+0x4834` 5.94. The Hand followed each turn with its index tip on the glow.
  Two faults: at the end of the story that hid the Hand, the blend started
  from an old pose that the hook had never seen, and the Hand was 8.3 m from
  its place on that frame. And the first measurement of the bones came
  during that blend. Now a blend with no known root ends at once, and the
  measurement waits for 30 passes with no blend. The right fake hand was in
  a hill there, so the land test lifted the point 35 cm onto the hill.
- ex2: the blend at the end of the story ended at once (log: "the blend ends
  at once"), and no pass moved the wrist more than 0.57 cm in the Hand's
  frame. The bones of the Normal state are the bones of the file (above). A
  trigger press on the land gave the Camera state (2) with the exact Hand.
- ex3: the hand at eye height, over the land. At each turn the Hand's point
  was 0.0 cm from the overlay's index tip, and in the pictures the game's
  Hand lies inside the overlay's test hand at each turn: the fingers along
  the test hand's fingers, the palm on its palm side, the thumb on its thumb
  side. A turn of 90 degrees on the frame of each change of state (Normal to
  Camera and back) moved the old pose to the new root twice. One pass later
  the index finger's last joint was 1.5 cm from the overlay's, against 0.7
  cm when steady. The rest is the game's grip animation, which moves the
  wrist by up to 6 cm from the index tip.

- ex4: `play_mode=0` is now the God view (log: "play_mode=0, the immersive
  mode, is now the God view"), so this was a fourth God view run. Two turns
  while the Hand was still: the point was 0.0 cm from the overlay's index tip
  and no pass moved the wrist in the Hand's frame. The word `seek` found no
  holdable object within 50 cm of the room at the pen, as in runs gr1d, bk2
  and bk3.

**Not reached on the desktop:** the Tug, the Holding, the Creature, the Grain
and the PlayAnim states, so the exact Hand there and the held object with no
lag are not seen yet. The math and the seam are the same as in the Normal and
the Camera states.

The test hand and the game's Hand differ in the fingers only: the game
plays its animations (the wiggle of the Normal state, the grip of the land
drag) and the test hand follows the trigger and the grip.

### Not built

Since 2026-10-04 the mod draws both hands with the game's mesh in the
overlay ("The game's hand on both real hands"). That is plan B below, and it
gives the left hand of A2. The game's own Hand keeps its bones for the held
object, the spells and the Creature, and its mesh stays hidden.

- **A2, the left hand from the game's mesh.** The mesh drawn a second time,
  mirrored, at the left controller (`hand_left_mesh`). The draw is deferred
  (`CHand::AddDrawing` puts `CHand::Draw` in the z sorter, `0x0046D1F8`), and
  the pose array holds one pose, so a second draw needs its own pose array
  and its own sorter entry. Until then the overlay draws the left hand.
- **B, the mesh in the overlay.** The measured plan is in "One pair of
  hands", "The full skin". An L3D reader, the bones and weights, the
  good and evil blend, a textured draw and the HBN animations. The game's Hand
  must stay exact for the held object, the spells and the Creature, so B only
  helps the left hand.
- **The fingers of the animations.** The game's animations keep their own
  fingers. The trigger and the grip do not bend the game's fingers.

**Needs the headset.** Whether the Hand sits on the real hand, with each
runtime's grip pose. The rest turn. Whether the Hand in the land drag, the
Tug and the Grain state looks right at the controller. Whether a left handed
player needs the game's left handed option.

## One pair of hands (2026-10-03)

Buck, after the headset test of build a785a15: "The position and size of the
god hand is much closer to the controller hands now, but in some cases it
still flips out, like grabbing certain things will make the god hand rotate at
weird angles ... I'm still leaning towards hiding the god hand and only using
the player hands instead ... I like how the player hand is sensitive to
trigger and grip sensitivity too ... I am still seeing both at the same time
now and maybe that breaks immersion a bit too."

So the mod's hand is now the hand. The game's Hand gives only its bones, and
its mesh is not drawn. The code is in `src/mod/hand_exact.cpp` (the game
side), `src/mod/overlay.cpp` (the poses) and the math in `overlay_math.h` and
`hand_exact_math.h`.

### Where the Hand turned away, and why

The lines of the tag `hand exact` in his log give the drawn bones against the
mod's hand in each state:

| State | The drawn wrist from the real wrist | The hand's line off | What it is |
| --- | --- | --- | --- |
| 1 Normal, steady | 2.5 cm | 4.5 degrees | the bent middle finger of the animation |
| 2 Camera (the land drag) | 1.5 to 2.0 cm | 4.2 degrees | the grip animation |
| 3 Tug (the pull of a far grab) | 1.5 to 3.4 cm | 4 to 7 degrees | |
| 4 Holding, steady | 13.9 to 14.2 cm | 33.4 to 34.2 degrees | **the Holding animation** |
| 1 Normal, for 0.13 s after a release | 12 to 14 cm | 28 to 33 degrees | the blend from the Holding pose |
| 1 Normal, the first frame after a dialog box (frame 7563) | 26.8 cm | 18.5 degrees | a blend from a pose that the game placed |

In the Holding state the bones in the stored mesh frame were: the wrist at
`69.9 -285.4 178.4` (the file: `-229.3 -555.3 -4.9`), and the knuckle line
turned 78.9 degrees toward the palm. So the Holding animation moves **the
wrist bone itself**: it turns the whole hand 79 degrees about its fingers, as
a hand that grips an object from the side, and it moves the wrist 14 cm. The
exact root puts only the root on the real hand. The compose then gives
`wrist of the animation * root`, so each animation that moves the wrist bone
moves the whole Hand. Each hold showed it, for each object. The Grain and the
PlayAnim states did not occur in his session.

The held object did not turn: the game puts only its translation, at the
Hand's point plus the hang offset, about 1 cm below the index tip.

### What the mod does

**The wrist lock** (`hand_wrist_lock`). Before the compose, the first matrix
of the pose array is the wrist bone of the animation in the frame of the
root. The hook gives the compose this root:

```
root' = inverse(wrist of the animation) * (wrist of the file) * (exact root)
```

Then `wrist * root'` is the wrist of the file under the exact root, in each
animation, and only the fingers move. The wrist of the file is bone 0 of
`Hand_Boned_Base2.l3d`: a turn of 17.27 degrees and the place `-229.27
-555.29 -4.86`. A spell on a bone and an effect on a bone now follow the real
hand in each exact state. The records for the move of an old pose keep the
exact roots, and the same move works for a locked pose, because the factor of
the lock is the same then and now. The lock works only after the Normal
animation showed the wrist of the file in the first matrix (within 3 degrees
and 20 mesh units). If it does not, the log says so and the lock stays off.

**A blend from a pose that the game placed ends at once.** That was the Hand
27 cm away for one frame after a dialog box.

**The mesh is hidden** (`hand_hide_game`, default 1) [V]. `CHand::Draw`
`0x0046D210` returns at once in three cases (`+0x49A0` is 1, the byte
`0x00C4CCEE`, or the interface says no). With `+0x47BC` set it calls the
mesh draw at `0x0046D258` and returns. In each other case it needs a state
that is not 0 and `+0x483C` clear, calls the mesh draw at `0x0046D27C`, and
then draws `+0x4948` (vtable `+0x10`), `GVirtualInfluence::Draw` (the ring)
and `CHand::DrawSpellInHand`. Both mesh calls are `call [vtable + 0x108]` on
the mesh object `CHand+0x482C`. That object comes from
`LH3DObject::Create(3)`, its vtable is `0x009A3068`, and `+0x108` there is
`fn_00813BA0`, thiscall with no arguments and a plain `ret`. No other vtable
points at it.

A MinHook hook on `fn_00813BA0` skips the draw when three things are true:
the return address is `0x0046D25E` or `0x0046D282`, the object is the Hand's
mesh, and the controller places the Hand in an exact state while the overlay
drew the pointing hand in this frame. So one hand is always there, each
other part of `CHand::Draw` still runs, and the mouse player sees the game's
Hand as before. MinHook puts the bytes back when the mod unloads. The hook is
made only when the first bytes of the function, the two call sites and the
vtable slot are as the disassembly gave them. In the states Invisible, Totem,
MultiPickUp and Citadel the game's Hand is not exact, so its mesh stays
drawn.

**Both hands.** With the mesh hidden the overlay draws both hands, with
`overlay=0` too.

**The held object in the palm** (`hand_hold_palm`, `hand_hold_turn`). After
`CHand::PrepareForDrawing` wrote the object's translation
(`0x0046CA50` to `0x0046CA97`, only in the Holding state, and not while the
release physics has the object), the mod writes the object's mesh matrix
(`Object+0x40`, then `+0x14`, 4 rows of 3):

- The body of the object is taken as a ball. Its radius is the larger of the
  object's 2D radius (vtable `+0x64`) and half of the hang offset
  `0x00D13F20`. Its middle is half of the hang offset before the origin.
- The ball touches the palm of the mod's hand, 17% of the hand's length
  behind the knuckles, in the middle of the palm's width.
- The object turns with the hand from the pose it had when it came into the
  hand: each row of its matrix is turned by `R now * transpose(R then)`. If
  the game gives the object new axes, the mod turns those.
- On the first frame the object is where the game put it. It moves to the
  palm in 0.12 s, in the frame of the hand, so there is no jump when the
  Holding state starts.
- The place is made once a frame, and both eye passes write the same matrix.
- The glide of a far grab and the first frames of a throw run after this.
  The glide ends at the place in the palm, and a throw starts from it.

The game's own position of the Hand does not change, so the pick, the drop
point on the land and the throw are as before.

**The poses** (`overlay_math.h`, THE POSES). A pose gives each finger a range
of curl, and the player's own finger moves the curl in that range: the
trigger for the index finger, the grip for the middle, ring and little
fingers, and the larger of the two for the thumb. The thumb now has a curl:
at 0 it rests on the top of the controller, and at 1 it lies across the
front of the closed fingers.

| Pose | Which hand, and what gives it | The fingers |
| --- | --- | --- |
| hold | the pointing hand, while the game's Hand holds an object in the Holding, the Tug or the Grain state | each finger closes until it touches the ball of the object. A large object opens the hand. An object smaller than 10% of the hand counts as 10%, so the hand stays a cup |
| point | the pointing hand, while the teleport aims, a far grab has a target, or the ray of the menu shows. Another source calls `OverlayHandPoints(hand)` in each frame of its aim | the index finger straight (a curl of 0 to 0.18 with the trigger), the thumb and the other fingers closed |
| flat | the pointing hand in the Creature state with the grip open (a stroke) | each finger open, a curl of 0.2 at most |
| fist | the Camera state (the land drag), the Creature state with the grip closed, and each hand with the grip closed on nothing (from 85%, until it opens to 60%) | each finger closed, a curl of 0.82 at least |
| relaxed | each other time | the first test hand: the rest curl of 0.12 to a fist |

The order is hold, point, flat or fist of a state, fist of the grip,
relaxed. Each pose has a weight. The weight of the pose in use grows by
`dt / 0.1 s`, so a change of pose takes 0.1 s, and the trigger and the grip
move the fingers in the same frame. The overlay chooses the pose once a
frame, before the eye passes, from the game's state of the last frame.

**The animation number.** Each hand state asks `Morphable::GetAnim`
`0x00619650` (thiscall, the number first, `ret 8`) for its animation. A hook
keeps the last number that the Hand asked for. The log gives it with each
state and each pose. The numbers in the code of the states are 0, 1, 2, 6, 9
and 60, and no pose is chosen by the number. Their names are in "How the game
poses the Hand": 0 is `wiggle`, 1 and 2 are its leans, 6 is `hold_above`, 9
is `hold_side` and 60 is `horn`.

**The tint** (`hand_tint`). `CHand::PrepareForDrawing` writes the player's
alignment to `CHand+0x9C` each frame: `GPlayer::GetAlignmentValue`, cut to
-1 and 1 (`0x0046C63A` to `0x0046C66D`) [V]. -1 is evil and 1 is good:
`GAlignment::GetDiscreteAlignmentValue` maps -1 to `ALIGNMENT_DEVILISH` and 1
to `ALIGNMENT_ANGELIC` [V]. `Morphable::MorphTexture` `0x00619500` mixes the
texture of the base mesh with one other mesh by this value. The mod's hands
take a skin color from it: `0.90 0.70 0.57` at 0, `0.98 0.91 0.80` at 1 and
`0.42 0.11 0.09` at -1, in a straight line. Since 2026-10-04 this tint colors
only the low poly hands (`hand_model=0`). The hands of the hand model have
the textures and the mesh of the game for the alignment.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hand_hide_game` | 1 | 1 hides the mesh of the game's Hand while the controller places it, and the overlay draws both hands. 0 is the build before |
| `hand_wrist_lock` | 1 | 1 keeps the wrist bone of the game's Hand at the real wrist in each animation |
| `hand_hold_palm` | 1 | 1 puts the held object in the palm of the mod's hand. 0 leaves it at the Hand's point |
| `hand_hold_turn` | 1 | 1 turns the held object with the hand |
| `hand_tint` | 1 | 1 gives the mod's hands the color of the player's alignment |

With `test_keys=1`, the minus key of the number pad shows and hides the mesh
of the game's Hand while the game runs. The pointing controller gives a long
pulse for hidden and a short pulse for shown. The keys are read from
`bwvr.ini` at the start. They are not in the launcher.

### The log

The tag `hand exact`: the keys at the start. Once for each state: the
animation number, and how far the wrist bone of the animation is from the
wrist of the file, in degrees and in cm. Once: the first hidden draw. At
each new hold (40 at most): the object, the ball of its body, the place that
the game gave, the place in the palm and the distance between them. The
report window counts the composes with the lock, the hidden draws and the
frames with an object in the palm. The lines of the drawn wrist against the
mod's wrist stay, so a hold must now show about 2.5 cm, not 14 cm.

The tag `overlay`: each change of pose (60 at most), with the hand, the
game's state, the animation number, the body of the held object, the trigger,
the grip and the alignment.

### Tests

**The math test** has 67 new checks (`hand_pose_checks.h`), 1297 in all with
the checks of the Creature commands:

- The thumb at a curl of 0 is the thumb of the first test hand. It moves
  with its curl, it ends in front of the closed fingers, and a step of 1/20
  of curl moves its tip less than 3% of the hand.
- The relaxed pose gives the curls of the first test hand.
- In the point pose the index finger stays out at each trigger value and its
  tip is far out along the fingers. The flat hand stays open and the fist
  stays closed at each value.
- In each pose and at each value, each segment keeps its length, and each
  joint and tip is less than 80% of the hand's length from the palm.
- The hold pose: each finger opens more for a larger ball, no finger is in
  the ball at the contact, and one step more of curl is in the ball. A small
  object counts as the least radius. With the grip closed the finger is at
  the contact.
- The blend: the weights sum to 1, a frame of 1/90 s moves no curl more than
  its share, the blend takes 9 frames, a change of the target in the middle
  makes no jump, and the trigger moves the index finger in the middle of a
  blend.
- The held object: the ball touches the palm for both hands at a turned
  grip, behind the knuckles and in the middle of the width. The origin is
  half the hang from the middle. The turn since the object came into the
  hand, the axes with no turn, and an axis along the fingers that stays
  along the fingers. On the first frame the object is where the game put it,
  the offset only gets smaller, and a frame takes less than 15% of it.
- The wrist lock: a wrist that is turned 79 degrees and moved becomes the
  wrist of the file under the exact root. An animation that keeps the wrist
  of the file keeps the exact root. An old pose with the lock, moved by the
  exact roots, is on the new hand.
- The tint at -1, 0, 0.5 and 1, and past the limits.

**Desktop runs.** None yet. Three tries of run hp1 were refused, because the
PC was in use. The run to do (`hands_run.ps1` in the scratchpad): the start
dialog of `god-view.md`, section 12, `far_grab_max_m=0`,
`far_test_influence=1`, `far_test_no_trees=1`, the points `-14,2,100
-15,-11,100 -10,-10,60 -10,-4,66`, the turns `90,0,-90 0,0,0 0,90,0 -90,0,90
30,45,-60`, and `point@2990+1 shot@2996+1 aimfar@3000+1 shot@3060+1
grip@3100+440 pull@3140+3 shot@3235+1`, then five times `rot` and `shot`, and
`shot@3600+1` after the grip opened. Check in the captures: no game Hand, both
of the mod's hands, the point pose at the far aim, the rock in the palm at
each turn, and the hand relaxed after the release. Check in the log: the wrist
of the Normal animation 0.0 degrees from the file's, the wrist of the Holding
animation about 79 degrees, and the drawn wrist about 2.5 cm from the mod's
wrist in the Holding state. Run hp2 is the same with `hand_hide_game=0` and
`overlay=1`, to see the game's Hand with the wrist lock beside the mod's hand.

**Not tested in the game:** the hook of the mesh draw, the wrist lock in each
state, the object in the palm, the glide and the throw from the palm, and
the poses with a real controller.

**Limits.**

- A set down puts the object on the land under the Hand's point (the index
  tip), not under the palm, so the object can move a few cm when the hand
  opens. A throw starts from the palm.
- When the game's physics takes a thrown object, the object takes the axes
  that the game has for it.
- A spell that the game puts at `+0x78` (not on a bone) does not follow the
  palm in the states where the game keeps its own position.
- The shadow of the game's Hand on the land still draws, with the game's
  fingers.
- The left handed player has the game's right Hand on the left controller.
  With the mesh hidden that no longer shows.

**Needs the headset.**

1. Look at both hands with nothing held. There must be one right hand, with
   no second hand in it or near it. Press the trigger and the grip: the
   fingers and the thumb must follow at once.
2. Pick up a rock, a villager and a tree. The object must sit in the palm
   and turn with the wrist, with no jump when the hand closes and when a far
   grab arrives. Say for which object the place or the size of the hand's
   opening looks wrong.
3. Aim the teleport, aim at a far object, open a menu, stroke the Creature
   and drag the land. The hand must point, lie flat or make a fist, and go
   back in about a tenth of a second.
4. With `test_keys=1`, press the minus key of the number pad while an object
   is in the hand. The game's Hand shows again. Its wrist must be at the
   real wrist, not turned away as before.

### The full skin: a measured plan

Built on 2026-10-04: see "The game's hand on both real hands". This plan
stays as the record of the first measurement. The aim was the game's own
Hand mesh on both of the mod's hands, with the fingers from the trigger and
the grip.

**What the three files hold** (measured in `Data\CreatureMesh`, read only):

| | `Hand_Boned_Base2.l3d` | `Hand_Boned_Good2.l3d` | `Hand_Boned_Evil2.l3d` |
| --- | --- | --- | --- |
| Size | 138,158 bytes | 138,158 bytes | 138,158 bytes |
| Header | `L3D0`, flags `0x00022316`, 7,082 bytes of mesh data | the same | the same |
| Submeshes and primitives | 1 and 1 | 1 and 1 | 1 and 1 |
| Vertices | 141 | 141 | 141 |
| Triangles | 169 | 169 | 169 |
| Bones | 22 | 22 | 22 |
| Bone groups | 20 | 20 | 20 |
| Vertex blends | 0 | 0 | 0 |
| Textures | 1, 256 by 256, 16 bits | 1 | 1 |
| Mean color of the texture | `0.43 0.26 0.08` | `0.59 0.53 0.33` | `0.23 0.04 0.04` |
| Vertices against the base | | the same (0.0 units) | up to 47.9 units away |

A vertex is 32 bytes: a position, a texture point and a normal. The texture
points are the same in the three files. A bone is 60 bytes: the parent, the
first child and the next bone as three ints, then a matrix of 4 rows of 3 in
the frame of the parent. A bone group is a count of vertices and a bone, so
each vertex has one bone and there are no weights. The vertices are in the
frame of their bone. A texture is an id and 131,072 bytes (4 bits each for
alpha, red, green and blue). The fingers: bones 4, 17, 18 and 19 are the
index finger, 7 to 10 the middle finger, 5, 14, 15 and 16 the ring finger,
6, 11, 12 and 13 the little finger, and 3, 20 and 21 the thumb.

**The steps**, each with an eye capture on the desktop:

1. **A reader.** Read the three files at the start: the vertices, the
   triangles, the bones, the bone groups and the texture. About 60 lines,
   and a check of each count against the table above.
2. **The rest pose.** Compose the bones as the game does (`bone * parent`),
   put each vertex through its bone, mirror x for the right hand and turn
   the winding. Place the result with `ExactRoot`, which already gives the
   size (623.6 mesh units are one hand) and the rest turn. Draw it with the
   plain skin color in place of the mod's hand. 141 vertices and 169
   triangles for each hand are less than the 330 triangles of today.
3. **The fingers.** Turn each finger bone about its bend axis by the curl of
   the pose, with the angles of today (75, 95 and 55 degrees at a full
   curl). The bend axis of each bone must be measured from its matrix. The
   thumb needs its two sets of directions as bone turns.
4. **The texture.** The overlay draws with the vertex format `0x1C4`, which
   has a texture point, and it leaves the game's texture bound. It needs a
   texture of its own on the game's Direct3D 7 device, from the 16 bit
   data, and stage 0 must multiply the texture by the lit color. The draw
   must put the texture and the stage states back.
5. **The blend.** For the vertices: the base and the evil file, mixed by the
   alignment below 0. The good file has the vertices of the base. For the
   texture: mix the base with the good or the evil texture on the CPU when
   the alignment changes by more than a small step (65,536 texels), or read
   the texture that `Morphable::MorphTexture` already mixed. Which of the
   two other meshes the game takes for which sign was read on 2026-10-04:
   the evil mesh below 0 and the good mesh from 0 ("How the game changes the
   look").

**Open.** The game's finger animations (the HBN file) are not needed for
this plan, because the fingers come from the controller. The seam between
the wrist and the arm: the mesh ends at the wrist, as the mod's hand does.

## The grab from a distance (2026-10-03)

Buck, after the headset test of the evening of 2026-10-02: "We need
something similar to the teleport button to allow grabbing things far away
instead of only grabbing what you can physically touch ... simply pointing
your hand at a grabbable object will create a glow around it and squeezing
the grip and pulling towards yourself would either throw the object towards
the player or just teleport it into your hand ... B&W throwing physics were
designed with a mouse in mind so they don't feel natural."

The research was Part 1 of `design-far-grab-one-hand.md`. This section
replaces it. The code is in `src/mod/far_grab.cpp` (the states, the claim,
the pull and the glide), `hand_direct.cpp` (the cone, the choice, the pick
and the throw), `controllers.cpp` (the ray, the claim and the fake words),
`overlay.cpp` (the tether) and `hand_glow.cpp` (the halo). The pure math is
in `controller_math.h`.

### What the player does

1. Point the hand at an object that is out of reach, and at most 2 m of the
   room from the head. A gold halo shows around the object, and the
   controller gives a short tick. The halo is red when the land under the
   object is outside the influence. Then the Hand cannot take the object.
2. Close the grip. A thin gold line goes from the Hand to the object (the
   lock), and the controller gives a firmer tick.
3. Pull the hand toward the head. The object glides to the Hand in 0.3 to
   0.5 s, and the grip carries it as it carries an object that the Hand
   touched. Open the grip to let go or to throw.

A grip that opens before the pull ends the lock and does nothing else. The
player never moves. Only the object moves. The Hand stays on the real hand.
The feature acts in the God view and on the table (play modes 3 and 4). It
acts only while the controller places the Hand, the Hand is empty in its
Normal state, and no grab of the world, gesture, teleport or trigger runs. It
is off in a multiplayer game.

A pile of a store, a field and a fish farm are targets too, and they need no
pull. See "A bulk source at a distance".

### What the game allows [V]

- **The pick up has no distance test.** `fn_005D1740` (thiscall on
  GInterface, the object as its argument, `ret 4`, called at `0x005D1536`)
  checks `+0x24` bit `0x20`, `ValidForPlaceInHand` (+0x6FC),
  `IsCannotBePickedUp` (+0x180) and the influence (`+0x48`, or vtable `+0x714`
  returns 0). Then it puts the object in the next record (`0x005D17BD`), sets
  the pick lock (`+0x38 |= 0x10`), starts Immersion 4, and sets the Tug
  (`CHand+0x4908 = 1`) or calls `fn_0046DC30(obj, 1)`.
- **Hand mode 9** (`fn_005D7F20`) needs the object of `+0x400` and the
  influence at the land point `+0x3F0`, or vtable `+0x714` returns 0
  (`0x005D7FA2`). Vtable `+0x714` is
  `InterfaceMustBeInInfluenceForInteraction`.
- **The object of the record must be nearer than the land.**
  `UpdateInterfaceCollide` (`0x005D5A70`) keeps the object of the next record
  only when its distance (`+0x404`) is less than the land distance (`+0x3FC`).
  Else it tests whether the object stands over the land, and it clears the
  record when that fails. The land distance is where the ray of the Hand's
  pixel meets the land. A far object is behind that land. So the first build
  (run far12) put the object in the record and pressed the button, and the
  game kept hand mode 3 for 40 frames. The mod now cuts the distance of the
  record to half of the land distance (`Inject()`), and the game keeps the
  object (run far13: hand mode 9 one frame after the press).
- **In the Holding state** the game writes the held mesh translation to the
  Hand's position plus the hang offset (`0x0046CA60` to `0x0046CA97`, the
  floats at `0x00D13F20`). In the Tug state it writes nothing.

### The cone and the choice

The ray is the aim pose of the pointing hand in the world, as the teleport
takes it (`ControllersFarRay`, from `ControllersTeleportInput`). Since the
evening of 2026-10-03 it has the turn and the point of a preset, and the
trim: see [controllers.md](controllers.md), section 8. The draw
offers only the objects whose circle holds the pick pixel. So the screen test
hook of the table (`h_ScreenTest`) also offers an object whose mesh point is
inside the cone, 4 degrees wider than the selection. The distance 0 of that
offer asks the mesh about the pick pixel, so the game's own pick gets no false
hit.

After the first eye pass, `FarSelect()` scores the candidates with
`FarConeScore`:

- `te = max(0, t - atan(r / d))`, with t the angle off the ray, r the
  `Get2DRadius` of the object and d its distance from the ray's origin. An
  object behind the ray is out. An object less than 0.35 m of the room from the
  Hand's point is a near object, and it is out too. A near object of `Choose()`
  always wins, and then there is no far target. Since 2026-10-03 there is also
  no far target while an object that a press acts on is 10 cm or less from
  the Hand's point: see "Near objects win".
- The score is `te + 1 degree x log2(1 + meters from the Hand)`.
- A new target needs `te <= 8` degrees (`far_grab_cone_deg`). The target stays
  while `te <= 14` degrees. Another object takes over only when its score is 2
  degrees lower (`FarPickTarget`).
- The object must be holdable (`Holdable()`), and the land must not hide it.
  The draw offers every object in the frustum, so the winner is tested against
  the land: 24 heights between the eye and a point half a radius over the base,
  with the last 5 percent left out (`FarVisible`). A hidden winner is out for
  that frame, and the next best is tested (12 tries at most). Without this test
  the hand chose a tree behind a hill (run far8). The sea does not hide an
  object: see "A target that moves, floats or dies".
- The target is inside when the influence at its land point is positive, or
  when vtable `+0x714` returns 0.

### The range (2026-10-03)

Buck, after the headset test of 2026-10-03: "Perhaps we can limit the range
from which we can grab objects in the distance? Maybe 2m in front of the
player or something?"

A far target is at most `far_grab_max_m` meters of the room from the head
(2.0). The distance is from the head, not from the chest anchor and not from
the Hand. It is measured along the floor: `FarFloorMeters` takes x and z of
the world and divides by the units in a meter. So the height of the land does
not count, and a player who leans toward an object brings it into range. The
head is the eye point of the aim ray (`ControllersFarRay`). The target of the
last frame stays to 15 cm more (`FarInRange`), so a small move of the head
does not turn the target off and on. `far_grab_max_m=0` gives no limit. An
object past the range gets no halo and no lock.

In his log of that day, 39 of the 124 targets that the log shows were more
than 2 m of the room from the Hand, and the farthest was 10.8 m away.

### A bulk source at a distance (2026-10-03)

Buck: "Maybe remove the distance grab from bulk objects like granary,
forests, fisheries etc? I can grab them, but it only takes one object/tree,
it doesn't feel very useful. Or maybe instead of removing it we could make it
to start bulk grabbing/collecting over distance instead?"

**His log.** Of the 124 targets, 58 were a `Tree`, 37 a villager and 16 a
`BigForest`. No pile of a store was a target, because the cone took only
holdable objects. Five fetches were of the `BigForest` `2AA70A4C`. Each ended
with "the game did not take the object" after 40 frames, with one new `Tree`
in the Hand: a press on a `BigForest` makes a new tree
(`BigForest::InterfaceSetInMagicHand`), and the fetch waited for the forest
itself. The end of the fetch held the grip back, so the grip machine saw an
opening and the Hand let the tree go.

**The choices.**

- **A pile of a store (`PileFood`, `PileWood`), a `Field` and a `FishFarm`
  are far targets** (`TakeValid`, the tests of `FUN_005D1330`). A grip that
  closes on one starts the game's bulk take at a distance, with no pull. The
  rule of "The take that did not end" applies: hold the grip to collect, open
  it to stop and keep the bundle, close and open it again to drop.
- **A forest (`BigForest`) is a far target again** (since the afternoon of
  2026-10-03). The build of 8fbebc3 left it out, and then no grip gave a tree
  from a forest: see "A forest gives a tree". The pull gives one new tree,
  and the glide draws it from the forest's point. The game has no bulk take
  from a forest. Since 2026-10-04 the mod adds one: see "Many trees from a
  forest".
- **A single `Tree` stays a far target.** The fetch gives that tree, as it
  gives any object.

**How.** The states of `far_grab.cpp` have one more, Take. A grip that closes
on a bulk target goes to Pull at once. For 2 frames the source is in the next
pick record and the land under it is at `+0x3F0`, so the game gives hand mode
`0xD`. Then Fetch sends the grip as a press, and the game starts the take
(`FUN_005D1330` reads the record). When `GInterfaceStatus+0x3C` is the source,
the state is Take: the grip is not held back, and `GripStep` makes it the
button. The tether and the halo stay until the game ends the take. A press
that the game does not answer in 40 frames ends as a fetch does.

On each game turn the game ends a take when the Hand's own position
(`GInterfaceStatus+0xC8`, set from the Hand at `+0xA4` in `fn_005DBC60`) is
over land outside the influence (`0x005DC540`) [V]. So the halo of a bulk
source is gold only when the land under the Hand is inside the influence too.

### A target that moves, floats or dies (2026-10-03)

Buck, after the headset test of the evening of 2026-10-03: "We are unable to
distance grab moving objects, or maybe objects in water specifically, I threw
in a ball I got as a reward into the water and it was slowly drifting away
and I could not distance grab it at all, I don't believe I could on land
either, the only way I could was by grabbing it with my hand."

The log of that session is lost, because the next start of the game wrote
over it. The log of his next, short session has an object of this kind on the
land: a `MobileObject`, 0.7 m from his head. It was the far target 7 times,
each time at 6.7 to 7.9 degrees from the ray, and each time it went away
again. The log has no lock and no grip on it. So the first cause is the aim:
see [controllers.md](controllers.md), section 8. A small object has a small
angle of its own, so the wrong aim left it at the edge of the cone.

**What the game does [V].** No test of the water and no test of the motion
stops a pick up:

- The press `fn_005D1330` and the pick up `fn_005D1740` test bit `0x20` of
  `Object+0x24`, `ValidForPlaceInHand`, `IsCannotBePickedUp` and the
  influence. `GInterface::PlaceObjectInMagicHand` (`0x005DA6F0`) tests the
  same and `IsSpaceInHands`. It takes an object that is in the physics (bit
  `0x40` of `+0x24`): at `0x005DA7F4` it has a help line for that catch.
- `Influence::CalculatePlayerInfluence` (`0x005CD170`) does not read the
  land. `MapCoords::IsWater` (`0x006035B0`) has 18 callers. Two are in the
  interface: `0x005D3A2F`, in a search for an object near a land point when
  the record is empty, and `0x005D6A83`, in a tooltip.
- `UpdateInterfaceCollide` writes the map point of the object of the next
  record to the land point `+0x3F0` (`0x005D5D09` to `0x005D5D2B`), and its
  distance to the land distance. So the game's own land point follows the
  object.
- The physics floats an object over the sea (`fn_007FD4D0`, from
  `0x007FD5A6`): the land under it is lower than 0.0001 and its center is
  lower than one radius. The lift grows with the depth. An object that
  floats does not come to rest, so it stays a physics object, and
  `PhysicsObject::DrawAll` draws it. The draw offers it to the pick as each
  other object (run all5: a `Rock` in flight was a far target). An object 4
  radii under the sea leaves the physics, and the game deletes it.

**What was wrong in the mod, and what it does now.**

- **The sea hid an object.** `FarVisible` tests the line from the eye to the
  object against the height of the land, and `GetAltitude` gives 0 for the
  sea. An object that floats can have its point under the sea plane. Then
  the line goes under 0 before it comes to the object, and the test said
  that land hid it. Now only land higher than 0 hides an object
  (`FarLandHides` in `controller_math.h`).
- **The lock did not follow the object.** The cone offers an object to the
  pick only inside 12 degrees of the ray. A locked object that moved out of
  that cone was not offered, so its place stayed where it was. The land
  point under it was from the frame of the selection. Now the screen test
  offers the mesh of the locked target on each frame (`g_far_keep_model`).
  `FarGrabTrack` takes its place, the land under it and the influence there
  on each frame. The tether, the halo and the start of the glide follow the
  object. An object that drifts out of the influence while the lock holds
  ends the lock at the pull: "the object left your influence".
- **The lock kept a pointer that could die.** The game deletes a rock 4
  radii under the sea, and a villager that dies. A pull after that put a
  dead pointer in the pick record, and `fn_005D5E40` calls a slot of its
  table. Now the selection keeps the first word of the object (its table).
  `FarTargetLives` tests once a frame, before each write of the record and
  before each read of the object, that the memory can be read and that the
  first word is the same. If not, `FarGrabGone` ends the lock, and no code
  reads the object again. The destructor of an object writes the table of
  its base class there, so a deleted object fails the test. [I: an object
  that the game makes at the same address with the same class passes. The
  fetch then takes that object, which is a live object.]

The fetch itself needed no change: the game takes the object of the record
by its pointer, where it is.

### Show

- **The halo** (`hand_glow.cpp`, `DrawHalo`) is the disc of the glow, gold, or
  red outside the influence. It is centered one radius over the base of the
  object. Its radius is `1.35 r + 1 cm` of the room, at least 2 cm and 1.2
  percent of the distance. The depth test uses a point one radius behind the
  center (`GlowProject` takes a negative pull now). So the object hides the
  middle of the disc, and the light shows around its outline.
- **The tether** (`overlay.cpp`, `BuildFar`). After the lock, a gold tube of
  3 mm goes from the Hand's point to the object. It also shows while the game
  takes from a far bulk source.
- **No ring.** The first build drew a ring on the land under the target
  (`BuildLandRing`): gold with a disc, or red with a cross. Buck, after the
  headset test of 2026-10-03: "Distance grab works nicely and feels intuitive,
  but remove the yellow target marks under the grab target, the subtle glow
  is more than enough." So nothing is drawn on the land now. The red state
  stays as the red halo.
- **The tick:** 0.15 for 10 ms at a new target, 0.3 for 30 ms at the lock, and
  0.2 up to 0.6 every 60 ms during the glide. The game's effect 4 starts at the
  pick up as before.

### The claim, the pull and the fetch

The states are Idle, Aim, Lock, Pull and Fetch (`far_grab.cpp`).

- **The claim.** A grip that closes while the target is selected and inside the
  influence is held back from the grip machine and from the grab of the world
  (`FarGrabPumpGrip`, called by the pump before `UpdateGrab`). The machine sees
  an open grip. A grip that closes on a red target is not claimed, and the log
  says that the game would refuse it. A claimed grip that is still closed when a
  lock ends stays held back until it opens. So it never reaches the machine as a
  new press on the land.
- **The pull.** `p` is the unit direction from the grip at the lock to the head,
  in the room. After 60 ms of lock, `PullDetect` fires when the mean velocity of
  the grip over 60 ms has `v . p >= 0.8 m/s` and an angle of 55 degrees or less
  to `p` (`far_grab_pull` sets the speed). It also fires when the grip moved 12
  cm along `p`, and for a flick up with `v.y >= 1.0` and `v . p >= 0.4`.
- **Pull state.** For 2 frames the mod puts the object in the next record
  (`fn_005D5E40`, through `Inject()`, and it wins over `Choose()` for these
  frames). It writes the land under the object to `+0x3F0` in the interface step
  (`h_InterfaceStep`).
- **Fetch state.** The claim goes, and the grip machine presses the button. The
  press needs no pixel in view (`FarGrabPressing`), because after a pull the
  hand is near the head. The game then picks the object up as for any object.
  When `CHand+0x4904` is the object, the fetch is done. After 40 frames without
  it, the fetch ends and the grip stays held back until it opens.
- After the Hand holds the object, the grip machine carries it as before, and
  the throw is the throw of "M4 as built" and "The grip holds".

### The timeline of the game [V, run far15]

The press is at frame 3143. The pick lock is set at 3145, and `+0x4904` is the
object. The Hand is in the Tug state (3) from 3146 to 3149 and in the Holding
state (4) from 3150. The grip machine says "the pick up is over" at 3150. The
Hand did not jump to the object: the exact Hand keeps the drawn Hand on the
controller in the Tug state, and the pictures of run far15 show the Hand at the
controller during the glide. Not measured: the position `+0x78` in the Tug
state.

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

p0 is the mesh translation of the object at the pull. p1 is the mesh
translation that the game wrote in the Holding state, and before that the
Hand's point. `GlideArc` and `GlideSeconds` are in `controller_math.h`. The
clock is the time of `FarGrabFrame`, so both eyes draw the same point. Only the
drawn mesh moves. The object itself is where the game puts it. Run far15 took
pictures at frames 3147, 3153, 3159 and 3167: a tree comes from the far hill to
the Hand.

**A release in the glide (2026-10-03).** The glide writes only the drawn mesh.
The throw took its start from that mesh. So a grip that opened in the glide
started the flight at the point of the glide, and not at the Hand. Run tearly0
(the build before the correction) opens the grip 21 frames after the start of
a glide of 3.5 m. The flight started 85 units over the land, 0.80 m of the
room from the place of the object in the palm.

Now `throw_physics.cpp` has a call that runs before the glide
(`ThrowCapture`, put in the list by `ThrowPhysicsInstallEarly`). On each frame
it keeps the place of the held object in the Hand: the mesh position that the
game and `hand_exact.cpp` wrote in the Holding state, or the Hand's point
before that state. At the release the flight starts at that place, when the
drawn mesh is more than 1 cm of the room from it. The picture starts at the
point of the glide and comes to the flight in 0.15 s (`CatchUpLeft`), so the
object does not jump. Run tearly2 has the same release: the log says that the
object was drawn 56.7 cm from its place in the Hand, the flight starts at the
Hand (12.5 units over the land, the start of each other throw of the recipe),
and the first contact is 1.5 cm from the real arc. The game still puts the
object at the point of the glide when its physics starts, and the mod moves
it to the flight as for each throw.

### The throw law

`throw_law=1` applies in the God view and on the table. Since 2026-10-03 the
default is `throw_law=2`, the real flight: see "The throw physics". The throw
sends `v_game = sqrt(s) v_room`, with s the units in a meter and v the real
speed of the grip in m/s (`SqrtThrowVelocity`). The game's gravity is 9.81
units/s^2, so the range is s times the real range from any release height. The
cap of 124 units/s comes at `124 / sqrt(s)` m/s, 12.4 m/s at s = 100. The speed
is cut to the cap with the direction kept, and the gentle drop rule is the old
one. `throw_law=0` gives the old throw (`1.5 s v`). The flight lasts sqrt(s)
times longer than a real throw. The law fixes where the object lands, not how
long it flies.

**The drag.** The design guessed that `PhysicsObject+0x174` is the drag. It is
not. `fn_007FD4D0` reads that byte at `0x007FD4DA`. When it is not 0, the
function jumps to `0x007FD98A`, which skips the drag (`0x007FD4E9`) and the
gravity (`0x007FD553`). So the byte is a mode and not a factor, and the mod does
not write it. The drag is `F -= k |v| v`, with k at `+0x14C`. It was not
measured: the pen is a slope, and a thrown rock rolls on after it lands.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `far_grab` | 1 | 0 turns the whole feature off |
| `far_grab_cone_deg` | 8 | The `te` that a new target needs. The target stays to this plus 6 |
| `far_grab_pull` | 80 | The speed of the pull, in cm/s |
| `far_grab_max_m` | 2.0 | The range: a target is at most this far from the head, in meters of the room along the floor. 0 gives no limit |
| `throw_law` | 2 | 2 is the real flight ("The throw physics"), 1 is this law, and 0 gives the old throw |

Test keys for the desktop runs only: `far_test_influence=1` (every land point is
inside the influence) and `far_test_no_trees=1` (no tree is a far target).
`far_test_thrown=1`: the word `aimfar` aims at the object that the Hand threw
last, where the draw saw it, so a run can fetch an object that flies, rolls or
is in the sea. `far_test_least_cm=N`: the word takes an object N cm or more
from the Hand, and not 50 cm.

The fake controller has two new words. `aimfar@F+n` turns the aim of the right
hand to the nearest far target: a holdable object that is no tree, more than
0.5 m of the room from the Hand, in the range of `far_grab_max_m` from the
head, inside the influence and seen, in the God view or on the table. With
`seek_bulk=1` it aims at the nearest bulk source in place of a holdable
object. Until 2026-10-03 it took an object 2 to 6 m from the Hand. The
hand stays where it is. `pull@F+n` steps the right hand 15 cm toward the head
over n frames, and the next `pull` steps it back.

### The log

Tag `far grab`: each new target with its class, the angle and the distance, the
lock, the pull (the speed along `p` and the move in cm), the press, "the Hand
holds" with the frames after the press, each frame of the glide, each end of a
lock or a fetch with the reason, and one line each report window. Since the
evening of 2026-10-03: how far the object moved while the lock held, a locked
target that goes in or out of the influence, a target that the game deleted
("the target is gone"), and how often the locked target went to the pick. Tag
`hand throw`: the speed sent and the landing of the real throw, then the position of
the object 40, 100, 200, 300 and 450 frames after the release. Tag `hand glow`
and tag `overlay`: the first draw of a halo and of a ring, with their pixels.

### Tests

The math test has 64 more checks (962 in all), in `far_checks.h`:

- The cone: an object on the ray or at its angular radius scores 0. An object
  behind the ray, square to it, or 20 cm from the Hand is out. The nearer of two
  equal targets scores lower. The hysteresis holds at 1.5, 2 and 2.5 degrees.
  The target is kept at 13 and 14 degrees and lost at 14.5.
- The pull: 0.9 m/s toward the head fires. 0.9 m/s across, a head step with a
  still hand, 0.7 m/s with 5 cm, and an angle of 57.8 degrees do not fire. A
  move of 12 cm fires and 11 cm does not. A flick up fires, and one away from
  the head does not.
- The glide: `p(0) = p0`, `p(1) = p1`, the peak of a flat glide is at u = 0.5
  and is `0.15 |p1 - p0|`, the ease never goes back, and the time is 0.30, 0.40
  and 0.50 s at 0, 2 and 4 m.
- The square root law: the range is s times the real range to 1e-4 at s = 7.5,
  100 and 1000, from a height of 1.2 m. The cap holds at s = 1000. The drop rule
  is the same as `TableThrowVelocity`.
- The aim of the fake hand: the angles of `FakeAimAngles` aim the hand along the
  wanted direction.

Desktop runs (God view, `fake_controller=2`, Land 1, `-NEWGAME`, the dialog
recipe of [god-view.md](god-view.md), section 12, then the pen from about frame
2750):

| Run | What it showed |
| --- | --- |
| far1 | A target with no test key: a Rock "outside your influence" at 2.3 degrees, a halo and a red ring (52 triangles) |
| far10 | The red target: the picture shows the red halo around the base of a tree and the red cross |
| far3 to far8 | Objects behind a hill were targets, and the halo was hidden. This led to `FarVisible` |
| far9 | After `FarVisible`: 1025 candidates hidden in one window, and targets with a halo and a ring |
| far12 | The first fetch: lock, pull (1.04 m/s), the record held the object, the button went down, and the game kept hand mode 3. The fetch ended after 40 frames |
| far13 | With the distance cut: hand mode 9 one frame after the press, the Hand held the object two frames after it, the grip machine carried it at 3085, the glide ran 0.45 s, and the open grip let go |
| far14 | A grip that opened 25 frames after the lock: "the lock ends: the grip opened before a pull". A second grip then pulled and fetched |
| far15 | The timeline of the Tug above, and four pictures of the glide |
| far18 | A rock thrown across at 1.84 m/s: sent 18.4 units/s |
| far19 | `throw_law=1`: a rock thrown up at 45 degrees at 1.08 m/s. Sent 6.2 7.6 4.4 (10.8 units/s). The real throw lands 13 units away. The rock was 7.2 units away after 40 frames and 11.7 across after 100 |
| far20 | `throw_law=0`, the same throw: sent 71.7 87.7 50.5, which is the cap of 124 units/s, and the rock was out of view 40 frames after the release |

**Tests of 2026-10-03.** The math test has 13 checks of the range
(`RangeChecks` in `grip_checks.h`): 150 units across at 100 units a meter are
1.5 m, the height does not count, 1.90 m and 2.00 m are in a range of 2 m,
2.05 m is out for a new target and keeps the target of the last frame, 2.20 m
loses it, a range of 0 is no limit, and an object with no known distance is
out.

**Desktop runs of 2026-10-03.** None yet, because the PC was in use. The runs
to do, with the start dialog and the store of run gt1 (see "The take that did
not end"):

- **gt3**, the take at a distance: `seek_bulk=1`, `bulk_test_influence=1`,
  `far_test_influence=1`, the third point `35,-10,20` (the hand 70 cm from the
  store), and `aimfar@3220+1 grip@3280+120 grip@3560+30` after the zoom. Look
  for "target X (PileWood, a bulk source)", "the grip closed on the bulk
  source", "the game takes from X at a distance", the amounts, "the take at a
  distance is over" after frame 3400, and the drop after 3590.
- **gt6**, the range at the pen: `far_test_influence=1`, the third point
  `-10,-10,60`, and `aimfar@3000+1 grip@3100+101 pull@3140+3`. Look for the
  count of objects "at most 2.0 m from the head" in the line of `aimfar`, and
  for "N past the range" in the report line.

**Not tested in a run:** the tether and the halo during a take at a distance,
a `Field` and a `FishFarm` as a far target, and a take with the Hand over land
outside the influence.

**Needs the headset.** Whether 2 m is the right range. Whether a take at a
distance reads well: the halo, the tether and the count at the Hand. Whether
the halo alone shows the target well against the land.

**Tests of the evening of 2026-10-03.** The math test has 10 more checks in
`far_checks.h`: an object 20 cm from the Hand is no target with its own
distance and is one with the distance of the score (`FarScoreMeters`), and
the 5 cases of `FarLandHides`: a hill hides, a hill lower than the slack does
not, the sea does not hide a line over it or under it, and land just over
the sea level does.

**Desktop run move1b** (the God view at 100 units per meter, a render of
2560 by 1440, `far_test_thrown=1`, `fake_controller_play_frame=2750`, so the
frames below are 849 later than the frames of the script):

| Frames | What it showed |
| --- | --- |
| 3950 to 3994 | A far fetch of a rock at rest, 3.3 m away: the lock, the pull, "The object moved 0.0 units while the lock held", and "the Hand holds" 2 frames after the press |
| 4053 | A throw of 1.92 m/s |
| 4081 to 4104 | `aimfar` at the rock in its flight, 35 units over the land and 0.27 m from the Hand. The target at 4082, the lock at 4083, the pull at 4100. "the next record holds 2927CC94 and the land point is 2315.8 3222.7. The object moved 14.8 units while the lock held". "the Hand holds" 2 frames after the press, and the glide started from 2317.1 74.6 3223.7, where the rock was then. The flight ended: "the game took the object out of its physics". So the game takes an object that flies, from a distance, and the lock follows it |
| 4273 | A throw of 5.90 m/s toward the sea, which is 220 units from the pen |
| 4361 to 4363 | `aimfar` at the rock in its flight over the sea: "at 2274.3 167.6 3444.0. The land under it is at 0.0 (0 is the sea), holdable 1, seen 1, inside 1". The target and the lock |
| 4458 | The rock fell under the sea, and the game deleted it: "the flight of 2927CC94 is over after 2.21 s (the object fell under the sea)", then "the target 2927CC94 is gone: the game deleted the object in the lock state. The mod lets it go and does not read it again". The run went on to its end with no fault, with the pull at 4549 and the grip open at 4555 |

**Not shown in a run:**

- A fetch of an object over the sea or in it. The lock of run move1b was on
  the rock over the sea, and the pull came after the rock was gone. A rock
  sinks, and the pen has no object that floats. The run to do is move2 of
  `aim_queue.ps1` in the scratchpad: the pull comes while the rock flies over
  the sea.
- An object with its point under the sea plane (`FarLandHides` has the math
  checks).
- A locked target that drifts out of the influence.
- A villager in the sea.

**Needs the headset.** Whether the ball on the sea gets the halo, the lock
and the fetch now, with the new aim.

**Not tested.** A multiplayer game (the code reads the flag at `0x00D46ABA` and
the game mode at `GGame+0x25017C`, 3 for multiplayer). A fetch of a villager, a
Creature or a building. The haptics on a real controller. The look of the halo
and the ring against open land: the pen is a slope, and the pictures show them
near the horizon only. The drag.

**Needs the headset.** Whether the cone of 8 degrees feels right with the real
aim pose. Whether the pull at 0.8 m/s or 12 cm is easy to do and does not fire
by accident. Whether the glide time reads well. How the sqrt(s) throw feels in
the God view: it lands where a real throw lands, but it stays in the air sqrt(s)
times longer.

## The throw physics (2026-10-03)

Buck, after the headset test of 2026-10-03 with the square root law: "Throwing
objects now is much worse than before, there seems to be a very low speed
limit, no matter how hard I throw something it goes really slow." And before
that: "the physics of B&W are super floaty", because the game made them for a
mouse.

### Why a throw was slow

**The law.** The square root law sent `sqrt(s) v` units per second, with s the
world units in a meter of the room. In the room that is `v / sqrt(s)`: at 67
units per meter the object left the hand 8 times slower than the hand moved,
and at 11 units per meter 3 times slower. His hardest throw was 4.84 m/s. It
left at 39.5 units per second, which is 0.59 m/s in the room. The flight then
took sqrt(s) times longer than a real flight. The law gave the correct landing
point, but each throw looked the same: slow.

**The estimate.** The speed was the mean of the 80 ms before the grip opened.
The math test flies a swing to 6 m/s in 150 ms that stops in 200 ms. With an
opening 44 ms after the top speed the mean is 5.76 m/s. With an opening 100 ms
late it is 4.61 m/s. So the estimate lost 4 to 23 percent, and more for a
short flick. It was not the main cause. The log of 2026-10-03 shows 59
releases (it stops at 60 lines). 18 of them were swings of 1.27 to 4.84 m/s
by that mean, and 31 were below 0.5 m/s. The log has no samples of the hand,
so it cannot show the top speed of those swings.

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
  against the land
  (`LH3DIsland::GetAltitude`), or floats the object when the land is under the
  sea.
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
ms before it to 24 ms after it. The throw takes the largest of them, from 120
ms before the opening of the grip to the release action. The direction is
from the same samples. In the math test the swing above reads as 5.91 m/s at
an opening 44 ms late and 5.92 m/s at 100 ms late, and the same at 72 and 120
samples a second.

**A set down stays a set down** (`SetDown`). The hand was still at the opening
when the mean of the last 80 ms, across the land, is 0.18 m/s or less. That is
the old rule. A still hand sets the object down, unless the top speed of the
window was 1 m/s or more. Then the hand stopped a moment before the opening,
as in a flick, and the object flies with the top speed.

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
   across the land, so the game throws the object. After the action
   `ThrowPhysicsReleased` reads the mark at `0x00D47824`. With the mark the
   flight starts, from the mesh position of that moment.
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
   For the search of the near objects the velocity is that of the real
   flight, up to 300 units per second, and a hook on `fn_007FD200` puts the
   velocity back before the first substep reads it.
8. **The damage.** A turn has more steps now. The hook makes the game's sum
   for each 20 steps in a row and gives the game the largest. A landing hurts
   as it does in the game at the same speed of the game's time, and a slide
   does not hurt more. With the fall from the hand, a throw from 30 cm over
   the land at 67 units per meter lands at 20 units per second or more.
9. **The draw.** The mod keeps the matrix of the object after each substep
   with its time in the flight (`fn_007FD140` gives the matrix). A hook on
   `DrawAll` puts the object where the flight is at the time of the frame
   (`FlightAt`), once a frame for both eyes. A turn that comes late lets the
   flight go on for 50 ms, and then the clock of the flight waits. On the
   first turn the mod compares its matrix of the last substep with the mesh
   matrix that the game wrote. When they agree it draws the axes of each
   substep. If not, it moves the object only, and the log gives the
   difference.
10. **The end.** The flight ends when the object comes to rest, falls under
    the sea, leaves the physics (a new pick up), or after 60 s. Up to 4
    objects fly at one time.

**What keeps the game's time.** A Miracle in the Hand gets the velocity of
the square root law. A tree flies as a rock does (see "Buck's log of
2026-10-03, afternoon"), unless `throw_trees=0`. A multiplayer game
uses the square root law for each throw, because all players must run the
same physics. The mouse player's throw does not pass through the release
hook. Each object that the player did not throw keeps the game's physics.

**The game's state is ahead.** A turn works out the next 0.1 s of the flight,
and the mod draws it as the time comes. So the Object of the game is up to
0.1 s ahead of the picture, or 0.2 s after the extra turn of a release. The
sound of a landing can come that much before the picture.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `throw_law` | 2 | 2 is the real flight, in the God view and on the table. 1 is the square root law with the game's physics. 0 is the old gain |
| `throw_log` | 0 | 1 logs each turn of a flight and each third frame of its draw, for the desktop runs. 2 also logs each substep and each frame after the first contact |
| `throw_gain_pct` | 150 | The gain of `throw_law=0`, and the gain in the set down rule |
| `throw_slow_x10` | 20 | The time factor k times 10, from 10 to 100. 20 is k = 2.0: the flight has the arc of the real throw, 2 times slower |
| `throw_trees` | 1 | 1 lets a thrown tree fly as a rock does. 0 gives the tree the game's physics |

**The time factor k.** The real flight in a small world looked too fast and
heavy. So the flight has one more factor, k. The object flies the same arc
(the landing point is the real one), but k times slower than real. In the room
the speed is `v / k` and the gravity is `g / k^2`. In the world that is
`s v / k` under `s g / k^2`. The game does not change: its velocity is still
`sqrt(s) v`, and its gravity is still `g`. Only the number of game steps in a
substep is `sqrt(s) / k` in place of `sqrt(s)`. So k = 1 is the real flight,
and k = sqrt(s) is the square root law, with one step in a substep. The game
cannot run less than one step, so k is cut to sqrt(s): at 7.5 units per meter
k is 2.74 at most. The log gives the k in use and the k that was asked for. The
arc, the draw, the first contact and the search of the near objects all use
the slowed flight. A flight keeps its k until it ends, and the plan of a
release reads k once.

**The keys of k.** With `test_keys=1`, Numpad Plus makes k larger by 0.25
(slower), and Numpad Divide makes it smaller by 0.25 (faster), from 1.0 to
10.0. Each press gives a short pulse of the pointing hand and a log line with
the new k. The pulse is weaker at the end of the range. The game and the mod
use neither key (the mod uses Numpad Star and Numpad Minus for other tests).

**An A/B in the headset.** With `test_keys=1`, Scroll Lock goes between the
real flight and the square root law while the game runs. The game has no
action on that key. A flight that is in the air goes on. The log names the
law after each press.

Off the table (play modes 0 to 2) the throw is the old one, with the gain.

### The log

Tag `hand throw`. The release: the top speed and its time before the release,
the mean of 80 ms, the speed of the flight in units per second, the speed in
the game's time, and what the release sent. The start: where the real arc
meets the land, and when. "The game's physics has" the object: the delay, the
place of the game and of the flight, the mass, the radius and the drag. The
first contact: the time, the place, and the distance from the real arc at that
time. The end: the time, the cause, the distance across the land, the count of
turns and steps, and the time that the steps took.

### Tests

The math test has 73 checks in `throw_checks.h`:

- The top speed: the swing above at 44 and 100 ms late, at 72, 90 and 120
  samples a second, an opening at the top speed, a constant speed, one sample,
  and a sample after the window.
- The set down: a still hand, a hand that came to rest from 0.67 m/s, a move
  at 1.5 m/s that stopped 200 ms before the opening. A hand at 0.30 m/s
  throws, and a flick of 2.5 m/s that stopped 60 ms before the opening throws
  at its top speed.
- The plan: the flight is `s v` and the game's velocity is `sqrt(s) v` at 7.5,
  100 and 1000 units per meter, along the hand's direction through a turned
  room. The release sends the cap at most, and 2.5 units per second across the
  land for a toss straight up.
- The arc: the game's step (`GameFreeStep`), run sqrt(s) times a substep,
  flies a throw of 5 m/s at 40 degrees from 1.0 m. At 7.5, 100 and 1000 units
  per meter the path stays within 8 mm, 2 mm and 1 mm of the real arc in the
  room. The flight lasts 0.885 s, the object lands 3.39 m away and the top is
  1.53 m: the values of the real throw.
- The steps: 200 substeps run 200 sqrt(s) steps, to 1, at 7.5, 67, 100 and
  4000 units per meter.
- The draw: the place between two samples, 10 ms after the newest one, the
  stop after 50 ms, and an object in contact.
- The time factor (49 more checks, 1346 in all): at 7.5, 100 and 1000 units
  per meter and k = 1, 2 and 4, the plan gives the game's velocity `sqrt(s) v`
  and `sqrt(s) / k` steps, and the game's step flies the arc of the plan. The
  object lands where the real throw lands, and the flight takes k times the
  real time. At 7.5 units per meter k = 4 is cut to 2.74.

### Buck's log of 2026-10-03, afternoon

His finding: rocks and people "fly too fast or are too heavy", the rocks
"were dropping way too fast, like physics was running 2x as fast", and a tree
is "still sluggish".

**Was the flight real time?** Yes. The log has 54 releases: 28 set downs, 12
trees and 14 flights of the real flight. The scale was 44.4 units per meter
for the rocks (the step runs 6.67 times as often) and 100 for the trees.
Each of the 14 flights has a first contact after 0.35 to 0.74 s of the flight.
That is 0.00 to 0.02 s from the time that the real arc meets the land. The
contact was 0.1 to 4.1 cm of the room from the real arc (0.1 cm for 5 of the
14, under 2 cm for 10). A throw of 2.5 to 3.3 m/s from about 1.2 m over the
land lands in 0.37 to 0.46 s at 4.0 to 5.0 m/s. That is a real throw: a stone
that falls 1.2 m takes 0.49 s. The steps are not doubled. A flight of 2.42 s
has 560 substeps and 28 turns, and the steps per substep are 6.67. The draw
hooks do not run the physics, and the physics hooks run in the game turn only.
So the log shows no fault: the object fell in the real time of a real throw.
It looked too fast because the world is small. A rock of 5 cm of the room that
falls 1.2 m in 0.4 s does not look like a big rock. The time factor k is the
answer to that.

**After the first contact.** The game's physics goes on with the same extra
steps until the object is at rest, so the bounce and the roll are the game's
own motion, played at the same rate as the arc. The game's rules do not
change with the rate: the speeds, the forces and the distances are the
game's own in its time, and only the clock is faster. So the roll ends in the
same place for each k (the log has 1.3 to 2.9 m from the release across the
land for throws of 2.5 to 5 m/s), and it takes k times as long. At k = 1 the
roll takes the real time, and at k = 2 it takes twice as long. One thing is
not the same. In a substep where another physics object touches the thrown one
(`PhysOb+0x78`), the game's one step runs. For a pile of objects, or a
thrown object that rolls into another one, the thrown object then moves
`sqrt(s) / k` times slower in that substep. 7 of the 14 flights had 21 to 73
percent of their substeps in that state. This is the "too heavy" that is left.
It is milder at larger k. The mod does not change it, because the game finds
the contact of two objects once a substep, and the extra steps would lose the
force of the pair.

**Trees.** The log has 12 tree releases. They kept the game's physics with the
velocity of the square root law: at 100 units per meter, a throw of 3.3 m/s is
33 units/s, and that flies 10 times slower than the real flight. The
disassembly shows that a tree is a physics object like a rock.
`Object::InitialisePhysicsFromHand` `0x00636F00` calls `PhysicsObject::AddObject`
for each object. The only difference is the argument that
`PhysOb::AdjustToGroundLevel` `0x007FCB80` gets (`0x00637075`, `0x006370D5`:
1 for a thing that is not a tree), and that a tree is not a gentle drop
(`IsAnyKindOfTree`). When a tree comes to rest, `Tree::EndPhysics` `0x0074B830`
plants it (dust from `SmokyStuff::Create`, a pulse of the hand and the search
for a place), and that is the game's own code. The first version of the real
flight kept trees out because the game plants them. But the plant is at the
end, so it does not matter for the flight. So a tree now flies as a rock does,
with the same k. [I for the safety: the hooks do not read the class. NOT
TESTED in a desktop run or in the headset.] If a tree does a wrong thing,
`throw_trees=0` gives it the game's physics back.

**Desktop runs.** None yet: each try was refused, because the PC was in use.
The run to do is `tp1`: the recipe of run far13 (a far fetch of a rock at the
pen, a step of the hand of 8.5 cm in one frame, and the opening of the grip)
with `throw_law=2` and `throw_log=1`. Look for the release line, "the game's
physics has" the object, the frames of the draw against the real arc, the
first contact and its distance from the real arc, and the end of the flight.

**Not built.** The last 0.1 to 0.2 s of a roll is not drawn: when the object
comes to rest, the picture goes to the place of rest at once. A Miracle and a
tree keep the game's time. The steps of other objects that the thrown object
pushes keep the game's time, so a hit pushes them as the game does.

**Needs the headset.** Whether a throw feels like a real throw at 7.5, 67 and
400 units per meter. Whether the top speed of 120 ms gives the throw that the
player meant, and the direction. Whether a quick put down throws by accident
(the 1 m/s rule). Whether a villager lives through a soft throw and dies from
a hard one. Whether the roll after the landing looks right in the real time.
Whether a rock breaks a house as before. The cost of the steps at 1000 units
per meter and more (the log gives it for each flight).

## The landing of a thrown object (2026-10-03)

Buck, after the headset test of 2026-10-03 in the evening, with the time
factor 2: "Throws feel a lot more natural. Physics of the objects after they
fall still seem to behave weird, they jitter around and bounce way faster
than feels natural but actual throwing and trajectory feels more natural."

So the flight stays as it is. This section is the time from the first contact
to the rest. The flight is in "The throw physics".

### What the object did, in numbers

The runs throw the rock of the pen at 1.9 to 2.1 m/s, at 100 units per meter.
The rock is 2.7 cm of the room across. It lands at 1.5 m/s on a slope of about
45 degrees. The rate of the flight is 5 steps a substep. The log line "after
the first contact" gives these numbers for each throw.

| After the first contact | As it was (tbase0, tbase1) | `throw_land=1` (tland2) | `throw_land=2` (tland2m) |
| --- | --- | --- | --- |
| The time to the rest | 2.07 s, 2.45 s | 3.04 s | 0.72 s |
| The way across the land | 0.68 m, 0.68 m | 0.19 m | 0.23 m |
| The speed of the picture in the first 0.25 s, and in the next | 137 and 74 cm/s, 125 and 76 cm/s | 57 and 26 cm/s | not read |
| The turns | 4 to 5, at 7.5 a second at most and 4 a second for 0.75 s | 1.8, at 0.9 a second after 0.25 s and 0.3 a second after 0.5 s | 1.5 |
| The hops | 5 and 4, the highest 0.6 and 2.2 cm | 1, of 0.4 cm | 1, of 0.1 cm |
| The changes of the vertical direction of the center | 14.5 and 11.0 a second, of 0.3 to 0.4 cm | 3.3 a second | 8.4 a second |
| Frames drawn after the newest step | 3 of 189 (3 ms), 8 of 216 (6 ms) | 1 of 266 (2 ms) | 0 of 67 |
| The picture turned back | 0 times | 0 times | 0 times |

**The cause of the jitter.** It is the rate. The game made its contact for one
step a substep. At 5 steps a substep the rock turns at 5 times the game's cap
of 1.5 turns a second, and its center goes up and down 11 to 15 times a second
while the rock turns over its points on the soft springs. The draw is not the
cause: the picture did not turn back on one frame, and the frames that the mod
drew after the newest step were 3 to 6 ms late. The bounce is not the cause:
the highest hop was 2.2 cm. No other object touched the rock in these runs.
In Buck's log of the afternoon 7 of 14 flights touched one, and there the
rate went between 1 step and the rate of the flight from substep to substep.

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
  of the land (`LH3DIsland::GetNormal`). `+0x4C` is null for the land and the
  other PhysOb for a contact of two objects (`fn_007FDA60`).
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
  radians a second (`0x009A2BB0`), which is 1.5 turns a second.
- **The rest.** The same function gives 2 when a point is in contact, the
  speed is below T, the angular speed is below T / 2, and the torque over
  `m r^2` is below T. T is 1. After 15 s of life (`+0x170`, 5 ms a step) T is
  the life in seconds over 15.

### What the mod does

`throw_land=1` is the default. All of it starts at the first contact, and the
flight latches the keys at that moment.

1. **The rate goes to the rate of the land.** The rate of the land is 1 step
   a substep, the game's own time (`throw_land_rate_x10`). The rate changes
   only while the object touches, with a time constant of 0.10 s of the
   picture (`LandRate`, `kLandEase`). So the rate has no step, and the speed
   of the picture has no step. A hop keeps one rate from the land to the
   land, so its arc is a true arc. In run tland2 the rate is 3.0 when the
   rock leaves the land the first time, 1.15 after 0.5 s and 1.0 after 0.75
   s. At 1 step a substep the thrown object runs as each other physics
   object, and a touch of another object changes nothing in the rate.
2. **The low bounce.** A hook on `fn_007FDE40` (`h_Contact`) takes the force
   that the function added to the object. While that force pushes the object
   along its own motion, the hook cuts it to a part (`ReboundFactor`). That
   is the way back of the spring. `throw_bounce_pct=50` keeps a quarter of
   the force, so a bounce keeps half of the speed that the game gives it. A
   speed along the force below 0.5 units per second gets a part of the cut in
   proportion, so an object that lies on the land does not sink. The ball of
   the math test leaves the land at 1.3 in place of 4.2.
3. **The brake of the land.** The hook adds a force against the velocity of
   the center while the object touches the land (`LandBrake`). Its size is a
   friction times the force of the contact, and never more than the force
   that stops the object in that step. The friction is
   `throw_land_brake_pct` percent of the friction that the game has for the
   object: 60 percent of 1.31 is 0.79 for a rock. The game keeps a part of
   the turn in each second, and for the thrown object that part is 0.25 at
   most (`kLandTurnKeep`, the game's value goes back at the end). Without the
   brake the rock rolled for 9.1 s at the rate of the land (run tland1).
4. **A contact with another object** keeps the game's force: the other object
   got the same force, and the two must agree.
5. **The damage** is as it was. The sum that the game reads for
   `ReactToPhysicsImpact` gets the force as the game made it, with no low
   bounce and no brake.
6. **The water.** An object that floats counts as in contact, so its rate
   goes to the rate of the land too. The float has no spring, so the hook
   changes no force there.

`throw_land=2` keeps the rate of the flight and has the low bounce and the
brake: the rock comes to rest in 0.7 s. `throw_land=0` is the landing as it
was.

**Why the brake is 60 percent.** That is a friction of 0.79 for a rock, near
the median friction of the materials of Half-Life: Alyx (0.8,
`research-hla.md`). The brake holds an object on a slope below the friction
angle, which is 38 degrees at 0.79. The slope of the pen is steeper, so the
rock slides there for about 1 s at 15 cm/s before the land gets flat (run
tland4). With 100 percent (run tland3, a friction of 1.31) the rock came to
rest after 0.12 m in place of 0.15 to 0.19 m, in about the same time. An object would then stay on a slope of 52 degrees.

**What does not change.** The flight to the first contact: the code of the
landing reads `Flight::contact` first. Runs tbase1 (the landing as it was)
and tland2 each have a throw of 1.91 m/s from the same place. Each has the
first contact after 0.810 s. The two places of the contact are 0.7 cm apart,
and the two arcs differ by 0.2 cm at the land. The first contact is 0.2 to
0.7 cm from the real arc in each run, with each landing.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `throw_land` | 1 | 1 is the rate of the land with the low bounce and the brake. 2 is the low bounce and the brake at the rate of the flight. 0 is the landing as it was |
| `throw_land_rate_x10` | 10 | The rate of the land times 10, from 10 to 100. 10 is the game's own time. A rate above the rate of the flight changes nothing |
| `throw_bounce_pct` | 50 | The share of its speed that a bounce keeps, of the speed that the game gives it. 100 is the game's bounce |
| `throw_land_brake_pct` | 60 | The friction of the brake, in percent of the friction that the game has for the object. 0 is no brake |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The test keys.** With `test_keys=1`, the Pause key goes through the three
landings: 1 (a long pulse of the pointing hand), then 2 (a pulse of middle
length), then 0 (a short pulse), and 1 again. Ctrl with Numpad Plus makes the
rate of the land lower by 0.25 (slower), and Ctrl with Numpad Divide makes it
higher, from 1.0 to 10.0. Each press gives a log line. An object that touched
the land before a press keeps its landing. The mod uses the Pause key for
nothing else. Whether the game uses it is not known [G].

**The fallback.** When the hook on `fn_007FDE40` fails, a thrown object has
the game's bounce and no brake, and the log says so at the start and at each
first contact. When the list of calls after the Hand's draw is full, a
release in the glide starts at the point of the glide, and the log says so.

### The log

Tag `hand throw`. "The contact of": the points and the constants of the
object. "The landing of": the landing that the flight latched, the two rates,
the bounce and the friction of the brake. "After the first contact": the time
and the way to the rest, the top speed, the hops (count, the highest, the
longest, the part of the time in the air), the changes of the vertical
direction of the center (a move of 0.5 mm of the room or more counts), the
turns, the rate at the first contact and at the end, the steps with a cut
force, the frames that the mod drew after the newest step, and the times that
the picture turned back. With `throw_log=2` the lines `s` (each substep: the
time, the center, its height over the land, the points in contact, the
velocity, the turn, the rate) and `d` (each frame: the drawn place, and how
late it was) follow each first contact. `throw2/trace.py` in the session
scratchpad reads them.

### Tests

The math test has these checks in `throw_checks.h`: the rate
of the land (no touch, one time constant, the end, no step larger than 5
percent of the change in a substep), the factor of the low bounce, a ball on
the contact of the game with and without the low bounce (it comes to rest and
does not lie deeper), the brake (its size, the stop in one step, a slide of
15 units per second that stops after 1.91 s, a slope of 30 degrees that holds
the block and one of 45 degrees that does not), the turn between two matrices
with three scales, the numbers of the log line, and the catch up of the
picture.

**Desktop runs** (2026-10-03, `throw_queue.ps1` in the session scratchpad):

- **tbase0, tbase1**: the landing as it was, in the table above.
- **tland1**: the rate of the land and the low bounce, no brake. 9.11 s to
  the rest, 0.62 m, 12 hops of 0.1 cm at most.
- **tland2**: the table above (a friction of 0.80 for the brake).
- **tland3**: the brake at the full friction of the object, see above.
- **tland4**: the defaults of the build that landed (a brake of 60 percent, a
  friction of 0.79), with a throw that lands at 1.89 m/s. 2.54 s to the rest
  and 0.15 m. The picture moves at 42 cm/s in the first 0.25 s. Then the rock
  slides at 15 cm/s for 1 s, and it is still 1.5 s after the first contact.
  The game says "at rest" 1 s later, when the slow turn in place is over. The
  center changed its vertical direction 38 times by 0.1 cm, all in the slide.
  The first contact is 0.4 cm from the real arc.
- **tland2m, tland3m**: `throw_land=2`. 0.72 s and 0.64 s to the rest, 0.23 m
  and 0.12 m.
- **ttree2**: a tree with `throw_land=1`. 2.88 s to the end, 0.19 m, 0.3
  turns at 1.0 a second at most, 4 changes of the vertical direction. Then
  the game planted the tree ("the game took the object out of its physics").
  16 of 251 frames came after the newest step, 9 ms at most. The first
  contact is 8.3 cm from the real arc, because a point of the tree touches
  the land before the center is there.
- **tearly0, tearly2**: a release in the glide, see "The glide".

**Not tested:** a villager, a throw into a pile of objects, a throw into the
sea with the new landing, and a scale other than 100 units per meter. At 44
units per meter the rate of the flight is 3.33.

**Needs the headset.** Whether the landing looks natural at the rate of the
land, or too slow: on the slope of the pen the rock moves at 6 cm/s and less
for 2.5 s before it stops. Then try `throw_land=2` with the Pause key, or a
higher rate with Ctrl and Numpad Divide. Whether the change of the rate
shows at the first contact. Whether a thrown villager still lives through a
soft throw and dies from a hard one.

## The release at once (2026-10-03)

Buck, after the headset test of 2026-10-03: "Release timing is weird too, it
seems like I release my grip but the object doesn't release or something,
hard to get a natural feeling throw to work."

The throw speed and the flight are in "The throw physics". This section is
the time from the opening of the real hand to the moment the object leaves
the Hand.

### What his log shows

The log has 59 releases of the grip. Each release has four moments:

| Moment | Frame | Cause |
| --- | --- | --- |
| The grip opens | F | The grip value comes below 35 percent |
| The click of the open grip | F and F + 1 | `GripStep` presses on the frame of the opening and releases when hand mode `0x17` shows |
| The release action `FUN_005D4DB0` | F + 2 | The game runs it one frame after the button goes up |
| The object leaves the Hand | F + 4 to F + 13 | The packets of the release wait for the next game turn |

- **No click was missed.** Each report window has as many clicks as openings
  and late objects, and as many throws as clicks: 59 of each.
- **The influence gate stopped no release.** No throw went "from the last
  point inside your influence", and no line says that the game kept an
  object.
- **The hold spring was not late.** Hand mode `0x17` showed one frame after
  each press. So the release action came 2 frames after the opening in 49
  releases and 3 frames in one (21 to 55 ms). That is the least that a click
  can take.
- **Nine releases were late objects.** The grip opened during the pick up,
  and the click waited 5 to 10 frames for the object.
- **The game turn.** The log shows all four moments for 23 releases. The
  object left the Hand 2 to 11 frames after the release action (5.8 as a
  mean), and 4 to 13 frames after the opening (7.8 as a mean, 87 ms at 90
  frames a second). In that time the Hand is in the Holding state, so the
  game draws the object at the Hand, and the Hand is on the real hand. A hand
  that swings at 3 m/s moves 26 cm in 87 ms. Then the object goes back to the
  release point of packet `0x4D` and starts its flight. This is the release
  that "doesn't release".
- **The grip value.** That build did not log the grip value. So the time
  from the start of the real opening to the level of 35 percent is not
  known.

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
is as before. `release_at_once=0` in `bwvr.ini` leaves the function unhooked.

**The grip opens on a fast fall.** A closed grip opened below 35 percent
(`kGripOpenBelow`). A hand that opens for a throw takes some frames to come
that far. `GripLevelStep` (`controller_math.h`) also opens a closed grip when
its value falls by 30 percent or more in 3 frames, to a value below 80
percent. A slow change of the grip opens it only below 35 percent, so a hand
that relaxes during a carry does not let go. After a fast opening the grip
closes again only when its value rises by 10 percent from its lowest value,
or from below 55 percent. Only the control that is the game's right button
with the direct hand has this rule: the grip, or the trigger with
`controller_swap_buttons=1`. The other control and the pointer of M2 keep the
plain levels. `grip_fast_open=0` in `bwvr.ini` gives the plain levels to
both.

The click of the open grip stays: 2 frames from the opening to the release
action. A release with no click would need the mod to put the interface in
action 12 by itself, and the game's press does more than that (the throw
spring of the Holding state). Not built.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `release_at_once` | 1 | 1 runs a game turn on the frame after a release of the Hand and after the opening of the grip in a bulk take. 0 waits for the game's own turn, up to 100 ms |
| `grip_fast_open` | 1 | 1 also opens a closed grip on a fast fall of its value. 0 opens it only below 35 percent |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The A/B key.** With `test_keys=1`, the star key of the number pad (`*`,
`VK_MULTIPLY`) turns the release at once off and on while the game runs: the
game turn after a release, and the fast opening of the grip. The pointing
controller gives a long pulse for on and a short pulse for off, and the log
has a line for each press. So the player can feel the old release and the
new one in one session. The mod reads the key only while a window of the
game is in front, and it does not take the key from the game. F11 and F12
have no free combination left (`PollHotkeys` in `stereo.cpp`), and Scroll
Lock is the A/B key of the throw law. Whether the game itself uses the star
key is not known [G].

**The cost [V for the code].** `GGame::Loop` keeps the time since the last
turn (`LoopTimeRemainder`, `0x00D019BC`, at `0x0054D2E7`). It takes 100 ms
from it when the turn count changes, and it keeps it from 0 to 99
(`0x0054D325`). The game time at `GGame+0x25053C` is 100 times the turn count
plus this time. After a turn that came early, that game time goes forward by
up to 100 ms at once, and then it waits for the same time at 99. So a thing
that the game moves by this time jumps a part of a turn forward and then
stands still for that part. This comes once for each release. Which things
use this time was not traced, and no run showed the effect.

### The log

Tag `hand throw`: "the object left the Hand N frames after the release
action" for each release, or "the Hand still holds the object" 90 frames
later when the game refused the drop. Each report window: the count of
releases, the mean and the largest count of frames, the turns that the mod
asked for and the turns that the game ran. Tag `grip`: for each opening of
the grip, whether it was a fast fall, and the values of the grip over the
last 12 frames. Each report window counts the two kinds of opening. The line
`hooks : GGame::LocalTimerSaysDoATurn` shows the hook at the start.

### Tests

The math test has 24 checks of the grip level (`LevelChecks` in
`grip_checks.h`): the plain levels as before, the same answer as the old rule
on 400 values with the fast opening off, a release in 8 frames that opens the
grip on frame 3 (the old rule: frame 6), a grip that relaxes by 2 percent a
frame and opens only below 35 percent, a fall of 28 percent in one frame and
a fall to 82 percent that keep it closed, a fall of 31 percent in 3 frames
that opens it, the rise that closes it again, and the trace of 12 values.

**Desktop runs.** None yet, because the PC was in use. The runs to do, with
the start dialog of `god-view.md`, section 12, `far_test_influence=1` and
`far_grab_max_m=4.0` (so the pen has a far target that is no tree), the
points `-14,2,100 -15,-11,100 -10,-10,60 -10,-4,66` and
`point@2990+1 aimfar@3000+1 grip@3100+101 pull@3140+3 point@3200+1`:

- **gt4**, with the defaults. Look for the hook line of
  `LocalTimerSaysDoATurn`, and for "the object left the Hand N frames after
  the release action" with N of 1 or 2.
- **gt5**, with `release_at_once=0`. Look for the same line with N from 1 to
  6 at the frame rate of the desktop, as before.

**Not tested:** a game turn at once while a story or a script runs, and in a
saved game that loads.

**Needs the headset.** Whether a throw now leaves the hand when the hand
opens. Whether the fast opening lets go of an object that the player wants to
keep: the lines of the tag `grip` show the grip values before each opening.

## The Creature commands (2026-10-03)

Buck, after the headset test of 2026-10-03 (build a785a15): "I am unable to
tell the creature where to go properly, I need to look directly down and
press the grip when touching the ground, I think there is a bit of a conflict
with the distance grab feature too ... maybe we can use the trigger button to
tell creature where to go and what to interact with instead of the grip
button when in leashed mode? It would be good if we could use the same
highlight system as we have for distance grab, but pressing the trigger would
make creature interact with the highlighted object, or holding the trigger
would tell creature where to go." And: "It's impossible to remove the
creatures leash by shaking your hand."

The code is in `src/mod/creature_command.cpp`, with the pure math in
`creature_command_math.h`. `controllers.cpp` gives it the trigger,
`hand_direct.cpp` the candidates of the cone, `hand_glow.cpp` draws the halo,
`overlay.cpp` the arc and the ring, and `hint_card.cpp` the prompts.

### What the game does with the mouse [V]

All of this is from `disasm.py` on `runblack.exe` 1.42.

**The leash.** `GInterfaceStatus+0x12C` is the `GLeashStatus`. `+0x14` is 1
while the leash is on, `+0x1C` is the kind of the leash (1 to 3), `+0x24` is
the object that the leash is tied to (null while the Hand holds it), and
`+0x38` is the `GInterfaceStatus`. `GLeashStatus::SetOn` (`0x005E6F70`) turns
it on and off. `Creature::GetInterfaceStatusLeashOn` (`0x004CF060`) gives the
status whose leash is on a Creature. The player's Creature is `GPlayer+0xA4C`.
`GInterface` keeps the three drawn leashes at `+0x198` to `+0x1A0`.

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

**The buttons.** The message handlers at `0x005D98E0` to `0x005D9960` set the
bits of `GInterface+0x39`: 1 for a left press, `0x20` for a left release, 2
for a right press, 8 and 2 for a right double click, and `0x40` for a right
release. The step of the default action (`fn_005D5190`) reads them: bit 8
runs `fn_005D4210`, bit 1 `fn_005D1320` and bit 2 `fn_005D1330`.

**A click sends the Creature to a place.** The right button (the Action
button) is the one. `fn_005D1330` is the right press with an empty Hand. With
no object in the next pick record, it calls `fn_005D3D10` with the land point
`GInterface+0x3F0` (`0x005D1548`). `fn_005D3D10` (thiscall, a `MapCoords*`,
`ret 4`) sends packet `0x5E` when the leash is on (`+0x14`), the Hand holds
it (`+0x24` is null), the player has a Creature and the action of the
Creature now is not `0x5D`. Else it does what a press on the land does with no
leash. The action is at `[Creature+0x164] + 0xFD8 + 0x60 * index`, with the
index at `+0xFB4`. What action `0x5D` is was not traced.

**A click on an object.** An object that the Hand can take is picked up, as
with no leash (`fn_005D1740`). For each other object, `fn_005D1740` calls
`fn_005D3930`. With the leash on the Hand, `ValidAsInterfaceTarget` (slot
`0x6F0`) at 1 and `ValidAsInterfaceLeashTarget` (slot `0x6F4`) not 0, it
calls `fn_005D4180` (thiscall on `GInterface`, the object, `ret 4`), which
sends packet `0x5D`. Else the object gets its own tap (packet `0x20`). Both
slots give 1 for `Object`, so almost each object is a target. A double click
on an object ties the leash to it (packet `0x5C`), and a double click on that
object or on the Creature takes it back.

**No marker on the land.** The first 0x320 bytes of `ForceActOnMapCoord`
make the actions of the Creature and play a sound. They make no object and
no effect at the land point. The rest of the function (3296 bytes) was not
read. The game shows a tooltip beside the Hand
(`GInterface::CalculateLeashToolTip` `0x005D67F0`): `0xEA2` over bare land,
`0xEA4` over an object that the leash can go to, and `0xEA3` over the object
that holds the leash. The symbol file has no draw function of a footprint
(`Creature::DrawLeashInfo` `0x004CEFB0` is 16 bytes). So the mod draws its own
ring. [I: the game has no marker. A search of the whole draw path was not
done.]

**The shake.** The shake is the gesture of type 5, the scribble.
`GInterface::ProcessPowerUpSystem` (`0x005CF300`) tests the ring of pointer
samples against the templates of that type with `fn_005CF220(5)`. At
`0x005CF725`, with an empty Hand (`GInterfaceStatus+0x90` is 0), the leash on
and no object on the leash, a match clears the ring (`fn_005CE420(0)`) and
sends packet `0x5F` (`0x005CF76B`). The test reads the turns and the
directions of a path in pixels (section 5). It has no speed and no size, so
there is no threshold in it. Other branches use the same gesture: it drops a
Miracle, it stops a chain of gestures, and in the leash selection it closes
the selection (`DoLeashSelectionSystem`, `0x005D0283`). The gesture of type
`[0x00DA0590]` puts the leash on and opens the selection of its kind.

**Why the shake did nothing in the headset.** While the controller owns the
pointer, the mod drops the game's own samples of the pointer (the hook on
`fn_005CEAD0`, "M6 as built"). Samples go into the ring only while the draw
button is held. So a shake of the hand never came to the game's test.

### What the trigger does

The command acts with `creature_command=1` (the default), while the leash is
on the player's Creature and the Hand holds it, the controller places the
Hand, and the Hand is empty in its Normal state. It is off with
`controller_swap_buttons=1`, where the trigger does the work of the grip.

Since 2026-10-04 the target of a held trigger is live, an object takes the
aim only in a tight cone near the player, and a double press ties the leash
to an object. See "The live target of a command" and "The leash on an
object". The table and the two paragraphs "The aim" and "The press chooses"
below say what `creature_command_rule=1` does.

| The trigger, with the leash on | What it does |
| --- | --- |
| A press with the aim on an object (the green halo), and a release with the aim still on it | The Creature acts on the object: the game's `fn_005D4180` sends packet `0x5D`. The length of the press does not matter |
| A press with the glow of the Hand on an object: the object that the grip acts on, within `hand_pick_cm` | The Creature acts on that object: packet `0x5D`. The aim does not count |
| A tap with the aim on no object: a press shorter than 250 ms | The release sends packet `0x5E` with the land point of the press: the Creature goes there. The green ring stays on the place for 0.6 s. `creature_command_hold_ms` sets the time |
| A hold with the aim on no object: 250 ms or more | The arc of the teleport and a green ring show the land point. The release sends packet `0x5E` with the point of the release |
| A press shorter than 450 ms with the aim on the player's own Creature | The leash click, packet `0x5F`: the leash comes off |
| A press while the game's pick has the Creature under the Hand | Not a command. The press goes to the game: the stroke, the slap and the short press that changes the leash, as in "The Creature by touch" |
| A drag of the land | None. The trigger of a command does not reach the game. The grip of the other hand, the stick and the teleport move the player. In the God view the trigger moved nothing before |
| A release with the aim off the target, on the sky, on the sea or off the map | Nothing |

With no leash, the trigger does what it did before.

**The grip keeps its jobs** (take, far grab, bulk take). One job is gone: a
grip on bare land sent the Creature there (the game's right press). A hook on
`fn_005D3D10` leaves the packet out while the controller places the Hand, the
leash is on the Hand and `creature_command=1`. It clears the pick lock and
gives 1, as the game does after the packet. The mouse gets the game's
function. This was the conflict with the grab from a distance: a grip that
missed its target sent the Creature to the land under the Hand.

**The aim.** The target is from the aim pose of the pointing hand, with the
cone and the hysteresis of the grab from a distance (`FarConeScore`,
`FarPickTarget`, 8 degrees for a new target and 14 to keep it). There is no
limit of the range: each object that the draw offers in the cone can be the
target, when the land does not hide it (`FarVisible`). The tests are the
game's: slot `0x6F0` gives 1 and slot `0x6F4` does not give 0. The target of a
far grab is also the target of the command, so the player sees one halo. The
player's own Creature is a target too: its body bone, with its 2D radius.
The cone needs `far_grab=1` and the God view or the table. Without them, only
the land command acts. A command has no least distance from the Hand, and the
object at the Hand is a target: see "A target near the Hand, and a target in
the sea".

**The press chooses.** A press with a target under the aim is a press on that
target, and the target stays until the release. A press with no target is a
press on the land: no object takes the aim while it is held, so a land point
next to a house can be the target. `ReleaseCommand` and `ReleaseLandAim` in
`creature_command_math.h` are the rule.

**A tap and a hold on the land** (2026-10-03, afternoon). Buck, after the
test of build `f194c59`: "it seems I need to hold the trigger for a few
seconds in place before creature will walk to that spot. It doesn't seem to
work if I just quickly point and click."

His log has 26 presses on the land with the leash on:

| Presses | Length | What the mod did |
| --- | --- | --- |
| 16 | 82 to 242 ms | Nothing: "the press was shorter than creature_command_hold_ms" |
| 3 | 466 to 2542 ms | Nothing: the arc was on the sea at the release |
| 7 | 387 to 5012 ms | Packet `0x5E`: the Creature went to the land point |

So each quick click was refused by the hold time of 250 ms. After a row of
clicks that did nothing, he held the trigger for 1 to 5 s. The other causes
were examined, and they are not in his log. No press went to the game (each
report window says 0). The filter of the aim starts again at each press, so
the first aim has no lag. The packet goes with the next game turn. Of his 6
presses on an object, 5 sent the Creature (168 to 2422 ms) and 1 lost its
target.

The rule now (`ReleaseLandAim`):

- The mod evaluates the land aim in the pump of the press, not one frame
  later. That first aim is the aim of the press.
- A tap, a press shorter than `creature_command_hold_ms`, sends the Creature
  to the aim of the press. The pull of the trigger and its release move the
  hand, so the aim of the release is the worse one for a tap.
- A hold sends the Creature to the aim of the release, as before. The player
  moved the arc to that place.
- A tap whose first aim met no land uses the aim of the release, if that one
  is valid. A hold that ends on the sea sends nothing.
- After each send to the land, the ring stays on the place for 0.6 s
  (`kSentMarkS`), with no arc. So a tap shows where the Creature goes. The
  ring does not move, and a new press takes its place.

**The land aim.** The arc is the arc of the teleport (`teleport_reach_m`), a
straight ray on the table. The aim goes through the same one euro filter
(`aim_smooth.h`). The target is valid when it is on the map and not the sea.
The ring has the size of the teleport ring (`teleport_ring_cm`). The arc and
the ring are green, the color of the trigger, and red for a target that is
not valid.

**The halo** is the halo of the grab from a distance, in green. A gold far
target keeps its gold halo, also when the trigger can send the Creature to
it. A red far target that is the target of a command has the green halo.

**A target near the Hand, and a target in the sea** (2026-10-03, evening).
Buck, after the test of build `3918f87`: "I did a quest where I must send my
creature to send drowning sailors in the water and for some reason I simply
could not send my creature to interact with a few of them, they were
highlighted red, but if I reached out physically (Not the trigger target)
then I could get my creature to grab them ... seems like water blocks certain
interactions."

The log of that session is lost. The cause is from the code, and the game's
part is from the disassembly.

- **The game does not refuse the water** [V]. The trigger and a press of the
  grip on the object send the same packet `0x5D` through `fn_005D4180`.
  `Creature::ForceActOnObject` (`0x004C5FF0`) has no test of the water at its
  start: it tests the leash (`Creature::IsLeashWorking`), a fight, and two
  values of the Creature. A press of the grip on a sailor outside the
  influence goes from `fn_005D1740` (`0x005D17A5`, the influence `+0x48` is 0
  and slot `0x714` gives 1) to `fn_005D3930`, which sends the packet. That is
  why his reach worked.
- **The red halo** was the halo of the grab from a distance. A sailor is an
  object that the Hand can hold, and the sea there was outside his
  influence. The halo of a far target was drawn before the halo of a command,
  so the green halo did not show.
- **A band with no target.** The cone of a command took `FarConeScore` with
  the real distance from the Hand's point, and that function gives no target
  less than 35 cm (`kFarMinHandM`) from the Hand. The grab from a distance
  takes 35 cm for a nearer object since the rule of the near objects, and its
  least distance is 15 cm. So an object 15 to 35 cm from the Hand's point had
  the halo of the far grab and was no target of the command. In the God view
  the land is at the height of the hands, and a player who points at a near
  object has his hand that near.
- **Then the press went to the land.** A press with no target is a press on
  the land, and it sends the Creature to the land point of the arc. Over the
  sea the arc has no land point ("the target is the sea"), so the release
  sent nothing. On the land the same fault sent the Creature to the land
  beside the object, so it did not show. [I: this is what the code does. No
  log shows that it is what happened at the sailors.]
- **A list of 48.** Each object is a valid target of the leash, and the list
  of the cone held the first 48 that the draw offered. With a forest in the
  cone, an object that came later was no target.

What the mod does now:

- A command has no least distance from the Hand. The score takes 35 cm for a
  nearer object (`FarScoreMeters`).
- The object at the Hand is a target: the object that a press of the grip
  acts on, within `hand_pick_cm` of the Hand's point (`HandDirectNearObject`,
  the choice of `Choose`). It wins over the aim. So the trigger with the glow
  on a sailor sends the Creature to him, as the grip did.
- The list holds 128. When it is full, a better target takes the place of
  the worst one.
- A red far target that is the target of a command has the green halo
  (`DrawHalo` in `hand_glow.cpp`). A gold far target and a locked one keep
  their halo.
- The target of a press keeps the first word of the object. The release
  sends nothing when the game deleted the object in that time
  (`HandDirectObjectLives`).

**The land command and the sea** [V]. The game takes a point in the sea for
packet `0x5E`: `Creature::ForceActOnMapCoord` asks `MapCoords::IsWater` at
`0x004C6C87` and then forces an activity of its own for the water (desire 3
with action `0x2A`, or desire `0xE` with action `0x37` or `0x2C`). What the
Creature then does was not traced. The mod does not send a point in the sea:
the ring is red there, as before. A sailor is a target as an object.

**Haptics.** A tick of 0.15 at a new target, 0.3 for 30 ms when a command is
sent, and 0.6 for 80 ms when the leash comes off.

### The shake of the real hand

The mod does not feed the game's shape test. A real hand moves all the time,
and the same ring starts the Miracle gestures and changes the kind of the
leash. So `ShakeStep` in `creature_command_math.h` finds the shake in the
motion of the real grip in the room, in meters and seconds:

- The anchor is the last turning point. The far point is the sample farthest
  from the anchor. When the hand comes back 2 cm from the far point, the
  stroke from the anchor to the far point is done.
- A stroke counts when it is 6 cm long or more, its mean speed is
  `leash_shake_mps` or more (0.6 m/s), and its length is 75 percent of its
  path or more. So a circle does not count.
- A stroke adds to the row only when it goes back along the stroke before
  it (the dot of the two directions is -0.5 or less).
- 4 strokes in a row, in 0.9 s or less, are a shake. The next shake needs 1 s.
- A stroke that takes more than 0.6 s, and a gap of 0.1 s in the samples, end
  the row.

A shake of 6 cm to each side, 4 times a second, has strokes of 12 cm at
0.96 m/s, and the detector fires after 0.6 s. Then the mod makes the game's
own tests (an empty Hand in its Normal state, the leash on, no object on the
leash) and does what the game does on a match: `fn_005CE420(0)` and packet
`0x5F`. No trigger, grab, gesture or teleport can run at that time. The
controller gives a pulse of 0.6 for 80 ms when the game turns the leash off.
With the mouse, the game's own test acts as before.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `creature_command` | 1 | 0: the trigger does not command the Creature, the grip on the land sends it as before, and no shake is read |
| `creature_command_hold_ms` | 250 | A press on the land shorter than this is a tap, and it uses the aim of the press. A longer press uses the aim of the release. 0 to 2000 |
| `leash_shake_mps` | 0.6 | The mean speed of each stroke of a shake, in m/s. 0 turns the shake off. 0.2 to 5 |
| `creature_command_rule`, `creature_command_cone_deg`, `creature_command_keep_deg`, `creature_command_max_m`, `creature_command_behind_cm` | 2, 3, 5, 4, 30 | See "The live target of a command" |
| `creature_command_double_ms` | 500 | See "The leash on an object". 0 turns the double press off |

A test key for the desktop runs only: `creature_command_test=N`. From frame N
of the mod the leash counts as on. The commands go to the log and no packet is
sent.

### The prompts

| When | The card shows |
| --- | --- |
| The leash is on and the Hand is over the land | "Trigger Send the Creature" |
| The aim is on an object | "Trigger Send the Creature" (after "Grip Pull to fetch" for a far target) |
| The aim is on the player's own Creature | "Trigger Take the leash off" |
| The trigger is held and the arc shows | "Trigger Release to send" |
| The aim is on an object that can take the leash | A second line: "Trigger x2 Tie the leash" |
| The leash is tied, and the aim is on the object that holds it | "Trigger x2 Take the leash back" |
| The leash is tied, and the aim is on the player's own Creature | "Trigger Take the leash back" |

### The log

The tag `creature cmd`: the keys at the start, each new target, each press
that the command takes and each press that goes to the game with the reason,
each release with what was sent, each shake with its strokes and speeds, each
change of the leash, and one line each report window. A new target in the
aim has its distance from the Hand, and a new target at the Hand has the line
"the Hand is on the".

```
creature cmd : the aim is on the Abode 0F3A2C10, 2.1 degrees off the ray, 1.42 m of the room from the Hand, at 2310.4 15.2 1987.0. A press of the trigger sends the Creature to it, frame 5120
creature cmd : the trigger opens after 180 ms on the Abode 0F3A2C10. The game sends packet 0x5D: the Creature acts on it, frame 5141
creature cmd : the trigger closes with the aim on no object. It does not go to the game. The arc of the press meets 2290.1 14.0 2001.7 (the target is land), frame 5300
creature cmd : the trigger opens after 130 ms, a tap, so the aim of the press counts. Packet 0x5E: the Creature goes to 2290.1 14.0 2001.7, frame 5312
creature cmd : the trigger opens after 910 ms, a hold, so the aim of the release counts. Packet 0x5E: the Creature goes to 2301.6 14.2 1998.3, frame 5420
creature cmd : a shake of the hand: 4 strokes in 510 ms, the slowest at 0.88 m/s, the last one 12 cm long. A stroke needs 0.60 m/s (leash_shake_mps). The leash click (packet 0x5F), frame 6010
creature cmd : the leash is off, 70 ms after the shake, frame 6016
```

(Examples of the format. No run made these lines.)

### Tests

**The math test** has 35 more checks (`command_checks.h`): a shake of 6 cm
at 4 Hz fires in 0.6 s after 4 strokes, on each axis and at 45 samples a
second, and a shake of 10 cm at 3 Hz. No shake: a wave at 1.5 Hz, a tremor of
1 cm, strokes of 4 cm, a still hand, a reach of 40 cm and back, the swing of a
throw, two slaps, four fast moves around a square, a fast circle, and a row
that a gap in the tracking cuts. The threshold: at 1.5 m/s, strokes of
0.96 m/s are no shake and strokes of 1.6 m/s are. A shake of 1.5 s is one
shake. Then the 10 cases of `ReleaseCommand`. The motions are sine waves and
eased moves, not records of a real hand.

The tap has 14 more checks: the 9 cases of `ReleaseLandAim` (a tap, a hold,
the limit of 250 ms, a tap that starts or ends on the sea, and
`creature_command_hold_ms=0`), the 16 short presses of Buck's log, which
each send now, and the time of the ring after a send.

**Desktop runs.** Run cc1 (`command_run.ps1` in the scratchpad) was refused,
because the PC was in use. It is the rig of the throw runs with
`creature_command_test=2950`: a press of 70 frames with the aim on the land,
a press of 6 frames, `aimfar`, then a press of 12 frames and one of 50 frames
on the object. Look for "the trigger closes ... It does not go to the game",
"the arc of a Creature command is drawn", "test: the mod would send packet
0x5E" and "0x5D", and "nothing is sent" for the press of 6 frames. No desktop
run can reach an owned Creature on a leash from a new game.

**Not tested:** all of the code that acts in the game. No run of the game was
made with this build. That is: each packet in a game with a leashed Creature,
the hook of the grip on the land, the Creature as a target of the aim, the
halo, the arc and the prompts, and the shake with a real hand.

**Tests of the evening of 2026-10-03.** The checks of the near target are
in `far_checks.h` (see "The grab from a distance").

**Desktop run cmd1b** (`creature_command_test=1000`, `far_test_thrown=1`,
`far_test_least_cm=5`, `fake_controller_play_frame=2750`, the frames are
1395 later than the frames of the script):

- A far fetch of a rock, then a set down with a still hand at frame 4548.
- At 4574: "hand pick : 2927DC94 (Rock) is 3.99 units from the Hand's point
  (4.0 cm of the room)" and "creature cmd : the Hand is on the Rock 2927DC94
  ... A press of the trigger sends the Creature to it".
- At 4607 `aimfar` gave the rock 0.03 m of the room from the Hand. Before
  this change an object that near was no target of a command.
- A press of 6 frames at 4645: "the trigger closes with the aim on the Rock
  2927DC94. It does not go to the game", then "test: the mod would send
  packet 0x5D with the Rock 2927DC94".
- The word `seek` put the Hand's point on the rock (0.0 cm), and a second
  press at 4765 gave the same two lines.
- The report: "2 presses of the trigger were commands and 0 went to the
  game. Sent: 2 to an object".

**Not tested:** an object 15 to 35 cm from the Hand in the aim (the rock of
the run was at the Hand), a sailor in the sea, an owned Creature on a leash,
the list of 128 with a forest in the cone, and the green halo on a red far
target.

**Needs the headset.** Whether a tap sends the Creature to the place that
the player pointed at, and whether a trigger that closes by accident sends
it too often. Whether 0.6 m/s and 4 strokes fit a real shake, and
whether a normal move of the hand never takes the leash off. Whether the
Creature does what the player expects on each kind of object. Whether the
halo on almost each object in the cone is too much while the leash is on.

## One point for each interaction (2026-10-03)

Buck, after the headset test of the afternoon of 2026-10-03 (build
f194c59): "I am still having trouble interacting with objects, I kept trying
to click on a quest scroll, but I kept picking up trees all around. I don't
think the little glowing orb on the index finger is actually where the
interaction is taking place, the interactions happen more at the palm of the
hand ... We might also want to make it so the distance grab is disabled if
our hand is physically near an interactable object." And: "I am unable to
interact with forests to grab wood at all now." And: "The leash model on the
hand is rendered in the wrong place, it's rendered below my actual in-game
hand."

The code is in `src/mod/hand_direct.cpp` (the pick, the rule of the near
objects, the leash), `far_grab.cpp` (the far touch, the forest),
`hand_glow.cpp` (the colors) and `hand_exact.cpp` (the palm of the mod's
hand). The pure math is in `near_math.h`.

### Where each interaction took its place from

| Interaction | Its place in build f194c59 | Its place now |
| --- | --- | --- |
| The glow | The Hand's point: the tip of the real index finger at rest (`g_target`) | The same |
| The pick up of an object, and the take from a pile (`Choose`) | The nearest body within `0.6 * 3.2 * size` of the Hand's point. For a hand of 19 cm that is 11.4 cm of the room, at each scale | The nearest body within `hand_pick_cm` (4 cm) of the Hand's point |
| A scroll, a house (the game's tap) | The game's own pick: the mesh under the pixel of the Hand's point, at each depth. Not a place in the room | The same list as the pick up: the nearest body within 4 cm of the Hand's point. The game's pick at the pixel stays for an object that is not near |
| The Creature | The game's pick at the pixel, then the capsule of the body about the Hand's point ("The Creature by touch") | The same |
| The grab from a distance | The aim pose of the controller | The same. It is off while an object is near the Hand's point |
| A held object | The palm of the mod's hand ("One pair of hands") | The same |
| The end of the leash at the Hand | The wrist bone of the game's Hand | The palm of the mod's hand |

So each press acted from the Hand's point, where the glow is. No interaction
used the palm, the wrist or the grip pose. But the reach of the pick was a
ball of 11.4 cm about the index tip. The palm is 9 cm from the tip, so that
ball holds the whole hand. An object at the palm, or 11 cm past the tip, was
taken. His log has these lines:

```
hand pick : 2AA66324 is 11.38 units from the Hand, inside 11.40 ... frame 2411
hand pick : 2AA66324 is 11.35 units from the Hand, inside 11.40 ... frame 2416
```

The scale there was 100 units a meter, so the tree was 11.4 cm from the glow.
The body of an object was a cylinder of its 2D radius, from its point to two
radii above it, so the crown of a tree was outside its own body.

**The glow stays at the index tip.** The other choice was to move the glow to
the palm. The index tip is better for a real hand. A person sees the tip and
aims with it, and the hand hides its own palm. A touch is a move of the
finger tip. The land stops the point, so a point at the palm keeps the
fingers 9 cm in the land before the palm comes to a villager. A villager in
the God view is 1 cm wide, and that needs a fine point.

### What happened at the scroll

The log has no class for a near pick, so the scroll does not show by name.
These lines show it:

- **Frame 5364.** The hint card says `[Grip] Activate` at frames 5272 and
  5353. The right button goes down at 5364, and the card says `[Grip]
  Reminder` at 5380. That is a scroll that the grip activated. No tree and no
  far target was near, so it worked.
- **Frames 52302 to 53259.** The glow shows "something to touch" in 50 eye
  passes of that window. At frame 52302 the game's record holds `2AA8074C`, an
  object that the Hand cannot hold (the mod takes each holdable object out of
  the record). That is the scroll [I]. But the far grab had a target in its
  cone, so it claimed the grip: 2 locks, 2 pulls and 2 fetches. The fetch of
  frame 52304 took `2AA52E0C`, 1.2 m of the room from the Hand, and the fetch
  of frame 52543 took `2AA520EC`, 1.7 m away. The log does not have their
  class, because the lines of the far grab were full. Buck says that they
  were trees. He set the second one down with a still hand (0.04 m/s) at
  frame 52629.
- That object was then inside 11.4 cm of the point. The mod's pick chose it,
  and the table hook kept each other object from the game's pick ("11253 kept
  from the game's pick for the choice" in the next window). So the game did
  not see the scroll. The grip took the same object `2AA520EC` at frames
  52694, 52909, 53097 and 53148, and he set it down each time with a hand at
  0.01 to 0.03 m/s. The glow showed "something to take" in 362 eye passes and
  "something to touch" in 6.

So two faults kept him from the scroll: the far grab took the grip, and an
object inside the large reach hid the scroll from the game.

### The pick at the glow

After the first eye pass, `Choose` measures each candidate that a press of
the grip acts on (`PressKind`):

| Kind | The test | What the grip does |
| --- | --- | --- |
| hold | `Holdable`: the tests of `PlaceObjectInMagicHand` | The pick up |
| take | `TakeValid`: slot `0x6CC` gives 1 | The bulk take |
| tap | `Tappable`: slot `0x740` (`InterfaceValidToTap`) gives 1, slot `0x180` gives 0, and the object is interactable | The game's tap |
| Creature | slot `0x34` (`IsCreature`) | Not a choice. The game's pick keeps it |

**The body** of an object is an upright cylinder (`BodyGap` in
`near_math.h`). Its base is the object's point, the translation of its mesh.
Its radius is the game's 2D radius (slot `0x64`). Its height is the game's
height: `Object::GetHeight` `0x00638120` (slot `0x42C`) gives the height of
the mesh times the scale of the object, and `Object::GetBoundingSphere`
`0x00637730` puts the middle of an object half of that over its point [V]. An
object with no height gets two radii, as before.

**The body of a tap** is two cylinders, and the nearer one counts. The first
is the cylinder from its point, which also goes half its height below the
point. The second stands about the middle of the box of its mesh in the
world (`BoxMiddle`). Slot `0xF8` of the `LH3DObject` gives the box: its middle
at `+0x18` and its half sizes at `+0x24`. `ScriptHighlight::Draw` takes that
middle through the matrix of the mesh (`0x00709ED7`) and gives the point to
`SendInvisibleDrawCollision` as the place of the scroll (`0x0070A2D7`) [V]. So
the second cylinder is where the game's own pick has a scroll that floats.
An object to hold and a bulk source keep the one cylinder from their point,
as in each build before.

**The gap** is the distance from the Hand's point to the body. It is 0 inside
the body.

**The choice** is the body with the smallest gap, when that gap is less than
`hand_pick_cm` of the room (4 cm). When two gaps agree to 1 mm, the body with
the nearer middle wins (`Beats`). So a villager who stands in a forest wins
over the forest, and a scroll wins over a tall tree beside it. When the
middles agree too, the order is hold, take, tap.

The choice goes in the next pick record, as before (`Inject`). The game then
gives its own hand mode, its own tooltip and its own action. A tap choice is
no choice while the Creature is as near as it is, because the table hook
keeps each other object from the game's pick while a choice stands.

### A scroll [V]

A scroll is a `ScriptHighlight` (vtable `0x0094228C`). All of this is from
`disasm.py`.

- **The button is the Action button: the right mouse button, the grip.** The
  right press with an empty Hand is `FUN_005D1330`. For the object of the next
  pick record it tests `ValidForPlaceInHand` (slot `0x6FC`), and then
  `InterfaceValidToTap` (slot `0x740`, at `0x005D150F`). Each of the two
  leads to `fn_005D1740`.
- `fn_005D1740` calls `fn_005D3930` for an object that the Hand cannot hold
  (`0x005D1833`). With no leash on the Hand that calls `fn_005D38A0`.
- `fn_005D38A0` sends packet `0x20` with the object when the land point of
  the record is in the influence (`GInterface+0x48`) or the object does not
  need it (slot `0x714`), slot `0x740` gives 1 and slot `0x180` gives 0
  (`0x005D3904`).
- The handler of the packet, `fn_005DA650`, calls `InterfaceTap` (slot
  `0x744`, at `0x005DA6D8`). `ScriptHighlight::InterfaceTap` `0x0070AC70`
  sets the scroll active and starts its script.
- `ScriptHighlight::InterfaceValidToTap` `0x0070ADD0` gives 1 while the scroll
  has a script (`+0x78`). `ScriptHighlight` does not need the influence
  (`0x00709840` gives 0), and the Creature cannot be sent to it
  (`ValidAsInterfaceTarget` `0x00709800` gives 0). So the leash does not
  change the tap of a scroll.
- **The trigger does not activate a scroll.** The left press is `fn_005D1320`,
  a jump to `fn_005D1AB0`, which grabs the land.
- The hand mode chooser `fn_005D7F20` gives `0x12` for a scroll: the mode of
  an object that is interactable and that the Hand cannot take.
- A house is an `Abode`, and `Abode::InterfaceValidToTap` `0x00406820` gives
  1 always. `Abode::InterfaceTap` `0x00406830` knocks on the house
  (`HowManyPeople::KnockKnock`).
- The scroll offers itself to the game's pick with
  `GInterface::SendInvisibleDrawCollision` (`0x0070A2D7`): a circle on the
  screen about its point. So the game's pick finds a scroll at each depth
  behind the pixel.

The game shows the tooltip of the tap ("Activate", then "Reminder" for a
scroll that is active), and the hint card shows it with the word of the
grip. So the card needed no change for a scroll at the Hand's point.

### Near objects win

The grab from a distance is off while an object that a press acts on is near
the Hand's point. The objects are the four kinds of the table above, the
Creature too. `Choose` keeps the smallest gap of them (`NearBlocks`):

- The far grab goes off when the gap is `far_grab_near_cm` or less (10 cm of
  the room).
- It comes back when the gap is more than `far_grab_near_out_cm` (15 cm).
- Between the two, the answer of the last frame stays. So a hand that rests
  at the border does not turn the halo on and off.

While it is off, there is no far target, no halo and no claim of the grip.
The grip goes to the game. The grip of the pointing hand is then no grab of
the world either (`HandDirectClaimFree`): the pick reaches only 4 cm now, and
a grip that closes 7 cm from a tree must not move the world. The hook of the table offers each object in view
within the larger of the two distances and 10 units more to the pick, so a
near object is a candidate before the pixel is on it.

**No band with no target.** The cone leaves out an object whose point is less
than 35 cm of the room from the Hand's point (`kFarMinHandM`). The Hand now
takes only what is 4 cm from its point. So an object 20 cm away was no target
of the Hand and no target of the aim, and the aim went to an object behind
it. With the rule on, the least distance of a far target is
`far_grab_near_out_cm` (`FarLeastMeters` in `near_math.h`), and never more
than 35 cm. The score takes 35 cm for a nearer object. So each object is a
target of the Hand, or it turns the far grab off, or it is a target of the
aim.

### The far touch

A scroll in the cone of the aim ray is a far target (`IsScroll`, by the class
name, and `Tappable`), with the range of `far_grab_max_m`. It has the gold
halo. It needs no pull:

1. The grip closes on the scroll.
2. For 2 frames the scroll is in the next pick record (the Pull state, as for
   a bulk source).
3. The grip goes to the game as a press (the Fetch state). The game taps the
   scroll with its own action, as in "A scroll".
4. After 8 frames (`kTouchFrames`) the far grab lets the press go. The grip
   stays held back until it opens.

A scroll scores 3 degrees better than its angle (`kFarTouchBiasDeg`), because
trees stand all around a scroll. No other object that a press taps is a far
target. Else the aim would knock on each house that it crosses, and a press
on the entrance of the Temple from 2 m would take the player into the Temple
[G]. `far_touch=0` turns the far touch off.

`FarDraw` (`far_grab.h`) has two new fields for the hint card: `touch` (the
target is a scroll, and the grip activates it) and `bulk`. The card can then
say "Activate" in place of "Pull to fetch". `HandDirectPressKind()` gives
what the grip does at the Hand's point (nothing, hold, take, tap or the
Creature).

### The order

1. An object within 4 cm of the Hand's point: the grip acts on it.
2. An object within 10 to 15 cm of the Hand's point: no far target. The
   player moves the Hand to the object.
3. A far target in the cone: the grip takes it, collects from it or
   activates it.
4. The game's own pick at the pixel of the Hand's point: a press goes to the
   game, as before. The glow shows nothing while a far target with a gold
   halo stands, because the grip then goes to that target.

### A forest gives a tree

**Why the grip on a forest gave nothing [V].** A press on a `BigForest` does
not put the forest in the Hand. `BigForest::IsTuggable` (slot `0x718`,
`0x00438DC0`) gives 0. So `fn_005D1740` sets only the Tug flag `CHand+0x4908`
and `GInterface+0x450` (`0x005D17F4`), the pick lock and the interface action
13. The step of action 13 is `fn_005D5250` (the table at `0x00D17DA8` is made
by `0x005D0890`, and its entry for 13 is written at `0x005D0C1C`). On each
frame that step reads the object of the next pick record (`+0x400`). When
there is none it gives 0 (`0x005D5295`), and the interface step then cancels
the action (`fn_005D1260`, called at `0x005D1246`). After 225 ms (`0xE1`)
with the Hand empty it clears the Tug flag (`0x005D52D6`), and it then sends
the pick up packet `0x13` through `fn_005D2800`. The handler calls
`BigForest::InterfaceSetInMagicHand` `0x004393C0`, which makes a `Tree` at
the forest's point (`Tree::Create`) and puts it in the Hand.

While the pick is locked the draw offers no candidates, so the mod had no
choice. `Inject` then took the forest out of the record as "a holdable object
far behind the Hand". The step found no object, and the game cancelled the
pick up on the frame after the press. A single `Tree` did not have this
fault: `Object::IsTuggable` gives 1, so `fn_0046DC30` puts the tree in the
Hand at the press, and `Inject` does nothing while the Hand holds an object. This fault was in each build with the
direct hand. Before 8fbebc3 a forest gave a tree only through the grab from a
distance, which keeps its object in the record. 8fbebc3 took the forest out
of the far targets, and then no path was left.

**The rule now.**

- **The Hand in the forest.** Close the grip with the Hand's point in the
  forest, and keep it closed. After the game's 225 ms a tree is in the Hand,
  and the grip carries it. A grip that opens before that gives no tree.
  `Inject` leaves the record alone while the pick is locked.
- **At a distance.** A forest in the cone and in the range is a far target,
  as it was before 8fbebc3. Close the grip and pull. The fetch is done when
  an object is in the Hand, and the glide draws the new tree from the forest's
  point. The fetch waits 90 frames for the tree (`kForestFetchFrames`), in
  place of 40.
- A forest needs the pull, and a pile does not. A forest is large, so the aim
  is on a forest often. A grip that closes for a grab of the world must not
  take a tree each time.
- The game has no bulk take from a forest. Each press gives one tree. Since
  2026-10-04 the mod adds a take while the grip stays closed: see "Many
  trees from a forest".

### The leash at the mod's hand

**Where the end of the rope comes from [V].** `GInterface::UpdateAllLeashes`
`0x005D9130` runs once a frame, from `GInterface::PostDrawProcess`
(`0x005CEAB3`). For each leash that is on it calls
`GInterface::GetLeashStartPoint` `0x005D8EA0` (thiscall, an `LHPoint*` and a
bool, `ret 8`) with 0, and gives the point to the rope (`fn_00848970`, 40
nodes with their own physics) as its end. The other end is a bone of the
Creature. `GInterface::DrawAllLeashes` `0x005D9310` draws the rope through
its nodes (`fn_008491B0`).

With the leash on the Hand (`GLeashStatus+0x24` is null) and the local
interface, `GetLeashStartPoint` gives the translation of the first matrix of
the Hand's pose array, `*(CHand+0x47F0) + 0x24` (`0x005D90B9`). That is the
wrist bone of the game's Hand. With the bool set it gives
`GInterfaceStatus+0xC8`. A leash that is tied to an object, and the leash of
another player, take other points.

**The fault.** The exact Hand puts the wrist bone 2.5 cm from the wrist of
the mod's hand. But the mesh of the game's Hand is hidden, and the player
holds the rope in the mod's hand. The wrist joint is about 7 cm behind the
middle of the palm, and with the fingers up it is below the hand. So the rope
hung from a point below the hand that he sees [I: no picture of his view, and
no desktop run can put the leash on]. The new line of the tag `leash` gives
the distance in the next log.

**The fix.** A hook on `GetLeashStartPoint` runs the game's function first.
Then it writes the middle of the palm of the mod's hand: the point where a
held object touches the palm (`overlay_math::HoldCenter`, 17 percent of the
hand's length behind the knuckles). `HandExactPalm` in `hand_exact.cpp` gives
that point from the exact root of the last compose. The point is made once a
frame.

The hook writes the point only when each of these is true:

- The interface is the local one, the leash is on the Hand, and the bool is 0.
- The controller placed the Hand on the last frame.
- The Hand is in an exact state (Normal, Camera, Tug, Holding, Creature, Grain
  or PlayAnim), and the exact root is from this frame or the one before.
- The overlay drew the pointing hand, and the mesh of the game's Hand is
  hidden.

In each other case the game's point stays, and the log says why (8 lines at
most). The hook goes in only when the first 13 bytes of the function are the
bytes of the disassembly. `leash_at_palm=0` leaves the hook out.

### The colors of the glow

The hint card shows the grip in gold and the trigger in green. The first glow
was green over each object "to touch", a scroll too. But the grip activates a
scroll, and the grip starts the touch of the Creature (the game's hint there
is "Interact" for the right button). The trigger acts on nothing at the
Hand's point. So the glow has no green look now:

| What a press does at the point | Look |
| --- | --- |
| The grip picks up, or takes from a pile | gold, bright, 1.7 cm (as before) |
| The grip activates: a scroll, a house, the Creature, a bubble, a leash | a paler gold, 1.5 cm |
| Nothing, and an object where a press does nothing | pale blue, 1.0 cm |
| The Hand holds an object | pale blue, faint, 0.8 cm (as before) |

For hand mode `0x12` the object of the pick record decides
(`PressKindAtPoint`). The game gives `0x12` also for the Temple and for an
object outside the influence that needs the influence, where a press does
nothing. The glow was green there. It is pale blue now.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hand_pick_cm` | 4 | The reach of the pick from the Hand's point, in cm of the room. 1 to 20 |
| `far_grab_near_cm` | 10 | The far grab goes off when an object that a press acts on is this near the Hand's point |
| `far_grab_near_out_cm` | 15 | The far grab comes back past this distance |
| `far_touch` | 1 | 1 makes a scroll in the aim a far target that the grip activates |
| `leash_at_palm` | 1 | 1 ends the rope of the leash at the palm of the mod's hand |
| `near_rule` | 2 | 2 is one target (see "One target"). 1 is the pick of this section. 0 gives the pick of the first builds: a reach of 60 percent of the Hand's length, no tap choice, no rule of the near objects and no far touch |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The A/B key.** With `test_keys=1`, the point key of the number pad (`.`,
`VK_DECIMAL`) turns `near_rule` off and on while the game runs. The pointing
controller gives a long pulse for on and a short pulse for off.

Test keys for the desktop runs only:

- `seek_forest=1`: the word `seek` goes to the nearest forest.
- `seek_tap=1`: the word `seek` goes to the nearest object that a press taps
  and that the Hand cannot hold.
- `seek_scroll=1`: the word `seek` goes to the nearest scroll that a press
  taps.
- `far_test_forest=1`: only a forest is a far target, and `aimfar` aims at
  one.
- `far_test_scroll=1`: only a scroll is a far target, and `aimfar` aims at
  one.

With one of the three `seek` keys the log has a census of the candidates by
class, and the body and the middle of the box of the object that `seek`
found. With one of the two `far_test` keys, `aimfar` takes a target from 17
cm of the room, because the tests run with the view zoomed out.

### The log

- Tag `hand pick`: the rule and the keys at the start. Each change of the
  choice, now with the class, the kind and the gap in cm of the room (400
  lines, 60 before). Each report window: how many times and for how many
  frames a near object turned the far grab off, and the passes that left the
  record to a pick up of the game.
- Tag `far grab`: "off. The Hand's point is N cm of the room from P (class)"
  and "on again" (80 lines). "the grip closed on the scroll", "the far touch
  is over", and "the forest P gave the new object Q".
- Tag `leash`: one line each time the leash comes on. It gives the palm, the
  distance from the game's point to it, and how far that point is below it.
  The next log shows there where the rope hung before.
- Tag `hand`: each change of the Hand's state and of the hand mode, 300 lines
  (60 before).
- Tag `hand glow`: the look "something to touch" is now "something to
  activate".

### Tests

**The math test** has 47 new checks, 1438 in all at commit af17a90
(`near_checks.h`, the functions of `near_math.h`):

- The body: a point in it has a gap of 0. A point beside it, over it, under
  its base and past its top edge has the gap of the geometry. The crown of a
  tree of height 8 and radius 1.5 is in the body, and with the old top of two
  radii it was 3 units out.
- The reach: 4 cm of the room is 4 units at 100 units a meter and 1.78 units
  at 44.5. The tree of the log, 11.38 units from the point, was inside the
  old reach of 11.40 and is outside the new one.
- The choice: the nearer body wins, a scroll over a tree too. With the point
  in two bodies the nearer middle wins: a villager in a forest, and a scroll
  beside a tall tree. At the same place the order is hold, take, tap. Gaps
  that agree to the tolerance are a tie.
- The near objects: the far grab goes off at 10 cm and at 0, stays off at 12
  and at 15 cm, and comes back at 15.1 cm and with no object. A hand that
  moves between 11 and 14 cm for 20 frames changes nothing, from each of the
  two states. An out gap below the in gap gives no band. A gap that is not a
  number does not turn it off.
- The least distance of a far target: 35 cm with the rule off, 15 cm with
  it on, never more than 35 cm. An object 20 cm from the Hand on the aim ray
  is out of the cone itself, and it is a target with the rule on.
- The waits: the fetch from a forest waits more than twice the game's 400
  ms, and the press of a far touch is down for 8 frames.

**Desktop runs.** None yet: the PC was in use for the whole of the work. Run
near1 (`near_run.ps1` in the scratchpad) started at an idle PC, and then the
real mouse moved. The log says "the mouse owns the pointer (the cursor
moved)" at frame 527, so the click on the start dialog went to the place of
the mouse, and the dialog stayed open. The run showed only the start: the
lines of the keys, and the hook `GInterface::GetLeashStartPoint ... at
0x005D8EA0 ok`.

The runs to do are the four kinds of `near_run.ps1`. Three of them use the
view of run bk8 at the store of Land 1 (four turns right and six zoom steps
out). The census of run bk9 has 2 `BigForest`, 17 `Abode` and 14
`ScriptHighlight` in view there.

- **forest**, with `seek_forest=1` and `far_test_forest=1`: `seek`, a grip of
  120 frames, then `aimfar`, a grip and `pull`. Look for the choice
  "(BigForest)", the Hand's state 3 with the pick locked, "the mod left the
  pick record to a pick up of the game", and then an object in the Hand. For
  the far part look for "the forest P gave the new object Q" and the glide.
- **scroll**, with `seek_scroll=1` and `far_test_scroll=1`: `seek`, a grip of
  40 frames, then `aimfar` and a grip. Look for the census line of
  `ScriptHighlight` with "a press taps it", the choice "which the grip taps",
  hand mode `0x12` and the look "something to activate". For the far part
  look for "the grip closed on the scroll" and "the far touch is over". If no
  scroll there has a script yet, the census says so, and the run tests
  nothing.
- **tap**, with `seek_tap=1`: `seek`, then a grip of 40 frames. Look for the
  same lines on a house.
- **base**, at the pen: the far fetch and the throw of `wait_and_run.ps1`,
  then `seek` and a grip. Look for the same lines as in run all3.

No desktop run can put the leash on from a new game.

**Not tested:** all of the code that acts in the game. That is:

- the choice of an object that a press taps, and its place in the record
- the body with the game's height, and the body of a tap about the middle of
  its box
- the rule of the near objects in a village
- the far touch of a scroll
- the tree from a forest, at the Hand and at a distance
- the hook of the leash, past its install

**Needs the headset.** Whether 4 cm is the right reach: an object must be
easy to take, and an object at the palm must not be taken. Whether a scroll
starts with the glow on it, and with the aim on it from a distance. Whether
the body of a scroll is where the scroll shows: the line of the tag `hand
pick` gives the gap. Whether 10 and 15 cm keep the far grab out of the way
near an object and do not turn it off too often in a village. Whether the
forest gives a tree after a short wait, and whether its body covers the trees
that show. Whether the rope of the leash now comes out of the hand. Whether
the paler gold of the activate look is easy to tell from the gold of the take
look.

## One target (2026-10-03, evening)

Buck, after the headset test of the evening of 2026-10-03 (build 3918f87):
"the point of interaction still seems vague, you would think that the glowing
orb on the hand is exactly the point where the interaction happens, but it
doesn't always seem to be the case, I again struggled clicking on a scroll
for example, no matter what I did it seemed to be picking up trees below the
scroll. I started spamming the grip button and eventually it did grip on
properly but it doesn't feel polished." And: "Fingertip glow should probably
only light up when our hand is in place to actually interact with something
not glow all the time."

This section replaces the choice, the glow and the order of "One point for
each interaction". The reach (`hand_pick_cm`), the rule of the near objects,
the far touch, the forest and the leash stay as that section has them. The
code is in `src/mod/hand_direct.cpp` (`Choose`, `Measure`, `AssertRecord`,
`h_PalmSearch`, `h_RightPress`) and `hand_glow.cpp` (`DrawNearHalo`). The
pure math is in `near_math.h`.

### Why a press at a scroll took a tree [V]

Three paths of the game acted on an object that was not at the Hand's point.
Build 3918f87 left each of them open.

**1. The game's search at the palm.** The right press `FUN_005D1330` reads the
object of the next pick record (`+0x400`). When the record holds no object, it
calls `fn_005D39E0` (thiscall on GInterface, a `MapCoords*`, `ret 4`) at
`0x005D13A1` and at `0x005D14A6`:

- `GLandscape::Draw` makes the box of the bones of the Hand's mesh
  (`0x005E46A5` to `0x005E47CD`). It casts a ray from the camera through the
  middle of that box (`fn_00802550`, called at `0x005E4848`). It keeps the
  land point that the ray meets in `0x00D1A3A0` (x) and `0x00D1A3A8` (z), and
  a flag in `0x00D2017C`.
- `fn_005D39E0` looks at each map cell within 5 units of that land point. It
  keeps the nearest `Object` that is no `Fragment` and is less than 5 units
  away (`0x005D3C41`).
- It then takes the distance from that land point to the land point of the
  record (`+0x3F0`). When the object is not farther than that, it puts the
  object in the record (`0x005D3CE9`) and gives it to the press.

With the mouse the two land points are the land under the finger and the land
under the palm, a few units apart. So the search finds only an object that
the Hand covers. With the real hand, the land point of the record is the land
straight under the Hand's point (`h_InterfaceStep`), and the other one is the
land that the eye sees through the palm. With the hand 20 cm over the land,
the two are far apart. So each press with no object in the record took the
nearest object to the land behind the palm. That is a tree below the hand
when the hand is at a scroll that floats. It is also the cause of his words of
the afternoon: "the interactions happen more at the palm of the hand".

Build 3918f87 made this fault more frequent. Its reach of 4 cm leaves the
record empty more often than the reach of 11.4 cm did. With `grab_hand=1` (his
setting) each press of the pointing grip with no far target goes to the game.

**2. The game took the choice out of the record.** After the draw,
`GInterface::PostDrawProcess` calls `GInterface::UpdateInterfaceCollide`
`0x005D5A70` (`0x005CEABA`). It keeps the object of the next record only in
two cases: its distance (`+0x404`) is less than the distance of the land
under the pointer (`+0x3FC`), or that land point is inside the circle of the
object's box. Build 3918f87 cut the distance only for a tap and for a far
fetch. His log shows the result for a tree:

```
hand pick    : 2AA3CB9C (Tree) is 2.56 units from the Hand's point ... frame 12652
hand pick    : the game has 2AA3CB9C under the Hand (+0x3C8), frame 12704
```

The mod put the tree in the record 268 times in 67 frames, and the game had
it for the last 15 frames only. The tree stood on lower land, 64.7 units from
the camera, and the land under the pointer was 53.0 units away. For the 52
frames between, the record was empty, the glow said "nothing", and a press
went to the search at the palm.

**3. The game's pick at the pixel.** With no choice, build 3918f87 took only a
holdable object out of the record. A scroll, a house or a pile behind the
pixel of the Hand's point stayed, at each depth, and a press acted on it.

**The glow.** In build 3918f87 the glow was drawn on each frame. His log has
"drawn in 900 eye passes: 900 over nothing" in most windows. With no target it
was pale blue and 1.0 cm.

### What the mod does (`near_rule=2`)

**One target.** At each moment the grip has one target or none: the choice of
the mod at the Hand's point, or the far target of the aim. The target has a
halo. A press acts on the object with the halo, and on no other object.

- **The record is the choice.** `Inject` gives each choice a distance that
  the game keeps (`near_math::RecordDistance`: at most half of the land
  distance). With no choice it takes each object out of the record, not only
  a holdable one. Before each interface step and before each right press,
  `AssertRecord` makes the record agree with the choice again. It acts only
  for the empty Hand in its Normal state, with no pick up, no take and no
  far fetch in work. It uses the choice only while the first word of the
  object is still the table of its class.
- **The search at the palm is off.** A MinHook hook on `fn_005D39E0` runs the
  game's function, so the log can say what it found. Then it takes the object
  out of the record again and gives the press no object. `palm_search=1`
  leaves the game's answer. The mouse, and each rule below 2, keep the game's
  answer.
- **The choice is made before the picture.** `HandGlowDraw` asks for the
  choice at the end of the world of the first eye pass
  (`HandDirectChooseNow`), when the draw has offered its objects. So the halo
  and the glow of a frame show the choice of that frame, and the press that
  the game reads next acts on that object. Build 3918f87 made the choice
  after the pass, so the picture showed the choice of the frame before.
- **The Creature is a choice too.** Its body is a capsule through its body
  bone. The special case "a tap is no choice while the Creature is as near"
  is gone. Until 2026-10-04 the mod did not know the body bone before a
  press, so the capsule did not run: see "The body of the Creature before a
  press".

**The rule of the choice** (`near_math::BeatsTarget`), for the bodies within
`hand_pick_cm` (4 cm) of the Hand's point:

1. A body that the point touches wins, when its shape is close to what the
   player sees: the Creature, a villager, a rock, a scroll. The point touches
   a body when the gap is `hand_touch_cm` (1 cm) or less.
2. Then a scroll in the reach wins. A scroll is small, it floats, and the
   player goes to it on purpose. Of two scrolls the nearer one wins.
3. Then a loose body that the point touches wins. A loose body is large, and
   it is wider than what the player sees: a house, a tree, a forest, an
   object to take from.
4. Then the nearest body in the reach wins.
5. Last, a scroll that is past the reach of the pick and inside the reach
   of a scroll (`scroll_reach_cm`, 10 cm). See "The scroll from its draw, and
   the click on a scroll".

In rule 1 and in rule 3 the smaller body wins: a villager in a forest, a rock
at a house. The size is the larger of the radius and half the height, in
steps of a factor of 2. In one step the nearer middle wins, so two trees with
crowns that meet share the room between them.

Until 2026-10-04 the scroll was the first rule. In run hk4 the Hand's point
was in the body of the Creature, a scroll was 3.4 cm away, and the grip
tapped the scroll. So a body that the point touches comes first now
(`near_math::TargetRule`). A loose body does not come first, because a
scroll at a tree must still win (item 8 of Buck's test).

Each rule gives a full order, so the choice does not depend on the order of
the candidates (the math test tries each of the 720 orders of 6 bodies).

**The bodies** (`Measure`):

| Object | Body |
| --- | --- |
| A scroll or a sign (`IsScriptHighlight`, slot `0x48C`) | A ball: the middle of the box of its mesh in the world, and the half diagonal of the box, through the matrix of the mesh as it is. `ScriptHighlight::Draw` gives this point and this radius plus 0.5 to `SendInvisibleDrawCollision` (`0x0070A2D7`), so it is where the game's own pick has a scroll. The game draws a scroll smaller when the camera is less than 30 units from it (`0x00709E60` to `0x00709EA6`), and the matrix holds that scale. From 2026-10-04 the ball is the one that the draw of this frame made, and each scroll that the game draws is a candidate |
| A tree (`IsAnyKindOfTree`, slot `0x478`, and no forest) | A trunk and a crown. The crown is the upright spheroid in the box: the game's 2D radius, and half the game's height. The trunk is a cylinder of a quarter of that radius, from the base to the middle. That is two thirds of the cylinder of the build before |
| The Creature | A capsule through the body bone, from the land up to `1.1 S`, with the 2D radius. The touch in the Creature state adds `creature_touch_cm`, and the choice does not |
| A house and each other object that a press taps | The two cylinders of the build before |
| Each other object | The cylinder of the build before |

`Object::Get2DRadius` `0x00638180` gives the larger of the two half sizes of
the box across the land (`+0x24` and `+0x2C`) times the scale [V]. So the
cylinder of a tree was as wide as its crown from the land to the top.

**The halo.** The target at the Hand has the halo of a far target
(`DrawNearHalo`): gold, or red when the land under the Hand is outside the
influence and the object needs the influence. It is as large as the body, 35
percent and 1 cm more. For a tall body it is an ellipse along the image of
the upright axis (`near_math::HaloEllipse`). Its depth test is one radius
behind the middle of the body, so the object hides the middle of the disc.

**The glow** shows only while a press of the grip has a target at the Hand's
point (`hand_glow_idle=0`). Its look comes from the choice, and not from the
game's hand mode, which is one frame late:

| What a press does | Look |
| --- | --- |
| The grip picks up, or takes from a pile | gold, bright, 1.7 cm |
| The grip activates: a scroll, a house, the Creature, a bubble, a leash | a paler gold, 1.5 cm |
| No target, a target that the influence stops, or an object in the Hand | no glow |

### What still acts from the pixel of the Hand's point

- **A bubble and a leash.** The game finds them at the pixel
  (`GInterface+0x408` and `+0x410`), and the right press acts on them before
  it reads the object. The glow shows the activate look for them.
- **While the Hand holds an object**, the mod does not change the record. A
  drop on a store or a villager goes to the object behind the pixel.
- **The far target** of the aim ray. It has its own halo.
- **The trigger** grabs the land under the pixel (`fn_005D1AB0`).

No path acts from the palm now. The held object, and the contact of the flat
hand on the Creature, are at the palm on purpose.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `near_rule` | 2 | 2 is one target. 1 is the pick of build 3918f87: the nearest body, the game's pick at the pixel for an object that is not near, and the search at the palm. 0 is the pick of the first builds |
| `hand_touch_cm` | 1 | The point touches a body when the gap is this or less, in cm of the room. 0 to `hand_pick_cm` |
| `palm_search` | 0 | 1 leaves the game's search at the palm on |
| `hand_glow_idle` | 0 | 1 draws the glow also with no target and while the Hand holds an object, as the builds before |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The A/B keys**, with `test_keys=1`:

- The point key of the number pad changes `near_rule` between 2 and 1. The
  pointing controller gives a long pulse for 2 and a short pulse for 1.
- The 7 key of the number pad changes `hand_glow_idle`. A long pulse is the
  new look (no glow with no target), and a short pulse is the glow of the
  builds before.

Test keys for the desktop runs only:

- `seek_plan`: a list of words with commas, one for each `seek` of the
  script. `scroll` puts the Hand's point in the middle of the ball of a
  scroll that a press taps: of the scrolls within 2 m of the room, the one
  with a tree nearest to it. `air` puts the palm on the line from the camera
  to the base of the nearest tree, 25 cm of the room from that base, where a
  press has no target. `crown` puts the Hand's point on the axis of the
  nearest tree at 80 percent of its height. Each place writes a census of the
  four nearest objects to the log. `palm` moves the hand across the view, so
  that the land behind the palm goes onto the base of the tree nearest to it.
  Three seeks one after the other bring it within 5 units.
- `near_test_flips`: a list of frames with commas. `near_rule` starts at 1
  and changes between 1 and 2 at each frame of the list, so one run shows
  both rules at the same places.

### The log

- Tag `hand press`: one line for each press of the grip that the game reads.
  It gives the target with its class, what the grip does to it and its
  distance, or "no target" with the distance of the nearest object that a
  press acts on (600 lines). One line when the game's search at the palm
  found an object: the object, its distance from the Hand's point, and
  whether the mod gave it to the press (60 lines). The report window counts
  the presses, the presses with no target, the answers of the search, and the
  times that `AssertRecord` changed the record.
- Tag `hand pick`: "the target is P (class): what the grip does", with the
  gap, whether the point touches the body, and the body. "no target". One
  line for each change, 400 lines. "before the interface step the pick record
  held ..." when the game or the second eye pass changed the record (40
  lines).
- Tag `hand glow`: "no glow now" with the reason, and "a halo of the target
  at the Hand is drawn" with the place and the two half axes in pixels (30
  lines). The report window counts the eye passes with no glow and with the
  halo.

### Tests

**The math test** has 42 new checks (`OneTargetChecks` in `near_checks.h`),
1600 in all:

- The ball of a scroll. The gap to an ellipse against a search along the
  ellipse, to 0.02 units. The tree: a point beside the trunk at the land is
  out of the body and was in the cylinder, the trunk and the crown are in,
  and the body is two thirds of the cylinder.
- The rule: a scroll 3 cm away wins over a tree that the point is in. A
  villager that the point touches wins over the tree about him. A tree that
  the point touches wins over a villager 3 cm away. With no touch the nearer
  body wins. Two trees of one size step: the nearer middle. A rock at the
  wall of a house wins over the house. Each of the 720 orders of 6 bodies
  gives the same target.
- The record distance: the tree of his log was farther than the land under
  the pointer, and with the cut the game keeps it.
- The halo: round from above, an ellipse along the axis from the side, and
  the axis leans with the head.

**Desktop runs** (2026-10-03, `hand_run.ps1` in the scratchpad). The rig of
`backend.md` at a render of 2560 by 1440, `play_mode=4`, `fake_controller=2`,
`far_grab=0`, `grab_hand=1`, `far_test_influence=1`. The place is the store
of Land 1: four turns right and six zoom steps out, which is 1139 units a
meter. At 100 units a meter a new game has no tree and no scroll within 2 m
of the room of the start place.

- **hs3, build 37c7090**, the hand at `-21,-52,74` cm, at the crown of a
  tree. The choice is `2929B7FC (Tree)`, 0.2 cm of the room from the point,
  from frame 4901. The game has it under the Hand from frame 4902 and takes
  it out at frame 4906: hand mode 9, then 3, and the glow says "nothing under
  the Hand". The hand does not move. The grip is down from frame 5001 to
  5041, and the Hand picks up nothing.
- **hs4, this build**, the same script. "the target is 2928E86C (Tree): the
  grip picks it up", with a gold halo of 24 px in each eye and the glow
  "something to take". "before the interface step the pick record held
  nothing ... The record now holds the target" at frame 4901. Hand mode 9
  from frame 4902, and it stays. The press at frame 5002 is "on the target",
  the Hand goes to state 3 at 5004 and holds the tree at 5015. "no glow now:
  the Hand holds an object".
- **hs2, this build** with `seek_plan=air,air,scroll,scroll,crown,crown` and
  `near_test_flips`, so each place has rule 1 and then rule 2.
  - The crown, rule 1: the choice is a tree 0.2 cm away. At the press of
    frame 5502 "the pick record holds nothing", the hand mode is 3, and "the
    game's search at the palm found no object within 5 units of the land
    behind the palm". So the game ran the search for a press with an empty
    record. Nothing is picked up.
  - The crown, rule 2: the same as hs4. The tree is in the Hand 13 frames
    after the press.
  - The scroll, rule 1 and rule 2: the point is in the ball of the scroll
    `29284574` (radius 3.08 units, with a house and two trees in the reach).
    The scroll is the target, the hint card says "[Grip] Activate", the press
    taps it, and the card says "[Grip] Reminder". With rule 2 the halo is 18
    px and the glow is "something to activate". After the hand left: "no
    target", "no glow now", and the record lost the scroll before the next
    step.
- **hs5**, the first commit of this section on the main of that time
  (9abf8ff), with the script of hs4: the same result as hs4. The tree is in
  the Hand at frame 5016.
- **hs6, this build** with `seek_plan=palm,palm,palm,palm,palm,palm` and
  `near_test_flips=5155`. Three seeks bring the land behind the palm to 4.7
  units from a tree. The Hand's point is 11.1 cm of the room over the land
  and 32 cm of the room from that tree, and no object that a press acts on
  is near it.
  - Rule 1, which leaves the game's search as it is: at the press of frame
    5072 "the game's search at the palm found 292A5114 (Tree) near the land
    behind the palm (1608.1 2293.9). That object is 31.9 cm of the room from
    the Hand's point. The press acts on it". The Hand goes to state 3 at
    frame 5074 and holds the tree at frame 5085. This is the fault of his
    sessions: a press with no object at the Hand's point takes a tree below
    the hand.
  - Rule 2: at the press of frame 5312 the search finds a tree again, "The
    mod gives the press no object", and the Hand stays empty in its Normal
    state.
- **hs1** ran its script in the first story. The story ends between frame
  3965 and frame 4100 at this render size (2975 at 2880 by 2880), so a script
  must start at frame 4600.

**Not shown on the desktop:**

- Build 37c7090 itself at the place of hs6. The places of the fake hand are
  whole cm, which is 11 units at this scale, and the search needs 5 units.
  Rule 1 of this build leaves the game's search as it is, so hs6 shows the
  same code of the game. The two `air` presses of hs2 started off view (the
  render is 16:9 now, and the place was 40 degrees below the middle), so the
  game did not get them.
- A scroll that wins over a tree that the point is in, the body of a tree in
  the game, the red halo, and the Creature as a choice. The math test has the
  rule and the bodies.
- A place at 66 to 100 units a meter, the scale of his sessions.

**Needs the headset.**

1. Go to a scroll with trees near it. The halo must be on the scroll when
   the finger tip is at it, and the grip must start it at the first press.
   If a press does the wrong thing, the line `hand press` of the log says
   what the target was.
2. Reach for a tree, a rock and a villager among trees. The halo must be on
   the object that you look at. Say when it is on another one.
3. Press the grip with no halo, with the hand over trees. Nothing must come
   into the Hand. The line `hand press` then says what the game's search at
   the palm found.
4. Whether the halo is easy to see and not too strong at the Hand, and
   whether the Hand with no glow is still easy to aim. The 7 key of the
   number pad gives the old glow for a comparison, and the point key the old
   rule.
5. Whether 1 cm is the right touch distance, and whether a scroll that wins
   at 4 cm is in the way of a villager near it.

## The open hand on the Creature (2026-10-03, evening)

Buck, after the headset test of the evening of 2026-10-03 (build 3918f87):
"When interacting with the creature my hand seems to stay curled into a
fist, so stroking it is weird, I think the vanilla game the hand actually
moved and interacted with the creature as you stroked it doing a little
tickling animation. We could make it so we only interact using a finger if
we're zoomed far away, but use the entire hand when zoomed in close ... Also,
it's too hard to slap a creature, vanilla was too sensitive and it was too
easy to accidentally slap it, but in VR I can't backhand it at all it seems
and in general I have to swing really hard to slap, we need to get the middle
ground."

This section adds to "The Creature by touch". The code is in
`src/mod/hand_direct.cpp` (`TouchFrame`, `h_CreatureUpdate`, `StrokeHand`) and
`overlay.cpp` (`ChoosePose`, `BuildFrame`). The pure math is in `near_math.h`.

### Why the hand was a fist

The grip is the game's Action button, and the Creature state lasts while it
is down. `ChoosePose` gave the fist for the Creature state with the grip
closed and the flat hand with the grip open. So the hand was a fist for the
whole of each stroke.

### Why a backhand did not slap

The game's slap test has no direction [V]: the branch at `0x005B1C0A` needs
the height, the wait and the speed, and the sign of the motion only chooses
the side of the slap (`0x005B1E15`). The mod's part had no direction either.
The fault was the place of the speed: the mod gave the game the speed of the
**grip** over 50 ms (`HandVelocity`), and the contact is at the index tip, 17
cm from the wrist. A slap with the arm moves the grip. A slap with the wrist,
as a backhand is, turns the hand about the wrist: the tip moves fast and the
grip, 7 cm from the wrist, moves at less than half of that speed. A backhand
of 420 degrees a second gives 1.25 m/s at the tip and 0.5 m/s at the grip, so
it was no slap at `creature_slap_mps=1.5`. [I: no log of his session has a
slap. The new lines of the log show the two speeds of each swing.]

### What the mod does

**The hand of a stroke is open.** In the Creature state the pointing hand
takes one of two poses, with the grip closed too (`near_math::StrokeFinger`):

| The body of the Creature against the hand | Pose | Contact point |
| --- | --- | --- |
| less than 0.8 of the hand's length | the index finger (the point pose) | the tip of the index finger, the Hand's point |
| more than 1.2 of the hand's length | the flat hand | the middle of the palm (`HandExactPalm`) |
| between the two | the pose of the frame before | |

From 2026-10-04 the limits are 0.6 and 0.9, and a stroke starts with the
flat hand from 0.75: see "The limits of the hand of a stroke".

The body is the capsule of the touch, `1.1 S` high, and the hand's length is
the real hand's (19 cm) at the scale now. So the zoom decides: at 100 units a
meter a young Creature is 3.6 cm high and gets the finger, and at the life
size scale it is 48 cm high and gets the flat hand. `creature_stroke_hand=1`
gives the flat hand always, and 2 the finger.

The capsule test and the swept test of a slap use the contact point of the
pose. A change of the pose moves the contact point, and that frame is no slap.

**The tickle.** While the contact point is in the body, the fingers move a
little: each 5 cm of path of the contact point is one wave, each finger is a
part of a wave behind the finger before it, and the wave closes a finger by
0.12 of its curl at most (`TicklePhase`, `TickleCurl`). The flat hand moves
its four fingers, and the finger pose moves the index finger only. The wave
goes on only with the path of the real hand. A hand that is still has still
fingers.

**The speed of a slap is the speed of the part that hits**
(`near_math::SlapSpeed`): the larger of the speed of the grip and the speed
of the Hand's point, each over 50 ms. A slap with the arm and a slap with the
wrist count the same.

**The middle ground.** `creature_slap_mps` is 1.0 by default (1.5 before). A
hard slap needs 1.8 times that. A stroke of a real hand is 0.1 to 0.5 m/s, so
it stays a stroke. The plain game slaps at 5 S world units a second of the
mouse pointer at the Creature's depth, which a small move of the mouse gives.

### The grip, and a stroke with no press

Recommendation: keep the grip for this round, and try a touch with no press
as its own step.

- The grip is the only start of the Creature state, and a short press is the
  leash click. A touch with no press changes that scheme.
- The mod must then hold the game's right button for the player
  (`controllers.cpp`, `PumpButtons`).
- The hand passes the Creature each time the player reaches for an object
  near it. Each pass would be a stroke, and the Creature learns from each
  stroke and slap. So the touch needs a guard: an open hand (no finger on the
  trigger and the grip, which the Touch controllers report) and 0.2 s in the
  body.
- A slap comes from outside the body in one or two frames, so it cannot wait
  0.2 s. A slap with no press would be as easy to do by accident as in the
  plain game. So the slap keeps the grip.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `creature_slap_mps` | 1.0 | The speed of a slap, in m/s of the grip or of the Hand's point, the larger of the two. A hard slap needs 1.8 times this. 0.2 to 10 |
| `creature_stroke_hand` | 0 | 0 chooses the hand of a stroke by the size of the Creature. 1 is the flat hand always, 2 the index finger always. 3 is the stroke of build 3918f87 (see "The way back") |

With `test_keys=1`:

- The 9 key of the number pad takes the next speed of a slap: 0.7, 1.0, 1.3
  and 1.6 m/s. The pointing controller gives a pulse that is longer for each
  next value.
- The 3 key of the number pad takes the next value of `creature_stroke_hand`:
  by the size, the flat hand, the index finger, and the stroke of build
  3918f87. The pulse is longer for each next value, so the longest pulse is
  the old stroke.

### The way back

`creature_stroke_hand=3` gives the stroke of build 3918f87, and the 3 key
reaches it while the game runs:

- The hand is a fist while the grip is closed, and the flat hand while it is
  open.
- The contact is at the Hand's point, and the fingers have no tickle.
- The speed of a slap is the speed of the grip only.
- A slap needs 1.5 m/s when `bwvr.ini` has no `creature_slap_mps`. With the
  key in the ini, its value counts. The 9 key changes the speed in each mode.

The lines of the log stay, so a session with the old stroke shows the two
speeds of each swing too.

### The Creature is no far target

The log of 2026-10-03 has "far grab: target 2AA07674 (Creature, a bulk
source)" with the Creature 0.47 m from the Hand. `FarSelect` took each object
that passes the game's test of a bulk take as a pile.
`Creature::ValidForLockedSelectProcess` `0x00476E10` gives 1 for a Creature
that has a player, no spell on it and no state `0xD` [V], because a press on
the Creature starts its touch with the locked select. So the halo and the
hint said "collect" for the Creature, a grip from a distance started the
Creature state with the hand far from the body, and a short grip changed the
leash. `FarSelect` now leaves the Creature out. The Creature is touched with
the hand, and the trigger commands it.

Since 2026-10-04 your Creature is a far target again, of its own kind: the
grip changes the leash and sends no press. See "The leash from a distance".

### The log

The tag `hand creature`:

```
hand creature: the hand of the stroke is the index finger, and its tip touches. The body is 3.6 cm of the room high, 0.19 of the hand's length ...
hand creature: a slap with the index finger, to the left as the eye sees it. The hand moved at 1.31 m/s (the grip 0.52, the Hand's point 1.31), which the game read as 21.6 units/s. A slap needs 1.00 m/s ...
hand creature: the hand went into the body at 0.74 m/s (the grip 0.31, the Hand's point 0.74), and that was no slap. A slap needs 1.00 m/s. The hand was too slow. The motion in the room: -0.93 right, 0.10 up, 0.35 forward ...
```

(Examples of the format.) Each slap goes to the log (200 lines), each stroke
step (80 lines), and each hand that went into the body and did not slap, with
the reason: too slow, the game's wait of 1 s after a stroke, or the game's
own test (60 lines).

### Tests

**The math test** has 21 new checks (`StrokeChecks` in `near_checks.h`), 1621
in all: the hand of a stroke at 3.6 cm and at 48 cm, the band (a zoom between
0.85 and 1.15 for 40 frames changes nothing), a hand that is still for 100
frames has the same wave, the wave only closes a finger and moves it less
than 0.03 of its curl a frame at 0.3 m/s, a backhand of 420 degrees a second
is a slap by the tip and was none by the grip, and at 1 m/s the game reads
5 S.

**Desktop runs.** The runs of 2026-10-03 (hc1 and hc2) said "no Creature
with a body bone". The cause was the mod's own test of the body bone, and not
the game: see "The body of the Creature before a press". The runs of
2026-10-04 reached the Creature state. The pose, the contact, the speed of
the Hand's point, the backhand and the way back ran in the game: see "A
Creature in a desktop run". The tickle of the fingers was not looked at in a
picture.

**Needs the headset.**

1. Hold the grip on the Creature. The hand must be open: the index finger
   with the land at waist height, the flat hand after a zoom in to the life
   size scale. Say if the change of the hand at a zoom step looks wrong.
2. Move the hand over the body. The fingers must move a little, and stop
   when the hand stops.
3. Slap with the arm, then with a turn of the wrist, forehand and backhand.
   Each must count with a clear swing and not with a stroke. Press the 9 key
   of the number pad to try 0.7, 1.0, 1.3 and 1.6 m/s. The log then has the
   speeds of each swing, also of the ones that did not count.

## The leash from a distance (2026-10-04)

Buck, after the build that took the Creature out of the far targets:
"Leashing the creature from a distance was really useful, bring it back, but
keep the fix to stop interaction with creature over distance."

The code is in `FarSelect` and `HandDirectLeashClick` (`hand_direct.cpp`),
`FarGrabPumpGrip` (`far_grab.cpp`) and `hint_card.cpp`. The pure rule is
`near_math::LeashClick`.

### What the click of the game does [V]

A short click on the Creature sends packet `0x5F` (`GGame::SetPacket`
`0x00550DD0`). Its handler is `fn_005D06E0` on the `GInterface`:

1. It takes the Creature of the player from `GPlayer+0xA4C`.
2. It stops when the Creature does not live (vtable slot `0x2C`) or when
   `Creature+0x1110` is 0. It also stops when the leash of a different
   player is on the Creature (`Creature::GetInterfaceStatusLeashOn`
   `0x004CF060`).
3. `fn_005E7140` on the `GLeashStatus` then does one of three things. A leash
   that is tied to an object (`+0x24`) comes back to the Hand. Else the leash
   goes on when it is off (`+0x14`), and comes off when it is on.

Packet `0x5B` takes the leash off, and packet `0x5C` ties it to an object.
The mod sends only `0x5F`, so the game decides as it does for the mouse.

### What the mod does

- **Your Creature in the aim is a far target of its own kind.** `FarSelect`
  takes it when `LeashKind` says that the click does something. The kinds
  are 1 (the leash goes on), 2 (it comes off) and 3 (it comes back from an
  object). The Creature of a different player is no target. A Creature that
  the game would not leash now is no target. The halo is the halo of each
  far target.
- **The hint says what the grip does.** The card of the hand shows "Grip Put
  the leash on", "Grip Take the leash off" or "Grip Take the leash back".
- **The grip sends the leash click and no press.** On the edge of the grip
  `FarGrabPumpGrip` calls `HandDirectLeashClick` (packet `0x5F`) and takes
  the grip away from the game until it opens. No press goes to the game. So
  the Creature state does not start, for a short grip and for a long one.
- **Near the Creature nothing changed.** "Near objects win" turns the far
  target off while the Creature is 10 cm or less from the Hand's point. The
  grip there is a press: a short press is the leash click, and a hold starts
  the Creature state.
- **The trigger commands stay.** With the leash on, a short press of the
  trigger with the aim on your Creature is the leash click too. That worked
  for the first time in the build of 2026-10-04: see "The body of the
  Creature before a press".

### Keys

| Key | Default | Meaning |
| --- | --- | --- |
| `far_leash` | 1 | Your Creature in the aim is a target of the leash. 0 takes the Creature out of the far targets, as in the build of 2026-10-03 |
| `far_test_creature` | 0 | A test key. 1: only your Creature is a far target, and the word `aimfar` aims at it |

### The log

```
far grab    : far_leash=1. Your Creature in the aim is a target of the leash: the grip sends the leash click, and no press
far grab    : the grip closed on your Creature 29258674, 0.75 m of the room from the Hand. The leash click (packet 0x5F): the leash goes on. No press goes to the game, so the Creature state does not start, frame 931
creature cmd : the leash is on, frame 936
```

The report of the far grab counts the leash clicks from a distance.

### Tests

**The math test**: 6 checks of `LeashClick` (`LeashChecks` in
`haptics_checks.h`). Three cases give a click: no leash, the leash on the
Hand, and the leash tied to an object. Three cases give none: the Creature of
a different player, a Creature that can have no leash, and a Creature on the
leash of a different player.

**Desktop runs**, each with `far_test_creature=1`:

| Run | Build, place | What it showed |
| --- | --- | --- |
| hk4 | before the landing, the shore, 337 units a meter | The Creature 0.85 m from the Hand. A grip of 6 frames at 1901: the leash is on at 1908. A grip at 2001: off at 2005. A grip of 300 frames at 2101: on at 2107, and no Creature state |
| hkK | this code on main `99f6124`, the Temple, 100 units a meter | The Creature 0.75 m from the Hand. The card says "Grip Put the leash on". A grip at 931: on at 936. The card then says "Grip Take the leash off" and "Trigger Take the leash off". A trigger press of 107 ms with the aim on the Creature: off at 1010. A press of 88 ms with the Hand on the Creature: on at 1539 |
| hkL | the same | A grip of 300 frames from 0.35 m: on at 814, and no Creature state. A grip of 6 frames at 1171: off at 1175 |
| hkO | the same, 44 units a meter | After a slap in the Creature state: a grip of 300 frames from 0.53 m. On at 1658, and no Creature state |

A leash that is tied to an object (kind 3, "Take the leash back"): see "The
leash on an object", run tie1.

**Needs the headset.** Point at your Creature from a distance and close the
grip. The leash must go on, and the Creature must not turn to the hand as it
does for a stroke. Say if the halo of the Creature shows when you do not
want it.

## Haptics (2026-10-04)

The pulses of the controllers came from each file with its own numbers, and
some were long. Now one table gives each kind of pulse (`haptics_math.h`),
and one function sends it (`HapticsPulse` in `haptics.cpp`). The levels
follow Half-Life: Alyx: short ticks of about 10 ms, and 0.40 for an object
that arrives.

The rule: **no pulse for a thing that the player did not do.**

### The kinds

| Kind | Amplitude | ms | When |
| --- | --- | --- | --- |
| target | 0.08 | 8 | A new target at the Hand's point or in the aim, at most one each 80 ms |
| lock | 0.20 | 10 | The grip locks a far target |
| arrive | 0.40 | 11 | Each frame of the last 0.125 s of the glide of a fetched object |
| take | 0.30 | 15 | The grip takes an object at the Hand's point, or a bulk source at a distance |
| let go | 0.20 | 10 | The Hand lets an object go |
| activate | 0.25 | 12 | The grip acts on a target that it does not take: a scroll, a house, the Creature |
| no target | 0.06 | 8 | A press of the grip with no target |
| touch | 0.12 | 10 | In the Creature state, the contact goes into the body |
| stroke | 0.07 | 6 | In the body, one tick for each 1.5 cm of the path of the contact |
| slap | 0.41 to 0.75 | 25 | A slap, by its speed: 0.41 at the speed of a slap, 0.75 at 1.8 times that speed |
| leash on | 0.30 | 20 | The leash goes on, within 1.5 s of a click of the player |
| leash off | 0.20 | 12 | The leash comes off, within 1.5 s of a click or a shake of the player |
| command | 0.25 | 15 | The trigger sends a command to the Creature |
| teleport | 0.30 | 20 | The teleport goes |
| refused | 0.35 | 30 | The teleport has no place to go to |
| tick | 0.15 | 10 | A small step: the draw of a gesture starts, a shake counts |
| gesture | 0.40 | 30 | The game knows a gesture (effect 3) |
| power | 0.30 | 40 | A miracle powers up (effects 8 and 9) |
| menu | 0.15 | 10 | A click in a menu (effect 44) |
| world | 0.15 | 10 | The two grips take or let go the world |
| other | 0.20 | 15 | Each other effect of the game |

No pulse is stronger than 0.75 or longer than 40 ms.

### What changed

- **The effects of the game** (`GInterface::StartImmersion` `0x005D8A10`,
  `HapticsGameEffect`). Effect 0 (the Hand touches the Creature) gives no
  pulse now. In run hk5 the game started it 6 times in 60 frames with a hand
  that was still. The mod gives one `touch` tick when the contact goes into
  the body, and `stroke` ticks for the path. A hand that is still gives none.
- Effects 1 and 2 (a slap) give the `slap` pulse with the speed of the real
  hand. Effects 4, 5, 6 and 46 (a pick up starts, the object is in the Hand,
  the Hand is over an object, a release) give no pulse. The mod gives `take`,
  `let go` and `target` at the action of the player.
- **The leash.** The pulse comes only within 1.5 s of a click or a shake of
  the player (`CreatureCommandLeashAsked`). A leash that the game changes by
  itself gives none. Effect 7 of the game is "the leash goes on": it starts
  one frame before the leash is on (run hkQ, frames 815 and 982). It gave a
  second pulse of the kind `other` with each `leash on`, so it gives no pulse
  now.
- **A fetched object.** The pulses of the glide are the `arrive` pulses now
  (`haptics_math::ArriveNow`).
- The pulses of the test keys stay as they were.

### Keys and the log

`haptics_pct` (100): the strength of each pulse in percent, 0 to 200. 0 turns
the pulses off. A value above 100 does not pass 0.75.

The first 6 pulses of each kind go to the log, and the report counts each
kind:

```
haptics      : take, 0.30 for 15 ms (strength 1.00, the pointing hand), frame 872
haptics      : 11 pulses this window (haptics_pct=100): 9 arrive, 1 let go, 1 no target
```

### Tests

**The math test**: 30 checks (`HapticsChecks` in `haptics_checks.h`). No
pulse is stronger than 0.75 or longer than 40 ms. `haptics_pct` scales the
amplitude and not the time. A slap that just counts has 0.41. A hand that is
still for 300 frames gives no stroke tick, a stroke of 30 cm gives 20, and a
jump of the tracking gives one. A glide of 0.4 s pulses on 11 or 12 frames
at 90 frames a second.

**Desktop runs** with this code on main `99f6124`:

| Run | What it showed |
| --- | --- |
| hkJ | A fetch of a rock from 1.4 m: `target` at frame 803, `lock` at 841, 11 `arrive` pulses from frame 899 (the log has the first 6, 899 to 904), and `let go` at 1103 when the grip opened. A press at no object: `no target` |
| hkN | A tree at the Hand's point: `target` at 833, `take` at 872 with the press, `let go` at 963 |
| hkO | The Creature: `activate` at the press (838), `touch` when the state starts (911), one `stroke` tick for each step of 2 cm, `slap` 0.75 for a slap at 6.90 m/s |
| hkK | `leash on` at 936, `command` at 1007, `leash off` at 1010 |
| hkB, hkP | A backhand at 1.29 m/s: `slap` 0.53 (strength 0.71). A backhand at 1.51 m/s: 0.63 (strength 0.84). A swing at 5.58 m/s: 0.75 |
| hkR | After the change for effect 7: the report of the pulses has no `other` when the leash goes on |

Not seen in a run: `teleport`, `refused`, `gesture`, `power` and `world`.
Their call sites did not change, only their numbers.

**Needs the headset**: how each pulse feels. The desktop shows only that the
pulse goes out at the right frame. Set `haptics_pct=50` or `200` and say
which kinds are too faint or too strong.

## The body of the Creature before a press (2026-10-04)

The desktop runs of 2026-10-03 said "no Creature with a body bone", with the
Creature in the list of the draw. The agent `spawn` saw the same with the
Hand 74 cm from the Creature (run sp11).

### The cause [V]

The mod read the body bone only when `LH3DCreature+0x526C` was not 0,
because `HandStateCreature::Update` has that test (`0x005B17EA`). The flag
does not say that the Creature has a body. It says that the Creature is
ready for the Hand:

- A press on the Creature sets `Creature+0x3CC` to 1.
- `Creature::ProcessState` then calls `fn_00477D00` (`0x00472FAB`). It waits
  while `LH3DCreature::IsPerformingBodyAction` is true. Then it sets
  `+0x3CC` to 0 and the flag to 1 (`fn_004806C0`).
- `Creature::GetReadyForNetworkUnfriendlyEndLockedSelect` clears the flag at
  the end of the press (`0x00476EF1`).

The image has four instructions with the offset `0x526C`: the start value,
these two, and the test in the Update. So the flag is 0 at each time before
a press. The body bone itself (`fn_004813D0`) has no test. It reads the bone
buffer (`LH3DCreature::GetSafeBuffer`), and the game calls it for a Creature
that the Hand does not have (`0x005D3E33`, `0x005CFF29`).

What did not run before a press, in each build to 2026-10-03:

- The capsule of the choice. The Creature was measured as each other object:
  a cylinder at its place on the map.
- The Creature as a target of a command. A press of the trigger with the aim
  on the Creature took the land under it. In run hkM (`creature_bone=0`) the
  tap sent the Creature to 1894.8 29.5 2519.8, its own place.
- The test words `touchc` and `seek_plan=creature`.

**The wait after the press.** The same function explains the time from the
press to the Creature state: the Creature first ends its body action. The
runs showed 30, 69, 188, 212 and 223 frames (hkB, hkO, hkE, hkD, hkF).

### What the mod does

- `BodyPlace` gives the place of the body at each time. It reads the body
  bone with no test of the flag. The bone counts when it is not more than
  `S + 2` units from the place of the Creature on the map, in x and z
  (`near_math::BoneSane`, with `S = 15 * size`). Else the place on the map
  counts, at half of `S` above the land.
- The capsule of the choice (`CreatureBody`) goes through that place, from
  the land up to `1.1 S`, with the 2D radius. It does not add
  `creature_touch_cm`. A first build added it. Then the capsule of a
  Creature of size 0.22 had a radius of 6.02 units (run hkE). That is 6 cm at
  100 units a meter. By the rule of the choice such a Creature takes the
  press from a tree that the Hand's point is in.
- The touch in the Creature state (`TouchFrame`) did not change. It keeps
  the test of the flag, as the game's Update has it, and `creature_touch_cm`.
- The Creature as a target of a command (`HandDirectCreatureBody`) and the
  test words use `BodyPlace` too.

### Keys and the log

`creature_bone` (1): 0 gives the behavior of the builds before. Then the mod
knows the body only while the Creature is ready for the Hand. There is no
test key for it, because each key of the number pad has a use.

```
hand creature: creature_bone=1. The body of a Creature is known before a press: the body bone, or the place on the map when the bone is not sane. The capsule of the choice and the Creature as a target of a command use it
hand creature: the body of the Creature 29253674 is at its body bone. Its body bone is at 1896.49 31.30 2520.04, 0.02 units from its place on the map (1896.50 2520.06) and 1.82 above the land there. A sane bone is not more than 5.30 units from that place. Size 0.22, ready for the Hand 0, frame 802
hand creature: the mod asked 100 times where the body of a Creature is: 100 times it was the body bone, 0 times the place on the map, and 0 times there was no place, this window
```

The line of the body comes once for each change of the answer.

### Tests

**The math test**: 5 checks of `BoneSane` in `LeashChecks`.

**Desktop runs.** Each pair is the same script with `creature_bone` 1 and 0.

| Runs | What they showed |
| --- | --- |
| hkE, hkF | With 1, the bone is read at frame 802 with "ready for the Hand 0". It is 0.02 units from the place on the map and 1.82 above the land. In 489 questions the answer was the bone each time. With 0: "its body bone is not known", and the body is a cylinder of radius 1.02 and half height 1.65 at the place on the map |
| hkG, hkH | With 1, the capsule has a radius of 1.02 and a half height of 1.81, and its middle follows the bone: 1896.49, then 1901.99, then 1894.68 in x while the Creature walks at the door. The press at 1222 is on the Creature in each run |
| hkK, hkM | With 1, with the leash on: "the aim is on the Creature, 1.9 degrees off the ray. A short press of the trigger is the leash click", and the press takes the leash off. With 0: "the trigger closes with the aim on no object", and the tap sends the Creature to the land under it |

Not shown: a bone that is not sane. No run had one, so the place on the map
as the answer has only the math test.

## A Creature in a desktop run (2026-10-04)

### The recipe

`creature_recipe.ps1` in the scratchpad of the session runs it, through
`spawn_run.ps1` and `desk_run.ps1`. The agent `spawn` found the start. The
choice "Keep your old Creature" in the start dialog gives a game with no
story. The Creature of the profile stands at the door of the Temple. From the
build `8757f73` the God view goes to that door when the dialog closes.

```
creature_recipe.ps1 -Name <run> -Dll <dll>
creature_recipe.ps1 -Name <run> -Dll <dll> -Kind leash

spawn_run.ps1 -Name <run> -Dll <dll> -Choice keep -Points "0,-30,60"
  -Set "seek_plan=creature;far_test_creature=1;far_test_influence=1;grab_hand=1;far_grab_max_m=0;fake_controller_rots=-112,1,0 8,1,0"
  -Steps "stick_up@800+10 stick_up@830+10 look@860+2000 point@900+1
          seek@930+1 grip@936+800 nudge@1260+1 nudge@1310+1 nudge@1360+1
          nudge@1410+1 rot@1460+1 rot@1530+1 slap@1580+1 slap@1660+1
          point@1800+1 aimfar@1830+1 grip@1850+300"
```

The first two lines are the script. The third command is what the script
runs for the kind `state`. The kind `leash` stays at 100 units a meter and
changes the leash from a distance with the grip and with the trigger.

- The dialog closes at frame 571. The game camera comes to rest at the
  Temple near frame 675, and the view goes there. The steps start at frame
  800. In run hkS a zoom step at frame 700 came before that rest, and the
  view stayed at the shore: "the player moved the view, so the move of the
  game camera is dropped".
- Two zoom steps in (44 units a meter) bring the Creature into the view of
  the desktop window. At 100 units a meter the Creature is at the lower edge
  of the window. The controller code drops a press that starts off view
  ("the right button press started off view", run hkL).
- `seek` puts the Hand's point on the body of the Creature. The Creature
  walks, so the grip comes 6 frames after the seek.
- `nudge` steps the hand 2 cm to the side, `slap` 12 cm, and `rot` turns the
  wrist (`fake_controller_rots=-112,1,0 8,1,0` is a turn of 120 degrees).
- `aimfar` aims the hand at the Creature for the leash from a distance.
- Before the build `8757f73` the view stayed at the shore, 609 units from
  the Creature. The runs there turned the view (`stick_right` two times) and
  let the fake hand reach to the Creature.

### What the runs proved

| Item | Run | Numbers |
| --- | --- | --- |
| The Creature is in the list of the mod and is the choice at its capsule | hkO | "the target is 28E7F674 (Creature): the grip touches the Creature", radius 1.02, half height 1.81, hand mode `0xD` |
| The grip there starts the Creature state | hkO | The press at 838, "a press on the Creature" at 842, the state at 911 |
| A stroke reaches the game | hkB | A step of 2 cm, 0.45 m/s: the total goes from 0.00 to 0.10, animation `0x1B` |
| A slap reaches the game | hkB, hkO | hkB: 5.58 m/s, a hard slap, the total from 0.00 to -0.20. hkO: 6.90 m/s, which the game read as 113.9 units a second, the same total |
| The backhand: the Hand's point fast, the grip slow | hkB, hkP | A turn of the wrist of 120 degrees. hkB: the grip 0.00 m/s, the Hand's point 1.29 m/s. The game read 21.3 units a second, and the total went from 0.10 to 0.00, animation `0x21`. hkP: the grip 0.00, the Hand's point 1.51 m/s, read as 25.0 units a second |
| The speed of a slap follows `creature_slap_mps` | hkA | With 2.0 the steps and the turn of the wrist gave no slap. A swing at 5.71 m/s gave one, read as 47.1 units a second |
| The pose by the size: the index finger | hk5, hkO | The body is 3.6 cm of the room high, 0.19 of the length of the hand: "the index finger, and its tip touches" |
| The pose by the size: the flat hand | hkD | Five zoom steps in (13 units a meter), `seek_plan=creaturepalm`: "the flat hand, and its palm touches", the contact 12.7 cm inside the body |
| The way back (`creature_stroke_hand=3`) | hkC | "the hand of build 3918f87: a fist while the grip is closed". The turn of the wrist gave no slap. A swing at 5.44 m/s gave a hard slap |
| The leash from a distance | hk4, hkK, hkL, hkO | See "The leash from a distance" |

The runs hk5 to hkD used builds before the landing, with the same code of
the stroke. hkK to hkQ used this code on main `99f6124`. hkP and hkQ are
the two kinds of `creature_recipe.ps1` with the steps from frame 700.

**The code as it landed** (on main `4254690`, the script as it is):

- **hkT**, the kind `state`. The press at frame 938, the state at 986. "The
  hand of the stroke is the index finger": the body is 8.2 cm of the room
  high, 0.43 of the length of the hand. A step of 2 cm at 0.58 m/s is a
  stroke, and the game read 9.6 units a second. A step at 1.15 m/s is a
  slap, read as 19.1. The backhand is a slap: the grip 0.00 m/s, the Hand's
  point 1.31 m/s, read as 21.6. The swing is a hard slap: 6.80 m/s, read as
  112.1. Then the grip of 300 frames from 0.51 m: the leash is on at 1853,
  and no Creature state starts. The pulses: `target`, `activate`, `touch`,
  6 `stroke`, 3 `slap` and `leash on`.
- **hkU**, the kind `leash`. The grip from 0.31 m: on at 914. The trigger
  for 109 ms with the aim on the Creature: off at 989. The grip: on at 1083.
  The grip of 300 frames: off at 1185, and no Creature state. The pulses: 2
  `leash on`, 2 `leash off`, a `command`, and no `other`.

### Not proven, and suspects

- **One slap that was none (run hk6).** A step of 4 cm in one frame at 0.93
  m/s counted as a slap. A second slap came 25 frames later with a hand that
  was still. The runs hkB, hkO, hkP and hkT did not show it again. The line of a
  slap in the log has four more facts now, to find the cause when it comes
  again. They are the contact flag (`+0x110`), the animation (`+0x114`), the
  ms since a stroke, and the Update of the frame (the first or a later one).
- **A stroke at 44 units a meter.** In run hkO four steps of 2 cm gave the
  `stroke` ticks of the mod and no stroke of the game. In run hkT the same
  steps gave one stroke and one slap. The speed of a step depends on the time
  of its frame (0.58 and 1.15 m/s in hkT). A step of the fake hand is one
  frame long, and a real stroke is not, so the headset decides.
- **The size of the Creature.** In run hkK the capsule had a radius of 1.02
  and a half height of 1.81 at frame 793. At frame 1463 it had 1.44 and 2.55,
  for the same object. The mod reads the size of the game on each frame. Why
  the game changed it was not looked at.
- A smooth move through the body (`pull`, 0.16 and 0.61 m/s) is no slap, as
  it must be. No run showed a stroke from such a move.
- The look of the halo and of the hand at the Creature needs the headset.
  In the desktop pictures at 100 units a meter the Creature is a few tens of
  pixels high, and the hand hides it.

## The live target of a command (2026-10-04)

Buck, after the headset test of the morning of 2026-10-04 (build
`e951f87`): "If I press and hold the trigger when in creature command mode
and I happened to have some object highlighted, then moving my hand away does
not return to creature movement target mode which is annoying because often
times the target will lock on to some distant object. The target should
appear as soon as you are no longer pointing/locking on to an object which
should make it more easy to command the creature."

The code is in `creature_command.cpp`, with the rules in
`creature_command_math.h` (`AimRule`, `AimAtBody`, `HeldStep`,
`ReleaseLive`). This section replaces "The press chooses" and "The aim" of
"The Creature commands" for `creature_command_rule=2`, the default.

### What his log shows

The log of that session has 242 `creature cmd` lines with the leash on.

- **The press fixed its kind.** Of his 7 presses on an object, 3 ended with
  "nothing is sent: the aim left the target", after 733, 2023 and 2012 ms.
  He pressed with a halo in the aim, moved the hand to the land, and no arc
  came. Each time his next press, with no object in the aim, sent the
  Creature to the land.
- **Objects took the aim from far away.** The aim was on 114 objects. 84 of
  them were more than 3 m of the room from the Hand, 54 were 5 to 8 m away,
  and 17 were more than 8 m away, to 23.6 m. 67 were trees.
- **Objects took the aim from the side.** 57 of the 114 were more than 3
  degrees off the ray, and 35 were 6 to 8 degrees off it, at the edge of the
  cone of 8 degrees. 5 of his 7 presses on an object had the object 6.7 to
  7.7 degrees off the ray.
- **Why so far.** The land of the God view is 54 to 57 cm below the head, so
  the hand is 20 to 40 cm above the land. A ray from there to a point 5 m away
  goes down at 3 degrees. So a hand that points ahead has each tree of the far
  field in its cone. The arc for the same aim meets the land about 2 m away.
- With a cone of 3 degrees and 4 m from the Hand, 16 of the 114 are left: the
  Temple, a cow, pigs and trees that he pointed at.

**The press on the land met no land.** Each of his 12 presses on the land
said "The arc of the press has no land point: the arc meets no land". So a
tap never used the aim of the press, and each one used the aim of the
release. The press is read in the pump of the buttons, and the pump asked
`ControllersTeleportInput` for the aim. The room of the God view is known
only inside the draw of a frame (`TabletopRoom`, from the main camera call to
the end of `GGame::ProcessGraphicsEngine`). Outside of that time the function
gave the map of the immersive view, at 7.5 units a meter from the game
camera. The arc of 10 m then ends 10 units under the head, and the land is 54
units under it, so it meets no land. The numbers fit each of his 12 presses.

The pump always runs after the draw. [V] `GGame::Loop` calls
`GGame::ProcessGraphicsEngine` at `0x0054D3BE` and then `GGame::FlipScreen`
at `0x0054D3F5`, and `LHScreen::Flip` calls the pump. The desktop runs had
the same fault, but there it gave a point that looked right. In run hkM the
game camera was 3 units above the land at the door of the Temple. The press
met the land 5 units from that camera (1894.8 29.5 2519.8).

`ControllersFrame` now makes the aim once a frame, inside the draw
(`LatchFrameAim`), and each caller gets that aim until the next frame. In run
lv1 a press on the land meets 2001.0 55.4 2512.3. That is the point of the
ring of a hold in the same direction, 113 units from the game camera.

### The live target

While the trigger is held, the target is what the hand points at now.

| The trigger, with the leash on the Hand | What it does |
| --- | --- |
| The aim is on an object | The halo of the object shows, green |
| The aim is on no object | The arc and the ring show the land, in the same frame |
| The aim comes onto an object while the arc shows | The object must stay in the aim for 100 ms (`kHeldCatchS`). Then its halo shows and the arc goes |
| A tap: a release less than `creature_command_hold_ms` (250 ms) after the press | It acts on what showed at the press: the object of the press (packet `0x5D`), or the land point of the press (packet `0x5E`). A tap on your Creature is the leash click |
| A hold: a later release | It acts on what shows at the release: the object with the halo, or the land point of the ring |
| A hold that ends on your Creature | The leash click, only when the press started on the Creature and is shorter than 450 ms. A press that started on the land or on an object does not take the Creature as a target, so the ring can go past it |

`HeldStep` is the rule of what shows, and `ReleaseLive` the rule of the
release. The object at the Hand's point stays a target, as before.

### An object must not take the aim easily

With the leash on, the land is the first target: a press with no object in
the aim sends the Creature to the land. So an object is the target only when
the player points at it. `AimRule` has three rules, and each has a band.

| Rule | Numbers | Reason |
| --- | --- | --- |
| The cone | A new target needs the aim within 3 degrees of its body (`creature_command_cone_deg`). The target stays to 5 degrees (`creature_command_keep_deg`). A different object takes the aim only with a score 1 degree lower | In his log 57 of 114 targets were more than 3 degrees off the ray. The grab from a distance keeps its cone of 8 and 14 degrees, because a grip with no target does nothing, and a trigger with no target has the land |
| The body | The angle is measured to the upright body of the object: the line from its base to its top, with its 2D radius (`AimAtBody`). The Creature is a ball around its body bone | With a cone of 3 degrees, the aim on the crown of a near tree is more than 3 degrees from the base of the tree. The cone starts at the side of the body, so the radius adds to it: 2.6 degrees for a tree with a radius of 3.8 cm that is 0.82 m from the Hand (run lv1) |
| The range | The object is at most 4 m of the room from the head, along the floor (`creature_command_max_m`), and 15 cm more for the target of the last frame | 84 of his 114 targets were more than 3 m away. The arc for the land reaches 10 m, so the Creature can still go that far |
| The land in front wins | When the aim ray meets the land before the object, the object is out when it is more than 30 cm (`creature_command_behind_cm`) plus 20 percent of the distance of the land point behind that point. The target of the last frame stays to 1.5 times that | The player who points at the land in front of a tree wants the land. The share of the distance is there because a ray near the land moves its land point by 1 m for 1 degree |

The land point of the ray comes from a march along the ray in steps of 3 cm
of the room, with the sea as land at 0 (`RayLandDistance`). The march runs
once a frame, and only when an object in the cone needs it.

**The halo of the grip and the halo of the trigger.** The target of the grab
from a distance is the target of a command too, when it is in the cone of the
command. When the two targets are different objects, each has its halo: gold
for the grip and green for the trigger (`DrawHalo`). Until this build one
halo showed, and the trigger took the far target of the grip at 8 degrees.

**No flicker.** The cone, the range and the land rule each have a band. An
object that comes into the aim while the arc shows waits 100 ms. The change
from an object to the land has no wait, as Buck asked. The math test moves
an aim on and off an object each frame for 200 frames: the land shows and
nothing changes.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `creature_command_rule` | 2 | 2 is the live target with the rules of this section. 1 is the rule of the builds to 2026-10-04: the press fixes its kind, and an object takes the aim in the cone of the grab from a distance at each distance |
| `creature_command_cone_deg` | 3 | A new target needs the aim this near to its body, in degrees. 0.5 to 20 |
| `creature_command_keep_deg` | the cone plus 2 | The target stays while the aim is this near |
| `creature_command_max_m` | 4 | The range from the head, in meters of the room along the floor. 0 is no limit |
| `creature_command_behind_cm` | 30 | The land in front wins past this many cm of the room, plus 20 percent of the distance. -1 turns the rule off |

**The A/B key.** With `test_keys=1`, Ctrl with the 5 key of the number pad
changes `creature_command_rule` between 2 and 1 while the game runs. The
pointing controller gives a long pulse for 2 and a short pulse for 1, and the
log has a line. The keys of the aim trim (numpad 8, 2, 4, 6 and 5) do not act
while Ctrl is down.

Test keys for the desktop runs only. The script word `aimcmd` turns the ray
of the fake hand to the middle of the body of the nearest object that a
command can act on (`CreatureCommandTestPoint`), and `command_test_class`
names the class of that object. The word `aimturn` turns the aim by the next
step of `fake_controller_turns` (`yaw,pitch,0` in degrees, yaw to the left).

### The log

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
creature cmd : creature_command_rule=2. While the trigger was held, what shows changed 5 times. The range left out 54490 objects, and the land in front left out 125 objects of the cone. 0 double presses tied the leash, and 0 took it back
```

(From the runs lv2 and lv1.)

### Tests

**The math test** has 55 more checks (`LiveChecks` in `command_checks.h`):

- `AimAtBody`: the angle to the point of an object, to the top of its body,
  through its middle, over its top, beside it, under its base, and a body
  behind the hand.
- `RayLandDistance`: flat land, a level ray, land past the most distance, a
  start under the land, a wall of land, and the sea.
- `AimLandWins` and `AimInRange`: each limit and each band, and the two keys
  that turn a rule off.
- `HeldStep`: the land at once, the object after 100 ms and not after 88 ms,
  one object to a different one at once, a ring that passes 5 objects with 66
  ms on each (no halo), an aim at the border for 200 frames (no change), and
  a wait that starts again for a different object.
- `ReleaseLive`: 11 cases, and the 3 holds of his log that sent nothing. Each
  sends the Creature to the land now.

**Desktop runs** (2026-10-04, the Creature recipe: `spawn_run.ps1 -Choice
keep`). Each run has a render of 2880 by 2880 and no story. The God view is
at the door of the Temple, at 100 units a meter. The game makes 45 to 50
frames a second, so a frame is about 22 ms. The word `aimcmd` put the aim on
a Tree ("tree A"). It is 0.82 m of the room from the Hand and 1.44 m from the
head. Its radius is 3.8 cm, and its height is 13 cm of the room. A second
Tree is 8 cm beside it ("tree B").

**Run lv2** (`desk-lv2a`): one press of 200 frames, 5.4 s.

| Frame | The aim | What the log and the pictures show |
| --- | --- | --- |
| 1001 | On tree A | "the trigger closes with the aim on the Tree" |
| 1020 | 10 degrees away | Frame 1022: "the aim is on no object now. The arc and the ring show the land", 476 ms after the press. The overlay draws the arc (180 triangles) with the ring at 2001.0 55.4 2512.3. The card: "Trigger Release to send". The picture of frame 1043 has the arc and the ring, and no halo |
| 1060 | Back on tree A | Frame 1061: tree A is in the aim. Frame 1067: "Its halo shows, and the arc is gone". That is the wait of 100 ms. The card: "Trigger Send the Creature" and "Trigger x2 Tie the leash". The picture of frame 1085 has the halo, and no arc |
| 1095 | 10 degrees away | Frame 1097: the arc and the ring |
| 1110 to 1124 | 8 turns of 2 degrees, one each 2 frames, across the edge of the cone of tree B | Frame 1111: tree B is in the aim, 2.3 degrees off the ray. The band kept it at about 4.3 degrees (2.3 plus the turn of 2). Frame 1117: its halo shows. Nothing changed in the other 7 turns |
| 1140, 1160 | 2 degrees toward tree B, and 2 back | No change |
| 1180 | 4 degrees more, about 8.3 degrees from tree B | Frame 1182: the arc and the ring. The picture of frame 1191 has them |
| 1201 | The release, after 5372 ms | "so the aim of the release counts. Packet 0x5E: the Creature goes to 2010.0 54.6 2518.6" |

What shows changed 5 times in the press: one time for each move of the aim
onto an object or off it, and no more. The change to the land came 2 frames
after each turn, about 45 ms. The selection runs in the draw of a frame, and
`CreatureCommandFrame` reads it at the start of the next frame.

**Run lv1** (`desk-lv1b`):

- A press on tree A. A turn of 6 degrees put the aim 0.3 degrees from tree B,
  and the halo of tree B showed at frame 1017. A change from one object to a
  different object has no wait.
- 4 degrees more, about 4.3 degrees from tree B: tree B stayed. Later in the
  press the aim came to the same direction from tree A, and no object took
  it (frame 1112: the arc and the ring). That is the band: 3 degrees for a
  new target, and 5 for the target of the last frame.
- The release after 3655 ms: "so the aim of the release counts. Packet 0x5E:
  the Creature goes to 2001.0 55.4 2512.3".
- A tap of 117 ms on tree A: "a tap, so the target of the press counts, on
  the Tree. The game sends packet 0x5D".
- A tap of 119 ms on the land: "The arc of the press meets 2001.0 55.4 2512.3
  (the target is land)", and "a tap, so the aim of the press counts. Packet
  0x5E".
- A press that started on the land. The aim came onto tree A at frame 1381,
  and its halo showed at frame 1387. The release after 2518 ms: "a hold, so
  the target that shows now counts, on the Tree. The game sends packet 0x5D".
- The two reports: 4 presses were commands, 2 to an object and 2 to the land.
  In 900 frames the range left out 61026 objects, and the land in front left
  out 205 objects of the cone.

**Run old1** (`desk-old1b`, `creature_command_rule=1`, the script of lv1):
the press on a tree kept that tree through each turn. At the release the aim
was 10 degrees from its direction at the press: "the trigger opens after 3288
ms, with the aim still on its target, on the Tree. The game sends packet
0x5D". No arc showed in that press. With rule 2 the same script sends the
Creature to the land.

**Not run on the desktop:**

- The A/B key, Ctrl and numpad 5. A key needs the game window in front.
- Two halos for more than one frame. In the recipe only the Creature is a far
  target of the grip. Frame 1283 of run tie1 drew the gold halo on the
  Creature and the green halo on the tree.
- One object that only the range leaves out, and one that only the land rule
  leaves out. The counts of the report show that both rules act.

**Only the headset can show:**

- That each press on the land has its land point in the headset. The log
  line of the press must say "The arc of the press meets" and not "has no
  land point: the arc meets no land". The desktop shows the cause and the
  correction: see "What his log shows".
- If 3 degrees and 4 m are the right numbers for a real hand.
- If the wait of 100 ms is too long or too short.


## The leash on an object (2026-10-04)

Buck, in the same test: "As far as I can tell, there is no way to leash
your creature to something, I think double trigger press should leash a
creature to something."

### What the game does [V]

- **The double click.** The step of the default action (`fn_005D5190`) reads
  the bits of `GInterface+0x39`. With bit 8, the double click of the Action
  button, it runs `fn_005D4210` and nothing else. So the first click of a
  double click is a click (packet `0x5D` on an object), and the second sends
  only the packet of the double click.
- **`fn_005D4210`** (thiscall on `GInterface`, no arguments) takes the object
  of the next pick record (`+0x400`). When a leash of the player is tied to an
  object, it sends packet `0x5C` with 0 if the object is that object or the
  Creature (`0x005D42E0`). Else it needs: the object is not the Creature,
  `ValidAsInterfaceTarget` (slot `0x6F0`) gives 1, and the leash of this
  status is on the Creature (`Creature::GetInterfaceStatusLeashOn`). A
  `OneOffSpellSeed` gets a tap and no leash (`0x005D42BB`). Then
  `ValidAsInterfaceLeashTarget` (slot `0x6F4`) must not give 0, and it sends
  packet `0x5C` with the index of the object (`Base::GetIndexOffset`
  `0x00436A70`, through `fn_00550C80`).
- **The handler of packet `0x5C`** (`0x0063D416`) needs a Creature that can
  have a leash (`+0x1110`) and a leash that is on. With an object it ties the
  leash (`fn_005E6BD0`), ends the action of the Creature
  (`Creature::FinishActionUnsuccessfully`) and calls
  `Creature::ForceActOnObject`. With 0 it calls `GLeashStatus::SetOn(creature,
  0)` and `fn_005E6EA0`, which clears `+0x24` and puts the leash on again: the
  leash is back on the Hand.
- **The leash click** (packet `0x5F`, `fn_005E7140`) on a tied leash clears
  `+0x24` too.
- **While the leash is tied**, the game sends no command: `fn_005D3D10` and
  `fn_005D3930` need `+0x24` at null.

Each object that is a target of a command passes these tests, but a
`OneOffSpellSeed`. So the same objects can take the leash.

### What the mod does

| The trigger | What it does |
| --- | --- |
| A tap on an object, with the leash on the Hand | As before: packet `0x5D` at once, with no wait |
| A second press on the same object, 500 ms or less after the first press started (`creature_command_double_ms`) | `TieLeash`: the game's tests, then packet `0x5C` with the index of the object. The press does its work when it starts, and its release does nothing |
| A short press with the aim on your Creature, while the leash is tied | The leash click, packet `0x5F`: the leash comes back to the Hand |
| A tap and a second press on the object that holds the leash | `TakeLeashBack`: packet `0x5C` with 0, as the game's double click on that object. The first tap sends nothing |
| Each other press, while the leash is tied | It goes to the game, as with no leash |

**The two presses do not fight.** The first tap starts `ForceActOnObject`.
The handler of the second packet ends that action and starts it again with
the leash tied. The plain game sends the same two packets for a double
click.

**The grip takes the leash back too.** The agent `hand` built "Take the
leash back" for the grip on the Creature from a distance (kind 3 of
`near_math::LeashClick`). It sends the leash click.

**The card.** With the aim on an object that can take the leash, the card of
the hand has two lines: "Trigger Send the Creature" and "Trigger x2 Tie the
leash" (`AddPromptTwice` in `hint_card.cpp`). While the leash is tied, the
object that holds it has the green halo and the line "Trigger x2 Take the
leash back", and your Creature has "Trigger Take the leash back".

**The log** has a line for the double press, and a line for each change of
the object of the leash:

```
creature cmd : a double press on the Tree 2A67A804, 288 ms after the first press. Packet 0x5C: the game ties the leash to it, frame 1023
creature cmd : the leash is tied to the Tree 2A67A804 now (the game has the leash on, and the Creature has it as the leash of this player). The trigger sends no command until the leash is back on the Hand, frame 1025
creature cmd : the trigger opens after 115 ms, and nothing is sent: the leash is tied to this object. A second press takes the leash back, frame 1106
creature cmd : a double press on the Tree 2A67A804, 276 ms after the first press. The leash is tied to it. Packet 0x5C with no object: the leash comes back to the Hand, frame 1113
creature cmd : the leash is not tied to an object now (it was on 2A67A804). The leash is on the Hand, frame 1116
far grab    : the grip closed on your Creature 2A64C674, 0.36 m of the room from the Hand. The leash click (packet 0x5F): the leash comes back to the Hand. No press goes to the game, so the Creature state does not start, frame 1301
creature cmd : a short press of 118 ms with the aim on your Creature: the leash click (packet 0x5F). The leash comes back from the object to the Hand, frame 1526
```

(From the run tie1.)

`creature_command_double_ms=0` turns the double press off.

### Tests

**The math test**: 7 checks of `DoublePress` in `LiveChecks`.

**Desktop run tie1** (`desk-tie1a`, 2026-10-04, the recipe and the tree A of
the runs above):

| Frame | The trigger or the grip | What the log and the pictures show |
| --- | --- | --- |
| 966 | | The card: "Trigger Send the Creature" and "Trigger x2 Tie the leash" |
| 1011 to 1016 | A tap of 115 ms on tree A | "a tap, so the target of the press counts, on the Tree. The game sends packet 0x5D" |
| 1023 | A second press, 288 ms after the first | "a double press on the Tree. Packet 0x5C: the game ties the leash to it". Frame 1025: "the leash is tied to the Tree now (the game has the leash on, and the Creature has it as the leash of this player)", with the pulse "leash on" |
| 1042, 1083 | | The card: "Trigger x2 Take the leash back". In the picture of frame 1083 the rope goes from the tree to the Creature. In the picture of frame 1008 it goes from the Hand to the Creature |
| 1101 to 1106 | A tap on tree A | "nothing is sent: the leash is tied to this object. A second press takes the leash back" |
| 1113 | A second press, 276 ms after the first | "Packet 0x5C with no object: the leash comes back to the Hand". Frame 1116: "the leash is not tied to an object now. The leash is on the Hand" |
| 1201 to 1213 | A tap and a second press | Frame 1218: the leash is tied again |
| 1282 | The aim goes to the Creature | The card: "Grip Take the leash back" and "Trigger Take the leash back" |
| 1301 | The grip closes | "far grab: the grip closed on your Creature. The leash click (packet 0x5F): the leash comes back to the Hand". Frame 1304: the leash is on the Hand |
| 1421 to 1433 | A tap and a second press | Frame 1435: the leash is tied again |
| 1521 to 1526 | A short press of 118 ms with the aim on the Creature | "the leash click (packet 0x5F). The leash comes back from the object to the Hand". Frame 1532: the leash is on the Hand |

3 double presses tied the leash, 2 to 5 frames after the second press. The
first tap and the second press did not fight. Each of the three ways back
worked:

- the double press on the object
- the grip on the Creature from a distance (the "Take the leash back" of the
  agent `hand`)
- the short press of the trigger on the Creature

**Not shown by the run:** the walk of the Creature to the tree. The script
took the leash back 2 s after each tie, and the Creature had moved 2 units.
What the Creature does on a tied leash is the game's own.

**Only the headset can show:**

- If 500 ms is the right time for a real finger.
- If it feels right that the first tap sends the Creature before the second
  press ties the leash.


## The scroll from its draw, and the click on a scroll (2026-10-04)

Buck, after the headset test of the morning of 2026-10-04 (build `e951f87`):
"The biggest one is I am still unable to consistently interact with scrolls.
The orb on my hand does not light up at all. I managed to trigger one quest
by spamming grip and moving my hand all around but it was not consistent at
all." This was the fourth report of the scroll.

The code is in `hand_direct.cpp` (`h_ScrollDraw`, `ClickScroll`, `Choose`,
`ScrollClick`, `h_RightPress`, `FarSelect`, the words of `seek_plan`),
`hand_glow.cpp` (`HandGlowPicturePoint`) and `near_math.h` (`ScrollReach`,
`ScrollPress`, `TargetRule`, `PicturePoint`).

### The cause [V]

**The scroll of a challenge is not active: the game makes it active at the
first click.** The game taps a scroll only while it is active. The mod took
"a press does not tap it" as "a press does nothing", so such a scroll was no
target: no glow, no halo, and a press with the pick record empty.

- `ScriptHighlight::InterfaceValidToTap` `0x0070ADD0` gives 1 for a silver
  scroll (type 1) with a script (`+0x78`). For each other type it also needs
  the active flag (`+0x60`).
- `ScriptHighlight::SetActivated` `0x0070A630` sets the flag. Its callers
  are the script word `SET_ACTIVE` (`GScript::SetActive` `0x006FD720`, native
  255), the creation and `Process` (type 1 only), and `InterfaceTap`.
- The script `ChallengeHighlightNotify` (`challenge.chl`, script 6, file
  `ChallengeNotify.txt`) owns the scroll of a challenge. It loops until
  `GAME_THING_CLICKED(Highlight)` or `GAME_THING_CLICKED(Location)` is true.
  Only then it calls `SET_ACTIVE(Highlight, 1)`, and the challenge starts.
  `Location` is an object of the challenge, for example a house.
- `GAME_THING_CLICKED` (`GScript::GameThingClicked` `0x0070AEB0`, native 16)
  compares its object with the clicked object of the interface
  (`GInterface+0x45C`, with the game turn at `+0x468`).
- `fn_005D36D0` writes that record. Only `fn_005D3930` (thiscall on
  `GInterface`, one `Object*`, `ret 4`) calls it, before each test of a tap.
- The game calls `fn_005D3930` from three places: the right press on an
  object that it can tap, hold or leash (`fn_005D1740`, `0x005D1833`), and
  two places of the interface step for a press that ended within 225 ms with
  an object in the pick record (`0x005D531B` and `0x005D5426`).

So in the plain game a short click on a scroll that is not active is a click
for the scripts, with no tap. The script then makes the scroll active and
starts the challenge. With one target (`near_rule=2`) the pick record held no
object at such a scroll, so no click reached the script.

In the loop the script also lets the advisor speak when the camera position
is less than the global `NotifyDistance` from the scroll and the scroll is in
the field of view. That part does not change the active flag. No instruction
of `challenge.chl` writes `NotifyDistance`.

**What the plain game shows at such a scroll.** The interface gives hand mode
`0x12` (something to interact with) for a scroll in the pick record, active
or not: the mode needs only that the object is available. In run hq4 the mode
went to `0x12` at frame 1134, two frames after the Hand came to the scroll
that was not active, and the click came at frame 1202. So the plain game
shows the same Hand over the two states, and a click works on the two. Only
the beam of light shows that a scroll is active. The glow and the halo of the
mod show on the two states for the same reason: a press of the grip does
something.

### What his log shows

`presses.py` in the scratchpad lists each press of the grip of a log with
the scale, the target and the answer of the game's search at the palm. His
session has 175 presses.

- 41 presses from frame 6054 to 8729 are at one place, at 29.6 units a
  meter. Each one says "no target". At 35 of them the line says "No object
  that a press acts on is near the point". The game's search at the palm
  found the scroll `2AA3DB54` 14 times, with its mesh point 3.5 to 13.7 cm of
  the room from the Hand's point.
- Two more groups of presses with no target are at 44.4 units a meter (16
  presses from frame 17834) and at 13.2 and 29.6 (20 presses from frame
  22502).
- One press tapped a scroll: `2AA3DAC4` at frame 44200, with a gap of 0.0
  cm. At frame 44188, 12 frames before, a press tapped a `Creche`. That house
  is the `Location` of the challenge, so the script made the scroll active,
  and the next press tapped it.

So the fault was not a body that was some centimeters from the picture. The
hand was at the scroll, and the mod said that a press does nothing there.

### How the game draws a scroll [V]

`ScriptHighlight::Draw` `0x00709C60`, on each call (each eye pass):

1. It does nothing when `+0x5C` is not 0, or with no mesh (`+0x40`). A
   scroll that is not silver is also not drawn while the flag at
   `*(GGame+0x250090)+0x80` is 0.
2. **The place.** The point of the scroll on the map, at the altitude of the
   land plus the height in `+0x1C`. `ScriptHighlight::Process` sets that
   height on each game turn. There is no bob.
3. **The turn.** The angle is the angle of the mesh (`+0x48`) plus 0.00314
   radians for each ms of the frame. Each call adds a step, so the second
   eye pass draws the scroll a step further than the first one. A silver
   scroll that is active faces the camera of the eye pass.
4. **The size.** A scroll that is not silver has its own scale times `d / 30`,
   where `d` is the distance from the game's camera to the scroll, held
   between 10 and 30 units. The camera is the point of `GGame::GetCamera` at
   `+0x118`, `+0x148` and `+0x178`: the game's own camera, and not the camera
   of the eye pass. So the two eye passes get one size. In the God view the
   game's camera is 400 units from the player, so each scroll is at its full
   size at each scale.
5. `LH3DObject::SetPosition` puts the point, the angle and the scale in the
   matrix of the mesh (`0x00709ED2`).
6. The draw takes the middle and the half sizes of the box of the mesh
   through that matrix: the middle in the world, and the length of the half
   diagonal (`0x00709ED7` to `0x00709FC0`).
7. `SingleMapFixed::Draw` draws the mesh. It offers the scroll to the pick
   as each object (`Game3DObject::AddForDrawing`).
8. `GInterface::SendInvisibleDrawCollision` `0x00519960` gets the middle and
   the half diagonal plus 0.5 (`0x0070A2D7`). It offers the scroll only when
   the pick pixel is inside the circle of that ball on the screen.
9. **The glow of the game.** A sprite goes 1.4 times the half diagonal from
   the middle toward the camera of the eye pass, with a size of 2 times the
   half diagonal (`0x0070A2DF` to `0x0070A447`). Each eye has its own sprite
   place, on the line from the middle to that eye.
10. A scroll that is active and not silver has a beam too (`+0x68`).

The type is the index of the info at `+0x28` in the list at `0x00D96390`,
with 272 bytes for each info: 0 bronze, 1 silver, 2 gold, 3 and 4 a
scoreboard, 5 and 6 the cancel scrolls, 7 and 8 the Creature scrolls, 9 the
brotherhood. The scrolls of the challenges of Land 1 are type 2.

### What the mod does

**The body of a scroll is its draw.** A hook on `ScriptHighlight::Draw`
records each scroll that the game draws in this frame, after the game's own
code. The body is the ball of step 6: the middle and the half diagonal of the
box of the mesh through the matrix that the draw set. So the body is the
scroll of the picture by construction. The scroll is a candidate of the
choice from that draw. The game's pick does not have to offer it.

**Both eyes have one body.** The place and the size do not use the camera of
the eye pass. In the runs the ball of the two passes was 0.000 units apart.
The second pass turns the scroll one step further, which does not move the
middle of the box by more than that.

**The reach.** The pick reaches 10 cm of the room for a scroll
(`scroll_reach_cm`), and 4 cm for each other object. 10 cm is the gap at
which the grab from a distance goes off, so no band is between the two. A
scroll past the reach of the pick is rule 5 of the choice, the last one. So
the longer reach takes no tree and no house from the Hand.

**The click.** `near_math::ScrollPress` gives what a press does on a scroll:
a tap when the game's test of a tap says yes, a click when the game draws
the scroll and does not tap it, and else nothing. A scroll of the click is a
target as a scroll of the tap is: the same glow, the same halo and the same
pulse. At the press the mod calls `fn_005D3930` with the scroll. The game's
own handler does not get that press, because it would take it as a press on
the land. The mod checks the first 10 bytes of the function before the call.

**The far touch** takes such a scroll too, and its press is the same click.

**A press with no target** names the nearest object that a press acts on,
its distance and its reach, at each press. It also names the nearest scroll
in view, whether it is active, and which test of the game says no.

### Other objects that float [V]

`GInterface::SendInvisibleDrawCollision` has six callers. Each one offers
its object only when the pick pixel is in the circle of a ball on the
screen:

- `ScriptHighlight::Draw`: the scroll.
- The draw of `SpellSeedGraphic`: the bubble of a miracle. The ball is the
  point plus the middle of the local box, with a radius of 2 times `+0x54`.
  The camera does not scale it.
- `SpellDispenser`, `MagicTeleport` and `LandscapeVortex`.
- `fn_00466730`, which `CitadelHeart::DrawNow` calls for three objects with
  a radius of 1.0. These are probably the three leash rings at the Temple.
- A reward draws with the draw of the bubble (`Reward::ProcessDrawing`) and
  `Game3DObject::AddForDrawing`.

Only the scroll takes its size from the distance of a camera. For each other
one the body of the mod is the box of the mesh at the place of the draw, as
for each object. No run tested a bubble, a dispenser, a teleport or a reward
at the Hand. The signpost was not examined.

### Keys

- `scroll_draw=1` (default): each scroll that the game draws is a candidate,
  with the ball of its draw. `0`: only what the game's pick offers, as in the
  builds before. Ctrl and the point key of the number pad change it in play.
- `scroll_reach_cm=10`: the reach of the pick for a scroll, 0 to 30.
- `scroll_click=1` (default): a press on a scroll that is not active is a
  click for the scripts. `0`: such a scroll is no target, as in the builds
  before.

Test words of `seek_plan`, for the desktop runs only:

- `go`: the God view teleports to the next place of `seek_go="x,z;x,z"`.
- `scrolls`: one log line for each scroll of the frame.
- `picture`: the next entry of `seek_picture="lx,ly,rx,ry;..."` is the pixel
  of one thing in the picture of the left eye and of the right eye. The mod
  finds the place of the world that shows at the two pixels
  (`near_math::PicturePoint`), and the fake hand goes there.

### The log

Each line that names a scroll gives its state in the same plain words
(`ScrollWords`): "The scroll is active: a press taps it", "The scroll is not
active: the game makes it active at the first click", or "The game says that
a press does not tap it" with the test of the game that says no.

- Tag `hand scroll`: `scroll_click` and `scroll_draw` at the start. One line
  for each change of the scroll that is nearest to the Hand's point within
  30 cm of the room, with what a press does. One line for each scroll that
  changes between active and not active: "the scroll P (type, script N) at x
  y z is active now: a press taps it", or "is not active now: the game makes
  it active at the first click", with the distance of the game's camera, of
  the camera of the eye and of the Hand's point.
- Tag `hand pick`: the line of the target says "the grip clicks it (the
  scroll is not active: the game makes it active at the first click)".
- Tag `hand press`: "the grip pressed on the target P (ScriptHighlight): the
  scroll is not active: the game makes it active at the first click. This
  press is that click (fn_005D3930 of the game), so the script that waits for
  it goes on". For the far touch the line adds ", the target of the far
  touch".
- Tag `hand press`, a press with no target: "The nearest object that a press
  acts on is P (class, kind), N cm of the room from the point, and its reach
  is R cm. The nearest scroll in view is P (type, script N), M cm of the room
  from the point, and its reach is 10 cm.", and then the state of that scroll
  in the plain words.
- Each report window: the frames with scrolls in view, the scrolls that the
  game's pick did not offer, the largest difference of a ball between the two
  eye passes, and the count of clicks.

### Tests

**The math test** has 4 new checks for `ScrollPress`, after the checks of
`ScrollReach`, rule 5 and `PicturePoint`: 2175 checks in all, 0 failed.

**Desktop runs**, all with the start "Keep your old Creature" of
`spawn_run.ps1`, at a render of 2880 by 2880. The scroll is the challenge
"Creature saving people" of Land 1: script 36, at 1755.47 13.27 2523.73.

- **hn4** (the list): after `go` to 1736.7 2518.5 at 29.6 units a meter, 19
  scrolls are in the frame. The ball of the scroll has its middle at 1755.42
  15.62 2523.75 and a radius of 2.32 units. The scroll is not active, and the
  line says that `InterfaceValidToTap` says no. The game's camera is 410.3
  units from it, and the eye 24.5.
- **hq2** (the pictures): from the Temple, after four turns to the right,
  the scroll is in the two eye pictures. `find_blob.py` gives its middle: at
  100 units a meter the pixel 1668.1 1734.3 of the left eye and 1611.8
  1734.0 of the right eye, and at 29.6 units a meter 1677.5 1387.0 and
  1657.8 1386.8.
- **hq3** (100 units a meter, word `picture`): the two pixels are the place
  1755.69 15.77 2523.79, 0.31 units (0.3 cm of the room) from the middle of
  the ball in the mod's list. The fake hand goes there, and the glow draws at
  the pixels 1668.1 1734.1 and 1611.8 1734.1. The target line says "the grip
  clicks it". The press at frame 1062 is the click, the scroll is active at
  frame 1067, and the cut scene of the challenge starts at frame 1106.
- **hq4** (29.6 units a meter, word `picture`): the two pixels are the place
  1759.26 16.51 2523.94, 13.0 cm of the room from the middle of the ball. At
  3.4 m of the room one pixel of disparity is about 17 cm, so this is the
  limit of the picture and not of the body. The Hand's point is 5.2 cm from
  the ball, inside the reach of 10 cm. The press at frame 1202 is the click,
  the scroll is active at frame 1208, and the cut scene starts at frame
  1213. The picture of frame 1173 has the gold halo on the scroll.
- **hq5 and hq6** (the installed build `e951f87`, the fake hand at the same
  two points): "the grip pressed with no target at the Hand's point ... No
  object that a press acts on is near the point" at frames 1062 and 1202. No
  cut scene starts, and the picture has no halo.
- **hn7** (the click with the word `go`): press at frame 1062, active at
  frame 1064, and the cinema bars are up at frame 1067.

The three runs below are on the build that landed (main `bedfa95` and the two
scroll commits). Buck worked at the PC in that time. Each log was checked
for input that was not from the test: no "the cursor moved" line, 1 press and
1 release sent, 0 forced releases, and the same two "the mouse owns the
pointer" lines as in runs hq3 and hq4 (the start dialog and the cut scene).

- **hr1** (the near click again, 29.6 units a meter, word `picture`): the
  target line says "the grip clicks it (the scroll is not active: the game
  makes it active at the first click)", 5.2 cm from the ball. The press at
  frame 1202 is the click, the scroll is active at frame 1207, and the cut
  scene starts at frame 1211.
- **hr2** (the far touch, `far_test_scroll=1`, 100 units a meter): `aimfar`
  aims at the scroll of script 36, 1.16 m of the room from the Hand. "the
  grip closed on the scroll" at frame 1063. The press line says ", the target
  of the far touch ... This press is that click". The scroll is active at
  frame 1071, the far touch is over at frame 1073, and the cinema bars are up
  at frame 1096.
- **hr3** (the same with `scroll_click=0`): the scroll of script 36 is no far
  target. `aimfar` takes a silver scroll (type 1, script 4170) 1.47 m away,
  which the game taps with no active flag. The far touch of the builds before
  taps it, and it is active at frame 1070.

**Only the headset can show:**

- If 10 cm is a good reach for a real hand at each scale.
- If the glow and the halo show at each scroll that Buck could not start.

**Not proven, and suspects:**

- The scoreboard and the Creature scrolls had no run. A silver scroll with a
  script is a tap from the start: run hr3 tapped one from a distance.
- The start value of `NotifyDistance` was not read in a run. If it is more
  than 0, the advisor speaks of a scroll only with the game's camera near
  it. In the God view that camera is 400 units from the player.

## The limits of the hand of a stroke (2026-10-04)

Buck, after the headset test of the morning of 2026-10-04 (build `e951f87`):
"Maybe increase/decrease the threshold for fingertip/palm creature
interaction a bit. I was zoomed in fairly close, the creature was the size of
my hand roughly but it was still using fingertip to stroke it, maybe change
the threshold by 25% or so and see how that works."

The code is in `hand_direct.cpp` (`TouchFrame`, `StrokeHand`, `PollNearKey`),
`overlay.cpp` (`ChoosePose`) and `near_math.h` (`StrokeLimits`,
`StrokeFingerAtStart`, `StrokeFinger`).

### What his log shows

His log has one stroke session, from frame 2284 to frame 4206 (46 s), at 13.2
units a meter. The body of the Creature was 27.6 cm of the room high, 1.45 of
the hand's length. The mod chose the flat hand at the start, and the overlay
took the pose "flat" at frame 2285. No line of the log has the index finger in
a stroke. So the limits did not give him the finger in that session.

**The body in the ratio** is the capsule of the touch: `1.1 S` high, with `S`
15 times the size of the Creature. In the picture of run hkT a Creature that
stands is 67 pixels high and its capsule 68. A Creature that sits or lies is
lower than its capsule.

**The picture of the flat hand is a flat hand.** In the captures of runs hp7
(13.2 units a meter) and hp4 (19.8), with the grip closed and the trigger
open, the four fingers are straight and together and the thumb is out. The
range of the pose lets the grip curl the three fingers by 0.20 at most.

**The point of the interaction was the fingertip.** [I] With the flat hand
the contact of the mod is the palm, but two things stay at the Hand's point,
the tip of the index finger, 12.1 cm of the room from the palm:

- the glow, which shows the player where the Hand acts
- the ray of the game's stroke: the mesh ray test `fn_00865020` casts from
  the eye through the Hand's point (`0x005B19F1`), so the place of the
  stroke on the body of the Creature is the place behind the fingertip

The palm was at the edge of the capsule in most strokes of his session (0.00
to 0.19 units inside). The capsule of the touch is `creature_touch_cm` (5 cm)
wider than the body, so the fingers were in the body and the palm was not.
This is the likely meaning of "it was still using fingertip to stroke it".

A second thing was a fist: from the press to the start of the Creature state
the hand was a fist for 184 frames. The game starts the state only when the
Creature has ended its body action.

### What the mod does

**The limits move 25 percent toward the flat hand**, as he asked. The finger
is below 0.60 of the hand's length (0.80 before), the flat hand over 0.90
(1.20 before), and the band between the two holds the pose of the frame
before. A stroke starts with the flat hand from 0.75 (1.00 before).

| The body against the hand | A stroke starts with | In a stroke |
| --- | --- | --- |
| less than 0.60 | the index finger | the index finger |
| 0.60 to 0.75 | the index finger | the pose of the frame before |
| 0.75 to 0.90 | the flat hand | the pose of the frame before |
| more than 0.90 | the flat hand | the flat hand |

For his Creature (size 0.22, a capsule of 3.63 units):

| Units a meter | Body | Ratio | Before | Now |
| --- | --- | --- | --- | --- |
| 13.2 | 27.6 cm | 1.45 | flat | flat |
| 19.8 | 18.4 cm | 0.97 | finger | flat |
| 29.6 | 12.3 cm | 0.64 | finger | finger |

**The hand opens before the state starts** (`creature_open_wait=1`). A press
on the Creature that is older than the time of a leash click (450 ms) is a
stroke, so the hand takes the pose that the stroke starts with, and not a
fist.

### The point of the flat hand

**The point of the flat hand is the palm** (`near_math::PalmPoint`,
`creature_palm_point=1`). In the Creature state with the flat hand the Hand's
point of a frame is the tip of the index finger of that frame and the offset
from the tip to the palm of the frame before (`HandExactPalm`,
`near_math::PalmOffset`). So the point moves with the hand of this frame,
with no lag. The glow shows at the palm, the ray of the game's stroke goes
through the palm, and the contact of the mod is the same point. With the
index finger the point stays at the tip. An offset of more than 8 frames ago
is not used, and then the point is the tip.

The mod's hand does not move. Only the point of the game's Hand, whose mesh
is hidden, goes to the palm: the target (`g_target`). The mod's hand stands
on the tip of the index finger (`g_tip`, `HandDirectExactFacts`). When the
land holds the tip up (`ResolveLand`), the palm goes up as far as the tip.

**The fault of the first build** (branch `hand-all`, run hs1). The palm comes
from the mod's hand, and in that build the mod's hand stood on the target.
With the target at the palm, the palm of the next frame was one offset
farther, and so on: the point ran away 11.4 cm of the room on each frame.

- **hs1** (19.8 units a meter, `creature_palm_point=1`): the state starts at
  frame 1130. The glow goes from the pixel 1996.2 1399.2 to 2041.9 1271.7 at
  frame 1132, and it leaves the view (414 eye passes "outside the view").
  After 30 frames the log says "The Hand's point is 60.45 units outside" the
  body, and "hand+0x78 is 2.2569 units from the target": one offset. The
  nudges give no touch and no stroke.
- The math test has this fault as a check: with a hand that stands on the
  target, the point is 60.9 units from the tip after 27 frames.

The two suspects of the first report were not the cause. `ResolveLand` did
set the height of the palm to the land under the tip, which is wrong only
with the tip in the land. It now lifts the palm as far as the tip. The test
words of the fake hand move the tip, as a controller does.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `creature_finger_below_pct` | 60 | The index finger while the body is below this percent of the hand's length. 10 to 500 |
| `creature_flat_above_pct` | 90 | The flat hand over this percent. Not less than the finger limit |
| `creature_open_wait` | 1 | The hand opens when a press on the Creature is 450 ms old. 0: a fist until the state starts, as in the builds before |
| `creature_palm_point` | 1 | With the flat hand of a stroke the Hand's point is the palm. 0: the tip of the index finger, as in the builds before |

With `test_keys=1`: Ctrl and the 3 key of the number pad change between the
limits of the ini and the limits of the builds before (80 and 120). The
pointing controller gives a long pulse for the limits of the ini and a short
pulse for the limits before. Ctrl and the 9 key change `creature_palm_point`
(a long pulse for the palm, a short pulse for the tip).

### The log

Tag `hand creature`:

- The keys at the start.
- "the hand of the stroke is the flat hand, and its palm touches. The body is
  N cm of the room high, R of the hand's length (the start of a stroke: the
  finger below A, the flat hand over B, and a stroke starts with the flat hand
  from C ...)", at each start of a stroke and at each change of the hand in a
  stroke. The line gives the contact point, its distance from the tip of the
  index finger and how far it is inside the body.
- "the point of the flat hand is the palm now ... The palm is N cm of the room
  from the tip of the index finger", and "the point of the Hand is the tip of
  the index finger again", one line for each change.
- "the press is N ms old and the Creature state has not started. The hand is
  open now".
- Each report window: the count of frames with the point at the palm.

### Tests

**The math test** has checks for the limits: the three scales of his
Creature, the start at 0.75, the limits of the builds before, and limits that
are not sane. It has 13 checks for the point of the flat hand: the palm is
the tip and the offset, the finger and the key at 0 keep the tip, an old
offset and an offset that is not a number are not used, the palm follows the
tip of this frame, and the point does not run away (2233 checks in all, 0
failed).

**Desktop runs**, with the Creature recipe:

- **hp4** (19.8 units a meter): body 18.4 cm, ratio 0.97. The hand opens 474
  ms after the press, flat. The state starts at frame 1127 with the flat
  hand, and the nudge gives a touch and a stroke at frame 1602.
- **hp5** (the same with the limits of the builds before and
  `creature_open_wait=0`): a fist until the state starts at frame 1130, then
  the index finger.
- **hp6** and **hs3** (29.6 units a meter): body 12.3 cm, ratio 0.64, the
  index finger, and its tip touches.
- **hp7** (13.2 units a meter): the hand opens flat 450 ms after the press.
  The state did not start in that run.

**The runs of the point of the flat hand** (2026-10-04, 16:35 to 16:43, the
build of this change). The steps are four zoom steps (19.8 units a meter),
the word `seek` to the body of the Creature, the grip, and four steps of the
hand of 2 cm (`nudge`) in the state. Runs sc-p3 and sc-p4 have
`god_ground_down_cm=120`, so that the eye sees the hand over the post of the
fence.

- **sc-p3** (`creature_palm_point=1`): the state starts at frame 1139 with
  the flat hand, and the point is the palm from frame 1140, 11.4 cm of the
  room from the tip. After 30 frames "The Hand's point is 0.23 units inside"
  the body, and "hand+0x78 is 0.0040 units from the target". The steps of the
  hand take the palm out of the body and in again: a touch, a tick of a
  stroke and a slap at the frames 1216 and 1286. The glow is at the pixel
  1942.3 1652.6 of the left eye, 48 pixels from the tip of the index finger
  at 1925.0 1697.8. In the picture of frame 1323 the glow is in the middle of
  the flat hand, beside the body of the Creature.
- **sc-p4** (`creature_palm_point=0`): the same steps. The glow is at the
  pixel 1925.0 1697.8, and in the picture of frame 1323 it is at the tips of
  the fingers, at the feet of the Creature. A touch and a tick of a stroke at
  the frames 1217 and 1287, and no slap (0.00 and 0.80 m/s).
- **sc-p1** (the first run, with no step of the hand in the state): the
  point is the palm from frame 1134, and it is 0.24 units inside the body
  after 30 frames. A post of the fence hides the hand in its picture.
- **sc-p2** (three zoom steps, 29.6 units a meter): the index finger, and its
  tip touches. In the picture of frame 1303 the glow is at the tip of the
  finger.

In sc-p3 the game read each step back into the body at 1.47 m/s, which is a
slap. In sc-p4 it read 0.00 and 0.80 m/s. [I: with the point at the palm the
contact has no lag of one frame, so the speed of the step of the fake hand is
in the test. The fake hand moves 2 cm in one frame. A real hand that strokes
does not.]

A capture of 2880 by 2880 can take more than 250 ms. The controller then
lets go of its buttons ("not usable now"), and the Creature state ends. So
the picture of each run is its last step.

**Only the headset can show:**

- If 0.75 is a good change point for a real hand.
- If the stroke feels right with the palm as the point, if the glow at the
  palm reads well, and if a slow stroke with the flat hand is never a slap.

**Not proven, and suspects:**

- The tickle moves the fingers of the flat hand while the palm is in the
  body. If Buck reads that as a stroke with the fingers, the tickle of the
  flat hand can be made smaller.
- The flat hand with the tip in the land (the palm goes up with the tip) had
  no run.

## A miracle in the Hand (2026-10-04)

Buck, after the headset test of the evening of 2026-10-04 (build be11418):
"I'm not sure how to use miracles, I basically softlocked the game, I did the
quest which gave a creature strength miracle, I picked it up, couldn't figure
out how to activate it, started pressing random buttons until I did somehow
activate it, and then it was basically stuck to my hand, wherever I pointed
on the ground the potion model kind of stuck to and floated around on the
terrain, leaving a nice red trail behind. But pressing grip button or trigger
button didn't actually do anything, I tried clicking on my creature with it,
it still didn't work, I just kept getting these yellow "X" symbols and a
"head shake" kind of sound ... I couldn't actually cancel the miracle either,
none of the buttons cancelled it, nor shaking the controller or anything."

The code is in `src/mod/miracle.cpp`, with the pure math in `miracle_math.h`.
`hand_direct.cpp` places the Hand and holds the pick record,
`controllers.cpp` gives the buttons, `hand_glow.cpp` draws the halo and
`hint_card.cpp` the lines of the card.

### What the game does [V]

All of this is from `disasm.py` and the Ghidra project on `runblack.exe`
1.42, and from `Scripts\info.dat`.

**Two classes.** A miracle is in one of two objects:

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

**A seed from a Worship Site** comes from the chain of gestures of section 5:
packet `0x25`, `FUN_005DABA0`, `FUN_0064BDB0`. It has the icon link at
`+0x5C`. `SpellSeed::ProcessInHand` `0x00729930` makes it ready after the
time at `0x00DA0584`.

**The states.** With a `SpellSeed` in the Hand, `CHand::GetRequiredState`
gives state 8, Grain, and the hand mode chooser `fn_005D7F20` gives hand mode
5 (`0x18` for each other object in the Hand). `HandStateGrain::Update`
`0x005B3310` calls `HandStateHolding::Update` at `0x005B3361`. So the Grain
state places the Hand as the Holding state does, with the ray, the distance
and the pose builder. Then it keeps the place of the Hand at `+0x48E0`, and a
filter of the speed of the Hand at `+0x48C8`: the throw velocity. The rate of
the filter is `-10 ln(1 - 0.8)` a second (`0x00BF014C`), a time constant of
62 ms. A one-off seed that the Hand carries is in state 4, Holding.

**The seed info.** `GSpellSeedInfo`, 30 records of `0x190` bytes at
`0x00D9D678` (`load_variables`, `0x0042C28E`, the block `DETAIL_SPELL_SEEDS`).
In `Scripts\info.dat` the records are `0x180` bytes from offset `0x3D28`, and
a field at `+X` in memory is at `+X - 0x10` in the file.

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

The desktop runs showed the same for types 0, 2, 4, 7, 9 and 16.

**The buttons.** The left button drags the land, as with an empty Hand. The
right button, the Action button, casts. `FUN_005D1330` (the right press)
goes to `FUN_005D1560` when the Hand holds an object
(`GInterfaceStatus+0x90`):

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

**How a miracle gets stronger.** Only a seed from a Worship Site can power
up. `fn_005CEE80` lists the gestures of the power ups that the player has and
that the site can pay (`fn_0077FBF0`), at `GInterface+0x16C`. `fn_005D0000`
matches them, and a match sends packet `0x6A` (`FUN_005DAC30`,
`SpellSeed::SetPowerUp` `0x00729B30`, which takes the chants from the site).
The gestures are the spiral and the inverse spiral. A shake of a seed with a
power up takes the power up off first.

**The cancel.** The shake is the scribble gesture, type 5. For a seed from a
Worship Site `fn_005D0000` matches it. For each other object in the Hand
`ProcessPowerUpSystem` matches it at `0x005CF6ED`, when the land point is
inside the influence and `ValidToShakeFromHand` (slot `0x710`) gives 1. A
match calls `PHandFX::DoRemoveFromHandVisual` (`0x0068CE90`),
`fn_005CE420(0)` and `fn_005D4350`. That sends packet `0x4D` with no velocity
and packet `0x1D`. The handler `FUN_005DA8F0` calls
`SpellSeed::ThrowObjectFromHand(status, 1)` `0x0072ACD0`: a seed from a
Worship Site gives its chants back to the site, and each seed is deleted. So
a one-off miracle that is shaken out of the Hand is lost.

**The refusal.** `fn_005D18F0` (the held object and a `MapCoords*`, `ret 8`)
plays sample `0x25` and sends packet `0x2B`, which makes the yellow cross at
the land point. Its callers for a miracle:

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

**The Immersion effects** have names in the game's own enum
(`IMMERSION_EFFECT_TYPE` of `Enum.h`). Those of a miracle: 3 a gesture is
known, 8 the trail of a gesture, 9 a miracle crackles, 14 a one-off seed
pops, 18 to 20 a fireball, 21 lightning, 23 a miracle is cast, 24 a shield,
25 water, 26 and 27 a flock, 28 a Creature miracle. `DoPostCastThings`
`0x00729260` starts the effect of the magic info (`+0x14`) when the spell
starts.

### Why his session failed

From his log (`bwvr.log` of 2026-10-04, 18:00 to 18:17, 72607 frames).

1. **Frames 28260 to 62055: the seed was an object.** The seed of the reward
   (`0C66F4E8`, later `0C66F168`) was the target of the grip. Each press
   was longer than 225 ms, so the game picked the seed up (state 3, then
   state 4 with hand mode `0x18`), and the grip carried it. The card said
   "Strength Creature Miracle" and "[Grip] Open to drop". Nothing said that
   a short press makes the seed active. He pressed B three times (frames
   61233 to 61972): the game asked for the scribble and the square spiral,
   and no draw of 0 to 5 cm fitted.
2. **Frame 62447: a short press.** The grip was down for 14 frames, 155 ms.
   The game's rule made it a tap: effect 14 at frame 62461, and the
   `SpellSeed` `0C737B24` was in the Hand, state 8 and hand mode 5.
3. **From frame 62462: the Hand left the real hand.** The hooks of the mod
   acted only in the states 1, 4 and 7. The report window after the tap has
   "0 depths, 0 poses, 0 rays". So the game placed the Hand on the land
   under the pointer: 71.7 units from the camera, with the real grip 18.8
   units from the eye. That is the "potion on the terrain". The mod still
   drew its own hand at the real hand.
4. **Frames 63137 to 71501: 116 presses, no cast.** With an object in the
   Hand the mod left the pick record to the game's pick at the pixel. The
   record held no object at 100 presses, a tree at 15 and a vortex at 1. It
   never held the Creature. A Creature miracle with no Creature in the
   record goes to `FUN_005D1850`, and the land point is not valid for it:
   the yellow cross and the sound. No Immersion effect of a cast started in
   the whole log.
5. **No cancel.** While the controller owns the pointer, the mod drops the
   game's samples of the pointer (M6). So a shake never came to the game's
   test, and no button of the mod dropped a miracle.

The desktop run mirH shows the same with `miracle_hand=0`. The Hand of the
game was 22.2 cm of the room from the point of the real hand. The game
refused each of the 4 presses. At each one the pick record held the Temple
(`CitadelHeart`), the object behind the pixel of the Hand, and never the
Creature. A shake and a press of B did nothing. The miracle was in the Hand
to the end of the run, so the seed of the second test could not be used.

### What the mod does (`miracle_hand=1`)

**The Hand is the real hand.** `DirectState` of `hand_direct.cpp` now has
the Grain state. The Grain state runs the update of the Holding state, so
the hooks of the ray, the distance and the pose builder place it with no
other change. The land point of the pick record is the land under the Hand,
as in the Holding state. So the influence, the pour and a cast at a place
follow the real hand. Runs mirB to mirK: the Hand of the game is 0.0 cm from
the point of the real hand in state 8.

**The target.** `MiracleSelect` runs at the end of the first eye pass. A
candidate is an object that the game casts this miracle on:
`ValidAsInterfaceTarget` and `ValidToApplyThisToObject`. The body is a ball
of the 2D radius of the object, about the body bone of a Creature. The
target is (`miracle_math::AimAt`, `Better`):

1. The body within `miracle_touch_cm` (3 cm of the room) of the Hand's
   point. Of two, the nearer one.
2. Else the body within `miracle_cone_deg` (6 degrees) of the aim ray, and
   `miracle_max_m` (4 m of the room) from its start. Of two, the one nearer
   to the ray.

**The candidates.** The Creature of each player comes from the game
(`GPlayer+0xA4C`, 8 players), with its body from `BodyPlace`. Each other
candidate is an object that the draw offered: near the Hand, or in the aim
cone, which the draw offers while a miracle is in the Hand
(`MiracleWantsCone`). The draw alone was not enough for a Creature. It
offers an object in the aim only when the point of its mesh, the feet of a
Creature, is in the cone of the far grab. The body of a Creature is 7 units
and more above its feet. So a Creature 50 cm of the room from the Hand had
the aim on its body, and the draw did not offer it (runs mirO and mirP: the
target went after 5 or 6 frames, "the draw did not offer").

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

**The grip casts**, as the Action button does in the game. The grip is the
game's button one to one while a miracle is in the Hand ("The grip holds").

| Kind of miracle | What the player does | What the game does |
| --- | --- | --- |
| A Creature miracle | Close the grip with the halo on a Creature | `fn_005D30D0`: the cast on the object |
| Heal, Forest, Teleport, Explosion | Close the grip with the Hand over the place | `FUN_005D3340`: the cast at the land under the Hand |
| Food, Wood, Water, Lightning | Keep the grip closed over the place | Action 10: a cast each game turn. The opening ends the pour |
| Fireball, the flocks | Close the grip, swing the hand, open the grip | Action 8: the release sends the speed of the Hand |
| Storm, the two shields | Close the grip, draw a circle with the hand, open the grip | The game matches the circle, and the release casts at its place |

**The throw** uses the game's own speed of the Hand: the Grain state filters
the motion of the Hand, which is now the motion of the real hand. The mod
does not change it.

**The circle.** The mod feeds the ring of the gestures while the grip is
closed with a miracle that needs a circle, and the interface action is 8 or
9. The path of the hand goes to the ring as it does for the draw button of M6
(`MiracleDrawsCircle`, `ControllersGestureInput`).

**The way out.** Two inputs take a miracle out of the Hand:

- A shake of the real hand: the detector of the leash
  (`command_math::ShakeStep`, 4 strokes of 6 cm at `miracle_shake_mps`,
  0.6 m/s).
- A short press of the second face button of the pointing hand (B, or Y for
  a left handed player): at most `miracle_cancel_ms` (400 ms) and 5 cm of
  hand motion. A longer press, or a press with motion, is the draw of a
  gesture as before.

The cancel runs before the next interface step (`MiracleInterfaceStep`). It
does what the game does after a match of the scribble: the visual of the
Hand, `fn_005CE420(0)` and `fn_005D4350`. The mod makes no test of the
influence, so the way out works at each place. The game's rule stays: a
one-off miracle that is cancelled is lost, and a miracle of a Worship Site
gives its power back.

**A seed that the Hand carries.** The grip still picks a one-off seed up as
an object, so a player can move a seed. A press shorter than 225 ms still
makes it active at once, by the rule of the game. While the Hand carries the
seed, the trigger makes it active (`miracle_seed_use=1`):

1. The trigger does not reach the game. The grip looks open to the grip
   machine, so the Hand lets the seed go (the click of "The grip holds").
2. One frame after the Hand is empty, the mod calls the game's tap
   (`fn_005D38A0`, packet `0x20`).
3. The miracle is in the Hand. The grip stays up for the game until the real
   grip opens, so a closed grip does not cast by accident.

The game needs the land under the Hand inside the influence for the let go
and for the tap. `miracle_math::UseStep` ends the use after 120 frames with
no miracle, and the card says "Use it inside your influence".

**The card** (`MiracleHint`, the context "a miracle in the Hand"). Its lines
stay while the miracle or the seed is in the Hand. The hints of the game for
a button go, and one line of its text stays (the name of the miracle).

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

**Haptics.** A new kind `cast` (0.45 for 30 ms) on the game's effects of a
cast (18 to 21 and 23 to 28). `activate` when a one-off seed pops (effect
14). `refused` once for each press that the game refuses. `let go` at a
cancel, `target` for a new target.

### The crash of a sound with no Base

The first cancel of a desktop run ended the game (run mirB, `0xC0000409` in
the DLL of the mod, `___report_gsfailure`). `PHandFX::DoRemoveFromHandVisual`
plays sample `0x77` with the `PHandFX` of the Hand as the object of the
sound. The audio code asked each object of a sound for its place through
slot `0x10` of its vtable, `Base::Get3DSoundPos` (one argument, `ret 4`).
`PHandFX` derives from `PSysHandFX` only. Its slot `0x10` is
`PHandFX::DrawHandFX`, with no argument and a plain `ret`. The call left 4
bytes on the stack, and the stack check of the caller ended the game. The
game's own shake of a miracle would have ended it in the same way.

`audio_object.h` (`AudioVtableIsBase`) reads the RTTI of the vtable: the
locator at `vt - 4`, the hierarchy at `+0x10`, and the names of the base
classes. `audio_sources.cpp` and `audio_trace.cpp` call slot `0x10` only for
a class that is `Base` or derives from it. On the exe 553 of the 949 classes
with RTTI derive from `Base`.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `miracle_hand` | 1 | 0: the builds to 2026-10-04. The game places the Hand with a miracle, and the mod gives no target, no cancel, no use of a seed and no lines |
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

### The log

The tag `miracle`, one line for each change, 600 lines:

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

### Tests

**The math test** has 75 new checks (`miracle_checks.h`), 2310 in all: the
lines of the card for each kind of miracle, each reason of a refusal in the
order of the game, the target at the Hand and in the aim (the gap, the
angle, the range, no ray, and the order), the target that stays, the cancel
button, the steps of the use of a seed, and the shake of the desktop runs (5
steps of 15 cm).

**Desktop runs** (`miracle_run.ps1` in the scratchpad, 2026-10-04, the God
view at 44 units a meter, a render of 2880 by 2880, about 45 frames a
second). The start is "Keep your old Creature": no story, and the Creature
at the door of the Temple. `miracle_test_seeds` makes the seeds. A press of
3 frames on a seed is the tap of the game.

- **mirH, the fault** (`miracle_hand=0`, the same DLL): see "Why his session
  failed".
- **mirB**: the crash of the first cancel, see above.
- **mirC, a Creature miracle.** The Hand of the game 0.0 cm from the real
  hand point in state 8. A press over the land: "the game refused the cast
  of Creature Strong ...: this miracle needs a Creature as its target", one
  line and one pulse. The aim on the Creature (36.8 cm of the room from the
  Hand, 1.3 degrees): "the game casts Creature Strong on the Creature
  2AA0A674 (packets 0x4D and 0x11), the target with the halo", effect 28
  four frames later, "the spell ... runs", and 174 frames later "left the
  Hand (when its spell ended). The Hand is free". A second seed with the
  Hand's point in the Creature (0.0 cm): the same cast.
- **mirD, the way out.** A shake (5 steps of 15 cm, 6 frames apart: "4
  strokes in 442 ms, the slowest at 1.07 m/s"): the miracle left the Hand 2
  frames later, state 1. A press of B for 5 frames: out of the Hand 4 frames
  later. A shake while the spell of a cast Creature miracle ran: out of the
  Hand 3 frames later. The game went on each time.
- **mirE and mirJ, a throw and a pour.** Fireball: the grip closed (action
  8), a step of 15 cm of the hand, the grip opened: "the game casts Fireball
  at the land point ... The Hand moves at -11.3 7.1 6.0 units a second
  (0.33 m/s of the room), interface action 8", effect 19, and the Hand free.
  Water: the grip closed for 90 frames: "the game casts Water at the land
  point ..., interface action 10", effect 25, "the pour ends: the game sent
  19 casts in 91 frames". The water stayed in the Hand, and a press of B
  took it out.
- **mirF, a circle.** Spiritual Shield and Storm: the grip closed while the
  fake hand stepped through the circle of `Gestures.jty` (template 5, the
  word `trace`): "the draw starts", "the game matched a circle (template 5)"
  66 frames later, effect 3, and at the opening "the game casts Spiritual
  Shield at the land point ...", effect 24 (23 for the storm). This is also
  the first run in which the game matched a stroke of M6.
- **mirG and mirI, a place and a carried seed.** Heal: "[Grip] Cast here",
  a press, "the game casts Heal at the land point ...", effect 23, the Hand
  free. A seed that the grip carried: the card "[Trigger] Use the miracle,
  [Grip] Open to drop", the trigger at frame 1501, the let go, the tap at
  frame 1517 and "Creature Strong is active in the Hand" at frame 1521. The
  grip stayed closed to frame 1660 and no press went to the game. The next
  close of the grip pressed (frame 1741). Run mirG found a fault here: the
  mask of the grip did not end, and that press did not come. It is
  corrected.
- **mirK, the halo.** The aim on the Creature 146.1 cm of the room from the
  Hand, 0.6 degrees off the ray: "a halo of the target of a miracle is drawn
  ... radius 2.5 units, 38 px", gold in the picture of the eye, and the card
  "[Grip] Cast on the Creature, [B] Tap to cancel, or shake".
- **mirL, mirM and mirN** (a clean build): the seed made active, a refusal
  with its reason, the cancel by a shake and by the button, Heal at a place,
  and the carried seed with the trigger, each as in the runs before. The
  Creature of the game walks, and the test words `aimfar` and `touchc` found
  no Creature in reach in these three runs.
- **mirO and mirP, the target that went.** The aim on the Creature at frame
  963 (47.7 and 49.3 cm of the room from the Hand, 1.6 and 2.6 degrees). The
  target went 6 and 5 frames later, and the press was refused. The log of
  mirP has the cause: "has no target now: the draw did not offer 2A63A674 to
  the pick in this frame". In run mirC the Creature was 36.8 cm away, inside
  the ball that the draw offers around the Hand. In run mirK it was 146 cm
  away, where its feet are in the cone.
- **mirQ, the last build**, with the Creatures from the game and the target
  that stays: the same aim (51.0 cm, 2.3 degrees) at frame 963, a press 47
  frames later, and "the game casts Creature Strong on the Creature 2AA0D674
  (packets 0x4D and 0x11), the target with the halo", the spell, and the
  Hand free 175 frames later. Then a refusal and a cancel by a shake.

**Not run on the desktop:**

- A seed from a Worship Site did not run here. It ran later, with the
  chain of gestures, a power up and the power that goes back at a cancel:
  see "The village with the real hand".
- Food, Wood and Lightning (the pour of Water), Forest, Teleport and
  Explosion (the cast of Heal), the flocks (the throw of the Fireball), and
  the Creature miracles other than Strong.
- A Creature outside the influence (the red halo and its line), each reason
  of a refusal other than "needs a Creature", a Worship Site or a dispenser
  as the target, and a magic fireball.
- The line of the card with `miracle_seed_use=0`, and each key at a value
  that is not its default, other than `miracle_hand`.
- A left handed player, `controller_swap_buttons=1`, and a multiplayer game.

**Needs the headset.** Whether a real shake of 0.6 m/s takes the miracle out
and a normal move does not. Whether 400 ms is the right limit of the tap of
B. Whether the speed of a real swing throws a fireball as far as the player
means: the runs had only a step of the hand. Whether a circle of the real
hand over the land fits the circle of the game. Whether 3 cm and 6 degrees
find the Creature with no effort. Whether the miracle looks right in the
real hand.

**Limits.** A cancel takes a power up off together with the miracle: the
game takes the power up off first. The trigger does not make a carried seed
active outside the influence. The aim finds a target only in the God view
and on the table, where the cone of the far grab is installed.

## Many trees from a forest (2026-10-04)

Buck, after the headset test of the evening of 2026-10-04: "bulk grab in
forests doesn't work last time I tested, I could only grab one tree at the
time which could be an issue." His rule for a bulk take, from 2026-10-03:
"holding the grip collects it (even over distance) and letting go of the
grip stops the collection, then pressing the grip again drops it".

### What the game does [V]

The plain game has no take of many trees. The disassembly shows it in four
places.

- **The Hand holds one object.** `GMagicHand` (`0x1C` bytes, the vtable at
  `0x0092BFC0`) has a count at `+0x14` and one object at `+0x18`. Its add
  (`0x005FAFC0`) refuses when the count is 1. `GInterfaceStatus` has one
  hand in use at most (`+0x90`, `0x005DC2A0`), and
  `GInterfaceStatus::IsSpaceInHands` `0x005DC2F0` says no when that hand has
  its object. `GInterface::PlaceObjectInMagicHand` `0x005DA6F0` tests it
  first, and a refusal resets the interface (`fn_005D1260`).
- **A tree and a forest are no bulk sources.** `ValidForLockedSelectProcess`
  (slot `0x6CC`) is 0 for a `Tree` and for a `BigForest`, and
  `GameThing::RemoveResource` (slot `0xA0`) of a `Tree` gives 0. A press on a
  `Tree` starts the Tug and puts that tree in the Hand. A press on a
  `BigForest` makes one new `Tree` (see "A forest gives a tree").
- **A press with a tree in the Hand takes no second tree.** The press goes to
  `fn_005D1560`. It applies the held object to the object under the Hand
  only when `Tree::ValidToApplyThisToObject` `0x0074BD50` says yes. That is
  for a store of wood (`IsResourceStore(1)`, slot `0x680`) and for one more
  class (the cast at `0x0074BD64`). A second tree is neither, so the press
  is the drop or the throw.
- **The hand state MultiPickUp is never the state of the Hand.** The state
  object exists (`CHand+0x4894`, made at `0x0046BD8C`, vtable `0x00900B6C`).
  `CHand::GetRequiredState` `0x0046CD10` has no path that gives 6. Its
  switch on the hand mode (the table at `0x0046CF48`, the index bytes at
  `0x0046CF5C`) gives 0, 5, 7 or 1, and 2 or 1. The paths before the switch
  give 10, 0, 8, 3, 4 and 9. The filter `fn_0046CF80` gives 6 for no input,
  and no other code writes the state (`+0x4878`). "Multi pickup" is the
  game's word for the bulk take of a pile (`PSysGlobal::StartMultiPickup`,
  `GPotInfo::GetAmountToPickUpDuringMultiPickup`). During that take the Hand
  is in the Holding state (`0x0046CD5D` to `0x0046CD87`).

So with the mouse a player takes one tree, sets it down, and takes the next
one. His logs of 2026-10-04 agree. In the session of 10:59 the Hand let go
of a tree 8 times, and the far grab did 24 fetches. In the session of 18:00
the Hand let go of a tree 1 time, and the far grab did 12 fetches. Each take
gave one object, and no fetch was from a `BigForest`.

The wood of a tree is `Tree::GetWoodValue` `0x0074B7B0`: the scale times the
life times the wood of its `GTreeInfo` (`+0x6C`). A store gets that amount
when the player drops the tree on it (`Tree::ApplyThisToObject`
`0x0074BDA0` calls slot `0x684`, `MultiMapFixed::DeleteObjectAndTakeResource`,
and that calls `Object::DoDeleteObjectAndTakeResource` `0x0063A940`). The
runs saw 599, 500, 361, 353 and 346 for the trees near the Temple of Land 1,
and 24 to 27 for a bush.

### What the mod adds (`forest_take=1`)

The take from a forest is new behavior. The plain game does not have it. It
uses only objects and functions of the game.

For the player:

1. Close the grip on a tree. The tree is in the Hand, as before.
2. Keep the grip closed. The take starts after 600 ms, when a second tree is
   within 10 cm of the room of the Hand's point. The tree in the Hand and
   that tree become one bundle of wood. Then one more tree goes in the
   bundle each 400 ms, while a tree is in reach. Move the Hand through the
   forest to get more. The card shows the amount ("Amount: N", the game's
   own text) and "Open to stop (N trees)".
3. Open the grip. The take stops, and the bundle stays in the Hand.
4. Close and open the grip again. The Hand lets the bundle go, as it does
   for a bundle from a store. The game gives the wood to a store or to a
   pile of wood near that place. With no store and no pile there, it makes a
   pile of wood on the land.

A grip that opens before the take starts lets the one tree go, as before. A
tree that the player lifts out of the forest stays one whole tree: when no
second tree is in reach for 300 ms, the take is off for that tree. So a tree
that the player carries to a different forest, to plant it there, starts no
take.

**At a distance.** A tree that came with a fetch (the pull of the far grab)
starts the same wait. The second tree is then a tree in the cone of the aim
(`far_grab_cone_deg`, with the radius of the tree counted), in the range of
the far grab and not behind a hill. Keep the grip closed and the aim on the
forest to collect. Turn the aim away for 300 ms to keep the one tree.

**A `BigForest`** gives the wood of one of its trees at each step
(`BigForest::RemoveResource`, slot `0xA0`, with the amount that
`BigForest::InterfaceSetInMagicHand` takes for the tree that it makes,
`0x00DA3F04`). The game deletes the forest when its wood is gone.

**How** (`forest_take.cpp`, the rule in `forest_take_math.h`):

- The bundle is the game's own object for wood in the Hand: the `PileWood`
  that a take from the wood of a store makes. `Pot::Create` `0x0066CF10`
  makes it with pot info 11 (`PileWood`, `0x0066EC40`), at the place of the
  Hand (`GInterfaceStatus+0x14`), for the nearest town
  (`MapCoords::GetNearestTown` `0x006020E0`), as the Creature's own pick up
  does (`0x004FFF97`).
- The start changes the object in the Hand. The mod makes the bundle with
  the wood of the tree in the Hand. The tree leaves the hand list
  (`0x005DC470`) and the `CHand` (`CHand::ThrowObject` `0x0046DDD0`), and
  the game deletes it as a store does (`GoolooGooloo` `0x005E6540`,
  `ToBeDeleted`). Then `GInterface::PlaceObjectInMagicHand` puts the bundle
  in the Hand. In the runs the Hand stayed in the Holding state through the
  start: the log has no change of the state.
- A tree goes into the bundle with `Object::DoDeleteObjectAndTakeResource`,
  with the bundle as the taker. So the bundle gets the wood that a store
  gets, the game plays its sound of wood, and the tree is deleted.
- The let go is the game's own [V]. `Pot::InitialisePhysicsFromHand`
  `0x0066DF00` calls `fn_0066F270`. That function looks at the 9 map cells
  around the place (`GUtils::Spiral`). An object there that is a store of
  wood (slot `0x680`), or a pot of wood (slots `0x4B4` and `0x690`), gets the
  wood with `AddResource` when the place is in its radius. For the wood that
  is left, and not on water, `fn_005FA8B0` makes a `MagicWood` (the vtable at
  `0x0092F7A4`): the game's pile of wood outside a store. Then the game
  deletes the bundle of the Hand.
- All of this runs before the interface step of the game
  (`h_InterfaceStep`), where the game itself puts objects in the Hand. The
  next tree is chosen at the end of the first eye pass, from the objects
  that the draw offered, and the step tests that it still lives. Never in a
  multiplayer game: the calls are not packets.
- The grip machine gets the fact `mod_take` (`GripStep`). While the take
  runs, the closed grip presses no button of the game, the opening latches
  no throw and sends no click, and the bundle is then in the Hand with the
  grip open (OpenHeld). The next close carries it, and the next opening lets
  it go.

The mouse is as before: the take runs only while the controller places the
Hand.

### Each state of the Hand, and what the mod does in it

The state is `CHand+0x4878`, and the state objects are at `CHand+0x487C`
(made in the constructor, `0x0046BC99` to `0x0046BE46`) [V]. The hooks that
place the Hand act in the states 1, 4, 7 and 8 (`DirectState` and `RayState`
in `hand_direct.cpp`). Until 2026-10-04 the state 8 was not in that list.

| State | When the game has it [V] | The Hand's place | The grip | Known gaps |
| --- | --- | --- | --- | --- |
| 0 Invisible | Hand mode 1, 2, `0xC` or `0x19`: a dialog box, a menu, a story scene | The game's. The hooks do not act | The button, but `Usable` is false in most of these | None known |
| 1 Normal | The Hand is empty | The real hand (the position hooks) | The button. The pick is the mod's (one target) | None known |
| 2 Camera | The Hand is empty and the left button holds the land (`CameraModeNew3+0x8C` bit 0) | The game's place on the land. The drawn Hand is exact | The button | In the God view the land moves with the grip of the other hand, so the state is rare there |
| 3 Tug | An object is in the Hand and the Tug flag `+0x4908` is set: the pull of a tree or a rock, and the leash | The game's place on the line of sight. The drawn Hand is exact | The button, held while the grip is closed | It lasts 6 to 10 frames for a tree in the runs |
| 4 Holding | An object is in the Hand. Also during the bulk take of a pile | The real hand | The grip machine: carry, let go, and the take rules | The pick record at the opening of the grip is the thing at the Hand that takes the object (`hold_target=1`, "An object in the Hand that goes onto a thing"). With no such thing it is the object behind the pixel of the Hand's point |
| 5 Totem | The Hand is empty and the hand mode is `0xE`: the Hand moves the totem of the Village Centre (the locked select of the game) | The game's: `HandStateTotem::Update` `0x005B77A0` puts the Hand on the top of the pole. The mesh of that Hand is hidden, and the mod draws its hands at the real hands | The button, held while the grip is closed. The pole follows the real hand one to one (`totem_hand=1`) | See "The village with the real hand". The Hand of the game is some cm from the real hand in this state |
| 6 MultiPickUp | Never (see above) | Not used | Not used | None: a player cannot meet it |
| 7 Creature | The Hand is empty, the hand mode is `0x13`, and the Creature is under the Hand | The real hand (the depth hook and the ray) | The button: stroke and slap | See "The Creature by touch" |
| 8 Grain | A miracle (a `SpellSeed`) is in the Hand | The real hand, with `miracle_hand=1` | The button, with the rules of `miracle.cpp` | See "A miracle in the Hand" |
| 9 PlayAnim | The Hand is empty and the byte `+0x491C` is set: a script plays an animation of the Hand | The game's. The drawn Hand is exact | The button | Not seen in a session of Buck |
| 10 Citadel | The game draws from the camera of the Temple (`GGame+0x205A28` is 1) | The game's | The button | The package "controls" must prove the Temple in a run |

### Keys

| Key | Default | Meaning |
| --- | --- | --- |
| `forest_take` | 0 | 1 lets the closed grip collect the trees of a forest in one bundle. 0 gives one tree for each grip, as in the plain game. The default was 1 until 2026-10-07: the plain game has no take of many trees, and the owner wants its behavior |
| `forest_take_wait_ms` | 600 | How long the grip stays closed on the first tree before the take starts (100 to 5000) |
| `forest_take_ms` | 400 | The time from one tree to the next (50 to 5000) |
| `forest_take_cm` | 10 | The reach of the take from the Hand's point to the body of a tree, in cm of the room (1 to 100) |

With `test_keys=1`, Ctrl with the 9 key of the number pad turns the take off
and on while the game runs. The pointing controller gives a long pulse for
on and a short pulse for off. The keys are not in the launcher.

Test keys for the desktop runs: `far_test_tree=1` makes only a tree a far
target, and the word `aimfar` then aims at the nearest tree.

### The log

Tag `forest take`: the rule at the start. "a tree is in the closed grip",
with how it came (at the Hand, or with a fetch). "no second tree was in
reach for 300 ms, so the tree in the Hand stays one tree". "the take
starts", with the tree, its wood and the bundle. "tree N goes in" for the
first six trees and then each tenth, with the distance or the angle and the
wood. "the take ends", with the reason, the count and the wood. "the bundle
left the Hand", with what the game did with it, and the nearest pile of wood
on the land. Each report window: the takes, the trees, the starts that the
game refused. Tag `grip`: "the grip opens during a take from a forest: the
take stops, and the bundle stays in the Hand".

### Tests

The math test has 39 new checks (`forest_checks.h`): the wait,
the start with two trees, the rate, a take with no tree in reach, the
opening that ends the take, no new take on the next close, a grip that opens
before the wait is over, a tree that leaves the forest, a gap in the reach,
a tree with no forest, an object that is no tree, a bundle that leaves the
Hand, a start that the game refused, the gap from the Hand's point to the
body of a tree, the angle from the aim, and the grip machine during the take
(no button, no end of a take of the game, the bundle kept at the opening,
the carry and the let go after it).

Desktop runs of 2026-10-04, `spawn_run.ps1 -Choice keep`, God view, at the
Temple of Land 1, with `fake_controller_front=0` (the game window was behind
in each run). The word `seek` with `seek_plan=crown` puts the Hand's point
in the crown of the nearest tree.

- **inF1** (a step of 250 ms, the default of that build): `seek`, a grip of
  400 frames from 1000, a grip of 30 frames from 1500. The tree `2AA35D74`
  (599 wood) is in the Hand at 1003 to 1011. The take starts at 1030: "The
  tree left the hand list 1, and the bundle is in the Hand". Tree 2 (3.4 cm,
  500 wood) and tree 3 (9.8 cm, a bush of 27 wood) go in: 1126 wood. The grip
  opens at 1400, and the take ends with the bundle in the Hand. The grip of
  1500 carries it, and its opening at 1530 lets it go: the bundle left the
  Hand 8 frames after the release. The Hand stayed in the state 4 from the
  tree to the let go.
- **inF2**: four more `seek` words while the grip is closed, so the Hand
  goes from crown to crown. 13 trees, 3964 wood. The opening kept the
  bundle. The let go at 1631: "the bundle left the Hand, and the game
  deleted it in 102 ms".
- **inF3** (`seek_forest=1`, four steps of the zoom out, 506 units in a
  meter of the room): the `BigForest` `286C34B4` gives a new tree (350 wood)
  at 1074. The take starts at 1103, and the forest gives 350 wood at each
  step: 24 trees, 8400 wood at 1360. The opening kept the bundle, and the
  let go was at 1531.
- **inF4** (`far_test_tree=1`, `aimfar`, a grip, `pull`): the tree "came
  with a fetch from a distance" at 1105. The take starts at 1132 and takes 8
  trees from the cone of the aim (0.0 to 1.1 degrees): 2787 wood. The
  opening at 1460 kept the bundle. The let go at 1631, and the game deleted
  the bundle 109 ms later.
- **inF5** (the defaults, 600 ms and 400 ms): the tree `2AA3AD74` is in the
  closed grip at 1002. The take starts at 1029 with tree 2 (3.4 cm, 500
  wood), and tree 3 (9.8 cm, 27 wood) goes in at 1048: 1126 wood. The card:
  "[Grip] Pick Up", "[Grip] Plant" (the game's hint for a tree in the Hand),
  "Amount: 1099", "Amount: 1126", and "Amount: 1126, [Grip] Open to stop (3
  trees)" at 1057. The opening at 1301: "the take ends: the grip opened. 3
  trees went into the bundle, and it holds 1126 wood", and the card has
  "[Grip] Drop". The grip of 1401 to 1431 lets the bundle go, 8 frames after
  the release. Then a grip of 20 frames on a second tree (`2AA39804`, 1603
  to 1621): it opens before the 600 ms are over, no take starts, and the
  Hand lets the one tree go.
- **inF6** (`forest_take=0`): the same `seek`, and a grip of 300 frames. One
  tree is in the Hand from 1003 to 1301, no take starts, and the opening
  lets the tree go. The log has "forest_take=0. A grip on a tree gives one
  tree, as in the plain game".
- **inF7** (the build after the rebase on the ring, the defaults): the same
  as inF5. The take starts at 1030, 3 trees and 1126 wood, the opening at
  1301 keeps the bundle, and the grip of 1401 to 1431 lets it go 5 frames
  after the release. Then: "the bundle 0F698C58 left the Hand, and the game
  deleted it in 101 ms", and "the nearest pile of wood on the land is
  0F6A7FB4 (MagicWood), with 1126 wood, 14.1 cm of the room from the Hand's
  point, frame 1443". So all the wood of the 3 trees is on the land in one
  pile of the game.
- **inF8** (the build after the rebase, `far_test_tree=1`): the same as
  inF4. The tree came with a fetch at 1104, the take starts at 1132, and 8
  trees go in from 0.0 to 1.1 degrees from the aim: 2787 wood. The opening
  at 1461 keeps the bundle, and the let go is at 1631. The Hand was in the
  air then and not at the land, and the draw offered no pile of wood near
  the Hand in the 60 frames after: where that wood went is not proven.
- **inF9** (the build after the rebase on the hand model, the build that
  is on main): the same as inF7. The take starts at 1029, 3 trees and 1126
  wood, the opening at 1301 keeps the bundle, and the let go is at 1431.
  "the nearest pile of wood on the land is 0F71AFB4 (MagicWood), with 1126
  wood, 14.1 cm of the room from the Hand's point, frame 1447". The math
  test of that build has 2540 checks, and none failed.

In each run the real cursor was at 1390 627 before and after, and a window
of a different program was in front.

**Not proven on the desktop:** a throw of the bundle with a swing, a bundle
that the Hand lets go over a store or from high in the air (inF8), the
sound, a forest outside the influence, and a game that the player saves
with the bundle in the Hand.

**Known limits.** The hint "Hold to collect" of the wait almost never
shows: the game has its own hint for a tree in the Hand ("[Grip] Plant"),
and the card shows that one. A `BigForest` gives the wood of one tree at
each step with no tree that leaves the picture, until the game deletes the
forest. The take changes the tree in the Hand into wood, so a player who
wants one whole tree must lift it away from the other trees in the first
600 ms, or set `forest_take=0`.

**Needs the headset.** Whether 600 ms and 400 ms feel right, and whether 10
cm is the right reach at the scales that Buck plays at. Whether one whole
tree is still easy to take from a forest. Whether the take at a distance
starts when he does not want it, after a fetch of one tree.

## The model of the Hand (2026-10-04)

Buck, 2026-10-04: "The next task would then be the hand models as our own
models as that is a huge part of the charm of the game. If you could rip the
model as an fbx so I can add some more polygons it would be useful."

This section records the files of the game's Hand, how the game poses the
Hand and changes its look, and the export tool. The reader is
`tools/hand_export/bw_hand.py`. Each fact with [V] is from the disassembly or
from the bytes of the files. The pose math is also checked against the bones
that run ex2 measured in the game ("The exact Hand", "The rest turn of the
mesh").

### The files of the Hand [V]

| File | What it holds |
| --- | --- |
| `Data\CreatureMesh\Hand_Boned_Base2.l3d` | The base mesh, the bones and the base texture |
| `Data\CreatureMesh\Hand_Boned_Good2.l3d` | The good mesh and the good texture |
| `Data\CreatureMesh\Hand_Boned_Evil2.l3d` | The evil mesh and the evil texture |
| `Data\CTR\hh.HBN` | The names of the three meshes and the 26 animations |
| `Data\hndspec5.txt` | The 69 animation names of the table in `hh.HBN` |

`Data\HandDemo\*.hnd` (16 files) are not part of the model. They were not
read.

**The L3D file** (138,158 bytes, the three have the same layout):

| Offset | What |
| --- | --- |
| `0x00` | `L3D0`, the flags `0x00022316`, 7,082 bytes of mesh data, 1 submesh, the submesh table at `0x4C` |
| `0x34` | After 32 bytes of 0: `0xFFFFFFFF`, 1 texture, the texture table at `0x1BA6`, 0 points |
| `0x50` | The submesh: the flags `0xE0000801`, 1 primitive, the primitive table at `0x58C`, 22 bones at `0x64` |
| `0x64` | 22 bones of 60 bytes |
| `0x590` | The primitive: 12 words. The material `4` and `0x5F650400` (`0x5F450400` in the good and the evil file), the id of the texture, one more word, then 141 vertices at `0x5C0`, 169 triangles at `0x1760`, 20 bone groups at `0x1B56` and 0 vertex blends |
| `0x5C0` | 141 vertices of 32 bytes |
| `0x1760` | 169 triangles of three 16 bit indices |
| `0x1B56` | 20 bone groups of 4 bytes |
| `0x1BAA` | The texture: its id, then 256 by 256 texels of 16 bits |

- **A vertex** is a position, a texture point and a normal (8 floats). The
  position and the normal are in the frame of the bone of the vertex.
- **A bone** is the parent, the first child and the next bone (three ints,
  -1 for none), then a matrix of 4 rows of 3 in the frame of the parent. A
  point is a row: `point * matrix`. A bone in the frame of the mesh is
  `bone * parent`. The wrist matrix holds a mirror, so each bone in the frame
  of the mesh has the determinant -1. The length of a bone is along its own
  z, and a finger bends about its own x.
- **A bone group** is a count of vertices and a bone (two 16 bit words). The
  groups cover the vertices in order. So each vertex has one bone, and there
  are no weights.
- **A texel** has 4 bits each for alpha, red, green and blue. The alpha is 1
  on the hand and goes to 0 at the wrist (9,758 texels are below 1), so the
  wrist fades.
- 141 vertices are at 88 places: a seam of the texture has two vertices at
  one place. Two vertices at one place always have the same bone.
- Each triangle has the same turn: `(v1 - v0) x (v2 - v0)` is along the
  normals in the stored frame.
- The texture points are the same in the three files. The bones are the
  same. The good mesh has the vertices and the normals of the base mesh. The
  evil mesh moves a vertex by up to 47.9 units (14.6 mm on a hand of 19 cm):
  longer fingers with a point.

**The bones.** The names are ours: the game has none.

| Bone | Parent | Vertices | Bone | Parent | Vertices |
| --- | --- | --- | --- | --- | --- |
| 0 wrist | | 17 | 11 little 1 | 6 | 5 |
| 1 attach | 0 | 0 | 12 little 2 | 11 | 5 |
| 2 palm | 0 | 0 | 13 little 3 | 12 | 7 |
| 3 thumb 1 | 2 | 3 | 14 ring 1 | 5 | 5 |
| 4 index 0 | 2 | 8 | 15 ring 2 | 14 | 5 |
| 5 ring 0 | 2 | 6 | 16 ring 3 | 15 | 7 |
| 6 little 0 | 2 | 10 | 17 index 1 | 4 | 5 |
| 7 middle 0 | 2 | 6 | 18 index 2 | 17 | 6 |
| 8 middle 1 | 7 | 5 | 19 index 3 | 18 | 9 |
| 9 middle 2 | 8 | 5 | 20 thumb 2 | 3 | 13 |
| 10 middle 3 | 9 | 7 | 21 thumb 3 | 20 | 7 |

Bone 0 of a finger is in the palm, from the root of the fingers to the
knuckle. Bone 1 (attach) has no vertices. Only the animations `horn` and
`phile` move it, so it is the place of an object that those poses hold.

**The HBN file** (21,680 bytes). `LiOnHeAd`, the segment name `Hand` in 32
bytes and the segment size, then the data at `0x2C`. Each offset in the data
counts from `0x2C`.

1. `CHand::LoadBinary` `0x0046D2C0` reads the hand version (0) and the data
   version (5). The data version names the spec file, `Data\hndspec5.txt`.
2. `Morphable::ReadBinary` `0x00617AE0` reads its version (6), the name of
   the base mesh in 32 bytes, and six more names of 32 bytes. The first is
   the evil mesh (mesh 2) and the second the good mesh (mesh 3). The other
   four are empty: only a Creature has them.
3. For each mesh with a name, `LH3DAnimSet::ReadBinaryNew` `0x00619B80`
   reads one offset for each of the 69 animations of the spec file (0 for
   none), and one more offset: the end of the animations of the set. The
   base set has 26 animations. The evil set and the good set have none, and
   the game then copies those of the base set (`Morphable::GetSetAnim`
   `0x00619690`).
4. One word for `+0x4830`, the count of hair groups (0), and the events of
   the animations (`Morphable::ReadExtraDataBinary` `0x00617EE0`).

`Morphable::LoadBase` `0x00618360` makes the base mesh two times: mesh 0 is
the mesh that the game draws and changes, and mesh 1 keeps the base. It also
makes three arrays of one matrix for each bone: the pose `+0x47F0`, the rest
pose in the frame of the mesh `+0x47F4`, and the inverse of the rest pose
`+0x47F8`.

**The spec file.** A line that starts with `=` names a group. Each other
line is a type letter and a name. `C` is a full animation. `L` is a lean.
`E` ends the file.

**An animation** (`CAnim::ReadBinary` `0x00860860`, `CFrame::ReadBinary`
`0x00860C30`):

| What | Type |
| --- | --- |
| The duration | 1 word. 266, 421, 526 or 533, most likely in ms [I] |
| The flags | 1 word. Bit 0: the animation loops |
| Five floats | 0 in each full animation. Eight of the ten leans have values there. Their use was not read |
| The count of frames | 1 word, 2 to 5 |
| The count of bones | 1 word, 22 |
| The count of bones with a turn, and with a place | 2 words |
| The bones with a turn, then the bones with a place | 1 word each |
| Each frame | 3 angles for each bone with a turn, then 3 floats for each bone with a place |

### How the game poses the Hand [V]

`fn_00860E00` (thiscall on the animation) samples an animation into the pose
array. Its arguments are the pose array, the mesh, the rest pose `+0x47F4`,
the inverse rest pose `+0x47F8`, the time, the stand frame, a map of bones
for the mirror, and a flag for the fill.

- **The frame.** `frame = count * time / total`. For an animation that loops,
  `total` is the duration, and the last frame blends back to frame 0. For
  the others `total = duration * count / (count - 1)`, so the last frame is
  at the duration.
- **The turn.** The three angles `(x, y, z)` of a frame make a matrix with
  `LHMatrix::SetYXZMatrixOnly` `0x007FAC10`, called with `(y, x, z)`. The
  game blends the matrices of two frames element by element and makes each
  row one long again (`fn_007FB5C0`). The turn is a turn of the bone from
  its rest pose, about the axes of the mesh:
  `local turn = rest of the bone * turn * inverse rest of the parent`.
- **The place.** The three floats of a frame are the place of the bone in
  the frame of its parent.
- **The fill.** A bone with no entry in the animation takes the turn and the
  place of the stand frame. The stand frame is frame 0 of animation 0. It
  has each bone, and its places are the places of the L3D bones. It is not
  the rest pose: the middle, ring and little fingers bend in it.
- **The lean.** `fn_00861EE0` adds a lean to a pose that is there:
  `local turn = old turn * rest of the parent * inverse(turn of the stand
  frame) * turn of the lean * inverse rest of the parent`, and the place
  moves by the place of the lean minus that of the stand frame. The inverse
  is `fn_007FACC0` with the angles negated [I].

Then `fn_00839F10` composes the bones with the root ("How the game draws the
Hand"), and each vertex goes through the matrix of its bone.

**The animations of the Hand.** The number is the index in the table, and
the mod's logs give it (`Morphable::GetAnim`).

| Number | Name | Type | Number | Name | Type |
| --- | --- | --- | --- | --- | --- |
| 0 | wiggle | C | 27 | tickle | C |
| 1 | wiggle_lr | L | 33 | slap | C |
| 2 | wiggle_fb | L | 34 | slap_lr | L |
| 3 | point | C | 42 | grip | C |
| 6 | hold_above | C | 43 | grip_lr | L |
| 9 | hold_side | C | 44 | grip_fb | L |
| 12 | can_pickup | C | 45 | rotate | C |
| 13 | can_pickup_lr | L | 48 | pitch | C |
| 14 | can_pickup_fb | L | 51 | zoom | C |
| 15 | hold_fingers | C | 57 | tap_house | C |
| 16 | hold_fingers_lr | L | 60 | horn | C |
| 17 | hold_fingers_fb | L | 63 | phile | C |
| 24 | stroke | C | | | |
| 25 | stroke_lr | L | | | |

The other 43 names of the spec file have no animation in the file. Checked
against run ex2: frame 0 of `wiggle` puts the last joint of the middle
finger at `-124.7 -28.4 21.4` (measured in the Normal state: `-124.8 -28.5
21.7`) and keeps the index finger of the file. `grip` puts the wrist at
`-193.7 -487.8 -136.0` (measured in the land drag: `-193.7 -487.6 -136.0`).
`hold_side` puts the wrist at `68.9 -272.7 177.1` in frame 0 and at `85.0
-477.6 197.1` in frame 1 (measured in the Holding state: `69.9 -285.4
178.4`).

### How the game changes the look [V]

`CHand::PrepareForDrawing` writes the player's alignment to `+0x9C`, from -1
(evil) to 1 (good). `Morphable::UpdateMorphing` `0x00618C40` acts when it
moved by 0.03 or more since the last time:

- **The vertices** (`Morphable::MorphVertices` `0x00618D10`). Below 0 the
  other mesh is mesh 2 (evil) and the weight is minus the alignment. From 0
  it is mesh 3 (good) and the weight is the alignment. Each position and
  each normal of mesh 0 becomes `base + (other - base) * weight`, and the
  normal is made one long again. The good mesh has the vertices of the base
  mesh, so only an evil hand changes its form.
- **The texture** (`Morphable::MorphTexture` `0x00619500`). The same choice
  of the other mesh. The weight is the alignment times 256 as an integer, at
  most 255. `fn_00870640` mixes each 4 bit channel of each texel, the alpha
  too: `(base * (255 - w) + other * w) / 255`. `CHand::MorphTexture`
  `0x0046BF80` then calls an empty function.
- **The bones** (`Morphable::MorphAnims` `0x00619100`). The rest pose is
  mixed in the same way. The three files have the same bones, so nothing
  changes for the Hand.

There is no tint and no bone scale. The look is the two textures and the
form of the evil mesh.

### The export (`tools/hand_export`)

```
python bwvr/tools/hand_export/export.py
```

The tool reads the files of the game and writes `media\hand`, which git does
not track. The output is the art of the game: it is never committed and
never in the installer. `README_hand.txt` in the tool folder is the text of
the README of the output.

| File of the tool | What it does |
| --- | --- |
| `bw_hand.py` | Reads the L3D files, the HBN file and the spec file, and makes the pose of each frame as the game does |
| `export.py` | Writes the textures as PNG and the data as JSON, starts Blender for each step below and makes the picture sheets |
| `blender_build.py` | In Blender 4.5: makes the mesh, the bones, the skin, the shape keys and one action for each animation, and writes the `.blend`, the FBX and the GLB |
| `blender_check.py` | In the newest Blender: reads the FBX and the GLB back, compares them with the data of the game and renders the pictures |
| `blender_example.py` | In the newest Blender: makes `example\bwvr-hand.glb`, an edited hand with each triangle cut in four, with the steps of "A hand that the player edited" |

**The frame of the files.** The stored mesh is left handed, and a 3D editor
is right handed. The tool mirrors x, so the hand stays the left hand that
the game stores: `(x, y, z)` of the mesh is `(-x, y, z)` in the FBX and the
GLB (the fingers to +y, the palm to +z, the thumb to -x) and `(-x, -z, y)` in
Blender. The mirror turns the order of each triangle. The origin is the tip
of the index finger. The size is real: 623.6 mesh units are 19 cm.

**The bones in the files.** A Blender bone has its length along its own y,
and the stored bone has a mirror in it. The tool gives each bone the place
of its joint and the axes `-x, z, y` of the stored bone, so a finger bends
about the x of its bone. The names are `b00_wrist` to `b21_thumb3`: the
number is the bone of the game. For each key of an animation the tool gives
Blender the move of each bone from its rest pose in the frame of the mesh
(`inverse(rest) * pose`), so the result does not depend on the axes of a
bone.

**What a file holds.** One mesh of 141 vertices and 169 triangles with the
texture points and the normals of the game. 22 bones. One bone with the
weight 1 for each vertex. The shape keys `good` and `evil`. 26 takes, each
animation of the file, at 30 frames a second. A take that loops ends with
its first frame again. A lean is a take on the stand frame. The textures are
PNG files of 256 by 256 with alpha. `looks\` has the good and the evil hand
as separate files.

**The check** (`check_report.txt`, 53 checks, all pass). Blender 4.5.4 wrote
the files. Blender 5.1.0 read the FBX with each of its two importers, and
the GLB:

| What | The FBX | The GLB |
| --- | --- | --- |
| Vertices, triangles, bones, takes | 141, 169, 22, 26 | 141, 169, 22, 26 |
| The worst place of a vertex | 0.00002 mm | 0.00004 mm |
| The worst place of a bone joint | 0.00003 mm | 0.00006 mm |
| A vertex with a wrong bone or weight | 0 | 0 |
| The worst texture point | 0.000000 | 0.000000 |
| The worst normal | 0.04 degrees | 0.03 degrees |
| The shape key `evil` against the evil mesh | 0.00002 mm | 0.00005 mm |
| The skinned mesh at the 105 keys of the takes, against the pose math above | 0.0002 mm | 0.0002 mm |

`preview\compare_game.png` puts a desktop capture of the Hand of the mouse
player beside the take `00_wiggle` of the FBX, mirrored: the same form, the
same texture and the same fade at the wrist.

**Limits.** A key of a take is on a whole frame of 30 a second, so it can be
17 ms from the time of the game. The old FBX importer of Blender also makes
one action for the shape keys in each take. The tool does not write the
five floats of a lean.

## The game's hand on both real hands (2026-10-04)

Buck, 2026-10-04: "The next task would then be the hand models as our own
models as that is a huge part of the charm of the game." Earlier he asked
for one pair of hands, with the look of the game's Hand, and he said that he
likes how the mod's hand follows the trigger and the grip by how far they
are pressed.

So the mod's hands are now the game's own hand mesh, with the game's
textures and the game's light. The poses and the fingers stay as they were:
the mod chooses a pose, and the trigger and the grip move the fingers in it.
The code is in `src/mod/hand_model.cpp` (the files, the look, the light and
the draw), `src/mod/hand_model_math.h` (the pure math) and
`src/mod/overlay.cpp` (the poses, and which hands draw). The format of the
files and the game's own code are in "The model of the Hand".

### What the mod does

**Nothing of the game is in the mod.** At the first frame with a controller
hand, the mod reads `Hand_Boned_Base2.l3d`, `Hand_Boned_Good2.l3d` and
`Hand_Boned_Evil2.l3d` from `Data\CreatureMesh` of the game folder of the
player. It checks each offset and each count. A file that is not there or
not as the mod knows it gives one log line with the reason, and the low poly
hands of "One pair of hands" stay.

**The pose.** `overlay.cpp` gives each hand a curl for each finger, from
the same poses (hold, point, flat, fist, relaxed), the same blend of 0.1 s
and the same trigger and grip as before. A curl bends the bones of a finger
about their own x, as each animation of the game does. The bend of each
bone at a curl of 1 is the game's own tightest pose, measured in `hh.HBN`:

| Finger | Bones | The bend at a curl of 1, in degrees | From |
| --- | --- | --- | --- |
| Thumb | 20, 21 | 44.9, 90.3 | `point`, frame 1 |
| Index | 17, 18, 19 | 73.8, 66.6, 66.6 | `tap_house` |
| Middle | 8, 9, 10 | 89.4, 83.5, 53.9 | `point`, frame 1 |
| Ring | 14, 15, 16 | 113.8, 94.3, 64.7 | `point`, frame 1 |
| Little | 11, 12, 13 | 123.7, 70.9, 59.3 | `point`, frame 1 |

A curl of 0 is the rest pose of the file. The bones in the palm and the
wrist do not move. A fist of the mesh with these angles is the hand of the
game's `point` animation, within 13.5 mesh units at each bone joint (4 mm on
a hand of 19 cm). The hand of the mouse player (`wiggle`) has the middle,
ring and little fingers at about 0.11 to 0.24 of these bends, which is the
rest curl of 0.12 of the mod.

The hold pose closes each finger on the ball of the held object with the
joints of the mesh (`MeshHoldRange`), not with the joints of the low poly
hand.

**The place.** The mesh has the turn of the exact root (the rest turn of
`hand_mesh_yaw_deg`, `hand_mesh_pitch_deg` and `hand_mesh_roll_deg`, then
the fingers along the finger axis of the grip and the palm away from the
back of the hand) and the size of the real hand: 623.6 mesh units are one
hand length. It is then moved until the tip of its own index finger, at the
rest curl of 0.12, is the point of the Hand (`MakePlacement`). So the glow,
the pick, the touch of the land and each other interaction are at the tip of
the index finger of the hand that the player sees. The trigger bends the
index finger and does not move the point.

With this place the wrist joint of the mesh is at `0.096 0.474 -0.030` of
the hand frame (the low poly hand: `0.130 0.490 0.000`), 0.9 cm away on a
hand of 19 cm. The palm of the mesh is 2.9 mm in front of the palm that the
code uses (`HandExactPalm`, `overlay_math::HoldCenter`), so a held object and
the leash are on the palm of the mesh.

**The two hands.** The right hand is the mirror image of the stored mesh,
as in the game. The left hand is the stored mesh.

**The look.** The mod mixes the base mesh and texture with the evil or the
good ones by the player's alignment (`CHand+0x9C`), with the game's own
formulas ("How the game changes the look"): the vertices and the normals in
a straight line, and each 4 bit channel of each texel as `fn_00870640` does.
The look changes when the alignment moved by 0.03, as in the game.

**The light.** The game lights a vertex of a boned mesh with
`min(1, 0.4 + 1.3 * max(0, n.l))` times the color of the object, where `l`
goes from the bone to the light position `0x00EA9E90` [V `fn_0084C3C0`,
`0x0084C626` to `0x0084C6B6`]. The color of the object is `+0x4C` of the
mesh object (`CHand+0x482C`) [V `fn_00813BA0`]. The mod reads the light
position and that color in each frame and uses the same formula, with the
direction from the grip of each hand. In run hm3 the light was at
`-500000 500000 -500000`, and the color went from `0xFFFFFFFF` to
`0xFFFFDE99` while the sun set. So the hands have the light of the game at
each time of day.

**The draw.** As the overlay: the game's Direct3D 7 device, vertices of the
format `0x1C4` projected on the CPU with the camera of the eye, one
`DrawPrimitive` for both hands in each eye, and each state put back after
it. The differences:

- The texture is a surface of the mod on the game's DirectDraw, 256 by 256
  with 8 bits a channel (4 bits when the device gives no other). The mod
  fills it again when the look changes. The game keeps its own copy of the
  texture of each stage (`0x00EC81D0`), so the texture that was on stage 0
  goes back after the draw.
- Stage 0 multiplies the texture by the lit color, and the alpha of the
  texture by the alpha of the color. The blend is the plain alpha blend, so
  the wrist fades as in the game. A texel with an alpha below 8 of 255
  writes nothing.
- A triangle that faces away from the eye is not drawn. The mod finds that
  on the CPU, with the sign of each mirror on the way to the camera
  (`FacesCamera`).
- The hands draw after the arc, the ray and the tether of the overlay,
  because the wrist is not opaque.

**When the low poly hands draw.** With `hand_model=0`. When a hand file of
the game is not readable. When the device gives no texture. After the test
key (below). Each case has a log line.

### The fingers from outside

A later feature will bend each finger by what is near it: over the land,
toward an object that the grip can take, round an object by its size. The
hand model takes one curl and one spread for each finger
(`hand_model::FingerPose`), and `overlay.cpp` has three functions for a
caller. A caller declares them in its own file, as the mod does it for
`OverlayHandPoints`.

```
void OverlaySetFinger(int hand, int finger, float curl, float spread,
                      float weight);
bool OverlayPoseCurls(int hand, float curls[5]);
bool OverlayFingerJoints(int hand, int finger, float curl, float spread,
                         float joints[4][3], float* radius);
```

- `hand` is 0 for the left hand and 1 for the right hand. `finger` is 0 for
  the thumb and 1 to 4 for the index finger to the little finger.
- `curl` is 0 for an open finger (the rest pose of the mesh) and 1 for a
  finger of a fist. `spread` is in radians at the knuckle, plus toward the
  thumb side. For the thumb it turns the base of the thumb.
- `OverlaySetFinger` gives one finger its curl and its spread. `weight` (0
  to 1) is the share of this curl against the curl of the pose, so a caller
  can fade its request in and out. Call it in each frame. The request ends
  one frame after the last call.
- `OverlayPoseCurls` gives the curls that the pose gave in the last frame,
  before a request. So a caller can make its curl larger or smaller than
  the pose.
- `OverlayFingerJoints` gives the knuckle, the two joints and the tip of a
  finger in the world for a curl and a spread, at the place where the hand
  was drawn last, and the radius of the finger. A caller can try some curls
  and take the largest one at which the finger is not in the land or in an
  object.

The three functions work with the mesh and with the low poly hands. Only
the mesh shows a spread.

Hand feel ("Hands that react to the world") does not use them. It needs
the curls of the pose in the same frame, so `overlay.cpp` gives them to
`HandFeelCurls` and takes the new curls back. A request of
`OverlaySetFinger` then acts on the curls that hand feel made.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hand_model` | 1 | 1 draws both hands with the game's own hand mesh. 0 draws the low poly hands |
| `hand_model_file` | 1 | 1 uses `bwvr-hand.glb` when it is beside the mod ("A hand that the player edited") |
| `hand_model_test_look` | | Test key. Frame numbers. At each one the look steps, as Shift and the minus key do |
| `hand_model_test_swap` | | Test key. Frame numbers. At each one the hands change, as Ctrl and the minus key do |
| `hand_model_file_path` | | Test key. The full path of a file to read in place of `bwvr-hand.glb` |

`hand_mesh_yaw_deg`, `hand_mesh_pitch_deg` and `hand_mesh_roll_deg` ("The
exact Hand") turn the mesh of the mod's hands too. `hand_tint` colors only
the low poly hands. The keys are read from `bwvr.ini` at the start. They are
not in the launcher.

With `test_keys=1`:

- **Ctrl and the minus key of the number pad** go through the hands: the
  mesh of the file of the player (when there is one), the mesh of the game,
  the low poly hands. The pointing controller gives a long pulse for a mesh
  and a short pulse for the low poly hands.
- **Shift and the minus key of the number pad** step the look: the
  alignment of the player, evil, neutral, good.
- The minus key alone still shows and hides the mesh of the game's own
  Hand ("One pair of hands").

### The log

The tag `hand model`. At the first frame with a hand: the counts of the
mesh that was read, or the reason why the low poly hands draw. Whether a
file of the player is in use. Where the wrist joint, the knuckle of the
index finger and the palm of the mesh are against the low poly hand. The
light position and the color of the game's Hand, and again when the color
is far from the last one in the log. Each change of the look (40 at most),
with the alignment and the two weights. The kind of the texture, and the
first draw. Each press of a test key. Once a report window: the eye passes
drawn, the triangles a pass, the triangles that face away, and the time on
the CPU to skin and to draw.

The tag `overlay`: once, the index tip of the mesh against the point of the
Hand. The line of the cost of the overlay now shows with `overlay=0` too.

### Tests

**The math test** has 86 new checks (`hand_model_checks.h`), 2501 in all
with the checks of the ring.
The test has no game files. It makes a hand with the layout of the game's
bones: the same parents, the length of a bone along its z, the bend about
its x, a mirror in the wrist and the index tip at the origin.

- A curl of 0 is the rest pose, and its skin matrices change nothing. A
  curl of 1 bends each bone by its angle of the table, keeps each length,
  goes to the palm side and moves no other finger. Half the curl is half
  the bend. The spread turns the knuckle to the thumb side and does not
  bend the finger.
- The place: the index tip at the rest curl is the point of the Hand, with
  and without a rest turn. The fingers go along -y of the hand frame, the
  palm faces -x, and the wrist joint to the end of the middle finger is 1
  hand length. A full trigger takes the tip toward the palm.
- One matrix for each bone gives the same place as the long way (the skin,
  the place on the hand, the place in the world), for both hands. The right
  hand has a mirror and the left hand has none. A triangle with its outside
  away from the camera is not drawn, and a mirror turns that.
- The hold: the joints of each finger keep their distances at each curl, a
  larger ball opens each finger more, and at the contact no joint is in the
  ball.
- The light of the game at four angles. The mix of two texels at the
  weights 0, 128 and 255, and the weight from the alignment. The look from
  the alignment.
- A hand from a file ("A hand that the player edited").

**Desktop runs** (2026-10-04, the fake controller, `fake_controller_front=1`,
a render of 2880 by 2880, the game of "Keep your old Creature" unless the
line says another one). The pictures are in the run folders of the
scratchpad, `desk-hm1` to `desk-hm5`.

- **hm1**, both hands 45 cm in front of the eyes. The log: 141 vertices, 169
  triangles, 22 bones and 3 textures read. A texture surface of 32 bits. The
  texture of stage 0 went back. With the relaxed pose the index tip of the
  mesh was 0.0 cm from the game's Hand position, which is the point of the
  Hand. The pictures show both hands with the mesh, the texture and the
  fade at the wrist: relaxed, with the trigger (the index finger bends),
  with both grips (a fist), at four turns of the wrist, with the evil look
  (red, with claws), the neutral look and the good look
  (`hand_model_test_look`), and the low poly hands after
  `hand_model_test_swap`. During a story of the game no hand shows, as
  before.
- **hm2**, the same with `hand_model=0` and `hand_hide_game=0`: the game's
  own Hand, drawn by the game, on the right hand. The mean color of the
  hand pixels of the right hand, hm1 (the mod's mesh) against hm2 (the
  game's Hand), at four turns of the wrist:

  | Shot | The mod's mesh | The game's Hand |
  | --- | --- | --- |
  | 873 | 167 102 29 | 163 101 36 |
  | 1103 | 182 106 24 | 176 102 29 |
  | 1163 | 177 102 26 | 176 101 28 |
  | 1223 | 175 94 43 | 172 94 44 |

  So the light and the texture of the mod's hands are those of the game's
  Hand, within 6 of 255.
- **hm3**, the first story and a far fetch of the rock at the pen
  (`aim_run.ps1`). The right hand points at the far rock. With the rock in
  the hand the fingers are a cup round it, at four turns of the wrist, and
  the rock stays in the palm. After the release the hand is open. The log
  has the pose `hold` in the Tug state and "the Hand holds".
- **hm4**, the Creature (`creature_recipe.ps1`). The Creature state starts,
  and the hand of the stroke is the index finger of the mesh, at the
  Creature.
- **hm8**, the same with `creature_finger_below_pct=5` and
  `creature_flat_above_pct=10`, so the small Creature of the profile counts
  as a large one. The log: "the hand of the stroke is the flat hand" and the
  pose `flat`. The picture shows the open hand of the mesh on the Creature.
- **The cost.** The overlay took 0.097 ms a frame on the CPU with the mesh
  (0.015 ms to skin and light, 0.081 ms to project and draw both eyes, 372
  triangles a frame) and 0.127 ms with the low poly hands (1334 triangles a
  frame, with no test of the front). The frame was 20.43 ms with the mesh
  and 20.83 ms with the low poly hands: the same. The first frame takes
  4.2 ms more, to make the texture.

The runs hm6 to hm8 ("A hand that the player edited" has hm6 and hm7) are
with the code as it landed. Run hm9 is run hm6 again with the build after
the rebase on the ring of the left hand: the same log lines and the same
pictures. The runs hm1 to hm5 were with the build before
the last changes: the bend of the index finger was 74.5, 67 and 67 degrees,
some log lines had other words, and the functions for the fingers from
outside were not there.

**Not run on the desktop:** a real press of Ctrl and the minus key or of
Shift and the minus key (the two ini tests ran the same code), a left
handed player, a miracle in the Hand, the leash, a caller of the functions
for the fingers from outside, and a device that gives no texture.

### Limits

- The bones of the game's own Hand, which the mod still places for a spell
  and for an effect on a bone, are on the hand with straight fingers. The
  mesh that shows has its index tip at the point with the rest curl. So a
  spell on a bone can be up to 1.9 cm from the same bone of the hand that
  shows.
- The shadow of the game's Hand on the land still has the fingers of the
  game's animation.
- The game's poses of the fingers are not played as animations: each pose
  of the mod is a curl for each finger. The poses `hold_side`, `horn` and
  `phile` of the game also turn the wrist and the bones in the palm, and
  the mod does not.
- The thumb has one curl (its two last bones) and one spread (its base).

**Needs the headset.**

1. Look at both hands. Each must be the hand of the game, with the right
   size, on your real hand, with the wrist that fades. Say if a hand sits
   wrong on the controller: `hand_mesh_yaw_deg`, `hand_mesh_pitch_deg` and
   `hand_mesh_roll_deg` turn it.
2. Press the trigger and the grip slowly. The index finger, then the other
   fingers and the thumb, must follow at once and look right at each point
   of the way.
3. Pick up a rock, a villager and a tree. Say if a finger goes into the
   object or stays far from it.
4. Aim the teleport, stroke the Creature and drag the land: the point, the
   flat hand and the fist of the mesh.
5. With `test_keys=1`: Ctrl and the minus key of the number pad for the low
   poly hands and back, and Shift and that key for the evil and the good
   look.

## A hand that the player edited (2026-10-04)

Buck: "If you could rip the model as an fbx so I can add some more polygons
it would be useful." So the mod can draw the hands with a mesh that he
edited. The reader is `src/mod/hand_model_file.cpp`, and the fit is in
`hand_model_math.h`.

**The art.** A mesh that is made from the game's mesh is still the art of
the game. `bwvr-hand.glb` is not in the repository and not in the installer,
unless Buck decides so.

### The workflow

1. Run `python bwvr/tools/hand_export/export.py`. It writes `media\hand`
   ("The export").
2. Open `media\hand\bw_hand.blend` in Blender 4.5 or later.
3. Edit the mesh of the object `hand`. To close the seams of the texture
   first, use Mesh > Clean Up > Merge by Distance. Blender gives each new
   vertex the bones and the shape keys of the vertices around it.
4. Keep the 22 bones: do not move, rename, add or delete a bone. Keep the
   shape keys `good` and `evil`. Keep the texture points on the texture of
   the game.
5. Use File > Export > glTF 2.0, with the format glTF Binary (.glb) and each
   other setting as it is.
6. Give the file the name `bwvr-hand.glb` and put it in the game folder,
   beside `runblack.exe`, `ddraw.dll` and `bwvr.ini`.
7. Start the game. A line of the tag `hand model` in `bwvr.log` says that
   the file is the mesh of the hands, or why it is not.

`media\hand\README.txt` has the same steps for the owner
(`tools/hand_export/README_hand.txt` is its source).

### What the mod reads

The format is glTF binary, because Blender writes it and a small reader
with a permissive license reads it: cgltf 1.14 by Johannes Kuhlmann (MIT),
one header, with the JSON reader jsmn by Serge Zaitsev (MIT) in it. The
build takes it from `third_party/cgltf` (a clone, as MinHook is). A build
with no clone has no reader, and the log of the mod says so.
`installer/Notices.txt` has the notice.

- The first mesh with a skin, and each of its triangle primitives: the
  positions, the normals, the first texture points, the first four bones
  and weights of each vertex, and the indices. A file with no normals gets
  normals from its triangles.
- The bones by their names. `b07_middle0` is bone 7 of the game: the mod
  reads `b`, two digits, and then `_` or the end. A prefix that ends with
  `:` or `|` is ignored. The file must have each of the 22 bones.
- The shape keys `evil` and `good`, by their names. A shape key can be a
  sparse accessor, as Blender writes it. A file with no shape key `evil`
  gives an evil hand with the form of the base hand.
- Not read: the textures, the materials and the takes. The hands keep the
  textures and the light of the game, and the pose comes from the
  controllers.

A vertex can have up to 4 bones. The game's own mesh has one bone for each
vertex, so a mesh with more bones at a joint bends more smoothly than the
game's.

### The fit

The mod does not trust the units and the axes of the file. It takes the
place of each bone joint in the file (the world matrix of the joint node)
and in the game (the rest pose of the L3D file), and it finds the one map
that puts the joints of the file on the joints of the game (`FitJoints`):
a turn, one size for all axes, and a mirror when the file needs one. The
map comes from three joints that are far apart, the wrist and the knuckles
of the index finger and the little finger. Then each of the 22 joints must
be within 2 % of the length of the hand of its place: 4 mm on a hand of 19
cm. So a bone that was moved gives a file that is not used, and the log
names the bone.

Each vertex goes through the mix of the rest matrices of its bones
(`joint world * inverse bind`), and then through the map. A shape key is a
step, so it goes through the map without the offset. The file of the export
has a mirror in its map (the stored mesh is left handed and glTF is right
handed), so the reader turns the order of each triangle back.

### When the file is not used

Each case gives one log line with the reason, and the hands have the mesh
of the game:

| The reason in the log | The cause |
| --- | --- |
| The file is not a glTF file that the mod can read | Another format, or a damaged file |
| The data of the file is not complete | A `.gltf` with its data in other files |
| The file has no mesh with a skin | The mesh was exported with no armature |
| The file has no bone with a name that starts with b07 | A bone was deleted or renamed |
| The bones of the file are not at the places of the bones of the game | A bone was moved |
| A vertex of the file has a bone that is not a bone of the game | Weights on a bone that the player added |
| A vertex of the file has no bone | A vertex with no weight |
| A part of the mesh has no texture points (or no bones and weights) | An export setting was turned off |
| The mesh of the file has ... vertices | More than 20000 vertices or 20000 triangles. The mod skins each vertex on the CPU in each frame, so the limit keeps that time small |
| This build of the mod has no reader for the file | The build had no `third_party/cgltf` |

With `hand_model_file=0` the mod does not look for the file.

### Tests

**The math test** (part of the 86 checks of the hand model): the fit finds
the turn, the size and the mirror of a file with and without a mirror, a
vertex and a normal of the file go to their places, a bone that was moved
by 30 units is 30 units from its place and the fit names it, joints on one
line give no fit, and the bone number of six names.

**The reader with no game** (a test program in the scratchpad that links
`hand_model_file.cpp`, with the bones of the game's L3D file):

| File | Result |
| --- | --- |
| `media\hand\bw_hand.glb`, the export as it is | Used. One unit of the file is 3282.1 mesh units, with a mirror, and the worst bone joint is 0.00 mm from its place. 141 vertices and 169 triangles. Each vertex is 0.0003 mesh units from the vertex of the game or less, with the same bone. The shape key `evil` is the evil mesh within 0.0003 units. The turn of 163 of 169 triangles agrees with the normals, as in the game |
| `media\hand\looks\bw_hand_evil.glb` | Used, with no shape keys |
| The export after Merge by Distance and Subdivide (1 cut) in Blender 5.1.0, exported with the default settings | Used. 447 vertices and 676 triangles. The vertices of the game are at their places, and 167 vertices have 2 bones or more |
| The same with 2 cuts and a smoothness of 1 | Used. 922 vertices and 1521 triangles |
| A file with the bone 12 moved by 2 cm | Not used: "the bone 12 is 20.0 mm from its place on a hand of 19 cm. Do not move a bone" |
| A file with the bone `b07_middle0` renamed | Not used: "The file has no bone with a name that starts with b07" |
| A file with the mesh and no armature | Not used: "The file has no mesh with a skin" |
| A file with a vertex on a bone that was added | Not used: "A vertex of the file has a bone that is not a bone of the game (extra)" |
| A PNG file, the FBX file, and a path with no file | Not used: "The file is not a glTF file that the mod can read" |

**Desktop runs** (2026-10-04, as the runs of "The game's hand on both real
hands", with the test key `hand_model_file_path` so that no file goes into
the game folder):

- **hm5**, the export subdivided one time. The log: "bwvr-hand.glb is the
  mesh of the hands: 447 vertices and 676 triangles", with a mirror, the
  worst bone joint 0.00 mm from its place, and both shape keys. The pictures
  show both hands with the mesh of the file: relaxed, with the trigger, with
  both grips (two fists), the mesh of the game and the low poly hands after
  `hand_model_test_swap`, two turns of the wrist, and the evil look with the
  shape key `evil` of the file. On the CPU the file took 0.045 ms an eye
  pass to project and draw and 0.010 ms to skin and light, against 0.033 ms
  and 0.004 ms for the mesh of the game.
- **hm6**, a file with the bone 12 moved by 2 cm. The log: "bwvr-hand.glb
  is NOT USED, so the hands have the mesh of the game. The bones of the file
  are not at the places of the bones of the game: the bone 12 is 20.0 mm
  from its place on a hand of 19 cm. Do not move a bone". The pictures show
  the mesh of the game: relaxed, two fists and the evil look.
- **hm7**, the export with 2 cuts and a smoothness of 1: 922 vertices and
  1521 triangles. The pictures show rounder hands: relaxed, two fists, with
  the trigger, and the evil look. The overlay took 0.219 ms a frame on the
  CPU for both hands (0.049 ms to skin and light, 0.170 ms to project and
  draw 2909 triangles in the two eyes), against 0.097 ms for the mesh of the
  game.

**Not run:** a file in the game folder itself (the test key gives the path
to the same reader), and a file from another 3D editor than Blender.

**Needs the owner.** An edit that he made himself, exported with his own
Blender, and how the joints of a mesh with more bones look in the headset.

## The village with the real hand (2026-10-04)

The plain game is played around the village. The player sets how many
villagers worship with the totem of the Village Centre. The worshippers make
the chants (the prayer power) of the Worship Site. The player takes a miracle
there, makes it stronger and casts it. He helps the villagers build with
scaffolds. This section records what the game does for each of these, what
the real hand can do, and what the mod adds.

The code is in `src/mod/village.cpp`, with the pure math in `village_math.h`.
The cast and the cancel of a miracle are in `miracle.cpp` ("A miracle in the
Hand"). The pick and the grip are in `hand_direct.cpp`.

### What the game does [V]

All of this is from `disasm.py` on `runblack.exe` 1.42, and from
`Scripts\info.dat` and `Data\Gestures.jty`.

**The totem of the Village Centre.** The totem is a `TotemStatue` (vtable
`0x008E029C`). The town has it at `Town+0x9A4` (the `TownCentre`) and then
`+0xCC`. Its value is at `+0x80`, from 0 to 1.

- `TotemStatue::ValidForLockedSelectProcess` `0x00738500` says yes for the
  player who owns the totem, when the totem is complete, the heart of the
  Temple is complete and the town has a complete Worship Site
  (`Town+0x98C`). A press of the Action button then starts the locked select
  of the game, the same action as a take from a store (interface action 4).
  So the hand mode at the totem is `0xD`, as at a store.
- `NetworkUnfriendlyStartLockedSelect` `0x007385E0` copies the value to
  `+0x88`, starts Immersion effect 15 and the sound of the totem. While the
  action runs, the hand mode is `0xE` (`fn_005D80C0`: an object in the pick
  record that is no Creature), and the Hand is in state 5, Totem.
- `HandStateTotem::Enter` `0x005B7690` puts the Hand at the map point of the
  totem, at the height of its mesh plus 0.7 of its height. It pins the mouse
  picture there (`LHMouse::PinImage`).
- `HandStateTotem::Update` `0x005B77A0` reads the object of the pick record
  of now (`GInterface+0x3C8`). It takes the screen rows that the pointer went
  up since the last frame (`CHand+0x4868` less `+0x4860`) and makes a move
  from them: rows times the height of the totem times 3, divided by the
  height of the screen. A move down stops at the land. The move goes to
  `TotemStatue::NetworkUnfriendlyLockedSelect` `0x007386A0`, which adds 0.1
  of it to `+0x88` and keeps the result in 0 to 1.
- `TotemStatue::Draw` `0x00738960` puts the pole 8 units higher for each 1.0
  of the value. While the Hand moves the totem it shows a tooltip (text
  `0xEEB`) with the value times 100: "People Worshipping: 22%".
- The release (`NetworkUnfriendlyEndLockedSelect` `0x00738630`) sends packet
  `0x29` with the value. The town then works to that value (`Town+0x5C0`).
  `Town::GetWorshipersNeeded` `0x0073C860` multiplies it with the count of
  the villagers that can worship (`Town+0x618` and `+0x61C`) and rounds. A
  value above 0 sends one villager at the least.

**The Worship Site and its chants.** Each town of the player has a Worship
Site at the Temple (`Town+0x98C`, a `WorshipSite`). A town gets one only
when the game permits it: on Land 1 a script of the story does that
(`SET_CAN_BUILD_WORSHIPSITE`, `0x006FEC40`, which clears `Town+0x5F0` and
calls `Citadel::FindOrCreateWorshipSite`). The start with no story has no
Worship Site. Then the totem does nothing and no miracle icon is there.

The site keeps a pool of chants at `+0xF0`. Its step of a game turn (it ends
at `0x0077B894`) adds what the worshippers made in that turn and puts the sum
at `+0xF8`. The chants that the turn used are at `+0xFC`. With `+0x108` set
the site has 1,000,000 chants (`WorshipSite::GetTotalChantsAvailable`
`0x0077CC30`).

**A miracle icon.** The miracles of a town come from
`Town::AddMagicTypesHeld` `0x0073D380`: the setup of a land and a reward call
it. It turns the magic type on for the player (`GPlayer::SetMagicTypeEnabled`)
and gives the Village Centre the icon (`TownCentre::AddSpell`) or the power
up. The Worship Site has a `WorshipSpellIcon` (vtable `0x0099D878`) for each
miracle of its towns: the list starts at `WorshipSite+0xE0` and goes on at
`+0x110` of each icon. The icon has its `GSpellSeedInfo` at `+0x80`, the byte
"charges" at `+0x120`, the power up that it charges for at `+0x124`, the
player it charges for at `+0x128` and its chants at `+0x134`.

A tap on the icon (`SpellIcon::InterfaceTap` `0x00726430`,
`WorshipSpellIcon::ActualInterfaceTap` `0x0077F880`) does the first of these
that fits:

1. The icon has the chants that the miracle needs, and the Hand is empty:
   the miracle goes into the Hand (`0x0077F8F0`).
2. The icon charges for this player: the charge stops, and the chants go
   back to the pool (`CancelCharge` `0x0077F9A0`).
3. The Hand is empty and the player has the magic type: the icon starts to
   charge (`fn_0077FA00`).

`WorshipSite::ProcessSpellIcons` `0x0077B4D0` gives the chants of the turn to
the icons that charge, once a game turn. `WorshipSpellIcon::Process`
`0x0077F390` puts the `SpellSeed` in the Hand when the icon has enough. With
chants in the pool that takes 3 or 4 frames. The chants go into the seed
(`SpellSeed+0x74`). Water needs 5000 chants, Heal 6000 and Food 7000.

**The gestures.** `GSpellSeedInfo+0x100`, `+0x104` and `+0x108` are the
gestures of the chain that asks for a miracle (section 5). The first is the
spiral (1) for each miracle of the god and the inverse spiral (2) for each
Creature miracle. The second gesture is:

| Miracle | Second gesture | Power up 1, power up 2 |
| --- | --- | --- |
| Storm | fork down | inverse spiral, spiral |
| Forest | Cyrillic L | none |
| Fireball | vertical | inverse spiral, spiral |
| Food | S shape | spiral |
| Spiritual Shield | fork right | none |
| Physical Shield | fork left | none |
| Lightning | fork up | inverse spiral, spiral |
| Heal | heart | spiral |
| Wood | three | none |
| Water | W shape | spiral |
| Flock of birds | square spiral | none |
| Flock of animals | inverse square spiral | none |
| Teleport | house | none |
| Explosion | star | inverse spiral, spiral |

No miracle has a third gesture. The game looks for the start gesture only
while a Worship Site of the player has an icon that the player can ask for
(`WorshipSpellIcon::ValidForRequestSpell` `0x0077FBA0`): the magic type is
on, the icon does not charge, and the site has chants.

**A power up.** The magic type of a miracle is at `GSpellSeedInfo+0x124`, the
magic types of its power ups at `+0x128` to `+0x130` and their gestures at
`+0x134` to `+0x13C`. `fn_005CEE80` lists the gestures of the power ups that
the player has. The game looks for them only while `fn_005CEF50` says yes:
the seed is ready, it is from a Worship Site, and `SpellSeed+0x98` is 0. That
field is not 0 after the first cast. So a miracle gets stronger only before
its first cast. A match sends packet `0x6A`: the icon charges for the power
up, and `SpellSeed::SetPowerUp` `0x00729B30` runs when the seed has the
chants (the first power up of Water costs 2000 more).

**The way back.** A miracle from a Worship Site that leaves the Hand with the
shake gives the chants that it still has back to the pool
(`SpellSeed::ApplyToWorshipSite` `0x00729A80`). A press with the miracle on
its icon, or on the totem of the Worship Site, does the same
(`IsSpellSeedReturnPoint`). A pour takes chants out of the seed.

**The Village Store.** The piles of food and of wood are bulk sources
(`PileResource::ValidForLockedSelectProcess` `0x0066E4F0`, "The bulk take").
Food or wood that the Hand lets go over the store goes into it
(`StoragePit::AddResource` `0x00732F60`). The flags of desire
(`TownDesireFlags`) have a draw and a help text only. The player reads them.
No press acts on them.

**The Workshop and a scaffold.** The Workshop is a store for wood
(`Workshop::AddResource` `0x00779E00`). It makes a `Scaffold` with
`fn_00779910` [V], when it has the wood [I].

- The Hand can take a scaffold that is not planted, or one whose building
  site has no work done (`Scaffold::ValidForPlaceInHand` `0x006E96A0`).
- While the Hand carries it, `Scaffold::ProcessInHand` `0x006E8570` shows the
  building that it would make at the place of the Hand.
- A scaffold that the Hand lets go over a second scaffold joins it
  (`Scaffold::ValidToApplyThisToObject` `0x006E9900`, `Combine`
  `0x006E9B60`). The count of the scaffolds sets the kind of the building
  [I].
- A scaffold that the Hand lets go over the land makes the building site
  there (`TryToBuildPlannedBuilding` `0x006E9080`), when the game has a place
  for the building. `Scaffold::ProcessInHand` looks for the place each game
  turn: the land must be good for a fixed object
  (`MapCoords::IsSuitableForFixed`), a town must be there for the building
  (`Scaffold::GetTownForBuilding`), and the place must be away from the
  Workshops (`Town::IsScaffoldAwayFromWorkshops`). The hint of the game for
  the grip is then the name of the building, and else "Throw". The site is
  there when the scaffold is on the land: 58 frames after the opening in run
  vilU, and not yet after 37 frames in run vilX. When the scaffold has more
  scaffolds than the building
  needs, the rest stays in the Hand (`InterfaceSetOutMagicHand`
  `0x006E9770`).
- A tap on a scaffold of two or more takes it apart
  (`Scaffold::InterfaceValidToTap` `0x006E9D80`, `InterfaceTap`
  `0x006E9DD0`).

**A villager and a job.** While the Hand carries a villager,
`Villager::InHand` `0x0076ADE0` looks for the objects near the villager each
game turn (`FindCloseObjectsForInteract`, from the place of the villager).
The first object that gives a job (`GetDiscipleStateIfInteractedWith`, slot
`0x644`: a field, a forest, a store, the Workshop, the Worship Site, a house)
sets it (`Villager::SetVillagerDisciple` `0x00756000`), and the game shows
the icon of the job. The villager keeps the job when the Hand lets him go.
The job comes from the place of the villager, so it follows the Hand. It does
not use the pick record.

### Can the real hand do it

| Thing | With the real hand | Proof |
| --- | --- | --- |
| The totem | Yes. The grip takes it, and the pole follows the hand one to one (`totem_hand=1`) | Runs vilC and vilE |
| A miracle icon | Yes. A short press of the grip takes the miracle | Runs vilD to vilG |
| The gestures of a miracle | Yes. B and a draw: the spiral, then the gesture of the miracle | Runs vilF and vilG |
| A power up | Yes, before the first cast. B and a draw of its gesture | Run vilG |
| The cast | Yes, by its kind ("A miracle in the Hand") | Runs vilD and vilG (the pour of Water) |
| The cancel, and the power that goes back | Yes. The shake or a tap of B | Runs vilD, vilF and vilG |
| The Village Store | Yes: hold the grip to collect. With `hold_target=1` the opening of the grip over the store puts the wood or the food in | "The bulk take", and runs vilP0 and vilP1 of "An object in the Hand that goes onto a thing" |
| The flags of desire | Nothing to do | |
| A scaffold: take, set down, take apart | Yes: the grip takes it and sets it down, and a short press takes a scaffold of two apart | Run vilL |
| A scaffold: a building site | Yes: the grip opens where the game has a place for the building | Runs vilU and vilX of "An object in the Hand that goes onto a thing" |
| The Workshop | Wood: yes, the grip opens with the wood at the Workshop. A scaffold from it was not seen | Runs vilW and vilW2 |
| Two scaffolds: join | Yes, with `hold_target=1`. Before, the opening of the grip only set the scaffold down | Runs vilK and vilL |
| A villager and a job | Yes: the job comes from the place of the villager in the Hand, and the card says the job | Runs vilT6 and vilT7 |

### State 5 before this change

The Hand state 5 had no handling in the mod. The runs show what it did.

- The start of the desktop runs has no Worship Site, so the totem was no
  target of the grip at all (run vilA). With a Worship Site the grip took
  the totem, and the game went to state 5 (run vilE, `totem_hand=0`).
- In state 5 the Hand of the game is on the top of the pole: 5.4 cm of the
  room from the point of the real hand in run vilE. The mesh of the game's
  Hand is hidden (`hand_hide_game=1`), and the mod draws its hands at the
  real hands. So the player sees no Hand on the pole.
- The pole did not follow the hand. The game read the screen rows of the
  pixel of the Hand's point. A hand that went 4 cm up moved the value by
  0.075: the pole went 1.35 cm up, a third of the way of the hand. The
  ratio changes with the distance of the head.
- The source of the take stays in the pick record ("The bulk take"), so the
  game kept the totem while the hand went away from it, and the opening of
  the grip ended the action.

### What the mod does

**The pole follows the real hand (`totem_hand=1`).** A hook before
`HandStateTotem::Update` gives the game the screen rows that make its own
math put the pole where the hand is (`village_math::TotemValue`,
`TotemRows`). The mod keeps the value of the totem and the height of the
point of the real hand from the frame in which the game starts to move the
totem. The value that it wants is that value plus the way of the hand in
world units, divided by 8. The rows are the rest between that value and the
value of the game, so no error adds up. A hand that goes above the top or
below the bottom of the travel does not move the pole. The pole moves again
when the hand comes back to the place where the pole stopped. The whole
travel is 8 units: 18 cm of the room at the default scale of the God view.

The mod changes only the rows. The game does the rest: the start, the stop
at the land, the tooltip, the sound, the packet.

**A scaffold joins the scaffold at the Hand.** Since 2026-10-05 this is one
case of a rule for each object in the Hand, with the key `hold_target`: see
"An object in the Hand that goes onto a thing". The first build had the key
`scaffold_join` and acted for a scaffold only. The game
joins two scaffolds when the Hand lets one go while the pick record holds
the other one. With an object in the Hand the mod left the record to the
game's pick at the pixel of the Hand's point. In run vilK that pixel was on
the Temple behind the scaffolds, so the game set the scaffold down beside
the second one. `VillageSelect` now runs at the end of the first eye pass
while the Hand carries a scaffold. It looks at each object that the draw
offered and that the game lets the scaffold go on
(`ValidToApplyThisToObject`). The target is the object whose body is nearest
to the Hand's point or to the scaffold in the Hand, within
`scaffold_join_cm` (6 cm of the room, `village_math::JoinGap`).
`VillageKeepRecord` puts the target in the next pick record after each eye
pass, before each interface step and before each press. So the opening of
the grip joins the two. With no target the record is as before.

**The card** has the context "the totem or a miracle icon". Its lines stay
while the Hand's point is there, and one line of the game's own text stays
with them.

| State | Lines |
| --- | --- |
| The Hand's point is at the totem | "[Grip] Hold, then raise or lower", "Worship: 22%, 10 of 44 villagers" |
| The Hand moves the totem | "[Grip] Raise or lower, open to set", and the same line of the worship |
| The Hand's point is at a miracle icon | "[Grip] Tap to take the miracle" |
| The icon charges for the player | "[Grip] Tap to stop", "The miracle charges: 60%" |
| The site and the icon have no chants | "No prayer power. Villagers must worship" |
| A scaffold in the Hand is at a second scaffold | "[Grip] Open to join the scaffolds" |
| A scaffold in the Hand is at an other object that takes it | "[Grip] Open to put the scaffold here" |

The count of the villagers is the count of the game
(`village_math::Worshippers`). With a miracle in the Hand the lines of
"A miracle in the Hand" show. At its icon the line is "[Grip] Give the
miracle back".

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `totem_hand` | 1 | 0: the builds to 2026-10-04. The game moves the totem with the screen rows of the pixel of the Hand's point |
| `hold_target`, `hold_target_cm` | 1, 6 | See "An object in the Hand that goes onto a thing". They replace `scaffold_join` and `scaffold_join_cm` |

The keys are read from `bwvr.ini`. They are not in the launcher.

**The A/B key**, with `test_keys=1`: Ctrl with the 4 key of the number pad
changes `totem_hand`. The pointing controller gives a long pulse for 1 and a
short pulse for 0.

Test keys for the desktop runs only (the launcher removes them):

- `village_test_site`: a frame. At that frame the first town of the player
  gets its Worship Site, complete, from the function that the setup of a
  land uses (`CitadelHeart::CreateBuiltWorshipSite` `0x00465110`).
- `village_test_miracles`: a list of magic types. Before the site is made,
  the town knows each of them (`Town::AddMagicTypesHeld`). 22 is Water and
  23 its power up, 14 Food and 15 its power up, 10 Heal.
- `village_test_chants`: a list `amount@frame`. At that frame the pool of
  each Worship Site of the player has that many chants.
- `village_test_census`: a list of frames. At each one the log gets the
  towns, the totems, the Worship Sites, the miracle icons and the miracle in
  the Hand.
- `village_test_icon`: the seed type of the icon for the place `icon`.
- `village_test_scaffolds`: `count@frame`. At that frame the game makes that
  many scaffolds on the land under the Hand, 12 units apart, with the
  function that a reward uses (`fn_006E8460`). The place `scaffold` of the
  word `seek` gives each scaffold in its turn.
- `seek_plan` has new places for the word `seek`: `gosite` and `gototem`
  (the God view teleports to 60 cm of the room before the Worship Site or
  the totem), `icon` and `totem` (the Hand's point goes there), `up` and
  `down` (the Hand's point goes 4 cm of the room up or down,
  `seek_step_cm`).

The word `trace` takes its templates from `fake_controller_gestures`. The
numbers of that list must have spaces between them, not commas.

### The log

The tag `village`, one line for each change, 400 lines:

- The key at the start, and each press of the A/B key.
- "the Hand holds the totem ... of the Village Centre", with the value, the
  count of the villagers, the rule, the Hand state and the hand mode.
- "in the Totem state the Hand of the game is at ... and the point of the
  real hand is at ...", 20 frames after the state starts.
- "the totem is at ...", for each 10 percent of change, with the way of the
  real hand, the value that the mod wanted and the rows that it gave.
- "the Hand let the totem ... go", with the frames and the value.
- "the town ... now works to a value of worship of ...".
- "the miracle icon ... starts to charge" and "the charge of the miracle
  icon ... ended", with the chants of the icon and of the site.
- "the pool of chants of the Worship Site ... went from ... to ...", for a
  change of 100 chants or more in one frame.
- "the scaffold ... in the Hand can go on the ...", with the gap and the
  reach, and "... has no object to go on now".
- One line each report window: the frames in the Totem state, the frames on
  which the mod gave the rows, the totems taken and the charges.

The lines of a miracle in the Hand have the tag `miracle`, and they say
"from a Worship Site". A press with the miracle at its icon gives "the game
takes ... back at the ... of its Worship Site".

### Tests

**The math test** has 39 new checks (`village_checks.h`): the value of the
totem for a hand that goes up, down, above the top and back, the rows
against the game's own math on a screen of 2880 and of 600 rows, the count of
the worshippers, the percent, each state of a miracle icon, the percent of
its charge, and the gap and the choice of a join.

**Desktop runs** (`village_run.ps1` in the scratchpad, 2026-10-04, the God
view at 44 units a meter, a render of 2880 by 2880). The start is "Keep your
old Creature". The town has 44 villagers that can worship.

- **vilA, the start as it is.** The town has no Worship Site. The totem is
  no target of the grip, and the site has no icon.
- **vilC, the totem** (`totem_hand=1`). The Hand's point on the pole: the
  card "[Grip] Hold, then raise or lower, Worship: 0%, 0 of 44 villagers",
  hand mode `0xD`. The grip closed: Immersion effect 15, hand mode `0xE`,
  state 5 four frames later. Three steps of 4.0 cm up: the value 0.22, 0.44
  and 0.67 (10, 20 and 29 villagers), 179 rows for each step. Three steps
  down: 0.44, 0.22 and 0.00. Two more steps down: the value stayed at 0. The
  grip opened: state 1 four frames later, "the Hand let the totem ... go
  after 279 frames". 4 cm of the room are 1.78 units, and 1.78 of 8 is 0.22.
- **vilE, the same with `totem_hand=0`.** Three steps of 4.0 cm up: 0.07,
  0.15 and 0.23. The Hand of the game 5.4 cm from the point of the real
  hand.
- **vilD, a miracle from the icon.** A pool of 20000 chants. The Hand's
  point on the icon of Water: "[Grip] Tap to take the miracle". A press of 3
  frames: "Water is active in the Hand ... from a Worship Site" 4 frames
  later, state 8, the Hand of the game 0.0 cm from the point of the real
  hand, and the pool at 15000. With the miracle at its icon: "[Grip] Give
  the miracle back". Over the land the grip closed for 60 frames: "the game
  casts Water at the land point ... interface action 10", effect 25, 14
  casts. A tap of B: the miracle left the Hand, and the pool went to 19860.
  A draw of the spiral after the pour: the game did not look for it
  (run vilF: `SpellSeed+0x98` was 22 after the pour).
- **vilG, the whole chain.** The tap on the icon, and then B with a draw of
  the spiral (the word `trace`, template 64): "the game matched a spiral" 116
  frames later, "Water has power up 0 now (it was -1)", the pool from 15000
  to 13000, and the card "Water Miracle Increase". The pour: 14 casts, and
  the seed went from 7000 to 4870 chants. A tap of B: the pool from 13000 to
  17870. With the Hand empty, B and the spiral: the game matched it. B and
  the W shape (template 48): "the game matched a W shape (template 49)", and
  2 frames later "Water is active in the Hand ... from a Worship Site", the
  pool from 17870 to 12870. A shake (5 steps of 15 cm): the miracle left the
  Hand 2 frames later, and the pool went back to 17870.
- **vilF** showed the same choice with the gestures and the same shake on
  the build before.
- **vilI, an icon that charges.** A pool of 3000 chants, and Water needs
  5000. The tap: "the miracle icon ... of Water starts to charge for the
  player ... It has 3000 chants and needs 2000 more", the pool at 0, and the
  card "Water Miracle 60%, [Grip] Tap to stop, The miracle charges: 60%". A
  second tap 160 frames later: "the charge ... ended. The icon has 0 chants
  (3000 before)", and the pool at 3000 again. With a pool of 20000 the tap
  gave the miracle. A press with the miracle at its icon ("[Grip] Give the
  miracle back"): the game took the miracle (packets `0x4D` and `0x11`), and
  the pool went from 15000 to 20000.
- **vilK, two scaffolds with `scaffold_join` not built yet.** The test town
  has no Workshop, so the test key made the scaffolds with no Workshop. The
  Hand's point on the first: the game's hint "[Grip] Scaffold Value 1", hand
  mode 9. The grip closed: state 3 for 6 frames, then state 4, and the hint
  "[Grip] Build". With the scaffold at the second one the grip opened: the
  pick record held the `CitadelHeart`, and "The game sets the object down".
  The census: two scaffolds of 1.
- **vilL, the same with `scaffold_join=1`.** "the scaffold ... in the Hand
  can go on the Scaffold ...: its body is -0.6 cm of the room away", and the
  card "[Grip] Open to join the scaffolds". The grip opened: the pick record
  held the second scaffold. The census: the first scaffold is gone, and the
  second is "a scaffold of 2" (the game's hint: "Scaffold Value 2"). A press
  of 3 frames on it: the census has two scaffolds of 1 again, 2 units apart.
  The grip took one (the hint "Combine Scaffolds" near the other one) and
  set it down 18 units away: "The game sets the object down".

**Not run on the desktop:**

- Each miracle other than Water, and the second power up.
- Worshippers that make the chants: the runs set the pool with a test key.
- A scaffold of three or more. The Workshop, a building site and the job
  of a villager ran later: see "An object in the Hand that goes onto a
  thing".
- The join with the key off on the build that has the key ran later: run
  vilQ0 of "An object in the Hand that goes onto a thing".
- A left handed player, and a multiplayer game.

**Needs the headset.** Whether the pole feels held: the mod's hand stays
where the grip closed on the pole, and the pole moves with it. Whether 18 cm
for the whole travel is easy to set to a wanted count. Whether a real draw of
the spiral and of the second gesture fits the templates of the game. Whether
a tap of the grip on an icon is easy to hit: the icons of one site are 4 to 8
cm of the room apart.

**Limits.** The pole follows the height of the hand only: the hand can move
away sideways while the grip is closed. The mod does not change the game's
rule that a Worship Site must exist. A player who starts with no story has no
Worship Site until the game makes one.

## Hands that react to the world (2026-10-04)

Buck, 2026-10-04: "Regarding hands, HL:A has really good interactions with
the environment, hands and fingers move as you hover over objects and stuff,
fingers flex when moving over shelves, or start grasping objects and door
handles when you get close, etc. I think we must incorporate something like
that so the hand feels like it's interacting with the world a bit more."

So the fingers of the mod's hands now react to what is near them: the land,
a target of the grip, the object in the Hand, and the body of an object that
the hand passes. The code is in `src/mod/hand_feel.cpp` (the game side),
`src/mod/hand_feel_math.h` (the pure math) and `src/mod/overlay.cpp` (the
call, after the curls of the pose).

### The research

**What Half-Life: Alyx does.** `research-hla.md`, section 6, has the facts
from the game files. The ones that count here:

- The hand has one curl value and one target point for each finger. Near a
  surface each finger goes to a point on that surface.
- A sensor of 20 cm around the hand starts the finger tests, with 10 samples
  for each finger. It reacts to each surface, not only to an object that the
  hand can take.
- The hand that the player sees does not go through the world. It stays at
  the surface, and a second, faint hand shows the real place when the two
  are more than 3.8 cm apart.
- An object that the hand takes goes to a hold pose in 0.10 s. The level
  designers put hand pose markers (`prop_handpose`) on things such as the
  rungs of a ladder.
- A hand that hits the world gives a pulse by the force of the hit, at most
  one in 0.2 s.

The public statements of the developers add little to that. Bronwen Grimes
of Valve said that the mix of the trigger with the tracked fingers of the
player was the best way to read what the player wants to do with an object.
In the question and answer session of January 2020 the team said that the
game simulates arms that the player does not see, to find a pose that a real
arm cannot have. The developer commentary of the game has no entry on the
fingers.

Sources:
`https://www.uploadvr.com/valve-index-alyx-interactions/`,
`https://www.uploadvr.com/half-life-alyx-ama/`,
`https://developer.valvesoftware.com/wiki/Half-Life:_Alyx_Workshop_Tools/Level_Design/Ladders`,
`https://combineoverwiki.net/wiki/Developer_commentary/Half-Life:_Alyx`.

**What the mod cannot take from it.** In Half-Life: Alyx the hand stops at a
surface. The mod has a rule against that: the Hand is the real hand, 1:1,
and its place and its turn never change. So in the mod only the fingers
react. A hand that the player pushes into the land goes into the land, and
the land hides it. There is one exception from before this work: the land
test of the Hand's point lifts the pointing hand so that the tip of its
index finger stays 0.5 cm over the land ("The point touches the land").

**What the game gives cheaply.**

| Data | From | Cost |
| --- | --- | --- |
| The height of the land at a point | `LH3DIsland::GetAltitude` `0x00803090`, as the land test of the Hand's point uses it | One call for each point |
| The objects near a hand | The candidates of the pick: each object that the game drew in the first eye pass (`hand_direct.cpp`) | A list that the mod has already |
| The body of an object | The body that the choice of the grip uses (`near_math.h`): a cylinder from `Get2DRadius` and `GetHeight`, a trunk and a crown for a tree, a capsule for a Creature | Two virtual calls for each object near a hand |
| The object in the Hand and its place | `CHand+0x4904`, and the matrix that `hand_exact.cpp` writes for it | Read only |
| The game's own hand poses | The 26 animations of `hh.HBN` ("The model of the Hand") | Measured once, with `tools/hand_export` |
| The game's own rule for a hold pose | `HandStateHolding::Update` and three virtual functions of `Object` (below) | Two virtual calls for each new object in the Hand |

**The rule of the game for a hold pose [V].** `HandStateHolding::Update`
`0x005B3C70` has a switch on `CHand+0x490C`, the hold type of the object in
the Hand. The case of the type 1 starts at `0x005B4EA5`, of the type 3 at
`0x005B513C` and of the types 5, 6 and 7 at `0x005B533D`. Each object
gives three values:

| Virtual function of `Object` | Slot | What it gives |
| --- | --- | --- |
| `GetHoldType` | `+0x58C` | 1 for `Object` and `Poo`. 5 for `Tree` and `DeadTree`. 6 for `MobileObject` and `FieldCrop`. 7 for `Villager` and `Animal`. A `MobileStatic` gives 6 for some of its kinds (the info numbers `0x0E`, `0x0F` and `0x31` to `0x34`, and two more tests) and 1 for the others. A `SpellSeed` gives 2, or the value of its info at `+0x158` |
| `GetHoldRadius` | `+0x590` | `Object`: 0.75 times the height for the hold type 1, and the 2D radius for each other type. `Tree` and `DeadTree`: 0.2 times the 2D radius, which is the trunk. `SpellSeed`: a value of its info |
| `GetHoldLoweringMultiplier` | `+0x594` | The share of its height that the object hangs under the Hand: 0 for `Object`, 0.1 for a tree, 0.65 for a villager and an animal, 0.7 or -0.3 for a `MobileObject`, 0.7, 0.4 or 0 for a `MobileStatic` |

The state then takes one moment of one animation. `r` is the hold radius
over the length of the Hand (3.2 times `CHand+0x4834`, `fn_0046C040` gives
the 3.2), and 1 at most:

| Hold type | Animation | The time in the animation |
| --- | --- | --- |
| 1 | `hold_above` (6) | `duration / 2 * (1 - r)`, with `r = radius / (1.2 * length)` |
| 2 | `wiggle` (0) | `duration / 2`, and the object does not hang |
| 3 | `horn` (60) | `duration / 2 * r`, with `r = radius / length` |
| 5, 6, 7 | `hold_side` (9) | `duration / 2 * r`, with `r = radius / length` |

So the game does not play a hold animation. The animation is a scale from a
closed hand to an open hand, and the size of the object chooses the place on
it. `hold_above` has 4 frames and `hold_side` has 3, and both loop, so the
frame is `2 * (1 - r)` and `1.5 * r`. The logs of the desktop runs agree:
in the Holding state the game composed the animation 6 or the animation 9,
and no other one.

**The game's poses as curls.** `hand_model.cpp` bends a finger with one
curl. The table has the curl that is nearest to the bends of each frame
(least squares over the three bones of a finger), from the thumb to the
little finger. The helper is `hf_poses.py` in the scratchpad of the session.

| Animation and frame | Thumb | Index | Middle | Ring | Little | What it is |
| --- | --- | --- | --- | --- | --- | --- |
| `hold_above`, frame 0 | 0.05 | 0.01 | 0.01 | 0.03 | 0.03 | The open hand, for a large object |
| `hold_above`, frame 1 | 0.21 | 0.01 | 0.01 | 0.18 | 0.16 | |
| `hold_above`, frames 2 and 3 | 0.84 | 0.66 | 0.58 | 0.65 | 0.64 | The claw, for a small object |
| `hold_side`, frame 0 | 0.13 | 0.99 | 0.90 | 0.83 | 0.84 | The fist with the thumb out, for a thin object |
| `hold_side`, frames 1 and 2 | 0.00 | 0.00 | 0.03 | 0.11 | 0.11 | The open hand |
| `can_pickup`, frame 0 | 0.00 | 0.00 | 0.00 | 0.10 | 0.08 | The open hand |
| `can_pickup`, frame 1 | 0.06 | 0.00 | 0.12 | 0.19 | 0.21 | |
| `can_pickup`, frame 2 | 0.62 | 0.00 | 0.24 | 0.20 | 0.10 | The thumb at the index finger, the middle finger a hook |
| `wiggle`, both frames | 0.13 | 0.00 | 0.13 | 0.15 | 0.21 | The hand of the mouse player |

The thumb of frame 0 of `hold_above` and of frames 1 and 2 of `hold_side`
bends back from its rest pose, and the thumb of `can_pickup` turns at its
base by up to 32 degrees. The hand model has no such bend, so those thumbs
are the open thumb. The index finger of `can_pickup` is straight in each
frame: it points at the thing.

### What the mod does

`overlay.cpp` makes the curl of each finger from the pose of the hand, the
trigger and the grip, as before. Then `hand_feel.cpp` changes the curls. It
has four parts, and each one has its own key.

**The rules.**

- The place and the turn of a hand never change. Only a curl changes.
- The player's own finger wins. The world can add bend to a finger. It
  opens a finger only while the trigger or the grip of that finger is at
  0.15 or less.
- What the world adds goes through a filter with a time constant of 0.05 s.
  The curl of the pose has no filter, so the trigger and the grip show with
  no lag.
- A finger moves only when the hand moved or the world near it changed. A
  hand that is still over land that is still has still fingers.
- The parts work only while the God view or the table owns the view. In a
  menu, in a Temple room and in a cut scene the fingers have the curls of
  the pose.

**The finger tables.** For each finger the mod keeps the knuckle, the two
joints and the tip at 17 curls, in the hand frame. They come from the hand
model in use (`HandModelFingerJoints`), or from the low poly hand. A curl
between two rows is the mix of the rows, which is within 0.2 % of the hand
length of the true place. For the hand of a frame the tables go to the
world one time. Each test of a finger uses six points: the middle of each
of its three bones, the two joints and the tip. A point has 0.8 of the
radius of the finger. The tip has 0.5 cm of the room, which is the clearance
that the Hand's point keeps over the land.

**The free curl** (`FreeCurl`). This is the one rule for the land and for a
body. When the finger is in the world at the curl of its pose, the mod looks
for the nearest curl at which it is not:

1. The first free curl above the pose on the grid of the table, and with an
   open grip the first one below it. The nearer one wins. The side of the
   last frame wins when the other side is nearer by less than 0.10, so a
   finger between two answers does not go back and forth.
2. Five steps of a search between that curl and the last curl that is not
   free. So the finger rests on the surface.
3. When no curl is free, the curl with the least depth, if it is less deep
   than the pose by 10 % or more. If not, the curl of the pose stays.

**The land** (`hand_feel_land`). The land is `LH3DIsland::GetAltitude`, with
the sea at 0, as the land test of the Hand's point has it. For each hand the
mod reads five heights under the hand in each frame. When the lowest point
that a finger can reach is less than a quarter of a hand length over the
highest of them, it reads a grid of 8 by 8 heights under the hand (the land
patch), and each finger test takes its height from that grid.

- Fingers that go down into the land bend more, each one by its own length,
  until the tip rests on the land. On a slope each finger finds its own
  height.
- A hand with the palm down has fingers that open as it comes to the land,
  until the flat hand lies on it.
- A hand that is under the land has no free curl. The land hides it.
- The index finger of the pointing hand bends on the land too. Its tip is
  the Hand's point, and the land test keeps that point 0.5 cm over the
  land, so the glow and the tip of the bent finger are both on the land.

**The approach** (`hand_feel_approach`). While the grip has a target at the
Hand's point (the object with the halo, "One target"), and a press picks it
up or takes from it, the pointing hand takes the form of the target. The
weight is 0 at the reach of the pick (`hand_pick_cm`) and 1 when the point
is 1 cm from the body, with a smooth step between.

- The form. Each finger goes to 0.55 of the curl at which it holds a ball
  of the size of the target at the palm. So a small target (a villager)
  gives a claw with the thumb at the index finger, and a large one (a tree)
  an open hand with a little bend. The size is the larger of the radius and
  half the height of the body, and 0.10 hand lengths at least.
- The game's own pose `can_pickup` is the least curl of each finger. The
  size of the target chooses its frame: a target of 0.30 hand lengths or
  more gets the open hand of frame 0, and a small one goes toward frame 2.
  The first build had only this pose. Its curls are near the curls of a
  hand at rest (11 0 13 19 20 percent against 0 12 12 12 12 at a rock), and
  the pictures of run hf6 showed no change of the hand. A real hand that
  reaches for a thing takes the form of the thing before it closes.
- The index finger keeps its curl, because its tip is the Hand's point.
- A grip that closes takes each finger from the form to the curl of the
  grip: the share of the approach is 1 with the grip open and 0 from a grip
  of 0.5 on.
- A target that a press does not act on (the red halo) and an object that a
  press taps do not change the hand.

**The hold** (`hand_feel_hold`). The game's rule gives the kind of the hold,
and the contact gives the curl.

- For an object that the game holds from the side (the hold types 5, 6 and
  7: a tree, a villager, an animal, a mobile object), the body is a capsule
  on the axis of the object, at the place where `hand_exact.cpp` put the
  object in the frame before, with the game's hold radius. For a tree that
  is the trunk. A finger closes from the open hand until it touches the
  capsule.
- A finger that cannot touch the capsule, and each finger for each other
  object, stops on the ball that `hand_exact.cpp` puts at the palm, as in
  the builds before.
- A finger that can touch neither takes the curl of the game's own hold for
  the hold type and the hold radius.
- From the side the thumb does not close more than the thumb of the game's
  `hold_side`: the fist of the game with the thumb out.
- A body is never thinner than 0.03 hand lengths. A capsule that is short
  too (a villager) counts as 0.07, so the hand is a loose cup and the player
  sees what is in it.

The pose is made again only when the body moved in the hand frame.

**A tree at the palm** (`hand_hold_trunk`, in `hand_exact.cpp`). The place
of an object in the hand comes from a ball that touches the palm ("The
exact Hand"). Its radius was the 2D radius of the object, and for a tree
that is the radius of the crown. So the trunk of a tree with a crown of 3.5
units was 3.5 cm in front of the palm, and the fingers were a cup under it.
The Hand holds a tree near its base. So a tree (the hold type 5) now has the
game's own hold radius, 0.2 times the 2D radius, which is the trunk
[V `Tree::GetHoldRadius` `0x0074B610`]. In run hf8 the ball of a tree went
from 3.51 to 0.70 units, and the trunk lies on the palm. Each other object
keeps its 2D radius: a rock that is wide and flat must not go into the
palm.

**The bodies** (`hand_feel_bodies`). At the end of the world of the first
eye pass, the mod takes the bodies near each hand from the candidates of the
pick. The fingers of the next frame use them.

| Object | Body |
| --- | --- |
| A tree | The trunk, a cylinder of a quarter of the radius to half the height, and the crown, a spheroid in the upper half |
| A Creature | The capsule of the choice of the grip |
| An object with a mesh box (a house, a rock) | The box of its mesh, with the turn of the object |
| Each other object | The cylinder of the choice of the grip |

These do not count: the object in the Hand, a scroll (the hand goes into it
to tap it), a forest (one object with many trees), a cylinder or a box with
a radius of more than 0.8 hand lengths (the Temple, a store), and a body
that the palm of the hand is in. While the pointing hand strokes the
Creature, it does not bend against a body. The index finger of the pointing
hand does not bend against a body at any time: its tip is the Hand's point,
and that point goes into a body to choose it. When the hand is more than 60
units long (the table), a finger is wider than a house, and no body counts.

**The pulse** (`hand_feel_pulse`). One pulse of the kind `surface` (0.06 for
8 ms, the faintest of the set) when a finger of a hand is in the land or in
a body and none was for 0.2 s. Half-Life: Alyx allows one pulse of a hit in
0.2 s too.

**The idle wave** (`hand_feel_idle`, off). A relaxed hand moves its fingers
a little: a wave of 4 s that adds 0.05 of curl at most. The two frames of
the game's `wiggle` have the same finger bends, so the game's Hand has no
such wave in its file, and this one is the mod's own. It is motion that the
player did not make, so it is off. Buck decides.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hand_feel` | 1 | 0 gives the fingers of the builds before: the curls of the pose only |
| `hand_feel_land` | 1 | The fingers bend on the land |
| `hand_feel_approach` | 1 | The pointing hand opens toward a target of the grip |
| `hand_feel_hold` | 1 | The fingers close on the body of the object in the Hand |
| `hand_feel_bodies` | 0 (built, and off until a desktop run proves it) | The fingers bend against a body near the hand |
| `hand_feel_pulse` | 1 | A faint pulse when a finger first meets the land or a body |
| `hand_hold_trunk` | 1 | A tree in the Hand has its trunk at the palm. 0 puts it at the radius of its crown, as in the builds before |
| `hand_feel_idle` | 0 | The idle wave of a relaxed hand |
| `hand_feel_test_toggle` | | Test key. Frame numbers. At each one hand feel goes off or on, as the test key of the keyboard does |
| `hand_feel_log` | 0 | Test key. A log line for each hand each so many frames |

The keys are read from `bwvr.ini` at the start. They are not in the
launcher. With `test_keys=1`, **Ctrl, Shift and the minus key of the number
pad** turn hand feel off and on while the game runs. The pointing controller
gives a long pulse for on and a short pulse for off.

### The log

The tag `hand feel`.

- At the start: which parts are on.
- One line when the fingers of a hand go into the land or into a body, and
  one when they are free again: the curls of the pose, the curls that the
  world asks for, the height of the hand over the land, the count of bodies
  near, the trigger and the grip. At most one line for each hand in 45
  frames, and 60 lines in all.
- One line when the pointing hand starts to take the form of a target: the
  size of the target, the frame of `can_pickup`, the curls of the form, the
  gap and the weight.
- One line for each new object in the Hand, and one more when its body
  becomes the capsule: its class, the hold type of the game, the hold
  radius, the game's own curls, the body in the hand, the hold curls and
  what each finger stops on.
- Each press of the test key.
- Once a report window: the frames, how often the land and a body were near
  a hand, the hand frames with a finger in the land and in a body, the
  frames with no free curl, the frames of the approach, the hold poses, the
  pulses, the time on the CPU and the land heights read in a frame.

### Tests

**The math test** has 102 new checks (`feel_checks.h`). The
test has no game: the hand is the low poly hand, 19 units long, and the land
is a function.

- The tables: a curl on a row gives the points of the hand model, a curl
  between two rows is within 0.2 % of the hand length, and a point of the
  world goes back to its place in the hand frame, for both hands. The land
  patch gives the height of a slope between its points.
- The free curl: a free finger keeps its curl. A finger in the world opens
  or bends to the nearer free curl, and bends only when the grip is
  pressed. The side of the last frame holds. With no free curl the finger
  takes the curl with the least depth, or keeps the curl of its pose.
- The land: palm down on flat land the finger opens, and its tip rests 0.5
  cm over the land. With the grip pressed it keeps its curl. A hand that
  lies on the land has flat fingers. Fingers down into the land bend more,
  a hand 1 cm lower bends them more, the little finger bends less than the
  middle finger, and on a slope the finger on the high side bends more.
- The bodies: the gap to a cylinder, a capsule, a ball, a tree and the box
  of a house. A finger bends against the wall of a house and rests on it. A
  house 20 cm away does not move it. The Temple does not count.
- The filter, the pulse and the idle wave. The approach: the weight by the
  gap, the frame by the size, and a grip that closes. The hold: the game's
  curls for each hold type and size, the least radius of a body, the ball
  at the palm against the contact curl of the builds before, a trunk across
  the palm, a trunk that no finger can reach, and a finger that is in the
  body.

**Desktop runs** (2026-10-04, the fake controller,
`fake_controller_front=1`, a render of 2880 by 2880, the game of "Keep your
old Creature", 100 units a meter). The pictures are in the run folders of
the scratchpad.

- **hf2, the land.** Both hands go down beside the Temple in steps, with
  the fingers down. The log gives the curls in percent, from the index
  finger to the little finger. The height is that of the middle of the
  reach of the hand over the land.

  | Height, left and right | Left hand | Right hand (pointing) |
  | --- | --- | --- |
  | 11.8 and 11.6 cm | 12 12 12 12 | the pose `point` |
  | 7.8 and 7.6 cm | 12 12 12 12 | 12 12 12 12 |
  | 4.8 and 4.6 cm | 33 31 17 12 | 33 31 19 12 |
  | 1.8 and 1.6 cm | 94 79 47 23 | 97 83 53 25 |
  | -1.2 and -1.4 cm | 100 100 100 91, the thumb 53 | 100 100 100 100, the thumb 53 |
  | The same, hand feel off | 12 12 12 12 | 12 12 12 12 |

  `desk-hf2\land_left_hand.png` shows the left hand at the last five rows:
  the fingers at rest, the first bend, the fingers bent on the land, the
  fist with its knuckles on the land, and with hand feel off the fingers
  gone into the land. So each finger bends by its own length, the longer
  ones first.
- **hf2, a slope.** 30 cm more to each side the land under the left hand
  has a slope of 0.33 and under the right hand of 0.04. At 1.7 cm the left
  hand had 88 69 38 12, and the right hand at -0.4 cm had 100 100 95 55. So
  on the slope each finger has its own curl.
- **hf2, the right hand on its side** (the fake hand turned by a pitch of 90
  degrees, then -90 and 45). At 2.9 cm only the ring finger and the little
  finger bent (12 12 100 100). With the thumb side down the thumb and the
  index finger bent (100 100 12 12 12).
- **hf11, the palm down on the land** (2026-10-05, a roll of -90 degrees of
  the fake hand). The right hand went down beside the Temple in steps. It
  had the pose `point`, because the aim had a far target, so its middle,
  ring and little fingers were at 88 90 92, under the palm. At the first
  step only the thumb was in the land, and it bent from 85 to 100. 2 cm
  lower the middle and the ring finger opened to 7 and 7. 1 cm lower the
  three fingers were at 2 2 1, and 1 cm lower at 0 0 0. With hand feel off
  they were at 88 90 92 again. `desk-hf11\palm_down.png` shows the row: the
  fingers under the palm, then three steps with the fingers flat on the
  land, then with hand feel off the fingers gone into the land. So a hand
  that comes down on its palm lies flat.
- **hf10, the palm up** (a roll of 90 degrees). With the back of the hand
  on the land the fingers bent up, out of the land: 0 100 94 81 75.
- **The pulse.** The log has one pulse of the kind `surface` for each hand
  at its first contact (frames 981 and 983), one at the next contact of the
  left hand (frame 1383), and one when hand feel came on again with the
  fingers in the land (frames 1270 and 1590). It has none while a hand
  stayed on the land.
- **The cost.** With both hands in the air hand feel took 0.0006 ms a
  frame on the CPU, and 10 land heights a frame when both hands show. With
  both hands on the land it took 0.011 to 0.014 ms a frame (the worst frame
  0.12 ms) and read 120 to 138 land heights a frame. In the two last report
  windows of the run, with both hands on the land and no capture,
  `GGame::ProcessGraphicsEngine` took 11.4 and 11.7 ms and
  `GGame::FlipScreen` 11.7 and 11.9 ms. So hand feel is less than 0.1 % of
  a frame. Run hf1, with the bodies on, had 54 to 111 candidates a frame
  and at most 0.0175 ms. No run measured the frame with `hand_feel=0`: the
  time of a frame changes by some ms with the view, which is much more than
  hand feel takes.

- **hf8, the approach and the hold** (2026-10-05, `hand_feel_approach=1`
  and `hand_feel_hold=1`). For each object the view went to a place where
  the object is under the right hand (the test words `go`, `hold` and
  `crown` of `seek_plan`, with `seek_class` for the class). Each picture is
  cut round the tip of the index finger, from the pixel that the test log
  of hand feel gives (`hf_crop2.py`), so the hand is in the middle. The
  curls are in percent, from the thumb to the little finger. The pictures
  are `desk-hf8\hand_<frame>.png`.

  | What | Hand feel on | Hand feel off | Pictures |
  | --- | --- | --- | --- |
  | The tip at a `Rock` (radius 2.62, half a height of 0.89 units, 0.14 hand lengths) | 35 12 36 31 33: a claw | 0 12 12 12 12 | 00926, 00966 |
  | That rock in the Hand (the hold type 1, `hold_above`, the ball at the palm) | 63 62 66 56 60 | 63 60 65 55 58 | 01153, 01193 |
  | The tip at the crown of a `Tree` (radius 3.51, height 12.18 units, 0.32 hand lengths) | 0 12 24 21 21: an open hand with a little bend | 0 12 12 12 12 | 01526, 01566 |
  | The tip 4 cm and 8 cm over that point | 0 12 23 20 20, then 0 12 12 12 12 (no target) | | 01606, 01626 |
  | That tree in the Hand (the hold type 5, `hold_side`, the trunk at the palm) | 12 70 83 63 80: the thumb out | 78 75 73 63 100 | 01753, 01793, 01853 |
  | The tip at a `Villager` (radius 0.26 units, 0.03 hand lengths) | 51 12 40 35 12: the thumb comes to the index finger | 0 12 12 12 12 | 02126, 02166, 02206 |

  The pictures show the same: at the rock the middle, ring and little
  fingers and the thumb bend toward the rock, and with hand feel off they
  are straight. At the villager the thumb comes to the index finger. The
  rock in the Hand looks the same with hand feel on and off, as it must: a
  rock has the ball of the builds before. The tree lies with its trunk on
  the palm and its roots at the knuckles, and with hand feel on the thumb
  is out.
- **An object that a press taps.** In run hf6, 3 cm over the rock, the
  target was a large rock beside it (radius 7.37 units), which the grip
  taps and does not take. In run hf8, 4 cm over the rock, the hand kept
  the curls of its pose.
- **hf6 and hf7, what the pictures corrected.** In run hf6 the approach was
  the game's `can_pickup` only, and the pictures showed no change of the
  hand. In runs hf6 and hf7 the trunk of a held tree was at the radius of
  its crown from the palm. Run hf7 has the form of the approach, and run
  hf8 the trunk at the palm.
- **hf3 and hf4, more objects in the Hand.** A `Pig` (the hold type 7): 12
  70 80 70 80, and 78 75 73 63 100 with hand feel off. A `MobileObject`
  (the hold type 6): 13 69 76 67 81, with the index finger on the capsule.
  For each one the game composed its animation 9, `hold_side`, and for the
  rock its animation 6, `hold_above`.
- **hf9, a villager in the Hand.** The grip took a `Villager` (the hold
  type 7, the hold radius 0.29 and the height 1.27 units), two times. The
  curls were 13 73 80 69 82, and with hand feel off 78 75 73 63 100. The
  pictures `desk-hf9\hand_01103.png` (on), `hand_01143.png` (off) and
  `hand_01203.png` (the wrist turned) show the villager on the palm, with
  no finger in it. With hand feel on the thumb is out. A villager walks: in
  run hf7 the grip came 30 frames after the Hand's point, the villager had
  gone, and the grip took food from the store. In run hf9 the grip comes 4
  frames after the point.
- **The table above is the state at rest.** The picture of the rock with
  hand feel on is 3 frames after the hand came, so its fingers are still on
  the way from the pose `point` (40 5 44 42 43 in the log).
- **The bodies** (`hand_feel_bodies=1`, runs hf1, hf3 and hf4). In run hf1
  the body of the Temple was a cylinder with no limit of size, and two
  fingers of a hand in the air beside the dome bent to 26 percent. With the
  limit of 0.8 hand lengths (run hf3) the same hand had 0 bodies near. In
  run hf4 the middle finger bent to 24 and to 33 percent against the crown
  of a tree, and in run hf10 to 30 percent. The key stays at 0, for
  three reasons:
  1. The Creature. In run hf10 the Hand's point was in the body of the
     Creature of the profile. That Creature is 2 cm across, so no finger
     beside the index finger is in its capsule, and the hand did not
     change. A run needs a grown Creature.
  2. A house. In runs hf10 and hf11 the Hand's point was in a house with a
     radius of 4.3 units. The test log has the box of the house 3.3 units
     from the middle of the hand. At two visits only the land bent a
     finger there (21 percent), and at one visit the same finger was in a
     body, with the same curl. So the box of the mesh is not where the
     point of the object is, and no picture shows where it is against the
     house that the player sees: the hand was at the edge of the picture.
  3. A tree. The crown is a spheroid, and the pictures of run hf10 are too
     small to show one finger at 30 percent against it.
- **The cost with each part on.** At the trees, with 176 candidates a
  frame, the approach and a finger in a body: 0.029 ms a frame, and the
  worst frame 0.13 ms.

**Not proven on the desktop:** a real press of the test key (the ini test
key ran the same code), and what the pulse feels like. For the approach: a
row of pictures with the weight between 0 and 1 at a small target. The step
of 4 cm over the rock found a second target, and a villager walks, so the
math test and the tree (23 20 20 at 4 cm, against 24 21 21 at the tree) have
that. For the bodies, which are off: a finger against a house, against a
trunk and against a grown Creature, and each picture.

### Limits

- The hand does not stop at the land or at a body. A hand that the player
  pushes into the land goes into it, with its fingers bent, and the land
  hides it.
- A body is a cheap form. The box of a house has no roof slope, the crown of
  a tree is a spheroid, and a Creature is a capsule. A finger can stop a
  little before the thing that the player sees, or go a little into it.
- A forest, a scroll, and a building that is larger than the hand (the
  Temple, a store) do not bend a finger.
- The land patch has a point each 3.3 units for a hand of 19 units. The
  land of the game has a point each 10 units, so the patch is fine enough
  at 100 units a meter. On the table the hand is much longer than a land
  cell, and a finger can go through a thin ridge.
- The bodies are those of the frame before, so a finger is one frame late
  against a thing that moves.
- The thumb has one curl. It bends on the land and against a body as a
  finger does, but it does not go round an object.

**Needs the headset.**

1. Put a hand down on the land with the fingers first, slowly. The fingers
   must bend as they meet the land, the longer ones first, with no jump.
   Say if a finger shakes when the hand is still.
2. Lay a flat hand on the land, palm down. The fingers must open and lie on
   it.
3. Do the same on a slope and across a ridge.
4. Press the trigger and the grip with the hand on the land. Your own
   fingers must still show.
5. Say if the pulse at the first touch is too weak, too strong or too
   often (`hand_feel_pulse=0` turns it off, and `haptics_pct` scales it).
6. With `test_keys=1`: Ctrl, Shift and the minus key of the number pad for
   the fingers of the build before, and back.
7. Bring the tip of the index finger to a rock, a villager and a tree. When
   the halo shows, the hand must take the form of the thing: a claw for a
   small thing, with the thumb at the index finger, and an open hand for a
   tree. Say if the form looks right, and if it comes too late or too
   early (`hand_feel_approach=0` turns it off).
8. Take a rock, a villager and a tree. Say if a finger goes into the object
   or stays far from it, and if the thumb that stays out for a villager and
   a tree looks right (`hand_feel_hold=0` gives the hold of the build
   before).
9. Take a tree with the palm to the side, as you take a pole. The trunk
   must lie in the hand, and the fingers must close on it. Say if the tree
   sits wrong (`hand_hold_trunk=0` puts it where it was).
10. With `hand_feel_bodies=1`: move a hand against a house, a tree and the
    Creature. The fingers must bend on them. Say if a finger bends in the
    air beside a thing.
11. With `hand_feel_idle=1`: say if you want the slow wave of the fingers
    of a hand at rest. It is motion that you did not make.

## An object in the Hand that goes onto a thing (2026-10-05)

The core loop of a village is to carry a thing to a place: wood and food to
the Village Store, wood to a building site, a scaffold to a second scaffold.
With an object in the Hand the pick record had the object behind the pixel of
the Hand's point, and not the thing at the Hand. The first correction was for
a scaffold on a scaffold only ("The village with the real hand"). This
section has the rule of the game for each class, and the correction for each
of them. The code is `VillageSelect`, `VillageKeepRecord` and `VillageHalo`
in `src/mod/village.cpp`, with the pure rules in `village_math.h`.

### What the game does [V]

**The press.** A press of the Action button with an object in the Hand goes
to `FUN_005D1560`. It does these tests in this order:

1. The thing is `CHand::GetCreatureToGiveTo` `0x0046E4B0`, or else the object
   of the next pick record (`GInterface+0x400`). With no thing the press
   sets the object down on the land point of the record.
2. The thing must be a target (`ValidAsInterfaceTarget`, slot `0x6F0`).
3. A Creature that takes the object (`InterfaceValidToGiveObject`, slot
   `0x748`, only `Creature` at `0x0047A330`): `fn_005D3080` sends packet
   `0x1F`, and the Creature takes the object.
4. A thing that needs the influence (slot `0x714`) with the land point of the
   record outside the influence: the press sets the object down.
5. The object in the Hand says yes to the thing
   (`ValidToApplyThisToObject`, slot `0x71C`, the status and the thing):
   `fn_005D30D0` sends packets `0x4D` and `0x11`, and the object goes onto
   the thing. Else the press sets the object down.

**The Creature that takes an object.** `HandStateHolding::Update` reads the
object of the pick record of now (`GInterface+0x3C8`). When it is a Creature
that says yes to the object in the Hand, the Hand keeps it at `CHand+0x49B4`
for 1100 ms (the timer at `0x00D13F3C`). So the record must hold the
Creature before the press.

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
| A miracle | `SpellSeed` `0x007286D0` | See "A miracle in the Hand" |
| Each other object | `Object` `0x004028B0` | Nothing |

**The stores of a resource** (`IsResourceStore`, slot `0x680`, with 0 for
food and 1 for wood):

| Thing | Function | It stores |
| --- | --- | --- |
| The Village Store (`StoragePit`) | `0x0055CD20` | Food and wood |
| Each building (`MultiMapFixed`) | `0x0052F1F0` | Wood, while it has a building site (`+0x74`): it is not complete, or it needs a repair |
| The Workshop | `0x0077A650` | Wood, always |
| The Worship Site | `0x0077DEC0` | Food, always (the food of the worshippers) |
| A scaffold | `0x006EAEA0` | Wood, when it is planted (it has a building site) |
| A tree, a rock, a pot | | Nothing |

**No rule.** Food has no villager as its thing: a villager is no store. The
player sets food down on the land, or puts it in the store. A tree and a rock
that go onto no thing go on the land.

**A villager and a job.** The job does not use the press. While the Hand
carries a villager, `Villager::InHand` `0x0076ADE0` asks each object near the
place of the villager for a job (`GetDiscipleStateIfInteractedWith`, slot
`0x644`), once a game turn:

| Thing | Job |
| --- | --- |
| A tree of the player (`Tree` `0x0074C150`) | 2, the forester |
| A field (`Field` `0x00529FB0`) | 1, the farmer. 4 while the field is not complete |
| A building that is not complete (`MultiMapFixed` `0x0052F0D0`) | 4, the builder |
| The Workshop (`0x0077A340`) | 8, the craftsman |
| The Worship Site of his town (`0x0077DE70`) | 11, the worshipper |
| A villager (`0x007560E0`) | 5, the breeder [R, run vilT6] |
| The complete Village Store of his own town (`0x00733A20`) | None [R, run vilT] |

The numbers are the values that the functions give. The names are those of
the game's list of disciples, and the runs fit them.

A thing counts when it is within 6 units of the villager
(`Villager::FindCloseObjectsForInteract` `0x00756E80`). So the Hand must have
the villager low at the thing: in run vilT6 the Hand was 2 units above the
base of the tree. The map point of the villager is the place of the Hand
(run vilT5). The game gives a job other than 11 only to a villager that
passes its tests (`0x0076AE6D` to `0x0076AEBC`: two flags and four slots of
the villager, not traced). One villager of the test town does not pass, and
he got no job at the base of a tree (run vilT5). That is the rule of the
plain game too.

### The fault

With `hold_target=0` the runs show it on the build that has the key.

- **Wood at the Village Store (run vilP0).** The grip collected 368 wood
  from the pile of the store. With the wood 3 units above the point of the
  store the grip closed and opened. At the press the pick record held a
  `Villager` who stood behind the pixel of the Hand's point. The game set
  the wood down on the land beside the store. The hint of the game went
  between "Supply" and "Drop" before, with the object that the pixel was on.
- **A scaffold at a scaffold (runs vilK and vilQ0).** The record held the
  `CitadelHeart`, the Temple behind the scaffolds. The game set the scaffold
  down beside the second one.

### What the mod does (`hold_target=1`)

`VillageSelect` runs at the end of the first eye pass while the Hand carries
an object (state 4). It does not run for a miracle, which has its own target,
and not while the game takes from a bulk source, which must stay in the
record. For each object that the draw offered it does the tests of the press:
the object is a target, and it is a Creature that takes the object in the
Hand, or the object in the Hand says yes to it.

**The thing at the Hand.** The body of a thing is the body that the choice of
the grip uses (`Measure` in `hand_direct.cpp`): a cylinder of its radius and
its height, or the capsule of a Creature. The gap is 0 in the body. The mod
takes the smaller of two gaps: from the Hand's point, and from the place of
the object in the Hand. The target is the thing with the smallest gap within
`hold_target_cm` (6 cm of the room, `village_math::HoldTakes`).

**No reach for a sacrifice and for a Creature** (`HoldReach`). The altar of a
Worship Site takes a villager, an animal and a tree as a sacrifice, and a
Creature takes most objects. For these two the Hand's point, or the object in
the Hand, must be in the body. So a villager that the player sets down
beside the altar is no sacrifice, and a Creature that stands near the Hand
does not take the wood.

**The record, the halo and the card.** `VillageKeepRecord` puts the target in
the next pick record after each eye pass, before each interface step and
before each press. The target has the gold halo of a target of the grip
(`VillageHalo`, `kHaloHold`). The halo is red when the thing needs the
influence and the land under the Hand is outside it. The card has one line
for the grip:

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

The line of a job needs no target: the game sets the job, and the mod reads
it from the villager (`Villager+0xF2`). Outside the influence the card adds
"This place is outside your influence".
With no target the record, the hint of the game and the card are as before.
A bundle that the Hand holds with the grip open goes onto the target at the
next close and opening of the grip ("The bulk take").

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hold_target` | 1 | 0: the builds to 2026-10-04. The pick record of an object in the Hand is the object behind the pixel of the Hand's point |
| `hold_target_cm` | 6 | The reach of a thing, from the Hand's point or from the object in the Hand to its body, in cm of the room. 1 to 50 |

The keys replace `scaffold_join` and `scaffold_join_cm` of the build of
2026-10-04. The old names still count when the new keys are not in the file.
The keys are not in the launcher.

**The A/B key**, with `test_keys=1`: Ctrl with the 2 key of the number pad
changes `hold_target`. The pointing controller gives a long pulse for 1 and a
short pulse for 0.

The word `seek` has new places in `seek_plan` (test only): `gostore` (the God
view teleports to 60 cm of the room before the Village Store), `store` (the
Hand's point goes 3 units above the point of the store), `pile` (the nearest
bulk source), `villager` (the nearest plain villager that the Hand can take
and that passes the tests of the game for a job), `treebase` (2 units above
the base of the nearest tree), and `goshop` and `shop` for the Workshop. The
groups of `seek_go` can have a bar between them, because a list of keys in a
script has the semicolon between its keys. The test key
`village_test_workshop` is a frame: the first town of the player gets a
complete Workshop on the land under the Hand (`Abode::Create` `0x00402E20`
with the info of a Workshop of its tribe, as the setup of a land does).

### The log

The tag `village`:

- The key at the start, and each press of the A/B key.
- "the ... in the Hand can go on the ...", with the gap, the reach and the
  line of the card. "... has no thing to go on now".
- "the game puts the ... in the Hand on the ... (packets 0x4D and 0x11)",
  from the hook of `fn_005D30D0`. It says whether the thing had the halo, or
  whether it was the object of the game's own pick.
- "the villager ... in the Hand has the job N now", for each change, and
  "the villager ... left the Hand with the job N".
- "the game has a place for a building for the scaffold ... in the Hand", and
  "... no place ...", for each change.
- Each report window: the frames with a target, the times the mod put it in
  the record, and the times the game put an object on a thing.

The tag `hand press` has "the grip pressed with ... in the Hand. The pick
record holds ...", as before.

### Tests

**The math test** (`village_checks.h`): the choice with the two gaps, the
reach, no reach for the altar and for a Creature, and the line of the card
for each pair of the tables above.

**Desktop runs** (`village_run.ps1`, 2026-10-05, the God view, a render of
2880 by 2880). The Village Store of the test town has 20000 food and 30000
wood.

- **vilP0, the fault** (`hold_target=0`): see above.
- **vilP1, wood at the Village Store.** The same steps. When the take ended:
  "the PileWood ... in the Hand can go on the StoragePit ...: its body is 0.0
  cm of the room away", the halo, and the card "[Grip] Open to store the
  wood". At the press the record held the `StoragePit`, and "the game puts
  the PileWood ... on the StoragePit ... (packets 0x4D and 0x11), the thing
  with the halo".
- **vilQ0, two scaffolds** (`hold_target=0`): see above.
- **vilQ1, two scaffolds.** "the Scaffold ... in the Hand can go on the
  Scaffold ...", the card "[Grip] Open to join the scaffolds", "the game
  puts the Scaffold ... on the Scaffold ...", and the census has one
  scaffold of 2. A press of 3 frames takes it apart. Later the Creature of
  the player came to the Hand, and the scaffold had the Creature as its
  target at 1.6 cm. That gave the rule of no reach for a Creature.
- **vilT, a villager at the Village Store.** The grip took a villager and
  carried him to the point of the store for 125 frames. His job stayed 0,
  and the opening set him down. That is the rule of the game: the complete
  store of his own town gives no job.
- **vilT6 and vilT7, a villager at a tree.** The grip took a plain villager
  that passes the tests of the game. Near a second villager his job went to
  5 after 8 to 11 frames, and the card said "[Grip] Open to make a breeder".
  With the Hand 2 units above the base of a tree his job went to 2 after 8
  to 13 frames: "[Grip] Open to make a forester". The grip opened: "the
  villager ... left the Hand with the job 2".
- **vilW, wood at the Workshop.** The test key made a Workshop ("The town
  now has 1 Workshops"). The grip collected 368 wood at the Village Store,
  and the God view went to the Workshop: "the PileWood ... in the Hand can go
  on the Workshop ...", the card "[Grip] Open to give the wood to the
  Workshop". At the press the record held the Workshop, and "the game puts
  the PileWood ... on the Workshop". The hint of the game at the Workshop
  then said "Wood Required: 686". With 2306 wood (run vilW2) it said "Wood
  Required: 0" and "Building Completed: 70%", and the Workshop had a
  building site. No scaffold came in the rest of the run.
- **vilX, a building site and wood for it.** The grip took a scaffold, and
  the God view went to open land: "the game has a place for a building for
  the scaffold ... in the Hand" (in run vilU the hint of the game said
  "Abode" there). The grip opened: "The game sets the object down", and the
  scaffold had a building site at the next census. Then 368 wood from
  the Village Store: at the place of the building "the PileWood ... in the
  Hand can go on the Abode ...", and at the scaffold "... can go on the
  Scaffold ...", both with "[Grip] Open to give the wood to the builders".
  At the press the record held the scaffold, and "the game puts the PileWood
  ... on the Scaffold ... (packets 0x4D and 0x11), the thing with the halo".

**Not run on the desktop:**

- Food, a tree, an animal and a crop at a store. They use the same code with
  a different class.
- The altar and a sacrifice, a teleport, and an object given to a Creature.
  The rule of no reach for a Creature is not run.
- A thing outside the influence (the red halo and its line).
- A villager at a field, at a building site and at the Workshop.
- A scaffold that the Workshop makes: the villagers must build it first.

**Needs the headset.** Whether 6 cm is the right reach for a store and a
scaffold. Whether the halo of a large thing (the Village Store has a body of
15 units, 35 cm of the room) reads as "the wood goes here". Whether a player
gives an object to the Creature with no effort when the object must be in
its body.

**Limits.** The draw offers a thing only when the point of its mesh is near
the Hand. A very large building with its point far from the Hand's point can
be no target, and then the game's pick at the pixel acts as before. The mod
does not change where an object lands when no thing takes it.

## The door of the Temple and the leash posts (2026-10-05)

The lead asked for a tap of the real hand on the door of the Temple and on
the three leash posts at the Temple, with the halo and a card, and for the
fault first. The inside of the Temple (the ray in a room, the Save Game Room,
the turn with the stick) is in controllers.md, section 11.

The code is `temple.cpp` (the hooks, the log, the card lines, the test
places) and `hand_direct.cpp` (`HandDirectOffer`, `BallOf`, the words `door`
and `post` of `seek_plan`).

### What the game does [V]

- **The door** is a `CitadelEntrance`, at `CitadelHeart + 0xDC`. It has a
  mesh for the collision only (`LH3DObject::Create`, `0x004676CD`), and the
  draw of the Temple offers it to the pick as each object
  (`CitadelHeart::DrawNow` `0x004670D0`).
  `CitadelEntrance::InterfaceValidToTap` `0x00468F50` says yes in a
  multiplayer game. In a single player game it says yes when the story has
  the door on (`*(g_game + 0x250090) + 0xA0`, set by
  `GScript::SetCitadelInteract` `0x0070B9A0`).
  `CitadelEntrance::InterfaceTap` `0x00468EF0` calls `GoInsideCitadel(0)`
  when the Temple belongs to the local player.
- **The three posts** are `LeashObj` objects with no mesh. `TempleLeash` at
  `CitadelHeart + 0xE4` holds them at `+4`, `+8` and `+0xC`
  (`CitadelHeart::CreateLeashes` `0x00464950`). `fn_00466730`, from
  `CitadelHeart::DrawNow`, gives each one to the pick as a ball of 1 unit
  (`GInterface::SendInvisibleDrawCollision` `0x00519960`, cdecl: the object,
  the point, the radius). It does that only when the player of the Temple
  has `+0xA4C` set and the Temple is at its full size.
- `LeashObj::InterfaceValidToTap` `0x00464450` says yes for the posts of the
  local player. `LeashObj::InterfaceTap` `0x00464490`: the number of the
  post is at `+0x54`, and `+0x58` points at the number of the chosen post. A
  tap on the chosen post sets -1 and sends nothing. A tap on a different
  post plays a sound, makes it the chosen one and sends packet `0x65` with
  the kind 1, 2 or 3 (the Leash of Aggression, of Learning, of Compassion).

### The fault [R]

Run tland1 on the build of before, with the Hand's point exactly at a post
or at the door (`seek_plan`), and a short press of the grip:

- **At a post:** "the grip pressed with no target at the Hand's point. The
  pick record holds 00000000 (nothing)." The choice of the real hand takes
  its candidates from `GInterface::SendObjectDrawCollision`, and a post
  never comes there.
- **At the door:** "the target is 0F8736A4 (CitadelEntrance): the grip taps
  it", with the halo, and the press put the player in the hall of the Temple
  3 frames later. So the door had no fault. Its body is the cylinder of the
  game: a radius of 3.25 and a height of 6.41 units.

At the scale of the start of that run (about 250 units a meter) the door is
2.6 cm wide and the posts are 1.4 cm from each other, 8 cm from the door. A
player who zooms in gets them larger.

### What the mod does (`temple_taps=1`)

A hook on `fn_00466730` knows the Temple of the local player while its draw
runs, and a hook on `GInterface::SendInvisibleDrawCollision` sees each post
with its point and its radius. `HandDirectOffer` makes the post a candidate
of the choice, and its body is that ball about that point (`BallOf`, in
`BodyOf` and in `Measure`). The rest is the path of each object that a
press taps: the nearest body in the reach of 4 cm is the target, it has the
halo, the pick record holds it, and the game taps it with its own tests and
its own `InterfaceTap`.

**The card** has the context "the door of the Temple or a leash post". The
game has its own hint for the grip at these things, and a hint of the game
for a button wins over a line of the mod for the same button
(controller-hints.md). So with the hints of the game on, the card has the
line of the game. The lines below show when the game gives no hint for the
grip there:

| The Hand's point is at | Line |
| --- | --- |
| The door of the Temple | "[Grip] Tap to go into the Temple" |
| A post | "[Grip] Tap for the Leash of Learning" (or Aggression, Compassion) |
| The chosen post | "The Leash of Learning is chosen" |

`temple_taps=0` gives the build of before: a post is no target.

### The log

```
temple land  : the Temple of the player is in the draw. The door is at 1915.05 29.48 2508.89. The story has the door on. The leash posts are at 1905.02 34.83 2525.41, 1902.88 34.86 2528.11 and 1899.45 34.75 2530.72, each with a ball of 1.0 units. The chosen post is -1. temple_taps is on, frame 604
temple land  : the Hand's point is at the leash post 1, the Leash of Learning (1902.88 34.86 2528.11). The game says that a press taps it. The chosen post is -1, frame 1043
temple land  : the chosen leash post is 1 now (the Leash of Learning). It was -1, frame 1105
temple land  : the Hand's point is at the door of the Temple (1915.05 29.48 2508.89): the grip taps it, frame 1473
temple land  : the story turns the door of the Temple on: a tap goes into the Temple, frame 5120
```

The first line also says "The story has the door off: a tap does nothing
yet" when that is so. The lines of each press are the lines of
`hand press`.

### Tests

Desktop runs of 2026-10-05 (the God view, `fake_controller=2`, Land 1, the
choice "Keep your old Creature", `temple_run.ps1 -NoEnter`). The words
`door` and `post` of `seek_plan` put the Hand's point at the door and at the
next post. `temple_test_taps_frame=N` keeps `temple_taps` at 0 before frame
N, for the fault and the fix in one run.

| Run | What it showed |
| --- | --- |
| tland1 | The fault, above. The door: the press at frame 1101, and the player is in the hall at frame 1104 |
| tland2 | Before frame 1000 (`temple_taps` off): the press at post 0 has no target. After it: "the target is 0F74FE60 (LeashObj): the grip taps it" at each post, with the body 0.0 cm from the Hand's point. A press at post 1: "the chosen leash post is 1 now (the Leash of Learning)". At post 2: the chosen post is 2, and "the leash is kind 3 and off (it was kind 2 and off)". At post 0: the chosen post is 0, and the leash is kind 1. The door: the press at frame 1531, and the player is in the hall at frame 1534. The halo of the target was drawn in 700 eye passes of that window |
| tland3 | The build of this change, the same steps: the same results. At each post the log has "The game says that a press taps it". The card of the pointing hand showed on 418 of 450 frames with hints of the game on it, so the lines of the mod did not show (0 prompts of this context) |

**Not tested, and what needs the headset.**

1. A real hand at the posts: they are small at the scale of the start, and
   the nearest one wins. Whether a player hits the one that he wants.
2. A tap on the chosen post (the game sets -1 and sends nothing).
3. The door before the story turns it on: the card says "Tap to go into the
   Temple" then too. The log has the state of the door.
4. The grab from a distance on a post or on the door.
5. The lines of the card of the mod: no run had the hints of the game off.
