// THE SIZE OF THE TELEPORT RING (docs/god-view.md, "The teleport"). Pure
// math: no Windows, no game. The checks are in test/controller_math/
// teleport_checks.h.
//
// The God view has a scale: world units for each meter of the room. A ring
// of one size in the room covers 0.8 units of the world at 7.5 units a
// meter and 440 units at 4000. So by default the ring has one size in the
// world. It gets larger in the room when the player zooms in and smaller when
// the player zooms out, between a smallest and a largest size in the room.

#pragma once

namespace teleport_math {

// teleport_ring_mode: 0 is a ring of one size in the room (teleport_ring_cm),
// 1 is a ring of one size in the world (teleport_ring_units).
const int kRingFixed = 0;
const int kRingWorld = 1;

// The size in the world: 11 units. At the start scale of 100 units a meter it
// is 11 cm of the room, the size of the fixed ring.
const float kRingUnitsDefault = 11.0f;
// The smallest and the largest outer radius in the room, in cm. At 4 cm the
// ring still has an angle of 2.3 degrees at 1 m, and the world size rule
// reaches it at 275 units a meter. At 40 cm the ring does not fill the view
// of a God view that lies at the height of the waist, and the rule reaches it
// at 27.5 units a meter.
const float kRingMinCmDefault = 4.0f;
const float kRingMaxCmDefault = 40.0f;

// The outer radius of the ring in cm of the room.
inline float RingRadiusCm(int mode, float fixed_cm, float world_units,
                          float min_cm, float max_cm,
                          float units_per_meter) {
  if (mode != kRingWorld) return fixed_cm;
  if (!(units_per_meter > 0.0f)) return fixed_cm;
  const float cm = world_units / units_per_meter * 100.0f;
  if (cm < min_cm) return min_cm;
  if (cm > max_cm) return max_cm;
  return cm;
}

// The same radius in units of the world.
inline float RingRadiusUnits(float radius_cm, float units_per_meter) {
  return radius_cm * 0.01f * units_per_meter;
}

}  // namespace teleport_math
