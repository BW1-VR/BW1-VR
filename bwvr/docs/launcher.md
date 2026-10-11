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
with `-NEWGAME`, on the first island, with no menu), "Save" (writes the
settings and does not start the game) and "Support".

## The table

The launcher is built from one array, `kSettings`:

```cpp
{kBool, "force_mips", "Enable texture mipmaps", nullptr, 1,
 "Uses smaller copies of each texture in the distance. ..."},
```

A row gives a control, a tooltip, a default, and the reading and writing.
There is no layout to keep in step and no separate load and save code.

The window sizes itself around the table and starts a new column when one gets
tall. A heading stays with at least three of its settings.

There are six kinds of row:

| Kind | Control | Stored in the ini as |
| --- | --- | --- |
| `kGroup` | A heading. | Nothing. |
| `kBool` | A checkbox. | 0 or 1. |
| `kInt` | A number. | The number. |
| `kChoice` | A list. | The index of the choice. |
| `kValues` | A list of `label=value` pairs. | The value, not the index. |
| `kScale` | A slider, in percent of what the headset asks for. | The percent. It also writes `ScreenW` and `ScreenH` to the registry. |

## What it does besides editing

Every save also writes these:

- The five keys that have only one right answer: `backend`
  (`dxwrapper\ddraw.dll`), `gpu_bridge`, `vr`, `stereo` and `head_camera`, all
  set to 1 except the backend.
- `FullScreen` 0 and `ScreenD` 32 in the registry of the game. If they are
  missing, the game starts full screen at the size of the desktop, in 16 bit
  color. The mod needs a window at the size that the launcher sets. The game
  does not read `WindowedMode`. Only the `Setup.exe` of the game uses it.
- `AnisotropicFiltering` in `dxwrapper\dxwrapper.ini` (see "Picture").
- The game's own `ScreenW`, `ScreenH`, `FullScreen` and `ScreenD`, once, in a
  `[game_before]` section of `bwvr.ini`, with -1 for a value that the game did
  not have. The uninstaller puts them back, so the plain game does not open in
  the square window that the mod asks for.
- It deletes the retired keys (see below) from `bwvr.ini`.

If Windows refuses the write (an unelevated write to `bwvr.ini` in Program
Files fails), the status line says so and not "Saved.".

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
| Resolution scale | 100% | A slider from 50 to 200 in steps of 5. Key `resolution_pct`. See "The resolution comes from the headset". |
| Anisotropic filtering | 16x | A list: off, 2x, 4x, 8x, 16x. Key `anisotropy` (1, 2, 4, 8, 16). The launcher also writes the level to `AnisotropicFiltering` in `dxwrapper\dxwrapper.ini`, because dxwrapper applies its own level to the Direct3D 9 device. A fixed 16 there would make "off" do nothing. dxwrapper reads 0 as "leave it to the game". |
| Enable texture mipmaps | on | Uses smaller copies of each texture in the distance. Key `force_mips`. |
| Model detail distance | 5x | How far out models keep their full detail. Key `detail_distance`, index 0 to 3: the game's own, 2x, 3x, 5x. |

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
| Zoom with the stick | In steps | Key `god_zoom_smooth`, a value. 0 In steps: one push of the right stick forward or back is one zoom step. 1 Smooth: the zoom changes while the stick is held, and it stops when the stick is let go (`god-view.md`, section 2). |
| Center of the zoom | The land you look at | Key `god_zoom_about`, a value. The point that stays where it is in a zoom with the stick. 1 The land that the head looks at. 0 The land under the head. 2 The Hand's point (`god-view.md`, section 2). |
| Blink when you jump or turn | off | Key `blink`, index 0 to 2: off, short (0.1 seconds), long (0.2 seconds). Black after a jump, a turn and a story camera cut. |

### Controllers

| Setting | Default | What it does |
| --- | --- | --- |
| Glow at the Hand's point | on | Key `hand_glow`. A small glow at the tip of the index finger shows the point of the Hand that picks up and clicks. It shows while a press of the grip acts on something (`hand-model.md`). |
| Grab the world | Grip of the other hand | Key `grab_hand`, a value. 1 Hold the grip of your other hand and move that hand to pull the world. Hold both grips to turn the world and to zoom. 2 Either grip: the grip of the pointing hand also grabs the world when the Hand is empty and not over an object. 0 Off. |

See `controllers.md` for the full controls.

### Testing

| Setting | Default | What it does |
| --- | --- | --- |
| Test keys | off | Key `test_keys`. Turns on the keys for tests. Each key changes one rule of the mod while the game runs, for a comparison. `backend.md` ("Keys for v1.0") and the player readme list them. F12 saves a picture of each eye in the game folder. A player does not need this. |

F11 makes the direction of your head straight ahead. This key is always on.

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
