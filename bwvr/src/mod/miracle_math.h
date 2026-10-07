// A miracle in the Hand: the pure math. See docs/hand.md, "A miracle in the
// Hand". miracle.cpp uses it, and test/controller_math checks it
// (miracle_checks.h).

#pragma once

#include <math.h>

namespace miracle_math {

// HOW THE GAME CASTS A MIRACLE: GSpellSeedInfo+0x110 [V].
//   0  The miracle is cast in the Hand: it pours while the Action button is
//      held (food, wood, water, lightning).
//   1  The miracle is cast at the release of the Action button, after the
//      game recorded the path of the Hand: a throw (fireball, the flocks), or
//      a circle on the land (storm, the two shields).
//   2  The miracle is cast at the press: on an object (the Creature
//      miracles), or at the land point (forest, heal, teleport, explosion).
enum Cast { kCastPour = 0, kCastThrow = 1, kCastPlace = 2 };

// The facts of the miracle that the Hand holds.
struct Seed {
  int type;        // SPELL_SEED_TYPE, 0 to 29
  int cast;        // Cast
  bool kept;       // +0x114: the seed stays in the Hand while the spell runs
  bool on_object;  // +0x118: the game can cast it on an object
  int gesture;     // +0x10C: the gesture that the cast needs, or 0
};

// WHAT THE CARD SAYS. One state of the Hand gives one set of lines.
enum Prompt {
  kPromptNone,
  kPromptSeedHeld,     // a one-off seed is in the Hand as an object
  kPromptTarget,       // an object is the target of a press, and it has the
                       // halo
  kPromptNeedTarget,   // a miracle for an object, and no target
  kPromptPlace,        // a miracle for a place: a press casts it at the Hand
  kPromptPour,         // a miracle to pour: a closed grip pours
  kPromptPouring,      // the grip is closed, and the miracle pours
  kPromptThrow,        // a miracle to throw
  kPromptCircle,       // a miracle that needs a circle on the land
  kPromptHeld,         // the grip is closed with a miracle to throw
  kPromptRuns,         // the spell runs, and the seed is still in the Hand
};

inline int PromptOf(bool seed_held, bool miracle, const Seed& s,
                    bool have_target, bool grip_down, bool spell_runs) {
  if (seed_held) return kPromptSeedHeld;
  if (!miracle) return kPromptNone;
  if (spell_runs && s.kept && s.cast != kCastPour) return kPromptRuns;
  // An object that the game casts this miracle on is the target of the next
  // press, for each kind of miracle: the game looks at the object of the pick
  // record first (FUN_005D1560). A Worship Site takes each of its miracles
  // back that way.
  if (have_target && !grip_down) return kPromptTarget;
  switch (s.cast) {
    case kCastPour:
      return grip_down ? kPromptPouring : kPromptPour;
    case kCastThrow:
      if (grip_down) return kPromptHeld;
      return s.gesture != 0 ? kPromptCircle : kPromptThrow;
    default:
      if (s.on_object) return have_target ? kPromptTarget : kPromptNeedTarget;
      return kPromptPlace;
  }
}

// WHY THE GAME REFUSED A CAST. The game shows a yellow cross and plays a
// sound (fn_005D18F0), and says no more. These are its tests, in its order
// (SpellSeed::ValidToApplyThisToMapCoord, GMagicInfo cast rule fn_005FB5D0,
// the gesture test fn_00729AF0).
enum Refusal {
  kRefuseNone,
  kRefuseNotReady,    // the seed is not ready yet (SpellSeed+0x90)
  kRefuseOffMap,      // the land point is not on the map
  kRefuseNeedsLand,   // the cast rule needs land, and the point is water
  kRefuseInfluence,   // the point is outside the influence
  kRefuseNeedsTarget, // a miracle for an object, cast with no object
  kRefuseNeedsCircle, // the miracle needs its gesture first
  kRefuseOther,
};

// CAST_RULE_TYPE of the game: 0 anywhere, 1 on land, 2 in the influence, 3 on
// land in the influence.
inline int RefusalOf(bool ready, bool on_map, bool land, bool influence,
                     int cast_rule, const Seed& s, bool had_object,
                     int gesture_drawn) {
  if (!ready) return kRefuseNotReady;
  if (!on_map) return kRefuseOffMap;
  if ((cast_rule == 1 || cast_rule == 3) && !land) return kRefuseNeedsLand;
  if ((cast_rule == 2 || cast_rule == 3) && !influence) return kRefuseInfluence;
  if (s.cast == kCastThrow && s.gesture != 0 && gesture_drawn != s.gesture) {
    return kRefuseNeedsCircle;
  }
  if (s.on_object && !had_object) return kRefuseNeedsTarget;
  // The release of a throw and the pour need the influence at the Hand
  // (GInterface+0x48) with each cast rule.
  if (!influence) return kRefuseInfluence;
  return kRefuseOther;
}

inline const char* RefusalWords(int refusal) {
  switch (refusal) {
    case kRefuseNotReady: return "the miracle is not ready yet";
    case kRefuseOffMap: return "the Hand is not over the map";
    case kRefuseNeedsLand: return "this miracle needs land under the Hand";
    case kRefuseInfluence: return "the place is outside your influence";
    case kRefuseNeedsTarget: return "this miracle needs a Creature as its target";
    case kRefuseNeedsCircle: return "this miracle needs a circle on the land first";
    case kRefuseOther: return "the game gave no reason that the mod knows";
    default: return "";
  }
}

// THE TARGET OF A MIRACLE FOR AN OBJECT. The target is the body that the
// Hand's point is in or near (`touch` world units), and else the body nearest
// to the aim ray, inside the cone. A body at the Hand wins against each body
// in the aim.
struct Aim {
  bool valid;
  bool at_hand;
  float gap;      // from the Hand's point to the body, in world units
  float degrees;  // from the aim ray to the body
};

// A ball of `radius` about `center`. hand is the Hand's point. origin and
// direction (a unit vector) are the aim ray. most is the most distance from
// the origin, in world units, or 0 for no limit.
inline Aim AimAt(const float hand[3], const float origin[3],
                 const float direction[3], bool have_ray,
                 const float center[3], float radius, float touch,
                 float cone_degrees, float most) {
  Aim out = {false, false, 0.0f, 0.0f};
  const float hx = center[0] - hand[0], hy = center[1] - hand[1],
              hz = center[2] - hand[2];
  float gap = sqrtf(hx * hx + hy * hy + hz * hz) - radius;
  if (gap < 0.0f) gap = 0.0f;
  out.gap = gap;
  if (gap <= touch) {
    out.valid = true;
    out.at_hand = true;
    return out;
  }
  if (!have_ray) return out;
  const float ox = center[0] - origin[0], oy = center[1] - origin[1],
              oz = center[2] - origin[2];
  const float along = ox * direction[0] + oy * direction[1] + oz * direction[2];
  if (!(along > 0.0f)) return out;
  const float d2 = ox * ox + oy * oy + oz * oz;
  if (most > 0.0f && d2 > most * most) return out;
  float across2 = d2 - along * along;
  if (across2 < 0.0f) across2 = 0.0f;
  float across = sqrtf(across2) - radius;
  if (across < 0.0f) across = 0.0f;
  out.degrees = atan2f(across, along) * 57.29578f;
  out.valid = out.degrees <= cone_degrees;
  return out;
}

// True when a is the better target. A body at the Hand wins, then the smaller
// gap. In the aim the smaller angle wins, and for the same angle the nearer
// body (the smaller gap).
inline bool Better(const Aim& a, const Aim& b) {
  if (!b.valid) return a.valid;
  if (!a.valid) return false;
  if (a.at_hand != b.at_hand) return a.at_hand;
  if (a.at_hand) return a.gap < b.gap;
  if (a.degrees != b.degrees) return a.degrees < b.degrees;
  return a.gap < b.gap;
}

// THE TARGET STAYS. A target that moves, a Creature that walks, must not
// leave the halo at the edge of the cone. So the target of the last frame is
// measured with wider limits (the keep limits), and a new body takes its
// place only when it is clearly the better one: at the Hand while the kept
// target is in the aim, nearer at the Hand, or `margin` degrees nearer to
// the ray. a is the new body under the limits of a new target. kept is the
// target of the last frame under the keep limits.
inline bool Takes(const Aim& a, const Aim& kept, float margin_degrees) {
  if (!a.valid) return false;
  if (!kept.valid) return true;
  if (a.at_hand != kept.at_hand) return a.at_hand;
  if (a.at_hand) return a.gap < kept.gap;
  return a.degrees + margin_degrees < kept.degrees;
}

// THE CANCEL BUTTON. The second face button of the pointing hand also draws
// the gestures of the game while it is held. A short press with a still hand
// is the cancel: it is too short and too small to be a gesture.
inline bool CancelTap(float held_seconds, float moved_meters,
                      float most_seconds, float most_meters) {
  return held_seconds >= 0.0f && held_seconds <= most_seconds &&
         moved_meters <= most_meters;
}

// USE A SEED THAT THE HAND CARRIES. The game makes a one-off seed active
// with a tap, and the tap needs an empty Hand. So the mod lets the seed go,
// waits until the Hand is empty, and then sends the tap.
enum UseStage { kUseIdle, kUseLetGo, kUseTap, kUseWait };

struct Use {
  int stage;
  int frames;  // in this stage
};

// One frame. hand_empty: the Hand holds nothing. seed_lives: the seed that
// was in the Hand is still an object of the game. miracle: a miracle is in
// the Hand now. *tap is true on the frame that the mod sends the tap. The
// return value is false when the use ended with no miracle.
inline bool UseStep(Use* u, bool hand_empty, bool seed_lives, bool miracle,
                    int most_frames, bool* tap) {
  *tap = false;
  if (u->stage == kUseIdle) return true;
  ++u->frames;
  if (miracle) {
    u->stage = kUseIdle;
    return true;
  }
  if (u->frames > most_frames || (!seed_lives && u->stage != kUseWait)) {
    u->stage = kUseIdle;
    return false;
  }
  if (u->stage == kUseLetGo && hand_empty) {
    u->stage = kUseTap;
    u->frames = 0;
  } else if (u->stage == kUseTap) {
    // One frame after the Hand is empty, so the interface saw it empty.
    *tap = true;
    u->stage = kUseWait;
    u->frames = 0;
  }
  return true;
}

}  // namespace miracle_math
