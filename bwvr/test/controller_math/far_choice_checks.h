// THE CHOICE OF A FAR TARGET (docs/grab.md, "The cone and the choice").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/far_choice_math.h"

namespace far_choice_checks {

// One candidate of a scene. The world units are 1 cm of the room: 100 units
// a meter, the start scale of the God view. A villager is 1.8 units tall with
// a radius of 0.4, a tree 15 tall with a radius of 3, a rock 0.8 and 0.5.
struct Thing {
  const char* name;
  float base[3];
  float radius;
  float height;
  bool living;
};

const Thing kVillager = {"villager", {0.0f, 0.0f, 100.0f}, 0.4f, 1.8f, true};
const Thing kTree = {"tree", {4.0f, 0.0f, 110.0f}, 3.0f, 15.0f, false};

inline void Aim(const float origin[3], const float at[3], float out[3]) {
  float d[3] = {at[0] - origin[0], at[1] - origin[1], at[2] - origin[2]};
  const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  for (int i = 0; i < 3; ++i) out[i] = d[i] / l;
}

// The ray, turned by deg degrees to the right (+x) about the up axis.
inline void Turn(const float ray[3], float deg, float out[3]) {
  const float a = deg * 0.0174532925f;
  out[0] = ray[0] * cosf(a) + ray[2] * sinf(a);
  out[1] = ray[1];
  out[2] = -ray[0] * sinf(a) + ray[2] * cosf(a);
}

// The pipeline of FarSelect for a list of things: the score of each with the
// choice, the bias for a living thing, and the pick. Returns the index, or -1.
// scores gets the ranked scores.
inline int Pick(int choice, const float origin[3], const float ray[3],
                const Thing* things, int count, int current,
                controller_math::FarScore* ranked,
                controller_math::FarScore* raw = nullptr,
                bool* living = nullptr) {
  for (int i = 0; i < count; ++i) {
    const float hand[3] = {0.0f, 25.0f, 20.0f};  // the Hand's point
    const float dx = things[i].base[0] - hand[0];
    const float dy = things[i].base[1] - hand[1];
    const float dz = things[i].base[2] - hand[2];
    const float hand_m = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
    controller_math::FarScore s = far_choice_math::CandidateScore(
        choice, origin, ray, things[i].base, things[i].height,
        things[i].radius, hand_m);
    if (raw != nullptr) raw[i] = s;
    const bool live = choice != 0 && things[i].living;
    if (living != nullptr) living[i] = live;
    if (live) s.score = far_choice_math::RankedScore(s.score, true);
    ranked[i] = s;
  }
  return controller_math::FarPickTarget(
      ranked, count, current, controller_math::kFarSelectDeg,
      controller_math::kFarKeepDeg, controller_math::kFarSwitchDeg);
}

inline void FarChoiceChecks() {
  using namespace controller_math;
  printf("\nThe choice of a far target\n");
  // The head is 60 cm over the land, the hand 30 cm over it, and the villager
  // is 1 m of the room away.
  const float origin[3] = {0.0f, 30.0f, 0.0f};

  // A VILLAGER BESIDE A TREE. The player aims at the chest of the villager,
  // and the hand is 1.2 degrees off toward the tree.
  {
    const Thing scene[2] = {kVillager, kTree};
    float chest[3] = {0.0f, 0.9f, 100.0f}, ray[3], off[3];
    Aim(origin, chest, ray);
    Turn(ray, 1.2f, off);
    FarScore ranked[2], raw[2];
    bool living[2];
    // The numbers of the case, for the doc.
    for (int choice = 0; choice <= 1; ++choice) {
      const int pick = Pick(choice, origin, off, scene, 2, -1, ranked, raw,
                            living);
      printf("  choice %d: villager te %.2f score %.2f, tree te %.2f score "
             "%.2f, pick %d\n", choice, ranked[0].te, ranked[0].score,
             ranked[1].te, ranked[1].score, pick);
    }
    int pick = Pick(0, origin, off, scene, 2, -1, ranked);
    Check(pick == 1, "choice 0: a tree 4 units from a villager wins when the "
          "hand is 1.2 degrees off", pick, 1);
    pick = Pick(1, origin, off, scene, 2, -1, ranked);
    Check(pick == 0, "choice 1: the villager wins in the same case", pick, 0);
    // On the villager the aim is good in both choices.
    pick = Pick(0, origin, ray, scene, 2, -1, ranked);
    Check(pick == 0, "choice 0: the aim exactly on the villager is the "
          "villager", pick, 0);
    pick = Pick(1, origin, ray, scene, 2, -1, ranked);
    Check(pick == 0, "choice 1: the aim exactly on the villager is the "
          "villager", pick, 0);
  }

  // THE AIM ON THE CROWN OF A TREE has a villager 4 units beside the tree, 20
  // degrees away from the ray. Trees stay easy to take.
  {
    const Thing scene[2] = {kVillager, kTree};
    const float crown[3] = {4.0f, 12.0f, 110.0f};
    float ray[3];
    Aim(origin, crown, ray);
    FarScore ranked[2];
    int pick = Pick(0, origin, ray, scene, 2, -1, ranked);
    printf("  crown, choice 0: tree te %.2f, villager te %.2f, pick %d\n",
           ranked[1].te, ranked[0].te, pick);
    Check(pick == -1 || pick == 1, "choice 0: the crown is a tree or nothing",
          pick, 1);
    pick = Pick(1, origin, ray, scene, 2, -1, ranked);
    Check(pick == 1, "choice 1: the aim on the crown takes the tree", pick, 1);
    Near("and its te is 0", ranked[1].te, 0.0, 1e-3);
  }

  // A LIVING THING FAR FROM THE RAY does not take a tree that is on the ray.
  {
    const Thing scene[2] = {kVillager, kTree};
    // The aim at the middle of the tree. The villager is 2.3 degrees away at
    // 1 m, which is inside the bias, so he wins. Move the villager 9 units
    // further from the tree (about 5 degrees more), and the tree stays.
    Thing far_villager = kVillager;
    far_villager.base[0] = -14.0f;
    const Thing scene2[2] = {far_villager, kTree};
    const float middle[3] = {4.0f, 7.0f, 110.0f};
    float ray[3];
    Aim(origin, middle, ray);
    FarScore ranked[2], raw[2];
    bool living[2];
    int pick = Pick(1, origin, ray, scene, 2, -1, ranked, raw, living);
    printf("  tree on the ray: villager te %.2f score %.2f, tree te %.2f "
           "score %.2f, pick %d\n", ranked[0].te, ranked[0].score,
           ranked[1].te, ranked[1].score, pick);
    Check(pick == 0, "choice 1: a villager 2.3 degrees from a tree on the ray "
          "comes first", pick, 0);
    pick = Pick(1, origin, ray, scene2, 2, -1, ranked, raw, living);
    printf("  tree on the ray, villager far: villager te %.2f score %.2f, "
           "tree te %.2f score %.2f, pick %d\n", ranked[0].te, ranked[0].score,
           ranked[1].te, ranked[1].score, pick);
    Check(pick == 1, "choice 1: a villager 8 degrees from the ray does not "
          "take a tree that is on it", pick, 1);
  }

  // TWO VILLAGERS: the one that the ray is on, then the nearer one.
  {
    const Thing scene[2] = {
        {"villager", {0.0f, 0.0f, 100.0f}, 0.4f, 1.8f, true},
        {"villager", {6.0f, 0.0f, 100.0f}, 0.4f, 1.8f, true}};
    float chest[3] = {6.0f, 0.9f, 100.0f}, ray[3];
    Aim(origin, chest, ray);
    FarScore ranked[2];
    int pick = Pick(1, origin, ray, scene, 2, -1, ranked);
    Check(pick == 1, "two villagers: the one under the ray wins", pick, 1);
    // The hand shakes 1 degree toward the other one: the target stays.
    float off[3];
    Turn(ray, -2.0f, off);
    pick = Pick(1, origin, off, scene, 2, 1, ranked);
    Check(pick == 1, "and a shake of 2 degrees does not move the halo", pick,
          1);
  }

  // A ROCK IN FRONT OF A TREE: the ray goes through both. The nearer small one
  // wins in both choices, so rocks stay easy to take.
  {
    const Thing scene[2] = {
        {"rock", {1.0f, 0.0f, 60.0f}, 0.5f, 0.8f, false},
        {"tree", {2.0f, 0.0f, 110.0f}, 3.0f, 15.0f, false}};
    const float at[3] = {1.0f, 0.4f, 60.0f};
    float ray[3];
    Aim(origin, at, ray);
    FarScore ranked[2];
    int pick = Pick(0, origin, ray, scene, 2, -1, ranked);
    Check(pick == 0, "choice 0: the rock in front of the tree", pick, 0);
    pick = Pick(1, origin, ray, scene, 2, -1, ranked);
    Check(pick == 0, "choice 1: the rock in front of the tree", pick, 0);
  }

  // A ROCK BESIDE A TREE THAT IS A LITTLE NEARER TO THE HAND: the ray goes
  // through both bodies, te is 0 for both, and the distance term alone would
  // take the tree. The size term gives the rock.
  {
    const Thing scene[2] = {
        {"rock", {1.0f, 0.0f, 100.0f}, 0.5f, 0.8f, false},
        {"tree", {2.0f, 0.0f, 96.0f}, 3.0f, 15.0f, false}};
    const float at[3] = {1.0f, 0.4f, 100.0f};
    float ray[3];
    Aim(origin, at, ray);
    FarScore ranked[2];
    int pick = Pick(1, origin, ray, scene, 2, -1, ranked);
    printf("  rock te %.2f score %.2f, tree te %.2f score %.2f\n", ranked[0].te,
           ranked[0].score, ranked[1].te, ranked[1].score);
    Check(pick == 0, "choice 1: the rock wins over a tree that is 4 units "
          "nearer to the Hand", pick, 0);
    pick = Pick(0, origin, ray, scene, 2, -1, ranked);
    Check(pick == 1, "choice 0: the tree wins, as the game showed", pick, 1);
  }

  // THE HYSTERESIS KEEPS A LIVING TARGET against a tree on the ray, and a
  // tree is kept against a villager that is only a little better.
  {
    const Thing scene[2] = {kVillager, kTree};
    const float middle[3] = {4.0f, 7.0f, 110.0f};
    float ray[3];
    Aim(origin, middle, ray);
    FarScore ranked[2];
    int pick = Pick(1, origin, ray, scene, 2, 0, ranked);
    Check(pick == 0, "the villager that has the halo keeps it against a "
          "tree on the ray", pick, 0);
    pick = Pick(1, origin, ray, scene, 2, 1, ranked);
    printf("  tree has the halo: villager score %.2f, tree score %.2f, pick "
           "%d", ranked[0].score, ranked[1].score, pick);
    puts("");
    Check(pick == 1, "a tree that has the halo keeps it against a villager "
          "that scores 1.7 degrees better", pick, 1);
    // The aim on the villager takes the halo from the tree.
    const float chest[3] = {0.0f, 0.9f, 100.0f};
    float on_villager[3];
    Aim(origin, chest, on_villager);
    pick = Pick(1, origin, on_villager, scene, 2, 1, ranked);
    Check(pick == 0, "and the aim on the villager takes the halo from it",
          pick, 0);
  }

  // THE LEAST SIZE. A villager 5 m of the room away, at 100 units a meter,
  // covers 0.1 degrees. With the least size of 2 degrees a ray 1.5 degrees
  // off still has te 0, and 4 degrees off has te 2.
  {
    FarScore s;
    const float base[3] = {0.0f, 0.0f, 500.0f};
    const float level[3] = {0.0f, 30.0f, 0.0f};
    float chest[3] = {0.0f, 0.9f, 500.0f}, ray[3], off[3];
    Aim(level, chest, ray);
    Turn(ray, 1.5f, off);
    s = far_choice_math::BodyScore(level, off, base, 1.8f, 0.4f, 4.9f);
    Near("a villager at 5 m, the ray 1.5 degrees off: te 0", s.te, 0.0, 0.2);
    Turn(ray, 4.0f, off);
    s = far_choice_math::BodyScore(level, off, base, 1.8f, 0.4f, 4.9f);
    Near("and 4 degrees off: te about 2", s.te, 2.0, 0.3);
    s = FarConeScore(level, off, base, 0.4f, 4.9f);
    Check(s.te > 3.5f, "against one point the same ray has te 4 or more", s.te,
          4.0);
  }

  // THE BODY. A ray through the middle of a tree has te 0 by the body and
  // more than 8 degrees by the base.
  {
    const float level[3] = {0.0f, 30.0f, 0.0f};
    const float base[3] = {0.0f, 0.0f, 60.0f};
    const float at[3] = {0.0f, 8.0f, 60.0f};
    float ray[3];
    Aim(level, at, ray);
    FarScore body = far_choice_math::BodyScore(level, ray, base, 15.0f, 2.0f,
                                               0.6f);
    FarScore point = FarConeScore(level, ray, base, 2.0f, 0.6f);
    Near("a ray through the middle of a tree: te 0 by the body", body.te, 0.0,
         1e-3);
    Check(point.te > 4.0f, "and more than 4 degrees by the base", point.te,
          4.0);
  }

  // THE ORDER IN THE LOG.
  {
    const Thing scene[2] = {kVillager, kTree};
    const float middle[3] = {4.0f, 7.0f, 110.0f};
    float ray[3];
    Aim(origin, middle, ray);
    FarScore ranked[2], raw[2];
    bool living[2];
    int pick = Pick(1, origin, ray, scene, 2, -1, ranked, raw, living);
    int why = far_choice_math::WhyWinner(raw, ranked, living, 2, pick, -1,
                                         kFarSelectDeg);
    Check(pick == 0 && why == far_choice_math::kWhyLiving,
          "the reason for a villager that beats a tree on the ray is the "
          "class", why, far_choice_math::kWhyLiving);
    Check(far_choice_math::RunnerUp(ranked, 2, pick, kFarSelectDeg) == 1,
          "the second one is the tree", 1, 1);
    FarScore one[1] = {ranked[0]};
    bool one_living[1] = {true};
    why = far_choice_math::WhyWinner(one, one, one_living, 1, 0, -1,
                                     kFarSelectDeg);
    Check(why == far_choice_math::kWhyOnly, "one candidate: the only one", why,
          far_choice_math::kWhyOnly);
  }

  // THE RANGE OF AN OBJECT TARGET OF A COMMAND, at the scales of the God view.
  {
    using far_choice_math::CommandRangeM;
    Near("the choice of now reaches 4 m", CommandRangeM(0, -1.0f), 4.0, 1e-6);
    Near("the new choice reaches 10 m, as the land arc", CommandRangeM(1, -1.0f),
         10.0, 1e-6);
    Near("an ini value is used as it is", CommandRangeM(1, 6.0f), 6.0, 1e-6);
    Near("0 is no limit in either choice", CommandRangeM(0, 0.0f), 0.0, 1e-6);
    const float scales[5] = {7.5f, 27.5f, 100.0f, 275.0f, 4000.0f};
    for (int i = 0; i < 5; ++i) {
      printf("  at %.1f units a meter: 4 m are %.0f units, 10 m are %.0f "
             "units\n", scales[i], 4.0f * scales[i], 10.0f * scales[i]);
    }
    Near("at 27.5 units a meter, 4 m are 110 units and 10 m are 275",
         10.0f * 27.5f - 4.0f * 27.5f, 165.0, 1e-3);
  }
}

}  // namespace far_choice_checks
