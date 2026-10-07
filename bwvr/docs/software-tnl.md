# The engine transforms on the CPU, and this is where it does it

Measured and disassembled 2026-09-20 against `runblack.exe` v1.42,
SHA1 `9c9aea96f046911ab1b7e496fae511ed5ebbfefe`.

This document replaces the camera parts of `phase0-results.md`. It answers the
question that blocked the project: **which matrix does Black & White render
from?**

## The short answer

`LH3DTech` is a complete software transform and lighting engine. It projects
every point on the CPU against a matrix at **`0x00EA9E40`**, and
`LH3DRender` only hands the finished screen space vertices to Direct3D.

The Direct3D VIEW transform that the engine also sets is a copy that nothing
in the scene reads. That is why three earlier attempts to steer the camera
failed. They all changed the copy.

| What we tried | Why it did nothing |
| --- | --- |
| Write `LH3DRender::g_d3d_world2view` at `StartFrame` | It is a Direct3D copy, not the render matrix |
| Write the same global at the first draw of the frame | Same reason |
| Roll `D3DTRANSFORMSTATE_VIEW` at the device | The scene never uses the fixed function transform |

## The proof

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
vertices are already in screen space.** The struct the decomp calls `Vertex3D`
is 32 bytes and holds x, y, z, rhw, diffuse, specular, u, v. The name is
misleading.

All three draw paths use the same format. `DrawAndClip` and `DrawAndClip2D`
take their FVF as an argument, and every call in a whole session passed
`0x1C4`.

So an earlier claim in `phase0-results.md` was wrong. It said 95.5% of
vertices arrive untransformed. They do not. That number came from the symbol
name `Vertex3D`, not from a measurement. **Risk 1 in the plan is real: the
game submits pre-projected screen space geometry.**

## A calling convention that mattered

`LH3DRender::DrawTriangle` is **`__fastcall`**, not `__cdecl`. All 26 call
sites look like this:

```
mov ecx, <vertex array>           ; arg 1 in ecx
movzx edx, word ptr [count]       ; arg 2 in edx
push <index count>                ; arg 4 on the stack
push <index array>                ; arg 3 on the stack
call 0x0082F810                   ; the callee ends in "ret 8"
```

The probe declared it `__cdecl`. That reads the wrong arguments and, worse,
leaves the stack 8 bytes out on every call. The game then died about ten
seconds after the first call, which looked exactly like a slow loading screen.
It cost a while to find, because the symptom was silent and consistent.

Two consequences:

1. **The `DrawTriangle` row of the old draw path table is not trustworthy.**
   What it counted as "vertices" was the index count of the wrong argument.
   The `DrawAndClip` and `DrawAndClip2D` rows are fine. Both really are
   `__cdecl`: they read every argument from the stack and end in a plain
   `ret`.
2. Check the convention of every Intel compiled function before hooking it.
   The bw1-decomp signature says `__cdecl` because that is what the mangled
   name encodes, and the mangled name does not record what the Intel compiler
   actually did. The epilogue does: `ret N` means the callee cleans, and a
   prologue that reads `ecx` or `edx` before writing them means register
   arguments.

`LH3DTech::UpdateCamera` has the same shape: two pointer arguments in `ecx`
and `edx`, no stack arguments, and a plain `ret`. That is MSVC `__fastcall`
with two arguments.

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

That is the whole projection. There is no separate projection matrix.

`m` is the 12 floats at `0x00EA9E40`. The layout is **4 rows of 3**, with the
translation in the last row, so the camera basis vectors are the **columns**:

- column 0 is the camera right axis,
- column 1 is up,
- column 2 is forward,
- `m[9..11]` is the camera space origin.

This matches the decomp's `LHMatrix::SetIdentityMatrix`, which writes 1.0 into
`m[0]`, `m[4]` and `m[8]`. An earlier note in `phase0-results.md` called that
a decomp bug. It is not. The decomp is right, and the earlier note was
measuring `g_d3d_world2view`, which is not an `LHMatrix` at all. It is the
first 12 floats of a Direct3D 4x4.

### The field of view lives in the matrix

Compare the same camera as the engine wrote it in both places:

```
Direct3D VIEW row 0     [ -0.7071  -0.4082  -0.5774 ]
world2camera row 0      [ -1.0099  -1.0365  -0.5774 ]
```

Column 0 is scaled by 1.4281 and column 1 by 2.5389. Those are exactly
`InfoTransform.inv_half_tan_fov_x` and `inv_half_tan_fov_y`. So
`LH3DTech::UpdateCamera` bakes the field of view into the basis, and
`ProjectPoint` then only has to divide and scale to half the resolution.

**For VR this is good news.** One matrix carries orientation, position and
field of view together, and per eye we need to change all three.

## InfoTransform, at 0x00E839E4

60 bytes. Measured at 1280x720:

```
resolution        1280 x 720
aspect x/y        1.7778
half res          640.00, 360.00
inv half tan fov  x 1.4281   y 2.5389
cos half fov      0.8192    (squared 0.5736)
implied fov       70.0 deg horizontal, 43.0 deg vertical
cull cone half    35.0 deg
```

Three things to note.

1. The field of view is **70 degrees horizontal and 43 vertical**, and the
   vertical follows the window. `tan(21.5) / tan(35) = 0.5626`, which is 9/16.
   An earlier note said the game builds a 4:3 frustum in a 16:9 window. It
   does not. It adapts.
2. `cos_half_fov` is `cos(35)`, the horizontal half angle. **This is the
   culling cone**, and it is one float. Risk 2 in the plan said mono frustum
   culling is invisible to a wrapper and needs a patch. It is not invisible
   here, because the whole frustum is ours.
3. The **field order in the decomp header is swapped**. `0x1C` holds the
   horizontal term and `0x20` the vertical one, the opposite of the names
   `InvHalfTanFovY` and `InvHalfTanFovX`. Worth correcting upstream.

## The camera entry points

Two functions write the whole matrix. Both take a position and a focus point
in `ecx` and `edx`, the Intel compiler's register convention.

| Address | Name | Notes |
| --- | --- | --- |
| `0x00819920` | `LH3DTech::UpdateCamera(LHPoint&, LHPoint&)` | Builds the matrix, then copies it to Direct3D through `LH3DRender::SetD3DMatrix` |
| `0x00819F50` | unnamed, 0x800 bytes | Same shape and same argument registers, its own up vector at `0x00EA1B28` |

Both call `LH3DTech::UpdateWorldToCamera` at `0x00819690`, which normalizes
`focus - position` and builds the basis.

`LH3DTech::UpdateCamera` calls `LHMatrix::SetInverse` right after, so a
camera to world matrix is kept at `0x00EA9E80`. Anything that needs to turn a
screen position back into the world can use it, which the Hand work will.

## Why this is better than the wrapper route

The plan's decision tree called this outcome 4: "the wrapper route dies, the
project becomes purely engine level". That is correct, and the plan already
said outcomes 3 and 4 are "arguably the better architecture anyway". This
result confirms it, and it is stronger than expected:

- **One matrix drives everything.** Position, orientation and field of view
  are in the same 12 floats, written by two known functions.
- **Culling is ours.** `cos_half_fov` is a single float in the same struct as
  the projection. Widening the frustum for two eyes and widening the cull cone
  to match is one change, not a search.
- **The projection is plain and readable.** `screen = (1 +- cam/cam.z) * half`
  has a hardcoded center of 1.0. An asymmetric per eye frustum needs that
  center to move, which is a small, well understood change.
- **No reprojection tricks.** Rendering the scene twice with two matrices is
  the honest approach, and a 2001 game on an RTX 3090 can afford it.

## The roll test passed

Armed a 180 degree roll on the matrix at `0x00EA9E40` from a hook on
`LH3DTech::UpdateCamera`, and played to Land 1. **The picture turned upside
down.** That is the gate the plan set for every claim of this kind, and it is
the first time anything we changed reached the screen.

![Land 1 rendered upside down by a 180 degree roll on the world to camera
matrix](images/roll-180-proof.png)

The terrain hangs from the top of the frame and the sky is at the bottom. The
frame came out of the game itself through `bwvr/src/probe/frame_grab.cpp`, so
no window had to be in front to record it.

The log shows the change landing on every call:

```
UpdateCamera call 3600
  position (-15.04, 12.94, 14.41)
  focus    (0.00, 0.00, 0.00)
  world2camera as written by the engine
    [ -0.9878   0.9676   0.6135 ]
    [  0.0000   2.1567  -0.5277 ]
    [ -1.0314  -0.9267  -0.5875 ]
    [  0.0000   0.0000  24.5193 ]  <- translation
  world2camera after our change
    [  0.9878  -0.9676   0.6135 ]
    ...
```

Columns 0 and 1 are negated, which is what a 180 degree roll does to the right
and up axes.

### One hook is enough

The probe counted how often the matrix changed and whether an `UpdateCamera`
call came first:

```
world2camera changed on 5039 of 12000 frames,
0 of those with no UpdateCamera call
```

**Zero.** Every change to the matrix in a whole session came through
`LH3DTech::UpdateCamera`. The second writer at `0x00819F50` never ran in this
path. So one hook, on one function, owns the camera.

In gameplay it is called about once per frame, which is the rate per eye
rendering needs.

### What this settles

| Plan risk | State |
| --- | --- |
| 1. Geometry may not be re-transformable | Confirmed as feared at the Direct3D layer, and irrelevant now. The transform is ours, one level up |
| 2. Mono frustum culling is invisible to a wrapper | Not invisible. `cos_half_fov` is one float in `InfoTransform` |
| 4. d7vk is untested on Windows | Lower priority. The scene never needed the fixed function pipeline, so the backend only has to present |

Phase 1 can start.

## Open questions

1. Find where `DrawAndClip2D` gets its coordinates. It should not go through
   `ProjectPoint` at all, because it is the 2D and UI route.
2. Decide how to make the projection center asymmetric: patch the two 1.0
   constants in `ProjectPoint`, or absorb the shift into the matrix.
3. Find every caller of `LH3DTech::UpdateCamera`, so the VR camera can supply
   a position and a focus point of its own instead of only editing the result.
4. Check whether the cinematic camera drives the same function. If it does,
   cutscenes are reachable through the same hook.

## Tools this produced

`bwvr/tools/disasm.py` disassembles any address with the bw1-decomp symbol
names attached, finds symbols by regex, and finds references to an address.
A full Ghidra analysis pass on a 4.88 MB `.text` takes tens of minutes. This
answers a single question in under a second, and it found everything above.

```
python bwvr/tools/disasm.py 0x0082F810        # one function
python bwvr/tools/disasm.py --find LH3DTech   # search symbols
python bwvr/tools/disasm.py --xref 0x00EA9E40 # who touches this
```

`bwvr/src/probe/frame_grab.cpp` saves the render target to a BMP from inside
the game. Capturing the window from the desktop needs the window in front,
which fights with using the machine. This does not.
