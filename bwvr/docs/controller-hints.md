# Controller hints: findings and the design for M8

Research for step M8 of `docs/roadmap.md`. Stages H0, H1 and H2 are built
(section 8, "As built"). H3 to H5 are not. This file records how the game
draws its mouse and key hints, what the mod can change, what art is possible,
and a staged design. Since 2026-10-03 the hints of a controller show on one
card beside the wrist, with prompts for the controller (section 9, "The hint
card").

Asked for by Buck on 2026-09-29: with motion controllers on, turn off the
vanilla mouse hints and show hints for the controller. Best case: draw the
real controller and light the button, as modern VR games do. Fallback: a hint
picture per controller type. He tests with a Quest 3, and many other
controllers exist.

Marks: **[V]** verified in this session (disassembly, symbols, the game
files or the SDK headers). **[I]** inferred, not yet seen at run time.
**[?]** unknown, needs a test.

All addresses are for `runblack.exe` v1.42, image base `0x00400000`. Nobody
ran the game for this research.

## Summary

- One function, `CameraHelp::DrawKeyOrMouse` `0x00447EA0`, draws every hint
  that has words. It has three looks: a mouse picture with a lit button, a
  key cap with text inside, or plain text with no picture [V].
- The mouse picture is one 256 by 256 texture, `data\textures\mousehelp.raw`,
  cut into 16 cells of 64 by 64. The game builds its material once, from a
  path string that sits in writable memory [V]. The mod can point that path,
  or the cached material, at its own art without touching the game folder [I].
- The game asks the control map which input an action uses, through
  `fn_005C78F0` `0x005C78F0`. So the hint follows the player's own bindings.
  This one function is the place to say "the controller" instead of "the
  mouse" [V].
- The key cap look needs no art. Any wide string goes into the cap [V]. So the
  first useful step (H1) shows "Trigger", "Grip" and so on with zero new art.
- The help text says "Action Button" 129 times and "left mouse" or "right
  mouse" zero times [V]. So the words mostly need no change. The icon does the
  work. Only about 20 lines need new words (wheel, keyboard, Escape) [V].
- Realistic now: key caps (H1), a new word list (H2), and an own icon atlas
  with flat glyphs (H3). Next: a small controller picture with the button lit,
  per controller family (H4). Later: the runtime's own 3D model (H5). Whether
  Buck's runtime offers such a model is unknown [?]. Step H0 finds out.

## 1. How the hint icons work

### 1.1 The draw function

`CameraHelp::DrawKeyOrMouse` is `__cdecl` with 11 arguments. The symbol gives
the types: `(CH_ANIMTYPE, int, int, wchar_t*, int, int, int, KEYALIGN,
LH3DColor*, LH3DColor*, int)` [V, `symbols.txt`]. The disassembly shows what
each one does. The mod (`draw_hooks.cpp`) names them `icon`, `unknown` and
`icon_type`. Those names are wrong, and this file uses the true meaning.

| Arg | Mod name | True meaning | Evidence |
| --- | --- | --- | --- |
| 1 | `icon` | Blink style. `-1` means "draw a key cap". Otherwise it goes to `fn_00447450` as its third argument, where `1`, `2` and `3` pick a blink pattern from `GetTickCount` and any other value is steady. | `0x00448011` compares it to `-1`. `0x00447486` to `0x004474EC` is the pattern switch [V] |
| 2 | `unknown` | The mouse button code (section 1.2). In key cap mode it is a key code with modifier bits, or `0` to use the string in arg 3. | `0x00448019` to `0x0044807A`, and the call `fn_00447450(arg2, arg3, arg1, ...)` at `0x0044859E` [V] |
| 3 | `icon_type` | The row of the mouse picture: `0` two buttons, `1` three buttons, `2` wheel mouse. `3` means "no picture, text only". In key cap mode it is a pointer to the key name. | `fn_00447450` exits at `cmp ebx, 3` (`0x00447455`). `0x00448122` sets the icon width to 0 when it is 3. `0x0044802A` to `0x004480EB` copies arg 3 with `wcscpy` in key mode [V] |
| 4 | text | The caption, beside the picture or cap. | `wcslen` and `GetStringWidth` at `0x004481DC` [V] |
| 5 to 7 | x, y, size | Position and size in render pixels. | [V] |
| 8 | flags | `KEYALIGN`. Bits `0x10` and `0x20` pick the alignment. `0x400` and `0x800` shift the box. `0x100` and `0x200` add extra frames. | `0x0044813F` to `0x00448187`, `0x004484C3` to `0x00448544` [V] |
| 9, 10, 11 | colors, alpha | Text color, box color, alpha. | [V] |

The hint box is built from `SetupThing::DrawBox` `0x00412980` calls with the
front end texture `0x00EDC368` (frame pieces). The mouse picture is drawn by
`fn_00447450`. The key cap is drawn by `fn_00447990`, which makes three
`DrawBox` calls (left end, middle, right end) and the text [V].

**The three looks.**

1. Mouse picture and caption: arg 1 is not `-1` and arg 3 is `0`, `1` or `2`.
2. Key cap: arg 1 is `-1`. The label is arg 3 (a string). The cap is drawn at
   `0x0044857B` [V].
3. Text only: arg 3 is `3`. This is the tooltip look the mod already knows.

The key cap copies the string with `wcscpy` into a stack buffer of about 118
wide characters (`0x004480EB`, buffer at `esp+0xD4` in a `0x1C0` frame)
[V for the copy, I for the size]. A label must stay short.

### 1.2 The button codes

`ControlMap::GetClickTypeForAction` `0x00471620` turns the binding of an
action into a button code [V]:

| Binding kind in the control map | Code returned | Meaning |
| --- | --- | --- |
| 1 | `0x01` | Left button [I] |
| 2 | `0x02` | Right button [I] |
| 3 | `0x04` | Middle button [I] |
| 4 | `0x10` | Wheel, one direction [I] |
| 5 | `0x08` | Wheel, the other direction [I] |
| a key | `-1` | Not a mouse binding. The key name goes to arg 3. |

The picture code, `fn_00447450` `0x00447450`, reads the code and picks a cell
of the atlas. The jump table is at `0x00447818` and the index bytes are at
`0x00447838` [V]:

| Code | Cell (frame `f`, then `f + 4 * row`) | Flip | Looks like |
| --- | --- | --- | --- |
| `0` | frame 0 | no | plain mouse |
| `1` | frame 1 | yes (`u` is mirrored, `0x00447611`) | left button lit [I, from the mirror] |
| `2` | frame 1 | no | right button lit |
| `3` | frame 2 | no | both buttons lit |
| `4` | frame 3 | no | middle button lit (three button and wheel rows) |
| `8`, `0x10` | absolute cells 15 down to 12, then 11 | no | wheel scrolling, two directions |
| `5`, `6`, `7`, `9` to `15` | as `1` | yes | not used by the control map [I] |

The blink style makes the frame flip between the lit cell and frame 0 (`edi`
is zero, `esi` becomes 0). Any other blink style keeps the lit cell.

### 1.3 The texture

`fn_00447450` builds its material on first use. It pushes the string at
`0x009CDF8C`, which is `data\textures\mousehelp.raw`, calls the loader
`fn_008379E0`, then `LH3DRender::CreateMaterial(5, ...)`. The material is
cached at `0x00C5A318` [V, `0x0044757F` to `0x004475A3`, and the string read
from the exe]. The string is in `.data`, which is writable [V, section flags
`0xC0000040`].

The files in `Data\Textures` are `mousehelp.raw` (196608 bytes) and
`mousehelpa.raw` (65536 bytes). That is 256 by 256 in BGR and a 256 by 256
alpha plane [V]. I converted both to a picture and looked at it. The atlas is
a 4 by 4 grid:

| Row | Cells (left to right) |
| --- | --- |
| 0 | Two button mouse: plain, right lit, both lit, both lit |
| 1 | Three button mouse: plain, right lit, both lit, middle lit |
| 2 | Wheel mouse: plain, right lit, both lit, wheel lit (this is cell 11) |
| 3 | Wheel mouse with the wheel scrolling, four frames (cells 12 to 15) |

The UV code uses steps of `0.25` [V, `0x004475D0`]. So a cell is 64 by 64
pixels.

**Which mouse row the game picks.** `fn_005C4800` `0x005C4800` returns 2 when
the global `0x00E85248` is set (a wheel mouse), else 1 or 0 by a second
global (`0x00E8524C`, the button count) [V]. It runs on the player's real
hardware.

### 1.4 Where the values come from

`fn_005C78F0` `0x005C78F0` is the choke point. It takes an action number and
fills three outputs [V]:

- If the action is bound to a mouse button: arg 2 is `0`, arg 3 is the button
  code, and arg 4 is the mouse row.
- If the action is bound to a key: arg 2 is `-1`, arg 3 is `0`, and arg 4 is
  a pointer to the key name (a wide string at `0x00D16110`).

Its six callers [V, `--callers`]:

| Caller | What it makes |
| --- | --- |
| `ToolTips::SubmitToolTips` `0x005C9A70` (at `0x005C9BC4`) | The tooltip beside the Hand for an action. The node goes into a list of `GKMIcon` records. |
| `GInterface::CalculateOverObject` `0x005C9FC0` (at `0x005CA07C`) | The tooltip for the object under the Hand. |
| `HelpSystem::Draw3D` `0x005C59A0` (at `0x005C5A45`) | Tooltips when the help system draws in the 3D pass. |
| unnamed, `0x005C5D63` | The same, in the help callback. |
| `fn_00457041` (at `0x004571AD`, the draw at `0x0045729D`) | A hint from that function. The purpose is not known [?]. |
| `fn_005760C0` (at `0x0057622C`, the draw at `0x005763CC`) | The icon inside a speech bubble. |

**The speech bubble tokens.** `fn_005760C0` reads two tokens in the help
text: `$m<number>` and `$g<number>`. `$m` asks `fn_005C78F0` for the action
and draws the icon with `DrawKeyOrMouse` (arg 4 is the buffer at
`0x00C4CD30`). `$g` draws a gesture glyph through `fn_005763E0` [V, the
`0x24`, `0x6D` and `0x67` compares at `0x005760C7` to `0x005761F6`].

### 1.5 Every caller that draws a hint

The direct callers of the two draw functions [V, `--callers`]:

| Function | Caller | Address | What it is |
| --- | --- | --- | --- |
| `DrawKeyOrMouse` | `fn_00448AC0` | `0x00448BD4` | One `GKMIcon` tooltip node. `fn_00447850` walks the list at `0x00C5AFE0` and calls it for each node [V]. |
| `DrawKeyOrMouse` | `fn_00457041` | `0x0045729D` | See above [?]. |
| `DrawKeyOrMouse` | `fn_005760C0` | `0x005763CC` | The bubble icon. |
| `fn_00447450` | `DrawKeyOrMouse` | `0x0044859E` | The picture inside every hint above. |
| `fn_00447450` | `fn_00447850` | `0x004478CD` | The screen edge hint: a picture with no text. Its button code, row, blink flag, position and fade are globals at `0x00C5AFBC` to `0x00C5AFDC`. Only `fn_00447850` reads them [V]. What writes them was not traced [?]. |
| `fn_00447450` | unnamed | `0x005129BB` | The controls page of the options. Each row shows the bound button picture [V by the mod's own comment in `draw_hooks.cpp`, line 1137]. |

`fn_00447850` itself has these callers [V]: `HelpSystem::Draw3D`
(`0x005C5B31`), the help callback (`0x005CCE41`), `FrontEnd::JustDoABox`
(`0x005451A4`) and an unnamed one (`0x0053CDBB`).

**Producers of a `GKMIcon` node.** `fn_004489D0` adds a node and
`??_GKMIcon` `0x00448C00` removes it. The adders are `HelpSystem::Draw3D`,
`fn_005C5C5C`, `fn_005C9D00` and one unnamed caller (`0x005C5E37`) [V]. The
removers include `HelpSystem::ClearTextDisplayed`, `ResetIcons` and
`ProcessInterface` [V]. So the help system owns the hint list.

### 1.6 What the mod does with hints today

From `draw_hooks.cpp` [V, read in this session]:

- It hooks `DrawKeyOrMouse` (`h_DrawKeyOrMouse`, line 1294).
- A call with a mouse picture (`icon_type != 3`) goes to the game unchanged.
  The game draws each such hint twice per pass, once projected and once
  pinned. A separate rule pairs the two quads from `fn_00447450` by image
  and drops one.
- A text only call (`icon_type == 3`) gets a key from its text, icon and type.
  The first copy per pass is kept. The second is skipped. The kept copy is
  drawn flat, at the Hand, at the interface size.
- A call from the bubble return address `0x005763D1` is left alone.
- A call inside a menu is left alone.

**A key cap does not fit this today.** The key cap goes through `DrawBox`,
not through `fn_00447450`, and arg 3 is a pointer, not `3`. So the mouse
picture pairing rule never sees it, and it fails the `icon_type == 3` test.
In stereo a cap could be drawn twice [I]. H1 must route a converted hint
through the tooltip path (section 5, H1).

### 1.6.1 The hints that matter in play

The help text names the action, not the device. The counts below are from
`Scripts\InfoScript2.txt` (6974 `ADD_TEXT` lines) [V]:

| Marker | Lines | Use in play [I where marked] |
| --- | --- | --- |
| `$m2` | 134 | Most hints: "Click the Action Button on ...". Pick up, drop, throw, drag, cast, stroke, leash, open a door, activate a scroll. |
| `$m1` | 25 | Camera and Hand hints: "rotate by holding the sides of the screen and moving the Hand", "pitch up and down", "double-click on the land to go straight there". Which binding action 1 is stays unknown [?]. |
| `$m4` | 10 | Zoom with the wheel. |
| `$m6` to `$m10`, `$m15` | 3 to 5 each | Keyboard move, rotate, pitch and zoom keys (5 lines). |
| `$m17`, `$m20`, `$m21`, `$m24`, `$m26`, `$m27` | 1 to 4 | Single keys: the Temple key, Space bar, and others. |
| `$g1` | 2 | A gesture glyph. |

The play actions and what a controller does for each. The controller side is
from `docs/controllers.md` (M2) and can still change while M3 is in progress:

| Player action | Game hint | Controller |
| --- | --- | --- |
| Pick up, hold | Action Button | Trigger, hold |
| Drop, throw | Release the Action Button | Release the trigger |
| Drag the land | Action Button on land | Trigger, hold |
| Cast a miracle | Action Button | Trigger, then the gesture |
| Stroke or slap the Creature | Action Button | Trigger (M5 adds motion) |
| Jump, fly to a place, leash | Double-click | Trigger twice, or A or X |
| Zoom | Wheel | Stick forward and back |
| Turn | Hold the screen edge, or right button | Stick left and right (a snap turn) |
| Tilt | Shift and a drag, or the top edge | Not mapped |
| Open the menu | Escape key | Not mapped (the menu button is free) |

**Hints for an action the controller cannot do must not show.** The tilt hint
and the keyboard hints are the clear cases (H1 hides them).

## 2. How to swap a mouse icon for a controller icon

Four ways, from the cheapest.

**A. Change the words only.** Edit the help text at load time. This cannot
change the mouse picture, because the picture comes from the binding, not
from the text. It only helps where words name a device (section 4).

**B. Convert the hint to a key cap at draw time.** In the hook of
`DrawKeyOrMouse`, when controllers are in use and the button code is one of
`1`, `2`, `3`, `4`, `8`, `0x10`, call the original with arg 1 `-1`, arg 2 `0`
and arg 3 a pointer to a mod string such as `Trigger`. The game draws its own
cap and text. No art. Effort: small. Look: text in a cap. Covers hints that
go through `DrawKeyOrMouse` [V]. It does not cover the screen edge hint from
`fn_00447850`, which has no text (it goes through `fn_00447450` only).

**C. Swap the picture in the game's own atlas.** Give the game a different
`mousehelp` material. There are two ways [I]:

1. Change the operand of `push 0x009CDF8C` at `0x0044757F` to a string in the
   mod, for example `bwvr\hints\touch.raw`. Change it before the first icon
   draw. The loader then reads the mod file. A second file with an `a` suffix
   holds the alpha, as `mousehelpa.raw` does (the alpha name is [I], from the
   file pair).
2. After the game builds the material, replace the pointer at `0x00C5A318`
   with a material the mod built the same way. This lets the mod switch atlas
   at any time, for example when the player picks up the other controller.

Do not overwrite `Data\Textures\mousehelp.raw`. It is the player's game
folder. The mod must leave it alone.

The game picks a cell from a button code and a row. With code `0`, `2`, `3`
or `4` and rows 0 to 2 the mod can reach cells 0 to 11 with no flip. The
codes `8` and `0x10` reach cells 11 to 15 [V]. That is 17 cells the mod can
fill with any picture. To pick a cell the mod rewrites the arguments of
`fn_00447450`, or of `DrawKeyOrMouse`, in a hook. Code `1` always mirrors
cell 1, so the mod must not send `1` to the game. It sends `0`, `2`, `3` or
`4` and puts trigger art in the chosen cell.

Cost: one fixed 64 by 64 cell. Covers everything that reaches `fn_00447450`,
including the screen edge hint and the options page [V]. It keeps the game's
blink and its box.

**D. Draw the mod's own picture.** Skip the game's icon and call
`SetupThing::DrawBox` `0x00412980` with a material the mod made [V]. Its
symbol is `DrawBox(int x0, int y0, int x1, int y1, float u0, float v0, float
u1, float v1, LH3DMaterial*, LH3DColor*, int, int, int, bool, float)`. The
callers in `fn_00447990` show a normal call: `100.0f`, `0`, `0xA000`,
`0xFFFF6000`, `0`, the color pointer, the material, four floats, four ints
[V]. The last five arguments are not decoded [?]. Advantage: any size, any
aspect, any UV, a flip for the left hand, and art that is not tied to 64 by
64 cells. It also needs the mod to place the picture by hand, next to the
game's box, and to repeat the two pass and flat rules of `draw_hooks.cpp`.

**What the mod can draw by itself.**

- Text in a cap (B). The game does it.
- The atlas cells (C). The game does it with the mod's art.
- A textured box through `DrawBox` (D). The game's own call, with a material
  the mod builds. It needs the same recipe as `fn_00447450`: the loader
  `fn_008379E0` and `CreateMaterial(5, ...)` [V for the recipe, I that the mod
  can call them at the right time].
- After the interface moves to an OpenXR layer (`docs/ui-layer.md`), the mod
  can also draw on its own layer from the D3D11 side, with no game call.

**Recommendation.** Use B first because it needs no art. Then C, because it
keeps every caller and the game's placement. Use D only for a large picture
or a left and right mirror.

## 3. Controller art

The mod must not ship art it may not ship. Three levels.

### (a) Flat icons per profile

A trigger shape, a grip shape, a stick, and a round button for A and B. The
mod draws them from art it owns.

- Effort: small. About 12 cells for a 64 by 64 atlas (trigger, grip, both,
  stick click, stick up, stick down, stick left, stick right, primary face
  button, secondary face button, menu, idle). Draw them once as vector art.
  One atlas covers all controllers that share a layout, and one small atlas
  covers the rest.
- Look: clear and readable at a small size. Not tied to the real controller.
- Legal: safe when the art is original. The controller shapes must be generic.
  Do not copy the logo, the ring or the exact outline of a real product.
  Do not copy icons from Meta, Valve, HTC or Microsoft brand kits. Check the
  license of any public set before use. Kenney's Input Prompts pack is CC0 as
  far as I recall [I, from memory]. It is for game pads, not VR controllers,
  so it may not help.

### (b) A small picture of the controller with the button lit

A silhouette of the controller family with the used button in a bright color.
This is the "modern VR" look at a small size.

- Effort: medium. One silhouette per family and one lit variant per control.
  Six or seven variants for four families. Draw them with a script from
  primitives (rounded rectangles and circles) so the art is code, not a
  scan. About 1 to 2 days for the script and 1 day for tuning.
- Look: good. The player sees which control to press, not only its name.
  Each cell must stay clear at 64 by 64. Test at the small hint size the
  mod uses today (`hint_scale_pct`, 20 percent by default).
- Legal: safe with own generic silhouettes. It is not a copy of a product.
  Left and right hands mirror one another on most controllers, so one picture
  serves both when the mod can flip the UV (needs D, or two cells).

### (c) A 3D model of the controller in the player's hand

The real controller model, drawn at the real controller pose, with the button
lit and a label. The runtime can supply the model.

**What the SDK offers** [V, `third_party/OpenXR-SDK`, version 1.1.43]:

- `XR_MSFT_controller_model` (`xrGetControllerModelKeyMSFT`,
  `xrLoadControllerModelMSFT`, `xrGetControllerModelStateMSFT`). The runtime
  gives a binary glTF and per node poses that move the button and stick
  nodes.
- `XR_FB_render_model` (`xrGetRenderModelPropertiesFB`,
  `xrLoadRenderModelFB`). The runtime gives a glTF for a path such as a
  controller.

**What a runtime supports** [I, from memory, not tested here]:

- `XR_MSFT_controller_model` is a Windows Mixed Reality feature.
- `XR_FB_render_model` is a Meta feature. It may be present on the Meta Link
  runtime.
- Virtual Desktop's 32 bit OpenXR runtime is the one Buck uses. Which of
  these extensions it offers is **[?]**. `xr_screen.cpp` asks for only
  `XR_KHR_D3D11_ENABLE_EXTENSION_NAME` today [V]. H0 must log the extensions
  the runtime lists, before any 3D work.
- The models the runtime returns may use compressed textures (KTX2 with
  Basis) [I]. A viewer needs a glTF reader (tinygltf or cgltf, both MIT) and
  a Basis transcoder (Apache 2.0) [I, from memory].

**Legal.** The runtime hands the model to the app at run time. The mod does
not ship it and must not save it into the mod folder or the repository. That
is the same position as any VR app that shows the controller. If a runtime
offers nothing, do not ship a copy of a maker's model. Options that stay
legal: a generic controller mesh the author makes, or the silhouette art from
(b).

**Other cost.** The mod needs a small glTF renderer on the D3D11 side
(`xr_screen`), a place in the frame for it, and a link from the game hook to
the XR side ("hint active: this button"). It also needs a model per hand and
node names for each button, which differ by maker.

**Realistic now:** (a) and (b). **Later:** (c), if H0 shows a runtime that
supplies a model. The hint can then use the real model where it exists and
fall back to (b).

**Controller families.** Use the profile string from `XrScreen::HandProfile`.
Group them so the art count stays small [I, the mapping is from the
profile names]:

| Family | Profiles | Layout |
| --- | --- | --- |
| Touch like | `oculus/touch_controller` and newer Meta touch profiles, Pico | Trigger, grip, thumbstick, two face buttons, menu (left hand) |
| Index | `valve/index_controller` | Trigger, grip, thumbstick, trackpad, A and B |
| Wand | `htc/vive_controller` | Trigger, grip, trackpad, menu. No thumbstick. |
| Mixed reality | `microsoft/motion_controller` and other Windows Mixed Reality makers | Trigger, grip, thumbstick, trackpad, menu |
| Simple | `khr/simple_controller` | Select and menu only |

An unknown profile uses the Touch like art, with a generic label set.

## 4. Where the words matter

### 4.1 What the text says

`Scripts\InfoScript2.txt` is UTF-16, 6974 `ADD_TEXT` lines [V]. Counts by
phrase:

| Phrase | Lines | Meaning for controllers |
| --- | --- | --- |
| "Action Button" | 129 | Correct as it stands. The icon shows the trigger. |
| "click", "clicking" | 91 | Understood with a trigger. Keep. |
| "left mouse", "right mouse", "left click", "right click" | 0 | Nothing to fix. |
| "mouse wheel" or "wheel" | 8 | Say "stick". |
| "mouse" | 32 | Most are banter ("I can see your mouse from here"). Keep. |
| "keyboard", "key", "Escape", "Space Bar" | about 28 | Hide or reword (see below). |

Fixed words for the options page [V, these keys exist]:
`HELP_TEXT_ACTIONS_LEFTMOUSEBUTTON` is "LMB", `..._RIGHTMOUSEBUTTON` is
"RMB ", `..._MIDDLEMOUSEBUTTON` is "MMB", `..._WHEEL_UP` is "Wheel Up" and
`..._WHEEL_DOWN` is "Wheel Down". These are the names of the five mouse
inputs in the controls page. They map one to one to controller inputs.

### 4.2 Voice lines

Most narrator lines have a spoken voice, tied to the text by the key name
(`docs/input-devices.md`). The voice says "Action Button". If the subtitle
says "trigger", the eye and the ear disagree. So the design keeps every
spoken line as it is. It does not replace "Action Button". It changes the
icon beside it.

### 4.3 How to change text without shipping Lionhead's text

The game reads the help text at load. `GSetup::LoadTextScripts` `0x00719280`
calls `LHScriptX::Load` with the path `scripts/InfoScript2.txt` and a handler
at `0x007192E0` [V]. The handler reads each `ADD_TEXT` line and calls a
method of `HelpTextDataBase` (object `0x00D17CA8`) at `0x005CCF80`, which
calls `0x005CAD00` [V]. That method stores the text [I].

The mod hooks `0x005CCF80` (`__thiscall`, `ret 0x14`, five stack arguments)
[V for the shape, I for which argument is the text]. When controllers are in
use it does two things to the wide string before the game stores it:

1. An override table. It holds a replacement sentence for a small set of
   keys, for example the five mouse names above. The mod ships only its own
   sentences and the key names (identifiers). It never ships a Lionhead
   sentence.
2. A short phrase table for the rest: "mouse wheel" to "stick" and "the
   wheel" to "the stick". The mod ships the search phrase (two words) and
   its own replacement.

Everything happens in memory on the player's own file. Nothing is written to
disk. This matches the note in `docs/input-devices.md` ("edit entries of the
user's own `InfoScript2.txt` at load time").

The text must be applied again when the player changes the controller family
in a session. The database is filled once at load. So the decision must be
made at load, from the profile at that time. Options: keep both versions and
switch at read time (better), or reload the scripts (risky). The hook at
`0x005CCF80` can store the two strings in a side table by key, and a second
hook at the reader can pick one [I]. Read `HelpTextDataBase` `0x005CAD00`
first [?].

**Keyboard lines.** Five lines say "You can also use the keyboard" with
`$m7` to `$m10` (`HELP_TEXT_HAND_DEMO_USE_KEYBOARD_01` to `_04` and
`HELP_TEXT_TUTORIAL_LAND_108`) [V]. With controllers the mod cannot honor
them. Replace them with a short controller sentence such as "Push the stick
to zoom and turn." The Escape lines (`TUTORIAL_LAND_09`, `_10`, `_40`, `_106`,
`CITADEL_GUIDE_13`) say "press the Escape key". If the menu button gets a
job, say so there. If not, leave them. Do not promise a button the mod does
not map.

## 5. Staged design

The stages go from the first useful result to the best result. H0 to H2
need no new art.

### H0. Probe and census (no visible change)

**Goal.** Prove the inventory in section 1 on the real game, and find out
what the runtime offers.

- Hook `fn_005C78F0` and `fn_00447450` (a read only hook) and log each
  distinct tuple once: caller return address, arg values, and the text. Add a
  count. This shows which action numbers, button codes, rows and captions
  the game really uses in a play session. It closes the three unknowns: what
  `$m1` is, what `fn_00457041` draws, and who fills the screen edge globals.
- Rename the three arguments in `draw_hooks.cpp` to `blink`, `button` and
  `row_or_label`.
- Log the OpenXR instance extensions the runtime lists (Virtual Desktop) and
  the interaction profile of each hand. `xr_screen.cpp` already knows the
  profile. Add `xrEnumerateInstanceExtensionProperties` to the log.
- Add the setting `hint_style`: `game` (today), `caps`, `icons`. Add a hotkey
  to cycle it, as in the rest of the mod, so Buck can A and B it live.

**Hooks.** `fn_005C78F0` `0x005C78F0`, `fn_00447450` `0x00447450`, existing
`h_DrawKeyOrMouse`.

**Desktop test.** Yes. Play with the mouse and read the census log. It needs
no headset.

**Risk.** Low. Read only.

### H1. Key caps with words (first useful step)

**Goal.** With controllers in use, every hint that has words shows the
controller control in a key cap, and hints for actions the controller cannot
do disappear.

- In `h_DrawKeyOrMouse`, when the controller state says "in use" and the
  button code (arg 2) is `1`, `2`, `3`, `4`, `8` or `0x10` and arg 1 is not
  `-1`: call the original with arg 1 `-1`, arg 2 `0` and arg 3 a pointer to a
  static wide string from a table by profile and code. **Convert first, then
  run the existing tooltip path.** The dedup key, the once per pass rule and
  the flat placement need the converted call to count as a text only call.
  Otherwise the two per pass copies double in stereo (section 1.6).
- Label table (words for the hint, short, at most 12 characters):

  | Code | Touch like | Index | Wand | Mixed reality |
  | --- | --- | --- | --- | --- |
  | `1` (Action Button) | Trigger | Trigger | Trigger | Trigger |
  | `2` | Grip | Grip | Grip | Grip |
  | `3` | Trigger + Grip | same | same | same |
  | `4` | Stick click | Stick click | Pad click | Stick click |
  | `8`, `0x10` | Stick up or down | same | Pad up or down | same |

  The real table must follow `controllers.cpp` (the trigger and the grip can
  swap through `controller_swap_buttons`) and the second press rule (A or X
  for the double click). Build it in one function, `HintLabel(code,
  profile)`, so H3 and H4 share it.
- Hide: a code that the controller cannot do (tilt, keyboard keys, the
  middle button when `stick_click` has no job). Skip the draw and let the
  caller advance its fade. The mod already does this for second copies.
- The pointer hand's profile decides the labels. If the hand is unknown, use
  the Touch like set.

**Art.** None. The cap is the game's.

**Desktop test.** Yes, with the fake controller (`fake_controller`) plus a
setting that forces a profile string (add `hint_profile`). The hint appears
in the eye capture as with today's hints.

**Risks.**

- The stack buffer for the label. Keep labels under 24 characters. The
  pointer must stay valid for the whole call, so use static strings.
- The key cap draws through `DrawBox`, so the widget rules in
  `draw_hooks.cpp` apply, not the hint rules. Check the size (the cap follows
  the game's size argument, not `hint_scale_pct`) and the placement beside
  the Hand in both eyes.
- A cap replaces the blink. The lit button no longer pulses. The cap can use
  the blink style as an alpha pulse if it needs one (arg 11 alpha). Confirm in
  the headset that a still cap is clear enough.
- The screen edge hint (`fn_00447850`, no text) is not covered. Hide it in H1
  while the camera hints are wrong for controllers, and cover it in H3.

### H2. The word list

**Goal.** Correct the words where they name the mouse.

- Hook the help text add call at `0x005CCF80`. Keep two strings per key: the
  original and the controller version. Choose at read time by the current
  input state, so a player who puts the controllers down sees the mouse
  words again (needs a look at `0x005CAD00` and the reader, [?]).
- Override table: the five mouse names (LMB, RMB, MMB, Wheel Up, Wheel Down)
  by key name, from the same `HintLabel` table as H1. Phrase table: "mouse
  wheel" and "wheel" to "stick" in the 8 wheel lines. The five keyboard lines
  become one controller sentence.
- Do not change a spoken line's "Action Button" (section 4.2).

**Art.** None.

**Desktop test.** Yes. Read the lines in the log. Read them on screen with a
forced profile.

**Risks.** The hook must not change the text when the mouse is in use. The
words switch at load in the simple form, so do the read time switch or accept
that the choice is fixed per session. Long strings can overflow a text box
the game sizes for the original. The mod's own sentences must be no longer
than the originals (the five mouse names are 3 to 10 characters).

### H3. An own atlas with flat icons

**Goal.** The mouse picture in every hint is replaced by a controller glyph,
including the screen edge hint and the controls page.

- Make the atlas at build time by script: a 256 by 256 BGR file plus a
  256 by 256 alpha file per family (cells of 64 by 64). Ship them in the mod
  folder. The art is the mod's own (section 3, level a).
- Point the game at the file (section 2, way C). Take the profile at the time
  the game builds the material. The path pointer is in the mod, patched at
  `0x0044757F`. To switch at run time replace `0x00C5A318` with a new
  material.
- In the hook of `fn_00447450`, rewrite the button code and row so the game
  reads the cell the mod wants. Never send code `1`.
- Keep the blink. Keep the game's box.

**Art needs.** Twelve glyphs per family. The families are Touch like, Index,
Wand, Mixed reality. Simple uses a subset.

**Desktop test.** Yes: force a profile and read the eye capture. Compare the
four atlases side by side with one hotkey.

**Risks.**

- The loader `fn_008379E0` may check the file size or a flag (`0x41`), and
  the alpha name is inferred [?]. Test with a copy of the original atlas
  first, from the mod folder.
- The path string has room for about 27 characters if written in place. A
  pointer to a mod string avoids the limit.
- The game builds the material once. A late switch needs the pointer swap
  and a safe release of the old material [?]. If that is a problem, choose
  the family when the game starts.
- A 64 by 64 cell at 20 percent hint size is about 13 pixels of the design
  size. Test that a glyph reads at that size in the headset. If not, raise
  `hint_scale_pct` for the icon or go to H4 with own placement.
- The atlas also shows on the options controls page, so a player sees
  "trigger" glyphs next to the mouse bindings. That is right while a
  controller is in use, and wrong on the desktop. Switch by input state.

### H4. A picture of the controller with the button lit

**Goal.** A larger picture of the controller with the used control lit, and
the same pipeline as H3.

- Art: one silhouette per family, one lit control per variant (section 3,
  level b). Draw them by script from primitives. Use a bright accent color
  on the used control, and no other change between variants.
- Two ways to show it. Use the atlas (way C) if 64 by 64 is clear. If not,
  use `DrawBox` with an own material (way D), with a 128 by 128 or 256 by 256
  cell and a flip for the left hand. Way D needs the mod to place the picture
  and to add the flat and once per pass rules from `draw_hooks.cpp`.
- Show the picture for the pointer hand only. Optionally, show it beside the
  Hand and the label in a cap under it. The label repeats the H1 word.

**Desktop test.** Yes, forced profile, as in H3.

**Risks.** Art quality. The silhouette must be generic (section 3). The
Touch controller shape on a Quest 3 differs from the Touch Plus shape. Use
one generic shape for the family. Check that the picture does not blur at
the hint size.

### H5. The runtime's own 3D model

**Goal.** The real controller model at the real controller, with the button
lit and a label. Falls back to H4 when the runtime has no model.

- Ask for the extension at `xrCreateInstance` only if H0 listed it. Ask for a
  model key per hand. Load the glTF at session start. Read the node names for
  the buttons and the stick.
- Render on the D3D11 side in `xr_screen`, at the hand's grip pose, in the
  same LOCAL space as the eyes. Draw it only while a hint is active. A
  shared record from the game hook says which button, when it started and
  when it ends. The mod must not add any motion the player did not make:
  the model stays still with the controller. Only the lit part changes
  (the mod rule: no automatic sway, glide or bob).
- Keep the label text from H1 near the lit button. Fade in and out with the
  game's own hint fade.

**Art.** The runtime's model. Fallback: H4.

**Desktop test.** Partly. The fake controller has a pose, so the model can
draw at it. The model itself needs a runtime that offers one. The desktop
run has no such runtime, so use a bundled generic mesh only for tests. Do not
ship it unless the author makes it.

**Risks.**

- The runtime may offer no model. That is the main risk [?].
- glTF and Basis decoding add code and dependencies (all permissive
  licenses, [I]).
- A model in the hand can hide the Hand of the game (M3 puts the Hand at the
  grip point). Draw it beside the Hand, or only when the Hand is idle.
- The button node names differ by maker. Map by profile.
- The cost is large. Do it last.

### Order and effort

| Stage | Art | Effort | State it needs |
| --- | --- | --- | --- |
| H0 | none | small | none |
| H1 | none | small to medium | controller in use flag, `HintLabel` |
| H2 | none | small to medium | `0x005CCF80` read |
| H3 | 12 glyphs, 4 families | medium | H1 flag, profile at draw |
| H4 | silhouettes, 4 families | medium | H3, maybe way D |
| H5 | runtime model | large | H0 result, XR side renderer |

**Realistic now:** H0, H1, H2, then H3. **Next:** H4. **Later:** H5.

## 6. Risks that cut across stages

- **The controller mapping is still moving.** M3 is in progress. The label
  table must read the same mapping as `controllers.cpp`, from one place.
  The hint must never show a button that does nothing.
- **Two hands.** The pointer hand decides the profile and the labels. If the
  player switches hands, the hint follows. If the hands are different kinds
  (rare), use the pointer hand.
- **Quest 3 profile.** The Quest 3 may report a newer Meta profile or the
  older Touch one, depending on the runtime. The mod logs the profile
  (`controllers.cpp`, line 383). Treat unknown profiles as Touch like.
- **Stereo.** Any new hint must go through the once per pass and flat
  placement rules. The old bug (a hint drawn twice, or apart in the two eyes)
  is the main way to break it.
- **The interface layer (`docs/ui-layer.md`).** When the help panel moves to
  its own layer, the hint next to the Hand may go with it or stay in the
  eyes. The design above works in both. H1 to H4 draw through the game, so
  they follow the panel. H5 draws on the XR side and does not.
- **Hotkey A and B.** Give Buck a hotkey to cycle `game`, `caps`, `icons`
  and (later) `model` live, so he can judge them in the headset.

## 7. Open questions

1. What action numbers `$m1` and `$m2` are, and what `fn_00457041` draws.
   H0 answers both.
2. What writes the screen edge globals at `0x00C5AFBC` to `0x00C5AFDC`.
   Likely the `CameraHelp` update code at `0x00449xxx`, which reads and
   writes through a base register [I]. The census in H0 shows the values
   without reading the writer.
3. Which arguments of `DrawBox` after the material are needed for an own
   picture (way D).
4. Whether the loader `fn_008379E0` accepts a file from the mod folder and
   how it names the alpha file.
5. Which model extensions Virtual Desktop's 32 bit runtime lists.
6. Where the help text is read back, so H2 can switch by input state.
7. Whether the hint can stay a 64 by 64 cell at the size the mod uses in a
   headset. Only a headset test answers this.

## 8. As built (H0, H1 and H2)

Code: `src/mod/controller_hints.cpp` and `.h` (the words, the census, the help
text), the hook `h_DrawKeyOrMouse` in `src/mod/draw_hooks.cpp`, and
`ControllersHintState` in `src/mod/controllers.cpp`. Keys of `bwvr.ini`:
`controller_hints` (default 1), `hint_profile` (a test key, forces a
profile) and `hint_selftest` (a test key, section 8.5).

### 8.1 When the controller words show

Only while `ControllersOwnPointer()` is true and `controller_hints` is 1. The
state is latched once a frame, when the left eye pass starts
(`UiDepthSetEye(0)` calls `ControllerHintsPass`). So both eyes always agree.
With the mouse as the owner, with `controllers=0` or with `controller_hints=0`,
no hook changes anything, and the hints are as before. The argument names of
`DrawKeyOrMouse` are now `blink`, `button` and `row_or_label`.

### 8.2 H0: the census

Three read only hooks and a table of 128 distinct calls. Each call goes in the
log once (`hints        : census ...`). The count goes in the log again every
1200 frames, if it changed.

- `fn_005C78F0` (action, kind, code, row or label): the ask.
- `fn_00447450` (7 arguments, cdecl): the picture. The screen edge hint is the
  call that returns to `0x004478D2`.
- `DrawKeyOrMouse`: return address, blink, button, look (cap, text only or
  mouse picture) and the caption.

The log also has the profile of the pointing hand, the profile used, the family
and every word (`the pointing hand reports ...`). With the VR start it also
lists the OpenXR instance extensions of the runtime, and says if it has
`XR_MSFT_controller_model` or `XR_FB_render_model`.

What the desktop runs showed: the screen edge picture is called in every eye
pass with button 0, row 0 and blink 0. No real hint was drawn in any desktop
run (four runs, up to the Creature select). So the open questions about `$m1`,
`fn_00457041` and the edge globals stay open. The census will answer them in a
headset run.

### 8.3 H1: key caps

`h_DrawKeyOrMouse` acts when the controller words are on, inside an eye pass,
not in a menu, not from the speech bubble, and with `hint_mode` not 0:

1. A key cap of the game (blink -1) is hidden. It names a keyboard key.
2. A hint with a mouse picture (row 0 to 2) gets a label from
   `ControllerHintLabel(button)`. If there is no label, the hint is hidden.
   Otherwise the call becomes a key cap: blink -1, button 0, row a pointer to
   the label.
3. The call then takes the tooltip path that already existed. It is kept once
   in a pass, moved as a whole and drawn flat. The two copies of the game are
   one call after the change, so each eye draws exactly one cap.

The labels come from one function, `BuildWords`. It follows `xr_screen.cpp`
(which controls a profile has) and `controllers.cpp` (what each one does):

| Button code | Word | Hidden when |
| --- | --- | --- |
| 1, the left button | Trigger (Grip with `controller_swap_buttons`) | never. The Simple profile says "Select". |
| 2, the right button | Grip (Trigger when swapped) | Simple has no grip |
| 8 and 0x10, the wheel | Stick ("Pad click" on Wand, because its pad works only when clicked) | Simple has no stick |
| 4, the middle button | none | always, it has no controller input |
| 3 | none | always, the control map never gives it |

`BuildWords` also has the jump word (A, or X on the left Touch hand, A on Index,
none on the other profiles) and the gesture word (B, or Y, or the stick click,
by `gesture_button`). No mouse code maps to them. The help text and H3 use
them.

The screen edge picture is hidden while the words are on, because it teaches
the mouse at the edge of the screen. The bubble icon (return address
`0x005763D1`) is not changed.

### 8.4 H2: the word list

The hook is on `0x005CAD00`, not on `0x005CCF80`. It stores one `ADD_TEXT`
line: `entry+0` and `entry+4` are two numbers, and `entry+8` is the text copy.
The key name of the line is the third argument and the text is the fourth.
For a line that the mod words differently, the hook keeps a row (the slot and
the game's own copy). When a profile is known, it makes a second copy with the
game's allocator (`0x007DB790`). A frame latch switches the pointer at
`entry+8` between the two copies. So the game's own free stays valid, the mouse
words come back when the mouse owns the pointer, and nothing goes to disk. The
mod holds only its own sentences and the key names.

- Four mouse names (`..._LEFTMOUSEBUTTON`, `..._RIGHTMOUSEBUTTON`,
  `..._WHEEL_UP`, `..._WHEEL_DOWN`) use the label table.
  `..._MIDDLEMOUSEBUTTON` stays.
- Five keyboard lines and two long wheel lines (`DYK_18`, `JUST_IN_CASE_37`)
  get one short sentence of the mod, for example "Push the stick to turn and
  zoom."
- Any other line with the whole words "mouse wheel" or "the wheel" gets
  "stick" ("pad" on Wand) in their place. Five lines match.
- The Escape lines and "Action Button" stay. The menu button has no job.
- A profile with no stick (Simple) leaves the wheel lines as they are.

Sixteen lines change in the game of this repository, and one database holds
all of them. The log lists each new line once. A line that the game changes
later is left alone.

### 8.5 Tests

Four desktop runs with the fake controller (`fake_controller=1`,
`controller_hand=1`) gave the census and the word list, but no real hint drew.
So the test key `hint_selftest=1` draws five hints of its own after the help
layer, two copies of each, as the game does: a left button, a right button, a
middle button, a wheel hint and the key cap of a keyboard key. Run 5 forced
`hint_profile=htc/vive_controller` and `ui_scale_pct=100`.

- Frame 1, the mouse owns the pointer: the four mouse hints and the "Space" cap
  draw as the game draws them.
- After the fake trigger press, the controller owns the pointer: caps with
  "Trigger", "Grip" and "Pad click" draw once in each eye at the same place.
  The middle button hint and the "Space" cap do not draw.
- The log counts agree: each pass has three converted hints and two hidden
  ones.

### 8.6 What needs the headset

- A real hint. The desktop runs never reached one, so the real callers
  (`fn_00448AC0`, `fn_00457041`) are not seen yet. Read the census.
- Whether the word in the cap reads at the real size. The cap is small and
  the word is smaller.
- The switch of the help text when the player puts the controller down and
  uses the mouse (it works in the desktop runs, at the start).
- The list of OpenXR extensions of Virtual Desktop (H5).

### 8.7 Real hints, 2026-10-02

The runs of 8.5 drew no real hint because the profile of this PC has the
game's help options at "No ToolTips", "No Text" and "No Help" (`ui-layer.md`,
section 15.1). Buck's headset test used the same profile. The log now gives
the four values once a game. The test key `help_test=1` turns the help on in
the game's memory for a desktop run.

With `help_test=1` the God view runs bk2 to bk11 drew real hints of the game.
While the fake controller placed the Hand, each hint with a mouse picture
became a key cap with the controller word:

| What the Hand was over | What the game showed |
| --- | --- |
| The land | "Move" and a key cap "Trigger" |
| The land, with an object in the Hand | "Drop" |
| A wood pile of the store | "Wood Amount: 30000" and a key cap "Grip" |
| A bulk take | "Amount: 57" and on, text only |

So the H1 words show in a real game: "Trigger" for the land drag and "Grip"
for a take. The census shows the caller: `fn_00448AC0` (return
`0x00448BD9`), the `GKMIcon` node of section 1.5, with button 1 for the land
and button 2 for a pile of the store. With the direct hand these
tooltips now go to the Hand, at its depth and at a size in the room
(`ui-layer.md`, section 15.3). The words of H2 in the help text were not seen,
because no help text line showed in these runs. Since 2026-10-03 these
tooltips go to the hint card (section 9), and they do not draw in the eye
pictures.

## 9. The hint card (2026-10-03)

Buck, after the headset test of 2026-10-03 (build `a785a15`): "I am still
seeing the doubled hints. [...] There should only ever be one hint visible,
not 2 in each eye, or 2 in completely wrong locations in both eyes or anything
like that, it should render next to your hand. Also the way they are
positioned now makes it almost impossible to actually see the hint, it's
inside the hand." And: "We definitely want HL:A style controller prompts."

Code: `src/mod/hint_card.cpp` and `.h` (the cards), `src/mod/hint_card_math.h`
(the place, the true hints, the prompt rule, the fade), the route in
`h_DrawKeyOrMouse` of `src/mod/draw_hooks.cpp`, the quad layers in
`src/vr/xr_screen.cpp`, and `ControllersCardPose` in
`src/mod/controllers.cpp`.

Since the afternoon of 2026-10-03 each controller has a card of its own, and
a hint of the game that is false with the controllers does not show. Section
9.9 has that change. Sections 9.2 to 9.6 give the values of today.

### 9.1 Why the hint showed double

Each cause below is a way for a flat hint in the eye pictures to show two
times. The proof is from Buck's log of 2026-10-03 (13666 lines, the build he
tested), from the desktop runs of 2026-10-02 and from the code [V].

| Cause | What happened | Proof |
| --- | --- | --- |
| 1. A hint with no disparity | `TooltipAtHand` put the hint at the Hand only while `HandGlowEyePoint` gave a point. That needs the Hand in the Normal, the Holding or the Creature state, and its point in the view. In a land drag (Hand state 2, hand mode `0x14`), in a Tug, with a Miracle, and with the hand out of the view, the hint took the old flat rule: the same pixel in both eyes. A picture with no disparity is at infinity. The eyes look at the hand, 0.3 to 0.6 m away, so they see that hint two times. | Buck's log: 9217 of 105020 tooltip draws (8.8 percent), in 70 of 128 report windows, were not at the Hand. Frames 7901, 8905 and 9081 show "Move" at x 2560 to 2616 in both eyes, with the pointer at the edge of the view (2020 2879, 2879 952 and 2879 302). The log has the land drag too: "state 2, interface hand mode 0x14". |
| 2. A hint that changes place | Each time the state of the Hand changed, the hint went from the Hand to the game's 2D place at 25 percent of its size, and back. The player saw a hint at two places. | The same counts. A land drag with the trigger is such a change. |
| 3. A hint in the Hand | At the Hand, the hint was at the depth of the Hand's point, beside the index tip. The game draws the Hand after the help, so the mesh hid a part of the hint, and a different part in each eye. | Buck's log, frame 12465: the hint starts 42 pixels left of the Hand's point (1669 against 1711) and goes to the right, through the Hand. Runs bk3 and bk9 of `ui-layer.md`, 15.5. |
| 4. One eye at the Hand, one eye flat | `TooltipAtHand` decided for each eye pass. A Hand's point in the view of one eye only gave a hint at the Hand in that eye and a flat hint at 25 percent in the other eye. At 0.48 m that band is 138 pixels wide at each side of the view. | The code. The 70 sample frames of Buck's log do not show it: both eyes agree in each one. |
| 5. One frame late | The tooltip list draws in the walk, before the glow sets the Hand's point of that frame. So the hint used the point of the frame before, and it moved against the Hand when the head or the hand moved. | The code (`HandGlowEyePoint`: "from this frame or the one before"). This shakes the hint. It does not make two. |
| 6. Less than 90 frames a second | Buck's log has windows at 50 and 63 frames a second. The headset then shows each game frame two times, and anything in the eye pictures that moves with the hand shows two times while the hand moves. | The frame rate lines of the log. This is true for the Hand too [I]. |

What is not a cause [V]:

- The two copies of the game's tooltip list (`HelpSystem::Draw3D` and the
  `HelpText` callback). The dedup of `h_DrawKeyOrMouse` works: each window of
  Buck's log has as many skipped copies as drawn ones, and the interface dump
  of run bk11 has one tooltip in each eye pass (25 batches).
- The disparity at the Hand. In the 70 sample frames of Buck's log, the hint
  at the Hand had the disparity of the Hand's point within 1 pixel, and no
  vertical difference.
- A hint left in the render target. The picture before the eye passes (run
  bk11) is the right eye of the frame before, and each pass draws the whole
  view again.
- The interface panel. Buck runs `ui_panel=0`, and with `ui_panel=1` a
  tooltip never went to the panel.

So no rule that puts a flat hint in two eye pictures is safe: it must be
right for each state of the Hand, in both eyes, in each frame.

### 9.2 One card in the room

While a controller owns the pointer (`ControllerHintsActive`) and
`hint_card` is 1, no hint of the game draws in an eye picture:

1. `UiDepthSetEye(0)` latches the route for the frame (`HintCardRoute`), so
   both eye passes agree.
2. `h_DrawKeyOrMouse` gives each hint that took the tooltip path to
   `HintCardGameHint` as words: the controller word (for a hint that had a
   mouse picture) and the caption. Then it returns. The game draws nothing,
   and its callers run their fades as before.
3. After both eye passes (`UiDepthSetEye(-1)`), `HintCardEndFrame` draws the
   words of each card into a picture of 768 by 320 pixels with GDI: a dark
   card, white letters, and the button word on a badge. The trigger badge is
   green and the grip badge is gold, as the glow at the Hand's point.
4. `XrScreen::StageCard` gives the picture and its pose to
   `XrScreen::EndFrame`. That makes a small swapchain at the first layer of
   a card, writes the part that shows, and adds one quad layer for the card
   in the LOCAL space, after the projection layer and the panels.

The runtime shows a quad at one place of the room to both eyes. So there is
one hint, at the depth of the hand, in each state of the Hand. The runtime
puts the quad again for each displayed frame from the head pose, so a slow
frame of the game does not double it when the head moves.

The card needs no interface panel, so it works with `ui_panel=0`. The hints
that were hidden stay hidden: a key cap of a keyboard key, a mouse button
with no controller input, and the screen edge picture. The icon in a speech
bubble and a hint in a menu do not change. With the mouse as the owner,
with `controller_hints=0`, with `hint_card=0`, and in the stereoscopic window
(a headset with no projection layer), the hints draw in the eye pictures as
before. If the runtime refuses the swapchain, the hints go back to the eye
pictures too.

### 9.3 The place of the card

`CardPose` in `hint_card_math.h`, from the grip pose of the hand of the card
and the head center, both in the LOCAL space (`ControllersCardPose`). The
card uses no game transform, so it is at the real wrist at each scale of the
world.

- The hand: `HandFromGrip` and `HandOutline` use the test hand of the overlay
  (`overlay_math.h`). The wrist is 9.3 cm from the grip toward the forearm.
  The outline is the corners of the palm and the five tips of the open
  fingers.
- The side: the card is beside the wrist as the head sees it, level with it,
  and at its distance from the head. `ext` is how far the outline goes to
  that side of the wrist in the view. The center is `ext` + 3 cm + half the
  width from the wrist.
- By construction, each point of the hand is at `ext` or less, and the card
  starts at `ext` + 3 cm. So the card is not in the hand in any pose, it
  does not cover the hand in the view, and it is not on the place where the
  fingers work. The runtime draws the quad over the eye pictures, so the Hand
  cannot hide the card.
- The card starts on the outer side: right of the right wrist, and left of
  the left wrist. A hand at the right of the body that points ahead has its
  fingers toward the middle of the view. When the fingers point far to the side of the card (the extent on
  its side is 4 cm more than on the other side), the card changes side, one
  time. It changes back only after the same excess the other way.
- The card is square to the line of sight to the wrist, with no roll. It
  goes with the hand one to one. It has no motion of its own: no glide and no
  follow delay. The card fades in in 0.25 s and out in 0.35 s, in place.
- The size: 40 pixels of the picture are 1 cm, so a capital letter is about
  8 mm high and a card with one line is 2.2 cm high. That letter is 0.8
  degrees at 55 cm, the length of an arm. Most cards are 7 to 14 cm wide. A
  wider card stops at 19 cm. `hint_card_size_pct` scales all of it.

Two places were tried in the math test and rejected. A card on the forearm
side of the wrist in the room (away from the fingers and up) is between the
eyes and the hand when the fingers point along the line of sight, which is
the usual reach into the land: it covered the Hand in 467 of 3000 poses of
play. A card under the hand in the view goes past the finger tips when the
fingers point down, and then it covers the object that the Hand touches.

### 9.4 The prompts

The card also shows what the controller can do now, as a button word and a
short order. The context comes from the mod's own state. `ContextNow` reads
it once a frame, and the first match counts:

| Context | Source | Prompt |
| --- | --- | --- |
| A dialog box | `ControllersMenuRay` | Trigger: "Select" |
| The teleport aim | `TeleportAiming` | A: "Release to go" |
| A far target that the Hand can take | `FarGrabDrawState`, active and valid | Grip: "Pull to fetch" |
| The Hand holds an object | `HandDirectGlowFacts`, holding | Grip: "Open to drop" |
| An object to take | hand mode 9 | Grip: "Take" |
| A pile, a field or a fish farm | hand mode `0xD` | Grip: "Hold to collect" |
| The Hand touches the Creature | hand mode `0x13` | Grip: "Stroke or slap" (section 9.9) |
| The land | hand mode 3 | On the card of the other hand, Grip: "Move the land", X: "Ring menu" (the ring, `controllers.md`, section 10, since 2026-10-04), and the first time Y: "Game menu". On the card of the pointing hand, A: "Teleport". In the old immersive view the trigger moves the land, so Trigger: "Move the land" shows on the card of the pointing hand |

The Creature commands add four contexts while the leash is on
(`CreatureCommandHint`, `hand.md`, "The Creature commands"). The trigger held
with the arc of a command comes after the teleport aim: Trigger: "Release to
send". An object under the aim comes after the far target: Trigger: "Send the
Creature". A far target that is also a target of the command shows both
lines. The player's own Creature under the aim: Trigger: "Take the leash
off". The land with the leash on takes the place of the land: Trigger: "Send
the Creature" and A: "Teleport".

A miracle in the Hand, or a one-off seed that the Hand carries, is a context
of its own (`MiracleHint`, `hand.md`, "A miracle in the Hand", 2026-10-04).
It comes after the teleport aim and before each other context. Its lines
stay for as long as the miracle is in the Hand: the rules of the dwell, the
hold and the count do not apply. The hints of the game for a button go, and
one line of its text stays. The lines say what the grip does with this kind
of miracle, why the game refused the last cast, and how to cancel: the
second face button, with its word from `kWordGesture`, and the shake.

The words come from `ControllerHintWord`, the same table as the key caps
(section 8.3), so they follow the profile, the left hand and
`controller_swap_buttons`. A line for a button that the controller does not
have is left out. The Hand's mode is latched in the left eye pass, in the
`HelpText` callback, after the world is drawn (`HintCardLatchContext`).

A prompt must help and must not stay in the way (`PromptStep`):

1. The context must hold for 0.3 s before the prompt shows (0.5 s for a
   dialog, 0.6 s for an object in the Hand, 1.5 s for the land). A hand that
   moves across objects shows nothing.
2. The prompt shows for 4 to 6 s. Then it goes, and it stays away until the
   context ends and starts again.
3. A context shows its prompt 2 to 4 times in a session.
4. A hint of the game for the same button on the same card takes the place
   of the prompt, and that does not count as a show. So "Grip Pick Up" of the
   game shows and "Grip Take" of the mod does not.

With `prompts=2`, rules 2 and 3 do not apply. With `prompts=0` the card shows
only the hints of the game. The prompts do not need the help options of the
game, so a player with "No ToolTips" sees them too.

### 9.5 The keys and the test key

| Key of `bwvr.ini` | Default | Meaning |
| --- | --- | --- |
| `hint_card` | 1 | 1 shows the hints on the card while a controller points. 0 draws them in the eye pictures, as before |
| `prompts` | 1 | 0 no prompts, 1 the first times, 2 always |
| `hint_card_size_pct` | 100 | The size of the card, 50 to 200 |

`hint_card.cpp` reads the three keys itself, at the first frame. With
`test_keys=1` and `ui_panel=0`, Ctrl Shift F11 switches between the card and
the hints in the eye pictures, for a live comparison.

### 9.6 The log

- `hint card : hint_card=1, ... prompts=1, ...` once, with the settings.
- `hint card : frame N, the card of the right hand: [Grip] Take. 6.0 by 2.2
  cm, 241 by 86 px, at x y z m, 7.4 cm to the right of the wrist in the view
  and 55 cm from the head. The prompt is for an object to take`: each time
  the words or the side of a card change (160 lines at most).
- `hint card : the hint "[Trigger] Move" of the game does not show: the
  trigger does not move the land here. The grip of the other hand moves it`:
  one time for each of the two false hints (section 9.9).
- On a capture frame: the line above, a line with the whole pose (the turn,
  the wrist, the finger tips, the grip, the head and the two extents), and
  two pictures for each card beside the eye pictures:
  `bwvr-hintcard-right-NNNNN.bmp` or `bwvr-hintcard-left-NNNNN.bmp` (with
  its alpha) and the same name with `-over` (over a checkerboard).
- Each report window: the frames with the route, the frames with each card,
  the hints of the game that went to the cards, the frames with a false hint
  that did not show, the frames with words and no tracked hand, the frames
  that a card did not show because it was on the other hand, the changes of
  side, and the prompts by context. `ui depth : N tooltip calls went to the
  hint card and did not draw in an eye picture`. `XrScreen` counts the quad
  layers of each card.

### 9.7 What came from H3 to H5

- The words come from one table (H1, `BuildWords`), now also for the prompts.
- H4 and H5 wanted a picture of the mod's own, drawn on the XR side at the
  grip pose in the LOCAL space, only while a hint is active, with no motion
  that the player did not make. The card is that picture.
- Level (a) of section 3: the art is the mod's own. A badge with the button
  word needs no picture of a real controller.
- Not built: glyph pictures of the buttons (H3), a picture of the controller
  with the button lit (H4) and the runtime's model (H5). Buck's runtime
  lists no controller model extension (his log: 31 extensions, "without a
  controller model").

### 9.8 Tests

The math test has 65 checks (`hint_checks.h`), and all 1195 checks pass:

- The hand: the wrist, the mirror image of the left hand, the outline.
- 4000 poses of both hands, with any turn: each one has a card, no point of
  the outline is nearer than 3 cm to the card, each corner of the card is at
  the depth of the wrist, and the card is square to the line of sight with no
  roll.
- 3000 poses of play (the hand 25 to 60 cm ahead and 20 to 50 cm under the
  head, the fingers ahead and down, turned 40 degrees to each side, rolled 60
  degrees): the card covers no point of the hand in an eye, its center is 8
  to 16 cm from the wrist, and its front is less than 32 degrees from the
  head.
- The side: fingers that turn to the right change the side one time, at 38
  degrees, and back one time, more than 10 degrees later. Between the
  changes, 1 degree of turn moves the card less than 4 mm. A hand that
  shakes 3 degrees never changes the side.
- A wrist straight under the head uses the last direction.
- The size, the prompt rule (dwell, hold, count, a hand that goes across
  objects, always) and the fade.

A harness (`hintcard-harness` in the scratchpad, not in the repository) links
the real `hint_card.cpp` with made up states. It ran 1410 frames: each
context gave its prompt after its dwell, "Grip Pick Up" of the game kept
"Grip Take" away, the land gave three lines the first time and two lines the
second time, "Take" showed 4 times and not a fifth time, fingers 60 degrees
to the right put the card on the left, and a hand with no tracking showed no
card. The pictures of the cards are sharp at 50 pixels a cm.

Desktop runs: not run yet. The PC was in use on 2026-10-03, and a desktop
run needs 10 minutes with no input. The runs to make are `hc0` (the build of
the headset test, for the pictures of causes 1 to 4), `hc1` (the card with
the hints of the game, `help_test=1`) and `hc2` (the card with the prompts
only, `prompts=2`). The script is `hints_runs.ps1` in the scratchpad.

**Needs the headset.**

- The quad layer itself: the swapchain of the card, its alpha over the world,
  and that Virtual Desktop shows it. The desktop has no runtime.
- Whether a letter of 6 mm reads at 40 to 60 cm (`hint_card_size_pct`).
- Whether the side of the wrist is the right place while the hand works, and
  how often the card changes side.
- Whether the card steps against the hand at a low frame rate. The hand pose
  is from the game frame, and the runtime moves the quad only for the head.
- The prompts in play: the dwell, the time of the show and the counts.

### 9.9 Two cards and true hints (2026-10-03, afternoon)

Buck, after the headset test of build `f194c59`: "The hints are great now,
but I keep getting the 'Trigger to move' hint, but we cannot actually move
using trigger any more, so there is no point showing that hint. Also I got a
hint saying 'Y' for menu shown on my left hand, but I think hints should be
separated and shown on each hand so it's more obvious what hint is for what
controller." His log shows that the card went out as a quad layer ("the hint
card goes out as one quad layer over the projection layer"), so quad layers
work in his runtime.

**The hints of the game, and what is true.** The card showed these hints of
the game in his log (392 lines with the tag `hint card`). `GameHintTrue` in
`hint_card_math.h` is the rule.

| Hint of the game | Mouse button | With the controllers | Now |
| --- | --- | --- | --- |
| "Move" over the land, in 39 lines of the card (the census counts 96494 draws) | Left, code 1 | False in the God view and on the table. The game's land drag runs, but the picture does not come from the game camera, so nothing moves (`GodDragAfter`). The grip of the other hand moves the land. False also with the leash on: the trigger commands the Creature and does not reach the game. True in the old immersive view | Does not show where it is false. The land prompt shows "Grip Move the land" on the card of the other hand |
| "Focus Creature" over the land with the leash on, in 33 lines, from frame 9917 (the leash went on at 9813) to the frame the leash came off | Right, code 2 | False. It is the tooltip of the leash (`0xEA2`): a press sends the Creature to that place. `creature_command.cpp` leaves that packet out for the grip, and the trigger sends the Creature | Does not show over the land (hand mode 3) while the leash is on. The prompt shows "Trigger Send the Creature" |
| "Pick Up", "Plant", "Catch", "Throw", "Activate", "Reminder" | Right, code 2 | True: the grip does the work of the right button | Stays |
| "Interact" on the Creature | Right, code 2 | True. The grip is the control that touches the Creature. In his log the right button went down at frame 9792, the press on the Creature started at 9799, and the leash click came at 9805 | Stays. The prompt of the mod for the Creature said "Trigger Stroke or slap", which was false. It now says "Grip Stroke or slap" |
| "Zoom Out" | The wheel, code `0x10` | True: the stick zooms | Stays |
| "Temple Health: 100%", "Amount: 128" | None | True | Stays |

The test is the button code and the mod's own state, never the caption, so
it holds in each language of the game.

**The control that touches the Creature** [V]. `controllers.cpp` sends the
trigger as the game's left button and the grip as the right button. The
game's step of the default action (`0x005D5190`) gives a left press to
`0x005D1320`, which is a jump to `fn_005D1AB0`: the land drag, with the
camera and the altitude of the land and no test of an object. It gives a
right press to `fn_005D1330`, which reads the object of the next pick record
and its bit `0x10` (`0x005D13DC`). So the grip starts the stroke and the
slap, and the trigger does not. `hand.md` ("The Creature by touch") and the
README said "trigger". The README is corrected. The tests of that section
of `hand.md` were plans with the trigger, and no run made them.

**One card for each controller.** A line names a control of one controller,
so it shows at the hand that holds that controller.

- The card of the hand that points: each hint of the game, and each prompt
  for the trigger, the grip, the stick and the face buttons of that hand.
- The card of the other hand: the prompts for the grip that moves the land
  (`kWordWorld`, only while `grab_world` is on), for the button of the
  game menu (`kWordMenu`: Y, or B when the left hand points), and since
  2026-10-04 for the control of the ring (`kWordRing`: X, or A when the left
  hand points, or the stick when only the stick opens the ring). The card of
  that hand does not show while the ring is open.
- Both cards can show in one frame. Each one is a quad layer of its own with
  its own swapchain (`kCardHintPointing` and `kCardHintOther` in
  `xr_screen.h`), placed by `CardPose` at the wrist of its hand.
- A card that would hide a point of the other hand in the view does not
  show (`CardCoversAny`, with 1 cm of margin). When the two cards overlap
  in the view (`CardsOverlap`), the card of the other hand does not show.
- A hand that is not tracked has no card.

**The look.** Short lines, a badge with the button word, and one size of
letter. The letters are 25 percent larger than before: 8 mm, 0.8 degrees at
55 cm. The corners are rounder and the card is a little more clear (alpha
0.86). The fade is soft: 0.25 s in and 0.35 s out, in place. The prompts keep
their rule: with `prompts=1` each one shows only the first times, and with
`prompts=2` each one shows always.

**Tests.** The math test has 32 more checks (`hint_checks.h`), and all
pass: the 15 cases of `GameHintTrue`, the size and the fade, 2000 poses of two
hands (each has two cards, more than 95 percent start on the outer side, no
card is on its own hand or on the other hand, and the two cards never meet),
a left hand at the card of the right hand, and `CardsOverlap`.

A harness (`cards\harness` in the scratchpad, not in the repository) links
the real `hint_card.cpp` with made up states. It ran 1740 frames:

- God view, the game says "Move" over the land: no card for 1.5 s, then
  "Grip Move the land" and "Y Menu" at the left wrist and "A Teleport" at
  the right wrist. The second time the left card has no "Y Menu".
- The leash on, the game says "Focus Creature": that hint does not show, and
  the right card has "Trigger Send the Creature" and "A Teleport".
- The old immersive view: "Trigger Move" of the game stays.
- "Grip Pick Up" and "Wood Amount: 30000" stay.
- The other hand at the place of the right card: the right card does not
  show on 80 of 80 frames, and it comes back when the hand goes away.
- The left hand points: its card is left of the left wrist.

The subtitles and the help text have a card of their own, fixed in the
room: `ui-layer.md`, section 17.

**Needs the headset.** Two quad layers of cards in one frame. Whether the
left card is easy to see while the right hand works. Whether 8 mm letters
are large enough. Whether the land prompt at the left wrist teaches the
grip.
