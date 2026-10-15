// The shading rate of foveated rendering, a test (foveation_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <stdlib.h>

#include "../../src/mod/foveation_math.h"

namespace foveation_checks {

inline void FoveationChecks() {
  using namespace foveation_math;
  printf("\nFoveated rendering: the shading rate of each part of an eye "
         "picture\n");

  // A picture of 100 by 80 degrees, 4000 by 3200 pixels.
  Params p;
  p.tan_h = tanf(50.0f / 57.2957795f);
  p.tan_v = tanf(40.0f / 57.2957795f);
  p.inner_deg = 20.0f;
  p.outer_deg = 35.0f;
  p.quarter = true;

  // The middle of the picture is straight ahead.
  float tx = 1.0f, ty = 1.0f;
  PixelTangent(2000.0f, 1600.0f, 4000, 3200, p, &tx, &ty);
  Near("the middle of the picture: x tangent 0", tx, 0.0, 1e-6);
  Near("the middle of the picture: y tangent 0", ty, 0.0, 1e-6);
  PixelTangent(4000.0f, 0.0f, 4000, 3200, p, &tx, &ty);
  Near("the top right corner: x is tan of 50 degrees", tx, p.tan_h, 1e-5);
  Near("the top right corner: y is tan of 40 degrees (y goes up)", ty,
       p.tan_v, 1e-5);
  float px = 0.0f, py = 0.0f;
  TangentPixel(0.3f, -0.2f, 4000, 3200, p, &px, &py);
  PixelTangent(px, py, 4000, 3200, p, &tx, &ty);
  Near("pixel and tangent go back and forth: x", tx, 0.3, 1e-5);
  Near("pixel and tangent go back and forth: y", ty, -0.2, 1e-5);

  // The angles.
  Near("the angle of a direction to itself is 0",
       AngleDeg(0.4f, 0.1f, 0.4f, 0.1f), 0.0, 0.05);
  Near("the angle from straight ahead to tan 45 degrees is 45",
       AngleDeg(0.0f, 0.0f, 1.0f, 0.0f), 45.0, 1e-3);
  Near("the angle from 30 degrees left to 30 degrees right is 60",
       AngleDeg(-tanf(30.0f / 57.2957795f), 0.0f, tanf(30.0f / 57.2957795f),
                0.0f),
       60.0, 1e-3);

  // The gaze.
  const float ahead[3] = {0.0f, 0.0f, -1.0f};
  const float behind[3] = {0.0f, 0.0f, 1.0f};
  const float right_up[3] = {0.5f, 0.25f, -1.0f};
  float gx = 9.0f, gy = 9.0f;
  Check(GazeTangent(ahead, &gx, &gy) && gx == 0.0f && gy == 0.0f,
        "the gaze straight ahead has the tangents 0, 0", gx, 0.0);
  Check(!GazeTangent(behind, &gx, &gy), "a gaze to the back is not used", 0,
        0);
  Check(GazeTangent(right_up, &gx, &gy), "a gaze to the right and up", 1, 1);
  Near("its x tangent", gx, 0.5, 1e-6);
  Near("its y tangent", gy, 0.25, 1e-6);

  // The rates.
  Check(RateForAngle(10.0f, p) == kRateFull, "10 degrees: the full rate",
        RateForAngle(10.0f, p), kRateFull);
  Check(RateForAngle(25.0f, p) == kRateHalf, "25 degrees: 2 by 2",
        RateForAngle(25.0f, p), kRateHalf);
  Check(RateForAngle(40.0f, p) == kRateQuarter, "40 degrees: 4 by 4",
        RateForAngle(40.0f, p), kRateQuarter);
  Params no_quarter = p;
  no_quarter.quarter = false;
  Check(RateForAngle(40.0f, no_quarter) == kRateHalf,
        "40 degrees with no 4 by 4 on the GPU: 2 by 2",
        RateForAngle(40.0f, no_quarter), kRateHalf);
  Params off = p;
  off.inner_deg = 0.0f;
  Check(RateForAngle(80.0f, off) == kRateFull,
        "with no foveation each angle has the full rate",
        RateForAngle(80.0f, off), kRateFull);

  // The levels of the key.
  Check(LevelOf(0).inner_deg == 0.0f, "foveation=0 is off",
        LevelOf(0).inner_deg, 0);
  Check(LevelOf(1).inner_deg > LevelOf(2).inner_deg &&
            LevelOf(2).inner_deg > LevelOf(3).inner_deg,
        "a stronger level has a smaller fovea", LevelOf(3).inner_deg, 15);
  Check(LevelOf(2).outer_deg > LevelOf(2).inner_deg,
        "the ring of 2 by 2 is outside the fovea", LevelOf(2).outer_deg, 35);

  // The map with the fovea straight ahead: 16 by 16 texels.
  const uint32_t mw = TexelCount(4000, 16), mh = TexelCount(3200, 16);
  Check(mw == 250 && mh == 200, "4000 by 3200 pixels are 250 by 200 texels",
        mw, 250);
  uint8_t* map = static_cast<uint8_t*>(malloc(mw * mh));
  BuildMap(map, mw, mh, 16, 16, 4000, 3200, 0.0f, 0.0f, p);
  Check(map[(mh / 2) * mw + mw / 2] == kRateFull,
        "straight ahead: the middle texel has the full rate",
        map[(mh / 2) * mw + mw / 2], kRateFull);
  Check(map[0] == kRateQuarter, "straight ahead: a corner has 4 by 4", map[0],
        kRateQuarter);
  // Each texel within inner_deg has the full rate, and each texel with the
  // full rate has a point within inner_deg.
  int wrong = 0;
  for (uint32_t y = 0; y < mh; ++y) {
    for (uint32_t x = 0; x < mw; ++x) {
      float cx = 0.0f, cy = 0.0f;
      PixelTangent(x * 16.0f + 8.0f, y * 16.0f + 8.0f, 4000, 3200, p, &cx,
                   &cy);
      const float deg = AngleDeg(cx, cy, 0.0f, 0.0f);
      if (deg < p.inner_deg - 0.5f && map[y * mw + x] != kRateFull) ++wrong;
      if (deg > p.inner_deg + 1.0f && map[y * mw + x] == kRateFull) ++wrong;
    }
  }
  Check(wrong == 0, "straight ahead: the full rate is the fovea of 20 degrees",
        wrong, 0);
  // The map follows the gaze: 25 degrees to the right.
  const float g = tanf(25.0f / 57.2957795f);
  BuildMap(map, mw, mh, 16, 16, 4000, 3200, g, 0.0f, p);
  TangentPixel(g, 0.0f, 4000, 3200, p, &px, &py);
  const uint32_t gtx = static_cast<uint32_t>(px) / 16;
  const uint32_t gty = static_cast<uint32_t>(py) / 16;
  Check(map[gty * mw + gtx] == kRateFull,
        "the gaze 25 degrees to the right: its texel has the full rate",
        map[gty * mw + gtx], kRateFull);
  Check(map[(mh / 2) * mw + 10] != kRateFull,
        "the gaze 25 degrees to the right: the left part is coarse",
        map[(mh / 2) * mw + 10], kRateHalf);
  free(map);
}

}  // namespace foveation_checks
