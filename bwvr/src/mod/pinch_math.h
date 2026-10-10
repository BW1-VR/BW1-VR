// A pinch for a small thing: the pure math. See docs/hand-model.md, "The pinch".
// hand_feel.cpp uses it, and test/controller_math checks it (pinch_checks.h).
// Nothing here reads a global or touches the game.
//
// THE RULE. The Hand holds a small object between the tips of the thumb and of
// the index finger, and not in the fist. The size of the object is its
// diameter (the larger of the diameter of its body and its height) against the
// length of the hand, both in world units, so the same object is a pinch at a
// far zoom and a fist at a near zoom. The limit has a hysteresis: the pinch
// starts below kEnterRatio and ends above kLeaveRatio, so a zoom step at the
// limit does not flip the pose back and forth.
//
// THE POSE. The thumb has one curl and one spread, and the index finger has
// one curl. For these three numbers the tips of the two fingers are two points
// in the hand frame (the hand model gives them). The pose is the triple at
// which the tips are the wanted gap apart (the object lies between them), the
// index finger is near a half curl, and the point between the tips is in front
// of the palm. The other fingers curl a little (kOtherCurl).

#pragma once

#include <math.h>

#include "overlay_math.h"

namespace pinch_math {

// The size of the object over the length of the hand, as a diameter.
const float kEnterRatio = 0.16f;
const float kLeaveRatio = 0.20f;

// True when the object is small against the hand. `was` is the answer of the
// frame before (the hysteresis).
inline bool Wanted(float diameter, float hand_length, bool was) {
  if (!(hand_length > 0.0f) || !(diameter >= 0.0f)) return false;
  const float ratio = diameter / hand_length;
  return ratio < (was ? kLeaveRatio : kEnterRatio);
}

// The pose goes in and out in this time. The object moves with it in the
// frame of the hand, so a change of the pose at a zoom step glides.
const float kBlendSeconds = 0.15f;

inline float Step(float weight, bool want, float dt) {
  if (!(dt > 0.0f)) return weight;
  const float step = dt / kBlendSeconds;
  weight += want ? step : -step;
  if (weight < 0.0f) weight = 0.0f;
  if (weight > 1.0f) weight = 1.0f;
  return weight;
}

// The weight with a smooth start and end.
inline float Ease(float weight) {
  return weight * weight * (3.0f - 2.0f * weight);
}

inline float Mix(float a, float b, float t) { return a + t * (b - a); }

// The curl of the middle, ring and little fingers in a pinch.
const float kOtherCurl = 0.35f;

// --- the search ---------------------------------------------------------------

const int kThumbCurls = 11;
const int kThumbSpreads = 25;
const int kIndexCurls = 9;
const float kSpreadMin = -0.6f;  // radians
const float kSpreadMax = 1.0f;
const float kIndexMin = 0.25f;
const float kIndexMax = 0.75f;
// The curl of the index finger that reads as a pinch.
const float kIndexWish = 0.45f;
// The point between the tips must be in front of the palm: the palm faces -x
// of the hand frame and its surface is at x = 0.065 (hand lengths).
const float kFrontX = 0.0f;
// The tips meet when they can come this close (hand lengths).
const float kMeetMax = 0.10f;
// The gap between the tips never asks for more than this.
const float kGapMax = 0.10f;

inline float ThumbCurlAt(int i) {
  return static_cast<float>(i) / (kThumbCurls - 1);
}
inline float ThumbSpreadAt(int i) {
  return Mix(kSpreadMin, kSpreadMax,
             static_cast<float>(i) / (kThumbSpreads - 1));
}
inline float IndexCurlAt(int i) {
  return Mix(kIndexMin, kIndexMax, static_cast<float>(i) / (kIndexCurls - 1));
}

// The tips in the hand frame, for each curl and spread of the thumb and each
// curl of the index finger.
struct Table {
  float thumb[kThumbCurls][kThumbSpreads][3];
  float index[kIndexCurls][3];
};

// thumb_tip(curl, spread, out[3]) and index_tip(curl, out[3]).
template <typename ThumbTip, typename IndexTip>
inline void Fill(ThumbTip thumb_tip, IndexTip index_tip, Table* out) {
  for (int c = 0; c < kThumbCurls; ++c) {
    for (int s = 0; s < kThumbSpreads; ++s) {
      thumb_tip(ThumbCurlAt(c), ThumbSpreadAt(s), out->thumb[c][s]);
    }
  }
  for (int i = 0; i < kIndexCurls; ++i) index_tip(IndexCurlAt(i), out->index[i]);
}

struct Pose {
  float thumb_curl, thumb_spread, index_curl;
  float point[3];  // between the tips, in the hand frame
  float gap;       // between the tips
  bool ok;         // the tips can meet
};

inline float Gap(const float a[3], const float b[3]) {
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

// The pose of the three numbers with the least cost. gap_wish is the gap that
// the tips should have (the diameter of the object, in hand lengths).
inline void Solve(const Table& table, float gap_wish, Pose* out) {
  if (!(gap_wish > 0.0f)) gap_wish = 0.0f;
  if (gap_wish > kGapMax) gap_wish = kGapMax;
  float best = 1e30f;
  *out = Pose();
  for (int i = 0; i < kIndexCurls; ++i) {
    for (int c = 0; c < kThumbCurls; ++c) {
      for (int s = 0; s < kThumbSpreads; ++s) {
        const float* a = table.thumb[c][s];
        const float* b = table.index[i];
        const float gap = Gap(a, b);
        const float mid_x = 0.5f * (a[0] + b[0]);
        float cost = fabsf(gap - gap_wish);
        cost += 0.12f * fabsf(IndexCurlAt(i) - kIndexWish);
        cost += 0.50f * fmaxf(0.0f, mid_x - kFrontX);
        cost += 0.01f * fabsf(ThumbSpreadAt(s));
        if (cost < best) {
          best = cost;
          out->thumb_curl = ThumbCurlAt(c);
          out->thumb_spread = ThumbSpreadAt(s);
          out->index_curl = IndexCurlAt(i);
          out->gap = gap;
          for (int k = 0; k < 3; ++k) out->point[k] = 0.5f * (a[k] + b[k]);
        }
      }
    }
  }
  out->ok = best < 1e29f && out->gap <= gap_wish + kMeetMax &&
            out->point[0] <= kFrontX + 0.05f;
}

// The curls of a hold range for the three last fingers: from the curl of the
// fist hold toward the pinch.
inline float OtherHold(float hold, float weight) {
  return Mix(hold, kOtherCurl, weight);
}

}  // namespace pinch_math
