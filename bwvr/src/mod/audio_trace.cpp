// T1 of Track 5: a log of the calls into QMixer. It changes nothing.
//
// Each hook calls the original first, then counts the call and keeps the
// arguments. Once a second, from the frame driver on the game thread, the
// counts and the last values go to the log. This settles the open questions of
// docs/audio.md: how often the game moves the listener, what the DLL sends for
// a source, and what distance mapping it uses. This file hooks the listener
// position and the listener orientation. The source position, the polar
// position and the distance mapping belong to audio_rolloff.cpp, and its
// hooks call AudioTraceNoteSource, AudioTraceNotePolar and
// AudioTraceNoteDistance. The mapping in the log is the game's, before the
// room factor. The source position is the one QMixer got.
//
// THE PLAY TRACE (docs/audio.md, section 11). The LHSamplePlay hook of
// audio_sources.cpp tells this file about each sample that starts. This file
// writes one line for each start: who played it (a game effect, an animation
// effect, the atmosphere or an advisor), the bank, the sample, the 3D flag, the
// class of the object, and what the DLL sent to QMixer during the call. Once a
// second it asks QMixer for the parameters of each channel that still plays:
// the distance, the distance gain and the total gain that QMixer uses. Every
// ten seconds it writes a table of the kinds of sound and how they played.
//
// Off unless audio_trace=1 in bwvr.ini.

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "audio.h"
#include "audio_object.h"

void ModLog(const char* format, ...);
float TabletopViewScale();
float HeadPoseScale();

namespace {

// All QMixer calls are stdcall and return 0 on success. The argument counts
// come from the ret operands of the exports (0x0C, 0x10, 0x10, 0x10, 0x10).
typedef DWORD(WINAPI* ListenerPosFn)(void*, const float*, DWORD);
typedef DWORD(WINAPI* ListenerOriFn)(void*, const float*, const float*, DWORD);
typedef DWORD(WINAPI* ChannelFn)(void*, int, DWORD, const void*);
// QSWaveMixGetChannelParams(session, channel, params). The first field of
// params is its size. QMixer copies at most 0xDC bytes.
typedef DWORD(WINAPI* ParamsFn)(void*, int, void*);
// Base::Get3DSoundPos, vtable offset 0x10. thiscall, ret 4. It returns 1 when
// the object has a position (0x004011E0 is the Base default, which returns 0).
typedef int(__fastcall* SoundPosFn)(const void* self, void* unused,
                                    float* out);
typedef int(__fastcall* IsPlayingFn)(void* self, void* unused, void* info);

ListenerPosFn o_ListenerPos = nullptr;
ListenerOriFn o_ListenerOri = nullptr;
ParamsFn f_Params = nullptr;
IsPlayingFn f_IsPlaying = nullptr;

struct Counter {
  volatile LONG calls;
  volatile LONG failed;
};
Counter g_listener_pos, g_listener_ori, g_source_pos, g_polar, g_distance;

// Last values. Written by the hook, read by the frame driver. Both run on the
// game thread, so a torn read is not a worry.
float g_lp[3];
float g_lo_front[3], g_lo_up[3];
int g_sp_channel;
DWORD g_sp_flags;
float g_sp[3];
DWORD g_sp_result;
int g_po_channel;
DWORD g_po_flags;
float g_po[3];
int g_dm_channel;
DWORD g_dm_flags;
DWORD g_dm_size;
float g_dm[3];

// ---- The play trace -------------------------------------------------------

// Return addresses of the calls to LHSamplePlay. The game ones are absolute.
// The DLL ones are RVAs in LHaudiodllR.dll.
const uintptr_t kGameEffectReturn = 0x00429FE5;  // GAudio::PlaySoundEffect
const uintptr_t kAdvisorReturn = 0x005BB54A;     // HelpDude::PlaySample
const uintptr_t kAtmosReturnA = 0x1A87;          // LHAtmosProcess
const uintptr_t kAtmosReturnB = 0x1E41;          // LHAtmosProcess, second call
const uintptr_t kAnimReturnA = 0x1490C;          // LHSamplePlayAnimEffect
const uintptr_t kAnimReturnB = 0x14BC3;          // LHSamplePlayAnimEffect

// GAudio keeps one bank for each AUDIO_SFX_BANK_TYPE at +0x3A8.
const uintptr_t kGlobalAudio = 0x00CD3B20;
const int kBankArray = 0x3A8;
const int kBankTypes = 14;
const char* const kBankNames[kBankTypes] = {
    "none",     "in game",   "editor",        "spell",
    "creature", "script",    "help sprites",  "villagers",
    "banter",   "spell talk", "guidance",     "ci villagers",
    "ci help",  "ci spell talk"};

// The game's code and its vtables.
const uintptr_t kExeTextStart = 0x00401000;
const uintptr_t kExeTextEnd = 0x008A9000;
const uintptr_t kExeRdataEnd = 0x009C6000;
// The GAudio functions around PlaySoundEffect. The stack scan skips them to
// find the game code that asked for the sound.
const uintptr_t kAudioCodeStart = 0x00429D00;
const uintptr_t kAudioCodeEnd = 0x0042A700;

enum Kind { kEffect, kAnim, kAtmos, kAdvisor, kOther, kKinds };
const char* const kKindNames[kKinds] = {"effect", "anim", "atmos", "advisor",
                                        "other"};

struct PlayNote {
  int kind;
  uintptr_t site[2];   // the game code that asked, for an effect
  uintptr_t caller;    // the return address of the LHSamplePlay call
  int bank;            // AUDIO_SFX_BANK_TYPE, or -1
  int sample;
  int flag3d;          // options + 0x08
  int relative;        // options + 0x14, a position relative to the listener
  uintptr_t object;    // options + 0x20
  const char* cls;     // the class of the object, from its RTTI
  bool have_obj_pos;
  float obj_pos[3];    // Get3DSoundPos of the object, at the start
  float pos[3];        // options + 0x30 plus the offset at + 0x3C
  int volume;          // options + 0x28
  int pan;             // options + 0x2C
  float opt_map[3];    // options + 0x54 to + 0x5C
  // From the QMixer hooks during the call.
  bool have_map;
  float map[3];
  bool have_src;
  float src[3];
  bool have_polar;
  float polar[3];
  // After the call.
  void* info;
  int channel;
};

bool g_play_trace = false;
bool g_in_play = false;
PlayNote g_cur;
const int kChannels = 64;
PlayNote g_channel[kChannels];
bool g_channel_used[kChannels];
DWORD g_play_second = 0;
int g_play_lines_this_second = 0;
int g_play_lines = 0;
const int kPlayLinesPerSecond = 30;
const int kPlayLinesMax = 4000;
uint64_t g_plays = 0;
uint64_t g_plays_dropped = 0;  // no sample info came back
uint64_t g_plays_again = 0;    // the sample played already (a sound tag)

// The table of kinds of sound.
struct Group {
  int kind;
  int bank;
  const char* cls;
  int flag3d;
  bool used;
  uint32_t plays;
  uint32_t played;   // the DLL returned a sample info
  uint32_t source;   // QMixer got a world position
  uint32_t polar;    // QMixer got a polar position
  uint32_t with_pos; // the object gave a position
  float near_room;   // the nearest start, in room meters
  float far_room;
};
const int kGroups = 96;
Group g_groups[kGroups];
DWORD g_last_table = 0;

bool Readable(const void* p, size_t n) {
  return p != nullptr && !IsBadReadPtr(p, n);
}

uintptr_t AudioDllBase() {
  return reinterpret_cast<uintptr_t>(AudioAudioDll());
}

int BankIndex(uintptr_t bank) {
  if (bank == 0) return -1;
  const uintptr_t* global = reinterpret_cast<const uintptr_t*>(kGlobalAudio);
  if (!Readable(global, 4) || *global == 0) return -1;
  const uintptr_t* banks =
      reinterpret_cast<const uintptr_t*>(*global + kBankArray);
  if (!Readable(banks, kBankTypes * 4)) return -1;
  for (int i = 0; i < kBankTypes; ++i) {
    if (banks[i] == bank) return i;
  }
  return -1;
}

// The class name from the RTTI of an object of the game, or null. The name
// is ".?AVName@@" in the type descriptor. The pointer stays valid, because the
// descriptors are in the data of the exe.
const char* ClassName(uintptr_t object) {
  if (object < 0x10000 || !Readable(reinterpret_cast<void*>(object), 4)) {
    return nullptr;
  }
  const uintptr_t vt = *reinterpret_cast<const uintptr_t*>(object);
  if (vt < kExeTextEnd || vt >= kExeRdataEnd) return nullptr;
  const uintptr_t* slot = reinterpret_cast<const uintptr_t*>(vt - 4);
  if (!Readable(slot, 4)) return nullptr;
  const uintptr_t locator = *slot;
  if (!Readable(reinterpret_cast<void*>(locator), 16)) return nullptr;
  const uintptr_t type = *reinterpret_cast<const uintptr_t*>(locator + 12);
  const char* name = reinterpret_cast<const char*>(type + 8);
  if (!Readable(name, 8)) return nullptr;
  if (name[0] != '.' || name[1] != '?' || name[2] != 'A') return nullptr;
  return name + 4;
}

// The position of an object as the game's sound code sees it.
bool ObjectPosition(uintptr_t object, float out[3]) {
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
  const int r = reinterpret_cast<SoundPosFn>(fn)(
      reinterpret_cast<const void*>(object), nullptr, p);
  if (r != 1) return false;
  for (int i = 0; i < 3; ++i) {
    if (!(p[i] == p[i]) || p[i] > 1e7f || p[i] < -1e7f) return false;
    out[i] = p[i];
  }
  return true;
}

// True when the bytes before a return address are a call.
bool AfterCall(uintptr_t v) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(v);
  if (!Readable(b - 6, 6)) return false;
  if (b[-5] == 0xE8) return true;                          // call rel32
  if (b[-6] == 0xFF && b[-5] == 0x15) return true;         // call [abs]
  if (b[-6] == 0xFF && (b[-5] & 0xF8) == 0x90) return true;  // [reg+d32]
  if (b[-3] == 0xFF && (b[-2] & 0xF8) == 0x50) return true;  // [reg+d8]
  if (b[-2] == 0xFF && (b[-1] & 0xF8) == 0xD0) return true;  // call reg
  return false;
}

// The first two return addresses on the stack that are in the game code and
// outside the GAudio play functions. A heuristic for the log only.
void GameSites(const uintptr_t* stack, uintptr_t out[2]) {
  out[0] = out[1] = 0;
  int found = 0;
  for (int i = 1; i < 96 && found < 2; ++i) {
    if (!Readable(stack + i, 4)) break;
    const uintptr_t v = stack[i];
    if (v < kExeTextStart || v >= kExeTextEnd) continue;
    if (v >= kAudioCodeStart && v < kAudioCodeEnd) continue;
    if (!AfterCall(v)) continue;
    out[found++] = v;
  }
}

float UnitsPerMeter() {
  const float table = TabletopViewScale();
  if (table > 0.0f) return table;
  const float life = HeadPoseScale();
  return life > 0.0f ? life : 7.5f;
}

float Distance(const float a[3], const float b[3]) {
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

// The azimuth of a point from the QMixer listener, 0 ahead, 90 right.
float ListenerAzimuth(const float p[3]) {
  const float d[3] = {p[0] - g_lp[0], p[1] - g_lp[1], p[2] - g_lp[2]};
  const float* f = g_lo_front;
  const float* u = g_lo_up;
  // right = up x front, for a left handed world with y up.
  const float r[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2],
                      u[0] * f[1] - u[1] * f[0]};
  const float x = r[0] * d[0] + r[1] * d[1] + r[2] * d[2];
  const float z = f[0] * d[0] + f[1] * d[1] + f[2] * d[2];
  float a = atan2f(x, z) * 57.29578f;
  if (a < 0.0f) a += 360.0f;
  return a;
}

Group* FindGroup(const PlayNote& n) {
  int free_slot = -1;
  for (int i = 0; i < kGroups; ++i) {
    Group& g = g_groups[i];
    if (!g.used) {
      if (free_slot < 0) free_slot = i;
      continue;
    }
    if (g.kind == n.kind && g.bank == n.bank && g.cls == n.cls &&
        g.flag3d == n.flag3d) {
      return &g;
    }
  }
  if (free_slot < 0) return nullptr;
  Group& g = g_groups[free_slot];
  memset(&g, 0, sizeof(g));
  g.used = true;
  g.kind = n.kind;
  g.bank = n.bank;
  g.cls = n.cls;
  g.flag3d = n.flag3d;
  g.near_room = 1e9f;
  return &g;
}

void* Session(void* system) {
  const uint8_t* s = static_cast<const uint8_t*>(system);
  if (!Readable(s + 0x88, 4)) return nullptr;
  return *reinterpret_cast<void* const*>(s + 0x88);
}

// ---- QMixer hooks ---------------------------------------------------------

void Note(Counter* c, DWORD result) {
  InterlockedIncrement(&c->calls);
  if (result != 0) InterlockedIncrement(&c->failed);
}

void Copy3(float* to, const void* from) {
  if (from == nullptr || IsBadReadPtr(from, 12)) return;
  memcpy(to, from, 12);
}

DWORD WINAPI h_ListenerPos(void* session, const float* pos, DWORD flags) {
  const DWORD r = o_ListenerPos(session, pos, flags);
  Copy3(g_lp, pos);
  Note(&g_listener_pos, r);
  return r;
}

DWORD WINAPI h_ListenerOri(void* session, const float* front, const float* up,
                           DWORD flags) {
  const DWORD r = o_ListenerOri(session, front, up, flags);
  Copy3(g_lo_front, front);
  Copy3(g_lo_up, up);
  Note(&g_listener_ori, r);
  return r;
}

}  // namespace

// From the source position hook of audio_rolloff.cpp, after the original
// call, with the position that QMixer got.
void AudioTraceNoteSource(int channel, const void* vec, DWORD r) {
  g_sp_channel = channel;
  g_sp_flags = 0;
  Copy3(g_sp, vec);
  g_sp_result = r;
  if (g_in_play) {
    g_cur.have_src = true;
    Copy3(g_cur.src, vec);
  } else if (channel >= 0 && channel < kChannels && g_channel_used[channel]) {
    // The game's turn update moves the source of a positional sample.
    g_channel[channel].have_src = true;
    Copy3(g_channel[channel].src, vec);
  }
  Note(&g_source_pos, r);
}

// From the hooks of audio_rolloff.cpp, after the original call.
void AudioTraceNotePolar(int channel, const void* polar) {
  g_po_channel = channel;
  g_po_flags = 0x20;
  Copy3(g_po, polar);
  if (g_in_play) {
    g_cur.have_polar = true;
    Copy3(g_cur.polar, polar);
  }
  Note(&g_polar, 0);
}

void AudioTraceNoteDistance(int channel, const void* mapping) {
  g_dm_channel = channel;
  g_dm_flags = 0x20;
  if (mapping != nullptr && !IsBadReadPtr(mapping, 16)) {
    memcpy(&g_dm_size, mapping, 4);
    memcpy(g_dm, static_cast<const char*>(mapping) + 4, 12);
    if (g_in_play) {
      g_cur.have_map = true;
      memcpy(g_cur.map, g_dm, 12);
    }
  }
  Note(&g_distance, 0);
}

namespace {

bool HookExport(HMODULE dll, const char* name, void* hook, void** original) {
  void* target = dll == nullptr ? nullptr : GetProcAddress(dll, name);
  if (target == nullptr) {
    ModLog("audio trace  : %s not found", name);
    return false;
  }
  MH_STATUS s = MH_CreateHook(target, hook, original);
  if (s == MH_OK) s = MH_EnableHook(target);
  ModLog("audio trace  : %s %s", name, s == MH_OK ? "ok" : "FAILED");
  return s == MH_OK;
}

LONG Take(Counter* c, LONG* failed) {
  *failed = InterlockedExchange(&c->failed, 0);
  return InterlockedExchange(&c->calls, 0);
}

DWORD g_last_log = 0;

void LogPlay(const PlayNote& n) {
  const DWORD now = GetTickCount();
  if (now - g_play_second >= 1000) {
    g_play_second = now;
    g_play_lines_this_second = 0;
  }
  if (g_play_lines >= kPlayLinesMax ||
      g_play_lines_this_second >= kPlayLinesPerSecond) {
    return;
  }
  ++g_play_lines;
  ++g_play_lines_this_second;
  const float upm = UnitsPerMeter();
  char where[160] = "no position";
  const float* at = n.have_src ? n.src : n.have_obj_pos ? n.obj_pos : nullptr;
  if (at != nullptr) {
    const float d = Distance(at, g_lp);
    _snprintf_s(where, sizeof(where), _TRUNCATE,
                "%s at %.0f %.0f %.0f, %.2f m from the ears, azimuth %.0f",
                n.have_src ? "source" : "object", at[0], at[1], at[2],
                d / upm, ListenerAzimuth(at));
  }
  char map[96] = "";
  if (n.have_map) {
    _snprintf_s(map, sizeof(map), _TRUNCATE, ", map %.1f %.0f %.2f",
                n.map[0], n.map[1], n.map[2]);
  } else if (n.flag3d) {
    _snprintf_s(map, sizeof(map), _TRUNCATE, ", options map %.1f %.0f %.2f",
                n.opt_map[0], n.opt_map[1], n.opt_map[2]);
  }
  char polar[64] = "";
  if (n.have_polar) {
    _snprintf_s(polar, sizeof(polar), _TRUNCATE, ", polar %.0f range %.2f",
                n.polar[0], n.polar[1]);
  }
  ModLog("audio play   : %s %s %d, 3d %d%s, ch %d, %s 0x%08X, site 0x%08X "
         "0x%08X, vol %d pan %d, %s%s%s", kKindNames[n.kind],
         n.bank >= 0 ? kBankNames[n.bank] : "bank ?", n.sample, n.flag3d,
         n.relative ? " relative" : "", n.channel,
         n.cls != nullptr ? n.cls : "object", static_cast<unsigned>(n.object),
         static_cast<unsigned>(n.site[0]), static_cast<unsigned>(n.site[1]),
         n.volume, n.pan, where, map, polar);
}

void LogChannels(void* system) {
  if (f_Params == nullptr || system == nullptr) return;
  void* session = Session(system);
  if (session == nullptr) return;
  const float upm = UnitsPerMeter();
  int lines = 0;
  for (int ch = 0; ch < kChannels && lines < 12; ++ch) {
    if (!g_channel_used[ch]) continue;
    PlayNote& n = g_channel[ch];
    const uint8_t* info = static_cast<const uint8_t*>(n.info);
    if (!Readable(info, 0x30) ||
        *reinterpret_cast<const int*>(info + 0x2C) != ch ||
        (f_IsPlaying != nullptr && f_IsPlaying(system, nullptr, n.info) == 0)) {
      g_channel_used[ch] = false;
      continue;
    }
    uint8_t p[0xDC];
    memset(p, 0, sizeof(p));
    *reinterpret_cast<DWORD*>(p) = sizeof(p);
    if (f_Params(session, ch, p) != 0) continue;
    const DWORD flags = *reinterpret_cast<const DWORD*>(p + 0x04);
    const float* pos = reinterpret_cast<const float*>(p + 0x10);
    const float* map = reinterpret_cast<const float*>(p + 0x40);
    const float range = *reinterpret_cast<const float*>(p + 0xA8);
    const float azimuth = *reinterpret_cast<const float*>(p + 0xAC);
    const float dist_gain = *reinterpret_cast<const float*>(p + 0xB8);
    const float gain = *reinterpret_cast<const float*>(p + 0xBC);
    ++lines;
    ModLog("audio chan   : ch %d %s %s %d %s, flags 0x%08lX, at %.0f %.0f "
           "%.0f, range %.0f (%.2f m), azimuth %.0f, map %.0f %.0f %.2f, "
           "distance gain %.3f, gain %.3f", ch, kKindNames[n.kind],
           n.bank >= 0 ? kBankNames[n.bank] : "bank ?", n.sample,
           n.cls != nullptr ? n.cls : "-", flags, pos[0], pos[1], pos[2],
           range, range / upm, azimuth, map[0], map[1], map[2], dist_gain,
           gain);
  }
}

void LogGroups() {
  ModLog("audio kinds  : %llu starts, %llu with no sample info, %llu for a "
         "sample that played already, scale %.1f units a meter", g_plays,
         g_plays_dropped, g_plays_again, UnitsPerMeter());
  for (int i = 0; i < kGroups; ++i) {
    const Group& g = g_groups[i];
    if (!g.used) continue;
    ModLog("audio kinds  : %-7s %-12s %-24s 3d %d: %u starts, %u played, "
           "%u source, %u polar, %u with an object position, %.2f to %.2f m",
           kKindNames[g.kind], g.bank >= 0 ? kBankNames[g.bank] : "?",
           g.cls != nullptr ? g.cls : "-", g.flag3d, g.plays, g.played,
           g.source, g.polar, g.with_pos,
           g.near_room < 1e8f ? g.near_room : 0.0f, g.far_room);
  }
}

}  // namespace

const char* AudioClassName(uintptr_t object) { return ClassName(object); }

bool AudioObjectPosition(uintptr_t object, float out[3]) {
  return ObjectPosition(object, out);
}

const char* AudioBankName(uintptr_t bank) {
  const int index = BankIndex(bank);
  return index >= 0 ? kBankNames[index] : "bank ?";
}

bool AudioTraceInstall() {
  HMODULE q = AudioMixerDll();
  bool ok = true;
  ok &= HookExport(q, "QSWaveMixSetListenerPosition",
                   reinterpret_cast<void*>(&h_ListenerPos),
                   reinterpret_cast<void**>(&o_ListenerPos));
  ok &= HookExport(q, "QSWaveMixSetListenerOrientation",
                   reinterpret_cast<void*>(&h_ListenerOri),
                   reinterpret_cast<void**>(&o_ListenerOri));
  // QSWaveMixSetSourcePosition, QSWaveMixSetPolarPosition and
  // QSWaveMixSetDistanceMapping belong to audio_rolloff.cpp. Its hooks call
  // AudioTraceNoteSource, AudioTraceNotePolar and AudioTraceNoteDistance.
  if (q != nullptr) {
    f_Params = reinterpret_cast<ParamsFn>(
        GetProcAddress(q, "QSWaveMixGetChannelParams"));
  }
  HMODULE dll = AudioAudioDll();
  if (dll != nullptr) {
    f_IsPlaying = reinterpret_cast<IsPlayingFn>(GetProcAddress(
        dll, "?LHSampleIsPlaying@LH_AudioSystem@@QAEHPAULH_SampleInfo@@@Z"));
  }
  g_play_trace = true;
  return ok;
}

bool AudioTraceOn() { return g_play_trace; }

void AudioTracePlayBegin(void* system, const void* options, uintptr_t caller,
                         const uintptr_t* stack) {
  (void)system;
  memset(&g_cur, 0, sizeof(g_cur));
  g_cur.caller = caller;
  g_cur.bank = -1;
  g_cur.channel = -1;
  const uintptr_t dll = AudioDllBase();
  if (caller == kGameEffectReturn) {
    g_cur.kind = kEffect;
    GameSites(stack, g_cur.site);
  } else if (caller == kAdvisorReturn) {
    g_cur.kind = kAdvisor;
  } else if (dll != 0 && (caller == dll + kAtmosReturnA ||
                          caller == dll + kAtmosReturnB)) {
    g_cur.kind = kAtmos;
  } else if (dll != 0 && (caller == dll + kAnimReturnA ||
                          caller == dll + kAnimReturnB)) {
    g_cur.kind = kAnim;
    GameSites(stack, g_cur.site);
  } else {
    g_cur.kind = kOther;
    g_cur.site[0] = caller;
  }
  const uint8_t* o = static_cast<const uint8_t*>(options);
  if (Readable(o, 0x60)) {
    g_cur.bank = BankIndex(*reinterpret_cast<const uintptr_t*>(o + 0x04));
    g_cur.flag3d = *reinterpret_cast<const int*>(o + 0x08);
    g_cur.relative = *reinterpret_cast<const int*>(o + 0x14);
    g_cur.object = *reinterpret_cast<const uintptr_t*>(o + 0x20);
    g_cur.sample = *reinterpret_cast<const int*>(o + 0x24);
    g_cur.volume = *reinterpret_cast<const int*>(o + 0x28);
    g_cur.pan = *reinterpret_cast<const int*>(o + 0x2C);
    const float* p = reinterpret_cast<const float*>(o + 0x30);
    const float* off = reinterpret_cast<const float*>(o + 0x3C);
    for (int i = 0; i < 3; ++i) g_cur.pos[i] = p[i] + off[i];
    memcpy(g_cur.opt_map, o + 0x54, 12);
    if (g_cur.kind != kAdvisor && g_cur.kind != kAtmos) {
      g_cur.cls = ClassName(g_cur.object);
      if (g_cur.cls != nullptr) {
        g_cur.have_obj_pos = ObjectPosition(g_cur.object, g_cur.obj_pos);
      }
    }
  }
  g_in_play = true;
}

void AudioTracePlayEnd(void* system, void* info) {
  (void)system;
  g_in_play = false;
  ++g_plays;
  g_cur.info = info;
  const uint8_t* i = static_cast<const uint8_t*>(info);
  if (Readable(i, 0x30)) {
    g_cur.channel = *reinterpret_cast<const int*>(i + 0x2C);
  } else {
    ++g_plays_dropped;
  }
  Group* g = FindGroup(g_cur);
  if (g != nullptr) {
    ++g->plays;
    if (info != nullptr) ++g->played;
    if (g_cur.have_src) ++g->source;
    if (g_cur.have_polar) ++g->polar;
    if (g_cur.have_obj_pos) ++g->with_pos;
    const float* at =
        g_cur.have_src ? g_cur.src : g_cur.have_obj_pos ? g_cur.obj_pos
                                                        : nullptr;
    if (at != nullptr) {
      const float room = Distance(at, g_lp) / UnitsPerMeter();
      if (room < g->near_room) g->near_room = room;
      if (room > g->far_room) g->far_room = room;
    }
  }
  // The DLL sent nothing to QMixer: the sample played already. A sound tag
  // asks for its sample each game turn (SoundTag, 0x0071E680). No line, and
  // the note of the first start stays.
  if (info != nullptr && !g_cur.have_map && !g_cur.have_src &&
      !g_cur.have_polar) {
    ++g_plays_again;
    return;
  }
  if (g_cur.channel >= 0 && g_cur.channel < kChannels && info != nullptr) {
    g_channel[g_cur.channel] = g_cur;
    g_channel_used[g_cur.channel] = true;
  }
  LogPlay(g_cur);
}

void AudioTraceFrame() {
  if (o_ListenerPos == nullptr) return;
  const DWORD now = GetTickCount();
  if (g_last_log == 0) g_last_log = now;
  const DWORD elapsed = now - g_last_log;
  if (elapsed < 1000) return;
  g_last_log = now;

  LONG f[5];
  const LONG lp = Take(&g_listener_pos, &f[0]);
  const LONG lo = Take(&g_listener_ori, &f[1]);
  const LONG sp = Take(&g_source_pos, &f[2]);
  const LONG po = Take(&g_polar, &f[3]);
  const LONG dm = Take(&g_distance, &f[4]);
  ModLog("audio trace  : %lu ms, calls: listener position %ld, orientation "
         "%ld, source position %ld, polar %ld, distance mapping %ld "
         "(failed %ld %ld %ld %ld %ld)", elapsed, lp, lo, sp, po, dm, f[0],
         f[1], f[2], f[3], f[4]);
  ModLog("audio trace  : QMixer listener at %.2f %.2f %.2f, front %.3f %.3f "
         "%.3f, up %.3f %.3f %.3f", g_lp[0], g_lp[1], g_lp[2], g_lo_front[0],
         g_lo_front[1], g_lo_front[2], g_lo_up[0], g_lo_up[1], g_lo_up[2]);
  if (sp > 0) {
    ModLog("audio trace  : last source position ch %d flags 0x%X at %.2f %.2f "
           "%.2f result %lu", g_sp_channel, g_sp_flags, g_sp[0], g_sp[1],
           g_sp[2], g_sp_result);
  }
  if (po > 0) {
    ModLog("audio trace  : last polar ch %d flags 0x%X azimuth %.1f range "
           "%.2f third %.2f", g_po_channel, g_po_flags, g_po[0], g_po[1],
           g_po[2]);
  }
  if (dm > 0) {
    ModLog("audio trace  : last distance mapping ch %d flags 0x%X size %lu "
           "min %.2f max %.2f scale %.2f", g_dm_channel, g_dm_flags, g_dm_size,
           g_dm[0], g_dm[1], g_dm[2]);
  }
  if (g_play_trace) {
    LogChannels(AudioSystem());
    if (g_last_table == 0) g_last_table = now;
    if (now - g_last_table >= 10000) {
      g_last_table = now;
      LogGroups();
    }
  }
}
