# The Hand: how the game places it, and the direct hand

This file describes how the game places the Hand, what the Hand points at,
and how the plain game picks up, holds, drops and throws an object. Then it
describes the mod's direct hand: the Hand at the real hand, with the ray, the
depth, the size, the exact pose, the touch of the land, and the choice of
which real hand is the Hand.

Other subjects of the Hand have their own files. See the last section, "The
other docs of the Hand".

## How to read this file

- **[V]** means verified. The claim comes from the disassembly (capstone,
  `bwvr/tools/disasm.py`) or from a Ghidra 12.1.3 decompile of our exact
  binary, and the text quotes the address.
- **[I]** means inferred. The claim fits the code but a live test or a
  deeper read must confirm it.
- **[G]** means a guess from the shape of the data.
- World units: the mod uses 7.5 units per metre in play mode 0 (see
  [backend.md](backend.md), "A separation of zero means ask"). The God view
  and the table use other scales.
- "The pointer" means the pointer globals `0x00E852C0` / `0x00E852C4`, in
  render pixels after the mod's pointer map (see [backend.md](backend.md),
  "One pointer, not two").

### Two symbol names are swapped

The bw1-decomp symbol file swaps two names. `disasm.py` prints the wrong
name for both. [V]

| Address | Symbol file says | It really is | Proof |
| --- | --- | --- | --- |
| `0x005B3C70` | `ObtainRequiredHandPosition` | `HandStateHolding::Update` | slot 3 (+0x0C) of the `HandStateHolding` vtable `0x00900B40`. It starts `mov esi, ecx` and reads the CHand at `[esi+4]` |
| `0x005B5E70` | `HandStateHolding::Update` | `ObtainRequiredHandPosition` | `HandStateNormal::Update` calls it at `0x005B71CB` with nine stack arguments and cleans `0x24` bytes. That matches the signature of the Mac symbol, `(CHand*, float, float, float&, LHPoint&, LHPoint&, int&, bool, bool)` |

This file always uses the real names.

### The decorated name of ObtainRequiredHandPosition has a wrong class

The symbol file gives this function the name
`?ObtainRequiredHandPosition@@YAXPAVCHand@@MMAAMAAVLHPoint@@AAV1@AAH_N5@Z`.
A demangler reads the sixth parameter of that name as `CHand&`: `AAV1@` is
a reference to the class of name 1, and name 1 is `CHand`, because name 0 is
the name of the function. The sixth parameter is an `LHPoint&`, the up
vector. [V]

| Proof | What it shows |
| --- | --- |
| The Mac symbol, `ObtainRequiredHandPosition__FP5CHandffRfR7LHPointR7LHPointRibb` | The names of the Mac build are from the compiler. The fifth and the sixth parameter are `LHPoint&` |
| The function, `0x005B5E70` | It writes three floats through the sixth parameter (`+0`, `+4`, `+8`) and copies them to `0x00D13F60` |
| `HandStateNormal::Update`, `0x005B71B5` | It gives the address of a local of 12 bytes (`lea edx, [esp+0x20]`). Then it gives the same local to the pose builder `fn_0046E160` as the up vector |

The exe has no symbols, so a person or a tool wrote each decorated Windows
name of the symbol file. The mod does not call this function and has no
declaration of it.

`bwvr/ghidra/name-corrections.txt` holds the right names, and `disasm.py` and
the Ghidra project use them. `bwvr/tools/check_names.py` compares the
parameter classes of each decorated name with the Mac symbol of the same
function. It found two names with a wrong back-reference digit: this one, and
`BlendColor` `0x005284C0` (its third parameter reads as a class
`BlendColor`, and it is an `LH3DColor*`). Both have a right name in the
corrections file.

### Tools used

- A Ghidra project of the exe with full auto analysis and the names of
  bw1-decomp is in `local\ghidra`. `bwvr/tools/re.ps1 setup` makes it in
  about 3 minutes, and `re.ps1 decompile` gives the C of a list of
  addresses in `local\ghidra\out\`.
- `bwvr/tools/disasm.py` prints the disassembly of an address range.
- In the project of `re.ps1` the names come before the analysis. Ghidra
  then makes the class, the signature and `this` from each decorated name.
  A signature that comes from a name is a claim of the symbol file, not a
  proof. Check it in the disassembly before you mark a fact [V]. The
  decompiler can lose the register arguments of `__thiscall` and
  `__fastcall` functions, so read each calling convention from the `ret` and
  the first instructions.

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
| `+0x9C` | the player's alignment, -1 (evil) to 1 (good), written each frame | [V] `0x0046C63A` to `0x0046C66D` |
| `+0x47F0` | `LHMatrix*`, the root pose (12 floats, translation at +0x24) | [V] copied to the mesh at `0x0046CAE5` |
| `+0x482C` | the Hand mesh object | [V] |
| `+0x484C` | mirror flag: set while the drawn Hand is a right hand | [V] `ToggleLeftRight` |
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
| holding, and the held object `IsSpellSeed` (vtable +0x4C4) | 8 `Grain`, the state of a miracle in the Hand |
| holding, and `+0x4908` set | 3 `Tug` |
| holding otherwise | 4 `Holding` |
| empty, and byte `+0x491C` set | 9 `PlayAnim` |
| empty, hand mode 1, 2 or `0xC` | 0 `Invisible` |
| empty, hand mode `0x19` (the story scenes) | 0 `Invisible` |
| empty, hand mode `0xE` | 5 `Totem` |
| empty, hand mode `0x13` | 7 `Creature` if `+0x49B8` is set, else 1 |
| empty otherwise | 2 `Camera` if `CameraModeNew3+0x8C` bit 0, else 1 `Normal` |

`fn_0046CF80` filters the result. On the way out of the Creature state it
sends packet `0x59` with the stroke and slap total (see
[creature.md](creature.md)). `GetRequiredState` never returns 6
(`MultiPickUp`), and no other code sets that state.

While the interface hand mode is `0xC` (the start dialog) the Hand is
Invisible, so no Hand test can run until the dialog closes.

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
   `fn_00803630`, which only turn the Hand's up vector to the land's normal
   [V, `0x005B6F8C`]. The decompile shows no write to the position after
   this test, so the land clearance of the game comes from step 1, which ends
   the ray half the Hand's size before the land [I].
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
- **`fn_0046C040`** returns 3.2, the Hand's length at size 1. The
  exponent of the scale curve is the double at `0x008CBF28`, 0.8.
- **`LH3DIsland::GetAltitude` `0x00803090`**: `__fastcall`, ecx = an
  `LH3DMapCoords` of three ints `{x * 6553.6, z * 6553.6, 0}`, the height in
  st0, a plain `ret` (`0x005B6D73` to `0x005B6DB9` builds the argument
  with the constants 65536 and 0.1).
- **The yaw.** `PrepareForDrawing` calls `fn_0074CBD0` at `0x0046C6F0` with
  ecx = `hand+0x485C`. That function calls `UnprojectScreenPoint` too, so a
  hook there turns the yaw with the ray.
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

### The places to override

The game builds the Hand from two inputs, a screen ray and a distance. Then
one function commits the pose.

- **The pose builder `fn_0046E160`** is the one place where six states
  commit the final world position and rotation. A hook there can write the
  controller point through its `position` argument (for Normal that argument
  is `hand+0x78` itself). It also writes `0x00C5E680`, so the held object and
  the drawing follow. [V for the callers and the argument]
- **The ray and the distance.** The game's own logic (the pick, the drop
  test, the hint, the held object target) runs before the pose builder, from
  the ray and the distance. The mod feeds both, so the logic sees the same
  point (section 4, "The hooks").

Normal and Holding end with `ScreenPointToWorldAtDistance(+0x4854)`, so the
ray, the distance and the pose together put the Hand on the controller point.

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
The draw offers an object to `SendObjectDrawCollision` only when its circle on
the screen holds the pick pixel.

### The records in GInterface [V]

The second record starts at `+0x3E8`. `+0x3E0` is the time over the same
object and `+0x3E4` that object.

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

### What a holdable object must pass

The pick up path `GInterface::PlaceObjectInMagicHand` `0x005DA6F0` tests [V]:
`IsInteractable` (+0x190), `GInterfaceStatus::IsSpaceInHands`, the byte
`+0x24` bits `0x04` and `0x20` clear, the byte `+0x0A` bit `0x10` clear
(`0x005DA766`), `ValidForPlaceInHand(status)` (+0x6FC) equal to 1 with the
status at `GInterface+0x39C`, and `IsCannotBePickedUp` (+0x180) equal to 0.

`fn_005D5E40` is thiscall on a record (`GInterface+0x3E8` for the next one),
`(Object*, float)`, `ret 8`. When `IsInteractable` is 1 it writes the object
at `+0x18` and the distance at `+0x1C`, else it writes 0 and `FLT_MAX`. The
nearest test is in `SendObjectDrawCollision`, not here. That function skips
everything while the byte `GInterface+0x38` is set. [V]

The mod gives the game its own pick by calling `fn_005D5E40` on the next
record after the eye passes. The object matrix is at `Object+0x14` (a 3x4,
translation at `+0x38`) [V from `ThrowObjectFromHand`]. The choice of the
object and the grab radius are in [grab.md](grab.md).

## 3. Pick up, hold, drop, throw

### Pick up [V]

The pick up is a network packet even in single player. `GPacket::ProcessPacket`
case `0x13` calls `GInterface::PlaceObjectInMagicHand(Object*)` `0x005DA6F0`.
For the local player it calls `fn_0046E590(object)`, which gives the object
to the CHand [I]. `fn_005DC330` then clears the gesture samples. The
interface action that sends the packet is not traced.

### Hold [V]

`HandStateHolding::Update` `0x005B3C70` calls `ObtainRequiredHandPosition`
for the target. It has two modes, chosen by the state field `+0x104`:

- **Direct** (`+0x104 == 0`): `hand+0x78` is the target. The target is
  the point of `ObtainRequiredHandPosition` (through the state's vtable
  slot `+0x14`), lifted by the hang height of the object, so the object
  hangs at the pointer and the Hand is above it. With an object, the
  pose builder gets a copy of `hand+0x78` on the stack.
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
   `+0x20` of interface action 11 in the action table at `0x00D17DB8` (the
   table write at `0x005D0C01` is verified, the slot meaning is [I]).
2. It checks the land point under the pointer (`+0x3F0`) with
   `MapCoords::InBounds` and `ValidToApplyThisToMapCoord` (+0x724), and the
   player's influence there (`+0x48`). If one fails, the object stays in the
   Hand (see "The influence gate").
3. It builds a local physics object at `0x00D47088` from the held object
   and sets `0x00D47824` / `0x00D47828`.
4. It reads `hand+0x48C8..+0x48D0` (`0x005D4E46`, `add esi, 0x48C8`) and
   copies the three floats unchanged (`0x005D4F34` to `0x005D4F59`), in world space, to `0x00D47164` (the
   first vector of packet `0x4D`). No rotation and no scale.
5. It caps the length at 124 units per second. The cap is on the length, not
   on each part: `0x005D4F75` compares `|v|^2` with `124^2` (the float at
   `0x00C371FC`), and above it `0x005D4FF6` to `0x005D501C` divide by `|v|`
   and multiply by 124. So the direction stays.
6. It marks a gentle drop when the object is not a tree
   (`IsAnyKindOfTree`, +0x478) and `|v|^2 <= 1`. The test uses the length
   before the cut.
7. It sends packet `0x4D` with 48 bytes: the velocity, a release point
   (`hand+0x48D4` through the physics matrix), a translation and three
   angles (`fn_007FD140`, `fn_007FAB30`).
8. `GPacket::ProcessPacket` case `0x4D` copies the 48 bytes to
   `GInterfaceStatus+0x44..+0x73`.
9. The drop action `fn_005DA400` calls `ApplyThisToMapCoord` (+0x728). For a
   `Mobile` or `MobileStatic` that calls `ThrowObjectFromHand` (+0x758),
   `Object::ThrowObjectFromHand` `0x006385E0`.
10. `ThrowObjectFromHand` puts `status+0x5C` in the object's matrix,
    rotates it by the angles at `+0x68`, and scales it. Then it calls
    `InitialisePhysicsFromHand(status+0x44, status+0x50, status, 0, int)`
    (+0x780, `Object` `0x00636F00`).
11. `InitialisePhysicsFromHand` treats a horizontal speed squared above 4 (2
    units per second) as a throw. Slower objects are set down and raised
    until they do not intersect.

`CHand::ThrowObject` `0x0046DDD0` is not the physics throw. It only lets go
of the object on the CHand side (`+0x4904` to `+0x48FC`). `fn_005D1260` calls
it when the interface resets. `CHand::GetMaxForce` `0x0046BFB0` returns
`(this+0xAC + 1.0) * 600000`. No caller in the throw path was found.

The spell seed path `FUN_005D3340` (`0x005D3340`) sends the same packet from the same CHand
fields (reads `+0x48C8`, `+0x48D4`, `+0x48E0`, `+0x48EC`).

**The mouse sequence.** A right press while the Hand holds an object gives
hand mode `0x17` (the throw spring) 2 frames later. The release action runs
1 frame after the right button goes up. So the mouse lets go of an object
with a second click: the press starts the throw spring, and the release of
the button lets go. [V]

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
influence. At a large world scale a swing of the direct hand carries the Hand
far over the land, so the mod sends a throw that ends outside from the last
point of the swing inside the influence (see [throw.md](throw.md)).

The mod feeds its own velocity to `hand+0x48C8..+0x48D0` before the release
action runs. The velocity, the gain, the grip that lets go and the landing are
in [throw.md](throw.md).

## 4. The direct hand

The direct hand puts the game's Hand at the player's real hand. The code is
in `src/mod/hand_direct.cpp`, with the grip point, the reach and the pixel in
`controllers.cpp` and the pure math in `controller_math.h`.

### The rule

The Hand is the real hand, one to one in place and size, always. There is no
Go-Go reach and no growth with distance. Reach comes from the world: the God
view puts the land at about waist height (see [god-view.md](god-view.md)).

### When it acts

`controller_hand=1` (the default) and the controller owns the pointer this
frame. With the mouse as the owner, with `controller_hand=0` or with
`controllers=0`, no hook changes anything. With `controller_hand=0` the hooks
of the direct hand are not installed.

`HandDirectFrame` latches the target, the size and the CHand once a frame,
before the eye passes. The hooks write those values, so a second eye pass
writes the same numbers again (`CHand::PrepareForDrawing` runs in both eye
passes). The hooks place the Hand in the Normal and Holding states, and the
Creature state (see [creature.md](creature.md)), and only inside the two eye
passes. The other states keep the game's own place, but the exact Hand draws
them at the controller (section 7).

While a dialog box is up, or in a room of the Temple that needs a ray, the
direct hand steps aside. The pointing hand then points like a mouse: the aim
ray picks the pixel and the trigger clicks, as with `controller_hand=0`. The
direct hand comes back when the box closes.

### The hooks

| Hook | What it writes |
| --- | --- |
| `UnprojectScreenPoint` `0x0081B370` (the hook in `pointer_map.cpp`) | For the screen point `hand+0x485C` only, outside the camera code: the ray to the target. The origin and the length stay |
| `CHand::SetDistanceFromView` `0x0046C0D0`, post | `+0x4854` = the distance from the camera `0x00EA1DB8` to the target, past the game's clamp of 2 units. `+0x4834` = the Hand's size. In the Creature state the hook writes the size divided by the root of the Creature's size, so the Hand keeps the size of the real hand |
| The pose builder `fn_0046E160`, pre | The target to `*position` and to `hand+0x78`. The screen spring `+0x48B8..+0x48C4` to the screen point with no speed, so the mouse lean does not tilt the Hand |
| `GInterface::SendObjectDrawCollision` `0x005D56C0`, post | Nothing. It keeps each object of the first eye pass and its mesh translation (`LH3DObject+0x38`) |

The mod writes the depth after the game's clamp, because the game clamps the
distance to 2 units (27 cm). A controller close to the face then still works.

### The target

The grip pose goes into the room. The reach curve moves it out along the line
from the reach anchor, a fixed point in the room: the head center at the last
recenter, `hand_reach_anchor_down_cm` (20) lower, at about the chest. Then
the anchor and the target go into the world with the same step as the aim
pose. The eye, the head matrix and the head center come from one head sample
(`EyeFromLean` in `controller_math.h`: `E = game_position + L * lean`). So with
full six degrees of freedom the target is `game_position + L * reach_room`,
and a head that moves with a still hand moves neither the Hand nor a held
object. The reach is measured from this fixed anchor and not from the render
eye, because the render position of the frame drawn now has the lean of the
frame before. In rotation only mode the Hand keeps its place to the eye, as
the aim ray does, so it moves with the head by the same distance.

The reach curve: `D = d + gain * (d - start)^2` past `hand_reach_start_cm`,
with d the distance from the anchor in meters and the gain
`hand_reach_gain` per meter. With `hand_reach_gain=0` (the default) the curve
is one to one, and the anchor does not matter. A hand that is farther than the
CHand far limit `+0x4838` (the upper clamp of `SetDistanceFromView`,
`0x0046C0E4`, 1800 units in the logs) is cut at the limit. The limit never
pulls the target nearer than the grip.

On the table (`play_mode` 3 and 4) the hand goes into the world through the
table, with no reach curve.

**Tracking.** When the runtime reports the grip as not tracked (for example a
controller outside the headset's cameras), the target stays at the last
tracked grip point in the room. The owner rule does not count an untracked
pose as motion. The log tag `controllers` says each change of the tracked
state. Until the runtime has reported the grip as tracked once, a valid grip
counts as tracked, so a runtime that never sets the tracked bits still moves
the Hand.

### The pointer

The pixel is the target seen from the eye (`PointPixel`). So the render pick,
the land lookup, the yaw and the gesture path see the point where the Hand
is. The Camera state (the land drag) keeps the game's own placement of the
Hand, on the ray through this pixel.

The camera code gets a different ray: the one from the game camera to the
target (`ControllerCameraRay`, used in `h_GCameraUpdate` and `h_Unproject`).
The target is fixed in the room, so a lean of the head does not turn this ray
and the land does not slide. The ray from the eye through the pixel turns with
every lean. The press ray and the drag rays all use the camera ray, so the
grab point does not jump at the press. The cost is a small parallax: the
camera code grabs the land on the game camera's ray through the target, and
the game draws the Hand on the eye's ray. The two rays meet at the target.

### The buttons

The trigger and the grip of the direct hand send no double clicks. The aim
direction says nothing about where the Hand is, so a ratchet of short land
drags with a still wrist would read as a double click, and a double click on
the land is a jump. The A button makes the jump. See
[controllers.md](controllers.md) for the buttons.

### The pick

After the first eye pass, the mod chooses the nearest holdable candidate
within the grab radius of the Hand's point. `Inject` calls `fn_005D5E40` on
the next record with the choice after each eye pass. With no choice, a
holdable object in the next record is taken out, because it is far behind
the Hand on the ray. Buildings, signposts and scrolls stay. The game copies
the record to `+0x3C8` the next frame, as it does with its own pick. The pick
runs while the Hand is empty. The reach `hand_pick_cm` (4 cm of the room by
default), the bodies of objects, and the far grab are in [grab.md](grab.md).

### The size

The Hand is the size of the real hand at the world scale. The size is
`hand_real_cm` (19 by default) in meters, times the world scale in units per
meter, divided by the Hand's length at size 1 (3.2 units), times
`hand_scale_pct` (100 by default) as a fraction. At 7.5 units per meter that
is size 0.445. The size never grows with the distance. On the table the size
gives `table_hand_cm` in the room at the table scale. The land clearance and
the grab radius follow the size.

The game's mesh scale is `k = 3.2 / (+0x8C) * (+0x4834) * (+0x90)`, written to
`+0x94`. `+0x8C` is the span in y of the bone joints (555.29 for the stored
bones: the wrist joint to the index tip). It is not the length of the hand:
the wrist joint to the end of the middle finger is 623.6 units, 12% more. See
section 7 for the size of the exact Hand.

### Keys of the direct hand

| Key | Default | What it does |
| --- | --- | --- |
| `controller_hand` | 1 | 1 puts the Hand at the controller. 0 keeps the pointer. Not in the launcher, and a save of the launcher removes the key |
| `hand_reach_start_cm` | 35 | The Hand follows the controller one to one up to this far from the reach anchor |
| `hand_reach_gain` | 0 | Per meter past the start. 0 gives one to one at all distances. 215 gives the Go-Go reach: a hand 65 cm from the anchor reaches 150 units |
| `hand_reach_anchor_down_cm` | 20 | The reach anchor is the head center at the last recenter, this far down (0 to 100) |
| `hand_real_cm` | 19 | The length of the real hand, from the wrist to the finger tips |
| `hand_scale_pct` | 100 | The Hand's size in percent of the real hand |
| `controller_left_handed` | 0 | 1 makes the left hand the Hand (section 8) |
| `fake_controller_points` | | Test key. A list of `right,up,forward` in cm |

Test script words (with the fake controller): `point@frame+frames` steps the
hand to the next point of `fake_controller_points`, and after the last point
the list starts again. `seek@frame+frames` steps it once onto the nearest
holdable object of the last frame, through the reach curve backward
(`ReachInverse`). The log tag `hand` prints the target, `hand+0x78` and
`0x00C5E680` 20 frames after each step. The tag `hand pick` prints the choice,
`+0x3C8` and `+0x4904`.

### How to test the direct hand on the desktop

The fake controller gives still poses, and no pose moves by itself. A hotkey
or a script word steps to the next pose. A speed test uses two still poses one
frame apart.

- Step the fake pose through points in front of the camera. `hand+0x78`,
  `0x00C5E680` and the target must agree to 0.01 units.
- Step the fake pose onto a villager's position. `+0x3C8` must be that
  villager the next frame.
- Step the pose under the land surface. The Hand must stop on the land.

The game boots to play with `-NEWGAME`. It first shows a start dialog with
four check boxes and OK, and the interface hand mode stays `0xC` (the Hand is
Invisible) until it closes. The fake controller can click it. With the fake
head straight ahead and a 2560 x 1440 render at 110 degrees, the pixel of a
point `r, u, f` in cm from the eye is `x = (1 + 0.7003 r / f) * 1280` and
`y = (1 - 1.2449 u / f) * 720`. The pixel of the direct hand is the pixel of
its reach point. The dialog recipe for the God view is in
[god-view.md](god-view.md), section 12.

## 5. The touch of the land

The target stops `kLandClearanceM`, 5 mm of the room, over the land, at the
scale now (`HandLandFloor` in `controller_math.h`). The scale is the table's
in play modes 3 and 4 and `HeadPoseScale` in play mode 0. `ResolveLand` in
`hand_direct.cpp` does it once a frame, from inside a Hand state, with
`GetAltitude`. So the point touches the land and the objects on it, one to one
at every scale (7.5, 100 and 1000 units per meter, and 5 cm on the table with
`table_hand_cm` 10). A hand pushed further down slides on the land. Nothing
moves that the player did not move. The glow follows the point. The grab
radius of the pick still measures from the point. On a real table the
controller's grip is about 5 cm over the top when the fingers touch it.

The game does not hold the point up. Its distance step ends the ray half a
Hand before the land, but the depth hook and the pose hook replace both. The
math test checks the clearance for the three scales.

The tag `hand` logs, 20 frames after the target stops, the point's height
over the land and how far the controller is under it, in cm of the room.

Needs the headset: whether the glow and the game's Hand reach the land and a
villager in the God view, and whether the Hand's mesh in the land looks wrong.

## 6. The pose builder and the drawn Hand

The pose builder uses only the yaw `+0x84` (from the screen ray, `0x0046C725`)
and an up vector that glides over 0.4 s: `A = normalize(d x u)`, `B = A x u`,
`C = -u`, with `d = (sin(yaw + pi), 0, -cos(yaw + pi))` (`0x0046E193` to
`0x0046E2C7`). Then `fn_0046D9D0` multiplies the scale matrix of `fn_0046D080`
by these rows. The translation of the matrix is the target (`0x0046E2CC`). The
mesh's origin is the tip of the index finger. The controller's rotation is
not in the game's pose, so the exact Hand (section 7) adds it.

**The blend.** After each change of state, `fn_0046C500` swaps the pose
arrays `+0x47F0` and `+0x49AC`, sets the timer `+0x49B0` to 0 and copies
`0x00C5E680` to `0x00C5E670`. For 0.13 s `PrepareForDrawing` then mixes each
float of the old and the new pose (`0x0046C932` to `0x0046CA28`), and the
drawn position `0x00C5E660` too. The pose arrays hold the world matrix of each
bone, so a Hand that moved glides from its old place.

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
  `LH3DAnim::SetTransform` (`0x00618602`) there: the span in y of the bone
  joints, with 0 in it.
- **The animation number.** Each hand state asks `Morphable::GetAnim`
  `0x00619650` (thiscall, the number first, `ret 8`) for its animation. The
  numbers in the code of the states are 0 (`wiggle`), 1 and 2 (its leans),
  6 (`hold_above`), 9 (`hold_side`) and 60 (`horn`). A hook keeps the last
  number that the Hand asked for, and the log gives it with each state.

## 7. The exact Hand

The exact Hand puts the game's Hand on the real hand: its index tip at the
real index tip, turned with the controller, at the real size. The code is in
`src/mod/hand_exact.cpp`, with the pure math in `src/mod/hand_exact_math.h`.

Without it the Hand follows the controller only roughly, for four reasons
[V]. The pose builder puts the origin at the target, which is the grip, about
one hand length behind the index tip. It uses only a yaw, so it never uses
the controller's rotation. The 0.13 s blend glides a Hand that moved. And the
game's mesh scale `k` makes a Hand of `hand_real_cm` 19 about 21.3 cm long,
because `+0x8C` is 12% less than the hand.

### What the mod does

**The seam.** A hook on `fn_00839F10` acts only on the Hand's own pose array
(`*(CHand+0x47F0)`). While the controller places the Hand, it gives the
compose the exact root in place of the game's root. The state's own root and
the game logic that reads it stay as the game made them. Only the drawn bones
and a spell on a bone follow the exact root. The seam reaches every state, the
Creature state too, so the pose builder hook of `hand_direct.cpp` needs no
change.

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
`size` is the Hand's size of section 4. So the Hand from the wrist joint to
the end of the middle finger is `hand_real_cm` long.

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
fingers, as the game does, and the Hand does not glide. A blend from a pose
that the hook never saw (for example the first frame after a dialog box)
ends at once. In the Normal and the Holding states the hook also sets
`0x00C5E670` to `0x00C5E680`, so the held object does not lag either. With
the mouse nothing is moved, and the game's blend stays.

**The wrist lock** (`hand_wrist_lock`). Some animations move the wrist bone
itself. The Normal animation keeps the wrist of the file, but the Holding
animation turns the whole hand 79 degrees about its fingers (as a hand that
grips an object from the side) and moves the wrist 14 cm. The compose gives
`wrist of the animation * root`, so such an animation would move the whole
Hand. Before the compose, the first matrix of the pose array is the wrist bone
of the animation in the frame of the root. The hook gives the compose this
root:

```
root' = inverse(wrist of the animation) * (wrist of the file) * (exact root)
```

Then `wrist * root'` is the wrist of the file under the exact root, in each
animation, and only the fingers move. The wrist of the file is bone 0 of
`Hand_Boned_Base2.l3d`: a turn of 17.27 degrees and the place `-229.27
-555.29 -4.86`. A spell on a bone and an effect on a bone follow the real hand
in each exact state. The records for the move of an old pose keep the exact
roots, and the same move works for a locked pose. The lock works only after
the Normal animation showed the wrist of the file in the first matrix (within
3 degrees and 20 mesh units). If it does not, the log says so and the lock
stays off.

**The mesh is hidden** (`hand_hide_game`, default 1) [V]. `CHand::Draw`
`0x0046D210` returns at once in three cases (`+0x49A0` is 1, the byte
`0x00C4CCEE`, or the interface says no). With `+0x47BC` set it calls the mesh
draw at `0x0046D258` and returns. In each other case it needs a state that is
not 0 and `+0x483C` clear, calls the mesh draw at `0x0046D27C`, and then draws
`+0x4948` (vtable `+0x10`), `GVirtualInfluence::Draw` (the ring) and
`CHand::DrawSpellInHand`. Both mesh calls are `call [vtable + 0x108]` on the
mesh object `CHand+0x482C`. That object comes from `LH3DObject::Create(3)`,
its vtable is `0x009A3068`, and `+0x108` there is `fn_00813BA0`, thiscall
with no arguments and a plain `ret`. No other vtable points at it.

A MinHook hook on `fn_00813BA0` skips the draw when three things are true:
the return address is `0x0046D25E` or `0x0046D282`, the object is the Hand's
mesh, and the controller places the Hand in an exact state while the overlay
drew the pointing hand in this frame. So one hand is always there, each other
part of `CHand::Draw` still runs, and the mouse player sees the game's Hand as
the game draws it. MinHook puts the bytes back when the mod unloads. The hook
is made only when the first bytes of the function, the two call sites and the
vtable slot are as the disassembly gave them. In the states Invisible, Totem,
MultiPickUp and Citadel the game's Hand is not exact, so its mesh stays drawn.
The overlay then draws both of the mod's hands (see
[hand-model.md](hand-model.md)). The shadow of the game's Hand on the land
still draws, with the game's fingers.

**The held object in the palm** (`hand_hold_palm`, `hand_hold_turn`). After
`CHand::PrepareForDrawing` wrote the object's translation (`0x0046CA50` to
`0x0046CA97`, only in the Holding state, and not while the release physics has
the object), the mod writes the object's mesh matrix (`Object+0x40`, then
`+0x14`, 4 rows of 3):

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
point on the land and the throw are as the game has them. A set down puts the
object on the land under the Hand's point (the index tip), not under the palm,
so the object can move a few cm when the hand opens.

### The states

| State | The drawn Hand | The game's position `+0x78` |
| --- | --- | --- |
| 1 Normal, 4 Holding | exact | the point (the hooks of section 4) |
| 7 Creature | exact | the point (the depth hook, see [creature.md](creature.md)) |
| 2 Camera (the land drag), 3 Tug, 8 Grain (a Miracle in the Hand), 9 PlayAnim | exact | the game's. Its logic (the land under the drag, the pull of the Tug, the cast of the Miracle) keeps it |
| 0 Invisible, 5 Totem, 6 MultiPickUp, 10 Citadel | the game's | the game's |

In the states of the third row only the drawn Hand moves to the controller.
In the Tug the game pulls the object toward a point on the line of sight
through the Hand, so the drawn Hand and that point stay on one line from the
eye. A spell of the Grain state on a bone follows the drawn Hand. A spell
placed at `+0x78` does not [G]. The Totem state was not read, and the Citadel
state draws from another camera. A spell that the game puts at `+0x78` (not
on a bone) does not follow the palm in the states where the game keeps its
own position.

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

In the Normal state the drawn bones, taken back into the stored mesh frame
through the inverse of the exact root, are the bones of the file: only the
middle, ring and little fingers bend toward the palm. In the Camera state (the
land drag) the game's grip animation curls the fingers, and the wrist moves up
to 6 cm from the index tip. The fingers of the game's animations are the
game's own: the trigger and the grip do not bend the game's fingers.

### Keys

| Key | Default | What it does |
| --- | --- | --- |
| `hand_exact` | 1 | 1 puts the game's Hand on the real hand: its index tip at the real index tip, turned with the controller, the real size. 0 turns it with the screen ray, and the point is the grip |
| `hand_mesh_yaw_deg` | 11.0 | The rest turn of the mesh about its palm normal, in degrees |
| `hand_mesh_pitch_deg` | -0.58 | The rest turn about its thumb axis |
| `hand_mesh_roll_deg` | 0 | The rest turn about its fingers |
| `hand_wrist_lock` | 1 | 1 keeps the wrist bone of the game's Hand at the real wrist in each animation |
| `hand_hide_game` | 1 | 1 hides the mesh of the game's Hand while the controller places it, and the overlay draws both hands. 0 draws the game's Hand too |
| `hand_hold_palm` | 1 | 1 puts the held object in the palm of the mod's hand. 0 leaves it at the Hand's point |
| `hand_hold_turn` | 1 | 1 turns the held object with the hand |
| `hand_left_mesh` | 0 | Does nothing. The hand model draws the left hand |
| `fake_controller_rots` | | Test key. `yaw,pitch,roll` in degrees, separated by spaces. The script word `rot` turns the fake right hand to the next one |

The keys are read from `bwvr.ini` at the start. They are not in the launcher.
With `test_keys=1`, the minus key of the number pad shows and hides the mesh
of the game's Hand while the game runs. The pointing controller gives a long
pulse for hidden and a short pulse for shown.

### The log

The tag `hand exact`. At the first exact Hand: the game's `k` and the exact
`k`, `+0x8C`, `+0x90`, `+0x4834`, the mirror and the bone count. Once: the
drawn bones in the stored mesh frame, against the bones of the file, and the
leans of the hand's line and of the knuckle line. Once for each state: the
animation number, and how far the wrist bone of the animation is from the
wrist of the file, in degrees and in cm. At each change of state, for 3
passes, and every 600 frames (60 lines at most): the drawn wrist and the
index finger's last joint against the overlay's hand, in cm of the room, the
angle between the two lines from the wrist to the middle finger, and how far
the wrist moved in the Hand's own frame since the last pass. A Hand that lags
moves there, a Hand on the exact root does not. With the lock, a hold shows
about 2.5 cm between the drawn wrist and the mod's wrist. At each new hold (40
at most): the object, the ball of its body, the place that the game gave, the
place in the palm and the distance between them. The report window counts the
exact composes, the composes as the game made them, the old poses moved to the
new root, the composes with the lock, the hidden draws, the frames with an
object in the palm, and the frames with the wrist moved more than 0.5 cm.

### Tests

The math test (`ExactChecks` in `test/controller_math/exact_checks.h`) checks,
at six grip turns and for both hands: `(A, B, C) / k` is orthonormal, the
determinant is below 0 for the right Hand (`+0x484C` set) and above 0 for the
left one, and it has the sign of the game's own root for each mirror. The
mesh's line from the wrist to the middle finger is along `f` within 0.02
degrees and as long as the real hand. The palm faces away from the back of the
hand. The thumb is on the thumb side of each hand. The origin is the point.
Each rest turn moves the side it says. The point is the overlay's index tip of
each hand, 8.8 cm from the grip, and the trigger does not move it. An old bone
moved to the new root is the new bone. The hold and wrist lock checks are in
`hand_pose_checks.h`: a wrist that is turned 79 degrees and moved becomes the
wrist of the file under the exact root, and an animation that keeps the wrist
of the file keeps the exact root.

Desktop test: use the God view rig of [god-view.md](god-view.md), section 12,
with `controller_hand=1` and `overlay=1`. The script word `rot` turns the
right fake hand through `90,0,-90` (palm down, fingers forward), `0,0,0`
(fingers down, palm to the left), `0,90,0` (fingers forward, thumb up),
`-90,0,90` (palm up) and `30,45,-60`. Take an eye capture at each. The Hand's
point must be 0.0 cm from the overlay's index tip, the game's Hand must lie
inside the overlay's test hand, and no pass may move the wrist more than 0.6
cm in the Hand's frame.

Not reached on the desktop: the Tug, the Holding, the Creature, the Grain and
the PlayAnim states, so the exact Hand there and the held object with no lag
are not seen yet, and the hook of the mesh draw, the wrist lock in each state,
the object in the palm and the poses with a real controller. The math and the
seam are the same as in the Normal and the Camera states.

Needs the headset: whether the Hand sits on the real hand with each runtime's
grip pose, the rest turn, whether the Hand in the land drag, the Tug and the
Grain state looks right at the controller, and for which object the place or
the opening of the hand in the palm looks wrong.

## 8. The two real hands and which one is the Hand

The Hand is the pointing hand: the right controller by default
(`PointingIndex()` is 1), the left one with `controller_left_handed=1`
(index 0). The pointing hand does these things:

- It places the game's Hand (section 4), points the ray of the teleport, the
  menus and the far grab, and sends the trigger and the grip to the game as
  mouse buttons. `controller_swap_buttons=1` swaps which of the two is the
  left and which is the right mouse button.
- It owns the gesture button (`gesture_button`, B on the right hand, Y on the
  left) and the haptics of the game.
- It strokes and commands the Creature (see [creature.md](creature.md)).

The other hand does not place the Hand. Its grip grabs the world: the land
moves with it, as the grab of the land in the God view (see [god-view.md](god-view.md), section 5). It
works while the pointing hand holds an object. With both grips closed, the
pointing grip joins the grab, but only when the Hand is empty and over nothing
it can hold. With `grab_hand=2` the pointing grip can also start a grab alone
by the same rule.

The Hand does not move to the other hand. The game has one Hand, and the mod
keeps it at the pointing controller. With `controller_left_handed=1` the
game's Hand is still a right hand (`+0x484C` is set), so on the left
controller its palm and fingers fit and its thumb is on the other side. The
mesh of the game is hidden by default, and the overlay draws a hand model on
both controllers (see [hand-model.md](hand-model.md)). The game's own left
handed option makes the game's Hand a left hand.

## Open questions

- Which interface action sends the pick up packet `0x13`, and which code sets
  hand mode `0x17` (the throw spring) and `GInterface+0x39` bit 6 (the
  release). The input is known: a right press while the Hand holds an object
  gives `0x17`, and the release of that press lets go.
- What a right press on bare land does with an empty Hand.
- Who reads `GInterfaceStatus+0x10C` (`HandVelocity`).
- The exact meaning of the second and third vectors of packet `0x4D`.
- Whether the Hand in the Totem state and the Citadel state can be exact. The
  Totem state was not read.
- A second draw of the game's mesh, mirrored, at the other controller is not
  built. The draw is deferred (`CHand::AddDrawing` puts `CHand::Draw` in the
  z sorter, `0x0046D1F8`), and the pose array holds one pose, so a second draw
  needs its own pose array and its own sorter entry. The hand model draws the
  left hand instead.

## The other docs of the Hand

- [grab.md](grab.md): the grip that holds, one point, one target, the far
  grab, forests, and an object onto a thing.
- [throw.md](throw.md): the throw, the landing, and the release.
- [creature.md](creature.md): the Creature.
- [miracles.md](miracles.md): miracles and gestures.
- [village.md](village.md): clicks on things, scrolls, the village, the Temple
  door, and leash posts.
- [hand-model.md](hand-model.md): the model of the Hand, both hands, the
  glow, the overlay renderer, haptics, and reactions.
