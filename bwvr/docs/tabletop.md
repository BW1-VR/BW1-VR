# The tabletop mode (play mode 3)

The tabletop mode shows the island as a model on a table in the room, about
1.5 m across. The player walks around it and leans over it with full head
tracking. With the Virtual Desktop passthrough mask, everything past the table
is one key color, so the island stands on the real desk. The launcher offers it
as "Tabletop (test)". The mode is a test and it is not complete.

The God view (play mode 4, `god-view.md`) uses the same camera and the same
code, with the land at the player's waist and no edge. This file has the math
that the two modes share: the table transform, the grab solve, the values that
the engine needs at a large scale, and the key color. `god-view.md` has what is
specific to the God view.

Every claim carries a mark:

| Mark | Meaning |
| --- | --- |
| [V] | Verified in the disassembly or in the game data |
| [M] | Measured, with the document named |
| [I] | Inferred. Test it before relying on it |

## Summary

- **Scale.** A land is 1400 to 3300 units across and at most 171 units
  high. To fit a 1.5 m table the scale is 1100 to 2800 units per meter,
  about 2000 for Land 1. That is 270 times the 7.5 units per meter of the
  life size view. A 1 cm head movement is 20 units, two map cells.
- **Camera.** The table camera is the formula `head_camera.cpp` uses for the
  life size view, with two inputs changed. The room origin maps to a world
  point `P0` over the table, not to the game camera. The lean is scaled by
  `s / 7.5`.
- **The engine is tuned for a camera 100 to 700 units away.** At 800 to
  3200 units each of these fails: the fog, which ends at 900, the map
  objects, which vanish past 700, the landscape detail, which drops at 1600
  and 2500, the landscape textures, the Hand, which stops at 1800, and the big
  models, which are gone past 2600. Each is one value in memory, listed in
  "What the engine does at the table scale".
- **The edge.** A hook on the per block cull `fn_00877210` drops land blocks
  past a radius. The plain sea becomes a disc. With a key color, a device
  clear paints the screen outside the disc.
- **The controllers** place the Hand on the table with no reach curve. The
  grips move the table: one hand slides it, two hands turn and scale it.
- **Frame cost** is the main limit. The whole island is in view at once, and
  each eye pass makes its own draw calls.

## The size of a land

### The grid

| Fact | Value | Evidence |
| --- | --- | --- |
| Cell size | 10 world units | `MapCoords::Set` `0x00603340` multiplies by 6553.6, which is 65536 / 10 [V] |
| Map | 512 by 512 cells, so 5120 by 5120 units | `LH3DIsland::GetAltitude` `0x00803090` rejects cells outside 0 to `0x1FF` [V] |
| Blocks | 32 by 32 blocks of 16 cells, 160 units | index table `0x00E9C964`, block pointers `0x00E9C564`, 17 by 17 cells per block [V] |
| Height | altitude byte times 0.67, so 0 to 171 units | float `0x00C3720C` at `0x0080332F` [V] |
| Sea level | y 0. An altitude byte of 3 or less reads as 0 | `0x008031B0` and the tests after it [V] |
| Map center | (2560, 2560) | the sky dome, the 3500 pull back and the land pick clamp all use it [V] |

The sea is not in the land data. It is `LH3DWater`, a plane at y 0. Outside
the land blocks there is nothing but that plane.

### The lands, measured

Parsed from `Data\Landscape\*.lnd` [V]. "Land" is a cell with an altitude
byte above 3. The center is the center of the bounding box. `R far` is the
distance from that center to the farthest land cell.

| Land | Blocks | Land x | Land z | Size | Center | Peak | R far |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Land1 | 100 | 1150 to 3320 | 1640 to 3590 | 2170 by 1950 | 2235, 2615 | 166.2 | 1345 |
| Land2 | 138 | 840 to 3640 | 1580 to 3920 | 2800 by 2340 | 2240, 2750 | 170.9 | 1784 |
| Land3 | 94 | 1690 to 3400 | 1750 to 3500 | 1710 by 1750 | 2545, 2625 | 166.8 | 1167 |
| Land4 | 149 | 1470 to 3960 | 1700 to 3930 | 2490 by 2230 | 2715, 2815 | 157.5 | 1384 |
| Land5 | 187 | 1040 to 4350 | 1790 to 4270 | 3310 by 2480 | 2695, 3030 | 170.9 | 1928 |
| LandT | 63 | 1470 to 3170 | 1800 to 3600 | 1700 by 1800 | 2320, 2700 | 146.1 | 990 |
| MPM_2P_1 | 53 | 1830 to 3250 | 2010 to 3010 | 1420 by 1000 | 2540, 2510 | 128.0 | 756 |
| MPM_3P_1 | 104 | 1690 to 3400 | 1730 to 3320 | 1710 by 1590 | 2545, 2525 | 170.9 | 862 |
| MPM_4P_1 | 112 | 1590 to 3330 | 1780 to 3380 | 1740 by 1600 | 2460, 2580 | 163.5 | 1162 |

Every block in every file carries its own index and map position. All of
them match their slot in the index table [V]. The mod computes the same
numbers at run time from the block table in memory (the land fit, below).

### The scale

Fit the circle of radius `R far` around the land's center, with a margin of
10 percent, to the table diameter `D`:

```
s = 2.2 * R_far / D          units per meter
```

| Land | D 1.0 m | D 1.5 m | D 2.0 m | Peak at 1.5 m |
| --- | --- | --- | --- | --- |
| Land1 | 2959 | 1973 | 1480 | 8.4 cm |
| Land2 | 3925 | 2617 | 1962 | 6.5 cm |
| Land5 | 4242 | 2828 | 2121 | 6.0 cm |
| LandT | 2178 | 1452 | 1089 | 10.1 cm |
| MPM_2P_1 | 1663 | 1109 | 832 | 11.5 cm |
| Whole map, 5120 | 5120 | 3413 | 2560 | 5.0 cm |

The mod fits the land, not the map. The map is mostly sea. The default `D` is
1.5 m, and the scale comes from the land at each level load.

### What a head movement means

At 2000 units per meter:

| Room | World |
| --- | --- |
| 1 cm of head movement | 20 units, two cells |
| The eye separation, 64 mm | 128 units |
| A lean of 50 cm | 1000 units, half of Land1 |
| An eye 20 cm above the land | 400 units away |

So the head is a very large lever. A still head is a steady view, and the
6DOF parallax is strong at this scale, which is what makes a model read as a
model.

### The island is flat

A peak of 171 units on a land 2170 wide is a relief of about 8 percent. On a
1.5 m table the highest mountain is 6 to 11 cm. That is the true shape of
the land. The mod does not exaggerate the height. The height unit at
`0x00C3720C` is writable, but `GetAltitude` uses it for the game logic, and
it has 713 callers [V]. A scale in the render matrix alone would not reach
`LH3DSprite::Draw`, which reads the unscaled matrix, or
`UnprojectScreenPoint`, which casts the Hand's ray [V from
`stereo-artifacts.md` and `backend.md`].

## The table camera

### The map

The table is a similarity map from the room to the world. The room is the
frame `head_pose.cpp` uses: meters, left handed, the origin at the recenter
pose, and only the yaw of that pose kept.

```
world = C + s * Lt * (room - Tc)
```

| Symbol | Meaning |
| --- | --- |
| `C` | the table center in the world, on the sea plane, y 0 |
| `s` | the table scale, in units per meter |
| `Lt` | the table basis: world up, and a heading `psi` in the world for the room forward |
| `Tc` | the table center in the room: `table_forward_cm` forward and `table_down_cm` down from the recenter pose |

The head at room position `r` maps to `C + Lt * (s * r - s * Tc)`. `s * r` is
exactly the lean `t` that `head_pose.cpp` returns when the units per meter
are `s`. So:

```
P0 = C - s * Lt * Tc       where the recenter pose lands in the world
E  = P0 + Lt * t           the render head
R  = Lt * H                the render rotation
```

This is the formula of the life size view (`head_camera.cpp` hooks
`LH3DTech::UpdateCamera` `0x00819920`, and builds the room basis `L` from
world up and the heading of the camera that the game asked for), with the
asked position replaced by `P0` and `L` replaced by `Lt`. `head_pose.cpp`
stays at 7.5 units per meter. The table multiplies the lean by `s / 7.5` on
the main call only. The controllers, the sub cameras of the walk and a frame
without the table keep the life size scale, and a switch of the mode has no
frame of lag.

On the main call, `head_camera.cpp` gets the camera `(P0, P0 + Lt forward *
d)` and the lean gain `s / 7.5`. The game still records the camera it asked
for, and its basis is the game basis. The rotation clamp near vertical
(`kMinHorizontal`) stays. A player looking straight down at the table is
common, and the clamp holds the view half a degree short of vertical [M,
`backend.md`, "The zenith"].

**The eyes.** The separation is the player's IPD times `s`, or 66.8 mm times
`s` with no headset. A separation set by hand is multiplied by `s / 7.5`. A
change of the scale clears the taken IPD in `stereo.cpp`, so the new
separation is set. It is logged after a change of a tenth, and not on every
frame of a zoom.

**The heading.** `psi` is the heading of the game camera at the first table
frame. It stays until Ctrl F11 or a new land. The grips change it.

### Which calls get the table camera

`UpdateCamera` runs about five times a frame: the main call at the end of
`GCamera::Update` (`0x00442622`, return `0x00442627`) and sub cameras inside
the scene walk [M]. The callers in play [V]:

| Caller | What it is | In the table mode |
| --- | --- | --- |
| `GCamera::Update` `0x00442622` | the main call | the table camera |
| `fn_0068B740` `0x0068B7C5` | a particle effect gives the engine a camera near the world origin: position (0, 0.2 d, 0), focus (0, 0, d) | as in the life size view. Whether it needs the lean is open |
| `fn_0068B7D0` `0x0068B7DA` | puts the stored camera back, from copies at `0x00D4EAF0` and `0x00D4EAE0` | it passes the stored camera, which is the render head inside the bracket, and it goes through without a second head |
| `fn_005DAEE0` `0x005DB120` | the hand demo | as in the life size view, held by `script_camera.cpp` |
| `InnerCamera::PreDraw` `0x007969CD` | the Temple interior | the table stands aside inside the Temple. The game skips the main call there already (`[game+0x205A28]`) |
| `RunAlexFX`, `Plasma::Render`, `StatsBox` | the intro effects, the loading plasma and a menu | not in play |

`h_GCameraUpdate` in `pointer_map.cpp` shows the camera code the game camera
from the last main call (`HeadCameraUseGameCamera`). That record stays the
real game camera in the table mode, not the synthetic one. So the table
branch records the real asked camera, and keeps `P0` and `Lt` in fields of
their own. `spirit_anchor.cpp` takes the table's `P0` from them.

### The stored camera and the render bracket

The Hand casts its ray from the stored camera `0x00EA1DB8`:
`ScreenPointToWorldAtDistance` `0x0074CAF0` returns the camera plus a
direction times a distance [M, `hand.md`]. At the table scale the game camera
is thousands of units from the eye, and the lean alone is hundreds. So inside
the rendering the stored camera is the render head `E`.

Outside the rendering it is the game camera, because the logic reads it: the
sound listener, the script natives and the help triggers. The bracket is:

- **Start:** the main `UpdateCamera` call. The table camera is built, and `E`
  and `E + forward * d` are stored.
- **Inside:** `GInterface::PreDrawProcess` (the land under the pointer),
  `GGame::Process3dEngine` (both eyes, the Hand, the pick, the objects, the
  spirits), `GInterface::PostDrawProcess` and `HelpSystem::PostDrawProcess`.
  This is the order in `GGame::ProcessGraphicsEngine` `0x0054D850` [V].
- **End:** `frame_timers.cpp` hooks `GGame::ProcessGraphicsEngine` (thiscall,
  two stack arguments, `ret 8` at `0x0054D8C9` [V]) and calls
  `TabletopGraphicsDone` after it, which calls `HeadCameraUseGameCamera`. That
  rebuilds the matrices and the stored camera from the game camera. The game
  logic and the script natives then see the game camera, including the render
  matrix `0x00EA9E40` that `IsPosFieldOfView` reads. The table does not start
  without that hook, because MinHook refuses a second hook on one function.
  [I for the complete list of readers inside the bracket.]

The cost is one more `UpdateCamera` call a frame.

### Who reads the game camera

From the readers of `0x00EA1DB8` and `0x00EA1DC4` and the callers of
`GGame::GetCamera` [V unless marked]:

| Reader | What it reads | What a far camera does |
| --- | --- | --- |
| Sound listener, `fn_004270D0`, once a game turn from `GAudio::ProcessAudioGameTurn` `0x00427080`, through the import slot `0x008A971C` | position `0x00EA1DB8`, focus `0x00EA1DC4`, up = column 1 of `0x00EA1D28` | `GAudio::PlaySoundEffect` `0x00429E30` does not start a sample past its maximum distance. Animation sounds need 1200 or less (`fn_006D11A0`). Proximity music needs 100 or less (`fn_00427660`). A listener 1500 units or more from the land hears almost nothing |
| Ambient sound, `GSoundMap` `0x0071D800` | distance from the camera to each surface type | fades with distance [I for the thresholds] |
| `HelpSystem::SendFOVObject` `0x005C9090` | a cone of about 40 degrees around the line from position to focus, and a depth below `[0x00D16170]` | with a far camera the depth test likely fails, so the advisors stop commenting on what the player looks at [I] |
| `HelpProfile::ProcessSpecialTriggers` `0x005C45A0` | the camera pitch and zoom | help events 0x2D and 0x2E |
| Script natives `GetCameraPos` `0x006ECFF0`, `GetCameraFocus`, `GetFacingCameraPosition` `0x006EE710`, `StoreCameraDetails`, `GameThingCanViewCamera` `0x006FE4D0` | `0x00EA1DB8`, `0x00EA1DC4` | a story that stores and restores the camera must get the game camera, never the table eye |
| `IsPosFieldOfView` `0x006F8060`, `IsGameThingFieldOfView` `0x006F8130` | the render matrix `0x00EA9E40` and a screen test | with the table view, nearly the whole island is "in view". The end of the bracket fixes this |
| `GInterface::Process` `fn_005D2250` | position and focus, sent as a packet when they change | none |
| `GCamera::Update` | the stored camera as a start point in some modes | guarded by `h_GCameraUpdate` [M, `backend.md`] |
| Map object list `fn_005E5CD0`, landscape textures, near plane | `0x00EA1DB8` | render side |

No simulation detail depends on the camera distance: nothing in the readers
slows villager, Creature or physics updates for a far camera [I].

### The game camera on the table

The logic works when the game camera is where a player would put it: over the
land, 200 to 600 units away. A camera parked high over the island center
fails three ways: the sound goes quiet, `GCamera::Update` pulls any camera
more than 3500 from (2560, 0, 2560) back in over 3 seconds (the height counts,
`0x0044224A` to `0x0044232F`, the test against 1.225e7 at `0x00442288` [V]),
and the help triggers lose their depth test [I].

In the God view the mod puts the game camera over the player's place after
each update of `CameraModeNew3` (`god-view.md`, "The game camera"). **On the
table the game camera is left free.** The mouse, the stick, the wheel and the
double click jump move the game camera, which the table does not show, and
only the sound and the help follow it. The game camera over the table (focus
`C`, heading `psi`, a pitch of about 50 degrees, a distance of about 400
units, written after the 3500 check and before the main call, in the player's
camera modes only) is not built. A story camera must stay free, because
`HAS_CAMERA_ARRIVED` never answers yes if a script cannot reach its target.

## What the engine does at the table scale

### The distances

With the table 80 cm forward and 40 cm down from the head, and `D` 1.5 m,
the eye is 0.40 m from the near edge and 1.60 m from the far edge. At 2000
units per meter that is **800 to 3200 units**, and up to 4500 for Land5. A
lean brings the eye to 400 units or less. The game was tuned for a camera
100 to 700 units from what it shows.

### The mechanisms and the values

| Mechanism | How it works | At the table | What the mod does |
| --- | --- | --- | --- |
| Fog | Computed on the CPU, not in Direct3D. No call sets a fog render state [V]. Objects through `fn_007FEB30`, the land at `fn_00874AA0+0x1BB` [V]. The factor is the depth along the view axis, clamped between start `0x00C37220` and end `0x00C37224` [V]. The sky sets both every frame (`fn_0086A330` from `GLandAlignement::DrawSky` `0x005E222E`): 400 to 900 in clear weather, down to 15 to 350 in rain [V] | Everything past 900 is fully fogged | Writes 0 to `0x00C37204` [V]. This is the game's own "Fog" setting (`BWSetup`, read at `0x008239E2`). With it at 0, `fn_007FEB30` returns its input and `fn_00877210` marks every block unfogged |
| Near plane `0x00E839E0` | Written every frame at `0x004424B4` from `LandFeature::GetNearClipping` `0x005E2F30`: 3.5 above 20 units of height, down to 0.3 near the ground. The code after it sets `0x00C3812C` and `0x00C38130` from it [V] | Fine for clipping. Poor for depth precision | `near_plane.cpp` hooks `GetNearClipping` (no arguments, float in ST0, plain `ret` [V]). `god-view.md`, section 8, has the values. On the table the game's value stays |
| Far plane | None. `ProjectPoint` tests only near and the screen. `SetProjMatrix` gets `FLT_MAX` in `StartFrame` and only the hardware path uses it [V] | Nothing is cut by distance | None |
| View cone | `CosHalfFov` at `0x00E83A10` and `0x00E83A08` are written by `ChangeFov` and read by nothing [V]. Objects are tested as a screen circle in `fn_00868C80`, blocks by their 8 corners in `fn_00877210` [V] | Correct for any view | None |
| Block cull `fn_00877210` | Rejects a block only when all 8 corners are behind the near plane or outside one side of the screen [V] | The whole island passes | The radius test of "Land blocks past a radius" |
| Landscape detail | Two lines on the ground in front of `0x00EA1DB8` along `0x00EA1DD4`, at depths 1600 (`0x00E9C4FC`) and 2500 (`0x00E9C4D4`), band 275. `LH3DIsland::Create` writes them at `0x00803E1B` and `0x00803E7B` [V]. Stride 1, 2 or 4 cells from `[block+0x930]` in `fn_00874AA0` [V] | Most of the island draws at stride 2 or 4 [I] | Writes 1e6 to both. Every block then gets stride 1 [V logic, I result] |
| Landscape textures | A third line at `0x00E9C508`, depth at `0x00E9C524`, 400 to 800 set in `Create`, band 85. Blocks past it use the low resolution atlas of the `.lnd` (`+0x938 = 1`). `TextureUpdateThread` `0x00871F00` builds 6 tiles per call, nearest to `0x00EA1DB8` first [V] | The whole island shows the low resolution atlas | Writes 1e6 to `0x00E9C524` unless `table_textures=0`. Every block then gets a 128 or 256 pixel texture, 16 to 64 MB for Land5 [I]. The first frames build them 6 at a time |
| Map objects | `fn_005E5CD0` fills `GLandscape::DrawObjects` from the visible blocks, near to far, and stops at a block distance of `[0x00C37200]` = 700 [V at `0x005E5DE6`]. The same 700 is used at `0x005E4A99` and in the shadow fade `fn_00874600` [V]. Scripts change it through `SetVanishObjectDist` `0x0087FD40` [V] | No trees, houses or villagers at all | Writes 8000 to `0x00C37200` every frame |
| Object list size | At most 2999 entries in the array of 3000 at `0x00D1D28C` (tests at `0x005E5E21`, `0x005E5E71`, `0x006E690F`). Rebuilt when the stored camera moves more than 1 unit, or every 10 turns [V] | A whole island can pass 2999 and lose the far objects. At the table scale 1 unit is 0.5 mm, so the list is rebuilt on almost every frame [I] | The mod counts the list once in each eye pass. A full list gets a log line. Growing the array means moving it [I] |
| Model detail | `LH3DObject::AddForDrawing` `0x00815A70` compares depth against 23.3, 66.7, 86.7 and 173.3 times a size figure [M, `sky_detail.cpp`]. The size figure is (`obj[4]` + 1) times the radius times the detail slider `0x00C3813C`, capped at 5.0 by `0x00C37EC8` [V]. Alpha fades from 86.7 to 173.3 times the size | The test is an angle, so small things correctly drop out. But the cap removes every mesh past 173.3 * 5 * 3 = 2600 units, with `detail_distance=3` | Writes 30 to `0x00C37EC8` |
| Transparent objects | `LH3DZSorter::NewZObject` `0x0083F310` has 2048 entries a frame and drops the rest silently [V] | Smoke, water effects and foliage over a whole island may pass it [I] | The mod counts the sorter once in each eye pass. A full list gets a log line |
| Reflection | `fn_007FF4F0` draws every block again, mirrored, when `0x00E9CD8C` is set [V] | A mirrored island below the table plane, past the sea disc [I] | Writes 0 to `0x00E9CD8C` |
| Scanline sea fade | Alpha from 255 at depth 7000 to 80 at 14000 (`0x00C39904`, `0x00C39900`) [V] | Not used. The plain sea draws | None |

The values that the table writes while it owns the frame:

| Value | Address | The game | The table |
| --- | --- | --- | --- |
| Fog | `0x00C37204` | 1 | 0 |
| Map objects stop | `0x00C37200` | 700 | 8000 |
| Size cap of the model detail | `0x00C37EC8` | 5 | 30 |
| Land detail lines | `0x00E9C4FC`, `0x00E9C4D4` | 1600, 2500 | 1e6 |
| Land texture line | `0x00E9C524` | 400 to 800 | 1e6, unless `table_textures=0` |
| Reflection | `0x00E9CD8C` | as set | 0 |
| The Hand's reach | `CHand+0x4838` | 1800 | 5600 |

`TabletopApplyFrame` writes all of them once a frame, beside
`WaterModeApply`. Each value keeps the game's own value and puts it back when
the table does not own the frame: after Ctrl F11, in the Temple, and in any
frame with no main camera call. A value that the game writes while the table
is on (a level load, a script) becomes the value to put back. The Hand's
reach is 5600 and not more because `SetDistanceFromView` gives the Hand a
size of 0 at 5650 units and turns it inside out past that. The God view
writes the same values scaled by `s / 7.5` (`god-view.md`, section 8).

### Depth precision

`Draw3DScreenTriangle` writes `z = 1 - near / cz` [M, `stereo-artifacts.md`].
The depth step is `cz^2 / (near * 2^bits)`. The device takes the first Z
buffer format the driver lists with no stencil, and `0x00C386D8` is 1 when it
is 16 bit [V]. `depth24=1` (the default) asks for a 24 bit buffer
(`backend.md`, "The depth buffer").

| Buffer | Near | Step at cz 800 | Step at cz 3200 |
| --- | --- | --- | --- |
| 16 bit | 3.5 | 2.8 units, 1.4 mm | 45 units, 22 mm |
| 16 bit | 100 | 0.1 units | 1.6 units, 0.8 mm |
| 24 bit | 3.5 | 0.01 units | 0.17 units |

With 16 bits and the game's near plane, houses flicker into the ground at the
far edge. A near plane of about 5 cm in the room (`s * 0.05`, 100 units at
2000 units per meter) would help, but it needs the fix of the interface z
below. The table mode keeps the game's near plane. The flat interface
computes `rhw = near / z` from its own z (`SetupThing::DrawBox`,
`Draw3DScreenTriangle` `0x0081C5FC`, CameraHelp, HelpSystem). With a near of
100 those batches get a z below 0 [I]. So the game's near must come back
before the interface draws, or `draw_hooks.cpp` must rewrite the z of
interface batches. The God view solves this with a near plane that is small
in the room (`god-view.md`, section 8).

### The frame cost

The scene walk costs 5.3 to 6.3 microseconds per draw call, and the vertex
count matters much less [M, `backend.md`, "What the scene walk is made of"].
At 90 Hz there is room for about 850 calls per eye. At 72 Hz there is room
for about 1100.

The whole of Land 1 on the table is about 3100 draw calls for both eyes
against 810 in the life size view [M]. The scene walk takes about 9 ms against
4.3 ms. A head that leans in costs 2 ms more, because the models near the eye
keep more detail. The lists stay under their limits: at most 2017 of 2999 map
objects and 97 of 2048 sorted objects in one pass. The fit of the land takes
0.5 ms. A lean over the table is at the edge of a 90 Hz frame. The larger
lands are not measured. If a land is too slow, the levers are, in this order:

1. Keep `detail_distance` at 1 in the table mode.
2. Draw the blocks that hold only sea at stride 4.
3. Drop objects smaller than a set number of pixels.

Nothing may differ between the two eyes, so no per eye trick is allowed
(`bw-vr-architecture-decisions`).

## The land fit

At the first table frame of a land, the mod asks `LH3DIsland::GetAltitude`
for every cell point of every block. A point above 0 is land. `C` is the
middle of the land, `R far` is the distance to the farthest land point, and
the blocks with land are marked. Land 1 takes 0.5 ms for 100 blocks. Then:

```
s = 2 (1 + margin) R far / D        R = s D / 2 is the edge
```

For a fitted table `R` is `R far` plus the margin. `table_units_per_meter`
and `table_center_x` and `table_center_z` replace the fit.

## The edge of the table

### What draws past the edge

| Source | Evidence |
| --- | --- |
| The sky dome, `LH3DSky::Draw` `0x0086B010`, placed once at (2560, 0, 2560) with scale 5, a sphere with a radius of about 240000 units. It sets `ZFUNC` to `ALWAYS` and draws the moon, the dome and the sun disc | [V]. Its only caller is a tail `jmp` at `0x005E2233` [V] |
| The sea, drawn first as a background with `ZFUNC ALWAYS` and no depth write | `0x005E4E8C` calls the water before the land at `0x005E4E96` [V] |
| Blocks past the radius, and the flat part of blocks on the edge. Cells with an altitude byte of 3 or less draw flat at y 0 | [V] |
| Map objects on those blocks | they come from the listed blocks [V] |
| Clouds and ground mist, `GWeather::DrawClouds` `0x0083FC90` and `LH3DMist::Draw` `0x007FA300`. They hang 300 to 500 units up, which is 15 to 25 cm over the table | [V] for the functions, [M, `sky_detail.cpp`] for the height |
| The reflection pass | off, see above |
| A full screen tint `fn_0086FEE0` when `0x00FA51DB` is set, and the letterbox bars | [I] |
| The Creature, flying things and particles | not map objects |

The game does not clear the color buffer in play. `GLandscape::Draw` sets
the clear mode `0x00C3871C` to 0 on every frame at `0x005E48A4` [V]. Mode 0
clears only two frames every 2 seconds, and the clear color `0x00EC7F08` is
never set, so it is black [V]. The sky dome is the background.

### Land blocks past a radius

The mod hooks `fn_00877210`, the per block cull that `LH3DIsland::PreDraw`
`0x007FF2D0` calls once per block [V]:

- `__fastcall`, the block in ecx, no stack arguments, a plain `ret`
  (`0x00877CDC`, `0x00877D03`) [V].
- The block's corner is at the floats `+0x90C` (x) and `+0x910` (z). The
  center is 80 units in from both [V].
- To reject a block, the hook does what the original reject path at
  `0x00877CDD` does: `+0x930 = 0`, `+0x920 &= ~5`, `+0x934 = 4`, and it
  returns without calling the original [V].

A rejected block leaves the land, the reflection and the object list [V]. The
hook drops a land block when all of it is past `R`. It drops a block of sea
only when its middle is past `R`, because the flat parts of edge blocks would
otherwise show past the disc by up to 226 units. A block with land stays while
any part of it is inside `R`.

The cut is 160 units wide, 8 cm at 2000 units per meter. That does not matter
while the whole island fits inside the radius, because then only sea blocks
cross the edge. When the table zooms in, land crosses the edge too, by up to
one block: 160 units, which is 4 m at 40 units per meter. A clean circle
through land needs a stencil mask. User clip planes do not work on XYZRHW
vertices [I].

### The sea as a disc

The mod uses the plain sea. It writes 0 to `0x00C38228` (`water_simple=1`,
`water_mode.cpp`). The scanline sea is a screen space band from the horizon
down, and it cannot follow a disc [V]. The table and the God view force the
plain sea.

The plain sea `fn_0087A090` [V]:

- `__cdecl`, one argument, the material. Plain `ret`.
- Four corners at (-70000, 0, 70000) to (70000, 0, -70000), in writable
  memory at `0x00FA92E0`, filled once under bit 0 of `0x00FA9310`.
- The texture repeats every 2800 units. The scroll is at `0x00FA9390` and
  `0x00FA9394`. The color is `0x00C398FC`.
- `ZFUNC ALWAYS`, no depth write, both restored after.
- It submits through `fn_0081C090`: vertex count in ecx, positions in edx,
  then six stack arguments (colors, texture coordinates, triangle count,
  indices, material, flags), `ret 0x18`. Flags 0 means "use `0x00EA9E40`",
  the eye matrix. It holds about 8200 vertices.

The mod replaces it with a fan of 64 triangles of radius `R` around `C`,
through the same `fn_0081C090` with the same states, and the texture scale
`u = s0 + (x + 70000) / 2800` and `v = t0 + (70000 - z) / 2800` [I].
Clouds and mist with their center past `R` are not drawn. With the key color
on, no cloud or mist draws (`sky_detail.cpp`).

## The key color

Virtual Desktop takes any RGB color as the passthrough mask, with a
Similarity slider for the tolerance and a Smoothness slider for the edge.
Players of flight simulators use 255, 0, 255 with a Similarity of about 20
percent (see Sources). Magenta survives the path: the mod picks an sRGB
swapchain first (`xr_screen.cpp`) and copies bytes, so the encoded 255, 0,
255 stays 255, 0, 255. It is also exact in a 16 bit R5G6B5 target.

With `table_key_color` set, the sky still draws first, as the game draws it.
The plain sea is not opaque, and a clear in place of the sky would show the
key color through it and tint the coast and the cloud edges. Right after the
sky, a device clear of the key color covers every pixel row outside the
outline of the sea disc on the screen of the eye. The clear is a list of
pixel rows, from the 64 rim points through the eye matrix, clipped at the
near plane (`table_math.h`, `RowSpans`). It leaves no pixel inside the
outline, and the pixels it leaves outside touch the outline. The clear comes
before the land, so a mountain that stands up past the outline still draws.
The request for the sun glare goes, because the glare needs `0x00FA275C`,
which only the sun disc sets [V].

What can still leak or break:

- A screen fade darkens the key color too, so the room goes dark during a
  fade. The quad at `0x0082F614` has a diffuse of 0 and only resets depth
  [V], so it is not a fade by itself.
- Interface drawn inside the walk, such as the hint beside the Hand
  (`0x00447450`), draws before a key quad would.
- The video encoder blends colors at the edge of the key. That is what the
  Similarity and Smoothness sliders are for.
- The game's own art may contain the key color. A capture can count exact
  key pixels inside the table disc, and that count must be near 0.
- Rain and snow are likely world particles near the camera (`GClimate`) [I].
  Near the render head they would fall in the player's face.

The mod cannot turn the mask on. The launcher must tell the player how: in
Virtual Desktop, turn on the passthrough mask, set the mask color to the key
color, and start at a Similarity of 20 percent.

### Not built: the ring and the depth key

A flat ring at y +0.5 from `R` to `R + 320` (two blocks), drawn with `ZFUNC
LESSEQUAL` and a depth write, would cover the flat parts of edge blocks past
`R` in a table color or the key color. A depth key would be one screen quad at
the depth of a plane `table_key_depth_cm` from the eye (250 cm), `ZFUNC
LESSEQUAL`, no depth write, in the key color. It would paint every pixel that
nothing nearer than that plane covers. Both need a draw of the mod's own in
each eye pass (`table_draw.cpp`, not built):

1. Transform world points through the current eye matrix `0x00EA9E40`.
2. Clip each polygon against `cz >= near` in camera space before the divide.
3. Project as `ProjectPoint` does: `x = (1 + cx / cz) * half_x`,
   `y = (1 - cy / cz) * half_y`, `rhw = near / cz`, `z = 1 - rhw`, with FVF
   `0x1C4` [M, `software-tnl.md`].
4. Save each render state and texture stage state it changes with
   `GetRenderState`, and put back the exact value. The game caches states
   (`0x00ECA1F0`, `0x00EC81F0`, and `ZFUNC` at `0x00ECA24C`), and a cache that
   is wrong makes the game skip a state it needs [I].

The end of `LH3DRender::FinishFrame` shows where to draw [V]:

| Address | What |
| --- | --- |
| `0x0082F480` | `fn_0082F280`, the flush of the sorted list and the deferred list at `0x00EE9D38` |
| `0x0082F4A7` to `0x0082F4C7` | the FinishFrame callbacks without the flag `0x80000000` |
| `0x0082F4C9` | the sun glare `fn_0086BB60`, which `sky_detail.cpp` already hooks. It runs in every pass |
| `0x0082F614` | a screen quad with `ZFUNC ALWAYS` that resets the depth to 1.0 |
| `0x0082F6AE` | the letterbox bars |
| `0x0082F6ED` onward | the callbacks with the flag, which draw the interface over the reset depth |

The glare hook is the place: all the world is drawn, the depth buffer still
holds it, and the interface has not drawn. The same draw would also serve the
teleport ring, which `overlay.cpp` draws another way (`god-view.md`, section
14.5).

## The spirits beside the table

The game places each spirit through `HelpDude::ScreenToWorld` `0x005BD2A0` at
a screen point of plus or minus 0.66 and a depth of
`(1 + 0.3 p) * lerp(+0x35B8, +0x35BC)` [V]. The data files `markgood.hd` and
`markevil.hd` give `+0x35B8` = 8.733 and `+0x35BC` = 6.03 or 5.64, times 1.2
after `HelpDudeControl::Init` [V]. So a spirit sits 7 to 11 units from the
camera. `HelpDude::Update1` copies the camera rotation into the spirit's
matrix at `+0x3350` and scales it by `+0x35B4` (0.0255 or 0.0268) times
`+0x37E4` (about 0.8) [V]. The size of the spirit is that scale times the
size of its model (`HelpDude + 0x10`, 63.4): 1.36 units for the good spirit
[M].

At 7.5 units per meter that is a spirit of 18 cm, 0.9 to 1.5 m away. At
2000 units per meter it would be 0.7 mm across, 5 mm from the eye, and behind
the near plane. So while the table or the God view owns the view, the
spirits' size (`+0x35B4`) and depth range (`+0x35B8`, `+0x35BC`) are `s / 7.5`
times the game's values (263 times at 1973 units per meter). `ScaleSpirits`
in `spirit_anchor.cpp` writes them one time for each frame, before the game
places the spirits. The spirits then have the size and the distance in the
room that they have off the table: in a desktop run the good spirit was 18 cm,
1.27 m from the head [M]. The game's values go back when the table goes off, and a value that
the game writes while the table is on becomes the one to put back. The
spirits' light (`SpiritLight`) gets the same factor on its depth.

The readers of these fields [V, a scan of the displacements]: the size builds
the mesh matrix in `HelpDude::Update1` and sizes the glow in
`HelpDude::Draw` (`fn_005C0700`). The depth range places the spirit in
`HelpDude::ScreenToWorld`. The other readers use only the ratio of the size to
the near depth, which the factor does not change.

The mod does not choose the place of a spirit. The game places it at its
screen point and its depth, and the mod gives the game the camera for that:
the room camera of `spirit_anchor.cpp` (`BuildRoomCamera`). It stands at `P0`
with the table's basis, it looks along the latched yaw and pitch of the head,
and it has no roll. Then the spirits stay fixed in the room when the player
leans or walks, which is right for things that float in the room. The latched
pitch stays between 0 and 15 degrees (`LimitPitch`), so the room camera looks
level or a little up, and never down at the table. The screen points of plus
or minus 0.66 in a 110 degree render are about 43 degrees left and right of
the latched direction, which is beside a 1.5 m table at 1 to 1.5 m.

The spirits are not at eye level. In a desktop run of the God view, with the
head 30 cm right and 40 cm forward of the recenter place, the good spirit at
its usual place was 83 cm below the head and 1.27 m from it, which is about
40 degrees below eye level [M]. The room camera does not look down, so the
screen point of the game is that far below the middle of the view [I]. No
headset session has checked where the spirits are for a real head.

A grab of the table moves `P0` with the land, so the spirits stay where they
are in the room. On the table the jumps and turns of the game camera are not
cuts. The table going on or off is a cut. With the passthrough mask on, the
spirits are drawn over the real room, because they are nearer than the key
depth.

### A spirit at a place of the world

A script can send a spirit to a place of the world. The script words
`SPIRIT_POINT_POS` (`GScript::SpiritPointAtPos` `0x00710510`) and
`SPIRIT_POINT_GAME_THING` (`0x007105B0`) have a last value, "in world". With
0 the spirit points from its place on the screen. With 1 it goes to the
place [V]. `HelpDudeControl` keeps that value in one byte for each spirit
(`+0x2C`), with the place (`+0x34`), a distance to the side (`+0x1C`) and a
height (`+0x24`) (`fn_005C3960`). `HelpDudeControl::Update` gives them to the
spirit in each frame (`fn_005BC4A0`).

`HelpDude::Update1` `0x005BDDA0` then moves an amount (`+0x35DC`) from 0 to 1
in one second, and back to 0 when the script lets the spirit go. With the
ease `e = (1 - cos(pi t)) / 2` (`fn_005BD250`) the spirit is at
`usual + (place - usual) * e`, and the scale of its mesh is `1 + 3 e` times
the usual scale (`0x005BE943` to `0x005BE96A`) [V]. So at the place the
spirit is 4 times as large as at its usual place. [I: in the game a place of
the world is far from the camera, and the spirit must not be too small
there.] From an amount of 0.5 `HelpSystem::Draw3D` `0x005C59A0` draws the
spirit, and below 0.5 the callback of the interface layers does
(`fn_005C3920`) [V].

The scripts that give 1 are the reminders of a scroll and some stories [V,
`Scripts\Quests\challenge.chl`]: `CreatureDevelopmentHighlightNotify` and
`CreatureDevelopmentHighlightNotifyTwo` (the scrolls of the Creature
lessons), `QuestHighlightNotify`, `QuestReminder`,
`CreatureDevInterimReminder`, `CreatureDevLeashAttachToHouse`,
`SwapCreatures`, `GremlinAttackWarning`, `LeaveThroughVortexL1` and
`HelpTalkAndPointInWorld`. A reminder of a Creature lesson starts when the
camera is nearer to the scroll than a radius and the scroll is in the view,
at most one time in 30 s. In the God view the camera of that test is the
player (`god-view.md`, section 16). `ChallengeHighlightNotify` and its
copies give 1 too, but their distance test is never true (`god-view.md`,
16.1).

The good spirit is 18 cm in the room at its usual place (the scale times the
model size `HelpDude + 0x10`, 63.4) [M]. With the size of the table, 4 times
the usual size is a spirit of 73 cm in the room at each scale. The place is
on the land, about 1 m from the head, so the spirit fills the view. So while
the table or the God view owns the view, the scale at the place follows a
rule of the mod (`spirit_world_size=1`, `spirit_size_math.h`):

- At the place the spirit has the angle of size that it has at its near
  depth (`+0x35B8`), seen from the room camera at `P0`. So a lean of the head
  does not change the size. The factor of the scale is the distance from the
  room camera to the place, divided by the near depth.
- The factor is at most the 4 of the game, and at least 1/4. A place more
  than 4 near depths away (4.7 m of the room) gets the size of the game.
- On the way the scale goes from the usual scale to the scale of the place,
  with the ease of the game.

The mod changes one instruction for this. The `fmul` at `0x005BE947` reads
the 3 of `1 + 3 e` from the constant at `0x008C2C50`, and the mod makes it
read a number of the mod. The hook of `HelpDude::ScreenToWorld` sets that
number for each spirit at the call of `Update1` that gives the usual place
(return address `0x005BE6D7`). The factor is decided one time for each
frame, so both eyes get one size. Off the table, and with
`spirit_world_size=0`, the number is the 3 of the game. The mod changes the
instruction only when its six bytes and the constant are the expected ones.
If they are not, the key is 0, and a line with the tag `hooks` says so.

The distance to the side of the place and the height above it stay in units
of the world, as the script gives them.

With `test_keys=1`, Shift and Home change `spirit_world_size` between 1 and
0. The pointing controller gives a long pulse for 1 and a short pulse for 0.

The log has one line for each change of what a spirit does: it "leaves for a
place of the world", "is at the place of the world", "returns to its usual
place" and "is at its usual place".

```
advisors     : spirit_world_size=1. On the table and in the God view a spirit at a place of the world has the angle of size that it has at its near depth. Shift and Home change it
advisors     : spirit 0 is at the place of the world. It is at 1878.4 29.7 3156.0, amount 1.00. Scale 1.2281, 0.85 times the usual scale. Size 77.89 units, 15.4 cm of the room at 506.2 units for a meter. From the head 500.3 units, and the angle of the size from the head is 8.9 degrees. From the room camera 500.3 units, from the game camera 716.6 units. spirit_world_size=1
```

The size in a line of the log is the scale times the model size. The figure
in the picture is about 20 percent higher than that.

**A desktop run.** With the start "Keep your old Creature" no script gives
1, because the Creature lessons do not run. So the runs use a test,
`spirit_point_world_test=1`, which sets the byte "in world" of a spirit that
points at a place from the screen. `PointOutScroll` of the missionaries makes
such a point when the player is nearer than 100 units to 1755 2692. The run:
the start "Keep your old Creature", four zoom steps out (506 units for a
meter, with `god_zoom_about=0`), two turns left, and the test word `go` to
1779 2779. The good spirit then goes to the scroll at the ark, 1 m of the
room in front of the head and 39 degrees below the middle of the view. The
test `spirit_world_size_test=N` changes the key at frame `N`, so one run
has both sizes [M]:

| `spirit_world_size` | Scale | Size in the room | Angle from the head |
| --- | --- | --- | --- |
| 0 | 4.00 times the usual scale | 72.6 cm | 40.0 degrees |
| 1 | 0.85 times the usual scale | 15.4 cm | 8.9 degrees |

At 100 units for a meter the same place is 4.5 m of the room away. The rule
gives 3.87 times the usual scale there, which is 8.9 degrees too.

## The interface

The menus, the help text and the Creature panel draw flat on the view, the
same in both eyes, and `draw_hooks.cpp` shifts them for depth [M,
`stereo-artifacts.md`]. The shift follows the nearest world depth, up to
`ui_shift_max_px` (24). A table can be much nearer than the interface sits,
so the interface can look behind the island when the player leans in [I].
`ui_scale_pct` and the subtitle angle still apply. The interface on its own
surface in the room is in `ui-layer.md`.

## The pointer and the Hand

With the stored camera at the render head, the Hand's ray starts at the eye
that drew the picture, so the Hand lands on the land under the pointer. The
object pick is a screen test at the pointer pixel inside the eye pass [M,
`hand.md`], so it follows the render camera. The land ray has no distance
limit: `fn_00800C30` marches cell by cell to the map border [V]. The render
pick has none either. It resets its best distance to `FLT_MAX` every frame
(`0x005CEA7F`) [V].

- **Lean moves the mouse's Hand.** `LockToRoom` (`pointer_map.cpp`) turns the
  cursor by the head rotation only. A lean of 10 cm is 0.75 units at 7.5
  units per meter and 200 units at 2000, so a lean slides the mouse's Hand
  across the island. A lock that holds the Hand's world point (project the
  point of the last frame, `0x00C5E680`, through the new render camera) is not
  built. The controller's Hand does not slide with a lean.
- **The Hand's reach.** `hand+0x4838` is 1800, set at `0x0046BC49` from the
  constant at `0x008CBEAC` [V]. `fn_0046DF60` clamps the Hand's distance to
  it [V]. The table writes 5600 every frame, because `fn_0046BF20` (from
  scripts) can clamp it back [V].
- **The Hand's size.** `SetDistanceFromView` `0x0046C0D0` sets the scale
  `+0x4834` to `(d / 150) * (1 - 0.3 * (d - 150) / 1650)` above 150 units
  [V]. That reaches 0 at 5650 and turns the Hand inside out after. A hook sets
  the scale for a fixed size in the room (see "The hand on the table").
- **Mouse speed.** One render pixel is a fixed angle, about 1 mm at 1 m. That
  is 2 units at 2000 units per meter. It needs no change.

## The hand on the table

The direct hand of `hand.md` works on the table with `play_mode=3` and the
table on.

**The map.** `controllers.cpp` asks `TabletopRoom` for the table of this
frame: `P0`, `Lt` and the gain `s / 7.5`. Every room point of
`head_pose.cpp` is multiplied by the gain, and the table's origin takes the
place of the game camera (`TableHand` in `controller_math.h`):

```
E    = P0 + Lt * (gain * lean)
grip = E + Lt * (gain * (t_grip - t_head))
```

With full six degrees of freedom that is `C + s * Lt * (room - Tc)`, the map
of "The table camera". So a real hand on the table top touches the land under
it. A hand 20 cm right of the table center and 30 cm past it is at sea level,
394.5 units right of `C` and 591.8 units forward, at 1973 units per meter. A
head that moves 1 cm, 5 cm, or 3 cm along each axis, and turns 20 degrees,
moves a still grip by 0.0003 units at most. The room points that the motion
tests, the throw and the grab keep stay at the life size scale. Only the
world point changes.

**No reach curve.** The whole land is within arm's reach, so the Go-Go reach
is off on the table. The Hand is where the hand is.

**The Hand's size.** `table_hand_cm` (10) is the Hand's length in the room.
The size is `0.10 * s / 3.2`, which is 61.6 at 1973 units per meter, and it
follows the zoom. The reasons for 10 cm:

- It is about half a real hand (19 to 21 cm), so the Hand does not hide the
  village under it. On the table a villager is 2 to 5 mm tall and a house is
  about 1.5 cm.
- The game keeps the Hand half its length over the land. Half of 10 cm is
  5 cm, which is about how high the controller's grip is over the table when
  the fingers touch it. (The mod lets the point come down to 5 mm over the
  land. On a real table the grip still stops about 5 cm over the top. See
  `hand.md`.)
- The grab radius of the pick, 0.6 of the length, is 6 cm. A hand does not
  have to hit a villager of 3 mm.

**The pick.** The render pick offers an object to `SendObjectDrawCollision`
only when the pick pixel is inside the object's circle on the screen.
`fn_00868C80`, the screen test of each object, sets `0x00EA1AD0` then, and
`Game3DObject::AddForDrawing` reads it at `0x0063B653` before the offer [V].
On the table a villager is about 3 pixels across, so nothing could be picked.
With `play_mode=3` and 4, a post hook on `fn_00868C80` (fastcall, the bounds
in ecx, the object on the stack, `ret 4` [V]) also sets that flag for an
object in view within twice the grab radius of the Hand, about 12 cm, in the
first eye pass only. The draw then offers it. With a distance of 0,
`SendObjectDrawCollision` asks the mesh whether the pick pixel hits it, so the
game's own pick gets no false hit. The candidate list holds 4096 objects on
the table and 256 off it. The nearest holdable object within the grab radius
goes into the next record. Two more changes make the grip pick up:

- **The choice holds the record.** The hand mode chooser `fn_005D7F20` reads
  the next record (`+0x400`) [V]. On the table the pixel pick can find a
  larger object behind the villager. So while the empty Hand has a holdable
  choice, the choice goes into the record at the start of each eye pass too,
  and `SendObjectDrawCollision` is not called for other objects.
- **The land under the Hand.** `fn_005D1120` sets `GInterface+0x48` from the
  player's influence at the land point of the next record (`+0x3F0`) [V].
  With it clear, `Object::InterfaceMustBeInInfluenceForInteraction` turns
  hand mode 9 (pick up) into 0x12 (touch), and the grip does not pick up. On
  the table that land point is where the pointer ray meets the land behind
  the Hand. A pre hook on `fn_005D1120` (thiscall, no arguments, plain `ret`
  [V]) writes the land point under the Hand there, while the controller
  places the Hand on the table. The influence, the hand mode and the drop then
  follow the Hand. Outside the influence the mode is 0x12, as the game wants.

**The throw.** `TableThrowVelocity` turns the speed of the real hand into
units at the table scale: the grip speed times `throw_gain_pct` times
`s / 7.5`, then the game's cap of 124. At 1973 units per meter and a gain of
150, a hand at 4.2 cm/s already reaches the cap. So every real throw goes at
the cap, in the direction of the hand. The game sets an object down when its
horizontal speed is 2 units per second or less. On the table the tremble of a
still hand, about 1.25 cm/s, would be 37 units per second, which is a throw.
So the table sets the object down whenever the life size scale would: when
the horizontal speed of the real hand at 7.5 units per meter, times the gain,
is 2 or less. With the gain of 150 that is a hand slower than 18 cm/s.

**The Creature.** The stroke and slap test keeps the speed of the real hand
at the life size scale, so a stroke and a slap need the same hand motion as
off the table.

**The word "seek".** In a desktop run it asks for a wider pick, 50 cm of the
room around the Hand, for 3 frames. Two frames later it steps the fake hand
onto the nearest holdable object to the Hand. The room point is the table map
backward (`TableRoomPoint`). The test word `shot` takes a picture of both
eyes 3 frames later.

## Moving the table by the grips

While the table owns the view, the grips move the land on the table, not the
game camera. The table top stays where it is in the room. The buttons, the
claim rule and the keys of the grab are in `god-view.md`, section 5.

- **One hand** (the other hand's grip): the land slides with the hand. Only
  the motion across the table counts. A hand that moves up or down moves no
  land. In the God view the same hand also moves the land up and down inside
  the ground limits (`god-view.md`, section 3).
- **Two hands** (both grips): the land turns about up with the hands, and it
  scales with their spread. Hands twice as far apart make the land twice as
  large, so `s` halves. The land point under the middle of the hands stays
  under it, and each hand keeps its own land point.
- **The solve** is from the press every time (`TableGrabSolve` in
  `table_math.h`), one to one:

  ```
  psi = psi_a - theta
  s   = s_a / k, kept between 40 units per meter and the scale of the fit
  C   = W - s * Lt(psi) * (m - Tc), with the height left out
  ```

  `W` is the land point under the middle of the hands at the press. `m` is
  the middle of the hands in the room. `theta` and `k` are the turn and the
  spread ratio of `GrabSolve`, with the same freeze below a spread of 8 cm.
  Only the grips go in. So a head that moves with still hands moves nothing, a
  second solve in one frame gives the same table, and nothing moves after the
  release. The smoothing of the grips (`grab_smoothing_ms`) stays. The gain of
  the grab on the game camera does not apply.
- **The anchors.** A new anchor starts at a press, when the hands change,
  after a grip jump of 50 cm, at a recenter, and when the table is built
  again. The grab ends when the table goes on or off.
- **The limits.** `C` stays on the map, 0 to 5120. The zoom stops at 40 units
  per meter, where a village fills the table, and at the scale of the fit,
  where the whole land fits.
- **The game camera stays the game's.** A double click jump flies the game
  camera and not the table, so it does not end a grab of the table.
- `TabletopMove` builds the basis, `P0`, the edge and the sea disc again from
  the new `C`, `psi` and `s`. The eyes, the Hand's size, the ears and the
  spirits follow `s` in the same frame.

Not built: the moves of the table by the mouse, the stick, the wheel and the
double click jump. They still move the game camera, which the table does not
show. The routing would be: a left drag pans the table, a snap turn turns
`psi` about `C`, the wheel steps `s` about the point under the Hand with `C' =
A - (s' / s) * (A - C)`, and a double click sets `C` to the flight focus (the flight target is at
`+0x110` and `+0x11C`). A key that sets `C` to the game camera's focus, a key that fits the
whole land again, and `table_follow_story` for story cuts are not built.

## The ears on the table

The ears go straight down from the render head to at most 400 units over the
land under them, and they stay inside the edge of the table. They turn with
the head. A head lower than that, for example when the player leans in or
zooms in, is the ears itself (`TabletopEars`). The render head is 800 to 3200
units from the land, and animation sounds stop at 1200. An ear at the game
camera would stay at an invisible camera when the player walks round the
table, so a sound in front of the player could come from behind. Over the land
under the head, a sound on the left of the head is on the left of the ears,
and the player hears the part of the island that they lean over, as with a
real model. 400 units is about the distance from the game's camera to what it
looks at.

The game still decides from the game camera which sounds start and stop
(`audio.md`). A sound far from the game camera does not start, even under the
head. A sound that the game glues to its camera comes from the game camera's
place. The advisors' polar angle comes from the render head itself, because
the spirits float in the room around the head.

## The throw from inside the influence

**The cause.** The game lets go of an object only when the land point of the
release is inside the player's influence [V]:

1. `fn_005D1120`, the interface step, sets `GInterface+0x48` from
   `Influence::CalculatePlayerInfluence` at the land point `+0x3F0`
   (`0x005D1144` to `0x005D1164`). Then it runs the step of the interface
   action `+0x44` (the table of steps at `0x00D17DA8`, `0x30` bytes an
   action). The step of action 12 is the release action `FUN_005D4DB0`.
2. The release action goes to its fail path at `0x005D5150` when `+0x48` is
   0 (`0x005D4E24`). The fail path turns the throw spring off
   (`HandStateHolding+0x104`) and sends no packet `0x4D`.
3. `FUN_005D1850` then calls the drop sender `FUN_005D3340`. It tests `+0x48`
   again, and without it `fn_005D18F0` refuses the drop: a sound and packet
   `0x2B`, and no packet `0x12`. So the drop action `fn_005DA400` never
   runs, the Hand keeps the object, and the hand mode goes back to `0x18`.

On the table the hook on `fn_005D1120` puts the land under the Hand in
`+0x3F0`. So the test is at the Hand at the release. A swing carries the Hand
`s` units over the land for each meter of the real hand. At 1973 units per
meter a step of 2 cm carries the Hand 39.5 units, which can be past the edge
of the influence [I]. At the life size scale a swing of 40 cm is 3 units, so a
throw almost never leaves the influence on the way. No other test between the
release and the drop depends on where the Hand is: `ValidToApplyThisToMapCoord`
of a `Mobile` is always 1, the land point is on the map, and
`Villager::IsAvailable` and the hand count of `fn_005DA400` do not change with
the throw [V]. The mouse has the same rule. The game keeps an object that the
pointer releases outside the influence.

**The fix** (`hand_direct.cpp`, `ThrowFrom`). The hook on `fn_005D1120` knows
when the step runs the release of a throw: bit `0x40` of `+0x39`, action 12 in
`+0x44`, an object in the Hand that is not a miracle, and a velocity that the
game throws (more than 2 units per second across the land). Then, when the
land under the Hand is outside the influence, the hook looks back along the
Hand's path. The path has one land point a frame. The hook takes the newest
point of the last 0.5 s that is inside the influence, and puts it in `+0x3F0`
in place of the land under the Hand. The game then finds `+0x48` set, and the
release action and the drop run as they do for the mouse.

- The object still flies from the Hand. `ThrowObjectFromHand` takes the start
  point and the velocity from packet `0x4D` (the held object and
  `CHand+0x48C8`), not from `+0x3F0`. For a `Mobile`, `ApplyThisToMapCoord`
  does not use the land point at all.
- A set down keeps the game's rule: a still hand outside the influence keeps
  the object. So does a throw whose whole last 0.5 s was outside it.
- A miracle keeps the game's rule, because its land point is where it acts.
- It acts only with play mode 3 and 4, while the table or the God view owns
  the view and the controller places the Hand. The life size throw of the
  immersive view does not change.
- The search calls `CalculatePlayerInfluence` once for each point, newest
  first, and only for a throw that ends outside. It is latched once a frame.

The log tag `hand throw` gives one line for each such throw: where it ends,
the point it goes from, and how far back that is in time and in units. The
report of the window counts the throws that went from the path. The math
test checks `ThrowFromIndex` in `table_math.h`: a swing of 30 cm at the table
scale ends outside and goes from the last point inside, the same swing at the
life size scale stays inside, a hand outside for the whole window gets no
point, a point older than the window does not count, and the newest point
inside wins.

**Not run in the game.** The run to make: `play_mode=3`, `table_sweep=1`, the
points `-15,-18,94 -16,-31,94 0,-20,41 -22,-35,95 10,-35,95`, and the script
`point@480+1 trigger@520+8 point@600+1 trigger@640+8 point@3400+1 a@3440+4
point@3610+1 seek@3630+1 grip@3660+20 grip@3740+6 nudge@3745+1
seek@3800+1 grip@3820+20 grip@3880+20 seek@3920+1 grip@3940+20
grip@3990+6 slap@3995+1`. The table is on from frame 3600 to 4049. Look for
the `hand throw` line "outside your influence. It goes from", then "the Hand
holds 00000000" about 6 frames after the release, and effect 46 of the
haptics. The throw with the slap of 12 cm (237 units) tests a longer swing.

## Keys

All in `[bwvr]`. Only the play mode is in the launcher.

| Key | Default | Meaning |
| --- | --- | --- |
| `play_mode` | 4 | 3 is the tabletop mode. The default, 4, is the God view (`god-view.md`) |
| `table_diameter_cm` | 150 | The table in the room. It is also the edge |
| `table_forward_cm` | 80 | The table center, ahead of the recenter pose |
| `table_down_cm` | 40 | The table top, below the recenter pose |
| `table_units_per_meter` | 0 | 0 fits the land to the table. Any other value is the scale |
| `table_margin_pct` | 10 | The room at the edge of a fitted table |
| `table_center_x`, `table_center_z` | -1 | The land point at the table center. -1 is the middle of the land |
| `table_textures` | 1 | 1 gives every land block its full texture |
| `table_key_color` | empty | A hex color, for example `FF00FF`. Empty or 0 keeps the sky |
| `table_sweep` | 0 | A test: the table goes off and on at each report window (every 450 frames in the runs), so one run measures both |
| `table_hand_cm` | 10 | The length of a controller's Hand on the table, in the room, 2 to 40 |
| `spirit_world_size` | 1 | 1: a spirit at a place of the world has the angle of size that it has at its near depth. 0: the size of the game, 4 times the usual size. On the table and in the God view only. Shift and Home change it with `test_keys=1` |
| `spirit_world_size_test` | 0 | A test with no key press: at this frame of the controllers the test key of `spirit_world_size` acts. 0 is never |
| `spirit_point_world_test` | 0 | A test for a desktop run: 1 sends a spirit that points at a place from the screen to that place of the world |

The fake lean keys are `fake_head_right_cm`, `fake_head_up_cm` and
`fake_head_forward_cm`, a still lean for the fake head, in the room.

**Ctrl F11** turns the table or the God view off and on, and logs which is
live. The game reads Ctrl as its zoom only with a mouse move, so a key pressed
with the mouse at rest is safe.

## The code

| File | What it holds |
| --- | --- |
| `src/mod/tabletop.cpp`, `tabletop.h` | The table state, the land fit, the hooks of the edge and the sky, the values of "What the engine does at the table scale", the room of the table, the Hand's size, the table pose for the grab, and the ears |
| `src/mod/table_math.h` | The table transform, the fit, the edge test, the sea disc, `RowSpans`, `HandSize`, `FlatWorld`, `TableGrabSolve`, `InsideEdge`, `EarsHeight` and `ThrowFromIndex`, as pure functions |
| `src/mod/head_camera.cpp` | The table takes the main camera call, and the stored camera keeps the render head |
| `src/mod/stereo.cpp` | The eyes of the table, and Ctrl F11 |
| `src/mod/frame_timers.cpp` | The end of the render bracket, after `GGame::ProcessGraphicsEngine` |
| `src/mod/sky_detail.cpp` | No clouds past the edge, and the key color clear |
| `src/mod/controllers.cpp`, `controller_math.h` | On the table, the hand goes into the world through the table, with no reach curve (`TableHand`, `TableRoomPoint`, `TableThrowVelocity`) |
| `src/mod/hand_direct.cpp` | The Hand's size, the pick and the throw on the table |
| `src/mod/world_grab.cpp` | The grips move the table |
| `src/mod/audio_listener.cpp` | The ears on the table |
| `src/mod/spirit_anchor.cpp` | The spirits beside the table |
| `src/mod/mod_main.cpp`, `src/launcher/launcher.cpp` | The keys, and the play mode row |
| `test/controller_math/main.cpp` | The checks of the table map, the hand, the throw, the grab and the ears |

## Known limits and open questions

1. **The frame cost.** The whole island at once can pass 90 Hz on the larger
   lands. The object list stops at 2999, and the Z sorter drops past 2048,
   both silently.
2. **The render bracket.** The stored camera feeds 184 readers of the position
   and 38 of the focus [M, `backend.md`]. Some readers inside the bracket may
   be logic and not drawing. A log of what each reader got, for one frame,
   would settle it.
3. **The logic sees a different camera from the player.** The sound, the help
   triggers and the tutorials follow the game camera, which the table leaves
   free. A player who looks at a corner of the table hears the middle.
4. **Depth precision** with a 16 bit buffer.
5. **Sub cameras inside the walk.** The particle camera at `0x0068B7C5` gets a
   lean that is 270 times larger. It needs a capture with that effect on
   screen.
6. **Key color leaks:** fades, interface drawn in the walk, rain, and the
   encoder at the edge.
7. **Story scenes lose their framing.** The story camera shows a shot, and the
   table ignores it. Scripts that frame a close shot play out small on the
   table.
8. **Other rooms.** The Temple and the challenge rooms draw from other
   cameras. The table stands aside there and comes back after, as a cut.
9. **Texture memory and build time** when every block gets a full texture.
10. **The edge cuts whole land blocks,** so when the table zooms in, land
    stands past the edge by up to one block.

Open questions:

- How many draw calls a land block takes in `fn_00874AA0`.
- How many map objects each land has, against the cap of 2999.
- Whether the fan through `fn_0081C090` keeps the look of the plain sea.
- Whether the particle camera at `0x0068B7C5` needs the lean at all.
- Where rain and snow spawn, relative to which camera.

Needs the headset: the size of the table in the room, the eye separation of
130 units, the frame rate at 90 Hz, the look of the relief, the height of the
table, the edge of the Virtual Desktop mask with the key color, the Hand's
size and the ears by eye and ear, the grab of the table with real hands, the
spirits beside the table (no advisor has appeared in a desktop run of the
table), the size of a spirit at a place of the world (no run of the table,
and no run with a script that sends the spirit there by itself), and a
throw once the throw fix has run. Also open: the table's place in the real
room. The recenter pose moves the table with the head, which is right in a
virtual room and wrong on a real desk. `table_down_cm` and `table_forward_cm`
set it by hand. A later version could set `Tc` by touching the desk with a
controller, and a recenter must then not move the table [I].

## Sources

- Virtual Desktop passthrough mask settings, as players use them:
  [Passthrough settings in VD](https://forum.falcon-bms.com/topic/28968/passthrough-settings-in-vd),
  [Mixed reality proof of concept](https://forum.falcon-bms.com/topic/25786/mixed-reality-proof-of-concept-and-new-virtual-desktop-with-passthrough-mask).
