// The pulses of the hands. See docs/hand-model.md, "Haptics".
//
// ONE SET. Each pulse has a kind (haptics_math::Kind), and the kind gives the
// amplitude and the time. Each is short and none is more than 0.75, so the hands tick and do not buzz.
//
// ONLY WHAT THE PLAYER DID. A pulse follows an act of the player: a grip, a
// slap, a stroke, a release, a new target under the hand or the aim. Nothing
// pulses on its own. A pulse of the world follows an act too: the world
// answers in the hand that touches it.
//
// THE WORLD (world_haptics=1, docs/hand-model.md, "Haptics from the world").
// The hand that does an act feels what the world does: a purr at the game's
// stroke of the Creature, a beat while the Hand holds a villager or an
// animal, a texture while the land moves under a grip, a thump when a thrown
// object lands or the Hand pulls a tree out, a build up while a miracle
// charges, and a tick for each piece of a take. The patterns are in
// haptics_math.h. HapticsWorldFrame runs them once a frame, from the frames
// of the mod and not from a thread. For 80 ms after a pulse of a control on a
// hand, that hand gets no pulse of the world, so a new target and a lock stay
// clear. world_haptics=0 gives no pulse of the world.
//
// haptics_pct (100) scales the amplitude of each kind. 0 turns the pulses of
// this file off. controller_haptics=0 turns each pulse off. The
// pulses of the test keys do not go through this file, so they stay as they
// are at each value.
//
// THE GAME'S OWN EFFECTS (GInterface::StartImmersion, controllers.cpp). The
// game starts an effect for a pick up, a release, a touch and a slap of the
// Creature, a gesture and a menu click. The mod has its own pulse for the pick
// up and the release, at the press and at the opening of the grip, which are
// some frames before the game's effects. So those effects give no pulse. The
// slap keeps the game's effect as its moment, and takes its strength from the
// speed of the hand.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "haptics.h"

extern void ModLog(const char* format, ...);

// controllers.cpp: a pulse on the hands of the mask (bit 0 the other hand,
// bit 1 the pointing hand), and the frame count.
void ControllersGrabPulse(int hands, float amplitude, float seconds);
uint64_t ControllersFrameCount();

// audio_cues.cpp: the cue that goes with a pulse of this kind, if it has one.
void AudioCueHaptic(int kind);

namespace {

using namespace haptics_math;

int g_pct = -1;
uint64_t g_counts[kKinds] = {0};
int g_kind_logs[kKinds] = {0};
const int kMaxKindLogs = 6;
// The speed of the last slap, for the game's slap effects.
float g_slap_mps = 0.0f;
float g_slap_least = 0.0f;
uint64_t g_slap_frame = 0;

int ReadKey(const char* key, int fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return fallback;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr || (slash - path) + 10 >= MAX_PATH) return fallback;
  slash[1] = 0;
  strcat_s(path, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

int ReadPct() {
  int pct = ReadKey("haptics_pct", 100);
  if (pct < 0) pct = 0;
  if (pct > 200) pct = 200;
  return pct;
}

// THE WORLD.
int g_world = -1;  // world_haptics
// The time of the last pulse of a control on each hand: 0 the other hand, 1
// the pointing hand.
double g_control_at[2] = {-1e9, -1e9};
// The patterns that run, one of each.
struct Running {
  bool on;
  int hands;
  float strength;
  double start;
  int next;
};
Running g_running[kPatterns] = {};
int g_pattern_logs[kPatterns] = {0};
uint64_t g_pattern_starts[kPatterns] = {0};
// The frame of the last call of HapticsWorldHold that said true.
uint64_t g_hold_frame = 0;
// Pulses of the world that a hand did not get, for a pulse of a control.
uint64_t g_held_back = 0;
int g_held_back_logs = 0;

double Now() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return frequency.QuadPart > 0
             ? static_cast<double>(count.QuadPart) / frequency.QuadPart
             : GetTickCount64() / 1000.0;
}

const char* HandsName(int hands) {
  return hands == 3   ? "both hands"
         : hands == 1 ? "the other hand"
                      : "the pointing hand";
}

}  // namespace

int HapticsWorldOn() {
  if (g_world < 0) {
    g_world = ReadKey("world_haptics", 1) != 0 ? 1 : 0;
    ModLog("haptics      : world_haptics=%d. %s", g_world,
           g_world != 0
               ? "The hand feels the world: a purr at a stroke of the "
                 "Creature, a beat of a villager or an animal in the Hand, the "
                 "land under a grip, a thump at a landing and at a tree that "
                 "comes out of the land, the charge of a miracle, and each "
                 "piece of a take. A hand gets no pulse of the world for 80 ms "
                 "after a pulse of a control"
               : "No pulse from the world. The pulses of the controls and of "
                 "the Creature stay");
  }
  return g_world;
}

int HapticsPct() {
  if (g_pct < 0) {
    g_pct = ReadPct();
    ModLog("haptics      : haptics_pct=%d. Each pulse of the mod has a kind, "
           "at most %.0f ms long and at most %.2f strong. %s", g_pct,
           kMostSeconds * 1000.0f, kMostAmplitude,
           g_pct == 0 ? "0 turns the pulses of the mod off"
                      : "0 turns them off");
  }
  return g_pct;
}

void HapticsPulseHands(int hands, int kind, float strength) {
  // The soft sound of the same move starts here too, so the hand and the
  // ear agree in time (audio_cues.cpp). It plays also with the pulses off.
  AudioCueHaptic(kind);
  const Pulse pulse = Scaled(kind, strength, HapticsPct());
  if (!(pulse.amplitude > 0.0f)) return;
  if (KeepsClear(kind)) {
    const double now = Now();
    if ((hands & 1) != 0) g_control_at[0] = now;
    if ((hands & 2) != 0) g_control_at[1] = now;
  }
  ++g_counts[kind];
  if (g_kind_logs[kind] < kMaxKindLogs) {
    ++g_kind_logs[kind];
    ModLog("haptics      : %s, %.2f for %.0f ms (strength %.2f, %s), frame "
           "%llu", kNames[kind], pulse.amplitude, pulse.seconds * 1000.0f,
           strength, HandsName(hands), ControllersFrameCount());
  }
  ControllersGrabPulse(hands, pulse.amplitude, pulse.seconds);
}

void HapticsPulse(int kind, float strength) {
  HapticsPulseHands(2, kind, strength);
}

int HapticsPulseHandsClear(int hands, int kind, float strength) {
  const double now = Now();
  int clear = hands & 3;
  if ((clear & 1) != 0 && !WorldClear(now, g_control_at[0])) clear &= ~1;
  if ((clear & 2) != 0 && !WorldClear(now, g_control_at[1])) clear &= ~2;
  if (clear != (hands & 3)) {
    ++g_held_back;
    if (g_held_back_logs < kMaxKindLogs) {
      ++g_held_back_logs;
      ModLog("haptics      : %s held back on %s: a pulse of a control went "
             "there less than 80 ms ago, frame %llu", kNames[kind],
             HandsName((hands & 3) & ~clear), ControllersFrameCount());
    }
  }
  if (clear != 0) HapticsPulseHands(clear, kind, strength);
  return clear;
}

void HapticsWorld(int hands, int kind, float strength) {
  if (!IsWorld(kind) || HapticsWorldOn() == 0) return;
  // THE CONTROLS STAY CLEAR: no pulse of the world on a hand that had a
  // pulse of a control in the last 80 ms.
  const double now = Now();
  int clear = hands & 3;
  if ((clear & 1) != 0 && !WorldClear(now, g_control_at[0])) clear &= ~1;
  if ((clear & 2) != 0 && !WorldClear(now, g_control_at[1])) clear &= ~2;
  if (clear != (hands & 3)) {
    ++g_held_back;
    if (g_held_back_logs < kMaxKindLogs) {
      ++g_held_back_logs;
      const int back = (hands & 3) & ~clear;
      const double control = (back & 2) != 0 ? g_control_at[1]
                                             : g_control_at[0];
      ModLog("haptics      : %s held back on %s: a pulse of a control went "
             "there %.0f ms ago, frame %llu", kNames[kind], HandsName(back),
             (now - control) * 1000.0, ControllersFrameCount());
    }
  }
  if (clear == 0) return;
  HapticsPulseHands(clear, kind, strength);
}

void HapticsWorldStart(int pattern, int hands, float strength) {
  if (pattern < 0 || pattern >= kPatterns || HapticsWorldOn() == 0) return;
  Running& r = g_running[pattern];
  r.on = true;
  r.hands = hands;
  r.strength = strength;
  r.start = Now();
  r.next = 0;
  ++g_pattern_starts[pattern];
  const PatternDef& d = kPatternDefs[pattern];
  if (g_pattern_logs[pattern] < kMaxKindLogs) {
    ++g_pattern_logs[pattern];
    const Pulse most = Scaled(d.kind, strength, HapticsPct());
    if (d.repeat > 0.0f) {
      ModLog("haptics      : the pattern %s starts on %s: %d pulses of %s in "
             "%.0f ms each %.1f s, %.2f at most and %.0f ms each, frame %llu",
             kPatternNames[pattern], HandsName(hands), d.steps,
             kNames[d.kind], PatternSeconds(pattern) * 1000.0f, d.repeat,
             most.amplitude, most.seconds * 1000.0f, ControllersFrameCount());
    } else {
      ModLog("haptics      : the pattern %s starts on %s: %d pulses of %s in "
             "%.0f ms, %.2f at most and %.0f ms each, frame %llu",
             kPatternNames[pattern], HandsName(hands), d.steps,
             kNames[d.kind], PatternSeconds(pattern) * 1000.0f,
             most.amplitude, most.seconds * 1000.0f, ControllersFrameCount());
    }
  }
  HapticsWorldFrame();
}

void HapticsWorldStop(int pattern) {
  if (pattern < 0 || pattern >= kPatterns) return;
  g_running[pattern].on = false;
}

void HapticsWorldHold(bool living) {
  if (!living) {
    HapticsWorldStop(kPatternHeart);
    return;
  }
  g_hold_frame = ControllersFrameCount();
  if (!g_running[kPatternHeart].on) HapticsWorldStart(kPatternHeart, 2);
}

void HapticsWorldFrame() {
  if (HapticsWorldOn() == 0) return;
  // THE BEAT STOPS when the frame code no longer says that the Hand holds a
  // living thing, for example when the controller no longer places the Hand.
  if (g_running[kPatternHeart].on &&
      g_hold_frame + 2 < ControllersFrameCount()) {
    HapticsWorldStop(kPatternHeart);
  }
  const double now = Now();
  for (int p = 0; p < kPatterns; ++p) {
    Running& r = g_running[p];
    if (!r.on) continue;
    bool done = false;
    const float s =
        PatternDue(p, static_cast<float>(now - r.start), &r.next, &done);
    if (s > 0.0f) HapticsWorld(r.hands, kPatternDefs[p].kind, s * r.strength);
    if (done) r.on = false;
  }
}

void HapticsSlapSpeed(float mps, float slap_mps) {
  g_slap_mps = mps;
  g_slap_least = slap_mps;
  g_slap_frame = ControllersFrameCount();
}

void HapticsGameEffect(int effect) {
  switch (effect) {
    case 0:  // touch the Creature
      // The game starts this effect again and again while the Hand is in the
      // body, also for a hand that is still (6 times in 60 frames).
      // The mod gives one tick when the hand goes into the body, and the
      // texture of the stroke (hand_direct.cpp, TouchFrame).
      break;
    case 1:    // soft slap
    case 2: {  // hard slap
      // The speed that the mod read in this frame or the one before. With no
      // speed, the two levels of the game.
      const bool known = g_slap_frame + 2 >= ControllersFrameCount() &&
                         g_slap_least > 0.0f;
      HapticsPulse(kSlap, known ? SlapStrength(g_slap_mps, g_slap_least)
                          : effect == 2 ? 1.0f : 0.55f);
      break;
    }
    case 3:
      HapticsPulse(kGesture);
      break;
    case 4:   // a pick up starts
    case 5:   // the object is in the Hand
    case 6:   // the Hand is over an object
    case 46:  // the release
      // The mod has its own pulse for each of these: at the press of the
      // grip, at the new target, and at the opening of the grip.
      break;
    case 7:  // the leash goes on
      // The game starts it one frame before the leash is on (run hkQ: frames
      // 815 and 982, with the leash on at 816 and 983). The mod gives the
      // pulse "leash on" for a leash that the player asked for, and a leash
      // that the game puts on by itself gets none.
      break;
    case 8:
    case 9:
      HapticsPulse(kPower);
      break;
    case 14:  // a one-off seed pops, and its miracle is in the Hand
      HapticsPulse(kActivate);
      break;
    case 18:  // a fireball of level 1, 2 and 3
    case 19:
    case 20:
    case 21:  // lightning
    case 23:  // a miracle is cast
    case 24:  // a shield
    case 25:  // water
    case 26:  // a flock that flies
    case 27:  // a flock on the land
    case 28:  // a Creature miracle
      // The cast ends the build up of the charge.
      HapticsWorldStop(kPatternCharge);
      HapticsPulse(kCast);
      break;
    case 44:
      HapticsPulse(kMenu);
      break;
    default:
      HapticsPulse(kOther);
      break;
  }
}

void HapticsReport() {
  uint64_t all = 0;
  for (int i = 0; i < kKinds; ++i) all += g_counts[i];
  if (all == 0) return;
  char text[512] = "";
  size_t used = 0;
  for (int i = 0; i < kKinds && used + 32 < sizeof(text); ++i) {
    if (g_counts[i] == 0) continue;
    used += static_cast<size_t>(sprintf_s(text + used, sizeof(text) - used,
                                          "%s%llu %s", used > 0 ? ", " : "",
                                          g_counts[i], kNames[i]));
    g_counts[i] = 0;
  }
  ModLog("haptics      : %llu pulses this window (haptics_pct=%d): %s", all,
         HapticsPct(), text);
  uint64_t starts = 0;
  for (int i = 0; i < kPatterns; ++i) starts += g_pattern_starts[i];
  if (starts > 0 || g_held_back > 0) {
    ModLog("haptics      : the world this window (world_haptics=%d): %llu "
           "purr, %llu heart and %llu charge patterns started, and %llu "
           "pulses of the world held back for a pulse of a control",
           HapticsWorldOn(), g_pattern_starts[kPatternPurr],
           g_pattern_starts[kPatternHeart], g_pattern_starts[kPatternCharge],
           g_held_back);
    for (int i = 0; i < kPatterns; ++i) g_pattern_starts[i] = 0;
    g_held_back = 0;
  }
}
