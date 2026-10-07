# Verified address map

Target: `runblack.exe`, fan patch v1.42, SHA1
`9c9aea96f046911ab1b7e496fae511ed5ebbfefe`, 8,663,040 bytes.
Image base `0x00400000`. Relocations stripped, no ASLR, no DEP, so **every
address here is constant on every machine**.

## Symbol source and why it applies

`bw1-decomp` ships `config/BW1W120/symbols.txt`: **37,057 symbols** for
`runblack-decrypted.exe` (SHA1 `bccccbd3a08fe9a5d6cf59cd114e916401abfb51`).

Our file hashes differently because the v1.42 fan patch applied byte patches on
top. The **addresses still match**, because byte patches cannot move code in an
image with relocations stripped. Verified by deriving six vtable addresses
independently with an RTTI walk and comparing:

| Address | Found by RTTI walk | Symbol in `BW1W120/symbols.txt` |
| --- | --- | --- |
| `0x008C76E0` | `CameraMode` vtable | `??_7CameraMode@@6B@` |
| `0x008CBECC` | `CHand` vtable | `??_7CHand@@6B@` |
| `0x008C7958` | `CameraModeFree` vtable | `??_7CameraModeFree@@6B@` |
| `0x00900A54` | `HandStateCamera` vtable | `??_7HandStateCamera@@6B@` |
| `0x008DF800` | `GestureSystem` vtable | `??_7GestureSystem@@6B@` |
| `0x008C7110` | `GCamera` vtable | `??_7GCamera@@6B@` |

Six for six. The symbol file is trustworthy on our binary.

Note on vtable sizes: the `size:` field in `symbols.txt` covers the symbol
region, which includes static constants that follow the vtable. The real
function pointer counts are lower. `CameraMode` is **18** functions, then five
floats (400.0, -400.0, 0.7, 365.25, 86400.0). `CHand` is **10**.
`HandStateCamera` is **5**. `GestureSystem` is **7**.

## Renderer: `LH3DRender`

`LH3DRender` is the Direct3D submission layer, not the transform. The scene
is projected on the CPU by `LH3DTech` and arrives here already in screen
space. See the `LH3DTech` section below, and
[software-tnl.md](software-tnl.md).

The two matrix globals in this table are Direct3D copies. Writing them
changes nothing on screen.

| Address | Symbol | Why it matters |
| --- | --- | --- |
| `0x00EC8010` | `g_d3d_world2view` | A Direct3D copy. **Not** what the scene renders from. It is the first 12 floats of a `D3DMATRIX`, not an `LHMatrix` |
| `0x00EC7FD0` | `g_d3d_view2proj` | A Direct3D copy, same warning |
| `0x0082B2D0` | `SetProjMatrix(D3DMATRIX*, D3DMATRIX*, float, float, float)` | Builds the projection. The three floats are very likely FOV, near, far. **Asymmetric per-eye frustum injects here** |
| `0x0082B220` | `SetD3DMatrix(D3DMATRIX&, uint, LHMatrix&)` | Pushes an `LHMatrix` to D3D |
| `0x0082F0E0` | `StartFrame()` | The real per-frame bracket, static |
| `0x0082F460` | `FinishFrame()` | The real per-frame bracket, static |
| `0x0082F2C0` | `RegisterFinishFrameCallback(ulong, bool, void(__stdcall*)(void*), void*)` | **An official end-of-frame callback registration.** Possibly no hook needed |
| `0x0082F3B0` | `RemoveFinishFrameCallback(...)` | Pair of the above |
| `0x0082F810` | `DrawTriangle(Vertex3D*, ulong, ushort*, ulong)` | **`__fastcall`**, verts in `ecx`, count in `edx`, `ret 8`. Hardcodes FVF `0x1C4` (XYZRHW), so `Vertex3D` is a screen space vertex despite the name |
| `0x00ECA638` | `Direct3DDevice7` (`IDirect3DDevice7*`) | **Global device pointer.** No need to hook `DirectDrawCreate` to reach the device |
| `0x00ECA644` | `g_started_frame` (int) | Frame state flag |
| `0x00ECA648` | `g_zsorter` (`LH3DZSorter*`) | Z sorting |
| `0x00ECA1F0` | `g_render_states` | Cached render state array |
| `0x00EC81F0` | `g_texture_stage_state` | Cached texture stage state array |
| `0x00412940` | `SetRenderState(D3DRENDERSTATETYPE, uint)` | Static |
| `0x0082B9C0` | `SetTextureStageState(uint, D3DTEXTURESTAGESTATETYPE, uint)` | |
| `0x00ECA620` | `b_open` | Init flag |
| `0x00ECA624` | `b_need_init` | Init flag |

### Device creation and the hardware T&L decision

| Address | Symbol or meaning |
| --- | --- |
| `0x0082CD80` | **`LH3DRender::OpenD3D()`** — the T&L capability probe |
| `0x0082D3F0` | `LH3DRender::CloseD3D()` |
| `0x0082B3C0` | `LH3DRender::Open()` |
| `0x0082B570` | `LH3DRender::Close()` |

Inside `OpenD3D`:

| Address | What happens |
| --- | --- |
| `0x0082CDA2` | pushes `IID_IDirect3D7` (GUID at `0x009A5730`) |
| `0x0082CDCC`, `0x0082D073` | pushes `IID_IDirect3DHALDevice` (GUID at `0x009A5700`) |
| `0x0082D103` | pushes `IID_IDirect3DTnLHalDevice` (GUID at `0x009A56C0`) |
| `0x0082D115` | logs `"Support Hardware T'n'L."` |
| `0x0082D132` | logs `"Doesn't support hardware T'n'L / tnl disabled."` |

`IID_IDirect3DRGBDevice` (GUID at `0x009A5710`) is never pushed, so the
software rasterizer fallback is dead code.

Other D3D GUIDs: `IID_IDirect3DDevice7` at `0x009A55C0`,
`IID_IDirectDrawSurface7` at `0x009A58F0`, `IID_IDirectDraw7` at `0x009A5940`
(pushed at `0x00472770`, `0x007DCFA2`, `0x007DD3F2`, `0x007DD4FB`,
`0x007DD6F8`).

## Software transform and lighting: `LH3DTech`

This is where the scene is really projected. Everything in this table was read
out of the disassembly, not guessed. Full working in
[software-tnl.md](software-tnl.md).

| Address | Symbol | Why it matters |
| --- | --- | --- |
| `0x00EA9E40` | world to camera matrix, 12 floats | **The matrix the scene renders from.** 4 rows of 3, translation in the last row, camera basis in the columns, field of view baked into the column scales |
| `0x00EA9E80` | camera to world, the inverse | Built by `LHMatrix::SetInverse` right after. Needed to turn a screen position back into the world |
| `0x00E839E4` | `g_info_transform` (`InfoTransform`, 0x3C bytes) | Screen mapping and the culling cone |
| `0x00E839E0` | near plane, float | `ProjectPoint` rejects anything closer |
| `0x00EA1DB8` | `g_camera` (`LHPoint`) | Camera position |
| `0x00819920` | `UpdateCamera(LHPoint&, LHPoint&)` | **`__fastcall`.** Builds the matrix from a position and a focus point. The VR camera hook goes here |
| `0x00819F50` | unnamed, 0x800 bytes | A second writer of the same matrix, same argument shape |
| `0x00819690` | `UpdateWorldToCamera(LHMatrix&, LHPoint&, LHPoint&, bool)` | Normalizes `focus - position` and builds the basis |
| `0x00819390` | `ProjectPoint(LHPoint*, int*, int*, float*)` | The projection itself: divide by camera z, scale to half the resolution |
| `0x008195B0` | `ChangeFov(float)` | **`__thiscall`.** Writes the `InfoTransform` field of view terms. The frustum widening entry point |
| `0x00819030` | `UpdateViewPort(long, long)` | Recomputes the screen mapping for a resolution |
| `0x00818C60` | `RenderInitialization(long, long)` | |

`InfoTransform` layout, with the two field of view terms in the order the
binary uses rather than the order the decomp header names them:

| Offset | Field | At 1280x720 |
| --- | --- | --- |
| `0x00` | `resolution.x`, `resolution.y` (int) | 1280, 720 |
| `0x08` | `AspectRatioXOverY` | 1.7778 |
| `0x0C` | `HalfRes.x`, `HalfRes.y` | 640.0, 360.0 |
| `0x14` | `InvHalfRes.x`, `InvHalfRes.y` | |
| `0x1C` | inverse half tangent, **horizontal** | 1.4281, so 70 degrees |
| `0x20` | inverse half tangent, **vertical** | 2.5389, so 43 degrees |
| `0x24` | `CosHalfFovSqr` | 0.5736 |
| `0x2C` | `CosHalfFov` | 0.8192, so a 35 degree cull cone |

## The Hand: `CHand`

vtable `0x008CBECC`, 10 virtual functions. Built around `0x0046B000`, which
both the vtable references (`0x0046BB5F`, `0x0046BFDF`) and the
`ControlHand.cpp` assert cluster (`0x0046BC6E` onward) agree on.

| Address | Symbol | Relevance to VR |
| --- | --- | --- |
| `0x0046C0D0` | `SetDistanceFromView(float)` | **The Hand's depth from the camera.** This is the scalar a 6DOF controller pose replaces |
| `0x0046C050` | `StartFixedPosAnimation(LHPoint&, int)` | Takes a world position directly |
| `0x0046CD10` | `GetRequiredState()` | Drives the state machine. Where a VR state gets selected |
| `0x0046DDD0` | `ThrowObject()` | Throwing |
| `0x0046BFB0` | `GetMaxForce()` | Throw force limit |
| `0x0046E490` | `UpdateHeldObject()` | Held object follow behavior |
| `0x0046CB60` | `OperateInteractionCamera(int)` | The hand and camera coupling |
| `0x0046C260` | `ToggleLeftRight()` | Handedness, pairs with the `HAND_ORIENTATION` registry value |
| `0x0046C1B0` | `Show(int)` | Visibility |
| `0x0046C4A0` | `SetSize(float)` | Virtual. Hand scale |
| `0x0046C350` | `UpdateTime(int)` | Virtual |
| `0x0046C550` | `PrepareForDrawing()` | Virtual |
| `0x0046D0F0` | `AddForDrawing()` | Virtual |
| `0x0046D100` | `AddDrawing()` | |
| `0x0046D210` | `Draw()` | |
| `0x0046DF00` | `DrawTheHeldObject()` | |
| `0x0046E680` | `DrawSpellInHand()` | |
| `0x0046E4E0` | `GameTurnUpdate()` | Per turn update |
| `0x0046BF80` | `MorphTexture()` | Virtual. Good/evil hand blending |
| `0x0046E4B0` | `GetCreatureToGiveTo()` | |
| `0x0046E8C0` | `OnClearMap()` | |
| `0x0046D2C0` / `0x0046D5F0` | `LoadBinary` / `SaveBinary` | Virtual |
| `0x0046E930` / `0x0046EB10` / `0x0046ECF0` | `Load` / `Save` / `ResolveLoad` | |

`LHPoint` is the engine's vector type.

### Hand state machine

Abstract base `HandState`, 11 concrete states, all 5 virtual functions except
`HandStateGrain` and `HandStateHolding` which have 8. The vtables sit in one
contiguous block.

| State | vtable |
| --- | --- |
| `HandStateCamera` | `0x00900A54` |
| `HandStateCitadel` | `0x00900A80` |
| `HandStateCreature` | `0x00900AAC` |
| `HandStateGrain` | `0x00900B00` |
| `HandStateHolding` | `0x00900B40` |
| `HandStateMultiPickUp` | `0x00900B6C` |
| `HandStateNormal` | `0x00900BA0` |
| `HandStatePlayAnim` | `0x00900BC4` |
| `HandStateTotem` | `0x00900BE4` |
| `HandStateTug` | `0x00900C1C` |
| `HandStateInvisible` | `0x008CBEB4` |

`HandStateCamera`'s first five slots: `0x005B02F0`, `0x0046E5E0`,
`0x005B02D0`, `0x005B03E0`, `0x005B02E0`.

## Cameras

`CameraMode` base vtable `0x008C76E0`, **18 virtual functions**. Nine of the
fourteen subclasses implement exactly that shape, so `CameraModeVR` is a
drop-in of known size.

| Class | vtable | Functions | Constructed at |
| --- | --- | --- | --- |
| `CameraMode` | `0x008C76E0` | 18 | `0x0044A37A` |
| `CameraModeCitadel` | `0x008C7694` | 18 | `0x0044A1AC` |
| `CameraModeCtrInteract` | `0x008C7740` | 18 | `0x0044A85C` |
| `CameraModeDance` | `0x008C77A0` | 18 | `0x0044ABB1` |
| `CameraModeFlyAndClick` | `0x008C7814` | 18 | `0x0044AD6C` |
| `CameraModeFollow` | `0x008C7884` | 21 | `0x0044B812` |
| `CameraModeFollowHeading` | `0x008C7904` | 18 | `0x0044D8CA` |
| `CameraModeFree` | `0x008C7958` | 18 | `0x0044DF9C` |
| `CameraModeNew` | `0x008C7AB4` | 22 | `0x00450B3A` |
| `CameraModeNew1` | `0x008C7A58` | 22 | `0x00450BD7` |
| `CameraModeNew2` | `0x008C7B58` | 22 | `0x004540BA` |
| `CameraModeNew3` | `0x008C7BFC` | 24 | `0x004572EC` |
| `CameraModePath` | `0x008C7D10` | 18 | `0x00460E5C` |
| `CameraModeTwoObjects` | `0x008C7DD0` | 18 | `0x00461BC4` |

`CameraMode` base vtable slots 0 to 17: `0x0044A3C0`, `0x0044A290`,
`0x0044A2A0`, `0x0044A2B0`, `0x0044A390`, `0x0044A2C0`, `0x0044A2D0`,
`0x0044A2E0`, `0x0044A2F0`, `0x0044A300`, `0x0044A3A0`, `0x0044A310`,
`0x0044A320`, `0x00441700`, `0x0044A330`, `0x0044A340`, `0x0044A350`,
`0x0044A3B0`.

Also `GCamera` (80 functions, vtable `0x008C7110`), `InnerCamera`,
`WorldRoomCamera`, `ChallengeRoomCamera`, `CreatureRoomCamera`,
`GCameraEditor`.

## Gestures

`GestureSystem` vtable `0x008DF800`, 7 functions, constructed at `0x0054BB4B`
and `0x0054BB65`. Slot 1 is `0x0054BB60`. Companion classes
`GestureSystemData` and `GestureSystemDataList`. Templates live in
`Data\Gestures.jty`: a 4 byte count of **81**, then 12 byte records (10,989 of
them), so trajectory blocks are variable length.

## Config, command line and draw distance

| Address | Meaning |
| --- | --- |
| `0x00642E88` | reads `HardwareTnL` |
| `0x00642EFA` | reads `FullScreen` |
| `0x00642F49` | reads `ScreenW` |
| `0x00642F6F` | reads `ScreenH` |
| `0x008238C5` | reads `MaxObjectDistance`, the draw distance |
| `0x00641AA0` | `ScanParameters()`, the whole command line parser |
| `0x00641C60` | `pc_main()`, which acts on the parsed flags |
| `0x00641BE5`, `0x00641C08` | `-SETTINGS:` command line parsing |
| `0x00714B8C` | `QUESTDIR` handling |
| `0x005F445F`, `0x007DEE61` | tests `IsDeveloperPatch`, the editor unlock |

The graphics config loader is one function spanning roughly `0x00642E00` to
`0x00643000`.

### Command line switches

`ScanParameters` at `0x00641AA0` accepts `-` or `/` and matches these names.
Each sets one byte, and `pc_main` at `0x00641C60` reads them.

| Switch | Flag byte | Effect |
| --- | --- | --- |
| `-NEWGAME` | `0x00D46AB9` | Sets the game mode to single player and skips the front end menu |
| `-MULTIPLAYER` | `0x00D46ABA` | Game mode 3 |
| `-LAND` | `0x00D46ABB` | |
| `-SKIRMISH` | `0x00D46ABC` | Game mode 4, opens the skirmish box |
| `-PREINTROVIDEO` | `0x00D46ABD` | Guards `PlayPreIntroVideo` |
| `-FORCEINETCONN` | `0x00D46ABE` | |
| `-NOINETCONN` | `0x00D46ABF` | |
| `-EDITOR` | | |
| `-CONVERT` | | |
| `-VERSION` | | |
| `-NOLOADMUSIC` | | |
| `-SETTINGS:<file>` | | Reads `QUESTDIR` and `MAPFILE` from a config file |

`-NEWGAME` is the useful one for testing. It boots to gameplay with no menu
click, so a test run needs no window focus and no mouse.

## Method notes

Three independent techniques all agree, which is why the map above is
trustworthy:

1. **RTTI walk.** Type descriptor to complete object locator to vtable, across
   1052 classes. Produced every vtable address here without a debugger.
2. **Assert string attribution.** 411 distinct source paths, all referenced
   from `.text`, across **4,768 call sites**. Each site names the `.cpp` of
   its containing function.
3. **The decomp symbol file.** 37,057 names, confirmed to map onto our exe.

Use all three. Where they disagree, investigate rather than picking one.
