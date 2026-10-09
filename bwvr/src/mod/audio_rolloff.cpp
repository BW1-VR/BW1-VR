// Room distances for the positional sounds (docs/audio.md, "6.3 Room
// distances").
//
// QMixer makes a positional sound quieter with its distance from the ears.
// The game gives each sound a distance mapping (min, max and a rolloff) with
// QSWaveMixSetDistanceMapping. It made those numbers for its own camera,
// with distances in world units. In the God view and on the table the world
// is s units for each meter of the room, and s goes from 7.5 to 4000. So the
// Creature 0.6 m from the head is 60 units away at the start. Its steps have
// the mapping 30, 80, 4 (docs/audio.md, "6.3 Room distances"): QMixer
// gives them 0.2 of full level there, and nothing past 0.8 m. Zoomed out,
// every near sound is silent.
//
// This file hooks QSWaveMixSetDistanceMapping. While the table or the God view
// owns the view and the ears are the head, it sends min and max times
// k = s / audio_units_per_meter (audio_math::RoomFactor). QMixer's gain is a
// function of distance / min and distance / max, so that is the same as all
// distances divided by k. One meter in the room then sounds as
// audio_units_per_meter world units of the game's design, at every zoom. With
// 40, the Creature's steps have full level within 0.75 m, the reach of an
// arm. When k changes, the frame driver sends the mapping again for each
// channel that plays a positional sound. In every other case the game's
// numbers go to QMixer unchanged.
//
// A flat sound does not get a mapping from the DLL. It keeps the last one of
// its channel, and it is placed at range 1.0. So when a flat sound starts on a
// channel that has a changed mapping, the hook on QSWaveMixSetPolarPosition
// puts the game's mapping back first. A sound with a position relative to the
// listener (options + 0x14, for example the birds of the atmosphere) keeps the
// game's mapping too, because its range is not a world distance.
//
// A SOUND AT THE LISTENER. A positional sound with no object (options + 0x20
// is 0) is at the game camera: the object callback of the game (0x004272F9)
// gives the camera position 0x00EA1DB8 for it, each game turn. When the ears
// are the game camera, such a sound is at the ears. With the ears on the
// head it would come from the game camera, in the God view up to a few meters behind
// and over the player. So the hook on QSWaveMixSetSourcePosition keeps the
// offset of such a sound from the game camera and puts it at the ears plus that
// offset, and the frame driver moves it with the ears. A sound that starts at
// a point of its own (the Hand over an object starts at the Hand) stays there
// until the game moves it to the camera, at the next game turn.
//
// The trace (audio_trace.cpp) gets the calls of the three hooks from here.
//
// Ini: audio_room (default 1), audio_units_per_meter (default 40).

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "audio.h"
#include "audio_math.h"

void ModLog(const char* format, ...);

namespace {

// QSWaveMixSetDistanceMapping(session, channel, flags, {size, min, max,
// rolloff}) and QSWaveMixSetPolarPosition(session, channel, flags, {azimuth,
// range, elevation}). stdcall, ret 0x10. The DLL passes the flags 0x20.
typedef DWORD(WINAPI* ChannelFn)(void*, int, DWORD, const void*);
ChannelFn o_Distance = nullptr;
ChannelFn o_Polar = nullptr;
ChannelFn o_Source = nullptr;

// The game camera position, three floats. The object callback reads it for a
// sound with no object (0x00427304).
const uintptr_t kGameCamera = 0x00EA1DB8;
// A sound that starts nearer than this to the game camera, on each axis, is
// at the camera from the start.
const float kAtCamera = 1.0f;

const DWORD kMappingFlags = 0x20;
const int kChannels = 128;
// The limits of k. They keep the numbers sane at both ends of the zoom.
const float kLowestFactor = 0.05f;
const float kHighestFactor = 100.0f;
// A new mapping goes out when k changes by more than this share.
const float kResendShare = 0.01f;

struct Mapping {
  DWORD size;
  float min_distance;
  float max_distance;
  float rolloff;
};

struct Channel {
  float game[3];     // the game's min, max and rolloff
  bool have;         // the game sent a mapping for this channel
  bool positional;   // the last sound on it has a world position
  float sent;        // the factor of the mapping QMixer has now
  bool glued;        // a positional sound with no object
  bool following;    // it is at the game camera now, so at the listener
  bool moved;        // QMixer has the ears plus offset, not the DLL's point
  float offset[3];   // its offset from the game camera
};
Channel g_ch[kChannels];

bool g_room = true;           // audio_room
float g_reference = 40.0f;    // audio_units_per_meter
float g_exponent = 0.5f;      // audio_zoom_pct / 100
bool g_linear = false;        // the test key: the same factor at each zoom
// The scale at which the zoom factor is the room factor: the start scale.
const float kPivotScale = 100.0f;
bool g_installed = false;
void* g_session = nullptr;
float g_factor = 1.0f;        // k now
bool g_active = false;        // the room distances apply in this frame
float g_logged_factor = 1.0f;

// The sample that LHSamplePlay starts now.
bool g_in_play = false;
bool g_play_relative = false;
bool g_play_glued = false;    // positional, no object, not relative
int g_play_mapped = -1;       // the channel that got a mapping in this play

uint64_t g_scaled = 0;        // mappings sent with k
uint64_t g_restored = 0;      // game mappings put back for a flat sound
uint64_t g_resent = 0;        // mappings sent again for a new k
uint64_t g_glued_plays = 0;   // positional sounds with no object
uint64_t g_glued_moves = 0;   // their points put at the ears

DWORD Send(void* session, int channel, const float game[3], float k) {
  Mapping m;
  m.size = sizeof(m);
  float scaled[3];
  audio_math::ScaleMapping(game, k, scaled);
  m.min_distance = scaled[0];
  m.max_distance = scaled[1];
  m.rolloff = scaled[2];
  return o_Distance(session, channel, kMappingFlags, &m);
}

DWORD WINAPI h_Distance(void* session, int channel, DWORD flags,
                        const void* mapping) {
  if (AudioTraceOn()) AudioTraceNoteDistance(channel, mapping);
  if (channel < 0 || channel >= kChannels) {
    return o_Distance(session, channel, flags, mapping);
  }
  Channel& c = g_ch[channel];
  if (!g_in_play) {
    // Not a sample: the music of the DLL sets its own mappings. The channel
    // is not this file's until a sample starts on it again.
    c.have = false;
    c.positional = false;
    c.glued = false;
    c.following = false;
    return o_Distance(session, channel, flags, mapping);
  }
  g_session = session;
  g_play_mapped = channel;
  c.glued = g_play_glued;
  c.following = false;
  if (!g_room || mapping == nullptr || IsBadReadPtr(mapping, 16) ||
      *static_cast<const DWORD*>(mapping) < 16) {
    c.have = false;
    return o_Distance(session, channel, flags, mapping);
  }
  memcpy(c.game, static_cast<const uint8_t*>(mapping) + 4, 12);
  c.have = true;
  c.positional = !g_play_relative;
  AudioVoicesMapping(channel, c.game, c.positional);
  const float k = c.positional ? g_factor : 1.0f;
  c.sent = k;
  if (k == 1.0f) return o_Distance(session, channel, flags, mapping);
  ++g_scaled;
  Mapping m;
  memcpy(&m, mapping, sizeof(m));
  m.size = sizeof(m);
  float scaled[3];
  audio_math::ScaleMapping(c.game, k, scaled);
  m.min_distance = scaled[0];
  m.max_distance = scaled[1];
  m.rolloff = scaled[2];
  return o_Distance(session, channel, flags, &m);
}

DWORD WINAPI h_Polar(void* session, int channel, DWORD flags,
                     const void* polar) {
  // audio_sources.cpp notes the polar call of a play, and turns a later call
  // on a steered channel to the place of its sound.
  float values[3];
  if (polar != nullptr && !IsBadReadPtr(polar, 12)) {
    memcpy(values, polar, 12);
    AudioVoicesPolar(channel, values);
    AudioSourcesPolar(channel, values);
    polar = values;
  }
  // A flat sound starts: it got no mapping in this play. Put the game's
  // mapping of the channel back before the DLL places it at range 1.0.
  if (g_in_play && channel != g_play_mapped && channel >= 0 &&
      channel < kChannels) {
    Channel& c = g_ch[channel];
    c.positional = false;
    c.glued = false;
    c.following = false;
    if (c.have && c.sent != 1.0f) {
      Send(session, channel, c.game, 1.0f);
      c.sent = 1.0f;
      ++g_restored;
    }
  }
  const DWORD r = o_Polar(session, channel, flags, polar);
  if (AudioTraceOn()) AudioTraceNotePolar(channel, polar);
  return r;
}

bool ReadCamera(float out[3]) {
  const float* camera = reinterpret_cast<const float*>(kGameCamera);
  if (IsBadReadPtr(camera, 12)) return false;
  memcpy(out, camera, 12);
  for (int i = 0; i < 3; ++i) {
    if (!(out[i] == out[i]) || out[i] > 1e7f || out[i] < -1e7f) return false;
  }
  return true;
}

DWORD WINAPI h_Source(void* session, int channel, DWORD flags,
                      const void* vec) {
  if (vec == nullptr || IsBadReadPtr(vec, 12) || channel < 0 ||
      channel >= kChannels) {
    const DWORD r = o_Source(session, channel, flags, vec);
    if (AudioTraceOn()) AudioTraceNoteSource(channel, vec, r);
    return r;
  }
  float point[3];
  memcpy(point, vec, 12);
  Channel& c = g_ch[channel];
  g_session = session;
  if (g_in_play) {
    c.glued = g_play_glued;
    c.following = false;
    c.moved = false;
    if (g_play_glued) ++g_glued_plays;
  }
  float camera[3], ears[3];
  if (c.glued && ReadCamera(camera)) {
    float offset[3];
    for (int i = 0; i < 3; ++i) offset[i] = point[i] - camera[i];
    // A start at a point of its own (for example the Hand over an object)
    // stays at that point. The game moves it to its camera at the next turn,
    // and from then on it is at the ears.
    const bool at_camera =
        !g_in_play || (fabsf(offset[0]) < kAtCamera &&
                       fabsf(offset[1]) < kAtCamera &&
                       fabsf(offset[2]) < kAtCamera);
    if (at_camera) {
      memcpy(c.offset, offset, sizeof(offset));
      c.following = true;
      if (AudioEarsPosition(ears)) {
        for (int i = 0; i < 3; ++i) point[i] = ears[i] + c.offset[i];
        c.moved = true;
        ++g_glued_moves;
      }
    }
  }
  const DWORD r = o_Source(session, channel, flags, point);
  AudioVoicesSource(channel, point);
  if (AudioTraceOn()) AudioTraceNoteSource(channel, point, r);
  return r;
}

bool HookExport(HMODULE dll, const char* name, void* hook, void** original) {
  void* target = dll == nullptr ? nullptr : GetProcAddress(dll, name);
  if (target == nullptr) {
    ModLog("audio room   : %s not found", name);
    return false;
  }
  MH_STATUS s = MH_CreateHook(target, hook, original);
  if (s == MH_OK) s = MH_EnableHook(target);
  ModLog("audio room   : %s at %p %s", name, target,
         s == MH_OK ? "ok" : "FAILED");
  return s == MH_OK;
}

}  // namespace

bool AudioRolloffInstall(bool room, int units_per_meter, bool others) {
  g_room = room;
  if (units_per_meter >= 1 && units_per_meter <= 100000) {
    g_reference = static_cast<float>(units_per_meter);
  }
  if (!room && !others) return false;
  HMODULE q = AudioMixerDll();
  const bool a = HookExport(q, "QSWaveMixSetDistanceMapping",
                            reinterpret_cast<void*>(&h_Distance),
                            reinterpret_cast<void**>(&o_Distance));
  const bool b = HookExport(q, "QSWaveMixSetPolarPosition",
                            reinterpret_cast<void*>(&h_Polar),
                            reinterpret_cast<void**>(&o_Polar));
  const bool c = HookExport(q, "QSWaveMixSetSourcePosition",
                            reinterpret_cast<void*>(&h_Source),
                            reinterpret_cast<void**>(&o_Source));
  g_installed = a && b && c;
  if (!g_installed) {
    g_room = false;
  }
  int zoom = AudioIni("audio_zoom_pct", 50);
  if (zoom < 20) zoom = 20;
  if (zoom > 100) zoom = 100;
  g_exponent = zoom / 100.0f;
  ModLog("audio room   : room distances %s, one meter sounds as %.0f units "
         "at %.0f units a meter. The factor grows with the scale to the "
         "power %.2f (audio_zoom_pct, 100 is the same at each zoom)",
         g_room ? "on" : "off", g_reference, kPivotScale, g_exponent);
  return g_installed;
}

void AudioRolloffPlayBegin(const void* options) {
  g_in_play = true;
  g_play_mapped = -1;
  g_play_relative = false;
  g_play_glued = false;
  const uint8_t* o = static_cast<const uint8_t*>(options);
  if (o != nullptr && !IsBadReadPtr(o, 0x24)) {
    g_play_relative = *reinterpret_cast<const int*>(o + 0x14) != 0;
    // The 3D flag (+0x08) on and no object (+0x20 is 0).
    g_play_glued = !g_play_relative &&
                   *reinterpret_cast<const int*>(o + 0x08) != 0 &&
                   *reinterpret_cast<const int*>(o + 0x20) == 0;
  }
}

void AudioRolloffPlayEnd() { g_in_play = false; }

bool AudioRoomActive(float units_per_meter) {
  return g_installed && g_room && units_per_meter > 0.0f;
}

void AudioRolloffSetLinear(bool linear) { g_linear = linear; }

bool AudioRoomNow(float* factor) {
  if (!g_installed || !g_room || !g_active) return false;
  *factor = g_factor;
  return true;
}

namespace {

// The QMixer session of the audio system now (LH_AudioSystem + 0x88). The
// hooks keep the session of the last call. This is the same handle, unless
// the DLL opened a new session.
void RefreshSession() {
  const uint8_t* system = static_cast<const uint8_t*>(AudioSystem());
  if (system == nullptr || IsBadReadPtr(system + 0x88, 4)) return;
  void* session = *reinterpret_cast<void* const*>(system + 0x88);
  if (session == nullptr || session == g_session) return;
  if (g_session != nullptr) {
    ModLog("audio room   : the QMixer session changed from %p to %p",
           g_session, session);
  }
  g_session = session;
}

}  // namespace

void AudioRolloffFrame(bool active, float units_per_meter) {
  if (!g_installed) return;
  g_active = active && g_room;
  RefreshSession();
  // The sounds at the listener follow the ears. With the ears on the game
  // camera they go back to the DLL's point.
  float ears[3], camera[3];
  const bool have_ears = AudioEarsPosition(ears);
  if (g_session != nullptr && ReadCamera(camera)) {
    for (int i = 0; i < kChannels; ++i) {
      Channel& c = g_ch[i];
      if (!c.glued || !c.following || (!have_ears && !c.moved)) continue;
      float point[3];
      for (int j = 0; j < 3; ++j) {
        point[j] = (have_ears ? ears[j] : camera[j]) + c.offset[j];
      }
      o_Source(g_session, i, 0, point);
      AudioVoicesSource(i, point);
      c.moved = have_ears;
    }
  }
  if (!g_room) return;
  const float k = active ? audio_math::ZoomFactor(
                               units_per_meter, g_reference, kPivotScale,
                               g_linear ? 1.0f : g_exponent, kLowestFactor,
                               kHighestFactor)
                         : 1.0f;
  g_factor = k;
  if (g_session != nullptr) {
    for (int i = 0; i < kChannels; ++i) {
      Channel& c = g_ch[i];
      if (!c.have || !c.positional) continue;
      const bool exact = k == 1.0f && c.sent != 1.0f;
      if (!exact && !audio_math::FactorChanged(c.sent, k, kResendShare)) {
        continue;
      }
      Send(g_session, i, c.game, k);
      c.sent = k;
      ++g_resent;
    }
  }
  if (audio_math::FactorChanged(g_logged_factor, k, 0.05f) ||
      (k == 1.0f && g_logged_factor != 1.0f)) {
    ModLog("audio room   : distances now divided by %.3f (%.1f units a "
           "meter, %s), mappings scaled %llu, put back %llu, sent again "
           "%llu. Sounds at the listener %llu, put at the ears %llu times",
           k, units_per_meter, active ? "the room" : "the game's own",
           g_scaled, g_restored, g_resent, g_glued_plays, g_glued_moves);
    g_logged_factor = k;
  }
}
