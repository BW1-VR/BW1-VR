// Flat sounds that have a place in the world come from
// that place.
//
// THE ADVISORS. They speak through HelpDude::PlaySample (0x005BB530) with
// the 3D flag off, so LHaudiodllR.dll plays them flat. It places the QMixer
// channel with QSWaveMixSetPolarPosition, at an angle from a fixed pan, and a
// range of 1.0. Turning the 3D flag on would make the game read the number
// 0x270C as a pointer in its object callback, and that crashes. So the flag
// stays off.
//
// OTHER FLAT SOUNDS (docs/audio.md, "6.5 Sounds the game plays flat"). Two kinds have a place:
//
// - A game effect with the 3D flag off and an object that has a position
//   (Base::Get3DSoundPos, vtable offset 0x10). The point is read once, when
//   the sound starts. A flat sound has no object callback, so the game does not
//   promise that the object lives as long as the sound.
// - A sound of the atmosphere with a position relative to the listener
//   (options + 0x14), for example a bird or a wave. The DLL picks a random
//   direction around the ears and keeps it relative to the ears, so it turns
//   with the head. Here it gets a point in the world in that direction (on a
//   level ring, see Choose) when it starts, kAtmosRoomMeters away in the
//   room, and it stays there. Only on the
//   table and in the God view: in a story the camera flies, and the sound
//   stays as the game made it.
//
// This file hooks LHSamplePlay, the one function that every sample reaches,
// also the samples of the atmosphere and of the animations, which start inside
// the DLL. It keeps the sample info and the place of each sound above. Every
// rendered frame it works out the azimuth (and for the other flat sounds the
// elevation) of the place in the frame of the head, and it sends that to the
// same channel with the same polar call the DLL uses. The range stays what the DLL sent, so the
// level does not change, and no channel changes its mode. Only the direction
// follows the room.
//
// When the DLL itself sends a new polar position to a steered channel (for
// example the turn update of a relative sound), the hook of
// QSWaveMixSetPolarPosition in audio_rolloff.cpp asks AudioSourcesPolar, and
// the direction of the place replaces the DLL's direction. The range stays.
//
// Why polar and not a world position: a flat channel is not configured for
// 3D, and the polar call is the one the DLL already uses on it. A polar
// position is relative to the listener, so the angle has to come from the
// head, which is what this file computes. The spirit position is
// HelpDude + 0x3374. It is fixed in the room by spirit_anchor.cpp.
//
// All calls run on the game thread. The list is touched from there only.

#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "audio.h"
#include "audio_math.h"
#include "audio_object.h"

void ModLog(const char* format, ...);
float TabletopViewScale();
float HeadPoseScale();

namespace {

// The return addresses of the calls to LHSamplePlay. The game ones are
// absolute. The DLL ones are RVAs in LHaudiodllR.dll.
const uintptr_t kAdvisorReturn = 0x005BB54A;  // HelpDude::PlaySample
const uintptr_t kEffectReturn = 0x00429FE5;   // GAudio::PlaySoundEffect
const uintptr_t kAtmosReturnA = 0x1A87;       // LHAtmosProcess
const uintptr_t kAtmosReturnB = 0x1E41;       // LHAtmosProcess, second call
// The options object is embedded in the spirit, HelpDude + 0x367C.
const int kOptionsOffset = 0x367C;
// The world position of the spirit. It is the translation of its matrix.
const int kSpiritPosition = 0x3374;
// The key the game stores in options + 0x20 for an advisor.
const int kAdvisorKey = 0x270C;

// LH_SamplePlayOptions fields.
const int kOptFlag3d = 0x08;
const int kOptRelative = 0x14;
const int kOptObject = 0x20;
const int kOptPosition = 0x30;
const int kOptOffset = 0x3C;
// LH_SampleInfo fields, from the DLL code at 0x1213F and 0x13E79.
const int kInfoObject = 0x18;   // the key, or the object of a positional sound
const int kInfoChannel = 0x2C;  // the QMixer channel
// LH_AudioSystem field: the QMixer session handle.
const int kSystemSession = 0x88;

// The game's code and its vtables, for the guard on an object.
const uintptr_t kExeTextStart = 0x00401000;
const uintptr_t kExeTextEnd = 0x008A9000;
const uintptr_t kExeRdataEnd = 0x009C6000;

// QSWaveMixSetPolarPosition(session, channel, flags, {azimuth, range,
// elevation}). The DLL passes flags 0x20 and, for a flat sound, the values
// {angle, 1.0, 0}. QMixer reads the two angles in degrees (0x0000AA85).
const DWORD kPolarFlags = 0x20;

// THE LAND OF EACH KIND OF ATMOSPHERE. GSoundMap (*(GGame + 0x250058)) keeps
// for each of the 14 ATMOS_TYPEs an AtmosMapTypeInfo of 12 bytes, from
// + 0x08: the count of cells of the type near the game camera (a word), the
// squared distance of the nearest one (a float), and its x and z in world
// units (two words). AtmosMapTypeInfo::Add 0x0071D510 writes them. The bank
// of type i is at GAudio + 0x194 + 4 i.
const uintptr_t kGame = 0x00D0195C;
const uintptr_t kGlobalAudio = 0x00CD3B20;
const int kGameSoundMap = 0x250058;
const int kSoundMapTypes = 0x08;
const int kAtmosTypes = 14;
const int kAtmosBanks = 0x194;
// A single sound goes toward the land of its kind when the nearest cell is
// at least this far from the ears, in meters of the room. Nearer, the land
// of the kind is all around the player, and the ring of the other flat sounds is right.
const float kTowardLeastMeters = 1.5f;
// The sound is this far to each side of the line to the cell, at most.
const float kTowardSpread = 0.45f;   // radians, 26 degrees

const int kMaxTracked = 24;
// A place closer to the head than this, on the ground plane, has no stable
// direction. The last angle stays.
const float kMinHorizontal = 0.05f;
// A sound of the atmosphere is put this far from the ears, in the room.
const float kAtmosRoomMeters = 3.0f;

typedef void*(__fastcall* SamplePlayFn)(void* self, void* unused,
                                        void* options);
typedef int(__fastcall* IsPlayingFn)(void* self, void* unused, void* info);
typedef DWORD(WINAPI* PolarFn)(void* session, int channel, DWORD flags,
                               const float* polar);
// Base::Get3DSoundPos. thiscall, ret 4. 1 when the object has a position.
typedef int(__fastcall* SoundPosFn)(const void* self, void* unused,
                                    float* out);

SamplePlayFn o_SamplePlay = nullptr;
IsPlayingFn f_IsPlaying = nullptr;
PolarFn f_Polar = nullptr;

enum Kind { kAdvisor, kEffect, kAtmos, kKinds };
const char* const kKindNames[kKinds] = {"advisor", "effect", "atmosphere"};

struct Tracked {
  void* info;           // LH_SampleInfo*
  int key;              // info + 0x18 when it started
  int channel;          // info + 0x2C when it started
  int kind;
  const uint8_t* dude;  // HelpDude*, for an advisor
  float point[3];       // the place in the world, for the other flat sounds
  float polar[3];       // what the DLL sent: azimuth, range, elevation
  float azimuth;        // the last angle sent
  float elevation;
  bool used;
};
Tracked g_tracked[kMaxTracked];

bool g_advisors = false;  // audio_advisors
bool g_flat = false;      // audio_flat_places
bool g_toward = true;     // audio_atmos_toward
uint64_t g_toward_count = 0;   // single sounds that went toward their land
uint64_t g_ring_count = 0;     // single sounds on the ring around the player
int g_toward_lines = 0;
bool g_hooked = false;
bool g_steering = false;  // some voice has the head angle now
bool g_sending = false;   // this file calls QMixer now
bool g_active = false;    // the head is fresh, from the last frame
// The play in progress, from the polar call of the DLL inside it.
bool g_in_play = false;
bool g_play_polar = false;
float g_play_polar_values[3];
int g_play_polar_channel = -1;
uint64_t g_plays[kKinds];
uint64_t g_updates = 0;
uint64_t g_failed = 0;
uint64_t g_replaced = 0;   // DLL polar calls that got the place's direction
DWORD g_last_log = 0;
DWORD g_last_report = 0;

bool Readable(const void* p, size_t n) {
  return p != nullptr && !IsBadReadPtr(p, n);
}

bool Finite3(const float* v) {
  for (int i = 0; i < 3; ++i) {
    if (!(v[i] == v[i]) || v[i] > 1e7f || v[i] < -1e7f) return false;
  }
  return true;
}

bool Alive(const Tracked& t, void* audio) {
  if (!t.used || t.info == nullptr) return false;
  const uint8_t* info = static_cast<const uint8_t*>(t.info);
  if (!Readable(info, kInfoChannel + 4)) return false;
  // The DLL reuses the info slots. The key and the channel tell whether this
  // one is still the same sound.
  if (*reinterpret_cast<const int*>(info + kInfoObject) != t.key ||
      *reinterpret_cast<const int*>(info + kInfoChannel) != t.channel) {
    return false;
  }
  if (f_IsPlaying != nullptr && audio != nullptr &&
      f_IsPlaying(audio, nullptr, t.info) == 0) {
    return false;
  }
  return true;
}

bool SendPolar(void* audio, const Tracked& t, float azimuth, float range,
               float elevation) {
  const uint8_t* system = static_cast<const uint8_t*>(audio);
  if (!Readable(system + kSystemSession, 4)) return false;
  void* session = *reinterpret_cast<void* const*>(system + kSystemSession);
  if (session == nullptr || t.channel < 0) return false;
  const float polar[3] = {azimuth, range, elevation};
  g_sending = true;
  const DWORD r = f_Polar(session, t.channel, kPolarFlags, polar);
  g_sending = false;
  ++g_updates;
  if (r != 0) {
    if (++g_failed <= 3) {
      ModLog("audio place  : the polar call failed, result %lu", r);
    }
    return false;
  }
  return true;
}

bool ReadSpirit(const uint8_t* dude, float out[3]) {
  if (dude == nullptr || !Readable(dude + kSpiritPosition, 12)) return false;
  memcpy(out, dude + kSpiritPosition, 12);
  return Finite3(out);
}

// The place of a tracked sound in the world now.
bool Place(const Tracked& t, float out[3]) {
  if (t.kind == kAdvisor) return ReadSpirit(t.dude, out);
  memcpy(out, t.point, 12);
  return true;
}

// One sound, one pose. Returns true when it sent an angle.
bool Steer(void* audio, Tracked& t, const float basis[9],
           const float head[3]) {
  float place[3];
  if (!Place(t, place)) return false;
  float distance = 0.0f;
  float azimuth = t.azimuth;
  if (!audio_math::Azimuth(basis, head, place, kMinHorizontal, &azimuth,
                           &distance)) {
    azimuth = t.azimuth;
  }
  t.azimuth = azimuth;
  // The advisors keep the DLL's elevation of 0.
  t.elevation =
      t.kind == kAdvisor ? 0.0f : audio_math::Elevation(basis, head, place);
  const float range = t.kind == kAdvisor ? 1.0f : t.polar[1];
  const bool sent = SendPolar(audio, t, azimuth, range, t.elevation);
  const DWORD now = GetTickCount();
  if (sent && now - g_last_log >= 1000) {
    g_last_log = now;
    ModLog("audio place  : %s at %.2f %.2f %.2f, head at %.2f %.2f %.2f, "
           "distance %.2f, azimuth %.1f, elevation %.1f degrees",
           kKindNames[t.kind], place[0], place[1], place[2], head[0], head[1],
           head[2], distance, azimuth, t.elevation);
  }
  return sent;
}

// Put a tracked sound back where the DLL placed it.
void Release(void* audio) {
  for (int i = 0; i < kMaxTracked; ++i) {
    Tracked& t = g_tracked[i];
    if (!t.used) continue;
    if (audio != nullptr && Alive(t, audio)) {
      SendPolar(audio, t, t.polar[0], t.polar[1], t.polar[2]);
    }
    t.used = false;
  }
  if (g_steering) {
    g_steering = false;
    ModLog("audio place  : the flat sounds are flat again, as the game plays "
           "them");
  }
}

// The point of an object as the game's sound code sees it, or false. The
// object came from the game a moment ago, so it lives. The guard checks only
// that it looks like an object of the game.
bool ObjectPoint(uintptr_t object, float out[3]) {
  if (object < 0x10000 || !Readable(reinterpret_cast<void*>(object), 4)) {
    return false;
  }
  const uintptr_t vt = *reinterpret_cast<const uintptr_t*>(object);
  if (vt < kExeTextEnd || vt >= kExeRdataEnd) return false;
  // Only a Base of the game has Get3DSoundPos in slot 0x10 (audio_object.h).
  if (!AudioVtableIsBase(vt)) return false;
  const uintptr_t* slot = reinterpret_cast<const uintptr_t*>(vt + 0x10);
  if (!Readable(slot, 4)) return false;
  const uintptr_t fn = *slot;
  if (fn < kExeTextStart || fn >= kExeTextEnd) return false;
  float p[3] = {0, 0, 0};
  if (reinterpret_cast<SoundPosFn>(fn)(reinterpret_cast<const void*>(object),
                                       nullptr, p) != 1) {
    return false;
  }
  if (!Finite3(p)) return false;
  memcpy(out, p, 12);
  return true;
}

float UnitsPerMeter() {
  const float table = TabletopViewScale();
  if (table > 0.0f) return table;
  const float life = HeadPoseScale();
  return life > 0.0f ? life : 7.5f;
}

// The direction on the ground from the ears to the nearest land of the kind
// of atmosphere that this bank plays, with the length 1. False when the
// kind has no cell near, or when its land is all around the player.
bool TowardLand(uintptr_t bank, const float head[3], float units_per_meter,
                float out[3], int* type, float* meters) {
  const uintptr_t* audio = reinterpret_cast<const uintptr_t*>(kGlobalAudio);
  const uintptr_t* game = reinterpret_cast<const uintptr_t*>(kGame);
  if (bank == 0 || !Readable(audio, 4) || *audio == 0 || !Readable(game, 4) ||
      *game == 0) {
    return false;
  }
  const uintptr_t* banks =
      reinterpret_cast<const uintptr_t*>(*audio + kAtmosBanks);
  if (!Readable(banks, 4 * kAtmosTypes)) return false;
  int index = -1;
  for (int i = 0; i < kAtmosTypes && index < 0; ++i) {
    if (banks[i] == bank) index = i;
  }
  if (index < 0) return false;
  const uintptr_t* map_slot =
      reinterpret_cast<const uintptr_t*>(*game + kGameSoundMap);
  if (!Readable(map_slot, 4) || *map_slot == 0) return false;
  const uint8_t* entry = reinterpret_cast<const uint8_t*>(
      *map_slot + kSoundMapTypes + 12 * index);
  if (!Readable(entry, 12)) return false;
  const int count = *reinterpret_cast<const uint16_t*>(entry);
  if (count <= 0) return false;
  const float x = *reinterpret_cast<const int16_t*>(entry + 8);
  const float z = *reinterpret_cast<const int16_t*>(entry + 10);
  const float dx = x - head[0], dz = z - head[2];
  const float length = sqrtf(dx * dx + dz * dz);
  *type = index;
  *meters = length / units_per_meter;
  if (!(length > kTowardLeastMeters * units_per_meter) || length > 20000.0f) {
    return false;
  }
  out[0] = dx / length;
  out[1] = 0.0f;
  out[2] = dz / length;
  return true;
}

// What the play is, and where its place is. False for a sound that stays as
// the game made it.
bool Choose(uintptr_t caller, const uint8_t* options, int* kind,
            float point[3]) {
  if (caller == kAdvisorReturn) {
    *kind = kAdvisor;
    return g_advisors;
  }
  if (!g_flat || !Readable(options, 0x48)) return false;
  const int flag3d = *reinterpret_cast<const int*>(options + kOptFlag3d);
  const int relative = *reinterpret_cast<const int*>(options + kOptRelative);
  const uintptr_t dll = reinterpret_cast<uintptr_t>(AudioAudioDll());
  if (caller == kEffectReturn) {
    // A positional effect is QMixer's job already.
    if (flag3d != 0) return false;
    *kind = kEffect;
    return ObjectPoint(
        *reinterpret_cast<const uintptr_t*>(options + kOptObject), point);
  }
  if (dll != 0 && (caller == dll + kAtmosReturnA ||
                   caller == dll + kAtmosReturnB)) {
    // Only while the table or the God view owns the view. A story moves
    // its camera through the world, and there the sound stays around the
    // ears, as the game made it.
    if (relative == 0 || !(TabletopViewScale() > 0.0f)) return false;
    *kind = kAtmos;
    // The direction the DLL picked. LHAtmosProcess (DLL RVA 0x1C2A) puts two
    // random values in + 0x30 and + 0x34 and 0 in + 0x38. The relative path
    // of LHSamplePlay (RVA 0x122BC) reads them as right, up and forward, so
    // half of the birds came from straight over or under the ears. Here the
    // second value is forward: the sounds are on a ring around the player at
    // the height of the ears. A position with a forward value of its own
    // keeps it.
    const float* p = reinterpret_cast<const float*>(options + kOptPosition);
    const float* o = reinterpret_cast<const float*>(options + kOptOffset);
    const float given[3] = {p[0] + o[0], p[1] + o[1], p[2] + o[2]};
    const float local[3] = {given[0], 0.0f,
                            given[2] != 0.0f ? given[2] : given[1]};
    float basis[9], head[3];
    if (!AudioHeadPose(basis, head)) return false;
    // THE SEA IS WHERE THE SEA IS. When the land of this kind of atmosphere
    // is to one side of the player, the sound goes toward it, with a little
    // spread from the random value of the DLL. Otherwise it is on the ring.
    float toward[3], meters = 0.0f;
    int type = -1;
    const float units = UnitsPerMeter();
    if (g_toward &&
        TowardLand(*reinterpret_cast<const uintptr_t*>(options + 0x04), head,
                   units, toward, &type, &meters)) {
      float side = 0.6f * given[0] + 0.4f * given[1];
      if (side > 1.0f) side = 1.0f;
      if (side < -1.0f) side = -1.0f;
      const float a = side * kTowardSpread;
      const float c = cosf(a), s = sinf(a);
      const float dx = toward[0] * c - toward[2] * s;
      const float dz = toward[0] * s + toward[2] * c;
      const float reach = kAtmosRoomMeters * units;
      point[0] = head[0] + dx * reach;
      point[1] = head[1];
      point[2] = head[2] + dz * reach;
      ++g_toward_count;
      if (AudioTraceOn() && g_toward_lines++ < 40) {
        ModLog("audio place  : a sound of the atmosphere of type %d goes "
               "toward the land of its kind, %.1f m away in the room (%.0f "
               "degrees from north, the sound %.0f)", type, meters,
               atan2f(toward[0], toward[2]) * 57.29578f,
               atan2f(dx, dz) * 57.29578f);
      }
      return true;
    }
    ++g_ring_count;
    return audio_math::PointFromEars(basis, head, local,
                                     kAtmosRoomMeters * units, point);
  }
  return false;
}

void* __fastcall h_SamplePlay(void* self, void* unused, void* options) {
  const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  AudioNoteSystem(self);
  const bool trace = AudioTraceOn();
  if (trace) {
    AudioTracePlayBegin(
        self, options, caller,
        static_cast<const uintptr_t*>(_AddressOfReturnAddress()));
  }
  // The place is read before the play, while the object is surely alive.
  int kind = kEffect;
  float point[3] = {0, 0, 0};
  const bool chosen = g_hooked && options != nullptr &&
                      Choose(caller, static_cast<const uint8_t*>(options),
                             &kind, point);
  AudioRolloffPlayBegin(options);
  {
    const uintptr_t base = reinterpret_cast<uintptr_t>(AudioAudioDll());
    const int who =
        caller == kAdvisorReturn ? kAudioAdvisor
        : caller == kEffectReturn ? kAudioEffect
        : base != 0 && (caller == base + kAtmosReturnA ||
                        caller == base + kAtmosReturnB) ? kAudioAtmos
                                                        : kAudioOther;
    AudioVoicesPlayBegin(who, options);
    if (chosen && kind == kAtmos) AudioVoicesPlayPlace(point);
  }
  g_in_play = true;
  g_play_polar = false;
  g_play_polar_channel = -1;
  void* info = o_SamplePlay(self, unused, options);
  g_in_play = false;
  AudioVoicesPlayEnd();
  AudioRolloffPlayEnd();
  AudioRangeNotePlay(info);
  if (trace) AudioTracePlayEnd(self, info);
  if (info == nullptr) return info;

  const uint8_t* i = static_cast<const uint8_t*>(info);
  if (!Readable(i, kInfoChannel + 4)) return info;
  const int channel = *reinterpret_cast<const int*>(i + kInfoChannel);
  // A new sound in an info slot or on a channel ends the old one there.
  for (int n = 0; n < kMaxTracked; ++n) {
    Tracked& t = g_tracked[n];
    if (t.used && (t.info == info || t.channel == channel)) t.used = false;
  }
  if (!chosen) return info;
  // The flat path always places the sound with a polar call. Without one,
  // the DLL treated this sound as positional after all (a bank flag), and
  // QMixer places it.
  if (kind != kAdvisor &&
      (!g_play_polar || g_play_polar_channel != channel)) {
    return info;
  }
  ++g_plays[kind];
  int slot = -1;
  for (int n = 0; slot < 0 && n < kMaxTracked; ++n) {
    if (!g_tracked[n].used) slot = n;
  }
  if (slot < 0) {
    // Full: the oldest is very likely finished.
    slot = static_cast<int>(g_plays[kind] % kMaxTracked);
  }
  Tracked& t = g_tracked[slot];
  t.info = info;
  t.key = *reinterpret_cast<const int*>(i + kInfoObject);
  t.channel = channel;
  t.kind = kind;
  t.dude = kind == kAdvisor
               ? static_cast<const uint8_t*>(options) - kOptionsOffset
               : nullptr;
  memcpy(t.point, point, 12);
  if (g_play_polar) {
    memcpy(t.polar, g_play_polar_values, 12);
  } else {
    t.polar[0] = 0.0f;
    t.polar[1] = 1.0f;
    t.polar[2] = 0.0f;
  }
  t.azimuth = t.polar[0];
  t.elevation = t.polar[2];
  t.used = true;

  if (kind == kAdvisor) {
    float spirit[3] = {0, 0, 0};
    ReadSpirit(t.dude, spirit);
    ModLog("audio adv    : line %llu starts, sample %d, spirit at %.2f %.2f "
           "%.2f, channel %d", g_plays[kAdvisor],
           *reinterpret_cast<const int*>(static_cast<const uint8_t*>(options) +
                                         0x24),
           spirit[0], spirit[1], spirit[2], channel);
  }

  // Place it now, so the sound does not start flat for a few milliseconds.
  float basis[9], head[3];
  void* audio = AudioSystem();
  if (audio != nullptr && AudioHeadPose(basis, head)) {
    if (Steer(audio, t, basis, head) && !g_steering) {
      g_steering = true;
      ModLog("audio place  : the flat sounds with a place follow the room and "
             "the head");
    }
  }
  return info;
}

}  // namespace

bool AudioSourcesInstall(bool advisors, bool flat, bool hook) {
  g_advisors = false;
  g_flat = false;
  if (!advisors && !flat && !hook) return false;
  HMODULE dll = AudioAudioDll();
  HMODULE mixer = AudioMixerDll();
  if (dll == nullptr || mixer == nullptr) {
    ModLog("audio place  : QMixer or the audio DLL is missing, the flat "
           "sounds stay flat");
    return false;
  }
  f_Polar = reinterpret_cast<PolarFn>(
      GetProcAddress(mixer, "QSWaveMixSetPolarPosition"));
  f_IsPlaying = reinterpret_cast<IsPlayingFn>(GetProcAddress(
      dll, "?LHSampleIsPlaying@LH_AudioSystem@@QAEHPAULH_SampleInfo@@@Z"));
  void* target = GetProcAddress(
      dll, "?LHSamplePlay@LH_AudioSystem@@QAEPAULH_SampleInfo@@PAVLH_"
           "SamplePlayOptions@@@Z");
  if (f_Polar == nullptr || f_IsPlaying == nullptr || target == nullptr) {
    ModLog("audio place  : an export is missing (polar %p, is playing %p, "
           "play %p), the flat sounds stay flat", f_Polar, f_IsPlaying,
           target);
    return false;
  }
  MH_STATUS s = MH_CreateHook(target, reinterpret_cast<void*>(&h_SamplePlay),
                              reinterpret_cast<void**>(&o_SamplePlay));
  if (s == MH_OK) s = MH_EnableHook(target);
  ModLog("audio adv    : LHSamplePlay at %p %s", target,
         s == MH_OK ? "ok" : "FAILED");
  g_hooked = s == MH_OK;
  g_advisors = advisors && g_hooked;
  g_flat = flat && g_hooked;
  g_toward = AudioIni("audio_atmos_toward", 1) != 0;
  ModLog("audio place  : advisors %s, flat effects and the atmosphere %s",
         g_advisors ? "steered" : "flat", g_flat ? "steered" : "flat");
  return g_hooked;
}

// From the polar hook of audio_rolloff.cpp, before the original call. In a
// play it notes what the DLL sends. Outside a play, on a steered channel, it
// puts the direction of the place in the call and keeps the range.
void AudioSourcesPolar(int channel, float polar[3]) {
  if (g_sending) return;
  if (g_in_play) {
    g_play_polar = true;
    g_play_polar_channel = channel;
    memcpy(g_play_polar_values, polar, 12);
    return;
  }
  if (!g_active) return;
  for (int n = 0; n < kMaxTracked; ++n) {
    Tracked& t = g_tracked[n];
    if (!t.used || t.channel != channel || t.kind == kAdvisor) continue;
    t.polar[0] = polar[0];
    t.polar[1] = polar[1];
    t.polar[2] = polar[2];
    polar[0] = t.azimuth;
    polar[2] = t.elevation;
    ++g_replaced;
    return;
  }
}

// Once per rendered frame. Active means the head is fresh and the switch is
// on. Otherwise every sound goes back to the flat place, once.
void AudioSourcesFrame(bool active, const float basis[9],
                       const float position[3]) {
  if (!g_hooked) return;
  g_active = active && (g_advisors || g_flat);
  void* audio = AudioSystem();
  if (!g_active) {
    Release(audio);
    return;
  }
  if (audio == nullptr) return;
  for (int i = 0; i < kMaxTracked; ++i) {
    Tracked& t = g_tracked[i];
    if (!t.used) continue;
    if (!Alive(t, audio)) {
      t.used = false;
      continue;
    }
    if (Steer(audio, t, basis, position) && !g_steering) {
      g_steering = true;
      ModLog("audio place  : the flat sounds with a place follow the room and "
             "the head");
    }
  }
  const DWORD now = GetTickCount();
  if (g_last_report == 0) g_last_report = now;
  if (now - g_last_report >= 10000) {
    g_last_report = now;
    ModLog("audio place  : started with a place: %llu advisor lines, %llu "
           "effects, %llu sounds of the atmosphere (%llu toward the land of "
           "their kind, %llu on the ring). %llu polar calls sent, %llu calls "
           "of the DLL turned to the place",
           g_plays[kAdvisor], g_plays[kEffect], g_plays[kAtmos],
           g_toward_count, g_ring_count, g_updates, g_replaced);
  }
}
