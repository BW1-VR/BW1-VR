# Sound in the room: how the game plays sound, and the design for Track 5

This file is step A1 of Track 5 in `roadmap.md`. It is research and design.
No source file, build, game run or registry value changed while it was
written.

Marks used in this file:

- **V** means verified. The evidence is a disassembly, an import or export
  table, a data file, or the mod source.
- **I** means inferred. The evidence points that way, but nobody ran it.

Addresses in `runblack.exe` are virtual addresses at base `0x00400000`.
Addresses in `LHaudiodllR.dll` and `QMixer.dll` are RVAs, unless a line says
otherwise. The tool for the DLL disassembly was a short capstone script in the
scratchpad. It is not in the repo.

## 1. Summary

1. Every game sound goes through one DLL, `LHaudiodllR.dll`. It is Lionhead's
   audio layer. It drives `QMixer.dll` (QSound QMixer), and QMixer does the 3D
   positioning. (V)
2. The game sets the listener in one place, `GAudio::ProcessAudioGameTurn`
   (`0x00427080`), once per game turn. It uses the game camera, not the head.
   (V)
3. Only two game functions start a sample: `GAudio::PlaySoundEffect`
   (`0x00429E30`) and `HelpDude::PlaySample` (`0x005BB530`). Both call
   `LHSamplePlay`. (V) The DLL also calls it, for the atmosphere and the
   animations (section 11.2).
4. The advisors speak through `HelpDude::PlaySample` with the 3D flag off, so
   they play flat. (V for the code path, I for the exact sound at the ear)
5. The plan: hook `LHListenerUpdate` and call it every frame with the head pose
   (A2). Hook `LHSamplePlay` and steer the advisors' QMixer channel to the
   room position of the spirit (A3). Use the same tracker for any other flat
   sound that has a known source (A4).

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

The tool reported "0 callers" for the thunks. The thunk is a `jmp [IAT]`, and
the game calls through the IAT slot directly. Use `--xref` on the IAT slot.

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

**Which one runs on a modern PC (I).** The 3D positioning is QMixer's own
software algorithm, in both modes. On Windows 10 and 11, DirectSound is a
software layer on WASAPI. It reports an emulated driver, and hardware 3D
acceleration is gone. So QMixer takes the "emulation" branch, mixes in
software, and sends stereo to DirectSound or waveOut. The DirectSound 3D
listener path does not run. Nobody has run the game with a trace to prove it.
The trace tool in section 6 (T1) proves it in one run.

Both DLLs sit in the game folder. There is no `qmixer.dll` in
`C:\Windows\SysWOW64`. So the game folder copy loads. (V)

### Configuration (V for the strings, I for the effect)

`LHaudiodllR.dll` reads `Software\Lionhead Studios Ltd\Audio\Override` and
these value names: `SpeakerConfig` (`STEREO` or `QUAD`), `UseHardware`,
`3DSpatialisation`, `Latency`, `HWRate`, `MaxSamp`, `HeapSize`, `NoAudio`,
`Wave`, `Music`, `Midi`, `Redbook`, `Sfx`, `Atmos`. On this PC the key
`HKCU\Software\Lionhead Studios Ltd\Black & White\Audio\Override` holds only
`NoAudio = FALSE`. Read only. Nothing changed. The defaults apply.

The init code at `0x15820` sets the speaker placement with
`QSWaveMixSetSpeakerPlacement` from `SpeakerConfig`. How many mixer channels
the game opens is open (see section 8).

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

## 3. The listener

### Where the game sets it (V)

`GAudio::ProcessAudioGameTurn` (`0x00427080`) has two callers:
`GGame::EndTurn` (call at `0x0054E9A6`) and `Temple::ProcessGameTurn`
(`0x00794A5A`). It calls the unnamed function `fn_004270D0`, which I call
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
  `UpdateCamera` (it names both). So the listener sees the game camera, with no
  head in it. (V)
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

### How often (I)

`GGame::EndTurn` runs once per game turn. `ProcessAudioGameTurn` is called only
when a flag test passes (`[GGame+0x205A40] > 5` and bit 2 of `[GGame+0x14]`
clear). So the listener moves at the game turn rate, not the frame rate. The
turn rate is not measured here. The trace in section 6 measures it.

The same function calls `LHSampleUpdate3DChannels` (`0x004271D7`) first. That
call moves every positional sample once per turn (section 4).

### What else reads the listener or the camera (V)

- The DLL object at `this+0x58` (music distance volume). It is refreshed only by
  `LHListenerUpdate`.
- The 3D object callback `fn_00427200` (section 4) reads the game camera
  globals `0x00EA1DB8` to work out the distance of a source.
- `GAudio::PlaySoundEffect` (`0x00429E30`) tests the distance of a 3D sound to
  the game camera before it plays it. It uses `GCamera::GetDistanceSq`
  (`0x00442D10`) or the globals when `[GGame+0x205A28] == 1`. It drops a sound
  that is beyond the maximum distance of its sample.
- These three keep the game camera on purpose. They decide what plays and what
  stops. They are game logic. Leave them alone.

## 4. Positional sources

### The one hook point that sees every sample (V)

Only two places in the exe start a sample:

| Caller | Address | What |
| --- | --- | --- |
| `GAudio::PlaySoundEffect(LH_SamplePlayOptions*)` | call `0x00429FDF`, return `0x00429FE5` | Everything else: 86 call sites reach this function, among them creatures, abodes, the Hand, script sound, sound tags and the interface. |
| `HelpDude::PlaySample` | call `0x005BB544`, return `0x005BB54A` | The advisors' speech. One caller: `HelpDude::UpdateSaySentence`. |

The first one drops the returned sample info (`xor eax, eax` at `0x00429FE5`).
The second keeps it (`0x005BB54A`).

`LHSamplePlay` (`?LHSamplePlay@LH_AudioSystem@@QAEPAULH_SampleInfo@@PAVLH_SamplePlayOptions@@@Z`,
RVA `0x113B0`, `thiscall`, `ret 4`) is one function that both reach. It returns
the sample info. It is the best hook point for A3 and A4.

### The options object (V from the code, I for names)

The game fills a `LH_SamplePlayOptions` (size `0x168`, constructor at RVA
`0x10E90`) and hands it to `LHSamplePlay`. The fields that matter:

| Offset | Meaning | Evidence |
| --- | --- | --- |
| `+0x04` | the bank | `GAudio::PlaySoundEffect` at `0x0042A064`, `HelpDude` at `0x005BB3A8` |
| `+0x08` | the 3D flag. 1 means positional | `0x00429E64` tests it, `HelpDude::Say` clears it at `0x005BB3D0` |
| `+0x0C` | second flag, default 1 | constructor, `0x00429FC6` |
| `+0x20` | the "object": a `Base*` for GAudio sounds, the number `0x270C` for the advisors | `0x0042A0FB` stores arg 0, `0x005BB3C6` |
| `+0x24` | the sample number in the bank | `0x0042A05A`, `0x005BB3C0` |
| `+0x2C` | the pan, default `0x5A` (90). The flat path maps 0 to 180 onto an angle | constructor, DLL `0x12640` |
| `+0x30` to `+0x38` | the position | `0x0042A08F` to `0x0042A0A1`, `0x005BB3AC` to `0x005BB3E0` |
| `+0x3C` to `+0x44` | an offset added to the position | `0x00429E6F` |
| `+0x54`, `+0x58`, `+0x5C` | defaults `1.0`, `10000.0`, `0.3`. Likely minimum distance, maximum distance and a scale | constructor, `0x10EF1` to `0x10EFF` |

The DLL copies the options and may replace some fields from the bank sample
flags (bits `0x4`, `0x8`, `0x10`, `0x20` of the flag word at `bank_sample+0x244`,
code `0x1193B` to `0x11A54`). If bit `0x4` or `0x8` is set, the sample is
treated as 3D whatever the game asked. What the flags are in the `.sad` files is
open (section 8).

### How a 3D sample is placed (V)

In `LHSamplePlay` when the 3D flag is 1 (RVA `0x1205B` to `0x121F2`):

1. `QSWaveMixConfigureChannel` and `QSWaveMixEnableChannel` set 3D flags.
2. `QSWaveMixSetDistanceMapping(session, channel, 0x20, {size 0x10, min, max, scale})`.
3. `QSWaveMixSetSourcePosition(session, channel, 0, {x, y, z})` with the
   **world** position plus the offsets.
4. `QSWaveMixSetVolume`.

QMixer then works out the direction and level from its own listener. The DLL
does no listener math for samples. (V for the calls, I for the QMixer side)

When the flag is 0 (RVA `0x125AB` to `0x1272A`), the DLL calls
`QSWaveMixSetPolarPosition` with an angle from the pan (the constants `0xB4`
and `0x5A` at `0x12640` and `0x12655`) and a distance. A polar position is
listener relative. So a flat sound stays in the same place in the head, wherever
the head turns. (V for the call, I for the QMixer meaning)

### Moving sources: the object callback (V)

At startup, `fn_00426D40` (the GAudio constructor, call at `0x00426E75`)
registers a callback with `LHSampleRegister3DObjectFunction(fn_00427200,
800.0f)`. The value `0x44480000` is 800.0. `LHSampleUpdate3DChannels` (RVA
`0x14310`) runs once per turn. For every playing 3D sample it calls the
callback, and it stops a sample when the callback returns 0 or the distance is
past `si+0x6C`. Then it calls `LHSampleSet3DPosition(si, x, y, z, 0)`. With
flag 0 that ends in `QSWaveMixSetSourcePosition` (RVA `0x13EA5`). With flag 1 it
computes an angle and calls `QSWaveMixSetPolarPosition` (RVA `0x13E6A`).

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
| Villagers, animals, buildings, the Creature, particle systems, tree and rock effects | `GAudio::PlaySoundEffect` with an object and the 3D flag from the caller | Yes when the caller sets the flag. The object callback moves it. (V for the mechanism, I for which sound sets it) |
| The Hand | `PlaySoundEffect` from the Hand code | Not checked. (Open) |
| Ambience (atmos banks) | `LHAtmos*` calls, `GAudio::ProcessAtmosBanks` (`0x00428FE0`) | Not through `LHSamplePlay`. Not checked. (Open) |
| Chant music, creature dance music | `LHMusicSet3DPosition` from `0x00427959`, `0x0042814C`, `0x00429658` | Distance volume only. (I) |
| Interface clicks and script "say" sounds | `PlaySoundEffect` with a null object or the flag off | Flat. (I) |
| **The advisors' speech** | `HelpDude::PlaySample` | **Flat.** See below. |

### How the advisors speak today (V)

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
  If it is changed to an object pointer, the advisors can no longer be stopped
  and the game's talk logic breaks.
- The waves in `HelpSprites.sad` (1923 samples) and `villagers.sad` (1328) are
  all mono, 22050 Hz, format tag `0x50`. `Guidance.sad` is mono, 22050 Hz too
  (114 samples with tag `0x50`, 113 with tag 1). Mono is the input QMixer needs
  for 3D. (V, read from the files)
- `HelpDude::Get3DSoundPos` (`0x005C20E0`) already returns `HelpDude+0x3374` and
  the value 1. So the game had a positional design for the spirits and turned it
  off with the flag. (V)

If the 3D flag were turned on today, the object callback would read `si+0x18 =
0x270C` as a pointer and crash inside `RTDynamicCast`. Do not turn the flag on
for the advisors. (V for the code, I for the crash)

## 5. Design

### Shared rules

- All calls come from the game main thread, from a hook that the mod already
  owns. That is the same thread that calls the audio in the game. Do not call
  QMixer from the OpenXR thread or from the timer callback.
- No audio motion that the player did not make. The listener follows the head.
  The sources stay where they are in the room.
- Every step has a switch in `bwvr.ini` and a log line, and it is off if the
  head camera is off or the world is not up.
- No registry change and no new file in the game folder.

### A2: the listener follows the head

**What to hook.** `LHListenerUpdate` in `LHaudiodllR.dll`. Two ways, pick one:

1. MinHook the exe thunk at `0x007C5166` (6 bytes, `FF 25` to the IAT). One
   caller only.
2. Get the export with `GetModuleHandle` and `GetProcAddress` and MinHook the
   DLL function (RVA `0x3850`). The prologue is `sub esp,0x38 / push esi / mov esi,ecx`,
   six bytes, no relative branch. Catches every caller.

Prefer 2. The mod would then own one pointer to the original.

**Convention.** `thiscall`, three pointer arguments, callee cleans 12 bytes:

```
void __fastcall h_ListenerUpdate(void* self, void* edx,
                                 const float* pos, const float* look_at,
                                 const float* up);
```

**What the hook does.** It ignores the three arguments when the head is
available and sends the head instead:

- `HeadCameraRenderBasis(basis, position)` from `head_camera.cpp` gives the
  rendered camera: the position and the columns right, up and forward, in game
  world coordinates. This is the head with no eye offset. It is set once a
  frame, at the first main camera call. (V for the function, I for "no eye
  offset": `HeadPoseUpdate` says "with no eye offset", and the log will show
  it)
- `pos` is the position. `look_at` is `pos + forward * 10`. `up` is the up
  column, with head roll. Ears roll with the head.
- No conversion of axes. The game passes world coordinates to QMixer today, so
  QMixer's convention already matches the world. The head basis is in the same
  world. (I. The trace shows it: a source on the right must give a right pan.)
- If there is no render camera yet, the head camera is off, or the world is not
  up, call the original with the game's arguments.

**A per frame driver.** The game calls the update once per turn. A head turns
at frame rate. So the mod also calls the original `LHListenerUpdate` every
rendered frame with the same values, from a place that already runs once a
frame on the game thread: the `LH3DRender::FinishFrame` hook at `0x0082F460` is
the natural one, or the end of the main `h_UpdateCamera` call. The audio system
pointer is `*(void**)( *(uintptr_t*)0x00CD3B20 + 0x14 )`. (V: `mov ecx,[0xCD3B20]`
then `mov ecx,[esi+0x14]` at `0x004270BB`.) New file: `audio_listener.cpp`.

Between frames QMixer holds the world positions of the sources. It recomputes
the direction against the newest listener at each mix block. So one update per
frame is enough for a head turn. (I)

**Test with no headset.**

- T1, the trace: log `LHListenerUpdate` arguments, and hook the QMixer exports
  `QSWaveMixSetListenerPosition`, `SetListenerOrientation`, `SetSourcePosition`,
  `SetPolarPosition` and `SetDistanceMapping` for logging only. Once a second,
  print the call rates and the last values. This measures the real call rate,
  and it shows the game camera against the head camera.
- T2, the fake head: `HeadPoseSetFake` and `HeadPoseFakeSweep` in
  `head_pose.cpp` already turn an invented head. Run with a fake yaw of 90
  degrees. The logged listener forward must turn by 90 degrees from the game
  forward, and it must equal the render basis forward to within rounding.
- T3, a source in the room: with the fake head at yaw 0, play an advisor line
  and log the source position and the listener. Then sweep the yaw. The log
  computes the angle of the source in the listener frame (dot with right and
  forward). It must change with the sweep. This needs no ears.
- An ear check needs a person, one time. Play a line, turn the fake head to
  90 degrees left and to 90 degrees right, and hear the sound move from one ear
  to the other. Give this as a hotkey A and B, so the author hears it live.

### A3: the advisors' voices come from where they float

**What to hook.** `LHSamplePlay` (DLL export, RVA `0x113B0`, `thiscall`,
`ret 4`, returns `LH_SampleInfo*`). It sees both callers. The advisors are the
ones whose return address is `0x005BB54A`. Use `_ReturnAddress()` in the hook.
The options pointer then is `HelpDude + 0x367C`, so `HelpDude = options - 0x367C`.

```
LH_SampleInfo* __fastcall h_SamplePlay(void* self, void* edx, void* options);
```

**Do not use the game's 3D flag** (section 4). Steer the channel that the DLL
opens for the flat sample:

1. Call the original. It returns `si`, or null if the sample did not play.
2. The QMixer channel is `*(DWORD*)(si + 0x2C)`. The session is
   `*(HANDLE*)(self + 0x88)`. (V: `0x1213F` to `0x12159` and `0x13E79` to
   `0x13EA5` use exactly these two fields.)
3. Register the pair `{si, key 0x270C, HelpDude*}` in a small list of tracked
   samples.
4. Every frame, for each tracked sample, read the spirit position
   `HelpDude + 0x3374` (three floats) and call
   `QSWaveMixSetSourcePosition(session, channel, 0, &pos)`. Set the distance
   mapping once, in step 3, with a large minimum distance so the level does not
   change (see below).
5. Drop the entry when `LHSampleIsPlaying(si)` is false, or when `si+0x18` no
   longer equals `0x270C`, because the DLL reuses the info slots. The DLL exports
   the `LH_SampleInfo*` form of `LHSampleIsPlaying`.

The QMixer functions are exports of `QMixer.dll`. Get them with `GetProcAddress`
on the loaded module. They are `stdcall`.

**Where the spirit is.** `HelpDude+0x3374` is the translation of the spirit's
world matrix. `spirit_anchor.cpp` already keeps that position fixed in the
room: it places each spirit through the room camera, so the position does not
follow the head. The head moves in the same world through the head pose. So the
listener and the source agree, and the spirit is where the player sees it. (V for
the offset, I for the agreement. T3 checks it.)

**The level.** Today the advisors play flat, with no distance loss. In 3D, QMixer
applies a distance model. The defaults in the options object (`1.0`, `10000.0`,
`0.3`) could make the voice quieter at a few world units. The head scale is
`head_units_per_meter_x10 = 75`, so 1 meter is 7.5 units. Set the mapping with
a minimum distance larger than the spirit distance, so the gain stays at full
level:

```
QSWaveMixSetDistanceMapping(session, channel, 0x20, {0x10, min = 1000.0, max = 10000.0, scale = 1.0})
```

The struct layout (size, min, max, scale, four 4 byte fields) is what the DLL
builds at `0x12113` to `0x12159`. The units and the exact model of `scale` are
open. T1 logs what the DLL passes for a normal 3D sound. Copy its numbers.
(I)

**Why not a pan only.** A pan from the head direction would work, but it needs
a per frame math step and it cannot show height or front and back. The world
position gives QMixer the whole job. (I)

**Test with no headset.** T1, T2 and T3 above. Add one more: a log of the
distance and the angle of each advisor line, so the level choice has data.

### A4: sounds the game plays flat when the source is known

The same tracker handles them. In `h_SamplePlay`, when the return address is
`0x00429FE5` (a `GAudio::PlaySoundEffect` sound) and `options+8 == 0`:

1. If `options+0x20` is a readable pointer to an object whose vtable is inside
   the exe `.rdata`, call its `Get3DSoundPos` (vtable offset `0x10`, `thiscall`,
   one argument, returns 1 when it has a position, `ret 4`). Treat a return of 0
   as "no source".
2. Else if `options+0x30` to `+0x38` are not all zero, use that point plus the
   offset at `+0x3C` to `+0x44`.
3. Else leave the sound flat.

If a position is found, track it as in A3. For a moving object, read the
position again each frame. Do not read it once. Note that the object can be
deleted while the sample plays. Guard the read (`IsBadReadPtr` on the object and
its vtable, and drop the entry on any doubt).

Sounds with the 3D flag on are already moved by the game once per turn. Leave
them alone. The only change for them is A2: QMixer places them against the head.
That gives head tracking to every positional sound, which is the main gain.

The level for A4 sounds needs care. Take the distance model the game chose
(`options+0x54` to `+0x5C`). Do not force full level. Only steer the direction.
If QMixer does not accept a source position on a channel that started in polar
mode, keep the polar call and compute the angle each frame with
`QSWaveMixSetPolarPosition` (open, see section 8).

**Music and ambience.** Chant and dance music have distance volume only. Atmos
banks are outside `LHSamplePlay`. They are out of scope for A4. They remain
unspatialized.

### Suggested order

1. **T1 first** (`audio_trace.cpp`, log only, no behavior change). It answers the
   open items: the turn rate of the listener update, the polar or world choice
   for the advisors, the distance numbers, and the real backend. It also
   proves the hook points work.
2. **A2**, with the per frame driver. Check with T2.
3. **A3**, advisors only. Check with T3, then by ear.
4. **A4**, after A3 works.

## 6. Tests without a headset (summary)

| Test | What it shows | Tool |
| --- | --- | --- |
| T1 trace | listener and source calls per second, the last values, the distance mapping the DLL uses | log only hooks on `LHListenerUpdate` and the QMixer exports |
| T2 fake head turn | the listener forward turns with the invented head and equals the render basis | `HeadPoseSetFake`, `HeadPoseFakeSweep` |
| T3 source angle | the angle of an advisor in the listener frame changes when the head turns | the same log, computed from the source position and the listener |
| T4 ear check | left and right, by a person | a hotkey pair that gives the same line with the head at 90 degrees left and 90 degrees right |

## 7. Risks

- **Threads.** The mixer runs on a `winmm` timer thread and on QMixer's own
  thread. Call the QMixer functions only from the game thread, as the DLL does.
  Keep the tracker list touched only on that thread. The QMixer API takes its
  own lock. (I)
- **Proxy DLL or MinHook.** A proxy `QMixer.dll` would give a full view of
  every call, but the game folder holds Lionhead's file and the installer would
  have to replace it. It is not needed: the two hook points are the DLL exports,
  and MinHook covers them. The QMixer functions are called by pointer, and the
  trace hooks are optional. Use MinHook, and no proxy. If a hook on `LHaudiodllR.dll`
  is needed before the mod loads, the mod is a `ddraw.dll` proxy, which loads
  before the audio init. (I)
- **Load order.** `GetModuleHandle("LHaudiodllR.dll")` must find the module when
  the hook is made. The game imports it statically, so it is loaded before
  `ddraw.dll` is called by the game. Check in the log, and retry on a later frame
  if the module is missing. (I)
- **Game camera in three places** (section 3). The game still culls and stops
  sounds by the game camera. With the head far from the game camera (a large
  lean, or tabletop mode with a large world scale) the culling distance and the
  QMixer distance differ. A sound could stop while it is still near the head.
  Tabletop mode changes the scale, so the listener scale must follow it. (I)
- **The listener up vector of the game** is the column of a matrix the mod
  rewrites. The hook must not trust it. It uses the render basis. (I)
- **Sound that jumps when the head turns fast.**
  - QMixer may not smooth the pan. The DLL sets `QSWaveMixSetPanRate` at init
    (`0x15B8F`, value `0x64`). It may slew a pan and lag a fast head turn. Or it
    may do nothing for world positions. T4 finds out.
  - A snap turn or a recenter changes the yaw at once. That can click. Rate limit
    the listener yaw change, or fade the tracked voices for one mix block.
  - The mix runs ahead by the QMixer latency (a buffer of tens of milliseconds
    or more, from the `Latency` value). A head turn is heard that much later. Do
    not lower it without a test. The setting is a registry value, and this work
    does not change the registry.
  - The listener velocity is set to 0 at init (`0x15C1A`). Keep it 0. A velocity
    from the head would add a Doppler shift.
- **A source at the listener.** If a spirit is very near the head, the direction
  is unstable. Keep the tracked position at least a small distance away from the
  listener.
- **A source in a script camera shot.** `script_camera.cpp` holds a shot. The
  head camera and the game camera differ. The spirit position may change with
  the cut. Recheck after a cut (`SpiritAnchorCameraCut`).
- **Speaker placement.** `SpeakerConfig` sets the QMixer speaker model. A
  stereo speaker model is not the same as headphones. A headset uses headphones.
  If it sounds narrow, the value is the first suspect. Do not change it without
  the author.
- **Mono only.** QMixer 3D positioning needs mono waves. Every advisor and
  villager wave is mono. Some effect banks were not checked. A stereo wave
  would keep its own image. (I)

## 8. Open questions

1. Which output runs on this PC: DirectSound or waveOut? T1 can log
   `waveOutOpen` and `CoCreateInstance` in QMixer, or read the "QMixer:" strings
   if they go to the log.
2. What is the flag word at `bank_sample+0x244` in the `.sad` files? Bits `0x4`
   and `0x8` make a sample 3D regardless of the game flag. If an advisor line has
   one of them, the DLL already treats it as 3D, and the object callback would
   read `0x270C` as a pointer. That did not crash so far (the author hears
   the advisors), so this is probably not the case, but it is not proven.
3. Does `QSWaveMixSetSourcePosition` move a channel that was configured with a
   polar position? The DLL calls both, on different channels. If it does not, use
   `SetPolarPosition` with an angle computed each frame.
4. Is a polar position listener relative? (I assumed yes.) T1 plus a fake head
   turn shows it: a polar source must not move when the listener turns.
5. What is the turn rate of `GGame::EndTurn`?
6. What do `min`, `max` and `scale` in the distance mapping mean in world units,
   and which options fields feed them? The DLL reads them from a local copy of
   the options. T1 logs the values for known sounds.
7. What is `QSWaveMixSetPanRate(session, channel, 1, 100)`? Its effect on a fast
   head turn is unknown.
8. Does the Hand play through `PlaySoundEffect` with a position? Do the atmos
   banks use QMixer 3D at all?
9. How many QMixer channels does the game open, and what happens when all are
   busy? A tracked voice could be stolen. The tracker checks the key each frame,
   so it is safe, but the advisor voice may get cut.
10. The distance from the head to a spirit in world units. It follows from
    `kSpiritNear` and `kSpiritFar` (`HelpDude+0x35B8`, `+0x35BC`), which come
    from a data file. T1 logs it.

## 9. Files that A2 to A4 would add or change

No file is changed by this step. For the next steps:

- New `src/mod/audio_listener.cpp`: the `LHListenerUpdate` hook and the frame
  driver (A2).
- New `src/mod/audio_sources.cpp`: the `LHSamplePlay` hook, the tracker, and
  the QMixer calls (A3, A4).
- New `src/mod/audio_trace.cpp`: T1, log only.
- `head_camera.cpp` already gives `HeadCameraRenderBasis`. Nothing to add.
- `spirit_anchor.cpp` needs no change. It may export a getter for the spirit
  position if `HelpDude+0x3374` should not be read in a second place.
- `bw_addresses.h`: add `0x00CD3B20` (GAudio), `0x00427080`, `0x005BB530`,
  `0x005BB54A`, `0x00429FE5`, `0x005C20E0` and the field offsets in section 4.
- `bwvr.ini`: `audio_listener=1`, `audio_advisors=1`, `audio_trace=0`.

## 10. What was built (T1, A2, A3)

Files: `src/mod/audio.h`, `audio_listener.cpp` (A2 and the frame driver),
`audio_sources.cpp` (A3), `audio_trace.cpp` (T1). Small edits in `mod_main.cpp`
(ini keys, install, one `AudioFrame()` call in the `FinishFrame` hook) and
`stereo.cpp` (the test key).

Ini keys: `audio_head` (default 1), `audio_advisors` (default 1),
`audio_trace` (default 0, not in the launcher). With `audio_head=0` and
`audio_advisors=0` the mod hooks nothing. Test key with `test_keys=1`:
Ctrl Shift F12 moves the ears between the head and the game camera (the old
sound, exactly) and logs the mode.

Hooks, all MinHook on DLL exports, all on the game thread:

| Hook | Convention | Notes |
| --- | --- | --- |
| `LHListenerUpdate` | `__fastcall(self, edx, pos, look_at, up)`, callee cleans 12 bytes | Sends the rendered head when it is fresh. Otherwise it passes the game arguments on. |
| `LHSamplePlay` | `__fastcall(self, edx, options)` | Picks out the advisors by return address `0x005BB54A`. |
| QMixer exports (trace only) | `stdcall`, 3 or 4 arguments | Stack sizes from the `ret` operands: `0xC` for `SetListenerPosition`, `0x10` for the other four. All return 0 on success. |

Rules kept:

- The frame driver runs from the `FinishFrame` hook. `FinishFrame` runs once
  for each eye pass, so the driver latches on `HeadCameraMainSerial()`, which
  goes up once per rendered frame.
- "Fresh" means the head camera is on and the main camera serial changed in the
  last 300 ms. Menus and loading screens make no main call, so the ears go back
  to the game camera there.
- The audio system pointer is `*(*(0x00CD3B20) + 0x14)`.

### A3 differs from the design (deliberate)

The design said to call `QSWaveMixSetSourcePosition` on the advisor channel.
The DLL configures a channel for 3D with `QSWaveMixConfigureChannel` (flags
`0x2020000` or `0x2010000`) and `QSWaveMixEnableChannel` before it sets a world
position. A flat channel gets other flags (`0x1020000` or `0x1010000`) and no
3D mode. No run could show whether QMixer moves a flat channel with a world
position, and the level model would change too. So A3 uses the call that the
DLL already uses on a flat channel, `QSWaveMixSetPolarPosition(session, channel,
0x20, {azimuth, 1.0, 0})`, and it sends the azimuth of the spirit in the frame
of the head every frame. The range stays 1.0, so the level does not change, and
no channel changes mode. `SetPolarPosition` and `SetSourcePosition` both queue
a command for the mixer (`QMixer.dll` `0x1520`), so they end in the same place.
The azimuth is `atan2(right . d, forward . d)` in degrees, 0 to 360, with `d`
the spirit position minus the head position. That is the DLL convention
(pan 90 is angle 0, and the angle grows toward the right).

Not known without ears: that a positive azimuth is the right ear, and that
QMixer does not smooth the pan too slowly (open questions 4 and 7). The test key
gives the A and B check. If the sides are swapped, change the sign in
`Azimuth` in `audio_sources.cpp`.

### What the desktop runs showed

Rig: `fake_head=1`, `fake_head_yaw=90`, `audio_trace=1`, `-NEWGAME`. Three runs
(the first used an old build and showed nothing). The game stops at the start
dialog, so the game turns and the advisors did not run.

- **V** Both DLL exports hook and the five QMixer trace hooks install:
  `audio        : LHListenerUpdate at 03123850 ok`, `audio adv    :
  LHSamplePlay at 031313B0 ok`. The RVAs and prologues are right.
- **V (A2, T2)** The frame driver reaches QMixer 60 times a second, the frame
  rate of the rig: `audio trace  : 1000 ms, calls: listener position 60,
  orientation 60`. QMixer gets `listener at 1486.32 26.70 1973.67, front -0.924
  0.000 -0.383, up 0.000 1.000 0.000`. The head camera log says `looking -0.924
  0.000 -0.383, was -0.373 -0.223 0.901`. The two forwards are 90 degrees apart
  (dot product 0.0), and the QMixer forward equals the render basis forward. The
  head basis needs no axis conversion to reach QMixer.
- **V** `GAudio` at `0x00CD3B20` is a pointer: `0x00794A54` loads it with
  `mov ecx,[0xCD3B20]` and calls `0x00427080`. The audio system is
  `*(GAudio+0x14)`. In the dialog stage the global gave no system, so hooks
  now also keep the `this` pointer that they receive (`AudioNoteSystem`), and
  the driver used that.
- **V** The trace saw one polar call before the world was up:
  `last polar ch 0 flags 0x20 azimuth 0.0 range 1.00 third 0.00`. That matches
  the DLL code (flags `0x20`, pan 90 gives azimuth 0, range 1.0). A3 sends
  the same shape.
- **Not seen** The game turn rate (`game calls` stayed 0 with the game paused at
  the dialog), the distance mapping of a normal 3D sound, any advisor line
  (T3), and any `SetSourcePosition` call. Open questions 3, 5, 6 and 10 stay
  open. They need a run past the dialog, or the author in the headset with
  `audio_trace=1`.

## 11. Sound in the God view: what was wrong, and A4 and A5 (2026-10-02)

Buck, headset test of 2026-10-02 (roadmap item 9): "The advisors sound pretty
awesome in stereo now, but most of other sounds don't sound like they are
coming from within the world."

### 11.1 The play trace

`audio_trace=1` now also logs each sample that starts (`audio play`), each
channel that still plays once a second (`audio chan`), and every 10 s a table
of the kinds of sound (`audio kinds`). The channel line comes from
`QSWaveMixGetChannelParams` (QMixer RVA `0x1950`). It copies `0xDC` bytes of
the channel parameters, and these fields were read (V, from the code at
`0xAA70` and `0xAD4C`):

| Offset | Field |
| --- | --- |
| `+0x04` | the channel type: `0x02010000` positional, `0x01010000` flat |
| `+0x10` | the source position in the world |
| `+0x40`, `+0x44`, `+0x48` | min, max and rolloff of the distance mapping |
| `+0xA8`, `+0xAC`, `+0xB0` | range, azimuth and elevation from the listener |
| `+0xB8` | the distance gain |
| `+0xBC` | the total gain |

The play line names the caller by its return address, the bank (from the
array `GAudio+0x3A8`, one bank for each `AUDIO_SFX_BANK_TYPE`), the class of
the object (from its RTTI), and what the DLL sent to QMixer in the call.

### 11.2 More callers of LHSamplePlay (V)

Section 1 and section 4 name two callers. There are four more, inside the DLL:

| Return address (DLL RVA) | Function | What |
| --- | --- | --- |
| `0x1A87`, `0x1E41` | `LHAtmosProcess` | the sounds of the atmosphere |
| `0x1490C`, `0x14BC3` | the two `LHSamplePlayAnimEffect` | the sounds of animations, for example the Creature's steps |

The game imports both `LHSamplePlayAnimEffect` forms (IAT `0x008A96D0` and
`0x008A96D8`) and calls them from `GAudio::SamplePlayAnimEffect` (`0x0042A5D6`
and `0x0042A4E1`). The MinHook hook patches the DLL function itself, so it sees
all six callers.

### 11.3 The distance model of QMixer (V)

QMixer computes the distance gain in `0xAD4C`, with the rolloff function at
`0x2CE50`:

```
d > max            gain 0 (silent)
d <= min           gain 1
rolloff 1          gain min / d
else               gain min / (min + rolloff * (d - min))
```

`audio_math::QMixerDistanceGain` is this model. In run au2 QMixer reported a
distance gain of 0.198 for the Creature at 61 units with the mapping 30, 80,
4. The model gives 0.197 at 60.6 units.

### 11.4 What the trace showed

Runs au2 to au4: a new game on Land 1, the God view at 100 units per meter
after the Creature select story, with the head over the Creature's pen.

| Sound | Path | How it played | What was wrong |
| --- | --- | --- | --- |
| The Creature's steps and body sounds | anim effect, 3D | positional, map 30 80 4 (also 15 80 4 and 30 160 4) | 0.6 m from the head gave a distance gain of 0.2, and past 0.8 m nothing |
| Game effects with the 3D flag | `PlaySoundEffect` | positional, maps such as 100 300 4 and the default 1 10000 0.3 | the same: world distances, made for the game camera |
| Game effects with no object (`options+0x20` is 0) | `PlaySoundEffect`, 3D | positional, at the game camera (`0x004272F9`) | in the God view the game camera is up to a few meters behind and over the head, so these sounds came from there, and quiet |
| The atmosphere, single sounds (birds, waves) | `LHAtmosProcess`, 3D with `options+0x14` | a random direction relative to the ears, map 1 9999 4, range 1.4 to 5 | they turned with the head. Half came from straight over or under the ears |
| The atmosphere, the bed | `LHAtmosProcess`, flat | one stereo wave in each atmosphere bank | flat by design: a stereo bed |
| Interface and script effects | `PlaySoundEffect`, flat | object 0, or a small number that is not an object | flat by design |
| The advisors | `HelpDude::PlaySample` | A3 | nothing |
| Music | `LHMusic*` | not through `LHSamplePlay` | not touched |

In the runs no flat game effect had an object with a position. The scene had
no villagers, buildings, fire or miracles, and the Hand made no sound. Their
mappings are not measured. They use the same paths, so the same rules apply to
them (11.5 and 11.6).

The game turn runs at 10 Hz: `game calls 10` a second in the listener line.
That closes open question 5.

### 11.5 A5: room distances (built)

`audio_rolloff.cpp` hooks `QSWaveMixSetDistanceMapping`. While the table or
the God view owns the view and the ears are the head, it sends min and max
times

```
k = s / audio_units_per_meter      kept between 0.05 and 100
```

with `s` the scale of the view in units per meter. QMixer's gain depends only
on `d / min` and `d / max`, so this is the same as all distances divided by
`k`. One meter in the room sounds as `audio_units_per_meter` world units of
the game's design, at every zoom. The default is 40. With 40 the Creature's
steps (min 30) have full level within 0.75 m, the reach of an arm, and are
silent past 2 m (max 80). A sound with the default mapping (min 1, max 10000)
is quiet past a few centimeters, as it was in the game.

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
  QMixer unchanged, and the mod does not touch the channel until a sample
  starts on it.
- With the ears on the game camera (Ctrl Shift F12, or no head camera frame),
  `k` is 1 and every channel gets the game's mapping back.
- Far sounds: QMixer still mutes a sound past `max * k`, and the game still
  stops a sound past its maximum distance from the game camera
  (`GAudio::PlaySoundEffect` at `0x00429EFC`, and the object callback). So a
  sound across the island stays silent at every zoom.

Run au3 showed the factor at each zoom step: `distances now divided by 2.500`
at 100 units per meter, then 3.750, 5.625, 8.438 and 12.656 at 506 units per
meter, and back down. The Creature 0.6 m from the head got the mapping 75 200
4 in QMixer and a distance gain of 1.000 (0.198 before).

**The sounds at the listener.** A positional sample with no object is at the
game camera: the object callback gives `0x00EA1DB8` for it each turn. The hook
on `QSWaveMixSetSourcePosition` (now in `audio_rolloff.cpp`, and the trace
gets a note) keeps its offset from the game camera and sends the ears plus
that offset. The frame driver moves it with the ears. With the ears on the
game camera it goes back to the DLL's point. A sound that starts at a point
of its own stays there until the game moves it to its camera at the next game
turn. Sample 52 of the in game bank is an example: it starts at the Hand when
the Hand comes over an object (`0x005E621E`).

Sample 43 of the in game bank starts at each zoom step (`0x0045FC64`), with
no object and the default mapping. In run au3 it came from the game camera, 1
to 2 m behind and over the head, at a distance gain of 0.04 to 0.29. In runs
au4 and au5 it was at the ears, at a distance gain of 1.000. In run au5
sample 52 stayed at the Hand, 0.83 m from the head, at a distance gain of
1.000.

### 11.6 A4: flat sounds with a place (built)

`audio_sources.cpp` now steers more than the advisors. Each tracked sound has
a place. Every frame it gets the azimuth and the elevation of that place in
the frame of the head, with the polar call that the DLL uses. The range stays
what the DLL sent, so the level does not change. The advisors keep the
elevation 0 of A3.

- **A flat game effect with an object.** If `Get3DSoundPos` (vtable offset
  `0x10`) gives a position when the sound starts, that point is its place. The
  point is read once. A flat sound has no object callback, so the game does
  not promise that the object lives as long as the sound.
- **A sound of the atmosphere relative to the listener.** It gets a point in
  the world when it starts, 3 m from the ears in the room, in the direction
  that the DLL picked. `LHAtmosProcess` (DLL RVA `0x1C2A`) gives two random
  values at `+0x30` and `+0x34` and 0 at `+0x38`, and the relative path reads
  them as right and up. Here the second value is forward, so the birds are on
  a ring around the player at the height of the ears, not under the land. This
  applies only while the table or the God view owns the view. In a story the
  camera flies, and the sound stays as the game made it.
- **The DLL's own polar calls.** When the DLL sends a new polar position to a
  steered channel outside a play, the hook puts the direction of the place in
  the call and keeps the range.
- A new sample in the same info slot or on the same channel ends the old
  entry. The entry also ends when the info key or the channel changes, or when
  the sample stops.

Run au5 measured the cost: all the sound work of a frame on the game thread
(the listener, the room factor, the sounds at the listener and the steered
sounds) took 4 to 8 microseconds on average and 104 microseconds at most. No
QMixer call waited for the mixer.

Run au3: 90 sounds of the atmosphere got a place in about 150 s, with about 70
polar calls a second. The DLL sent no polar calls to them. Run au4, with the
level ring: 78 of 83 places had an elevation of 0 degrees. The other 5 had
less than 6 degrees, from the change of the head height with the zoom.

### 11.7 The ears

In the God view and on the table the ears are now the head, while the room
distances are on. The rule of `TabletopEars` (the ears at most 400 units over
the land) was there because the game's distances made the sounds silent that
far from the land. The room factor fixes that, and only the head gives the
right direction to a sound below the player. With `audio_room=0` the old rule
applies. See `god-view.md`, section 9.

### 11.8 Ini keys and the test key

| Key | Default | What |
| --- | --- | --- |
| `audio_room` | 1 | Room distances and the sounds at the listener (A5) |
| `audio_units_per_meter` | 40 | The world units that one meter of the room sounds as |
| `audio_flat_places` | 1 | Flat effects and the atmosphere get a place (A4) |

None of them is in the launcher. Ctrl Shift F12 (with `test_keys=1`) still
moves the ears between the head and the game camera. Since section 12 it has
one step between the two (12.4). On the game camera the room factor is 1, the
sounds at the listener go back to the game camera, and every steered sound
goes back to the DLL's polar position. So it is the old sound, exactly.

### 11.9 Not done, and open

- **The game camera decided what plays.** `GAudio::PlaySoundEffect` drops a
  sound past its maximum distance from the game camera, and the turn update
  stops one. In the God view the game camera is 400 units from the player's
  place. That was the cause of the loops that stopped. Section 12 has the
  cause and the change: the ears decide now.
- **The bed of the atmosphere has no direction.** It is one stereo wave. The
  game knows where each kind of land is: `GSoundMap` (`*(GGame+0x250058)`)
  keeps, for each of the 14 `ATMOS_TYPE`s, the count of cells and the cell
  nearest to the game camera (`AtmosMapTypeInfo` at `+0x08`, 12 bytes each:
  the count, the squared distance, and the x and z of the cell). Bank `i` at
  `GAudio+0x194` is type `i`. A later step can steer the single sounds of the
  sea and the shore toward the nearest cell of their type.
- **The sign of the azimuth** is still not proven by ear (open question 4).
  A4 uses the same sign as A3.
- **The value 40** is a first choice from the Creature's mapping. Buck can
  set `audio_units_per_meter` to try others. A smaller value makes far sounds
  louder.

### 11.10 Headset test

1. Start a game. Put the head near the Creature. Its steps must be clear
   within arm's reach and become quiet at 1 to 2 m. Turn the head: they stay
   where the Creature is.
2. Listen to the birds and the sea for a minute, and turn the head slowly. A
   single bird must stay where it is in the room. It must not turn with the
   head.
3. Zoom out and in with the stick. A sound at the same distance in the room
   must keep its level.
4. Ctrl Shift F12 gives the old sound for an A and B. Press it again for the
   new sound.

## 12. Sounds that stop: the ears decide what plays (2026-10-03)

Buck, headset test of 2026-10-03, build `3918f87`: "at times background
sounds like the large portal sound seems to completely cut out. Then I will
teleport around a bit and it will come back."

### 12.1 The cause (V)

The game decides which positional sounds play with its own camera, in world
units. Section 3 said so, and A5 left it. The tests:

| Test | Where | Rule |
| --- | --- | --- |
| The start of a game effect | `GAudio::PlaySoundEffect` `0x00429E30`, the compare at `0x00429F16` | No start when the squared distance from the game camera is more than the squared maximum distance of the sample. The maximum is `GAudio::GetGSFXSampleMaxDistance` `0x0042A430`, or `options+0x58` when that gives 0 |
| The stop of each positional sample | `LHSampleUpdate3DChannels`, DLL RVA `0x14310`, each game turn | For a sample that plays with the 3D flag (`info+0x48`) and the update flag (`info+0x4C`, from `options+0x0C`), the object callback `0x00427200` gives the distance from the game camera. When the callback gives no position, or the distance is not less than `info+0x6C` (the maximum, from `options+0x58`), the DLL calls `LHSampleStop` |
| The start of a delayed sound | `SoundTag::CheckDelay` `0x0071E760`, and the process of the particle sounds at `0x006D1312` | Also the game camera. Not changed (12.6) |

Nothing in the DLL starts a stopped loop again. The game does, in two ways:

- **A sound tag** (`SoundTag`, vtable `0x00980538`). The script command
  `GScript::AttachSoundTag` `0x00710150` makes one with `SoundTag::Create`
  `0x0071E840`. `SoundTag::ProcessSoundTags` `0x0071E5F0` runs the list at
  `GGame+0x205C1C`. Each tag with an object calls `GAudio::PlaySoundEffect`
  in each game turn (`0x0071E6F2`). When the sample plays already, the DLL
  does nothing (play mode 2 at `options+0x50`, DLL RVA `0x11200`). So the
  sound of a tag starts again in the first game turn in which the game camera
  is in range.
- **The sound of a particle system** (`PSysSound`). `AtomCore::StartSound`
  `0x006745D0` starts it one time through `GAudio::SamplePlayAnimEffect`,
  with no distance test. The process of these sounds (`0x006D11A0`) deletes
  the `PSysSound` when its sample does not play. The rule
  `StartStopSoundOnCondition` `0x0069DC40` starts the sound again when the
  atom has no sound for the action. An effect that only adds the sound
  (`AddSoundToAtom` `0x0069DCA0`) does not.

In the God view the game camera is 400 units from the player's place, 50
degrees up (`god-view.md`, section 7). The ears are the head, and QMixer hears
a sound up to `max * k` from the head (11.5). The math test has the case. At
100 units a meter, a sound with a maximum of 500 is on the land 3 m in front
of the player. It is 636 units from the game camera, so the game stops it and
does not start it. For the head it sounds as 122 units, near full level. A
snap turn puts the game camera on the other side of the player, and a
teleport moves it. So a loop came and went with moves that did not change
what the player was near.

Which sound the "large portal" is, is not known (I): the log of that session
is lost. The tests above apply to each positional loop. The log of 12.3 names
the sound in the next headset session.

The other guesses of the work plan:

- **The channel limit.** When no sample slot is free, the DLL takes the slot
  of the sample with the lowest priority, if the new sample has a higher one
  (DLL RVA `0x112CA`, the priority is at `info+0x70`). Otherwise it refuses
  the new sample. The room distances did not make more sounds play, because
  the game camera decided the starts. So this was not the cause. 12.2 says
  what limits the slots now.
- **The scale.** QMixer computes the gain of a channel at each mix block,
  from the mapping and the listener. The frame driver sends the mapping again
  when `k` changes by 1%. So a loop that plays out of range is silent, and it
  sounds again in range. Not a cause. 12.2 uses this.
- **The session handle.** `audio_rolloff.cpp` kept the handle of the last
  hooked call. No run showed a change of it. The frame driver now reads
  `LH_AudioSystem+0x88` in each frame, and it logs a change.

### 12.2 What the mod does now

`audio_range.cpp` has three hooks, all on the game thread. They act while the
ears are the head and the room distances apply: the table or the God view
owns the view. In a story, in a menu, with the ears on the game camera and
with `audio_room=0`, the game's tests apply. The loop rule is the one
exception.

- **The stop test.** A hook on the object callback `0x00427200` puts the
  distance from the ears divided by `k` in the answer
  (`audio_math::RangeDistance`). The DLL compares it with the maximum of the
  sample. So a sample stops at `max * k` from the head. That is where QMixer
  makes it silent.
- **A loop keeps its slot** (`audio_loops_keep=1`, the default since run
  keep1 of 2026-10-04, see 20.2). A loop is a sample with a loop count (`info+0x40`, the
  count that `QSWaveMixPlayEx` got). For a loop that is out of range, the
  hook gives the distance 0. The DLL does not stop the loop, and QMixer makes
  it silent. When the head comes back in range, the loop sounds at once, and
  the game does not have to start it again. This rule applies in each view,
  also in a story and in a menu. The DLL still stops a loop when its object
  has no position.
- **The reserve of slots.** The DLL has 16 sample slots (12.5). With the
  rule above, a loop out of range stops, as in the game, when fewer than
  half of the slots are free (`audio_math::KeepSilentLoop`). So silent loops
  do not take the slots that new sounds need.
- **The start test.** A hook on `GAudio::PlaySoundEffect` makes the test with
  the same distance. Out of range, the hook returns, and the game does not
  play the sound. In range, the hook calls the game's function. A hook on
  `GAudio::GetGSFXSampleMaxDistance` then gives a very large maximum to the
  one call at `0x00429EF7`, so the game's distance test passes. The other
  tests of the function stay: the interface state and the citadel.
- **A sound with no object** is at the game camera, and A5 puts it at the
  ears. It keeps the game's tests.

So a sound tag starts and stops its sound with the range of the head, at each
zoom. A loop that the game starts one time is no longer stopped while the
head is in range. With `audio_loops_keep=1` it also stays when the head goes
out of range, silent, for as long as its object lives.

Each hook checks the first bytes of its function. When they are not the bytes
of version 1.42, the hook is not made, the log says so, and the game camera
decides.

### 12.3 The log

The tag is `audio range`. There is one line for each change of a loop, or of
the sound of a sound tag. Each line gives the place, the distance from the
head, the range in the room, and the distance from the game camera:

```
audio range  : the DLL has 16 sample slots, 10 are free. With audio_loops_keep=1, a loop out of range keeps its slot while 8 or more are free
audio range  : sound in game 52 of SoundTag starts. At 2230 16 3155, 1.20 m from the head (120 units). Range 7.50 m (300 units times 2.500). The game camera is 412 units from it. 100.0 units a meter
audio range  : loop (count -1) ... plays, in range. ...
audio range  : loop ... is out of range and silent. It keeps its slot (10 of 16 are free). ...
audio range  : loop ... is in range again. ...
audio range  : loop ... stops: out of range, and only 3 of 16 slots are free. ...
audio range  : sound ... stops: out of range of the head. ...
audio range  : sound ... is refused: out of range of the head. ...
audio range  : sound ... is refused by a rule of the game (not the distance). ...
audio range  : sound ... is refused: the DLL gave no sample slot (0 of 16 are free). ...
audio range  : loop ... stopped, and not by its range: the game or the DLL stopped it. ...
audio range  : loop ... stops: its object has no position now. ...
audio range  : the head decided N starts: N started that the game camera refuses, ...
```

The last line comes each 30 s, when a number changed. With `audio_range=0`,
or with the test key on its second step, the same lines show what the game
camera decides: "is refused: the game camera is out of range" and "stops: the
game camera is out of range". So a log tells a drop-out by the game from a
drop-out by the range. There are at most 60 lines for one sound and 600 in
all. The loop of an animation starts and ends in a few seconds (a Creature
that sniffs, a tree that moves). So the first start of a loop gets its line
after the loop has played for 5 s, and a loop that ends before gets no line.

The play trace (`audio_trace=1`) no longer writes a line when a sample
played already. A sound tag asks 10 times a second. The table of kinds counts
these calls.

### 12.4 The keys

| Key | Default | What |
| --- | --- | --- |
| `audio_range` | 1 | The head and the room distances decide which positional sounds start and stop. 0: the game camera decides, as in the game |
| `audio_loops_keep` | 1 | 1: a loop out of range keeps its sample slot and is silent, while half of the slots are free. 0: it stops, as in the game. It was 0 until run keep1 showed the rule (20.2) |
| `audio_test_tag` | 0 | Test only. A sample number: the mod puts a sound tag with this sample on the player's Creature, 120 frames after the table or the God view has the view |
| `audio_test_tag_bank` | 1 | Test only. The bank type of that sample. 1 is the in game bank |

None of them is in the launcher. Ctrl Shift F12 (with `test_keys=1`) now has
three steps, and the log names each one:

1. The ears are the head, and the head decides which sounds play (as set).
2. The ears are the head, but the game camera decides which sounds play.
   This is the sound before `audio_range`, for an A and B of this change.
3. The ears are the game camera. This is the sound of the game, exactly.

### 12.5 What the desktop runs showed

The rig is the recipe of `aim_run.ps1`: a new game on Land 1, and the God
view at the Creature's pen after the first story. `audio_trace=1` and
`audio_test_tag=52` are on. The tag is on the Creature, which stands 0.60 m
under the head. Sample 52 of the in game bank has the mapping 100, 300, 4.
The script turns the view 4 times, zooms out to 225 units a meter, teleports
340 units, zooms out to 506 units a meter and comes back to 225. Two runs had
the same build and the same script:

| | aur0b, `audio_range=0`: the game camera decides | aur1b, `audio_range=1`: the head decides |
| --- | --- | --- |
| Starts of the tag's sample in the run | 91 | 666 |
| After the first zoom step, with the source 0.60 m from the head | The game camera is 404 units from the source. The log says "is refused: the game camera is out of range" and "stops: the game camera is out of range" | The sample plays |
| After the teleport, with the source 1.77 m from the head (398 units) | The game camera is 314 units from the source. 1 start, then nothing to the end of the run | 573 starts, and 635 channel lines of the trace |
| QMixer for a channel of the tag after the teleport | The same mapping, on the last channels that played | Mapping 562 1688 4, distance gain 1.000 |
| Lines "the game camera is out of range" | 7 | 0 |
| The report line | None | "47 started that the game camera refuses", "4298 game turns of a sound in range that the game camera stops" |

So the game camera's rule gives the fault on the desktop: a sound 0.6 m from
the head stopped after a zoom step, and it stayed away after a teleport. With
the head's rule it played after each move.

More facts from the runs:

- The DLL has 16 sample slots. (V)
- Sample 52 has a play mode of its own in the bank (the flag `0x400` at
  `bank sample+0x244` and the mode at `+0x274`, DLL RVA `0x110C3`). It starts
  one more copy for each request, so the tag filled the slots, and the log
  has "the DLL gave no sample slot" lines. This comes from the test sample.
  With play mode 2 a tag plays one copy.
- In this state of the game the player has no Creature of their own
  (`GPlayer+0xA4C` is empty). The test tag goes on the Creature that the
  sound code showed.
- Run aur1a had no tag. In it 13 effects started that the game camera
  refuses.
- Run aur1c used the build that landed, with `audio_loops_keep=0`. It gave
  the result of aur1b: 667 starts, 580 of them after the teleport, 638
  channel lines, and no line "the game camera is out of range".
- The loops of animations (a tree, the Creature) started and ended within
  seconds in each run. No loop was out of range, so no run shows the rule of
  `audio_loops_keep`.

### 12.6 Not done, limits, and what needs the headset

- **By ear:** that a loop sounds whenever the head is in its range, after a
  grab, a turn, a zoom and a teleport. That a loop which goes out of range
  fades to silence at the range and does not click. The test key gives the A
  and B.
- **The name of the sound.** Which loop Buck heard is not known. Send the
  log after a session with a drop-out: the `audio range` lines name each loop
  and say why it changed.
- **Sounds that do not use `GAudio::PlaySoundEffect`.** The effects of
  animations, of physics and of particle systems start through
  `GAudio::SamplePlayAnimEffect` `0x0042A4B0`. Since 2026-10-04 that start
  also uses the distance of the head (20.2). A sound tag with a delay
  (`SoundTag::CheckDelay`) and a particle sound with a delay (`0x006D1312`)
  still test the game camera in their own code.
- **Far zoom.** At 4000 units a meter `k` is 100, and each sound on the
  island can be in range. The DLL's slots and its priorities then decide,
  and with only 16 slots the DLL refuses many of them. No run measured this.
- **`audio_loops_keep`** had no run on 2026-10-03. Run keep1 of 2026-10-04
  shows it with a loop of the game's own bank, and the key is 1 now (20.2).
- **A loop that the game itself stops** (a script, the end of an effect)
  stops as before, with each setting.

## 13. What still does not sound as if it comes from the world (a plan)

Buck said that most sounds did not sound as if they came from the world.
A5, A4 and section 12 changed the level, the place of the flat sounds, and
which sounds play. These items remain. Nothing here is built. The order is
value against cost, and `research-hla.md`, section 8, has the reasons from
Half-Life: Alyx.

| Item | What the player hears | The cause | The plan |
| --- | --- | --- | --- |
| 1. No height | A sound on the land at the waist seems to be at the height of the ears | QMixer pans for two speakers (`QSWaveMixSetSpeakerPlacement` from `SpeakerConfig`, section 2). A pan has no cue for below | First try the headphone placement of QMixer: the mod can call `QSWaveMixSetSpeakerPlacement` itself, with no registry change, on a test key. If QMixer has no filter for height, the mod needs its own HRTF mixer for the sounds with a place (Steam Audio, Apache 2.0). That is a track of its own |
| 2. A near sound is in one ear | The Hand and the things in it are 30 to 60 cm from the ears, and their sounds jump hard to one side | Plain panning by the angle. Half-Life: Alyx spreads a source nearer than 25 to 50 cm to the two ears | In the source position hook, keep a source at least 50 cm from the ears along the forward axis of the head, or blend its direction toward the front as it comes near. Small |
| 3. Sounds with the default mapping are faint | An effect with an object and no mapping of its own is quiet a few centimeters from the head | The options default is min 1, max 10000, rolloff 0.3 (section 4). With room distances the min is `k` units, 2.5 cm at 100 units a meter | Give the min of such a mapping a floor before the room factor (for example 30 units, the Creature's value). First list these sounds with the play trace in a scene with villagers |
| 4. Music with a place has no direction | The chant at a worship site and the dance music get quieter with distance, and come from the middle | `LHMusicSet3DPosition` sets a volume from the distance to the listener (section 3), in world units, with no pan and no room factor | Trace the three music entries first. Then scale the distance by `k`, and give the music channel a pan from the head, as A4 does for flat samples |
| 5. The bed of the atmosphere is flat | The sea and the wind are the same all around | One stereo wave for each atmosphere bank (11.4) | Keep the bed flat, as Half-Life: Alyx keeps its low and long layers. Steer the single sounds of the sea and the shore toward the nearest cell of their type (`GSoundMap`, 11.9) |
| 6. Some sounds near the player do not start | A villager or a tree near the Hand makes no sound | The start tests that section 12 did not change: the DLL's own test for animation effects, and the delayed sounds (12.6) | Give those paths the distance of the ears too: the call sites of `GCamera::GetDistanceSq` `0x00442D10` in the sound code, and the distance argument of `LHSamplePlayAnimEffect` |
| 7. The advisors have no height | A spirit that floats over the shoulder sounds level | A3 sends elevation 0, as the DLL did | Send the elevation of the spirit, as A4 does. Only of use after item 1 |
| 8. A head turn is late | A sound lags a fast turn of the head | The latency of the QMixer buffer, and maybe the pan rate (section 7) | Measure by ear with the test key. A change of the latency is a registry value, so it needs Buck's word |

Not for this list: sounds of the interface and of the scripts with no place.
They are flat by design.

## 14. The sound of the world: the program of 2026-10-04

Buck, 2026-10-03: "We should try to make the world sounds as good as we
possibly can and add as many improvements as needed. Things like sound
distortion from far distance, being able to hear certain frequencies from
further away when zoomed out like crashes and bangs and so on. Maybe add
slight sounds for actions we might not have sounds for, like slight swoosh
for camera rotation, zooming and teleporting, etc. The sounds not coming from
the right height is a big one. If background sounds like waves and birds are
stereo in game files we should rotate the sound with the camera/head angle."
The advisors sound very good, and they must not change.

### 14.1 The ranked list

The order is what the player hears most, against the cost and the risk.

| Rank | Item | State | Section |
| --- | --- | --- | --- |
| 0 | Hear a desktop run: capture the stream and measure it | Built, used for each proof below | 15 |
| 1 | The mixer of the mod: each sound with a place gets its direction for two ears, with its height | Built, proven | 16 |
| 2 | A near sound goes to both ears | Built with the mixer, proven | 16.4 |
| 3 | A far sound loses its high frequencies and is a little diffuse | Built with the mixer, proven | 16.5 |
| 4 | The bed of the atmosphere turns with the world | Built, proven | 17 |
| 5 | The zoom changes how far a sound carries, so loud events carry and small sounds go away | Built, proven by numbers and the log | 18 |
| 6 | Soft sounds for the player's own moves | Built and proven: the moves of the view, and the sounds of the hands at their haptic pulses | 19 |
| 7 | Loops and slots | The loop rule is proven and on. Animation effects start by the distance of the head. The slots by loudness are not built | 20.2 |
| 8 | Balance of the levels | Measured, see 16.6 and 20 | 20 |

Rank 1 comes first because all the others need it: only a mixer of the mod
has the signal of one source. The capture is rank 0 because no change of the
sound has a proof without it.

### 14.2 The keys of the night

All in `[bwvr]`, none in the launcher. Each one has the old behavior as a
value.

| Key | Default | What | The old behavior |
| --- | --- | --- | --- |
| `audio_mixer` | 1 | The mixer of the mod plays each sound with a place, and the bed | 0: QMixer pans each sound |
| `audio_height` | 1 | The mixer gives a source its height | 0: each source is at the level of the ears |
| `audio_height_pct` | 100 | The strength of the height band, 0 to 200 | |
| `audio_near` | 1 | A source nearer than 50 cm goes toward both ears | 0 |
| `audio_far` | 1 | The low pass and the diffuse share by distance | 0 |
| `audio_bed` | 1 | The bed of the atmosphere turns with the world | 0: QMixer plays the bed flat |
| `audio_atmos_mixer` | 1 | The single sounds of the atmosphere (birds, waves) go through the mixer | 0: QMixer plays them, steered by A4 |
| `audio_atmos_toward` | 1 | A single sound of the atmosphere goes toward the land of its kind (17) | 0: a random place on the ring of A4 |
| `audio_loops_keep` | 1 | A loop out of range keeps its slot and is silent (20.2) | 0: it stops, as in the game |
| `audio_zoom_pct` | 50 | The power of the scale in the distance factor | 100: the same distances at each zoom |
| `audio_cues` | 1 | Soft sounds for the player's moves | 0 |
| `audio_cues_pct` | 100 | Their level, 0 to 300 | |
| `audio_latency_ms` | 0 | The latency that QMixer starts with. 25 puts the stream 66 ms ahead of the device in place of 116 ms (20.3) | 0: QMixer's own value |
| `audio_capture` | 0 | Test only: write the stream to `bwvr-audio.wav` | |
| `audio_test_voice`, `audio_test_path`, `audio_test_step_ms`, `audio_test_solo` | 0 | Test only: the test voice and the test bed (15.3) | |
| `audio_test_tag`, `audio_test_tag_bank`, `audio_test_bank` | 0, 1, -1 | Test only: a sound tag on the Creature, and a list of the loops of a bank (20.2) | |

Ctrl Shift F12 (with `test_keys=1`) now has five steps. Each step takes one
more change away, and the log names the step:

1. All as set.
2. The distances are the same at each zoom (the sound before `audio_zoom_pct`).
3. Also QMixer pans each sound, with no height (the sound before
   `audio_mixer`). The change is at once, in both directions: the voices of
   the mod keep their time while QMixer plays.
4. Also the game camera decides which sounds play (before `audio_range`).
5. The ears are on the game camera. This is the sound of the game, exactly.

## 15. Hearing a desktop run

A desktop run has no listener. `audio_capture.cpp` and
`tools/audio_measure.py` give each change of the sound a proof in numbers.

### 15.1 Where the stream is (V)

- QMixer does not use the wave device on this PC. No call of `waveOutOpen`
  or `waveOutWrite` came from `QMixer.dll` in run au2p2. QMixer mixes into a
  secondary DirectSound buffer: 22050 Hz, 16 bit, stereo (run au2p4).
- QMixer locks a part of the buffer, mixes its channels into it, and unlocks
  it, about 60 times a second (run mx1: 62 blocks and 88,192 bytes in a
  second, so a block is 16 ms). The first block is 200 ms.
- The mod gets the DirectSound object from `QSWaveMixGetDirectSound`. It
  makes two buffers of its own for a moment, a secondary one and the primary
  one, to find the code of `IDirectSoundBuffer::Unlock` for each class, and
  it hooks that code. A call that comes from `QMixer.dll` is a block of the
  stream. The import table of `QMixer.dll` for the wave device is patched
  too, for a PC where QMixer uses it.
- QMixer gives no block while no channel plays. In the world the bed of the
  atmosphere plays, so the blocks do not stop.
- The game calls `QSWaveMixActivate(1)` when its window is not in front, and
  QMixer mixes all the time (58 s of stream in a run of 62 s with the window
  behind the taskbar). So a run needs no window in front for its sound.
- The stream is the last place before the device. The mod adds its own
  voices there (section 16), so they need no device of their own and no
  change of a Windows setting.

### 15.2 The capture and the script

With `audio_capture=1` each block goes to `bwvr-audio.wav` in the game
folder, after the mod's voices are in it. The header is written again each
half second, because a test run ends the game by force.

`python bwvr/tools/audio_measure.py run.wav --log bwvr.log` prints for each
part of the run: the level of each ear, the difference between the ears in
dB (ild), the delay between the ears from the cross correlation under 1.5
kHz (itd), their correlation, the center of the spectrum, the level of each
octave band from 63 Hz to 8 kHz for each ear, and the largest step between
two samples against the level (a click shows as a large number). `--compare`
gives the difference of two parts, and `--track` gives the level over time.
The parts come from log lines of the form `audio test   : step N <name>,
capture frame F`. The frame count of the capture is the clock: the line of
an event gives its place in the file.

### 15.3 The test voice and the test bed

- `audio_test_voice=1` with `audio_test_path=az,el,m,part/az,el,m/...`
  plays a voice of noise through the mixer, at each place of the list in
  turn (degrees to the right, degrees up, meters of the room, and the part
  of its range). Each step lasts `audio_test_step_ms` of the capture. The
  place follows the head, so the direction is exact.
- `audio_test_voice=2` plays a stereo test bed: low noise in the left
  channel and high noise in the right one.
- `audio_test_solo=2` makes all other sound silent in the stream, so the
  capture has only the test. `1` makes only QMixer silent.
- With `audio_trace=1` each cue of section 19 writes a step line too.

## 16. The mixer of the mod: direction with height, the near field, distance

### 16.1 Why QMixer cannot do it (V)

Run mx3 measured QMixer's own sound for a source 77 degrees to the right, 6 m
away: the right channel is 8.9 dB louder at each frequency, also at 63 Hz,
and the two channels have a correlation of -0.64. That is a pan for two
speakers with a phase trick. On headphones it has no cue for height, and
the phase trick is a pressure in the ears. QMixer has no call that gives the
signal of one channel back.

### 16.2 Where the mod takes the signal (V)

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

### 16.3 How it works

`audio_voices.cpp`. For a play of `LHSamplePlay` that sets a place for its
channel (`QSWaveMixSetSourcePosition` in the same play, not relative to the
listener, not an advisor), the hook on `QSWaveMixPlayEx` copies the wave and
starts a voice of the mod. QMixer plays the same channel with the volume 0.
So QMixer is still the clock: it tells the DLL when the sample is done, and
the game's logic does not change. The voice follows the channel through the
hooks on `SetVolume`, `SetFrequency`, `StopChannel`, `FlushChannel`,
`PauseChannel` and `RestartChannel`. The hooks of `audio_rolloff.cpp` give
it the place and the game's distance mapping.

For each block of the stream, the mod renders each voice and adds it to the
block. A voice gets its level from the same model as QMixer
(`QMixerDistanceGain` with the distance divided by `k`), so the range of a
sound is the same with the mixer on and off.

The direction comes from a structural model of the head
(`audio_spatial.h`), after Brown and Duda, "A Structural Model for Binaural
Sound Synthesis" (1998):

| Part | What it does |
| --- | --- |
| The delay between the ears | Woodworth's formula for a head of 8.75 cm: up to 0.66 ms |
| The shadow of the head | One pole and one zero for each ear: +6 dB of high frequencies at the near ear, -20 dB at 150 degrees |
| The echoes of the outer ear | Five echoes whose delays change with the elevation. The second one makes a notch that goes from 4.9 kHz (straight below) to 11 kHz (straight above) |
| The height band | A shelf over 5 kHz: +7 dB straight above, 0 at the level of the ears, -9 dB straight below (`audio_height_pct` scales it) |
| The body | The part over 1.5 kHz loses 2.5 dB straight below and 3.1 dB straight behind. A source over the ears gets an echo of the shoulder |

**Why a model and not a measured HRTF.** A model needs no data set, so
there is no file to license and to ship. It costs a few multiplications for
each sample. Its parts have a meaning, so a key can scale one of them. The
samples of the game are 22050 Hz, and the stream is too, so there is nothing
over 11 kHz for a measured HRTF to shape. A measured set (for example the
MIT KEMAR set, which is free to use with a citation) can replace
`audio_spatial::Process` later: the callers give only the place of the
source.

**No click.** Each control (the angles, the level, the corner of the low
pass) goes to its new value with a time constant of 15 ms, and the filters
get new values each 16 samples. A voice starts at its full level, so the
attack of a crash or of a step stays sharp, and only its first 32 samples
rise from zero (1.5 ms). A stop lets the level go to 0 in 80 ms. A sample that is in its last 0.3 s when QMixer stops
the channel plays to its end. A loop of the mod has no end of its own, so
the mod asks QMixer twice a second whether the channel of each loop still
plays (`QSWaveMixIsChannelDone`). When QMixer ended it with no stop call
that the hooks saw, the voice stops too. The sum of the stream and the voices goes
through a soft limiter (`audio_spatial::SoftLimit`): under 26000 a sample
does not change, and over it the curve bends, so a loud moment has no hard
edge. The report counts the samples over the knee. In run land2, at the door
of the Temple, 54 samples of a minute came to full level, and QMixer's own
mix came to full level there too.

**The single sounds of the atmosphere** (a bird, a wave) go through the
mixer too (`audio_atmos_mixer=1`). A4 makes a place in the room for each one
when it starts, 3 m from the ears on a level ring. The DLL plays them with a
polar position. The mixer takes the place of A4, and the level that QMixer
gives the range of the polar call with the mapping of the channel. So a bird
has the two ear model, at the level of the ears, over the sounds of the land.

**What stays with QMixer:** each flat sound (the advisors, the story
speech, the interface, the music), a flat effect that A4 steers, and a
sample whose wave is not 16 bit PCM. The mixer also stays off when the
stream is not 16 bit stereo. The log gives the count of each.

### 16.4 The near field

Half-Life: Alyx sends a source nearer than 25 to 50 cm to both ears. Here a
source keeps a share of its angle to the side: all of it from 50 cm of the
room, 0.2 of it at 20 cm (`audio_spatial::NearFocus`). The delay and the
shadow follow the smaller angle. The height does not change.

### 16.5 Distance

`part` is where a source is in its own range: 0 at its minimum distance, 1
at its maximum distance, both from the game's mapping with the factor `k`.
So it is a distance of the room, and it is right at each zoom. A low pass
has its corner at 12 kHz for part 0, 5 kHz half way, and 1.2 kHz at the
maximum. A share of the sound, up to 0.25, goes to both ears with no
direction. A sound with a long range (a crash) is brighter at the same
distance than one with a short range (a voice).

The low pass and the diffuse share work only while the room distances
apply. In a story the game puts its camera where the speech must be heard.
There a far sound keeps its high frequencies, as in the game. The direction
for two ears and the height apply in a story too. Run story1: the first
story of a new game (the Creature select) played to its end with the mixer,
and the animation sounds of the Creature went through it.

### 16.6 The proof

**The math test** (the model alone, white noise, bands in dB):

| Source | 1 kHz band | 4 kHz band | 8 kHz band |
| --- | --- | --- | --- |
| Ahead, 60 degrees below | -43.2 | -43.0 | -51.6 |
| Ahead, at the level of the ears | -43.2 | -40.1 | -42.7 |
| Ahead, 60 degrees above | -44.3 | -37.7 | -38.6 |

**Run mx4** (in the game, the test voice alone in the capture, with
`audio_test_solo=2`). The level of the 4 kHz and the 8 kHz band, and the
two ears:

| Step | 4 kHz | 8 kHz | Left minus right | Delay of the far ear |
| --- | --- | --- | --- | --- |
| Ahead, 90 degrees below | -1.7 | -8.8 | 0.0 dB | 0 |
| Ahead, 60 degrees below | -1.2 | -7.4 | 0.0 dB | 0 |
| Ahead, 30 degrees below | 0.1 | -3.7 | 0.0 dB | 0 |
| Ahead, level | 2.3 | 1.4 | 0.0 dB | 0 |
| Ahead, 60 degrees above | 4.8 | 6.6 | 0.0 dB | 0 |
| Right, level, 1 m | | | -7.0 dB | 794 us |
| Left, level, 1 m | | | +6.9 dB | 794 us |
| Right, level, 20 cm | | | -3.2 dB | 221 us |
| Ahead, far (part 0.9) | -11.2 | -20.4 | 0.0 dB | 0 |
| Behind, level | 0.3 | -0.9 | 0.0 dB | 0 |

So the top octave has 14 dB more for a source 60 degrees above than for one
60 degrees below, and the bands up to 2 kHz change by less than 2 dB. The
far source of this run had the first curve of the low pass. The curve of
16.5 is softer.

**A sample of the game below the head** (runs hb1 and hb2: a sound tag
with the loop 171 of the in game bank on the Creature, 0.73 m from the head,
ahead and about 55 degrees below, with only the voices of the mod in the
capture). With `audio_height=0` the 4 kHz band of the left ear is 4.1 dB
and the 8 kHz band is -9.1 dB. With the height on they are 1.2 dB and
-15.6 dB. So the height takes 2.9 dB from the 4 kHz band and 6.5 dB from the
8 kHz band of this source, and the bands up to 500 Hz change by about 1 dB.
The right ear gives 2.0 and 6.2 dB.

**The cost** (runs mx2 and mx5): a block of 16 ms takes 15 to 17
microseconds with 6 voices, and 191 microseconds on average with 18 voices
(1 ms at most), on the mixer thread of QMixer. The work on the game thread
is 8 microseconds a frame. No sample was cut at full level.

**The level** (runs mx7 and mx8: the test tag, 16 copies of sample 52,
6.2 m from the head and 75 degrees to the right). With the mixer the two
ears have -23.8 and -22.5 dB, and their sum is -20.1 dB. With QMixer the two
channels have -30.3 and -20.5 dB, and their sum is -20.1 dB. So a sound is as
loud with the mixer as with QMixer. The level of a voice at its minimum
distance is 0.7 for each ear. QMixer's own number for a positional channel
(0.315) gave 7 dB less in runs mx2 and mx3. With the mixer the two ears have
a correlation of 0.99 and a delay of 641 microseconds. With QMixer the
correlation is -0.64.

## 17. The bed of the atmosphere turns with the world

Each atmosphere bank has one stereo wave, the bed, that QMixer played flat.
So the sea was in the same ear wherever the player looked. Now the mixer
plays the bed (a flat stereo wave of `LHAtmosProcess`), and it turns the
stereo field against the yaw of the head in the world
(`audio_spatial::ProcessBed`). With M = (L + R) / 2 and S = (L - R) / 2:

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
it moves in steps of 15 ms.

**Run bed1** (the test bed: low noise in the left channel of the wave, high
noise in the right one, alone in the capture, and four snap turns of 45
degrees). The level of the 125 Hz band and the 8 kHz band in each ear:

| Yaw | Left ear, 125 Hz | Right ear, 125 Hz | Left ear, 8 kHz | Right ear, 8 kHz |
| --- | --- | --- | --- | --- |
| 45 degrees | 12.1 | 1.2 | 0.3 | 13.1 |
| 90 degrees | 5.1 | 11.5 | 10.1 | 10.5 |
| 135 degrees | 1.8 | 15.1 | 13.5 | 7.0 |
| 180 degrees | -1.9 | 15.3 | 14.3 | -0.8 |

The low noise goes from the left ear to the right ear, and the high noise
goes the other way.

**The sea is where the sea is.** A single sound of the atmosphere (a wave,
a bird) got a random place on a ring around the player (A4). The game knows
where the land of each kind of atmosphere is: `GSoundMap`
(`*(GGame+0x250058)`) keeps for each of the 14 kinds, from `+0x08`, 12
bytes: the count of cells near the game camera (a word), the squared
distance of the nearest one (a float), and its x and z in world units (two
words). `AtmosMapTypeInfo::Add` `0x0071D510` writes them. Now, when the
nearest cell of the kind of a sound is 1.5 m of the room or more from the
ears, the sound goes toward it, 3 m away, with a spread of up to 26 degrees
to each side (`audio_atmos_toward=1`). When the land of the kind is all
around the player, the ring stays. Run sea1: at the first place of the view
three sounds of kind 7 went toward their land, 6.6 m away and 41 degrees from
north (the sounds at 16 and 41 degrees). At the door of the Temple 18 of 22
sounds stayed on the ring.

## 18. The zoom changes how far a sound carries

With A5, one meter of the room sounded as 40 units at each zoom. So a
villager the size of an ant was as loud as one of life size, and at a far
zoom each sound of the island was in range. Buck wants the other thing: far
out, the small sounds go away, and a crash still carries, mostly its low
frequencies.

**How the game classes its sounds.** Each sample has its own distance
mapping in its bank (min, max, rolloff: 11.4), and that is the class. A
footstep of the Creature has the maximum 80, a sound of the Hand 300, and a
sound with no mapping 10000. The game has no other table of loudness.

**The change.** The distance factor grows more slowly than the scale
(`audio_math::ZoomFactor`):

```
k = (100 / audio_units_per_meter) * (s / 100) ^ (audio_zoom_pct / 100)
```

At 100 units a meter, the scale at the start, nothing changes. With the
power 0.5:

| Scale (units a meter) | k | A sound with the maximum 300 carries | One with the maximum 2000 carries |
| --- | --- | --- | --- |
| 7.5 (life size) | 0.68 | 27 m | 183 m |
| 100 | 2.5 | 7.5 m | 50 m |
| 225 | 3.75 | 5 m | 33 m |
| 1000 | 7.9 | 2.4 m | 15.8 m |
| 4000 | 15.8 | 1.2 m | 7.9 m |

At 4000 units a meter the island is 1.3 m across. A voice is heard only
near the hand, and a loud event is heard from each place. The low pass of
16.5 works on the part of the range, so the far part of a loud event is
mostly low frequencies. `k` goes to QMixer's mappings, to the mixer of the
mod and to the range tests of section 12, so all three agree. Fewer sounds
are in range at a far zoom, which also helps the 16 sample slots.

Run mx5: at 225 units a meter the log says "distances now divided by 3.750".
Run mx6 with `audio_zoom_pct=100` says 5.625 there, as before.

## 19. Soft sounds for the player's own moves

`audio_cues.cpp`. A move of the view that the mod makes had no sound. Now a
short, quiet cue plays, through the mixer, flat:

| Cue | The sound | Length |
| --- | --- | --- |
| A turn step | A swish: noise through a band that moves from 800 to 1700 Hz (the other way for a left turn), a little to the side of the turn | 0.10 s |
| A zoom step | A swoosh: 450 to 1300 Hz to go in, 1300 to 450 Hz to go out | 0.15 s |
| A teleport | A whoosh from 350 to 2000 Hz, then a soft landing at 72 Hz | 0.31 s |
| A hand takes the land | A soft thud at 105 Hz | 0.10 s |
| The grab from a distance has its target | A ping at 1320 Hz | 0.20 s |
| An object arrives in the hand | A tap at 235 Hz | 0.08 s |

The sounds are made in code when the game starts (noise through a state
variable filter, and sine tones that fall away). The mod ships no sound
file. Each one starts and ends at zero. Their peak is about 0.06 of full
level. Half-Life: Alyx plays its ping at 0.15 and one at a time: here a cue
of a kind waits 60 ms for the next one.

**The moves of the view need no call.** `AudioCuesFrame` sees them in the
ears of each frame: a yaw of more than 15 degrees in one frame is a turn
step, a move of more than 0.5 m of the room in one frame is a teleport, and
a change of the scale by one zoom step (`god_zoom_step_pct`, within 40%) is
a zoom step. A real head cannot do these in one frame. Two hands that scale
the land change the scale a little in each frame, and that has no cue. In
the God view a cue also needs a new version of the view
(`TabletopGodGameCamera`), which only a move of the player gives. The game
can put the view at a new place by itself (a new camera mode, the end of a
story), and that has no sound: run land2 played a teleport cue there, and
run land3 does not.
Nothing plays while a story owns the view, or in the first 45 frames after
the view comes back. A recenter (F11) sounds as a teleport.

**The sounds of the hands** start where their haptic pulse starts, so the
hand and the ear agree in time. `HapticsPulseHands` (`haptics.cpp`) calls
`AudioCueHaptic` with the kind of the pulse: `lock` gives the ping,
`arrive` gives the tap (one for an arrival, though the pulse comes in each
frame of the end of the glide), and `world` gives the thud. The cue plays
also with `haptics_pct=0`.

Run hands1 (a far grab of a rock, then the left grip on the land): the log
has the cue of the lock in the frame of its pulse (901), one cue of the
arrival for the six pulses of frames 967 to 972, and the cue of the grab in
the frame of the world pulse (1171). The report says "1 grab, 1 lock and 1
arrival cues". Run grab1 of the build before, with the call in
`world_grab.cpp`: two presses of the left grip gave two grab cues, and the
drag of the land gave no other cue.

Runs mx5 and bed1: 4 turn steps and 4 zoom steps gave "played 4 turn, 4
zoom" in the log, and each cue has its step line with the frame of the
capture. Run land3 (2 turns, 2 zoom steps and a teleport, in the scene at
the door of the Temple): at the zoom step the 1 kHz band of the two ears
went from 2 and 5 dB to 13 dB for 0.15 s. At the teleport the 63 Hz band
went from -9 dB to 3 dB, which is the landing.

## 20. Loops, slots, balance, and what is not built

### 20.1 Balance (measured)

- **The mixer against QMixer:** the same loudness (16.6).
- **Across the zoom:** one model, `k` of section 18. At the start scale it
  is the level of before.
- **The world against the advisors:** the advisors, the story speech and
  the music stay with QMixer, at the level of the game. The mixer gives the
  world the level that QMixer gave it. So the balance between them is the
  one of before. No run measured an advisor line, because none played in
  the runs of the night.
- **The cues** have a peak of 0.06 of full level, under each world sound
  near the player.

### 20.2 Loops and slots

**The loops of the game.** With `audio_test_bank=1` the mod lists the
samples of a bank that the bank loops without end (the flag `0x40` at
`bank sample+0x244`, the count -1 at `+0x248`, the range at `+0x268` and
`+0x26C`, the play mode at `+0x274`). The in game bank has 18 samples with a
loop count. Sample 171 has the range 350 to 500, the play mode 2 and the
priority 4000.

**The loop rule (runs keep1 and keep0).** A sound tag with sample 171 is on
the Creature, 610 units from the head. Four zoom steps in, to 19.8 units a
meter, put it out of range (the range is 500 units times 1.111). Four steps
out bring it back.

| | keep1, `audio_loops_keep=1` | keep0, `audio_loops_keep=0` |
| --- | --- | --- |
| At 19.8 units a meter | "is out of range and silent. It keeps its slot (9 of 16 are free)" | "stops: out of range of the head" |
| The capture there | -55 dB (-17 dB before) | -56 dB |
| Back at 29.6 units a meter | "is in range again" | "starts" |
| Starts of the sample in the run | 1 | 2 |

So with the rule a loop comes back with no new start from the game. That is
what a loop needs that the game starts one time (the sound of a particle
system). A sound tag comes back with each setting, because it asks again.
The key is 1 now. The reserve holds: a silent loop keeps its slot only while
8 of the 16 slots are free.

**The start of animation effects.** `GAudio::SamplePlayAnimEffect`
`0x0042A4B0` starts the effects of animations, of physics and of particle
systems. Its caller gives the distance from the game camera, and the DLL
does not start an effect past 800 units or past the maximum of its sample
(DLL RVA `0x14A83` to `0x14AC6`). A hook now gives the distance from the
ears divided by `k` (`audio_range.cpp`), when the caller gave a distance and
the object has a place. Runs rng1 and rng0 (the same scene): with the head,
the animation sounds of two seagulls 1.4 m from the head played, and those
of the Creature 6.1 m away did not. With the game camera it was the other
way. In run rng1, 1071 effects got the distance of the head in a minute, and
for 1067 of them it was the nearer one.

**The game's own sound tags** show in the log of each run now, for example
"sound in game 147 of SoundTag is refused: out of range of the head" for a
tag with a range of 3 to 5 units.

### 20.3 How late a turn of the head is heard (measured)

The mod's voices go into the stream that QMixer writes, so they are as far
ahead of the device as that stream. The mod now measures it: the hook on
`IDirectSoundBuffer::Lock` gives the place of each write, and the play
cursor of the buffer gives what the device plays. The report of the mixer
has the line "QMixer writes the stream N ms ahead of the device on average
(least to most)", and a count of the times the device passed a write, which
is a gap in the sound.

The game starts QMixer with `QSWaveMixInitEx`: 22050 Hz, 22 channels, the
flags 8, and the latency 0, which lets QMixer choose (the config has 0x40
bytes, with the latency in ms at `+0x1C`, filled at DLL RVA `0x1589E`). A
hook on that call can set the latency (`audio_latency_ms`, default 0, which
leaves QMixer's own value). QMixer writes its first block 4 times the
latency ahead, and then keeps the stream 2 to 4 times the latency ahead.

| `audio_latency_ms` | The first block | The stream is ahead by | Gaps in 68 s |
| --- | --- | --- | --- |
| 0 (QMixer's own, which is 50) | 200 ms | 116 ms on average, 100 to 200 | 0 |
| 25 | 100 ms | 66 ms on average, 50 to 101 | 0 |
| 15 | 60 ms | 47 ms on average, 0 to 58 | 0, but the least is 0 |

The default stays 0: no desktop run can show a gap that comes only with the
load of the headset. With 25 a turn of the head is heard 50 ms sooner. Try
it in the headset, and read the count of the gaps in the log. 15 is too
low.

### 20.4 Not built, with the cause and a plan

| Item | The cause | The plan |
| --- | --- | --- |
| The advisors have no height | They must not change, and they are flat by the game's design | Leave them. A later step can try the mixer for them on a test key only |
| A head turn is as late as the game's sound | The mod's voices ride on QMixer's stream, which is 116 ms ahead of the device on average (20.3) | `audio_latency_ms=25` brings it to 66 ms, and it needs a headset session to show that the sound has no gap. The full answer is a stream of the mod's own (WASAPI) with a short buffer |
| A measured HRTF | The model of 16.3 is the first step | Put a measured set behind `audio_spatial::Process`, with a license that lets the mod ship it |
| Music with a place has no direction | Section 13, item 4 | As there |
| A delayed sound near the head does not start | A sound tag with a delay and a particle sound with a delay test the game camera in their own code (12.6) | Give the two call sites of `GCamera::GetDistanceSq` the distance of the ears |
| The slots at a far zoom | The DLL has 16 slots and gives them by the priority of the bank, not by what the head hears | Section 18 takes most small sounds out of range at a far zoom. A later step can stop the quietest sample when a louder one finds no slot |
| The pause of the game and its menu have no desktop run | The fake Esc needs the game window in front | In the headset: open the menu while a loop plays. The voices must hold and come back |

## 21. Headset test of the sound of the world (10 minutes)

Use a game with the Creature. The view starts at the door of the Temple with
the Creature in front. Ctrl Shift F12 (with test keys on) steps the sound
back, one change for each press, and the fifth press is the new sound again.

1. **Height (2 minutes).** Stand still and look ahead. The Creature is on
   the land at your waist. Its steps and its voice must come from below and
   ahead, not from the height of your ears. Listen to a bird: it must be at
   the height of your ears or over them. Press Ctrl Shift F12 two times for
   QMixer's sound, and three times more for the new sound. If the sounds of
   the land are too dull, set `audio_height_pct=60`. If the height is not
   clear, set 150.
2. **Near (1 minute).** Put your head 20 cm from the Creature, or hold a
   thing that makes a sound near one ear. The sound must stay in both ears
   and not sit hard in one.
3. **The bed (1 minute).** Go to the shore. Note in which ear the sea is.
   Turn the view with the stick, 4 steps. The sea must stay where it is in
   the world, so after 4 steps it is in the other ear. Turn your head: the
   same. A single wave must come from the side of the sea.
4. **The zoom (2 minutes).** Stand in a village. Zoom out step by step to
   the far end. The voices and the small sounds must go away. Throw a rock at
   a house, or cast a miracle: the crash must carry, low and dull. Zoom in
   again: the small sounds come back.
5. **Distance (1 minute).** Teleport away from a sound that loops (a fire,
   the Temple, a vortex) and back. It must get quiet and dull with the
   distance, and it must be there again when you come back, with no cut.
6. **The cues (1 minute).** A turn step, a zoom step, a teleport, and the
   left grip on the land. Each one has a soft sound. `audio_cues_pct` sets
   their level, and `audio_cues=0` turns them off.
7. **The advisors (1 minute).** They must sound as before. So must the
   speech of a story.
8. **All the time:** a click, a sound from a wrong side, a sound that lags
   a fast turn of the head. Say when, and send `bwvr.log`. The lines
   `audio mixer`, `audio range` and `audio cues` tell what played.
9. **If the sound lags a turn of the head:** set `audio_latency_ms=25` and
   play again. The log line "QMixer writes the stream" must then say about
   66 ms, and "passed a write 0 times". A crackle or a count over 0 means
   that 25 is too low for the headset: go back to 0.
