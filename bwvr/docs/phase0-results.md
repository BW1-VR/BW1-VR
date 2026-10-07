# Phase 0 results: the probe ran

Measured 2026-09-20 on an RTX 3090, driver 32.0.16.1088, Windows 11.
Two runs of `runblack.exe` v1.42 in windowed mode at 1280x720, with the
pass-through `ddraw.dll` probe from `bwvr/src/probe`.

Raw logs: `probe-run1-tnl-off.log` (51,000 frames) and
`probe-run2-tnl-on.log` (30,600 frames).

> **Superseded, same day.** Two claims below turned out to be wrong, and the
> disassembly settled both. Read
> [software-tnl.md](software-tnl.md) instead.
>
> 1. **Geometry does not arrive untransformed.** The 95.5% figure came from
>    the symbol name `Vertex3D`, not from a measurement.
>    `LH3DRender::DrawTriangle` hardcodes FVF `0x1C4` (XYZRHW) in its
>    `DrawIndexedPrimitive` call, so every path submits screen space
>    vertices. Risk 1 in the plan is real.
> 2. **`LHMatrix` is 4 rows of 3, not 3 rows of 4.** The decomp was right.
>    The note below was measuring `g_d3d_world2view`, which is not an
>    `LHMatrix` but the first 12 floats of a Direct3D 4x4.
>
> The camera itself is now found. `LH3DTech` transforms the scene on the CPU
> against a matrix at `0x00EA9E40`, written by `LH3DTech::UpdateCamera` at
> `0x00819920`.
>
> The draw path counts below are still valid, and so is the hardware
> transform and lighting section.

## Draw path split (the counts hold, the reading of them does not)

Over 51,000 frames:

| Path | Calls | Vertices | Share of vertices |
| --- | ---: | ---: | ---: |
| `LH3DRender::DrawTriangle` (see the warning below) | 41,634,804 | 610,100,094 | not comparable |
| `DrawAndClip` (XYZRHW) | 83,281 | 17,231,282 | 2.7% |
| `DrawAndClip2D` (XYZRHW) | 1,979,150 | 11,502,840 | 1.8% |

> The `DrawTriangle` row is **not trustworthy**. The probe declared that
> function `__cdecl`, and it is `__fastcall`. The count it recorded as
> vertices was really the index count of the wrong argument. The call count
> is right. The `DrawAndClip` and `DrawAndClip2D` rows are correct, because
> those two really are `__cdecl`.

`Vertex3D` is not what the name suggests. It is 32 bytes of x, y, z, rhw,
diffuse, specular, u, v: a **screen space** vertex. All three paths submit
FVF `0x1C4`. See [software-tnl.md](software-tnl.md).

Every path uses one vertex format, **FVF `0x01C4`**: XYZRHW plus diffuse,
specular and one texture. `DrawAndClip2D` is the 2D and UI route, and
`DrawAndClip` is the software-clipped 3D route for geometry that straddles the
frustum edge. Together they are 4.5% of vertices.

That gives us the UI classifier the plan wanted, and it is better than an FVF
heuristic: **`DrawAndClip2D` is a named function**. Draw it once, mono, on a
quad at a comfortable depth.

## All five hooks landed

Every address derived from the RTTI walk and the decomp symbol file hooked
cleanly on the first attempt:

```
hook ok  LH3DRender::OpenD3D        at 0x0082CD80
hook ok  LH3DRender::FinishFrame    at 0x0082F460
hook ok  LH3DRender::DrawTriangle   at 0x0082F810
hook ok  DrawAndClip                at 0x0082A500
hook ok  DrawAndClip2D              at 0x0082A5B0
```

The image loaded at `0x00400000` exactly as predicted, so no address fixups
are needed. `OpenD3D` starts with `81 EC 9C 01 00 00`, a 412 byte stack frame.

## The matrix globals: not what the scene renders from

`g_d3d_world2view` at `0x00EC8010` and `g_d3d_view2proj` at `0x00EC7FD0` are
Direct3D copies. `LH3DTech::UpdateCamera` writes them through
`LH3DRender::SetD3DMatrix` after it has built the real matrix. Nothing in the
scene reads them, and writing them changes nothing on screen.

`g_d3d_world2view` is the first 12 floats of a Direct3D 4x4 matrix, not an
`LHMatrix`. Reading it as 3 rows of 4 happened to give orthonormal rows,
which is what produced the wrong layout note in the first version of this
document.

The matrix the scene really uses is at `0x00EA9E40`, it is 4 rows of 3, and
the camera basis is in its columns. See [software-tnl.md](software-tnl.md).

Projection, unchanged across both runs:

```
[ 1.428   0.000   0.000 ]
[ 0.000   0.000   1.904 ]
[ 0.000   0.000   0.000 ]
[ 0.000   1.000   1.000 ]
```

Reading `1.428` as the cotangent of the half angle gives a **70 degree
horizontal field of view**, which the direct read of `InfoTransform` later
confirmed. The vertical follows the window rather than staying at 4:3: at
1280x720 it is 43 degrees, and `tan(21.5)/tan(35)` is 0.5626, which is 9/16.

Two consequences for VR:

1. **70 degrees is far short of a headset's roughly 100 degrees.** This is the
   frustum widening target. The real entry point is `LH3DTech::ChangeFov` at
   `0x008195B0`, which writes the `InfoTransform` terms that
   `LH3DTech::UpdateCamera` then bakes into the camera matrix.
2. The projection center is hardcoded at 1.0 in `LH3DTech::ProjectPoint` and
   has to become per eye and asymmetric.

## Hardware transform and lighting: the question dissolved

The RTX 3090 reports, through the plain Direct3D 7 HAL device:

```
hardware transform and lighting : YES
hardware rasterization          : YES
max vertex blend matrices       : 4
device GUID : 84E63DE0-46AA-11CF     (IID_IDirect3DHALDevice)
```

Setting `HardwareTnL = 1` in the registry **did not change the device GUID**.
Both runs got `IID_IDirect3DHALDevice`, never `IID_IDirect3DTnLHalDevice`
(`F5049E78-...`). So either `OpenD3D` does not consult that registry value
directly, or the TnL HAL enumeration is declined by the modern driver.

It does not matter. The plain HAL already advertises hardware transform and
lighting, and the draw path distribution was materially the same in both runs.
**`HardwareTnL` is not a blocker and does not need further investigation.**

Note that `OpenD3D` returned `0` in both runs while the device was created
successfully and the game rendered. So the return value is not a simple
success flag, contrary to the decomp's `bool32_t` annotation. Worth correcting
upstream once confirmed.

## What this means for the architecture

The plan put the stereo seam inside the game rather than in the wrapper, and
measurement supports that:

- 95.5% of geometry is still untransformed when the engine receives it.
- The UI has its own named entry point, so it needs no heuristic.
- The per-frame bracket exists and hooks cleanly.
- The engine's field of view is reachable through one named function.

How to move the camera is established and proven on screen: write the matrix
at `0x00EA9E40` from a hook on `LH3DTech::UpdateCamera`. **Phase 1 can
start.**

## Still open

1. `DrawAndClip` handles 17.2M vertices of software-clipped **3D** geometry.
   Confirm it derives its screen space positions from the same two globals. If
   it caches its own copy, that path needs separate handling.
2. ~~Both runs sat in the menu rather than in gameplay.~~ Resolved: later
   runs reached the tutorial on Land 1, with the Temple, a Creature and
   terrain on screen, and the draw path split was materially the same.
3. Decode the exact `view2proj` convention, including where the near and far
   planes live, since a 3x4 `LHMatrix` cannot hold a full perspective
   projection on its own.
4. Check whether any GDI text path runs during gameplay, which affects the
   d7vk choice.

## Reproducing

```
cmake -S bwvr -B bwvr/build -A Win32
cmake --build bwvr/build --config Release
copy bwvr\build\src\probe\Release\ddraw.dll "Black & White Game\"
```

Run the game, then read `bwvr-probe.log` beside `runblack.exe`. Delete
`ddraw.dll` to remove the probe. It only observes, and it refuses to hook if
the image base is not `0x00400000`.


## Camera control: solved

**The roll test passed.** A 180 degree roll on the matrix at `0x00EA9E40`,
applied from a hook on `LH3DTech::UpdateCamera`, turns the picture upside
down in gameplay. Every matrix change in a whole session came through that one
function. See [software-tnl.md](software-tnl.md).

### The three earlier attempts, and why they failed

Every one of them changed a Direct3D copy that the scene does not read.

| Experiment | Where the change was made | Result |
| --- | --- | --- |
| 1 | Write `g_d3d_world2view` in `LH3DRender::StartFrame` | No visible change |
| 2 | Write `g_d3d_world2view` at the first `DrawTriangle` of each frame | No visible change |
| 3 | Roll `D3DTRANSFORMSTATE_VIEW` inside `IDirect3DDevice7::SetTransform` | No visible change |

The device hook is what made it readable. Over 36,000 frames the engine set
`VIEW` about once per frame and never set `WORLD` or `PROJECTION` at all. A
renderer that sets one transform, never the other two, and ignores the one it
sets, is not using the fixed function pipeline for its scene.

The answer is in [software-tnl.md](software-tnl.md): `LH3DTech::ProjectPoint`
projects every point against the matrix at `0x00EA9E40`, and
`LH3DTech::UpdateCamera` at `0x00819920` builds it.

### A note on method

The first version of this document overclaimed from indirect evidence: the
matrices existed, they were writable, and they tracked the camera, so it
looked settled. It was not settled until something visible changed on screen.
The roll test is cheap and binary, and it should be the gate for every claim
of this kind.

The second lesson is narrower and just as useful. **A symbol name is not a
measurement.** `Vertex3D` sounded like an untransformed vertex, so the draw
path counts were read as proof that geometry arrives untransformed. One line
of disassembly, `push 0x1c4`, said otherwise. Read the code.
