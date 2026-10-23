// The ears are the real head.
//
// The game sets the listener once per game turn, from the game camera, in
// GAudio::UpdateListener (0x004270D0). It calls LHListenerUpdate in
// LHaudiodllR.dll with a position, a look at point and an up vector. That
// function passes them on to QMixer.
//
// This file hooks that export. While the head camera is drawing, the hook
// throws the arguments of the game away and sends the rendered head instead.
// The game calls it about ten times a second, and a head turns at the frame
// rate. So AudioFrame also calls the original once for every rendered frame,
// with the same values, from the FinishFrame hook on the game thread. Between
// two calls QMixer holds the world positions of the sources, and it finds the
// direction of each one against the newest listener at every mix block.
//
// The head has no axis conversion. The game gives QMixer world coordinates
// today, and the head basis is in the same world.
//
// The sources do not move. Only the listener follows the head.
//
// ON THE TABLE AND IN THE GOD VIEW the ears are the head, and
// audio_rolloff.cpp makes the distances room distances (docs/audio.md,
// "6.3 Room distances"). With audio_room=0 the ears go straight down from the
// head to at most 400 units over the land under it (TabletopEars), because
// the game's distances make the sounds silent that far from the land.
//
// Ini: audio_head (default 1), audio_advisors (default 1), audio_trace (0),
// audio_room (1), audio_units_per_meter (40), audio_flat_places (1),
// audio_range (1), audio_loops_keep (1).
// Test key: Ctrl Shift F12 has five steps, and each one takes one more
// change away. All as set. The distances are the same at each zoom. QMixer
// pans each sound. The game camera decides which sounds play. The ears are
// on the game camera: the behavior of the plain game, with the game camera
// and the game's own distances.

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "audio.h"
#include "debug_menu.h"

void ModLog(const char* format, ...);
bool HeadCameraRenderBasis(float basis_out[9], float position_out[3]);
bool HeadCameraEnabled();
uint64_t HeadCameraMainSerial();
// tabletop.cpp: the ears on the table, from the render head, and the scale
// of the table or the God view (0 when neither owns the view).
bool TabletopEars(const float head[3], float out[3]);
float TabletopViewScale();

namespace {

// GGlobal::Global holds the GAudio pointer. The audio system is at +0x14.
// Verified at 0x004270BB: mov ecx,[0xCD3B20], then mov ecx,[esi+0x14].
const uintptr_t kGlobalAudio = 0x00CD3B20;
const int kAudioSystemOffset = 0x14;

// The head is stale after this long with no new main camera call. The menus
// and the loading screens draw no world, so the head basis stops changing.
const DWORD kFreshMs = 300;

// How far ahead of the head the look at point is. The DLL normalizes it.
const float kLookAhead = 10.0f;

// LH_AudioSystem::LHListenerUpdate(LHAudioPoint* position, LHAudioPoint*
// look_at, LHAudioPoint* up). thiscall, ret 0xC. Each point is three floats.
typedef void(__fastcall* ListenerFn)(void* self, void* unused,
                                     const float* position,
                                     const float* look_at, const float* up);
ListenerFn o_Listener = nullptr;

HMODULE g_audio_dll = nullptr;
HMODULE g_mixer_dll = nullptr;
bool g_installed = false;
bool g_head_switch = true;   // audio_head
bool g_advisors = true;      // audio_advisors
bool g_trace = false;        // audio_trace
bool g_room = true;          // audio_room
int g_room_units = 40;       // audio_units_per_meter
bool g_places = true;        // audio_flat_places
bool g_range = true;         // audio_range
bool g_keep_loops = true;    // audio_loops_keep
int g_test_tag = 0;          // audio_test_tag
int g_test_tag_bank = 1;     // audio_test_tag_bank
// The test key. 0 all as set. 1 the room factor is the same at each zoom
// (the sound without audio_zoom_pct). 2 also QMixer's own panning (the sound
// without audio_mixer). 3 also the game camera decides which sounds play (the
// sound without audio_range). 4 the ears on the game camera.
int g_mode = 0;

// Head freshness.
uint64_t g_seen_serial = 0;
DWORD g_seen_tick = 0;
uint64_t g_driven_serial = 0;

// What the hook and the driver did, for the trace and the log.
uint64_t g_game_calls = 0;      // calls that came from the game
uint64_t g_replaced = 0;        // of those, calls that sent the head
uint64_t g_driver_calls = 0;    // calls from AudioFrame
float g_game_pos[3], g_game_look[3], g_game_up[3];
bool g_have_game_args = false;
float g_sent_pos[3], g_sent_forward[3], g_sent_up[3];
bool g_sent_head = false;
bool g_was_head = false;
bool g_was_table = false;
bool g_warned_no_system = false;
void* g_noted_system = nullptr;
DWORD g_last_trace = 0;
uint64_t g_last_game_calls = 0;
uint64_t g_last_driver_calls = 0;
uint64_t g_last_replaced = 0;
// The cost of the frame work, for a report every 1800 frames.
double g_work_us = 0.0;
double g_work_worst_us = 0.0;
int g_work_frames = 0;

bool Fresh() {
  if (!HeadCameraEnabled()) return false;
  const uint64_t serial = HeadCameraMainSerial();
  if (serial == 0) return false;
  const DWORD now = GetTickCount();
  if (serial != g_seen_serial) {
    g_seen_serial = serial;
    g_seen_tick = now;
  }
  return now - g_seen_tick < kFreshMs;
}

bool Finite3(const float* v) {
  for (int i = 0; i < 3; ++i) {
    if (!(v[i] == v[i]) || v[i] > 1e30f || v[i] < -1e30f) return false;
  }
  return true;
}

// THE EARS: the head. With audio_room=0, on the table they are the point
// over the land under the head. The basis is the head's either way. on_table
// says which.
bool EarsPose(float basis_out[9], float position_out[3], bool* on_table) {
  *on_table = false;
  if (!AudioHeadPose(basis_out, position_out)) return false;
  if (AudioRoomActive(TabletopViewScale())) return true;
  float ears[3];
  if (TabletopEars(position_out, ears)) {
    for (int i = 0; i < 3; ++i) position_out[i] = ears[i];
    *on_table = true;
  }
  return true;
}

// Send one head pose to the original function.
void SendHead(void* self, const float basis[9], const float position[3]) {
  const float forward[3] = {basis[2], basis[5], basis[8]};
  const float up[3] = {basis[1], basis[4], basis[7]};
  const float look[3] = {position[0] + forward[0] * kLookAhead,
                         position[1] + forward[1] * kLookAhead,
                         position[2] + forward[2] * kLookAhead};
  o_Listener(self, nullptr, position, look, up);
  AudioVoicesListener(position, forward, up);
  memcpy(g_sent_pos, position, sizeof(g_sent_pos));
  memcpy(g_sent_forward, forward, sizeof(g_sent_forward));
  memcpy(g_sent_up, up, sizeof(g_sent_up));
  g_sent_head = true;
}

void __fastcall h_Listener(void* self, void* unused, const float* position,
                           const float* look_at, const float* up) {
  ++g_game_calls;
  g_noted_system = self;
  if (position != nullptr && look_at != nullptr && up != nullptr &&
      !IsBadReadPtr(position, 12) && !IsBadReadPtr(look_at, 12) &&
      !IsBadReadPtr(up, 12)) {
    memcpy(g_game_pos, position, 12);
    memcpy(g_game_look, look_at, 12);
    memcpy(g_game_up, up, 12);
    g_have_game_args = true;
  }
  if (g_head_switch) {
    float basis[9], where[3];
    bool on_table = false;
    if (EarsPose(basis, where, &on_table) && Finite3(where)) {
      ++g_replaced;
      SendHead(self, basis, where);
      return;
    }
  }
  o_Listener(self, unused, position, look_at, up);
  // The mixer of the mod gets the ears that QMixer got: the game camera.
  if (g_have_game_args) {
    float f[3] = {g_game_look[0] - g_game_pos[0],
                  g_game_look[1] - g_game_pos[1],
                  g_game_look[2] - g_game_pos[2]};
    const float n = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    float u[3] = {g_game_up[0], g_game_up[1], g_game_up[2]};
    const float m = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (n > 1e-6f && m > 1e-6f) {
      for (int i = 0; i < 3; ++i) {
        f[i] /= n;
        u[i] /= m;
      }
      AudioVoicesListener(g_game_pos, f, u);
    }
  }
}

void LogTrace(DWORD now) {
  if (g_last_trace == 0) g_last_trace = now;
  const DWORD elapsed = now - g_last_trace;
  if (elapsed < 1000) return;
  g_last_trace = now;
  ModLog("audio listen : %lu ms, game calls %llu, replaced with the head "
         "%llu, frame calls %llu", elapsed, g_game_calls - g_last_game_calls,
         g_replaced - g_last_replaced, g_driver_calls - g_last_driver_calls);
  g_last_game_calls = g_game_calls;
  g_last_replaced = g_replaced;
  g_last_driver_calls = g_driver_calls;
  if (g_have_game_args) {
    float f[3] = {g_game_look[0] - g_game_pos[0], g_game_look[1] - g_game_pos[1],
                  g_game_look[2] - g_game_pos[2]};
    const float n = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (n > 1e-6f) {
      for (int i = 0; i < 3; ++i) f[i] /= n;
    }
    ModLog("audio listen : game at %.2f %.2f %.2f, forward %.3f %.3f %.3f, up "
           "%.3f %.3f %.3f", g_game_pos[0], g_game_pos[1], g_game_pos[2], f[0],
           f[1], f[2], g_game_up[0], g_game_up[1], g_game_up[2]);
    if (g_sent_head) {
      float dot = f[0] * g_sent_forward[0] + f[1] * g_sent_forward[1] +
                  f[2] * g_sent_forward[2];
      if (dot > 1.0f) dot = 1.0f;
      if (dot < -1.0f) dot = -1.0f;
      ModLog("audio listen : ears at %.2f %.2f %.2f, forward %.3f %.3f %.3f, "
             "up %.3f %.3f %.3f, %.1f degrees from the game forward",
             g_sent_pos[0], g_sent_pos[1], g_sent_pos[2], g_sent_forward[0],
             g_sent_forward[1], g_sent_forward[2], g_sent_up[0], g_sent_up[1],
             g_sent_up[2], acosf(dot) * 57.29578f);
    }
  }
}

}  // namespace

void AudioNoteSystem(void* system) { g_noted_system = system; }

int AudioIni(const char* key, int fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return fallback;
  slash[1] = 0;
  strcat_s(path, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

void AudioIniText(const char* key, char* out, int size) {
  out[0] = 0;
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return;
  slash[1] = 0;
  strcat_s(path, "bwvr.ini");
  GetPrivateProfileStringA("bwvr", key, "", out, static_cast<DWORD>(size),
                           path);
}

void* AudioSystem() {
  const uintptr_t* global = reinterpret_cast<const uintptr_t*>(kGlobalAudio);
  if (!IsBadReadPtr(global, sizeof(*global)) && *global != 0) {
    const void* const* slot =
        reinterpret_cast<const void* const*>(*global + kAudioSystemOffset);
    if (!IsBadReadPtr(slot, sizeof(*slot)) && *slot != nullptr) {
      return const_cast<void*>(*slot);
    }
  }
  return g_noted_system;
}

bool AudioEarsPosition(float out[3]) {
  if (!g_was_head || !g_sent_head || !g_head_switch) return false;
  memcpy(out, g_sent_pos, sizeof(g_sent_pos));
  return true;
}

HMODULE AudioAudioDll() { return g_audio_dll; }
HMODULE AudioMixerDll() { return g_mixer_dll; }

bool AudioHeadPose(float basis_out[9], float position_out[3]) {
  if (!g_installed || g_mode == 4) return false;
  if (!Fresh()) return false;
  return HeadCameraRenderBasis(basis_out, position_out);
}

void AudioSetRoom(bool room, int units_per_meter, bool places) {
  g_room = room;
  g_room_units = units_per_meter;
  g_places = places;
}

void AudioSetRange(bool by_ears, bool keep_loops, int test_tag,
                   int test_tag_bank) {
  g_range = by_ears;
  g_keep_loops = keep_loops;
  g_test_tag = test_tag;
  g_test_tag_bank = test_tag_bank;
}

void AudioInstall(bool head, bool advisors, bool trace) {
  g_head_switch = head;
  g_advisors = advisors;
  g_trace = trace;
  if (!head && !advisors && !trace) {
    ModLog("audio        : off in bwvr.ini, the sound stays as the game "
           "made it");
    return;
  }
  g_audio_dll = GetModuleHandleA("LHaudiodllR.dll");
  g_mixer_dll = GetModuleHandleA("QMixer.dll");
  if (g_audio_dll == nullptr) {
    ModLog("audio        : LHaudiodllR.dll is not loaded, no sound changes");
    return;
  }
  g_installed = true;

  if (head) {
    void* target = GetProcAddress(
        g_audio_dll,
        "?LHListenerUpdate@LH_AudioSystem@@QAEXPAVLHAudioPoint@@00@Z");
    if (target == nullptr) {
      ModLog("audio        : LHListenerUpdate not found, the ears stay on "
             "the game camera");
      g_head_switch = false;
    } else {
      MH_STATUS s =
          MH_CreateHook(target, reinterpret_cast<void*>(&h_Listener),
                        reinterpret_cast<void**>(&o_Listener));
      if (s == MH_OK) s = MH_EnableHook(target);
      ModLog("audio        : LHListenerUpdate at %p %s", target,
             s == MH_OK ? "ok" : "FAILED");
      if (s != MH_OK) {
        g_head_switch = false;
        o_Listener = nullptr;
      }
    }
  }
  if (trace) AudioTraceInstall();
  // The mixer of the mod needs the stream that QMixer gives to the device.
  const bool stream =
      AudioCaptureInstall(AudioIni("audio_capture", 0) != 0, trace);
  AudioVoicesInstall(stream && AudioIni("audio_mixer", 1) != 0);
  AudioCuesInstall();
  // Room distances need the head: with the ears on the game camera the
  // game's distances are right.
  AudioRolloffInstall(g_room && g_head_switch, g_room_units,
                      trace || g_places || advisors);
  AudioSourcesInstall(advisors, g_places,
                      trace || g_head_switch);
  // The range tests need the head too: with the ears on the game camera the
  // game's tests are right.
  if (g_head_switch) {
    AudioRangeInstall(g_range, g_keep_loops, g_test_tag, g_test_tag_bank);
  }
  ModLog("audio        : head ears %s, advisors %s, room distances %s, "
         "trace %s", g_head_switch ? "on" : "off", advisors ? "on" : "off",
         g_room && g_head_switch ? "on" : "off", trace ? "on" : "off");
}

// From the FinishFrame hook. FinishFrame can run more than once in a frame,
// once for each eye pass, so the work is latched by the main camera serial.
// The serial goes up once per rendered frame.
void AudioFrame() {
  if (!g_installed) return;
  if (g_trace) {
    AudioTraceFrame();
    LogTrace(GetTickCount());
  }
  AudioCaptureFrame();

  float basis[9], where[3], ears[3];
  const bool head = AudioHeadPose(basis, where) && Finite3(where);
  bool on_table = false;
  float ears_basis[9];
  const bool have_ears =
      head && EarsPose(ears_basis, ears, &on_table) && Finite3(ears);
  if (head != g_was_head || (head && on_table != g_was_table)) {
    g_was_head = head;
    g_was_table = head && on_table;
    ModLog("audio        : the ears are on the %s",
           !head ? (g_mode == 4 ? "game camera (test key)"
                                : "game camera (no head camera frame)")
           : on_table ? "table, over the land under the head"
                      : "head");
  }
  const uint64_t serial = HeadCameraMainSerial();
  if (serial == g_driven_serial) return;
  g_driven_serial = serial;
  LARGE_INTEGER start;
  QueryPerformanceCounter(&start);

  if (have_ears && g_head_switch && o_Listener != nullptr) {
    void* self = AudioSystem();
    if (self != nullptr) {
      ++g_driver_calls;
      SendHead(self, ears_basis, ears);
    } else if (!g_warned_no_system) {
      g_warned_no_system = true;
      const uintptr_t* global =
          reinterpret_cast<const uintptr_t*>(kGlobalAudio);
      ModLog("audio        : the audio system is not there yet (GAudio "
             "0x%08X)", IsBadReadPtr(global, 4)
                            ? 0u : static_cast<unsigned>(*global));
    }
  }
  // The room distances follow the scale of the view, while the ears are the
  // head. Otherwise the game's own distances go back to QMixer. This comes
  // after the ears, so the sounds at the listener get the ears of this frame.
  const float units_per_meter = TabletopViewScale();
  AudioRolloffFrame(have_ears && g_head_switch && units_per_meter > 0.0f,
                    units_per_meter);
  AudioSourcesFrame(head, basis, where);
  AudioRangeFrame();
  AudioVoicesFrame();
  AudioCuesFrame(have_ears, ears_basis, ears, units_per_meter > 0.0f,
                 units_per_meter);

  // The cost of this work on the game thread. Each QMixer call takes the
  // lock of the mixer, so a long wait here would show as a stall.
  LARGE_INTEGER end, frequency;
  QueryPerformanceCounter(&end);
  QueryPerformanceFrequency(&frequency);
  const double us = 1e6 * static_cast<double>(end.QuadPart - start.QuadPart) /
                    static_cast<double>(frequency.QuadPart);
  g_work_us += us;
  if (us > g_work_worst_us) g_work_worst_us = us;
  if (++g_work_frames >= 1800) {
    ModLog("audio        : the sound work took %.0f us a frame on average, "
           "%.0f us at most, over %d frames", g_work_us / g_work_frames,
           g_work_worst_us, g_work_frames);
    g_work_us = 0.0;
    g_work_worst_us = 0.0;
    g_work_frames = 0;
  }
}

// Ctrl Shift F12. Three steps, and the log says which one it is now. The
// head. The head, but the game camera decides which sounds play, as with
// audio_range=0. The game camera, which is the sound of the game.
void AudioCycleMode() {
  if (!g_installed) {
    ModLog("audio        : test key, but the audio is off or not loaded");
    return;
  }
  g_mode = (g_mode + 1) % 5;
  AudioRolloffSetLinear(g_mode >= 1);
  AudioVoicesAudible(g_mode <= 1);
  AudioRangeSetTestOff(g_mode >= 3);
  ModLog("audio        : test key, step %d of 5: %s", g_mode + 1,
         g_mode == 0 ? "all as set: the mixer of the mod, and the zoom "
                       "changes how far a sound carries"
         : g_mode == 1 ? "the distances are the same at each zoom (the sound "
                         "before audio_zoom_pct)"
         : g_mode == 2 ? "also QMixer pans each sound, with no height (the "
                         "sound before audio_mixer)"
         : g_mode == 3 ? "also the game camera decides which sounds play "
                         "(the sound before audio_range)"
                       : "the ears are on the game camera (the sound of the "
                         "game)");
}

// The step of the Ctrl Shift F12 test key for the debug page: 0 to 4.
int AudioModeValue() { return g_installed ? g_mode : kDebugCannot; }
