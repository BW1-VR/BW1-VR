# The interface on its own layer

The goal: the menus, the subtitles, the help text and the Creature panel are
on flat quads that stay at a fixed place in the room. OpenXR draws each quad
as its own layer, over the world. The tooltips and anything else attached to
the world stay in the eye pictures or go to the hint cards.

Marks, as in `tabletop.md`:

- [V] verified: read in the binary with `tools/disasm.py`, or in the source.
- [M] measured in a run.
- [I] inferred. It needs a test before anything depends on it.
- [G] a guess.

## Summary

- The game draws the screen interface in `LH3DRender::FinishFrame`, in the
  callbacks that run after the world, the screen quad and the cinema bars.
  FinishFrame runs inside the scene walk, so this happens once per eye pass.
  The tooltip list draws earlier, inside the walk.
- The mod can move the screen interface to a render target of its own. It
  switches the Direct3D 9 target around those draw calls in eye pass 0 and
  skips them in eye pass 1. dxwrapper's Dd7to9 does not see the switch, so
  the mod puts the target, the depth surface and its own states back.
- The target reaches OpenXR the way the eyes do: a shared texture, one copy,
  and a third swapchain. It goes out as `XrCompositionLayerQuad` layers in
  the LOCAL space, after the projection layer, with the source alpha flag.
- One target feeds three quads: a menu panel, a help panel and a Creature
  panel. Screens with no scene use an opaque quad for the picture behind the
  menus (planned, section 6).
- The subtitles and the help text have a card of their own (section 17). It
  works with `ui_panel=0`.
- The hints and the prompts of the controllers are on the hint cards
  (section 16 and `controller-hints.md`).
- The pointer on the panel is not built (section 5). The menus draw on the
  panel at their own pixels, but the pointer still comes from the eye view.

## 1. Where the interface is drawn

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
| `0x0082F5E4` to `0x0082F614` | `ZFUNC` set to `ALWAYS`, then a screen quad (`DrawIndexedPrimitive`, vtable `+0x68`). It sets the depth to 1.0 (`tabletop.md`, "Not built: the ring and the depth key"). |
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
`0x0064326B`. Every path to `0x00643376` goes through `mov edi, 1` there, and
between it and the push at `0x006433D7` there are only calls, which keep `edi`
[V]. The callback is registered at priority `0xA0000` with the late flag.
`FallingSpell::Init` also registers a callback (`0x00526480`) with a flag from
`ebp` [I].

The rect queue callback runs before HelpText in the same pass. So the subtitle
box drawn in a pass is the one that HelpText queued in the pass before [I].

### 1.3 Each part of the interface

| Part | Drawn by | In the frame | Goes to |
| --- | --- | --- | --- |
| Setup dialogs | the Setup callback `0x006424E0` | late callbacks | menu panel (`ui_panel=1`), else the eyes |
| Subtitle letters | `fn_005CCAB0`, through `Draw3DScreenTriangle` | late callbacks | the subtitle card (section 17), else the help panel or the eyes |
| Subtitle box | the rect queue, caller `0x0081E3C0` to `0x0081E58C` | late callbacks | the same |
| Help text | `fn_005CCAB0` | late callbacks | the same |
| Creature panel | `fn_005178D0` | late callbacks | its own quad (section 13) |
| Hint beside the Hand | CameraHelp quads `0x00447450`, inside the HelpText callback | late callbacks | the hint card (section 16) |
| Tooltips | `HelpSystem::Draw3D` `0x005C59A0` (called only from `Process3dEngine` at `0x0054E2ED` [V]) and again from HelpText | in the walk, and late | the hint card, or the Hand with `hint_card=0` (section 15) |
| Screen quad, cinema bars | FinishFrame | before the late callbacks | the eyes. The bars are hidden. |
| Advisors | `HelpDude` code | in the walk and callbacks | the eyes (room camera, `spirit_anchor.cpp`) |
| Speech bubbles (a signpost, a villager that speaks, the Creature) | the bubble draw `0x005767D0`, an object of the Z sort | in the walk | the eyes, as one card at the depth of the sign (section 18) |
| Video in play | `LHVideoPlayer::thedraw`, also from `FallingSpell::Draw` | late callbacks | the eyes. Not classified yet. |

The size, the placement and the shift of the interface in the eye pictures are
in `draw_hooks.cpp` and `backend.md` ("Interface size", "Subtitles",
"Tooltips").

`Creature::Draw` arms the Creature panel only while the Creature is
`GInterface::GetInteractObject` [V]. The box itself is fixed to the screen.

### 1.4 Screens with no scene

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
  screen is fixed in the room.
- **After the first stereo frame:** `stereo.cpp` sets the projection flag
  once and never clears it, and `have_render_pose_` stays true. So a screen
  with no scene goes out as a projection layer. It declares the eye poses of
  the last stereo frame and the render frustum of about 110 degrees. The
  runtime then shows the flat picture over the whole view, with no depth,
  fixed in the room where the head pointed at that last frame [I]. A player
  who turned since then sees the picture to one side and black past its edge.

The menu text is a fixed 20 to 35 px at any render size [M, `backend.md`],
which is about 0.8 to 1.3 degrees at 26 px per degree.

## 2. Drawing the interface to its own target

### 2.1 The choice

The mod switches the Direct3D 9 render target around the panel batches. It
already holds the Direct3D 9Ex device (`gpu_bridge.cpp`), makes shared render
target textures on it, and reads its render target. This depends only on the
three caches in 2.2, not on dxwrapper internals.

Two other ways do not fit. A Direct3D 7 surface with
`IDirect3DDevice7::SetRenderTarget` lets Dd7to9 know about the switch, but it
ties the mod to one dxwrapper version (it needs `IID_GetInterfaceX` to find
the Direct3D 9 surface behind it). A double draw, once into the eye and once
into the panel, only serves as a test mode (`ui_panel=2`), because the goal
is to take the interface out of the eyes.

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
`IDirect3DDeviceX.cpp` and `IDirectDrawX.cpp`]. The mod sets these three
without a conflict and puts them back.

The shipped `dxwrapper.ini` has `DdrawAllowMultiSampling = 1` [V]. The game's
render target is `X8R8G8B8` (format 22) with multisample type 4, quality 0
[M]. The panel target has its own depth surface and no multisampling, so this
does not matter for the panel.

### 2.3 Rules for the switch

1. The panel target has the same size as the game's render target. The bridge
   reads the size with `GetDesc` on the current target when it starts. The
   format is `A8R8G8B8` with no multisampling.
2. The panel target has its own depth surface (`D24S8`) of the same size, with
   no multisampling. The late callbacks draw after the screen quad at
   `0x0082F614` has set the game's depth to 1.0. So a panel depth cleared to
   1.0 gives the same depth test [I: the interface does not read the depth of
   the world].
3. The mod binds the pair when the first panel batch of a span arrives
   (`GpuBridgePanelBind`). It saves target 0, the depth surface, the viewport,
   the scissor rect and the three alpha states of 2.5, binds the panel pair
   and sets the three alpha states. `SetRenderTarget` sets the viewport and the
   scissor rect to the whole target, so both come back from the saved values.
4. At the first panel bind of a frame, the mod clears the panel target to 0
   with `ColorFill` and the panel depth to 1.0 (stencil 0) with `Clear`.
5. `GpuBridgePanelUnbind` puts back all seven saved values. This runs at the
   next eye batch, at the end of the Setup, HelpText, `HelpSystem::Draw3D` and
   rect queue scopes, at every `DrawTriangle` call, and after `FinishFrame`.
   A panel still bound after `FinishFrame` counts as a miss in the log.
6. Before each panel draw in a span, the bind reads target 0 and the depth
   surface again. If either changed, it binds again and counts it in the log.
   This guards against a Dd7to9 path that binds the game's target in the
   middle of a span.
7. The mod draws panel batches in eye pass 0 only. In eye pass 1, it does not
   call the engine draw for a panel batch. `DrawAndClip` and `DrawAndClip2D`
   clip and submit, so a skipped call only loses pixels [I].
8. Fail safe. If the GPU bridge is not ready, the projection layer is off, or
   any create fails, the interface stays in the eyes. With `play_mode` 1 (the
   stereoscopic window) the world is already a screen, so the interface stays
   in it.

The spans are short. The HelpText scope mixes panel batches and the hint, so a
frame can have a few binds. The log counts them.

### 2.4 Which batches go to the panel

`DrawInterface` in `draw_hooks.cpp` decides:

```
panel batch, when ui_panel is on and the rules of 2.3 allow it:
  inside the Setup callback 0x006424E0            (g_menu_depth > 0)
  inside the HelpText callback 0x005CD020 only    (g_helptext_depth, because
                                                   g_help_depth also counts
                                                   HelpSystem::Draw3D)
    and not a tooltip                             (g_in_tooltip)
    and not a kept hint copy                      (g_widget_hint, g_widget_scale,
                                                   g_widget_flat after
                                                   IsDuplicateWidget)
  the rect queue caller 0x0081E3C0 to 0x0081E58C  (the subtitle box)

eye batch: everything else

eye pass 0, or a FinishFrame outside the eye passes: draw a panel batch
  into the panel target
eye pass 1: do not draw a panel batch
```

A panel batch gets none of the eye steps: no interface shift, no interface
size, no subtitle lift, no subtitle placement. The panel has its own depth
and its own size in the room. So the menus sit exactly on their hit test. In
the eye pictures the shift moves them 8 px from it in each eye.

The HelpText scope has its own counter, `g_helptext_depth`. The rect queue callback `0x0081E7D0` (`__stdcall`, one argument, `ret 4` [V])
has a hook that closes the span after the subtitle box. The mod installs it
when `ui_panel` is not 0 or the subtitle card is on. `IsDuplicateWidget` runs
first. A batch it marks as a hint stays in the eyes. A panel batch still
records the subtitle band, because the hint rule reads it. A batch inside
`HelpSystem::Draw3D` stays in the eyes.

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

Every menu batch uses source alpha, inverse source alpha, with the depth test
on, the depth write off and the stencil off [M]. The alpha test is on for the
text (`Draw3DScreenTriangle`) and off for the boxes and lines. The help layer
is not measured. The mod reads the blend and the tests after each panel draw
for the census (`ui depth` and `ui layer` log lines), because Dd7to9 sends its
states at the draw.

The composite check proves the math on the desktop (`ui_panel=2`). On a
capture frame, the mod copies the eye target to a spare surface just before
the first panel batch, and draws the panel batches over it the game's way.
The panel composited over the spare (`color + spare * (1 - alpha)`) must equal
that check picture within rounding of 1 or 2 levels.

## 3. From the target to OpenXR

### 3.1 The share

`gpu_bridge.cpp` makes one more shared texture on the same device:
`A8R8G8B8`, `D3DUSAGE_RENDERTARGET`, with a shared handle, opened on the
Direct3D 11 device of the session. This is the eye path again [V,
`GpuBridgeStart`]. The panel target, its depth surface and the shared texture
live and die with the device, through the `Release` hook the bridge already
has.

After eye pass 0 returns, `stereo.cpp` calls `GpuBridgeCaptureUi`, then
`GpuBridgeCaptureEye(0)`. `GpuBridgeCaptureUi` does one `StretchRect` from the
panel target to the shared texture and no flush. The eye 0 capture then
issues its event query flush, and that flush drains every command before it,
the panel copy too [V, `backend.md`, "Keeping the two devices in step"]. So
the panel adds no wait. Then `SetEyeTexture(0)`, `SetPanelTexture` and
`SetPanelQuads` follow.

A frame with no panel batch makes no copy and submits no panel layer.

### 3.2 The quad layers

`XrScreen` has a panel swapchain: the same format as the eyes, the render
size, usage `TRANSFER_DST` and `SAMPLED`. It exists only with `ui_panel=1`.
`SetPanelTexture` acquires, waits, copies with `CopyResource` and releases,
as `SetEyeTexture` does. Then `EndFrame` submits:

```
layers[0] = the projection layer
layers[1] = the menu panel quad, when it has content
layers[2] = the help panel quad, when it has content
layers[3] = the Creature panel quad, when it has content

each quad:
  type          XR_TYPE_COMPOSITION_LAYER_QUAD
  layerFlags    XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT
  space         the LOCAL space the projection layer uses
  eyeVisibility XR_EYE_VISIBILITY_BOTH
  subImage      the panel swapchain, the rect of that panel (4.1), layer 0
  pose, size    from 4.3
```

OpenXR draws later layers over earlier ones. Without
`XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT` the runtime reads the color
as premultiplied, which matches 2.5. The quads read one swapchain image
through several rects, which `EndFrame` clamps to the image. If a runtime
refuses one image in several layers, use a second swapchain and
`CopySubresourceRegion` [I]. `stereo.cpp` passes the menu and the help quad to
`SetPanelQuads`. `ui_panel.cpp` gives the Creature quad to
`XrScreen::StagePanelQuad` before it, in the same frame. A frame that does not
stage it shows no Creature panel.

The size of a quad in meters, for a rect `w` px wide at `k` px per degree and
a distance `d` (`PanelWidth` in `controller_math.h`):

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
darker than on a monitor [I]. If that shows in the headset, a pixel shader
pass on the Direct3D 11 side can convert the panel to linear premultiplied
color.

## 4. Placement

### 4.1 One panel or two

Three quads come from one target:

- **The menu panel** shows the Setup dialogs.
- **The help panel** shows the subtitles and the help text. Nothing on it
  takes a click.
- **The Creature panel** (section 13) is cut out of the help scope.

The kinds want a different size and a different place. The menus are small
text that a player reads and clicks, straight ahead. The subtitles are large
text that a player reads in a glance, low down. The subtitle bar sits at the
bottom of the render [V, `backend.md`, "Subtitles"]. The Setup dialogs sit
near the middle. A rect for each kind cuts them apart in one target. The rects
are fixed values, not fitted each frame, so a panel never changes size while
the player looks at it. If the kinds ever overlap in the target, use two
targets.

The default rects (`DefaultRect` in `ui_panel.cpp`), used when `ui_menu_rect`
and `ui_help_rect` are 0:

| Rect | Default | Why |
| --- | --- | --- |
| Menu | 1280 by 960 px, in the middle of the render, clamped to it | The menus are a fixed size in pixels and sit in the middle (question 5 of section 11). The front end menu, 780 by 570 px, fits with room for a larger page. |
| Help | the whole width, from 60 percent of the height to the bottom | The subtitle box sits at the bottom of the render. Not measured on the panel. |

At 16 px a degree the menu rect is 80 degrees wide, but most of it is clear.
The front end menu itself is about 49 degrees wide, 1.27 m at 140 cm.

### 4.2 Pixels per degree

The render is about 2880 px across about 110 degrees, which is 26 px per
degree [M, `backend.md`]. On the eye pictures the interface can only be the
angle its pixels give it. The quad size sets the angle on the panel.

- **Menu panel, 16 px per degree** (`ui_menu_px_per_deg`). Menu text of 20 to
  35 px becomes 1.25 to 2.2 degrees. The texture is magnified 1.6 times, so
  the letters are a little soft. The other way is to draw the menus larger in
  the target and give the pointer the inverse scale (5.4), which is sharper.
  An A/B in the headset decides.
- **Help panel, 44 px per degree** (`ui_help_px_per_deg`, at a 2880 px tall
  render, scaled with the render height as the help text is). On the panel
  the help layer draws at the game's own size, 77 px letters at a 2880 px
  render [M], which is 1.75 degrees. The compositor then reduces the quad
  about 1.8 times with no mip levels, which can crawl [I]. The other way is
  to keep `ui_scale_pct` in the target and show the quad at about the display
  density. An A/B in the headset decides.

### 4.3 Normal placement

The anchor is the recenter pose: the head position at recenter and its yaw.
`head_pose.cpp` keeps this pose, and it keeps only the yaw [V]. Each quad
faces the head position of the anchor.

`PanelPose` puts the center `d` meters from the recenter pose, at the pitch of
the panel, straight ahead of the recenter yaw. It turns the quad about its own
x axis by the same pitch, so it faces the recenter head position. The menu
density is fixed, because the menus draw at a fixed size in pixels. The log
prints both poses once, and again after each recenter.

| Quad | Center, from the anchor | Content |
| --- | --- | --- |
| Menu panel | 140 cm forward, 10 degrees below eye level | Setup dialogs |
| Help panel | 120 cm forward, 30 degrees below eye level | subtitles, help text |
| Creature panel | section 13 | the Creature panel |

The advisors float about 43 degrees to each side, 1 to 1.5 m away
(`tabletop.md`, "The spirits beside the table"), so they do not meet the panels.

A quad layer draws over the projection layer everywhere. A panel behind a
nearer object still covers it, and the two depth cues then disagree. In
normal play the land is tens of meters away, so a panel at 1.2 to 1.4 m is
nearer than the world behind it in nearly all views [I].

### 4.4 Tabletop placement (planned)

With the table on, the panels use the normal placement. The plan is to hang
them on the table anchor of `tabletop.md`, "The table camera" (the table center in the room,
80 cm forward and 40 cm down from the recenter pose):

| Quad | Center | Content |
| --- | --- | --- |
| Menu panel | above the far edge of the table, about 170 cm forward, at eye level | Setup dialogs |
| Help panel | at the far edge, 15 cm over the table surface, tilted back to face the head | subtitles, help text |

The island sits below the line of sight to both panels, so the panels cover
little of it. With the Virtual Desktop passthrough mask on, the panels are not
in the key color, so they float in the real room [I]. The planned keys are
`table_menu_up_cm` (40) and `table_help_up_cm` (15). Neither key exists in
the code yet.

### 4.5 Recenter

The panels never follow the head. They move only when the anchor moves. F11
and a recenter from the headset menu
(`XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING`) both clear the origin in
`head_pose.cpp`, and the next frame takes a new one [V]. The panels land in
front of the player on that frame, once. `HeadPoseOrigin` gives the origin
position and yaw in the LOCAL space.

## 5. Input on the panel (planned)

Not built. `ui_panel_pointer` is not in the code.

### 5.1 One pointer, two surfaces

`pointer_map.cpp` keeps one pointer [V]. One owner, the mouse or a
controller, sets one float render pixel (`g_true_x`, `g_true_y`). The mod
writes it once a frame to `0x00E852C0` and `0x00E852C4` and to the Windows
cursor. `h_MouseMoveHandler` and `h_MouseSetPosition` convert the window
messages the same way, so the Hand and the interface agree.

The plan keeps this rule and adds one choice per frame: the surface of that
pixel. On the **world**, the pixel is a pixel of the eye view. On the **menu
panel**, the pixel is a pixel of the panel target. The panel target is the
render size. So a panel pixel is the exact pixel the game drew the dialog at,
and the exact pixel its hit test expects [I: the dialogs test the pixels they
draw]. The game still gets one pointer.

The game draws its own teal arrow at the pointer in the Setup dialogs [M],
inside the Setup callback. It lands on the panel with the dialog, at the pixel
that the hit test reads. So the plan needs no mark of its own for the menus.

### 5.2 When the pointer is on the menu panel

The pointer is on the menu panel while a Setup dialog is up, or while a screen
with no scene shows. `SetupBox::GetCurrentActiveBox` `0x00407ED0` returns
`[0x00C4CD20]`, and a value other than 0 means a dialog is up [V].

The game's state decides the surface, not the ray. A ray that leaves the panel
does not drop the pointer onto the island. The pointer keeps its last panel
pixel. A press keeps its surface until its release. A drag that starts on the
panel ends there. A land drag that is held when a dialog opens ends in the
world. The help panel never takes the pointer.

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

It is a hit when `u` and `v` are both in 0 to 1. The function belongs in
`controller_math.h` with tests in `test/controller_math`: a ray at the center,
at a corner, from behind, parallel to the quad, and a turned quad. The ray
starts at the hand and ends on the panel, so there is no parallax, unlike the
world ray (`controllers.md` 3.2).

### 5.4 The mouse

On the menu panel the mouse pixel is a panel pixel with no conversion. The mod
clamps it to the menu rect and does not turn it by the head, because a panel
pixel is already fixed in the room. If the menus are drawn larger in the
target (4.2), the pointer takes the inverse: the game gets
`c + (pixel - c) / scale` about the same center `c`.

### 5.5 What changes in pointer_map.cpp

1. A test, `PanelOwnsPointer()`: `ui_panel` is 1, `ui_panel_pointer` is 1,
   and a dialog is up or `StereoIdle()` is true.
2. `ChooseOwner`: on the panel, a controller does not need the head camera or
   a scene. It needs only a hit on the panel.
3. `PointerMapApply`: on the panel, take a panel pixel from the ray instead of
   `ControllerRenderPixel`. Skip `LockToRoom`. Clamp to the menu rect.
4. When the pointer moves from the world to the panel, keep the last world
   pixel. When it moves back, restore that pixel, so the Hand is where the
   player left it.
5. `h_GCameraUpdate`: while the pointer is on the panel, give the camera code
   the last world pixel. A drag on a menu slider must not move the camera
   [I, test it].
6. `controllers.cpp`: record the surface at each press, for 5.2.

Today the menus draw on the panel at their own pixels and the hit test reads
those pixels. But the pointer still comes from the eye view: the mouse pixel
is turned with the head, and a controller ray goes through the world. So the
arrow moves on the panel when the head turns, and a controller does not point
at the panel.

## 6. Screens with no scene, with the panel (planned)

Not built. A frame with no scene still goes out as in 1.4.

- The picture (the plasma background, the loading art, a video) goes on the
  screen quad of 1.4. It is always a quad fixed in the room, and never a
  projection layer. This removes the stale pose of 1.4.
- The Setup dialogs of the front end and of the load and save boxes go to the
  menu panel, in front of the screen quad. A FinishFrame outside the eye
  passes counts as pass 0 for the routing of 2.4. Whether `LoadSaveBox` runs
  its FinishFrame with the callbacks on is not checked [I].
- The pointer is on the menu panel (5.2). A controller can then point in these
  screens (`controllers.md`, open question 5).

Only `ui_panel=2` routes a FinishFrame outside the eye passes (12.2).

## 7. Cost

At a 2880 by 2880 render one surface of 4 bytes a pixel is 31.6 MiB.

| Item | Cost |
| --- | --- |
| Memory, `ui_panel=1` | The panel target, its depth, the shared texture and the swapchain images (3 [I]): about 190 MiB. A swapchain of the rect size and `CopySubresourceRegion` bring it down. |
| Memory, `ui_panel=2` | The panel pair, and on the first capture frame two more targets, a depth and a readback surface |
| Clears | Two clears at the first bind of a frame |
| Draws | The panel batches draw once instead of twice. That is a small saving. |
| Copies | One `StretchRect` and one `CopyResource` of the target. The eye copy of the same size measures 0.00 to 0.01 ms [M, `backend.md`]. |
| Flush | None added. The eye 0 flush, 0.34 ms [M], covers it. |
| Binds | A few `SetRenderTarget` pairs a frame. With `ui_panel=2`, one bind and one unbind for each panel batch. |
| Compositor | One or two more quad layers |
| A frame with no interface | Nothing: no clear, no copy, no layer |
| `ui_panel=0` | Nothing. The route, the switch, the share and the swapchain are off. |

The biggest cost is memory, and it is small against a headset render.

## 8. Risks

1. **The switch behind dxwrapper's back.** This is the biggest risk. Dd7to9
   caches the render target, the viewport and the render states, and it does
   not check them against the device (2.2). If the mod leaves a state or the
   target wrong, the world or the next frame draws wrong. The rules of 2.3,
   the target check before each panel draw, and the composite check of 2.5
   cover it. All of it is testable on the desktop.
2. **A wrong route.** Other drawing in `HelpSystem::Draw3D` and video in play
   are not classified (1.3). A part that is fixed to the world but goes to the
   panel floats in the wrong place. The `ui depth` census names the caller of
   each batch.
3. **The skipped draw in pass 1.** If a panel caller draws only in pass 1, it
   is lost. `UiDepthReportPassMismatch` names every caller that draws a
   different count in each pass.
4. **Alpha.** Blend modes other than source alpha give a halo or a hole on
   the panel (2.5). The sRGB edges are a little dark (3.3).
5. **Device loss.** The surfaces go when the device goes, through the
   bridge's `Release` hook.
6. **The quad is always on top.** A world object nearer than a panel is still
   covered by it (4.3).
7. **Streaming.** Virtual Desktop sends one video stream, so its runtime
   probably composes all layers before the encode [I]. The panel is then less
   sharp than on a headset that composes layers itself. The fixed place and
   the free size still hold.
8. **The Hand while a dialog is up in play.** The Hand and the pick read the
   pointer as a pixel of the eye view (`hand.md` on the pick). On the panel
   that pixel would be a menu pixel, so the Hand could move in the world
   behind the menu [I]. The camera code would get the last world pixel (5.5),
   but the Hand would not.
9. **Readers of the screen.** `ScriptedScreenShot::Process`, the save
   picture, is skipped while VR runs [V, `snapshot_guard.cpp`].
   `CreatureRoom::TakeScreenShot`, `GScript::SnapShot` and
   `SaveCreatureSnapshot` also read the screen [V for the names, I for what
   they read]. Their pictures lose the interface, which is better. The copy to
   the monitor loses it too.
10. **Stencil.** The panel has its own depth surface, so the world's stencil,
    if the game uses one, is not touched. The panel batches may set a stencil
    test [I]. The census logs it.

## 9. What is built and what is planned

| Part | State |
| --- | --- |
| The panel target, the switch, the share, the census and the captures (`ui_panel=2`) | built |
| The panel layers in the headset (`ui_panel=1`): menu and help quads | built, never run with `ui_panel=1` on the desktop, never seen in a headset |
| The Creature panel quad | built (section 13) |
| The subtitle card | built (section 17) |
| Input on the panel (section 5) | planned |
| Screens with no scene on a screen quad (section 6) | planned |
| Tabletop placement of the panels (4.4) | planned |
| A line from the controller to the panel hit | planned |
| The panel over the monitor copy, when the monitor copy is on | planned |
| The launcher: the interface size row changes meaning when `ui_panel` is 1, and says so | planned |

With `ui_panel=1` these are unchecked on the desktop: that eye pass 1 skips
the panel batches cleanly, that the eye pictures lose the menus, and that a
scripted click still works with the menu only on the panel. The code for the
hit test does not change, so it should.

Only a headset shows: the panel swapchain, `SetPanelTexture`, the quad layers,
their place and size, the alpha over the world, the sRGB edges of 3.3, the
recenter, and whether Virtual Desktop composes quad layers before the encode.

## 10. Ini keys and the test key

| Key | Default | Meaning |
| --- | --- | --- |
| `ui_panel` | 0 | 0 the interface in the eyes. 1 the interface on its own layers. 2 the test mode: each panel batch also draws in the eye, and the captures of 12.5 are saved (desktop only). |
| `ui_menu_distance_cm` | 140 | menu panel distance |
| `ui_menu_pitch_deg` | -10 | menu panel angle to eye level |
| `ui_menu_px_per_deg` | 16 | menu panel scale |
| `ui_menu_rect` | 0 | the menu rect in render pixels, `x y w h`. 0 takes the default of 4.1. |
| `ui_help_distance_cm` | 120 | help panel distance |
| `ui_help_pitch_deg` | -30 | help panel angle to eye level |
| `ui_help_px_per_deg` | 44 | help panel scale at a 2880 px tall render. It scales with the render height, as the help text does. |
| `ui_help_rect` | 0 | the help rect in render pixels. 0 takes the default of 4.1. |
| `creature_panel` | 2 | the Creature panel with `ui_panel=1`: 0 off, 1 fixed in the room, 2 near the Creature (section 13) |
| `screen_distance_cm`, `screen_width_cm` | 200, 260 | the screen quad of 1.4 |
| `ui_scale_pct`, `hint_scale_pct`, `hint_flat`, `ui_shift_max_px`, `subtitle_lift_pct`, `subtitle_deg` | 25, 20, 1, 24, 0, 35 | the interface in the eye pictures (`backend.md`) |
| `subtitle_*` | section 17.5 | the subtitle card |
| `hint_card`, `prompts`, `hint_card_size_pct` | `controller-hints.md` | the hint cards |
| `bubble_card` | section 18.4 | a speech bubble of the game |

The test key: Ctrl Shift F11 with `test_keys=1` switches `ui_panel` between
its ini value and 0, at the next frame (`UiPanelToggle`, `PollHotkeys` in
`stereo.cpp`). With `ui_panel=0` it switches the hint cards and the subtitle
card between the card and the eye pictures. Ctrl Shift F12 is the audio key.

## 11. Open questions

1. Is the Creature panel `fn_005178D0` fixed to the screen or to the
   Creature? **Fixed to the screen [V].** See section 13.
2. What else does `HelpSystem::Draw3D` draw, besides the tooltip list?
   **Open.** The census counts `Draw3D` batches by caller ("Draw3D" in the
   caller table), but no run has drawn one.
3. Where do videos in play draw, and should they go on a panel? **Open.**
4. Does the game draw a cursor in the Setup dialogs? **Yes [M].** A teal arrow
   at the pointer, drawn inside the Setup callback.
5. Do the Setup dialogs sit in the middle of the render at every render size?
   **In the middle at 2560 by 1440 [M].** The start dialog of a new game is
   406 by 316 px with its center at 1277, 727. The front end menu is 780 by
   570 px with its center at 1281, 729. Other render sizes are not tested.
6. Which blend modes, depth tests and stencil tests do the panel batches use?
   For the menus, see 2.5. The help layer is not measured.
7. Does an open dialog stop the Hand and the camera in play? **Open.**
8. Does Virtual Desktop's runtime compose quad layers before the encode?
   **Open.** It needs the headset.
9. Does `LoadSaveBox` run its FinishFrame with the callbacks on? **Open.**
10. No desktop run has shown a real subtitle, a help text of the game, or a
    tooltip beside a panel scope. So the help rect and the help half of the
    census are not measured.

## 12. The implementation

### 12.1 Files

| File | What it does |
| --- | --- |
| `src/mod/ui_panel.h` | The settings, the shared names, and the functions of the three files below. No Direct3D, so the Direct3D 7 files can include it. |
| `src/mod/draw_hooks.cpp` | The route of each batch (2.4), the census, and the hook on the rect queue callback `0x0081E7D0` |
| `src/mod/gpu_bridge.cpp` | The panel target and its depth, the switch, the clears, the share to Direct3D 11, the readback for a BMP, the half size copy of the subtitle card, and the composite check targets. All of it goes with the device, through the `Release` hook. |
| `src/mod/ui_panel.cpp` | The route of each frame, the test key, the captures, the placement of the quads, the Creature panel and the subtitle card |
| `src/mod/stereo.cpp` | Latches the route before the eye passes. After eye pass 0 it saves the capture and copies the panel before the eye 0 capture. |
| `src/mod/mod_main.cpp` | The ini keys. `FinishFrame` outside the eye passes, and the last release after it. |
| `src/vr/xr_screen.cpp` | The panel swapchain, `SetPanelTexture`, `SetPanelQuads`, `StagePanelQuad`, the cards, and the quad layers after the projection layer |
| `src/mod/head_pose.cpp` | `HeadPoseOrigin`, the recenter pose in the LOCAL space |
| `src/mod/controller_math.h` | `PanelPose`, `PanelWidth`, `WorldToRoom`, `RoomToLocal`, `FacePose`, `CreaturePanelPoint`, tested in `test/controller_math` |
| `src/mod/subtitle_card_math.h` | The pure math of the subtitle card |
| `src/mod/eye_capture.cpp` | A 32 bit BMP with a V4 header that keeps the alpha, and the panel over a checkerboard |

### 12.2 The route of a frame

`UiPanelBeginFrame` latches the route once a frame, before the eye passes:

| `ui_panel` | Eye pass 0 | Eye pass 1 | Outside the eye passes |
| --- | --- | --- | --- |
| 0 | the eyes | the eyes | the eyes |
| 1 | panel only | not drawn | the eyes |
| 2 | panel, then the eye | the eyes | panel, then the eye |

With `ui_panel=1` in the headset, the route needs the shared panel texture and
the projection layer. So the stereoscopic window (`play_mode` 1) keeps its
interface in its picture. With no headset, `ui_panel=1` routes anyway, as the
desktop test. A failed bind turns the route off for the rest of the frame,
both passes draw into the eyes, and no panel goes out in that frame.

### 12.3 The quads of a frame

After eye pass 0, `stereo.cpp` calls `GpuBridgeCaptureUi` (one `StretchRect`,
no flush), then `GpuBridgeCaptureEye(0)`, whose flush drains both copies, then
`SetEyeTexture(0)`, `SetPanelTexture` and `SetPanelQuads`. A frame that drew
nothing to the panel sends no copy and no layer. `EndFrame` adds one quad for
each kind that drew in that frame, after the projection layer.

### 12.4 The captures

On a capture frame (F12 or `capture_every_seconds`) with `ui_panel` not 0,
`UiPanelAfterFirstEye` writes, beside the eye pictures:

| File | What |
| --- | --- |
| `bwvr-ui-NNNNN.bmp` | The panel, 32 bit, with its alpha. The color is premultiplied. |
| `bwvr-ui-over-NNNNN.bmp` | The panel over a gray checkerboard |
| `bwvr-ui-spare-NNNNN.bmp` | The eye as it was just before the first panel batch |
| `bwvr-ui-check-NNNNN.bmp` | The spare, with the panel batches drawn over it the game's way |
| `bwvr-ui-frame-NNNNN.bmp` | Outside the eye passes (`ui_panel=2`): the finished frame |

The check picture, and not the finished eye picture, is the reference of the
composite check, because the eye gets the interface shift and the interface
size, and the panel does not. In the test mode each batch binds and unbinds,
because the eye draws it right after the panel. Outside the menu, the eye
picture must equal the check picture, which shows that the switch puts the
target, the depth, the viewport and the states back exactly.

### 12.5 The log

The tag `ui layer` and `ui panel`, per report window:

```
ui layer     : N panel batches (M menu, H help, S subtitle box), K kept in the eyes
ui layer     : B binds, R times the target was found changed
ui layer     : menu rect x y w h, help rect x y w h (render pixels)
ui layer     : blend modes: A source alpha, O off, D additive, X other
```

R must be 0. The Direct3D 9 states of each panel batch are read after its
draw. A desktop test is a headless stereo run with `ui_panel=2`, with the
Esc menu open, the Options page, and a subtitle.

## 13. The Creature panel

The camera part is in `creature.md` (the Creature by touch).

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
  scope. The help rect is the bottom 40 percent of the target, so the box
  needs the quad of its own.

### 13.2 What the mod does

- A hook on `fn_005178D0` latches, once a frame, that the panel drew, its
  layout and its four values. For layout 1 it also reads the Creature of the
  pick (`GInterface+0x400`) and puts its head in the LOCAL space: the base
  `+0x78` of its `LH3DCreature`, raised by 15 times its size `+0x90`.
  `WorldToRoom` and `RoomToLocal` in `controller_math.h` undo the steps that
  put a controller in the world, on the table and off it.
- A third rect and quad (`kPanelCreature`) cut the box out of the panel
  target, 8 px wider on each side. The quad uses the density of the help
  panel. `ui_panel.cpp` gives it to `XrScreen::StagePanelQuad` before
  `SetPanelQuads`, in the same frame.
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

The math test checks the face of the quad (its front points at the eyes, with
no roll), the place near the Creature (to the left, over the head, 0.5 to
1.5 m from the eyes), and the room to LOCAL against `head_pose.cpp`.

No desktop run has drawn the box. A run needs a game where the player owns a
Creature: the God view rig of `god-view.md` with `ui_panel=2`, the fake hand
on the Creature (`touchc`) and a capture (`shot`). The capture must show the
box at 2.5 to 27.5 percent across and 25 to 58 percent down, and the log must
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
  subtitles in the world, and that read worse than subtitles on the view. A
  real layer needs a second render target and control of the render target
  switch, which section 2 gives.
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

## 15. The tooltip at the Hand

This path runs only with `hint_card=0`, or when the hint card cannot show.
With the default `hint_card=1`, a tooltip of a controller does not draw in the
eye pictures: it goes to the hint card (section 16).

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
`fn_005C6E60` gives no subtitles. A profile with these options at 0 shows no
hints and no subtitles, in the headset and in a desktop run. The player
changes them in the options of the game, under "Help Options".

`controller_hints.cpp` logs the four values once a game, for example:
`hints : the game's help options are "No ToolTips", "No Text", "No Help" and
Creature help off (0 0 0 0)`.

The test key `help_test=1` sets All ToolTips, All Text, High Help and the
Creature help in the game's memory, never in the profile. It is for desktop
runs.

### 15.2 Why a flat copy does not work

The game draws a tooltip as flat interface. A flat copy in the two eye
pictures has the same pixel in both eyes, so it has no disparity and shows at
the far end of the view. The Hand has a disparity of tens of pixels (68 px at
1 m and 123 px at 0.55 m, at a 2880 px render of 110 degrees). A player who
looks at the hand sees a flat hint two times. So a tooltip must have the
Hand's disparity.

### 15.3 What the mod does

While the controller places the Hand (`HandGlowEyePoint` in `hand_glow.cpp`
gives the Hand's point in this eye), a tooltip goes to the Hand
(`TooltipAtHand` in `draw_hooks.cpp`, `HintAtHandScale` and `HintAtHandPoint`
in `controller_math.h`):

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
the Hand. With the mouse nothing of this runs. The game's mouse picture hint
(the widget band) is not changed. With the controller words on, the game's
hints become key caps and take the tooltip path.

The game draws its Hand after the help, so the mesh can hide a part of a
label. The offset of 6 cm keeps a label clear of the arm and of the bundle in
the Hand. That value is not yet checked in a desktop run.

### 15.4 The log

On a sample frame (every 150 frames, and when a tooltip with a new text
shows) the tag `ui hint` gives each tooltip, hand hint picture and subtitle
bar with the eye, its box as the game drew it, the pointer and the Hand's
point in that eye. A second line gives the scale and the place at the Hand.
The report of the `ui depth` tag counts the tooltips at the Hand.

### 15.5 Tests

The math test (`HintHandChecks`) covers the size at 1 m and at 0.55 m, the
limits, the scale about the Hand's point in the left eye, and the Hand's
disparity for a point of the tooltip in both eyes. The desktop rig is the bulk
take rig of `hand.md` with `help_test=1`.

**Needs the headset.** Whether 2.5 cm reads well at 40 to 80 cm, whether the
label fuses at the hand's depth, and where the Hand's mesh hides it.

## 16. The hint card

The hint beside the Hand and the prompts of the controller show on small quad
layers of their own: one card beside the wrist of each hand. The whole design
is in `controller-hints.md`, section 9. For the interface layer, these are the
facts:

- Each card is a quad layer. `XrScreen::EndFrame` adds the cards after the
  projection layer and the three panel quads, so they are over all of them.
  The order is the subtitle card (section 17), then the card of the hand that
  points, then the card of the other hand. Each uses
  `XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` with the color
  multiplied by the alpha, as the panels do.
- Each card has its own swapchain, in the format of the eyes: 768 by 320
  pixels for a hint card. `XrScreen` makes it at the first layer of the card
  (`SendCard`). The picture comes from the CPU: GDI draws the words,
  `hint_card.cpp` adds the card and the alpha, and `UpdateSubresource` writes
  the part that shows into the swapchain image of the frame. There is no
  Direct3D 9 target and no share, so a hint card does not use the panel target
  of section 2.
- It does not need `ui_panel`. `UiPanelBeginFrame` gives it the frame with
  `ui_panel=0` too, and `UiPanelReport` adds its report.
- The mod stages a card with `XrScreen::StageCard` before `EndFrame`, as
  `StagePanelQuad` does for the Creature panel. It shows only in the frame that
  staged it.
- When the runtime refuses a frame with a card, `CardFailed` turns that kind
  of card off: first the subtitle card, and then the hint cards.
- With `ui_panel=1` the route of 12.2 does not change: a tooltip never goes to
  the panel. It goes to the card.
- With `ui_panel=0` the test key Ctrl Shift F11 switches the cards on and off:
  the hint cards and the subtitle card (`UiPanelToggle`).

## 17. The subtitle card

Code: the route in `src/mod/draw_hooks.cpp` (`kUiPanelSubtitles`), the frame,
the place and the picture in `src/mod/ui_panel.cpp` (`SubtitleBeginFrame`,
`SubtitleEndPasses`), the pure math in `src/mod/subtitle_card_math.h`, the
half size copy in `src/mod/gpu_bridge.cpp` (`GpuBridgePanelReadHalf`), and the
quad layer in `src/vr/xr_screen.cpp` (`kCardSubtitles`).

### 17.1 Why the subtitles are not in the eye pictures

Flat subtitles in the two eye pictures sit at one depth, and `ShiftIn` sets
that depth with a shift of `shift` pixels toward the nose in each eye. No
depth is right for flat text in the eye pictures of the God view. At a render
of 2880 pixels for 110 degrees (a focal length of 1008 pixels) and eyes 65 mm
apart:

| Reason | Detail |
| --- | --- |
| The subtitles are behind the things they cover | The interface depth is 1.37 m at the ceiling of 24 pixels (`ui_shift_max_px`), and 4.10 m at the resting shift of 8 pixels. In the God view the land is at the waist (0.5 to 1 m below the head), and the subtitles are 35 degrees below the middle of the view, on that land and on the hands. The Hand's point is 0.4 to 0.7 m away. A thing that covers a nearer thing gives two cues that do not agree. The eyes stay on the near thing, and they see the far text two times. With the eyes on the Hand, the two pictures of a subtitle at 4.10 m are 7.8 degrees apart. |
| The clamp | The adaptive rule follows the nearest world, and asks for up to 97 pixels at 100 units a meter, which is 0.34 m. That is too near to read, and the clamp of 24 pixels cannot put the text in front of the world. |
| The depth changes | The adaptive rule follows the nearest world, and the eye distance in world units follows the zoom. So the subtitles move between 1.37 m and 4.10 m in one session. |
| A low frame rate | Text in the eye pictures is fixed to the head. The runtime shows a late frame again, turned for the new head pose, so text that is fixed to the head steps against the head while the head turns [I]. |
| An icon in a line | A button icon in a subtitle draws through the camera help quads, so the hint rule takes it: the hint size, and no interface depth. |
| The first frame of a subtitle | The letters draw before the box that defines their band. In the left eye of that frame they have no band, so they are not moved to 35 degrees. In the right eye they are. |

The box (the rect queue caller) and its letters all go through `ShiftIn` with
one shift, left eye plus and right eye minus [V].

### 17.2 One card in the room

While the headset shows a projection layer and `subtitle_card` is 1, no help
text of the HelpText scope draws in an eye picture. This runs with
`ui_panel=0`, which is the default.

1. `UiPanelBeginFrame` latches the route for the frame (`SubtitleBeginFrame`):
   `UiPanelRouteFrame(kUiPanelSubtitles, on)`. Both eye passes agree.
2. In eye pass 0, `PanelTake` draws each batch of the HelpText scope and of
   the subtitle box into the panel target of section 2, at the pixels of the
   game, with no interface size and no depth. In eye pass 1 it draws them
   nowhere. This is the route of `ui_panel=1` for the help text only: the menus
   stay in the eye pictures. The hint beside the Hand and the tooltips go to
   the hint card.
3. After both eye passes (`UiDepthSetEye(-1)`), `SubtitleEndPasses` takes the
   bounds of what drew, with 8 pixels of room, on a grid of 16 pixels
   (`CardRect`). The copy of that rect at half the size is one `StretchRect`
   (linear, so each pixel is the mean of four) into a target that is only as
   large as the rect, on a grid of 128 pixels, and one `GetRenderTargetData`
   to system memory. A subtitle costs a copy of about 1 MB.
4. **The read ahead** (`subtitle_read_ahead=1`, the default). The GPU does the
   calls of the device in the order of its queue, so `GetRenderTargetData`
   waits until the GPU has done each call before it. After the eye passes that
   is both eye passes. With `frame_send` 1 and 2 the frame thread does not
   wait for the GPU at all, so that read would be a wait of the frame thread.
   So the copy goes into one of three slots, with an event query after it
   (`GpuBridgePanelQueueHalf`). At the start of a later frame, before the eye
   passes, `GpuBridgePanelFetchHalf` reads the newest slot whose query is done,
   when the query of the last frame end is done too. Then the GPU has no work
   left, and the read is only the copy. `GpuBridgePanelReadyHalf` gives those
   pixels and the rect that they hold, and the quad has that rect. So the card
   shows the help text of one or two frames before. A copy from before the
   frame in which the help text of now started does not show, so old text
   never comes back.
   With `subtitle_read_ahead=0`, `GpuBridgePanelReadHalf` reads the copy of
   the frame at once, and the frame thread waits for the GPU.
5. `XrScreen::StageCard(kCardSubtitles)` gives the pixels and the pose to
   `EndFrame`. That makes a swapchain of half the render size at the first
   subtitle, writes the rect with `UpdateSubresource`, and adds ONE quad layer
   in the LOCAL space. This is the path of the hint card.

A quad shows at one place of the room to both eyes, so the text cannot be
double. The runtime places the quad again for each displayed frame, so a slow
game frame does not move it against the room. The text is at half the size of
the render: the quad shows 1440 texels across 35 degrees, which is 1.7 texels
for each pixel of the headset, so the letters do not shimmer as they would at
the full size.

The card installs the hook of `ui_panel=1` on the rect queue callback
`0x0081E7D0`, which puts the game's target back after the subtitle box. The
binary has `call 0x81e3c0` and `ret 4` there [V], and `fn_0081E740` registers
it as a FinishFrame callback.

A picture of the camera help code inside the subtitle rows goes to the card
with its line, when it draws in the HelpText scope. The Creature panel is
drawn in the HelpText scope too, so it shows on the same screen, left of the
middle and near eye level.

### 17.3 The place

`subtitle_card_math.h`. The game's pixels show on a flat screen that hangs from
an anchor: a head position and a yaw.

- The row at 93 percent of the render height (the middle of the subtitle box)
  is 1.6 m from the anchor head, 17 degrees below eye level, straight ahead of
  the anchor yaw (`RectQuad`). The screen is square to that line of sight. The
  whole width of the render is 1 m, so a subtitle is 35 degrees wide and a
  letter of 77 pixels is 1 degree high.
- 17 degrees below eye level at 1.6 m is 47 cm below the head. So the card is
  above a land that is 0.6 m below the head, and in front of the land behind
  it. It is not in the place where the hands work.
- The quad of a frame is the rect that drew. A pixel has one place in the room
  in each rect, so a rect that grows moves no letter.
- **The anchor** (`AnchorStep`) starts at the recenter pose (`HeadPoseOrigin`),
  and each recenter puts it there again. The card does not follow the head: a
  head that turns, leans or looks down moves nothing.
- The head is away when it has turned more than 50 degrees from the anchor,
  moved more than 1 m along the floor, or more than 35 cm up or down. While the
  card shows, the anchor goes to the head only after the head was away for 1 s
  without a break. So the card is never lost behind the player. While no card
  shows, an away head takes the anchor at once, because nothing moves in the
  view.
- **The card never slides** (`ShownStep`). When the anchor changes while the
  card shows, the card fades out in 0.2 s at its old place, takes the new place
  while it is clear, and fades in. The yaw comes from the right axis of the
  head, so a head that looks straight down keeps its yaw.

### 17.4 The fallback

| Case | What the help text does |
| --- | --- |
| The headset with a projection layer | The card |
| The runtime refused the swapchain of the card or a frame with it (`CardFailed`), the panel target could not be made or bound, or the copy failed 3 times | The eye pictures, at one fixed depth of 0.75 m (`subtitle_fallback_cm`, 43 pixels in each eye at a 2880 px render). The depth does not follow the world, and it is in front of the land under the subtitles. The log says why. |
| No headset: the mouse player, and the desktop rig | The eye pictures at the interface depth |
| The stereoscopic window (a headset with no projection layer) | The interface is in its picture |
| A flat frame of the cut scene window | The card, in the plane of the screen under the scene (17.9). With no card there, the help text is in the picture of the window, on the screen plane. |
| `subtitle_card=0` | The eye pictures at the interface depth |
| `ui_panel=1` | The help panel of section 4 |

The fixed depth of 0.75 m is for the God view. In a flat frame of the cut scene
window the help text stays in the picture of the window.

### 17.5 Keys

| Key of `bwvr.ini` | Default | Meaning |
| --- | --- | --- |
| `subtitle_card` | 1 | 1 shows the subtitles and the help text on the card in the headset. 0 keeps them in the eye pictures |
| `subtitle_card_distance_cm` | 160 | The distance from the head to the subtitles, 60 to 400 |
| `subtitle_card_down_deg` | 17 | The angle of the subtitles below eye level, -20 to 60 |
| `subtitle_card_width_cm` | 100 | The width of the whole render on the card, 30 to 300. It sets the size of the letters |
| `subtitle_fallback_cm` | 75 | The fixed depth of the fallback, 30 to 400 |
| `subtitle_read_ahead` | 1 | 1 reads the copy of the card at the start of a later frame, when the GPU has done it, so the frame thread does not wait (17.2, step 4). 0 reads it after the eye passes of its frame, and the frame thread waits for the GPU |

Test keys: `subtitle_card_test=1` runs the route with no headset, for the desktop
rig. `subtitle_selftest=1` draws two lines of help text in the HelpText scope
with the game's own text code (`SubtitleSelfTest`), because a desktop run shows
no subtitle. With `test_keys=1` and `ui_panel=0`, Ctrl Shift F11 switches
between the cards and the eye pictures, for a live comparison. With
`test_keys=1`, Ctrl Shift Pause turns `subtitle_read_ahead` off and on.

### 17.6 The log

The tag `subtitles`:

- The keys at the start.
- `the subtitles and the help text now go to the subtitle card, and none draws
  in an eye picture`, or the reason for the fallback or for the old path, at
  each change.
- `frame N, the subtitle card: the rect x y w h of the 2880 by 2880 render,
  1440 by 152 px on the card, at x y z m, turn ..., 1.00 by 0.11 m, 1.60 m from
  the head and 17 degrees below eye level. The head has a yaw of 3 degrees and
  the card of 0. Fade 1.00, the copy took 0.21 ms`: each time the card comes
  into the view (80 lines at most), and on a capture frame.
- `the subtitle card hangs from the recenter pose` and `goes to a new place,
  because the head was far away from the old one`.
- On a capture frame: `bwvr-subtitle-card-NNNNN.bmp` (with its alpha) and
  `bwvr-subtitle-card-over-NNNNN.bmp` (over a checkerboard).
- Each report window: the frames with the route, the frames with a card, the
  moves, the time and the size of the copy, the frames with help text and no
  card, and the batches of the fallback. `ui panel` gives the batches that went
  to the target and the half size copies. With the read ahead it gives the
  copies in the queue, the reads, the frame starts with a copy that the GPU
  had not done, and the copies that were never read. The time of the copy is
  then the time of the lock and of the copy in the CPU, with no wait for the
  GPU. `XrScreen` counts the quad layers of the card.

### 17.7 Cost

| Item | Cost |
| --- | --- |
| Memory | The panel target and its depth at the render size (66 MiB at 2880 by 2880), the copy targets (1 to 2 MiB each for a subtitle), a buffer and a swapchain of half the render size (8 MiB, and 24 MiB for 3 images) [I] |
| A frame with a subtitle | One clear of the panel, one bind for each span, one `StretchRect`, one event query, one `GetRenderTargetData`, one copy and one `UpdateSubresource` of the rect. The log gives the time of the copy. |
| The wait for the GPU at the copy | With `subtitle_read_ahead=1`: none. With 0 and `frame_send` 1 or 2: the frame thread waits until the GPU has done both eye passes. Headset sessions with `frame_send=2`, `one_walk=1` and the card in each frame had 4 to 8 ms in each frame that no part of the frame report had, and 45 fps in views that held 90 fps with no card. With `frame_send=0` the frame thread waits for the GPU after the eye passes anyway |
| The copy, with no game [M] | The same Direct3D 9 calls on an RTX 3090: a subtitle strip of 2112 by 304 pixels takes 0.41 ms, the Creature panel 0.32 ms, and a rect that holds both 2.2 ms |
| A frame with no help text | Nothing: no clear, no copy, no layer |
| `subtitle_card=0` | Nothing |

### 17.8 Tests

The math test (`SubtitleCardChecks` in `hint_checks.h`) covers:

- The depths of 17.1: 24 pixels are 1.37 m and 8 pixels are 4.10 m, and the
  angle between the two pictures of a subtitle with the eyes on the Hand.
- The yaw of the head, also for a head that looks straight down.
- The anchor: a head that turns 49 degrees and leans 40 cm moves no card. A
  look of 0.9 s at 80 degrees moves no card. A head that stays at 80 degrees
  moves the card one time, after 1 s. A turn of 180 degrees, a walk of 1.2 m
  and a player who stands up each move it after 1 s. A recenter puts it at the
  new recenter pose.
- The card is at the old place or at the new one in each frame of a move, and
  it takes the new place while it is clear.
- The screen: the strip is 1.6 m away, 17 degrees down, 1 m wide and above the
  land. It faces the head. A pixel has one place in the room in a small rect
  and in a large one.
- The rect of the card, and the fixed shift of the fallback.

The desktop route runs with `subtitle_card_test=1` and `subtitle_selftest=1`:
the two test lines must be on the card and not in the eye pictures, and with
the card off they must be in the eye pictures.

**Not tested:** a real subtitle of the game in a desktop run. The place of the
game's subtitle box in the render is not measured.

**Needs the headset.**

- The subtitle card as a quad layer: its swapchain of 1440 by 1440 pixels, the
  alpha of the game's subtitle box over the world, and its colors.
- Whether the letters read well at 1 degree, and whether 17 degrees below eye
  level at 1.6 m is a good place while the hands work.
- Whether the row at 93 percent is the middle of the game's subtitle box.
- A move of the card after a turn of the head, with its fade.
- The time of the copy at 2880 by 2880, from the report line.
- The Creature panel on the card.

### 17.9 In the cut scene window

A flat frame of the cut scene window has the card too (`cutscene-window.md`,
section 7):

- `SubtitleBeginFrame` asks `CutsceneWindowSubtitles`. In a flat frame with
  `cutscene_subtitles=1` the mode is the card, with the same route: the
  HelpText scope and the subtitle box draw into the panel target in eye pass 0,
  and nowhere in eye pass 1.
- `SubtitleEndPasses` gives such a frame to `SubtitleWindowCard`. The card has
  no anchor of its own there and no fade. `CutsceneWindowSubtitleQuad` gives
  its pose: in the plane of the screen, at the yaw of the screen, with its top
  4 cm under the scene. The whole width of the render is
  `cutscene_subtitle_width_cm` (140) on the card.
- `SubtitleCopy` is the half size copy of 17.2, one function for the card of
  the room and the card of the window.
- `XrScreen::EndFrame` adds the card to the quad path of the window, as one
  quad layer over the two eye quads. If the runtime refuses that frame, the
  card is off (`CardFailed`) and the subtitles stay in the pictures.
- When the window is gone, the card has its place in the room again, and its
  anchor rule starts as for a new card.

The log has `the subtitle card hangs under the scene of the cut scene window,
and no subtitle draws in its picture` when the first flat frame takes the
route, and `the subtitle card under the cut scene window: the rect ...` with
the pose and the size each time the card comes into the view.

The math test has checks for the place (`window_checks.h`, "THE SUBTITLES OF
THE WINDOW").

## 18. A speech bubble as one card

### 18.1 How the game draws a bubble [V]

A bubble is a blue box with a tail and a text in it. Three things of the
game have one:

- A wooden signpost. It is a `ScriptHighlight` of the kind "did you know"
  (`ScriptHighlight::IsDidYouKnow` `0x0070AC20`: kind 1 of the table at
  `0x00D96390`). A tap on it (`ScriptHighlight::InterfaceTap` `0x0070AC70`)
  calls `HelpSystem::SetBubbleProperties` `0x005C8330`, which makes the
  signpost the object of the bubble of the help system (`HelpSystem + 0x20`).
  A tap on the same signpost takes the bubble away.
- A villager that speaks (`SpecialVillager::Draw`, call at `0x0071F439`).
- The Creature (`Creature::Draw`, call at `0x00517E78`).

One class draws all three, in three steps:

| Step | Function | What it does |
| --- | --- | --- |
| The layout | `fn_00576F20`. For a signpost `HelpSystem::Draw3D` calls it through `fn_005C83D0` (`0x005C8478`), in the scene walk | It gets the pixel of the sign. `across` is that pixel across the picture, from -1 at the left edge to 1 at the right edge. A sign in the right half gets the offset -10 pixels, and a sign in the left half +10. The anchor is `0.2 + 0.6 * abs(across)`: the part of the width of the box that is left of the sign |
| The place | `fn_00575EA0` (`__thiscall`: the point of the sign, the offset, the anchor, `ret 0xC`) | It takes the depth of the sign from the matrix with no field of view in it (`0x00EA1D28`). The size of the box goes from its full size at 3 units to one fourteenth at 450 units. The bubble fades in from 3 to 8 units and fades out from 300 to 450 units. The left edge of the box is the pixel of the sign, plus the offset, less the anchor times the width. The offset also gives the side of the tail |
| The draw | `0x005767D0` (the bubble in `ecx`), later in the frame: `fn_00576E90` gives the Z sort an object for it (`LH3DZSorter::NewZObject`) | `fn_005766E0` gives the depth of the bubble: the depth of the sign less 5 units, and less again while the camera looks down (times `1 - (2 * pitch / pi)^2`), and two times the near plane at the least. `GatheringText::DrawBubble` `0x008334C0` draws the box and the tail. `GatheringText::DrawText` draws the text through `LH3DTech::Draw3DScreenTriangle`. `SetupThing::DrawBox` draws two arrows when the text is longer than the box, and `fn_005760C0` draws an icon of the text |

`DrawBubble` does not draw the box at its pixels. The box has 16 corners
and the tail has 4. For each corner `DrawBubble` asks
`LH3DTech::Get3DPointFromScreen` `0x0081B370` for a point of the world: the
point at that pixel, at the depth of the bubble. Then it draws those points
through `LH3DTech::Draw3DWorldTriangle` `0x0081C090`, which ends in
`LH3DRender::DrawTriangle`. `Get3DPointFromScreen` makes the point from the
camera point of the game (`0x00EA1DB8`).

### 18.2 What two eye passes do to it

Each step runs in each eye pass, with the camera of the eye. Three things
then put the parts of the bubble at three depths:

1. **The layout.** The pixel of the sign differs between the eyes by the eye
   difference of the sign. So `across` differs, and the two passes can get a
   different offset, a different side of the tail and a different anchor. A
   sign that the player reads is in the middle of the view, which is where
   the offset changes its sign.
2. **The box.** The camera point of the game does not move with the eye
   (`stereo-artifacts.md`, "Moving the camera point with the eye"). So a
   corner that `Get3DPointFromScreen` made comes back at a different pixel
   when the camera of the eye draws it. The box gets the eye difference of
   the depth of the bubble, on top of the eye difference of its pixels. At
   100 units for each meter, a bubble 34 units deep moves 98 pixels in each
   eye picture of 2880 pixels.
3. **The text.** The text code draws the text at its pixels. `draw_hooks.cpp`
   gives each flat batch of that code the interface depth: 8 to 24 pixels
   in each eye.

From far each of the three is small. From near the box is in front of the
text, and both are in front of the sign.

### 18.3 What the mod does (`bubble_card=1`)

The bubble is one flat card at the depth of the sign (`bubble_card.cpp`,
`bubble_card_math.h`):

- **One layout for a frame.** A hook on the place function keeps the layout
  that the game asks for in the first eye pass of a frame. Each later ask of
  that frame gets the same one (`Choose`). The box of the right eye is then
  the box of the left eye, moved by the eye difference of the sign.
- **The side near the middle.** The middle band is 6 percent of the half
  width of the picture, to each side. While the sign is in that band, the
  offset of the frame before stays. So a small turn of the head does not
  change the side of the tail.
- **The box at its pixels.** A hook on the draw of the bubble marks its
  batches. In `h_DrawTriangle` each corner of the box goes back to the pixel
  that the game made it from (`CornerToItsPixel`). The mod has the camera
  point of the game in the camera space of the eye pass. It takes that
  point away from the corner and projects the corner again. The Z value of
  the corner stays, so the land near the sign does not cut the card, as in
  the plain game.
- **The text at its pixels.** `DrawInterface` draws each flat batch of a
  bubble as the game made it: the text, the arrows and the icon. Such a
  batch gets no interface depth and no interface size.

With `bubble_card=0` the mod changes nothing of the bubble.

### 18.4 Keys, the test key and the log

| Key | Default | Meaning |
| --- | --- | --- |
| `bubble_card` | 1 | 1: a speech bubble of the game is one flat card at the depth of its sign. 0: the bubble is drawn as the game and the interface depth make it in each eye pass |
| `bubble_card_test` | 0 | A test key of the desktop runs. 1: one line of the log each 30 frames while a bubble is in view |
| `bubble_card_test_frame` | 0 | A test key of the desktop runs: the frame at which `bubble_card` changes, as the test key does |

With `test_keys=1`, Shift and Delete change `bubble_card` in play. The
pointing hand gets a long pulse for 1 and a short pulse for 0.

The log has the tag `bubble card`. One line comes when a bubble comes into
view. It has these numbers:

- the depth of the sign, and the pixel of the sign in each eye picture
- the layout that the game asks for in each pass, and the layout that each
  pass gets
- the box of the game, the box as drawn and the flat parts as drawn, each
  with how far apart the two eyes have it

Each report has the counts.

### 18.5 Tests

The math test (`bubble_card_checks.h`) has the rule of the game for the
layout, and it shows that a near sign in the middle of the view gets a
different side in each eye pass. It checks `Choose`: the first frame, the
side near the middle, and the side far from the middle. For the box it has
`Get3DPointFromScreen` as its listing has it. With eyes 6.6 units apart and
a bubble 34 units deep, the game draws the box 97.9 pixels to the side in
each eye picture. `CornerToItsPixel` puts each corner back within 0.01
pixels, also for a camera of an eye with its own turn and a roll.

No desktop run has shown the change in the game. A run needs a tap on a
signpost. `bwvr/tools/desk/hand_run.ps1 -Kind fixed -Save temple` can make
one, with these test keys in `-Set`:

- `seek_plan=go,scrolls` and `seek_go=<x>,<z>`. The first word `seek` of
  `-Steps` puts the head over that place of the land. The second one writes
  each scroll in view to the log and puts the Hand's point on the nearest
  one. The log names a signpost a silver scroll. A `grip` after that taps
  it.
- `fake_head_pitch=-50`, so the land near the head is in the pictures.
- `bubble_card_test=1`, and `bubble_card_test_frame=<frame>` for the old
  value in the same run. Each line `bubble card` of the log then has the
  pixel of the sign in each eye, and how far apart the two eyes have the
  box and the text. With 1 the three numbers must be the same. A word
  `shot` after each line saves the two eye pictures.

No run has made this recipe yet: the place of a signpost in the test save
is not known.

### 18.6 Limits

- The name of a villager (`VillagerName::Draw` `0x00762DC0`) draws its own
  box through `DrawBubble`, with no bubble object. The mod does not change
  it. Its box has the eye difference of its depth on top of the eye
  difference of its pixels, and its text has the interface depth.
- The size of a bubble comes from the depth of the sign in units (full size
  at 3 units, gone at 450 units). The mod does not change that for the scale
  of the God view.
- `fn_00576F20` tests the pointer against the box in each eye pass
  (`0x005772AE` to `0x005773A0`). The pointer in the box keeps the bubble in
  view, and a drag moves a long text. No run has tested that with the box of
  two eye passes.
- `Draw3DWorldTriangle` clips the box at the edge of the picture before the
  corners go to their pixels. So a box at the edge of an eye picture can
  lose a strip, as wide as the move of its corners.
