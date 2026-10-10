// THE SIZE OF AN ADVISOR AT A PLACE OF THE WORLD (docs/tabletop.md, "The
// spirits beside the table"). Pure math: no Windows, no game. The checks are
// in test/controller_math/spirit_size_checks.h.
//
// The game moves a spirit from its usual place to a place of the world by an
// amount t from 0 to 1, with the ease e = (1 - cos(pi t)) / 2. The spirit is
// at usual + (place - usual) * e, and the scale of its mesh is (1 + 3 e)
// times the usual scale (HelpDude::Update1, 0x005BE943). So at the place the
// spirit is 4 times as large as at its usual place. In the game a place of
// the world is far from the camera, and the spirit must not be too small
// there.
//
// On the table and in the God view the mod multiplies the size of a spirit
// by s / 7.5, so that it has one size in the room: 18 cm, 1 to 1.5 m away.
// The game's factor 4 then makes a spirit of 73 cm in the room at each
// scale. A place of the world is on the land at the waist, about 1 m from
// the head, so the spirit fills the view.
//
// THE RULE. At a place of the world the spirit has the angle of size that it
// has at its near depth, seen from the camera that places the spirit (the
// room camera of spirit_anchor.cpp). The factor of its scale is the distance
// of the place divided by the near depth. It is at most the 4 of the game,
// and at least 1/4.

#pragma once

#include <math.h>

namespace spirit_size_math {

// The number of the game in (1 + 3 e).
const float kGameGrow = 3.0f;
// The factor of the scale at the place: the game's own, and the limits of
// the rule.
const float kGameFactor = 1.0f + kGameGrow;
const float kMostFactor = kGameFactor;
const float kLeastFactor = 0.25f;

const float kPi = 3.14159265f;
const float kDegrees = 57.29578f;

inline bool Finite(float v) { return v == v && v < 1e30f && v > -1e30f; }

// The ease of the game (fn_005BD250).
inline float Ease(float amount) {
  if (!(amount > 0.0f)) return 0.0f;
  if (amount > 1.0f) return 1.0f;
  return (1.0f - cosf(amount * kPi)) * 0.5f;
}

// The scale of the mesh as a share of the usual scale, as the game makes it.
inline float Share(float grow, float eased) { return 1.0f + grow * eased; }

// The factor of the scale at a place of the world. place_distance is the
// distance from the camera to the place, and near_depth is the near depth of
// the spirit, both in units of the world. A number that is not usable gives
// the factor of the game.
inline float WorldFactor(float place_distance, float near_depth) {
  if (!Finite(place_distance) || !Finite(near_depth)) return kGameFactor;
  if (!(near_depth > 0.0f) || place_distance < 0.0f) return kGameFactor;
  const float factor = place_distance / near_depth;
  if (factor < kLeastFactor) return kLeastFactor;
  if (factor > kMostFactor) return kMostFactor;
  return factor;
}

// The number that takes the place of the game's 3, for a factor.
inline float Grow(float factor) { return factor - 1.0f; }

// Where the spirit stays at a place of the world: aside units to the left of
// the place, seen from the camera, and above units over it. right is the
// right axis of the camera. The game adds a slow wave of 1 unit to the
// height, and it keeps the spirit 3 units over the land.
inline void WorldPlace(const float place[3], const float right[3],
                       float aside, float above, float out[3]) {
  out[0] = place[0] - right[0] * aside;
  out[1] = place[1] - right[1] * aside + above;
  out[2] = place[2] - right[2] * aside;
}

inline float Distance(const float a[3], const float b[3]) {
  const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
  return sqrtf(x * x + y * y + z * z);
}

// The angle of a thing of this size at this distance, in degrees.
inline float AngleDeg(float size, float distance) {
  if (!(distance > 0.0f) || !(size > 0.0f)) return 0.0f;
  return 2.0f * atan2f(size * 0.5f, distance) * kDegrees;
}

// What a spirit does, from its amount and from the amount that it goes to.
enum Phase { kUsual = 0, kLeaves, kAtPlace, kReturns, kPhases };

inline Phase PhaseOf(float amount, float goal) {
  if (goal > amount) return kLeaves;
  if (goal < amount) return kReturns;
  return amount > 0.0f ? kAtPlace : kUsual;
}

}  // namespace spirit_size_math
