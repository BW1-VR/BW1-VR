// A fight of Creatures: the pure rules. See docs/creature.md, "A fight of
// Creatures". hand_direct.cpp (fight_grip) and fight_marks.cpp
// (fight_mark_depth) use them, and test/controller_math checks them
// (fight_checks.h). It has no game and no headset in it.

#pragma once

#include <math.h>

namespace fight_math {

const float kTiny = 1e-4f;

// THE RAY OF A PRESS ON A CREATURE (fight_grip=1). In a fight the game casts
// a ray from its camera through the pointer against the mesh of the Creature
// (fn_005D3EF0), and the place that the ray hits is the place of the attack
// or the block. The mod casts the ray from the Hand's point to the axis of the
// body. So the press hits the place of the body that is nearest to the Hand.
//
// The body is a vertical capsule: an axis that goes up height units from base,
// with radius. The ray goes to the point of the axis that is nearest to the
// Hand. It starts at the Hand's point, or at two radii from the axis when the
// Hand is nearer than that, so that it starts outside the body. When the
// Hand's point is on the axis, the ray goes across from the side of the
// camera. direction gets a vector of length 1, as the game gives. False when
// the body has no size.
inline bool BodyRay(const float base[3], float height, float radius,
                    const float hand[3], const float camera[3],
                    float origin[3], float direction[3]) {
  if (!(height > 0.0f) || !(radius >= 0.0f)) return false;
  float y = hand[1];
  if (y < base[1]) y = base[1];
  if (y > base[1] + height) y = base[1] + height;
  const float axis[3] = {base[0], y, base[2]};
  float v[3] = {axis[0] - hand[0], axis[1] - hand[1], axis[2] - hand[2]};
  float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (length < kTiny) {
    // The Hand's point is on the axis: across the body, from the camera.
    v[0] = axis[0] - camera[0];
    v[1] = 0.0f;
    v[2] = axis[2] - camera[2];
    const float across = sqrtf(v[0] * v[0] + v[2] * v[2]);
    if (across < kTiny) {
      v[0] = 0.0f;
      v[2] = 1.0f;
    } else {
      v[0] /= across;
      v[2] /= across;
    }
    length = 0.0f;
  } else {
    for (int i = 0; i < 3; ++i) v[i] /= length;
  }
  float back = length;
  if (back < 2.0f * radius) back = 2.0f * radius;
  if (!(back > kTiny)) return false;
  for (int i = 0; i < 3; ++i) {
    direction[i] = v[i];
    origin[i] = axis[i] - v[i] * back;
  }
  return true;
}

// The part of the height of the body where the Hand's point is, 0 at the base
// and 1 at the top, for the log. The game makes the height of the attack from
// the place that its ray hits: below 0.33 low, above 0.66 high
// (0x005D40A7 to 0x005D40C6).
inline float BodyLevel(const float base[3], float height,
                       const float hand[3]) {
  if (!(height > 0.0f)) return 0.0f;
  return (hand[1] - base[1]) / height;
}

// THE MARKS OF A FIGHT (fight_mark_depth=1). LH3DCreature::DrawFightSparkles
// 0x0048DD70 draws each mark at the middle between the camera and its place
// (the factor 0.5 at 0x0048E118 to 0x0048E169), with a size of its own. On a
// flat screen the mark then shows on its place, in front of the body. In a
// headset it floats half way between the eye and the body.
//
// MarkPlace gets the camera, the middle that the game gives and the size that
// the game gives. It puts the mark back on its place, moved toward the camera
// by one size of the mark, so that the body does not cut the mark. It also
// makes the size larger, so that the mark has the size on the screen that
// the game gives. It never puts the mark nearer to the camera than the
// middle. False when the camera and the middle are at one point.
inline bool MarkPlace(const float camera[3], const float middle[3],
                      float size, float place[3], float* size_out) {
  float place_far[3], to_camera[3];
  for (int i = 0; i < 3; ++i) {
    place_far[i] = 2.0f * middle[i] - camera[i];
    to_camera[i] = camera[i] - place_far[i];
  }
  const float d = sqrtf(to_camera[0] * to_camera[0] +
                        to_camera[1] * to_camera[1] +
                        to_camera[2] * to_camera[2]);
  if (!(d > kTiny) || !(size >= 0.0f)) return false;
  // The game's mark at d / 2 has the size on the screen of a mark of two
  // sizes at d.
  const float size_at_place = 2.0f * size;
  float lift = size_at_place;
  if (lift > 0.5f * d) lift = 0.5f * d;
  for (int i = 0; i < 3; ++i) {
    place[i] = place_far[i] + to_camera[i] / d * lift;
  }
  *size_out = size_at_place * (d - lift) / d;
  return true;
}

}  // namespace fight_math
