// The pulses of the hands: the pure math. See docs/hand-model.md, "Haptics".
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
#include <stdint.h>

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
  // THE WORLD (world_haptics=1): the hand feels what it touches.
  kPurr,      // the game counts a stroke of the Creature
  kHeart,     // a beat of a villager or an animal in the Hand
  kLand,      // the land moves under a grip: one tick for each step of it
  kThump,     // a thrown object lands, or the Hand pulls a tree out
  kCharge,    // a step of the charge of a miracle
  kPiece,     // a piece of a take comes into the Hand
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
    {0.12f, 0.012f},  // purr
    {0.12f, 0.012f},  // heart
    {0.06f, 0.006f},  // land
    {0.55f, 0.030f},  // thump
    {0.30f, 0.012f},  // charge
    {0.10f, 0.008f},  // piece
};

const char* const kNames[kKinds] = {
    "target",  "lock",      "arrive",  "take",     "let go",  "activate",
    "no target", "touch",   "stroke",  "slap",     "leash on", "leash off",
    "command", "teleport",  "refused", "tick",     "gesture", "power",
    "menu",    "world",     "other",   "cast",     "surface", "purr",
    "heart",   "land",      "thump",   "charge",   "piece"};

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

inline bool PathTick(float* path_cm, float moved_cm, float step_cm) {
  if (!(moved_cm > 0.0f) || !(step_cm > 0.0f)) return false;
  if (moved_cm > 10.0f) moved_cm = 10.0f;
  *path_cm += moved_cm;
  if (*path_cm < step_cm) return false;
  *path_cm = fmodf(*path_cm, step_cm);
  return true;
}

inline bool StrokeTick(float* path_cm, float moved_cm) {
  return PathTick(path_cm, moved_cm, kStrokeStepCm);
}

// A FETCHED OBJECT ARRIVES. The hand pulses on each frame of the last
// kArriveSeconds of the glide to the hand, as in Half-Life: Alyx (0.125 s).
const float kArriveSeconds = 0.125f;

inline bool ArriveNow(float glide_seconds, float since_start) {
  if (!(glide_seconds > 0.0f)) return false;
  const float left = glide_seconds - since_start;
  return left >= 0.0f && left <= kArriveSeconds;
}

// THE WORLD (world_haptics=1). The hand that touches the world feels it: the
// stroke of the Creature, a living thing in the Hand, the land under a grip,
// a landing, a tree that comes out of the land, the charge of a miracle and
// the pieces of a take. Each pulse is a kind of the table, so haptics_pct
// scales it and the limits of the table hold. A pattern is a few pulses and
// never a long buzz. The frames of the mod run it (haptics.cpp).

// The kinds of the world.
inline bool IsWorld(int kind) { return kind >= kPurr && kind < kKinds; }

// THE CONTROLS STAY CLEAR. A pulse of a control (a new target, a lock, a
// take, a click) tells the player what the controls did. For
// kControlClearSeconds after a pulse of a control on a hand, that hand gets
// no pulse of the world. So a pulse of the world never cuts a pulse of a
// control short, and the player feels each control alone. The touch, the
// stroke and the slap of the Creature, the power, the cast and the surface
// are feelings and not controls.
const float kControlClearSeconds = 0.08f;

inline bool KeepsClear(int kind) {
  switch (kind) {
    case kTarget: case kLock: case kArrive: case kTake: case kLetGo:
    case kActivate: case kNoTarget: case kLeashOn: case kLeashOff:
    case kCommand: case kTeleport: case kRefused: case kTick: case kGesture:
    case kMenu: case kWorld:
      return true;
    default:
      return false;
  }
}

inline bool WorldClear(double now, double last_control) {
  return now - last_control >= kControlClearSeconds;
}

// THE PATTERNS. A step is a pulse of the kind at a time from the start, with
// a strength of 0 to 1. A pattern with a repeat starts again each repeat
// seconds, while the thing that it shows lasts.
enum Pattern {
  kPatternPurr,    // 4 soft pulses: the game counts a stroke of the Creature
  kPatternHeart,   // a beat of two pulses each 0.9 s: a living thing in the Hand
  kPatternCharge,  // 3 pulses that become stronger: a miracle charges
  kPatterns
};

const int kMostSteps = 4;

struct Step {
  float at;  // seconds from the start of the round
  float strength;
};

struct PatternDef {
  int kind;
  int steps;
  Step step[kMostSteps];
  float repeat;  // seconds, 0 for one round
};

const PatternDef kPatternDefs[kPatterns] = {
    {kPurr, 4, {{0.00f, 0.6f}, {0.06f, 1.0f}, {0.12f, 1.0f}, {0.18f, 0.6f}},
     0.0f},
    {kHeart, 2, {{0.00f, 1.0f}, {0.16f, 0.6f}, {0.0f, 0.0f}, {0.0f, 0.0f}},
     0.9f},
    {kCharge, 3, {{0.00f, 0.4f}, {0.08f, 0.7f}, {0.16f, 1.0f}, {0.0f, 0.0f}},
     0.0f},
};

const char* const kPatternNames[kPatterns] = {"purr", "heart", "charge"};

// The pulse of a pattern that is due at `since` seconds from its start.
// *next is the index of the next step, counted over the rounds of a repeat.
// Each step that is due moves *next on. Two steps that are due in one frame
// give one pulse with the larger strength. A pattern with a repeat skips each
// round that a long frame or a pause jumped over. The result is the strength
// of the pulse now, or 0 for no pulse. *done is true when a pattern with no
// repeat has no step left.
inline float PatternDue(int pattern, float since, int* next, bool* done) {
  *done = false;
  if (pattern < 0 || pattern >= kPatterns) {
    *done = true;
    return 0.0f;
  }
  const PatternDef& d = kPatternDefs[pattern];
  if (d.steps <= 0 || d.steps > kMostSteps) {
    *done = true;
    return 0.0f;
  }
  if (*next < 0) *next = 0;
  if (d.repeat > 0.0f && since > 0.0f) {
    const int now_round = static_cast<int>(since / d.repeat);
    if (*next < now_round * d.steps) *next = now_round * d.steps;
  }
  float strength = 0.0f;
  for (int guard = 0; guard < kMostSteps; ++guard) {
    if (!(d.repeat > 0.0f) && *next >= d.steps) break;
    const int round = *next / d.steps;
    const int i = *next % d.steps;
    const float at = round * d.repeat + d.step[i].at;
    if (since < at) break;
    if (d.step[i].strength > strength) strength = d.step[i].strength;
    ++*next;
  }
  if (!(d.repeat > 0.0f) && *next >= d.steps) *done = true;
  return strength;
}

// The length of one round of a pattern, from the start of its first pulse to
// the end of its last pulse.
inline float PatternSeconds(int pattern) {
  if (pattern < 0 || pattern >= kPatterns) return 0.0f;
  const PatternDef& d = kPatternDefs[pattern];
  if (d.steps <= 0 || d.steps > kMostSteps) return 0.0f;
  return d.step[d.steps - 1].at + kPulses[d.kind].seconds;
}

// THE LAND UNDER A GRIP. One faint tick for each kLandStepCm that the land
// moves in the room under the grip. The land moves with the hand, so a hand
// that is still gives none, and land that a limit holds gives none.
const float kLandStepCm = 2.0f;

inline bool LandTick(float* path_cm, float moved_cm) {
  return PathTick(path_cm, moved_cm, kLandStepCm);
}

// THE THUMP OF A LANDING, by the speed of the object in the room at its
// first contact: 0.35 at 0.5 m/s or less, and 1 at 3 m/s or more.
inline float ThumpBySpeed(float mps) {
  if (!(mps > 0.5f)) return 0.35f;
  if (mps >= 3.0f) return 1.0f;
  return 0.35f + 0.65f * (mps - 0.5f) / 2.5f;
}

// THE THUMP OF A TREE THAT COMES OUT OF THE LAND, by the radius of its body
// in the room: 0.5 at 1 cm or less, and 1 at 6 cm or more. A radius that is
// not known gives 0.7.
inline float ThumpBySize(float radius_cm) {
  if (!(radius_cm > 0.0f)) return 0.7f;
  if (radius_cm <= 1.0f) return 0.5f;
  if (radius_cm >= 6.0f) return 1.0f;
  return 0.5f + 0.5f * (radius_cm - 1.0f) / 5.0f;
}

// THE PIECES OF A TAKE. The game deletes a piece of the stream when its age
// passes its life, and then the piece is in the Hand. was and was_age are
// the pieces of the last frame, now and now_age those of this frame. A piece
// of the last frame that is gone now, or that has a younger piece at its
// address now, came into the Hand. The result is the count of those pieces.
inline int PiecesIn(const uintptr_t* was, const float* was_age, int was_count,
                    const uintptr_t* now, const float* now_age,
                    int now_count) {
  int in = 0;
  for (int i = 0; i < was_count; ++i) {
    bool kept = false;
    for (int j = 0; j < now_count; ++j) {
      if (now[j] == was[i] && !(now_age[j] < was_age[i])) {
        kept = true;
        break;
      }
    }
    if (!kept) ++in;
  }
  return in;
}

}  // namespace haptics_math
