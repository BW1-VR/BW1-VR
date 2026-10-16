# The launcher

`Black & White VR Mod.exe` sets the options and starts the game. The installer
puts it in the game folder and makes the shortcuts. The code is
`src/launcher/launcher.cpp`.

It exists because the settings live in two places and neither is obvious. The
mod reads `bwvr.ini` beside the game. The render resolution is not in that
file. The game keeps it in the registry, under

    HKCU\SOFTWARE\Lionhead Studios Ltd\Black & White\BWSetup

as `ScreenW` and `ScreenH`. If a person edits one and forgets the other, the
result looks like a bug in the mod.

The window has these buttons: "Play in VR", "Play, new game" (starts the game
with `-NEWGAME`, on the first island, with no menu), "Play, no VR" (see "The
game without VR"), "Save Config" (writes the settings and does not start the
game) and "Support". The buttons are a block below the table. "Play in VR"
and "Play, new game" are in the first row, "Save Config" and "Play, no VR" in
the second row, and "Support" has the third row to itself.

The launcher asks Windows for version 6 of the common controls (a manifest
line at the top of `launcher.cpp`). The controls then have the visual styles
of Windows, and a button can show a picture beside its text.

## The table

The launcher is built from one array, `kSettings`:

```cpp
{kBool, "force_mips", "Enable texture mipmaps", nullptr, 1,
 "Uses smaller copies of each texture in the distance. ..."},
```

A row gives a control, a tooltip, a default, and the reading and writing.
There is no layout to keep in step and no separate load and save code.

The window sizes itself around the table and starts a new column when one gets
tall. A heading stays with at least three of its settings. A column has 22
rows at most. The table of now has 22 rows, so the window has one column. One
more row starts a second column.

A field is 160 units wide, which is enough for the longest choice ("Grip of
the other hand"). The block of the buttons is below the last column and as
wide as its fields. When a column before the last is taller, the block is at
the bottom of the window. The status line is below the table and the buttons,
over the whole window, and has room for two lines of text.

There are eight kinds of row:

| Kind | Control | Stored in the ini as |
| --- | --- | --- |
| `kGroup` | A heading. | Nothing. |
| `kBool` | A checkbox. | 0 or 1. |
| `kInt` | A number. | The number. |
| `kChoice` | A list. | The index of the choice. |
| `kValues` | A list of `label=value` pairs. | The value, not the index. |
| `kScale` | A slider, in percent of what the headset asks for. | The percent. It also writes `ScreenW` and `ScreenH` to the registry. |
| `kSlider` | A slider with a value of its own. Its spec is the low value, the high value, the step and the unit, with a bar between them. Its label says the value and the unit. | The value, on the step. |
| `kRenderer` | A list: Vulkan, Direct3D 9. | `backend` as text: `vulkan` or `dxwrapper\ddraw.dll` (see "Performance"). |

## What it does besides editing

Every save also writes these:

- The keys that have only one right answer: `vr`, `stereo` and
  `head_camera`, all set to 1, and `gpu_bridge=1` with the renderer
  Direct3D 9. With the renderer Vulkan the save removes `gpu_bridge`,
  because Vulkan has no bridge of Direct3D 9.
- `FullScreen` 0 and `ScreenD` 32 in the registry of the game. If they are
  missing, the game starts full screen at the size of the desktop, in 16 bit
  color. The mod needs a window at the size that the launcher sets. The game
  does not read `WindowedMode`. Only the `Setup.exe` of the game uses it.
- With the renderer Direct3D 9: `AnisotropicFiltering` in
  `dxwrapper\dxwrapper.ini` (see "Picture").
- The game's own `ScreenW`, `ScreenH`, `FullScreen` and `ScreenD`, once, in a
  `[game_before]` section of `bwvr.ini`, with -1 for a value that the game did
  not have. The uninstaller puts them back, so the plain game does not open in
  the square window that the mod asks for.
- It deletes the retired keys (see below) from `bwvr.ini`.

If Windows refuses the write (an unelevated write to `bwvr.ini` in Program
Files fails), the status line says so and not "Saved.".

## The game without VR

"Play, no VR" starts the game on the monitor, for a look at what the plain
game does. It saves the settings as each other button does. Then it writes
`vr=0`, so the mod does nothing in that game (it only answers the check for an
internet connection, and the backend is dxwrapper for that game, also with
the renderer Vulkan). It writes `FullScreen`
1, and the size of the main monitor as `ScreenW` and `ScreenH`.

These values are for one game. The launcher closes its window and stays until
the game ends. Then it writes `vr=1`, the `backend` of the renderer, `FullScreen` 0 and the size for the
headset again, so a start of `runblack.exe` without the launcher and a desktop
test find the values for the headset. It writes them after the end of the
game, because the game can write its display values when it ends. If the
launcher stops before the game ends, the values stay until the next "Play in
VR" or "Save Config".

With `vr=1`, the mod does not start a headset session in a full screen game.
The log says so. `vr_full_screen=1` starts one.

## Retired keys

A setting that only breaks the game when a person changes it is not a choice.
The launcher does not offer these keys, and each save deletes them from
`bwvr.ini`, so an old ini cannot keep a wrong value. The default of the mod is
the right answer for each key. The keys still work when a person writes them by
hand, for tests.

| Key | The value the mod uses |
| --- | --- |
| `head_fov_deg` | 0: the angle comes from the headset |
| `eye_separation_milli` | 0: the headset's own measurement of your eyes |
| `hand_anchored` | 1: the Hand stays put when you look around |
| `head_roll` | 1: the world stays level when you tilt your head |
| `ui_world_lock` | 0: parked, see `backend.md` |
| `pointer_clip`, `pointer_scale` | 1: the cursor stays in the window and reaches the whole render |
| `window_width`, `window_height` | 0: the window fits the desktop |
| `freeze_time`, `skip_hand_pass2`, `skip_physics_pass2`, `skip_snapshot` | 1 |
| `profile`, `capture_every_seconds` | 0 |
| `subtitle_lift_pct` | 0: subtitles stay where the game puts them, and the interface size brings them into view |
| `controller_hand` | 1: the Hand is at the real hand. 0 is the pointer: the controller points a ray, and the Hand goes where the ray points |
| `script_camera` | 2: a story camera that has no window holds each shot and cuts at the end of each move. 0 lets it glide as the game moves it, 1 cuts at the start of each move |
| `instant_jumps` | 1: a double click on the land takes the player there in one frame |
| `camera_glide` | 0: the camera stops when the player releases the land |
| `head_units_per_meter_x10` | 75: 7.5 game units for each meter, the life size scale |

The list `kRetiredKeys` also holds the keys of earlier modes (`head_tracking`,
`immersive`) and the keys of the desktop test tools: `fake_head*`,
`fake_controller*`, `fake_mouse*`, `seek_*`, the `*_probe` keys, each key
with `_test` in its name, and some more. `stereo_headless=1` is in that list
because it stops the headset session with no message.

Every OpenXR runtime reports a pose for each eye, so the IPD is always known.
The mod takes it from the distance between the two eye poses, and takes it
again when it changes by more than 0.5 mm (for example when a Quest 3 IPD wheel
turns).

### Settings that are not offered and that stay in the ini

These keys have settled defaults, so the launcher has no row for them. Their
rows are in comments in `kSettings`, and the mod still reads each key. A save
does not delete them, so a value that a person writes in `bwvr.ini` is kept.

| Key | Default | What it does |
| --- | --- | --- |
| `ui_scale_pct` | 25 | The size of subtitles, help text and the Creature panel in the eye pictures, 25 to 100 (`backend.md`, "Interface size") |
| `ui_shift_px`, `ui_adaptive` | 8, 1 | How far the interface sits in front of the player, and whether it comes nearer when the world in front of it is nearer |
| `hint_scale_pct`, `hint_flat` | 20, 1 | The size and the depth of the game's hint beside the Hand in the eye pictures. With controllers the hints are on the hint cards (`controller-hints.md`) |
| `zoom_step` | 2 | The step of one wheel click on the camera of the game (`backend.md`, "Zoom in steps"). The God view has its own zoom steps |

## Settings

### Picture

| Setting | Default | What it does |
| --- | --- | --- |
| One scene walk for both eyes (prototype) | On | Key `one_walk`. On also makes the render a quarter wider (`one_walk_guard_pct`, 25), see `backend.md`, "One walk for both eyes". It is before the resolution in the table, because a save writes the rows in order and the size reads it |
| Margin around the view | None | Key `fov_margin_pct`, 0 to 30 in steps of 5. The game draws that many percent more on each side of the view of the headset, and the render has that many percent more pixels each way, so a degree keeps its pixels (`backend.md`, "A margin around the view"). It is before the resolution in the table for the same reason as the row above |
| Resolution scale | 100% | A slider from 50 to 200 in steps of 5. Key `resolution_pct`. See "The resolution comes from the headset". |
| Anisotropic filtering | 16x | A list: off, 2x, 4x, 8x, 16x. Key `anisotropy` (1, 2, 4, 8, 16). With the renderer Direct3D 9 the launcher also writes the level to `AnisotropicFiltering` in `dxwrapper\dxwrapper.ini`, because dxwrapper applies its own level to the Direct3D 9 device. A fixed 16 there would make "off" do nothing. dxwrapper reads 0 as "leave it to the game". The renderer Vulkan takes the level that the game sets on its textures, so it needs no other file. |
| Enable texture mipmaps | on | Uses smaller copies of each texture in the distance. Key `force_mips`. |
| Model detail distance | 5x | How far out models keep their full detail. Key `detail_distance`, index 0 to 3: the game's own, 2x, 3x, 5x. |

### Performance

| Setting | Default | What it does |
| --- | --- | --- |
| Renderer | Direct3D 9 | Key `backend`. Vulkan: the mod draws the game with its own DirectDraw and Direct3D 7 on Vulkan, the session is a session with Vulkan, and the window of the game has a swapchain of Vulkan. The mod then uses no dxwrapper, no Direct3D 9 and no Direct3D 11 (`backend.md`, "The backend of the mod on Vulkan", "Only Vulkan"). The launcher writes `vulkan` only when the Vulkan of Windows has a GPU with Vulkan 1.3. Else it writes dxwrapper, sets the list back, and the status line of Save says so. Direct3D 9: dxwrapper and Direct3D 9. A bwvr.ini with no `backend=vulkan` shows Direct3D 9 |
| Direct3D 9 through DXVK | off | Key `d3d9_backend`. Only with the renderer Direct3D 9, so the row is not in the table with Vulkan (`backend.md`, "DXVK under dxwrapper") |
| Headset session with Vulkan | off | Key `xr_vulkan`. Only with the renderer Direct3D 9, so the row is not in the table with Vulkan: the renderer Vulkan always has a session with Vulkan (`backend.md`, "The session with Vulkan") |

A change of the renderer in the list puts the two rows for Direct3D 9 in
the table or takes them out at once, and the rows below move up or down.
A save keeps the values of the two rows in `bwvr.ini` also with Vulkan, so
a change back to Direct3D 9 has them.

### Foveated rendering

Both kinds need the eye pictures of the Vulkan renderer: the renderer
Vulkan, or Direct3D 9 with the session with Vulkan. The sliders are there to
find a setting that looks good. "Show the parts with fewer details" shows
where the parts are.

| Setting | Default | What it does |
| --- | --- | --- |
| Fewer shades away from the middle | off | Key `foveation` (the launcher writes 1). `backend.md`, "Foveated rendering" |
| All shades within | 22 degrees | A slider from 10 to 45. Key `foveation_inner_deg` |
| One shade for 2 by 2 within | 35 degrees | A slider from 15 to 70. Key `foveation_outer_deg`. The mod takes at least the angle of the row above |
| Fewer pixels away from the middle | off | Key `fovea_res`. `backend.md`, "Foveated resolution" |
| Resolution of the whole view | 50% | A slider from 5 to 100 in steps of 5. Key `fovea_res_outer_pct` |
| Resolution of the middle | 100% | A slider from 50 to 200 in steps of 5. Key `fovea_res_center_pct` |
| Size of the middle | 25 degrees | A slider from 10 to 45. Key `fovea_res_center_deg` |
| Middle lower by | 0 degrees | A slider from 0 to 15. Key `fovea_res_center_down_deg`. Only with no eye tracker |
| Show the parts with fewer details | off | Key `foveation_show`. A blue colour over the parts with fewer shades or fewer pixels |

### View

| Setting | Default | What it does |
| --- | --- | --- |
| Play mode | God view | Key `play_mode`, a value and not an index. 4 God view: the land is at your waist, so your hands can reach it, with motion controllers or with the mouse (see `god-view.md`). 1 Stereoscopic window: a 3D screen in front of you, and your head does not move the camera. 3 Tabletop (test): the island is a model on a table in front of you (see `tabletop.md`). A window that follows the head is mode 2, which the launcher does not offer. An ini with `play_mode=0` gets the God view. |
| Intro in stereo | off | The Lionhead logo at the start shows in 3D around you. The view goes along the camera path of the game and does not turn. The menu button skips it. It is a test: no headset session has checked it. Key `intro_vr`. The key `intro_motion` chooses the motion, and the launcher has no row for it. See `intro.md`. |

### Moving around

This group decides how the camera moves when the head did not move. Motion that
the head did not make is what makes a headset uncomfortable, so the defaults
move the camera in steps.

| Setting | Default | What it does |
| --- | --- | --- |
| Turn in steps | 45 degrees | Key `snap_turn`, index 0 to 3: off, 30, 45, 90 degrees. A push of the right stick to the left or the right, a drag sideways with the middle mouse button, or a turn key, turns you in one step. You stay where you are. The middle mouse button does not tilt the camera. Off gives the smooth turn and tilt of the game. |
| Stick forward | Zooms out | Key `god_zoom_forward_in`, a value. 0 Zooms out: the right stick forward makes the world smaller, and back makes it larger. 1 Zooms in: the other way (`god-view.md`, section 2). |
| Zoom with the stick | In steps | Key `god_zoom_smooth`, a value. 0 In steps: one push of the right stick forward or back is one zoom step. 1 Smooth: the zoom changes while the stick is held, and it stops when the stick is let go (`god-view.md`, section 2). |
| Center of the zoom | The land you look at | Key `god_zoom_about`, a value. The point that stays where it is in a zoom with the stick. 1 The land that the head looks at. 0 The land under the head. 2 The Hand's point (`god-view.md`, section 2). |
| Move with the left stick | Head direction | Key `move_stick`, a value. In the God view the stick of the other hand (the left stick, or the right stick of a left handed player) moves you over the land while you push it, and you stop when you let it go. 1 Head direction: forward is where the head faces. 2 Controller direction: forward is where that controller points. 0 Off: the stick does not move you. A walk with a stick makes some players ill, so the row has Off (`god-view.md`, section 5.7). The key `move_stick_speed_cm` sets the speed, and the launcher has no row for it. |
| Blink when you jump or turn | off | Key `blink`, index 0 to 2: off, short (0.1 seconds), long (0.2 seconds). Black after a jump, a turn and a story camera cut. |

### Controllers

| Setting | Default | What it does |
| --- | --- | --- |
| Glow at the Hand's point | off | Key `hand_glow`. A small glow at the tip of the index finger shows the point of the Hand that picks up and clicks. It shows while a press of the grip acts on something (`hand-model.md`). |
| Grab the world | Grip of the other hand | Key `grab_hand`, a value. 1 Hold the grip of your other hand and move that hand to pull the world. Hold both grips to turn the world and to zoom. 2 Either grip: the grip of the pointing hand also grabs the world when the Hand is empty and not over an object. 0 Off. |

See `controllers.md` for the full controls.

### Testing

| Setting | Default | What it does |
| --- | --- | --- |
| Test keys | off | Key `test_keys`. Turns on the keys for tests. Each key changes one rule of the mod while the game runs, for a comparison. `backend.md` ("Keys for v1.0") and the player readme list them. F12 saves a picture of each eye in the game folder. A player does not need this. |

F11 makes the direction of your head straight ahead. This key is always on.

## A picture of the window

A test aid: `"Black & White VR Mod.exe" --picture <file.bmp>` saves a
picture of the window with the values of the bwvr.ini and ends. The window
is outside the screen, at the bottom of the order of the windows, and it is
never active. The launcher writes nothing. `--game <folder>` names the
folder of the game when the launcher is not in it, for example in the
folder of the build.

## The resolution comes from the headset

The resolution is a percentage, not a list. A list of sizes picked by hand is
right for one headset and wrong for the next.

Only a running OpenXR session can ask a headset what it wants, and the
launcher runs before the game. So the game asks, with
`xrEnumerateViewConfigurationViews`, and writes the answer into `bwvr.ini` as
`headset_width` and `headset_height` every time it starts with a headset
connected. The launcher reads the values from there and shows the pixels beside
the slider. Until the first run it uses the figures of a Quest 3 as a guess.

One number drives both axes, taken from the larger side. The game derives its
vertical angle from the render aspect, so a square render covers the headset
and wastes the fewest pixels.

100% is a little softer than native. The mod renders a 108 by 110 degree
symmetric view where the view of the headset is 94 by 99 asymmetric, so the same
pixels cover a wider angle. About 115% matches the native density.

## What it checks

On start, the status line shows the result of these checks. It says "Ready"
only when all pass.

1. **The files.** `ddraw.dll`, `openxr_loader.dll`, `dxwrapper\ddraw.dll`,
   `dxwrapper\dxwrapper.dll` and `dxwrapper\dxwrapper.ini`. The status line
   names the file that is missing.
2. **The build of the game.** It hashes `runblack.exe` and compares it with the
   v1.42 file, as the mod does at every start (`src/common/exe_check.h`). A
   player with another build is told which one.
3. **A 32 bit OpenXR runtime.** The game is 32 bit, so a runtime registered
   only for 64 bit cannot serve it, and the headset shows nothing. The status
   line names the runtime that it found, for example
   `virtualdesktop-openxr-32.json`.

## Where it finds the game

The launcher looks beside itself, then in `Black & White Game` folders up to
three levels above it, and last at the `GameDir` value that the game's own
installer wrote. It works from inside the game folder, from the project root
and from wherever a player moves it. It has no dependency beyond Windows.

Build it with:

```
cmake --build build --config Release --target bwvr_launcher
```

or `.\dev.ps1 build`. The output is `Black & White VR Mod.exe`.

## The Support button

It opens https://ko-fi.com/buckzor in the browser of the player. The mod is
free, and the button is the only place where the mod asks for anything.

The button is as wide as the two buttons above it together, and it shows a
red heart to the left of its text. The launcher draws the heart itself, at
the size of the text for the scale of the screen (`MakeHeartIcon`), so the
art folder has no file for it.

## A picture of the window for a test

`-shot <file>` at the end of the command line makes the launcher write its
window to a BMP file and end. A person who uses the PC does not see the
window: it is outside the screen, it has no button on the taskbar, and it
does not take the focus. The launcher reads the settings for the picture and
writes nothing but the file. With no game folder it does not stop: the
picture then shows the default of each row.

`-rows <n>` before `-shot` sets the most rows of a column. This gives the
picture of a table that has more columns.

`bwvr/tools/desk/launcher_shot.ps1 -Name <name>` runs the launcher of the
build folder in this way and makes a PNG in `local\launcher-shot`. Look at
the picture after each change of the table or of the layout: a text that is
cut off and two controls that overlap show there. The picture is the client
area only. It does not show the frame of the window or a tooltip.
