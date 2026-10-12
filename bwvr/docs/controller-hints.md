# Controller hints

This file records how the game draws its mouse and key hints, what the mod
does with them while a controller is in use, how the hint cards work, and
what is still open (pictures of the controllers and models).

While a controller owns the pointer, the hints show on one card beside the
wrist of each hand, with prompts for the controller (section 9). The words of
the controller replace the mouse words in the game's help text (section 8).

Marks: **[V]** verified (disassembly, symbols, the game files or the SDK
headers). **[I]** inferred, not yet seen at run time. **[?]** unknown, needs a
test. **[M]** measured in a run.

All addresses are for `runblack.exe` v1.42, image base `0x00400000`.

## Summary

- One function, `CameraHelp::DrawKeyOrMouse` `0x00447EA0`, draws every hint
  that has words. It has three looks: a mouse picture with a lit button, a
  key cap with text inside, or plain text with no picture [V].
- The mouse picture is one 256 by 256 texture, `data\textures\mousehelp.raw`,
  cut into 16 cells of 64 by 64. The game builds its material once, from a
  path string that sits in writable memory [V]. The mod could point that path,
  or the cached material, at its own art without touching the game folder [I].
- The game asks the control map which input an action uses, through
  `fn_005C78F0` `0x005C78F0`. So the hint follows the player's own bindings.
  This one function is the place to say "the controller" instead of "the
  mouse" [V].
- The key cap look needs no art. Any wide string goes into the cap [V]. The
  mod converts each mouse hint to a key cap with the word of the controller
  control ("Trigger", "Grip") and hides the hints for inputs that the
  controller does not have.
- The help text says "Action Button" 129 times and "left mouse" or "right
  mouse" zero times [V]. So most words need no change. The mod changes about
  16 lines (the mouse names, the wheel, the keyboard) in memory.
- With a controller, no hint of the game draws in an eye picture. The words go
  to a hint card, a small quad layer beside the wrist of each hand. The card
  also shows prompts for what the controller can do now. A hint of the game
  that is false with the controllers does not show.
- A text of the game can wait for a click of the left button. The prompt of
  the game for that click shows on the card in each view, and the trigger is
  the click (section 10).
- Not built: glyph pictures of the buttons, a picture of the controller with
  the button lit, and the runtime's 3D model (section 5).

## 1. How the hint icons work

### 1.1 The draw function

`CameraHelp::DrawKeyOrMouse` is `__cdecl` with 11 arguments. The symbol gives
the types: `(CH_ANIMTYPE, int, int, wchar_t*, int, int, int, KEYALIGN,
LH3DColor*, LH3DColor*, int)` [V, `symbols.txt`]. The mod (`draw_hooks.cpp`)
names the first three `blink`, `button` and `row_or_label`.

| Arg | Mod name | Meaning | Evidence |
| --- | --- | --- | --- |
| 1 | `blink` | Blink style. `-1` means "draw a key cap". Otherwise it goes to `fn_00447450` as its third argument, where `1`, `2` and `3` pick a blink pattern from `GetTickCount` and any other value is steady. | `0x00448011` compares it to `-1`. `0x00447486` to `0x004474EC` is the pattern switch [V] |
| 2 | `button` | The mouse button code (section 1.2). In key cap mode it is a key code with modifier bits, or `0` to use the string in arg 3. | `0x00448019` to `0x0044807A`, and the call `fn_00447450(arg2, arg3, arg1, ...)` at `0x0044859E` [V] |
| 3 | `row_or_label` | The row of the mouse picture: `0` two buttons, `1` three buttons, `2` wheel mouse. `3` means "no picture, text only". In key cap mode it is a pointer to the key name. | `fn_00447450` exits at `cmp ebx, 3` (`0x00447455`). `0x00448122` sets the icon width to 0 when it is 3. `0x0044802A` to `0x004480EB` copies arg 3 with `wcscpy` in key mode [V] |
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
3. Text only: arg 3 is `3`. This is the tooltip look.

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
alpha plane [V]. The atlas is a 4 by 4 grid:

| Row | Cells (left to right) |
| --- | --- |
| 0 | Two button mouse: plain, right lit, both lit, both lit |
| 1 | Three button mouse: plain, right lit, both lit, middle lit |
| 2 | Wheel mouse: plain, right lit, both lit, wheel lit (this is cell 11) |
| 3 | Wheel mouse with the wheel scrolling, four frames (cells 12 to 15) |

The UV code uses steps of `0.25` [V, `0x004475D0`]. So a cell is 64 by 64
pixels.

**Which mouse row the game picks.** `fn_005C4800` `0x005C4800` returns 2 when
the global `0x00E85248` is set (a wheel mouse), else 1 or 0 by a second global
(`0x00E8524C`, the button count) [V]. It runs on the player's real hardware.

### 1.4 Where the values come from

`fn_005C78F0` `0x005C78F0` is the choke point. It takes an action number and
fills three outputs [V]:

- If the action is bound to a mouse button: arg 2 is `0`, arg 3 is the button
  code, and arg 4 is the mouse row.
- If the action is bound to a key: arg 2 is `-1`, arg 3 is `0`, and arg 4 is a
  pointer to the key name (a wide string at `0x00D16110`).

Its six callers [V, `--callers`]:

| Caller | What it makes |
| --- | --- |
| `ToolTips::SubmitToolTips` `0x005C9A70` (at `0x005C9BC4`) | The tooltip beside the Hand for an action. The node goes into a list of `GKMIcon` records. |
| `GInterface::CalculateOverObject` `0x005C9FC0` (at `0x005CA07C`) | The tooltip for the object under the Hand. |
| `HelpSystem::Draw3D` `0x005C59A0` (at `0x005C5A45`) | Tooltips when the help system draws in the 3D pass. |
| unnamed, `0x005C5D63` | The same, in the help callback. |
| `fn_00457041` (at `0x004571AD`, the draw at `0x0045729D`) | A hint from that function. The purpose is not known [?]. |
| `fn_005760C0` (at `0x0057622C`, the draw at `0x005763CC`) | The icon inside a speech bubble. |

**The speech bubble tokens.** `fn_005760C0` reads two tokens in the help text:
`$m<number>` and `$g<number>`. `$m` asks `fn_005C78F0` for the action and
draws the icon with `DrawKeyOrMouse` (arg 4 is the buffer at `0x00C4CD30`).
`$g` draws a gesture glyph through `fn_005763E0` [V, the `0x24`, `0x6D` and
`0x67` compares at `0x005760C7` to `0x005761F6`].

### 1.5 Every caller that draws a hint

The direct callers of the two draw functions [V, `--callers`]:

| Function | Caller | Address | What it is |
| --- | --- | --- | --- |
| `DrawKeyOrMouse` | `fn_00448AC0` | `0x00448BD4` | One `GKMIcon` tooltip node. `fn_00447850` walks the list at `0x00C5AFE0` and calls it for each node [V]. |
| `DrawKeyOrMouse` | `fn_00457041` | `0x0045729D` | See above [?]. |
| `DrawKeyOrMouse` | `fn_005760C0` | `0x005763CC` | The bubble icon. |
| `fn_00447450` | `DrawKeyOrMouse` | `0x0044859E` | The picture inside every hint above. |
| `fn_00447450` | `fn_00447850` | `0x004478CD` | The screen edge hint: a picture with no text. Its button code, row, blink flag, position and fade are globals at `0x00C5AFBC` to `0x00C5AFDC`. Only `fn_00447850` reads them [V]. What writes them is not traced [?]. |
| `fn_00447450` | unnamed | `0x005129BB` | The controls page of the options. Each row shows the bound button picture [V by the mod's own comment in `draw_hooks.cpp`]. |

`fn_00447850` itself has these callers [V]: `HelpSystem::Draw3D`
(`0x005C5B31`), the help callback (`0x005CCE41`), `FrontEnd::JustDoABox`
(`0x005451A4`) and an unnamed one (`0x0053CDBB`).

**Producers of a `GKMIcon` node.** `fn_004489D0` adds a node and
`??_GKMIcon` `0x00448C00` removes it. The adders are `HelpSystem::Draw3D`,
`fn_005C5C5C`, `fn_005C9D00` and one unnamed caller (`0x005C5E37`) [V]. The
removers include `HelpSystem::ClearTextDisplayed`, `ResetIcons` and
`ProcessInterface` [V]. So the help system owns the hint list. A real hint
reaches the mod through `fn_00448AC0` (return `0x00448BD9`) [M], with button 1
for the land and button 2 for a pile of the store.

### 1.6 The hints that matter in play

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

What the controller does for each action is in `controllers.md`. The prompts
of the card (9.4) say what is true now. A hint for an action that the
controller cannot do must not show. The tilt hint and the keyboard hints are
the clear cases.

## 2. Ways to swap a mouse icon for a controller icon

Four ways, from the cheapest.

**A. Change the words only.** Edit the help text at load time. This cannot
change the mouse picture, because the picture comes from the binding, not from
the text. It only helps where words name a device (section 4). The mod does
this (8.4).

**B. Convert the hint to a key cap at draw time.** In the hook of
`DrawKeyOrMouse`, when controllers are in use and the button code is one of
`1`, `2`, `3`, `4`, `8`, `0x10`, call the original with arg 1 `-1`, arg 2 `0`
and arg 3 a pointer to a mod string such as `Trigger`. The game draws its own
cap and text. No art. It covers hints that go through `DrawKeyOrMouse` [V]. It
does not cover the screen edge hint from `fn_00447850`, which has no text (it
goes through `fn_00447450` only). The mod does this (8.3).

**C. Swap the picture in the game's own atlas.** Give the game a different
`mousehelp` material. Two ways [I]:

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

The game picks a cell from a button code and a row. With code `0`, `2`, `3` or
`4` and rows 0 to 2 the mod can reach cells 0 to 11 with no flip. The codes `8`
and `0x10` reach cells 11 to 15 [V]. That is 17 cells the mod can fill with any
picture. To pick a cell the mod rewrites the arguments of `fn_00447450`, or of
`DrawKeyOrMouse`, in a hook. Code `1` always mirrors cell 1, so the mod must
not send `1` to the game. It sends `0`, `2`, `3` or `4` and puts trigger art in
the chosen cell.

Cost: one fixed 64 by 64 cell. It covers everything that reaches
`fn_00447450`, including the screen edge hint and the options page [V]. It
keeps the game's blink and its box.

**D. Draw the mod's own picture.** Skip the game's icon and call
`SetupThing::DrawBox` `0x00412980` with a material the mod made [V]. Its symbol
is `DrawBox(int x0, int y0, int x1, int y1, float u0, float v0, float u1,
float v1, LH3DMaterial*, LH3DColor*, int, int, int, bool, float)`. The callers
in `fn_00447990` show a normal call: `100.0f`, `0`, `0xA000`, `0xFFFF6000`,
`0`, the color pointer, the material, four floats, four ints [V]. The last five
arguments are not decoded [?]. Advantage: any size, any aspect, any UV, a flip
for the left hand, and art that is not tied to 64 by 64 cells. It needs the
mod to place the picture by hand, next to the game's box. It needs the same
recipe as `fn_00447450` for the material: the loader `fn_008379E0` and
`CreateMaterial(5, ...)` [V for the recipe, I that the mod can call them at the
right time].

The hint card (section 9) is a fifth way: the mod draws the hint on its own
OpenXR layer and the game draws nothing. A picture of a button could go on the
card with no game call.

## 3. Controller art

The mod must not ship art it may not ship. Three levels. None is built.

### (a) Flat icons per profile

A trigger shape, a grip shape, a stick, and a round button for A and B. The
mod draws them from art it owns.

- Effort: small. About 12 cells for a 64 by 64 atlas (trigger, grip, both,
  stick click, stick up, stick down, stick left, stick right, primary face
  button, secondary face button, menu, idle). Draw them once as vector art. One
  atlas covers all controllers that share a layout, and one small atlas covers
  the rest.
- Look: clear and readable at a small size. Not tied to the real controller.
- Legal: safe when the art is original. The controller shapes must be generic.
  Do not copy the logo, the ring or the exact outline of a real product. Do
  not copy icons from Meta, Valve, HTC or Microsoft brand kits. Check the
  license of any public set before use.

### (b) A small picture of the controller with the button lit

A silhouette of the controller family with the used button in a bright color.
This is the "modern VR" look at a small size.

- Effort: medium. One silhouette per family and one lit variant per control.
  Six or seven variants for four families. Draw them with a script from
  primitives (rounded rectangles and circles) so the art is code, not a scan.
- Look: good. The player sees which control to press, not only its name. Each
  cell must stay clear at 64 by 64.
- Legal: safe with own generic silhouettes. Left and right hands mirror one
  another on most controllers, so one picture serves both when the mod can flip
  the UV (needs way D, or two cells, or the card).

### (c) A 3D model of the controller in the player's hand

The real controller model, drawn at the real controller pose, with the button
lit and a label. The runtime can supply the model.

**What the SDK offers** [V, `third_party/OpenXR-SDK`, version 1.1.43]:

- `XR_MSFT_controller_model` (`xrGetControllerModelKeyMSFT`,
  `xrLoadControllerModelMSFT`, `xrGetControllerModelStateMSFT`). The runtime
  gives a binary glTF and per node poses that move the button and stick nodes.
- `XR_FB_render_model` (`xrGetRenderModelPropertiesFB`,
  `xrLoadRenderModelFB`). The runtime gives a glTF for a path such as a
  controller.

**What a runtime supports.** `XR_MSFT_controller_model` is a Windows Mixed
Reality feature. `XR_FB_render_model` is a Meta feature [I]. Virtual Desktop's
32 bit OpenXR runtime lists neither [M]. `xr_screen.cpp` asks for only
`XR_KHR_D3D11_ENABLE_EXTENSION_NAME` and logs the extensions that the runtime
lists. The models may use compressed textures (KTX2 with Basis) [I]. A viewer
needs a glTF reader (tinygltf or cgltf, both MIT) and a Basis transcoder
(Apache 2.0) [I].

**Legal.** The runtime hands the model to the app at run time. The mod does not
ship it and must not save it into the mod folder or the repository. If a
runtime offers nothing, do not ship a copy of a maker's model. Options that
stay legal: a generic controller mesh the author makes, or the silhouette art
from (b).

**Other cost.** The mod needs a small glTF renderer on the Direct3D 11 side
(`xr_screen`), a place in the frame for it, and a link from the hint code
("hint active: this button"). It needs a model per hand and node names for each
button, which differ by maker. The model stays still with the controller. Only
the lit part changes. A model in the hand can hide the Hand of the game, so
draw it beside the Hand, or only when the Hand is idle.

**Controller families.** The mod uses the profile string from
`XrScreen::HandProfile` (`FamilyOf` in `controller_hints.cpp`):

| Family | Profiles | Layout |
| --- | --- | --- |
| Touch like | `oculus/touch_controller`, newer Meta touch profiles, Pico. Also the fake controller and any unknown profile. | Trigger, grip, thumbstick, two face buttons, menu (left hand) |
| Index | `valve/index_controller` | Trigger, grip, thumbstick, trackpad, A and B |
| Wand | `htc/vive_controller` | Trigger, grip, trackpad, menu. No thumbstick. |
| Mixed reality | `microsoft/motion_controller` and other Windows Mixed Reality makers | Trigger, grip, thumbstick, trackpad, menu |
| Simple | `khr/simple_controller` | Select and menu only |

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
| "keyboard", "key", "Escape", "Space Bar" | about 28 | Hide or reword (see 8.4). |

Fixed words for the options page [V]: `HELP_TEXT_ACTIONS_LEFTMOUSEBUTTON` is
"LMB", `..._RIGHTMOUSEBUTTON` is "RMB ", `..._MIDDLEMOUSEBUTTON` is "MMB",
`..._WHEEL_UP` is "Wheel Up" and `..._WHEEL_DOWN` is "Wheel Down". These are
the names of the five mouse inputs in the controls page.

### 4.2 Voice lines

Most narrator lines have a spoken voice, tied to the text by the key name
(`docs/input-devices.md`). The voice says "Action Button". If the subtitle said
"trigger", the eye and the ear would disagree. So the mod keeps every spoken
line as it is. It does not replace "Action Button". It changes the icon beside
it.

### 4.3 How the text changes without shipping Lionhead's text

The game reads the help text at load. `GSetup::LoadTextScripts` `0x00719280`
calls `LHScriptX::Load` with the path `scripts/InfoScript2.txt` and a handler
at `0x007192E0` [V]. The handler reads each `ADD_TEXT` line and calls a method
of `HelpTextDataBase` (object `0x00D17CA8`) at `0x005CCF80`, which calls
`0x005CAD00` [V]. The mod hooks `0x005CAD00` (8.4). It ships only its own
sentences and the key names (identifiers). It never ships a Lionhead sentence.
Everything happens in memory on the player's own file. Nothing is written to
disk.

## 5. Plans for pictures

The words and the card are built. These parts are open, in the order of effort:

1. **Flat glyph atlas.** Replace the mouse picture in every hint by a
   controller glyph, including the screen edge hint and the controls page.
   Make the atlas by script: a 256 by 256 BGR file plus a 256 by 256 alpha file
   per family (cells of 64 by 64), in the mod folder. Point the game at the file
   (section 2, way C). Rewrite the button code and row so the game reads the
   cell the mod wants, and never send code `1`. Keep the blink and the game's
   box. About 12 glyphs per family. Risks: the loader `fn_008379E0` may check
   the file size or a flag (`0x41`), and the alpha name is inferred [?] (test
   with a copy of the original atlas first). The path string has room for about
   27 characters if written in place, so use a pointer to a mod string. The game
   builds the material once, so a late switch needs the pointer swap and a safe
   release of the old material [?]. A 64 by 64 cell at the 20 percent hint size
   is about 13 pixels, which may be too small. The atlas also shows on the
   options controls page, so switch it by input state.
2. **A picture of the controller with the button lit.** One silhouette per
   family, one lit control per variant (section 3, level b), with a bright
   accent on the used control. Show it through the atlas if 64 by 64 is clear.
   If not, show it through `DrawBox` with an own material (way D) or on the
   hint card. The Touch controller shape on a Quest 3 differs from the Touch
   Plus shape, so use one generic shape for the family.
3. **The runtime's 3D model.** Ask for the extension at `xrCreateInstance`
   only if the runtime lists it. Load the glTF at session start, read the node
   names, and render on the Direct3D 11 side at the hand's grip pose in the
   LOCAL space, only while a hint is active. The model has no motion that the
   player did not make. Fall back to the picture of item 2 when the runtime has
   no model. This is the largest item. The main risk is that the runtime offers
   no model (it offers none now, section 3).

The card of section 9 is the place for items 2 and 3: they draw on the XR side
at the grip pose, only while a hint is active, with no motion that the player
did not make.

## 6. Risks that cut across

- **The controller mapping can change.** The label table reads the same mapping
  as `controllers.cpp`, from one place (`BuildWords`). A hint must never show a
  button that does nothing.
- **Two hands.** The pointer hand decides the profile and the labels. If the
  player switches hands, the hint follows. If the hands are different kinds
  (rare), the pointer hand wins.
- **Quest 3 profile.** The Quest 3 may report a newer Meta profile or the older
  Touch one, depending on the runtime. The mod logs the profile
  (`controllers.cpp`). Unknown profiles use the Touch like words.
- **Stereo.** A flat hint in the eye pictures needs the once per pass rule and
  a place that is right in both eyes. The hint card avoids this (9.1).
- **Live A/B.** `hint_card` and the test key of 9.5 switch the card and the
  hints in the eye pictures for a live comparison. A key that cycles pictures
  would be needed for items 1 to 3 of section 5.

## 7. Open questions

1. What action numbers `$m1` and `$m2` are, and what `fn_00457041` draws. The
   census (8.2) answers both in a play session with `help_test=1`.
2. What writes the screen edge globals at `0x00C5AFBC` to `0x00C5AFDC`. Likely
   the `CameraHelp` update code at `0x00449xxx`, which reads and writes through
   a base register [I]. The census shows the values without reading the writer.
3. Which arguments of `DrawBox` after the material are needed for an own
   picture (way D).
4. Whether the loader `fn_008379E0` accepts a file from the mod folder and how
   it names the alpha file.
5. Whether a hint can stay a 64 by 64 cell at the size the mod uses in a
   headset. Only a headset test answers this.

## 8. The words

Code: `src/mod/controller_hints.cpp` and `.h` (the words, the census, the help
text), the hook `h_DrawKeyOrMouse` in `src/mod/draw_hooks.cpp`, and
`ControllersHintState` in `src/mod/controllers.cpp`. Keys of `bwvr.ini`:

| Key | Default | Meaning |
| --- | --- | --- |
| `controller_hints` | 1 | 0 leaves the hints and the help text as the game makes them |
| `hint_profile` | empty | Test key. Forces a profile string, for example `htc/vive_controller`. Empty takes the profile the runtime reports. |
| `hint_selftest` | 0 | Test key (8.5) |
| `help_test` | 0 | Test key. Turns the game's help on in memory (`ui-layer.md`, 15.1). |
| `hint_mode` | 1 with head tracking, else 2 | What to do with the two copies the game draws of a picture hint: 0 draw both, 1 keep the copy at the Hand, 2 keep the pinned copy (`draw_hooks.cpp`) |
| `controller_swap_buttons`, `gesture_button`, `grab_world` | `controllers.md` | Change the words (the trigger and the grip swap, the gesture word, the word of the world grip) |

### 8.1 When the controller words show

Only while `ControllersOwnPointer()` is true and `controller_hints` is 1. The
state is latched once a frame, when the left eye pass starts
(`UiDepthSetEye(0)` calls `ControllerHintsPass`). So both eyes always agree.
With the mouse as the owner, with `controllers=0` or with
`controller_hints=0`, no hook changes anything.

### 8.2 The census

Three read only hooks and a table of 128 distinct calls. Each call goes in the
log once (`hints : census ...`). The counts go in the log again every 1200
frames, if they changed.

- `fn_005C78F0` (action, kind, code, row or label): the ask.
- `fn_00447450` (7 arguments, cdecl): the picture. The screen edge hint is the
  call that returns to `0x004478D2`.
- `DrawKeyOrMouse`: return address, blink, button, look (cap, text only or
  mouse picture) and the caption.

The log also has the profile of the pointing hand, the profile used, the family
and every word (`the pointing hand reports ...`). With the VR start it also
lists the OpenXR instance extensions of the runtime, and says if it has
`XR_MSFT_controller_model` or `XR_FB_render_model`.

The screen edge picture is called in every eye pass with button 0, row 0 and
blink 0 [M].

### 8.3 Key caps

`h_DrawKeyOrMouse` acts when the controller words are on, inside an eye pass,
not in a menu, not from the speech bubble, and with `hint_mode` not 0:

1. A key cap of the game (blink -1) is hidden. It names a keyboard key.
2. A hint with a mouse picture (row 0 to 2) gets a label from
   `ControllerHintLabel(button)`. If there is no label, the hint is hidden.
   Otherwise the call becomes a key cap: blink -1, button 0, row a pointer to
   the label.
3. The call then takes the tooltip path. With the hint card on (9.2), it goes
   to the card as words. With `hint_card=0` it is kept once in a pass, moved as
   a whole and drawn flat (`ui-layer.md`, section 15). The two copies of the
   game are one call after the change, so each eye draws exactly one cap.

The labels come from one function, `BuildWords`. It follows `xr_screen.cpp`
(which controls a profile has) and `controllers.cpp` (what each one does):

| Button code | Word | Hidden when |
| --- | --- | --- |
| 1, the left button | Trigger (Grip with `controller_swap_buttons`) | never. The Simple profile says "Select". |
| 2, the right button | Grip (Trigger when swapped) | Simple has no grip |
| 8 and 0x10, the wheel | Stick ("Pad click" on Wand, because its pad works only when clicked) | Simple has no stick |
| 4, the middle button | none | always, it has no controller input |
| 3 | none | always, the control map never gives it |

`BuildWords` also has the words for the prompts (`ControllerHintWord`):

| Word | Touch like | Index | Other profiles |
| --- | --- | --- | --- |
| jump | A, or X on the left hand | A | none |
| gesture (`gesture_button`) | B, or Y on the left hand | B | the stick click, or the pad click on Wand (`gesture_button=2`) |
| menu (the game menu, on the other hand) | Y, or B when the left hand points | B | Menu |
| world (the grip of the other hand, with `grab_world`) | Grip | Grip | Grip (not Simple) |
| ring (the control of the other hand that opens the ring menu) | X, or A when the left hand points | A | the stick, when only the stick opens the ring |

The screen edge picture is hidden while the words are on, because it teaches
the mouse at the edge of the screen. The bubble icon (return address
`0x005763D1`) is not changed.

### 8.4 The word list

The hook is on `0x005CAD00`. It stores one `ADD_TEXT` line: `entry+0` and
`entry+4` are two numbers, and `entry+8` is the text copy. The key name of the
line is the third argument and the text is the fourth. For a line that the mod
words differently, the hook keeps a row (the slot and the game's own copy).
When a profile is known, it makes a second copy with the game's allocator
(`0x007DB790`). A frame latch switches the pointer at `entry+8` between the two
copies. So the game's own free stays valid, and the mouse words come back when
the mouse owns the pointer. The mod holds only its own sentences and the key
names.

- Four mouse names (`..._LEFTMOUSEBUTTON`, `..._RIGHTMOUSEBUTTON`,
  `..._WHEEL_UP`, `..._WHEEL_DOWN`) use the label table.
  `..._MIDDLEMOUSEBUTTON` stays.
- Five keyboard lines (`HELP_TEXT_HAND_DEMO_USE_KEYBOARD_01` to `_04` and
  `HELP_TEXT_TUTORIAL_LAND_108`, with `$m7` to `$m10`) and two long wheel lines
  (`HELP_TEXT_DYK_18`, `HELP_TEXT_JUST_IN_CASE_37`) get one short sentence of
  the mod, for example "Push the stick to turn and zoom."
- Any other line with the whole words "mouse wheel" or "the wheel" gets "stick"
  ("pad" on Wand) in their place. Five lines match.
- The camera lessons of the tutorial and the hints about them tell of the
  Move Button at the edge of the screen. In the God view the stick turns, and
  the head looks up and down (`god-view.md`, section 7, "The turn lesson").
  The turn lesson line (`HELP_TEXT_FINAL_BEGINNING_44`) gets "Push the stick
  left or right to turn.", and the turn hint (`HELP_TEXT_NEW_INTERFACE_03`)
  "You know, Boss, you can turn with the stick." Eight lines about looking up
  and down get a sentence about the head: the tilt lesson
  (`HELP_TEXT_FINAL_BEGINNING_50`, "Look up and down with your head."), its
  hint (`HELP_TEXT_NEW_INTERFACE_04`) and the six hints for a look at the sky
  (`HELP_TEXT_INTERFACE_LOOK_AT_SKY_01` to `_06`). The voice of the spirits
  still says the game's own words.
- The Escape lines (`TUTORIAL_LAND_09`, `_10`, `_40`, `_106`,
  `CITADEL_GUIDE_13`) and "Action Button" stay. The menu button has no job
  there.
- A profile with no stick (Simple) leaves the wheel lines as they are.

The log lists each new line once. A line that the game changes later is left
alone. The mod's sentences are not longer than the originals, so they fit the
text boxes that the game sizes for the original.

### 8.5 Tests

A desktop run seldom shows a real hint. The test key `hint_selftest=1` draws
five hints of its own after the help layer, two copies of each, as the game
does: a left button, a right button, a middle button, a wheel hint and the key
cap of a keyboard key. Use the fake controller (`fake_controller=1`,
`controller_hand=1`), `hint_profile=htc/vive_controller` and
`ui_scale_pct=100`.

- With the mouse as the owner, the four mouse hints and the "Space" cap draw as
  the game draws them.
- With the controller as the owner, caps with "Trigger", "Grip" and "Pad
  click" draw once in each eye at the same place. The middle button hint and
  the "Space" cap do not draw. Each pass has three converted hints and two
  hidden ones.

To see real hints, use `help_test=1` (the profile of a test PC may have the
game's help options at 0, `ui-layer.md`, 15.1). Over the land the game shows
"Move" and a key cap "Trigger". With an object in the Hand it shows "Drop". Over
a wood pile of the store it shows "Wood Amount: 30000" and a key cap "Grip". A
bulk take shows "Amount: 57" and on, as text only. The words of the help text
(8.4) were not seen in a run, because no help text line showed.

### 8.6 What needs the headset

- Whether the word in the cap reads at the real size.
- The switch of the help text when the player puts the controller down and uses
  the mouse (it works in the desktop runs, at the start).
- A census of a play session (section 7).

## 9. The hint card

Code: `src/mod/hint_card.cpp` and `.h` (the cards), `src/mod/hint_card_math.h`
(the place, the true hints, the prompt rule, the fade), the route in
`h_DrawKeyOrMouse` of `src/mod/draw_hooks.cpp`, the quad layers in
`src/vr/xr_screen.cpp`, and `ControllersCardPose` in `src/mod/controllers.cpp`.

### 9.1 Why a card

A flat hint in the eye pictures shows two times when the two pictures do not
agree. Each rule that gave a flat hint a place had a case where they did not:

| Case | What happens |
| --- | --- |
| A hint with no disparity | The same pixel in both eyes is at infinity. The eyes look at the hand, 0.3 to 0.6 m away, so they see that hint two times. This happens when the Hand has no point in the view: in a land drag, in a Tug, with a Miracle, and with the hand out of the view. |
| A hint that changes place | Each time the state of the Hand changes, a hint at the Hand and a hint at the game's 2D place give two places. |
| A hint in the Hand | At the depth of the Hand's point, the Hand's mesh hides a part of the hint, and a different part in each eye. The game draws the Hand after the help. |
| One eye at the Hand, one eye flat | A Hand's point in the view of one eye only gives a hint at the Hand in that eye and a flat hint in the other eye. At 0.48 m that band is 138 pixels wide at each side of the view. |
| One frame late | The tooltip list draws in the walk, before the glow sets the Hand's point of that frame. So the hint moves against the Hand. |
| Less than 90 frames a second | The headset shows each game frame two times, and anything in the eye pictures that moves with the hand shows two times while the hand moves. |

No rule that puts a flat hint in two eye pictures is safe. A quad layer shows
at one place of the room to both eyes, so the hint cannot be double.

### 9.2 One card in the room

While a controller owns the pointer (`ControllerHintsActive`) and `hint_card`
is 1, no hint of the game draws in an eye picture:

1. `UiDepthSetEye(0)` latches the route for the frame (`HintCardRoute`), so
   both eye passes agree.
2. `h_DrawKeyOrMouse` gives each hint that took the tooltip path to
   `HintCardGameHint` as words: the controller word (for a hint that had a
   mouse picture) and the caption. Then it returns. The game draws nothing, and
   its callers run their fades as before.
3. After both eye passes (`UiDepthSetEye(-1)`), `HintCardEndFrame` draws the
   words of each card into a picture of 768 by 320 pixels with GDI (Segoe UI):
   a dark card, white letters, and the button word on a badge. The trigger
   badge is green and the grip badge is gold, as the glow at the Hand's point.
   Other buttons have a light badge.
4. `XrScreen::StageCard` gives the picture and its pose to
   `XrScreen::EndFrame`. That makes a small swapchain at the first layer of a
   card, writes the part that shows, and adds one quad layer for the card in
   the LOCAL space, after the projection layer and the panels.

The runtime puts the quad again for each displayed frame from the head pose, so
a slow frame of the game does not double it when the head moves.

The card needs no interface panel, so it works with `ui_panel=0`. The hints
that are hidden stay hidden: a key cap of a keyboard key, a mouse button with
no controller input, and the screen edge picture. The icon in a speech bubble
and a hint in a menu do not change. With the mouse as the owner, with
`controller_hints=0`, with `hint_card=0`, and in the stereoscopic window (a
headset with no projection layer), the hints draw in the eye pictures. If the
runtime refuses the swapchain, the hints go back to the eye pictures too.

### 9.3 The place of the card

`CardPose` in `hint_card_math.h`, from the grip pose of the hand of the card and
the head center, both in the LOCAL space (`ControllersCardPose`). The card uses
no game transform, so it is at the real wrist at each scale of the world.

- The hand: `HandFromGrip` and `HandOutline` use the test hand of the overlay
  (`overlay_math.h`). The wrist is 9.3 cm from the grip toward the forearm. The
  outline is the corners of the palm and the five tips of the open fingers.
- The side: the card is beside the wrist as the head sees it, level with it,
  and at its distance from the head. `ext` is how far the outline goes to that
  side of the wrist in the view. The center is `ext` + 3 cm (`kGap`) + half the
  width from the wrist.
- By construction, each point of the hand is at `ext` or less, and the card
  starts at `ext` + 3 cm. So the card is not in the hand in any pose, it does
  not cover the hand in the view, and it is not on the place where the fingers
  work. The runtime draws the quad over the eye pictures, so the Hand cannot
  hide the card.
- The card starts on the outer side: right of the right wrist, and left of the
  left wrist. A hand at the right of the body that points ahead has its fingers
  toward the middle of the view. When the fingers point far to the side of the
  card (the extent on its side is 4 cm more than on the other side,
  `kSideExcess`), the card changes side, one time. It changes back only after
  the same excess the other way.
- The card is square to the line of sight to the wrist, with no roll. It goes
  with the hand one to one. It has no motion of its own: no glide and no follow
  delay. The card fades in in 0.25 s and out in 0.35 s, in place.
- The size: 4000 pixels of the picture are 1 m (40 pixels a cm), so a capital
  letter is about 8 mm high and a card with one line is 2.2 cm high. That
  letter is 0.8 degrees at 55 cm, the length of an arm. Most cards are 7 to
  14 cm wide. A wider card stops at 19 cm. `hint_card_size_pct` scales all of
  it. The card has an alpha of 0.86 and round corners.

A card on the forearm side of the wrist in the room (away from the fingers and
up) would be between the eyes and the hand when the fingers point along the
line of sight, which is the usual reach into the land. A card under the hand in
the view would go past the finger tips when the fingers point down, and then it
would cover the object that the Hand touches. So the card is beside the wrist.

### 9.4 The prompts

The card also shows what the controller can do now, as a button word and a
short order. The context comes from the mod's own state. `ContextNow` reads it
once a frame, and the first match counts, in this order:

| Context | Source | Prompt |
| --- | --- | --- |
| A dialog box | `ControllersMenuRay` | Trigger: "Select" |
| A room of the Temple, with the ray | `TempleCardRoom` (`temple.cpp`) | "Select" (the trigger with `temple_click=1`, else the grip), the ring word: "Rooms", the menu word: "Leave the Temple" in the hall, "Back to the hall" in other rooms |
| The teleport aim | `TeleportAiming` | A: "Release to go" |
| A miracle, or a one-off seed, in the Hand | `MiracleHint` (`miracle.cpp`) | The lines of `miracle.cpp`: what the grip does with this kind of miracle, why the game refused the last cast, and how to cancel (the gesture word and the shake) |
| The totem of the Village Centre or a miracle icon of a Worship Site | `VillageHint` (`village.cpp`) | The lines of `village.cpp`. They stay while the Hand is there. |
| The door of the Temple or a leash post, on the land | `TempleLandHint` (`temple.cpp`) | The lines of `temple.cpp` |
| The trigger is held with the arc of a Creature command | `CreatureCommandHint`, aim | Trigger: "Release to send" |
| A far target that the Hand can take | `FarGrabDrawState`, active and valid | Grip: "Pull to fetch". "Activate" for a scroll, "Hold to collect" for a bulk source. For the player's own Creature: "Put the leash on", "Take the leash off" or "Take the leash back", and with `creature_hold_stay=1` a second line, Grip hold: "Make it stay". While the grip holds the Creature in a stay, the one line is Grip: "Open to let it go". With the leash on, the trigger lines of a command target are added. |
| An object under the aim that the Creature can act on, with the leash on | `CreatureCommandHint`, object | Trigger: "Send the Creature". Trigger x2: "Tie the leash", when the leash can go on it. |
| The player's own Creature under the aim, with the leash on | `CreatureCommandHint`, Creature | Trigger: "Take the leash off" |
| A leash tied to an object, with the aim on that object or on the Creature | `CreatureCommandHint`, tied | Trigger: "Take the leash back". Trigger x2 when the aim is on the object. |
| The Hand holds an object | `HandDirectGlowFacts`, holding | Grip: "Open to drop". In a forest, or with the bundle of a take from a forest: the line of `forest_take.cpp`. |
| An object to take | hand mode 9 | Grip: "Take" |
| A pile, a field or a fish farm | hand mode `0xD` | Grip: "Hold to collect" |
| The Hand touches the Creature | hand mode `0x13` | Grip: "Stroke or slap" |
| The land, with the leash on | hand mode 3 with the leash | Trigger: "Send the Creature", A: "Teleport" |
| The land | hand mode 3 | On the card of the other hand: the world word "Move the land" (the grip of the other hand, in the God view and on the table), the ring word "Ring menu", and the first time the menu word "Game menu". On the card of the pointing hand: A "Teleport" (or "Jump" with `teleport=0`). In the old immersive view the trigger moves the land, so Trigger: "Move the land" shows on the card of the pointing hand. |

The words come from `ControllerHintWord`, the same table as the key caps
(8.3), so they follow the profile, the left hand and `controller_swap_buttons`.
A line for a button that the controller does not have is left out. Outside
the contexts of a target, a line that has the same button word as a hint of the
game on the same card is left out (the hint of the game says what the button
does now). For a target, see "One text for one action" below. The Hand's mode is latched in
the left eye pass, in the `HelpText` callback, after the world is drawn
(`HintCardLatchContext`).

A prompt must help and must not stay in the way (`PromptStep`). Each context
has a dwell, a hold time and a count (`kRules` in `hint_card.cpp`):

1. The context must hold for the dwell before the prompt shows: 0.3 s for most
   contexts, 0.5 s for a dialog or a room of the Temple, 0.6 s for an object in
   the Hand, 1.0 s for the land with the leash on, 1.5 s for the land. A hand
   that moves across objects shows nothing.
2. The prompt shows for the hold time, 4 to 6 s. Then it goes, and it stays
   away until the context ends and starts again.
3. A context shows its prompt 2 to 4 times in a session. After that the player
   knows it.
4. Outside the contexts of a target, a hint of the game for the same button on
   the same card takes the place of the prompt, and that does not count as a
   show.

**One text for one action.** The hint of the game for the button of a target
comes some frames after the target. With `hint_words` 1, 2 or 3 (3 is the
default) the prompt of a target owns its button for as long as the target is
there: the
hint of the game for that button does not show, so the words of the card do
not change while the target and the action stay the same. The mod chooses its
own prompt because it is there in the first frame and it names the action of
the controller. The hints of the game for other buttons, and the hints with no
button (for example the name of a miracle), stay. For the contexts that are
not about a target (an object in the Hand, the empty land, a dialog box, the
teleport aim) the hint of the game still takes the place of the prompt, and it
is the only text there when it came before the dwell of the prompt ended. With
`hint_words=0` the hint of the game takes the place of the prompt in every
context.

**The take in work.** After the grip closes on an object to take, the game
puts the Hand in its pick up state (state 3), and some frames later the Hand
holds the object (9 frames in a desktop run). In state 3 the mod has no facts
of the Hand, so the context is none. The game gives its hint of the take
("Pick Up") in those frames, and for some frames more while the Hand already
holds the object (6 in a desktop run). Then it gives the hint of the object in
the Hand ("Drop").

With `hint_words` 2 or 3 the context of the take stays for those frames
(`TakeKeepStep` in `hint_card_math.h`). So the card says "Grip Take" from the
frame in which the target comes until the hint of the object in the Hand
comes. The rule:

1. It starts when the grip closes while the card has the prompt of the take,
   or less than 0.15 s after that prompt showed (`kLingerSeconds`). A grip that
   closes on nothing, or on a target with a different prompt, starts nothing.
2. It holds while the Hand is on the object, and while the Hand is empty and
   on no target. It also holds while the Hand has the object and the game
   still gives the hint that the prompt owned.
3. It ends when a different context comes, or when the grip opens. It also
   ends 1.0 s after the close (`kTakeKeepSeconds`), when the game did not take
   the object.

The hints of the game for other buttons, and the hints with no button, show
all through the take. With `hint_words=1` the hint of the game for the grip
shows from the close of the grip until the hint of the object in the Hand
comes.

**The fetch in work.** A fetch from a distance ends as a take does. The far
target goes when the game puts the object in the Hand (`far_grab.cpp`). The
Hand is then in its pick up state, and the mod draws the object on its way to
the Hand (the glide, `grab.md`). The game gives its hint of the take ("Pick
Up") in those frames. In a desktop run it gives "Pick Up" for 16 frames after
the far target went, then no hint for the grip for 3 frames, and then the hint
of the object in the Hand ("Drop").

With `hint_words=3` the context of the fetch stays for those frames
(`FetchKeepStep` in `hint_card_math.h`). So the card says "Grip Pull to fetch"
from the frame in which the far target comes until the hint of the object in
the Hand comes. The rule:

1. It starts when the far target goes while the card has the prompt "Pull to
   fetch", or less than 0.15 s after that prompt showed. The grip must be
   closed, and the object must be on its way to the Hand
   (`FarGrabGlideOrigin`: the game took the press of the fetch). A far target
   that goes for a different reason starts nothing. A far target with a
   different prompt (a scroll, a bulk source, the Creature) starts nothing.
2. It holds while the Hand is empty and on no target. It also holds while the
   Hand has the object and the game still gives the hint that the prompt
   owned. It holds too while the Hand has the object, the object is still on
   its way, and the game gives no hint for the grip (`FetchWaits`).
3. It ends when the Hand has the object and the game gives a new hint for
   the grip. It also ends as the take in work ends: a different context comes,
   the grip opens, or 1.0 s passed since the far target went.

With `hint_words` 2 or less the hint of the game for the grip shows from the
end of the far target until the hint of the object in the Hand comes.

**After the prompt went.** The card has the hint of the game for the button of
a target for some frames after the target went: 1 to 11 frames in desktop
runs, at most 0.17 s. The last 4 of those frames are the hold of the card
(`kHoldFrames`). No prompt owns the button in those frames. With
`hint_words=3` a hint of the game that a prompt owned does not show for 0.25 s
after the prompt went (`kOwnedSeconds`, `OwnedStale` in `hint_card_math.h`),
while the card has the same button and the same text with no break. So the
card keeps the words of the target for the linger time (see below) and then
shows nothing. It does not show "Pick Up" and then "Take" again. A change of
the target for one frame does not change the words.

The rule hides only the hint that the prompt owned:

1. A hint with a different text shows at once.
2. A hint that comes after a frame with no such hint shows at once.
3. A hint that the game still gives 0.25 s after the prompt went shows then:
   it is true for the place of the Hand. In a frame of the hold of the card
   the game gives the hint no more, so the hint does not show in that frame.

With `hint_words` 2 or less the hint of the game shows in the frame after the
target went, and the words of the target come back for the rest of the linger
time when the hint goes.

A hint of the game with no words (the text has no letter and no digit, for
example "," or ".") never goes to the card with `hint_words` 1, 2 or 3.

**The prompt of a target follows the target.** With `prompt_timing=1` (the
default) rules 1 to 3 do not apply to a context about a thing that the Hand or
the aim is on (`IsTargetContext`, `TargetRule`): a far target, a pile, a field
or a fish farm, an object to take, the Creature, the targets of the Creature
commands and of the leash, the aim of a command, the totem or a miracle icon,
and the door of the Temple or a leash post. Its prompt shows in the frame in
which the context begins, it stays for as long as the target is there, and it
shows each time. When the target changes, the card changes in that frame.

The contexts that are not about a target keep rules 1 to 3, so they do not stay
in the way: the empty land (with or without the leash), an object in the Hand
(its line does not depend on what is under it), a dialog box, a room of the
Temple, and the teleport aim.

A target at the edge: the words of the last target stay for 0.15 s
(`kLingerSeconds`, `LingerOpen`) after it goes, when no other words show. A
new target does not wait for this. The fade of a card that comes is 0.10 s with
`prompt_timing=1` and 0.25 s with 0. `prompt_timing=0` gives the rules 1 to 3
for all contexts, and no linger.

The facts of the frame are the Hand's facts of the same frame: the left eye
pass latches them, and `HintCardEndFrame` reads them after both passes. The
hints of the game for the same button (rule 4) come from the draw of the game
and stay for 4 frames after the last one (`kHoldFrames`).

A miracle in the Hand and the Village lines have no dwell and no limit on
shows: a player with a miracle in the Hand must always see how to cast it and
how to cancel it. With `prompts=2`, rules 2 and 3 do not apply. With
`prompts=0` the card shows only the hints of the game. The prompts do not need
the help options of the game, so a player with "No ToolTips" sees them too.

### 9.5 The keys and the test key

| Key of `bwvr.ini` | Default | Meaning |
| --- | --- | --- |
| `hint_card` | 1 | 1 shows the hints on the card while a controller points. 0 draws them in the eye pictures (`ui-layer.md`, section 15). |
| `prompts` | 1 | 0 no prompts, 1 the first times, 2 always |
| `hint_card_size_pct` | 100 | The size of the card, 50 to 200 |
| `prompt_timing` | 1 | 1 the prompt of a target shows at once, stays while the target is there and shows each time. 0 the dwell, hold and count for all prompts (9.4) |
| `hint_words` | 3 | 1 the prompt of a target owns its button and a hint of the game with no words does not show. 2 is 1, and the prompt of a take also keeps the grip from the close of the grip until the Hand holds the object. 3 is 2, and the prompt of a fetch keeps the grip from the end of the far target until the Hand holds the object, and a hint of the game that a prompt owned does not show for 0.25 s after the prompt went. 0 the hint of the game takes the place of the prompt, as it comes (9.4) |
| `text_click` | 1 | 1 shows the prompt of a text that waits for a click in each view, and makes the trigger that click. 0 gives that prompt the rule of each other hint of the left button (section 10) |

`hint_card.cpp` reads the keys itself, at the first frame. With `test_keys=1`
and `ui_panel=0`, Ctrl Shift F11 switches between the card and the hints in the
eye pictures, for a live comparison. With `test_keys=1`, Ctrl End switches
`prompt_timing` between 1 (a long pulse in the pointing controller) and 0 (a
short pulse). Ctrl Home changes `hint_words` from 3 to 2 to 1 to 0 and back.
The pointing controller gives one short pulse for each unit of the value:
three pulses for 3, two for 2 and one for 1. It gives one long pulse for 0.

### 9.6 The log

- `hint card : hint_card=1, ... prompts=1, ...` once, with the settings.
- `hint card : frame N, the card of the right hand: [Grip] Take. 6.0 by 2.2 cm,
  241 by 86 px, at x y z m, 7.4 cm to the right of the wrist in the view and
  55 cm from the head. The prompt is for an object to take`: each time the words
  or the side of a card change (160 lines at most).
- `hint card : frame N, the pointing card now says: [Grip] Take. The context of
  the Hand is an object to take since frame M, so the card is K frames and T ms
  behind it (prompt_timing=1)`: one line each time the words of the pointing
  card change (400 lines at most). K is the wait of the player.
- `hint card : frame N, the grip closed on an object to take. The prompt of the
  take keeps the grip until the Hand holds the object ...`, and `hint card :
  frame N, the prompt of the take let the grip go K frames after the close:
  the context of the Hand is now an object in the Hand` (or `the grip opened`,
  or `the time limit passed`): one line for the start and one for the end of
  each take in work, with `hint_words` 2 or 3 (60 lines at most).
- `hint card : frame N, the far target went with the grip closed, and the
  object comes to the Hand. The prompt of the fetch keeps the grip ...`, and
  `hint card : frame N, the prompt of the fetch let the grip go K frames after
  the far target went: the context of the Hand is now an object in the Hand`
  (or `the grip opened`, or `the time limit passed`): one line for the start
  and one for the end of each fetch in work, with `hint_words=3`. These lines
  count in the same 60 lines.
- `hint card : frame N, the hint "[Grip] Pick Up" of the game did not show on
  K frames after its prompt went at frame M: the game gives it no more` (or
  `the time limit passed, so it shows now`, or `a prompt owns it again`): one
  line for each hint that a prompt owned and that did not show after the
  prompt went, with `hint_words=3` (60 lines at most).
- `hint card : the hint "[Trigger] Move" of the game does not show: the trigger
  does not move the land here. The grip of the other hand moves it`: one time
  for each of the two false hints (9.9).
- On a capture frame: the line above, a line with the whole pose (the turn, the
  wrist, the finger tips, the grip, the head and the two extents), and two
  pictures for each card beside the eye pictures:
  `bwvr-hintcard-right-NNNNN.bmp` or `bwvr-hintcard-left-NNNNN.bmp` (with its
  alpha) and the same name with `-over` (over a checkerboard).
- Each report window: the frames with the route, the frames with each card, the
  hints of the game that went to the cards, the frames with a false hint that
  did not show, the frames with words and no tracked hand, the frames that a
  card did not show because it was on the other hand, the changes of side, and
  the prompts by context. The line of `hint_words` has the hints with no
  words, the hints that a prompt owned, the closes of the grip on a take, and
  the frames with no target in which the prompt of the take kept the grip. It
  also has the fetches in work, the frames in which the prompt of the fetch
  kept the grip, and the hints that did not show after their prompt went.
  `ui depth : N tooltip calls went to the hint card and
  did not draw in an eye picture`. `XrScreen` counts the quad layers of each
  card.

### 9.7 What is not built

- Glyph pictures of the buttons, a picture of the controller with the button
  lit, and the runtime's model (section 5). The badge with the button word is
  the mod's own art and needs no picture of a real controller.
- Virtual Desktop's runtime lists no controller model extension [M], so a
  model is not possible there.

### 9.8 Tests

The math test (`hint_checks.h`, in `test/controller_math`) covers:

- The hand: the wrist, the mirror image of the left hand, the outline.
- 4000 poses of both hands, with any turn: each one has a card, no point of the
  outline is nearer than 3 cm to the card, each corner of the card is at the
  depth of the wrist, and the card is square to the line of sight with no roll.
- 3000 poses of play (the hand 25 to 60 cm ahead and 20 to 50 cm under the
  head, the fingers ahead and down, turned 40 degrees to each side, rolled 60
  degrees): the card covers no point of the hand in an eye, its center is 8 to
  16 cm from the wrist, and its front is less than 32 degrees from the head.
- The side: fingers that turn to the right change the side one time, at 38
  degrees, and back one time, more than 10 degrees later. Between the changes,
  1 degree of turn moves the card less than 4 mm. A hand that shakes 3 degrees
  never changes the side.
- A wrist straight under the head uses the last direction.
- The size, the prompt rule (dwell, hold, count, a hand that goes across
  objects, always), the rule of a target (no dwell, hold or count), the linger
  and the fade.
- The take in work and the fetch in work: each start, each end, and each case
  that starts nothing. The hint that a prompt owned: 6 frames of it do not
  show at each frame rate from 24 frames a second, and it shows after 0.25 s.
- `GameHintTrue` for each case of 9.9.
- 2000 poses of two hands: each has two cards, more than 95 percent start on
  the outer side, no card is on its own hand or on the other hand, and the two
  cards never meet. A left hand at the card of the right hand, and
  `CardsOverlap`.

The desktop runs to make: the card with the hints of the game (`help_test=1`)
and the card with the prompts only (`prompts=2`). The pictures of the cards are
sharp at 40 pixels a cm.

**Needs the headset.**

- The quad layers themselves: the swapchain of each card, its alpha over the
  world, and two cards in one frame. The desktop has no runtime.
- Whether a letter of 8 mm reads at 40 to 60 cm (`hint_card_size_pct`).
- Whether the side of the wrist is the right place while the hand works, and
  how often the card changes side.
- Whether the card steps against the hand at a low frame rate. The hand pose is
  from the game frame, and the runtime moves the quad only for the head.
- The prompts in play: the dwell, the time of the show and the counts.
- The words of the card at the end of a take and of a fetch, and when the
  Hand leaves a target, at the frame rate of the headset (`hint_words=3`, the
  0.25 s of `kOwnedSeconds`). The desktop runs have 60 to 90 frames a second.
- Whether the left card is easy to see while the right hand works, and whether
  the land prompt at the left wrist teaches the grip.

### 9.9 Two cards and true hints

**The hints of the game, and what is true.** `GameHintTrue` in
`hint_card_math.h` is the rule. It tests the button code and the mod's own
state, never the caption, so it holds in each language of the game. The
prompt "Continue" is known by its node in the HelpSystem, not by its words
(section 10).

| Hint of the game | Mouse button | With the controllers | The card |
| --- | --- | --- | --- |
| "Move" over the land | Left, code 1 | False in the God view and on the table. The game's land drag runs, but the picture does not come from the game camera, so nothing moves (`GodDragAfter`). The grip of the other hand moves the land. False also with the leash on: the trigger commands the Creature and does not reach the game. True in the old immersive view. | Does not show where it is false. The land prompt shows "Grip Move the land" on the card of the other hand. |
| "Focus Creature" over the land with the leash on | Right, code 2 | False. It is the tooltip of the leash (`0xEA2`): a press sends the Creature to that place. `creature_command.cpp` leaves that packet out for the grip, and the trigger sends the Creature. | Does not show over the land (hand mode 3) while the leash is on. The prompt shows "Trigger Send the Creature". |
| "Continue", while a text waits for a click | Left, code 1 | True in each view: the trigger is the left button, and the game takes that click at each place of the pointer (section 10) | Shows as the first line, "Trigger Continue". No prompt of the mod for the trigger takes its place. With `text_click=0` it has the rule of "Move" |
| "Pick Up", "Plant", "Catch", "Throw", "Activate", "Reminder" | Right, code 2 | True: the grip does the work of the right button | Shows |
| "Plant" with a tree in the Hand | Right, code 2 | The text is the same for each release, so it is not always true: a hand higher than `release_fall_cm`, a tree that leans more than 0.2 rad, no dry land or a tree on fire lets the tree fall | The card keeps "Plant" when the opening plants the tree. Else it says "Lower to plant. Open to drop" or a line for the other cause (`tree_plant_hint`, `grab.md`, "A tree in the Hand") |
| "Interact" on the Creature | Right, code 2 | True. The grip is the control that touches the Creature. | Shows. The prompt of the mod says "Grip Stroke or slap". |
| "Zoom Out" | The wheel, code `0x10` | True: the stick zooms | Shows |
| "Temple Health: 100%", "Amount: 128" | None | True | Shows |

**The control that touches the Creature** [V]. `controllers.cpp` sends the
trigger as the game's left button and the grip as the right button. The game's
step of the default action (`0x005D5190`) gives a left press to `0x005D1320`,
which is a jump to `fn_005D1AB0`: the land drag, with the camera and the
altitude of the land and no test of an object. It gives a right press to
`fn_005D1330`, which reads the object of the next pick record and its bit
`0x10` (`0x005D13DC`). So the grip starts the stroke and the slap, and the
trigger does not.

**One card for each controller.** A line names a control of one controller, so
it shows at the hand that holds that controller.

- The card of the hand that points: each hint of the game, and each prompt for
  the trigger, the grip, the stick and the face buttons of that hand.
- The card of the other hand: the prompts for the grip that moves the land
  (`kWordWorld`, only while `grab_world` is on), for the button of the game
  menu (`kWordMenu`: Y, or B when the left hand points), and for the control of
  the ring (`kWordRing`: X, or A when the left hand points, or the stick when
  only the stick opens the ring). The card of that hand does not show while the
  ring is open.
- Both cards can show in one frame. Each one is a quad layer of its own with
  its own swapchain (`kCardHintPointing` and `kCardHintOther` in `xr_screen.h`),
  placed by `CardPose` at the wrist of its hand.
- A card that would hide a point of the other hand in the view does not show
  (`CardCoversAny`, with 1 cm of margin). When the two cards overlap in the view
  (`CardsOverlap`), the card of the other hand does not show.
- A hand that is not tracked has no card.

**The look.** Short lines (at most four), a badge with the button word, and one
size of letter. The fade is soft: 0.25 s in (0.10 s with `prompt_timing=1`) and 0.35 s out, in place. The
prompts of a target show at once and stay while the target is there. The other
prompts keep their rule: with `prompts=1` each one shows only the first times,
and with `prompts=2` each one shows always.

The subtitles and the help text have a card of their own, fixed in the room:
`ui-layer.md`, section 17.

## 10. A text that waits for a click

Code: `src/mod/text_click.cpp` and `.h` (the flag of the game, the log, the
keys), `src/mod/text_click_math.h` (the rules), `KeepTrueHints` in
`src/mod/hint_card.cpp`, and `PumpButtons` in `src/mod/controllers.cpp`.

### 10.1 What the game does

`[V]` is from the disassembly and from the compiled scripts
(`Scripts\Quests\challenge.chl`).

- **The mode of a text.** The script word `RUN_TEXT` (native 13,
  `GScript::RunText` `0x006F7D60`) takes three values: a flag, the number of
  the help text, and a mode. It gives the number and the mode to
  `fn_005C5F90` of the HelpSystem. `fn_005C6100` sets `HelpSystem + 0x57C`
  for mode 1 and `HelpSystem + 0x580` for mode 2 [V]. The scripts call
  `RUN_TEXT` 1767 times with a fixed mode: 1552 times with 0, 154 times with
  1 ("with interaction") and 61 times with 2. The 154 are in 53 scripts of 24
  script files: the guide of the Creature, the hand demos, and many
  challenges [V].
- **The wait.** While `HelpSystem + 0x57C` is set,
  `HelpSystem::IsTextRead` `0x005C64E0` gives 0. The script word `TEXT_READ`
  (native 15, `0x006F8260`) gives that value, so a script that waits until
  the text is read does not go on. The text stays on the screen [V].
- **The prompt.** `HelpSystem::Draw3D` `0x005C59A0` makes a hint while the
  flag is set: the picture of the button of action 1 (the left button), the
  caption of help text 3705 ("Continue"), and a blink. It keeps the node at
  `HelpSystem + 0x24`. `fn_004489D0` copies the caption into the node at
  `+ 0x20`, and the draw of the node (`fn_00448AC0`) gives that copy to
  `CameraHelp::DrawKeyOrMouse` [V]. On a monitor the prompt is in the lower
  right corner of the screen.
- **The click.** The step of the interface (`fn_005D1120`, at `0x005D11C0`)
  calls `HelpSystem::ProcessInterface` `0x005C69B0` with bit `0x20` of
  `GInterface + 0x39`: the left button went up. When the text came 1 s ago
  or more (`fn_005C68C0`), the function clears the flag and deletes the
  node. No time clears the flag. The other writers are the next text
  (`fn_005C6100`), `HelpSystem::ReInitialiseText` `0x005C7A80`, the load of
  a saved game (a saved game keeps the flag), and
  `HelpSystem::ClearTextDisplayed` `0x005C54E0`. That function runs through
  `HelpSystem::ClearAllText` `0x005C5550` when a task gets the dialogue
  (`HelpSystem::DialogueControlRequest` `0x005C6790`), when the dialogue
  ends, and for the script words `GAME_CLEAR_DIALOGUE` and
  `GAME_CLOSE_DIALOGUE` [V]. With the cinema bars up, the same click ends
  the text at once.
- **One dialogue.** A script says a text in a dialogue. `START_DIALOGUE`
  (native 120, `GScript::StartDialogue` `0x00710690`) gives the dialogue to
  the task when `HelpSystem + 0x45CC` is 0, and it gives 0 to each other
  task while a task has it. One case is different: a script of type 1 (a
  challenge) takes the dialogue from a script of type 2 (a help script,
  `GScript::StopHelpScripts` `0x006EC780`). A script calls
  `START_DIALOGUE` again until it gets 1. `END_DIALOGUE` (native 121) comes
  after the text is read [V].
- **A cinema.** A script that starts a cinema calls `START_CAMERA_CONTROL`
  (native 30) first, then `START_DIALOGUE`, then `START_GAME_SPEED` and
  `SET_WIDESCREEN` [V]. So while a different script has the dialogue, the
  new script has the camera, and the cinema bars are down.

A plain player moves the land with the left button, so the first move ends
the wait. A player who gives no left click has a text that stays. The
script that said it does nothing more, and each story that starts after it
stops with the camera of its script and no cinema bars. A left click then
lets all of them go on.

**The sailors of Land 1** (`TheMissionaries.txt`, the scroll with the
number 18) [V]:

- The script takes wood only as a pile: an object of type 16 with the kind 9
  that is 40 units or less from the boat. It does not take a pile while the
  Hand or the Creature holds a sailor. It looks each 3 s.
- Each 3.4 s, until the boat has its wood and one time only, the script looks
  for an object of type 13 (a tree) 40 units or less from the boat. When
  there is one, the good advisor says help text 1275, "A tree is no good.
  These people require prepared wood.", with mode 1, in a dialogue.
- The test for the wood comes after that text in the loop of the script. So
  while the text waits for a click, the boat takes no wood.
- "Can we have some more wood please?", "That's good grain. Please can we
  have more?" and "We need more meat, though." have mode 1 too.

### 10.2 What the mod does

With `text_click=1`:

- **The prompt shows in each view.** The hint is a hint of the left button,
  and the rule of 9.9 hides each other one where the trigger does not move
  the land (the God view, the table, and with the leash on).
  `TextClickIsPrompt` knows the hint by the node of `HelpSystem + 0x24`
  while the flag is set. `GameHintTrue` then gives true. The line is the
  first line of the card of the pointing hand: "Trigger Continue", with the
  caption in the language of the game.
- **No prompt of the mod takes its place.** The prompt of a target does not
  own it, and a miracle in the Hand keeps it in the place of the name of the
  miracle. A prompt of the mod for the trigger does not show beside it, also
  not a line for two presses ("Trigger x2").
- **The trigger is the click.** While the flag is set, a new press of the
  trigger is no Creature command and makes no seed active
  (`TriggerMayCommand`): it goes to the game as the left button. It starts
  with the Hand off the view too (`PressMayStart`), as a press in the cut
  scene window does. A grab of the land with the other hand still holds the
  trigger back.

The mod writes nothing in the game for this, except in the test for the
desktop runs. `TextClickFrame` reads the flag one time for a frame. With
`text_click=0` the prompt has the rule of "Move", and the trigger keeps its
other jobs.

In the cut scene window the card does not show. The prompt of the game is in
the picture there, and the trigger is a left click (`cutscene-window.md`).

**The test key**, with `test_keys=1`: Shift with Insert, and Ctrl up, switches
`text_click`. The pointing controller gives a long pulse for 1 and a short
pulse for 0.

**The test for the desktop runs.** `text_click_test=N` or `N,M` (4 frames at
most): at each frame the mod starts a wait. With `text_click_test_text=0`
it sets `HelpSystem + 0x57C`. With a text number it makes the game say that
text with mode 1, through `fn_005C5F90`, as `RUN_TEXT` does for the values
1, the number and 1. The prompt, the wait and the click are then the
game's own. The text of the test has no script, so no task has the
dialogue: a script that starts a dialogue in the wait ends the wait.

### 10.3 The log

- `text click : text_click=1. ...` once, with the rule.
- `text click : a text of the game waits for a click now (HelpSystem + 0x57C),
  frame N. The script task T has the dialogue (0 is none), and the cinema
  bars are down. text_click=1`: the start of each wait.
- `text click : the wait for a click is over after K frames, frame N. The
  controller sent P presses of the left button to the game in that time`: its
  end.
- `hint card : frame N, the game gives its prompt for a text that waits for a
  click, "[Trigger] Continue". It shows on the card of the pointing hand, also
  here, where the trigger does not move the land (text_click=1)`: one line for
  each wait (80 lines at most). With `text_click=0` the line says that the
  prompt does not show.
- Each report window with a wait: the waits that started, the frames with a
  text that waits, the frame of the start of a wait that still holds, and the
  frames on which the prompt showed and did not show.

### 10.4 Tests

The math checks (`test/controller_math/text_click_checks.h` and
`hint_checks.h`): the wait from frame to frame with its length and its
clicks, `TriggerMayCommand`, `PressMayStart`, the frames of the test key, and
`GameHintTrue` for the click prompt in each view, with the leash on and with
`text_click=0`.

The desktop run is `creature_recipe.ps1 -Kind click` in `bwvr/tools/desk`
(the God view, in the test save `temple`, or with `-NewGame` the start "Keep
your old Creature"). The leash goes on, and the test makes the game say help
text 1275 with mode 1 at the frames 960 and 1500. The advisor speaks the
line, and the text is in the picture. The frames here are the frames of the
recipe. In a run in the test save, a frame of the log is about 730 less.

| Step | `text_click=1` | `text_click=0` |
| --- | --- | --- |
| The wait starts | The card says "Trigger Continue" as its first line, with the lines of the grip under it | The card does not have the line |
| The trigger at frame 1100, with the leash on and the aim on the Creature | The press goes to the game as the left button, and the wait is over 1 frame after the button goes up. No command | The press is a Creature command (the leash comes off). No left press goes to the game, and the wait goes on |
| The trigger at frame 1300 | A Creature command: the leash comes off | The left button. The wait is over 1 frame after the button goes up |
| The trigger at frame 1700, with the hand behind the head | The press goes to the game, and the second wait is over 1 frame after the button goes up | The press is dropped, and the wait stays to the end of the run (2500 frames) |

With `text_click=0` the press at frame 1100 is a command for the object in
the aim. When that object is not the Creature, the Creature gets a job. In
one run the wait was then over 10 frames after the command, with no left
press. The text of the test has no task that has the dialogue, so a script
that starts a dialogue ends the wait [I].

Not run: the quest of the sailors itself, a miracle or a seed in the Hand in
a wait, and a wait in the Temple. A desktop run does not reach that quest.
The start "Keep your old Creature" has no scroll at the place of the boat.
The start "Skip all of the Creature tutorial" plays the first story and
goes to the Creature select, and it has no scroll there too.

### 10.5 Limits

- A script that has the camera and waits for the dialogue has no cinema
  bars. The God view stands aside for each story camera, so the player then
  sees the camera of the game with a held shot (`script_camera`,
  `backend.md`), and the cut scene window does not open. The same must
  happen for the time of a spoken line, when a quest starts while a
  different challenge script has the dialogue [I].
- The card is at the wrist of the pointing hand. Only a headset shows
  whether a player sees the prompt there while a text stays on the subtitle
  card.
