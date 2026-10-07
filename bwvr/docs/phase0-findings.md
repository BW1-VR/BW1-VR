# Phase 0 findings, part 1: static analysis

Done before running anything. The runtime probe is still required, but the
decomp's reconstructed headers already answer most of the question.

## The headline: the risk that could have killed the project is much reduced

The plan's Risk 1 was that the game might transform vertices on the CPU and
hand Direct3D pre-projected screen space vertices, leaving no matrix to
change. The evidence now says this does not matter, for a reason better than
"the matrices are in the API".

**The view and projection matrices are engine globals:**

- `LH3DRender::g_d3d_world2view` at `0x00EC8010`, type `LHMatrix`
- `LH3DRender::g_d3d_view2proj` at `0x00EC7FD0`, type `LHMatrix`

`LHMatrix` is 12 floats: a 3x3 rotation plus a translation at `m[9]`, `m[10]`,
`m[11]`.

Whether the engine transforms on the CPU or lets the GPU do it, **it uses
these same two matrices**. So writing per-eye values into them and rendering
again produces correct stereo either way. The `HardwareTnL` question stops
being existential and becomes a performance and compatibility detail.

This is exactly why the plan put the seam inside the game rather than in the
wrapper. That choice is now vindicated by evidence rather than by argument.

## There are two draw paths, and the split is useful

- `LH3DRender::DrawTriangle(Vertex3D*, uint32, uint16*, uint32)` at
  `0x0082F810`. `Vertex3D` is 32 bytes: `LHPoint position`, `Point2D uv`,
  `LHPoint normal`. That is **untransformed model space geometry**, no RHW.
- `DrawAndClip(D3DPRIMITIVETYPE, fvf, ..., count, indices, count)` at
  `0x0082A500` and `DrawAndClip2D(...)` at `0x0082A5B0`. The decomp types
  these vertices as `D3DTLVERTEX`, that is transformed and lit screen space
  vertices, and they take an explicit FVF.
- `ProjectPoint(Vertex3D*, uint*, LHPoint*)` at `0x005B7C10` is a software
  projection helper.

So the engine has a software transform and clip path alongside the hardware
path, and `DrawAndClip2D` is clearly the 2D and UI route. **That 3D versus 2D
split is the UI classifier the plan wanted**, and it is a named function
rather than an FVF heuristic.

Still to measure at runtime: which path terrain actually uses, and whether
that changes with `HardwareTnL`.

## A supported hook point, no detour needed

`LH3DRender::RegisterFinishFrameCallback(unsigned long, bool, void(__stdcall*)(void*), void*)`
at `0x0082F2C0`, with `RemoveFinishFrameCallback` at `0x0082F3B0`. The engine
offers end of frame callbacks. There is also a global
`g_enable_callbacks` at `0x00C386D0`.

The per frame bracket is `StartFrame()` at `0x0082F0E0` and `FinishFrame()` at
`0x0082F460`, with `g_started_frame` at `0x00ECA644`.

Correction to an earlier assumption: `0x0082B3C0` and `0x0082B570` are
`Open()` and `Close()`, which are initialization, not the per frame bracket.

## The device pointer is a global

`LH3DRender::Direct3DDevice7` at `0x00ECA638`. We can reach the
`IDirect3DDevice7` directly and do not have to hook `DirectDrawCreate` just to
get it.

## Projection construction is a named function with named parameters

`LH3DRender::SetProjMatrix(D3DMATRIX* param_1, D3DMATRIX* projection, float near_plane, float far_plane, float fov)`
at `0x0082B2D0`.

Near plane, far plane and field of view as separate floats is precisely what a
per eye asymmetric frustum needs. `GCamera::SetCameraFov(float fov, float time)`
at `0x00443680` is the higher level entry, and is also where frustum widening
for stereo culling belongs.

## A problem to design around: the camera cannot express roll

`GCamera` works in distance, heading and pitch:

- `GCamera::SetPointFromPointDistanceHeadingAndPitch(LHPoint*, const LHPoint*, float, float, float)` at `0x00442810`
- `GCamera::GetHeadingAndPitchFromPoints(LHPoint*, LHPoint*, float*, float*)` at `0x004428D0`
- `GCamera::Update()` at `0x00441F80`
- Members: `LHPoint pos`, `float time` at `0x68`, `float TimeDelta`. Inherits
  `GameThingWithPos`.

A tracked head pose is a full rigid transform and has roll. Two axes are not
enough. This is the same shape of problem openblack has.

**The workaround is already available:** write `g_d3d_world2view` directly
with a full rigid transform, instead of driving the camera through its
heading and pitch model. Let `GCamera` keep managing position and game logic,
and overwrite the view matrix after it runs.

## The hand and camera share one interaction model, confirmed in an enum

`CAMERA_MODE_HAND_STATUS` is declared with:
`NORMAL`, `ZOOMING`, `TILT_ON`, **`GRABBING_LAND`**, `PANNING`, `TILTING`,
plus three unnamed values.

`GRABBING_LAND` is the grab the world and pull yourself along primitive, which
is the most comfortable VR locomotion technique, already present as a first
class state. `CAMERA_MODE_MOUSE_STATUS` is `NONE`, `LEFT`, `MIDDLE`.

## How complete the decomp really is

The public figure of 5.67% decompiled counts byte exact function bodies. For
our purposes the useful number is different: **768 reconstructed headers and
820 source files**, including `Camera.h`, `CameraMode.h` and every
`CameraMode` subclass.

We need struct layouts and function signatures, not matching bodies, so the
decomp is far more valuable to this project than its headline suggests.

What is still missing and is real work for us: `CameraMode.h` states that only
the first three of the 18 virtual slots are recovered. Slot 0 is the
destructor at `0x0044A3C0`, slot 1 is
`CanPlayerGestureWhenCameraMoving()` at `0x0044A290`, slot 2 is `Update()` at
`0x0044A2A0`. `CameraMode` also holds `GCamera* camera` at offset `0x4`.

Recovering the other 15 slots is a prerequisite for `CameraModeVR`, and it is
work worth contributing back upstream.

## What the runtime probe still has to answer

1. Which draw path does terrain use, and does `HardwareTnL` change it?
2. Does `OpenD3D` at `0x0082CD80` accept the TnL HAL device on an RTX 3090?
3. Does any GDI text path run during normal gameplay, which would affect the
   d7vk choice?
4. What are the actual values in `g_d3d_world2view` and `g_d3d_view2proj`
   during play, so we can confirm handedness, units and world scale?
