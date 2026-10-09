# Input devices the game already knows about

This file records what the binary already contains for input devices other
than the plain mouse. The Hand and gesture work hooks these existing paths
instead of faking mouse movement.

## The P5 glove: patch 1.30, and nothing to take from it

Patch v1.30 (March 2003) added support for the Essential Reality P5 glove, a
2002 consumer hand tracker with finger bend sensors. It is the only time this
game shipped support for a hand input device. It was checked read only,
against pure retail 1.20 and 1.30 installs.

**Result: nothing from 1.30 can or needs to move into the VR build, and users
never need to install 1.30.**

### It is not in our executable

Our binary is v1.20 code. It has an identical `.text` virtual size and section
layout to v1.20 retail. The fan patch v1.42 applied byte patches on top, and
byte patches cannot add code to an image with relocations stripped. No
`Glove`, `P5`, `Essential` or `Reality` string appears in it, and no glove
symbol or DLL. The 28 byte matches for `P5` are the CPU name table (`Intel
P5`) and ordinary code bytes.

### What 1.30 changed

A hash diff of the two retail folders:

| | Files |
| --- | --- |
| Added | `p5dll.dll`, `Scripts\InfoScript2Glove.txt`, `Audio\Dialogue\GloveDialogue.sad` (178 voice lines), four glove icon textures (`P5Finger`, `S_P5glove` and their alpha files), the readme |
| Changed | `runblack.exe`, a rebuilt `LHLogR.dll` with no glove content, the SafeDisc files |
| Unchanged | every hand, gesture, camera and data file |

The readme lists R (repeat a Miracle), M (Miracle selection), Ctrl+Shift (zoom
to the Hand), Miracle Dispensers and tree watering. Those are not new. The
retail `readme.txt` of February 2001 has the same text, and the v1.20 code has
them.

### The glove code cannot be read

The glove logic is about 20 KB of new code (`.text` grew by `0x5180`) in the
1.30 `runblack.exe`. That file is SafeDisc protected. SafeDisc encrypts all
of `.text` and all of `.data` on disk, including the strings, the assert
paths and the RTTI names. Reading the code means removing the protection,
which this project does not do. No public analysis exists: bw1-decomp lists
1.30 only as work in progress.

### What the plaintext shows

- **Imports.** The exe imports four functions from `P5DLL.dll`: the `CP5DLL`
  constructor and destructor, `P5_Init` and `P5_SetMouseState`. Every other
  import is the same as 1.20.
- **How the game read the glove.** `p5dll.dll` is not protected. `P5_Init`
  stores a glove count and a pointer to per-glove records in the game's own
  8-byte `CP5DLL` object. A thread fills the records at about 64 Hz. Each
  record (`0x3EC` bytes) holds:
  - x, y, z at `+0x3A4`. This is a relative, drifting position, integrated
    from one LED, not an absolute one.
  - yaw, pitch and roll at `+0x3B0`, and a rotation matrix at `+0x3C8`.
    They change only while four LEDs are visible.
  - five finger bends at `+0x3BC`, 6 bits each (0 to 63).
  - four buttons at `+0x3C1`.

  `P5_SetMouseState` only switches the glove's own firmware mouse on or off.
- **No new polymorphic classes.** 1.30 has the same 1014 RTTI locators and
  the same vtable lengths as 1.20. So it adds no class with virtual methods,
  and no virtual method to an existing class. RTTI does not show classes
  without virtual methods, such as `CP5DLL`.
- **A voice table.** 1.20 has six copies of the help text to voice table.
  1.30 adds a new table after each copy, about 502 KB of its 504 KB `.rdata`
  growth. In the new table, 163 help lines point at the glove voice bank.

### How the glove played, in Lionhead's words

`InfoScript2Glove.txt` is a full replacement for the game's help text, with
about 208 lines rewritten for the glove. It is the only description of the
scheme:

| Game action | Glove |
| --- | --- |
| Move the Hand | Move your hand, with an "acceleration" gain slider |
| Grab, pick up, cast, hold a Miracle | Close four fingers, thumb free |
| Throw, drop, release | Open the hand |
| Click on interface and information: scrolls, signposts, Question Mark | Index finger |
| A command in the world: leash, Creature, fight, houses | Middle finger |
| Fly to a place | Double click the index finger |
| Drag the land | Make a fist and pull the hand toward you |
| Zoom | Close middle, ring and pinky, then move toward or away from the receptor |
| Turn and tilt the camera | Close thumb, ring and pinky, then move left, right, up or down |
| Stroke or slap the Creature | Open hand up and down, or tap four fingers and swipe |
| Unleash | Shake the hand |
| Space bar and the Leash key | Glove buttons A and C |

A "click sensitivity" slider set how far a finger bends for a click. No line
uses wrist rotation. The glove only drove the game's existing pointer,
buttons and camera actions, which is the same approach as this mod.

### Open ideas that need no 1.30 install

1. Use the pose table above as a reference for the controller mapping.
2. "The Immersion Mushrooms" challenge on Land 1 is the "glove challenge" that
   the community remembers. Its script is already in the 1.20 files, and
   `GScript::ImmersionExists` at `0x006FDFA0` gates it. A hook there could
   open it, and controller haptics could give the mushroom shake. This needs
   a live test. Do not force the global at `0x00D18588` to a nonzero value.
   It is the `CImmMouse` pointer, and the init and shutdown code call through
   it.
3. 1.30 ships a second help text file for the glove. The 1.20 code has no
   such switch: `GSetup::LoadTextScripts` always loads the path
   `scripts/InfoScript2.txt` at `0x00C22110`. A VR build could make Quest
   wording in two ways. It could edit entries of the user's own
   `InfoScript2.txt` at load time. It could also point that path string to
   another file. Voice lines are tied to text by key name, so keep the keys.
   Do not ship Lionhead's glove text, audio or icons.

## Immersion force feedback

The game links directly against **`IFC22.dll`**, the Immersion Foundation
Classes 2.2 runtime, and uses 30 of its exports. This is the TouchSense haptics
layer for the Immersion feedback mouse. The effects are named after the
interactions that the Hand performs, and they are wired to the objects that
perform them. The mod routes them to the controller haptics (hook in
`controllers.cpp`, pulses in `haptics.cpp`, and the haptics section of
`hand.md`).

### What is there

`Data\Immersion\FullFeedback.ifr` and `Tactile.ifr` are Immersion effect
projects, about 22 KB each, dated February 2001. They define named effects,
and the names are the interaction vocabulary of the game:

`CreatureHit`, `CreatureSlap`, `GestureSuccess`, `GestureTrail`, `GripForce`,
`AnimatingInHand`, `GoldScroll`, `CanvasSewing`, `Texture`, `Creed`,
`CreedHeart`, `Challenge1`.

Each effect carries real parameters: `Magnitude`, `Period`, `Waveform`,
`Duration`, `FadeDuration`, `AttackLevel`, `AttackDuration`, `StartDelay`,
`Direction`, `Iterations`, `Phase`. A `Texture` effect adds
`PosBumpSpace`, `PosBumpWidth`, `NegBumpSpace` and `NegBumpWidth`, which is a
surface roughness description.

### The game side

43 symbols mention Immersion. The useful ones:

| Address | Name |
| --- | --- |
| `0x005D8A10` | `GInterface::StartImmersion(int effect)` |
| `0x005D8A80` | `GInterface::StopImmersion` |
| `0x005D8AF0` | `GInterface::StopAllImmersion` |
| `0x005D8610` | `GInterface::IsImmersionPlaying` |
| `0x005D8320` | `GInterface::UpdateCollideImmersion` |
| `0x005D8DD0` | `GInterface::SetImmersionGainAndFrequency` |
| `0x005D8D00` | `GInterface::SetPeriodAndDurationKeepingRatio` |
| `0x00514A80` | `DialogBoxImmersion::Init`, the settings dialog |

`StartImmersion` takes an effect index and rejects anything outside 0 to 0x2F,
so there are **48 effect slots**. It reads an effect pointer array at
`0x00D18580` and a 16 byte record per effect at `0x00BF232C`. The global at
`0x00D18588` holds the `CImmMouse` device pointer. Init at `0x005D86A0`
creates the object and stores it at `0x005D86FD`. Shutdown deletes it through
its vtable at `0x005D89D9` and clears the global. The other functions only
test it for nonzero, so it also acts as the gate for the whole system. Do not
force this global to a nonzero value, because it is a device pointer.

The game calls `GInterface::StartImmersion` with no Immersion hardware, so a
hook at its entry sees every effect. The signature is `(int effect, unsigned
int)`, `ret 8`, and the second argument is `0x80000000` every time. The
effect slots 0 to 3, 8 and 9 come from the callers, and slots 4, 5, 6, 44 and
46 from when they start in play (see `hand.md`).

Every object class implements **`GetInHandImmersionTexture`**. `Bonfire`,
`DeadTree`, `HanoiBlock`, `PuzzleMobileObject` and `Living` all have their own.
So the game carries a per object answer to "what does this feel like when you
are holding it", reached through the Hand.

### Why this is useful

1. `IFC22.dll` is imported by name, so it can be proxied like `ddraw.dll`.
2. A hook on `GInterface::StartImmersion` gives a stream of named haptic
   events at the correct moments, with magnitude and duration attached.
   Routing that to controller haptics is a mapping job.
3. `GetInHandImmersionTexture` gives a texture for each object, which is better
   than one generic buzz for every grab.
4. `CImmMouse::SwitchToAbsoluteMode` is one of the imported exports. The game
   can already accept an absolute pointer position from a device rather than
   relative movement. This is worth reading before a decision on how a tracked
   controller drives the Hand.

### Open questions

- Which of the 48 slots each named effect occupies (see the slots above).
- Whether `UpdateCollideImmersion` runs during ordinary play or only while the
  mouse is on a widget.
