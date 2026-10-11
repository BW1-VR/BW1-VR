# Stereo artifacts, and what causes them

Stereo works: the scene renders twice per frame, once for each eye, at 90 Hz.
This document lists the faults that stereo can show, what the mod does about
each one, and the faults that are still there.

## How the mod makes the two eyes

The two eye passes change `m[9]` of the world to camera matrix, and the mod
keeps the other members of the camera family in step (see "The camera is a
family of matrices"). `LH3DTech::ProjectPoint` computes

```
cam.x = p.x*m[0] + p.y*m[3] + p.z*m[6] + m[9]
cam.y = p.x*m[1] + p.y*m[4] + p.z*m[7] + m[10]
cam.z = p.x*m[2] + p.y*m[5] + p.z*m[8] + m[11]
```

so `m[9]` can only move `cam.x`. **For a point at a fixed world position the
projected y and the rhw are identical in both passes, and only x may
differ.** This is an exact test with nothing to tune. The draw probe uses it.

## The draw probe

`draw_probe.cpp` applies the test at the Direct3D device draw methods, which
every triangle passes through. It sorts each batch into one of five outcomes:

| Outcome | Meaning |
| --- | --- |
| `rebuilt` | y or rhw moved, so the object was at a different world position in the second pass |
| `retex` | the vertices held still and the texture coordinates moved, which is how a projected shadow goes wrong |
| `relit` | the vertices held still and the vertex colors moved, so lighting was computed from the camera |
| `flat` | x did not move either, so the batch has no disparity and is the screen space interface |
| `stereo` | x moved, y and rhw held, which is correct |
| `nomatch` | the two passes did not draw the same number of batches |

Each batch carries the address of the subsystem that asked for the draw, so
the report names the code. Resolve one with:

```
python bwvr/tools/disasm.py 0x????????
```

### Running it without a headset

`stereo_headless=1` runs both passes and sends nothing to a runtime. Stereo
correctness is a property of what the two passes submit, and that happens
inside the game, so a measuring run needs no headset. It takes about ninety
seconds.

```ini
[bwvr]
vr=0
stereo=1
stereo_headless=1
draw_probe=1
draw_probe_delay=8000
draw_probe_frames=150
```

| Key | Default | Meaning |
| --- | --- | --- |
| `stereo` | 1 | Render two eye passes |
| `stereo_headless` | 0 | Run both passes and send nothing to a runtime |
| `draw_probe` | 0 | Arm the draw probe |
| `draw_probe_delay` | 0 | Milliseconds to wait before the probe starts. The mod loads before the island does, so set a delay of about 8000 or the probe measures the loading screen |
| `draw_probe_frames` | 120 | How many frames the probe measures |
| `ui_accounting` | 0 | Log every function that submits a screen space batch, and whether the mod moved it. A new interface path shows as a line in the log |
| `eye_clean` | 1 | Each eye pass starts from its own frame before. See "The frame before of each eye" |

## Where every triangle goes

The whole frame reaches Direct3D through four call sites, all
`DrawIndexedPrimitive`:

| Call site | Function | What it carries |
| --- | --- | --- |
| `0x0082F919` | `LH3DRender::DrawTriangle` | the world |
| `0x0082A81B` | `DrawAndClip2D` | flat interface, z 0 to 0.33 |
| `0x0082A5A9` | `DrawAndClip` | flat interface, z 0, rhw 1 |
| `0x0082F617` | `LH3DRender::FinishFrame` | the sky, z 1.0 |

`FinishFrame` draws the sky at z 1.0 and the sky belongs at infinity, so it
stays flat. Every other flat batch is interface.

## What the mod does about each fault

### The camera is a family of matrices, not one matrix

`LH3DTech::UpdateCamera` builds several values from one camera position, and
different parts of the engine read different members. The mod must move all of
them for each eye.

| Address | What it is | Who reads it |
| --- | --- | --- |
| `0x00EA1D28` | world to camera, **unscaled** | `LH3DSprite::Draw` |
| `0x00EA9E40` | the same, scaled by the field of view terms | `LH3DTech::ProjectPoint` |
| `0x00EA1D58` | a copy of the scaled one | `GLandscape::Draw` |
| `0x00EA9DE0` | the inverse of the scaled one | 17 places, including shadows |

The scaling relation is exact, at `0x00819B40`:
`fld [0xea1d4c] / fmul st(1) / fstp [0xea9e64]`. An offset of d world units is
d on the unscaled matrix and `d * inv_half_tan_fov_x` on the scaled ones.
`eye_camera.cpp` does this.

### Smoke and particles

`LH3DSprite::Draw` at `0x00840530` reads the **unscaled** matrix and nothing
else. It loads `m[9]` from `0x00EA1D4C` at `0x008405B4`. If the mod does not
move that address, every sprite lands on the same screen position in both eyes.
Identical pixels are zero disparity, and zero disparity reads as infinitely
far away. The mod moves the unscaled matrix too.

### Villager shadows

A shadow is a projected texture. `fn_008801D0` builds each shadow's texture
matrix by copying the camera to world matrix at `0x00EA9DE0`. The ground
vertices that receive the shadow are transformed by the world to camera
matrix. The two cancel, and the shadow lands in the right place, **as long as
they agree**.

`UpdateCamera` is not called from `Process3dEngine`, so a change of
`0x00EA9E40` between the passes leaves `0x00EA9DE0` at the mono camera. The
mismatch leaves a translation along the eye offset, and the per vertex
perspective divide turns it into a skew. The shadow then slides across the
ground and changes shape, in opposite directions for the two eyes.

The shadow map, the light position and the bounding box are identical in both
passes. Only the projection must differ per eye. It is correct when the inverse
is the true inverse of the matrix in use, so `eye_camera.cpp` calls the game's
own `LHMatrix::SetInverse` at `0x007FB290` after each move. The probe outcome
`retex` finds this fault, because the vertices hold still and only the texture
coordinates move.

### Depth is not a classifier

Every draw path writes the same two fields, and
`LH3DTech::Draw3DScreenTriangle` at `0x0081C5DD` shows how:

```
rhw = near / camera_z        near is 3.5, at 0x00E839E0
z   = 1.0 - rhw
```

So `z < 0.9` means `rhw > 0.1`, which means `camera_z < 35` world units. It
does not mean "interface". It means "near the camera". Zoomed in, the water
and the ground have a depth like a menu. `GLandscape::Draw` submits the water
surface through `fn_00879930` at a constant depth of **exactly the near
plane**, the same z as a menu box. No depth test can tell those two apart.

So the mod decides by the **calling function**, which the camera cannot
confuse. The ranges are single contiguous blocks (`kUiCallers` in
`draw_hooks.cpp`):

| Range | What |
| --- | --- |
| `0x00411690` to `0x00413FF0` | `SetupThing`, the whole widget toolkit |
| `0x0081C5C0` to `0x0081C780` | `LH3DTech::Draw3DScreenTriangle`, text |
| `0x00447450` to `0x00447815` | `CameraHelp` quads, the screen edge hints |
| `0x0081E3C0` to `0x0081E58C` | `HelpSystem` quads, the subtitle bar |

`draw_hooks.cpp` hooks both `DrawAndClip2D` and `DrawAndClip`, because both
draw flat interface. It also refuses any batch whose depth is not below 0.9
(`kNearDepth`), so a path that carries world geometry in some other scene
cannot be pulled out of the world.

### The interface follows the world when the camera zooms

The disparity of a world vertex, in pixels per eye, is

```
scale * separation * half_res_x * rhw / (2 * near)
```

It is proportional to `rhw`, and `rhw` is how near the vertex is. A fixed
interface shift cannot work. With the camera zoomed out, the nearest ground has
`rhw` near 0.03 and eight pixels puts the interface well in front of it. With
the camera zoomed in, the ground reaches `rhw` 0.35 and needs more than twenty
pixels. If the shift is smaller, the interface is drawn over terrain that
stereo says is nearer, the two cues disagree, and the interface is
unreadable.

So the shift tracks the world. It is never below `ui_shift_px` (default 8),
never above `ui_shift_max_px` (default 24), and in between it is whatever the
nearest geometry needs plus a tenth. "Nearest" is the 98th percentile of per
batch `rhw`, not the maximum, because every frame has a few vertices
extremely close to the camera, and one of them would pin the interface at its
nearest limit.

`ui_shift_px` is half the total, because each eye moves that much in opposite
directions. For eyes e apart, a screen plane at distance S and a wanted
distance D, the two images must differ on the screen by `e * (S/D - 1)`. At
1280 pixels across 2.60 m the screen is 2.03 mm per pixel. With e = 64 mm and
S = 2.0 m:

| `ui_shift_px` | Apparent distance |
| --- | --- |
| 4 | about 1.6 m |
| 8 | about 1.3 m |
| 12 | about 1.15 m |

In the God view the land and the hands are nearer than the clamp allows, so
flat help text would be behind the things that it covers. The subtitles and
the help text of the headset do not draw in the eye pictures. They show on a
card in the room: see `ui-layer.md`, section 17.

### The frame before of each eye

The game draws each frame over the frame before. Its render target keeps
its picture from one frame to the next. After each present the game calls
one clean function, `fn_0082EE70`. `LHScreen::Clear` `0x007DE200` calls it,
from `LHScreen::LHFlip` (`0x007DE61E`) and from `LHScreen::BlitToMSWindow`
(`0x007DE6AD`). The cleaning mode `LH3DRender::g_mode_cleaning`
(`0x00C3871C`) says what that clean does. The clean function is the only
code that reads the mode.

| Mode | The clean after a present | Who writes the mode |
| --- | --- | --- |
| 1 | The picture and the Z buffer (`Clear` with the flags 3, the color at `0x00EC7F08`, Z 1.0) | `LH3DRender::OpenD3D` `0x0082CDD0`, the front end (`0x0053CC4C`, `0x0053CCE5`), `FrontEnd::JustDoABox` (`0x0054502A`, `0x005450E7`), `DrawLoading` `0x005F3F26`, `PlayLogoScreens` `0x00642AD4`, and `RunAlexFX` when it builds the logo (`0x005F8FA1`). `GLandscape::Draw` writes it when the wireframe flag `0x00ECA670` is set (`0x005E489C`) |
| 2 | The Z buffer only (the flags 2). The picture of the frame before stays | `RunAlexFX` in each frame of the Lionhead logo (`0x005F8FF1`), and no other code |
| 0 | Nothing | `GLandscape::Draw` in each frame of the land (`0x005E48A4`). The scene walk for the Temple (`0x0054DE1F`) and for the video of a falling spell (`0x0054DDB5`). `DoCitadelDraw` `0x0055542A`, `StartTipOfTheDayText` `0x005F4E23` and `RenderLoadingFrame` `0x005F516C` |

So all of play has mode 0, and no effect of play has mode 2: no miracle,
no storm, no fade and no room of the Temple. In mode 0 the sky dome is the
clean of a frame. `GLandAlignement::DrawSky` `0x005E2160` draws it first
(from `GLandscape::Draw` at `0x005E48AE`, and from the scene walk for the
Temple at `0x0054DE34`). `LH3DSky::Draw` `0x0086B010` sets `ZFUNC` to
`ALWAYS` and draws the first layer of the dome with an alpha of 255 and no
blend (`fn_0086B6B0`).

Mode 0 has one clean of its own. While the callbacks of the frame end are
on, a timer in the clean function makes the clean of mode 1 for two frames
in each 2000 ms. The function keeps a stamp of the time at `0x00ECA67C` and
a flag for the second frame at `0x00ECA680`.

One scene of play draws no sky. `PSysGlobal::StartAVISequence` starts the
video of a falling spell (`GGame::KickOffFallingSpellVideo` `0x005539A0`,
the scene kind 2 at `GGame + 0x205A28`). The scene walk then draws the video
with an alpha of 80 of 255 (`0x0054DC11` to `0x0054DC6D`) and no sky, in
mode 0. So the frame before stays in view.

**In two eye passes.** The two eye passes share that render target, and the
game cleans it one time for a frame. Eye pass 0 then draws over the picture
of eye pass 1 of the frame before. Eye
pass 1 draws over the picture of eye pass 0 of the same frame. A pixel that
a pass does not cover with a draw of no blend keeps the picture of the
other eye. For the left eye that picture is one frame old. For the right
eye it is of the same moment.

**What the mod does (`eye_clean=1`, `eye_clean.cpp`).** Each eye pass of the
scene starts from its own frame before. `eye_clean_math.h` has the plan of a
pass (`Plan`), and the math test checks it with one pixel through some
frames.

- A hook on the clean function records what each call did. Eye pass 1 gets
  that same clean from the mod, with the clean call and the color of the
  game.
- In mode 0 the mod cleans the picture and the Z buffer of each eye pass
  just before its sky. A pixel that the scene does not cover then has the
  color of the clean in each eye. The two timed frames of the game show
  the same color there.
- When the frame before stays in view, each eye has a keep of its picture:
  the surfaces that the Lionhead logo uses (`gpu_bridge.cpp`, `intro.md`,
  "The trails"). The keep of an eye goes into the render target before its
  pass, and the picture goes into the keep after it. This is the rule for
  mode 2, and for mode 0 in a frame that draws no sky.
- The mod knows that a frame draws no sky from the frame before it. So the
  first frame of such an effect is drawn as with `eye_clean=0`. Before the
  second frame the keep of eye 1 takes the picture that is in the render
  target, so both eyes start from the same picture.
- The keeps are free again when the passes draw the sky. They are also
  free when the game draws a frame outside the eye passes: a menu, a load,
  or a frame that the headset did not ask for.

The Lionhead logo draws its own pair of frames, each with its own present.
Its keeps are in `intro_vr.cpp` (`intro_eye_trails`).

| Key | Default | Meaning |
| --- | --- | --- |
| `eye_clean` | 1 | 1: each eye pass of the scene starts from its own frame before. 0: the two eye passes share the render target, and the game cleans it one time for a frame |
| `eye_clean_test` | 0 | A test key of the desktop runs. 1: the way of the Lionhead logo in play. After each frame of the scene the game cleans only the Z buffer (mode 2), and the first layer of the sky dome has an alpha of 160 of 255 (the byte at `0x0086B5BB`). 2: the clean before the sky has the color FF00FF, so a picture of an eye shows each pixel that the scene does not cover |
| `eye_clean_test_frame` | 0 | A test key of the desktop runs: the frame at which `eye_clean` changes, as the test key does |

With `test_keys=1`, Shift and Page Down change `eye_clean` in play. The
pointing hand gets a long pulse for 1 and a short pulse for 0.

The log has the tag `eye clean`. It has one line for each change of the
cleaning mode. It has one line when an eye pass draws no sky. That line
names the scene kind of the walk, and it says if a video plays. Each report
has the counts of the cleans of the game, of the cleans of the mod and of
the copies of the keeps.

**Tests.** The math test (`eye_clean_checks.h`) has the clean function of
the game as its listing has it. It compares 10 s of mode 0 with the answer
of the mod for each frame. It also follows one pixel through some frames,
with each value of `eye_clean`:

- A pixel that a thing covers for the left eye only, in the way of the
  logo (mode 2, an alpha of 160). With `eye_clean=0` the right eye sees 27
  percent of that thing. With 1 it sees nothing of it.
- A pixel that no pass draws, in play. With 0 it keeps the old picture.
  With 1 it has the color of the clean in each eye.
- A scene with no sky (an alpha of 80). From its second frame each eye has
  only its own picture, and no keep stays when the sky comes back.

No desktop run has shown the change in the game. Two runs of
`bwvr/tools/desk/hand_run.ps1` are made for it, with `-Kind fixed -Save
temple` and the build in `-Dll`:

- **The way of the logo in play.** `-Points "12,28,45"`, `-Set
  "eye_clean_test=1;eye_clean=1;eye_clean_test_frame=5020"`, and `-Steps
  "point@4610+1 shot@4700+1 stick_right@4720+8 stick_right@4750+8
  shot@4850+1 stick_right@4870+8 stick_right@4900+8 shot@5000+1 shot@5120+1
  stick_left@5140+8 stick_left@5170+8 shot@5270+1 stick_left@5290+8
  stick_left@5320+8 shot@5420+1"`. The hand is 45 cm from the head, before
  the sky, with a still head at three headings. The first three pairs of
  pictures have `eye_clean=1`, and the last three have 0. The measure is
  the strength of a second picture of the hand in the left picture, at the
  place that the hand has in the right picture (about 148 pixels to the
  left at a render of 2880 pixels). It must be 0 with 1, and about 37
  percent with 0.
- **The pixels that the scene does not cover.** `-Set "eye_clean_test=2"`,
  with `fake_head_pitch` at 0, -70 and 60, and with `fake_head_roll=30`.
  The count of the pixels of the color FF00FF in each eye picture is the
  count of the pixels that kept an old picture before.

### The frame budget and the readback

The frame at 90 Hz is 11.11 ms. The scene takes about 3.2 ms for both eyes and
the readback of the picture about 1.5 ms, at 1280x720. The quad layer is
submitted with `layerFlags` of 0, and without
`XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` the runtime ignores the
alpha, so the mod does not force the alpha opaque. The surface stays locked
while the VR layer reads it and the frame is copied once. What is left is the
lock itself (about 0.8 ms), which is the wait for the GPU to finish the frame.

## Tried and rejected

### Putting the random seed back between the passes

`ParticleChainJoint::DrawAt` and `ParticleLightMap::DrawAt` each call
`GRand::LocalFloatRand` three times and add the results to a world position,
as a side effect of drawing. `GRand::LocalFloatRand` at `0x006DE590` reads its
seed from `[0xd0195c] + 0x205a3c` and writes it back. Drawing twice gives the
second eye different jitter. Freezing the animation clock does not help,
because the generator is not driven by time.

Putting the seed back before the second pass is **wrong**. Something decides
what to draw from that generator, so replaying the sequence changes the scene:
the two passes stop drawing the same number of things (the `nomatch` count
rises) and the rebuilt batches rise from about 150 to about 25000 per 150
frames. The switch `fix_random` exists and its default is 0. Keep it off.

### Moving the camera point with the eye

`LH3DTech::g_camera` at `0x00EA1DB8` is the camera position in world space,
read in 184 places. Moving it with each eye seems right for billboards, but
the Hand is placed relative to that point. If the point moves per eye, the Hand
moves with the eye, and anything that moves with the eye has no disparity and
reads as infinitely far away. Real eyes move around a head that stays still:
the matrices move, and everything positioned relative to the camera stays where
the mono camera put it. The key `move_camera_point` (default 0) exists and
stays off.

## Known limits

- **Particle jitter.** The generator fix is rejected, so the jitter is still
  there. A fix that works must leave the sequence alone and make the particles
  ask for the same numbers. That means a hook on the two `DrawAt` functions
  rather than on the generator.
- **`fn_00856D40` and `fn_0084C3C0`** still report a small number of rebuilt
  and relit batches, in the low hundreds out of about twenty five thousand.
  Both project through the current transform pointer at `0x00EA9EA0`, which
  points at per object matrices, so the remaining work is in how those are
  built.
- **Landscape level of detail.** `fn_00877210` picks a tessellation stride of
  2 or 4 per island from the distance to a ring rebuilt each frame. An island
  on the ring can choose differently for the two eyes, and two triangulations
  of one hill cannot fuse. The tabletop mode removes the choice (see
  `tabletop.md`).

## Capturing the two eyes

A file `bwvr-eye-frames.txt` beside `runblack.exe`, with one stereo frame
number on each line, makes the mod write `bwvr-eye-left-NNNNN.bmp` and
`bwvr-eye-right-NNNNN.bmp` (`eye_capture.cpp`). A difference image shows at
once whether something differs by a horizontal shift, which fuses, or by
shape, which does not.

## World scale

The eye separation is in the game's own world units (key
`eye_separation_milli`). The camera zooms, so the whole world grows and
shrinks around the player, and the separation is a scale relative number
rather than an absolute one. The head pose (`head_pose.cpp`) takes the number
of game units for one meter from the ratio of the eye separation of the
player in meters to `eye_separation` (key `head_units_per_meter_x10`, default
75, which means 7.5 units). It is open how head movement must follow the zoom
in every camera mode: a rule that ties head movement to the current zoom,
not a fixed number of meters per world unit.
