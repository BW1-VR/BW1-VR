# The engine transforms on the CPU

Facts in this file are for `runblack.exe` v1.42, SHA1
`9c9aea96f046911ab1b7e496fae511ed5ebbfefe`. They come from the disassembly.

## Summary

`LH3DTech` is a complete software transform and lighting engine. It projects
every point on the CPU against a matrix at **`0x00EA9E40`**, and `LH3DRender`
only hands the finished screen space vertices to Direct3D.

The Direct3D VIEW transform that the engine also sets is a copy that nothing
in the scene reads. Changes to `LH3DRender::g_d3d_world2view` or to
`D3DTRANSFORMSTATE_VIEW` do not reach the screen.

The matrix at `0x00EA9E40` holds position, orientation and field of view
together. One hook on `LH3DTech::UpdateCamera` owns the camera. The mod uses
this for both eyes (`bwvr/src/mod/eye_camera.cpp`), for the head camera and
for the asymmetric frustum of each eye.

## The Direct3D submission

`LH3DRender::DrawTriangle` at `0x0082F810` ends like this:

```
0x0082F8FD  mov eax, dword ptr [0xeca638]   ; LH3DRender::Direct3DDevice7
0x0082F902  mov esi, dword ptr [eax]        ; its vtable
0x0082F904  push 0                          ; dwFlags
0x0082F906  push edi                        ; dwIndexCount
0x0082F90B  push edi                        ; lpwIndices
0x0082F90C  push edx                        ; dwVertexCount
0x0082F90D  push ecx                        ; lpvVertices
0x0082F90E  push 0x1c4                      ; dwVertexTypeDesc  <-- the FVF
0x0082F913  push 4                          ; D3DPT_TRIANGLELIST
0x0082F915  push eax                        ; this
0x0082F916  call dword ptr [esi + 0x68]     ; slot 26, DrawIndexedPrimitive
```

FVF `0x1C4` is `D3DFVF_XYZRHW | DIFFUSE | SPECULAR | TEX1`. **XYZRHW means the
vertices are already in screen space.** The struct that the decomp calls
`Vertex3D` is 32 bytes and holds x, y, z, rhw, diffuse, specular, u, v. The
name is misleading. All three draw paths use this format. `DrawAndClip` and
`DrawAndClip2D` take their FVF as an argument, and every call passes `0x1C4`.

## Calling conventions

`LH3DRender::DrawTriangle` is **`__fastcall`**, not `__cdecl`. All 26 call
sites look like this:

```
mov ecx, <vertex array>           ; arg 1 in ecx
movzx edx, word ptr [count]       ; arg 2 in edx
push <index count>                ; arg 4 on the stack
push <index array>                ; arg 3 on the stack
call 0x0082F810                   ; the callee ends in "ret 8"
```

`DrawAndClip` and `DrawAndClip2D` are `__cdecl`. They read every argument from
the stack and end in a plain `ret`.

`LH3DTech::UpdateCamera` takes two pointer arguments in `ecx` and `edx`, has no
stack arguments and ends in a plain `ret`. That is MSVC `__fastcall` with two
arguments.

Check the convention of every Intel compiled function before you hook it. The
bw1-decomp signature says `__cdecl` because the mangled name encodes that, and
the mangled name does not record what the Intel compiler did. The epilogue
does: `ret N` means the callee cleans the stack, and a prologue that reads
`ecx` or `edx` before it writes them means register arguments. A wrong
convention leaves the stack 8 bytes out on every call, and the game dies some
seconds later with no clear symptom.

## Where the transform happens

`LH3DTech::ProjectPoint` at `0x00819390`:

```
cam.x = p.x*m[0] + p.y*m[3] + p.z*m[6] + m[9]
cam.y = p.x*m[1] + p.y*m[4] + p.z*m[7] + m[10]
cam.z = p.x*m[2] + p.y*m[5] + p.z*m[8] + m[11]

if (cam.z < near_plane) return 0;          // near plane clip, near at 0x00E839E0

screen_x = (1.0 + cam.x / cam.z) * half_res_x
screen_y = (1.0 - cam.y / cam.z) * half_res_y
```

That is the whole projection. There is no separate projection matrix. The
center of the projection is a hardcoded 1.0. An asymmetric frustum for each
eye must move this center.

`m` is the 12 floats at `0x00EA9E40`. The layout is **4 rows of 3**, with the
translation in the last row, so the camera basis vectors are the **columns**:

- column 0 is the camera right axis,
- column 1 is up,
- column 2 is forward,
- `m[9..11]` is the camera space origin.

This matches the decomp's `LHMatrix::SetIdentityMatrix`, which writes 1.0 into
`m[0]`, `m[4]` and `m[8]`. `g_d3d_world2view` is not an `LHMatrix`. It is the
first 12 floats of a Direct3D 4x4.

### The field of view lives in the matrix

For the same camera, the engine writes two different matrices:

```
Direct3D VIEW row 0     [ -0.7071  -0.4082  -0.5774 ]
world2camera row 0      [ -1.0099  -1.0365  -0.5774 ]
```

Column 0 is scaled by 1.4281 and column 1 by 2.5389. Those are exactly
`InfoTransform.inv_half_tan_fov_x` and `inv_half_tan_fov_y`.
`LH3DTech::UpdateCamera` bakes the field of view into the basis, and
`ProjectPoint` then only divides and scales to half the resolution. So one
matrix carries orientation, position and field of view, and each eye changes
all three. The unscaled copies and the other members of the camera family are
in `stereo-artifacts.md`.

## InfoTransform, at 0x00E839E4

60 bytes. Values at 1280x720:

```
resolution        1280 x 720
aspect x/y        1.7778
half res          640.00, 360.00
inv half tan fov  x 1.4281   y 2.5389
cos half fov      0.8192    (squared 0.5736)
implied fov       70.0 deg horizontal, 43.0 deg vertical
cull cone half    35.0 deg
```

1. The field of view is **70 degrees horizontal and 43 vertical**, and the
   vertical follows the window: `tan(21.5) / tan(35) = 0.5626`, which is 9/16.
   The game adapts the frustum to the aspect of the window.
2. `cos_half_fov` is `cos(35)`, the horizontal half angle. **This is the
   culling cone**, and it is one float in the same struct as the projection.
   A wider frustum for two eyes needs a wider cull cone, and that is one
   change.
3. The **field order in the decomp header is swapped**. `0x1C` holds the
   horizontal term and `0x20` the vertical one, the opposite of the names
   `InvHalfTanFovY` and `InvHalfTanFovX`. The table in `address-map.md` uses
   the order of the binary.

## The camera entry points

Two functions write the whole matrix. Both take a position and a focus point
in `ecx` and `edx`.

| Address | Name | Notes |
| --- | --- | --- |
| `0x00819920` | `LH3DTech::UpdateCamera(LHPoint&, LHPoint&)` | Builds the matrix, then copies it to Direct3D through `LH3DRender::SetD3DMatrix` |
| `0x00819F50` | unnamed, 0x800 bytes | Same shape and same argument registers, its own up vector at `0x00EA1B28` |

Both call `LH3DTech::UpdateWorldToCamera` at `0x00819690`, which normalizes
`focus - position` and builds the basis.

`LH3DTech::UpdateCamera` calls `LHMatrix::SetInverse` right after, so a camera
to world matrix is kept at `0x00EA9E80`. Use it to turn a screen position back
into the world.

Every change to the matrix at `0x00EA9E40` in a long session came through
`LH3DTech::UpdateCamera`. The second writer at `0x00819F50` did not run in
that path. So one hook on one function owns the camera. In gameplay the game
calls it about once per frame, which is the rate that rendering for each eye
needs.

## What this means for the mod

- **One matrix drives everything.** Position, orientation and field of view
  are in the same 12 floats, written by two known functions.
- **Culling is ours.** The cull cone is a single float in `InfoTransform`.
- **The scene never uses the fixed function pipeline.** The backend only
  has to present the finished picture.
- **The scene renders twice**, once for each eye, with two matrices. There
  is no reprojection.

A 180 degree roll of the matrix at `0x00EA9E40` from a hook on
`LH3DTech::UpdateCamera` turns the whole picture upside down. This is the
test for any claim that a change reaches the picture.

## Open questions

1. Where `DrawAndClip2D` gets its coordinates. It should not go through
   `ProjectPoint`, because it is the 2D and interface route.
2. How to make the projection center asymmetric: patch the two 1.0 constants
   in `ProjectPoint`, or absorb the shift into the matrix.
3. Every caller of `LH3DTech::UpdateCamera`, so that the VR camera can supply
   a position and a focus point instead of only editing the result.
4. Whether the cinematic camera drives the same function. If it does, cut
   scenes are reachable through the same hook.

## Tools

`bwvr/tools/disasm.py` disassembles any address with the bw1-decomp symbol
names attached, finds symbols by regex, and finds references to an address.
It answers a single question in under a second.

```
python bwvr/tools/disasm.py 0x0082F810        # one function
python bwvr/tools/disasm.py --find LH3DTech   # search symbols
python bwvr/tools/disasm.py --xref 0x00EA9E40 # who touches this
```

`bwvr/src/probe/frame_grab.cpp` saves the render target to a BMP from inside
the game. A capture from the desktop needs the window in front, and this does
not.
