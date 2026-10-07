// The pulses of the hands: the pure math. See docs/hand.md, "Haptics".
// haptics.cpp uses it, and test/controller_math checks it (haptics_checks.h).
//
// ONE SET. Each pulse of the mod is one of the kinds below, so the hands feel
// the same for the same thing, and one key (haptics_pct) sets the strength of
// all. Each pulse is short, and none is stronger than kMostAmplitude: a tick,
// not a buzz. Half-Life: Alyx uses three levels (0.05, 0.40 and 0.75) and
// pulses of about 11 ms (docs/research-hla.md). The levels here are a little
// over its lowest one, because a Touch controller does not show 0.05 at 11 ms.

#pragma once

#include <math.h>

namespace haptics_math {

enum Kind {
  kTarget,    // a new target of the grip: a far target, or one at the Hand
  kLock,      // the far grab locks its target
  kArrive,    // a fetched object comes into the hand: each frame at the end
  kTake,      // the grip closes on an object to hold or to take from
  kLetGo,     // the Hand lets an object go
  kActivate,  // the grip taps: a scroll, a house, the far touch of a scroll
  kNoTarget,  // the grip closes with no target
  kTouch,     // the hand goes into the body of the Creature
  kStroke,    // the texture of a stroke: one tick for each step of the path
  kSlap,      // a slap of the Creature, by its speed
  kLeashOn,   // the leash goes on the Creature, or comes back to the Hand
  kLeashOff,  // the leash comes off
  kCommand,   // the trigger sends the Creature
  kTeleport,  // the teleport goes
  kRefused,   // the game or the mod refused what the player asked for
  kTick,      // a step of a control: the second tick of the teleport aim
  kGesture,   // the game knows the gesture of a miracle
  kPower,     // a miracle powers up
  kMenu,      // a click in a menu
  kWorld,     // a grip takes the world
  kOther,     // an effect of the game that has no name yet
  kCast,      // the game casts a miracle
  kSurface,   // a finger first meets the land or a body (hand_feel.cpp)
  kKinds
};

struct Pulse {
  float amplitude;  // 0 to 1
  float seconds;
};

const float kMostAmplitude = 0.75f;
const float kMostSeconds = 0.040f;

// The pulse of each kind at haptics_pct=100 and a strength of 1.
const Pulse kPulses[kKinds] = {
    {0.08f, 0.008f},  // target
    {0.20f, 0.010f},  // lock
    {0.40f, 0.011f},  // arrive
    {0.30f, 0.015f},  // take
    {0.20f, 0.010f},  // let go
    {0.25f, 0.012f},  // activate
    {0.06f, 0.008f},  // no target
    {0.12f, 0.010f},  // touch
    {0.07f, 0.006f},  // stroke
    {0.75f, 0.025f},  // slap
    {0.30f, 0.020f},  // leash on
    {0.20f, 0.012f},  // leash off
    {0.25f, 0.015f},  // command
    {0.30f, 0.020f},  // teleport
    {0.35f, 0.030f},  // refused
    {0.15f, 0.010f},  // tick
    {0.40f, 0.030f},  // gesture
    {0.30f, 0.040f},  // power
    {0.15f, 0.010f},  // menu
    {0.15f, 0.010f},  // world
    {0.20f, 0.015f},  // other
    {0.45f, 0.030f},  // cast
    {0.06f, 0.008f},  // surface
};

const char* const kNames[kKinds] = {
    "target",  "lock",      "arrive",  "take",     "let go",  "activate",
    "no target", "touch",   "stroke",  "slap",     "leash on", "leash off",
    "command", "teleport",  "refused", "tick",     "gesture", "power",
    "menu",    "world",     "other",   "cast",     "surface"};

// The pulse of a kind: the table, times the strength of this event (0 to 1),
// times haptics_pct. Never more than kMostAmplitude and kMostSeconds. A pct of
// 0, and a kind that is not in the table, give no pulse.
inline Pulse Scaled(int kind, float strength, int pct) {
  Pulse out = {0.0f, 0.0f};
  if (kind < 0 || kind >= kKinds || pct <= 0 || !(strength > 0.0f)) return out;
  if (strength > 1.0f) strength = 1.0f;
  if (pct > 200) pct = 200;
  out.amplitude = kPulses[kind].amplitude * strength * pct * 0.01f;
  if (out.amplitude > kMostAmplitude) out.amplitude = kMostAmplitude;
  out.seconds = kPulses[kind].seconds;
  if (out.seconds > kMostSeconds) out.seconds = kMostSeconds;
  return out;
}

// THE STRENGTH OF A SLAP, by its speed: 0.55 at the least speed of a slap,
// and 1 at the speed of a hard slap (1.8 times that) and past it. So a slap
// that just counts is 0.41 of the full amplitude, and a hard one is 0.75.
inline float SlapStrength(float mps, float slap_mps) {
  if (!(slap_mps > 0.0f) || !(mps > 0.0f)) return 0.55f;
  const float u = (mps / slap_mps - 1.0f) / 0.8f;
  if (!(u > 0.0f)) return 0.55f;
  if (u >= 1.0f) return 1.0f;
  return 0.55f + 0.45f * u;
}

// THE TEXTURE OF A STROKE. One faint tick for each kStrokeStepCm of path of
// the contact point in the body. The path of the real hand makes the ticks: a
// hand that is still gives none, and a fast stroke gives a fine buzz. path_cm
// keeps what is left of the path. A jump of the tracking counts as 10 cm, and
// one call gives one tick at most.
const float kStrokeStepCm = 1.5f;

inline bool StrokeTick(float* path_cm, float moved_cm) {
  if (!(moved_cm > 0.0f)) return false;
  if (moved_cm > 10.0f) moved_cm = 10.0f;
  *path_cm += moved_cm;
  if (*path_cm < kStrokeStepCm) return false;
  *path_cm = fmodf(*path_cm, kStrokeStepCm);
  return true;
}

// A FETCHED OBJECT ARRIVES. The hand pulses on each frame of the last
// kArriveSeconds of the glide to the hand, as in Half-Life: Alyx (0.125 s).
const float kArriveSeconds = 0.125f;

inline bool ArriveNow(float glide_seconds, float since_start) {
  if (!(glide_seconds > 0.0f)) return false;
  const float left = glide_seconds - since_start;
  return left >= 0.0f && left <= kArriveSeconds;
}

}  // namespace haptics_math
