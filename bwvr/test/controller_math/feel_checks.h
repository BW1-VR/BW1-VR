// HANDS THAT REACT TO THE WORLD (docs/hand.md): the finger tables, the land
// patch, the free curl, the filter, the approach, the game's hold and the
// wrap of a body in the hand. main.cpp includes this file after Check and
// Near, just before main.
//
// The test has no game. The hand is the low poly hand of overlay_math.h, 19
// units long, as the real hand is at 100 units a meter. The land is a
// function, and y is up.
#pragma once

#include "../../src/mod/hand_feel_math.h"

namespace feel_checks {

namespace hf = hand_feel;
namespace om = overlay_math;

const float kLength = 19.0f;
// The right hand with the palm down: the back of the hand is up (+y of the
// world), the fingers go along +z of the world and the knuckles along x.
const float kPalmDown[9] = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f,
                            0.0f, -1.0f, 0.0f};
// The right hand with the fingers down: the fingers go along -y of the world
// and the palm faces -z.
const float kFingersDown[9] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
                               1.0f, 0.0f, 0.0f};

struct Hand {
  hf::FingerTable local[hf::kFingers];
  hf::FingerTable world[hf::kFingers];
  float radius[hf::kFingers][hf::kSamples];
};

inline void MakeHand(const float position[3], const float rotation[9],
                     Hand* out) {
  for (int f = 0; f < hf::kFingers; ++f) {
    hf::FillTable(f, [](int finger, float curl, float joints[hf::kPoints][3]) {
      om::FingerJoints(finger, curl, joints);
    }, &out->local[f]);
    hf::PlaceTable(out->local[f], position, rotation, kLength, false,
                   &out->world[f]);
    hf::SampleRadii(om::kShape[f].radius * kLength, 0.5f, out->radius[f]);
  }
}

template <typename Ground>
inline void MakeLand(float x, float z, Ground ground, hf::World* world) {
  world->land = true;
  world->bodies = 0;
  hf::FillPatch(x, z, 0.6f * kLength, ground, &world->patch);
}

inline float DepthOf(const Hand& hand, const hf::World& world, int finger,
                     float curl) {
  float points[hf::kPoints][3], samples[hf::kSamples][3];
  hf::TableAt(hand.world[finger], curl, points);
  hf::Samples(points, samples);
  return hf::Depth(world, samples, hand.radius[finger], nullptr);
}

inline hf::Free Solve(const Hand& hand, const hf::World& world, int finger,
                      float pose, bool may_open, int last_side = 0) {
  return hf::FreeCurl(pose, may_open, last_side, [&](float curl) {
    return DepthOf(hand, world, finger, curl);
  });
}

inline void FeelChecks() {
  printf("\nHands that react to the world: the tables and the land patch\n");
  {
    hf::FingerTable table;
    hf::FillTable(2, [](int finger, float curl, float joints[hf::kPoints][3]) {
      om::FingerJoints(finger, curl, joints);
    }, &table);
    float worst_row = 0.0f, worst_mix = 0.0f;
    for (int i = 0; i <= 64; ++i) {
      const float curl = static_cast<float>(i) / 64.0f;
      float mix[hf::kPoints][3], exact[hf::kPoints][3];
      hf::TableAt(table, curl, mix);
      om::FingerJoints(2, curl, exact);
      for (int p = 0; p < hf::kPoints; ++p) {
        float d = 0.0f;
        for (int k = 0; k < 3; ++k) {
          d += (mix[p][k] - exact[p][k]) * (mix[p][k] - exact[p][k]);
        }
        d = sqrtf(d);
        if (i % 4 == 0 && d > worst_row) worst_row = d;
        if (d > worst_mix) worst_mix = d;
      }
    }
    Near("a curl on a row of the table gives the points of the hand model",
         worst_row, 0.0, 1e-5);
    Check(worst_mix < 0.002f, "a curl between two rows is within 0.2 percent "
          "of the hand length of the true place", worst_mix, 0.002);
    // The hand frame and back, for both hands.
    const float position[3] = {1400.0f, 20.0f, 2000.0f};
    const float local[3] = {0.13f, -0.3f, 0.1f};
    float world[3], back[3];
    float worst = 0.0f;
    for (int left = 0; left < 2; ++left) {
      om::PlacePoint(position, kFingersDown, kLength, left != 0, local, world);
      hf::ToHandFrame(position, kFingersDown, kLength, left != 0, world, back);
      for (int k = 0; k < 3; ++k) {
        if (fabsf(back[k] - local[k]) > worst) worst = fabsf(back[k] - local[k]);
      }
    }
    Near("a point of the world goes back to its place in the hand frame, for "
         "the right hand and the left hand", worst, 0.0, 1e-5);
    float radius[hf::kSamples];
    hf::SampleRadii(0.9f, 0.5f, radius);
    Near("the tip of a finger has the clearance of the Hand's point as its "
         "radius", radius[hf::kSamples - 1], 0.5, 1e-6);
    Near("and each other sample has 0.8 of the radius of the finger",
         radius[0], 0.72, 1e-6);
    hf::SampleRadii(0.4f, 0.5f, radius);
    Near("a tip is never wider than the finger", radius[hf::kSamples - 1],
         0.32, 1e-6);
  }
  {
    hf::LandPatch patch;
    hf::FillPatch(100.0f, 200.0f, 12.0f, [](float x, float z) {
      return 5.0f + 0.3f * x - 0.2f * z;
    }, &patch);
    Near("the patch gives the height of a slope between its points",
         hf::PatchHeight(patch, 103.3f, 195.1f),
         5.0 + 0.3 * 103.3 - 0.2 * 195.1, 1e-3);
    Near("past its edge the patch gives the height of the edge",
         hf::PatchHeight(patch, 500.0f, 200.0f),
         5.0 + 0.3 * 112.0 - 0.2 * 200.0, 1e-3);
    Near("the patch has 8 by 8 points", patch.step, 24.0 / 7.0, 1e-5);
  }

  printf("\nHands that react to the world: the free curl\n");
  {
    // A finger that is in the world from a curl of 0.30 to 0.60.
    const auto band = [](float curl) {
      return curl > 0.30f && curl < 0.60f ? 1.0f : -1.0f;
    };
    hf::Free free = hf::FreeCurl(0.20f, true, 0, band);
    Check(!free.blocked && free.free && free.curl == 0.20f && free.side == 0,
          "a finger that is free keeps the curl of its pose", free.curl, 0.20);
    free = hf::FreeCurl(0.40f, true, 0, band);
    Check(free.blocked && free.free && free.side == -1,
          "a finger in the world with an open grip goes to the nearer free "
          "curl: it opens", free.side, -1);
    Near("and it stops where the finger comes free", free.curl, 0.30, 0.003);
    free = hf::FreeCurl(0.40f, false, 0, band);
    Check(free.side == 1, "with the grip pressed the world can only add "
          "bend", free.side, 1);
    Near("so the finger goes to the free curl above", free.curl, 0.60, 0.003);
    free = hf::FreeCurl(0.50f, true, 0, band);
    Near("a finger nearer to the upper end bends more", free.curl, 0.60,
         0.003);
    free = hf::FreeCurl(0.44f, true, 1, band);
    Near("a finger near the middle stays on the side of the last frame: more "
         "bend", free.curl, 0.60, 0.003);
    free = hf::FreeCurl(0.44f, true, -1, band);
    Near("or less bend", free.curl, 0.30, 0.003);
    free = hf::FreeCurl(0.33f, true, 1, band);
    Near("but not when the other side is nearer by more than 0.10",
         free.curl, 0.30, 0.003);
    // No free curl: deeper as the finger opens.
    free = hf::FreeCurl(0.20f, true, 0,
                        [](float curl) { return 2.0f - curl; });
    Check(free.blocked && !free.free && free.curl == 1.0f,
          "with no free curl the finger takes the curl with the least depth",
          free.curl, 1.0);
    free = hf::FreeCurl(0.20f, true, 0, [](float) { return 2.0f; });
    Check(!free.free && free.curl == 0.20f && free.side == 0,
          "and the curl of the pose when no curl is less deep", free.curl,
          0.20);
    Check(hf::MayOpen(0.0f) && hf::MayOpen(0.15f) && !hf::MayOpen(0.16f) &&
              !hf::MayOpen(1.0f),
          "the world opens a finger only while its trigger or grip is at 0.15 "
          "or less", hf::MayOpen(0.16f), 0);
  }

  printf("\nHands that react to the world: the land\n");
  {
    const auto flat = [](float, float) { return 0.0f; };
    const float rest = om::kRestCurl;
    // The palm down, with the grip point on the land. The knuckles are 2.47
    // units over the land, and a finger at rest dips 2.6 units.
    const float low[3] = {1000.0f, 0.0f, 1000.0f};
    Hand hand;
    MakeHand(low, kPalmDown, &hand);
    hf::World world;
    MakeLand(low[0], low[2], flat, &world);
    Check(DepthOf(hand, world, 2, rest) > 0.0f &&
              DepthOf(hand, world, 2, 0.0f) < 0.0f,
          "palm down on flat land: the middle finger at rest is in the land, "
          "and the straight finger is not", DepthOf(hand, world, 2, rest), 0.0);
    hf::Free free = Solve(hand, world, 2, rest, true);
    Check(free.blocked && free.free && free.curl < rest && free.side == -1,
          "so the finger opens", free.curl, rest);
    Check(DepthOf(hand, world, 2, free.curl) <= 0.0f &&
              DepthOf(hand, world, 2, free.curl + 0.01f) > 0.0f,
          "and it rests on the land: a little more curl is in the land again",
          DepthOf(hand, world, 2, free.curl + 0.01f), 0.0);
    float tip[hf::kPoints][3];
    hf::TableAt(hand.world[2], free.curl, tip);
    Near("the tip is 0.5 cm of the room over the land, as the Hand's point "
         "is", tip[hf::kPoints - 1][1], 0.5, 0.06);
    free = Solve(hand, world, 2, rest, false);
    Check(free.blocked && !free.free && free.curl == rest,
          "with the grip pressed the finger keeps its curl: the player's "
          "finger wins", free.curl, rest);
    // The flat hand lies flat: with the grip point 1.2 units under the land,
    // each finger is straight.
    const float flat_at[3] = {1000.0f, -1.2f, 1000.0f};
    MakeHand(flat_at, kPalmDown, &hand);
    float most = 0.0f;
    for (int f = 1; f < hf::kFingers; ++f) {
      const float curl = Solve(hand, world, f, rest, true).curl;
      if (curl > most) most = curl;
    }
    Check(most < 0.05f, "a hand that lies on the land has flat fingers", most,
          0.05);
    // A hand in the air.
    const float high[3] = {1000.0f, 12.0f, 1000.0f};
    MakeHand(high, kPalmDown, &hand);
    free = Solve(hand, world, 2, rest, true);
    Check(!free.blocked && free.curl == rest,
          "a hand over the land keeps its curls", free.curl, rest);

    // The fingers down. The straight middle finger is 9.69 units long from
    // the grip point.
    const float poke[3] = {1000.0f, 9.0f, 1000.0f};
    MakeHand(poke, kFingersDown, &hand);
    free = Solve(hand, world, 2, rest, true);
    Check(free.blocked && free.free && free.curl > rest && free.side == 1,
          "fingers down into the land: the middle finger bends more",
          free.curl, rest);
    hf::TableAt(hand.world[2], free.curl, tip);
    Near("and its tip rests on the land", tip[hf::kPoints - 1][1], 0.5, 0.06);
    const float first = free.curl;
    const float deeper[3] = {1000.0f, 8.0f, 1000.0f};
    MakeHand(deeper, kFingersDown, &hand);
    free = Solve(hand, world, 2, rest, true);
    Check(free.free && free.curl > first,
          "a hand that goes down 1 cm more bends the finger more", free.curl,
          first);
    Check(Solve(hand, world, 4, rest, true).curl <
              Solve(hand, world, 2, rest, true).curl,
          "the little finger is shorter, so it bends less than the middle "
          "finger", Solve(hand, world, 4, rest, true).curl, free.curl);
    const float under[3] = {1000.0f, -8.0f, 1000.0f};
    MakeHand(under, kFingersDown, &hand);
    free = Solve(hand, world, 2, rest, true);
    Check(free.blocked && !free.free,
          "a hand that is under the land has no free curl", free.free, 0);

    // A slope across the knuckles: the land goes up toward the index finger,
    // 3 units in 10.
    const auto slope = [](float x, float) { return 0.3f * (x - 1000.0f); };
    const float on_slope[3] = {1000.0f, 8.0f, 1000.0f};
    MakeHand(on_slope, kFingersDown, &hand);
    MakeLand(on_slope[0], on_slope[2], slope, &world);
    const hf::Free index = Solve(hand, world, 1, rest, true);
    const hf::Free ring = Solve(hand, world, 3, rest, true);
    Check(index.free && ring.free && index.curl > ring.curl + 0.03f,
          "on a slope the finger on the high side bends more than the finger "
          "on the low side", index.curl, ring.curl);
    Check(fabsf(DepthOf(hand, world, 1, index.curl)) < 0.08f &&
              fabsf(DepthOf(hand, world, 3, ring.curl)) < 0.08f,
          "and each of the two rests on the slope",
          DepthOf(hand, world, 1, index.curl), 0.0);
  }

  printf("\nHands that react to the world: the bodies\n");
  {
    const hf::Body house = {hf::kCylinder, {0.0f, 0.0f, 0.0f}, 5.0f, 8.0f};
    const float inside[3] = {2.0f, 3.0f, 0.0f};
    const float beside[3] = {8.0f, 3.0f, 0.0f};
    const float over[3] = {0.0f, 10.0f, 0.0f};
    Near("a point in a cylinder has no gap", hf::BodyGapAt(house, inside), 0.0,
         1e-6);
    Near("a point beside it has the gap to its side",
         hf::BodyGapAt(house, beside), 3.0, 1e-5);
    Near("a point over it has the gap to its top", hf::BodyGapAt(house, over),
         2.0, 1e-5);
    const hf::Body creature = {hf::kCapsule, {0.0f, 0.0f, 0.0f}, 2.0f, 6.0f};
    Near("the capsule of a Creature: the gap from its axis less its radius",
         hf::BodyGapAt(creature, beside), 6.0, 1e-5);
    const hf::Body ball = {hf::kBall, {0.0f, 3.0f, 0.0f}, 1.5f, 0.0f};
    Near("a ball: the gap from its middle less its radius",
         hf::BodyGapAt(ball, beside), 6.5, 1e-5);
    const hf::Body tree = {hf::kTree, {0.0f, 0.0f, 0.0f}, 4.0f, 12.0f};
    const float at_trunk[3] = {1.5f, 2.0f, 0.0f};
    Near("a tree: the trunk is a quarter of the radius, so a point 1.5 units "
         "from the axis is 0.5 units from it", hf::BodyGapAt(tree, at_trunk),
         0.5, 1e-4);
    const float in_crown[3] = {3.0f, 9.0f, 0.0f};
    const float under_crown[3] = {3.0f, 3.0f, 0.0f};
    Near("the crown is in the upper half: a point 3 units from the axis at "
         "three quarters of the height is in it",
         hf::BodyGapAt(tree, in_crown), 0.0, 1e-6);
    Check(hf::BodyGapAt(tree, under_crown) > 1.5f,
          "and the same point at a quarter of the height is in the air beside "
          "the trunk", hf::BodyGapAt(tree, under_crown), 2.0);
    // A house of 8 by 4 units and 5 units high, turned 90 degrees about the
    // up axis, with its middle 2.5 units over the land.
    hf::Body box = {hf::kBox, {0.0f, 2.5f, 0.0f}, 4.5f, 5.0f,
                    {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f},
                    {4.0f, 2.5f, 2.0f}};
    const float at_wall[3] = {3.0f, 2.0f, 0.0f};
    const float at_end[3] = {0.0f, 2.0f, 6.5f};
    Near("the box of a house: a point 3 units from its middle across the "
         "house is 1 unit from the wall", hf::BoxGap(box, at_wall), 1.0, 1e-5);
    Near("and a point 6.5 units along the house is 2.5 units from its end",
         hf::BodyGapAt(box, at_end), 2.5, 1e-5);
    Near("a point in the box has no gap", hf::BoxGap(box, inside), 0.0, 1e-6);
    Check(hf::BodyCounts(box, 19.0f) && !hf::BodyCounts(box, 5.0f),
          "a house of 4.5 units counts for a hand of 19 units, and not for a "
          "hand of 5 units", hf::BodyCounts(box, 19.0f), 1);
    const hf::Body temple = {hf::kCylinder, {0.0f, 0.0f, 0.0f}, 30.0f, 35.0f};
    Check(!hf::BodyCounts(temple, 19.0f) && hf::BodyCounts(creature, 2.0f),
          "the Temple is larger than the hand, so it does not count. A "
          "Creature counts at each size", hf::BodyCounts(temple, 19.0f), 0);
    const float a[3] = {0.0f, 0.0f, 0.0f}, b[3] = {0.0f, 0.0f, 4.0f};
    const float p[3] = {3.0f, 0.0f, 8.0f};
    Near("the gap to a capsule past its end is the gap to the ball of the "
         "end", hf::SegmentGap(a, b, 1.0f, p), 4.0, 1e-5);

    // The palm down, and the wall of a house in front of the finger tips.
    // The straight middle finger ends at z = 1009.69.
    const float at[3] = {1000.0f, 20.0f, 1000.0f};
    Hand hand;
    MakeHand(at, kPalmDown, &hand);
    hf::World world;
    world.land = false;
    world.bodies = 1;
    world.body[0] = {hf::kCylinder, {1000.0f, 0.0f, 1018.0f}, 9.0f, 40.0f};
    int what = hf::kInNothing;
    float points[hf::kPoints][3], samples[hf::kSamples][3];
    hf::TableAt(hand.world[2], om::kRestCurl, points);
    hf::Samples(points, samples);
    const float depth = hf::Depth(world, samples, hand.radius[2], &what);
    Check(depth > 0.0f && what == hf::kInBody,
          "a hand against a house: the middle finger at rest is in the body",
          what, hf::kInBody);
    const hf::Free free = Solve(hand, world, 2, om::kRestCurl, true);
    Check(free.blocked && free.free && free.curl > om::kRestCurl,
          "so the finger bends against the wall", free.curl, om::kRestCurl);
    Check(fabsf(DepthOf(hand, world, 2, free.curl)) < 0.08f,
          "and it rests on the wall", DepthOf(hand, world, 2, free.curl), 0.0);
    world.body[0].base[2] = 1040.0f;
    Check(!Solve(hand, world, 2, om::kRestCurl, true).blocked,
          "a house that is 20 cm away does not move the finger", 0, 0);
    // The land and a body: the deeper one counts.
    world.land = true;
    hf::FillPatch(at[0], at[2], 12.0f, [](float, float) { return 30.0f; },
                  &world.patch);
    hf::Depth(world, samples, hand.radius[2], &what);
    Check(what == hf::kInLand, "a finger that is deeper in the land than in a "
          "body is in the land", what, hf::kInLand);
  }

  printf("\nHands that react to the world: the filter and the pulse\n");
  {
    Near("after one time constant the filter is at 63 percent",
         hf::FilterStep(0.0f, 1.0f, hf::kFilterSeconds, hf::kFilterSeconds),
         0.632, 0.001);
    float value = 0.0f;
    for (int i = 0; i < 9; ++i) {
      value = hf::FilterStep(value, 0.5f, 1.0f / 90.0f, hf::kFilterSeconds);
    }
    Check(value > 0.40f && value < 0.5f,
          "after 0.1 s at 90 frames a second the curl is at 86 percent of "
          "its way, and not past the target", value, 0.43);
    Near("a frame with no time changes nothing",
         hf::FilterStep(0.2f, 1.0f, 0.0f, hf::kFilterSeconds), 0.2, 1e-6);
    Check(hf::kFilterSeconds <= 0.10f, "the filter is short: 0.1 s or less",
          hf::kFilterSeconds, 0.10);
    float quiet = 10.0f;
    int pulses = 0;
    for (int i = 0; i < 90; ++i) {
      if (hf::MeetPulse(true, 1.0f / 90.0f, &quiet)) ++pulses;
    }
    Check(pulses == 1, "a hand that stays on a surface for a second gives "
          "one pulse", pulses, 1);
    for (int i = 0; i < 5; ++i) hf::MeetPulse(false, 1.0f / 90.0f, &quiet);
    Check(!hf::MeetPulse(true, 1.0f / 90.0f, &quiet),
          "a hand that comes back after 0.06 s gives no pulse", 0, 0);
    for (int i = 0; i < 27; ++i) hf::MeetPulse(false, 1.0f / 90.0f, &quiet);
    Check(hf::MeetPulse(true, 1.0f / 90.0f, &quiet),
          "a hand that comes back after 0.3 s gives a pulse", 1, 1);
    Near("the pulse of a surface is the faintest of the set",
         haptics_math::Scaled(haptics_math::kSurface, 1.0f, 100).amplitude,
         haptics_math::Scaled(haptics_math::kNoTarget, 1.0f, 100).amplitude,
         1e-6);
    Check(hf::IdleCurl(0.0f, 0) == 0.0f &&
              hf::IdleCurl(3.14159265f, 0) <= hf::kIdleCurl + 1e-6f,
          "the idle wave adds 0.05 of curl at most", hf::kIdleCurl, 0.05);
    Near("and a frame with no time does not move it",
         hf::IdlePhase(1.0f, 0.0f), 1.0, 1e-6);
  }

  printf("\nHands that react to the world: the approach\n");
  {
    // 100 units a meter: the reach of the pick is 4 units, the touch 1 unit.
    Near("at the reach of the pick the hand has the curls of its pose",
         hf::ApproachWeight(4.0f, 4.0f, 1.0f), 0.0, 1e-6);
    Near("at the touch the hand has the pose of the game",
         hf::ApproachWeight(1.0f, 4.0f, 1.0f), 1.0, 1e-6);
    Near("in the body too", hf::ApproachWeight(0.0f, 4.0f, 1.0f), 1.0, 1e-6);
    Near("half the way is half the weight",
         hf::ApproachWeight(2.5f, 4.0f, 1.0f), 0.5, 1e-6);
    Check(hf::ApproachWeight(3.0f, 4.0f, 1.0f) <
              hf::ApproachWeight(2.0f, 4.0f, 1.0f),
          "a nearer hand is more open", 1, 1);
    Near("past the reach there is no approach",
         hf::ApproachWeight(6.0f, 4.0f, 1.0f), 0.0, 1e-6);
    Near("a tree is large: the open hand of frame 0 of can_pickup",
         hf::PickupFrame(6.0f / 19.0f), 0.0, 1e-6);
    Near("a villager is small: near frame 2, the thumb at the index finger",
         hf::PickupFrame(1.0f / 19.0f), 1.649, 0.001);
    float curls[hf::kFingers];
    hf::FrameCurls(hf::kPickupCurls, 3, 2.0f, curls);
    Check(curls[0] > 0.6f && curls[1] == 0.0f,
          "in frame 2 the thumb is bent and the index finger is straight",
          curls[0], 0.62);
    hf::FrameCurls(hf::kPickupCurls, 3, 0.5f, curls);
    Near("a frame between two frames is their mix", curls[4],
         0.5 * (0.08 + 0.21), 1e-6);
    hf::FrameCurls(hf::kPickupCurls, 3, 9.0f, curls);
    Near("a frame past the last one is the last one", curls[0], 0.62, 1e-6);
    Near("with the grip open the finger goes to the pose of the game",
         hf::ApproachCurl(0.12f, 0.60f, 1.0f, 0.0f), 0.60, 1e-6);
    Near("a grip that is half closed shows the curl of the grip only",
         hf::ApproachCurl(0.56f, 0.10f, 1.0f, 0.5f), 0.56, 1e-6);
    Near("and a grip at a quarter shows half of the approach",
         hf::ApproachCurl(0.34f, 0.10f, 1.0f, 0.25f), 0.22, 1e-6);
    Near("the form of the approach is 0.55 of the curl that holds the "
         "target", hf::PreShapeCurl(0.80f, 0.10f), 0.44, 1e-6);
    Near("and never less than the curl of can_pickup",
         hf::PreShapeCurl(0.10f, 0.20f), 0.20, 1e-6);
  }

  printf("\nHands that react to the world: the hold\n");
  {
    float curls[hf::kFingers];
    hf::GameHoldCurls(hf::kHoldAbove, 0.0f, kLength, curls);
    Near("the game's hold of a pebble from above is the closed frame of "
         "hold_above", curls[2], 0.58, 1e-6);
    hf::GameHoldCurls(hf::kHoldAbove, 1.2f * kLength, kLength, curls);
    Near("and of an object 1.2 hands in radius the open frame", curls[2],
         0.01, 1e-6);
    hf::GameHoldCurls(hf::kHoldAbove, 0.6f * kLength, kLength, curls);
    Near("half that radius is the middle frame", curls[3], 0.18, 1e-6);
    hf::GameHoldCurls(hf::kHoldLiving, 0.4f, kLength, curls);
    Check(curls[1] > 0.9f && curls[0] < 0.2f,
          "the game holds a villager from the side: the fingers closed and "
          "the thumb out", curls[1], 0.96);
    hf::GameHoldCurls(hf::kHoldTree, kLength, kLength, curls);
    Near("an object from the side with a hold radius of a hand opens the "
         "hand", curls[1], 0.0, 1e-6);
    hf::GameHoldCurls(hf::kHoldSeed, 1.0f, kLength, curls);
    Near("a miracle seed has the hand of the mouse player", curls[4], 0.21,
         1e-6);
    Check(hf::HoldFromSide(5) && hf::HoldFromSide(6) && hf::HoldFromSide(7) &&
              !hf::HoldFromSide(1) && !hf::HoldFromSide(2),
          "the hold types 5, 6 and 7 are the holds from the side", 1, 1);

    hf::HeldBody body;
    const float a[3] = {0.0f, 0.1f, 0.0f}, far_b[3] = {0.0f, 0.1f, 0.8f};
    hf::MakeHeldBody(a, a, 0.01f, &body);
    Near("a small short body counts as 0.07 hand lengths: a loose cup",
         body.radius, hf::kHeldSmallRadius, 1e-6);
    hf::MakeHeldBody(a, far_b, 0.01f, &body);
    Near("a thin long body counts as 0.03: the fingers close on a trunk",
         body.radius, hf::kHeldMinRadius, 1e-6);
    hf::MakeHeldBody(a, a, 0.15f, &body);
    Near("a large body keeps its radius", body.radius, 0.15, 1e-6);

    // The ball of a rock at the palm.
    hf::FingerTable tables[hf::kFingers];
    float finger_radius[hf::kFingers];
    for (int f = 0; f < hf::kFingers; ++f) {
      hf::FillTable(f, [](int finger, float curl,
                          float joints[hf::kPoints][3]) {
        om::FingerJoints(finger, curl, joints);
      }, &tables[f]);
      finger_radius[f] = om::kShape[f].radius;
    }
    float center[3];
    om::HoldCenter(0.12f, center);
    hf::MakeHeldBody(center, center, 0.12f, &body);
    hf::GameHoldCurls(hf::kHoldAbove, 0.12f * kLength, kLength, curls);
    om::PoseRange range;
    int touches[hf::kFingers];
    hf::HoldRange(tables, finger_radius, nullptr, body, curls, false, &range,
                  touches);
    Check(touches[1] == 2 && touches[2] == 2 && touches[3] == 2 &&
              touches[4] == 2,
          "a ball at the palm: each of the four fingers stops on it",
          touches[2], 2);
    float worst = 0.0f;
    for (int f = 1; f < hf::kFingers; ++f) {
      const float d = fabsf(range.hi[f] - om::ContactCurl(f, center, 0.12f));
      if (d > worst) worst = d;
    }
    Check(worst < 0.08f, "the curl of each finger is the contact curl of the "
          "builds before, within 0.08", worst, 0.08);
    bool clear = true;
    for (int f = 1; f < hf::kFingers; ++f) {
      float radius[hf::kSamples], points[hf::kPoints][3];
      float samples[hf::kSamples][3];
      for (int s = 0; s < hf::kSamples; ++s) radius[s] = finger_radius[f];
      hf::TableAt(tables[f], range.hi[f], points);
      hf::Samples(points, samples);
      for (int s = 0; s < hf::kSamples; ++s) {
        if (hf::SegmentGap(body.a, body.b, body.radius, samples[s]) <
            radius[s] - 1e-4f) {
          clear = false;
        }
      }
    }
    Check(clear, "at the hold no finger is in the body", clear, 1);
    Near("the index finger with the trigger open is at 0.75 of its hold",
         range.lo[1], 0.75 * range.hi[1], 1e-6);
    Check(range.lo[2] <= om::kRestCurl + 1e-6f,
          "the middle finger with the grip open is at the rest curl or less",
          range.lo[2], om::kRestCurl);
    const float first = range.hi[2];
    om::HoldCenter(0.20f, center);
    hf::MakeHeldBody(center, center, 0.20f, &body);
    hf::HoldRange(tables, finger_radius, nullptr, body, curls, false, &range,
                  touches);
    Check(range.hi[2] < first, "a larger ball opens the middle finger more",
          range.hi[2], first);
    const hf::HeldBody large_ball = body;
    const float cup = range.hi[2];

    // The trunk of a tree across the palm, along the knuckles.
    const float trunk_a[3] = {-0.02f, 0.14f, -0.6f};
    const float trunk_b[3] = {-0.02f, 0.14f, 0.6f};
    hf::MakeHeldBody(trunk_a, trunk_b, 0.03f, &body);
    hf::GameHoldCurls(hf::kHoldTree, 0.6f, kLength, curls);
    hf::HoldRange(tables, finger_radius, &body, large_ball, curls, true,
                  &range, touches);
    Check(touches[1] == 1 && touches[2] == 1 && touches[3] == 1 &&
              touches[4] == 1,
          "a trunk across the palm: the four fingers close on it", touches[1],
          1);
    Check(range.hi[2] > 0.5f, "a thin trunk closes the hand", range.hi[2],
          0.5);
    Check(range.hi[0] <= curls[0] + 1e-6f,
          "and the thumb stays out, as in the game's hold from the side",
          range.hi[0], curls[0]);
    // A trunk that no finger can reach: the ball of the builds before.
    const float away_a[3] = {-2.0f, 0.0f, -0.6f};
    const float away_b[3] = {-2.0f, 0.0f, 0.6f};
    hf::MakeHeldBody(away_a, away_b, 0.03f, &body);
    hf::HoldRange(tables, finger_radius, &body, large_ball, curls, true,
                  &range, touches);
    Check(touches[2] == 2 && fabsf(range.hi[2] - cup) < 1e-6f,
          "a finger that cannot touch the trunk stops on the ball at the "
          "palm: the cup of the builds before", range.hi[2], cup);
    // No body that a finger can reach: the game's own pose.
    const float away[3] = {-2.0f, 0.0f, 0.0f};
    hf::HeldBody far_ball;
    hf::MakeHeldBody(away, away, 0.05f, &far_ball);
    hf::HoldRange(tables, finger_radius, nullptr, far_ball, curls, false,
                  &range, touches);
    Check(touches[1] == 0 && touches[2] == 0 && range.hi[2] == curls[2] &&
              range.hi[0] == curls[0],
          "a finger that can touch no body takes the curl of the game's own "
          "hold", range.hi[2], curls[2]);
    // A body that the open finger is in.
    const float in_finger[3] = {om::kPalmX, -0.20f, 0.05f};
    hf::MakeHeldBody(in_finger, in_finger, 0.08f, &body);
    hf::HoldRange(tables, finger_radius, nullptr, body, curls, false, &range,
                  touches);
    Near("a finger that is in the body when it is open stays open",
         range.hi[2], 0.0, 1e-6);
  }
}

}  // namespace feel_checks
