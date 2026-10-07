# The interface on its own layer (Track 3)

Research and design, 2026-09-29. U1 and U2 built on 2026-09-30, see
section 12. Sections 1 to 10 are the design as it was written. Where the
build differs, section 12 says so.

The goal: the menus, the subtitles, the help text and the Creature panel on a
flat panel that stays at a fixed place in the room. OpenXR draws the panel as
its own layer, over the world. The hint beside the Hand, the tooltips and
anything else attached to the world stay in the eye pictures.

Marks, as in `tabletop.md`:

- [V] verified: read in the binary with `tools/disasm.py`, or in the source.
- [M] measured in a run. The number comes from the named doc.
- [I] inferred. It needs a test before anything depends on it.

## Summary

- The game draws the screen interface in `LH3DRender::FinishFrame`, in the
  callbacks that run after the world, the screen quad and the cinema bars.
  FinishFrame runs inside the scene walk, so this happens once per eye pass.
  The tooltip list draws earlier, inside the walk.
- The mod can move the screen interface to a render target of its own. It
  switches the Direct3D 9 target around those draw calls in eye pass 0 and
  skips them in eye pass 1. dxwrapper's Dd7to9 does not see the switch, so
  the mod must put the target, the depth surface and its own states back.
- The target reaches OpenXR the way the eyes do: a shared texture, one copy,
  and a third swapchain. It goes out as `XrCompositionLayerQuad` layers in
  the LOCAL space, after the projection layer, with the source alpha flag.
- One target feeds two quads: a menu panel and a help panel. Screens with no
  scene get a third, opaque quad for the picture behind the menus.
- Input keeps one pointer. The mod picks the surface of that pointer each
  frame: the world, or the menu panel while a dialog is up or no scene shows.
- The first step is U1: draw the panel target and save it to a BMP on the
  desktop, with nothing changed in the headset.

## 1. Where the interface is drawn today

### 1.1 FinishFrame runs once per eye pass

`GGame::Process3dEngine` `0x0054DA80` walks the scene and ends in
`LH3DRender::FinishFrame` `0x0082F460` (call at `0x0054E41C`) [V]. The mod runs
`Process3dEngine` twice a frame, once per eye (`stereo.cpp`). So FinishFrame,
and all interface drawn inside it, runs once per eye pass [V].

The order inside FinishFrame [V, disassembly of `0x0082F460`]:

| Address | What |
| --- | --- |
| `0x0082F49C` to `0x0082F4C7` | The callbacks without the late flag `0x80000000` |
| `0x0082F4C9` | The sun glare, `fn_0086BB60` |
| `0x0082F5E4` to `0x0082F614` | `ZFUNC` set to `ALWAYS`, then a screen quad (`DrawIndexedPrimitive`, vtable `+0x68`). It sets the depth to 1.0 (`tabletop.md` 4.4). |
| `0x0082F686` to `0x0082F6DD` | The cinema bars, only when `0x00EB9950` is not zero |
| `0x0082F6ED` to `0x0082F718` | The callbacks with the late flag. The screen interface draws here. |
| `0x0082F7D3` | `EndScene` (vtable `+0x18`) |

Both callback loops only run when `g_enable_callbacks` `0x00C386D0` is set
[V].

### 1.2 The FinishFrame callbacks

`LH3DRender::RegisterFinishFrameCallback` `0x0082F2C0` takes a priority, a
late flag, a `__stdcall` callback and its argument. It ORs `0x80000000` into
the priority when the flag is set, and keeps the list sorted by that value
[V]. So the late loop runs the late callbacks in order of priority.

| Priority | Late | Callback | Registered by | What it draws |
| --- | --- | --- | --- | --- |
| 100 | no | `0x005C2E30` | `HelpDudeControl::Init` | the advisors [I] |
| 1000 | [I] | `0x004562E0` | `CameraModeNew3::Initialise` | camera work [I] |
| 100 | yes | `0x005C2E10` | `HelpDudeControl::Init` | the advisors, calls `fn_005C3920` [I] |
| 10000 | yes | `0x0081E7D0` | `fn_0081E740` | the rect queue: the subtitle box. It is `call 0x0081E3C0`, then `ret 4` [V]. |
| 20000 | yes | `0x005CD020` | `HelpText` constructor | `fn_005CCAB0`, then `fn_005178D0` (the Creature panel) [V]. Between them: the subtitles, the help text, the hint beside the Hand and the second tooltip copy (`draw_hooks.cpp`). |
| 32768 | yes | `LHVideoPlayer::thedraw` `0x00844E30` | `GGame` constructor | video frames [I] |
| 655360 | yes [I] | `0x006424E0` | `start_system` | every `SetupBox` dialog: the Esc menu, options, load and save |

The late flag of the Setup callback comes from `edi`, which is 1 at
`0x0064326B` [V]. It is not checked that `edi` is still 1 at `0x006433D7`
[I]. `FallingSpell::Init` also registers a callback (`0x00526480`) with a flag
from `ebp` [I].

A consequence of the order: the rect queue callback runs before HelpText in
the same pass. So the subtitle box drawn in a pass is the one that HelpText
queued in the pass before [I]. This does not change with the panel.

### 1.3 Each part of the interface

| Part | Drawn by | In the frame | What the mod does today | Goes to |
| --- | --- | --- | --- | --- |
| Setup dialogs | the Setup callback `0x006424E0` | late callbacks | the interface shift only (`h_SetupFinish`) | menu panel |
| Subtitle letters | `fn_005CCAB0`, through `Draw3DScreenTriangle` | late callbacks | size, placement at 35 degrees, shift | help panel |
| Subtitle box | the rect queue, caller `0x0081E3C0` to `0x0081E58C` | late callbacks | size, placement, shift | help panel |
| Help text | `fn_005CCAB0` | late callbacks | size, shift | help panel |
| Creature panel | `fn_005178D0` | late callbacks | size, shift | help panel, if it is fixed to the screen [I] |
| Hint beside the Hand | CameraHelp quads `0x00447450`, inside the HelpText callback | late callbacks | one copy kept at the Hand, flat, hint size | the eyes |
| Tooltips | `HelpSystem::Draw3D` `0x005C59A0` (called only from `Process3dEngine` at `0x0054E2ED` [V]) and again from HelpText | in the walk, and late | first copy kept, flat at the Hand | the eyes |
| Screen quad, cinema bars | FinishFrame | before the late callbacks | bars hidden | the eyes |
| Advisors | `HelpDude` code | in the walk and callbacks | room camera (`spirit_anchor.cpp`) | the eyes |
| Video in play | `LHVideoPlayer::thedraw`, also from `FallingSpell::Draw` | late callbacks | nothing | the eyes, until checked |

The sources for the "today" column are `draw_hooks.cpp` and `backend.md`
("Subtitles", "Menus keep their size", "Tooltips drawn twice in each eye").

`Creature::Draw` arms the Creature panel only while the Creature is
`GInterface::GetInteractObject` [V]. The box itself is fixed to the screen.
Section 13 cuts it out as a quad of its own.

### 1.4 Screens with no scene, today

FinishFrame has 23 call sites, and only one is in `Process3dEngine` [V,
`disasm.py --callers 0x0082F460`]. The others are in `FrontEnd::JustDoABox`,
`LoadSaveBox` (from `GameLoadingBox` and `GameSavingBox`), `DrawLoading`,
`RenderLoadingFrame`, `DoCitadelDraw`, `DoLogo`, `MakeTipVideo`,
`PlayLogoScreens`, `PlayPreIntroVideo`, `RunAlexFX`, two text screens in
`GGame::Loop`, `fn_00640054`, and unnamed code at `0x0053CA71`, `0x0053CE0F`,
`0x00822779` and `0x00822997`. `JustDoABox` draws the plasma background and
then runs FinishFrame with the callbacks on, so the Setup callback draws its
dialog there too [V].

These frames do not reach `Process3dEngine`. After 150 ms with no stereo
frame, `StereoIdle` is true, and `h_FinishFrame` in `mod_main.cpp` sends the
whole render target through `SubmitTexture`, the same picture to both eyes
[V]. What the headset then shows depends on the session [V, `xr_screen.cpp`
`EndFrame`]:

- **Before the first stereo frame:** two quads, one per eye, 260 cm wide at
  200 cm (`screen_width_cm`, `screen_distance_cm`), in the LOCAL space. This
  is the Phase 1 screen, and it is fixed in the room.
- **After the first stereo frame:** `stereo.cpp` sets the projection flag
  once and never clears it, and `have_render_pose_` stays true. So a screen
  with no scene goes out as a projection layer. It declares the eye poses of
  the last stereo frame and the render frustum of about 110 degrees. The
  runtime then shows the flat picture over the whole view, with no depth,
  fixed in the room where the head pointed at that last frame [I, from the
  code, not seen in a headset]. A player who turned since then sees the
  picture to one side and black past its edge.

In both cases the menus are small. Their text is a fixed 20 to 35 px at any
render size [M, `backend.md`], which is about 0.8 to 1.3 degrees at 26 px per
degree.

## 2. Drawing the interface to its own target

### 2.1 Three ways, and the choice

1. **Switch the Direct3D 9 render target** around the panel batches. The mod
   already holds the Direct3D 9Ex device (`gpu_bridge.cpp`), makes shared
   render target textures on it, and reads its render target. This does not
   depend on dxwrapper internals, only on the three caches in 2.2.
2. **Give the game a Direct3D 7 surface** and call
   `IDirect3DDevice7::SetRenderTarget` with it. Dd7to9 then knows about the
   switch. But the mod must make the surface through the game's DirectDraw
   object, attach a depth surface, and then find the Direct3D 9 surface behind
   it through dxwrapper's private interface (`IID_GetInterfaceX`). That ties
   the mod to one dxwrapper version, and the surface follows dxwrapper's
   shadow and lock rules.
3. **Draw each panel batch twice**, once into the eye and once into the
   panel. This is only useful as a test mode (U1), because the goal is to
   take the interface out of the eyes.

The choice is way 1, with the rules in 2.3.

### 2.2 What Dd7to9 caches

Read in the dxwrapper 1.8.8600.25 source, folder `ddraw` [V]:

| What Dd7to9 keeps | Where | What a switch behind its back does |
| --- | --- | --- |
| The render target, as its own surface objects (`RenderTargetSurface`, `lpCurrentRenderTargetX`) | `SetDrawStates` compares the two before every draw (`IDirect3DDeviceX.cpp`, about line 6713). `SetRenderTargetSurface` (`IDirectDrawX.cpp`, line 4286). | It compares its own pointers, not the device. So it does not see the switch and does not undo it. It also does not put the game's target back. The mod must do that. |
| The viewport | `SetDrawStates` calls `SetViewport` before every draw ("Need to always set viewport"). `Clear` does too (line 5713). | Direct3D 9 `SetRenderTarget` resets the viewport to the new target. Dd7to9 sets its own again at the next draw. A target smaller than that viewport makes the call invalid, so the panel target must be the render size. |
| Render, texture stage and sampler states | `SetD9RenderState` (line 5900) writes a shadow copy and a list of changes. `PrepDevice` (line 6360) sends only that list, at the next draw. | A state that the mod changes and does not put back stays wrong until the game sets it again. |
| The depth surface | set only together with the target (`SetRenderTargetSurface`, line 4329) | Direct3D 9 `SetRenderTarget` does not change it. The game's depth stays bound unless the mod binds its own. |
| Draws | `DrawIndexedPrimitive` calls `DrawIndexedPrimitiveUP` at once (from line 3035). Nothing is kept for later. | A switch around one engine draw call moves exactly that draw. |
| The present | `Flip` calls `SetCurrentRenderTarget` (`IDirectDrawSurfaceX.cpp`, line 1456). The present draw saves and restores the device target (`IDirectDrawX.cpp`, lines 5322 to 5343). | Both run outside FinishFrame, so they do not meet the switch. |

No file in the Dd7to9 source uses `D3DRS_SEPARATEALPHABLENDENABLE`,
`D3DRS_SRCBLENDALPHA` or `D3DRS_DESTBLENDALPHA` [V, a search of
`IDirect3DDeviceX.cpp` and `IDirectDrawX.cpp`]. The mod can set these three
without a conflict, if it puts them back.

Multisampling: the shipped `dxwrapper.ini` has `DdrawAllowMultiSampling = 1`
[V]. That sets `Device.AntiAliasing` (`IDirectDrawX.cpp`, line 2599), and the
device then asks for multisampling when the card has it (line 3541) [V].
Whether the game's render target really is multisampled was not checked [I].
The panel target has its own depth surface (2.3), so this does not matter for
the panel.

### 2.3 Rules for the switch

1. Make the panel target the same size as the game's render target. Read the
   size with `GetDesc` on the current target when the bridge starts. Use
   `A8R8G8B8` with no multisampling.
2. Give the panel target its own depth surface of the same size, with no
   multisampling. The late callbacks draw after the screen quad at
   `0x0082F614` has set the game's depth to 1.0. So a panel depth cleared to
   1.0 gives the same depth test [I: the interface does not read the depth
   of the world].
3. Bind the pair when the first panel batch of a span arrives. Save target 0
   and the depth surface with `GetRenderTarget` and `GetDepthStencilSurface`,
   bind the panel pair, and set the three alpha states of 2.5.
4. At the first panel bind of a frame, clear the panel target to 0 with
   `ColorFill` and the panel depth to 1.0 with `Clear`.
5. Put the saved pair and the three states back at the next eye batch, at the
   end of each callback scope, and before FinishFrame returns.
6. Before each panel draw, check that target 0 is still the panel target. If
   it is not, bind it again and count it in the log. This guards against a
   Dd7to9 path that binds the game's target in the middle of a span.
7. Draw panel batches in eye pass 0 only. In eye pass 1, do not call the
   engine draw for a panel batch. `DrawAndClip` and `DrawAndClip2D` clip and
   submit, so a skipped call only loses pixels [I].
8. Fail safe. If the GPU bridge is not ready, the projection layer is off, or
   any create fails, the interface stays in the eyes as it does today. With
   `play_mode` 1 (the stereoscopic window) the world is already a screen, so
   the interface stays in it.

The spans are short. The HelpText scope mixes panel batches and the hint, so
a frame can have a few binds. The log counts them.

### 2.4 Which batches go to the panel

The rule, in the order `DrawInterface` in `draw_hooks.cpp` already works:

```
panel batch, when ui_layer is on and the rules of 2.3 allow it:
  inside the Setup callback 0x006424E0            (g_menu_depth > 0)
  inside the HelpText callback 0x005CD020 only    (a new counter, because
                                                   g_help_depth also counts
                                                   HelpSystem::Draw3D)
    and not a tooltip                             (g_in_tooltip)
    and not a kept hint copy                      (g_widget_hint, g_widget_scale,
                                                   g_widget_flat after
                                                   IsDuplicateWidget)
  the rect queue caller 0x0081E3C0 to 0x0081E58C  (the subtitle box)

eye batch: everything else, exactly as today

eye pass 0, or a FinishFrame outside the eye passes: draw a panel batch
  into the panel target
eye pass 1: do not draw a panel batch
```

A panel batch gets none of today's steps: no interface shift, no interface
size, no subtitle lift, no subtitle placement. The panel has its own depth
and its own size in the room. So the menus also sit exactly on their hit
test again. Today the shift moves them 8 px from it in each eye.

The routing needs one new hook, on the rect queue callback `0x0081E7D0`
(`__stdcall`, one argument, `ret 4` [V]), to close the span there. The
HelpText and Setup callbacks are hooked already.

### 2.5 A clear background and the alpha

The panel target starts each frame at 0 in all four channels. The interface
then draws over it. With the game's usual blend (source alpha, inverse source
alpha) the color comes out premultiplied, which is what OpenXR wants. The
alpha would come out wrong: the same factors give `a * a` for a first layer.
The separate alpha blend fixes that:

```
D3DRS_SEPARATEALPHABLENDENABLE  TRUE
D3DRS_SRCBLENDALPHA             D3DBLEND_ONE
D3DRS_DESTBLENDALPHA            D3DBLEND_INVSRCALPHA
```

For each blend mode of the game:

| Game blend | Color on the panel | Alpha on the panel |
| --- | --- | --- |
| source alpha, inverse source alpha | premultiplied, exact | "over", exact |
| blend off | the source color | the source alpha. Opaque only when that alpha is 1 [I]. |
| one, one (additive) | added | coverage grows, so a glow shows as a faint shape [I] |

The first row is the common one [I]. U1 counts the blend modes of every panel
batch. A read-only hook on the game's `IDirect3DDevice7::SetRenderState`
(vtable `+0x50`) gives the mode of each batch, because the device state is
only sent at the draw. `texture_filter.cpp` already hooks the same device's
`SetTexture`.

A composite check proves the math on the desktop. On a capture frame, copy
the eye target to a spare surface just before the first panel batch. Then
draw the panel batches both ways. The spare surface with the panel
composited over it must match the finished eye picture, except in the rows
of the hint and the tooltips, and except for rounding of 1 or 2 levels.

## 3. From the target to OpenXR

### 3.1 The share

`gpu_bridge.cpp` makes one more shared texture on the same device:
`A8R8G8B8`, `D3DUSAGE_RENDERTARGET`, with a shared handle, opened on the
Direct3D 11 device of the session. This is the eye path again [V, `GpuBridgeStart`].
The panel target, its depth surface and the shared texture live and die with
the device, through the `Release` hook the bridge already has.

After eye pass 0 returns, `stereo.cpp` calls a new `GpuBridgeCaptureUi` before
`GpuBridgeCaptureEye(0)`. It does one `StretchRect` from the panel target to
the shared texture and no flush. The eye 0 capture then issues its event
query flush, and that flush drains every command before it, the panel copy
too [V, `backend.md`, "Keeping the two devices in step"]. So the panel adds no
wait.

A frame with no panel batch makes no copy and submits no panel layer.

### 3.2 The quad layers

`XrScreen` gets a third swapchain, the panel swapchain: the same format as the
eyes, the render size, usage `TRANSFER_DST` and `SAMPLED`. A new
`SetPanelTexture` acquires, waits, copies with `CopyResource` and releases,
as `SetEyeTexture` does [V for the pattern]. Then `EndFrame` submits:

```
layers[0] = the projection layer, as today
layers[1] = the menu panel quad, when it has content
layers[2] = the help panel quad, when it has content

each quad:
  type          XR_TYPE_COMPOSITION_LAYER_QUAD
  layerFlags    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT
  space         the LOCAL space the projection layer uses
  eyeVisibility XR_EYE_VISIBILITY_BOTH
  subImage      the panel swapchain, the rect of that panel (4.1), layer 0
  pose, size    from 4.3 or 4.4
```

OpenXR draws later layers over earlier ones. Without
`XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT` the runtime reads the color
as premultiplied, which matches 2.5. Both quads read one swapchain image
through two rects. If a runtime refuses one image in two layers, use a second
swapchain and `CopySubresourceRegion` [I].

The size of a quad in meters, for a rect `w` px wide at `k` px per degree and
a distance `d`:

```
width  = 2 * d * tan(w / k / 2 degrees)
height = width * h / w
```

### 3.3 Color and alpha in the swapchain

The eye swapchains prefer `B8G8R8A8_UNORM_SRGB`, because the game's colors are
already gamma encoded [V, `xr_screen.cpp`]. The panel uses the same format.
The game blends in gamma space, so the panel color is premultiplied in gamma
space, and the compositor then blends in linear space. The result is exact for
opaque and fully clear pixels. On the soft edges of letters it is a little
darker than on a monitor [I]. If that shows, a pixel shader pass on the
Direct3D 11 side can convert the panel to linear premultiplied color.

## 4. Placement

### 4.1 One panel or two

Two quads from one target:

- **The menu panel** shows the Setup dialogs.
- **The help panel** shows the subtitles, the help text and the Creature
  panel. Nothing on it takes a click.

The two kinds want a different size and a different place. The menus are
small text that a player reads and clicks, straight ahead. The subtitles are
large text that a player reads in a glance, low down. The subtitle bar sits at
the bottom of the render [V, `backend.md`, "Subtitles"]. The Setup dialogs
sit near the middle [I]. So a rect for each kind can cut them apart in one
target.

U1 logs the bounds of each kind in render pixels. If the two kinds overlap in
the target, use two targets instead. The rects are fixed values, not fitted
each frame, so a panel never changes size while the player looks at it.

### 4.2 Pixels per degree

Today the render is about 2880 px across about 110 degrees, which is 26 px per
degree [M, `backend.md`]. The interface can only be the angle its pixels give
it. The panel breaks that tie: the quad size sets the angle.

- **Menu panel, 16 px per degree.** Menu text of 20 to 35 px becomes 1.25 to
  2.2 degrees, larger than today. The texture is magnified 1.6 times, so the
  letters are a little soft. The other way is to draw the menus larger in the
  target and give the pointer the inverse scale (5.4). That is sharper,
  because the glyph cache holds 40 row letters. Decide by an A/B in the
  headset.
- **Help panel, 44 px per degree.** On the panel the help layer draws at the
  game's own size, 77 px letters at a 2880 px render [M]. That is 1.75
  degrees. The letters then come from the 40 row glyph copy without the
  minification that `ui_scale_pct` causes today. The compositor then reduces
  the quad about 1.8 times with no mip levels, which can crawl [I]. The
  other way is to keep `ui_scale_pct` in the target and show the quad at
  about the display density. Decide by an A/B in the headset.

### 4.3 Normal mode

The anchor is the recenter pose: the head position at recenter and its yaw.
`head_pose.cpp` already keeps this pose, and it keeps only the yaw [V]. Each
quad faces the head position of the anchor.

| Quad | Center, from the anchor | Content |
| --- | --- | --- |
| Menu panel | 140 cm forward, 10 degrees below eye level | Setup dialogs |
| Help panel | 120 cm forward, 30 degrees below eye level | subtitles, help text, Creature panel |
| Screen | 200 cm forward, at eye level, 260 cm wide | the picture of a screen with no scene (section 6) |

The advisors float about 43 degrees to each side, 1 to 1.5 m away
(`tabletop.md` 5.1), so they do not meet the panels.

A quad layer draws over the projection layer everywhere. A panel behind a
nearer object still covers it, and the two depth cues then disagree. In
normal play the land is tens of meters away, so a panel at 1.2 to 1.4 m is
nearer than the world behind it in nearly all views [I].

### 4.4 Tabletop mode

The panels hang on the table anchor of `tabletop.md` 2.2: the table center
`Tc` in the room, 80 cm forward and 40 cm down from the recenter pose. A 1.5 m
table then has its far edge about 155 cm forward.

| Quad | Center | Content |
| --- | --- | --- |
| Menu panel | above the far edge of the table, about 170 cm forward, at eye level | Setup dialogs |
| Help panel | at the far edge, 15 cm over the table surface, tilted back to face the head | subtitles, help text, Creature panel |
| Screen | as in normal mode | screens with no scene |

The island sits below the line of sight to both panels, so the panels cover
little of it. With the Virtual Desktop passthrough mask on, the panels are not
in the key color, so they float in the real room [I]. When T5 moves the table
to a real desk, the panels move with the table anchor.

### 4.5 Recenter

The panels never follow the head. They move only when the anchor moves. F11
and a recenter from the headset menu (`XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING`)
both clear the origin in `head_pose.cpp`, and the next frame takes a new one
[V]. The panels land in front of the player on that frame, once. A new
export, `HeadPoseOrigin`, gives the origin position and yaw in the LOCAL
space.

## 5. Input on the panel

### 5.1 One pointer, two surfaces

`pointer_map.cpp` keeps one pointer today [V]. One owner, the mouse or a
controller, sets one float render pixel (`g_true_x`, `g_true_y`). The mod
writes it once a frame to `0x00E852C0` and `0x00E852C4` and to the Windows
cursor. `h_MouseMoveHandler` and `h_MouseSetPosition` convert the window
messages the same way, so the Hand and the interface agree.

The panel keeps this rule. It adds one choice per frame: the surface of that
pixel.

- On the **world**, the pixel is a pixel of the eye view, as today.
- On the **menu panel**, the pixel is a pixel of the panel target.

The panel target is the render size. So a panel pixel is the exact pixel the
game drew the dialog at, and the exact pixel its hit test expects [I: the
dialogs test the pixels they draw, which the menu scope in `draw_hooks.cpp`
already relies on]. The game still gets one pointer.

### 5.2 When the pointer is on the menu panel

The pointer is on the menu panel while a Setup dialog is up, or while a
screen with no scene shows. `SetupBox::GetCurrentActiveBox` `0x00407ED0`
returns `[0x00C4CD20]`, and a value other than 0 means a dialog is up [V].

The game's state decides the surface, not the ray. A ray that leaves the
panel does not drop the pointer onto the island. The pointer keeps its last
panel pixel. This answers the open problem in `tabletop.md` 5.2: a menu click
cannot reach the island.

A press keeps its surface until its release. A drag that starts on the panel
ends there. A land drag that is held when a dialog opens ends in the world.

The help panel never takes the pointer. The mod's notes call the menus "the
only interface anyone clicks" [I].

### 5.3 A ray against a quad

The controller's aim pose is in the LOCAL space, the same space as the quads
[V, `xr_screen.h` `HandInput`]. For a quad with pose `p`, `q`, size `w` by `h`
in meters, and a rect `x0`, `y0`, `cw`, `ch` in target pixels:

```
n  = q * (0, 0, 1)             the side that faces the player
s  = dot(d, n)                 below 0: the ray meets the front side
t  = dot(p - o, n) / s         above 0: the quad is in front of the hand
k  = conj(q) * (o + t*d - p)   the hit, in the quad's own frame
u  = k.x / w + 0.5             0 at the left edge, 1 at the right edge
v  = 0.5 - k.y / h             0 at the top, 1 at the bottom
pixel = (x0 + u * cw, y0 + v * ch)
```

It is a hit when `u` and `v` are both in 0 to 1. Put the function in
`controller_math.h` and add tests to `test/controller_math`: a ray at the
center, a ray at a corner, a ray from behind, a ray parallel to the quad, and
a turned quad. The ray starts at the hand and ends on the panel, so there is
no parallax, unlike the world ray (`controllers.md` 3.2).

### 5.4 The mouse

The mouse moves the render pixel as it does today. On the menu panel that
pixel is a panel pixel with no conversion. The mod clamps it to the menu rect
and does not turn it by the head, because a panel pixel is already fixed in
the room. The mouse then works on the panel, and the mark of 5.6 shows where
it is.

If the menus are drawn larger in the target (4.2), the pointer takes the
inverse: the game gets `c + (pixel - c) / scale` about the same center `c`.

### 5.5 What changes in pointer_map.cpp

1. A new test, `PanelOwnsPointer()`: `ui_layer` is 1, `ui_panel_pointer` is
   1, and a dialog is up or `StereoIdle()` is true.
2. `ChooseOwner`: on the panel, a controller does not need the head camera or
   a scene. It needs only a hit on the panel.
3. `PointerMapApply`: on the panel, take `ControllerPanelPixel` instead of
   `ControllerRenderPixel`. Skip `LockToRoom`. Clamp to the menu rect.
4. When the pointer moves from the world to the panel, keep the last world
   pixel. When it moves back, restore that pixel, so the Hand is where the
   player left it.
5. `h_GCameraUpdate`: while the pointer is on the panel, give the camera code
   the last world pixel. A drag on a menu slider must not move the camera
   [I, test it].
6. `controllers.cpp`: record the surface at each press, for 5.2.

### 5.6 A mark where the pointer is

`FrontEnd::SetCursorOn` exists [V], so the game may draw its own cursor in
some menus. The U1 captures show whether a cursor lands on the panel. If none
does, the mod draws a small mark into the panel target after the last panel
batch. A Direct3D 9 state block (`CreateStateBlock(D3DSBT_ALL)`, capture,
apply) around that draw leaves the device as Dd7to9 expects it. A line from
the controller to the hit comes later (U6).

## 6. Screens with no scene, with the panel

- The picture (the plasma background, the loading art, a video) goes on the
  screen quad of 4.3. It is always a quad fixed in the room, and never a
  projection layer. This removes the stale pose of 1.4.
- The Setup dialogs of the front end and of the load and save boxes go to the
  menu panel, in front of the screen quad. A FinishFrame outside the eye
  passes counts as pass 0 for the routing of 2.4. Whether `LoadSaveBox` runs
  its FinishFrame with the callbacks on is not checked [I].
- The pointer is on the menu panel (5.2). A controller can now point in these
  screens, which answers open question 5 of `controllers.md`.
- The result: menus in one place in the room, in play and before it, larger
  and fixed.

## 7. Cost

At a 2880 by 2880 render one surface of 4 bytes a pixel is 31.6 MiB.

| Item | Cost |
| --- | --- |
| Memory | The panel target, its depth, the shared texture and the swapchain images (3 [I]): about 190 MiB. A swapchain of the rect size and `CopySubresourceRegion` bring it down. |
| Clears | Two clears at the first bind of a frame |
| Draws | The panel batches draw once instead of twice. That is a small saving. |
| Copies | One `StretchRect` and one `CopyResource` of the target. The eye copy of the same size measures 0.00 to 0.01 ms [M, `backend.md`]. |
| Flush | None added. The eye 0 flush, 0.34 ms [M], covers it. |
| Binds | A few `SetRenderTarget` pairs a frame |
| Compositor | One or two more quad layers |
| A frame with no interface | Nothing: no clear, no copy, no layer |

The biggest cost is memory, and it is small against a headset render.

## 8. Risks

1. **The switch behind dxwrapper's back.** This is the biggest risk. Dd7to9
   caches the render target, the viewport and the render states, and it does
   not check them against the device (2.2). If the mod leaves a state or the
   target wrong, the world or the next frame draws wrong. The rules of 2.3,
   the target check before each panel draw, and the composite check of 2.5
   cover it. All of it is testable on the desktop.
2. **A wrong route.** The Creature panel, other drawing in
   `HelpSystem::Draw3D`, and video in play are not classified yet (1.3). A
   part that is fixed to the world but goes to the panel floats in the wrong
   place. The U1 census and captures settle it.
3. **The skipped draw in pass 1.** If a panel caller draws only in pass 1, it
   is lost. `UiDepthReportPassMismatch` already names every caller that draws
   a different count in each pass.
4. **Alpha.** Blend modes other than source alpha give a halo or a hole on
   the panel (2.5). The sRGB edges are a little dark (3.3).
5. **Device loss.** The new surfaces must go when the device goes, through the
   bridge's `Release` hook.
6. **The quad is always on top.** A world object nearer than a panel is still
   covered by it (4.3).
7. **Streaming.** Virtual Desktop sends one video stream, so its runtime
   probably composes all layers before the encode [I]. The panel is then less
   sharp than on a headset that composes layers itself. The fixed place and
   the free size still hold.
8. **The Hand while a dialog is up in play.** The Hand and the pick read the
   pointer as a pixel of the eye view (`hand.md`, "A render pick"). On the
   panel that pixel is a menu pixel, so the Hand can move in the world behind
   the menu [I]. The camera code gets the last world pixel (5.5), but the
   Hand does not.
9. **Readers of the screen.** `ScriptedScreenShot::Process`, the save
   picture, is skipped while VR runs [V, `snapshot_guard.cpp`].
   `CreatureRoom::TakeScreenShot`, `GScript::SnapShot` and
   `SaveCreatureSnapshot` also read the screen [V for the names, I for what
   they read]. Their pictures lose the interface, which is better. The copy
   to the monitor loses it too.
10. **Stencil.** The panel has its own depth surface, so the world's stencil,
    if the game uses one, is not touched. The panel batches may set a stencil
    test [I]. The census logs it.

## 9. Stages

Each stage keeps the old behavior when its key is 0.

The stages follow the roadmap (Track 3). U1 and U2 are built, see section
12. The ini key is `ui_panel`, not `ui_layer`.

### U1: draw the panel target, and save it (desktop only)

- `ui_panel=2`: in pass 0, each panel batch draws into the panel target and
  also into the eye, as today. Nothing changes in the headset.
- Code: the panel pair and its life in `gpu_bridge.cpp`. The routing, the
  spans and the HelpText-only counter in `draw_hooks.cpp`. The hook on
  `0x0081E7D0`. A read-only hook on the Direct3D 7 `SetRenderState` for the
  census. A 32 bit BMP writer in `eye_capture.cpp`.
- On a capture frame (F12, or `bwvr-eye-frames.txt`), save
  `bwvr-ui-NNNNN.bmp` with its alpha, and `bwvr-ui-over-NNNNN.bmp` over a gray
  checkerboard.
- The log, per report window:

  ```
  ui layer     : N panel batches (M menu, H help, S subtitle box), K kept in the eyes
  ui layer     : B binds, R times the target was found changed
  ui layer     : menu rect x y w h, help rect x y w h (render pixels)
  ui layer     : blend modes: A source alpha, O off, D additive, X other
  ```

- Desktop test: a headless stereo run with the Esc menu open, the Options
  page, and a subtitle in the opening. Then the composite check of 2.5.
- Done when every Setup, HelpText and rect queue batch has a class, the
  composite check passes, R is 0, and the captures show whole panels.

### U2: the panel layers in the headset

- `ui_panel=1`: pass 1 skips the panel batches, and the eyes lose the screen
  interface. The shared texture, the panel swapchain, `SetPanelTexture`.
- Two quads with their own place and scale (4.1 to 4.3), from the menu rect
  and the help rect of U1. The A/B of 4.2 is for the headset.
- The test key of section 10 switches `ui_panel` between its ini value and 0
  while the game runs, for a live A/B.
- Desktop test: a headless run with `ui_panel=1`. The eye pictures have no
  menus or subtitles, and the panel picture has them. The size math has a
  unit test.
- Headset test: the Options page and the subtitles are sharp, fixed in the
  room, and in front of the world.

### U3: input on the panel

- Section 5: `PanelOwnsPointer`, the ray and quad math with tests, the mouse
  on the panel, the press that keeps its surface, the last world pixel for
  the camera code, and the mark.
- Desktop test: `test/controller_math` with the new cases. Then a run with the
  fake controller aimed at a known button of the Options page. The log shows
  the panel pixel and the press, and a capture shows the page change.
- Headset test: the Esc menu with the mouse and with a controller. A click on
  a menu must not move the land.

### U4: screens with no scene

- A frame with no scene goes out as the screen quad, never as a projection
  layer. The menus of those screens go to the menu panel (section 6).
- Desktop test: the log names the layers of each frame.
- Headset test: the front end, the load and save boxes, a loading screen, a
  video, and the Esc menu in play.

### U5: tabletop placement

- With T2 of the tabletop mode. The panels hang on the table anchor (4.4).
- Desktop test: the panel poses in the log for a set table anchor.

### U6: polish

- A line from the controller to the panel hit.
- The panel over the monitor copy, when the monitor copy is on.
- The launcher: the interface size row changes meaning when `ui_layer` is 1,
  and says so.

## 10. Ini keys and the test key

| Key | Default | Meaning |
| --- | --- | --- |
| `ui_panel` | 0 | 0 the interface in the eyes, as before. 1 the interface on its own layers (U2). 2 the test mode of U1. Built. |
| `ui_menu_distance_cm` | 140 | menu panel distance. Built. |
| `ui_menu_pitch_deg` | -10 | menu panel angle to eye level. Built. |
| `ui_menu_px_per_deg` | 16 | menu panel scale. Built. |
| `ui_menu_rect` | 0 | the menu rect in render pixels, `x y w h`. 0 takes the default of 12.4. Built. |
| `ui_help_distance_cm` | 120 | help panel distance. Built. |
| `ui_help_pitch_deg` | -30 | help panel angle to eye level. Built. |
| `ui_help_px_per_deg` | 44 | help panel scale at a 2880 px tall render. It scales with the render height, as the help text does. Built. |
| `ui_help_rect` | 0 | the help rect in render pixels. Built. |
| `creature_panel` | 2 | the Creature panel with `ui_panel=1`: 0 off, 1 fixed in the room, 2 near the Creature. Built, section 13. |
| `ui_panel_pointer` | 1 | the pointer moves onto the menu panel while a dialog is up (U3) |
| `screen_distance_cm`, `screen_width_cm` | 200, 260 | the screen quad. These keys exist now [V, `mod_main.cpp`]. |
| `table_menu_up_cm` | 40 | tabletop: the menu panel over the table surface (U5) |
| `table_help_up_cm` | 15 | tabletop: the help panel over the table surface (U5) |

The test key: Ctrl Shift F11 with `test_keys=1` switches `ui_panel` between
its ini value and 0, at the next frame. With `ui_panel=0` in the ini it does
nothing and says so in the log. Ctrl Shift F12, the key this section first
named, is the audio key now (`stereo.cpp`, `PollHotkeys`).

## 11. Open questions

The answers found on 2026-09-30 follow each question. The runs are u1a (a
new game that stays on its start dialog) and u1b (the front end), both with
`ui_panel=2` at a 2560 by 1440 render. See 12.6.

1. Is the Creature panel `fn_005178D0` fixed to the screen or to the
   Creature? **Fixed to the screen [V].** See section 13.
2. What else does `HelpSystem::Draw3D` draw, besides the tooltip list?
   **Open.** The census counts `Draw3D` batches by caller now ("Draw3D" in
   the caller table), but neither run drew one.
3. Where do videos in play draw, and should they go on a panel? **Open.**
4. Does the game draw a cursor in the Setup dialogs? **Yes [M].** A teal
   arrow at the pointer, drawn inside the Setup callback. It lands on the
   panel with the dialog, at the pixel the dialog's hit test reads.
5. Do the Setup dialogs sit in the middle of the render at every render size?
   **In the middle at 2560 by 1440 [M].** The start dialog of a new game is
   406 by 316 px with its center at 1277, 727. The front end menu is 780 by
   570 px with its center at 1281, 729. Other render sizes are not tested.
6. Which blend modes, depth tests and stencil tests do the panel batches use?
   **For the menus [M]:** every batch is source alpha, inverse source alpha.
   The depth test is on, the depth write off, the stencil off. The alpha test
   is on for the text (`Draw3DScreenTriangle`) and off for the boxes and
   lines. The help layer is not measured yet.
7. Is the game's render target multisampled on this backend? **Yes [M].**
   `X8R8G8B8` (format 22), multisample type 4, quality 0. The panel has its
   own depth surface, so this does not matter for the panel.
8. Is the Setup callback late? **Yes [V].** Every path to `0x00643376` goes
   through `mov edi, 1` there. Between it and the push at `0x006433D7`
   there are only calls, and a call keeps `edi`. The callback is registered
   at priority `0xA0000` with the late flag.
9. Does an open dialog stop the Hand and the camera in play? **Open.**
10. Does Virtual Desktop's runtime compose quad layers before the encode?
    **Open.** It needs the headset.
11. Does `LoadSaveBox` run its FinishFrame with the callbacks on? **Open.**

## 12. As built: U1 and U2, 2026-09-30

### 12.1 Files

| File | What it does |
| --- | --- |
| `src/mod/ui_panel.h` | The settings, the shared names, and the functions of the three files below. No Direct3D, so the Direct3D 7 files can include it. |
| `src/mod/draw_hooks.cpp` | The route of each batch (2.4), the census, and the hook on the rect queue callback `0x0081E7D0`. The hook goes in only when `ui_panel` is not 0. |
| `src/mod/gpu_bridge.cpp` | The panel target and its depth, the switch, the clears, the share to Direct3D 11, the readback for a BMP, and the composite check targets. All of it goes with the device, through the `Release` hook. |
| `src/mod/ui_panel.cpp` | The route of each frame, the test key, the U1 captures, and the placement of the quads. |
| `src/mod/stereo.cpp` | Latches the route before the eye passes. After eye pass 0 it saves the U1 capture and copies the panel before the eye 0 capture. |
| `src/mod/mod_main.cpp` | The ini keys. `FinishFrame` outside the eye passes, and the last release after it. |
| `src/vr/xr_screen.cpp` | The panel swapchain, `SetPanelTexture`, `SetPanelQuads`, and the quad layers after the projection layer. |
| `src/mod/head_pose.cpp` | `HeadPoseOrigin`, the recenter pose in the LOCAL space. |
| `src/mod/controller_math.h` | `PanelPose` and `PanelWidth`, tested in `test/controller_math` (`PanelChecks`). |
| `src/mod/eye_capture.cpp` | A 32 bit BMP with a V4 header that keeps the alpha, and the panel over a checkerboard. |

### 12.2 The switch

`GpuBridgePanelBind` saves target 0, the depth surface, the viewport, the
scissor rect and the three alpha states of 2.5 at the first panel batch of a
span. It binds the panel pair and sets the three alpha states.
`GpuBridgePanelUnbind` puts back all seven. `SetRenderTarget` sets the
viewport and the scissor rect to the whole target, so both come back from the
saved values. Dd7to9 sets its viewport again at its next draw, but another
path might not.

The game's target is back at the next eye batch, at the end of the Setup,
HelpText, `HelpSystem::Draw3D` and rect queue scopes, at every `DrawTriangle`
call, and after `FinishFrame` (the last one counts a miss in the log). Before
each panel draw in a span the bind reads target 0 and the depth surface
again (rule 6), and binds again if either changed.

The first bind of a frame clears the panel with `ColorFill` to 0 and its
depth with `Clear` to 1.0, and its stencil to 0. The panel depth is
`D24S8`.

The alpha states are read after each panel draw for the census, because Dd7to9
sends its states at the draw. That replaces the read-only Direct3D 7
`SetRenderState` hook of 2.5, which is not needed.

### 12.3 The route

`draw_hooks.cpp` classifies as 2.4 says. The HelpText scope has its own
counter (`g_helptext_depth`). `HelpSystem::Draw3D` has another, and a batch
inside `Draw3D` stays in the eyes. `IsDuplicateWidget` runs first, as before.
A batch it marks as a hint stays in the eyes. A panel batch still records
the subtitle band, because the hint rule reads it.

| `ui_panel` | Eye pass 0 | Eye pass 1 | Outside the eye passes |
| --- | --- | --- | --- |
| 0 | as before | as before | as before |
| 1 | panel only | not drawn | as before (U4) |
| 2 | panel, then the eye as before | as before | panel, then the eye as before |

The route is latched once a frame, before the eye passes
(`UiPanelBeginFrame`). With `ui_panel=1` in the headset it needs the shared
panel texture and the projection layer. So the stereoscopic window
(`play_mode` 1) keeps its interface in its picture. With no headset,
`ui_panel=1` routes anyway, as the desktop test. A failed bind turns the route
off for the rest of the frame, both passes draw into the eyes, and no panel
goes out in that frame.

### 12.4 The quads

`XrScreen` makes the panel swapchain only with `ui_panel=1`. After eye pass
0, `stereo.cpp` calls `GpuBridgeCaptureUi` (one `StretchRect`, no flush), then
`GpuBridgeCaptureEye(0)`, whose flush drains both copies, then
`SetEyeTexture(0)`, `SetPanelTexture` and `SetPanelQuads`. A frame that drew
nothing to the panel sends no copy and no layer. `EndFrame` adds one quad for
each kind that drew in that frame, after the projection layer, with
`XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT`, in the LOCAL space,
visible to both eyes. Both quads read one image through two rects, which
`EndFrame` clamps to the image.

The pose: `PanelPose` puts the center `d` meters from the recenter pose, at
the pitch of the panel, straight ahead of the recenter yaw, and turns the
quad about its own x axis by the same pitch, so it faces the recenter head
position. The width is `PanelWidth`, `2 d tan(w / k / 2)`, and the height
keeps the rect's aspect. The menu density is fixed, because the menus draw at
a fixed size in pixels. The help density is `ui_help_px_per_deg` times the
render height over 2880, because the help text scales with the render height.
The log prints both poses once, and again after each recenter.

The default rects (`DefaultRect` in `ui_panel.cpp`), used when
`ui_menu_rect` and `ui_help_rect` are 0:

| Rect | Default | Why |
| --- | --- | --- |
| Menu | 1280 by 960 px, in the middle of the render, clamped to it | The menus are a fixed size in pixels and sit in the middle (question 5). The front end menu, 780 by 570 px, fits with room for a larger page. |
| Help | the whole width, from 60 percent of the height to the bottom | The subtitle box sits at the bottom of the render. Not measured on the panel yet. |

At 16 px a degree the menu rect is 80 degrees wide, but most of it is clear.
The front end menu itself is about 49 degrees wide, 1.27 m at 140 cm.

The tabletop placement (4.4) is U5. With the table on, the panels use the
normal placement.

### 12.5 The U1 captures

On a capture frame (F12 or the capture timer) with `ui_panel` not 0,
`UiPanelAfterFirstEye` writes, beside the eye pictures:

| File | What |
| --- | --- |
| `bwvr-ui-NNNNN.bmp` | The panel, 32 bit, with its alpha. The color is premultiplied. |
| `bwvr-ui-over-NNNNN.bmp` | The panel over a gray checkerboard |
| `bwvr-ui-spare-NNNNN.bmp` | The eye as it was just before the first panel batch |
| `bwvr-ui-check-NNNNN.bmp` | The spare, with the panel batches drawn over it the game's way |
| `bwvr-ui-frame-NNNNN.bmp` | Outside the eye passes (`ui_panel=2`): the finished frame |

The composite check: the panel composited over the spare
(`color + spare * (1 - alpha)`) must equal the check picture, within rounding.
The check picture replaces the design's "finished eye picture" of 2.5,
because the eye gets the interface shift and the interface size, and the
panel does not.

### 12.6 What U1 showed

Two headless runs at 2560 by 1440 with `ui_panel=2`, `capture_every_seconds`
10 and 15, four capture frames each:

- **u1a**, a new game. It stays on the start dialog ("Please choose from
  one of the following options") for the whole run, because nothing clicks.
- **u1b**, the front end. After the Lionhead videos the main menu shows over
  the 3D island. The front end is a scene: its frames go through the eye
  passes, not through the path for screens with no scene. The videos went
  through `FinishFrame` outside the eye passes and drew nothing to the panel.

Results [M]:

| Check | u1a | u1b |
| --- | --- | --- |
| Panel batches a frame | 52, all menu | 88, all menu |
| Callers | `SetupThing::DrawBox` 20, `DrawLine` 20, `Draw3DScreenTriangle` 12 | 34, 41, 13 |
| Batches kept in the eyes inside the scopes | 0 | 0 |
| Binds, and times the target was found changed (R) | one bind a batch, R 0 | one bind a batch, R 0 |
| Panel still bound after `FinishFrame` | never | never |
| Composite check, largest difference | 2.13 levels | 2.09 levels |
| Composite check, mean over the panel pixels | 0.39 | 0.45 |
| Pixels more than 2 levels off | 3 of 126 682 | 3 of 450 635 |
| Eye picture against the check picture, 30 px outside the menu | identical | not compared |

In the test mode each batch binds and unbinds, because the eye draws it
right after the panel. The eye picture is identical to the check picture
everywhere outside the menu, so the switch puts the target, the depth, the
viewport and the states back exactly. Inside the menu the eye's copy is the
panel's copy moved 8 px, which is the interface shift. The captures show
whole dialogs with their clear parts, and the game's cursor (question 4).

Not seen yet: a subtitle, the help text, the Creature panel, the hint beside
the Hand next to a panel scope, a tooltip, and a video in play. So the help
rect and the help half of the census are not measured. The next desktop run
is a new game with a fake controller that clicks through the start dialog
(`fake_controller_points=-15,-18,94 -16,-31,94`,
`fake_controller_script=point@400+1 trigger@440+8 point@520+1 trigger@560+8`,
as in the earlier `h1e` run), with `ui_panel=2` and then `ui_panel=1`.

### 12.7 What U2 does, and what is not tested

With `ui_panel=1` the eye passes route as the table in 12.3 says, the panel
goes to the headset as in 12.4, and the eyes lose the screen interface. The
hint beside the Hand and the tooltips stay in the eyes.

Not tested at all, not even on the desktop: a run with `ui_panel=1`. So these
are unchecked: that eye pass 1 skips the panel batches cleanly, that the eye
pictures lose the menus, and that the scripted click still works with the
menu only on the panel. The code for the hit test does not change, so it
should.

Needs the headset: the panel swapchain, `SetPanelTexture`, the quad layers,
their place and size, the alpha over the world, the sRGB edges of 3.3, the
recenter, and question 10.

The pointer (U3 is not built). The menus draw on the panel at their own
pixels, and the hit test reads those pixels, so a click lands under the
game's arrow on the panel. But the pointer still comes from the eye view:
the mouse pixel is turned with the head, and a controller ray goes through
the world. So the arrow moves on the panel when the head turns, and a
controller does not point at the panel.

### 12.8 Cost

| Item | Measured or estimated |
| --- | --- |
| Memory, `ui_panel=1` | The panel target and its depth, the shared texture, and the swapchain images: at 2880 by 2880 about 32 MiB each, so about 190 MiB with three swapchain images [I] |
| Memory, `ui_panel=2` | The panel pair, and on the first capture frame two more targets, a depth and a readback surface [M, made] |
| A frame, `ui_panel=1` | Two clears, a few binds, one `StretchRect` and one `CopyResource` of the panel, no flush of its own [I]. Pass 1 draws no panel batch, which is a small saving. Not timed. |
| A frame, `ui_panel=2` | One bind and one unbind for each panel batch, 52 to 88 a frame in the runs. The frame rate was not compared with a run without it. |
| A frame with no panel batch | Nothing: no clear, no copy, no layer |
| `ui_panel=0` | Nothing. The route, the switch, the rect queue hook, the share and the swapchain are all off. |

### 12.9 What U3 needs

- The game already draws its arrow on the panel at the pointer's pixel.
  So U3 does not need the mark of 5.6 for the menus.
- `PanelOwnsPointer` and the ray against the menu quad of 5.3. `PanelPose`
  in `controller_math.h` gives the pose, and the rect gives the pixels.
- On the panel the mouse must not be turned by the head (5.4).
- `UiPanelQuads` has the quad of each frame. A U3 function can read the same
  pose, the size and the rect.

## 13. The Creature panel (2026-10-02)

Buck asked for this after a headset test: "ideally we would render the
creature panel HUD in space but not move the camera at all". The camera part
is in `hand.md`, "The Creature by touch".

### 13.1 What the game draws [V]

- `fn_005178D0` (cdecl, no arguments, plain `ret`) runs from the `HelpText`
  callback at `0x005CD03C`. When the alpha at `0x00CC630C` is above 0, it
  calls `DrawCreatureStats` at its real entry `0x00517080`. The symbol file's
  `0x00517265` is inside that function. Then it clears the alpha and the
  layout at `0x00CC6310`. So the first eye pass draws the panel, and the
  second one does not.
- `Creature::Draw` (`0x00517910`) arms it with alpha 255 and layout 1 when the
  Creature has bit `0x10` of `Object+0x24` and is
  `GInterface::GetInteractObject`, the object of the next pick record. So the
  panel shows whenever the Hand is over the player's Creature, not only in the
  Creature state. `CameraModeFollow::Update` arms it with layout 0.
- The four values, at `0x00CC62EC` down to `0x00CC62E0`: a = 1 - (vtable
  `+0x11C`), b = 1 - `[+0x160]+0x1C`, c = `[+0x160]+0x30`, and d = the lesson
  total `+0xD8` of the Creature state. The labels are the help texts
  `0x1A52`, `0x1A66`, `0x1AAB` and `0x1A98`. What each one says is a guess
  [G].
- The box is fixed to the screen. It goes from W/40 across and is W/4 wide.
  Layout 1 goes from H/4 down and is H/3 high. Layout 0 goes from H/40 down
  and is H/4 high. W and H come from `0x00E85058`. The box takes no clicks.
- With `ui_panel=1` its batches go to the panel target, in the `HelpText`
  scope. The help rect is the bottom 40 percent of the target, so the box did
  not show in the headset at all.

### 13.2 What the mod does

- A hook on `fn_005178D0` latches, once a frame, that the panel drew, its
  layout and its four values. For layout 1 it also reads the Creature of the
  pick (`GInterface+0x400`) and puts its head in the LOCAL space: the base
  `+0x78` of its `LH3DCreature`, raised by 15 times its size `+0x90`.
  `WorldToRoom` and `RoomToLocal` in `controller_math.h` undo the steps that
  put a controller in the world, on the table and off it.
- A third rect and quad (`kPanelCreature`) cut the box out of the panel
  target, 8 px wider on each side. The quad uses the density of the help
  panel. `xr_screen.cpp` has three quads now. `stereo.cpp` still passes two,
  so `ui_panel.cpp` gives the third one to `XrScreen::StagePanelQuad` before
  `SetPanelQuads`, in the same frame. A frame that does not stage it shows no
  Creature panel.
- `creature_panel` places the quad:

| Value | Place |
| --- | --- |
| 0 | No quad. The panel does not show in the headset. |
| 1 | Fixed in the room: 0.9 m out, 35 degrees to the left of the recenter yaw and 15 degrees down, facing the head. A recenter moves it. |
| 2 (default) | Near the Creature. When the panel shows, it goes 30 cm over the Creature's head and 15 cm to its left as the eyes see it, then 0.5 to 1.5 m from the eyes, facing them (`CreaturePanelPoint`, `FacePose`). It stays there while it shows and until it was hidden for 1 s, so it does not follow a Creature that walks. With no head (layout 0, or no head pose) it goes to the place of 1. |

The log, once each time the panel shows:

```
ui panel     : the Creature panel shows (layout 1, values 0.62 0.80 0.45 0.10), near the Creature at 0.31 1.18 -0.74 m, 0.84 m from the eyes, 0.35 by 0.45 m, rect 64 712 736 976
```

The report window adds how many frames it drew and how many times it went to
the room. `XrScreen` counts its quad layers.

### 13.3 Tests

The math test checks the face of the quad (its front points at the eyes,
with no roll), the place near the Creature (to the left, over the head, 0.5
to 1.5 m from the eyes), and the room to LOCAL against `head_pose.cpp`.

Not run on the desktop: the runs were refused while the PC was in use, and a
run needs a game where the player owns a Creature. The run to do: the God
view rig of `god-view.md` section 12 with `ui_panel=2`, the fake hand on the
Creature (`touchc`) and a capture (`shot`). The U1 capture must show the box
at 2.5 to 27.5 percent across and 25 to 58 percent down, and the log must
show the values.

Needs the headset: the place and the size of the quad, its alpha over the
world, and whether 15 cm to the left keeps it clear of the Hand.

## 14. Sources

- `bwvr/src/mod/draw_hooks.cpp`: the caller ranges, the scopes, the hint rule,
  the tooltip dedup, the interface size, the subtitle placement, the shift,
  and `DrawInterface`.
- `bwvr/src/mod/stereo.cpp`: the eye loop, the projection flag, the render
  pose, `StereoIdle`.
- `bwvr/src/mod/mod_main.cpp`: `h_FinishFrame`, the path for screens with no
  scene, the screen keys.
- `bwvr/src/mod/gpu_bridge.cpp`: the shared textures, `StretchRect`, the
  flush, the device `Release` hook.
- `bwvr/src/vr/xr_screen.cpp`: the swapchains, `EndFrame`, the quad and
  projection layers, `SubmitTexture`.
- `bwvr/src/mod/pointer_map.cpp`: the owner, `LockToRoom`, the window message
  hooks, `PointerMapApply`.
- `bwvr/src/mod/ui_worldlock.cpp`: parked. It moved screen vertices to hang
  subtitles in the world, and that read worse than subtitles on the view.
  It also notes that a real layer needs "a second render target and control
  of the render target switch". Section 2 gives both.
- `bwvr/src/mod/snapshot_guard.cpp`: the save picture is skipped in VR.
- `bwvr/src/mod/head_pose.cpp`: the recenter origin.
- `bwvr/docs/backend.md`, `tabletop.md`, `controllers.md`, `hand.md`.
- dxwrapper 1.8.8600.25, `ddraw/IDirect3DDeviceX.cpp`,
  `ddraw/IDirectDrawX.cpp`, `ddraw/IDirectDrawSurfaceX.cpp`, and the shipped
  `bwvr/installer/payload/dxwrapper.ini`.
- `tools/disasm.py` on `0x0082F460`, `0x0082F2C0` and its eight callers,
  `0x005CD020`, `0x005CCAB0`, `0x006424E0`, `0x00407ED0`, `0x0081E7D0`,
  `0x00544F90`, and `--callers` of `0x0082F460`, `0x0082F0E0`, `0x005C59A0`
  and `0x00557D10`.

## 15. The tooltip at the Hand (2026-10-02)

Buck, after the headset test of 2026-10-02 (roadmap item 4): "I don't know
if I had hints and subtitles disabled or what, but I didn't see any anyway."

### 15.1 The game's help options [V]

HelpSystem (`GGame+0x25005C`) reads four options of the player's profile at
`fn_005C6CF0`. The options menu of the game writes them to the profile
(`DialogBoxOptions` at `0x005148B0`). The mod never writes them.

| Profile value | Field | Values | Missing |
| --- | --- | --- | --- |
| `TOOLTIP_LEVEL` | `+0x45FC` | 0 No ToolTips, 1 Minimum, 2 Intelligent, 3 All ToolTips | 2 |
| `TEXT_DRAW` | `+0x4604` | 0 No Text, 1 Story Text, 2 All Text | 1 |
| `CREATURE_HELP` | `+0x4608` | 0 off, 1 on | 1 |
| `HELP_LEVEL` | `+0x45F4` | 0 No Help to 4 All Help | 3 |

With No ToolTips, `fn_005C9D00` makes no tooltip at all: no hint beside the
Hand, no tooltip of an object and no "Amount" of a bulk take. With No Text,
`fn_005C6E60` gives no subtitles. The profile of this PC, which the headset
test and the desktop runs both use, has all four at 0: "No ToolTips", "No
Text", "No Help" and no Creature help. That is why Buck saw no hints and no
subtitles, and why no desktop run before showed a real hint
(`controller-hints.md`, 8.5).

`controller_hints.cpp` now logs the four values once a game, for example:
`hints : the game's help options are "No ToolTips", "No Text", "No Help" and
Creature help off (0 0 0 0)`. The fix for the player is in the game: the
options, then "Help Options".

The test key `help_test=1` sets All ToolTips, All Text, High Help and the
Creature help in the game's memory, never in the profile. It is for the
desktop runs.

### 15.2 Where a tooltip landed [V, runs bk2 and bk7]

A 2880 by 2880 render at 110 degrees, the God view, the fake hand. The hint
beside the Hand of the land is "Move" with a key cap "Trigger". The game
asks for a box of 115 pixels (the size of the call). The first batch is 56
pixels high.

| | Left eye | Right eye |
| --- | --- | --- |
| The Hand's point, 1.00 m (bk2) | x 1242 | x 1174 |
| The game's first copy of "Move" | x 1448 | x 1366 |
| The flat copy that the mod drew | x 1448 | x 1448 |
| The Hand's point, 0.55 m (bk7) | x 1207 | x 1084 |
| "Amount: 57" as the game drew it | x 1090 | x 940 |

So the flat copy had no disparity and showed at the far end of the view,
while the Hand had 68 pixels of disparity at 1 m and 123 at 0.55 m. A player
who looked at the hand saw the hint twice. At `ui_scale_pct` 25 the box was
29 pixels, about 1.6 degrees, and its text was about 1 degree. No
subtitle drew in these runs, even with All Text, so the place of a subtitle
was not measured. The subtitle rules did not change.

### 15.3 What the mod does

Since 2026-10-03 this runs only with `hint_card=0`. With the default,
`hint_card=1`, a tooltip of a controller does not draw in the eye pictures:
it goes to the hint card (section 16).

While the controller places the Hand (`HandGlowEyePoint` in `hand_glow.cpp`
gives the Hand's point in this eye), a tooltip goes to the Hand
(`TooltipAtHand` in `draw_hooks.cpp`, `HintAtHandScale` and
`HintAtHandPoint` in `controller_math.h`):

1. The size. The box of the call becomes `kHintHandCm` (2.5 cm) of the room
   high at the Hand's depth. The scale is 0.22 at 1 m and 0.40 at 0.55 m, and
   it stays between 0.1 and 3.
2. The left eye scales the tooltip about the Hand's point, at the game's
   layout, and moves it `kHintHandSideCm` (6 cm) to the right. It keeps that
   place as an offset from the Hand's point.
3. The right eye puts the tooltip at the same offset from its own Hand's
   point. So both eyes give the tooltip the Hand's disparity, and it shows at
   the Hand's depth, beside the hand.

`ui_scale_pct`, `hint_scale_pct` and `hint_flat` do not change a tooltip at
the Hand. With the mouse nothing of this runs, and the hints are as before.
The game's mouse picture hint (the widget band) is not changed. With the
controller words on, the game's hints become key caps and take the tooltip
path.

The game draws its Hand after the help, so the mesh can hide a part of a
label. In run bk9 the first letter of "Wood Amount" was behind the arm of the
Hand. With 4 cm to the right (run bk11) that label was clear, but the count of
a take was half behind the bundle in the Hand. So the move is now 6 cm. That
value has no desktop run yet.

### 15.4 The log

On a sample frame (every 150 frames, and when a tooltip with a new text
shows) the tag `ui hint` gives each tooltip, hand hint picture and subtitle
bar with the eye, its box as the game drew it, the pointer and the Hand's
point in that eye. A second line gives the scale and the place at the Hand.
The report of the `ui depth` tag counts the tooltips at the Hand.

### 15.5 Tests

The math test has 11 new checks (`HintHandChecks`): the size at 1 m and at
0.55 m, the limits, the scale about the Hand's point in the left eye, and the
Hand's disparity of 68 pixels for a point of the tooltip in both eyes.

Desktop runs bk3 to bk11 (`hand.md`, "The bulk take", for the rig):

- bk3: "Move" went to the Hand with scale 0.219 at 1.00 m, in both eyes at
  the Hand's disparity. The Hand's mesh covered it.
- bk7: "Amount: 118" showed below and to the right of the Hand in both eyes,
  123 pixels apart, as the Hand's point. Its letters were about 1 cm high.
- bk9, bk10 and bk11: "Wood Amount: 30000" with the key cap "Grip" over a
  store pile, at scale 0.40 at 0.55 m. In bk11 the label was clear of the
  Hand, 123 pixels apart in the two eyes as the Hand's point.

**Needs the headset.** Whether 2.5 cm reads well at 40 to 80 cm, whether the
label fuses at the hand's depth, and where the Hand's mesh hides it.

## 16. The hint card (2026-10-03)

The hint beside the Hand and the prompts of the controller show on small
quad layers of their own: one card beside the wrist of each hand. The whole
design is in `controller-hints.md`, section 9. For the interface layer, these
are the facts:

- Each card is a quad layer. `XrScreen::EndFrame` adds the cards after the
  projection layer and the three panel quads, so they are over all of them.
  The order is the subtitle card (section 17), then the card of the hand
  that points, then the card of the other hand. Each uses
  `XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` with the color
  multiplied by the alpha, as the panels do.
- Each card has its own swapchain, in the format of the eyes: 768 by 320
  pixels for a hint card. `XrScreen` makes it at the first layer of the card
  (`SendCard`). The picture comes from the CPU: GDI draws the words,
  `hint_card.cpp` adds the card and the alpha, and `UpdateSubresource` writes
  the part that shows into the swapchain image of the frame. There is no
  Direct3D 9 target and no share, so a hint card does not use the panel
  target of section 2.
- It does not need `ui_panel`. `UiPanelBeginFrame` gives it the frame with
  `ui_panel=0` too, and `UiPanelReport` adds its report.
- The mod stages a card with `XrScreen::StageCard` before `EndFrame`, as
  `StagePanelQuad` does for the Creature panel. It shows only in the frame
  that staged it.
- When the runtime refuses a frame with a card, `CardFailed` turns that kind
  of card off: first the subtitle card, and then the hint cards.
- With `ui_panel=1` the route of 12.3 does not change: a tooltip never went
  to the panel, and now it goes to the card.
- With `ui_panel=0` the test key Ctrl Shift F11 switches the cards on and
  off: the hint cards and the subtitle card (`UiPanelToggle`).

## 17. The subtitle card (2026-10-03)

Buck, after the headset test of build `f194c59`: "The subtitles sometimes
get bugged where they are too far apart in each eye where it's impossible to
focus on them to read them."

Code: the route in `src/mod/draw_hooks.cpp` (`kUiPanelSubtitles`), the frame,
the place and the picture in `src/mod/ui_panel.cpp` (`SubtitleBeginFrame`,
`SubtitleEndPasses`), the pure math in `src/mod/subtitle_card_math.h`, the
half size copy in `src/mod/gpu_bridge.cpp` (`GpuBridgePanelReadHalf`), and
the quad layer in `src/vr/xr_screen.cpp` (`kCardSubtitles`).

### 17.1 Why the subtitles went apart

The subtitles were flat interface in the two eye pictures. `ShiftIn` moved
them `shift` pixels toward the nose in each eye, so the two pictures were
`2 * shift` pixels apart and showed at one depth. His render is 2880 pixels
for 110 degrees (a focal length of 1008 pixels), and his eyes are 65 mm
apart. His log has 42 report windows with subtitles (512838 batches).

| Cause | What happened | Proof |
| --- | --- | --- |
| 1. The subtitles were behind the land and the hands that they covered | The interface depth was 1.37 m at the ceiling of 24 pixels, and 4.10 m at the resting shift of 8 pixels. In the God view the land is at the waist, and the subtitles were 35 degrees below the middle of the view, on that land and on the hands. A thing that covers a nearer thing gives two cues that do not agree. The eyes stay on the near thing, and they see the far text two times | The log: the land was 0.53 to 1.02 m below the head, so the land under the subtitles was 1.05 m away or nearer. In the 51 sample frames with a subtitle and a tracked hand, the Hand's point was 0.42 to 0.66 m away. Frame 1078: the Hand's point at x 1245 in the left eye and 1091 in the right eye, 154 pixels apart, while the subtitles were 48 pixels apart. With the eyes on that Hand, the two pictures of the subtitles were 6.0 degrees apart |
| 2. The clamp | `ui_shift_max_px=24` is the nearest depth the interface can have: 1.37 m. At 100 units a meter the adaptive rule asked for 97 pixels (the eyes 6.5 units apart, the nearest world 37 units away), which is 0.34 m. So the rule could not put the text in front of the world, and no value of the clamp is right: text at 0.34 m is too near to read | 30 of the 42 windows have "shift in use 24.0 px" |
| 3. The depth changed | The adaptive rule follows the nearest world, and the eye distance in world units follows the zoom. So the subtitles went between 1.37 m and 4.10 m in one session, and "sometimes" they were far worse | 7 windows at 8.0 pixels, and 5 between 10.9 and 21.5. At 4.10 m the two pictures are 7.8 degrees apart with the eyes on the Hand, and 2.7 degrees with the eyes on the land |
| 4. A low frame rate | The subtitles were in the eye pictures, fixed to the head. The runtime shows a late frame again, turned for the new head pose. Text that is fixed to the head then steps against the head while the head turns | 21 of the 42 windows were under 85 frames a second, down to 52.8 [I: the step was not measured] |
| 5. An icon in a line | A button icon in a subtitle draws through the camera help quads, so the hint rule took it: the hint size, and no interface depth | The code. His log has no sample of it |
| 6. The first frame of a subtitle | The letters draw before the box that defines their band. In the left eye of that frame they had no band, so they were not moved to 35 degrees, and in the right eye they were | The code. It lasts one frame |

What is not a cause [V]: a subtitle path that skips the clamp. The box (the
rect queue caller) and its letters all go through `ShiftIn` with one shift,
left eye plus and right eye minus. The rows of the box are the same in both
eyes in each of the 68 sample frames of the log (y 2101 to 2192).

So no depth is right for flat text in the eye pictures of the God view. Each
depth that is good to read is behind the hands, and the land comes nearer
when the player looks down or zooms.

### 17.2 One card in the room

While the headset shows a projection layer and `subtitle_card` is 1, no help
text of the HelpText scope draws in an eye picture. This runs with
`ui_panel=0`, which is the default.

1. `UiPanelBeginFrame` latches the route for the frame (`SubtitleBeginFrame`):
   `UiPanelRouteFrame(kUiPanelSubtitles, on)`. Both eye passes agree.
2. In eye pass 0, `PanelTake` draws each batch of the HelpText scope and of
   the subtitle box into the panel target of section 2, at the pixels of the
   game, with no interface size and no depth. In eye pass 1 it draws them
   nowhere. This is the route of `ui_panel=1` for the help text only: the
   menus stay in the eye pictures. The hint beside the Hand and the tooltips
   go to the hint card, as before.
3. After both eye passes (`UiDepthSetEye(-1)`), `SubtitleEndPasses` takes
   the bounds of what drew, with 8 pixels of room, on a grid of 16 pixels
   (`CardRect`). `GpuBridgePanelReadHalf` copies that rect at half the size
   with one `StretchRect` (linear, so each pixel is the mean of four) and
   one `GetRenderTargetData`. That call copies a whole surface, so the copy
   goes through a target that is only as large as the rect, on a grid of 128
   pixels. Six sizes are kept. A subtitle costs a copy of about 1 MB. Both
   eye captures are done at that time, so the GPU has no work left and the
   copy does not wait.
4. `XrScreen::StageCard(kCardSubtitles)` gives the pixels and the pose to
   `EndFrame`. That makes a swapchain of half the render size at the first
   subtitle, writes the rect with `UpdateSubresource`, and adds ONE quad
   layer in the LOCAL space. This is the path of the hint card, which works
   in Buck's runtime.

A quad shows at one place of the room to both eyes, so the text cannot be
double. The runtime places the quad again for each displayed frame, so a
slow game frame does not move it against the room. The text is at half the
size of the render: the quad shows 1440 texels across 35 degrees, which is
1.7 texels for each pixel of the headset, so the letters do not shimmer as
they would at the full size.

The card adds no new hook. With `ui_panel=0` it installs the hook of
`ui_panel=1` on the rect queue callback `0x0081E7D0`, which puts the game's
target back after the subtitle box. The binary has `call 0x81e3c0` and
`ret 4` there [V], and `fn_0081E740` registers it as a FinishFrame callback.

A picture of the camera help code inside the subtitle rows (cause 5) goes to
the card with its line, when it draws in the HelpText scope. The Creature panel is drawn in the HelpText scope
too, so it shows on the same screen, left of the middle and near eye level.

### 17.3 The place

`subtitle_card_math.h`. The game's pixels show on a flat screen that hangs
from an anchor: a head position and a yaw.

- The row at 93 percent of the render height (the middle of the subtitle
  box) is 1.6 m from the anchor head, 17 degrees below eye level, straight
  ahead of the anchor yaw (`RectQuad`). The screen is square to that line of
  sight. The whole width of the render is 1 m, so a subtitle is 35 degrees
  wide and a letter of 77 pixels is 1 degree high.
- 17 degrees below eye level at 1.6 m is 47 cm below the head. So the card is
  above a land that is 0.6 m below the head, and in front of the land behind
  it. It is not in the place where the hands work.
- The quad of a frame is the rect that drew. A pixel has one place in the
  room in each rect, so a rect that grows moves no letter.
- **The anchor** (`AnchorStep`) starts at the recenter pose
  (`HeadPoseOrigin`), and each recenter puts it there again. The card does
  not follow the head: a head that turns, leans or looks down moves nothing.
- The head is away when it has turned more than 50 degrees from the anchor,
  moved more than 1 m along the floor, or more than 35 cm up or down. While
  the card shows, the anchor goes to the head only after the head was away
  for 1 s without a break. So the card is never lost behind the player.
  While no card shows, an away head takes the anchor at once, because
  nothing moves in the view.
- **The card never slides** (`ShownStep`). When the anchor changes while the
  card shows, the card fades out in 0.2 s at its old place, takes the new
  place while it is clear, and fades in. The yaw comes from the right axis
  of the head, so a head that looks straight down keeps its yaw.

### 17.4 The fallback

| Case | What the help text does |
| --- | --- |
| The headset with a projection layer | The card |
| The runtime refused the swapchain of the card or a frame with it (`CardFailed`), the panel target could not be made or bound, or the copy failed 3 times | The eye pictures, at one fixed depth of 0.75 m (`subtitle_fallback_cm`, 43 pixels in each eye at his render). The depth does not follow the world, and it is in front of the land under the subtitles. The log says why |
| No headset: the mouse player, and the desktop rig | The eye pictures at the interface depth, as before |
| The stereoscopic window (a headset with no projection layer) | As before: the interface is in its picture |
| A flat frame of the cut scene window | The card, in the plane of the screen under the scene (17.9). With no card there, the help text is in the picture of the window, on the screen plane |
| `subtitle_card=0` | As before |
| `ui_panel=1` | The help panel of section 4, as before |

In a flat frame of the cut scene window, the cases of the second row put the
help text in the picture of the window, and not at the fixed depth of 0.75 m.
That depth is for the God view.

### 17.5 Keys

| Key of `bwvr.ini` | Default | Meaning |
| --- | --- | --- |
| `subtitle_card` | 1 | 1 shows the subtitles and the help text on the card in the headset. 0 keeps them in the eye pictures |
| `subtitle_card_distance_cm` | 160 | The distance from the head to the subtitles, 60 to 400 |
| `subtitle_card_down_deg` | 17 | The angle of the subtitles below eye level, -20 to 60 |
| `subtitle_card_width_cm` | 100 | The width of the whole render on the card, 30 to 300. It sets the size of the letters |
| `subtitle_fallback_cm` | 75 | The fixed depth of the fallback, 30 to 400 |

Test keys: `subtitle_card_test=1` runs the route with no headset, for the
desktop rig. `subtitle_selftest=1` draws two lines of help text in the
HelpText scope with the game's own text code (`SubtitleSelfTest`), because a
desktop run shows no subtitle. With `test_keys=1` and `ui_panel=0`, Ctrl
Shift F11 switches between the cards and the eye pictures, for a live
comparison.

### 17.6 The log

The tag `subtitles`:

- The keys at the start.
- `the subtitles and the help text now go to the subtitle card, and none
  draws in an eye picture`, or the reason for the fallback or for the old
  path, at each change.
- `frame N, the subtitle card: the rect x y w h of the 2880 by 2880 render,
  1440 by 152 px on the card, at x y z m, turn ..., 1.00 by 0.11 m, 1.60 m
  from the head and 17 degrees below eye level. The head has a yaw of 3
  degrees and the card of 0. Fade 1.00, the copy took 0.21 ms`: each time
  the card comes into the view (80 lines at most), and on a capture frame.
- `the subtitle card hangs from the recenter pose` and `goes to a new place,
  because the head was far away from the old one`.
- On a capture frame: `bwvr-subtitle-card-NNNNN.bmp` (with its alpha) and
  `bwvr-subtitle-card-over-NNNNN.bmp` (over a checkerboard).
- Each report window: the frames with the route, the frames with a card,
  the moves, the time and the size of the copy, the frames with help text
  and no card, and the batches of the fallback. `ui panel` gives the batches
  that went to the target and the half size copies. `XrScreen` counts the
  quad layers of the card.

### 17.7 Cost

| Item | Measured or estimated |
| --- | --- |
| Memory | The panel target and its depth at the render size (66 MiB at 2880 by 2880), the copy targets (1 to 2 MiB each for a subtitle), a buffer and a swapchain of half the render size (8 MiB, and 24 MiB for 3 images) [I] |
| A frame with a subtitle | One clear of the panel, one bind for each span, one `StretchRect`, one `GetRenderTargetData`, one copy and one `UpdateSubresource` of the rect. The log gives the time of the copy |
| The copy, with no game [M] | `halfcopy.exe` (the scratchpad, `cards\d3dtest`) makes the same Direct3D 9 calls on the RTX 3090 of this PC, 200 times each: a subtitle strip of 2112 by 304 pixels takes 0.41 ms, the Creature panel 0.32 ms, and a rect that holds both 2.2 ms |
| A frame with no help text | Nothing: no clear, no copy, no layer |
| `subtitle_card=0` | Nothing |

### 17.8 Tests

The math test has 58 checks for the card (`SubtitleCardChecks` in
`hint_checks.h`):

- The numbers of 17.1: 24 pixels are 1.37 m and 8 pixels are 4.10 m, the
  Hand's point of frame 1078 is at 0.43 m, and the two pictures of the
  subtitles are 6.0 and 7.8 degrees apart with the eyes on that Hand.
- The yaw of the head, also for a head that looks straight down.
- The anchor: a head that turns 49 degrees and leans 40 cm for 20 s moves no
  card. A look of 0.9 s at 80 degrees moves no card. A head that stays at 80
  degrees moves the card one time, after 1 s. A turn of 180 degrees, a walk
  of 1.2 m and a player who stands up each move it after 1 s. A recenter
  puts it at the new recenter pose.
- The card is at the old place or at the new one in each frame of a move,
  and it takes the new place while it is clear.
- The screen: the strip is 1.6 m away, 17 degrees down, 1 m wide and above
  the land. It faces the head. A pixel has one place in the room in a small
  rect and in a large one.
- The rect of the card, and the fixed shift of the fallback.

A harness (`cards\subharness` in the scratchpad, not in the repository) links
the real `ui_panel.cpp` with a made up panel picture and a made up head. It
ran about 800 frames:

- A subtitle with the head at the recenter pose (0, 1.2, 0): the card is
  1056 by 152 pixels and 0.73 by 0.11 m, at 0.000 0.742 -1.533 m. That is
  1.60 m from the head and 17 degrees below eye level.
- The head turns 40 degrees, leans 30 cm and looks 50 degrees down for 2 s:
  the card is at the same place in each frame.
- A look of 0.8 s at 80 degrees: the card stays. The head stays at 80
  degrees: after 1.0 s the card fades out, and it fades in at -1.210 0.742
  -0.266 m, ahead of the head.
- With no subtitle, the head turns to -100 degrees. The next subtitle shows
  ahead of the head in its first frame.
- The Creature panel and a subtitle in one frame: one card of 1216 by 1048
  pixels. The Creature panel only: 368 by 496 pixels and 0.26 by 0.34 m,
  left of the middle and 1 degree above eye level.
- A recenter: the card fades out and goes to the new recenter pose.
- The test key: no card, and then the card again.
- Three copies in a row that fail: the fallback, with a depth of 0.75 m for
  the help text.

`halfcopy.exe` (`cards\d3dtest` in the scratchpad) makes the Direct3D 9 calls
of `GpuBridgePanelReadHalf` with no game. Its 15 checks pass on the RTX 3090
of this PC: the targets, the `StretchRect`, the copy and the lock work, a
pixel inside the box keeps its color and its alpha, and a line of 1 pixel
becomes the mean of the line and the box.

**Desktop runs.** Not run yet. The PC was in use on the afternoon of
2026-10-03, and a desktop run needs 10 minutes with no input. The script is
`cards\cards_run.ps1` in the scratchpad: the rig of the hands runs with
`subtitle_selftest=1`, one run with `subtitle_card_test=1` (the two test
lines must be on the card and not in the eye pictures) and one run without
it (the two test lines in the eye pictures).

**Not tested:** the route in the game. That is: the draw of the help text
into the panel target, the eye pictures with no help text, the bounds, and
the copy of a real panel. The same switch of the target was tested for the
menus only (12.6).


**Needs the headset.**

- The subtitle card as a quad layer: its swapchain of 1440 by 1440 pixels,
  the alpha of the game's subtitle box over the world, and its colors.
- Whether the letters read well at 1 degree, and whether 17 degrees below
  eye level at 1.6 m is a good place while the hands work.
- Whether the row at 93 percent is the middle of the game's subtitle box.
  No desktop run shows a real subtitle, so the place of the box in the
  render was not measured.
- A move of the card after a turn of the head, with its fade.
- The time of the copy at 2880 by 2880, from the report line.
- The Creature panel on the card.

### 17.9 In the cut scene window (2026-10-03, evening)

Buck, after the headset test of build `3918f87`: "Move subtitles during
cutscenes to below the cutscene window we're looking through. The strange
depth clashing issues are very annoying." In that build a flat frame of the
cut scene window had no card: the subtitles were in its picture, at the
interface depth, and the scene came out of the screen in front of them.

Now the card takes the help text of a flat frame too
(`cutscene-window.md`, section 7):

- `SubtitleBeginFrame` asks `CutsceneWindowSubtitles`. In a flat frame with
  `cutscene_subtitles=1` the mode is the card, with the same route: the
  HelpText scope and the subtitle box draw into the panel target in eye pass
  0, and nowhere in eye pass 1.
- `SubtitleEndPasses` gives such a frame to `SubtitleWindowCard`. The card
  has no anchor of its own there and no fade. `CutsceneWindowSubtitleQuad`
  gives its pose: in the plane of the screen, at the yaw of the screen, with
  its top 4 cm under the scene. The whole width of the render is
  `cutscene_subtitle_width_cm` (140) on the card.
- `SubtitleCopy` is the half size copy of 17.2, now one function for the
  card of the room and the card of the window.
- `XrScreen::EndFrame` adds the card to the quad path of the window, as one
  quad layer over the two eye quads. If the runtime refuses that frame, the
  card is off (`CardFailed`) and the subtitles stay in the pictures.
- When the window is gone, the card has its place in the room again, and
  its anchor rule starts as for a new card.

The log has `the subtitle card hangs under the scene of the cut scene
window, and no subtitle draws in its picture` when the first flat frame takes
the route, and `the subtitle card under the cut scene window: the rect ...`
with the pose and the size each time the card comes into the view.

The math test has 14 checks for the place (`window_checks.h`, "THE SUBTITLES
OF THE WINDOW").

**The desktop run cw2b** (2026-10-03, `subtitle_card_test=1` and
`subtitle_selftest=1`, a render of 2560 by 1440, no headset) is the first
run of the route of the help text in the game. Before the story, the two
test lines are on the card of the room: the rect 480 1216 624 176, at
-0.191 -0.455 -1.534 m, 1.57 m from the head and 17 degrees below eye level.
From the first flat frame (675) they are on the card under the window: at
-0.981 -0.819 -1.699 m with the turn of the screen (30 degrees), 0.34 by
0.10 m, which is in the plane of the screen and 4 cm under the scene. In the
eye pictures of frames 803 and 1303 the rect of the test lines has no white
pixel, and the picture of the card has 346. At the first full view frame
after the story (3937) the card has its place in the room again. The copy
took 0.12 to 0.32 ms. The run had no headset, so no layer went out, and the
game showed no subtitle of its own.
