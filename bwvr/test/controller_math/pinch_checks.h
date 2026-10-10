// A PINCH FOR A SMALL THING (docs/hand-model.md, "The pinch"): the size rule
// with its hysteresis, the blend, the search for the pose at which the tips of
// the thumb and of the index finger meet, and the place of the held object
// between the tips. main.cpp includes this file after Check and Near, just
// before main.
//
// The test has no game files. The thumb and the index finger are made up from
// round numbers, and the low poly hand of overlay_math.h is the second case.
#pragma once

#include "../../src/mod/pinch_math.h"

namespace pinch_checks {

namespace pm = pinch_math;
namespace om = overlay_math;
namespace hx = hand_exact_math;

// A made up hand: the index tip moves in x and y with its curl, and the thumb
// tip moves with its curl and spread. At the curl 0.5 and the spread 0.3 of
// the thumb, and the curl 0.45 of the index finger, the two tips are one
// point.
inline void MadeIndex(float curl, float out[3]) {
  out[0] = 0.10f - 0.30f * curl;
  out[1] = -0.40f + 0.20f * curl;
  out[2] = 0.15f;
}
inline void MadeThumb(float curl, float spread, float out[3]) {
  out[0] = 0.065f - 0.20f * curl;
  out[1] = -0.26f - 0.10f * curl;
  out[2] = 0.30f - 0.50f * spread;
}

inline double Gap3(const float a[3], const float b[3]) {
  const double d0 = static_cast<double>(a[0]) - b[0];
  const double d1 = static_cast<double>(a[1]) - b[1];
  const double d2 = static_cast<double>(a[2]) - b[2];
  return sqrt(d0 * d0 + d1 * d1 + d2 * d2);
}

inline void PinchChecks() {
  printf("-- a pinch for a small thing\n");

  // The size rule.
  Check(pm::Wanted(0.10f * 19.0f, 19.0f, false),
        "an object of 0.10 of the hand starts a pinch", 0.10, 0.16);
  Check(!pm::Wanted(0.18f * 19.0f, 19.0f, false),
        "an object of 0.18 of the hand does not start a pinch", 0.18, 0.16);
  Check(pm::Wanted(0.18f * 19.0f, 19.0f, true),
        "an object of 0.18 of the hand keeps a pinch (the hysteresis)", 0.18,
        0.20);
  Check(!pm::Wanted(0.22f * 19.0f, 19.0f, true),
        "an object of 0.22 of the hand ends a pinch", 0.22, 0.20);
  Check(!pm::Wanted(1.0f, 0.0f, false) && !pm::Wanted(-1.0f, 19.0f, false),
        "a hand with no length and an object with no size give no pinch", 0, 0);
  // The same object: a near zoom has a fist, a far zoom a pinch. The length
  // of the hand is 19 units at 100 units a meter and 190 at 1000.
  Check(!pm::Wanted(5.0f, 19.0f, false) && pm::Wanted(5.0f, 190.0f, false),
        "an object of 5 units is a fist at 100 units a meter and a pinch at "
        "1000", 5.0 / 19.0, 5.0 / 190.0);
  // A zoom that walks over the limit and back flips once each way.
  {
    bool pinch = false;
    int flips = 0;
    const float lengths[10] = {19.0f, 24.0f, 28.0f, 34.0f, 33.0f,
                               31.0f, 28.0f, 24.0f, 19.0f, 15.0f};
    for (int i = 0; i < 10; ++i) {
      const bool now = pm::Wanted(5.0f, lengths[i], pinch);
      if (now != pinch) ++flips;
      pinch = now;
    }
    Check(flips == 2, "a zoom over the limit and back flips the pose twice",
          flips, 2);
  }

  // The blend: 0.15 s from the fist to the pinch, and back.
  {
    float w = 0.0f;
    for (int i = 0; i < 5; ++i) w = pm::Step(w, true, 0.03f);
    Near("the weight is 1 after the blend time", w, 1.0, 1e-5);
    w = pm::Step(w, false, 0.075f);
    Near("half the time takes half the weight", w, 0.5, 1e-5);
    Near("the ease at 0.5 is 0.5", pm::Ease(0.5f), 0.5, 1e-6);
    Check(pm::Ease(0.0f) == 0.0f && pm::Ease(1.0f) == 1.0f,
          "the ease keeps the ends", pm::Ease(1.0f), 1.0);
    Check(pm::Step(0.4f, true, 0.0f) == 0.4f, "no time gives no step", 0, 0);
  }

  // The search, for the made up hand.
  {
    pm::Table table;
    pm::Fill(MadeThumb, MadeIndex, &table);
    pm::Pose pose;
    pm::Solve(table, 0.0f, &pose);
    Check(pose.ok, "the tips of the made up hand meet", pose.gap, 0.0);
    Check(pose.gap < 0.03f, "the least gap is small", pose.gap, 0.0);
    Check(pose.point[0] <= pm::kFrontX + 0.05f,
          "the point between the tips is in front of the palm", pose.point[0],
          pm::kFrontX);
    Check(pose.index_curl >= pm::kIndexMin && pose.index_curl <= pm::kIndexMax,
          "the index curl is in its range", pose.index_curl, pm::kIndexWish);
    Near("the made up pose is found: the thumb curl", pose.thumb_curl, 0.5,
         0.11);
    // A wanted gap: the tips are that far apart.
    pm::Solve(table, 0.06f, &pose);
    Near("the tips keep the gap that the object needs", pose.gap, 0.06, 0.03);
    // A gap that is too big counts as the largest one.
    pm::Solve(table, 5.0f, &pose);
    Check(pose.gap <= pm::kGapMax + pm::kMeetMax,
          "a very large gap is cut to the largest", pose.gap, pm::kGapMax);
    // A thumb that cannot reach: the tips do not meet.
    pm::Table far_table = table;
    for (int c = 0; c < pm::kThumbCurls; ++c) {
      for (int s = 0; s < pm::kThumbSpreads; ++s) {
        far_table.thumb[c][s][2] += 1.0f;
      }
    }
    pm::Solve(far_table, 0.0f, &pose);
    Check(!pose.ok, "a thumb that cannot reach gives no pinch", pose.gap,
          pm::kMeetMax);
    // The point between the tips is behind the palm: no pinch.
    pm::Table behind = table;
    for (int c = 0; c < pm::kThumbCurls; ++c) {
      for (int s = 0; s < pm::kThumbSpreads; ++s) {
        behind.thumb[c][s][0] += 0.6f;
      }
    }
    for (int i = 0; i < pm::kIndexCurls; ++i) behind.index[i][0] += 0.6f;
    pm::Solve(behind, 0.0f, &pose);
    Check(!pose.ok, "tips behind the palm give no pinch", pose.point[0],
          pm::kFrontX);
  }

  // The other fingers curl a little.
  Near("the other fingers go to the pinch curl", pm::OtherHold(0.60f, 1.0f),
       pm::kOtherCurl, 1e-6);
  Near("with no weight they keep the fist", pm::OtherHold(0.60f, 0.0f), 0.60,
       1e-6);

  // The search for the low poly hand runs and gives a number.
  {
    pm::Table table;
    pm::Fill(
        [](float curl, float spread, float out[3]) {
          (void)spread;
          om::FingerTip(0, curl, out);
        },
        [](float curl, float out[3]) { om::FingerTip(1, curl, out); }, &table);
    pm::Pose pose;
    pm::Solve(table, 0.04f, &pose);
    Check(pose.gap == pose.gap && pose.gap >= 0.0f,
          "the search for the low poly hand gives a gap", pose.gap, 0.0);
  }

  // The place of the held object between the tips.
  {
    const float rotation[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f};
    const float same[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                           0.0f, 0.0f, 1.0f};
    const float grip[3] = {10.0f, 20.0f, 30.0f};
    const float hang[3] = {0.0f, -0.66f, 0.0f};
    const float length = 19.0f;
    float fist[3], none[3], pinch[3], at[3];
    hx::HoldOrigin(grip, rotation, length, false, 0.33f, hang, same, fist);
    const float point[3] = {-0.20f, -0.30f, 0.15f};
    hx::HoldOrigin(grip, rotation, length, false, 0.33f, hang, same, none,
                   0.0f, point);
    Check(Gap3(fist, none) < 1e-6, "a pinch of weight 0 is the fist",
          Gap3(fist, none), 0.0);
    hx::HoldOrigin(grip, rotation, length, false, 0.33f, hang, same, pinch,
                   1.0f, point);
    hx::HoldOriginAt(grip, rotation, length, false, point, hang, same, at);
    Check(Gap3(pinch, at) < 1e-5,
          "a pinch of weight 1 puts the ball at the point between the tips",
          Gap3(pinch, at), 0.0);
    // The middle of the ball is the point of the hand frame, in the world.
    float middle[3];
    om::PlacePoint(grip, rotation, length, false, point, middle);
    const float expect[3] = {middle[0], middle[1] - 0.33f, middle[2]};
    Check(Gap3(pinch, expect) < 1e-4,
          "the object hangs half the hang offset under the point",
          Gap3(pinch, expect), 0.0);
    // Half the weight is half the way.
    float mid[3];
    hx::HoldOrigin(grip, rotation, length, false, 0.33f, hang, same, mid, 0.5f,
                   point);
    Near("half the weight is half the way", Gap3(fist, mid),
         0.5 * Gap3(fist, pinch), 1e-4);
  }
}

}  // namespace pinch_checks
