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

// THE CLASSES OF THE RANKS (ClassOf, PickClassed) AND THE BODY OF THE CREATURE.
// One candidate of a scene, with what FarSelect knows of it.
struct Item {
  const char* name;
  float base[3];
  float radius;
  float height;
  bool living;
  bool scroll;   // a scroll: it keeps the point
  bool sign;     // and the scroll is a signpost
  bool miracle;  // a miracle that the Hand can take
  bool point;    // the Creature with far_grab_creature=0: the point of its mesh
};

struct SceneOut {
  controller_math::FarScore s[6];
  float near_deg[6];
  int cls[6];
  int why;
};

// The pipeline of FarSelect for a list of items, with the keys far_grab_sign
// and far_grab_miracle. units is world units in a meter of the room, and the
// Hand's point is the origin of the ray.
inline int PickScene(int choice, int stay, bool sign_last, bool miracle_first,
                     float units, const float origin[3], const float ray[3],
                     const Item* items, int count, int current,
                     SceneOut* out) {
  using namespace far_choice_math;
  const ChoiceSizes sizes = {kLivingDeg, kBallLivingDeg, kBallOtherDeg};
  for (int i = 0; i < count; ++i) {
    const float dx = items[i].base[0] - origin[0];
    const float dy = items[i].base[1] - origin[1];
    const float dz = items[i].base[2] - origin[2];
    const float hand_m = controller_math::FarScoreMeters(
        sqrtf(dx * dx + dy * dy + dz * dz) / units);
    const bool living = choice != kChoicePoint && items[i].living;
    out->s[i] = ChoiceScore(choice, sizes, origin, ray, items[i].base,
                            items[i].height, items[i].radius, living,
                            items[i].scroll || items[i].point, hand_m,
                            &out->near_deg[i]);
    if (choice == kChoiceBias && living) {
      out->s[i].score = RankedScore(out->s[i].score, true);
    }
    out->cls[i] = ClassOf(ChoiceRanks(choice), living,
                          choice != kChoicePoint && items[i].scroll,
                          items[i].sign, items[i].miracle, sign_last,
                          miracle_first);
    out->s[i].score -= TouchBias(items[i].scroll, out->cls[i]);
  }
  const StayRule rule = MakeStayRule(
      choice, kLivingDeg, stay, kStayDeg, controller_math::kFarSelectDeg,
      controller_math::kFarKeepDeg, controller_math::kFarSwitchDeg);
  controller_math::FarScore work[6];
  bool first[6];
  return PickClassed(out->s, out->near_deg, out->cls, count, current, rule,
                     work, first, &out->why);
}

inline void FarClassChecks() {
  using namespace controller_math;
  using namespace far_choice_math;
  printf("\nThe classes of the ranks: a miracle, a signpost, the Creature\n");

  // THE CLASS OF A CANDIDATE, with each value of the two keys.
  Check(ClassOf(true, false, true, true, false, true, true) == kClassLast,
        "a signpost is in the last class (far_grab_sign=1)",
        ClassOf(true, false, true, true, false, true, true), kClassLast);
  Check(ClassOf(true, false, true, true, false, false, true) == kClassFirst,
        "with far_grab_sign=0 a signpost is a scroll of the first class",
        ClassOf(true, false, true, true, false, false, true), kClassFirst);
  Check(ClassOf(false, false, true, true, false, true, true) == kClassFirst,
        "a choice with no ranks has no last class",
        ClassOf(false, false, true, true, false, true, true), kClassFirst);
  Check(ClassOf(true, false, true, false, false, true, true) == kClassFirst,
        "a scroll of a quest stays in the first class",
        ClassOf(true, false, true, false, false, true, true), kClassFirst);
  Check(ClassOf(true, false, false, false, true, true, true) == kClassFirst,
        "a miracle is in the first class (far_grab_miracle=1)",
        ClassOf(true, false, false, false, true, true, true), kClassFirst);
  Check(ClassOf(true, false, false, false, true, true, false) == kClassPlain,
        "with far_grab_miracle=0 a miracle has no rank",
        ClassOf(true, false, false, false, true, true, false), kClassPlain);
  Check(ClassOf(false, false, false, false, true, true, true) == kClassPlain,
        "a choice with no ranks gives a miracle no rank",
        ClassOf(false, false, false, false, true, true, true), kClassPlain);
  Check(ClassOf(true, true, false, false, false, true, true) == kClassFirst &&
            ClassOf(true, false, false, false, false, true, true) ==
                kClassPlain,
        "a living thing is in the first class, and a tree has no rank", 1, 1);
  Near("a scroll of a quest keeps its bias of 3 degrees",
       TouchBias(true, kClassFirst), 3.0, 1e-6);
  Near("a signpost of the last class has no bias", TouchBias(true, kClassLast),
       0.0, 1e-6);

  // A MIRACLE BESIDE A SIGNPOST, as in the pit of the tutorial. 100 units a
  // meter. The hand is 30 cm over the land. The miracle is 1 m away, 4.6 cm
  // across and 4.5 cm tall. The signpost stands 12 cm to the right of it.
  const float origin[3] = {0.0f, 30.0f, 0.0f};
  const Item kSeed = {"miracle", {0.0f, 0.0f, 100.0f}, 2.3f, 4.5f,
                      false, false, false, true, false};
  const Item kSign = {"signpost", {12.0f, 0.0f, 100.0f}, 3.0f, 0.0f,
                      false, true, true, false, false};
  {
    const Item scene[2] = {kSeed, kSign};
    const float middle[3] = {0.0f, 2.25f, 100.0f};
    float ray[3], off[3];
    Aim(origin, middle, ray);
    SceneOut o;
    // The aim on the miracle.
    int pick = PickScene(kChoiceDefault, 1, true, true, 100.0f, origin, ray,
                         scene, 2, -1, &o);
    printf("  the aim on the miracle: miracle te %.2f score %.2f, signpost te "
           "%.2f score %.2f\n", o.s[0].te, o.s[0].score, o.s[1].te,
           o.s[1].score);
    Check(pick == 0 && o.why == kWhyRank,
          "the aim on a miracle gives the miracle, by its rank", pick, 0);
    // The aim 2.5 degrees off the miracle, toward the signpost.
    Turn(ray, 2.5f, off);
    pick = PickScene(kChoiceDefault, 1, false, false, 100.0f, origin, off,
                     scene, 2, -1, &o);
    printf("  2.5 degrees toward the signpost: miracle te %.2f, signpost te "
           "%.2f\n", o.s[0].te, o.s[1].te);
    Check(o.s[0].te > 0.0f && o.s[0].te < 1.0f && o.s[1].te > 1.0f &&
              o.s[1].te < kLivingDeg,
          "there the aim is less than 1 degree past the cover of the miracle, "
          "and the signpost is in its cone but not in the aim", o.s[1].te, 2.6);
    Check(pick == 1 && o.why == kWhyRank,
          "with the keys at 0 the signpost has the halo there, by its rank",
          pick, 1);
    pick = PickScene(kChoiceDefault, 1, true, true, 100.0f, origin, off, scene,
                     2, -1, &o);
    Check(pick == 0, "with the keys at 1 the miracle has the halo there", pick,
          0);
    pick = PickScene(kChoiceDefault, 1, true, false, 100.0f, origin, off,
                     scene, 2, -1, &o);
    Check(pick == 0 && o.why == kWhyNotLast,
          "far_grab_sign=1 alone gives the miracle there too: a signpost comes "
          "after a plain thing", pick, 0);
    pick = PickScene(kChoiceDefault, 1, false, true, 100.0f, origin, off,
                     scene, 2, -1, &o);
    Check(pick == 0 && o.why == kWhyNearest,
          "far_grab_miracle=1 alone gives the miracle there: it is the nearer "
          "one of the first rank", pick, 0);
    // The signpost has the halo, and the aim comes back onto the miracle.
    pick = PickScene(kChoiceDefault, 1, false, false, 100.0f, origin, ray,
                     scene, 2, 1, &o);
    Check(pick == 1 && o.why == kWhyHeld,
          "with the keys at 0 a signpost that has the halo keeps it with the "
          "aim on the miracle", pick, 1);
    pick = PickScene(kChoiceDefault, 1, true, true, 100.0f, origin, ray, scene,
                     2, 1, &o);
    Check(pick == 0, "with the keys at 1 the miracle takes the halo from the "
          "signpost", pick, 0);
    // The aim on the signpost.
    float at_sign[3];
    Aim(origin, kSign.base, at_sign);
    pick = PickScene(kChoiceDefault, 1, true, true, 100.0f, origin, at_sign,
                     scene, 2, -1, &o);
    printf("  the aim on the signpost: miracle te %.2f, signpost te %.2f\n",
           o.s[0].te, o.s[1].te);
    Check(pick == 1 && o.why == kWhyLastOn,
          "the aim on the signpost and on no other target gives the signpost",
          pick, 1);
    // A tree behind the signpost, and the ray crosses its body.
    const Item behind[3] = {
        kSeed, kSign,
        {"tree", {12.0f, 0.0f, 112.0f}, 3.0f, 15.0f, false, false, false,
         false, false}};
    pick = PickScene(kChoiceDefault, 1, true, true, 100.0f, origin, at_sign,
                     behind, 3, -1, &o);
    Near("the ray on the signpost crosses the body of the tree behind it",
         o.s[2].te, 0.0, 1e-3);
    Check(pick == 2 && o.why == kWhyNotLast,
          "then the tree has the halo: a signpost comes last", pick, 2);
    pick = PickScene(kChoiceDefault, 1, false, true, 100.0f, origin, at_sign,
                     behind, 3, -1, &o);
    Check(pick == 1, "with far_grab_sign=0 the signpost has it, by its rank",
          pick, 1);
    // A signpost with no other candidate in the cone.
    const Item alone[1] = {kSign};
    Turn(at_sign, 5.0f, off);
    pick = PickScene(kChoiceDefault, 1, true, true, 100.0f, origin, off, alone,
                     1, -1, &o);
    Check(pick == 0 && o.s[0].te > 2.0f,
          "a signpost with no other target in the cone has the halo, also "
          "with the aim beside it", pick, 0);
  }

  // THE EDGE OF A BODY. The aim is on the target of now to kLastStayDeg, so
  // the halo does not go to and fro. Candidate 0 is a rock, 1 a signpost and
  // 2 a villager.
  {
    const StayRule rule = MakeStayRule(kChoiceRanks, kLivingDeg, 1, kStayDeg,
                                       kFarSelectDeg, kFarKeepDeg,
                                       kFarSwitchDeg);
    FarScore s[3] = {{true, 3.0f, 3.5f}, {true, 0.8f, 1.3f},
                     {false, 0.0f, 0.0f}};
    float near_deg[3] = {3.0f, 0.8f, 0.0f};
    const int cls[3] = {kClassPlain, kClassLast, kClassFirst};
    FarScore work[3];
    bool first[3];
    int why = 0;
    int pick = PickClassed(s, near_deg, cls, 3, 1, rule, work, first, &why);
    Check(pick == 1 && why == kWhyLastOn,
          "a signpost that has the halo keeps it to 1 degree past its body",
          pick, 1);
    pick = PickClassed(s, near_deg, cls, 3, -1, rule, work, first, &why);
    Check(pick == 0, "a new signpost needs the ray in its body", pick, 0);
    s[1].te = 1.5f;
    pick = PickClassed(s, near_deg, cls, 3, 1, rule, work, first, &why);
    Check(pick == 0, "past 1 degree the rock takes the halo from the signpost",
          pick, 0);
    s[1].te = 0.0f;
    s[0].te = 0.7f;
    pick = PickClassed(s, near_deg, cls, 3, 0, rule, work, first, &why);
    Check(pick == 0, "a rock that has the halo keeps it while the aim is less "
          "than 1 degree past its body, with the ray in a signpost", pick, 0);
    s[0].te = 1.5f;
    pick = PickClassed(s, near_deg, cls, 3, 0, rule, work, first, &why);
    Check(pick == 1, "and past 1 degree the signpost takes it", pick, 1);
    // A villager 2 degrees past his cover, and the ray in the signpost.
    s[0].valid = false;
    s[2].valid = true;
    s[2].te = 2.0f;
    s[2].score = 2.5f;
    near_deg[2] = 4.0f;
    pick = PickClassed(s, near_deg, cls, 3, -1, rule, work, first, &why);
    Check(pick == 1, "the ray in a signpost and beside a villager gives the "
          "signpost", pick, 1);
    s[2].te = 0.0f;
    pick = PickClassed(s, near_deg, cls, 3, -1, rule, work, first, &why);
    Check(pick == 2 && why == kWhyRank,
          "the ray in a signpost and in the body of a villager gives the "
          "villager", pick, 2);
    s[2].te = 2.0f;
    s[1].te = 0.5f;
    pick = PickClassed(s, near_deg, cls, 3, -1, rule, work, first, &why);
    Check(pick == 2, "a villager in his cone comes before a signpost that the "
          "ray is not in", pick, 2);
  }

  // NO SIGNPOST: THE ANSWER OF PickRanked. The rule of the quest character
  // beside a tree does not change.
  {
    const float low[3] = {0.0f, 30.0f, 0.0f};
    const Thing scene[2] = {
        {"man", {0.0f, 0.0f, 100.0f}, 0.4f, 1.8f, true},
        {"tree", {4.0f, 0.0f, 100.0f}, 3.0f, 15.0f, false}};
    const float crown[3] = {4.0f, 7.0f, 100.0f};
    float ray[3];
    Aim(low, crown, ray);
    int same = 0, cases = 0;
    for (int choice = 0; choice <= kChoiceMost; ++choice) {
      for (int stay = 0; stay <= 1; ++stay) {
        for (int current = -1; current <= 1; ++current) {
          for (int step = 0; step <= 6; ++step) {
            float turned[3];
            Turn(ray, -3.0f + static_cast<float>(step), turned);
            ChoiceOut o;
            const int want = PickChoice(choice, stay, low, turned, scene, 2,
                                        current, -1, &o);
            const StayRule rule = MakeStayRule(
                choice, kLivingDeg, stay, kStayDeg, kFarSelectDeg, kFarKeepDeg,
                kFarSwitchDeg);
            const int cls[2] = {o.first[0] ? kClassFirst : kClassPlain,
                                o.first[1] ? kClassFirst : kClassPlain};
            FarScore work[2];
            bool first[2];
            int why = 0;
            const int got = PickClassed(o.s, o.near_deg, cls, 2, current, rule,
                                        work, first, &why);
            ++cases;
            if (got == want && why == o.why) ++same;
          }
        }
      }
    }
    Check(same == cases, "with no signpost among the candidates, PickClassed "
          "gives the target and the reason of PickRanked in each case", same,
          cases);
  }

  // THE CREATURE BY ITS BODY (far_grab_creature). 50 units a meter. The
  // capsule of a Creature of size 0.22 is 3.63 units tall with a radius of
  // 1.02. The point of its mesh is at its feet.
  {
    // The hand is 30 cm over the land and 44 cm from the Creature. The aim
    // is on its head, and the ray crosses the body of a tree behind it.
    const float hand[3] = {0.0f, 15.0f, 0.0f};
    const float head[3] = {0.0f, 3.4f, 22.0f};
    float ray[3];
    Aim(hand, head, ray);
    Item scene[2] = {
        {"Creature", {0.0f, 0.0f, 22.0f}, 1.02f, 3.63f, true, false, false,
         false, true},
        {"tree", {2.0f, 0.0f, 30.0f}, 3.0f, 15.0f, false, false, false, false,
         false}};
    SceneOut o;
    int pick = PickScene(kChoiceDefault, 1, true, true, 50.0f, hand, ray,
                         scene, 2, -1, &o);
    printf("  a small Creature 44 cm away, by its point: Creature te %.2f "
           "score %.2f, tree te %.2f score %.2f\n", o.s[0].te, o.s[0].score,
           o.s[1].te, o.s[1].score);
    Check(o.s[0].te > kLivingDeg,
          "by the point of its mesh, the aim on the head of a small Creature "
          "44 cm away is out of the cone of a living thing", o.s[0].te, 4.3);
    Check(pick == 1, "and the tree behind it has the halo", pick, 1);
    scene[0].point = false;
    pick = PickScene(kChoiceDefault, 1, true, true, 50.0f, hand, ray, scene, 2,
                     -1, &o);
    Near("by its body, the aim on its head is on the Creature", o.s[0].te, 0.0,
         1e-3);
    Check(pick == 0 && o.why == kWhyRank,
          "and the Creature has the halo, by its rank", pick, 0);
  }
  {
    // A Creature of size 0.5 (8.25 units tall, a radius of 2.6), 70 cm from a
    // hand that is 40 cm over the land.
    const float hand[3] = {0.0f, 20.0f, 0.0f};
    const float head[3] = {0.0f, 7.8f, 35.0f};
    float ray[3], off[3];
    Aim(hand, head, ray);
    Item scene[2] = {
        {"Creature", {0.0f, 0.0f, 35.0f}, 2.6f, 8.25f, true, false, false,
         false, true},
        {"tree", {2.5f, 0.0f, 45.0f}, 3.0f, 15.0f, false, false, false, false,
         false}};
    SceneOut o;
    int pick = PickScene(kChoiceDefault, 1, true, true, 50.0f, hand, ray,
                         scene, 2, -1, &o);
    printf("  a Creature of size 0.5, 70 cm away, by its point: Creature te "
           "%.2f, tree te %.2f\n", o.s[0].te, o.s[1].te);
    Check(pick == 1 && o.s[0].te > kLivingDeg,
          "by its point, the aim on the head of a Creature of size 0.5 gives "
          "the tree behind it", pick, 1);
    scene[0].point = false;
    pick = PickScene(kChoiceDefault, 1, true, true, 50.0f, hand, ray, scene, 2,
                     -1, &o);
    Check(pick == 0 && o.s[0].te < 1e-3f,
          "by its body, the same aim gives the Creature", pick, 0);
    // The Creature has the halo, and the hand shakes to the side.
    Turn(ray, 9.5f, off);
    pick = PickScene(kChoiceDefault, 1, true, true, 50.0f, hand, off, scene, 2,
                     0, &o);
    printf("  9.5 degrees to the side: Creature te %.2f, tree te %.2f\n",
           o.s[0].te, o.s[1].te);
    Check(o.s[0].te > kLivingDeg && o.s[0].te < kLivingDeg + kStayDeg,
          "there the aim is past the cone of the Creature and in its stay "
          "margin", o.s[0].te, 4.3);
    Check(pick == 0, "and the Creature keeps the halo, as a living thing does",
          pick, 0);
    // A tree 12 units beside the Creature, with the aim on its crown.
    const Item beside[2] = {
        scene[0],
        {"tree", {12.0f, 0.0f, 40.0f}, 3.0f, 15.0f, false, false, false, false,
         false}};
    const float crown[3] = {12.0f, 8.0f, 40.0f};
    Aim(hand, crown, ray);
    pick = PickScene(kChoiceDefault, 1, true, true, 50.0f, hand, ray, beside,
                     2, -1, &o);
    printf("  the aim on the crown of a tree beside it: Creature te %.2f, "
           "tree te %.2f\n", o.s[0].te, o.s[1].te);
    Check(pick == 1, "a tree beside the Creature is the target when the aim "
          "is on its crown", pick, 1);
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
  FarClassChecks();
}

}  // namespace far_choice_checks
