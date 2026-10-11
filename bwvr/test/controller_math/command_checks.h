// THE CREATURE COMMANDS (docs/creature.md, "The commands"): the shake of
// the real hand that takes the leash off, and what a release of the trigger
// does. main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/creature_command_math.h"

namespace command_checks {

using command_math::ShakeResult;
using command_math::ShakeState;

const double kTwoPi = 6.283185307179586;

// A hand that rests for rest_s and then moves along a unit axis as
// amplitude * sin(2 pi hz t), for seconds, at rate samples a second. Returns
// the count of the shakes, and the time of the first one after the rest.
struct Run {
  int shakes;
  double first;       // seconds after the rest, -1 for none
  ShakeResult result; // of the first shake
};

inline Run Wave(float mps, double amplitude, double hz, const float axis[3],
                double seconds, double rate, double rest_s = 0.5) {
  ShakeState s;
  command_math::ShakeReset(&s);
  Run run = {0, -1.0, {0, 0.0f, 0.0f, 0.0f}};
  const int rest = static_cast<int>(rest_s * rate);
  const int count = static_cast<int>(seconds * rate);
  for (int i = 0; i < rest + count; ++i) {
    const double t = i / rate;
    const double u = i < rest ? 0.0 : amplitude * sin(kTwoPi * hz * (t - rest_s));
    const float p[3] = {0.20f + static_cast<float>(u * axis[0]),
                        1.10f + static_cast<float>(u * axis[1]),
                        0.35f + static_cast<float>(u * axis[2])};
    ShakeResult result;
    if (command_math::ShakeStep(&s, mps, p, t, &result)) {
      if (run.shakes == 0) {
        run.first = t - rest_s;
        run.result = result;
      }
      ++run.shakes;
    }
  }
  return run;
}

// A hand that goes through the points, each leg a smooth move of its own
// time, at 90 samples a second. Returns the count of the shakes.
inline int Path(float mps, const float (*points)[3], const double* leg_s,
                int legs) {
  ShakeState s;
  command_math::ShakeReset(&s);
  int shakes = 0;
  double t = 0.0;
  const double dt = 1.0 / 90.0;
  // A rest at the first point.
  for (int i = 0; i < 45; ++i, t += dt) {
    if (command_math::ShakeStep(&s, mps, points[0], t, nullptr)) ++shakes;
  }
  for (int leg = 0; leg < legs; ++leg) {
    const int n = static_cast<int>(leg_s[leg] / dt);
    for (int i = 1; i <= n; ++i, t += dt) {
      const double u = static_cast<double>(i) / n;
      const double e = 0.5 - 0.5 * cos(3.14159265358979 * u);  // ease in, out
      float p[3];
      for (int k = 0; k < 3; ++k) {
        p[k] = static_cast<float>(points[leg][k] +
                                  (points[leg + 1][k] - points[leg][k]) * e);
      }
      if (command_math::ShakeStep(&s, mps, p, t, nullptr)) ++shakes;
    }
  }
  return shakes;
}

inline void CommandChecks() {
  using namespace command_math;
  printf("\nThe Creature commands: the shake and the release\n");
  const float kMps = 0.6f;  // the default of leash_shake_mps
  const float x_axis[3] = {1.0f, 0.0f, 0.0f};
  const float y_axis[3] = {0.0f, 1.0f, 0.0f};
  const float slant[3] = {0.57735f, 0.57735f, 0.57735f};

  // A CLEAR SHAKE: 6 cm to each side, 4 times a second. Each stroke is 12 cm
  // in 125 ms, 0.96 m/s.
  {
    const Run run = Wave(kMps, 0.06, 4.0, x_axis, 1.0, 90.0);
    Check(run.shakes == 1, "a shake of 6 cm at 4 Hz for 1 s is one shake",
          run.shakes, 1);
    Check(run.first > 0.3 && run.first < 0.8,
          "it comes off in 0.3 s to 0.8 s", run.first, 0.6);
    Check(run.result.strokes == kShakeStrokes, "after 4 strokes",
          run.result.strokes, kShakeStrokes);
    Check(run.result.slowest > 0.7f && run.result.slowest < 1.2f,
          "the slowest stroke is near 0.96 m/s", run.result.slowest, 0.96);
    Near("the last stroke is 12 cm long", run.result.length, 0.12, 0.01);
    Check(run.result.seconds < kShakeWindowS, "the row is inside the window",
          run.result.seconds, kShakeWindowS);
  }
  // The axis does not matter: up and down, and a slanted line.
  {
    const Run up = Wave(kMps, 0.06, 4.0, y_axis, 1.0, 90.0);
    Check(up.shakes == 1, "a shake up and down is a shake", up.shakes, 1);
    const Run diagonal = Wave(kMps, 0.06, 4.0, slant, 1.0, 90.0);
    Check(diagonal.shakes == 1, "a shake on a slanted line is a shake",
          diagonal.shakes, 1);
  }
  // A larger and slower shake of the arm: 10 cm to each side, 3 times a
  // second. 20 cm in 167 ms, 1.2 m/s.
  {
    const Run run = Wave(kMps, 0.10, 3.0, x_axis, 1.2, 90.0);
    Check(run.shakes == 1, "a shake of 10 cm at 3 Hz is a shake", run.shakes,
          1);
    Check(run.first < 1.0, "in less than 1 s", run.first, 0.8);
  }
  // A low frame rate: 45 samples a second.
  {
    const Run run = Wave(kMps, 0.06, 4.0, x_axis, 1.0, 45.0);
    Check(run.shakes == 1, "the same shake at 45 samples a second",
          run.shakes, 1);
  }

  // NOT A SHAKE.
  {
    // A slow wave: 12 cm in 333 ms, 0.36 m/s.
    const Run slow = Wave(kMps, 0.06, 1.5, x_axis, 3.0, 90.0);
    Check(slow.shakes == 0, "a wave of 6 cm at 1.5 Hz is no shake",
          slow.shakes, 0);
    // The tremor of a hand in the air: 1 cm at 8 Hz.
    const Run tremor = Wave(kMps, 0.01, 8.0, x_axis, 3.0, 90.0);
    Check(tremor.shakes == 0, "a tremor of 1 cm at 8 Hz is no shake",
          tremor.shakes, 0);
    // Short and fast: 2 cm to each side at 6 Hz. The strokes are 4 cm.
    const Run small = Wave(kMps, 0.02, 6.0, x_axis, 3.0, 90.0);
    Check(small.shakes == 0, "strokes of 4 cm are no shake", small.shakes, 0);
    // A still hand.
    const Run still = Wave(kMps, 0.0, 4.0, x_axis, 3.0, 90.0);
    Check(still.shakes == 0, "a still hand is no shake", still.shakes, 0);
  }
  {
    // A reach and back: 40 cm out in 0.4 s and back in 0.4 s. Two strokes.
    const float reach[3][3] = {
        {0.2f, 1.1f, 0.3f}, {0.2f, 1.1f, 0.7f}, {0.2f, 1.1f, 0.3f}};
    const double reach_s[2] = {0.4, 0.4};
    const int n = Path(kMps, reach, reach_s, 2);
    Check(n == 0, "a reach of 40 cm and back is no shake", n, 0);
    // A throw: 30 cm back, 60 cm forward, 30 cm back to rest.
    const float swing[4][3] = {{0.2f, 1.1f, 0.3f},
                               {0.2f, 1.2f, 0.0f},
                               {0.2f, 1.0f, 0.6f},
                               {0.2f, 1.1f, 0.3f}};
    const double swing_s[3] = {0.3, 0.2, 0.4};
    const int m = Path(kMps, swing, swing_s, 3);
    Check(m == 0, "the swing of a throw is no shake", m, 0);
    // Two slaps: across and back, twice, with a rest of 0.7 s between them.
    const float slaps[5][3] = {{0.0f, 1.0f, 0.4f},
                               {0.3f, 1.0f, 0.4f},
                               {0.0f, 1.0f, 0.4f},
                               {0.0f, 1.0f, 0.4f},
                               {0.3f, 1.0f, 0.4f}};
    const double slaps_s[4] = {0.15, 0.25, 0.7, 0.15};
    const int k = Path(kMps, slaps, slaps_s, 4);
    Check(k == 0, "two slaps with a rest between them are no shake", k, 0);
    // A fast pointing here and there: four moves that do not go back along
    // the one before (a square of 20 cm).
    const float square[5][3] = {{0.0f, 1.0f, 0.4f},
                                {0.2f, 1.0f, 0.4f},
                                {0.2f, 1.2f, 0.4f},
                                {0.0f, 1.2f, 0.4f},
                                {0.0f, 1.0f, 0.4f}};
    const double square_s[4] = {0.15, 0.15, 0.15, 0.15};
    const int q = Path(kMps, square, square_s, 4);
    Check(q == 0, "four fast moves around a square are no shake", q, 0);
  }
  {
    // A circle of 8 cm radius, 3 turns a second. The hand is far from the
    // anchor and near it again twice a turn, but the path is not a line.
    ShakeState s;
    ShakeReset(&s);
    int shakes = 0;
    for (int i = 0; i < 270; ++i) {
      const double t = i / 90.0;
      const double a = kTwoPi * 3.0 * t;
      const float p[3] = {static_cast<float>(0.08 * cos(a)), 1.0f,
                          static_cast<float>(0.4 + 0.08 * sin(a))};
      if (ShakeStep(&s, kMps, p, t, nullptr)) ++shakes;
    }
    Check(shakes == 0, "a fast circle is no shake", shakes, 0);
  }

  // THE THRESHOLD is in real units. At 1.5 m/s the shake of 0.96 m/s is not
  // enough, and one of 8 cm at 5 Hz (16 cm in 100 ms, 1.6 m/s) is.
  {
    const Run weak = Wave(1.5f, 0.06, 4.0, x_axis, 2.0, 90.0);
    Check(weak.shakes == 0, "at 1.5 m/s, 0.96 m/s is no shake", weak.shakes,
          0);
    const Run strong = Wave(1.5f, 0.08, 5.0, x_axis, 1.0, 90.0);
    Check(strong.shakes == 1, "at 1.5 m/s, 1.6 m/s is a shake", strong.shakes,
          1);
  }

  // ONCE. A hand that shakes on for 1.5 s gives one shake: the next one
  // needs a second of rest of the detector.
  {
    const Run on = Wave(kMps, 0.06, 4.0, x_axis, 1.5, 90.0);
    Check(on.shakes == 1, "a shake of 1.5 s is one shake", on.shakes, 1);
    const Run longer = Wave(kMps, 0.06, 4.0, x_axis, 4.0, 90.0);
    Check(longer.shakes >= 3 && longer.shakes <= 4,
          "a shake of 4 s is 3 or 4 shakes, more than a second apart",
          longer.shakes, 4);
  }

  // A GAP in the tracking starts the detector again: two strokes, 0.3 s with
  // no samples, two strokes.
  {
    ShakeState s;
    ShakeReset(&s);
    int shakes = 0;
    double t = 0.0;
    for (int part = 0; part < 2; ++part) {
      for (int i = 0; i < 34; ++i, t += 1.0 / 90.0) {  // 1.5 periods at 4 Hz
        const double u = 0.06 * sin(kTwoPi * 4.0 * (i / 90.0));
        const float p[3] = {static_cast<float>(u), 1.0f, 0.4f};
        if (ShakeStep(&s, kMps, p, t, nullptr)) ++shakes;
      }
      t += 0.3;
    }
    Check(shakes == 0, "a row cut by a gap in the tracking is no shake",
          shakes, 0);
  }

  // THE RELEASE OF THE TRIGGER.
  Check(ReleaseCommand(kPressObject, 0.10, true, false) == kCommandObject,
        "a short press on an object: the Creature acts on it", 1, 1);
  Check(ReleaseCommand(kPressObject, 2.00, true, false) == kCommandObject,
        "a long press on an object: the same", 1, 1);
  Check(ReleaseCommand(kPressObject, 0.10, false, true) == kCommandNone,
        "the aim left the object before the release: nothing", 0, 0);
  Check(ReleaseCommand(kPressCreature, 0.20, true, false) == kCommandLeash,
        "a short press on the Creature: the leash click", 1, 1);
  Check(ReleaseCommand(kPressCreature, 0.50, true, false) == kCommandNone,
        "a press of 500 ms on the Creature: nothing", 0, 0);
  Check(ReleaseCommand(kPressCreature, 0.20, false, false) == kCommandNone,
        "the aim left the Creature: nothing", 0, 0);
  Check(ReleaseCommand(kPressLand, 0.60, false, true) == kCommandLand,
        "a held press on the land: the Creature goes there", 1, 1);
  Check(ReleaseCommand(kPressLand, 0.10, false, true) == kCommandLand,
        "a tap of 100 ms on the land: the Creature goes there too", 1, 1);
  Check(ReleaseCommand(kPressLand, 0.60, false, false) == kCommandNone,
        "a held press with no land under the arc: nothing", 0, 0);
  Check(ReleaseCommand(kPressNone, 0.60, true, true) == kCommandNone,
        "a press that the command did not take: nothing", 0, 0);

  // WHICH AIM A PRESS ON THE LAND USES. The limit between a tap and a hold is
  // creature_command_hold_ms, 250 ms.
  const double hold = 0.25;
  Check(ReleaseLandAim(0.10, hold, true, true) == kLandAimPress,
        "a tap of 100 ms: the aim of the press", 1, 1);
  Check(ReleaseLandAim(0.249, hold, true, true) == kLandAimPress,
        "a tap of 249 ms: the aim of the press", 1, 1);
  Check(ReleaseLandAim(0.25, hold, true, true) == kLandAimRelease,
        "a hold of 250 ms: the aim of the release", 1, 1);
  Check(ReleaseLandAim(3.00, hold, true, true) == kLandAimRelease,
        "a hold of 3 s: the aim of the release", 1, 1);
  Check(ReleaseLandAim(0.10, hold, true, false) == kLandAimPress,
        "a tap that ends on the sea: the aim of the press, which was land", 1,
        1);
  Check(ReleaseLandAim(0.10, hold, false, true) == kLandAimRelease,
        "a tap that started on the sea: the aim of the release", 1, 1);
  Check(ReleaseLandAim(0.10, hold, false, false) == kLandAimNone,
        "a tap with no land at the press and at the release: no aim", 0, 0);
  Check(ReleaseLandAim(1.00, hold, true, false) == kLandAimNone,
        "a hold that ends on the sea: no aim, the press does not count", 0, 0);
  Check(ReleaseLandAim(0.10, 0.0, true, true) == kLandAimRelease,
        "creature_command_hold_ms=0: each press uses the aim of the release",
        1, 1);
  // 16 short presses (82 to 242 ms). Each one sends the Creature.
  {
    const double taps[16] = {0.155, 0.142, 0.133, 0.133, 0.120, 0.201,
                             0.145, 0.242, 0.145, 0.145, 0.169, 0.110,
                             0.110, 0.147, 0.082, 0.122};
    int sent = 0;
    for (int i = 0; i < 16; ++i) {
      const LandAim aim = ReleaseLandAim(taps[i], hold, true, true);
      if (aim == kLandAimPress &&
          ReleaseCommand(kPressLand, taps[i], false, aim != kLandAimNone) ==
              kCommandLand) {
        ++sent;
      }
    }
    Check(sent == 16,
          "the 16 short presses of the headset test each send the Creature",
          sent, 16);
  }

  // THE MARK OF A COMMAND stays 0.6 s, and it does not show with no send.
  Check(!SentMarkShows(0.0, 5.0), "no send: no mark", 0, 0);
  Check(SentMarkShows(10.0, 10.0), "the mark shows at the send", 1, 1);
  Check(SentMarkShows(10.0, 10.59), "the mark shows after 590 ms", 1, 1);
  Check(!SentMarkShows(10.0, 10.61), "the mark is gone after 610 ms", 0, 0);
}

// THE LIVE TARGET OF A COMMAND, AND THE DOUBLE PRESS (docs/creature.md, "The live
// target of a command" and "The leash on an object").
inline void LiveChecks() {
  using namespace command_math;
  printf("\nThe Creature commands: the live target and the double press\n");
  const double k = 3.14159265358979 / 180.0;
  // The hand is 30 units over flat land at 10, and it aims level and ahead.
  const float origin[3] = {0.0f, 40.0f, 0.0f};
  const float ahead[3] = {0.0f, 0.0f, 1.0f};

  // THE AIM AT A BODY.
  {
    // A tree 200 units away: its base 30 units lower than the hand, 20 units
    // high, radius 2. The level ray passes 10 units over its top.
    const float base[3] = {0.0f, 10.0f, 200.0f};
    BodyAim point = AimAtBody(origin, ahead, base, 0.0f, 2.0f);
    const double to_base = atan(30.0 / 200.0) / k;
    const double covered = atan(2.0 / sqrt(200.0 * 200.0 + 30.0 * 30.0)) / k;
    Near("the aim at the point of an object: the angle less the radius",
         point.te, to_base - covered, 0.01);
    BodyAim body = AimAtBody(origin, ahead, base, 20.0f, 2.0f);
    const double to_top = atan(10.0 / 200.0) / k;
    const double top_covered =
        atan(2.0 / sqrt(200.0 * 200.0 + 10.0 * 10.0)) / k;
    Near("the aim at the body: the angle to its top", body.te,
         to_top - top_covered, 0.01);
    Check(body.te < 3.0f && point.te > 3.0f,
          "the top is in a cone of 3 degrees, and the base is not", body.te,
          3.0);
    // A ray at the middle of the body goes through it.
    const float down[3] = {0.0f, static_cast<float>(-sin(atan(20.0 / 200.0))),
                           static_cast<float>(cos(atan(20.0 / 200.0)))};
    body = AimAtBody(origin, down, base, 20.0f, 2.0f);
    Near("a ray at the middle of the body: te 0", body.te, 0.0, 1e-3);
    Near("and the body is 201 units along the ray", body.along, 201.0, 0.5);
    // A ray over the top by 1 degree.
    const double over = atan(10.0 / 200.0) - 1.0 * k;
    const float high[3] = {0.0f, static_cast<float>(-sin(over)),
                           static_cast<float>(cos(over))};
    body = AimAtBody(origin, high, base, 20.0f, 2.0f);
    Near("a ray 1 degree over the top: 1 degree less the radius", body.te,
         1.0 - top_covered, 0.02);
    // A body beside the ray: the height does not help.
    const float beside[3] = {30.0f, 30.0f, 200.0f};
    body = AimAtBody(origin, ahead, beside, 20.0f, 2.0f);
    Near("a body 30 units beside the ray", body.te,
         atan(30.0 / 200.0) / k - atan(2.0 / sqrt(200.0 * 200.0 + 900.0)) / k,
         0.01);
    // A body behind the hand is no target.
    const float behind[3] = {0.0f, 10.0f, -50.0f};
    body = AimAtBody(origin, ahead, behind, 20.0f, 2.0f);
    Check(!body.valid, "a body behind the hand is not valid", body.valid, 0);
    // The answer does not depend on which end is nearer: the ray under the
    // base.
    const float low[3] = {0.0f, static_cast<float>(-sin(12.0 * k)),
                          static_cast<float>(cos(12.0 * k))};
    body = AimAtBody(origin, low, base, 20.0f, 0.0f);
    Near("a ray under the base: the angle to the base", body.te,
         12.0 - atan(30.0 / 200.0) / k, 0.01);
  }

  // THE LAND POINT OF THE RAY.
  {
    const auto flat = [](float, float) { return 10.0f; };
    const float down[3] = {0.0f, -0.6f, 0.8f};
    Near("a ray 30 units over flat land, down at 0.6: the land is 50 along",
         RayLandDistance(origin, down, 3.0f, 400.0f, flat), 50.0, 0.02);
    Check(RayLandDistance(origin, ahead, 3.0f, 400.0f, flat) < 0.0f,
          "a level ray meets no land", 0, 0);
    Check(RayLandDistance(origin, down, 3.0f, 30.0f, flat) < 0.0f,
          "the land past the most distance is not found", 0, 0);
    const float under[3] = {0.0f, 5.0f, 0.0f};
    Near("a ray that starts under the land: 0",
         RayLandDistance(under, down, 3.0f, 400.0f, flat), 0.0, 1e-6);
    // A hill from z = 100, higher than the ray there: the ray meets its side.
    const auto hill = [](float, float z) { return z < 100.0f ? 10.0f : 39.5f; };
    const float slight[3] = {0.0f, -0.01f, 0.99995f};
    Near("a wall of land at 100: the land is 100 along",
         RayLandDistance(origin, slight, 3.0f, 400.0f, hill), 100.0, 0.05);
    // The sea is land at 0.
    const auto sea = [](float, float) { return 0.0f; };
    Near("over the sea the ray meets the sea plane",
         RayLandDistance(origin, down, 3.0f, 400.0f, sea), 40.0 / 0.6, 0.02);
  }

  // THE LAND IN FRONT WINS, at 100 units a meter, with 30 cm and 20 percent.
  {
    const AimRule rule = {3.0f, 5.0f, 4.0f, 0.30f};
    // The land point is 200 units along the ray. The allowance is 30 + 40.
    Check(!AimLandWins(rule, 150.0f, 200.0f, 100.0f, false),
          "an object in front of the land point is a target", 0, 0);
    Check(!AimLandWins(rule, 260.0f, 200.0f, 100.0f, false),
          "an object 60 units behind the land point is a target", 0, 0);
    Check(AimLandWins(rule, 280.0f, 200.0f, 100.0f, false),
          "an object 80 units behind the land point: the land wins", 1, 1);
    Check(!AimLandWins(rule, 280.0f, 200.0f, 100.0f, true),
          "the target of the last frame stays there (105 units)", 0, 0);
    Check(AimLandWins(rule, 310.0f, 200.0f, 100.0f, true),
          "and the land wins at 110 units behind", 1, 1);
    Check(!AimLandWins(rule, 900.0f, -1.0f, 100.0f, false),
          "a ray that meets no land: the rule does not act", 0, 0);
    const AimRule off = {3.0f, 5.0f, 4.0f, -1.0f};
    Check(!AimLandWins(off, 900.0f, 200.0f, 100.0f, false),
          "creature_command_behind_cm=-1: the rule is off", 0, 0);
    // THE RANGE.
    Check(AimInRange(rule, 3.9f, false) && AimInRange(rule, 4.0f, false),
          "3.9 m and 4.0 m from the head are in a range of 4 m", 1, 1);
    Check(!AimInRange(rule, 4.1f, false),
          "4.1 m is out for a new target", 0, 0);
    Check(AimInRange(rule, 4.1f, true) && !AimInRange(rule, 4.2f, true),
          "the target of the last frame stays to 4.15 m", 1, 1);
    const AimRule no_limit = {3.0f, 5.0f, 0.0f, 0.30f};
    Check(AimInRange(no_limit, 500.0f, false),
          "creature_command_max_m=0: no limit", 1, 1);
  }

  // THE TARGET WHILE THE TRIGGER IS HELD.
  {
    int tree = 0, house = 0;
    const Shown none = {kShownNone, nullptr};
    const Shown on_tree = {kShownObject, &tree};
    const Shown on_house = {kShownObject, &house};
    HeldState s;
    // A press on an object. The aim leaves it: the land shows at once.
    HeldStart(&s, on_tree);
    Shown shown = HeldStep(&s, on_tree, 0.00);
    Check(shown.kind == kShownObject && shown.object == &tree,
          "the press was on a tree: the tree shows", shown.kind, kShownObject);
    shown = HeldStep(&s, none, 0.011);
    Check(shown.kind == kShownLand,
          "the aim leaves the tree: the land shows in that frame", shown.kind,
          kShownLand);
    // The aim comes back: the tree shows after 100 ms, and not before.
    shown = HeldStep(&s, on_tree, 0.022);
    Check(shown.kind == kShownLand, "the aim is on the tree again: not yet",
          shown.kind, kShownLand);
    shown = HeldStep(&s, on_tree, 0.110);
    Check(shown.kind == kShownLand, "after 88 ms: not yet", shown.kind,
          kShownLand);
    shown = HeldStep(&s, on_tree, 0.123);
    Check(shown.kind == kShownObject && shown.object == &tree,
          "after 101 ms the tree shows", shown.kind, kShownObject);
    // From the tree to a house: at once.
    shown = HeldStep(&s, on_house, 0.134);
    Check(shown.kind == kShownObject && shown.object == &house,
          "from one object to a different one: at once", shown.kind,
          kShownObject);
    // A ring that passes objects: 60 ms on each is not a catch.
    HeldStart(&s, Shown{kShownLand, nullptr});
    int halos = 0;
    double t = 1.0;
    for (int pass = 0; pass < 5; ++pass) {
      for (int i = 0; i < 6; ++i, t += 0.011) {
        if (HeldStep(&s, pass % 2 ? on_house : on_tree, t).kind !=
            kShownLand) {
          ++halos;
        }
      }
      for (int i = 0; i < 6; ++i, t += 0.011) {
        if (HeldStep(&s, none, t).kind != kShownLand) ++halos;
      }
    }
    Check(halos == 0,
          "a ring that passes 5 objects, 66 ms on each: no halo shows", halos,
          0);
    // An aim that goes on and off an object each frame, at the border, shows
    // the land and no flicker.
    HeldStart(&s, Shown{kShownLand, nullptr});
    int changes = 0;
    int before = kShownLand;
    for (int i = 0; i < 200; ++i) {
      const int kind =
          HeldStep(&s, i % 2 ? on_tree : none, 2.0 + i * 0.011).kind;
      if (kind != before) ++changes;
      before = kind;
    }
    Check(changes == 0, "an aim at the border for 200 frames: no change",
          changes, 0);
    // The object of the wait must stay the same one.
    HeldStart(&s, Shown{kShownLand, nullptr});
    HeldStep(&s, on_tree, 5.00);
    HeldStep(&s, on_house, 5.06);
    shown = HeldStep(&s, on_house, 5.12);
    Check(shown.kind == kShownLand,
          "the wait starts again for a different object", shown.kind,
          kShownLand);
    shown = HeldStep(&s, on_house, 5.17);
    Check(shown.kind == kShownObject && shown.object == &house,
          "and that object shows 100 ms after it came", shown.kind,
          kShownObject);
  }

  // A PRESS ON THE LAND THAT STAYS A WALK (creature_command_rule=3).
  {
    int tree = 0, house = 0;
    const Shown none = {kShownNone, nullptr};
    const Shown on_tree = {kShownObject, &tree};
    const Shown on_house = {kShownObject, &house};
    const Shown on_creature = {kShownCreature, &house};
    HeldState s;
    HeldStart(&s, Shown{kShownLand, nullptr}, true);
    int halos = 0;
    double t = 1.0;
    for (int i = 0; i < 300; ++i, t += 0.011) {
      const Shown candidate = i % 30 < 10   ? on_tree
                              : i % 30 < 20 ? on_house
                              : i % 30 < 25 ? on_creature
                                            : none;
      if (HeldStep(&s, candidate, t).kind != kShownLand) ++halos;
    }
    Check(halos == 0,
          "a press on the land with land_stays: 300 frames on trees, houses "
          "and the Creature show no object", halos, 0);
    // The same aim without land_stays shows the tree after 100 ms.
    HeldStart(&s, Shown{kShownLand, nullptr}, false);
    HeldStep(&s, on_tree, 0.00);
    Check(HeldStep(&s, on_tree, 0.12).kind == kShownObject,
          "without land_stays the tree shows after 100 ms", 1, 1);
    // A press on an object ignores land_stays.
    HeldStart(&s, on_tree, true);
    Check(HeldStep(&s, on_tree, 0.00).kind == kShownObject &&
              HeldStep(&s, none, 0.011).kind == kShownLand,
          "a press on an object keeps the live target", 1, 1);
    Check(HeldStep(&s, on_tree, 0.022).kind == kShownLand &&
              HeldStep(&s, on_tree, 0.13).kind == kShownObject,
          "and the object can come back after 100 ms", 1, 1);
    // A hold on the land that stays a walk acts on the land of the release.
    const LiveRelease r =
        ReleaseLive(1.0, 0.25, kShownLand, kShownLand, true, true);
    Check(r.command == kCommandLand && !r.from_press,
          "the hold on the land sends the walk to the land of the release", 1,
          1);
  }

  // THE WALK MODE (creature_command_rule=4). A press on an object becomes a
  // walk when the aim leaves the object, and it stays a walk.
  {
    int tree = 0, house = 0, creature = 0;
    const Shown none = {kShownNone, nullptr};
    const Shown on_tree = {kShownObject, &tree};
    const Shown on_house = {kShownObject, &house};
    const Shown on_creature = {kShownCreature, &creature};
    HeldState s;
    // The aim stays on the object: the object shows for the whole press.
    HeldStart(&s, on_tree, true, true);
    int other = 0;
    double t = 0.0;
    for (int i = 0; i < 300; ++i, t += 0.011) {
      const Shown shown = HeldStep(&s, on_tree, t);
      if (shown.kind != kShownObject || shown.object != &tree) ++other;
    }
    Check(other == 0 && !s.land_stays,
          "rule 4: a press on a tree with the aim on it shows the tree for "
          "300 frames", other, 0);
    // The aim drops off the object for 55 ms and comes back: no walk.
    for (int i = 0; i < 5; ++i, t += 0.011) {
      if (HeldStep(&s, none, t).kind != kShownObject) ++other;
    }
    Shown shown = HeldStep(&s, on_tree, t);
    Check(other == 0 && shown.kind == kShownObject && !s.land_stays,
          "an aim that is off the tree for 55 ms does not start the walk",
          other, 0);
    // The aim leaves the object: the land shows after 100 ms.
    t += 0.011;
    const double left = t;
    shown = HeldStep(&s, none, t);
    Check(shown.kind == kShownObject, "the aim leaves the tree: not yet",
          shown.kind, kShownObject);
    shown = HeldStep(&s, none, left + 0.09);
    Check(shown.kind == kShownObject, "after 90 ms: not yet", shown.kind,
          kShownObject);
    shown = HeldStep(&s, none, left + 0.101);
    Check(shown.kind == kShownLand && s.land_stays && s.walk_from_object,
          "after 101 ms the press is a walk, and the land shows", shown.kind,
          kShownLand);
    // The aim comes back to the first object, and then goes over a second
    // object and the Creature: the land stays.
    int halos = 0;
    t = left + 0.2;
    for (int i = 0; i < 300; ++i, t += 0.011) {
      const Shown candidate = i < 100   ? on_tree
                              : i < 200 ? on_house
                              : i < 250 ? on_creature
                                        : none;
      if (HeldStep(&s, candidate, t).kind != kShownLand) ++halos;
    }
    Check(halos == 0,
          "the walk mode: 300 frames on the first tree, on a house and on "
          "the Creature show no object", halos, 0);
    // The aim goes from the object to a second object with no land between
    // them: the second object never shows.
    HeldStart(&s, on_tree, true, true);
    HeldStep(&s, on_tree, 10.0);
    int houses = 0, trees = 0;
    for (int i = 0; i < 9; ++i) {
      shown = HeldStep(&s, on_house, 10.011 + 0.011 * i);
      if (shown.object == &house) ++houses;
      if (shown.object == &tree) ++trees;
    }
    Check(houses == 0 && trees == 9,
          "the aim goes from the tree to a house: the tree shows for the "
          "first 99 ms, and the house does not", houses, 0);
    shown = HeldStep(&s, on_house, 10.13);
    Check(shown.kind == kShownLand && s.land_stays,
          "and then the land shows, with the aim on the house", shown.kind,
          kShownLand);
    // A press on the Creature has the same rule.
    HeldStart(&s, on_creature, true, true);
    HeldStep(&s, on_creature, 20.0);
    HeldStep(&s, none, 20.011);
    shown = HeldStep(&s, none, 20.12);
    Check(shown.kind == kShownLand && s.land_stays,
          "a press on the Creature becomes a walk when the aim leaves it",
          shown.kind, kShownLand);
    // A press on the land is a walk from its start, as with rule 3.
    HeldStart(&s, Shown{kShownLand, nullptr}, true, true);
    Check(s.land_stays && !s.leave_walks && !s.walk_from_object,
          "rule 4: a press on the land is a walk from its start", 1, 1);
    Check(HeldStep(&s, on_tree, 30.0).kind == kShownLand &&
              HeldStep(&s, on_tree, 30.5).kind == kShownLand,
          "and no object takes it", 1, 1);
    // Rule 3 has no walk mode: the object comes back.
    HeldStart(&s, on_tree, true, false);
    HeldStep(&s, on_tree, 40.0);
    HeldStep(&s, none, 40.2);
    HeldStep(&s, on_tree, 40.3);
    Check(HeldStep(&s, on_tree, 40.45).kind == kShownObject && !s.land_stays,
          "rule 3: the object comes back after the aim left it", 1, 1);
    // The release. A tap acts on the object of the press, also when the walk
    // has started. A hold acts on what shows.
    const double hold = 0.25;
    LiveRelease r =
        ReleaseLive(0.20, hold, kShownObject, kShownLand, false, true);
    Check(r.command == kCommandObject && r.from_press,
          "a tap of 200 ms acts on the object of the press, also in the walk "
          "mode", r.command, kCommandObject);
    r = ReleaseLive(0.80, hold, kShownObject, kShownObject, false, true);
    Check(r.command == kCommandObject && !r.from_press,
          "a hold that ends with the aim on the object acts on the object",
          r.command, kCommandObject);
    r = ReleaseLive(0.80, hold, kShownObject, kShownLand, false, true);
    Check(r.command == kCommandLand && !r.from_press,
          "a hold in the walk mode sends the Creature to the land of the "
          "release", r.command, kCommandLand);
    r = ReleaseLive(0.80, hold, kShownObject, kShownLand, false, false);
    Check(r.command == kCommandNone,
          "a hold in the walk mode that ends on the sea sends nothing",
          r.command, kCommandNone);
  }

  // WHAT THE RELEASE DOES WITH THE LIVE TARGET.
  {
    const double hold = 0.25;
    LiveRelease r = ReleaseLive(0.15, hold, kShownObject, kShownLand, false,
                                true);
    Check(r.command == kCommandObject && r.from_press,
          "a tap on an object acts on that object, also when the aim left",
          r.command, kCommandObject);
    r = ReleaseLive(0.15, hold, kShownLand, kShownObject, true, true);
    Check(r.command == kCommandLand && r.from_press,
          "a tap on the land sends the aim of the press", r.command,
          kCommandLand);
    r = ReleaseLive(2.0, hold, kShownObject, kShownLand, false, true);
    Check(r.command == kCommandLand && !r.from_press,
          "a hold that started on an object and ends on the land: the land",
          r.command, kCommandLand);
    r = ReleaseLive(2.0, hold, kShownLand, kShownObject, true, true);
    Check(r.command == kCommandObject && !r.from_press,
          "a hold that started on the land and ends on an object: the object",
          r.command, kCommandObject);
    r = ReleaseLive(2.0, hold, kShownObject, kShownLand, false, false);
    Check(r.command == kCommandNone,
          "a hold that ends on the sea: nothing", r.command, kCommandNone);
    r = ReleaseLive(0.15, hold, kShownLand, kShownLand, false, true);
    Check(r.command == kCommandLand && !r.from_press,
          "a tap whose first aim met no land: the aim of the release",
          r.command, kCommandLand);
    r = ReleaseLive(0.15, hold, kShownCreature, kShownCreature, false, false);
    Check(r.command == kCommandLeash, "a tap on the Creature: the leash click",
          r.command, kCommandLeash);
    r = ReleaseLive(0.40, hold, kShownCreature, kShownCreature, false, false);
    Check(r.command == kCommandLeash,
          "a press of 400 ms on the Creature: the leash click", r.command,
          kCommandLeash);
    r = ReleaseLive(0.50, hold, kShownCreature, kShownCreature, false, false);
    Check(r.command == kCommandNone,
          "a press of 500 ms on the Creature: nothing", r.command,
          kCommandNone);
    r = ReleaseLive(0.40, hold, kShownLand, kShownCreature, true, true);
    Check(r.command == kCommandNone,
          "a hold that ends on the Creature and did not start on it: nothing",
          r.command, kCommandNone);
    r = ReleaseLive(2.0, hold, kShownNone, kShownNone, true, true);
    Check(r.command == kCommandNone, "a press that was stopped: nothing",
          r.command, kCommandNone);
    // Three holds (733, 2023 and 2012 ms) where the aim left the target send
    // the Creature to the land.
    const double holds[3] = {0.733, 2.023, 2.012};
    int sent = 0;
    for (int i = 0; i < 3; ++i) {
      if (ReleaseLive(holds[i], hold, kShownObject, kShownLand, false, true)
              .command == kCommandLand) {
        ++sent;
      }
    }
    Check(sent == 3,
          "the 3 holds of the headset test that left their target send now",
          sent, 3);
  }

  // THE DOUBLE PRESS.
  {
    int house = 0, tree = 0;
    Check(DoublePress(10.30, 10.00, 0.5, &house, &house),
          "a second press on the same object 300 ms after the first", 1, 1);
    Check(DoublePress(10.50, 10.00, 0.5, &house, &house),
          "and 500 ms after it", 1, 1);
    Check(!DoublePress(10.51, 10.00, 0.5, &house, &house),
          "510 ms after it is no double press", 0, 0);
    Check(!DoublePress(10.30, 10.00, 0.5, &tree, &house),
          "a press on a different object is no double press", 0, 0);
    Check(!DoublePress(10.30, 10.00, 0.5, nullptr, nullptr),
          "two presses on the land are no double press", 0, 0);
    Check(!DoublePress(10.30, 10.00, 0.0, &house, &house),
          "creature_command_double_ms=0: no double press", 0, 0);
    Check(!DoublePress(10.30, -1e9, 0.5, &house, &house),
          "with no tap before it, a press is no double press", 0, 0);
  }
}

}  // namespace command_checks
