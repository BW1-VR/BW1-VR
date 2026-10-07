# The launcher

`Black & White VR Mod.exe` sets the options and starts the game. The
installer puts it in the game folder and makes the shortcuts.

It exists because the settings live in two places and neither is obvious. The
mod reads `bwvr.ini` beside the game. The render resolution is not in there at
all: the game keeps it in the registry, under

    HKCU\SOFTWARE\Lionhead Studios Ltd\Black & White\BWSetup

as `ScreenW` and `ScreenH`. Editing one and forgetting the other is easy, and
the result looks like a bug in the mod.

## It is a table, not a dialog

Every phase of this project has added settings, and there will be more. So the
launcher is built from one array:

```cpp
{kBool, "hand_anchored", "Hand follows the mouse only", nullptr, 1,
 "The Hand is placed where the cursor meets the world..."},
```

A row gives a control, a tooltip, a default, and the reading and writing. There
is no layout to keep in step and no separate load and save code to forget.

The window sizes itself around whatever the table holds and starts a new column
when one gets tall, so the table can grow a long way before anyone has to think
about the layout again. A heading is kept with at least three of its settings
so a group is never stranded at the bottom of a column.

Five kinds are supported: a heading, a checkbox, a number, a list, and the
resolution, which is the one setting that writes the registry instead of the
ini and writes two values at once.

## What it does besides editing

It carries over the checks that were in `tools/launch-vr.ps1`, because they
catch the two failures that look identical from inside a headset:

- **The mod is not beside the game.** It looks for `ddraw.dll` and
  `openxr_loader.dll` and says which one is missing.
- **No 32 bit OpenXR runtime is registered.** The game is 32 bit, so a runtime
  registered only for 64 bit cannot serve it and the headset shows nothing. The
  status line names the runtime it found, for example
  `virtualdesktop-openxr-32.json`.

It also writes the five settings that have only one right answer, rather than
offering them: the backend, the GPU bridge, VR, stereo, and the head camera.

## Settings that are not offered

A setting that only breaks the game when it is changed is not a choice. These
were offered once and are now removed from the launcher:

| Key | The value the mod uses |
| --- | --- |
| `head_fov_deg` | 0: the angle comes from the headset |
| `eye_separation_milli` | 0: the headset's own measurement of your eyes |
| `hand_anchored` | 1: the Hand stays put when you look around |
| `head_roll` | 1: the world stays level when you tilt your head |
| `ui_world_lock` | 0: parked, see backend.md |
| `pointer_clip`, `pointer_scale` | 1: the cursor stays in the window and reaches the whole render |
| `window_width`, `window_height` | 0: the window fits the desktop |
| `freeze_time`, `skip_hand_pass2`, `skip_physics_pass2`, `skip_snapshot` | 1 |
| `profile`, `capture_every_seconds` | 0 |
| `subtitle_lift_pct` | 0: subtitles stay where the game puts them, and the interface size brings them into view |

Each time it saves, the launcher deletes these keys from `bwvr.ini`, so an old
ini cannot keep a wrong value. The mod's own default is the right answer for
each. The keys still work when they are written by hand, for testing.

Every OpenXR runtime must report a pose for each eye, so the IPD is always
known. The mod takes it from the distance between the two eye poses, and takes
it again when it changes by more than 0.5 mm, for example when a Quest 3 IPD
wheel is turned. World scale is the setting that changes how big the world
feels.

## Picture and view

| Setting | Default | What it does |
| --- | --- | --- |
| Resolution scale | 100% | See below. |
| Anisotropic filtering | 16x | A list: off, 2x, 4x, 8x, 16x. The ini keeps the level, for example `anisotropy=16`. The launcher also writes it to `AnisotropicFiltering` in `dxwrapper\dxwrapper.ini`, because dxwrapper applies its own level to the Direct3D 9 device, and a fixed 16 there would make "off" do nothing. |
| Enable texture mipmaps | on | Uses smaller copies of each texture in the distance. Key `force_mips`. |
| Model detail distance | 5x | How far out models keep their full detail. Key `detail_distance`: 0 to 3 is the game's own, 2x, 3x and 5x. |
| Play mode | God view | Key `play_mode`. 4 God view, the first choice: the land is at your waist, so your hands can reach it, with motion controllers or with the mouse. See `god-view.md`. 1 Stereoscopic window: a 3D screen in front of you, and your head does not move the camera. 3 Tabletop (test): the island is a model on a table in front of you, and Ctrl F11 turns the table off and on. See `tabletop.md`. The God view replaced 0, Immersive, on 2026-10-02: an ini with 0 shows the God view, and the mod plays the God view. A window that follows the head is mode 2, which the launcher does not offer yet. The launcher keeps the value, not the place in the list. |
| World scale | 75 | Game units in a meter, in tenths. |
| Intro in stereo | off | The Lionhead logo at the start forms in 3D around you. The menu button skips it. Key `intro_vr`. See `intro.md`. |

Play mode replaces the "Head tracking" and "Fill your view" switches, which
only made sense in these combinations. Each mode is a row in one table in
`ReadSettings`. The tabletop mode is row 3.

Before the first write, the launcher copies the game's own `ScreenW`,
`ScreenH`, `FullScreen` and `ScreenD` into a `[game_before]` section of
`bwvr.ini`, with -1 for a value the game did not have. The uninstaller puts
them back, so the plain game does not open in the square window the mod asks
for.

Two of the game's own registry values are also written each time:
`FullScreen` 0 and `ScreenD` 32. If they are missing, the game starts full
screen at the size of the desktop, in 16 bit color. Another install of the
game can remove them, and the mod needs a window at the size the launcher
sets. The game does not read `WindowedMode`. Only `Setup.exe` uses it.

## The resolution comes from the headset

The resolution is a percentage, not a list. A list of sizes picked by hand is
right for one headset and wrong for the next.

Only a running OpenXR session can ask a headset what it wants, and the launcher
runs before the game. So the game asks, with
`xrEnumerateViewConfigurationViews`, and writes the answer into `bwvr.ini` as
`headset_width` and `headset_height` every time it starts with a headset
connected. The launcher reads it back from there and shows the pixels beside
the slider. Until the first run it uses a Quest 3's figures as a guess.

One number drives both axes, taken from the larger side. The game derives its
vertical angle from the render aspect, so a square render covers the headset
while wasting the fewest pixels.

Note that 100% is a little softer than native. The mod renders a 108 by 110
degree symmetric view where the headset's own view is 94 by 99 asymmetric, so
the same pixels are spread over a wider angle. About 115% matches native
density.

## Comfort

| Setting | Default | What it does |
| --- | --- | --- |
| Interface size | 25% | The size of subtitles, help text, tooltips and the Creature panel. A smaller size also moves them nearer to the middle of your view. Menus keep their own size. The list is 25%, 30%, 40%, 50%, 75% and 100%. Key `ui_scale_pct`, 20 to 100 when written by hand. |
| Hint size, percent | 20 | The size of the hint beside the Hand. 20 is what the headset sessions settled on. |
| Draw the hint flat | on | The hint stays on the Hand without the Hand's depth. |
| Interface depth, pixels | 8 | How far the interface sits in front of you. |
| Bring the interface forward when needed | on | Moves the interface nearer when scenery is nearer. |

## Moving around

The "Moving around" group decides how the camera moves when you did not move
your head. Motion that your head did not make is what makes a headset
uncomfortable, so the defaults move the camera in steps.

| Setting | Default | What it does |
| --- | --- | --- |
| Turn in steps | 45 degrees | A drag sideways with the middle mouse button, or a turn key, turns you in one step. You stay where you are. The middle mouse button does not tilt the camera. Off gives the game's smooth turn and tilt. |
| Jump at once | on | A double click on the land takes you there in one frame. |
| Let the camera glide | off | Off, the camera stops when you release the land. |
| Zoom step | Medium | How far one click of the mouse wheel zooms. Game keeps the game's own step, about 1.06 times the distance. Small, medium and large change the distance by 1.12, 1.20 and 1.30 times a click. With these three, the first click zooms at once. Clicks in the next 0.15 seconds join the next step, up to three clicks. |
| Story camera | Cut at the end | How the camera moves in cutscenes and tutorials. Game: it glides as the game moves it. Cut at the start: the view cuts to where each move goes. Cut at the end: the view holds until the camera stops, then it cuts. In both cut modes a long move also cuts on the way, a close shot gets a little more room, and the camera does not shake. |
| Blink when you jump or turn | off | Black for 0.1 or 0.2 seconds after a jump or a turn, and after a story camera cut. |

The launcher writes each list to `bwvr.ini` as an index:

- `snap_turn` 0 to 3: off, 30, 45 and 90 degrees.
- `zoom_step` 0 to 3: the game's step, small, medium and large.
- `script_camera` 0 to 2: as the game moves it, a cut at the start, and a cut at the end.
- `blink` 0 to 2: off, short and long.
- `detail_distance` 0 to 3, see above.

`play_mode` is a list of values, not an index: 4, 1 and 3.

## Testing

| Setting | Default | What it does |
| --- | --- | --- |
| Test keys | off | Turns on six keys for tests. F12 saves a picture of each eye in the game folder, and one of the frame before either eye pass. Shift F12 moves the subtitles down one step, and after the lowest step they go back up. Ctrl F12 sets who points: the mouse and the controllers, the controllers only, or the mouse only. Ctrl Shift F12 moves the ears between the head and the game camera. Shift F11 sets the grab of the world: as set, one to one, or off. Ctrl Shift F11 turns the interface panel on and off, when `ui_panel` is set. Press these keys with the mouse still. Key `test_keys`. |

You need one key only. F11 makes the direction of your head straight ahead.
This key is always on.

## Building and placing it

```
cmake --build build --config Release --target bwvr_launcher
```

The output is `Black & White VR Mod.exe`. It finds the game by looking beside
itself, then in the folders above, and last at the `GameDir` value the game's
own installer wrote. So it works from the project root, from inside the game
folder, and from wherever a player moves it. It has no dependencies beyond Windows, so it is
one file to copy and nothing to install.

## What it checks before it says "Ready"

1. **The files.** `ddraw.dll`, `openxr_loader.dll`, `dxwrapper\ddraw.dll`,
   `dxwrapper\dxwrapper.dll` and `dxwrapper\dxwrapper.ini`. A missing
   dxwrapper file used to pass, and the mod then fell back to a slow path
   without a word.
2. **The build of the game.** It hashes `runblack.exe` and compares it with
   the v1.42 file, exactly as the mod does at every start. See
   `src/common/exe_check.h`. A player with another build is told which one
   they have, not shown a crash.
3. **A 32 bit OpenXR runtime.** The game is 32 bit, so a runtime registered
   only for 64 bit cannot serve it. The status line names the runtime it
   found.

A save that Windows refuses is now reported. Under Program Files an
unelevated write to `bwvr.ini` fails, and the launcher used to say "Saved."
all the same.

## The Support button

It opens https://ko-fi.com/buckzor in the player's own browser. The mod is
free, and the button is the only place the mod ever asks for anything.
