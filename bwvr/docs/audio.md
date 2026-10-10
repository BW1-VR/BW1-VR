# Sound in the room: how the game plays sound, and what the mod does

This file describes the sound of the game in VR. It has three parts:

1. How the game plays sound (sections 2 to 5).
2. What the mod does with it (sections 6 to 8).
3. The keys, the tools, the known limits and the open work (sections 9 to 14).

Marks used in this file:

- **V** means verified. The evidence is a disassembly, an import or export
  table, a data file, or the mod source.
- **I** means inferred. The evidence points that way, but nobody ran it.

Addresses in `runblack.exe` are virtual addresses at base `0x00400000`.
Addresses in `LHaudiodllR.dll` and `QMixer.dll` are RVAs, unless a line says
otherwise.

## 1. Summary

1. Every game sound goes through one DLL, `LHaudiodllR.dll`. It is Lionhead's
   audio layer. It drives `QMixer.dll` (QSound QMixer), and QMixer does the 3D
   positioning. (V)
2. The game sets the listener in one place, `GAudio::ProcessAudioGameTurn`
   (`0x00427080`), once per game turn. It uses the game camera, not the head.
   (V)
3. Two game functions start a sample: `GAudio::PlaySoundEffect` (`0x00429E30`)
   and `HelpDude::PlaySample` (`0x005BB530`). Both call `LHSamplePlay`. The DLL
   also calls it, for the atmosphere and for animation effects. (V)
4. The advisors speak through `HelpDude::PlaySample` with the 3D flag off, so
   the game plays them flat. (V)
5. The mod hooks `LHListenerUpdate` and sends the head pose to QMixer every
   frame. The ears are the head.
6. The mod makes the distances of the game into distances of the room. It
   scales them with the zoom of the view. The head, not the game camera,
   decides which positional sounds play.
7. The mod steers sounds that the game plays flat when it knows their source.
   The advisors get their direction from the place of the spirit.
8. A mixer of the mod plays each sound that has a place. It gives the sound a
   direction for two ears with a height, a near field, and a loss of high
   frequencies with distance. The bed of the atmosphere turns with the
   world. The mixer takes its signal from the stream that QMixer writes.
9. The mod plays soft cues for the moves of the player: a turn step, a zoom
   step, a teleport and the actions of the hands.

## 2. The chain from the game to the speakers

```
runblack.exe
  GAudio (GGlobal::Global 0x00CD3B20), LH_AudioSystem* at GAudio+0x14
    imports LH_AudioSystem methods from LHaudiodllR.dll (thunks 0x007C5142..0x007C5258)
      LHaudiodllR.dll (Lionhead, built 2001-02-20)
        imports 30 QSWaveMix* functions from QMIXER.dll
          QMixer.dll: its own mixer, output by DirectSound or waveOut
```

### What the game imports (V)

`runblack.exe` lists `LHaudiodllR.dll` as a dependency. The symbol file has 60
or more `LH_AudioSystem` imports. The ones that matter here:

| Import | Thunk in the exe | IAT slot | Game callers |
| --- | --- | --- | --- |
| `LHListenerUpdate` | `0x007C5166` | `0x008A971C` | 1: `0x004271EF` |
| `LHSampleUpdate3DChannels` | `0x007C516C` | `0x008A9720` | 1: `0x004271D7` |
| `LHSampleRegister3DObjectFunction` | `0x007C5142` | `0x008A9704` | 1: `0x00426E75` |
| `LHSamplePlay` | `0x007C5202` | `0x008A979C` | 2: `0x00429FDF`, `0x005BB544` |
| `LHSampleSet3DPosition` (two forms) | `0x007C51CC`, `0x007C51D2` | `0x008A977C`, `0x008A9754` | a few, in `0x004287AE` and the object code |
| `LHMusicSet3DPosition` | `0x007C518A` | `0x008A9734` | 3: chant music, creature dance music, `0x00429658` |
| `LHWaveGetQMixerDirectSoundObject` | `0x008A5B06` | | not called |

The thunk is a `jmp [IAT]`, and the game calls through the IAT slot directly.
To find the callers, search for references to the IAT slot.

### What `LHaudiodllR.dll` imports (V)

`LHaudiodllR.dll` imports these 30 functions from `QMIXER.dll`:

```
QSWaveMixInitEx  Activate  SetOptions  OpenChannel  CloseSession
SetSpeakerPlacement  SetListenerPosition  SetListenerOrientation
SetListenerVelocity  SetListenerRolloff  OpenWaveEx  FreeWave  PlayEx
ConfigureChannel  EnableChannel  GetChannelParams  SetSourcePosition
SetPolarPosition  SetDistanceMapping  SetVolume  SetFrequency  SetPanRate
StopChannel  PauseChannel  RestartChannel  FlushChannel  IsChannelDone
GetPlayPosition  Pump  GetDirectSound  GetLastError  GetErrorText
```

It does not import `dsound.dll`. It does import `winmm.dll` (waveOut volume,
`timeSetEvent`), `msacm32.dll` (the ADPCM style decoder), `ole32.dll`
(`CoCreateInstance`, for DirectMusic and DirectSound) and `LHLogR.dll`.

Mixed 3D sound is all QMixer. The DLL never calls a DirectSound 3D method
itself. (V)

### What QMixer does (V for the strings and tables, I for the run time choice)

`QMixer.dll` has 109 exports. It imports `winmm.dll` (`waveOutOpen`,
`waveOutWrite` and others), `ole32.dll` (`CoCreateInstance`), `kernel32.dll`
(`CreateThread`, `SetThreadPriority`, `CreateMutexA`) and `user32.dll`
(`CreateWindowExA`). It has no import of `dsound.dll`. It creates DirectSound
through COM. The class id `CLSID_DirectSound` (`0x363B8`) and `IID_IDirectSound`
(`0x363D8`) are in `.rdata`.

The strings show two output modes:

```
QMIXER:  detected DirectSound emulation
QMixer:  latency too big for primary buffer, reduced to %d ms
QMixer: using waveOut mode, detected DMA buffer size = %d ms (%d bytes)
```

QMixer also contains the two DirectSound 3D interface ids. `IID_IDirectSound3DListener`
is used at `0x4E32` and `IID_IDirectSound3DBuffer` at `0x5396` and `0x28CB4`.
The use at `0x5396` sits behind a test of a capability flag (`0x2000000`, which
looks like `DSBCAPS_CTRL3D`). So QMixer has a hardware 3D path.

On a modern PC, QMixer does not use the hardware 3D path and not the wave
device. It mixes in software into a secondary DirectSound buffer: 22050 Hz,
16 bit, stereo. The 3D positioning is QMixer's own software algorithm. (V,
measured with the capture of section 11)

QMixer locks a part of that buffer, mixes its channels into it, and unlocks
it, about 60 times a second (a block of about 16 ms). The first block is 200
ms. QMixer gives no block while no channel plays. In the world the bed of the
atmosphere plays, so the blocks do not stop. The game calls
`QSWaveMixActivate(1)` when its window is not in front, and QMixer mixes all
the time. So the sound runs with no window in front. (V)

Both DLLs are in the game folder. There is no `qmixer.dll` in
`C:\Windows\SysWOW64`. So the game folder copy loads. (V)

### Configuration (V for the strings, I for the effect)

`LHaudiodllR.dll` reads `Software\Lionhead Studios Ltd\Audio\Override` and
these value names: `SpeakerConfig` (`STEREO` or `QUAD`), `UseHardware`,
`3DSpatialisation`, `Latency`, `HWRate`, `MaxSamp`, `HeapSize`, `NoAudio`,
`Wave`, `Music`, `Midi`, `Redbook`, `Sfx`, `Atmos`. The init code at `0x15820`
sets the speaker placement with `QSWaveMixSetSpeakerPlacement` from
`SpeakerConfig`. The mod does not change the registry.

The game starts QMixer with `QSWaveMixInitEx`: 22050 Hz, 22 channels, the
flags 8, and the latency 0, which lets QMixer choose (50 ms). The config has
0x40 bytes, with the latency in ms at `+0x1C`, filled at DLL RVA `0x1589E`.
QMixer writes its first block 4 times the latency ahead, and then keeps the
stream 2 to 4 times the latency ahead. The DLL has 16 sample slots. (V)

### Threads (V for the calls, I for who mixes)

- The game main thread calls every `LH_AudioSystem` method. The listener
  update and the sample play calls come from `GGame::EndTurn` and the game
  code. (V)
- `LHaudiodllR.dll` starts a helper thread with `_lhbeginthread` at `0xDF27`
  and `0x15F36`. It also starts `timeSetEvent(20 ms, 10 ms, callback 0x15800)`
  at `0x15C46`. The callback calls `QSWaveMixPump`. So a timer thread of
  `winmm` runs the mixer every 20 ms. (V)
- QMixer creates its own thread and a mutex (`CreateThread`, `CreateMutexA`,
  `InitializeCriticalSection`). (V)
- So audio runs on other threads than the game loop. The QMixer functions take
  a session handle and a channel number. They are built for calls from the app
  thread while the mixer thread runs. (I)

The mod calls QMixer only from the game thread, as the DLL does. The mixer of
the mod runs on the thread of QMixer (section 7).

## 3. The listener

### Where the game sets it (V)

`GAudio::ProcessAudioGameTurn` (`0x00427080`) has two callers:
`GGame::EndTurn` (call at `0x0054E9A6`) and `Temple::ProcessGameTurn`
(`0x00794A5A`). It calls the unnamed function `fn_004270D0`, called
`GAudio::UpdateListener` in this file.

`fn_004270D0` reads the game camera from globals and calls the DLL. This is
the whole call at `0x004271EF`:

```
LHListenerUpdate(this = [GAudio+0x14],
                 position = &[0x00EA1DB8]   (3 floats)
                 look_at  = &[0x00EA1DC4]   (3 floats)
                 up       = &[0x00EA1D2C, 0x00EA1D38, 0x00EA1D44]  copied to the stack)
```

- `0x00EA1DB8` and `0x00EA1DC4` are the stored camera position and focus. This
  is the pair that `head_camera.cpp` gives back to the game after
  `UpdateCamera`. So the game listener is the game camera, with no head in
  it. (V)
- The up vector is column 1 of the matrix at `0x00EA1D28`, three floats
  at stride 12. The mod writes that matrix from the head camera and from the
  room camera swap in `spirit_anchor.cpp`. So the up vector can carry head roll
  or a sub camera. It is not reliable. (I)
- The DLL entry point is `?LHListenerUpdate@LH_AudioSystem@@QAEXPAVLHAudioPoint@@00@Z`,
  RVA `0x3850`, `thiscall`, `ret 0xC`. The second argument is a **look at point**,
  not a direction: the DLL computes `forward = look_at - position` and
  normalizes it (code at `0x3A7A` to `0x3B39`). The third argument is the up
  vector, normalized. (V)
- The DLL keeps a copy (position, look at, forward, up, and a 3x3 basis plus
  translation) in an object at `this+0x58`. It then calls
  `QSWaveMixSetListenerPosition(session, &pos, 0)` at `0x398E` and
  `QSWaveMixSetListenerOrientation(session, &forward, &up, 0)` at `0x39A7`.
  The session handle is at `this+0x88`. (V)
- The same routine also walks 3 music entries (stride `0x44`, until `0xCC`) and
  sets a volume from the distance to the listener, through a virtual method at
  offset `0x3C`. So positional music has distance volume and no pan. (V for the
  code, I for the reading)
- The listener velocity is set to 0 at init (`0x15C1A`). The mod keeps it 0, so
  there is no Doppler shift.

### How often

`GGame::EndTurn` runs once per game turn. The game turn runs at 10 Hz. (V,
measured) `ProcessAudioGameTurn` is called only when a flag test passes
(`[GGame+0x205A40] > 5` and bit 2 of `[GGame+0x14]` clear). So the game moves
the listener at the turn rate, not at the frame rate. The same function
calls `LHSampleUpdate3DChannels` (`0x004271D7`) first. That call moves every
positional sample once per turn (section 4).

### What else reads the listener or the camera (V)

- The DLL object at `this+0x58` (music distance volume). Only
  `LHListenerUpdate` refreshes it.
- The 3D object callback `fn_00427200` (section 4) reads the game camera
  globals `0x00EA1DB8` to work out the distance of a source.
- `GAudio::PlaySoundEffect` (`0x00429E30`) tests the distance of a 3D sound to
  the game camera before it plays it. It uses `GCamera::GetDistanceSq`
  (`0x00442D10`) or the globals when `[GGame+0x205A28] == 1`. It drops a sound
  that is beyond the maximum distance of its sample.

These three decide what plays and what stops. They are game logic. Section 6.4
says how the mod changes the distance that they use.

## 4. Positional sources

### The hook point that sees every sample (V)

Two places in the exe start a sample:

| Caller | Address | What |
| --- | --- | --- |
| `GAudio::PlaySoundEffect(LH_SamplePlayOptions*)` | call `0x00429FDF`, return `0x00429FE5` | Everything else: 86 call sites reach this function, among them creatures, abodes, the Hand, script sound, sound tags and the interface. |
| `HelpDude::PlaySample` | call `0x005BB544`, return `0x005BB54A` | The advisors' speech. One caller: `HelpDude::UpdateSaySentence`. |

The first one drops the returned sample info (`xor eax, eax` at `0x00429FE5`).
The second keeps it (`0x005BB54A`).

The DLL has four more callers of `LHSamplePlay`:

| Return address (DLL RVA) | Function | What |
| --- | --- | --- |
| `0x1A87`, `0x1E41` | `LHAtmosProcess` | the sounds of the atmosphere |
| `0x1490C`, `0x14BC3` | the two `LHSamplePlayAnimEffect` | the sounds of animations, for example the Creature's steps |

The game imports both `LHSamplePlayAnimEffect` forms (IAT `0x008A96D0` and
`0x008A96D8`) and calls them from `GAudio::SamplePlayAnimEffect` (`0x0042A5D6`
and `0x0042A4E1`).

`LHSamplePlay` (`?LHSamplePlay@LH_AudioSystem@@QAEPAULH_SampleInfo@@PAVLH_SamplePlayOptions@@@Z`,
RVA `0x113B0`, `thiscall`, `ret 4`) is one function that all six callers
reach. It returns the sample info. The mod hooks the DLL function itself, so
it sees all six.

### The options object (V from the code, I for names)

The game fills a `LH_SamplePlayOptions` (size `0x168`, constructor at RVA
`0x10E90`) and hands it to `LHSamplePlay`. The fields that matter:

| Offset | Meaning | Evidence |
| --- | --- | --- |
| `+0x04` | the bank | `GAudio::PlaySoundEffect` at `0x0042A064`, `HelpDude` at `0x005BB3A8` |
| `+0x08` | the 3D flag. 1 means positional | `0x00429E64` tests it, `HelpDude::Say` clears it at `0x005BB3D0` |
| `+0x0C` | second flag, default 1. It is the update flag of the sample (`info+0x4C`) | constructor, `0x00429FC6` |
| `+0x20` | the "object": a `Base*` for GAudio sounds, the number `0x270C` for the advisors | `0x0042A0FB` stores arg 0, `0x005BB3C6` |
| `+0x24` | the sample number in the bank | `0x0042A05A`, `0x005BB3C0` |
| `+0x2C` | the pan, default `0x5A` (90). The flat path maps 0 to 180 onto an angle | constructor, DLL `0x12640` |
| `+0x30` to `+0x38` | the position | `0x0042A08F` to `0x0042A0A1`, `0x005BB3AC` to `0x005BB3E0` |
| `+0x3C` to `+0x44` | an offset added to the position | `0x00429E6F` |
| `+0x50` | the play mode. Mode 2 plays one copy at a time | DLL RVA `0x11200` |
| `+0x54`, `+0x58`, `+0x5C` | defaults `1.0`, `10000.0`, `0.3`. Minimum distance, maximum distance and rolloff of the distance mapping | constructor, `0x10EF1` to `0x10EFF` |

The DLL copies the options and may replace some fields from the bank sample
flags (bits `0x4`, `0x8`, `0x10`, `0x20` of the flag word at `bank_sample+0x244`,
code `0x1193B` to `0x11A54`). If bit `0x4` or `0x8` is set, the DLL treats the
sample as 3D whatever the game asked.

The bank sample has these fields (V): the flag `0x40` at `+0x244` marks a
loop, the loop count is at `+0x248` (-1 for a loop without end), the range is
at `+0x268` and `+0x26C`, the play mode is at `+0x274`, and the flag `0x400`
at `+0x244` makes the DLL start one more copy for each request. The priority
is at `info+0x70`. The loop count that `QSWaveMixPlayEx` got is at `info+0x40`.

The waves in `HelpSprites.sad` (1923 samples) and `villagers.sad` (1328) are
all mono, 22050 Hz, format tag `0x50`. `Guidance.sad` is mono, 22050 Hz too
(114 samples with tag `0x50`, 113 with tag 1). Mono is the input QMixer needs
for 3D. (V, read from the files)

### How the DLL places a sample (V)

In `LHSamplePlay` when the 3D flag is 1 (RVA `0x1205B` to `0x121F2`):

1. `QSWaveMixConfigureChannel` (flags `0x2020000` or `0x2010000`) and
   `QSWaveMixEnableChannel` set the 3D mode.
2. `QSWaveMixSetDistanceMapping(session, channel, 0x20, {size 0x10, min, max, scale})`.
3. `QSWaveMixSetSourcePosition(session, channel, 0, {x, y, z})` with the
   **world** position plus the offsets.
4. `QSWaveMixSetVolume`.

QMixer then works out the direction and level from its own listener. The DLL
does no listener math for samples. The session handle is `*(HANDLE*)(self + 0x88)`
and the QMixer channel is `*(DWORD*)(si + 0x2C)`.

When the flag is 0 (RVA `0x125AB` to `0x1272A`), the DLL gives the channel
other flags (`0x1020000` or `0x1010000`) and no 3D mode. It calls
`QSWaveMixSetPolarPosition` with an angle from the pan (the constants `0xB4`
and `0x5A` at `0x12640` and `0x12655`) and a range. A polar position is
listener relative. So a flat sound stays in the same place in the head, wherever
the head turns. (V for the call, I for the QMixer meaning)

`SetPolarPosition` and `SetSourcePosition` both queue a command for the
mixer (`QMixer.dll` `0x1520`), so they end in the same place.

### Moving sources: the object callback (V)

At startup, `fn_00426D40` (the GAudio constructor, call at `0x00426E75`)
registers a callback with `LHSampleRegister3DObjectFunction(fn_00427200,
800.0f)`. The value `0x44480000` is 800.0. `LHSampleUpdate3DChannels` (RVA
`0x14310`) runs once per turn. For every playing 3D sample it calls the
callback, and it stops a sample when the callback returns 0 or the distance is
past `si+0x6C` (the maximum, from `options+0x58`). Then it calls
`LHSampleSet3DPosition(si, x, y, z, 0)`. With flag 0 that ends in
`QSWaveMixSetSourcePosition` (RVA `0x13EA5`). With flag 1 it computes an angle
and calls `QSWaveMixSetPolarPosition` (RVA `0x13E6A`).

The callback (`0x00427200`):

- `si+0x18` is the object (it is `options+0x20`). Value `-1`: not positional.
  Value 0: the source is at the camera position (a sound glued to the
  listener). Other: `dynamic_cast` to `GameThing` and call the virtual
  `Get3DSoundPos` at vtable offset `0x10`.
- It adds the sample's own offset (`si+0x5C`, `+0x60`, `+0x64`).
- It measures the distance to the game camera (`0x00EA1DB8`).

`Get3DSoundPos` overrides seen: `GameThingWithPos` (`0x0056FE20`, converts the
map cell to world with a land altitude), `Dance` (`0x0050C250`), `PSysSound`
(`0x006D1000`, the sound of a particle system), `GInterface` (`0x005CEC50`, a
point from the interface), `HelpDude` (`0x005C20E0`), and the `Base` default
(`0x004011E0`, returns 0, which means no position).

### Who is positional and who is flat

| Sound | How it plays | Positional? |
| --- | --- | --- |
| Villagers, animals, buildings, the Creature, particle systems, tree and rock effects | `GAudio::PlaySoundEffect` with an object and the 3D flag from the caller | Yes when the caller sets the flag. The object callback moves it. |
| Effects of animations, physics and particle systems | `GAudio::SamplePlayAnimEffect` (`0x0042A4B0`) | Yes, with a distance from the caller |
| The Hand | `PlaySoundEffect` from the Hand code | Not checked |
| Atmosphere, single sounds (birds, waves) | `LHAtmosProcess`, 3D with `options+0x14` | Relative to the listener, at a random direction |
| Atmosphere, the bed | `LHAtmosProcess`, flat | One stereo wave in each atmosphere bank |
| Chant music, creature dance music | `LHMusicSet3DPosition` from `0x00427959`, `0x0042814C`, `0x00429658` | Distance volume only. (I) |
| Interface clicks and script "say" sounds | `PlaySoundEffect` with a null object or the flag off | Flat. (I) |
| The advisors' speech | `HelpDude::PlaySample` | Flat. See below. |

A game effect with no object (`options+0x20` is 0) is at the game camera: the
object callback gives `0x00EA1DB8` for it each turn. The atmosphere banks are
prepared by `GAudio::ProcessAtmosBanks` (`0x00428FE0`) and play through
`LHAtmosProcess`. Music and interface sounds do not use the object path.
The object callback gives the game camera for a source with object 0 at
`0x004272F9`.

Two sounds with a known start: sample 43 of the in game bank starts at each
zoom step of the game (`0x0045FC64`) with no object and the default mapping.
Sample 52 of the in game bank starts at the Hand when the Hand comes over an
object (`0x005E621E`).

### How the advisors speak (V)

- `HelpDude::Say` (`fn_005BB340`, called from `fn_005C36D0` at `0x005C3735`)
  fills the options object that is embedded in the spirit at `HelpDude+0x367C`:
  - bank `+0x3680`, sample `+0x36A0` (the sentence number),
  - key `+0x369C = 0x270C`,
  - **3D flag `+0x3684 = 0`** (`0x005BB3D0`),
  - position `+0x36AC` to `+0x36B4`, copied from `HelpDude+0x3374` to `+0x337C`
    (the spirit's world position, the translation of its matrix).
- `HelpDude::UpdateSaySentence` (`0x005BB610`) waits for the delay, then calls
  `HelpDude::PlaySample` (`0x005BB530`). That sets `options+0x164 = 1` and
  calls `LHSamplePlay`. The DLL never reads `+0x164` (only the constructor
  writes it). So the advisors play through the flat path, at the default pan 90.
- `IsTalking` (`0x005BB760`) and `StopSentence` (`0x005BB840`) find the sample
  again with the key `0x270C`: `LHSampleIsPlaying(bank, 0x270C, sample)` and
  `LHSampleStop(bank, 0x270C, sample)`. So **`options+0x20` must stay `0x270C`**.
  If it is an object pointer, the advisors cannot be stopped and the game's
  talk logic breaks.
- `HelpDude::Get3DSoundPos` (`0x005C20E0`) returns `HelpDude+0x3374` and the
  value 1. So the game has a positional design for the spirits and turns it
  off with the flag.

Do not turn the 3D flag on for the advisors. The object callback would read
`si+0x18 = 0x270C` as a pointer and crash inside `RTDynamicCast`. (V for the
code, I for the crash)

`HelpDude+0x3374` is the translation of the spirit's world matrix.
`spirit_anchor.cpp` keeps that position fixed in the room, so the position
does not follow the head. `kSpiritNear` and `kSpiritFar` (`HelpDude+0x35B8`,
`+0x35BC`) come from a data file.

## 5. The distance model and the game's range tests

### The distance model of QMixer (V)

QMixer computes the distance gain in `0xAD4C`, with the rolloff function at
`0x2CE50`:

```
d > max            gain 0 (silent)
d <= min           gain 1
rolloff 1          gain min / d
else               gain min / (min + rolloff * (d - min))
```

`audio_math::QMixerDistanceGain` is this model. The gain depends only on
`d / min` and `d / max`.

`QSWaveMixGetChannelParams` (QMixer RVA `0x1950`) copies `0xDC` bytes of the
channel parameters. These fields were read (V, from the code at `0xAA70` and
`0xAD4C`):

| Offset | Field |
| --- | --- |
| `+0x04` | the channel type: `0x02010000` positional, `0x01010000` flat |
| `+0x10` | the source position in the world |
| `+0x40`, `+0x44`, `+0x48` | min, max and rolloff of the distance mapping |
| `+0xA8`, `+0xAC`, `+0xB0` | range, azimuth and elevation from the listener |
| `+0xB8` | the distance gain |
| `+0xBC` | the total gain |

The `min`, `max` and `rolloff` of the mapping are in world units. Each sample
has its own mapping in its bank. That is the class of the sound. For example,
a footstep of the Creature has the mapping 30, 80, 4, a sound of the Hand 100,
300, 4, and a sound with no mapping 1, 10000, 0.3. The game has no other table
of loudness.

### The tests that decide what plays (V)

The game decides which positional sounds play with its own camera, in world
units:

| Test | Where | Rule |
| --- | --- | --- |
| The start of a game effect | `GAudio::PlaySoundEffect` `0x00429E30`, the compare at `0x00429F16` | No start when the squared distance from the game camera is more than the squared maximum distance of the sample. The maximum is `GAudio::GetGSFXSampleMaxDistance` `0x0042A430`, or `options+0x58` when that gives 0. The call of that function is at `0x00429EF7` |
| The stop of each positional sample | `LHSampleUpdate3DChannels`, DLL RVA `0x14310`, each game turn | For a sample that plays with the 3D flag (`info+0x48`) and the update flag (`info+0x4C`, from `options+0x0C`), the object callback `0x00427200` gives the distance from the game camera. When the callback gives no position, or the distance is not less than `info+0x6C`, the DLL calls `LHSampleStop` |
| The start of an animation effect | `GAudio::SamplePlayAnimEffect` `0x0042A4B0` | The caller gives the distance from the game camera. The DLL does not start an effect past 800 units or past the maximum of its sample (DLL RVA `0x14A83` to `0x14AC6`) |
| The start of a delayed sound | `SoundTag::CheckDelay` `0x0071E760`, and the process of the particle sounds at `0x006D1312` | Also the game camera |

Nothing in the DLL starts a stopped loop again. The game does, in two ways:

- **A sound tag** (`SoundTag`, vtable `0x00980538`). The script command
  `GScript::AttachSoundTag` `0x00710150` makes one with `SoundTag::Create`
  `0x0071E840`. `SoundTag::ProcessSoundTags` `0x0071E5F0` runs the list at
  `GGame+0x205C1C`. Each tag with an object calls `GAudio::PlaySoundEffect`
  in each game turn (`0x0071E6F2`). When the sample plays already, the DLL
  does nothing (play mode 2). So the sound of a tag starts again in the first
  game turn in which the game camera is in range.
- **The sound of a particle system** (`PSysSound`). `AtomCore::StartSound`
  `0x006745D0` starts it one time through `GAudio::SamplePlayAnimEffect`,
  with no distance test. The process of these sounds (`0x006D11A0`) deletes
  the `PSysSound` when its sample does not play. The rule
  `StartStopSoundOnCondition` `0x0069DC40` starts the sound again when the
  atom has no sound for the action. An effect that only adds the sound
  (`AddSoundToAtom` `0x0069DCA0`) does not.

When no sample slot is free, the DLL takes the slot of the sample with the
lowest priority, if the new sample has a higher one (DLL RVA `0x112CA`).
Otherwise it refuses the new sample.

### The game camera in the God view

In the God view the game camera is 400 units from the player's place, 50
degrees up (`god-view.md`). The ears are the head. So the game tests and the
ears disagree. At 100 units a meter, a sound with a maximum of 500 can be on
the land 3 m in front of the player. It is 636 units from the game camera, so
the game does not start it or stops it. For the head it sounds as 122 units,
near full level. A snap turn puts the game camera on the other side of the
player, and a teleport moves it. Section 6.4 is the answer.

## 6. What the mod does

### 6.1 Files and hooks

All hooks run on the game thread, except the mixer of the mod. They are
MinHook hooks on exports of `LHaudiodllR.dll` and `QMixer.dll`, and on
functions of the exe.

| File | What it does |
| --- | --- |
| `audio.h` | The interface between the files of the sound |
| `audio_listener.cpp` | The `LHListenerUpdate` hook and the frame driver. The ears are the head |
| `audio_sources.cpp` | The `LHSamplePlay` hook and the tracker. It steers the advisors and the flat sounds that have a place |
| `audio_rolloff.cpp` | The hooks on `QSWaveMixSetDistanceMapping`, `SetSourcePosition` and `SetPolarPosition`. The room distances |
| `audio_range.cpp` | The hooks on the object callback `0x00427200`, `GAudio::PlaySoundEffect`, `GAudio::GetGSFXSampleMaxDistance` and `GAudio::SamplePlayAnimEffect`. The ears decide what plays |
| `audio_voices.cpp` | The mixer of the mod |
| `audio_capture.cpp` | The hook on the stream of QMixer: the mixer takes its block here, the capture writes it, and the latency is set here |
| `audio_cues.cpp` | The soft sounds for the moves of the player |
| `audio_trace.cpp` | Log only hooks (section 11) |
| `audio_math.h`, `audio_spatial.h` | The pure math, with checks in `bwvr/test/controller_math/` |

Each hook that patches a function of the exe checks the first bytes of the
function. When they are not the bytes of version 1.42, the hook is not made,
the log says so, and the game camera decides. With `audio_head=0` and
`audio_advisors=0` the mod hooks nothing.

### 6.2 The listener is the head

`LHListenerUpdate` (DLL RVA `0x3850`) is a `__fastcall(self, edx, pos,
look_at, up)` in the hook. The callee cleans 12 bytes. The hook ignores the
three arguments when the head camera is fresh and sends the head instead:

- `HeadCameraRenderBasis(basis, position)` from `head_camera.cpp` gives the
  rendered camera: the position and the columns right, up and forward, in game
  world coordinates, with no eye offset.
- `pos` is the position. `look_at` is `pos + forward * 10`. `up` is the up
  column, with head roll. The ears roll with the head.
- The mod does not convert axes. The game passes world coordinates to QMixer,
  and the head basis is in the same world.
- "Fresh" means the head camera is on and the main camera serial changed in
  the last 300 ms. Menus and loading screens make no main camera call, so the
  ears go back to the game camera there. Also, if there is no render camera
  yet, the head camera is off, or the world is not up, the hook calls the
  original with the game arguments.

**The frame driver.** The game calls the update once per turn. A head turns
at frame rate. So the mod also calls the original `LHListenerUpdate` every
rendered frame with the same values. The driver runs from the
`LH3DRender::FinishFrame` hook (`0x0082F460`) through `AudioFrame()`.
`FinishFrame` runs once for each eye pass, so the driver latches on
`HeadCameraMainSerial()`, which goes up once per rendered frame. Between
frames QMixer holds the world positions of the sources and works out the
direction against the newest listener at each mix block. So one update per
frame is enough for a head turn.

The audio system pointer is `*(*(0x00CD3B20) + 0x14)` (V: `mov ecx,[0xCD3B20]`
then `mov ecx,[esi+0x14]` at `0x004270BB`). `0x00794A54` also loads it, then
calls `0x00427080`. The hooks also keep the
`this` pointer that they receive (`AudioNoteSystem`), because the global gives
no system before the world is up. The driver reads the session handle
`LH_AudioSystem+0x88` in each frame, and logs a change of it.

### 6.3 Room distances

`audio_rolloff.cpp` hooks `QSWaveMixSetDistanceMapping`. While the table or
the God view owns the view and the ears are the head, it sends min and max
times

```
k = (100 / audio_units_per_meter) * (s / 100) ^ (audio_zoom_pct / 100)
      kept between 0.05 and 100
```

with `s` the scale of the view in units per meter (`audio_math::ZoomFactor`).
All distances are divided by `k`. At the start scale of 100 units a meter, and
`audio_units_per_meter=40`, `k` is 2.5. One meter in the room then sounds as
40 units of the design of the game. With the Creature's steps (min 30, max 80)
the level is full within 0.75 m and the sound is silent past 2 m.

The distance factor grows more slowly than the scale, so the zoom changes how
far a sound carries. With the power 0.5 (`audio_zoom_pct=50`):

| Scale (units a meter) | k | A sound with the maximum 300 carries | One with the maximum 2000 carries |
| --- | --- | --- | --- |
| 7.5 (life size) | 0.68 | 27 m | 183 m |
| 100 | 2.5 | 7.5 m | 50 m |
| 225 | 3.75 | 5 m | 33 m |
| 1000 | 7.9 | 2.4 m | 15.8 m |
| 4000 | 15.8 | 1.2 m | 7.9 m |

At 4000 units a meter the island is 1.3 m across. A voice is heard only near
the hand, and a loud event is heard from each place. The low pass of section
7.4 works on the part of the range, so the far part of a loud event is mostly
low frequencies. With `audio_zoom_pct=100` the distances are the same at each
zoom. Fewer sounds are in range at a far zoom, which also helps the 16 sample
slots.

`k` goes to QMixer's mappings, to the mixer of the mod and to the range tests
of section 6.4, so all three agree.

Rules:

- When `k` changes by more than 1%, the frame driver sends the mapping again
  for each channel that plays a positional sample. So a zoom step or a two
  hand grab changes the level smoothly.
- A flat sample gets no mapping from the DLL and keeps the last one of its
  channel. So when a flat sample starts on a channel with a scaled mapping,
  the hook on `QSWaveMixSetPolarPosition` sends the game's mapping first.
- A sample relative to the listener keeps the game's mapping, because its
  range is not a world distance.
- A mapping that is set outside `LHSamplePlay` (the music of the DLL) goes to
  QMixer unchanged. The mod does not touch the channel until a sample starts
  on it.
- With the ears on the game camera (test key step 5, or no head camera frame),
  `k` is 1 and every channel gets the game's mapping back.
- QMixer still mutes a sound past `max * k`, and the game still stops a sound
  past its maximum distance from the game camera (`GAudio::PlaySoundEffect` at
`0x00429EFC`, and the object callback), except as section 6.4
  changes it. So a sound across the island is silent at every zoom.

**The sounds at the listener.** A positional sample with no object is at the
game camera. The hook on `QSWaveMixSetSourcePosition` (in `audio_rolloff.cpp`)
keeps its offset from the game camera and sends the ears plus that offset. The
frame driver moves it with the ears. With the ears on the game camera it goes
back to the DLL's point. A sound that starts at a point of its own stays there
until the game moves it to its camera at the next game turn.

**The ears in the God view.** In the God view and on the table the ears are the
head while the room distances are on. Only the head gives the right direction
to a sound below the player. With `audio_room=0` the rule `TabletopEars` applies:
the ears are at most 400 units over the land. See `god-view.md`.

### 6.4 The ears decide what plays

`audio_range.cpp` has the hooks below. They act while the ears are the head
and the room distances apply: the table or the God view owns the view. In a
story, in a menu, with the ears on the game camera and with `audio_room=0`,
the game's tests apply. The loop rule is the one exception. All of them need
`audio_range=1`.

- **The stop test.** A hook on the object callback `0x00427200` puts the
  distance from the ears divided by `k` in the answer
  (`audio_math::RangeDistance`). The DLL compares it with the maximum of the
  sample. So a sample stops at `max * k` from the head. That is where QMixer
  makes it silent.
- **A loop keeps its slot** (`audio_loops_keep=1`). A loop is a sample with a
  loop count (`info+0x40`). For a loop that is out of range, the hook gives
  the distance 0. The DLL does not stop the loop, and QMixer makes it
  silent. When the head comes back in range, the loop sounds at once, and the
  game does not have to start it again. This rule applies in each view, also
  in a story and in a menu. The DLL still stops a loop when its object has no
  position. The game also still stops a loop with its own logic (a script, the
  end of an effect).
- **The reserve of slots.** The DLL has 16 sample slots. A loop out of range
  stops, as in the game, when fewer than half of the slots are free
  (`audio_math::KeepSilentLoop`). So silent loops do not take the slots that
  new sounds need.
- **The start test.** A hook on `GAudio::PlaySoundEffect` makes the test with
  the same distance. Out of range, the hook returns, and the game does not
  play the sound. In range, the hook calls the game's function. A hook on
  `GAudio::GetGSFXSampleMaxDistance` then gives a very large maximum to the
  one call at `0x00429EF7`, so the game's distance test passes. The other
  tests of the function stay: the interface state and the citadel.
- **The start of animation effects.** A hook on `GAudio::SamplePlayAnimEffect`
  gives the distance from the ears divided by `k` when the caller gave a
  distance and the object has a place.
- **A sound with no object** is at the game camera, and section 6.3 puts it at
  the ears. It keeps the game's tests.

So a sound tag starts and stops its sound with the range of the head, at each
zoom.

### 6.5 Sounds the game plays flat

`audio_sources.cpp` steers more than the advisors. Each tracked sound has a
place. Every frame it gets the azimuth and the elevation of that place in the
frame of the head, with the polar call that the DLL uses for a flat channel:
`QSWaveMixSetPolarPosition(session, channel, 0x20, {azimuth, range, 0})`. The
range stays what the DLL sent, so the level does not change. No channel
changes mode.

The azimuth is `atan2(right . d, forward . d)` in degrees, 0 to 360, with `d`
the place minus the head position. That is the DLL convention (pan 90 is angle
0, and the angle grows toward the right). The sign is in `Azimuth` in
`audio_sources.cpp`.

The mod picks the sound by the return address of the `LHSamplePlay` call.

- **The advisors** have the return address `0x005BB54A`. The options pointer is
  `HelpDude + 0x367C`, so `HelpDude = options - 0x367C`. The place is
  `HelpDude + 0x3374`, read each frame. The elevation is 0. The advisors play
  with a range of 1.0 and keep the key `0x270C`.
- **A flat game effect with an object** has the return address `0x00429FE5`
  and `options+8 == 0`. If `Get3DSoundPos` (vtable offset `0x10`) gives a
  position when the sound starts, that point is its place. The point is read
  once. A flat sound has no object callback, so the game does not promise
  that the object lives as long as the sound.
- **A sound of the atmosphere relative to the listener** gets a point in the
  world when it starts, 3 m from the ears in the room, in the direction that
  the DLL picked. `LHAtmosProcess` (DLL RVA `0x1C2A`) gives two random values
  at `+0x30` and `+0x34` and 0 at `+0x38`, and the relative path reads them as
  right and up. The mod reads the second value as forward, so the birds are on
  a ring around the player at the height of the ears, not under the land. This
  applies only while the table or the God view owns the view. In a story the
  camera flies, and the sound stays as the game made it.
- **The land of the kind.** When the nearest cell of the kind of a sound is
  1.5 m of the room or more from the ears, the sound goes toward it, 3 m
  away, with a spread of up to 26 degrees to each side (`audio_atmos_toward=1`).
  When the land of the kind is all around the player, the ring stays. The game
  knows where the land of each kind of atmosphere is: `GSoundMap`
  (`*(GGame+0x250058)`) keeps for each of the 14 `ATMOS_TYPE`s, from `+0x08`,
  12 bytes: the count of cells near the game camera (a word), the squared
  distance of the nearest one (a float), and its x and z in world units (two
  words). `AtmosMapTypeInfo::Add` `0x0071D510` writes them. Bank `i` at
  `GAudio+0x194` is type `i`.
- **The DLL's own polar calls.** When the DLL sends a new polar position to a
  steered channel outside a play, the hook puts the direction of the place in
  the call and keeps the range.

The registration is a list of `{si, key, place}`. A new sample in the same
info slot or on the same channel ends the old entry. The entry also ends when
the info key or the channel changes, or when the sample stops. The game thread
touches this list only. For a moving object the mod reads the position again
each frame, guarded with `IsBadReadPtr` on the object and its vtable.

Sounds with the 3D flag on are already moved by the game once per turn. The
ears at the head give them head tracking.

## 7. The mixer of the mod

### 7.1 Why QMixer cannot do it (V)

QMixer's own sound for a source to the side is a pan for two speakers with a
phase trick. The right channel is louder by the same amount at each frequency,
also at 63 Hz, and the two channels have a negative correlation. On
headphones it has no cue for height, and the phase trick is a pressure in the
ears. QMixer has no call that gives the signal of one channel back.

### 7.2 Where the mod takes the signal (V)

- `QSWaveMixPlayEx(session, channel, flags, wave, loops, params)`. `wave` is
  a MIXWAVE of 0x48 bytes: a PCMWAVEFORMAT at `+0x00`, the pointer to the
  samples at `+0x10`, the bytes at `+0x14`, and QMixer's own object at
  `+0x44` (the frames at `+0x10` of it). The samples are the DLL's own
  memory: 16 bit PCM at the rate of the sample, not converted
  (`QSWaveMixOpenWaveEx` with the flags 8 gets the same pointer).
- `params` has 0x28 bytes: the callback of the DLL at `+0x0C`, the sample
  info at `+0x10`, and four play positions from `+0x14`, which are 0.
- `QSWaveMixSetVolume(session, channel, flags, volume)`: the full volume is
  32767, and the DLL passes the flags 0x20. For a stop it sends the volume 0
  with the flags 0, then `QSWaveMixFlushChannel`.
- `QSWaveMixSetFrequency(session, channel, 0x20, hz)` comes before each
  play.

### 7.3 The stream

The mod gets the DirectSound object from `QSWaveMixGetDirectSound`. It makes
two buffers of its own for a moment, a secondary one and the primary one, to
find the code of `IDirectSoundBuffer::Unlock` for each class, and it hooks that
code. A call that comes from `QMixer.dll` is a block of the stream. The import
table of `QMixer.dll` for the wave device is patched too, for a PC where
QMixer uses it. The hook on `IDirectSoundBuffer::Lock` gives the place of each
write.

The stream is the last place before the device. The mod adds its own voices
there, so they need no device of their own and no change of a Windows
setting. The mixer stays off when the stream is not 16 bit stereo.

### 7.4 How it works

For a play of `LHSamplePlay` that sets a place for its channel
(`QSWaveMixSetSourcePosition` in the same play, not relative to the listener,
not an advisor), the hook on `QSWaveMixPlayEx` copies the wave and starts a
voice of the mod. QMixer plays the same channel with the volume 0. So QMixer
is still the clock: it tells the DLL when the sample is done, and the game's
logic does not change. The voice follows the channel through the hooks on
`SetVolume`, `SetFrequency`, `StopChannel`, `FlushChannel`, `PauseChannel` and
`RestartChannel`. The hooks of `audio_rolloff.cpp` give it the place and the
game's distance mapping.

For each block of the stream, the mod renders each voice and adds it to the
block. A voice gets its level from the same model as QMixer
(`QMixerDistanceGain` with the distance divided by `k`), so the range of a
sound is the same with the mixer on and off. The level of a voice at its
minimum distance is 0.7 for each ear. The mixer gives a sound the same
loudness as QMixer does.

**The direction.** It comes from a structural model of the head
(`audio_spatial.h`), after Brown and Duda, "A Structural Model for Binaural
Sound Synthesis" (1998):

| Part | What it does |
| --- | --- |
| The delay between the ears | Woodworth's formula for a head of 8.75 cm: up to 0.66 ms |
| The shadow of the head | One pole and one zero for each ear: +6 dB of high frequencies at the near ear, -20 dB at 150 degrees |
| The echoes of the outer ear | Five echoes whose delays change with the elevation. The second one makes a notch that goes from 4.9 kHz (straight below) to 11 kHz (straight above) |
| The height band | A shelf over 5 kHz: +7 dB straight above, 0 at the level of the ears, -9 dB straight below (`audio_height_pct` scales it) |
| The body | The part over 1.5 kHz loses 2.5 dB straight below and 3.1 dB straight behind. A source over the ears gets an echo of the shoulder |

A model needs no data set, so there is no file to license and to ship. It
costs a few multiplications for each sample. Its parts have a meaning, so a key
can scale one of them. The samples of the game are 22050 Hz, and the stream is
too, so there is nothing over 11 kHz for a measured HRTF to shape. A measured
set (for example the MIT KEMAR set, which is free to use with a citation) can
replace `audio_spatial::Process` later: the callers give only the place of the
source.

**The near field.** A source keeps a share of its angle to the side: all of it
from 50 cm of the room, 0.2 of it at 20 cm (`audio_spatial::NearFocus`). The
delay and the shadow follow the smaller angle. The height does not change. So
a near sound goes toward both ears.

**Distance.** `part` is where a source is in its own range: 0 at its minimum
distance, 1 at its maximum distance, both from the game's mapping with the
factor `k`. So it is a distance of the room, and it is right at each zoom. A
low pass has its corner at 12 kHz for part 0, 5 kHz half way, and 1.2 kHz at
the maximum. A share of the sound, up to 0.25, goes to both ears with no
direction. A sound with a long range (a crash) is brighter at the same
distance than one with a short range (a voice).

The low pass and the diffuse share work only while the room distances apply.
In a story the game puts its camera where the speech must be heard. There a
far sound keeps its high frequencies, as in the game. The direction for two
ears and the height apply in a story too.

**No click.** Each control (the angles, the level, the corner of the low pass)
goes to its new value with a time constant of 15 ms, and the filters get new
values each 16 samples. A voice starts at its full level, so the attack of a
crash or of a step stays sharp, and only its first 32 samples rise from zero
(1.5 ms). A stop lets the level go to 0 in 80 ms. A sample that is in its
last 0.3 s when QMixer stops the channel plays to its end. A loop of the mod
has no end of its own, so the mod asks QMixer twice a second whether the
channel of each loop still plays (`QSWaveMixIsChannelDone`). When QMixer ended
it with no stop call that the hooks saw, the voice stops too. The sum of the
stream and the voices goes through a soft limiter (`audio_spatial::SoftLimit`):
under 26000 a sample does not change, and over it the curve bends, so a loud
moment has no hard edge. The report counts the samples over the knee.

**What the mixer plays.** Each positional sound that has a place, and the
single sounds of the atmosphere (a bird, a wave, `audio_atmos_mixer=1`). The
DLL plays the single sounds with a polar position. The mixer takes the place
of section 6.5, and the level that QMixer gives the range of the polar call
with the mapping of the channel. So a bird has the two ear model, at the
level of the ears, over the sounds of the land.

**What stays with QMixer:** each flat sound (the advisors, the story speech,
the interface, the music), a flat effect that section 6.5 steers, and a sample
whose wave is not 16 bit PCM. The log gives the count of each. The advisors,
the story speech and the music keep the level of the game, so the balance
between them and the world is the balance of the game.

**The cost.** The mixer works in the thread of QMixer, one block of 16 ms at
a time. The work on the game thread is a few microseconds a frame. No QMixer
call waits for the mixer.

### 7.5 The bed of the atmosphere turns with the world

Each atmosphere bank has one stereo wave, the bed. The mixer plays the bed (a
flat stereo wave of `LHAtmosProcess`), and it turns the stereo field against
the yaw of the head in the world (`audio_spatial::ProcessBed`). With
M = (L + R) / 2 and S = (L - R) / 2:

```
left  = M + cos(yaw) S + sin(yaw) Q
right = M - cos(yaw) S - sin(yaw) Q
```

Q is S through two all pass filters, so it has the level of S and is not in
step with it. At yaw 0 the wave plays as it is. At 180 degrees the channels
have changed places. At 90 degrees both ears get M, and Q keeps the width.
This is a turn of the field, not a swap. The level of each ear is the same
at each yaw (the math test: within 0.05 dB). The yaw is the head's, so a
snap turn, a grab that turns the land, and a real turn of the head all turn
the bed. The level of the bed is the game's (the volume of its channel), and
it moves in steps of 15 ms. `audio_bed=0` leaves the bed flat in QMixer.

### 7.6 The wait of a stop

The audio DLL stops a sample that plays in three steps. Three functions of
`LHaudiodllR.dll` have these steps, at RVA `0x12CC1`, `0x12D79` and `0x12E6E`
[V]:

1. `QSWaveMixSetPanRate(session, channel, 0, 20)` and
   `QSWaveMixSetVolume(session, channel, 0, 0)`: the volume of the channel
   goes to 0, with a pan rate of 20.
2. `Sleep(20)`, on the thread of the caller.
3. `QSWaveMixSetPanRate(session, channel, 0, 100)` and
   `QSWaveMixFlushChannel`.

The caller is the frame thread of the game. `GAudio::ProcessAudioGameTurn`
(`0x00427080`, from `GGame::EndTurn` in each game turn) calls
`LHSampleUpdate3DChannels`, which stops each positional sample that is out of
range (section 6.4). So each such stop holds the frame for 20 ms, and two
stops in one game turn hold it for 40 ms. In a village at 44 units a meter a
desktop run has 4 to 8 of these stops in 10 s (`backend.md`, "The slow
frames").

QMixer has the volume 0 for a channel that a voice of the mod plays, from the
start of the play (7.4). So the wait lets nothing fade there. With
`audio_stop_wait=0`, the default, the mod skips the `Sleep` of step 2 for such
a channel:

- The mod has its own function in the import slot of `Sleep` of the audio DLL.
  Each other `Sleep` of the DLL goes to Windows. The function knows a stop
  by the address that the call returns to: RVA `0x12CEB`, `0x12DAD` or
  `0x12EA2`. The stops of `LHSampleUpdate3DChannels` return to `0x12EA2`.
- The hook on `QSWaveMixSetVolume` keeps the channel of the last call with the
  volume 0 and the flags 0, and the thread that made it. The mod skips the
  wait only when the same thread made that call. The channel must be one that
  a voice of the mod plays, and QMixer must have it at the volume 0.
- The voice of the mod sounds the same. Its level goes to 0 from the volume
  call, with the time constant of each control (7.4). With no wait the flush
  comes 20 ms sooner, so the voice gets 20 ms more before its slot is free.
  The slot is free 100 ms after the volume call, as with the wait
  (`audio_math::ReleaseStart`).
- A channel that QMixer plays (a flat sound, an advisor, the speech of a
  story) keeps the wait. There the wait is the time that the DLL gives QMixer
  for its own fade.

The mod first tests the 4 bytes at 8 before each of the three addresses. They
must be `6A 14 FF 15`. If they are not, the audio DLL is not the known one:
the mod changes nothing, and the log says so. `audio_stop_wait=1` gives each
stop its wait, as in the game. The mixer report (each 30 s) counts the stops:

```
audio mixer  : audio_stop_wait=0. The audio DLL calls Sleep(20) on the thread of the game when it stops a sound that plays. The mod skips that wait for a sound that the mixer of the mod plays: QMixer is silent for it, and the voice of the mod fades the same way. A sound that QMixer plays keeps the wait
audio mixer  : the audio DLL stopped 26 sounds that played. The mod skipped the wait of 20 ms for 24 of them, and 2 kept it (audio_stop_wait=0)
```

With `test_keys=1`, Ctrl and Pause change `audio_stop_wait` between 0 and 1
while the game runs. The pointing controller gives a long pulse when the mod
skips the wait and a short pulse when each stop has it. Windows gives the key
`VK_CANCEL` for Pause with Ctrl down, so the keys of Pause alone (`throw.md`)
do not see the press. The test `audio_stop_wait_test=N` does the same at
frame `N` of the controllers, for a desktop run.

## 8. Cues for the moves of the player

`audio_cues.cpp`. A short, quiet cue plays for a move that the mod makes,
through the mixer, flat:

| Cue | The sound | Length |
| --- | --- | --- |
| A turn step | A swish: noise through a band that moves from 800 to 1700 Hz (the other way for a left turn), a little to the side of the turn | 0.10 s |
| A zoom step | A swoosh: 450 to 1300 Hz to go in, 1300 to 450 Hz to go out | 0.15 s |
| A teleport | A whoosh from 350 to 2000 Hz, then a soft landing at 72 Hz | 0.31 s |
| A hand takes the land | A soft thud at 105 Hz | 0.10 s |
| The grab from a distance has its target | A ping at 1320 Hz | 0.20 s |
| An object arrives in the hand | A tap at 235 Hz | 0.08 s |

The sounds are made in code when the game starts (noise through a state
variable filter, and sine tones that fall away). The mod ships no sound file.
Each one starts and ends at zero. Their peak is about 0.06 of full level, under
each world sound near the player. A cue of a kind waits 60 ms for the next one.

**The moves of the view need no call.** `AudioCuesFrame` sees them in the ears
of each frame: a yaw of more than 15 degrees in one frame is a turn step, a
move of more than 0.5 m of the room in one frame is a teleport, and a change of
the scale by one zoom step (`god_zoom_step_pct`, within 40%) is a zoom step. A
real head cannot do these in one frame. Two hands that scale the land, and
the smooth zoom of the stick (`god_zoom_smooth=1`), change the scale a little
in each frame, and that has no cue. In the God view a cue
also needs a new version of the view (`TabletopGodGameCamera`), which only a
move of the player gives. The game can put the view at a new place by itself
(a new camera mode, the end of a story), and that has no sound.
Nothing plays while a story owns the view, or in the first 45 frames after the
view comes back. A recenter (F11) sounds as a teleport.

**The sounds of the hands** start where their haptic pulse starts, so the hand
and the ear agree in time. `HapticsPulseHands` (`haptics.cpp`) calls
`AudioCueHaptic` with the kind of the pulse: `lock` gives the ping, `arrive`
gives the tap (one for an arrival, though the pulse comes in each frame of the
end of the glide), and `world` gives the thud. The cue plays also with
`haptics_pct=0`. The grab of the land in `world_grab.cpp` calls the thud
cue. See `hand-model.md`, "Haptics".

## 9. Ini keys and test keys

All keys are in `[bwvr]`. None is in the launcher. A value that gives the old
behavior is in the last column.

| Key | Default | What | Old behavior |
| --- | --- | --- | --- |
| `audio_head` | 1 | The ears are the head (section 6.2) | 0: the game camera |
| `audio_advisors` | 1 | The advisors get a direction from the place of the spirit | 0: flat |
| `audio_room` | 1 | Room distances and the sounds at the listener (6.3) | 0: the distances of the game |
| `audio_units_per_meter` | 40 | The world units that one meter of the room sounds as | |
| `audio_flat_places` | 1 | Flat effects and the atmosphere get a place (6.5) | 0 |
| `audio_zoom_pct` | 50 | The power of the scale in the distance factor | 100: the same distances at each zoom |
| `audio_range` | 1 | The head and the room distances decide which positional sounds start and stop (6.4) | 0: the game camera decides |
| `audio_loops_keep` | 1 | A loop out of range keeps its sample slot and is silent, while half of the slots are free | 0: it stops, as in the game |
| `audio_mixer` | 1 | The mixer of the mod plays each sound with a place, and the bed | 0: QMixer pans each sound |
| `audio_height` | 1 | The mixer gives a source its height | 0: each source is at the level of the ears |
| `audio_height_pct` | 100 | The strength of the height band, 0 to 200. Use 60 if the sounds of the land are too dull, 150 if the height is not clear | |
| `audio_near` | 1 | A source nearer than 50 cm goes toward both ears | 0 |
| `audio_far` | 1 | The low pass and the diffuse share by distance | 0 |
| `audio_bed` | 1 | The bed of the atmosphere turns with the world | 0: QMixer plays the bed flat |
| `audio_atmos_mixer` | 1 | The single sounds of the atmosphere (birds, waves) go through the mixer | 0: QMixer plays them, steered by 6.5 |
| `audio_atmos_toward` | 1 | A single sound of the atmosphere goes toward the land of its kind | 0: a random place on the ring |
| `audio_stop_wait` | 0 | The mod skips the wait of 20 ms of the audio DLL at the stop of a sound that the mixer of the mod plays (7.6) | 1: each stop holds the frame thread for 20 ms |
| `audio_cues` | 1 | Soft sounds for the player's moves | 0 |
| `audio_cues_pct` | 100 | Their level, 0 to 300 | |
| `audio_latency_ms` | 0 | The latency that QMixer starts with (section 10) | 0: QMixer's own value, 50 ms |
| `audio_trace` | 0 | Test only: log the calls to QMixer and each sample that starts | |
| `audio_capture` | 0 | Test only: write the stream to `bwvr-audio.wav` | |
| `audio_test_voice`, `audio_test_path`, `audio_test_step_ms`, `audio_test_solo` | 0, empty, 3000, 0 | Test only: the test voice and the test bed (section 11) | |
| `audio_test_tag`, `audio_test_tag_bank`, `audio_test_bank` | 0, 1, -1 | Test only: a sound tag on the Creature, and a list of the loops of a bank | |
| `audio_stop_wait_test` | 0 | Test only: the frame of the controllers at which `audio_stop_wait` changes, as the test key does (7.6) | |

`audio_test_tag` is a sample number. The mod puts a sound tag with this sample
on the player's Creature, 120 frames after the table or the God view has the
view. `audio_test_tag_bank` is the bank type of that sample (1 is the in game
bank). `audio_test_bank=1` lists the samples of a bank that loop without end.

**Test key.** Ctrl Shift F12 (with `test_keys=1`) has five steps. Each step
takes one more change away, and the log names the step. Press it with the
mouse at rest, because Ctrl and Shift together start the game's zoom to the
Hand while the mouse moves.

1. All as set.
2. The distances are the same at each zoom (the sound before `audio_zoom_pct`).
3. Also QMixer pans each sound, with no height (the sound before
   `audio_mixer`). The change is at once, in both directions: the voices of
   the mod keep their time while QMixer plays.
4. Also the game camera decides which sounds play (the sound before
   `audio_range`).
5. The ears are on the game camera. This is the sound of the game, exactly. The
   room factor is 1, the sounds at the listener go back to the game camera, and
   every steered sound goes back to the DLL's polar position.

Ctrl and Pause (with `test_keys=1`) change `audio_stop_wait` (7.6).

## 10. Latency

The mod's voices go into the stream that QMixer writes, so they are as far
ahead of the device as that stream. A turn of the head is heard that much
later. The report of the mixer has the line "QMixer writes the stream N ms
ahead of the device on average (least to most)", and a count of the times the
device passed a write, which is a gap in the sound. The hook on
`IDirectSoundBuffer::Lock` and the play cursor of the buffer give these values.

A hook on `QSWaveMixInitEx` can set the latency (`audio_latency_ms`). The
default 0 leaves QMixer's own value (50 ms), which keeps the stream about
116 ms ahead of the device. With 25 the first block is 100 ms and the stream
is about 66 ms ahead. A value under 25 leaves no margin: 15 gives a stream
that can reach 0 ahead of the device. No desktop run can show a gap that comes
only with the load of the headset, so the default stays 0. Try 25 in the
headset, and read the count of the gaps in the log.

## 11. Tools and tests without a headset

A desktop run has no listener. The capture and the measure script give each
change of the sound a proof in numbers.

**`audio_capture.cpp`.** With `audio_capture=1` each block of the stream goes
to `bwvr-audio.wav` in the game folder, after the mod's voices are in it. The
header is written again each half second, because a test run ends the game by
force.

**`bwvr/tools/audio_measure.py`** measures that file. It needs numpy.

```
python bwvr/tools/audio_measure.py run.wav --log bwvr.log
```

For each part of the run it prints: the level of each ear, the difference
between the ears in dB (ild), the delay between the ears from the cross
correlation under 1.5 kHz (itd), their correlation, the center of the
spectrum, the level of each octave band from 63 Hz to 8 kHz for each ear, and
the largest step between two samples against the level (a click shows as a
large number). `--seg name:start:end` gives a part by hand. `--compare` gives
the difference of two parts, and `--track` gives the level over time. The
parts come from log lines of the form `audio test   : step N <name>,
capture frame F`. The frame count of the capture is the clock: the line of an
event gives its place in the file.

**The test voice and the test bed.**

- `audio_test_voice=1` with `audio_test_path=az,el,m,part/az,el,m/...` plays a
  voice of noise through the mixer, at each place of the list in turn (degrees
  to the right, degrees up, meters of the room, and the part of its range).
  Each step lasts `audio_test_step_ms` of the capture. The place follows the
  head, so the direction is exact.
- `audio_test_voice=2` plays a stereo test bed: low noise in the left channel
  and high noise in the right one.
- `audio_test_solo=2` makes all other sound silent in the stream, so the
  capture has only the test. `1` makes only QMixer silent.
- With `audio_trace=1` each cue of section 8 writes a step line too.

**The play trace.** `audio_trace=1` logs each sample that starts (`audio
play`), each channel that still plays once a second (`audio chan`), and every
10 s a table of the kinds of sound (`audio kinds`). The play line names the
caller by its return address, the bank (from the array `GAudio+0x3A8`, one
bank for each `AUDIO_SFX_BANK_TYPE`), the class of the object (from its RTTI),
and what the DLL sent to QMixer in the call. The channel line comes from
`QSWaveMixGetChannelParams`. The trace hooks on the QMixer exports are
`stdcall`, with stack sizes from the `ret` operands: `0xC` for
`SetListenerPosition` and `0x10` for the other four. They log only. A sound
tag asks 10 times a second, so the trace writes no line when a sample played
already. The table of kinds counts these calls.

**Logs.** The line tag `audio range` has one line for each change of a loop,
or of the sound of a sound tag. Each line gives the place, the distance from
the head, the range in the room, and the distance from the game camera:

```
audio range  : the DLL has 16 sample slots, 10 are free. With audio_loops_keep=1, a loop out of range keeps its slot while 8 or more are free
audio range  : sound in game 52 of SoundTag starts. At 2230 16 3155, 1.20 m from the head (120 units). Range 7.50 m (300 units times 2.500). The game camera is 412 units from it. 100.0 units a meter
audio range  : loop ... is out of range and silent. It keeps its slot (10 of 16 are free). ...
audio range  : loop ... is in range again. ...
audio range  : loop ... stops: out of range, and only 3 of 16 slots are free. ...
audio range  : sound ... is refused: out of range of the head. ...
audio range  : sound ... is refused by a rule of the game (not the distance). ...
audio range  : sound ... is refused: the DLL gave no sample slot (0 of 16 are free). ...
audio range  : loop ... stopped, and not by its range: the game or the DLL stopped it. ...
audio range  : loop ... stops: its object has no position now. ...
audio range  : the head decided N starts: N started that the game camera refuses, ...
```

The last line comes each 30 s, when a number changed. With `audio_range=0`, or
with the test key on step 4, the same lines show what the game camera
decides: "is refused: the game camera is out of range" and "stops: the game
camera is out of range". So a log tells a drop-out by the game from a
drop-out by the range. There are at most 60 lines for one sound and 600 in
all. The loop of an animation starts and ends in a few seconds. So the first
start of a loop gets its line after the loop has played for 5 s, and a loop
that ends before gets no line. Other lines are `audio mixer` and `audio
cues`: they tell what played.

**Rigs.** `bwvr/tools/desk/` has the desktop scripts. The recipe of
`aim_run.ps1` starts a new game on Land 1 and puts the God view at the
Creature's pen after the first story. With `audio_test_tag=52` the Creature
carries a looping tag whose range is known. The rig for a sound without a
headset is `fake_head=1` with `fake_head_yaw`, which turns an invented head
(`HeadPoseSetFake`, `HeadPoseFakeSweep` in `head_pose.cpp`). The logged
listener forward must equal the render basis forward.

A desktop run cannot show: that the sign of the azimuth is right by ear, that
a loop fades to silence without a click, that a sound that stops with
`audio_stop_wait=0` ends as it does with 1, the late sound of a fast head turn
on the real device, the pause of the game and its menu (the fake Esc needs
the game window in front), and the level balance by ear.

## 12. Known limits and open questions

**Limits.**

- The advisors have no height. They are flat by the design of the game, and the
  mod keeps their sound. A later step can try the mixer for them on a test key
  only.
- A delayed sound near the head does not start. A sound tag with a delay
  (`SoundTag::CheckDelay`) and a particle sound with a delay (`0x006D1312`)
  test the game camera in their own code. The fix is to give the two call
  sites of `GCamera::GetDistanceSq` the distance of the ears.
- The slots at a far zoom. The DLL has 16 slots and gives them by the priority
  of the bank, not by what the head hears. The zoom factor takes most small
  sounds out of range at a far zoom. At 4000 units a meter `k` is 100 at
  `audio_zoom_pct=100`, and each sound of the island can be in range. A later
  step can stop the quietest sample when a louder one finds no slot.
- A head turn is as late as the stream of QMixer (section 10). The full answer
  is a stream of the mod's own (WASAPI) with a short buffer.
- Music with a place has no direction. `LHMusicSet3DPosition` sets a volume
  from the distance to the listener, in world units, with no pan and no room
  factor.
- The bed has no direction of its own. It turns with the yaw, and the single
  sounds go toward their land.
- A source very near the head has an unstable direction. The near field keeps
  the angle small.
- A script camera shot changes the game camera and the head camera. The spirit
  position can change with the cut. Recheck after a cut (`SpiritAnchorCameraCut`).
- `SpeakerConfig` sets the QMixer speaker model. A stereo speaker model is not
  the same as headphones. The mod does not change it, because it is a registry
  value.
- QMixer 3D positioning needs mono waves. The advisor and villager waves are
  mono. Some effect banks are not checked. A stereo wave keeps its own image.
- A snap turn or a recenter changes the yaw at once. The controls of the
  mixer ease it (7.4). The pan rate of QMixer (`QSWaveMixSetPanRate`, set at
  init at `0x15B8F` to `0x64`) may lag a fast turn for the sounds that QMixer
  plays.
- A measured HRTF is not in. Put a measured set behind `audio_spatial::Process`,
  with a license that lets the mod ship it.
- Which sound is a "large portal" sound in the God view, and so which loop
  drops out, is not known. The `audio range` lines name each loop.
- The stop of a sound that QMixer plays holds the frame thread for 20 ms
  (7.6). A desktop run of 110 s has 2 such stops. Which sounds they are is
  not known.

**Open questions.**

1. What is the flag word at `bank_sample+0x244` in the `.sad` files? If an
   advisor line has the bit `0x4` or `0x8`, the DLL treats it as 3D, and the
   object callback would read `0x270C` as a pointer. The advisors play
   without a crash, so this is probably not the case.
2. Does `QSWaveMixSetSourcePosition` move a channel that was configured with a
   polar position? The mod uses the polar call, so it does not need the answer.
3. Is the sign of the azimuth right? A positive azimuth must be the right
   ear. Only a person can check it.
4. Does the Hand play through `PlaySoundEffect` with a position?
5. How many QMixer channels does the game open, and what happens when all are
   busy? A tracked voice could be stolen. The tracker checks the key each
   frame, so it is safe, but the advisor voice may get cut.
6. What is the effect of `QSWaveMixSetPanRate(session, channel, 1, 100)` on a
   fast head turn?

## 13. Plan: what is not built

The order is value against cost. `research-hla.md` has the reasons from
Half-Life: Alyx.

| Item | What the player hears | The cause | The plan |
| --- | --- | --- | --- |
| 1. Sounds with the default mapping are faint | An effect with an object and no mapping of its own is quiet a few centimeters from the head | The options default is min 1, max 10000, rolloff 0.3. With room distances the min is `k` units, 2.5 cm at 100 units a meter | Give the min of such a mapping a floor before the room factor (for example 30 units, the Creature's value). First list these sounds with the play trace in a scene with villagers |
| 2. Music with a place has no direction | The chant at a worship site and the dance music get quieter with distance, and come from the middle | `LHMusicSet3DPosition` sets a volume from the distance to the listener | Trace the three music entries first. Then scale the distance by `k`, and give the music channel a pan from the head |
| 3. Some sounds near the player do not start | A villager or a tree near the Hand makes no sound | The start tests that the mod did not change: the delayed sounds | Give those paths the distance of the ears too |
| 4. The advisors have no height | A spirit that floats over the shoulder sounds level | They send elevation 0, as the DLL did | Send the elevation of the spirit, as section 6.5 does for the others, on a test key first |
| 5. A head turn is late | A sound lags a fast turn of the head | The latency of the QMixer buffer | Try `audio_latency_ms=25`. A stream of the mod's own is the full answer |
| 6. Loops and slots | Many loops at a far zoom fill the 16 slots | The DLL gives slots by priority | Stop the quietest sample when a louder one finds no slot |

Not for this list: sounds of the interface and of the scripts with no place.
They are flat by design.

## 14. Headset test of the sound of the world (10 minutes)

Use a game with the Creature. The view starts at the door of the Temple with
the Creature in front. Ctrl Shift F12 (with test keys on) steps the sound
back, one change for each press, and the fifth press is the new sound again.

1. **Height (2 minutes).** Stand still and look ahead. The Creature is on the
   land at your waist. Its steps and its voice must come from below and
   ahead, not from the height of your ears. Listen to a bird: it must be at
   the height of your ears or over them. Press Ctrl Shift F12 two times for
   QMixer's sound, and three times more for the new sound.
2. **Near (1 minute).** Put your head 20 cm from the Creature, or hold a
   thing that makes a sound near one ear. The sound must stay in both ears
   and not sit hard in one.
3. **The bed (1 minute).** Go to the shore. Note in which ear the sea is.
   Turn the view with the stick, 4 steps. The sea must stay where it is in
   the world, so after 4 steps it is in the other ear. Turn your head: the
   same. A single wave must come from the side of the sea.
4. **The Creature (1 minute).** Put the head near the Creature. Its steps must
   be clear within arm's reach and become quiet at 1 to 2 m. Turn the head:
   they stay where the Creature is.
5. **The zoom (2 minutes).** Stand in a village. Zoom out step by step to the
   far end. The voices and the small sounds must go away. Throw a rock at a
   house, or cast a miracle: the crash must carry, low and dull. Zoom in again:
   the small sounds come back.
6. **Distance (1 minute).** Teleport away from a sound that loops (a fire, the
   Temple, a vortex) and back. It must get quiet and dull with the distance,
   and it must be there again when you come back, with no cut.
7. **The cues (1 minute).** A turn step, a zoom step, a teleport, and the left
   grip on the land. Each one has a soft sound. `audio_cues_pct` sets their
   level, and `audio_cues=0` turns them off.
8. **The advisors (1 minute).** They must sound as in the game. So must the
   speech of a story.
9. **All the time:** a click, a sound from a wrong side, a sound that lags a
   fast turn of the head. Say when, and send `bwvr.log`.
10. **If the sound lags a turn of the head:** set `audio_latency_ms=25` and
    play again. The log line "QMixer writes the stream" must then say about
    66 ms, and "passed a write 0 times". A crackle or a count over 0 means
    that 25 is too low for the headset: go back to 0.
