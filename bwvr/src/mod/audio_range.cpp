// The ears decide which positional sounds play (docs/audio.md, "6.4 The ears
// decide what plays").
//
// THE PROBLEM. The game decides what plays with its own camera, in world
// units:
//
// - GAudio::PlaySoundEffect (0x00429E30) does not start a positional sound
//   that is past its maximum distance from the game camera (0x00429F16).
// - LHSampleUpdate3DChannels (DLL RVA 0x14310) runs each game turn. It asks
//   the object callback of the game (0x00427200) for the distance of each
//   positional sample from the game camera, and it stops a sample that is not
//   nearer than its maximum distance (info + 0x6C).
//
// In the God view the game camera is 400 units from the player's place, and
// the ears are the head with room distances (audio_rolloff.cpp): QMixer hears
// a sound up to max * k from the head. So the game would stop a loop that the
// player is near. A sound tag (SoundTag, 0x0071E680) asks for its sound
// again each game turn, so its loop comes back when a teleport puts the game
// camera in range. The sound of a particle system is started one time
// (AtomCore::StartSound, 0x006745D0), and it stays away until the game makes
// the effect again.
//
// WHAT THIS FILE DOES. Three hooks, all on the game thread:
//
// - The object callback: while the ears are the head and the room distances
//   apply, the distance that the DLL tests is the distance from the ears
//   divided by k (audio_math::RangeDistance). With audio_loops_keep=1, a
//   loop that is out of range is not stopped while the DLL has spare sample
//   slots: QMixer makes it silent, and it is there at once when the head
//   comes back in range.
// - GAudio::PlaySoundEffect: the same distance decides the start. The hook
//   makes the test itself. For a sound in range it makes the game's test
//   pass, through the hook on GAudio::GetGSFXSampleMaxDistance.
// - GAudio::SamplePlayAnimEffect starts the effects of animations, of
//   physics and of particle systems. Its caller gives the distance from the
//   game camera, and the DLL does not start an effect past 800 units or past
//   the maximum of its sample (DLL RVA 0x14A83 to 0x14AC6). The hook gives
//   the distance from the ears divided by k.
// - A sound with no object is at the game camera, and audio_rolloff.cpp puts
//   it at the ears. It keeps the game's tests.
// - THE LOOP OF A BULK TAKE is a sound with no object at a place of its own,
//   the Hand. While the Hand takes from a pile or a field, the game asks for
//   it on each game turn (fn_0068F930, the call at 0x0068F9E8): sample 44
//   (food) or 98 (wood) of the in game bank, a loop with a range of 60 to
//   180 units. The game camera is 400 units away, so the game refuses each
//   ask. With audio_take_loop=1 the ears decide that start too
//   (audio_math::TakeLoopStart). The game stops the loop at the end of the
//   take (PSysGlobal::StopMultiPickup, 0x0068FA50).
//
// With the ears on the game camera, in a story, and with audio_range=0, the
// game's own tests apply, and this file only writes the log.
//
// THE LOG. One line for each change of a loop or of the sound of a sound
// tag: it starts, it goes out of range, it is in range again, it stops, or
// it is refused. Each line gives the distance from the head and from the
// game camera.
//
// The loop of a take has the tag "audio take": one line when it starts, with
// its place and the two distances, one line when the game or the head
// refuses it, and one line when it stops.
//
// Ini: audio_range (default 1), audio_loops_keep (default 1),
// audio_take_loop (default 1, with the test key of Ctrl, Shift and the
// equals key). Test only:
// audio_test_tag and audio_test_tag_bank put a sound tag on the Creature,
// and audio_test_bank lists the loops of a bank.

#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "audio.h"
#include "audio_math.h"

void ModLog(const char* format, ...);
float TabletopViewScale();
float HeadPoseScale();
void ControllersGesturePulse(float amplitude, float seconds);

namespace {

// GAudio::PlaySoundEffect(LH_SamplePlayOptions*). thiscall, ret 4.
const uintptr_t kPlayEffect = 0x00429E30;
// GAudio::GetGSFXSampleMaxDistance(sample, bank). thiscall, ret 8. The
// result is a float in st(0).
const uintptr_t kMaxDistance = 0x0042A430;
// The return address of its call in PlaySoundEffect.
const uintptr_t kMaxDistanceReturn = 0x00429EFC;
// The return address of the call of PlaySoundEffect for the loop of a bulk
// take (fn_0068F930, from PileResource::ProcessInInteract 0x0066E5F9).
const uintptr_t kTakeLoopReturn = 0x0068F9ED;
// The 3D object callback that the game gives to the DLL. cdecl.
const uintptr_t kObjectCallback = 0x00427200;
// GAudio::SamplePlayAnimEffect(object, distance, effect, a, bank, b, f1,
// f2). thiscall, ret 0x20. distance is the distance from the game camera.
const uintptr_t kPlayAnimEffect = 0x0042A4B0;
// GGlobal::Global holds the GAudio pointer. Its banks, one for each
// AUDIO_SFX_BANK_TYPE, are at +0x3A8.
const uintptr_t kGlobalAudio = 0x00CD3B20;
const int kAudioBanks = 0x3A8;
// LH_BankSample, 0x280 bytes for each sample, the first one has the number 1
// (DLL RVA 0x110A8). The flag 0x40 says that the bank gives the loop count.
const int kBankSampleSize = 0x280;
const int kBankSampleGroup = 0x118;
const int kBankSamplePriority = 0x240;
const int kBankSampleFlags = 0x244;
const int kBankSampleLoops = 0x248;
const int kBankSampleMin = 0x268;
const int kBankSampleMax = 0x26C;
const int kBankSampleMode = 0x274;
// The game camera position, three floats.
const uintptr_t kGameCamera = 0x00EA1DB8;
// The vtable of SoundTag.
const uintptr_t kSoundTagVtable = 0x00980538;
// SoundTag::Create(thing, sample, flag, mode, loops, a, 3D, bank type, b).
// cdecl. GScript::AttachSoundTag (0x007101B9) calls it with mode 2.
const uintptr_t kSoundTagCreate = 0x0071E840;
// GGame, its players and the Creature of a player.
const uintptr_t kGame = 0x00D0195C;
const int kGamePlayers = 0x18;
const int kGamePlayerSize = 0xA60;
const int kGameLocalPlayer = 0x205A59;
const int kPlayerCreature = 0xA4C;

// LH_SamplePlayOptions.
const int kOptBank = 0x04;
const int kOptFlag3d = 0x08;
const int kOptObject = 0x20;
const int kOptSample = 0x24;
const int kOptPosition = 0x30;
const int kOptOffset = 0x3C;
const int kOptLoops = 0x4C;
const int kOptMax = 0x58;
// LH_SampleInfo, from LHSamplePlay (DLL RVA 0x129BC to 0x12A94).
const int kInfoBank = 0x04;
const int kInfoObject = 0x18;
const int kInfoSample = 0x1C;
const int kInfoLoops = 0x40;   // the loop count that QSWaveMixPlayEx got
const int kInfoFlag3d = 0x48;
const int kInfoPosition = 0x50;
const int kInfoOffset = 0x5C;
const int kInfoMax = 0x6C;     // the maximum distance
const int kInfoUsed = 0x8C;
const int kInfoSize = 0x90;
// LH_AudioSystem: the first and the last sample info (DLL RVA 0x11104).
const int kSystemFirstInfo = 0x50;
const int kSystemLastInfo = 0x54;

// The maximum distance that makes the game's own start test pass.
const float kPass = 1e15f;

typedef int(__fastcall* PlayEffectFn)(void* self, void* unused,
                                      void* options);
typedef float(__fastcall* MaxDistanceFn)(void* self, void* unused,
                                         unsigned sample, void* bank);
typedef DWORD(__cdecl* ObjectFn)(void* info, float* x, float* y, float* z,
                                 float* distance, int* ok);
typedef int(__fastcall* IsPlayingFn)(void* self, void* unused, void* info);
typedef void*(__fastcall* PlayAnimFn)(void* self, void* unused, void* object,
                                      float distance, void* effect, int a,
                                      void* bank, int b, float f1, float f2);
typedef void*(__fastcall* FirstSampleFn)(void* bank, void* unused);
PlayAnimFn o_PlayAnim = nullptr;
int g_test_bank = -1;      // audio_test_bank: a bank type to list, or -1
bool g_test_bank_done = false;
uint64_t g_anim_tests = 0;     // effects that got the distance of the ears
uint64_t g_anim_nearer = 0;    // of those, the ears were nearer
typedef void*(__cdecl* TagCreateFn)(void* thing, unsigned sample, int flag,
                                    unsigned mode, unsigned loops, int a,
                                    int flag3d, int bank_type, int b);

PlayEffectFn o_PlayEffect = nullptr;
MaxDistanceFn o_MaxDistance = nullptr;
ObjectFn o_Object = nullptr;
IsPlayingFn f_IsPlaying = nullptr;

bool g_hooked = false;
bool g_by_ears = true;     // audio_range
bool g_keep_loops = true;  // audio_loops_keep
bool g_test_off = false;   // the test key: the game camera decides
int g_test_tag = 0;        // audio_test_tag: a sample number, or 0
int g_test_tag_bank = 1;   // audio_test_tag_bank: AUDIO_SFX_BANK_TYPE
bool g_test_tag_made = false;
int g_test_tag_frames = 0;

// The call of PlaySoundEffect in progress.
bool g_force = false;         // make the game's distance test pass
bool g_call_active = false;
bool g_call_reached = false;  // the game called LHSamplePlay
void* g_call_info = nullptr;

// THE LOOP OF A BULK TAKE.
enum TakeLoopState {
  kTakeLoopNone,
  kTakeLoopPlays,
  kTakeLoopOutOfRange,   // refused: out of range of the head
  kTakeLoopGameRefused,  // refused: the game camera is out of range
  kTakeLoopRule,         // refused by another test of the game
  kTakeLoopNoSlot        // the DLL gave no sample info
};
bool g_take_loop = true;        // audio_take_loop
bool g_take_loop_keys = false;  // test_keys
bool g_take_loop_key_down = false;
int g_take_loop_state = kTakeLoopNone;
void* g_take_loop_info = nullptr;  // the sample info while the loop plays
int g_take_loop_sample = 0;
DWORD g_take_loop_tick = 0;     // of the last ask of the game
DWORD g_take_loop_start = 0;    // of the start of the loop
uint64_t g_take_loop_asks = 0;  // asks of the game in this take
// The game asks on each game turn of a take, 100 ms. An ask that comes later
// than this after the last one is the first ask of a new take.
const DWORD kTakeLoopGapMs = 500;

// The sample slots of the DLL, counted at most each 100 ms.
int g_slots = 0;
int g_free_slots = 0;
DWORD g_slots_tick = 0;
bool g_slots_logged = false;

enum State {
  kNone,
  kPlays,         // it plays, and it is in range
  kSilent,        // a loop out of range that keeps its slot
  kOutStopped,    // stopped, out of range
  kRefusedRange,  // not started, out of range of the head
  kRefusedGame,   // not started, out of range of the game camera
  kRefusedRule,   // not started by another test of the game
  kNoSlot,        // the DLL gave no sample info
  kEnded,         // a loop that stopped, and not by its range
  kGone           // its object has no position now
};

// The facts of one test, for the log.
struct Facts {
  float point[3];
  float max;      // the maximum distance of the sample, in the game's units
  float k;        // the room factor
  bool by_ears;   // the ears and the room distances decide
  float head;     // world units from the ears, or -1
  float camera;   // world units from the game camera, or -1
};

// A loop, or the sound of a sound tag.
struct Sound {
  bool used;
  uintptr_t object;
  int sample;
  uintptr_t bank;
  void* info;      // while it plays
  bool loop;       // the DLL plays it with a loop count
  int loops;       // that count (info + 0x40)
  int state;
  DWORD tick;      // of the last change
  int lines;
  bool pending;    // its first start has no line yet
  bool told;       // the log has a line for it
  Facts last;      // the facts of the last test
  char cls[28];    // the class of the object, read while the object lived
};
const int kSounds = 48;
Sound g_sounds[kSounds];

// The loop of an animation starts and ends in a few seconds (a Creature
// that sniffs, a tree that moves). The first start of a loop gets its line
// when the loop has played this long. A loop that ends before gets no line.
const DWORD kToldAfterMs = 5000;
// TEST ONLY: the first object of the class Creature that has a loop.
uintptr_t g_seen_creature = 0;

const int kMaxLines = 600;
const int kMaxLinesPerSound = 60;
int g_lines = 0;

// For the report.
uint64_t g_tests = 0;            // start tests that the ears decided
uint64_t g_head_started = 0;     // started, and the game camera was too far
uint64_t g_head_refused = 0;     // refused, and the game camera was in range
uint64_t g_kept = 0;             // game turns of a silent loop that kept a slot
uint64_t g_cut_saved = 0;        // game turns of a sound in range of the head
                                 // that the game camera would stop
uint64_t g_reported = 0;
DWORD g_last_report = 0;
DWORD g_last_check = 0;

bool Readable(const void* p, size_t n) {
  return p != nullptr && !IsBadReadPtr(p, n);
}

int Int(const uint8_t* p) { return *reinterpret_cast<const int*>(p); }
float Float(const uint8_t* p) { return *reinterpret_cast<const float*>(p); }
uintptr_t Ptr(const uint8_t* p) {
  return *reinterpret_cast<const uintptr_t*>(p);
}

bool Finite(float v) { return v == v && v < 1e9f && v > -1e9f; }

bool ReadCamera(float out[3]) {
  const float* camera = reinterpret_cast<const float*>(kGameCamera);
  if (!Readable(camera, 12)) return false;
  memcpy(out, camera, 12);
  return Finite(out[0]) && Finite(out[1]) && Finite(out[2]);
}

float Distance(const float a[3], const float b[3]) {
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

float UnitsPerMeter() {
  const float table = TabletopViewScale();
  if (table > 0.0f) return table;
  const float life = HeadPoseScale();
  return life > 0.0f ? life : 7.5f;
}

// True when the ears and the room distances decide. Gives the ears and k.
bool ByEars(float ears[3], float* k) {
  if (!g_by_ears || g_test_off) return false;
  if (!AudioRoomNow(k)) return false;
  return AudioEarsPosition(ears);
}

// The distances of a point, for a test and for the log.
void Measure(Facts* f) {
  float ears[3], camera[3];
  f->k = 1.0f;
  f->by_ears = ByEars(ears, &f->k);
  float head[3];
  f->head = f->by_ears ? Distance(f->point, ears)
            : AudioEarsPosition(head) ? Distance(f->point, head) : -1.0f;
  f->camera = ReadCamera(camera) ? Distance(f->point, camera) : -1.0f;
}

void CountSlots() {
  const DWORD now = GetTickCount();
  if (g_slots != 0 && now - g_slots_tick < 100) return;
  g_slots_tick = now;
  const uint8_t* system = static_cast<const uint8_t*>(AudioSystem());
  if (!Readable(system + kSystemFirstInfo, 8)) return;
  const uintptr_t first = Ptr(system + kSystemFirstInfo);
  const uintptr_t last = Ptr(system + kSystemLastInfo);
  if (first == 0 || last < first || last - first > kInfoSize * 4096) return;
  int slots = 0, free_slots = 0;
  for (uintptr_t p = first; p <= last; p += kInfoSize) {
    const uint8_t* info = reinterpret_cast<const uint8_t*>(p);
    if (!Readable(info, kInfoSize)) return;
    ++slots;
    if (Int(info + kInfoUsed) == 0) ++free_slots;
  }
  g_slots = slots;
  g_free_slots = free_slots;
  if (!g_slots_logged) {
    g_slots_logged = true;
    ModLog("audio range  : the DLL has %d sample slots, %d are free. With "
           "audio_loops_keep=1, a loop out of range keeps its slot while %d "
           "or more are free",
           slots, free_slots, audio_math::SlotReserve(slots));
  }
}

bool StillPlays(const Sound& s) {
  const uint8_t* info = static_cast<const uint8_t*>(s.info);
  if (!Readable(info, kInfoSize)) return false;
  if (Int(info + kInfoUsed) != 1 || Ptr(info + kInfoObject) != s.object ||
      Int(info + kInfoSample) != s.sample) {
    return false;
  }
  void* system = AudioSystem();
  return f_IsPlaying == nullptr || system == nullptr ||
         f_IsPlaying(system, nullptr, s.info) != 0;
}

Sound* Find(uintptr_t object, int sample) {
  for (int i = 0; i < kSounds; ++i) {
    Sound& s = g_sounds[i];
    if (s.used && s.object == object && s.sample == sample) return &s;
  }
  return nullptr;
}

// The entry of a sound. A new one takes a free entry, or the entry of the
// sound that changed longest ago and does not play.
Sound* Track(uintptr_t object, int sample, uintptr_t bank) {
  Sound* s = Find(object, sample);
  if (s != nullptr) return s;
  Sound* oldest = nullptr;
  for (int i = 0; i < kSounds && s == nullptr; ++i) {
    Sound& c = g_sounds[i];
    if (!c.used) {
      s = &c;
    } else if (c.state != kPlays && c.state != kSilent &&
               (oldest == nullptr || c.tick < oldest->tick)) {
      oldest = &c;
    }
  }
  if (s == nullptr) s = oldest;
  if (s == nullptr) return nullptr;
  memset(s, 0, sizeof(*s));
  s->used = true;
  s->object = object;
  s->sample = sample;
  s->bank = bank;
  s->tick = GetTickCount();
  const char* cls = AudioClassName(object);
  strncpy_s(s->cls, cls != nullptr ? cls : "object", _TRUNCATE);
  char* at = strchr(s->cls, '@');
  if (at != nullptr) *at = 0;
  if (g_seen_creature == 0 && strcmp(s->cls, "Creature") == 0) {
    g_seen_creature = object;
  }
  return s;
}

// One log line about a sound. what is the event, in words.
void Line(Sound* s, const Facts& f, const char* what) {
  s->pending = false;
  s->told = true;
  if (g_lines >= kMaxLines || s->lines >= kMaxLinesPerSound) return;
  ++g_lines;
  ++s->lines;
  const float upm = UnitsPerMeter();
  char head[128] = "the ears are the game camera";
  if (f.head >= 0.0f) {
    _snprintf_s(head, sizeof(head), _TRUNCATE,
                "%.2f m from the head (%.0f units)%s", f.head / upm, f.head,
                f.by_ears ? "" : ", but the head does not decide");
  }
  char kind[32] = "sound";
  if (s->loop) {
    _snprintf_s(kind, sizeof(kind), _TRUNCATE, "loop (count %d)", s->loops);
  }
  ModLog("audio range  : %s %s %d of %s %s. At %.0f %.0f %.0f, %s. Range "
         "%.2f m (%.0f units times %.3f). The game camera is %.0f units from "
         "it. %.1f units a meter",
         kind, AudioBankName(s->bank), s->sample, s->cls, what, f.point[0],
         f.point[1], f.point[2], head, f.max * f.k / upm, f.max, f.k, f.camera,
         upm);
  if (g_lines == kMaxLines) {
    ModLog("audio range  : %d lines written, no more lines of this kind",
           kMaxLines);
  }
}

// A change of state, with one log line. The first start of a loop waits for
// kToldAfterMs, and a loop that ends before has no line at all.
void Change(Sound* s, int state, const Facts& f, const char* format, ...) {
  if (s == nullptr) return;
  s->last = f;
  if (s->state == state) return;
  const int before = s->state;
  s->state = state;
  s->tick = GetTickCount();
  // The stop test and the start test both see a sound that is out of range.
  const bool was_out = before == kOutStopped || before == kRefusedRange ||
                       before == kRefusedGame;
  const bool is_out = state == kOutStopped || state == kRefusedRange ||
                      state == kRefusedGame;
  if (was_out && is_out) return;
  if (state == kPlays && before == kNone && s->loop) {
    s->pending = true;
    return;
  }
  if ((state == kEnded || state == kGone) && !s->told) {
    s->used = false;
    return;
  }
  char what[160];
  va_list args;
  va_start(args, format);
  _vsnprintf_s(what, sizeof(what), _TRUNCATE, format, args);
  va_end(args);
  Line(s, f, what);
}

// ---- The start test --------------------------------------------------------

float __fastcall h_MaxDistance(void* self, void* unused, unsigned sample,
                               void* bank) {
  if (g_force &&
      reinterpret_cast<uintptr_t>(_ReturnAddress()) == kMaxDistanceReturn) {
    return kPass;
  }
  return o_MaxDistance(self, unused, sample, bank);
}

// THE LOOP OF A BULK TAKE: one ask of the game, on a game turn of a take.
// The ears decide the start (audio_math::TakeLoopStart). The log has one line
// for each change: the loop starts, or it is refused.
int PlayTakeLoop(void* self, void* options, int sample) {
  const uint8_t* o = static_cast<const uint8_t*>(options);
  const uintptr_t bank = Ptr(o + kOptBank);
  Facts f;
  for (int i = 0; i < 3; ++i) {
    f.point[i] = Float(o + kOptPosition + 4 * i) + Float(o + kOptOffset + 4 * i);
  }
  f.max = o_MaxDistance(self, nullptr, static_cast<unsigned>(sample),
                        reinterpret_cast<void*>(bank));
  if (f.max == 0.0f) f.max = Float(o + kOptMax);
  if (!Finite(f.point[0]) || !Finite(f.point[1]) || !Finite(f.point[2]) ||
      !Finite(f.max)) {
    return o_PlayEffect(self, nullptr, options);
  }
  Measure(&f);
  const DWORD now = GetTickCount();
  if (now - g_take_loop_tick > kTakeLoopGapMs &&
      g_take_loop_state != kTakeLoopPlays) {
    g_take_loop_state = kTakeLoopNone;
    g_take_loop_asks = 0;
  }
  g_take_loop_tick = now;
  ++g_take_loop_asks;
  const bool game_in_range =
      f.camera >= 0.0f && audio_math::RangeStarts(f.camera, f.max);
  const int rule = audio_math::TakeLoopStart(g_take_loop, f.by_ears, f.head,
                                             f.k, f.max);
  const bool by_head = rule == audio_math::kTakeLoopStart;
  int state = kTakeLoopOutOfRange;
  int r = 0;
  if (rule != audio_math::kTakeLoopRefuse) {
    g_force = by_head;
    g_call_active = true;
    g_call_reached = false;
    g_call_info = nullptr;
    r = o_PlayEffect(self, nullptr, options);
    g_force = false;
    g_call_active = false;
    if (g_call_reached) {
      state = g_call_info != nullptr ? kTakeLoopPlays : kTakeLoopNoSlot;
    } else {
      state = by_head || game_in_range ? kTakeLoopRule : kTakeLoopGameRefused;
    }
  }
  if (state == kTakeLoopPlays) {
    g_take_loop_info = g_call_info;
    g_take_loop_sample = sample;
  }
  if (state == g_take_loop_state) return r;
  g_take_loop_state = state;
  const float upm = UnitsPerMeter();
  char where[256];
  _snprintf_s(where, sizeof(where), _TRUNCATE,
              "%s %d, at %.0f %.0f %.0f. That place is %.2f m from the head "
              "(%.0f units), and the range of the sample is %.2f m (%.0f "
              "units times %.3f). The game camera is %.0f units from it",
              AudioBankName(bank), sample, f.point[0], f.point[1], f.point[2],
              f.head >= 0.0f ? f.head / upm : -1.0f, f.head,
              f.max * f.k / upm, f.max, f.k, f.camera);
  // With audio_capture=1 each line has the frame of the capture file, so a
  // measure of that file finds the take (bwvr/tools/audio_measure.py).
  char capture[48] = "";
  if (AudioCaptureFrames() > 0) {
    _snprintf_s(capture, sizeof(capture), _TRUNCATE, ", capture frame %ld",
                AudioCaptureFrames());
  }
  switch (state) {
    case kTakeLoopPlays:
      g_take_loop_start = now;
      ModLog("audio take   : the loop of the take starts: %s. %s "
             "(audio_take_loop=%d)%s", where,
             !by_head ? "The test of the game decided"
             : game_in_range ? "The head decided, and the game camera is in "
                               "range too"
                             : "The head decided. The game camera is past "
                               "the maximum of the sample, so the game "
                               "refuses this loop",
             g_take_loop ? 1 : 0, capture);
      break;
    case kTakeLoopOutOfRange:
      ModLog("audio take   : the loop of the take does not start: %s. The "
             "place is out of range of the head (audio_take_loop=1)", where);
      break;
    case kTakeLoopGameRefused:
      ModLog("audio take   : the game refuses the loop of the take: %s, past "
             "the maximum of the sample. The take has no sound "
             "(audio_take_loop=%d%s)%s", where, g_take_loop ? 1 : 0,
             g_take_loop ? ", and the ears do not decide in this view" : "",
             capture);
      break;
    case kTakeLoopRule:
      ModLog("audio take   : a rule of the game that is not the distance "
             "refuses the loop of the take: %s", where);
      break;
    default:
      CountSlots();
      ModLog("audio take   : the loop of the take does not start: %s. The "
             "DLL gave no sample slot (%d of %d are free)", where,
             g_free_slots, g_slots);
      break;
  }
  return r;
}

// Once a frame: the loop of a take that plays, and the test key.
void TakeLoopFrame() {
  if (g_take_loop_state == kTakeLoopPlays) {
    const uint8_t* info = static_cast<const uint8_t*>(g_take_loop_info);
    const bool plays = Readable(info, kInfoSize) &&
                       Int(info + kInfoUsed) == 1 &&
                       Ptr(info + kInfoObject) == 0 &&
                       Int(info + kInfoSample) == g_take_loop_sample;
    if (!plays) {
      const DWORD now = GetTickCount();
      g_take_loop_state = kTakeLoopNone;
      g_take_loop_info = nullptr;
      char capture[48] = "";
      if (AudioCaptureFrames() > 0) {
        _snprintf_s(capture, sizeof(capture), _TRUNCATE, ", capture frame %ld",
                    AudioCaptureFrames());
      }
      ModLog("audio take   : the loop of the take stopped, %lu ms after its "
             "start. The game asked for it %llu times, and its last ask was "
             "%lu ms before the stop%s", now - g_take_loop_start,
             g_take_loop_asks, now - g_take_loop_tick, capture);
      g_take_loop_asks = 0;
    }
  }
  // THE A/B KEY (test_keys=1): Ctrl and Shift with the equals key of the row
  // of digits change audio_take_loop. The plus key of the number pad is a
  // key of the throw. The pointing controller gives a long pulse for 1 and a
  // short pulse for 0. Only while a window of the game is in front.
  if (!g_take_loop_keys) return;
  const bool down = (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool pressed = down && !g_take_loop_key_down;
  g_take_loop_key_down = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_take_loop = !g_take_loop;
  ControllersGesturePulse(0.5f, g_take_loop ? 0.30f : 0.08f);
  ModLog("audio take   : Ctrl, Shift and the equals key: audio_take_loop is "
         "%d now. %s", g_take_loop ? 1 : 0,
         g_take_loop ? "The ears decide the start of the loop of a bulk take"
                     : "The game camera decides the start of the loop of a "
                       "bulk take, as in the game");
}

int __fastcall h_PlayEffect(void* self, void* unused, void* options) {
  const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  const uint8_t* o = static_cast<const uint8_t*>(options);
  if (!Readable(o, 0x60)) return o_PlayEffect(self, unused, options);
  const int sample = Int(o + kOptSample);
  const uintptr_t object = Ptr(o + kOptObject);
  // A flat sound has no distance test. A sound with no object is at the game
  // camera, and it keeps the game's test. The loop of a bulk take is at the
  // Hand, and the ears decide its start.
  if (sample == 0 || Int(o + kOptFlag3d) == 0 || object == 0) {
    if (sample != 0 && Int(o + kOptFlag3d) != 0 && caller == kTakeLoopReturn) {
      return PlayTakeLoop(self, options, sample);
    }
    return o_PlayEffect(self, unused, options);
  }
  const uintptr_t bank = Ptr(o + kOptBank);
  Facts f;
  for (int i = 0; i < 3; ++i) {
    f.point[i] = Float(o + kOptPosition + 4 * i) + Float(o + kOptOffset + 4 * i);
  }
  // The maximum distance, as the game finds it (0x00429EE9 to 0x00429F0B).
  f.max = o_MaxDistance(self, nullptr, static_cast<unsigned>(sample),
                        reinterpret_cast<void*>(bank));
  if (f.max == 0.0f) f.max = Float(o + kOptMax);
  if (!Finite(f.point[0]) || !Finite(f.point[1]) || !Finite(f.point[2]) ||
      !Finite(f.max)) {
    return o_PlayEffect(self, unused, options);
  }
  Measure(&f);
  const bool game_in_range =
      f.camera >= 0.0f && audio_math::RangeStarts(f.camera, f.max);

  const bool tag = Readable(reinterpret_cast<void*>(object), 4) &&
                   *reinterpret_cast<const uintptr_t*>(object) ==
                       kSoundTagVtable;
  Sound* s = tag || Int(o + kOptLoops) != 0 ? Track(object, sample, bank)
                                            : Find(object, sample);
  const bool plays = s != nullptr && s->info != nullptr && StillPlays(*s);

  if (f.by_ears) {
    ++g_tests;
    if (!audio_math::RangeStarts(f.head / f.k, f.max)) {
      if (game_in_range) ++g_head_refused;
      // A loop that plays is the callback's to judge.
      if (!plays) {
        Change(s, kRefusedRange, f, "is refused: out of range of the head");
      }
      return 0;
    }
    g_force = true;
  }
  g_call_active = true;
  g_call_reached = false;
  g_call_info = nullptr;
  const int r = o_PlayEffect(self, unused, options);
  g_force = false;
  g_call_active = false;

  const uint8_t* info = static_cast<const uint8_t*>(g_call_info);
  const bool loop = Readable(info, kInfoSize) && Int(info + kInfoLoops) != 0;
  if (f.by_ears && g_call_reached && !game_in_range && !plays) {
    ++g_head_started;
  }
  if (s == nullptr && loop) s = Track(object, sample, bank);
  if (s == nullptr) return r;
  if (!g_call_reached) {
    if (!plays) {
      if (!f.by_ears && !game_in_range) {
        Change(s, kRefusedGame, f,
               "is refused: the game camera is out of range");
      } else {
        Change(s, kRefusedRule, f,
               "is refused by a rule of the game (not the distance)");
      }
    }
  } else if (g_call_info == nullptr) {
    CountSlots();
    Change(s, kNoSlot, f,
           "is refused: the DLL gave no sample slot (%d of %d are free)",
           g_free_slots, g_slots);
  } else {
    s->info = g_call_info;
    s->loop = loop;
    s->loops = loop ? Int(info + kInfoLoops) : 0;
    if (s->state != kSilent) Change(s, kPlays, f, "starts");
  }
  return r;
}

// The start of an effect of an animation, of physics or of a particle
// system. The game's distance is from its camera.
void* __fastcall h_PlayAnim(void* self, void* unused, void* object,
                            float distance, void* effect, int a, void* bank,
                            int b, float f1, float f2) {
  float ears[3], point[3], k = 1.0f;
  // A caller that gives no distance wants the effect at each distance.
  if (distance > 0.0f && ByEars(ears, &k) &&
      AudioObjectPosition(reinterpret_cast<uintptr_t>(object), point)) {
    const float by_ears = audio_math::RangeDistance(ears, point, k);
    if (Finite(by_ears)) {
      ++g_anim_tests;
      if (by_ears < distance) ++g_anim_nearer;
      distance = by_ears;
    }
  }
  return o_PlayAnim(self, unused, object, distance, effect, a, bank, b, f1,
                    f2);
}

// ---- The stop test ---------------------------------------------------------

DWORD __cdecl h_Object(void* info, float* x, float* y, float* z,
                       float* distance, int* ok) {
  const DWORD r = o_Object(info, x, y, z, distance, ok);
  const uint8_t* si = static_cast<const uint8_t*>(info);
  if (ok == nullptr || distance == nullptr || x == nullptr || y == nullptr ||
      z == nullptr || !Readable(si, kInfoSize)) {
    return r;
  }
  const uintptr_t object = Ptr(si + kInfoObject);
  // A sound with no object is at the game camera: audio_rolloff.cpp puts it
  // at the ears, and the game's distance is right for it.
  if (object == 0 || object == static_cast<uintptr_t>(-1)) return r;
  const int sample = Int(si + kInfoSample);
  const bool loop = Int(si + kInfoLoops) != 0;
  Facts f;
  f.max = Float(si + kInfoMax);
  if (*ok != 1) {
    // The DLL stops the sample now: the object has no position.
    Sound* s = Find(object, sample);
    if (s != nullptr && s->info == info) {
      memcpy(f.point, si + kInfoPosition, 12);
      Measure(&f);
      s->info = nullptr;
      Change(s, kGone, f, "stops: its object has no position now");
    }
    return r;
  }
  f.point[0] = *x + Float(si + kInfoOffset);
  f.point[1] = *y + Float(si + kInfoOffset + 4);
  f.point[2] = *z + Float(si + kInfoOffset + 8);
  if (!Finite(f.point[0]) || !Finite(f.point[1]) || !Finite(f.point[2]) ||
      !Finite(f.max)) {
    return r;
  }
  Measure(&f);
  const float game = *distance;
  const float decides = f.by_ears ? f.head / f.k : game;
  const bool out = audio_math::RangeStops(decides, f.max);
  if (f.by_ears && !out && audio_math::RangeStops(game, f.max)) ++g_cut_saved;
  bool keep = false;
  if (out && loop && g_keep_loops && !g_test_off) {
    CountSlots();
    keep = audio_math::KeepSilentLoop(g_free_slots, g_slots);
  }
  // The DLL stops the sample when this distance is not less than info + 0x6C.
  if (keep) {
    *distance = 0.0f;
    ++g_kept;
  } else if (f.by_ears) {
    *distance = decides;
  }

  Sound* s = loop ? Track(object, sample, Ptr(si + kInfoBank))
                  : Find(object, sample);
  if (s == nullptr) return r;
  s->info = info;
  s->loop = loop;
  s->loops = Int(si + kInfoLoops);
  if (!out) {
    Change(s, kPlays, f,
           s->state == kSilent ? "is in range again" : "plays, in range");
  } else if (keep) {
    Change(s, kSilent, f,
           "is out of range and silent. It keeps its slot (%d of %d are free)",
           g_free_slots, g_slots);
  } else {
    s->info = nullptr;
    if (loop && g_keep_loops && !g_test_off) {
      Change(s, kOutStopped, f,
             "stops: out of range, and only %d of %d slots are free",
             g_free_slots, g_slots);
    } else {
      Change(s, kOutStopped, f,
             f.by_ears ? "stops: out of range of the head"
                       : "stops: the game camera is out of range");
    }
  }
  return r;
}

bool HookAt(uintptr_t address, const uint8_t* bytes, size_t count, void* hook,
            void** original, const char* name) {
  void* target = reinterpret_cast<void*>(address);
  if (!Readable(target, count) || memcmp(target, bytes, count) != 0) {
    ModLog("audio range  : %s at 0x%08X does not have the code of game "
           "version 1.42", name, static_cast<unsigned>(address));
    return false;
  }
  MH_STATUS s = MH_CreateHook(target, hook, original);
  if (s == MH_OK) s = MH_EnableHook(target);
  ModLog("audio range  : %s at 0x%08X %s", name,
         static_cast<unsigned>(address), s == MH_OK ? "ok" : "FAILED");
  return s == MH_OK;
}

// TEST ONLY. Puts a sound tag on the player's Creature, as the script
// command of the game does (GScript::AttachSoundTag, 0x00710150). The tag
// asks for its sample again each game turn.
// TEST ONLY. Lists the samples of one bank that the bank loops without
// end, with their range, so that a test can put one on a sound tag.
void TestBank() {
  if (g_test_bank < 0 || g_test_bank > 13 || g_test_bank_done) return;
  if (!(TabletopViewScale() > 0.0f)) return;
  g_test_bank_done = true;
  const uintptr_t* global = reinterpret_cast<const uintptr_t*>(kGlobalAudio);
  HMODULE dll = AudioAudioDll();
  FirstSampleFn first_fn = dll == nullptr ? nullptr
      : reinterpret_cast<FirstSampleFn>(GetProcAddress(
            dll, "?LHGetFirstBankSample@LH_AudioBank@@QAEPAULH_BankSample@@XZ"));
  if (!Readable(global, 4) || *global == 0 || first_fn == nullptr) return;
  const uint8_t* audio = reinterpret_cast<const uint8_t*>(*global);
  if (!Readable(audio + kAudioBanks + 4 * g_test_bank, 4)) return;
  void* bank = *reinterpret_cast<void* const*>(audio + kAudioBanks +
                                               4 * g_test_bank);
  if (!Readable(bank, 16)) return;
  const uint8_t* first =
      static_cast<const uint8_t*>(first_fn(bank, nullptr));
  int lines = 0, loops = 0, count = 0;
  for (int i = 1; i <= 600; ++i) {
    const uint8_t* p = first + (i - 1) * kBankSampleSize;
    if (!Readable(p, kBankSampleSize)) break;
    const uint32_t flags = static_cast<uint32_t>(Int(p + kBankSampleFlags));
    const float max = Float(p + kBankSampleMax);
    // Past the last sample the memory is not a sample.
    if (flags > 0xFFFF || !(max >= 0.0f) || max > 1e6f) break;
    ++count;
    if ((flags & 0x40) == 0 || Int(p + kBankSampleLoops) == 0) continue;
    ++loops;
    if (lines++ < 60) {
      ModLog("audio range  : TEST, bank type %d sample %d: loop count %d, "
             "flags 0x%X, play mode %d, priority %d, group %d, range %.0f "
             "to %.0f", g_test_bank, i, Int(p + kBankSampleLoops), flags,
             Int(p + kBankSampleMode), Int(p + kBankSamplePriority),
             Int(p + kBankSampleGroup) & 0xFFFF, Float(p + kBankSampleMin),
             max);
    }
  }
  ModLog("audio range  : TEST, bank type %d has %d samples, %d with a loop "
         "count", g_test_bank, count, loops);
}

void TestTag() {
  TestBank();
  if (g_test_tag <= 0 || g_test_tag_made) return;
  if (!(TabletopViewScale() > 0.0f)) {
    g_test_tag_frames = 0;
    return;
  }
  if (++g_test_tag_frames < 120) return;
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (!Readable(slot, 4) || *slot == nullptr) return;
  const uint8_t* game = static_cast<const uint8_t*>(*slot);
  if (!Readable(game + kGameLocalPlayer, 1)) return;
  const uint8_t* player =
      game + kGamePlayers + kGamePlayerSize * game[kGameLocalPlayer];
  if (!Readable(player + kPlayerCreature, 4)) return;
  void* creature = *reinterpret_cast<void* const*>(player + kPlayerCreature);
  if (!Readable(creature, 0x20)) {
    // The player has no Creature of their own yet. Use the Creature that the
    // sound code showed.
    creature = reinterpret_cast<void*>(g_seen_creature);
    if (!Readable(creature, 0x20)) return;
    ModLog("audio range  : TEST, the player has no Creature, so the tag goes "
           "on the Creature that had a loop");
  }
  g_test_tag_made = true;
  void* made = reinterpret_cast<TagCreateFn>(kSoundTagCreate)(
      creature, static_cast<unsigned>(g_test_tag), 1, 2, 0, 0, 1,
      g_test_tag_bank, 0);
  ModLog("audio range  : TEST, a sound tag with sample %d of bank type %d is "
         "on the Creature %p (tag %p)", g_test_tag, g_test_tag_bank, creature,
         made);
}

}  // namespace

bool AudioRangeInstall(bool by_ears, bool keep_loops, int test_tag,
                       int test_tag_bank) {
  g_by_ears = by_ears;
  g_keep_loops = keep_loops;
  g_test_tag = test_tag;
  g_test_tag_bank = test_tag_bank;
  HMODULE dll = AudioAudioDll();
  if (dll != nullptr) {
    f_IsPlaying = reinterpret_cast<IsPlayingFn>(GetProcAddress(
        dll, "?LHSampleIsPlaying@LH_AudioSystem@@QAEHPAULH_SampleInfo@@@Z"));
  }
  static const uint8_t kPlayBytes[] = {0x83, 0xEC, 0x0C, 0x53, 0x55};
  static const uint8_t kMaxBytes[] = {0x8B, 0x44, 0x24, 0x08, 0x85, 0xC0};
  static const uint8_t kObjectBytes[] = {0x83, 0xEC, 0x18, 0x53, 0x56};
  const bool a = HookAt(kObjectCallback, kObjectBytes, sizeof(kObjectBytes),
                        reinterpret_cast<void*>(&h_Object),
                        reinterpret_cast<void**>(&o_Object),
                        "the object callback");
  const bool b = a && HookAt(kMaxDistance, kMaxBytes, sizeof(kMaxBytes),
                             reinterpret_cast<void*>(&h_MaxDistance),
                             reinterpret_cast<void**>(&o_MaxDistance),
                             "GAudio::GetGSFXSampleMaxDistance");
  const bool c = b && HookAt(kPlayEffect, kPlayBytes, sizeof(kPlayBytes),
                             reinterpret_cast<void*>(&h_PlayEffect),
                             reinterpret_cast<void**>(&o_PlayEffect),
                             "GAudio::PlaySoundEffect");
  g_hooked = a;
  static const uint8_t kAnimBytes[] = {0x8B, 0x44, 0x24, 0x10, 0x85, 0xC0};
  if (c) {
    HookAt(kPlayAnimEffect, kAnimBytes, sizeof(kAnimBytes),
           reinterpret_cast<void*>(&h_PlayAnim),
           reinterpret_cast<void**>(&o_PlayAnim),
           "GAudio::SamplePlayAnimEffect");
  }
  g_test_bank = AudioIni("audio_test_bank", -1);
  g_take_loop = AudioIni("audio_take_loop", 1) != 0;
  g_take_loop_keys = AudioIni("test_keys", 0) != 0;
  ModLog("audio take   : audio_take_loop=%d. %s", g_take_loop ? 1 : 0,
         !c ? "A hook is missing, so the game camera decides the start of "
              "the loop of a bulk take"
         : g_take_loop ? "The ears decide the start of the loop of a bulk "
                         "take, as for a sound with an object"
                       : "The game camera decides the start of the loop of a "
                         "bulk take, as in the game");
  if (!c) {
    ModLog("audio range  : a hook is missing, so the game camera decides "
           "which sounds %s", a ? "start" : "start and stop");
    if (!a) g_by_ears = false;
  }
  ModLog("audio range  : %s (audio_range=%d). A loop out of range %s "
         "(audio_loops_keep=%d)",
         g_by_ears ? "the head decides which positional sounds play, with "
                     "room distances"
                   : "the game camera decides which positional sounds play, "
                     "as in the game",
         by_ears ? 1 : 0,
         g_keep_loops ? "keeps its slot and is silent" : "stops, as in the game",
         g_keep_loops ? 1 : 0);
  return g_hooked;
}

void AudioRangeSetTestOff(bool off) { g_test_off = off; }

void AudioRangeNotePlay(void* info) {
  if (g_call_active) {
    // GAudio::PlaySoundEffect drops the result. Its hook reads it here.
    g_call_reached = true;
    g_call_info = info;
    return;
  }
  // A loop that another path starts, for example the sound of a particle
  // system (AtomCore::StartSound).
  if (!g_hooked) return;
  const uint8_t* si = static_cast<const uint8_t*>(info);
  if (!Readable(si, kInfoSize) || Int(si + kInfoLoops) == 0 ||
      Int(si + kInfoFlag3d) != 1) {
    return;
  }
  const uintptr_t object = Ptr(si + kInfoObject);
  if (object == 0 || object == static_cast<uintptr_t>(-1)) return;
  Sound* s = Track(object, Int(si + kInfoSample), Ptr(si + kInfoBank));
  if (s == nullptr || (s->info == info && s->state == kPlays)) return;
  s->info = info;
  s->loop = true;
  s->loops = Int(si + kInfoLoops);
  Facts f;
  for (int i = 0; i < 3; ++i) {
    f.point[i] = Float(si + kInfoPosition + 4 * i) +
                 Float(si + kInfoOffset + 4 * i);
  }
  f.max = Float(si + kInfoMax);
  Measure(&f);
  if (s->state != kSilent) Change(s, kPlays, f, "starts");
}

void AudioRangeFrame() {
  if (!g_hooked) return;
  TestTag();
  TakeLoopFrame();
  const DWORD now = GetTickCount();
  if (now - g_last_check >= 1000) {
    g_last_check = now;
    // A loop that stopped with no word from the callback: the game or the DLL
    // stopped it.
    for (int i = 0; i < kSounds; ++i) {
      Sound& s = g_sounds[i];
      if (!s.used || !s.loop || s.info == nullptr ||
          (s.state != kPlays && s.state != kSilent)) {
        continue;
      }
      if (StillPlays(s)) {
        if (s.pending && now - s.tick >= kToldAfterMs) {
          Line(&s, s.last, "plays, in range");
        }
        continue;
      }
      s.info = nullptr;
      const Facts last = s.last;
      Change(&s, kEnded, last,
             "stopped, and not by its range: the game or the DLL stopped it");
    }
  }
  if (g_last_report == 0) g_last_report = now;
  if (now - g_last_report < 30000) return;
  g_last_report = now;
  const uint64_t sum = g_tests + g_head_started + g_head_refused + g_kept +
                       g_cut_saved + g_anim_tests;
  if (sum == g_reported) return;
  g_reported = sum;
  int silent = 0, playing = 0;
  for (int i = 0; i < kSounds; ++i) {
    if (!g_sounds[i].used) continue;
    if (g_sounds[i].state == kSilent) ++silent;
    if (g_sounds[i].state == kPlays) ++playing;
  }
  CountSlots();
  ModLog("audio range  : the head decided %llu starts: %llu started that the "
         "game camera refuses, %llu refused that the game camera starts. "
         "%llu game turns of a sound in range that the game camera stops. "
         "%llu effects of animations got the distance of the head, and for "
         "%llu it was the nearer one. Now %d loops and tag sounds play, %d "
         "loops are silent out of range (%llu game turns), and %d of %d "
         "slots are free",
         g_tests, g_head_started, g_head_refused, g_cut_saved, g_anim_tests,
         g_anim_nearer, playing, silent, g_kept, g_free_slots, g_slots);
}
