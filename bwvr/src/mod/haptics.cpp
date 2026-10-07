// The pulses of the hands. See docs/hand.md, "Haptics".
//
// ONE SET. Before this file each part of the mod sent its own pulse, with its
// own amplitude and time: 1.0 for 100 ms at a hard slap, 0.6 for 80 ms when
// an object came into the Hand. Now each pulse has a kind
// (haptics_math::Kind), and the kind gives the amplitude and the time. Each
// is short and none is more than 0.75, so the hands tick and do not buzz.
//
// ONLY WHAT THE PLAYER DID. A pulse follows an act of the player: a grip, a
// slap, a stroke, a release, a new target under the hand or the aim. Nothing
// pulses on its own.
//
// haptics_pct (100) scales the amplitude of each kind. 0 turns the pulses of
// this file off. controller_haptics=0 turns each pulse off, as before. The
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

int ReadPct() {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return 100;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr || (slash - path) + 10 >= MAX_PATH) return 100;
  slash[1] = 0;
  strcat_s(path, "bwvr.ini");
  int pct = static_cast<int>(GetPrivateProfileIntA("bwvr", "haptics_pct", 100,
                                                    path));
  if (pct < 0) pct = 0;
  if (pct > 200) pct = 200;
  return pct;
}

}  // namespace

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
  ++g_counts[kind];
  if (g_kind_logs[kind] < kMaxKindLogs) {
    ++g_kind_logs[kind];
    ModLog("haptics      : %s, %.2f for %.0f ms (strength %.2f, %s), frame "
           "%llu", kNames[kind], pulse.amplitude, pulse.seconds * 1000.0f,
           strength,
           hands == 3 ? "both hands"
           : hands == 1 ? "the other hand" : "the pointing hand",
           ControllersFrameCount());
  }
  ControllersGrabPulse(hands, pulse.amplitude, pulse.seconds);
}

void HapticsPulse(int kind, float strength) {
  HapticsPulseHands(2, kind, strength);
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
      // body, also for a hand that is still (run hk5: 6 times in 60 frames).
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
}
