// The mixer of the mod (docs/audio.md, "7. The mixer of the mod").
//
// QMixer pans a positional sound between two speakers. It has no cue for up
// and down, a near sound sits hard in one ear, and a far sound is only
// quieter. To change that, the mod must have the signal of each source. This
// file takes it where the audio DLL gives it to QMixer:
//
// - QSWaveMixPlayEx gets the wave of a sample: 16 bit PCM that the DLL owns
//   (MIXWAVE: the format at +0x00, the data at +0x10, the bytes at +0x14).
//   For a sample with a place, the hook copies the wave and starts a voice of
//   the mod on it. QMixer plays the same channel with the volume 0. So QMixer
//   stays the clock: it still tells the DLL when the sample is done, and the
//   game's logic does not change.
// - The voice follows the channel: the hooks on SetVolume, SetFrequency,
//   StopChannel, FlushChannel, PauseChannel and RestartChannel move it, and
//   the hooks of audio_rolloff.cpp give it the place and the distance
//   mapping of the game.
// - audio_capture.cpp calls Render for each block that QMixer gives to the
//   sound device. Render adds the voices to the block, each one through the
//   model of audio_spatial.h: the direction with its height for two ears,
//   the near field, and the low pass by distance.
//
// WHAT STAYS WITH QMIXER: each flat sound, so the advisors, the story
// speech, the interface, and the music. They sound as in the plain game.
//
// SAFE BY DEFAULT. A voice starts only when the stream is 16 bit stereo and
// the wave is 16 bit PCM. In each other case QMixer plays the sample as in
// the plain game. Each level and each filter moves in small steps (15 ms), a
// stop fades in 8 ms, and the sum is kept inside 16 bits. The waves are copies
// in buffers that the slots keep and use again.
//
// Ini: audio_mixer (default 1, 0 is QMixer's own panning), audio_height,
// audio_near, audio_far, audio_bed, audio_atmos_mixer (each default 1),
// audio_height_pct (100, the strength of the height band, 0 to 200). Test
// only:
// audio_test_voice, audio_test_path, audio_test_step_ms, audio_test_solo.

#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MinHook.h"
#include "audio.h"
#include "audio_math.h"
#include "audio_spatial.h"

void ModLog(const char* format, ...);
float TabletopViewScale();
float HeadPoseScale();

namespace {

namespace sp = audio_spatial;

typedef DWORD(WINAPI* PlayExFn)(void*, int, DWORD, void*, int, void*);
typedef DWORD(WINAPI* Channel4Fn)(void*, int, DWORD, DWORD);
typedef DWORD(WINAPI* Channel3Fn)(void*, int, DWORD);
typedef DWORD(WINAPI* ActivateFn)(void*, BOOL);
// QSWaveMixIsChannelDone(session, channel). stdcall, ret 8. Not 0 when the
// channel plays nothing.
typedef BOOL(WINAPI* IsDoneFn)(void*, int);
IsDoneFn f_IsDone = nullptr;
DWORD g_last_watch = 0;
int g_watch_lines = 0;

PlayExFn o_PlayEx = nullptr;
Channel4Fn o_SetVolume = nullptr;
Channel4Fn o_SetFrequency = nullptr;
Channel3Fn o_Stop = nullptr;
Channel3Fn o_Pause = nullptr;
Channel3Fn o_Restart = nullptr;
Channel3Fn o_Flush = nullptr;
ActivateFn o_Activate = nullptr;

// MIXWAVE of QMixer (QSWaveMixPlayEx checks 0x48 bytes of it).
const int kWaveFormat = 0x00;   // PCMWAVEFORMAT
const int kWaveData = 0x10;     // the samples
const int kWaveBytes = 0x14;
const int kWaveSize = 0x48;
// The full volume of QSWaveMixSetVolume.
const float kFullVolume = 32767.0f;
const DWORD kVolumeFlags = 0x20;
// The level of a voice with a place at its minimum distance, for each ear.
// With 0.315 (QMixer's own number for a positional channel) the two ears have
// 7 dB less power than QMixer's two channels for the same sounds. With 0.7
// the power is the same for a source at the side, and QMixer is 1 to 2 dB
// louder for a source ahead: its pan gives a source ahead more power. 0.77 is
// between the two, within about 1 dB of QMixer in each direction.
const float kPlaceGain = 0.77f;
// A wave larger than this stays with QMixer.
const DWORD kMaxWaveBytes = 16u * 1024u * 1024u;
// A buffer larger than this many samples is freed when its voice ends. A
// slot keeps a smaller one for its next voice, so 40 slots keep 8 MB at most.
const int kKeepSamples = 100 * 1024;
const float kReleaseSeconds = 0.008f;
// A voice this near its end, in seconds, plays to its end when QMixer stops
// its channel. QMixer is the clock, and the voice can be a block behind.
const float kTailSeconds = 0.30f;

const int kChannels = 128;
const int kSlots = 40;
const int kChunk = 512;

enum Kind { kPlace, kBed, kTest, kCue, kKinds };

struct Slot {
  bool used;
  int channel;          // the QMixer channel, or -1 when it has none now
  int kind;
  int16_t* data;        // the copy of the wave
  int capacity;         // samples that data can hold
  int frames;
  int channels;         // 1 or 2
  float wave_rate;      // Hz, with SetFrequency
  double cursor;        // frames
  int loops_left;       // more plays after this one, -1 without end
  bool paused;
  bool releasing;       // it fades out, then the slot is free
  float volume;         // 0..1, the volume of the channel
  float position[3];
  float map[3];         // the game's min, max and rolloff
  bool have_map;
  float fixed_gain;     // a level that does not change with distance, or -1
  float test_part;      // TEST: the part of the range, or -1
  float pan_left, pan_right;   // the two gains of a cue
  float fade;           // the release, 1 down to 0
  sp::Voice dsp;
  sp::Bed bed;
};

struct ChannelState {
  DWORD volume;         // the last volume that the DLL set
  DWORD volume_flags;
  DWORD frequency;      // the last frequency that the DLL set, or 0
  int frequency_play;   // the play in which it was set
  bool shadowed;        // a voice of the mod plays this channel's sample
  bool muted;           // QMixer has the volume 0 for it, from the mod
  float point[3];
  int point_play;       // the play in which the place was set
  float map[3];
  int map_play;         // the play in which the mapping was set
  bool map_positional;
  float polar_range;    // the range of the polar position of the DLL
  int polar_play;       // the play in which it was set
  int done_looks;       // looks in a row at which QMixer played nothing
};

struct Listener {
  float position[3];
  float basis[9];       // columns right, up and forward
  float k;              // the room factor
  float units_per_meter;
  bool room;            // the room distances apply: no story owns the view
  bool have;
};

CRITICAL_SECTION g_lock;
Slot g_slots[kSlots];
ChannelState g_channels[kChannels];
Listener g_listener;

bool g_on = false;          // audio_mixer, and the hooks are in
bool g_audible = true;      // the test key: false gives QMixer's panning
bool g_height = true;       // audio_height
float g_height_pct = 1.0f;  // audio_height_pct / 100
bool g_near = true;         // audio_near
bool g_far = true;          // audio_far
bool g_bed = true;          // audio_bed
bool g_atmos = true;        // audio_atmos_mixer
volatile LONG g_started_atmos = 0;
int g_solo = 0;             // audio_test_solo
void* g_session = nullptr;

// The play of LHSamplePlay in progress.
bool g_in_play = false;
int g_play = 0;             // goes up with each play
int g_play_kind = 0;
int g_play_flag3d = 0;
int g_play_relative = 0;
// A place that the mod made for this play: a single sound of the atmosphere
// (audio_sources.cpp).
bool g_play_has_place = false;
float g_play_place[3];

// The test voice.
int g_test_voice = 0;
int g_test_step_ms = 3000;
struct TestStep { float azimuth, elevation, meters, part; };
TestStep g_test_steps[24];
int g_test_count = 0;
int g_test_step = -1;
long g_test_frame = 0;      // the capture frame at which the step started
long g_test_seen = 0;       // the capture frame of the last frame
int g_test_slot = -1;
int g_test_wait = 0;

// For the report.
volatile LONG g_started[kKinds] = {0, 0, 0, 0};
volatile LONG g_refused_format = 0;
volatile LONG g_refused_slots = 0;
volatile LONG g_clipped = 0;
volatile LONG g_render_calls = 0;
double g_render_us = 0.0, g_render_worst_us = 0.0;
int g_most_voices = 0;
DWORD g_last_report = 0;
LONG g_reported = -1;
bool g_first_logged = false;

bool Readable(const void* p, size_t n) {
  return p != nullptr && !IsBadReadPtr(p, n);
}

struct Lock {
  Lock() { EnterCriticalSection(&g_lock); }
  ~Lock() { LeaveCriticalSection(&g_lock); }
};

float UnitsPerMeter() {
  const float table = TabletopViewScale();
  if (table > 0.0f) return table;
  const float life = HeadPoseScale();
  return life > 0.0f ? life : 7.5f;
}

Slot* SlotOfChannel(int channel) {
  for (int i = 0; i < kSlots; ++i) {
    if (g_slots[i].used && g_slots[i].channel == channel) return &g_slots[i];
  }
  return nullptr;
}

// What a voice with a place gets now. Under the lock.
sp::Target TargetOf(const Slot& s) {
  sp::Target t = {0.0f, 0.0f, 0.0f, 0.0f, 12000.0f, 0.0f, g_height_pct};
  const Listener& l = g_listener;
  float distance = 0.0f;
  sp::Aim aim = {0.0f, 0.0f, 0.0f};
  if (l.have) aim = sp::AimOf(l.basis, l.position, s.position, &distance);
  const float k = l.k > 0.0f ? l.k : 1.0f;
  const float design = distance / k;
  float gain = 1.0f, part = 0.0f;
  if (s.fixed_gain >= 0.0f) {
    gain = s.fixed_gain;
  } else if (s.have_map) {
    gain = audio_math::QMixerDistanceGain(design, s.map[0], s.map[1],
                                          s.map[2]);
    part = sp::RangePart(design, s.map[0], s.map[1]);
  }
  if (s.test_part >= 0.0f) part = s.test_part;
  const float meters =
      l.units_per_meter > 0.0f ? distance / l.units_per_meter : distance;
  const float focus = g_near ? sp::NearFocus(meters) : 1.0f;
  t.lateral = aim.lateral * focus;
  t.elevation = g_height ? aim.elevation : 0.0f;
  t.back = aim.back * focus;
  t.gain = s.volume * gain * kPlaceGain;
  // In a story the game puts its camera where it wants the speech to be
  // heard, so a far sound keeps its high frequencies there, as in the game.
  if (g_far && (l.room || s.test_part >= 0.0f)) {
    t.corner = sp::DistanceCorner(part);
    t.diffuse = sp::DiffuseShare(part);
  }
  return t;
}

// Reads `count` frames of the wave of a slot at the rate of the output, with
// a straight line between two samples. Mono goes to out[i]. Stereo goes to
// out[2 i] and out[2 i + 1], or to its mean when `mono` is set. Returns
// false when the wave ended in this chunk.
bool ReadWave(Slot* s, float rate, int count, bool mono, float* out) {
  const double step = s->wave_rate / rate;
  const int16_t* d = s->data;
  const int frames = s->frames;
  bool alive = true;
  for (int i = 0; i < count; ++i) {
    if (alive && s->cursor >= frames) {
      if (s->loops_left != 0) {
        if (s->loops_left > 0) --s->loops_left;
        s->cursor -= frames;
        if (s->cursor >= frames) s->cursor = 0.0;
      } else {
        alive = false;
      }
    }
    float a = 0.0f, b = 0.0f;
    if (alive) {
      const int i0 = static_cast<int>(s->cursor);
      const float part = static_cast<float>(s->cursor - i0);
      int i1 = i0 + 1;
      if (i1 >= frames) i1 = s->loops_left != 0 ? 0 : i0;
      if (s->channels == 1) {
        a = d[i0] + (d[i1] - d[i0]) * part;
        b = a;
      } else {
        a = d[2 * i0] + (d[2 * i1] - d[2 * i0]) * part;
        b = d[2 * i0 + 1] + (d[2 * i1 + 1] - d[2 * i0 + 1]) * part;
      }
      s->cursor += step;
    }
    if (mono || s->channels == 1) {
      out[i] = 0.5f * (a + b);
    } else {
      out[2 * i] = a;
      out[2 * i + 1] = b;
    }
  }
  return alive;
}

void EndSlot(Slot* s) {
  s->used = false;
  s->channel = -1;
}

// From audio_capture.cpp, on the mixer thread of QMixer: add the voices.
void Render(int16_t* stereo, int frames, int rate) {
  if (!g_on) return;
  LARGE_INTEGER start;
  QueryPerformanceCounter(&start);
  static float left[kChunk], right[kChunk], source[2 * kChunk];
  const float out_rate = static_cast<float>(rate);
  const float release = 1.0f / (kReleaseSeconds * out_rate);
  {
    Lock lock;
    const float yaw = g_listener.have ? sp::HeadYaw(g_listener.basis) : 0.0f;
    for (int done = 0; done < frames; done += kChunk) {
      const int n = frames - done < kChunk ? frames - done : kChunk;
      memset(left, 0, sizeof(float) * n);
      memset(right, 0, sizeof(float) * n);
      int voices = 0;
      for (int v = 0; v < kSlots; ++v) {
        Slot& s = g_slots[v];
        if (!s.used) continue;
        ++voices;
        // A paused voice fades first, and then it keeps its place.
        const bool hold = s.paused && !s.releasing;
        if (hold && (s.kind == kBed ? s.bed.gain_now : s.dsp.now.gain) <
                        1e-4f) {
          continue;
        }
        if (g_solo == 2 && s.kind != kTest && s.kind != kCue &&
            v != g_test_slot) {
          // TEST: the other voices keep their time and make no sound.
          if (!ReadWave(&s, out_rate, n, s.kind != kBed, source)) EndSlot(&s);
          continue;
        }
        bool alive = true;
        if (s.kind == kCue) {
          // A cue is flat. Its wave starts and ends at zero.
          alive = ReadWave(&s, out_rate, n, true, source);
          for (int i = 0; i < n; ++i) {
            left[i] += s.pan_left * source[i];
            right[i] += s.pan_right * source[i];
          }
        } else if (s.kind == kBed) {
          alive = ReadWave(&s, out_rate, n, false, source);
          float gain = hold ? 0.0f : s.volume;
          if (s.releasing) {
            // The level goes to 0 in the steps of ProcessBed. The slot is
            // free when it is there.
            s.fade -= release * n;
            gain = 0.0f;
            if (s.fade <= -9.0f) alive = false;
          }
          sp::ProcessBed(&s.bed, yaw, gain, out_rate, source, n, left, right);
        } else {
          alive = ReadWave(&s, out_rate, n, true, source);
          sp::Target t = TargetOf(s);
          if (hold) t.gain = 0.0f;
          if (s.releasing) {
            // The gain goes to 0 in the steps of Process. The slot is free
            // when it is there (80 ms, more than 5 time constants).
            s.fade -= release * n;
            t.gain = 0.0f;
            if (s.fade <= -9.0f) alive = false;
          }
          sp::Process(&s.dsp, t, out_rate, source, n, left, right);
        }
        if (!alive) EndSlot(&s);
      }
      if (voices > g_most_voices) g_most_voices = voices;
      if (g_solo != 0) memset(stereo + 2 * done, 0, sizeof(int16_t) * 2 * n);
      if (g_audible) {
        for (int i = 0; i < n; ++i) {
          const float mix[2] = {left[i], right[i]};
          for (int c = 0; c < 2; ++c) {
            const float sum = stereo[2 * (done + i) + c] + mix[c];
            const float bent = sp::SoftLimit(sum);
            if (bent != sum) InterlockedIncrement(&g_clipped);
            stereo[2 * (done + i) + c] = static_cast<int16_t>(
                bent + (bent < 0.0f ? -0.5f : 0.5f));
          }
        }
      }
    }
  }
  LARGE_INTEGER end, frequency;
  QueryPerformanceCounter(&end);
  QueryPerformanceFrequency(&frequency);
  const double us = 1e6 * static_cast<double>(end.QuadPart - start.QuadPart) /
                    static_cast<double>(frequency.QuadPart);
  g_render_us += us;
  if (us > g_render_worst_us) g_render_worst_us = us;
  InterlockedIncrement(&g_render_calls);
}

// A free slot with room for `frames` frames of `channels` channels, or
// null. Only the game thread starts a voice, so a free slot stays free
// until StartSlot, and the buffer can be made outside the lock.
Slot* TakeSlot(int frames, int channels) {
  Slot* s = nullptr;
  {
    Lock lock;
    for (int i = 0; i < kSlots && s == nullptr; ++i) {
      if (!g_slots[i].used && i != g_test_slot) s = &g_slots[i];
    }
  }
  if (s == nullptr) return nullptr;
  const int need = frames * channels;
  if (s->data == nullptr || s->capacity < need) {
    free(s->data);
    s->data = static_cast<int16_t*>(malloc(sizeof(int16_t) * need));
    s->capacity = s->data != nullptr ? need : 0;
    if (s->data == nullptr) return nullptr;
  }
  return s;
}

// Puts a filled slot in use. Under the lock, the last step of a start.
void StartSlot(Slot* s, int channel, int kind, int frames, int channels,
               float wave_rate, int loops) {
  s->channel = channel;
  s->kind = kind;
  s->frames = frames;
  s->channels = channels;
  s->wave_rate = wave_rate;
  s->cursor = 0.0;
  s->loops_left = loops;
  s->paused = false;
  s->releasing = false;
  s->fade = 1.0f;
  s->test_part = -1.0f;
  s->have_map = false;
  s->fixed_gain = -1.0f;
  sp::Reset(&s->dsp);
  sp::ResetBed(&s->bed);
  s->used = true;
}

// The voice of a channel ends. With `tail`, a voice near its own end plays
// to it. Under the lock.
void ReleaseChannel(int channel, bool tail) {
  Slot* s = SlotOfChannel(channel);
  if (s == nullptr) return;
  s->channel = -1;
  if (tail && s->loops_left == 0 && s->wave_rate > 0.0f &&
      (s->frames - s->cursor) / s->wave_rate < kTailSeconds) {
    return;
  }
  s->releasing = true;
}

DWORD WINAPI h_PlayEx(void* session, int channel, DWORD flags, void* wave,
                      int loops, void* params) {
  AudioCaptureSession(session);
  g_session = session;
  bool shadow = false;
  const bool valid = channel >= 0 && channel < kChannels;
  if (g_on && valid) {
    ChannelState& c = g_channels[channel];
    {
      Lock lock;
      ReleaseChannel(channel, true);
    }
    c.shadowed = false;
    const uint8_t* w = static_cast<const uint8_t*>(wave);
    const int rate = AudioOutRate();
    // A place that was set in this play makes it a positional sample. The
    // bed of the atmosphere is the flat stereo wave of LHAtmosProcess.
    // Before the first ears are known a voice has no direction and no
    // distance, so QMixer plays the sample.
    const bool place = g_in_play && c.point_play == g_play &&
                       g_play_relative == 0 &&
                       g_play_kind != kAudioAdvisor && g_listener.have;
    // A single sound of the atmosphere (a bird, a wave) has a place in the
    // room from the mod. The DLL plays it with a polar position, and its
    // range in that call gives its level.
    const bool made = g_in_play && g_play_has_place && g_atmos &&
                      c.polar_play == g_play && !place;
    int kind = -1;
    if (rate > 0 && g_in_play && Readable(w, kWaveSize)) {
      const PCMWAVEFORMAT* f =
          reinterpret_cast<const PCMWAVEFORMAT*>(w + kWaveFormat);
      const uint8_t* data = *reinterpret_cast<uint8_t* const*>(w + kWaveData);
      const DWORD bytes = *reinterpret_cast<const DWORD*>(w + kWaveBytes);
      const int channels = f->wf.nChannels;
      const bool pcm = f->wf.wFormatTag == WAVE_FORMAT_PCM &&
                       f->wBitsPerSample == 16 &&
                       (channels == 1 || channels == 2) &&
                       f->wf.nSamplesPerSec >= 4000 &&
                       f->wf.nSamplesPerSec <= 96000;
      if (place || (made && pcm && channels == 1)) {
        kind = kPlace;
      } else if (g_bed && g_play_kind == kAudioAtmos && g_play_flag3d == 0 &&
                 pcm && channels == 2) {
        kind = kBed;
      }
      if (kind >= 0 && (!pcm || bytes < 8 || bytes > kMaxWaveBytes ||
                        !Readable(data, bytes))) {
        InterlockedIncrement(&g_refused_format);
        kind = -1;
      }
      if (kind >= 0) {
        const int frames = static_cast<int>(bytes / (2 * channels));
        Slot* s = TakeSlot(frames, channels);
        if (s == nullptr) {
          InterlockedIncrement(&g_refused_slots);
        } else {
          memcpy(s->data, data, sizeof(int16_t) * frames * channels);
          float wave_rate = static_cast<float>(f->wf.nSamplesPerSec);
          if (c.frequency_play == g_play && c.frequency >= 1000) {
            wave_rate = static_cast<float>(c.frequency);
          }
          Lock lock;
          StartSlot(s, channel, kind, frames, channels, wave_rate, loops);
          s->volume = static_cast<float>(c.volume) / kFullVolume;
          memcpy(s->position, c.point, sizeof(c.point));
          if (!place) {
            // The level that QMixer gives this range with the mapping of
            // the channel. The DLL sends 1, 9999, 4 for these sounds.
            const bool mapped = c.map_play == g_play;
            s->fixed_gain = audio_math::QMixerDistanceGain(
                c.polar_range, mapped ? c.map[0] : 1.0f,
                mapped ? c.map[1] : 9999.0f, mapped ? c.map[2] : 4.0f);
            memcpy(s->position, g_play_place, sizeof(g_play_place));
            InterlockedIncrement(&g_started_atmos);
          } else if (c.map_play == g_play && c.map_positional) {
            memcpy(s->map, c.map, sizeof(c.map));
            s->have_map = true;
          }
          shadow = true;
          InterlockedIncrement(&g_started[kind]);
        }
      }
    }
    // QMixer plays a shadowed channel with no sound. A channel that was
    // shadowed gets its volume back for a sample that QMixer plays.
    if (shadow) {
      c.shadowed = true;
      if (g_audible) {
        o_SetVolume(session, channel, kVolumeFlags, 0);
        c.muted = true;
      }
    } else if (c.muted) {
      o_SetVolume(session, channel, kVolumeFlags, c.volume);
      c.muted = false;
    }
  }
  const DWORD r = o_PlayEx(session, channel, flags, wave, loops, params);
  if (r != 0 && shadow) {
    Lock lock;
    Slot* s = SlotOfChannel(channel);
    if (s != nullptr) EndSlot(s);
    g_channels[channel].shadowed = false;
  }
  return r;
}

DWORD WINAPI h_SetVolume(void* session, int channel, DWORD flags,
                         DWORD volume) {
  if (g_on && channel >= 0 && channel < kChannels) {
    ChannelState& c = g_channels[channel];
    c.volume = volume;
    c.volume_flags = flags;
    if (c.shadowed) {
      {
        Lock lock;
        Slot* s = SlotOfChannel(channel);
        if (s != nullptr) {
          s->volume = static_cast<float>(volume) / kFullVolume;
        } else {
          c.shadowed = false;
        }
      }
      if (c.shadowed && g_audible) {
        c.muted = true;
        return o_SetVolume(session, channel, flags, 0);
      }
    }
    c.muted = false;
  }
  return o_SetVolume(session, channel, flags, volume);
}

DWORD WINAPI h_SetFrequency(void* session, int channel, DWORD flags,
                            DWORD frequency) {
  if (g_on && channel >= 0 && channel < kChannels) {
    ChannelState& c = g_channels[channel];
    c.frequency = frequency;
    c.frequency_play = g_in_play ? g_play : -1;
    if (c.shadowed && frequency >= 1000) {
      Lock lock;
      Slot* s = SlotOfChannel(channel);
      if (s != nullptr) s->wave_rate = static_cast<float>(frequency);
    }
  }
  return o_SetFrequency(session, channel, flags, frequency);
}

void StopVoice(int channel) {
  if (!g_on || channel < 0 || channel >= kChannels) return;
  Lock lock;
  ReleaseChannel(channel, true);
  g_channels[channel].shadowed = false;
}

DWORD WINAPI h_Stop(void* session, int channel, DWORD flags) {
  StopVoice(channel);
  return o_Stop(session, channel, flags);
}

DWORD WINAPI h_Flush(void* session, int channel, DWORD flags) {
  StopVoice(channel);
  return o_Flush(session, channel, flags);
}

void PauseVoice(int channel, bool paused) {
  if (!g_on || channel < 0 || channel >= kChannels) return;
  Lock lock;
  Slot* s = SlotOfChannel(channel);
  if (s != nullptr) s->paused = paused;
}

DWORD WINAPI h_Pause(void* session, int channel, DWORD flags) {
  PauseVoice(channel, true);
  return o_Pause(session, channel, flags);
}

DWORD WINAPI h_Restart(void* session, int channel, DWORD flags) {
  PauseVoice(channel, false);
  return o_Restart(session, channel, flags);
}

DWORD WINAPI h_Activate(void* session, BOOL active) {
  const DWORD r = o_Activate(session, active);
  if (active) AudioCaptureSession(session);
  return r;
}

bool Hook(HMODULE dll, const char* name, void* hook, void** original) {
  void* target = dll == nullptr ? nullptr : GetProcAddress(dll, name);
  if (target == nullptr) {
    ModLog("audio mixer  : %s not found", name);
    return false;
  }
  MH_STATUS s = MH_CreateHook(target, hook, original);
  if (s == MH_OK) s = MH_EnableHook(target);
  if (s != MH_OK) ModLog("audio mixer  : the hook on %s FAILED", name);
  return s == MH_OK;
}

// ---- The test voice --------------------------------------------------------

// TEST: a stereo wave with low noise at the left and high noise at the
// right, so a capture shows which channel is in which ear.
void FillTestBed(int16_t* out, int frames) {
  uint32_t seed = 4242u;
  float low_a = 0.0f, low_b = 0.0f, slow = 0.0f;
  for (int i = 0; i < frames; ++i) {
    seed = seed * 1664525u + 1013904223u;
    const float a =
        static_cast<float>(static_cast<int32_t>(seed)) / 2147483648.0f;
    seed = seed * 1664525u + 1013904223u;
    const float b =
        static_cast<float>(static_cast<int32_t>(seed)) / 2147483648.0f;
    low_a += 0.08f * (a - low_a);
    low_b += 0.08f * (low_a - low_b);
    slow += 0.45f * (b - slow);
    out[2 * i] = static_cast<int16_t>(low_b * 60000.0f);
    out[2 * i + 1] = static_cast<int16_t>((b - slow) * 9000.0f);
  }
}

// Noise with more level at the low end, 2 s, the same in each run.
void FillNoise(int16_t* out, int frames) {
  uint32_t seed = 12345u;
  float low = 0.0f, lower = 0.0f;
  for (int i = 0; i < frames; ++i) {
    seed = seed * 1664525u + 1013904223u;
    const float white =
        static_cast<float>(static_cast<int32_t>(seed)) / 2147483648.0f;
    low += 0.15f * (white - low);
    lower += 0.02f * (white - lower);
    const float v = 0.5f * white + 1.5f * low + 3.0f * lower;
    out[i] = static_cast<int16_t>(v * 9000.0f);
  }
}

void ParseTestPath(const char* text) {
  g_test_count = 0;
  const char* p = text;
  while (*p != 0 && g_test_count < 24) {
    TestStep step = {0.0f, 0.0f, 1.0f, -1.0f};
    const int got = sscanf_s(p, "%f,%f,%f,%f", &step.azimuth, &step.elevation,
                             &step.meters, &step.part);
    if (got >= 2) g_test_steps[g_test_count++] = step;
    // The steps have a slash between them: a run script splits its list of
    // keys at each semicolon.
    p = strpbrk(p, "/;");
    if (p == nullptr) break;
    ++p;
  }
}

// TEST ONLY. A voice of noise that goes through a list of places around the
// head. Each step writes a log line with the frame of the capture, and
// tools/audio_measure.py cuts the capture at these frames.
void TestVoiceFrame() {
  if (g_test_voice == 2 && AudioOutRate() != 0 && g_test_slot < 0 &&
      g_listener.have && TabletopViewScale() > 0.0f) {
    // The test bed: the view turns under it, and the capture shows it.
    const int frames = 44100;
    Slot* s = TakeSlot(frames, 2);
    if (s == nullptr) return;
    FillTestBed(s->data, frames);
    Lock lock;
    StartSlot(s, -1, kBed, frames, 2, 22050.0f, -1);
    s->volume = 1.0f;
    g_test_slot = static_cast<int>(s - g_slots);
    return;
  }
  if (g_test_voice != 1 || g_test_count == 0 || AudioOutRate() == 0) return;
  if (!g_listener.have || !(TabletopViewScale() > 0.0f)) {
    g_test_wait = 0;
    return;
  }
  // The steps count frames of the capture, so each one has its full time in
  // the file. QMixer gives no blocks while no channel plays, and then the
  // test waits.
  const long now = AudioCaptureFrames();
  const bool flows = now != g_test_seen;
  g_test_seen = now;
  if (!flows) return;
  if (++g_test_wait < 200) return;
  const long step_frames =
      static_cast<long>(g_test_step_ms) * AudioOutRate() / 1000;
  if (g_test_slot < 0) {
    const int frames = 44100;
    Slot* s = TakeSlot(frames, 1);
    if (s == nullptr) return;
    FillNoise(s->data, frames);
    Lock lock;
    StartSlot(s, -1, kTest, frames, 1, 22050.0f, -1);
    s->volume = 1.0f;
    g_test_slot = static_cast<int>(s - g_slots);
    g_test_step = -1;
    g_test_frame = now - step_frames;
  }
  if (g_test_step >= g_test_count) return;
  if (now - g_test_frame >= step_frames) {
    g_test_frame = now;
    ++g_test_step;
    if (g_test_step >= g_test_count) {
      Lock lock;
      g_slots[g_test_slot].releasing = true;
      ModLog("audio test   : step %d end, capture frame %ld", g_test_step,
             AudioCaptureFrames());
      return;
    }
    const TestStep& t = g_test_steps[g_test_step];
    ModLog("audio test   : step %d az%+.0f el%+.0f %.2fm part%.2f, capture "
           "frame %ld", g_test_step, t.azimuth, t.elevation, t.meters,
           t.part, AudioCaptureFrames());
  }
  // The place follows the head, so the direction is the one of the step.
  const TestStep& t = g_test_steps[g_test_step];
  const float az = t.azimuth / 57.29578f, el = t.elevation / 57.29578f;
  const float local[3] = {sinf(az) * cosf(el), sinf(el), cosf(az) * cosf(el)};
  Lock lock;
  Slot& s = g_slots[g_test_slot];
  const Listener& l = g_listener;
  const float reach = t.meters * l.units_per_meter;
  for (int i = 0; i < 3; ++i) {
    s.position[i] = l.position[i] +
                    (l.basis[i * 3 + 0] * local[0] +
                     l.basis[i * 3 + 1] * local[1] +
                     l.basis[i * 3 + 2] * local[2]) * reach;
  }
  s.test_part = t.part >= 0.0f ? t.part : 0.0f;
}

void Report() {
  const DWORD now = GetTickCount();
  if (g_last_report == 0) g_last_report = now;
  if (now - g_last_report < 30000) return;
  g_last_report = now;
  const LONG total = g_started[kPlace] + g_started[kBed];
  if (total == g_reported) return;
  g_reported = total;
  const LONG calls = InterlockedExchange(&g_render_calls, 0);
  const double mean = calls > 0 ? g_render_us / calls : 0.0;
  ModLog("audio mixer  : %ld sounds with a place (%ld of them single "
         "sounds of the atmosphere) and %ld beds of the "
         "atmosphere went through the mixer of the mod. %ld stayed with "
         "QMixer for their format and %ld for want of a slot. At most %d "
         "voices at one time. A block took %.0f us on average and %.0f us "
         "at most. %ld samples were over the knee of the limiter",
         g_started[kPlace], g_started_atmos, g_started[kBed],
         g_refused_format,
         g_refused_slots, g_most_voices, mean, g_render_worst_us, g_clipped);
  g_render_us = 0.0;
  g_render_worst_us = 0.0;
  float lead = 0.0f, most = 0.0f, least = 0.0f;
  long passed = 0;
  if (AudioCaptureLead(&lead, &most, &least, &passed)) {
    ModLog("audio mixer  : QMixer writes the stream %.0f ms ahead of the "
           "device on average (%.0f to %.0f ms). A turn of the head is heard "
           "that much later. The device passed a write %ld times (a gap in "
           "the sound)", lead, least, most, passed);
  }
}

}  // namespace

bool AudioVoicesInstall(bool on) {
  HMODULE q = AudioMixerDll();
  const bool activate = Hook(q, "QSWaveMixActivate",
                             reinterpret_cast<void*>(&h_Activate),
                             reinterpret_cast<void**>(&o_Activate));
  if (!on) {
    ModLog("audio mixer  : audio_mixer=0, QMixer pans each sound as before");
    return activate;
  }
  InitializeCriticalSection(&g_lock);
  for (int i = 0; i < kChannels; ++i) {
    g_channels[i].volume = static_cast<DWORD>(kFullVolume);
    g_channels[i].volume_flags = kVolumeFlags;
    g_channels[i].point_play = -1;
    g_channels[i].map_play = -1;
    g_channels[i].frequency_play = -1;
  }
  bool ok = true;
#define HOOK(name, hook, original) \
  ok &= Hook(q, name, reinterpret_cast<void*>(&hook), \
             reinterpret_cast<void**>(&original))
  HOOK("QSWaveMixSetVolume", h_SetVolume, o_SetVolume);
  HOOK("QSWaveMixSetFrequency", h_SetFrequency, o_SetFrequency);
  HOOK("QSWaveMixStopChannel", h_Stop, o_Stop);
  HOOK("QSWaveMixPauseChannel", h_Pause, o_Pause);
  HOOK("QSWaveMixRestartChannel", h_Restart, o_Restart);
  HOOK("QSWaveMixFlushChannel", h_Flush, o_Flush);
  if (ok) HOOK("QSWaveMixPlayEx", h_PlayEx, o_PlayEx);
#undef HOOK
  f_IsDone = reinterpret_cast<IsDoneFn>(
      GetProcAddress(q, "QSWaveMixIsChannelDone"));
  g_height = AudioIni("audio_height", 1) != 0;
  g_height_pct = sp::Clamp(AudioIni("audio_height_pct", 100) / 100.0f, 0.0f,
                           2.0f);
  g_near = AudioIni("audio_near", 1) != 0;
  g_far = AudioIni("audio_far", 1) != 0;
  g_bed = AudioIni("audio_bed", 1) != 0;
  g_atmos = AudioIni("audio_atmos_mixer", 1) != 0;
  g_solo = AudioIni("audio_test_solo", 0);
  g_test_voice = AudioIni("audio_test_voice", 0);
  g_test_step_ms = AudioIni("audio_test_step_ms", 3000);
  char path[512];
  AudioIniText("audio_test_path", path, sizeof(path));
  ParseTestPath(path);
  g_on = ok;
  if (ok) AudioCaptureSetRender(&Render);
  ModLog("audio mixer  : %s. Height %s, near field %s, far filter %s, the "
         "bed turns with the world %s",
         ok ? "the mixer of the mod plays the sounds with a place "
              "(audio_mixer=1)"
            : "a hook is missing, so QMixer pans each sound as before",
         g_height ? "on" : "off", g_near ? "on" : "off",
         g_far ? "on" : "off", g_bed ? "on" : "off");
  if (g_test_voice != 0) {
    ModLog("audio test   : TEST, a voice of noise goes through %d places, "
           "%d ms each, solo %d", g_test_count, g_test_step_ms, g_solo);
  }
  return ok;
}

void AudioVoicesPlayBegin(int kind, const void* options) {
  ++g_play;
  g_in_play = true;
  g_play_kind = kind;
  g_play_flag3d = 0;
  g_play_relative = 0;
  g_play_has_place = false;
  const uint8_t* o = static_cast<const uint8_t*>(options);
  if (Readable(o, 0x24)) {
    g_play_flag3d = *reinterpret_cast<const int*>(o + 0x08);
    g_play_relative = *reinterpret_cast<const int*>(o + 0x14);
  }
}

void AudioVoicesPlayEnd() { g_in_play = false; }

void AudioVoicesPlayPlace(const float point[3]) {
  memcpy(g_play_place, point, sizeof(g_play_place));
  g_play_has_place = true;
}

void AudioVoicesPolar(int channel, const float polar[3]) {
  if (!g_on || !g_in_play || channel < 0 || channel >= kChannels) return;
  g_channels[channel].polar_range = polar[1];
  g_channels[channel].polar_play = g_play;
}

void AudioVoicesSource(int channel, const float point[3]) {
  if (!g_on || channel < 0 || channel >= kChannels) return;
  ChannelState& c = g_channels[channel];
  memcpy(c.point, point, sizeof(c.point));
  if (g_in_play) c.point_play = g_play;
  if (!c.shadowed) return;
  Lock lock;
  Slot* s = SlotOfChannel(channel);
  if (s != nullptr) memcpy(s->position, point, sizeof(s->position));
}

void AudioVoicesMapping(int channel, const float game[3], bool positional) {
  if (!g_on || channel < 0 || channel >= kChannels) return;
  ChannelState& c = g_channels[channel];
  memcpy(c.map, game, sizeof(c.map));
  c.map_positional = positional;
  c.map_play = g_in_play ? g_play : -1;
}

void AudioVoicesListener(const float position[3], const float forward[3],
                         const float up[3]) {
  if (!g_on) return;
  // right = up x forward, for the left handed world of the game with y up.
  const float right[3] = {up[1] * forward[2] - up[2] * forward[1],
                          up[2] * forward[0] - up[0] * forward[2],
                          up[0] * forward[1] - up[1] * forward[0]};
  Lock lock;
  Listener& l = g_listener;
  for (int i = 0; i < 3; ++i) {
    l.position[i] = position[i];
    l.basis[i * 3 + 0] = right[i];
    l.basis[i * 3 + 1] = up[i];
    l.basis[i * 3 + 2] = forward[i];
  }
  l.have = true;
}

void AudioVoicesFrame() {
  if (!g_on) return;
  float k = 1.0f;
  const bool room = AudioRoomNow(&k);
  if (!room) k = 1.0f;
  const float units = UnitsPerMeter();
  {
    Lock lock;
    g_listener.k = k;
    g_listener.room = room;
    g_listener.units_per_meter = units;
    // A buffer that is much larger than the common sample goes back when its
    // voice has ended. So the memory of the mixer does not grow.
    for (int i = 0; i < kSlots; ++i) {
      Slot& s = g_slots[i];
      if (!s.used && s.data != nullptr && s.capacity > kKeepSamples &&
          i != g_test_slot) {
        free(s.data);
        s.data = nullptr;
        s.capacity = 0;
      }
    }
  }
  // QMIXER IS THE CLOCK, also for a loop. A loop of the mod has no end of
  // its own. If QMixer ended its channel with no stop call that the hooks
  // saw (a new session, a land that loads), the voice must end too, or it
  // plays without end. Two looks in a row must agree, because a channel is
  // empty for a moment between two plays.
  const DWORD tick = GetTickCount();
  if (f_IsDone != nullptr && g_session != nullptr &&
      tick - g_last_watch >= 500) {
    g_last_watch = tick;
    for (int channel = 0; channel < kChannels; ++channel) {
      ChannelState& c = g_channels[channel];
      if (!c.shadowed) {
        c.done_looks = 0;
        continue;
      }
      if (f_IsDone(g_session, channel) == FALSE) {
        c.done_looks = 0;
        continue;
      }
      if (++c.done_looks < 2) continue;
      c.done_looks = 0;
      bool loop = false;
      {
        Lock lock;
        Slot* s = SlotOfChannel(channel);
        if (s != nullptr && s->loops_left != 0) {
          loop = true;
          ReleaseChannel(channel, false);
        }
        if (s == nullptr || loop) c.shadowed = false;
      }
      if (loop && g_watch_lines++ < 20) {
        ModLog("audio mixer  : QMixer ended channel %d with no stop call, so "
               "the loop of the mod on it stops too", channel);
      }
    }
  }
  if (!g_first_logged && g_started[kPlace] + g_started[kBed] > 0) {
    g_first_logged = true;
    ModLog("audio mixer  : the first voice of the mod plays (the stream is "
           "%d Hz)", AudioOutRate());
  }
  TestVoiceFrame();
  Report();
}

// A cue of audio_cues.cpp: a short mono wave of 22050 Hz, flat, with a pan
// from -1 (left) to 1 (right). False when the mixer cannot play it now.
bool AudioVoicesCue(const int16_t* data, int frames, float gain, float pan) {
  if (!g_on || !g_audible || data == nullptr || frames <= 0 ||
      AudioOutRate() == 0) {
    return false;
  }
  Slot* s = TakeSlot(frames, 1);
  if (s == nullptr) return false;
  memcpy(s->data, data, sizeof(int16_t) * frames);
  const float angle = (sp::Clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * sp::kPi;
  Lock lock;
  StartSlot(s, -1, kCue, frames, 1, 22050.0f, 0);
  s->volume = 1.0f;
  s->pan_left = gain * cosf(angle) * 1.41421f;
  s->pan_right = gain * sinf(angle) * 1.41421f;
  InterlockedIncrement(&g_started[kCue]);
  return true;
}

// The test key. false gives each shadowed channel back to QMixer, at once,
// and the voices of the mod keep their time with no sound.
void AudioVoicesAudible(bool audible) {
  if (!g_on || audible == g_audible) return;
  g_audible = audible;
  if (g_session == nullptr) return;
  for (int i = 0; i < kChannels; ++i) {
    ChannelState& c = g_channels[i];
    if (!c.shadowed) continue;
    o_SetVolume(g_session, i, kVolumeFlags, audible ? 0 : c.volume);
    c.muted = audible;
  }
}
