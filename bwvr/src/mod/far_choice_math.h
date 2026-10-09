// The choice of a far target (far_grab_choice=1). Pure functions with no game
// state. hand_direct.cpp calls them, and test/controller_math checks them
// (far_choice_checks.h). See docs/grab.md, "The cone and the choice".
#pragma once

#include <math.h>

#include "controller_math.h"
#include "creature_command_math.h"

namespace far_choice_math {

// far_grab_choice=0 measures the angle to one point of the object, its base,
// less the angle of its 2D radius. far_grab_choice=1 has four rules:
//
//   1. THE BODY. The angle is measured to the nearest point of the upright
//      body of the object: the line from its base up to its height, with its
//      2D radius around it (command_math::AimAtBody). A tall tree is hit by a
//      ray through its crown, and a villager by a ray through his chest.
//   2. THE LEAST SIZE. A body covers at least kMinCoverDeg of the view. A
//      villager is 1.8 units tall and has a radius of under 1 unit, so at 1 m
//      of the room and 100 units a meter he covers 1 degree, and a hand
//      shakes more than that.
//   3. SMALL BEFORE LARGE. When the ray goes through two bodies, the score
//      adds kSizeDeg for each log2(1 + size), with size the larger of the
//      radius and half the height, in world units. A rock (0.5) before a tree
//      (7.5) that stands behind it.
//   4. LIVING THINGS FIRST. A living thing (a villager, an animal, the
//      player's own Creature) scores kLivingBiasDeg better than its angle. It
//      wins over a tree or a rock when both are in the cone and the living
//      thing is not more than about kLivingBiasDeg further from the ray (a
//      little more against a tree, because of the size term). With no living
//      thing near the ray, the plain order stays. The bias is in the score, so
//      the hysteresis of FarPickTarget keeps a living target against a tree.
const float kMinCoverDeg = 2.0f;
const float kLivingBiasDeg = 3.0f;
const float kSizeDeg = 0.5f;

// The height of the body: the game's height, or two radii when the game gives
// no sane height.
inline float BodyHeight(float height, float radius) {
  if (height > 0.0f && height < 2000.0f) return height;
  return 2.0f * (radius > 0.0f ? radius : 0.0f);
}

// A radius that holds the whole body, for the first cut of the candidates: the
// angle from the ray to the base, less the angle of this radius, is never more
// than the angle to the body.
inline float CutRadius(float height, float radius) {
  return (radius > 0.0f ? radius : 0.0f) + (height > 0.0f ? height : 0.0f);
}

// The size term of the score, in degrees (see rule 3 above). A forest takes
// it too, with the radius of its field, so a tree in the ray comes before the
// forest around it.
inline float SizePenalty(float radius, float height) {
  const float r = radius > 0.0f ? radius : 0.0f;
  const float h = 0.5f * (height > 0.0f ? height : 0.0f);
  return kSizeDeg * log2f(1.0f + (r > h ? r : h));
}

// The score of a body, as FarConeScore has it for a point: te in degrees, and
// the score adds 1 degree for each log2(1 + meters from the Hand's point).
// valid is false behind the ray and for an object nearer than kFarMinHandM to
// the Hand's point.
inline controller_math::FarScore BodyScore(const float origin[3],
                                           const float direction[3],
                                           const float base[3], float height,
                                           float radius,
                                           float hand_distance_m) {
  controller_math::FarScore out = {false, 0.0f, 0.0f};
  if (!(hand_distance_m >= controller_math::kFarMinHandM)) return out;
  // With radius 0, te is the angle to the nearest point of the line itself.
  const command_math::BodyAim line =
      command_math::AimAtBody(origin, direction, base, height, 0.0f);
  if (!line.valid) return out;
  const float radius_deg =
      atanf((radius > 0.0f ? radius : 0.0f) / line.distance) * 57.29578f;
  const float covered = radius_deg > kMinCoverDeg ? radius_deg : kMinCoverDeg;
  const float t = line.te > covered ? line.te - covered : 0.0f;
  out.valid = true;
  out.te = t;
  out.score = t + controller_math::kFarDistanceDeg *
                      log2f(1.0f + hand_distance_m) +
              SizePenalty(radius, height);
  return out;
}

// The score of one candidate with the choice in use: by the body (choice 1),
// or by one point of the object (choice 0). The point is the base.
inline controller_math::FarScore CandidateScore(
    int choice, const float origin[3], const float direction[3],
    const float base[3], float height, float radius, float hand_distance_m) {
  return choice != 0
             ? BodyScore(origin, direction, base, height, radius,
                         hand_distance_m)
             : controller_math::FarConeScore(origin, direction, base, radius,
                                             hand_distance_m);
}

// The score with the order of the classes. A living thing scores
// kLivingBiasDeg lower. The te does not change, so the cone is the same.
inline float RankedScore(float score, bool living) {
  return living ? score - kLivingBiasDeg : score;
}

// WHY THE TARGET IS THE TARGET, for the log. raw is the score with no bias,
// ranked the score with the bias, living the class of each candidate.
enum Why {
  kWhyOnly = 0,   // no other candidate is in the cone
  kWhyScore = 1,  // the lowest score
  kWhyLiving = 2, // a living thing, and without the bias another wins
  kWhyKept = 3    // the target of the last frame, and another has a lower score
                  // by less than the switch
};

// The index of the candidate that comes second: the lowest ranked score among
// the valid candidates in the cone, less the winner. -1 for none.
inline int RunnerUp(const controller_math::FarScore* ranked, int count,
                    int winner, float select_deg) {
  int best = -1;
  for (int i = 0; i < count; ++i) {
    if (i == winner || !ranked[i].valid || !(ranked[i].te <= select_deg)) {
      continue;
    }
    if (best < 0 || ranked[i].score < ranked[best].score) best = i;
  }
  return best;
}

inline int WhyWinner(const controller_math::FarScore* raw,
                     const controller_math::FarScore* ranked,
                     const bool* living, int count, int winner, int current,
                     float select_deg) {
  const int runner = RunnerUp(ranked, count, winner, select_deg);
  if (runner < 0) return kWhyOnly;
  if (winner == current && ranked[runner].score < ranked[winner].score) {
    return kWhyKept;
  }
  // The best raw score in the cone.
  int low = -1;
  for (int i = 0; i < count; ++i) {
    if (!raw[i].valid || !(raw[i].te <= select_deg)) continue;
    if (low < 0 || raw[i].score < raw[low].score) low = i;
  }
  if (low >= 0 && low != winner && living[winner]) return kWhyLiving;
  return kWhyScore;
}

inline const char* WhyWords(int why) {
  switch (why) {
    case kWhyOnly: return "the only one in the cone";
    case kWhyScore: return "the lowest score";
    case kWhyLiving: return "a living thing comes first";
    case kWhyKept: return "it kept the target of the last frame";
  }
  return "";
}

// THE RANGE OF AN OBJECT TARGET OF A CREATURE COMMAND, in meters of the room
// from the head. The land target reaches the teleport arc (10 m). The object
// target reaches the same with the new choice, so a place that the land
// command reaches has its objects too. The choice of now has 4 m. An ini
// value that is 0 or more is used as it is.
const float kCommandRangeOldM = 4.0f;
const float kCommandRangeNewM = 10.0f;
inline float CommandRangeM(int choice, float ini_m) {
  if (ini_m >= 0.0f) return ini_m;
  return choice != 0 ? kCommandRangeNewM : kCommandRangeOldM;
}

}  // namespace far_choice_math
