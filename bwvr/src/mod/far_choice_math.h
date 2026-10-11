// The choice of a far target (far_grab_choice, far_grab_stay). Pure functions
// with no game state. hand_direct.cpp and creature_command.cpp call them, and
// test/controller_math checks them (far_choice_checks.h). See docs/grab.md,
// "The cone and the choice".
#pragma once

#include <math.h>

#include "controller_math.h"
#include "creature_command_math.h"

namespace far_choice_math {

// THE VALUES OF far_grab_choice.
//
//   0  ONE POINT. The angle to the base of the object, less the angle of its
//      2D radius. No class counts.
//   1  THE BODY, WITH A BIAS. The four rules below. A living thing scores
//      kLivingBiasDeg better, so the score of a tree on the ray decides how
//      far from a villager the aim can be.
//   2  THE BODY, WITH RANKS (the default). The angle is measured as with 1. A
//      living thing in its cone comes before each thing that is not living
//      (PickRanked). No bias.
//   3  BALLS. Each candidate is a ball with a size in degrees that comes from
//      its class and not from its mesh (BallScore). No rank and no bias.
//   4  BALLS, WITH RANKS. The balls of 3 and the ranks of 2.
const int kChoicePoint = 0;
const int kChoiceBias = 1;
const int kChoiceRanks = 2;
const int kChoiceBalls = 3;
const int kChoiceBallRanks = 4;
const int kChoiceMost = 4;
const int kChoiceDefault = kChoiceRanks;

inline int ClampChoice(int choice) {
  return choice < 0 ? 0 : choice > kChoiceMost ? kChoiceMost : choice;
}
// The choice has ranks.
inline bool ChoiceRanks(int choice) {
  return choice == kChoiceRanks || choice == kChoiceBallRanks;
}
// The choice measures to a ball.
inline bool ChoiceBalls(int choice) {
  return choice == kChoiceBalls || choice == kChoiceBallRanks;
}

// THE NUMBERS OF THE RANKS, THE BALLS AND THE STAY RULE, each with an ini key.
//
// kLivingDeg (far_grab_living_deg): the cone of a living thing. A living thing
// is in the first rank while its te is at or below this. With the bias of
// value 1, a living thing won over a tree on the ray to a te of about 4
// degrees, and it lost from about 5 degrees.
//
// kStayDeg (far_grab_stay_deg): a living thing that has the halo keeps its
// rank to this many degrees past its cone (far_grab_stay=1). It is more than
// kFarSwitchDeg, the margin between two scores.
//
// kBallLivingDeg and kBallOtherDeg (far_grab_ball_living_deg,
// far_grab_ball_other_deg): the radius of the ball of a living thing, and of
// each other thing. The size is an angle from the aim ray, and not a length in
// the world or in the room. A length in the world is 4 cm of the room at 7.5
// units a meter and 0.01 cm at 4000. A length in the room covers half the
// angle at two times the distance from the hand, as the picture of the object
// does. The shake of a hand is an angle, so an angle is as easy to hit at each
// scale and at each distance.
const float kLivingDeg = 4.0f;
const float kStayDeg = 3.0f;
const float kBallLivingDeg = 3.0f;
const float kBallOtherDeg = 1.5f;

struct ChoiceSizes {
  float living_deg;       // far_grab_living_deg
  float ball_living_deg;  // far_grab_ball_living_deg
  float ball_other_deg;   // far_grab_ball_other_deg
};

// far_grab_choice=0 measures the angle to one point of the object, its base,
// less the angle of its 2D radius. far_grab_choice=1 has four rules, and 2 has
// the first three of them:
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
// the Hand's point. near_deg gets the angle from the ray to the nearest point
// of the line of the body, with no cover: the order of the first rank.
inline controller_math::FarScore BodyScore(const float origin[3],
                                           const float direction[3],
                                           const float base[3], float height,
                                           float radius,
                                           float hand_distance_m,
                                           float* near_deg = nullptr) {
  controller_math::FarScore out = {false, 0.0f, 0.0f};
  if (near_deg != nullptr) *near_deg = 0.0f;
  if (!(hand_distance_m >= controller_math::kFarMinHandM)) return out;
  // With radius 0, te is the angle to the nearest point of the line itself.
  const command_math::BodyAim line =
      command_math::AimAtBody(origin, direction, base, height, 0.0f);
  if (!line.valid) return out;
  if (near_deg != nullptr) *near_deg = line.te;
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

// THE BALL (far_grab_choice=3 and 4). The middle of the ball of a living thing
// is the middle of its body, so a ray at the chest of a villager is at the
// middle of his ball. The middle of the ball of each other thing is its
// origin: the base of a tree, of a rock and of a building.
inline void BallMiddle(const float base[3], float height, bool living,
                       float out[3]) {
  out[0] = base[0];
  out[1] = base[1] + (living && height > 0.0f ? 0.5f * height : 0.0f);
  out[2] = base[2];
}

// The score of a ball: te is the angle from the ray to the middle, less
// ball_deg, and the score adds the distance term of FarConeScore. No size of
// the mesh counts. near_deg gets the angle to the middle.
inline controller_math::FarScore BallScore(const float origin[3],
                                           const float direction[3],
                                           const float middle[3],
                                           float ball_deg,
                                           float hand_distance_m,
                                           float* near_deg = nullptr) {
  controller_math::FarScore out = controller_math::FarConeScore(
      origin, direction, middle, 0.0f, hand_distance_m);
  if (near_deg != nullptr) *near_deg = out.valid ? out.te : 0.0f;
  if (!out.valid) return out;
  const float ball = ball_deg > 0.0f ? ball_deg : 0.0f;
  const float t = out.te > ball ? out.te - ball : 0.0f;
  out.score += t - out.te;
  out.te = t;
  return out;
}

// THE SCORE OF ONE CANDIDATE WITH EACH VALUE OF far_grab_choice, before the
// bias of value 1. point is true for a candidate that keeps the point with
// each value: a forest, a scroll and the Creature. near_deg gets the angle
// that gives the order in the first rank.
inline controller_math::FarScore ChoiceScore(
    int choice, const ChoiceSizes& sizes, const float origin[3],
    const float direction[3], const float base[3], float height, float radius,
    bool living, bool point, float hand_distance_m, float* near_deg) {
  controller_math::FarScore s;
  float near_angle = 0.0f;
  if (choice == kChoicePoint || point) {
    s = controller_math::FarConeScore(origin, direction, base, radius,
                                      hand_distance_m);
    near_angle = s.te;
  } else if (ChoiceBalls(choice)) {
    float middle[3];
    BallMiddle(base, height, living, middle);
    s = BallScore(origin, direction, middle,
                  living ? sizes.ball_living_deg : sizes.ball_other_deg,
                  hand_distance_m, &near_angle);
  } else {
    s = BodyScore(origin, direction, base, height, radius, hand_distance_m,
                  &near_angle);
  }
  if (near_deg != nullptr) *near_deg = near_angle;
  return s;
}

// THE RANKS AND THE TARGET THAT STAYS (PickRanked).
//
// THE FIRST RANK (far_grab_choice=2 and 4). A candidate of the first class (a
// living thing, and a scroll) is in the first rank while its te is at or
// below first_deg. A candidate of the first rank wins over each candidate
// that is not in it, also over a tree whose body the ray crosses. In the
// first rank the candidate nearest to the ray wins (near_deg). When the first
// rank is empty, the plain rule of FarPickTarget decides among all candidates
// with their scores.
//
// THE STAY RULE (far_grab_stay).
//   0  The margins of FarPickTarget only: the target stays while its te is at
//      or below keep_deg, unless a different one has a score switch_deg lower.
//      With ranks, a living target has its rank only while it is in its cone.
//   1  A target of the first class keeps the halo while its te is at or below
//      hold_deg (the cone of a living thing plus far_grab_stay_deg). In that
//      time no thing that is not living takes the halo, whatever its score. A
//      different thing of the first class takes it only when it has a score
//      switch_deg lower (with no ranks). So a new target needs the aim in the
//      tight cone, and the target of now stays in the wide cone.
//
// TWO IN THE FIRST RANK. A different one takes the halo when it is
// first_switch_deg nearer to the ray (kFirstSwitchDeg, 1 degree). The two
// angles have no cover, so the margin is the real angle between the ray and
// each body. A hand that shakes 1 degree about a villager moves the two
// angles by 1 degree, and the aim on the chest of a villager 1.7 degrees
// beside the target takes the halo.
const float kFirstSwitchDeg = 1.0f;

struct StayRule {
  bool ranks;        // far_grab_choice has ranks
  float first_deg;   // the cone of the first rank
  int stay;          // far_grab_stay
  float hold_deg;    // a first class target that has the halo stays to this te
  float select_deg;  // a new target needs this te
  float keep_deg;    // the target stays to this te
  float switch_deg;  // the margin between two scores
  float first_switch_deg;  // the margin between two in the first rank
};

inline StayRule MakeStayRule(int choice, float living_deg, int stay,
                             float stay_deg, float select_deg, float keep_deg,
                             float switch_deg) {
  StayRule rule;
  rule.ranks = ChoiceRanks(choice);
  rule.first_deg = living_deg < select_deg ? living_deg : select_deg;
  if (!(rule.first_deg >= 0.0f)) rule.first_deg = 0.0f;
  rule.stay = stay != 0 ? 1 : 0;
  rule.hold_deg = rule.first_deg + (stay_deg > 0.0f ? stay_deg : 0.0f);
  if (rule.hold_deg > keep_deg) rule.hold_deg = keep_deg;
  rule.select_deg = select_deg;
  rule.keep_deg = keep_deg;
  rule.switch_deg = switch_deg;
  rule.first_switch_deg =
      switch_deg < kFirstSwitchDeg ? switch_deg : kFirstSwitchDeg;
  return rule;
}

// WHY THE TARGET IS THE TARGET, for the log. raw is the score with no bias,
// ranked the score with the bias, living the class of each candidate.
enum Why {
  kWhyOnly = 0,   // no other candidate is in the cone
  kWhyScore = 1,  // the lowest score
  kWhyLiving = 2, // a living thing, and without the bias another wins
  kWhyKept = 3,   // the target of the last frame, and another has a lower score
                  // by less than the switch
  kWhyRank = 4,   // the first rank: a living thing in its cone
  kWhyNearest = 5,  // the nearest to the ray of two or more in the first rank
  kWhyHeld = 6,   // a living target that keeps the halo past its cone
  kWhyPlain = -1  // PickRanked: the plain rule decided (see WhyWinner)
};

// The target among the candidates. s has te and the plain score of each one,
// near_deg its angle for the order in the first rank, and first its class.
// current is the index of the target of the last frame, or -1. why gets the
// reason, or kWhyPlain when FarPickTarget decided. Returns the index, or -1.
inline int PickRanked(const controller_math::FarScore* s,
                      const float* near_deg, const bool* first, int count,
                      int current, const StayRule& rule, int* why = nullptr) {
  if (why != nullptr) *why = kWhyPlain;
  const bool have = current >= 0 && current < count && s[current].valid &&
                    s[current].te <= rule.keep_deg;
  const bool held = rule.stay != 0 && have && first[current] &&
                    s[current].te <= rule.hold_deg;
  if (rule.ranks) {
    // The best one in the first rank, and how many are in it.
    int best = -1, in_rank = 0;
    for (int i = 0; i < count; ++i) {
      if (!s[i].valid || !first[i] || !(s[i].te <= rule.first_deg) ||
          !(s[i].te <= rule.select_deg)) {
        continue;
      }
      ++in_rank;
      if (best < 0 || near_deg[i] < near_deg[best]) best = i;
    }
    const bool current_first =
        have && first[current] &&
        (s[current].te <= rule.first_deg || held);
    if (current_first) {
      if (best >= 0 && best != current &&
          near_deg[best] + rule.first_switch_deg <= near_deg[current]) {
        if (why != nullptr) *why = kWhyNearest;
        return best;
      }
      if (why != nullptr) {
        *why = !(s[current].te <= rule.first_deg) ? kWhyHeld
               : best >= 0 && best != current     ? kWhyKept
                                                  : kWhyRank;
      }
      return current;
    }
    if (best >= 0) {
      if (why != nullptr) *why = in_rank > 1 ? kWhyNearest : kWhyRank;
      return best;
    }
  } else if (held) {
    // No ranks: only a different thing of the first class with a score
    // switch_deg lower takes the halo from a first class target.
    int best = -1;
    for (int i = 0; i < count; ++i) {
      if (!s[i].valid || !first[i] || !(s[i].te <= rule.select_deg)) continue;
      if (best < 0 || s[i].score < s[best].score) best = i;
    }
    if (best >= 0 && best != current &&
        s[best].score + rule.switch_deg <= s[current].score) {
      return best;
    }
    // The plain rule gives the same answer when no other candidate has a
    // score switch_deg lower. The reason is the hold only when one has.
    const int plain = controller_math::FarPickTarget(
        s, count, current, rule.select_deg, rule.keep_deg, rule.switch_deg);
    if (plain != current && why != nullptr) *why = kWhyHeld;
    return current;
  }
  return controller_math::FarPickTarget(s, count, current, rule.select_deg,
                                        rule.keep_deg, rule.switch_deg);
}

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
    case kWhyRank:
      return "a living thing in its cone comes before each thing that is not "
             "living";
    case kWhyNearest: return "the nearest to the ray of the living things";
    case kWhyHeld:
      return "a living thing keeps the halo while the aim is near it";
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
