# Tabletop mode: research and design

This is step T1 of Track 2 in `roadmap.md`, researched on 2026-09-29. It
finds the scale of the island and the place of the camera. It lists what the
engine culls and fogs at that scale, and how to cut the world at the edge of
the table. Then it gives a staged design for T2 to T5.

The tabletop mode shows the island as a model on a table in the room, about
1.5 m across. The player walks around it and leans over it with full head
tracking. With the Virtual Desktop passthrough mask, everything past the table
is one key color, so the island stands on the real desk.

The God view (play mode 4, `god-view.md`) uses the same camera and the same
code, with the land at the player's waist and no edge.

Sections 1 to 9 are the design, written before anything was built or run.
Section 10 says what was built, how it differs from the design, and what
the desktop runs showed. Every claim of the design carries a mark:

| Mark | Meaning |
| --- | --- |
| [V] | Verified in the disassembly or in the game data this session |
| [M] | Measured in an earlier session, with the document named |
| [I] | Inferred. Test it before relying on it |

## Summary

- **Scale.** A land is 1400 to 3300 units across and at most 171 units
  high. To fit a 1.5 m table the scale is 1100 to 2800 units per meter,
  about 2000 for Land 1. That is 270 times the 7.5 units per meter of the
  life size mode. A 1 cm head movement is 20 units, two map cells.
- **Camera.** The table camera is the formula `head_camera.cpp` uses today,
  with two inputs changed. The room origin maps to a world point `P0` over
  the table, not to the game camera. The lean is scaled by the table scale.
  The game camera keeps running for the game logic.
- **The engine is tuned for a camera 100 to 700 units away.** At 800 to
  3200 units each of these fails:
  - the fog, which ends at 900,
  - the map objects, which vanish past 700,
  - the landscape detail, which drops at 1600 and 2500,
  - the landscape textures, which are low resolution past 400 to 800,
  - the Hand, which stops at 1800,
  - the big models, which are gone past 2600.

  Each is one value in memory, listed in section 3.
- **The edge.** A hook on the per block cull `fn_00877210` drops land blocks
  past a radius. The plain sea becomes a disc. The mod draws its own ring at
  sea level past the radius to hide the rest.
- **The key color.** In play the game does not clear the frame. The sky
  dome is the background. Replace `LH3DSky::Draw` with a device clear in the key color,
  and draw one screen quad at a far depth as a safety net.
- **Biggest risk.** The frame cost. The whole island is in view at once.
  The walk costs about 5.8 microseconds per draw call, and each eye pass
  makes its own calls. The first gate of T2 is a measurement.

## 1. The size of a land

### 1.1 The grid

| Fact | Value | Evidence |
| --- | --- | --- |
| Cell size | 10 world units | `MapCoords::Set` `0x00603340` multiplies by 6553.6, which is 65536 / 10 [V] |
| Map | 512 by 512 cells, so 5120 by 5120 units | `LH3DIsland::GetAltitude` `0x00803090` rejects cells outside 0 to `0x1FF` [V] |
| Blocks | 32 by 32 blocks of 16 cells, 160 units | index table `0x00E9C964`, block pointers `0x00E9C564`, 17 by 17 cells per block [V] |
| Height | altitude byte times 0.67, so 0 to 171 units | float `0x00C3720C` at `0x0080332F` [V] |
| Sea level | y 0. An altitude byte of 3 or less reads as 0 | `0x008031B0` and the tests after it [V] |
| Map center | (2560, 2560) | the sky dome, the 3500 pull back and the land pick clamp all use it [V] |

The sea is not in the land data. It is `LH3DWater`, a plane at y 0 (see
4.3). Outside the land blocks there is nothing but that plane.

### 1.2 The lands, measured

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
them match their slot in the index table, so the parse is sound [V]. The
script is `lnd.py` in the research scratch folder. The mod can compute the
same numbers at run time from the block table in memory, which avoids reading
the file.

### 1.3 The scale

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

Recommendation: fit the land, not the map. The map is mostly sea. The
default is `D` 1.5 m, and the scale comes from the land at each level load.

### 1.4 What a head movement means

At 2000 units per meter:

| Room | World |
| --- | --- |
| 1 cm of head movement | 20 units, two cells |
| The eye separation, 64 mm | 128 units |
| A lean of 50 cm | 1000 units, half of Land1 |
| An eye 20 cm above the land | 400 units away |

So the head is a very large lever. A still head is a steady view, and the
6DOF parallax is strong at this scale, which is what makes a model read as a
model. The head tracking in `head_pose.cpp` needs no change for this. Only
the scale changes.

### 1.5 The island is flat

A peak of 171 units on a land 2170 wide is a relief of about 8 percent. On a
1.5 m table the highest mountain is 6 to 11 cm. That is the true shape of
the land. A vertical exaggeration was considered and rejected for now:

- The height unit at `0x00C3720C` is writable, but `GetAltitude` uses it for
  the game logic, and it has 713 callers [V].
- A scale in the render matrix alone would not reach `LH3DSprite::Draw`,
  which reads the unscaled matrix, or `UnprojectScreenPoint`, which casts the
  Hand's ray [V from `stereo-artifacts.md` and `backend.md`], [I for the
  outcome].

## 2. The camera

### 2.1 What the mod does today

`head_camera.cpp` hooks `LH3DTech::UpdateCamera` `0x00819920`. For each call
it builds the room basis `L`: world up, and the heading of the camera the
game asked for (`head_camera.cpp:284` to `304`). The render rotation is
`R = L * H` (`head_camera.cpp:334`). The render position is the asked
position plus `L * t`, where `t` is the lean (`head_camera.cpp:370` to
`375`). `head_pose.cpp:323` to `326` makes `t` from the lean in meters times
`g_units_per_meter`, which is 7.5 (`head_units_per_meter_x10=75`,
`mod_main.cpp:546`).

After the call, the hook writes the asked camera back into the stored camera
at `0x00EA1DB8` and `0x00EA1DC4` (`head_camera.cpp:479` to `486`). The
renderer keeps the head, and the game logic sees its own camera.

### 2.2 The table camera is the same formula

The table is a similarity map from the room to the world. The room is the
frame `head_pose.cpp` already uses: meters, left handed, the origin at the
recenter pose, and only the yaw of that pose kept.

```
world = C + s * Lt * (room - Tc)
```

| Symbol | Meaning |
| --- | --- |
| `C` | the table center in the world, on the sea plane, y 0 |
| `s` | the table scale, in units per meter |
| `Lt` | the table basis: world up, and a heading `psi` in the world for the room forward |
| `Tc` | the table center in the room, for example 80 cm forward and 40 cm down from the head at recenter |

The head at room position `r` maps to `C + Lt * (s * r - s * Tc)`. `s * r` is
exactly the lean `t` that `head_pose.cpp` returns when `units_per_meter` is
`s`. So:

```
P0 = C - s * Lt * Tc       where the recenter pose lands in the world
E  = P0 + Lt * t           the render head
R  = Lt * H                the render rotation
```

This is the formula of 2.1, with the asked position replaced by `P0` and `L`
replaced by `Lt`. So the table camera needs three changes and no new math:

1. `HeadPoseSetScale(s)` while the mode is on, and 7.5 again when it is off.
2. On the main call, give the existing code the synthetic camera
   `(P0, P0 + Lt.forward * d)`, where `d` is the distance to `C`. It builds
   `L = Lt` from that heading and applies the head as today.
3. Hand back the render head `E` as the stored camera, not `P0` (see 2.4).

The rotation clamp near vertical (`kMinHorizontal`, `head_camera.cpp:217`)
stays. A player looking straight down at the table is common here, and the
clamp holds the view half a degree short of vertical [M, `backend.md`, "The
zenith"].

The eye separation needs one fix. `stereo.cpp:608` to `620` sets the
separation to `ipd * HeadPoseScale()`, but only with a headset, and only when
the IPD changes by more than 0.5 mm. A change of scale does not trigger it.
So a change of `s` must clear `g_taken_ipd`. The fake head path never sets
the separation at all. Until the fix is in, a desktop test must set
`eye_separation_milli` to `66.8 * s`, for example 133600 at 2000 units per
meter.

### 2.3 Which calls get the table camera

`UpdateCamera` runs about five times a frame: the main call at the end of
`GCamera::Update` (`0x00442622`, return `0x00442627`) and sub cameras inside
the scene walk [M, `head_camera.cpp:40`]. The callers in play [V, `disasm.py
--callers 0x00819920`]:

| Caller | What it is | In tabletop mode |
| --- | --- | --- |
| `GCamera::Update` `0x00442622` | the main call | the table camera |
| `fn_0068B740` `0x0068B7C5` | a particle effect gives the engine a camera near the world origin: position (0, 0.2 d, 0), focus (0, 0, d) [V] | as today, but with the lean at 7.5 units per meter or none [I] |
| `fn_0068B7D0` `0x0068B7DA` | puts the stored camera back, from copies at `0x00D4EAF0` and `0x00D4EAE0` [V] | the table camera again. It passes the stored camera, which is the render head inside the bracket of 2.4, so the hook must see it as a table camera and not add the head twice [I] |
| `fn_005DAEE0` `0x005DB120` | the hand demo | as today, held by `script_camera.cpp` |
| `InnerCamera::PreDraw` `0x007969CD` | the Temple interior | tabletop mode is off inside the Temple. The game skips the main call there already (`[game+0x205A28]`, `pointer_map.cpp:547`) |
| `RunAlexFX`, `Plasma::Render`, `StatsBox` | the intro effects, the loading plasma and a menu [V] | not in play |

One more thing must stay apart. `h_GCameraUpdate` in `pointer_map.cpp:750`
shows the camera code the game camera from the last main call
(`HeadCameraUseGameCamera`, `head_camera.cpp:653`). That record must stay the
real game camera in tabletop mode, not the synthetic one. So the table branch
records the real asked camera as today, and keeps `P0` and `Lt` in fields of
their own. `spirit_anchor.cpp` needs the table's `P0` (see 5.1).

### 2.4 The stored camera, and a render bracket

The Hand casts its ray from the stored camera `0x00EA1DB8`:
`ScreenPointToWorldAtDistance` `0x0074CAF0` returns the camera plus a
direction times a distance [M, `hand.md`, 1]. Today the mod writes the game
camera back there. At 7.5 units per meter the lean is a few units, so the ray
starts close enough to the eye. At the table scale the game camera is
thousands of units from the eye, and the lean alone is hundreds. So inside
the rendering the stored camera must be the render head `E`.

Outside the rendering it must be the game camera, because the logic reads it:
the sound listener, the script natives and the help triggers (2.5). The
proposal is a bracket:

- **Start:** the main `UpdateCamera` call. The table camera is built, and `E`
  and `E + forward * d` are stored.
- **Inside:** `GInterface::PreDrawProcess` (the land under the pointer),
  `GGame::Process3dEngine` (both eyes, the Hand, the pick, the objects, the
  spirits), `GInterface::PostDrawProcess` and `HelpSystem::PostDrawProcess`.
  This is the order in `GGame::ProcessGraphicsEngine` `0x0054D850` [V].
- **End:** a post hook on `GGame::ProcessGraphicsEngine` (thiscall, two stack
  arguments, `ret 8` at `0x0054D8C9` [V]) calls `HeadCameraUseGameCamera`.
  That rebuilds the matrices and the stored camera from the game camera. The
  game logic and the script natives then see the game camera, including the
  render matrix `0x00EA9E40` that `IsPosFieldOfView` reads (2.5). [I for the
  complete list of readers inside the bracket.]

The cost is one more `UpdateCamera` call a frame.

### 2.5 Who reads the game camera

From the readers of `0x00EA1DB8` and `0x00EA1DC4` and the callers of
`GGame::GetCamera` [V unless marked]:

| Reader | What it reads | What a far camera does |
| --- | --- | --- |
| Sound listener, `fn_004270D0`, once a game turn from `GAudio::ProcessAudioGameTurn` `0x00427080`, through the import slot `0x008A971C` | position `0x00EA1DB8`, focus `0x00EA1DC4`, up = column 1 of `0x00EA1D28` | `GAudio::PlaySoundEffect` `0x00429E30` does not start a sample past its maximum distance. Animation sounds need 1200 or less (`fn_006D11A0`). Proximity music needs 100 or less (`fn_00427660`). A listener 1500 units or more from the land hears almost nothing |
| Ambient sound, `GSoundMap` `0x0071D800` | distance from the camera to each surface type | fades with distance [I for the thresholds, set at run time] |
| `HelpSystem::SendFOVObject` `0x005C9090` | a cone of about 40 degrees around the line from position to focus, and a depth below `[0x00D16170]` | with a far camera the depth test likely fails, so the advisors stop commenting on what the player looks at [I] |
| `HelpProfile::ProcessSpecialTriggers` `0x005C45A0` | the camera pitch and zoom | help events 0x2D and 0x2E |
| Script natives `GetCameraPos` `0x006ECFF0`, `GetCameraFocus`, `GetFacingCameraPosition` `0x006EE710`, `StoreCameraDetails`, `GameThingCanViewCamera` `0x006FE4D0` | `0x00EA1DB8`, `0x00EA1DC4` | a story that stores and restores the camera must get the game camera, never the table eye |
| `IsPosFieldOfView` `0x006F8060`, `IsGameThingFieldOfView` `0x006F8130` | the render matrix `0x00EA9E40` and a screen test | with the table view, nearly the whole island is "in view", so a script that waits for the player to look at something fires at once [I]. The end of the bracket in 2.4 fixes this |
| `GInterface::Process` `fn_005D2250` | position and focus, sent as a packet when they change | none |
| `GCamera::Update` | the stored camera as a start point in some modes | already guarded by `h_GCameraUpdate` [M, `backend.md`] |
| Map object list `fn_005E5CD0`, landscape textures, near plane | `0x00EA1DB8` | render side, see section 3 |

No simulation detail depends on the camera distance: nothing in the readers
slows villager, Creature or physics updates for a far camera [I, from the
reader list].

### 2.6 Should the game camera move

Yes, but not high. The logic in 2.5 works when the game camera is where a
player would put it: over the land, 200 to 600 units away. A camera parked
high over the island center fails three ways:

- The sound goes quiet (2.5).
- `GCamera::Update` pulls any camera more than 3500 from (2560, 0, 2560)
  back in over 3 seconds, and the height counts (`0x0044224A` to
  `0x0044232F`, the test against 1.225e7 at `0x00442288`) [V].
- The help triggers lose their depth test [I].

So the recommendation, for T4:

- **In the player's camera modes, the table owns the game camera.** After
  `CameraModeNew3::Update` (already hooked in `comfort_camera.cpp`), put the
  game camera over the table center: focus `C`, heading `psi`, a pitch of
  about 50 degrees, and a distance of about 400 units (`table_game_distance`).
  `comfort_camera.cpp` already does this kind of instant placement for snap
  turns, with `Zoomer::SetPosition` `0x00441AC0` on the six Zoomers and the
  mode's own copies. The write must come after the 3500 check and before the
  main call at `0x00442622` [V for the order].
- **In the story modes, the game camera stays free.** A script that moves
  the camera must reach its target, or `HAS_CAMERA_ARRIVED` never answers
  yes. `script_camera.cpp` already tells the two kinds apart by the mode on
  top of the stack.

In T2 the table is fixed and the game camera is simply left free. The
player's drags then move an invisible camera, and only the sound and the help
follow it. That is acceptable for a first look.

## 3. Near, far, fog and culling at the table scale

### 3.1 The distances

With the table 80 cm forward and 40 cm down from the head, and `D` 1.5 m,
the eye is 0.40 m from the near edge and 1.60 m from the far edge. At 2000
units per meter that is **800 to 3200 units**, and up to 4500 for Land5. A
lean brings the eye to 400 units or less. The game was tuned for a camera
100 to 700 units from what it shows.

### 3.2 What the engine does there, and what to change

| Mechanism | How it works | At the table | Change |
| --- | --- | --- | --- |
| Fog | Computed on the CPU, not in Direct3D. No call sets a fog render state [V]. Objects through `fn_007FEB30`, the land at `fn_00874AA0+0x1BB` [V]. The factor is the depth along the view axis, clamped between start `0x00C37220` and end `0x00C37224` [V]. The sky sets both every frame (`fn_0086A330` from `GLandAlignement::DrawSky` `0x005E222E`): 400 to 900 in clear weather, down to 15 to 350 in rain [V] | Everything past 900 is fully fogged | Write 0 to `0x00C37204` [V]. This is the game's own "Fog" setting (`BWSetup`, read at `0x008239E2`). With it at 0, `fn_007FEB30` returns its input and `fn_00877210` marks every block unfogged |
| Near plane `0x00E839E0` | Written every frame at `0x004424B4` from `LandFeature::GetNearClipping` `0x005E2F30`: 3.5 above 20 units of height, down to 0.3 near the ground. The code after it sets `0x00C3812C` and `0x00C38130` from it [V] | Fine for clipping. Poor for depth precision, see below | Hook `GetNearClipping` (no arguments, float in ST0, plain `ret` [V]) for the world only, see below |
| Far plane | None. `ProjectPoint` tests only near and the screen. `SetProjMatrix` gets `FLT_MAX` in `StartFrame` and only the hardware path uses it [V] | Nothing is cut by distance | None |
| View cone | `CosHalfFov` at `0x00E83A10` and `0x00E83A08` are written by `ChangeFov` and read by nothing [V]. Objects are tested as a screen circle in `fn_00868C80`, blocks by their 8 corners in `fn_00877210` [V] | Correct for any view | None |
| Block cull `fn_00877210` | Rejects a block only when all 8 corners are behind the near plane or outside one side of the screen [V] | The whole island passes | Add the radius test of 4.2 |
| Landscape detail | Two lines on the ground in front of `0x00EA1DB8` along `0x00EA1DD4`, at depths 1600 (`0x00E9C4FC`) and 2500 (`0x00E9C4D4`), band 275. `LH3DIsland::Create` writes them at `0x00803E1B` and `0x00803E7B` [V]. Stride 1, 2 or 4 cells from `[block+0x930]` in `fn_00874AA0` [V] | Most of the island draws at stride 2 or 4 [I] | Write about 1e6 to both after every `Create`. Every block then gets stride 1 [V logic, I result]. This also removes the stride choice that `stereo-artifacts.md` lists as open, because no block sits on a line |
| Landscape textures | A third line at `0x00E9C508`, depth at `0x00E9C524`, 400 to 800 set in `Create`, band 85. Blocks past it use the low resolution atlas of the `.lnd` (`+0x938 = 1`). `TextureUpdateThread` `0x00871F00` builds 6 tiles per call, nearest to `0x00EA1DB8` first [V] | The whole island shows the low resolution atlas | Write about 1e6 to `0x00E9C524`. Every block then gets a 128 or 256 pixel texture, 16 to 64 MB for Land5 [I]. The first frames build them 6 at a time |
| Map objects | `fn_005E5CD0` fills `GLandscape::DrawObjects` from the visible blocks, near to far, and stops at a block distance of `[0x00C37200]` = 700 [V at `0x005E5DE6`]. The same 700 is used at `0x005E4A99` and in the shadow fade `fn_00874600` [V]. Scripts change it through `SetVanishObjectDist` `0x0087FD40` [V] | **No trees, houses or villagers at all** | Write about 8000 to `0x00C37200` every frame |
| Object list size | At most 2999 entries in the array of 3000 at `0x00D1D28C` (tests at `0x005E5E21`, `0x005E5E71`, `0x006E690F`). Rebuilt when the stored camera moves more than 1 unit, or every 10 turns [V] | A whole island can pass 2999 and lose the far objects. At the table scale 1 unit is 0.5 mm, so the list is rebuilt on almost every frame [I] | Measure the count first. Growing the array means moving it [I] |
| Model detail | `LH3DObject::AddForDrawing` `0x00815A70` compares depth against 23.3, 66.7, 86.7 and 173.3 times a size figure [M, `sky_detail.cpp`]. The size figure is (`obj[4]` + 1) times the radius times the detail slider `0x00C3813C`, capped at 5.0 by `0x00C37EC8` [V]. Alpha fades from 86.7 to 173.3 times the size | The test is an angle, so small things correctly drop out. But the cap removes every mesh past 173.3 * 5 * 3 = 2600 units, with the mod's `detail_distance=3`. Large buildings at the far edge fade away | Raise the cap at `0x00C37EC8` to about 30 in tabletop mode. Keep `detail_distance` at 3 |
| Transparent objects | `LH3DZSorter::NewZObject` `0x0083F310` has 2048 entries a frame and drops the rest silently [V] | Smoke, water effects and foliage over a whole island may pass it [I] | Count at T2 |
| Reflection | `fn_007FF4F0` draws every block again, mirrored, when `0x00E9CD8C` is set [V] | A mirrored island below the table plane, past the sea disc [I] | Write 0 to `0x00E9CD8C` |
| Scanline sea fade | Alpha from 255 at depth 7000 to 80 at 14000 (`0x00C39904`, `0x00C39900`) [V] | Not used, see 4.3 | None |

### 3.3 Depth precision

`Draw3DScreenTriangle` writes `z = 1 - near / cz` [M, `stereo-artifacts.md`].
The depth step is `cz^2 / (near * 2^bits)`. The device takes the first
Z buffer format the driver lists with no stencil, and `0x00C386D8` is 1 when
it is 16 bit [V]. Which format dxwrapper lists first is not known.

| Buffer | Near | Step at cz 800 | Step at cz 3200 |
| --- | --- | --- | --- |
| 16 bit | 3.5 | 2.8 units, 1.4 mm | 45 units, 22 mm |
| 16 bit | 100 | 0.1 units | 1.6 units, 0.8 mm |
| 24 bit | 3.5 | 0.01 units | 0.17 units |

With 16 bits and the game's near plane, houses flicker into the ground at
the far edge. So:

1. Log `0x00C386D8` at the start of T2.
2. If it is 1, raise the near plane to about 5 cm in the room (`s * 0.05`,
   100 units at 2000 units per meter) through the `GetNearClipping` hook.
   That hook keeps `0x00C3812C` and `0x00C38130` consistent, and
   `UnprojectScreenPoint`, `fn_00868C80` and the water use near only as a
   ratio with them [V].
3. The flat interface computes `rhw = near / z` from its own z
   (`SetupThing::DrawBox`, `Draw3DScreenTriangle` `0x0081C5FC`, CameraHelp,
   HelpSystem). With a near of 100 those batches get a z below 0 [I]. So put
   the game's near back before the interface draws, or let `draw_hooks.cpp`
   rewrite the z of interface batches, which it already sees one by one.

### 3.4 The frame cost

Measured before [M, `backend.md`, "What the scene walk is made of"]: the
walk costs 5.3 to 6.3 microseconds per draw call, and the vertex count
matters much less. A heavy scene is about 900 draw calls per eye and 9.8 ms
for both. At 90 Hz there is room for about 850 calls per eye. At 72 Hz there
is room for about 1100.

Estimate for the table [I]:

| Part | Per eye | Both eyes |
| --- | --- | --- |
| Land at stride 1, one call per block, 100 to 190 blocks | 100 to 190 calls, 29000 to 54000 vertices | 1.2 to 2.2 ms for the calls, plus about 1 to 2 ms of vertex work |
| Map objects that pass the angular detail test | 400 to 1200 calls | 4.6 to 14 ms |
| Sea disc, sky clear, table ring, key quad | under 10 calls | about 0.1 ms |
| **Total** | **500 to 1400 calls** | **about 7 to 18 ms** |

So a small land fits 90 Hz and a large one may need 72 Hz. The first gate of
T2 is `profile=1` with a still fake head over Land1 and Land5. If it is too
slow, use these levers in this order:

1. Keep `detail_distance` at 1 in tabletop mode.
2. Draw the blocks that hold only sea at stride 4.
3. Drop objects smaller than a set number of pixels.

Nothing may differ between the two eyes, so no per eye trick is allowed
(`bw-vr-architecture-decisions`).

## 4. The edge of the table

### 4.1 What draws past the edge

| Source | Evidence |
| --- | --- |
| The sky dome, `LH3DSky::Draw` `0x0086B010`, placed once at (2560, 0, 2560) with scale 5, a sphere with a radius of about 240000 units. It sets `ZFUNC` to `ALWAYS` and draws the moon, the dome and the sun disc | [V]. Its only caller is a tail `jmp` at `0x005E2233` [V] |
| The sea, drawn first as a background with `ZFUNC ALWAYS` and no depth write | `0x005E4E8C` calls the water before the land at `0x005E4E96` [V] |
| Blocks past the radius, and the flat part of blocks on the edge. Cells with an altitude byte of 3 or less draw flat at y 0 | [V] |
| Map objects on those blocks | they come from the listed blocks [V] |
| Clouds and ground mist, `GWeather::DrawClouds` `0x0083FC90` and `LH3DMist::Draw` `0x007FA300`. They hang 300 to 500 units up, which is 15 to 25 cm over the table | [V] for the functions, [M, `sky_detail.cpp`] for the height |
| The reflection pass | 3.2 |
| A full screen tint `fn_0086FEE0` when `0x00FA51DB` is set, and the letterbox bars | [I] |
| The Creature, flying things and particles | not map objects |

The game does not clear the color buffer in play. `GLandscape::Draw` sets
the clear mode `0x00C3871C` to 0 on every frame at `0x005E48A4` [V]. Mode 0
clears only two frames every 2 seconds, and the clear color `0x00EC7F08` is
never set, so it is black [V]. The sky dome is the background.

### 4.2 Land blocks past a radius

Hook `fn_00877210`, the per block cull that `LH3DIsland::PreDraw`
`0x007FF2D0` calls once per block [V]:

- `__fastcall`, the block in ecx, no stack arguments, a plain `ret`
  (`0x00877CDC`, `0x00877D03`) [V].
- The block's corner is at the floats `+0x90C` (x) and `+0x910` (z). The
  center is 80 units in from both [V].
- To reject a block, do what the original reject path at `0x00877CDD` does:
  `+0x930 = 0`, `+0x920 &= ~5`, `+0x934 = 4`, and return without calling the
  original [V].

A rejected block leaves the land, the reflection and the object list [V].

Test the distance from `C` to the nearest point of the block's square, not
to its center. Then a block that touches the disc still draws, and the ring
of 4.5 hides its flat part past the edge.

The cut is 160 units wide, 8 cm at 2000 units per meter. That does not
matter while the whole island fits inside the radius, because then only sea
blocks cross the edge. When the table zooms in (T4), land crosses the edge
too. A clean circle through land needs a stencil mask. User clip planes do
not work on XYZRHW vertices [I].

### 4.3 The sea as a disc

Use the plain sea. Write 0 to `0x00C38228`. The mod already does this by
default (`water_simple=1`, `water_mode.cpp`). The scanline sea is a screen
space band from the horizon down, and it cannot follow a disc [V].

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

Replace it with a fan of 64 triangles of radius `R` around `C`, through the
same `fn_0081C090` with the same states. Keep the texture scale with
`u = s0 + (x + 70000) / 2800` and `v = t0 + (70000 - z) / 2800` [I]. Only
moving the four corners would give a square and squeeze the texture [I].

### 4.4 The mod's own geometry

The ring of 4.5 and the key quad of 4.6 are drawn by the mod, in each eye
pass, straight to the Direct3D 7 device at `0x00ECA638`. A new file,
`table_draw.cpp`, does four things:

1. Transform world points through the current eye matrix `0x00EA9E40`, which
   is the per eye matrix during the pass.
2. Clip each polygon against `cz >= near` in camera space before the divide.
   The player can lean over the ring.
3. Project exactly as `ProjectPoint` does: `x = (1 + cx / cz) * half_x`,
   `y = (1 - cy / cz) * half_y`, `rhw = near / cz`, `z = 1 - rhw`, with FVF
   `0x1C4`, as the game does [M, `software-tnl.md`].
4. Save each render state and texture stage state it changes with
   `GetRenderState` and put back the exact value after. The game caches
   states (`0x00ECA1F0`, `0x00EC81F0`, and `ZFUNC` at `0x00ECA24C`), and a
   cache that is wrong makes the game skip a state it needs [I].

Where to draw: the end of `LH3DRender::FinishFrame` shows the order [V]:

| Address | What |
| --- | --- |
| `0x0082F480` | `fn_0082F280`, the flush of the sorted list and the deferred list at `0x00EE9D38` |
| `0x0082F4A7` to `0x0082F4C7` | the FinishFrame callbacks without the flag `0x80000000` |
| `0x0082F4C9` | the sun glare `fn_0086BB60`, which `sky_detail.cpp` already hooks |
| `0x0082F614` | a screen quad with `ZFUNC ALWAYS` that resets the depth to 1.0 |
| `0x0082F6AE` | the letterbox bars |
| `0x0082F6ED` onward | the callbacks with the flag, which draw the interface over the reset depth |

So the glare hook is the place: all the world is drawn, the depth buffer
still holds it, and the interface has not drawn. The call at `0x0082F4C9`
runs in every pass, whether or not a glare is wanted [V]. In tabletop mode
the glare hook skips the glare and draws the ring and the key quad instead.

### 4.5 The ring

A flat ring at y +0.5 from `R` to `R + 320` (two blocks), drawn with
`ZFUNC LESSEQUAL` and a depth write. The half unit keeps it above the flat
y 0 cells, so it covers the flat parts of edge blocks past `R` without
fighting them. Land above the sea level is inside `R` by the choice of `R`.
In T3 the ring has a table color. In T5 it has the key color.

### 4.6 The key color

Virtual Desktop takes any RGB color as the mask, with a Similarity slider
for the tolerance and a Smoothness slider for the edge. Players of flight
simulators use 255, 0, 255 with a Similarity of about 20 percent (see
Sources). Magenta survives the path: the mod picks an sRGB swapchain first
(`xr_screen.cpp:807`) and copies bytes, so the encoded 255, 0, 255 stays
255, 0, 255. It is also exact in a 16 bit R5G6B5 target.

The plan, in order of what it covers:

1. **Replace `LH3DSky::Draw` with a clear.**
   `Clear(0, NULL, TARGET | ZBUFFER, key, 1.0, 0)` on the device, as the
   game's own `fn_0082EE70` does it [V for the call shape]. This also clears
   the depth, which the sky did with `ZFUNC ALWAYS` [I]. The sun glare needs
   `0x00FA275C`, which only the sun disc sets, so the glare goes too [V].
2. **Skip the clouds and the mist** past `R`, or all of them
   (`table_clouds`). The hook on `LH3DMist::Draw` exists.
3. **The sea disc and the block cull** of 4.2 and 4.3.
4. **The ring** of 4.5, in the key color.
5. **The depth key**, a safety net. One screen quad at the depth of a plane
   `table_key_depth_cm` from the eye (250 cm by default), `ZFUNC LESSEQUAL`,
   no depth write, in the key color. It paints every pixel that nothing
   nearer than that plane covers, whatever drew it.

What can still leak or break:

- A screen fade darkens the key color too, so the room goes dark during a
  fade. The quad at `0x0082F614` has a diffuse of 0 and only resets depth
  [V], so it is not a fade by itself.
- Interface drawn inside the walk, such as the hint beside the Hand
  (`0x00447450`), draws before the key quad. If it writes no depth, the key
  quad paints over it where it covers the background [I]. `draw_hooks.cpp`
  sees each such batch and can draw the key quad before the first one.
- The video encoder blends colors at the edge of the key. That is what the
  Similarity and Smoothness sliders are for.
- The game's own art may contain the key color. A capture can count exact
  key pixels inside the table disc, and that count must be near 0.
- Rain and snow are likely world particles near the camera (`GClimate`) [I].
  Near the render head they would fall in the player's face. Test in rain.

The mod cannot turn the mask on. The launcher must tell the player how:

1. In Virtual Desktop, turn on the passthrough mask.
2. Set the mask color to the key color.
3. Start at a Similarity of 20 percent.

## 5. The advisors and the interface

### 5.1 The spirits

The game places each spirit through `HelpDude::ScreenToWorld` `0x005BD2A0` at
a screen point of plus or minus 0.66 and a depth of
`(1 + 0.3 p) * lerp(+0x35B8, +0x35BC)` [V]. The data files
`markgood.hd` and `markevil.hd` give `+0x35B8` = 8.733 and `+0x35BC` = 6.03
or 5.64, times 1.2 after `HelpDudeControl::Init` [V]. So a spirit sits 7 to
11 units from the camera. `HelpDude::Update1` copies the camera rotation
into the spirit's matrix at `+0x3350` and scales it by `+0x35B4` (0.0255 or
0.0268) times `+0x37E4` (about 0.8) [V]. The spirit is about 1 unit across
[I].

At 7.5 units per meter that is a spirit of about 13 cm, about 1.2 m away. At
2000 units per meter it would be 0.5 mm across, 5 mm from the eye, and
behind the near plane. So tabletop mode must scale the spirits by
`k = s / 7.5`:

- Multiply `+0x35B8` and `+0x35BC` by `k`. The game then places the spirits
  at 1 to 1.5 m in the room.
- Multiply `+0x35B4` by `k`. The game then builds the matrix and the glow
  sprite at the life size in the room. `HelpDude::Draw` `0x005C0700` sizes
  the glow from `+0x35B4` too [V for the readers, I for the result].
- Keep the originals and put them back when the mode goes off.
- The spirits' light goes through `SpiritLight` (`spirit_anchor.cpp:627`),
  which can scale its depth argument by `k` too [I].

The room camera in `spirit_anchor.cpp` takes its position from
`HeadCameraGameCamera` (`spirit_anchor.cpp:472`). In tabletop mode it must
take `P0`, the recenter pose in the world. Then the spirits stay fixed in the
room when the player leans or walks, which is right for things that float in
the room.

Where they float: the screen points of plus or minus 0.66 in a 110 degree
render are about 43 degrees left and right of the latched direction. At 1 to
1.5 m that is beside a 1.5 m table. Today the latch keeps a pitch of -35 to
+20 degrees (`spirit_anchor.cpp:124`). A player at a table looks down, so a
latch at -35 degrees would stand the spirits on the table's edge. In tabletop
mode use a pitch of 0 to +15 degrees, so they float at eye level beside the
table.

With the passthrough mask on, the spirits are drawn over the real room,
because they are nearer than the key depth. That is the effect the mode
wants.

### 5.2 The interface

The menus, the help text and the Creature panel draw flat on the view, the
same in both eyes, and `draw_hooks.cpp` shifts them for depth [M,
`stereo-artifacts.md`]. That stays as it is in T2 to T5. Two notes for the
table:

- The interface shift follows the nearest world depth, up to
  `ui_shift_max_px` (24). A table can be much nearer than the interface
  then sits, so the interface can look behind the island when the player
  leans in [I]. This is the Track 3 problem, not a new one.
- `ui_scale_pct` and the subtitle angle still apply.

Plan for Track 3, not designed here:

1. In eye pass 0, send the interface batches to a texture of their own. The
   batches are the caller ranges in `draw_hooks.cpp` and the FinishFrame
   callbacks with the flag (4.4).
2. In eye pass 1, skip them.
3. Submit that texture as an `XrCompositionLayerQuad` at a fixed room pose,
   for example above the far edge of the table.
4. When the Hand's ray meets the quad, convert the hit to interface pixels
   and give the game that pointer.

Open problems: the pointer then has two owners, and a menu click must not
also reach the island.

## 6. Input

### 6.1 The pointer and the Hand

With the stored camera at the render head (2.4), the Hand's ray starts at the
eye that drew the picture, so the Hand lands on the land under the pointer.
The object pick is a screen test at the pointer pixel inside the eye pass
[M, `hand.md`, 2], so it follows the render camera. The land ray has no
distance limit: `fn_00800C30` marches cell by cell to the map border [V].
The render pick has none either. It resets its best distance to `FLT_MAX`
every frame (`0x005CEA7F`) [V].

Four things change at the table scale:

- **Lean moves the Hand.** `LockToRoom` (`pointer_map.cpp:340`) turns the
  cursor by the head rotation only. A lean of 10 cm is 0.75 units at 7.5
  units per meter and 200 units at 2000, so a lean would slide the Hand
  across the island. In tabletop mode the lock must hold the Hand's world
  point instead: project the Hand's point from the last frame (`0x00C5E680`)
  through the new render camera, and use that pixel.
- **The Hand's reach.** `hand+0x4838` is 1800, set at `0x0046BC49` from the
  constant at `0x008CBEAC` [V]. `fn_0046DF60` clamps the Hand's distance to
  it [V]. Past 1800 units, 0.9 m at 2000 units per meter, the Hand stops in
  the air. Write about 8000 there every frame, because `fn_0046BF20` (from
  scripts) can clamp it back [V].
- **The Hand's size.** `SetDistanceFromView` `0x0046C0D0` sets the scale
  `+0x4834` to `(d / 150) * (1 - 0.3 * (d - 150) / 1650)` above 150 units
  [V]. That reaches 0 at 5650 and turns the Hand inside out after. In
  tabletop mode a post hook sets the scale for a fixed size in the room,
  `table_hand_cm` (12 cm by default, tuned by eye). The Hand is also kept
  above the land by half of 3.2 times the scale [M, `hand.md`], which stays
  small in the room.
- **Mouse speed.** One render pixel is a fixed angle, about 1 mm at 1 m. That
  is 2 units at 2000 units per meter. It needs no change.

### 6.2 The camera modes

The land drag, the snap turns, the double click jump and the zoom steps all
move GCamera (`comfort_camera.cpp`, `pointer_map.cpp`). In tabletop mode the
picture does not follow GCamera. Each of these must move the table instead,
and each already has a place where the mod steps in:

| Player action | Where the mod already acts | In tabletop mode |
| --- | --- | --- |
| Left drag on the land | `h_GCameraUpdate`, `h_Unproject`, `h_ClickParams` in `pointer_map.cpp` | Pan the table (6.3) |
| Snap turn: middle drag or a turn key | `SnapTurn` in `comfort_camera.cpp:316` | Turn `psi` by the same step about `C` |
| Wheel | `PrepareWheel` in `comfort_camera.cpp:438` | Step `s` about the point under the Hand |
| Double click jump | `FinishFlight` in `comfort_camera.cpp:248`, target at `+0x110` and `+0x11C` | Set `C` to the flight focus at `+0x11C` |

After each, the game camera is put over the new table (2.6). The game's own
drag also runs, but that placement overwrites what it did.

### 6.3 What moving the table means

Every change is an instant step, or it follows the player's own hand
exactly. Nothing glides.

- **Pan.** At the press, take the land point under the pointer, `G`. Each
  frame, cast the pointer ray from the render eye and hit the level plane
  through `G`. Call the hit `P`. Move `C` by `G - P` in x and z. The grabbed
  point then stays exactly under the pointer. A lean does not pan, because
  the lock of 6.1 keeps the ray through `G`.
- **Turn.** Add the snap step to `psi`, 45 degrees by default, about `C`.
  The player can also walk around the table, which needs no code.
- **Zoom.** Multiply `s` by the zoom ratio for each wheel step, and keep the
  land point `A` under the Hand fixed in the room:
  `C' = A - (s' / s) * (A - C)`. Keep `s` between the fit of the whole land
  and about 40 units per meter, where a village fills the table.
- **Snap to the game camera.** One key sets `C` to the game camera's focus
  and `psi` to its heading. At a story cut, `table_follow_story=1` does the
  same.
- **Home.** One key fits the whole land again (1.3).

Roadmap step M7, move by grabbing the world with a controller, is this same
pan, turn and zoom, with the controller pose in place of the pointer ray.

## 7. The staged design

All values that the mode writes into the game go through one function,
`TabletopApplyFrame`, called once a frame beside `WaterModeApply`
(`stereo.cpp:812`). It keeps each original and writes it back when the mode
goes off, so one key can switch the mode during play for an A/B test.

### T2: a fixed table camera

**What it gives.** The island on a fixed table, whole, with head tracking,
fog off, full detail, objects drawn and the Hand working.

Changes:

1. `head_camera.cpp`: the table branch of 2.2 and 2.3 on the main call, and
   the table's `P0`, `Lt` and `E` in their own fields.
2. `head_pose.cpp`: `HeadPoseSetScale(s)` on entry and 7.5 on exit.
3. `stereo.cpp`: clear `g_taken_ipd` on a scale change. For the fake head,
   set the separation to `0.0668 * s`.
4. The render bracket of 2.4: a new post hook on
   `GGame::ProcessGraphicsEngine` `0x0054D850`.
5. `TabletopApplyFrame`: fog flag `0x00C37204` = 0, detail lines
   `0x00E9C4FC` and `0x00E9C4D4` = 1e6, texture line `0x00E9C524` = 1e6,
   vanish distance `0x00C37200` = 8000, size cap `0x00C37EC8` = 30,
   reflection `0x00E9CD8C` = 0, the plain sea, the Hand's reach 8000.
6. `pointer_map.cpp`: the lock that holds the Hand's world point (6.1).
7. A post hook on `CHand::SetDistanceFromView` for the Hand's size.
8. `spirit_anchor.cpp`: the scale of 5.1, `P0` as the room camera's
   position, and the tabletop pitch range.
9. `C` is the center of the land's bounding box, from the block table, and
   `psi` is the game camera's heading when the mode starts.

New ini keys:

| Key | Default | Meaning |
| --- | --- | --- |
| `tabletop` | 0 | 1 starts the game in tabletop mode |
| `table_diameter_cm` | 150 | the table diameter `D` |
| `table_forward_cm` | 80 | the table center, forward of the recenter pose |
| `table_down_cm` | 40 | the table surface, below the recenter pose |
| `table_units_per_meter` | 0 | 0 fits the land (1.3). Any other value is used as is |
| `table_margin_pct` | 10 | the margin of the fit |
| `table_hand_cm` | 12 | the Hand's size in the room |
| `table_game_distance` | 400 | the game camera's distance from `C`, for T4 |
| `fake_head_x_mm`, `fake_head_y_mm`, `fake_head_z_mm` | 0 | a still lean for the fake head, in the room |

New key: **Ctrl F11** switches tabletop mode on and off and logs which is
live. Ctrl F11 is free since v1.0 (`backend.md`, "Keys for v1.0"). The game
reads Ctrl as its zoom only with a mouse move, so a key pressed with the
mouse at rest is safe.

Desktop tests, with the fake head setup of `backend.md` ("A headset is not
needed to test most of this") plus `tabletop=1`, `fake_head_pitch=-45` and
`capture_every_seconds=6`:

1. The whole island is in both eye captures, with trees and houses, and no
   fog.
2. `profile=1` over Land1 and Land5: draw calls per eye, scene time, the
   object list count and the Z sorter count. This is the gate for the frame
   cost (3.4).
3. `draw_probe=1`: every world batch is in the `stereo` class, with the
   separation of `0.0668 * s`.
4. The log line of `head_camera.cpp` still reads "N recorded over N frames"
   (`bw-vr-latch-per-frame-state`).
5. The Hand: jiggle the mouse with relative input (`mouse_event` with
   `MOUSEEVENTF_MOVE`, since `SetCursorPos` does not reach the drag). The
   Hand's world point `0x00C5E680` must equal the land hit of the pointer ray
   from `E`. Then step `fake_head_x_mm` between 0 and 100 on the sweep. The
   Hand's world point must hold to about 1 unit.
6. `0x00C386D8` in the log, for the depth buffer question of 3.3.

No test adds motion. The fake head and the fake lean are still poses that
the sweep alternates (`vr-no-motion-the-player-did-not-make`).

### T3: the edge of the table

**What it gives.** Nothing drawn past the radius. The table has a visible
rim in a table color.

Changes:

1. A hook on `fn_00877210`: the block radius test of 4.2.
2. A hook on `fn_0087A090`: the sea disc of 4.3.
3. `table_draw.cpp`: the projection, clip and draw of 4.4.
4. The glare hook in `sky_detail.cpp`: in tabletop mode, skip the glare and
   draw the ring.
5. The `LH3DMist::Draw` hook: skip domes past `R`, or all of them.
6. `R` is `R far` times (1 + margin).

New ini keys: `table_rim_color` (a dark wood color), `table_clouds` (0 hides
the clouds, 1 keeps those over the table).

Desktop test: a capture with a known `C`, `R` and eye matrix. A script
projects the circle of radius `R` into each eye image and counts pixels
outside it that are not the sky or the rim. The count must be 0 in a still
scene, from three fake head poses: level, pitched down 45 degrees, and
leaned over the edge.

### T4: moving the table

**What it gives.** Pan, turn and zoom the table with the mouse, a snap to
the game camera, and the game camera kept over the table for the logic.

Changes:

1. The table state `C`, `psi`, `s` in one place, owned by the mod.
2. The routing of 6.2 in `comfort_camera.cpp` and `pointer_map.cpp`.
3. The game camera placement of 2.6, after `CameraModeNew3::Update`, in the
   player's modes only.
4. `table_follow_story` for the story cuts that `script_camera.cpp`
   already finds.
5. `GCamera`'s moving flag (`+0x74`) from the table, not from the game
   camera, because the interface clears the gesture trail while the camera
   moves [M, `backend.md`, "The story camera holds its shot"].
6. When `s` changes, the spirits' `k` and the separation change with it.

New ini key: `table_follow_story` (0). New keys: **Shift F11** snaps the
table to where the game camera looks, and **Ctrl Shift F11** fits the whole
land again.

Desktop tests, with relative mouse input:

1. Press on the land and move 300 counts. The land point grabbed at the press
   must stay under the pointer to within 1 unit, and the game camera focus
   must equal `C`.
2. One turn key: `psi` changes by 45 degrees and `C` does not change.
3. One wheel step: `s` changes by the ratio, and the land point under the
   Hand keeps its room position.
4. A double click on far land: `C` jumps there in one frame.

### T5: the table in the real room

**What it gives.** Everything past the table in the key color, so the island
stands on the desk with the passthrough mask on.

Changes:

1. A hook on `LH3DSky::Draw` `0x0086B010` (cdecl, no arguments, plain `ret`
   at `0x0086B6AA` [V]) that clears to the key color instead.
2. The ring in the key color.
3. The depth key quad of 4.6 at the glare point, or before the first
   interface batch of the pass.
4. The launcher: one paragraph on the Virtual Desktop mask, with the key
   color shown as numbers.
5. The table's place in the real room. The recenter pose moves the table
   with the head, which is right in a virtual room and wrong on a real desk.
   First version: `table_down_cm` and `table_forward_cm` by hand. Later,
   with M1: touch the desk with a controller and press a button to set `Tc`.
   A recenter must then not move the table [I, needs a design with the
   stage space].

New ini keys: `table_key` (0), `table_key_color` (`FF00FF`),
`table_key_depth_cm` (250).

Desktop test: a capture with the key on. Outside the projected disc every
pixel must be exactly the key color. Inside, the count of exact key pixels
must be near 0. Then one headset session with the Virtual Desktop mask for
the edge, the fades and the frame rate.

## 8. Risks

1. **The frame cost** (3.4). The whole island at once can pass 90 Hz on the
   larger lands. The object list stops at 2999, and the Z sorter drops past
   2048, both silently. T2 measures before anything else is built.
2. **The render bracket** (2.4). The stored camera feeds 184 readers of the
   position and 38 of the focus [M, `backend.md`]. Some readers inside the
   bracket may be logic and not drawing. The first list is in 2.5. A log of
   what each reader got, for one frame, would settle it.
3. **The logic sees a different camera from the player** (2.5, 2.6). The
   sound, the help triggers and the tutorials follow the game camera. With
   the game camera kept over the table center they follow the middle of the
   table, not the place the player looks. A player who looks at a corner of
   the table hears the middle.
4. **Depth precision** with a 16 bit buffer (3.3).
5. **Sub cameras inside the walk** (2.3). The particle camera at
   `0x0068B7C5` gets a lean that is 270 times larger. It needs a capture
   with that effect on screen.
6. **Key color leaks** (4.6): fades, interface drawn in the walk, rain, and
   the encoder at the edge.
7. **Story scenes lose their framing.** The story camera shows a shot, and
   the table ignores it unless `table_follow_story=1`. Scripts that frame a
   close shot will play out small on the table.
8. **Other rooms.** The Temple and the challenge rooms draw from other
   cameras. Tabletop mode must stand aside there and come back after, as a
   cut.
9. **Texture memory and build time** when every block gets a full texture
   (3.2).

## 9. Open questions

1. Which Z buffer format dxwrapper gives the game (`0x00C386D8`).
2. How many draw calls a land block takes in `fn_00874AA0`.
3. How many map objects each land has, against the cap of 2999.
4. Whether the fan through `fn_0081C090` keeps the look of the plain sea.
5. Whether the particle camera at `0x0068B7C5` needs the lean at all.
6. Where rain and snow spawn, relative to which camera.
7. The size of the Hand and of the spirits on the table, by eye in the
   headset.

## 10. As built (first build, 2026-09-30)

The first build has T2, T3 without the ring, and the clear in the key color
of T5. The second build, on the same day, puts the motion controllers on
the table and lets the grips move the table (T4). Sections 10.6 to 10.10
describe it. The code of the first build:

| File | What it holds |
| --- | --- |
| `src/mod/tabletop.cpp` | The table state, the land fit, the hooks of the edge and the sky, and the values of 3.2 |
| `src/mod/table_math.h` | The table transform, the fit, the edge test and the sea disc, as pure functions |
| `src/mod/head_camera.cpp` | The table takes the main camera call, and the stored camera keeps the render head |
| `src/mod/stereo.cpp` | The eyes of the table, and Ctrl F11 |
| `src/mod/frame_timers.cpp` | The end of the render bracket, after `GGame::ProcessGraphicsEngine` |
| `src/mod/sky_detail.cpp` | No clouds past the edge |
| `src/mod/mod_main.cpp`, `src/launcher/launcher.cpp` | The keys, and the play mode row |

### 10.1 What each part does

| Part | As built |
| --- | --- |
| The play mode | `play_mode=3`. It turns on head tracking and the full view, and it forces the plain sea. Only this mode hooks or writes anything in `tabletop.cpp`. Ctrl F11 turns the table off and on during play |
| The land fit | At the first table frame of a land, the mod asks `LH3DIsland::GetAltitude` for every cell point of every block. A point above 0 is land. `C` is the middle of the land, and `R far` is the distance to the farthest land point. Land 1 took 0.5 ms for 100 blocks |
| The scale and the edge | `s = 2 (1 + margin) R far / D`. The edge is `R = s D / 2`, the radius of the table in the world. For a fitted table that is `R far` plus the margin |
| The camera | On the main call, `head_camera.cpp` gets the camera `(P0, P0 + Lt forward * d)` and a lean gain of `s / 7.5`. The head goes on it with the formula of 2.1. The game still records the camera it asked for, and its basis is the game basis |
| The heading | `psi` is the heading of the game camera at the first table frame. It stays until Ctrl F11 or a new land. Nothing else moves the table |
| The render bracket | In a table frame the stored camera keeps the render head `E`. `frame_timers.cpp` calls `TabletopGraphicsDone` after `GGame::ProcessGraphicsEngine`, and that calls `HeadCameraUseGameCamera` |
| The restore inside the walk | A sub camera call inside the walk that passes the render head (`fn_0068B7D0`) goes through without a second head |
| The eyes | The separation is the player's IPD times `s`, or 66.8 mm times `s` with no headset. A separation set by hand is multiplied by `s / 7.5` |
| The edge | The block cull drops a land block when all of it is past `R`, and a block of sea only when its middle is past `R`. The plain sea is a disc of radius `R` with 64 triangles. Clouds and mist with their center past `R` are not drawn. With the key color on, no cloud or mist draws |
| The key color | With `table_key_color` set, the sky still draws. Right after it, a device clear of the key color covers every pixel row outside the outline of the sea disc on the screen of the eye. The land and the objects draw after the clear. The request for the sun glare goes |
| The lists | Once in each eye pass, before `FinishFrame`, the mod reads `GLandscape::DrawObjectCount` and the count of the Z sorter. A full list gets a log line |

The values of 3.2, while the table owns the frame:

| Value | Address | The game | The table |
| --- | --- | --- | --- |
| Fog | `0x00C37204` | 1 | 0 |
| Map objects stop | `0x00C37200` | 700 | 8000 |
| Size cap of the model detail | `0x00C37EC8` | 5 | 30 |
| Land detail lines | `0x00E9C4FC`, `0x00E9C4D4` | 1600, 2500 | 1e6 |
| Land texture line | `0x00E9C524` | 400 to 800 | 1e6, unless `table_textures=0` |
| Reflection | `0x00E9CD8C` | as set | 0 |
| The Hand's reach | `CHand+0x4838` | 1800 | 5600 |

Each value keeps the game's own value and puts it back when the table does
not own the frame: after Ctrl F11, in the Temple, and in any frame with no
main camera call. A value that the game writes while the table is on (a
level load, a script) becomes the value to put back.

### 10.2 Changes from the design

- **A lean gain, not a new head scale.** `head_pose.cpp` stays at 7.5 units
  per meter. The table multiplies the lean by `s / 7.5` on the main call
  only. The math is the same as `HeadPoseSetScale(s)`. But the controllers,
  the sub cameras of the walk and a frame without the table keep the life
  size scale, and a switch of the mode has no frame of lag.
- **The bracket ends in the timer hook.** `frame_timers.cpp` already hooks
  `GGame::ProcessGraphicsEngine`, and MinHook refuses a second hook on one
  function. The first desktop run found this. The table does not start
  without that hook.
- **The Hand reaches 5600, not 8000.** `SetDistanceFromView` gives the Hand
  a size of 0 at 5650 units and turns it inside out past that.
- **A block of sea only is cut by its middle.** With no ring yet, the flat
  parts of edge blocks would show past the disc by up to 226 units. A block
  with land still stays while any part of it is inside `R`. The land fit
  marks the blocks with land.
- **The key color does not replace the sky.** The first build cleared the
  frame in place of the sky, and run t2c showed a purple sea: the plain sea
  is not opaque, and it showed the key color behind it. The coast and the
  cloud edges took the tint too. Now the sky draws first, as the game
  draws it, and the key color covers only the screen outside the sea disc.
  The clear is a list of pixel rows, from the 64 rim points through the eye
  matrix, clipped at the near plane (`table_math.h`, `RowSpans`). It leaves
  no pixel inside the outline, and the pixels it leaves outside touch the
  outline. The clear comes before the land, so a mountain that stands up
  past the outline still draws.
- **No ring and no depth key yet.** See 10.5.
- **Ctrl F11 needs no test keys**, but it acts only with `play_mode=3`.
- **A test step, `table_sweep=1`.** The table goes off and on at each
  report window, and a capture follows each step. So one run measures both.

### 10.3 Keys

All are in `[bwvr]`. Only the play mode is in the launcher.

| Key | Default | Meaning |
| --- | --- | --- |
| `play_mode` | 4 | 3 is the tabletop mode. The launcher row "Play mode", "Tabletop (test)". The default, 4, is the God view (`god-view.md`) |
| `table_diameter_cm` | 150 | The table in the room. It is also the edge |
| `table_forward_cm` | 80 | The table center, ahead of the recenter pose |
| `table_down_cm` | 40 | The table top, below the recenter pose |
| `table_units_per_meter` | 0 | 0 fits the land to the table. Any other value is the scale |
| `table_margin_pct` | 10 | The room at the edge of a fitted table |
| `table_center_x`, `table_center_z` | -1 | The land point at the table center. -1 is the middle of the land |
| `table_textures` | 1 | 1 gives every land block its full texture |
| `table_key_color` | empty | A hex color, for example `FF00FF`. Empty or 0 keeps the sky |
| `table_sweep` | 0 | A test: the table goes off and on at each report window |
| `table_hand_cm` | 10 | The length of a controller's Hand on the table, in the room, 2 to 40 (second build, 10.6) |

Keys of the design that are not built: `tabletop` (the play mode does its
job), `table_game_distance`, `table_rim_color`,
`table_clouds`, `table_key`, `table_key_depth_cm` and `table_follow_story`.
The fake lean keys exist already as `fake_head_right_cm`,
`fake_head_up_cm` and `fake_head_forward_cm`.

### 10.4 Desktop results

The runs used the rig of `backend.md` with `fake_head=1`,
`fake_head_pitch=-40`, a 2560 by 1440 render at 110 degrees, and
`-NEWGAME` on Land 1. The start dialog stays open, and the land and its map
objects draw behind it.

Five runs, each 100 or 120 seconds:

| Run | Keys | What it showed |
| --- | --- | --- |
| t2a | `play_mode=3`, `profile=1` | The table did not start. `frame_timers.cpp` already hooks `GGame::ProcessGraphicsEngine`, and MinHook refused the second hook. So the run was play mode 0, the baseline |
| t2b | `table_sweep=1` | Four windows with the table and four without, in one run, with a capture after each step |
| t2c | `table_key_color=FF00FF`, a 1 m table at 1973 units per meter, `fake_head_sweep=1`, a lean of 10 cm forward | The cut at the edge, and the purple sea of 10.2 |
| t2d | The same, with the fitted table of 1.5 m | The sea in its own color, and the key color outside it |
| t2e | The key color, the fitted table, a lean of 40 cm forward and 15 cm right, `fake_head_pitch=-60`, `fake_head_sweep=1` | The eye over the edge of the table, and the key after the fix |

**The frame cost.** The scene walk of both eyes, from `profile=1`, with the
same game camera. Each row is a still head:

| Head | Draw calls, both eyes | Scene walk, both eyes | Of that, Direct3D |
| --- | --- | --- | --- |
| Play mode 0, or the table off, pitch -40 | 810 | 4.31 ms | 1.51 ms |
| The table, pitch -40 | 3093 | 9.03 to 9.08 ms | 4.97 ms |
| The table, level, a lean of 40 cm | 2360 | 7.0 ms | 3.7 ms |
| The table, pitch -60, a lean of 40 cm | 3869 | 11.2 ms | 6.1 ms |

The first window with the table was about 1.2 ms slower. The land textures
build in that window, which is the likely cause. So the whole of Land 1 costs about 4.7 ms more than the
life size view, and a head that leans in costs 2 ms more again, because the
models near the eye keep more detail. The estimate of 3.4 was 7 to 18 ms.
A lean over the table is at the edge of a 90 Hz frame. The larger lands are
not measured.

The lists: at most 2017 of 2999 map objects and 97 of 2048 sorted objects
in one pass, against 635 and 24 without the table. Neither list was full in
any pass of any run. The fit of the land took 0.5 ms.

**The eye pictures.**

- t2b, the table on: the whole of Land 1 on a round sea, with the sky and
  clouds around it. The mountains stand in relief, the forests are dark
  green clumps, and the start dialog is over the middle. There is no fog.
  The two eyes differ by the separation of 131.8 units.
- t2b, the table off: the shore under the game camera, the same picture as
  play mode 0 in t2a. The draw counts of the two runs are the same to the
  vertex, so the table puts back every value it changes.
- t2c: past the edge of a 1 m table everything is magenta. The disc stops at
  986 units, and 6 of the 100 land blocks are cut in each pass. A land
  block that is only partly past the edge stays, so the south cape stands
  out past the disc. The sea, the coast and the cloud edges are purple.
- t2d: the sea is blue again. Everything past the disc is magenta, with a
  line of sky 1 pixel wide at the rim. Clouds that reach past the edge
  show pink. So the key now takes every pixel whose middle is outside the
  disc, and no cloud draws while the key is on.
- t2e, leaned over the table and 60 degrees down: the island fills most of
  the view, and the edge of the disc goes through the top corners with
  magenta past it. With the head level: the island from the side, magenta
  above it, and no cloud. At the rim, 91 light pixels of 3.7 million touch
  the key.

**Other numbers.**

- The Z buffer is 16 bit, and the near plane is 3.5 units. See 10.5.
- The head camera recorded one main call in each frame (450 of 450), all of
  them the table's. `fn_0068B7D0` did not give the stored camera back in
  these scenes.
- The Hand stood on the sea under the pointer, 1826 units from the eye.
  That is past the game's own reach of 1800.
- About 29 cloud domes in each pass had their middle past the edge.

### 10.5 Not built, and what needs the headset

- **The ring** of 4.5. It is not simple. The mod has no draw of its own at
  the glare point. It needs `table_draw.cpp` of 4.4: the projection through
  the eye matrix, a clip at the near plane, a `DrawPrimitive` on the device
  with the FVF `0x1C4`, and a save and restore of each render state, the
  texture of stage 0 and its stage states. The game caches all of them, so
  one wrong value makes it skip a state that it needs.
- **The depth key quad** of 4.6 needs the same draw.
- **The near plane.** The Z buffer is 16 bit (`0x00C386D8` is 1), and the
  near plane is 3.5 units. So 3.3 applies: at the far edge a depth step is
  about 22 mm. It needs the `GetNearClipping` hook and the fix of the
  interface z of 3.3.
- **The rest of T2 in section 7:** the pointer lock that holds the Hand's
  world point when the head leans (6.1). The mouse's Hand still slides with
  a lean. The controller's Hand does not (10.6).
- **The second build** (10.6 to 10.10) adds the controllers on the table,
  the size of the Hand, the ears, the spirits and the moves of the table by
  the grips (T4). The moves by the mouse, the stick and the keys of 6.2, and
  the game camera over the table of 2.6, are not built.
- **The headset:** the size of the table in the room, the eye separation of
  130 units, the frame rate at 90 Hz, the look of the relief, the height of
  the table, and the edge of the Virtual Desktop mask with the key color.

### 10.6 The hand on the table (second build)

The second build puts the direct hand of `hand.md` (M3 to M5) on the table.
It works only with `play_mode=3` and the table on. The code:

| File | What changed |
| --- | --- |
| `src/mod/controllers.cpp` | On the table, the hand goes into the world through the table, with no reach curve |
| `src/mod/controller_math.h` | `TableHand`, `TableRoomPoint` and `TableThrowVelocity`, pure functions |
| `src/mod/hand_direct.cpp` | The Hand's size, the pick and the throw on the table |
| `src/mod/tabletop.cpp`, `tabletop.h` | The room of the table, the Hand's size, the table pose for the grab, and the ears |
| `src/mod/table_math.h` | `HandSize`, `FlatWorld`, `TableGrabSolve`, `InsideEdge` and `EarsHeight` |
| `src/mod/world_grab.cpp` | The grips move the table (10.7) |
| `src/mod/audio_listener.cpp` | The ears on the table (10.8) |
| `src/mod/spirit_anchor.cpp` | The spirits beside the table (10.9) |
| `src/mod/stereo.cpp` | A new eye separation is logged after a change of a tenth, not on every frame of a zoom |
| `src/mod/mod_main.cpp` | `table_hand_cm`, and the two hooks of the pick on the table, only with `play_mode=3` |
| `src/mod/controllers.cpp` | Also the test word `shot`: a picture of both eyes 3 frames later |
| `test/controller_math/main.cpp` | 62 checks of the hand, the throw, the grab and the ears on the table |

**The map.** `controllers.cpp` asks `TabletopRoom` for the table of this
frame: `P0`, `Lt` and the gain `s / 7.5`. Every room point of
`head_pose.cpp` is multiplied by the gain, and the table's origin takes the
place of the game camera (`TableHand`):

```
E    = P0 + Lt * (gain * lean)
grip = E + Lt * (gain * (t_grip - t_head))
```

With full six degrees of freedom that is `C + s * Lt * (room - Tc)`, the map
of 2.2. So a real hand on the table top touches the land under it. The math
test puts a hand on the table top, 20 cm right of the table center and 30 cm
past it. The grip is at sea level, 394.5 units right of `C` and 591.8 units
forward, which is 0.2 s and 0.3 s at 1973 units per meter. A head that moves
1 cm, 5 cm, or 3 cm along each axis, and turns 20 degrees, moves a still
grip by 0.0003 units at most.

The room points that the motion tests, the throw and the grab keep stay at
the life size scale. Only the world point changes.

**No reach curve.** The whole land is within arm's reach, so the Go-Go reach
is off on the table. The Hand is where the hand is.

**The Hand's size.** `table_hand_cm` (10) is the Hand's length in the room.
The size is `0.10 * s / 3.2`, which is 61.6 at 1973 units per meter, and it
follows the zoom of 10.7. The reasons for 10 cm:

- It is about half a real hand (19 to 21 cm), so the Hand does not hide the
  village under it. On the table a villager is 2 to 5 mm tall and a house is
  about 1.5 cm.
- The game keeps the Hand half its length over the land. Half of 10 cm is
  5 cm, which is about how high the controller's grip is over the table when
  the fingers touch it. So the Hand rests on the land when the real hand
  rests on the table. (Since 2026-10-02 the mod lets the point come down to
  5 mm over the land, for the God view. On a real table the grip still stops
  about 5 cm over the top. See `hand.md`, "The point touches the land".)
- The grab radius of the pick, 0.6 of the length, is 6 cm. A hand does not
  have to hit a villager of 3 mm.
- In run th1 the Hand reads clearly at 1 m, as a hand over the model and
  not as a speck.

**The pick.** The render pick offers an object to `SendObjectDrawCollision`
only when the pick pixel is inside the object's circle on the screen.
`fn_00868C80`, the screen test of each object, sets `0x00EA1AD0` then, and
`Game3DObject::AddForDrawing` reads it at `0x0063B653` before the offer [V].
`hand.md`, section 2, inferred that the draw offers every object it draws.
That is not so. On the table a villager is about 3 pixels across, and run
th1 offered 45 objects in 450 frames, against 7186 off the table. Nothing
could be picked.

So with `play_mode=3` a post hook on `fn_00868C80` (fastcall, the bounds in
ecx, the object on the stack, `ret 4` [V]) also sets that flag for an object
in view within twice the grab radius of the Hand, about 12 cm, in the first
eye pass only. The draw then offers it. With a distance of 0,
`SendObjectDrawCollision` asks the mesh whether the pick pixel hits it, so
the game's own pick gets no false hit. The candidate list holds 4096 objects
on the table and 256 off it, as before. The rest is the pick of M3: the
nearest holdable object within the grab radius goes into the next record.

Two more changes on the table made the grip pick up (runs th3 to th5):

- **The choice holds the record.** The hand mode chooser `fn_005D7F20` reads
  the next record (`+0x400`) [V]. On the table the pixel pick found a larger
  object behind the villager on some frames. So while the empty Hand has a
  holdable choice, the choice goes into the record at the start of each eye
  pass too, and `SendObjectDrawCollision` is not called for other objects.
- **The land under the Hand.** `fn_005D1120` sets `GInterface+0x48` from the
  player's influence at the land point of the next record (`+0x3F0`) [V].
  With it clear, `Object::InterfaceMustBeInInfluenceForInteraction` turns
  hand mode 9 (pick up) into 0x12 (touch), and the grip does not pick up.
  On the table that land point is where the pointer ray meets the land
  behind the Hand, and the mode went from 9 to 0x12 and back on every frame.
  A pre hook on `fn_005D1120` (thiscall, no arguments, plain `ret` [V]) now
  writes the land point under the Hand there, while the controller places
  the Hand on the table. The influence, the hand mode and the drop then
  follow the Hand. Outside the influence the mode is 0x12, as the game
  wants.

**The throw.** `TableThrowVelocity` turns the speed of the real hand into
units at the table scale: the grip speed times `throw_gain_pct` times
`s / 7.5`, then the game's cap of 124. At 1973 units per meter and a gain of
150, a hand at 4.2 cm/s already reaches the cap. So every real throw goes at
the cap, in the direction of the hand.

**The throw did not let go** in runs th5 and th6: the game kept the object in
the Hand, in hand mode 0x18, and a later set down worked. The cause is the
player's influence at the land point of the release, not the speed. See 10.11
for the cause and the fix.

The game sets an object down when its horizontal speed is 2 units per second
or less. On the table the tremble of a still hand, about 1.25 cm/s, would be
37 units per second, which is a throw. So the table sets the object down
whenever the life size scale would: when the horizontal speed of the real
hand at 7.5 units per meter, times the gain, is 2 or less. With the gain of
150 that is a hand slower than 18 cm/s.

**The Creature.** The stroke and slap test keeps the speed of the real hand
at the life size scale, so a stroke and a slap need the same hand motion as
off the table. This is not run.

**The word "seek" on the table** asks for a wider pick, 50 cm of the room
around the Hand, for 3 frames. Two frames later it steps the fake hand onto
the nearest holdable object to the Hand. The room point is the table map
backward (`TableRoomPoint`).

### 10.7 Moving the table (T4, by the grips)

While the table owns the view, the grips of `grab-world.md` move the land on
the table, not the game camera. The table top stays where it is in the room.

- **One hand** (the other hand's grip): the land slides with the hand. Only
  the motion across the table counts. A hand that moves up or down moves no
  land.
- **Two hands** (both grips): the land turns about up with the hands, and it
  scales with their spread. Hands twice as far apart make the land twice as
  large, so `s` halves. The land point under the middle of the hands stays
  under it, and each hand keeps its own land point.
- **The solve** is from the press every time (`TableGrabSolve`), one to one:

  ```
  psi = psi_a - theta
  s   = s_a / k, kept between 40 units per meter and the scale of the fit
  C   = W - s * Lt(psi) * (m - Tc), with the height left out
  ```

  `W` is the land point under the middle of the hands at the press. `theta`
  and `k` are the turn and the spread ratio of `GrabSolve`, with the same
  freeze below a spread of 8 cm. Only the grips go in. So a head that moves
  with still hands moves nothing, a second solve in one frame gives the same
  table, and nothing moves after the release. The smoothing of the grips
  (`grab_smoothing_ms`) stays. The gain of the world grab does not apply.
- **The anchors.** A new anchor starts at a press, when the hands change,
  after a grip jump of 50 cm, at a recenter, and when the table is built
  again. The grab ends when the table goes on or off.
- **The limits.** `C` stays on the map, 0 to 5120. The zoom stops at 40
  units per meter, where a village fills the table, and at the scale of the
  fit, where the whole land fits.
- **The game camera stays the game's.** A double click jump flies the game
  camera and not the table, so it does not end a grab of the table.
- `TabletopMove` builds the basis, `P0`, the edge and the sea disc again from
  the new `C`, `psi` and `s`. The eyes, the Hand's size, the ears and the
  spirits follow `s` in the same frame.

Not built: the game camera over the table (2.6), and the moves by the mouse,
the stick, the wheel and the jump (6.2). They still move the game camera,
which the table does not show. The edge cuts whole land blocks, so when the
table zooms in, land stands past the edge by up to one block: 160 units,
which is 4 m at 40 units per meter.

### 10.8 The ears on the table

**The choice.** The ears go straight down from the render head to at most
400 units over the land under them, and they stay inside the edge of the
table. They turn with the head. A head lower than that, for example when the
player leans in or zooms in, is the ears itself (`TabletopEars`).

**Why.** The render head is 800 to 3200 units from the land, and animation
sounds stop at 1200. Two places for the ears were considered:

- At the game camera, turned with the head. The game camera is not in the
  picture on the table. When the player walks round the table, the ears stay
  at an invisible camera, so a sound in front of the player can come from
  behind.
- Over the land under the head (built). A sound on the left of the head on
  the table is on the left of the ears. The player hears the part of the
  island that they lean over, as with a real model. 400 units is about the
  distance from the game's camera to what it looks at.

**The cost.** The game still decides from the game camera which sounds start
and stop (`audio.md`, section 3). A sound far from the game camera does not
start, even under the head. A sound that the game glues to its camera comes
from the game camera's place.

**The advisors.** Their polar angle still comes from the render head itself,
because the spirits float in the room around the head.

### 10.9 The spirits beside the table

- On the table the room camera of `spirit_anchor.cpp` stands at `P0` with
  the table's basis. The latched pitch stays between 0 and 15 degrees, so
  the spirits float at eye level beside the table, not on its edge.
- While the table owns the view, the spirits' size (`+0x35B4`) and depth
  range (`+0x35B8`, `+0x35BC`) are `s / 7.5` times the game's values, 263
  times at 1973 units per meter. The spirits then float 1 to 1.5 m away in
  the room, at the size they have off the table. The game's values go back
  when the table goes off, and a value that the game writes while the table
  is on becomes the one to put back.
- The readers of these fields [V, a scan of the displacements]: the size
  builds the mesh matrix in `HelpDude::Update1` and sizes the glow in
  `HelpDude::Draw` (`fn_005C0700`). The depth range places the spirit in
  `HelpDude::ScreenToWorld`. The other readers use only the ratio of the size
  to the near depth, which the factor does not change.
- The spirits' light gets the same factor on its depth.
- A grab of the table moves `P0` with the land, so the spirits stay where
  they are in the room. On the table the jumps and turns of the game camera
  are not cuts. The table going on or off is a cut.

### 10.10 Desktop results of the second build

The math test has 343 checks, and all pass. The 62 new ones: a hand on the
table top touches the land point of the table map, head moves with a still
hand move the grip by 0.0003 units at most, the Hand is 10 cm long, the
tremble of a still hand sets an object down, the throw reaches the cap, and
the grab of the table solves from the press (a still hand, a slide, a hand
that moves up, a turn, a zoom, the zoom limits, and a second solve).

Six runs, each with `play_mode=3`, `table_sweep=1` (the table goes off and on
every 450 frames), the fake head level, and the fake controller. The start
dialog and the ape use the recipe of `hand.md` in the windows off the table.

| Run | What it showed |
| --- | --- |
| th1 | The Hand on the table, 10 cm long, reads well over the model. The ears sat on the rim of the table, 400 units over the sea, with the head 789 units over it, and a lean of 10 cm right and 10 cm forward moved them with the head. The draw offered 45 objects to the pick in 450 frames, so nothing could be picked. After the ape, a follow camera refused every grab |
| th2 | No ape, so the Creature select kept the player's camera. One hand 10 cm right slid the table center 197.3 units (0.1 s), and 10 cm up moved nothing. Two hands 40 cm apart spread to 50 cm: the scale went from 1972.8 to 1578.2 units per meter (k 1.25), and a turn of the left hand of 10 cm gave 11.3 degrees and 1547.6. In each settled step the land point of the anchor was 0.0001 units from the land under the hands. After the release the report of the window showed the same pose, and with still hands the table moved on 0 of 201 frames. A lean moved the Hand by nothing. The pick now had 83 candidates near the Hand, 894 for a seek |
| th3, th4 | In the pen, the hand mode went from 9 to 0x12 and back on every frame, and the grip picked nothing. The cause is the influence test of 10.6 |
| th5 | With the land under the Hand: hand mode 9, the Tug state 4 frames after the grip, and the Holding state 8 frames later. A lean while holding moved nothing. A release with no motion set the villager down. A throw sent 124 units/s but did not let go |
| th6 | The held villager is at the Hand, 97.7 units over the land. The throw again did not let go, and the set down after it worked: the villager landed on the land under the Hand. Outside the player's influence the mode was 0x12, as the game wants |

With the table on, both scene walks took 9.8 to 10.1 ms in the pen, against
6.8 to 8.4 ms off the table, at 55 to 59 frames per second. No advisor
appeared in these runs, so the spirits on the table are not seen yet.

**Needs the headset:** the Hand's size and the ears by eye and ear, the grab
of the table with real hands, the spirits beside the table, and a throw once
the throw lets go.

### 10.11 The throw from inside the influence

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

On the table the hook on `fn_005D1120` (10.6) puts the land under the Hand in
`+0x3F0`. So the test is at the Hand at the release. A swing carries the Hand
`s` units over the land for each meter of the real hand. At 1973 units per
meter the step of 2 cm in runs th5 and th6 carried the Hand 39.5 units, from
the villager at 2278, 3141 to 2300, 3108. That point is past the edge of the
influence at the pen [I]. The set downs worked because they were at a
villager, where the hand mode was 9, so inside the influence. At the life
size scale a swing of 40 cm is 3 units, so a throw almost never leaves the
influence on the way. No other test between the release and the drop
depends on where the Hand is: `ValidToApplyThisToMapCoord` of a `Mobile` is
always 1, the land point is on the map, and `Villager::IsAvailable` and the
hand count of `fn_005DA400` do not change with the throw [V]. So the speed,
the height over the land and the packet were not the cause.

The mouse has the same rule. The game keeps an object that the pointer
releases outside the influence.

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
  the view and the controller places the Hand. The life size throw of play
  mode 0 does not change.
- The search calls `CalculatePlayerInfluence` once for each point, newest
  first, and only for a throw that ends outside. It is latched once a frame.

The log tag `hand throw` gives one line for each such throw: where it ends,
the point it goes from, and how far back that is in time and in units. The
report of the window counts the throws that went from the path.

The math test checks `ThrowFromIndex` in `table_math.h`: a swing of 30 cm at
the table scale ends outside and goes from the last point inside, the same
swing at the life size scale stays inside, a hand outside for the whole
window gets no point, a point older than the window does not count, and the
newest point inside wins. It has 469 checks, and all pass.

**Not run yet.** The PC was in use, so no desktop run tested the fix. The
run to make is th6 again: `play_mode=3`, `table_sweep=1`, the points
`-15,-18,94 -16,-31,94 0,-20,41 -22,-35,95 10,-35,95`, and the script
`point@480+1 trigger@520+8 point@600+1 trigger@640+8 point@3400+1 a@3440+4
point@3610+1 seek@3630+1 grip@3660+20 grip@3740+6 nudge@3745+1
seek@3800+1 grip@3820+20 grip@3880+20 seek@3920+1 grip@3940+20
grip@3990+6 slap@3995+1`. The table is on from frame 3600 to 4049. Look
for the `hand throw` line "outside your influence. It goes from", then "the
Hand holds 00000000" about 6 frames after the release, and effect 46 of the
haptics. The throw with the slap of 12 cm (237 units) tests a longer swing.

## Sources

- Research this session: the land file parse, `fn_00877210`, `fn_0087A090`,
  `fn_005E5CD0`, the fog path, the near plane, the Z buffer choice, the
  sound listener, the Hand's reach and size, the 3500 pull back and the
  spirit fields. The Ghidra output and the scripts are in the scratch
  folder of this session, under `tabA`, `tabB` and `tabC`.
- Virtual Desktop passthrough mask settings, as players use them:
  [Passthrough settings in VD](https://forum.falcon-bms.com/topic/28968/passthrough-settings-in-vd),
  [Mixed reality proof of concept](https://forum.falcon-bms.com/topic/25786/mixed-reality-proof-of-concept-and-new-virtual-desktop-with-passthrough-mask).
