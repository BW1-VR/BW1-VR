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

// The pipeline of FarSelect with each value of far_grab_choice and of
// far_grab_stay. quest is the index of a quest character: a living thing of a
// class that the bias of value 1 does not know. -1 for none.
struct ChoiceOut {
  controller_math::FarScore s[4];
  float near_deg[4];
  bool first[4];
  int why;
};

inline int PickChoice(int choice, int stay, const float origin[3],
                      const float ray[3], const Thing* things, int count,
                      int current, int quest, ChoiceOut* out) {
  using namespace far_choice_math;
  const ChoiceSizes sizes = {kLivingDeg, kBallLivingDeg, kBallOtherDeg};
  for (int i = 0; i < count; ++i) {
    const float hand[3] = {0.0f, 25.0f, 20.0f};  // the Hand's point
    const float dx = things[i].base[0] - hand[0];
    const float dy = things[i].base[1] - hand[1];
    const float dz = things[i].base[2] - hand[2];
    const float hand_m = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
    const bool living = choice == kChoicePoint  ? false
                        : choice == kChoiceBias ? things[i].living && i != quest
                                                : things[i].living;
    out->s[i] = ChoiceScore(choice, sizes, origin, ray, things[i].base,
                            things[i].height, things[i].radius, living, false,
                            hand_m, &out->near_deg[i]);
    out->first[i] = living;
    if (choice == kChoiceBias && living) {
      out->s[i].score = RankedScore(out->s[i].score, true);
    }
  }
  const StayRule rule = MakeStayRule(
      choice, kLivingDeg, stay, kStayDeg, controller_math::kFarSelectDeg,
      controller_math::kFarKeepDeg, controller_math::kFarSwitchDeg);
  return PickRanked(out->s, out->near_deg, out->first, count, current, rule,
                    &out->why);
}

// THE RANKS, THE BALLS AND THE TARGET THAT STAYS (far_grab_choice 2 to 4,
// far_grab_stay).
inline void FarRankChecks() {
  using namespace controller_math;
  using namespace far_choice_math;
  printf("\nThe ranks, the balls and the target that stays\n");
  const float origin[3] = {0.0f, 30.0f, 0.0f};
  const Thing kMan = {"man", {0.0f, 0.0f, 100.0f}, 0.4f, 1.8f, true};

  // A QUEST CHARACTER 4 UNITS FROM A TREE, and the ray through the crown of
  // the tree, 7 units over its base. The ray is 3.6 degrees from the man. The
  // bias of value 1 does not know his class.
  {
    const Thing scene[2] = {
        kMan, {"tree", {4.0f, 0.0f, 100.0f}, 3.0f, 15.0f, false}};
    const float crown[3] = {4.0f, 7.0f, 100.0f};
    float ray[3];
    Aim(origin, crown, ray);
    ChoiceOut o;
    for (int choice = 0; choice <= kChoiceMost; ++choice) {
      const int pick = PickChoice(choice, 1, origin, ray, scene, 2, -1, 0, &o);
      printf("  choice %d: man te %.2f score %.2f near %.2f, tree te %.2f "
             "score %.2f, pick %d\n", choice, o.s[0].te, o.s[0].score,
             o.near_deg[0], o.s[1].te, o.s[1].score, pick);
    }
    int pick = PickChoice(kChoiceBias, 0, origin, ray, scene, 2, -1, 0, &o);
    Check(pick == 1, "choice 1: the ray through the crown of a tree gives the "
          "tree, with a quest character 4 units from it", pick, 1);
    Near("the tree has te 0", o.s[1].te, 0.0, 1e-3);
    Check(o.s[0].te > 1.0f && o.s[0].te < 2.5f,
          "and the man is 1 to 2.5 degrees past his cover", o.s[0].te, 1.6);
    pick = PickChoice(kChoiceDefault, 1, origin, ray, scene, 2, -1, 0, &o);
    Check(pick == 0, "the default choice gives the man in the same case", pick,
          0);
    Check(o.why == kWhyRank, "and the reason is his rank", o.why, kWhyRank);
    Check(kChoiceDefault == kChoiceRanks, "the default is the body with ranks",
          kChoiceDefault, kChoiceRanks);
    // The same man with a class that the bias knows wins with value 1 too:
    // the class was the cause.
    pick = PickChoice(kChoiceBias, 0, origin, ray, scene, 2, -1, -1, &o);
    Check(pick == 0, "choice 1: a villager in his place wins by the bias",
          pick, 0);
    pick = PickChoice(kChoiceBallRanks, 1, origin, ray, scene, 2, -1, 0, &o);
    Check(pick == 0, "choice 4: the man", pick, 0);
    // The tree has the halo, and the aim comes onto the man with the ray
    // still in the crown: the man takes the halo at once.
    pick = PickChoice(kChoiceDefault, 1, origin, ray, scene, 2, 1, 0, &o);
    Check(pick == 0, "the man takes the halo from the tree that has it", pick,
          0);
    // The man has the halo: the tree does not take it back.
    pick = PickChoice(kChoiceDefault, 1, origin, ray, scene, 2, 0, 0, &o);
    Check(pick == 0, "and he keeps it", pick, 0);
  }

  // BALLS WITH NO RANKS. The aim is on the foot of a tree that is nearer to
  // the Hand, and the ball of the man holds the ray too. Both have te 0, so
  // the distance from the Hand decides. The ranks give the man.
  {
    const Thing scene[2] = {
        kMan, {"tree", {4.0f, 0.0f, 90.0f}, 3.0f, 15.0f, false}};
    float ray[3];
    Aim(origin, scene[1].base, ray);
    ChoiceOut o;
    int pick = PickChoice(kChoiceBalls, 1, origin, ray, scene, 2, -1, 0, &o);
    printf("  balls, the foot of a tree: man te %.2f score %.2f, tree te %.2f "
           "score %.2f, pick %d\n", o.s[0].te, o.s[0].score, o.s[1].te,
           o.s[1].score, pick);
    Check(pick == 1, "choice 3: the tree at its foot, with the man 3 degrees "
          "from the ray", pick, 1);
    pick = PickChoice(kChoiceBallRanks, 1, origin, ray, scene, 2, -1, 0, &o);
    Check(pick == 0, "choice 4: the man", pick, 0);
    pick = PickChoice(kChoiceRanks, 1, origin, ray, scene, 2, -1, 0, &o);
    Check(pick == 0, "choice 2: the man", pick, 0);
    // The crown of the tree, 12 units up, is 7 degrees from its origin: the
    // ball at the origin does not hold it, and the body does.
    const float crown[3] = {4.0f, 12.0f, 90.0f};
    Aim(origin, crown, ray);
    PickChoice(kChoiceBalls, 1, origin, ray, scene, 2, -1, 0, &o);
    const float ball_te = o.s[1].te;
    PickChoice(kChoiceRanks, 1, origin, ray, scene, 2, -1, 0, &o);
    printf("  the crown of a tree 12 units up: te %.2f by the ball at its "
           "origin, %.2f by the body\n", ball_te, o.s[1].te);
    Check(ball_te > 4.0f, "a ball at the origin of a tree does not hold its "
          "crown", ball_te, 5.0);
    Near("and the body holds it", o.s[1].te, 0.0, 1e-3);
  }

  // THE BALL. te is the angle to the middle less the size of the ball, and
  // the size is the same angle at each scale of the God view.
  {
    const float level[3] = {0.0f, 0.0f, 0.0f};
    const float ahead[3] = {0.0f, 0.0f, 1.0f};
    const float scales[2] = {7.5f, 4000.0f};
    for (int k = 0; k < 2; ++k) {
      // A point 5 degrees off the ray, 1 m of the room away.
      const float d = scales[k];
      const float at[3] = {d * sinf(5.0f * 0.0174532925f), 0.0f,
                           d * cosf(5.0f * 0.0174532925f)};
      float near_deg = 0.0f;
      const FarScore s =
          BallScore(level, ahead, at, kBallLivingDeg, 1.0f, &near_deg);
      Near("a ball of 3 degrees, 5 degrees off the ray: te 2", s.te, 2.0,
           1e-2);
      Near("and its middle is 5 degrees off", near_deg, 5.0, 1e-2);
      const FarScore in =
          BallScore(level, ahead, at, 6.0f, 1.0f, nullptr);
      Near("a ball of 6 degrees holds the ray: te 0", in.te, 0.0, 1e-4);
      Near("and its score is the distance term", in.score, 1.0, 1e-4);
    }
    // The middle of the ball of a living thing is the middle of its body.
    float middle[3];
    BallMiddle(kMan.base, kMan.height, true, middle);
    Near("the ball of a villager is at half his height", middle[1], 0.9, 1e-5);
    BallMiddle(kMan.base, 15.0f, false, middle);
    Near("the ball of a tree is at its origin", middle[1], 0.0, 1e-5);
  }

  // THE TARGET THAT STAYS, WITH A HAND THAT SHAKES BY 1 DEGREE. A tree stands
  // 8 units beside the man. The aim is 6.2 degrees from the man toward the
  // tree: the edge of his cone, with the ray in the body of the tree. The
  // hand shakes 1 degree to each side for 200 frames.
  {
    const Thing scene[2] = {
        kMan, {"tree", {8.0f, 0.0f, 100.0f}, 3.0f, 15.0f, false}};
    const float chest[3] = {0.0f, 0.9f, 100.0f};
    float on_man[3], ray[3];
    Aim(origin, chest, on_man);
    ChoiceOut o;
    for (int stay = 0; stay <= 1; ++stay) {
      int current = PickChoice(kChoiceDefault, stay, origin, on_man, scene, 2,
                               -1, 0, &o);
      int changes = 0, on_tree = 0;
      float least = 1e9f, most = 0.0f;
      for (int i = 0; i < 200; ++i) {
        Turn(on_man, 6.24f + sinf(0.7f * i), ray);
        const int pick = PickChoice(kChoiceDefault, stay, origin, ray, scene,
                                    2, current, 0, &o);
        if (o.s[0].te < least) least = o.s[0].te;
        if (o.s[0].te > most) most = o.s[0].te;
        if (pick != current) ++changes;
        if (pick == 1) ++on_tree;
        current = pick;
      }
      printf("  far_grab_stay=%d: the man was %.2f to %.2f degrees past his "
             "cover, the halo changed %d times and was on the tree in %d of "
             "200 frames\n", stay, least, most, changes, on_tree);
      if (stay == 0) {
        Check(changes > 10, "far_grab_stay=0: a shake of 1 degree at the edge "
              "of the cone moves the halo between the man and the tree",
              changes, 11);
      } else {
        Check(changes == 0 && on_tree == 0, "far_grab_stay=1: the halo stays "
              "on the man", changes, 0);
        Check(o.why == kWhyHeld || o.why == kWhyRank,
              "and the reason is his rank or the hold", o.why, kWhyHeld);
      }
    }
    // The aim leaves the man by more than his cone and the stay margin: the
    // tree takes the halo.
    Turn(on_man, 10.5f, ray);
    int pick = PickChoice(kChoiceDefault, 1, origin, ray, scene, 2, 0, 0, &o);
    printf("  10.5 degrees from the man: man te %.2f, tree te %.2f, pick %d\n",
           o.s[0].te, o.s[1].te, pick);
    Check(pick == 1, "the aim 10.5 degrees from the man gives the tree", pick,
          1);
    // With no ranks (value 1) the hold acts too, for a class that the bias
    // knows. A rock is on the ray, 7.5 degrees from a villager that has the
    // halo. Its score is more than 2 degrees lower, so the margin of the
    // scores gives it the halo, and the hold does not.
    const Thing rocks[2] = {
        kMan, {"rock", {13.7f, 0.0f, 100.0f}, 0.5f, 0.8f, false}};
    Aim(origin, rocks[1].base, ray);
    pick = PickChoice(kChoiceBias, 0, origin, ray, rocks, 2, 0, -1, &o);
    printf("  value 1, the aim on a rock 7.5 degrees from a villager: "
           "villager te %.2f score %.2f, rock te %.2f score %.2f\n", o.s[0].te,
           o.s[0].score, o.s[1].te, o.s[1].score);
    Check(pick == 1, "value 1 with far_grab_stay=0: a rock on the ray takes "
          "the halo from a villager 5.5 degrees past his cover", pick, 1);
    pick = PickChoice(kChoiceBias, 1, origin, ray, rocks, 2, 0, -1, &o);
    Check(pick == 0 && o.why == kWhyHeld,
          "value 1 with far_grab_stay=1: the villager keeps the halo", pick,
          0);
  }

  // TWO VILLAGERS 3.6 UNITS APART. The aim is on the chest of the far one,
  // and the body of the near one holds the ray too.
  {
    const Thing scene[2] = {
        kMan, {"villager", {3.0f, 0.0f, 98.0f}, 0.4f, 1.8f, true}};
    const float chest[3] = {0.0f, 0.9f, 100.0f};
    float ray[3], off[3];
    Aim(origin, chest, ray);
    ChoiceOut o;
    int pick = PickChoice(kChoiceBias, 0, origin, ray, scene, 2, -1, -1, &o);
    printf("  two villagers: te %.2f and %.2f, near %.2f and %.2f, value 1 "
           "picks %d\n", o.s[0].te, o.s[1].te, o.near_deg[0], o.near_deg[1],
           pick);
    Check(pick == 1, "value 1: both have te 0, so the one nearer to the Hand "
          "wins", pick, 1);
    pick = PickChoice(kChoiceDefault, 1, origin, ray, scene, 2, -1, -1, &o);
    Check(pick == 0 && o.why == kWhyNearest,
          "ranks: the villager nearest to the ray wins", pick, 0);
    // A shake of 1 degree toward the other one does not move the halo.
    Turn(ray, 1.0f, off);
    pick = PickChoice(kChoiceDefault, 1, origin, off, scene, 2, 0, -1, &o);
    Check(pick == 0, "and a shake of 1 degree toward the other one keeps the "
          "halo", pick, 0);
    // The aim on the chest of the other one, with the first as the target:
    // the other one is 1.7 degrees nearer to the ray, and it takes the halo.
    const float chest2[3] = {3.0f, 0.9f, 98.0f};
    Aim(origin, chest2, ray);
    pick = PickChoice(kChoiceDefault, 1, origin, ray, scene, 2, 0, -1, &o);
    printf("  the aim on the second villager: near %.2f and %.2f, pick %d\n",
           o.near_deg[0], o.near_deg[1], pick);
    Check(pick == 1 && o.why == kWhyNearest,
          "the aim on the chest of a villager 1.7 degrees beside the target "
          "takes the halo", pick, 1);
    // The aim between the two, 0.4 degrees nearer to the other one: the
    // target stays.
    Aim(origin, chest, ray);
    Turn(ray, 1.05f, off);
    pick = PickChoice(kChoiceDefault, 1, origin, off, scene, 2, 0, -1, &o);
    printf("  the aim between the two: near %.2f and %.2f, pick %d\n",
           o.near_deg[0], o.near_deg[1], pick);
    Check(pick == 0 && o.near_deg[1] < o.near_deg[0],
          "a villager that is less than 1 degree nearer to the ray does not "
          "take the halo", pick, 0);
  }

  // TREES AND ROCKS STAY EASY TO TAKE when no living thing is in its cone.
  {
    const Thing scene[2] = {kVillager, kTree};
    const float crown[3] = {4.0f, 12.0f, 110.0f};
    float ray[3];
    Aim(origin, crown, ray);
    ChoiceOut o;
    int pick = PickChoice(kChoiceDefault, 1, origin, ray, scene, 2, -1, -1, &o);
    Check(pick == 1 && o.why == kWhyPlain,
          "ranks: the crown of a tree with a villager 20 degrees away is the "
          "tree, by the plain rule", pick, 1);
    // A villager alone, 7.5 degrees from the ray: he is past his cone, and
    // he is in the cone of a new target.
    const Thing alone[1] = {kMan};
    const float chest[3] = {0.0f, 0.9f, 100.0f};
    float on_man[3];
    Aim(origin, chest, on_man);
    Turn(on_man, 7.8f, ray);
    pick = PickChoice(kChoiceDefault, 1, origin, ray, alone, 1, -1, -1, &o);
    printf("  a villager alone, 7.8 degrees off: te %.2f, pick %d\n",
           o.s[0].te, pick);
    Check(pick == 0 && o.s[0].te > kLivingDeg && o.why == kWhyPlain,
          "ranks: a villager alone past his cone is the target by the plain "
          "rule", pick, 0);
  }

  // THE VALUES.
  {
    Check(ClampChoice(-1) == 0 && ClampChoice(7) == kChoiceMost &&
              ClampChoice(3) == 3,
          "far_grab_choice is 0 to 4", ClampChoice(7), kChoiceMost);
    Check(!ChoiceRanks(0) && !ChoiceRanks(1) && ChoiceRanks(2) &&
              !ChoiceRanks(3) && ChoiceRanks(4),
          "2 and 4 have ranks", 1, 1);
    Check(!ChoiceBalls(0) && !ChoiceBalls(1) && !ChoiceBalls(2) &&
              ChoiceBalls(3) && ChoiceBalls(4),
          "3 and 4 have balls", 1, 1);
    const StayRule rule = MakeStayRule(kChoiceRanks, 4.0f, 1, 3.0f, 8.0f, 14.0f,
                                       2.0f);
    Near("a living target stays to its cone plus the stay margin",
         rule.hold_deg, 7.0, 1e-6);
    const StayRule tight = MakeStayRule(kChoiceRanks, 4.0f, 1, 3.0f, 3.0f, 5.0f,
                                        1.0f);
    Near("the cone of a living thing is not wider than the cone of a new "
         "target", tight.first_deg, 3.0, 1e-6);
    Near("and the hold is not wider than the cone that keeps a target",
         tight.hold_deg, 5.0, 1e-6);
  }
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
    Near("each choice with ranks or balls reaches 10 m",
         CommandRangeM(far_choice_math::kChoiceBallRanks, -1.0f), 10.0, 1e-6);
  }
  FarRankChecks();
}

}  // namespace far_choice_checks
