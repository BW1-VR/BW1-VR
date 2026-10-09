// THE SIZE OF THE TELEPORT RING (docs/god-view.md, "The teleport").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/teleport_math.h"

namespace teleport_checks {

inline void NearAt(double got, double want, double tolerance,
                   const char* what) {
  Near(what, got, want, tolerance);
}

inline void TeleportChecks() {
  using namespace teleport_math;
  printf("\nThe size of the teleport ring\n");
  const float mn = kRingMinCmDefault, mx = kRingMaxCmDefault;
  const float units = kRingUnitsDefault;

  // The fixed mode keeps the cm of the room at each scale.
  const float scales[6] = {7.5f, 66.7f, 100.0f, 150.0f, 1139.0f, 4000.0f};
  bool fixed = true;
  for (int i = 0; i < 6; ++i) {
    if (RingRadiusCm(kRingFixed, 11.0f, units, mn, mx, scales[i]) != 11.0f) {
      fixed = false;
    }
  }
  Check(fixed, "mode 0 gives 11 cm at each scale", fixed, 1);

  // The start scale: the same 11 cm as the fixed ring.
  NearAt(RingRadiusCm(kRingWorld, 11.0f, units, mn, mx, 100.0f), 11.0f, 0.001f,
       "mode 1 at 100 units a meter is 11 cm");
  // A zoom step out (150) makes it smaller, a zoom step in (66.7) larger.
  NearAt(RingRadiusCm(kRingWorld, 11.0f, units, mn, mx, 150.0f), 7.333f, 0.01f,
       "mode 1 at 150 units a meter is 7.3 cm");
  NearAt(RingRadiusCm(kRingWorld, 11.0f, units, mn, mx, 66.667f), 16.5f, 0.01f,
       "mode 1 at 66.7 units a meter is 16.5 cm");

  // The limits: life size and the farthest zoom.
  NearAt(RingRadiusCm(kRingWorld, 11.0f, units, mn, mx, 7.5f), mx, 1e-4f,
       "mode 1 at life size stops at the largest size");
  NearAt(RingRadiusCm(kRingWorld, 11.0f, units, mn, mx, 4000.0f), mn, 1e-4f,
       "mode 1 at 4000 units a meter stops at the smallest size");

  // Between the limits the size in the world stays at 11 units. Outside, it
  // is the limit.
  bool world = true, within = true, shrinks = true;
  float last = 1e9f;
  float s = 7.5f;
  for (int i = 0; i < 40 && s <= 4000.0f; ++i, s *= 1.5f) {
    const float cm = RingRadiusCm(kRingWorld, 11.0f, units, mn, mx, s);
    const float w = RingRadiusUnits(cm, s);
    if (cm < mn - 1e-4f || cm > mx + 1e-4f) within = false;
    if (cm > last + 1e-4f) shrinks = false;
    last = cm;
    const bool clamped = cm <= mn + 1e-4f || cm >= mx - 1e-4f;
    if (!clamped && fabsf(w - units) > 0.01f) world = false;
  }
  Check(within, "the size is always from 4 to 40 cm", within, 1);
  Check(shrinks, "a zoom step out never makes the ring larger", shrinks, 1);
  Check(world, "between the limits the ring is 11 units of the world", world,
        1);

  NearAt(RingRadiusUnits(11.0f, 100.0f), 11.0f, 1e-4f,
       "11 cm at 100 units a meter is 11 units");
  NearAt(RingRadiusUnits(11.0f, 4000.0f), 440.0f, 1e-3f,
       "11 cm at 4000 units a meter is 440 units");
  // A bad scale gives the fixed size.
  NearAt(RingRadiusCm(kRingWorld, 11.0f, units, mn, mx, 0.0f), 11.0f, 1e-4f,
       "no scale gives the fixed size");
}

}  // namespace teleport_checks
