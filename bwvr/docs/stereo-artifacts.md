# Stereo artifacts, and what causes them

State after Phase 2, second pass. Stereo works: the scene renders twice per
frame, once per eye, at a locked 90 Hz.

The first version of this document listed four things that looked wrong and
offered guesses about each. This version replaces the guesses with
measurements, because a tool now exists that can tell the difference.

## The draw probe

The two eye passes change one number: `m[9]` of the world to camera matrix.
`LH3DTech::ProjectPoint` computes

```
cam.x = p.x*m[0] + p.y*m[3] + p.z*m[6] + m[9]
cam.y = p.x*m[1] + p.y*m[4] + p.z*m[7] + m[10]
cam.z = p.x*m[2] + p.y*m[5] + p.z*m[8] + m[11]
```

so `m[9]` can only move `cam.x`. **For a point at a fixed world position the
projected y and the rhw are therefore identical in both passes, and only x may
differ.** That is an exact test with nothing to tune, and it turns every
report of "this looks wrong" into a number.

`draw_probe.cpp` applies it at the Direct3D device draw methods, which every
triangle passes through, and sorts each batch into one of five outcomes:

| Outcome | Meaning |
| --- | --- |
| `rebuilt` | y or rhw moved, so the object was at a different world position in the second pass |
| `retex` | the vertices held still and the texture coordinates moved, which is how a projected shadow goes wrong |
| `relit` | the vertices held still and the vertex colors moved, so lighting was computed from the camera |
| `flat` | x did not move either, so the batch has no disparity and is the screen space interface |
| `stereo` | x moved, y and rhw held, which is correct |

Each batch carries the address of the subsystem that asked for the draw, so
the report names the code rather than the renderer. Resolve one with:

```
python bwvr/tools/disasm.py 0x????????
```

### Running it without a headset

`stereo_headless=1` runs both passes and sends nothing to a runtime. Stereo
correctness is a property of what the two passes submit, and that happens
inside the game, so the headset is not needed to measure it. A measuring run
is one command on the desktop and takes about ninety seconds.

```ini
[bwvr]
vr=0
stereo=1
stereo_headless=1
draw_probe=1
draw_probe_delay=8000
draw_probe_frames=150
```

`draw_probe_delay` matters. The mod loads before the island does, and the
first attempt spent its whole measurement on the loading screen and reported
one draw call.

## Where every triangle goes

Measured, not assumed. The whole frame reaches Direct3D through four call
sites, all `DrawIndexedPrimitive`:

| Call site | Function | Share | What it carries |
| --- | --- | --- | --- |
| `0x0082F919` | `LH3DRender::DrawTriangle` | 19951 batches | the world |
| `0x0082A81B` | `DrawAndClip2D` | 3900 | flat interface, z 0 to 0.33 |
| `0x0082A5A9` | `DrawAndClip` | 3000 | flat interface, z 0, rhw 1 |
| `0x0082F617` | `LH3DRender::FinishFrame` | 149 | the sky, z 1.0 |

Those depth values look like they separate the interface from the world:

```
world geometry   z 0.9751 .. 0.9998
flat interface   z 0.0000 .. 0.3333
the sky          z 1.0000
```

**They do not, and believing they did caused a bug.** See "Depth is not a
classifier" below.

## Fixed

### The camera is a family of matrices, not one matrix

This is the finding that explains most of what was wrong.
`LH3DTech::UpdateCamera` builds several values from one camera position, and
different parts of the engine read different members. The mod moved one of
them.

| Address | What it is | Who reads it |
| --- | --- | --- |
| `0x00EA1D28` | world to camera, **unscaled** | `LH3DSprite::Draw` |
| `0x00EA9E40` | the same, scaled by the field of view terms | `LH3DTech::ProjectPoint` |
| `0x00EA1D58` | a copy of the scaled one | `GLandscape::Draw` |
| `0x00EA9DE0` | the inverse of the scaled one | 17 places, including shadows |

The scaling relation is exact, at `0x00819B40`:
`fld [0xea1d4c] / fmul st(1) / fstp [0xea9e64]`. So an offset of d world units
is d on the unscaled matrix and `d * inv_half_tan_fov_x` on the scaled ones.

### Smoke and particles had no depth at all

`LH3DSprite::Draw` at `0x00840530` reads the **unscaled** matrix and nothing
else. It loads `m[9]` from `0x00EA1D4C` at `0x008405B4`. The mod never touched
that address, so every sprite landed on the same screen position in both eyes.
Identical pixels are zero disparity, and zero disparity reads as infinitely
far away.

Measured, in the same scene: zero disparity sprite batches fell from **6350 to
132**, and batches with correct disparity rose from 0 to 5232.

### Villager shadows changed shape between the eyes

A shadow is a projected texture, so the bug moved texture coordinates and left
the vertices alone. The first version of the probe compared positions only and
could not see it.

`fn_008801D0` builds each shadow's texture matrix by copying the camera to
world matrix at `0x00EA9DE0`. The ground vertices that receive the shadow are
transformed by the world to camera matrix. The two cancel, and the shadow
lands in the right place, **as long as they agree**.

They stopped agreeing. `UpdateCamera` is not called from `Process3dEngine`, so
poking `0x00EA9E40` between the passes left `0x00EA9DE0` at the mono camera.
What survived the failed cancellation was a translation along the eye offset,
and the per vertex perspective divide turned that translation into a skew. The
shadow slid across the ground and changed shape, in opposite directions for
the two eyes.

The fix is not to reuse the first pass geometry. The shadow map, the light
position and the bounding box are already identical in both passes. The
projection has to differ per eye, and it is correct as soon as the inverse is
the true inverse of the matrix in use. `eye_camera.cpp` calls the game's own
`LHMatrix::SetInverse` at `0x007FB290` after each move.

Measured: `fn_0084BA90` fell from 158 rebuilt batches to 0.

### Depth is not a classifier

Every path writes the same two fields, and
`LH3DTech::Draw3DScreenTriangle` at `0x0081C5DD` shows how:

```
rhw = near / camera_z        near is 3.5, at 0x00E839E0
z   = 1.0 - rhw
```

So `z < 0.9` means `rhw > 0.1`, which means `camera_z < 35` world units. It
does not say "interface". It says "near the camera". The 0.975 measured for
world geometry was a zoomed out camera and nothing more.

The interface shift used that test, so zooming in dragged the water and the
ground forward with the menu. The water made it obvious, because
`GLandscape::Draw` submits the water surface through `fn_00879930` at a
constant depth of **exactly the near plane**, the same z as a menu box. No
depth test can tell those two apart.

The test is now the **calling function**, which cannot be confused by the
camera. The ranges, all single contiguous blocks:

| Range | What |
| --- | --- |
| `0x00411690` to `0x00413FF0` | `SetupThing`, the whole widget toolkit |
| `0x0081C5C0` to `0x0081C780` | `LH3DTech::Draw3DScreenTriangle`, text |
| `0x00447450` to `0x00447815` | `CameraHelp` quads, the screen edge hints |
| `0x0081E3C0` to `0x0081E58C` | `HelpSystem` quads, the subtitle bar |

`ui_accounting=1` logs every function that submits a screen space batch and
whether it was moved, so an interface path nobody has found yet shows up as a
line in the log rather than as a complaint.

### The interface has to follow the world when the camera zooms

A fixed shift cannot work. The disparity of a world vertex, in pixels per eye,
is

```
scale * separation * half_res_x * rhw / (2 * near)
```

which is proportional to `rhw`, and `rhw` is how near the vertex is. Zoomed
out, the nearest ground has `rhw` near 0.03 and eight pixels puts the
interface well in front of it. Zoom in and the ground reaches `rhw` 0.35 and
needs more than twenty pixels. Past that the interface is drawn over terrain
that stereo says is nearer, the two cues disagree, and it becomes unreadable.

So the shift tracks the world: never below `ui_shift_px`, never above
`ui_shift_max_px`, and in between whatever the nearest geometry needs plus a
tenth.

In the God view the land and the hands are nearer than the ceiling allows,
so the flat help text was behind the things that it covered, and the player
saw it double. Since 2026-10-03 the subtitles and the help text of the
headset do not draw in the eye pictures. They show on a card in the room:
`ui-layer.md`, section 17.

"Nearest" is the 98th percentile of per batch `rhw`, not the maximum. The
maximum read 6077, which is a vertex three thousandths of a unit from the
camera. Every frame has a few of those, and one would pin the interface at its
nearest limit forever.

### The interface split in half

`ui_shift_px` used to default to 0 because shifting the interface left a
checkbox with its border at one depth and its fill at another.

The probe found the missing path in one run. Only `DrawAndClip2D` was hooked.
**`DrawAndClip` draws flat interface as well**, 3000 batches in the same 150
frames, and none of it moved. `draw_hooks.cpp` now covers both, and refuses
any batch whose depth is not below 0.9, so a path that carries world geometry
in some other scene cannot be pulled out of the world.

Verified by measurement: with `ui_shift_px=8` every interface caller leaves
the `flat` column. The only batch still flat is `FinishFrame` at z 1.0, which
is the sky, and the sky belongs at infinity.

### How far away the interface sits

For eyes e apart, a screen plane at distance S and a wanted distance D, the
two images must differ on the screen by `e * (S/D - 1)`. At 1280 pixels across
2.60 m the screen is 2.03 mm per pixel, so with e = 64 mm and S = 2.0 m:

| `ui_shift_px` | Apparent distance |
| --- | --- |
| 4 | about 1.6 m |
| 8 | about 1.3 m |
| 12 | about 1.15 m |

`ui_shift_px` is half the total, because each eye moves that much in opposite
directions.

## Tried, measured, and rejected

### Putting the random seed back between the passes

`ParticleChainJoint::DrawAt` and `ParticleLightMap::DrawAt` each call
`GRand::LocalFloatRand` three times and add the results to a world position,
as a side effect of drawing. `GRand::LocalFloatRand` at `0x006DE590` reads its
seed from `[0xd0195c] + 0x205a3c` and writes it back. Drawing twice gives the
second eye different jitter, and freezing the animation clock does not help,
because the generator is not driven by time.

Putting the seed back before the second pass is the obvious answer and it is
**wrong**. With that change alone, the mesh transform at `fn_00856D40` went
from 150 wrong batches per 150 frames to **24760**, and `SetupThing::DrawBox`
reported batches with disparity, which a screen space menu cannot have. The
`nomatch` count says what really happened: the two passes stopped drawing the
same number of things. Something decides what to draw from that generator, so
replaying the sequence changes the scene instead of steadying it.

It stays in as a switch, `fix_random`, and it stays off.

### Moving the camera point with the eye

Kept from the first version, because it is still a useful lesson.
`LH3DTech::g_camera` at `0x00EA1DB8` is the camera position in world space,
read in 184 places. Moving it with each eye seems obviously right for
billboards and it is not. The Hand is placed relative to that point, so moving
it per eye moves the Hand with the eye, and anything that moves with the eye
has no disparity and reads as infinitely far away.

Real eyes move around a head that stays still. So the matrices move and
everything positioned relative to the camera stays where the mono camera put
it.

## The frame budget

```
frame 11.11 ms = wait 5.93 + scene x2 3.21 + grab x2 1.53
```

90.0 frames per second at 1280x720, nothing skipped, 6.4 ms spare.

The readback used to cost 3.76 ms and now costs 1.53 ms, of which the copy is
0.00. It was copying every frame twice: once into a buffer here, forcing the
alpha opaque a pixel at a time, and once into the swapchain staging texture.
The quad layer is submitted with `layerFlags` of 0, and without
`XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` the runtime ignores the
alpha, so none of that first pass was needed. The surface now stays locked
while the VR layer reads it and the frame is copied once.

What is left is the lock itself, about 0.8 ms, which is waiting for the GPU to
finish the frame rather than work we can remove.

## Still open

- **Particle jitter.** The generator fix above is rejected, so the jitter is
  still there. A fix that works has to leave the sequence alone and make the
  particles ask for the same numbers, which means hooking the two `DrawAt`
  functions rather than the generator.
- **`fn_00856D40` and `fn_0084C3C0`** still report a small number of rebuilt
  and relit batches, in the low hundreds out of twenty five thousand. Both
  project through the current transform pointer at `0x00EA9EA0`, which points
  at per object matrices, so the remaining work is in how those are built.
- **Landscape level of detail.** `fn_00877210` picks a tessellation stride of
  2 or 4 per island from the distance to a ring rebuilt each frame. An island
  sitting on the ring can still choose differently for the two eyes, and two
  triangulations of one hill cannot fuse.

## Live controls

Judging depth needs the person wearing the headset, and a rebuild per attempt
is far too slow a loop. So each of the settings below had a key while it was
being judged: the depth, the interface depth, stereo itself, the freeze on the
second pass, and the draw probe.

v1.0 removed those keys, because the values are settled. The ini keys do the
same work, and `draw_probe=1` with `draw_probe_delay` arms the probe at the
start. See `backend.md`, "Keys for v1.0", for the full list and for the three
keys that are left.

## Capturing the two eyes

`bwvr-eye-frames.txt` beside `runblack.exe`, one stereo frame number per line,
writes `bwvr-eye-left-NNNNN.bmp` and `bwvr-eye-right-NNNNN.bmp`. A difference
image shows at once whether something differs by a horizontal shift, which
fuses, or by shape, which does not.

## World scale

The eye separation is in the game's own world units and the right value was
found by eye. That is less of a problem than it looked. The camera zooms, so
the whole world grows and shrinks around the player, and the separation is a
scale relative number rather than an absolute one. Phase 3 needs a rule that
ties head movement to the current zoom, not a fixed number of meters per world
unit.
